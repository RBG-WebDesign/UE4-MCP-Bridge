# Gameplay Generator Phase 1: PIE Harness + Telemetry

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the ability to launch PIE, capture runtime state via log polling and direct API queries, and evaluate structured acceptance test predicates against a live PIE session.

**Architecture:** New `pie_harness.py` and `telemetry_capture.py` in `generation/` own the PIE lifecycle and runtime observation. New `handlers/gameplay.py` exposes them as MCP commands. New `mcp-server/src/tools/gameplay.ts` defines the TypeScript tool definitions. The `ClassResolutionCache` in `telemetry_capture.py` is built during this phase and reused by Phase 4 (repair engine) and later phases. All communication follows the existing `POST /` -> `router.py` pattern -- no new ports.

**Tech Stack:** UE4.27 Python (experimental, game-thread only), log-file cursor for log scraping, `unreal.EditorLevelLibrary.get_pie_worlds()` for PIE world access, `unreal.WidgetLibrary.get_all_widgets_of_class()` for widget visibility, TypeScript + Zod for MCP tool schemas, `MockUnrealServer.setHandler()` for unit tests.

**Assistant workflow note:** This plan defines PIE tools, but normal UE Bridge
authoring work should not start PIE automatically after edits. Use the tools for
explicit runtime testing requests, PIE debugging tasks, and planned validation
runs only.

---

## File Map

### New Python files
- `unreal-plugin/Content/Python/mcp_bridge/generation/telemetry_capture.py` -- log-cursor utility, `ClassResolutionCache`, `TelemetryFrame`, `snapshot()`, PIE world helpers
- `unreal-plugin/Content/Python/mcp_bridge/generation/pie_harness.py` -- `launch_pie()`, `stop_pie()`, `wait_for_pie_ready()`, `PIETestSpec`, `AssertionResult`, `run_assertions()`
- `unreal-plugin/Content/Python/mcp_bridge/handlers/gameplay.py` -- MCP command handlers

### Modified Python files
- `unreal-plugin/Content/Python/mcp_bridge/router.py` -- add 4 `gameplay_*` command routes
- `unreal-plugin/Content/Python/mcp_bridge/generation/spec_schema.py` -- add `PIETestSpec`, `AssertionResult`, `TelemetryFrame`, `ClassResolutionCache` dataclasses

### New Python test files
- `unreal-plugin/Content/Python/tests/__init__.py` -- package marker so pytest can find tests
- `unreal-plugin/Content/Python/tests/test_pie_test_spec.py` -- pure Python unit tests for `PIETestSpec.from_string()`

### New TypeScript files
- `mcp-server/src/tools/gameplay.ts` -- `createGameplayTools(client)` with 4 tool definitions

### Modified TypeScript files
- `mcp-server/src/index.ts` -- import and register `createGameplayTools`
- `mcp-server/package.json` -- add `gameplay-tools.test.ts` to test chain

### Test files
- `mcp-server/tests/gameplay-tools.test.ts` -- unit tests using `MockUnrealServer.setHandler()`

---

## Task 1: Schema dataclasses

**Files:**
- Modify: `unreal-plugin/Content/Python/mcp_bridge/generation/spec_schema.py`

- [ ] **Step 1: Add dataclasses to spec_schema.py**

Open [spec_schema.py](unreal-plugin/Content/Python/mcp_bridge/generation/spec_schema.py) and append after the existing `BuildSpec` class and `spec_to_dict` function:

```python
# ---------------------------------------------------------------------------
# PIE Harness / Telemetry
# ---------------------------------------------------------------------------

@dataclass
class PIETestSpec:
    """A single acceptance test predicate for a PIE session."""
    predicate: str               # "pawn_possessed" | "widget_visible" | "log_contains"
                                 # | "ai_state" | "survive"
    target: Optional[str] = None     # class name, widget name, log string, or actor name
    expected: Optional[str] = None   # expected state value (for ai_state predicate)
    timeout_seconds: float = 5.0

    @staticmethod
    def from_string(s: str) -> "PIETestSpec":
        """Parse shorthand string into PIETestSpec.

        Examples:
            "pawn_possessed:BP_Character"  -> PIETestSpec("pawn_possessed", "BP_Character")
            "widget_visible:WBP_HUD"       -> PIETestSpec("widget_visible", "WBP_HUD")
            "log_contains:GameStarted"     -> PIETestSpec("log_contains", "GameStarted")
            "ai_state:BP_Enemy:Patrol"     -> PIETestSpec("ai_state", "BP_Enemy", "Patrol")
            "survive:5"                    -> PIETestSpec("survive", timeout_seconds=5.0)
        """
        parts = s.split(":", 2)
        predicate = parts[0]
        if predicate == "survive":
            timeout = float(parts[1]) if len(parts) > 1 else 5.0
            return PIETestSpec(predicate=predicate, timeout_seconds=timeout)
        target = parts[1] if len(parts) > 1 else None
        expected = parts[2] if len(parts) > 2 else None
        return PIETestSpec(predicate=predicate, target=target, expected=expected)


@dataclass
class AssertionResult:
    """Result of evaluating one PIETestSpec."""
    predicate: str
    target: Optional[str]
    passed: bool
    observed: str                # human-readable observed value


@dataclass
class TelemetryFrame:
    """Runtime state captured from one PIE snapshot."""
    log_lines_since_last: List[str]       # new log lines since last snapshot
    possessed_pawn_class: Optional[str]   # class name of possessed pawn, or None
    ai_controller_states: Dict[str, str]  # actor name -> state string
    visible_widgets: List[str]            # widget class names currently visible
    fps: float


@dataclass
class ClassResolutionCache:
    """Maps asset short names to resolved content paths.

    Built during generation and reused by pie_harness, repair_engine,
    and reference_validator. Avoids repeated load_object calls during PIE.
    """
    class_paths: Dict[str, str] = field(default_factory=dict)
    # e.g. {"BP_Character": "/Game/Generated/Gameplay/BP_Character"}
```

