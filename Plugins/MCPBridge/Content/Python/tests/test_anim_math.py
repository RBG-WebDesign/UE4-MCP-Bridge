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
