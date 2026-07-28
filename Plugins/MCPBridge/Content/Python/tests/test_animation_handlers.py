"""Tests for the pure logic in handlers/animation.py. No UE4 required.

The handler module imports unreal only inside function bodies, so it can be
imported and its non-editor logic exercised directly. Covered here: the
drift verdict thresholds, and bone mask resolution driven by a stub that
stands in for unreal.AnimationLibrary.

Run: python Plugins/MCPBridge/Content/Python/tests/test_animation_handlers.py
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PYTHON_ROOT = os.path.dirname(HERE)
if PYTHON_ROOT not in sys.path:
    sys.path.insert(0, PYTHON_ROOT)

from mcp_bridge.handlers import animation as ah  # noqa: E402


# A small humanoid hierarchy, child -> parent.
PARENTS = {
    "root": None,
    "pelvis": "root",
    "spine_01": "pelvis",
    "spine_02": "spine_01",
    "clavicle_l": "spine_02",
    "upperarm_l": "clavicle_l",
    "lowerarm_l": "upperarm_l",
    "hand_l": "lowerarm_l",
    "thigh_l": "pelvis",
    "calf_l": "thigh_l",
    "foot_l": "calf_l",
}

ALL_BONES = list(PARENTS.keys())


class StubAnimLibrary:
    """Stands in for unreal.AnimationLibrary for hierarchy queries."""

    def __init__(self) -> None:
        self.calls = 0

    def find_bone_path_to_root(self, _sequence, bone):
        self.calls += 1
        path = []
        current = PARENTS.get(bone)
        while current is not None:
            path.append(current)
            current = PARENTS.get(current)
        return path


def _resolve(mask, available=None):
    lib = StubAnimLibrary()
    return ah._resolve_bone_mask(lib, object(), mask, available or ALL_BONES)


# --- verdict thresholds -----------------------------------------------------


def test_verdict_aligned_below_threshold() -> None:
    verdict = ah._reanchor_verdict(0.9, 1.5, 30.0)
    assert verdict["code"] == "aligned", verdict


def test_verdict_drifted_between_threshold_and_ceiling() -> None:
    for value in (1.5, 6.2, 29.99):
        verdict = ah._reanchor_verdict(value, 1.5, 30.0)
        assert verdict["code"] == "drifted", (value, verdict)


def test_verdict_divergent_at_and_above_ceiling() -> None:
    for value in (30.0, 80.65, 179.0):
        verdict = ah._reanchor_verdict(value, 1.5, 30.0)
        assert verdict["code"] == "divergent", (value, verdict)


def test_divergent_verdict_explains_itself() -> None:
    """The summary has to say why, since it is the guard against a bad apply."""
    verdict = ah._reanchor_verdict(80.65, 1.5, 30.0)
    summary = verdict["summary"].lower()
    assert "different start pose" in summary, verdict
    assert "review" in summary, verdict


def test_review_ceiling_is_configurable() -> None:
    assert ah._reanchor_verdict(40.0, 1.5, 30.0)["code"] == "divergent"
    assert ah._reanchor_verdict(40.0, 1.5, 90.0)["code"] == "drifted"


# --- bone mask resolution ---------------------------------------------------


def test_default_mask_is_upper_body_only() -> None:
    """The safety default: spine_01 and below, never root, pelvis, or legs."""
    selected, explanation = _resolve({})
    assert explanation["used_default_mask"] is True, explanation
    assert "root" not in selected, selected
    assert "pelvis" not in selected, selected
    for leg in ("thigh_l", "calf_l", "foot_l"):
        assert leg not in selected, f"{leg} must not be in the default mask"
    for arm in ("spine_01", "spine_02", "clavicle_l", "upperarm_l", "lowerarm_l", "hand_l"):
        assert arm in selected, f"{arm} should be in the default mask"


def test_subtree_includes_its_own_root() -> None:
    selected, _ = _resolve({"include_subtrees": ["clavicle_l"]})
    assert "clavicle_l" in selected, selected
    assert "hand_l" in selected, selected
    assert "spine_02" not in selected, "must not walk upward from the subtree root"


def test_explicit_include_bones_bypasses_subtrees() -> None:
    selected, explanation = _resolve({"include_bones": ["foot_l", "root"]})
    assert selected == ["root", "foot_l"], selected
    assert explanation["used_default_mask"] is False, explanation


def test_exclude_beats_include() -> None:
    selected, _ = _resolve({
        "include_subtrees": ["spine_01"],
        "exclude_bones": ["hand_l", "lowerarm_l"],
    })
    assert "hand_l" not in selected, selected
    assert "lowerarm_l" not in selected, selected
    assert "upperarm_l" in selected, selected


def test_mask_preserves_track_order() -> None:
    """Selection order must follow the track list, not the mask spec."""
    selected, _ = _resolve({"include_bones": ["hand_l", "spine_01"]})
    assert selected == ["spine_01", "hand_l"], selected


def test_mask_ignores_bones_with_no_track() -> None:
    selected, _ = _resolve({"include_bones": ["hand_l", "nonexistent_bone"]},
                           available=["spine_01", "hand_l"])
    assert selected == ["hand_l"], selected


def test_ancestry_lookup_is_cached() -> None:
    """One find_bone_path_to_root call per bone, not per subtree comparison."""
    lib = StubAnimLibrary()
    ah._resolve_bone_mask(lib, object(), {"include_subtrees": ["spine_01", "pelvis"]},
                          ALL_BONES)
    assert lib.calls == len(ALL_BONES), lib.calls


def test_unknown_profile_is_rejected_before_any_read() -> None:
    try:
        ah._reanchor_options({"profile": "ease_out_bounce"})
    except ah._ReanchorSkip as exc:
        assert "ease_out_bounce" in str(exc), exc
        return
    raise AssertionError("expected _ReanchorSkip for an unknown profile")


def test_options_defaults() -> None:
    opts = ah._reanchor_options({})
    assert opts["profile"] == "decay", opts
    assert opts["window_frames"] == 12, opts
    assert opts["threshold_degrees"] == 1.5, opts
    assert opts["include_translation"] is False, opts
    assert opts["review_ceiling_degrees"] == ah.REVIEW_CEILING_DEGREES, opts


def main() -> int:
    tests = [
        test_verdict_aligned_below_threshold,
        test_verdict_drifted_between_threshold_and_ceiling,
        test_verdict_divergent_at_and_above_ceiling,
        test_divergent_verdict_explains_itself,
        test_review_ceiling_is_configurable,
        test_default_mask_is_upper_body_only,
        test_subtree_includes_its_own_root,
        test_explicit_include_bones_bypasses_subtrees,
        test_exclude_beats_include,
        test_mask_preserves_track_order,
        test_mask_ignores_bones_with_no_track,
        test_ancestry_lookup_is_cached,
        test_unknown_profile_is_rejected_before_any_read,
        test_options_defaults,
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