The file already imports `dataclass`, `field`, `List`, `Optional`, `Dict`, `Any` from the standard library -- no new imports needed.

- [ ] **Step 2: Commit**

```bash
cd d:/UE/UE_Bridge
git add unreal-plugin/Content/Python/mcp_bridge/generation/spec_schema.py
git commit -m "feat(gameplay): add PIETestSpec, AssertionResult, TelemetryFrame, ClassResolutionCache dataclasses"
```

---

## Task 2: Python unit test for PIETestSpec.from_string (no UE4)

**Files:**
- Create: `unreal-plugin/Content/Python/tests/__init__.py`
- Create: `unreal-plugin/Content/Python/tests/test_pie_test_spec.py`

`PIETestSpec` is in `spec_schema.py` which has no `import unreal` -- it is pure Python. The test imports it directly via `sys.path` manipulation to avoid needing UE4.

- [ ] **Step 1: Create tests/ package marker**

```bash
mkdir -p d:/UE/UE_Bridge/unreal-plugin/Content/Python/tests
touch d:/UE/UE_Bridge/unreal-plugin/Content/Python/tests/__init__.py
```

- [ ] **Step 2: Create test_pie_test_spec.py**

```python
"""Unit tests for PIETestSpec.from_string -- no UE4 required.

Run from repo root:
    python unreal-plugin/Content/Python/tests/test_pie_test_spec.py
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
    _pass("widget_visible")


def test_log_contains() -> None:
    spec = PIETestSpec.from_string("log_contains:GameStarted")
    assert spec.predicate == "log_contains"
    assert spec.target == "GameStarted"
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
    spec = PIETestSpec.from_string("survive:5")
    assert spec.timeout_seconds == 5.0
    _pass("survive:5 (default)")


if __name__ == "__main__":
    print("Running PIETestSpec.from_string tests...")
    test_pawn_possessed()
    test_widget_visible()
    test_log_contains()
    test_ai_state()
    test_survive_with_seconds()
    test_survive_default()
    print("All PIETestSpec.from_string tests passed.")
```

- [ ] **Step 3: Run test to verify it passes**

```bash
cd d:/UE/UE_Bridge
python unreal-plugin/Content/Python/tests/test_pie_test_spec.py
```

Expected output:
```
Running PIETestSpec.from_string tests...
  PASS  pawn_possessed
  PASS  widget_visible
  PASS  log_contains
  PASS  ai_state
  PASS  survive:10
  PASS  survive:5 (default)
All PIETestSpec.from_string tests passed.
```

- [ ] **Step 4: Commit**

```bash
cd d:/UE/UE_Bridge
git add unreal-plugin/Content/Python/tests/
git commit -m "test(gameplay): add PIETestSpec.from_string unit tests (pure Python, no UE4)"
```

---

## Task 3: telemetry_capture.py

**Files:**
- Create: `unreal-plugin/Content/Python/mcp_bridge/generation/telemetry_capture.py`

UE4 does not expose a seekable log file via Python API. UE4 writes the session log to disk at `<ProjectDir>/Saved/Logs/<ProjectName>.log`. This is a plain text file that grows during the session. The log cursor reads this file directly using `unreal.Paths.project_log_dir()` and `unreal.SystemLibrary.get_game_name()` at runtime, falling back to an empty string when not in UE4 (for pure-Python test contexts).

For PIE world access: `unreal.EditorLevelLibrary.get_pie_worlds(False)` returns a list of active PIE worlds (UE4.27 API). If the list is empty, falls back to `unreal.EditorLevelLibrary.get_game_world()`. Both are safer than `get_editor_world()` which is documented as invalid during PIE.

- [ ] **Step 1: Create telemetry_capture.py**

