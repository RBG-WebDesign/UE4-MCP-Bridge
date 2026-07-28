"""Reconcile a game project's animation handlers to this repo's canonical schema.

Surgical on purpose. install-mcp-bridge.ps1 copies Plugins/MCPBridge wholesale,
which would overwrite router.py and drop any routes the project added that this
repo does not have. This script touches only the animation workstream:

    mcp_bridge/utils/anim_math.py       copied
    mcp_bridge/handlers/animation.py    copied
    tests/test_anim_math.py             copied
    mcp_bridge/router.py                patched in place, animation entries only

Dry run by default. Pass --apply to write.

Usage:
    python Scripts/reconcile_animation_schema.py --project "D:/path/to/YourProject"
    python Scripts/reconcile_animation_schema.py --project "D:/path/to/YourProject" --apply

The preflight refuses to run when the project's router imports an animation
handler this repo does not define. Copying over animation.py in that state
would break the import and take the whole command router down with it, not just
the animation commands.
"""

import argparse
import ast
import hashlib
import os
import re
import shutil
import subprocess
import sys
from typing import Dict, List, Optional, Set, Tuple

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLUGIN_PYTHON = os.path.join("Plugins", "MCPBridge", "Content", "Python")

# Files copied verbatim. Relative to the plugin Python root on both sides.
MANAGED_FILES = (
    os.path.join("mcp_bridge", "utils", "anim_math.py"),
    os.path.join("mcp_bridge", "handlers", "animation.py"),
    os.path.join("tests", "test_anim_math.py"),
    os.path.join("tests", "test_animation_handlers.py"),
)

# Copied test files that the verify step runs inside the target project.
VERIFY_TESTS = (
    os.path.join("tests", "test_anim_math.py"),
    os.path.join("tests", "test_animation_handlers.py"),
)

ROUTER_RELATIVE = os.path.join("mcp_bridge", "router.py")

# Commands this repo owns. The router patch is limited to exactly these.
ANIMATION_COMMANDS = (
    "anim_pose_snapshot",
    "anim_pose_delta",
    "anim_root_motion_analyze",
    "anim_reanchor",
    "anim_batch_reanchor",
)

IMPORT_BLOCK_PATTERN = re.compile(
    r"from mcp_bridge\.handlers\.animation import \([^)]*\)\n", re.MULTILINE)


class ReconcileError(Exception):
    """Fatal problem that must stop the run before anything is written."""


def _sha(path: str) -> str:
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()[:12]


def _read(path: str) -> str:
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read()


def _handler_names(source: str) -> Set[str]:
    """Top-level function names defined in a module source."""
    tree = ast.parse(source)
    return {
        node.name for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
    }


def _imported_animation_names(router_source: str) -> Set[str]:
    """Names the router imports from handlers.animation."""
    tree = ast.parse(router_source)
    names: Set[str] = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.ImportFrom) and node.module == "mcp_bridge.handlers.animation":
            for alias in node.names:
                names.add(alias.name)
    return names


def _routed_animation_commands(router_source: str) -> Dict[str, str]:
    """Map of command -> handler name for routes pointing at animation handlers."""
    animation_names = _imported_animation_names(router_source)
    routes: Dict[str, str] = {}
    tree = ast.parse(router_source)
    for node in ast.walk(tree):
        if not isinstance(node, ast.Dict):
            continue
        for key, value in zip(node.keys, node.values):
            if not isinstance(key, ast.Constant) or not isinstance(key.value, str):
                continue
            if isinstance(value, ast.Name) and value.id in animation_names:
                routes[key.value] = value.id
    return routes


