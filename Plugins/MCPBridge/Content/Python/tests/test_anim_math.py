"""Tests for animation pose statistics. Plain Python, no UE4 required.

Run: python Plugins/MCPBridge/Content/Python/tests/test_anim_math.py
"""

import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PYTHON_ROOT = os.path.dirname(HERE)
if PYTHON_ROOT not in sys.path:
    sys.path.insert(0, PYTHON_ROOT)

from mcp_bridge.utils import anim_math as am  # noqa: E402


def _close(actual: float, expected: float, tol: float = 1e-6) -> bool:
    return abs(actual - expected) <= tol


# --- roughness: the three cases the design spec pins down --------------------


def test_linear_ramp_has_zero_roughness() -> None:
    """Constant-velocity motion has no second derivative, so roughness is 0."""
    points = [(0.1 * k, 0.0, 0.0) for k in range(50)]
    assert _close(am.roughness(points), 0.0), am.roughness(points)


def test_sawtooth_roughness_is_two() -> None:
    """Alternating every frame reverses direction each step.

    First differences are all s, second differences all 2s, so the ratio is
    exactly 2. This is the analytic worst case and anchors the upper end of
    the scale.
    """
    step = 0.05
    points = [((step if k % 2 else 0.0), 0.0, 0.0) for k in range(50)]
    assert _close(am.roughness(points), 2.0), am.roughness(points)


def test_constant_track_is_static_with_zero_step_mean() -> None:
    """A motionless track must not divide by zero."""
    points = [(3.0, 4.0, 5.0)] * 40
    steps = am.position_steps(points)
    dist = am.distribution(steps)
    assert _close(dist["mean"], 0.0), dist
    assert _close(am.roughness(points), 0.0), am.roughness(points)
    assert am.classify(dist["mean"], am.roughness(points)) == "static"


def test_roughness_handles_short_tracks() -> None:
    """Fewer than three keys cannot have a second difference."""
    assert am.roughness([]) == 0.0
    assert am.roughness([(0.0, 0.0, 0.0)]) == 0.0
    assert am.roughness([(0.0, 0.0, 0.0), (1.0, 0.0, 0.0)]) == 0.0


# --- classification ---------------------------------------------------------


def test_classify_boundaries() -> None:
    moving = am.STATIC_STEP_EPSILON * 10.0
    assert am.classify(0.0, 0.0) == "static"
    assert am.classify(moving, 0.0) == "smooth_drift"
    assert am.classify(moving, 0.49) == "smooth_drift"
    assert am.classify(moving, 0.5) == "stepped"
    assert am.classify(moving, 0.99) == "stepped"
    assert am.classify(moving, 1.0) == "noisy"
    assert am.classify(moving, 2.0) == "noisy"


def test_smooth_drift_case_matches_idle_shape() -> None:
    """A slow sinusoidal sway over 556 frames must classify as smooth_drift.

    This is the shape of the SK_Donathan_Idle_Final root track: a few
    centimetres of range spread across the whole clip. It must not read as
    jiggle.
    """
    points = [
        (1.7 * math.sin(2.0 * math.pi * k / 556.0),
         1.2 * math.cos(2.0 * math.pi * k / 556.0),
         0.0)
        for k in range(556)
    ]
    steps = am.position_steps(points)
    dist = am.distribution(steps)
    r = am.roughness(points)
    assert am.classify(dist["mean"], r) == "smooth_drift", (dist, r)
    # Range should recover roughly the sinusoid's peak-to-peak amplitude.
    rng = am.axis_range(points)
    assert _close(rng["x"], 3.4, tol=0.01), rng
    assert _close(rng["y"], 2.4, tol=0.01), rng