```python
"""PIE telemetry capture: log scraping, runtime state queries, class resolution cache.

All functions that call unreal.* must be called from the game thread.
This module is imported by pie_harness.py and (in Phase 4) by repair_engine.py.
"""
from __future__ import annotations
import os
from typing import Any, Dict, List, Optional

from mcp_bridge.generation.spec_schema import TelemetryFrame, ClassResolutionCache


# ---------------------------------------------------------------------------
# Log cursor
# ---------------------------------------------------------------------------

# Module-level cursor: byte offset into the log file after the last read.
_log_cursor: int = 0


def _get_log_path() -> str:
    """Return the absolute path to the current UE4 session log file.

    UE4 writes to <ProjectDir>/Saved/Logs/<ProjectName>.log.
    Returns empty string outside UE4 (unit test context).
    """
    try:
        import unreal
        log_dir = unreal.Paths.project_log_dir()
        project_name = unreal.SystemLibrary.get_game_name()
        return os.path.join(log_dir, f"{project_name}.log")
    except Exception:
        return ""


def reset_log_cursor() -> None:
    """Reset the cursor to the current end of the log file.

    Call this before launching PIE so assertions only see post-launch log lines.
    """
    global _log_cursor
    path = _get_log_path()
    if path and os.path.exists(path):
        _log_cursor = os.path.getsize(path)
    else:
        _log_cursor = 0


def read_new_log_lines() -> List[str]:
    """Return all log lines written to disk since the last call (or since reset_log_cursor).

    Uses a module-level byte-offset cursor. Thread-safe for single-threaded UE4 game thread use.
    """
    global _log_cursor
    path = _get_log_path()
    if not path or not os.path.exists(path):
        return []
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            f.seek(_log_cursor)
            new_text = f.read()
            _log_cursor = f.tell()
        return [line for line in new_text.splitlines() if line.strip()]
    except Exception:
        return []


# ---------------------------------------------------------------------------
# PIE world helpers
# ---------------------------------------------------------------------------

def get_pie_world() -> Any:
    """Return the active PIE game world, or None if PIE is not running.

    Uses get_pie_worlds(False) (UE4.27 recommended) not get_editor_world(),
    which is documented as invalid during PIE mode.
    Falls back to get_game_world() for UE4.27 builds where get_pie_worlds
    is not available.
    """
    try:
        import unreal
        # Primary: get_pie_worlds returns list of active PIE worlds
        get_pie_worlds_fn = getattr(unreal.EditorLevelLibrary, "get_pie_worlds", None)
        if get_pie_worlds_fn is not None:
            worlds = get_pie_worlds_fn(False)
            if worlds:
                return worlds[0]
        # Fallback: get_game_world()
        get_game_world_fn = getattr(unreal.EditorLevelLibrary, "get_game_world", None)
        if get_game_world_fn is not None:
            return get_game_world_fn()
        return None
    except Exception:
        return None


# ---------------------------------------------------------------------------
# Runtime state queries (all require a live PIE world)
# ---------------------------------------------------------------------------

def get_possessed_pawn_class(pie_world: Any) -> Optional[str]:
    """Return the class name of the possessed pawn in PIE, or None."""
    try:
        import unreal
        pc = unreal.GameplayStatics.get_player_controller(pie_world, 0)
        if pc is None:
            return None
        pawn = pc.get_controlled_pawn()
        if pawn is None:
            return None
        return pawn.get_class().get_name()
    except Exception:
        return None


def get_visible_widget_classes(pie_world: Any) -> List[str]:
    """Return class names of all visible UUserWidget instances in PIE.

    Uses WidgetLibrary.get_all_widgets_of_class() (UE4.27 direct API).
    Returns empty list if the API is unavailable.
    """
    try:
        import unreal
        widget_lib = getattr(unreal, "WidgetLibrary", None)
        if widget_lib is None:
            return []
        base_class = getattr(unreal, "UserWidget", None)
        if base_class is None:
            return []
        get_widgets_fn = getattr(widget_lib, "get_all_widgets_of_class", None)
        if get_widgets_fn is None:
            return []
        widgets = get_widgets_fn(pie_world, base_class, False)
        visible: List[str] = []
        for w in widgets:
            try:
                vis = w.get_visibility()
                if vis != unreal.SlateVisibility.COLLAPSED:
                    visible.append(w.get_class().get_name())
            except Exception:
                pass
        return visible
    except Exception:
        return []


def get_ai_controller_states(pie_world: Any) -> Dict[str, str]:
    """Return {actor_label: state_string} for AI controllers in PIE."""
    try:
        import unreal
        ai_cls = getattr(unreal, "AIController", None)
        if ai_cls is None:
            return {}
        controllers = unreal.GameplayStatics.get_all_actors_of_class(pie_world, ai_cls)
        states: Dict[str, str] = {}
        for ctrl in controllers:
            name = ctrl.get_actor_label()
            try:
                brain = ctrl.get_editor_property("brain_component")
                state = brain.get_active_task_name() if brain else "unknown"
            except Exception:
                state = "unknown"
            states[name] = state
        return states
    except Exception:
        return {}


# ---------------------------------------------------------------------------
# Snapshot
# ---------------------------------------------------------------------------

def snapshot(pie_world: Any) -> TelemetryFrame:
    """Capture current runtime state from the live PIE world."""
    return TelemetryFrame(
        log_lines_since_last=read_new_log_lines(),
        possessed_pawn_class=get_possessed_pawn_class(pie_world),
        ai_controller_states=get_ai_controller_states(pie_world),
        visible_widgets=get_visible_widget_classes(pie_world),
        fps=0.0,  # FPS via stats subsystem: deferred to Phase 1.1
    )


# ---------------------------------------------------------------------------
# ClassResolutionCache
# ---------------------------------------------------------------------------

def build_class_cache(asset_paths: List[str]) -> ClassResolutionCache:
    """Build a ClassResolutionCache from a list of content paths.

    asset_paths: e.g. ["/Game/Generated/Gameplay/BP_Character"]
    """
    cache = ClassResolutionCache()
    for path in asset_paths:
        short_name = path.split("/")[-1]
        cache.class_paths[short_name] = path
    return cache
```

- [ ] **Step 2: Commit**

```bash
cd d:/UE/UE_Bridge
git add unreal-plugin/Content/Python/mcp_bridge/generation/telemetry_capture.py
git commit -m "feat(gameplay): add telemetry_capture with log cursor, PIE world helpers, snapshot"
```

---

## Task 4: pie_harness.py

**Files:**
- Create: `unreal-plugin/Content/Python/mcp_bridge/generation/pie_harness.py`

- [ ] **Step 1: Create pie_harness.py**

