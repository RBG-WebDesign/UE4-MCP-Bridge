#!/usr/bin/env python3
"""Verify that an Unreal project is Unreal Engine 4.27.

Standalone (stdlib only) and importable. Scripts/ue427.py and the test suite
both use these functions. Checks, in order:

1. `EngineAssociation` in the `.uproject` file.
2. For a source build (GUID or blank association): `Engine/Build/Build.version`,
   located via --engine-root, UE_ENGINE_ROOT, or by walking up from the
   project directory.

Requires major version 4 and minor version 27. Anything else is a hard
failure with a clear error.

There is no environment-variable bypass. The check always runs, in production
and in tests alike: a user-settable variable is not an acceptable way to skip
a gate whose whole purpose is refusing to drive a non-4.27 editor. Tests that
need to avoid a real project on disk inject a fake `env` mapping or a stub
verifier instead.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from typing import Any, Dict, Mapping, Optional, Tuple

REQUIRED_MAJOR = 4
REQUIRED_MINOR = 27

ENGINE_ROOT_ENV = "UE_ENGINE_ROOT"

_VERSION_RE = re.compile(r"^(\d+)\.(\d+)")


def find_uproject(path: str) -> Optional[str]:
    """Resolve a path that is either a .uproject file or a directory holding one."""
    if os.path.isfile(path) and path.lower().endswith(".uproject"):
        return path
    if os.path.isdir(path):
        for name in sorted(os.listdir(path)):
            if name.lower().endswith(".uproject"):
                return os.path.join(path, name)
    return None


def read_engine_association(uproject_path: str) -> str:
    """Return the EngineAssociation string from a .uproject file."""
    with open(uproject_path, "r", encoding="utf-8-sig") as handle:
        data = json.load(handle)
    return data.get("EngineAssociation", "")


def parse_version_string(text: str) -> Optional[Tuple[int, int]]:
    """Parse '4.27' or '4.27.2' into (4, 27). GUIDs, blanks and paths give None."""
    match = _VERSION_RE.match(text.strip())
    if not match:
        return None
    return int(match.group(1)), int(match.group(2))


def read_build_version(engine_root: str) -> Optional[Tuple[int, int]]:
    """Read (major, minor) from <engine_root>/Engine/Build/Build.version."""
    candidates = [
        os.path.join(engine_root, "Engine", "Build", "Build.version"),
        os.path.join(engine_root, "Build", "Build.version"),
    ]
    for candidate in candidates:
        if os.path.isfile(candidate):
            with open(candidate, "r", encoding="utf-8-sig") as handle:
                data = json.load(handle)
            return int(data["MajorVersion"]), int(data["MinorVersion"])
    return None


def find_build_version_upward(start_dir: str) -> Optional[Tuple[int, int]]:
    """Walk up from start_dir looking for Engine/Build/Build.version."""
    directory = os.path.abspath(start_dir)
    while True:
        version = read_build_version(directory)
        if version is not None:
            return version
        parent = os.path.dirname(directory)
        if parent == directory:
            return None
        directory = parent


def verify_project(
    project_path: str,
    engine_root: Optional[str] = None,
    env: Optional[Mapping[str, str]] = None,
) -> Dict[str, Any]:
    """Return {ok, version, source, error} for a project path.

    `env` is injectable for tests and is consulted only for UE_ENGINE_ROOT,
    a source-build lookup path. There is no bypass: this always runs the
    real check.
    """
    env = os.environ if env is None else env

    uproject = find_uproject(project_path)
    if uproject is None:
        return {
            "ok": False,
            "version": None,
            "source": None,
            "error": "No .uproject found at %s" % project_path,
        }

    try:
        association = read_engine_association(uproject)
    except (OSError, ValueError, KeyError) as exc:
        return {
            "ok": False,
            "version": None,
            "source": None,
            "error": "Could not parse %s: %s" % (uproject, exc),
        }

    version = parse_version_string(association) if association else None
    source = "EngineAssociation"

    if version is None:
        # Source build: GUID or blank association. Locate Build.version.
        source = "Build.version"
        root = engine_root or env.get(ENGINE_ROOT_ENV)
        if root:
            version = read_build_version(root)
        if version is None:
            version = find_build_version_upward(os.path.dirname(uproject))
        if version is None:
            return {
                "ok": False,
                "version": None,
                "source": None,
                "error": (
                    "Unknown engine version: EngineAssociation is %r and no "
                    "Engine/Build/Build.version was found. Pass --engine-root "
                    "or set %s. This bridge supports Unreal Engine 4.27 only."
                    % (association, ENGINE_ROOT_ENV)
                ),
            }

    major, minor = version
    if (major, minor) != (REQUIRED_MAJOR, REQUIRED_MINOR):
        return {
            "ok": False,
            "version": "%d.%d" % (major, minor),
            "source": source,
            "error": (
                "Unsupported engine version %d.%d (from %s). This bridge "
                "supports Unreal Engine 4.27 only. Stop: do not run editor "
                "operations against this project." % (major, minor, source)
            ),
        }

    return {
        "ok": True,
        "version": "%d.%d" % (major, minor),
        "source": source,
        "error": None,
        "uproject": uproject,
    }


def main(argv: Optional[list] = None) -> int:
    """CLI entry point. Exit 0 when the project is 4.27, 2 otherwise."""
    parser = argparse.ArgumentParser(description="Verify a project is Unreal Engine 4.27.")
    parser.add_argument("project", help="Path to a .uproject file or its directory")
    parser.add_argument("--engine-root", help="Engine root for source builds")
    parser.add_argument("--json", action="store_true", help="Emit the result as JSON")
    args = parser.parse_args(argv)

    result = verify_project(args.project, engine_root=args.engine_root)
    if args.json:
        print(json.dumps(result, indent=2))
    elif result["ok"]:
        print("OK: Unreal Engine %s (%s)" % (result["version"], result["source"]))
    else:
        print("FAIL: %s" % result["error"], file=sys.stderr)
    return 0 if result["ok"] else 2


if __name__ == "__main__":
    sys.exit(main())