def test_quantized_sway_reads_rougher_than_the_smooth_original() -> None:
    """Rounding a smooth sway to a coarse grid is the compression failure mode.

    The quantized version must score meaningfully rougher than the smooth one;
    that gap is the whole reason the metric exists.
    """
    smooth = [(1.7 * math.sin(2.0 * math.pi * k / 556.0), 0.0, 0.0) for k in range(556)]
    grid = 0.05
    quantized = [(round(p[0] / grid) * grid, 0.0, 0.0) for p in smooth]
    assert am.roughness(quantized) > am.roughness(smooth) * 10.0, (
        am.roughness(quantized), am.roughness(smooth))


# --- percentile and distribution --------------------------------------------


def test_percentile_interpolates() -> None:
    values = [0.0, 1.0, 2.0, 3.0, 4.0]
    assert _close(am.percentile(values, 0.0), 0.0)
    assert _close(am.percentile(values, 1.0), 4.0)
    assert _close(am.percentile(values, 0.5), 2.0)
    # 0.95 of 4 intervals is 3.8, i.e. 80% of the way from 3.0 to 4.0.
    assert _close(am.percentile(values, 0.95), 3.8)


def test_percentile_edge_cases() -> None:
    assert am.percentile([], 0.95) == 0.0
    assert am.percentile([7.0], 0.95) == 7.0


def test_percentile_does_not_mutate_input() -> None:
    values = [3.0, 1.0, 2.0]
    am.percentile(values, 0.5)
    assert values == [3.0, 1.0, 2.0], values


def test_distribution_empty() -> None:
    assert am.distribution([]) == {"mean": 0.0, "max": 0.0, "p95": 0.0}


# --- quaternions ------------------------------------------------------------


def _axis_angle(axis, degrees: float):
    half = math.radians(degrees) / 2.0
    s = math.sin(half)
    n = math.sqrt(sum(c * c for c in axis))
    return (axis[0] / n * s, axis[1] / n * s, axis[2] / n * s, math.cos(half))


def test_quat_angle_identity_is_zero() -> None:
    q = _axis_angle((0.0, 0.0, 1.0), 30.0)
    assert _close(am.quat_angle_degrees(q, q), 0.0, tol=1e-4)


def test_quat_angle_known_separation() -> None:
    a = _axis_angle((0.0, 0.0, 1.0), 10.0)
    b = _axis_angle((0.0, 0.0, 1.0), 55.0)
    assert _close(am.quat_angle_degrees(a, b), 45.0, tol=1e-4)


def test_quat_angle_ignores_double_cover() -> None:
    """q and -q are the same rotation and must compare as 0 degrees apart."""
    q = _axis_angle((0.3, 0.5, 0.8), 74.0)
    negated = (-q[0], -q[1], -q[2], -q[3])
    assert _close(am.quat_angle_degrees(q, negated), 0.0, tol=1e-4)


def test_quat_delta_maps_target_onto_reference() -> None:
    """delta * target should equal reference, which is the re-anchor identity."""
    reference = _axis_angle((0.0, 1.0, 0.0), 80.0)
    target = _axis_angle((0.0, 1.0, 0.0), 25.0)
    delta = am.quat_delta(reference, target)
    assert _close(am.quat_angle_degrees(am.quat_multiply(delta, target), reference),
                  0.0, tol=1e-4)


def test_quat_delta_magnitude_matches_separation() -> None:
    reference = _axis_angle((1.0, 0.0, 0.0), 90.0)
    target = _axis_angle((1.0, 0.0, 0.0), 20.0)
    identity = (0.0, 0.0, 0.0, 1.0)
    delta = am.quat_delta(reference, target)
    assert _close(am.quat_angle_degrees(delta, identity), 70.0, tol=1e-4)


def test_quat_normalize_handles_zero() -> None:
    assert am.quat_normalize((0.0, 0.0, 0.0, 0.0)) == (0.0, 0.0, 0.0, 1.0)


# --- axis range -------------------------------------------------------------


def test_axis_range_empty() -> None:
    assert am.axis_range([]) == {"x": 0.0, "y": 0.0, "z": 0.0}