```python
"""PIE session lifecycle and acceptance test runner.

launch_pie() / stop_pie() manage the session.
wait_for_pie_ready() polls the log for the UE4.27 PIE-ready marker.
run_assertions() evaluates PIETestSpec predicates against live runtime state.

All unreal.* calls must be on the game thread.
"""
from __future__ import annotations
import time
from typing import Any, List, Optional

from mcp_bridge.generation.spec_schema import PIETestSpec, AssertionResult, TelemetryFrame
from mcp_bridge.generation import telemetry_capture as tc

# The exact string UE4.27 emits to the output log when PIE is ready.
_PIE_READY_MARKER = "PIE: play in editor start"
_PIE_READY_TIMEOUT_S = 30.0
_PIE_POLL_INTERVAL_S = 0.5


def launch_pie(level_path: Optional[str] = None) -> bool:
    """Launch a PIE session. Resets log cursor. Does NOT block until ready."""
    try:
        import unreal
        tc.reset_log_cursor()
        subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        if subsystem is None:
            return False
        subsystem.play_in_editor(in_editor=False)
        return True
    except Exception:
        return False


def stop_pie() -> bool:
    """End the current PIE session."""
    try:
        import unreal
        subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        if subsystem is None:
            return False
        subsystem.request_end_play_map()
        return True
    except Exception:
        return False


def wait_for_pie_ready(timeout_s: float = _PIE_READY_TIMEOUT_S) -> bool:
    """Poll the log file for the PIE-ready marker.

    UE4.27 emits "PIE: play in editor start" when PIE is ready.
    Returns True if found within timeout_s, False otherwise.
    """
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        lines = tc.read_new_log_lines()
        for line in lines:
            if _PIE_READY_MARKER in line:
                return True
        time.sleep(_PIE_POLL_INTERVAL_S)
    return False


def _check_predicate(
    spec: PIETestSpec,
    frame: TelemetryFrame,
    all_lines: List[str],
) -> AssertionResult:
    """Evaluate one PIETestSpec against telemetry and accumulated log lines."""
    p = spec.predicate
    target = spec.target or ""

    if p == "log_contains":
        passed = any(target in line for line in all_lines)
        return AssertionResult(predicate=p, target=target, passed=passed, observed=f"found={passed}")

    if p == "pawn_possessed":
        pawn_class = frame.possessed_pawn_class or ""
        passed = target in pawn_class
        return AssertionResult(predicate=p, target=target, passed=passed, observed=pawn_class or "None")

    if p == "widget_visible":
        # Primary: runtime widget list
        passed = any(target in w for w in frame.visible_widgets)
        # Fallback: log convention [WidgetVisible:<name>]
        if not passed:
            marker = f"[WidgetVisible:{target}]"
            passed = any(marker in line for line in all_lines)
        return AssertionResult(
            predicate=p, target=target, passed=passed,
            observed=f"visible_widgets={frame.visible_widgets}",
        )

    if p == "ai_state":
        expected = spec.expected or ""
        matching = {k: v for k, v in frame.ai_controller_states.items() if target in k}
        passed = any(expected in v for v in matching.values())
        observed = str(matching) if matching else "no_matching_actors"
        return AssertionResult(predicate=p, target=target, passed=passed, observed=observed)

    if p == "survive":
        fatal_markers = ["Fatal:", "Error: (Assertion failed)"]
        crash_lines = [l for l in all_lines if any(m in l for m in fatal_markers)]
        passed = len(crash_lines) == 0
        observed = crash_lines[0] if crash_lines else "no_crash"
        return AssertionResult(predicate=p, target=None, passed=passed, observed=observed)

    return AssertionResult(predicate=p, target=target, passed=False, observed=f"unknown_predicate:{p}")


def run_assertions(tests: List[PIETestSpec]) -> List[AssertionResult]:
    """Evaluate predicates against the live PIE session.

    Must be called after wait_for_pie_ready() returns True.
    """
    pie_world = tc.get_pie_world()
    if pie_world is None:
        return [
            AssertionResult(predicate=t.predicate, target=t.target, passed=False, observed="PIE not running")
            for t in tests
        ]

    all_log_lines: List[str] = tc.read_new_log_lines()
    results: List[AssertionResult] = []

    for spec in tests:
        if spec.predicate == "survive":
            time.sleep(spec.timeout_seconds)
            all_log_lines += tc.read_new_log_lines()
            frame = tc.snapshot(pie_world)
            all_log_lines += frame.log_lines_since_last
        else:
            frame = tc.snapshot(pie_world)
            all_log_lines += frame.log_lines_since_last

        results.append(_check_predicate(spec, frame, all_log_lines))

    return results


def parse_test_specs(tests: List[str]) -> List[PIETestSpec]:
    """Convert shorthand strings into PIETestSpec objects."""
    return [PIETestSpec.from_string(t) for t in tests]
```

- [ ] **Step 2: Commit**

```bash
cd d:/UE/UE_Bridge
git add unreal-plugin/Content/Python/mcp_bridge/generation/pie_harness.py
git commit -m "feat(gameplay): add pie_harness with launch/stop/wait_for_ready and assertion runner"
```

---

## Task 5: handlers/gameplay.py

**Files:**
- Create: `unreal-plugin/Content/Python/mcp_bridge/handlers/gameplay.py`

- [ ] **Step 1: Create handlers/gameplay.py**

