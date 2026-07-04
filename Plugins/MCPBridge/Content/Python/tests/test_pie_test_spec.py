"""Unit tests for PIETestSpec.from_string -- no UE4 required.

Run from repo root:
    python Plugins/MCPBridge/Content/Python/tests/test_pie_test_spec.py
"""
import sys
import os

# Add the Python root to path so mcp_bridge is importable without UE4
_PYTHON_ROOT = os.path.join(os.path.dirname(__file__), "..")
sys.path.insert(0, _PYTHON_ROOT)

# spec_schema.py has no `import unreal` -- safe to import here
from mcp_bridge.generation.spec_schema import PIETestSpec


def _pass(name: str) -> None:
    print(f"  PASS  {name}")


def test_pawn_possessed() -> None:
    spec = PIETestSpec.from_string("pawn_possessed:BP_Character")
    assert spec.predicate == "pawn_possessed", f"bad predicate: {spec.predicate}"
    assert spec.target == "BP_Character", f"bad target: {spec.target}"
    assert spec.expected is None
    _pass("pawn_possessed")


def test_widget_visible() -> None:
    spec = PIETestSpec.from_string("widget_visible:WBP_HUD")
    assert spec.predicate == "widget_visible"
    assert spec.target == "WBP_HUD"
    assert spec.expected is None
    _pass("widget_visible")


def test_log_contains() -> None:
    spec = PIETestSpec.from_string("log_contains:GameStarted")
    assert spec.predicate == "log_contains"
    assert spec.target == "GameStarted"
    assert spec.expected is None
    _pass("log_contains")


def test_ai_state() -> None:
    spec = PIETestSpec.from_string("ai_state:BP_Enemy:Patrol")
    assert spec.predicate == "ai_state"
    assert spec.target == "BP_Enemy"
    assert spec.expected == "Patrol"
    _pass("ai_state")


def test_survive_with_seconds() -> None:
    spec = PIETestSpec.from_string("survive:10")
    assert spec.predicate == "survive"
    assert spec.timeout_seconds == 10.0
    assert spec.target is None
    _pass("survive:10")


def test_survive_default() -> None:
    spec = PIETestSpec.from_string("survive")
    assert spec.predicate == "survive"
    assert spec.timeout_seconds == 5.0
    assert spec.target is None
    _pass("survive (bare default)")


if __name__ == "__main__":
    print("Running PIETestSpec.from_string tests...")
    test_pawn_possessed()
    test_widget_visible()
    test_log_contains()
    test_ai_state()
    test_survive_with_seconds()
    test_survive_default()
    print("All PIETestSpec.from_string tests passed.")
