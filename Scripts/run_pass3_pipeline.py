"""Drive the whole Pass 3 bring-up from one command.

Two phases, because a C++ rebuild needs the editor closed and everything after
it needs the editor open. That gap cannot be crossed inside a single process.

    # editor CLOSED
    python Scripts/run_pass3_pipeline.py --phase build --project "D:/path/to/Project"

    # editor OPEN again
    python Scripts/run_pass3_pipeline.py --phase verify --project "D:/path/to/Project" \
        --reference /Game/Path/Idle --folder /Game/Path/Clips

Phase build : reconcile the project files, then compile the editor target.
Phase verify: hot-reload the listener, confirm the canonical handlers are live,
              then run a batch dry-run sweep.

Every stage aborts the run on failure with the reason. The full transcript is
written to a report file so it can be pasted somewhere useful.
"""

import argparse
import json
import os
import platform
import subprocess
import sys
import time
import urllib.error
import urllib.request
from typing import Any, Dict, List, Optional

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
DEFAULT_URL = "http://localhost:8080/"


class StageError(Exception):
    """A stage failed. The message is the reason the run stopped."""


def banner(text: str) -> None:
    print()
    print("=" * 68)
    print(text)
    print("=" * 68)


def post(url: str, command: str, params: Dict[str, Any], timeout: float) -> Dict[str, Any]:
    body = json.dumps({"command": command, "params": params}).encode("utf-8")
    headers = {"Content-Type": "application/json"}
    # Only sent when configured, so a listener without MCP_BRIDGE_TOKEN set
    # sees exactly the requests it saw before.
    token = (os.environ.get("UNREAL_BRIDGE_TOKEN") or "").strip()
    if token:
        headers["X-MCP-Bridge-Token"] = token
    request = urllib.request.Request(url, data=body, headers=headers)
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def listener_alive(url: str, timeout: float = 5.0) -> bool:
    try:
        result = post(url, "ping", {}, timeout)
        return bool(result.get("success"))
    except Exception:
        return False


# --- build phase ------------------------------------------------------------


def stage_reconcile(project: str, report: Dict[str, Any]) -> None:
    banner("STAGE 1  Reconcile project files to this repo")
    script = os.path.join(HERE, "reconcile_animation_schema.py")
    run = subprocess.run(
        [sys.executable, script, "--project", project, "--apply"],
        capture_output=True, text=True)
    print(run.stdout.strip())
    if run.stderr.strip():
        print(run.stderr.strip(), file=sys.stderr)
    report["reconcile"] = {
        "returncode": run.returncode,
        "stdout": run.stdout,
        "stderr": run.stderr,
    }
    if run.returncode != 0:
        raise StageError(
            "Reconcile failed. Nothing downstream will be meaningful until it "
            "passes, because the project would still be running different code.")


def find_uproject(project: str) -> str:
    candidates = [f for f in os.listdir(project) if f.endswith(".uproject")]
    if len(candidates) != 1:
        raise StageError(
            f"Expected exactly one .uproject in {project}, found {len(candidates)}: "
            + ", ".join(candidates))
    return os.path.join(project, candidates[0])


def stage_build(project: str, engine: Optional[str], report: Dict[str, Any]) -> None:
    banner("STAGE 2  Compile the editor target")
    if platform.system() != "Windows":
        raise StageError(
            f"The build stage needs Windows; this is {platform.system()}. Run this "
            "phase on the machine that has UE4.27 installed.")

    uproject = find_uproject(project)
    target = os.path.splitext(os.path.basename(uproject))[0] + "Editor"

    engine_root = engine or os.environ.get("UE4_ROOT") or r"C:\Program Files\Epic Games\UE_4.27"
    build_bat = os.path.join(engine_root, "Engine", "Build", "BatchFiles", "Build.bat")
    if not os.path.isfile(build_bat):
        raise StageError(
            f"Build.bat not found at {build_bat}. Pass --engine with the UE4.27 root, "
            "or set UE4_ROOT.")

    command = [build_bat, target, "Win64", "Development",
               f"-Project={uproject}", "-WaitMutex", "-NoHotReload"]
    print("running:", " ".join(command))
    run = subprocess.run(command, capture_output=True, text=True)

    output = run.stdout + "\n" + run.stderr
    report["build"] = {"returncode": run.returncode, "output": output, "target": target}

    # Surface the lines that matter rather than thousands of lines of UBT noise.
    interesting = [
        line for line in output.splitlines()
        if "error" in line.lower() or "warning C" in line or "Error C" in line
    ]
    if interesting:
        print("\n--- compiler diagnostics ---")
        for line in interesting[:60]:
            print(line)
        if len(interesting) > 60:
            print(f"... {len(interesting) - 60} more, see the report file")

    if run.returncode != 0:
        raise StageError(
            "Build failed. The diagnostics above and the full log in the report file "
            "are what is needed to fix AnimPoseLibrary.cpp; none of its API usage "
            "could be verified against a signature database when it was written.")
    print("\nBuild succeeded.")


# --- verify phase -----------------------------------------------------------


