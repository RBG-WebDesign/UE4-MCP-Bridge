"""Tests for listener command cancellation (zombie-command fix).

Runs with plain Python, no UE4 required: exercises the game-thread queue
processor directly. A request whose HTTP thread timed out must be skipped
by the processor instead of executed.

Run: python Plugins/MCPBridge/Content/Python/tests/test_listener_cancellation.py
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PYTHON_ROOT = os.path.dirname(HERE)
if PYTHON_ROOT not in sys.path:
    sys.path.insert(0, PYTHON_ROOT)

from mcp_bridge import listener  # noqa: E402


def _reset_state() -> None:
    while not listener._command_queue.empty():
        listener._command_queue.get_nowait()
    listener._response_map.clear()
    listener._response_data.clear()
    with listener._cancelled_lock:
        listener._cancelled_requests.clear()


def test_cancelled_command_is_skipped() -> None:
    _reset_state()
    request_id = listener._get_next_request_id()
    listener._command_queue.put({
        "id": request_id,
        "command": "actor_delete",  # a mutation; must never run after cancel
        "params": {"name": "SomeActor"},
    })
    with listener._cancelled_lock:
        listener._cancelled_requests.add(request_id)

    listener._process_command_queue(0.0)

    assert request_id not in listener._response_data, (
        "cancelled command produced a response - it was executed"
    )
    with listener._cancelled_lock:
        assert request_id not in listener._cancelled_requests, (
            "cancelled id not drained from the cancellation set"
        )
    assert listener._command_queue.empty(), "queue should be drained"


def test_live_command_still_executes() -> None:
    _reset_state()
    request_id = listener._get_next_request_id()
    listener._command_queue.put({
        "id": request_id,
        "command": "ping",
        "params": {},
    })

    listener._process_command_queue(0.0)

    assert request_id in listener._response_data, "live command produced no response"
    result = listener._response_data[request_id]
    assert isinstance(result, dict) and "success" in result, (
        f"malformed response envelope: {result!r}"
    )


def test_cancelled_and_live_mixed() -> None:
    _reset_state()
    cancelled_id = listener._get_next_request_id()
    live_id = listener._get_next_request_id()
    listener._command_queue.put({"id": cancelled_id, "command": "level_save", "params": {}})
    listener._command_queue.put({"id": live_id, "command": "ping", "params": {}})
    with listener._cancelled_lock:
        listener._cancelled_requests.add(cancelled_id)

    listener._process_command_queue(0.0)

    assert cancelled_id not in listener._response_data, "cancelled command executed"
    assert live_id in listener._response_data, "live command skipped"


def main() -> int:
    tests = [
        test_cancelled_command_is_skipped,
        test_live_command_still_executes,
        test_cancelled_and_live_mixed,
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
