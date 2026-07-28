"""Report which animation handler implementation a running listener is serving.

Reconciling files on disk does not change what a running UE4 listener answers
with. Python caches imported modules, so until the listener restarts it keeps
executing whatever was loaded at startup. That gap is invisible from the
filesystem and has already caused a divergent-clip write to go through a code
path that had no divergent gate in it.

This asks the listener directly and reports which implementation replied.

Usage:
    python Scripts/check_live_handler.py --sequence /Game/Path/To/AnimSequence
    python Scripts/check_live_handler.py --sequence /Game/A --reference /Game/Idle
"""

import argparse
import json
import sys
import urllib.error
import urllib.request
from typing import Any, Dict, Optional, Tuple

DEFAULT_URL = "http://localhost:8080/"

# Keys unique to this repo's nested response shape.
CANONICAL_ANALYZE_MARKERS = ("root", "pelvis", "compression")
CANONICAL_STEP_KEYS = ("mean", "max", "p95")


def post(url: str, command: str, params: Dict[str, Any], timeout: float) -> Dict[str, Any]:
    body = json.dumps({"command": command, "params": params}).encode("utf-8")
    request = urllib.request.Request(
        url, data=body, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def classify_analyze(data: Dict[str, Any]) -> Tuple[str, str]:
    """Return (verdict, detail) for an anim_root_motion_analyze payload."""
    if not isinstance(data, dict):
        return "unknown", "response data was not an object"

    root = data.get("root")
    if isinstance(root, dict):
        steps = root.get("step_cm_per_frame")
        if isinstance(steps, dict) and all(k in steps for k in CANONICAL_STEP_KEYS):
            missing = [k for k in CANONICAL_ANALYZE_MARKERS if k not in data]
            if missing:
                return "partial", f"nested shape but missing: {', '.join(missing)}"
            return "canonical", "nested root/pelvis/compression with mean, max, p95"
        return "partial", "has a root block but no step_cm_per_frame distribution"

    flat_markers = [k for k in ("step_mean_cm", "has_root_track", "compression_settings")
                    if k in data]
    if flat_markers:
        return "legacy", "flat shape, keys: " + ", ".join(flat_markers)

    return "unknown", "keys: " + ", ".join(sorted(data)[:12])


def classify_reanchor(data: Dict[str, Any]) -> Tuple[str, str]:
    """Return (verdict, detail) for an anim_reanchor dry-run payload."""
    if not isinstance(data, dict):
        return "unknown", "response data was not an object"

    if "verdict" in data and isinstance(data.get("verdict"), dict):
        code = data["verdict"].get("code", "?")
        return "canonical", f"reports a verdict ({code}); the divergent gate is live"
    if "bone_mask" in data or "bones_selected" in data:
        return "partial", "has mask fields but no verdict; the divergent gate is NOT live"
    return "legacy", "no verdict field; the divergent gate is NOT live"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check which animation handler a running listener serves.")
    parser.add_argument("--sequence", required=True,
                        help="Any AnimSequence content path to probe with")
    parser.add_argument("--reference",
                        help="Optional reference AnimSequence; enables the verdict check")
    parser.add_argument("--url", default=DEFAULT_URL)
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()

    try:
        analyze = post(args.url, "anim_root_motion_analyze",
                       {"sequence_path": args.sequence}, args.timeout)
    except urllib.error.URLError as exc:
        print(f"ERROR  Cannot reach the listener at {args.url}: {exc}")
        print("       Is UE4 open with the MCP bridge running?")
        return 2
    except Exception as exc:
        print(f"ERROR  Probe failed: {exc}")
        return 2

    if not analyze.get("success"):
        print(f"ERROR  anim_root_motion_analyze failed: {analyze.get('error')}")
        return 2

    verdict, detail = classify_analyze(analyze.get("data") or {})
    print(f"anim_root_motion_analyze : {verdict.upper()}  ({detail})")

    reanchor_verdict: Optional[str] = None
    if args.reference:
        try:
            reanchor = post(args.url, "anim_reanchor", {
                "target_path": args.sequence,
                "reference_path": args.reference,
                "dry_run": True,
            }, args.timeout)
        except Exception as exc:
            print(f"anim_reanchor            : probe failed ({exc})")
            reanchor = None
        if reanchor is not None:
            if reanchor.get("success"):
                reanchor_verdict, rdetail = classify_reanchor(reanchor.get("data") or {})
                print(f"anim_reanchor            : {reanchor_verdict.upper()}  ({rdetail})")
            else:
                print(f"anim_reanchor            : error ({reanchor.get('error')})")

    print()
    verdicts = [v for v in (verdict, reanchor_verdict) if v]
    if all(v == "canonical" for v in verdicts):
        print("The listener is serving this repo's handlers. Safety gates are live.")
        return 0

    print("The listener is NOT serving this repo's handlers.")
    print()
    print("Files on disk are not what is executing. Python caches imported modules,")
    print("so the listener keeps running whatever it loaded at startup. Until it is")
    print("restarted, the divergent-clip refusal and the pre-write asset backup are")
    print("not in the path, and a write can go through with neither.")
    print()
    print("Fix: restart the UE4 listener, then re-run this check.")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