```python
"""Gameplay handler: PIE control, telemetry, acceptance test runner.

Commands registered in router.py:
    gameplay_pie_start            -- launch PIE and wait until ready
    gameplay_pie_stop             -- end PIE session
    gameplay_telemetry_snapshot   -- capture current runtime state
    gameplay_run_acceptance_tests -- run predicates against live PIE
"""
from __future__ import annotations
import traceback
from typing import Any, Dict, List

from mcp_bridge.generation import pie_harness
from mcp_bridge.generation import telemetry_capture as tc


def handle_pie_start(params: Dict[str, Any]) -> Dict[str, Any]:
    """Launch PIE and wait until the session is ready.

    Params:
        level_path (str, optional): Content path to load before PIE.
    """
    try:
        level_path = params.get("level_path")
        ok = pie_harness.launch_pie(level_path=level_path)
        if not ok:
            return {"success": False, "data": {}, "error": "Failed to call play_in_editor()"}
        ready = pie_harness.wait_for_pie_ready()
        if not ready:
            return {
                "success": False,
                "data": {},
                "error": f"PIE did not become ready within {pie_harness._PIE_READY_TIMEOUT_S}s",
            }
        return {"success": True, "data": {"status": "pie_ready"}}
    except Exception as e:
        return {"success": False, "data": {}, "error": f"{e}\n{traceback.format_exc()}"}


def handle_pie_stop(params: Dict[str, Any]) -> Dict[str, Any]:
    """End the current PIE session."""
    try:
        ok = pie_harness.stop_pie()
        return {"success": ok, "data": {"status": "pie_stopped" if ok else "stop_failed"}}
    except Exception as e:
        return {"success": False, "data": {}, "error": str(e)}


def handle_telemetry_snapshot(params: Dict[str, Any]) -> Dict[str, Any]:
    """Capture current PIE runtime state."""
    try:
        pie_world = tc.get_pie_world()
        if pie_world is None:
            return {"success": False, "data": {}, "error": "PIE is not running"}
        frame = tc.snapshot(pie_world)
        return {
            "success": True,
            "data": {
                "log_lines": frame.log_lines_since_last,
                "possessed_pawn_class": frame.possessed_pawn_class,
                "ai_controller_states": frame.ai_controller_states,
                "visible_widgets": frame.visible_widgets,
                "fps": frame.fps,
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": f"{e}\n{traceback.format_exc()}"}


def handle_run_acceptance_tests(params: Dict[str, Any]) -> Dict[str, Any]:
    """Run acceptance test predicates against the live PIE session.

    Params:
        tests (List[str]): Predicate strings.
        timeout_seconds (float, optional): Per-predicate timeout override.
    """
    try:
        raw_tests: List[str] = params.get("tests", [])
        if not raw_tests:
            return {"success": False, "data": {}, "error": "No tests provided"}
        timeout_override = params.get("timeout_seconds")
        specs = pie_harness.parse_test_specs(raw_tests)
        if timeout_override is not None:
            for spec in specs:
                spec.timeout_seconds = float(timeout_override)
        results = pie_harness.run_assertions(specs)
        total = len(results)
        passed = sum(1 for r in results if r.passed)
        return {
            "success": True,
            "data": {
                "total": total,
                "passed": passed,
                "failed": total - passed,
                "results": [
                    {"predicate": r.predicate, "target": r.target, "passed": r.passed, "observed": r.observed}
                    for r in results
                ],
            },
        }
    except Exception as e:
        return {"success": False, "data": {}, "error": f"{e}\n{traceback.format_exc()}"}
```

- [ ] **Step 2: Commit**

```bash
cd d:/UE/UE_Bridge
git add unreal-plugin/Content/Python/mcp_bridge/handlers/gameplay.py
git commit -m "feat(gameplay): add gameplay handler with pie_start, pie_stop, snapshot, run_acceptance_tests"
```

---

## Task 6: Register commands in router.py

**Files:**
- Modify: `unreal-plugin/Content/Python/mcp_bridge/router.py`

- [ ] **Step 1: Add import block**

In [router.py](unreal-plugin/Content/Python/mcp_bridge/router.py), after the `from mcp_bridge.handlers.promptbrush import (...)` block (around line 62), add:

```python
from mcp_bridge.handlers.gameplay import (
    handle_pie_start,
    handle_pie_stop,
    handle_telemetry_snapshot,
    handle_run_acceptance_tests,
)
```

- [ ] **Step 2: Add routes to COMMAND_ROUTES dict**

After the `# PromptBrush` section (around line 122), add:

```python
    # Gameplay (PIE harness + telemetry)
    "gameplay_pie_start": handle_pie_start,
    "gameplay_pie_stop": handle_pie_stop,
    "gameplay_telemetry_snapshot": handle_telemetry_snapshot,
    "gameplay_run_acceptance_tests": handle_run_acceptance_tests,
```

- [ ] **Step 3: Commit**

```bash
cd d:/UE/UE_Bridge
git add unreal-plugin/Content/Python/mcp_bridge/router.py
git commit -m "feat(gameplay): register gameplay_* commands in router"
```

---

## Task 7: TypeScript tool definitions

**Files:**
- Create: `mcp-server/src/tools/gameplay.ts`
- Modify: `mcp-server/src/index.ts`

- [ ] **Step 1: Create mcp-server/src/tools/gameplay.ts**

