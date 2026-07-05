"""Tests for the subprocess job system. Plain Python, no UE4 required.

Run: python Plugins/MCPBridge/Content/Python/tests/test_jobs.py
"""

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PYTHON_ROOT = os.path.dirname(HERE)
if PYTHON_ROOT not in sys.path:
    sys.path.insert(0, PYTHON_ROOT)

from mcp_bridge import jobs  # noqa: E402
from mcp_bridge.handlers.cpp_tools import parse_build_output  # noqa: E402

PY = sys.executable


def _wait_for(job_id: str, timeout: float = 20.0) -> dict:
    deadline = time.time() + timeout
    while time.time() < deadline:
        view = jobs.get_job(job_id)["data"]
        if view["status"] != "running":
            return view
        time.sleep(0.1)
    raise AssertionError(f"job {job_id} still running after {timeout}s")


def test_job_success_captures_output() -> None:
    r = jobs.start_subprocess_job("test", "echo", [PY, "-c", "print('hello-job')"])
    assert r["success"], r
    view = _wait_for(r["data"]["job_id"])
    assert view["status"] == "succeeded", view
    assert any("hello-job" in line for line in view["output_tail"]), view["output_tail"]


def test_job_failure_reports_returncode() -> None:
    r = jobs.start_subprocess_job("test", "fail", [PY, "-c", "import sys; sys.exit(3)"])
    view = _wait_for(r["data"]["job_id"])
    assert view["status"] == "failed", view
    assert view["returncode"] == 3, view


def test_job_cancel_terminates() -> None:
    r = jobs.start_subprocess_job(
        "test", "sleeper",
        [PY, "-c", "import time\nfor i in range(100):\n    print(i, flush=True)\n    time.sleep(0.2)"],
    )
    job_id = r["data"]["job_id"]
    time.sleep(0.6)
    c = jobs.cancel_job(job_id)
    assert c["success"], c
    view = _wait_for(job_id)
    assert view["status"] == "cancelled", view


def test_exclusive_kind_rejects_duplicate() -> None:
    r1 = jobs.start_subprocess_job(
        "cpp_build", "long build",
        [PY, "-c", "import time; time.sleep(5)"],
    )
    assert r1["success"], r1
    r2 = jobs.start_subprocess_job("cpp_build", "second build", [PY, "-c", "print('no')"])
    assert not r2["success"], "second exclusive job must be rejected"
    assert "already running" in (r2["error"] or ""), r2
    jobs.cancel_job(r1["data"]["job_id"])
    _wait_for(r1["data"]["job_id"])


def test_build_output_parser() -> None:
    lines = [
        r"D:\Proj\Source\Game\Private\Foo.cpp(42): error C2065: 'Bar': undeclared identifier",
        r"D:\Proj\Source\Game\Private\Foo.cpp(50): warning C4100: 'Param': unreferenced formal parameter",
        r"UE4Editor-Game.dll : fatal error LNK1104: cannot open file 'UE4Editor-Game.dll'",
        "Some unrelated line",
    ]
    result = parse_build_output(lines, 6)
    assert result["build_succeeded"] is False
    assert result["error_count"] == 2, result
    assert result["warning_count"] == 1, result
    assert result["errors"][0]["code"] == "C2065"
    assert result["errors"][0]["line"] == 42
    assert "note" in result, "LNK1104 lock note expected"


def main() -> int:
    tests = [
        test_job_success_captures_output,
        test_job_failure_reports_returncode,
        test_job_cancel_terminates,
        test_exclusive_kind_rejects_duplicate,
        test_build_output_parser,
    ]
    failed = 0
    for fn in tests:
        try:
            fn()
            print(f"  PASS  {fn.__name__}")
        except AssertionError as exc:
            failed += 1
            print(f"  FAIL  {fn.__name__}: {exc}")
    print(f"\n{len(tests) - failed} passed, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