def preflight(repo_python: str, project_python: str) -> List[str]:
    """Check the project can accept this repo's animation.py. Raises on danger."""
    notes: List[str] = []

    project_router_path = os.path.join(project_python, ROUTER_RELATIVE)
    if not os.path.isfile(project_router_path):
        raise ReconcileError(f"Project router not found: {project_router_path}")

    canonical_handlers = _handler_names(
        _read(os.path.join(repo_python, "mcp_bridge", "handlers", "animation.py")))

    router_source = _read(project_router_path)
    imported = _imported_animation_names(router_source)

    orphans = sorted(name for name in imported if name not in canonical_handlers)
    if orphans:
        routed = _routed_animation_commands(router_source)
        commands = sorted(cmd for cmd, fn in routed.items() if fn in orphans)
        raise ReconcileError(
            "Refusing to reconcile: the project router imports animation handlers that "
            "this repo does not define.\n"
            f"  Missing after copy: {', '.join(orphans)}\n"
            f"  Commands that would break: {', '.join(commands) or '(none routed)'}\n"
            "\n"
            "Copying animation.py now would make the import fail and take the entire "
            "command router down, not just these commands. Resolve first by either:\n"
            "  (a) removing those imports and routes from the project router, if the "
            "commands are not needed yet, or\n"
            "  (b) porting the handlers into this repo so they are part of the "
            "canonical file."
        )

    project_animation_path = os.path.join(project_python, "mcp_bridge", "handlers", "animation.py")
    if os.path.isfile(project_animation_path):
        project_handlers = _handler_names(_read(project_animation_path))
        lost = sorted(project_handlers - canonical_handlers)
        lost = [name for name in lost if not name.startswith("_")]
        if lost:
            notes.append(
                "Project-only public functions in animation.py that will be replaced: "
                + ", ".join(lost))

    return notes


def plan_file_copies(repo_python: str, project_python: str) -> List[Tuple[str, str, str]]:
    """Return (relative_path, status, detail) for each managed file."""
    plan: List[Tuple[str, str, str]] = []
    for relative in MANAGED_FILES:
        source = os.path.join(repo_python, relative)
        target = os.path.join(project_python, relative)
        if not os.path.isfile(source):
            raise ReconcileError(f"Canonical file missing from this repo: {source}")
        if not os.path.exists(target):
            plan.append((relative, "create", "not present in project"))
        elif _sha(source) == _sha(target):
            plan.append((relative, "unchanged", "already identical"))
        else:
            plan.append((relative, "overwrite", f"{_sha(target)} -> {_sha(source)}"))
    return plan


def build_router_patch(repo_python: str, project_python: str) -> Tuple[str, List[str]]:
    """Return (new_router_source, change_descriptions). Idempotent."""
    repo_router = _read(os.path.join(repo_python, ROUTER_RELATIVE))
    project_router_path = os.path.join(project_python, ROUTER_RELATIVE)
    source = _read(project_router_path)
    changes: List[str] = []

    canonical_import = IMPORT_BLOCK_PATTERN.search(repo_router)
    if not canonical_import:
        raise ReconcileError(
            "This repo's router.py has no 'from mcp_bridge.handlers.animation import (...)' "
            "block; cannot derive the canonical import.")
    canonical_import_text = canonical_import.group(0)

    existing_import = IMPORT_BLOCK_PATTERN.search(source)
    if existing_import:
        if existing_import.group(0) != canonical_import_text:
            source = source[:existing_import.start()] + canonical_import_text + source[existing_import.end():]
            changes.append("replaced the handlers.animation import block")
    else:
        anchor = source.rfind("\nfrom mcp_bridge.handlers.")
        if anchor < 0:
            raise ReconcileError("Could not find a handler import block to anchor against")
        insert_at = source.find(")\n", anchor)
        if insert_at < 0:
            raise ReconcileError("Malformed handler import block in the project router")
        insert_at += 2
        source = source[:insert_at] + canonical_import_text + source[insert_at:]
        changes.append("inserted the handlers.animation import block")

    routed = _routed_animation_commands(source)
    missing = [cmd for cmd in ANIMATION_COMMANDS if cmd not in routed]
    if missing:
        lines = "".join(
            f'    "{cmd}": handle_{cmd},\n' for cmd in missing)
        marker = "    # Animation pose analysis (read-only)\n"
        if marker in source:
            source = source.replace(marker, marker + lines, 1)
        else:
            # Insert before the dispatch table's closing brace at column 0.
            table_start = source.find("COMMAND_ROUTES")
            brace = source.find("{", table_start)
            end = source.find("\n}", brace)
            if end < 0:
                raise ReconcileError("Could not locate the end of COMMAND_ROUTES")
            block = "\n\n    # Animation pose analysis\n" + lines.rstrip("\n")
            source = source[:end] + block + source[end:]
        changes.append("added routes: " + ", ".join(missing))

    return source, changes