def test_axis_range_basic() -> None:
    points = [(0.0, -1.0, 5.0), (3.35, 1.34, 5.0), (1.0, 0.0, 5.0)]
    rng = am.axis_range(points)
    assert _close(rng["x"], 3.35)
    assert _close(rng["y"], 2.34)
    assert _close(rng["z"], 0.0)


# --- weight profiles (Pass 2) -----------------------------------------------


def test_smoothstep_endpoints_and_midpoint() -> None:
    assert _close(am.smoothstep(0.0), 0.0)
    assert _close(am.smoothstep(1.0), 1.0)
    assert _close(am.smoothstep(0.5), 0.5)
    assert _close(am.smoothstep(-3.0), 0.0), "must clamp below 0"
    assert _close(am.smoothstep(3.0), 1.0), "must clamp above 1"


def test_smoothstep_has_flat_ends() -> None:
    """Zero slope at both ends is the reason for using it over a linear ramp."""
    near_start = am.smoothstep(0.01) - am.smoothstep(0.0)
    near_middle = am.smoothstep(0.51) - am.smoothstep(0.50)
    assert near_start < near_middle / 10.0, (near_start, near_middle)


def test_constant_profile_is_one_everywhere() -> None:
    for i in range(20):
        assert _close(am.weight_at("constant", i, 20, 5), 1.0)


def test_decay_profile_shape() -> None:
    window = 12
    assert _close(am.weight_at("decay", 0, 60, window), 1.0), "anchor must be fully corrected"
    assert _close(am.weight_at("decay", window, 60, window), 0.0), "must reach zero at the window"
    assert _close(am.weight_at("decay", 59, 60, window), 0.0), "must stay zero past the window"
    # Monotonically decreasing across the window.
    previous = 1.1
    for i in range(window + 1):
        current = am.weight_at("decay", i, 60, window)
        assert current < previous, f"not decreasing at {i}: {current} >= {previous}"
        previous = current