def stage_reload(url: str, report: Dict[str, Any]) -> None:
    banner("STAGE 3  Hot-reload the listener")
    if not listener_alive(url):
        raise StageError(
            f"No listener at {url}. Open the project in UE4 and make sure the MCP "
            "bridge started, then re-run this phase.")

    result = post(url, "restart_listener", {"reload": True}, 30.0)
    report["reload"] = result
    if not result.get("success"):
        raise StageError(f"restart_listener failed: {result.get('error')}")
    print("restart scheduled; waiting for the listener to come back")

    deadline = time.time() + 60.0
    while time.time() < deadline:
        time.sleep(2.0)
        if listener_alive(url):
            print("listener is back")
            return
    raise StageError(
        "The listener did not come back within 60s. Check the UE4 Output Log; a "
        "module that failed to reload is logged there by name.")


def stage_check(url: str, sequence: str, reference: str, report: Dict[str, Any]) -> None:
    banner("STAGE 4  Confirm the canonical handlers are live")
    script = os.path.join(HERE, "check_live_handler.py")
    run = subprocess.run(
        [sys.executable, script, "--sequence", sequence,
         "--reference", reference, "--url", url],
        capture_output=True, text=True)
    print(run.stdout.strip())
    if run.stderr.strip():
        print(run.stderr.strip(), file=sys.stderr)
    report["check"] = {"returncode": run.returncode, "stdout": run.stdout}
    if run.returncode != 0:
        raise StageError(
            "The listener is not serving this repo's handlers, so the divergent-clip "
            "refusal and the pre-write backup are not in the path. Refusing to sweep "
            "against code that is not the code under test.")


def stage_sweep(url: str, reference: str, folder: str, report: Dict[str, Any]) -> None:
    banner("STAGE 5  Batch dry-run sweep")
    result = post(url, "anim_batch_reanchor", {
        "reference_path": reference,
        "folder_path": folder,
        "dry_run": True,
    }, 300.0)
    report["sweep"] = result

    if not result.get("success"):
        raise StageError(f"Sweep failed: {result.get('error')}")

    data = result.get("data") or {}
    counts = data.get("verdict_counts") or {}
    print(f"analyzed        : {data.get('analyzed')}")
    print(f"verdict counts  : {json.dumps(counts)}")
    print(f"review ceiling  : {data.get('review_ceiling_degrees')} deg")
    skipped = data.get("skipped") or []
    if skipped:
        print(f"skipped         : {len(skipped)}")

    rows = data.get("sequences") or []
    if rows:
        print("\nranked by drift:")
        print(f"  {'verdict':<11} {'max deg':>9}  sequence")
        for row in rows:
            name = str(row.get("sequence", "")).rsplit("/", 1)[-1]
            print(f"  {row.get('verdict', '?'):<11} "
                  f"{row.get('max_delta_degrees', 0):>9.2f}  {name}")

    if counts.get("drifted", 0) == 0 and counts.get("divergent", 0) > 0:
        print("\nEverything above threshold is divergent rather than drifted. For a")
        print("locomotion set compared against an idle that is the expected shape, not")
        print("a fault: those clips start in genuinely different poses. Raising the")
        print("ceiling to force them through would defeat the gate.")


def main() -> int:
    parser = argparse.ArgumentParser(description="Drive Pass 3 bring-up end to end.")
    parser.add_argument("--phase", required=True, choices=["build", "verify"])
    parser.add_argument("--project", required=True, help="Game project root")
    parser.add_argument("--reference", help="Reference AnimSequence (verify phase)")
    parser.add_argument("--folder", help="Folder to sweep (verify phase)")
    parser.add_argument("--probe-sequence",
                        help="Sequence used for the liveness probe. Defaults to the "
                             "first clip the sweep would touch, or the reference.")
    parser.add_argument("--engine", help="UE4.27 root, e.g. C:/Program Files/Epic Games/UE_4.27")
    parser.add_argument("--skip-reconcile", action="store_true")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--url", default=DEFAULT_URL)
    parser.add_argument("--report", default="pass3_report.json")
    args = parser.parse_args()

    report: Dict[str, Any] = {"phase": args.phase, "project": args.project}
    stages: List[str] = []
    exit_code = 0

    try:
        if args.phase == "build":
            if not args.skip_reconcile:
                stage_reconcile(args.project, report)
                stages.append("reconcile")
            if not args.skip_build:
                stage_build(args.project, args.engine, report)
                stages.append("build")
            banner("BUILD PHASE COMPLETE")
            print("Now reopen the project in UE4, then run:")
            print(f"  python {os.path.relpath(__file__, REPO_ROOT)} --phase verify \\")
            print(f"      --project \"{args.project}\" \\")
            print("      --reference /Game/Path/Idle --folder /Game/Path/Clips")
        else:
            if not args.reference or not args.folder:
                raise StageError("--reference and --folder are required for the verify phase")
            probe = args.probe_sequence or args.reference
            stage_reload(args.url, report)
            stages.append("reload")
            stage_check(args.url, probe, args.reference, report)
            stages.append("check")
            stage_sweep(args.url, args.reference, args.folder, report)
            stages.append("sweep")
            banner("VERIFY PHASE COMPLETE")
            print("No asset was written; every stage was read-only or a dry run.")
    except StageError as exc:
        print()
        print(f"STOPPED  {exc}")
        exit_code = 1
    except Exception as exc:
        print()
        print(f"STOPPED  Unexpected failure: {exc}")
        exit_code = 2

    report["stages_completed"] = stages
    report["exit_code"] = exit_code
    try:
        with open(args.report, "w", encoding="utf-8") as handle:
            json.dump(report, handle, indent=2)
        print(f"\nreport written to {args.report}")
    except OSError as exc:
        print(f"\ncould not write the report: {exc}")

    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