```typescript
/**
 * Gameplay tools: PIE control, telemetry, acceptance tests.
 *
 * None of these commands are modifying in the undo-stack sense (PIE
 * sessions are transient). gameplay_pie_start is NOT added to modifyingCommands.
 */

import { z } from "zod";
import { UnrealClient } from "../unreal-client.js";
import type { ToolDefinition } from "../types.js";

export function createGameplayTools(client: UnrealClient): ToolDefinition[] {
  return [
    {
      name: "gameplay_pie_start",
      description:
        "Launch a Play In Editor (PIE) session and wait until the game world is ready " +
        "(up to 30s). Returns success once the PIE-ready log marker is found. " +
        "Call this before gameplay_run_acceptance_tests or gameplay_telemetry_snapshot.",
      inputSchema: z.object({
        level_path: z
          .string()
          .optional()
          .describe(
            "Content path of the level to load before PIE (e.g. /Game/Maps/Gameplay). " +
            "If omitted, uses the current level."
          ),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("gameplay_pie_start", params);
        return {
          content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
        };
      },
    },
    {
      name: "gameplay_pie_stop",
      description: "End the current PIE session.",
      inputSchema: z.object({}),
      handler: async (params) => {
        const result = await client.sendCommand("gameplay_pie_stop", params);
        return {
          content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
        };
      },
    },
    {
      name: "gameplay_telemetry_snapshot",
      description:
        "Capture the current PIE runtime state: new log lines since last snapshot, " +
        "possessed pawn class, visible widget class names, AI controller states. " +
        "PIE must be running.",
      inputSchema: z.object({}),
      handler: async (params) => {
        const result = await client.sendCommand("gameplay_telemetry_snapshot", params);
        return {
          content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
        };
      },
    },
    {
      name: "gameplay_run_acceptance_tests",
      description:
        "Run structured acceptance test predicates against a live PIE session. " +
        "PIE must already be running (call gameplay_pie_start first). " +
        "Returns pass/fail per predicate with observed value. " +
        "Predicates: 'pawn_possessed:ClassName', 'widget_visible:WidgetName', " +
        "'log_contains:String', 'ai_state:ActorName:StateName', 'survive:N' (N seconds without crash).",
      inputSchema: z.object({
        tests: z
          .array(z.string())
          .min(1)
          .describe(
            "List of predicate strings. Examples: " +
            "'pawn_possessed:BP_Character', 'widget_visible:WBP_HUD', " +
            "'log_contains:GameStarted', 'ai_state:BP_Enemy:Patrol', 'survive:5'"
          ),
        timeout_seconds: z
          .number()
          .positive()
          .optional()
          .describe("Override timeout_seconds for all predicates (default: 5.0 per predicate)"),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("gameplay_run_acceptance_tests", params);
        return {
          content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
        };
      },
    },
  ];
}
```

- [ ] **Step 2: Register in mcp-server/src/index.ts**

In [index.ts](mcp-server/src/index.ts), add import after the `createPromptBrushTools` import (around line 28):

```typescript
import { createGameplayTools } from "./tools/gameplay.js";
```

In the `allTools` array (around line 51), add:

```typescript
    ...createGameplayTools(client),
```

`gameplay_pie_start` and other gameplay commands do not modify editor state in the undo-stack sense (PIE is transient) -- do NOT add them to `modifyingCommands`.

- [ ] **Step 3: Build TypeScript**

```bash
cd d:/UE/UE_Bridge
npm run build
```

Expected: no errors, `mcp-server/dist/tools/gameplay.js` created.

- [ ] **Step 4: Commit**

```bash
git add mcp-server/src/tools/gameplay.ts mcp-server/src/index.ts mcp-server/dist/
git commit -m "feat(gameplay): add TypeScript tool definitions for gameplay_* commands"
```

---

## Task 8: Unit tests (mock server, no UE4)

**Files:**
- Create: `mcp-server/tests/gameplay-tools.test.ts`

The existing `MockUnrealServer` in `mock-server.ts` has a `setHandler(command, fn)` method that registers custom command handlers. All 4 gameplay commands need handlers registered. Tests use the same pattern as `material-blueprint-tools.test.ts`: a `test()` function, `assert()` / `assertEqual()` helpers, setup/teardown with a dedicated port, and a `callTool()` wrapper.

- [ ] **Step 1: Verify MockUnrealServer.setHandler exists**

```bash
cd d:/UE/UE_Bridge
grep "setHandler" mcp-server/tests/mock-server.ts
```

Expected: `setHandler(command: string, handler: CommandHandler): void` appears.

- [ ] **Step 2: Write gameplay-tools.test.ts**

```typescript
/**
 * Unit tests for gameplay tools.
 *
 * Uses MockUnrealServer with setHandler() to simulate UE4 PIE responses.
 * No UE4 instance required.
 *
 * Run: npx tsx mcp-server/tests/gameplay-tools.test.ts
 */

import { MockUnrealServer } from "./mock-server.js";
import { UnrealClient } from "../src/unreal-client.js";
import { createGameplayTools } from "../src/tools/gameplay.js";
import type { ToolDefinition } from "../src/types.js";

const TEST_PORT = 18770;

let server: MockUnrealServer;
let client: UnrealClient;
let toolMap: Map<string, ToolDefinition>;

interface TestCase { name: string; fn: () => Promise<void>; }
const tests: TestCase[] = [];
let passed = 0;
let failed = 0;

function test(name: string, fn: () => Promise<void>): void {
  tests.push({ name, fn });
}

function assert(condition: boolean, message: string): void {
  if (!condition) throw new Error(`Assertion failed: ${message}`);
}

function assertEqual<T>(actual: T, expected: T, label: string): void {
  if (actual !== expected) {
    throw new Error(`${label}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
  }
}