def test_both_ends_profile_is_symmetric() -> None:
    num_keys, window = 40, 8
    assert _close(am.weight_at("both_ends", 0, num_keys, window), 1.0)
    assert _close(am.weight_at("both_ends", num_keys - 1, num_keys, window), 1.0)
    assert _close(am.weight_at("both_ends", num_keys // 2, num_keys, window), 0.0)
    for i in range(num_keys):
        mirrored = am.weight_at("both_ends", num_keys - 1 - i, num_keys, window)
        assert _close(am.weight_at("both_ends", i, num_keys, window), mirrored), i


def test_degenerate_window_touches_only_the_anchor() -> None:
    assert _close(am.weight_at("decay", 0, 30, 0), 1.0)
    assert _close(am.weight_at("decay", 1, 30, 0), 0.0)
    assert _close(am.weight_at("both_ends", 29, 30, 0), 1.0)
    assert _close(am.weight_at("both_ends", 15, 30, 0), 0.0)


def test_unknown_profile_raises() -> None:
    try:
        am.weight_at("ease_in_out_quart", 0, 10, 4)
    except ValueError as exc:
        assert "ease_in_out_quart" in str(exc), exc
        return
    raise AssertionError("expected ValueError for an unknown profile")


# --- slerp and delta application (Pass 2) -----------------------------------


def test_slerp_endpoints() -> None:
    a = _axis_angle((0.0, 0.0, 1.0), 0.0)
    b = _axis_angle((0.0, 0.0, 1.0), 90.0)
    assert _close(am.quat_angle_degrees(am.quat_slerp(a, b, 0.0), a), 0.0, tol=1e-4)
    assert _close(am.quat_angle_degrees(am.quat_slerp(a, b, 1.0), b), 0.0, tol=1e-4)


def test_slerp_midpoint_is_half_the_angle() -> None:
    a = _axis_angle((0.0, 0.0, 1.0), 0.0)
    b = _axis_angle((0.0, 0.0, 1.0), 90.0)
    mid = am.quat_slerp(a, b, 0.5)
    assert _close(am.quat_angle_degrees(a, mid), 45.0, tol=1e-4)
    assert _close(am.quat_angle_degrees(mid, b), 45.0, tol=1e-4)


def test_slerp_takes_the_short_way() -> None:
    """A negated endpoint is the same rotation and must not cause a long spin."""
    a = _axis_angle((0.0, 0.0, 1.0), 0.0)
    b = _axis_angle((0.0, 0.0, 1.0), 90.0)
    negated = (-b[0], -b[1], -b[2], -b[3])
    mid = am.quat_slerp(a, negated, 0.5)
    assert _close(am.quat_angle_degrees(a, mid), 45.0, tol=1e-4)


def test_slerp_handles_coincident_inputs() -> None:
    q = _axis_angle((0.2, 0.9, 0.1), 33.0)
    assert _close(am.quat_angle_degrees(am.quat_slerp(q, q, 0.5), q), 0.0, tol=1e-4)


def test_full_weight_reproduces_the_reference_exactly() -> None:
    """The identity the whole re-anchor rests on.

    At the anchor key with weight 1, the corrected rotation must equal the
    reference rotation. If this drifts, every re-anchored clip starts wrong.
    """
    reference = _axis_angle((0.1, 0.7, 0.3), 62.0)
    target = _axis_angle((0.4, 0.2, 0.9), 17.0)
    delta = am.quat_delta(reference, target)
    corrected = am.apply_weighted_delta(delta, target, 1.0)
    assert _close(am.quat_angle_degrees(corrected, reference), 0.0, tol=1e-4)


def test_zero_weight_leaves_the_key_untouched() -> None:
    reference = _axis_angle((0.1, 0.7, 0.3), 62.0)
    target = _axis_angle((0.4, 0.2, 0.9), 17.0)
    delta = am.quat_delta(reference, target)
    corrected = am.apply_weighted_delta(delta, target, 0.0)
    assert _close(am.quat_angle_degrees(corrected, target), 0.0, tol=1e-4)


def test_partial_weight_lands_between() -> None:
    reference = _axis_angle((0.0, 0.0, 1.0), 90.0)
    target = _axis_angle((0.0, 0.0, 1.0), 0.0)
    delta = am.quat_delta(reference, target)
    corrected = am.apply_weighted_delta(delta, target, 0.5)
    assert _close(am.quat_angle_degrees(target, corrected), 45.0, tol=1e-4)


def test_decay_tail_is_bit_identical_to_the_original() -> None:
    """Past the window the clip must be untouched, not merely close.

    A decay re-anchor that nudges the tail would defeat the point of using a
    window at all.
    """
    reference = _axis_angle((0.0, 1.0, 0.0), 40.0)
    keys = [_axis_angle((0.0, 1.0, 0.0), 5.0 * k) for k in range(30)]
    delta = am.quat_delta(reference, keys[0])
    for i in range(12, 30):
        weight = am.weight_at("decay", i, 30, 12)
        assert weight == 0.0, (i, weight)
        corrected = am.apply_weighted_delta(delta, keys[i], weight)
        assert _close(am.quat_angle_degrees(corrected, keys[i]), 0.0, tol=1e-9), i


# --- rotation roughness: the wobble detector ---------------------------------


def test_constant_turn_has_no_rotation_reversal() -> None:
    quats = [_axis_angle((0.0, 0.0, 1.0), 2.0 * k) for k in range(60)]
    assert _close(am.rotation_roughness(quats), 0.0, tol=1e-6), am.rotation_roughness(quats)


def test_alternating_rotation_is_a_full_reversal() -> None:
    """Back-and-forth every frame is the analytic worst case, and scores 1."""
    quats = [_axis_angle((0.0, 0.0, 1.0), 3.0 if k % 2 else 0.0) for k in range(60)]
    assert _close(am.rotation_roughness(quats), 1.0, tol=1e-6), am.rotation_roughness(quats)


def test_slow_sine_rotation_reads_as_smooth() -> None:
    """A gentle authored sway must not be mistaken for a wobble."""
    quats = [_axis_angle((0.0, 0.0, 1.0), 5.0 * math.sin(2 * math.pi * k / 120))
             for k in range(120)]
    assert am.rotation_roughness(quats) < 0.1, am.rotation_roughness(quats)


def test_rotation_roughness_is_amplitude_independent() -> None:
    """A tiny wobble scores the same as a large one; only the shape matters."""
    small = [_axis_angle((0.0, 1.0, 0.0), 0.05 if k % 2 else 0.0) for k in range(40)]
    large = [_axis_angle((0.0, 1.0, 0.0), 12.0 if k % 2 else 0.0) for k in range(40)]
    assert _close(am.rotation_roughness(small), am.rotation_roughness(large), tol=1e-4)


def test_rotation_roughness_handles_static_and_short_tracks() -> None:
    static = [_axis_angle((0.0, 0.0, 1.0), 7.0)] * 40
    assert am.rotation_roughness(static) == 0.0
    assert am.rotation_roughness([]) == 0.0
    assert am.rotation_roughness([_axis_angle((0.0, 0.0, 1.0), 1.0)]) == 0.0


def test_classify_rotation_boundaries() -> None:
    moving = 1.0
    assert am.classify_rotation(0.0, 0.0) == "static"
    assert am.classify_rotation(moving, 0.0) == "smooth_turn"
    assert am.classify_rotation(moving, 0.34) == "smooth_turn"
    assert am.classify_rotation(moving, 0.35) == "unsteady"
    assert am.classify_rotation(moving, 0.69) == "unsteady"
    assert am.classify_rotation(moving, 0.7) == "wobble"
    assert am.classify_rotation(moving, 1.0) == "wobble"


def main() -> int:
    tests = [
        test_linear_ramp_has_zero_roughness,
        test_sawtooth_roughness_is_two,
        test_constant_track_is_static_with_zero_step_mean,
        test_roughness_handles_short_tracks,
        test_classify_boundaries,
        test_smooth_drift_case_matches_idle_shape,
        test_quantized_sway_reads_rougher_than_the_smooth_original,
        test_percentile_interpolates,
        test_percentile_edge_cases,
        test_percentile_does_not_mutate_input,
        test_distribution_empty,
        test_quat_angle_identity_is_zero,
        test_quat_angle_known_separation,
        test_quat_angle_ignores_double_cover,
        test_quat_delta_maps_target_onto_reference,
        test_quat_delta_magnitude_matches_separation,
        test_quat_normalize_handles_zero,
        test_axis_range_empty,
        test_axis_range_basic,
        test_smoothstep_endpoints_and_midpoint,
        test_smoothstep_has_flat_ends,
        test_constant_profile_is_one_everywhere,
        test_decay_profile_shape,
        test_both_ends_profile_is_symmetric,
        test_degenerate_window_touches_only_the_anchor,
        test_unknown_profile_raises,
        test_slerp_endpoints,
        test_slerp_midpoint_is_half_the_angle,
        test_slerp_takes_the_short_way,
        test_slerp_handles_coincident_inputs,
        test_full_weight_reproduces_the_reference_exactly,
        test_zero_weight_leaves_the_key_untouched,
        test_partial_weight_lands_between,
        test_decay_tail_is_bit_identical_to_the_original,
        test_constant_turn_has_no_rotation_reversal,
        test_alternating_rotation_is_a_full_reversal,
        test_slow_sine_rotation_reads_as_smooth,
        test_rotation_roughness_is_amplitude_independent,
        test_rotation_roughness_handles_static_and_short_tracks,
        test_classify_rotation_boundaries,
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
