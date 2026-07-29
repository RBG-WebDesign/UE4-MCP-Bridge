"""Tests for PIE readiness detection. Plain Python, no UE4 required.

Run: python Plugins/MCPBridge/Content/Python/tests/test_pie_ready.py

wait_for_pie_ready scanned the log for "PIE: play in editor start", which
UE4.27.2 never writes. The real line is "PIE: Play in editor total start time
0.366 seconds." So the check never matched: every gameplay_pie_start blocked the
game thread for the full 30 second timeout and then reported failure, while PIE
had actually started in under half a second.

Log scanning was the wrong mechanism regardless of the string. The function runs
on the game thread, so its own sleep blocks the thread that flushes the log.
Engine state is readable without a tick, so that is what it polls now.
"""

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PYTHON_ROOT = os.path.dirname(HERE)
if PYTHON_ROOT not in sys.path:
    sys.path.insert(0, PYTHON_ROOT)

from mcp_bridge.generation import pie_harness  # noqa: E402
from mcp_bridge.generation import telemetry_capture as tc  # noqa: E402

_passed = 0
_failed = 0


def check(name, condition, detail=""):
    global _passed, _failed
    if condition:
        print("  PASS  {}".format(name))
        _passed += 1
    else:
        print("  FAIL  {}{}".format(name, ": " + detail if detail else ""))
        _failed += 1


print("\nPIE readiness\n")


class _Swap(object):
    """Temporarily replace get_pie_world and read_new_log_lines."""

    def __init__(self, world_seq, log_seq):
        self.world_seq = list(world_seq)
        self.log_seq = list(log_seq)

    def __enter__(self):
        self._world = tc.get_pie_world
        self._lines = tc.read_new_log_lines
        tc.get_pie_world = lambda: self.world_seq.pop(0) if self.world_seq else None
        tc.read_new_log_lines = lambda: self.log_seq.pop(0) if self.log_seq else []
        return self

    def __exit__(self, *a):
        tc.get_pie_world = self._world
        tc.read_new_log_lines = self._lines


WORLD = object()

with _Swap([WORLD], []):
    check("a PIE world present immediately is ready", pie_harness.wait_for_pie_ready(1.0) is True)

with _Swap([None, None, WORLD], [[], []]):
    started = time.time()
    ok = pie_harness.wait_for_pie_ready(10.0)
    elapsed = time.time() - started
    check("a world appearing after a poll or two is ready", ok is True)
    check("it returns as soon as the world exists, not at the deadline", elapsed < 5.0,
          "took {:.1f}s".format(elapsed))

with _Swap([], [[]]):
    started = time.time()
    ok = pie_harness.wait_for_pie_ready(1.0)
    elapsed = time.time() - started
    check("no world within the timeout reports not ready", ok is False)
    check("it honours the timeout rather than hanging", elapsed < 4.0,
          "took {:.1f}s".format(elapsed))

# The real 4.27.2 line, which the old marker missed entirely.
REAL_LINE = "[2026.07.29-16.38.37:905][252]PIE: Play in editor total start time 0.366 seconds."
with _Swap([None, None], [[REAL_LINE]]):
    check("the log fallback matches the line this engine actually writes",
          pie_harness.wait_for_pie_ready(3.0) is True)

OLD_MARKER_LINE = "[2026.07.29-16.38.37:905][252]PIE: play in editor start"
check(
    "the old marker is no longer what detection depends on",
    "PIE: play in editor start" not in pie_harness._PIE_READY_MARKERS,
    "the string UE4.27.2 never emits must not be a marker",
)
check(
    "the real start line is among the markers",
    any("total start time" in m for m in pie_harness._PIE_READY_MARKERS),
)

with _Swap([None, None], [["some unrelated log line"], []]):
    check("an unrelated log line does not count as ready",
          pie_harness.wait_for_pie_ready(1.0) is False)

print("\n{} passed, {} failed\n".format(_passed, _failed))
sys.exit(1 if _failed else 0)