async function callTool(name: string, params: Record<string, unknown>): Promise<Record<string, unknown>> {
  const tool = toolMap.get(name);
  if (!tool) throw new Error(`Tool not found: ${name}`);
  const result = await tool.handler(params);
  return JSON.parse((result.content[0] as { text: string }).text) as Record<string, unknown>;
}

async function setup(): Promise<void> {
  server = new MockUnrealServer();
  await server.start(TEST_PORT);
  client = new UnrealClient({ port: TEST_PORT });
  const tools = createGameplayTools(client);
  toolMap = new Map(tools.map((t) => [t.name, t]));

  // Register mock handlers for all 4 gameplay commands
  server.setHandler("gameplay_pie_start", (_params) => ({
    success: true,
    data: { status: "pie_ready" },
  }));

  server.setHandler("gameplay_pie_stop", (_params) => ({
    success: true,
    data: { status: "pie_stopped" },
  }));

  server.setHandler("gameplay_telemetry_snapshot", (_params) => ({
    success: true,
    data: {
      log_lines: ["[2026.03.18] GameStarted", "[2026.03.18] PIE: play in editor start"],
      possessed_pawn_class: "BP_Character_C",
      ai_controller_states: { "BP_Enemy_1": "Patrol" },
      visible_widgets: ["WBP_HUD_C"],
      fps: 60.0,
    },
  }));

  server.setHandler("gameplay_run_acceptance_tests", (params) => {
    const tests = (params.tests as string[]) ?? [];
    return {
      success: true,
      data: {
        total: tests.length,
        passed: tests.length,
        failed: 0,
        results: tests.map((t) => {
          const parts = t.split(":");
          return { predicate: parts[0], target: parts[1] ?? null, passed: true, observed: "mock" };
        }),
      },
    };
  });
}

async function teardown(): Promise<void> {
  await server.stop();
}

// ---- Tool registration ----

test("all 4 gameplay tools are registered", async () => {
  const expected = [
    "gameplay_pie_start",
    "gameplay_pie_stop",
    "gameplay_telemetry_snapshot",
    "gameplay_run_acceptance_tests",
  ];
  for (const name of expected) {
    assert(toolMap.has(name), `Tool '${name}' should be registered`);
  }
  assertEqual(toolMap.size, 4, "gameplay tool count");
});

// ---- gameplay_pie_start ----