def verify(project_python: str) -> List[str]:
    """Compile the touched files, run the math tests, confirm routes resolve."""
    results: List[str] = []

    targets = [os.path.join(project_python, rel) for rel in MANAGED_FILES]
    targets.append(os.path.join(project_python, ROUTER_RELATIVE))
    compile_run = subprocess.run(
        [sys.executable, "-m", "py_compile", *targets],
        capture_output=True, text=True)
    if compile_run.returncode != 0:
        raise ReconcileError(f"py_compile failed after writing:\n{compile_run.stderr}")
    results.append("py_compile passed on all touched files")

    for relative in VERIFY_TESTS:
        test_path = os.path.join(project_python, relative)
        test_run = subprocess.run(
            [sys.executable, test_path], capture_output=True, text=True)
        if test_run.returncode != 0:
            raise ReconcileError(
                f"{os.path.basename(relative)} failed after writing:\n{test_run.stdout}")
        summary = test_run.stdout.strip().splitlines()[-1]
        results.append(f"{os.path.basename(relative)}: {summary}")

    router_source = _read(os.path.join(project_python, ROUTER_RELATIVE))
    routed = _routed_animation_commands(router_source)
    absent = [cmd for cmd in ANIMATION_COMMANDS if cmd not in routed]
    if absent:
        raise ReconcileError(f"Routes still missing after patch: {', '.join(absent)}")
    results.append(f"all {len(ANIMATION_COMMANDS)} animation routes resolve")

    return results


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Reconcile a project's animation handlers to this repo's schema.")
    parser.add_argument("--project", required=True,
                        help="Game project root containing Plugins/MCPBridge")
    parser.add_argument("--apply", action="store_true",
                        help="Write changes. Without this the run is a dry run.")
    parser.add_argument("--no-backup", action="store_true",
                        help="Skip .bak copies of overwritten files.")
    args = parser.parse_args()

    repo_python = os.path.join(REPO_ROOT, PLUGIN_PYTHON)
    project_python = os.path.join(args.project, PLUGIN_PYTHON)

    if not os.path.isdir(project_python):
        print(f"ERROR  Not a bridge-installed project: {project_python} does not exist")
        return 2

    print(f"repo     {repo_python}")
    print(f"project  {project_python}")
    print(f"mode     {'APPLY' if args.apply else 'dry run'}\n")

    try:
        notes = preflight(repo_python, project_python)
        for note in notes:
            print(f"NOTE   {note}")
        if notes:
            print()

        plan = plan_file_copies(repo_python, project_python)
        for relative, status, detail in plan:
            print(f"{status.upper():<10} {relative}  ({detail})")

        _, router_changes = build_router_patch(repo_python, project_python)
        if router_changes:
            for change in router_changes:
                print(f"PATCH      {ROUTER_RELATIVE}  ({change})")
        else:
            print(f"UNCHANGED  {ROUTER_RELATIVE}  (already canonical)")

        if not args.apply:
            print("\nDry run. Re-run with --apply to write these changes.")
            return 0

        print()
        for relative, status, _ in plan:
            if status == "unchanged":
                continue
            source = os.path.join(repo_python, relative)
            target = os.path.join(project_python, relative)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            if os.path.exists(target) and not args.no_backup:
                shutil.copy2(target, target + ".bak")
            shutil.copy2(source, target)
            print(f"wrote    {relative}")

        if router_changes:
            router_target = os.path.join(project_python, ROUTER_RELATIVE)
            patched, _ = build_router_patch(repo_python, project_python)
            if not args.no_backup:
                shutil.copy2(router_target, router_target + ".bak")
            with open(router_target, "w", encoding="utf-8") as handle:
                handle.write(patched)
            print(f"wrote    {ROUTER_RELATIVE}")

        print()
        for line in verify(project_python):
            print(f"OK       {line}")
        print("\nReconciled. Restart the UE4 listener to pick up the new handlers.")
        return 0

    except ReconcileError as exc:
        print(f"\nERROR  {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