test("gameplay_pie_start returns pie_ready status", async () => {
  const result = await callTool("gameplay_pie_start", {});
  assertEqual(result.success as boolean, true, "pie_start success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.status as string, "pie_ready", "pie_start status");
});

test("gameplay_pie_start accepts optional level_path", async () => {
  const result = await callTool("gameplay_pie_start", { level_path: "/Game/Maps/TestLevel" });
  assertEqual(result.success as boolean, true, "pie_start with level_path");
});

// ---- gameplay_pie_stop ----

test("gameplay_pie_stop returns pie_stopped status", async () => {
  const result = await callTool("gameplay_pie_stop", {});
  assertEqual(result.success as boolean, true, "pie_stop success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.status as string, "pie_stopped", "pie_stop status");
});

// ---- gameplay_telemetry_snapshot ----

test("gameplay_telemetry_snapshot returns telemetry frame shape", async () => {
  const result = await callTool("gameplay_telemetry_snapshot", {});
  assertEqual(result.success as boolean, true, "snapshot success");
  const data = result.data as Record<string, unknown>;
  assert(Array.isArray(data.log_lines), "log_lines is array");
  assert(typeof data.possessed_pawn_class === "string", "possessed_pawn_class is string");
  assert(Array.isArray(data.visible_widgets), "visible_widgets is array");
  assert(typeof data.ai_controller_states === "object", "ai_controller_states is object");
  assert(typeof data.fps === "number", "fps is number");
});

// ---- gameplay_run_acceptance_tests ----

test("gameplay_run_acceptance_tests rejects empty tests array (Zod validation)", async () => {
  let threw = false;
  try {
    await callTool("gameplay_run_acceptance_tests", { tests: [] });
  } catch {
    threw = true;
  }
  assert(threw, "empty tests array should throw Zod validation error");
});

test("gameplay_run_acceptance_tests returns results array with correct shape", async () => {
  const result = await callTool("gameplay_run_acceptance_tests", {
    tests: ["pawn_possessed:BP_Character", "log_contains:GameStarted"],
  });
  assertEqual(result.success as boolean, true, "run_tests success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.total as number, 2, "total is 2");
  assertEqual(data.passed as number, 2, "passed is 2");
  const results = data.results as Array<Record<string, unknown>>;
  assert(Array.isArray(results), "results is array");
  assertEqual(results.length, 2, "results has 2 entries");
  // Verify shape of first result
  assert("predicate" in results[0], "result has predicate");
  assert("passed" in results[0], "result has passed");
  assert("observed" in results[0], "result has observed");
});

test("gameplay_run_acceptance_tests accepts optional timeout_seconds", async () => {
  const result = await callTool("gameplay_run_acceptance_tests", {
    tests: ["survive:5"],
    timeout_seconds: 10,
  });
  assertEqual(result.success as boolean, true, "run_tests with timeout_seconds");
});

// ---- Runner ----

async function run(): Promise<void> {
  await setup();
  for (const t of tests) {
    try {
      await t.fn();
      console.log(`  PASS  ${t.name}`);
      passed++;
    } catch (e) {
      console.log(`  FAIL  ${t.name}: ${(e as Error).message}`);
      failed++;
    }
  }
  await teardown();
  console.log(`\n${passed} passed, ${failed} failed`);
  if (failed > 0) process.exit(1);
}

run();
```

- [ ] **Step 3: Run test before build to confirm it fails**

```bash
cd d:/UE/UE_Bridge
npx tsx mcp-server/tests/gameplay-tools.test.ts
```

Expected: module not found error for `gameplay.js` (TypeScript not built yet). This confirms the test is actually testing the real module.

- [ ] **Step 4: Build and run**

```bash
npm run build
npx tsx mcp-server/tests/gameplay-tools.test.ts
```

Expected:
```
  PASS  all 4 gameplay tools are registered
  PASS  gameplay_pie_start returns pie_ready status
  PASS  gameplay_pie_start accepts optional level_path
  PASS  gameplay_pie_stop returns pie_stopped status
  PASS  gameplay_telemetry_snapshot returns telemetry frame shape
  PASS  gameplay_run_acceptance_tests rejects empty tests array (Zod validation)
  PASS  gameplay_run_acceptance_tests returns results array with correct shape
  PASS  gameplay_run_acceptance_tests accepts optional timeout_seconds

8 passed, 0 failed
```

- [ ] **Step 5: Commit**

```bash
git add mcp-server/tests/gameplay-tools.test.ts
git commit -m "test(gameplay): add MockUnrealServer-based unit tests for gameplay_* tools"
```

---

## Task 9: Wire into npm test chain

**Files:**
- Modify: `mcp-server/package.json`

- [ ] **Step 1: Update test script**

In [mcp-server/package.json](mcp-server/package.json), change the `"test"` script from:

```json
"test": "tsx tests/actor-tools.test.ts && tsx tests/level-viewport-tools.test.ts && tsx tests/material-blueprint-tools.test.ts"
```

to:

```json
"test": "tsx tests/actor-tools.test.ts && tsx tests/level-viewport-tools.test.ts && tsx tests/material-blueprint-tools.test.ts && tsx tests/gameplay-tools.test.ts"
```

- [ ] **Step 2: Run full suite**

```bash
cd d:/UE/UE_Bridge
npm test
```

Expected: 4 test files all pass. If any earlier file fails, fix it before proceeding.

- [ ] **Step 3: Commit**

```bash
git add mcp-server/package.json
git commit -m "test(gameplay): add gameplay-tools.test.ts to npm test chain"
```

---

## Task 10: Manual integration smoke test (requires UE4 running)

Skip if UE4 is not running. Validates the full stack without Claude Code MCP.

- [ ] **Step 1: Verify listener is alive**

```bash
curl -s -X POST http://localhost:8080/ -H "Content-Type: application/json" \
  -d "{\"command\":\"ping\",\"params\":{}}" | python -m json.tool
```

Expected: `{"success": true, "data": {"pong": true}, "error": null}`

- [ ] **Step 2: Launch PIE**

```bash
curl -s -X POST http://localhost:8080/ -H "Content-Type: application/json" \
  -d "{\"command\":\"gameplay_pie_start\",\"params\":{}}" | python -m json.tool
```

Expected (within 30s): `{"success": true, "data": {"status": "pie_ready"}, "error": null}`

- [ ] **Step 3: Take telemetry snapshot**

```bash
curl -s -X POST http://localhost:8080/ -H "Content-Type: application/json" \
  -d "{\"command\":\"gameplay_telemetry_snapshot\",\"params\":{}}" | python -m json.tool
```

Expected: `success: true` with `log_lines`, `possessed_pawn_class`, `visible_widgets` fields present.

- [ ] **Step 4: Run survive:3 acceptance test**

```bash
curl -s -X POST http://localhost:8080/ -H "Content-Type: application/json" \
  -d "{\"command\":\"gameplay_run_acceptance_tests\",\"params\":{\"tests\":[\"survive:3\"]}}" | python -m json.tool
```

Expected (after 3 seconds): `{"data": {"passed": 1, "failed": 0, ...}}`

- [ ] **Step 5: Stop PIE**

```bash
curl -s -X POST http://localhost:8080/ -H "Content-Type: application/json" \
  -d "{\"command\":\"gameplay_pie_stop\",\"params\":{}}" | python -m json.tool
```

Expected: `{"success": true, "data": {"status": "pie_stopped"}}`

---

## Definition of Done (offline-testable)

These can be verified without UE4:

1. `npm test` passes (4 test files, 0 failures)
2. `python unreal-plugin/Content/Python/tests/test_pie_test_spec.py` prints "All PIETestSpec.from_string tests passed."
3. `npm run build` completes with no TypeScript errors
4. `mcp-server/dist/tools/gameplay.js` exists and exports `createGameplayTools`

**With UE4 running (not required for merge):**

5. `gameplay_pie_start` returns `{status: "pie_ready"}` within 30s
6. `gameplay_run_acceptance_tests` with `"survive:3"` returns `passed: 1` after 3 seconds

---

## Phase handoff

After Phase 1 is merged:

- **Phase 2** (Prompt Interpreter) and **Phase 3a/3b/3c** (Authoring Depth) can start immediately in parallel -- no dependency on Phase 1.
- **Phase 4** (Repair Engine) can start after Phase 1 -- it reuses the log-cursor utility from `telemetry_capture.py`. If Phase 4 starts before Phase 1, write the log-cursor inline in `repair_engine.py` and extract later.
