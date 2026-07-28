"""Pure math helpers for animation pose analysis.

No unreal imports anywhere in this module. Every function takes plain Python
numbers, tuples, and lists, so the whole thing is testable without an editor.
The handlers in handlers/animation.py convert unreal types into these shapes
before calling in.

Quaternions are (x, y, z, w) tuples. Vectors are (x, y, z) tuples.

Weight profiles for re-anchoring are deliberately not here yet; they land in
Pass 2 alongside the dry-run path that uses them.
"""

import math
from typing import Dict, List, Sequence, Tuple

Quat = Tuple[float, float, float, float]
Vec3 = Tuple[float, float, float]

# Below this mean step magnitude a track is treated as static rather than
# classified, because roughness is meaningless when there is no motion to
# divide by. Units are centimetres per frame.
STATIC_STEP_EPSILON = 1e-5

# Roughness boundaries. Empirical, not derived. See the design spec: these are
# a ranking signal until calibrated against a clip known to look correct.
ROUGHNESS_SMOOTH = 0.5
ROUGHNESS_STEPPED = 1.0


# --- vector helpers ---------------------------------------------------------


def vec_sub(a: Vec3, b: Vec3) -> Vec3:
    """Component-wise a - b."""
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vec_length(v: Vec3) -> float:
    """Euclidean magnitude."""
    return math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])


def axis_range(points: Sequence[Vec3]) -> Dict[str, float]:
    """Per-axis max minus min over a list of positions.

    Returns zeros for an empty input rather than raising, so a track with no
    position keys reports as motionless instead of failing the whole analysis.
    """
    if not points:
        return {"x": 0.0, "y": 0.0, "z": 0.0}
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    zs = [p[2] for p in points]
    return {
        "x": max(xs) - min(xs),
        "y": max(ys) - min(ys),
        "z": max(zs) - min(zs),
    }


# --- quaternion helpers -----------------------------------------------------


def quat_normalize(q: Quat) -> Quat:
    """Return q scaled to unit length. Returns identity for a zero quat."""
    n = math.sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3])
    if n == 0.0:
        return (0.0, 0.0, 0.0, 1.0)
    return (q[0] / n, q[1] / n, q[2] / n, q[3] / n)


def quat_inverse(q: Quat) -> Quat:
    """Inverse of a rotation quaternion (conjugate of the normalized value)."""
    x, y, z, w = quat_normalize(q)
    return (-x, -y, -z, w)


def quat_multiply(a: Quat, b: Quat) -> Quat:
    """Hamilton product a * b, i.e. apply b then a."""
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    )


def quat_angle_degrees(a: Quat, b: Quat) -> float:
    """Shortest angle between two rotations, in degrees.

    Uses the absolute dot product so that q and -q (which represent the same
    rotation) compare as identical rather than 180 degrees apart.
    """
    an = quat_normalize(a)
    bn = quat_normalize(b)
    dot = abs(an[0] * bn[0] + an[1] * bn[1] + an[2] * bn[2] + an[3] * bn[3])
    dot = max(-1.0, min(1.0, dot))
    return math.degrees(2.0 * math.acos(dot))


def quat_delta(q_reference: Quat, q_target: Quat) -> Quat:
    """Rotation that takes q_target onto q_reference.

    This is the per-bone correction the re-anchor pass applies. Pass 1 only
    reports its magnitude via quat_angle_degrees.
    """
    return quat_multiply(quat_normalize(q_reference), quat_inverse(q_target))


# --- statistics -------------------------------------------------------------


def percentile(values: Sequence[float], fraction: float) -> float:
    """Linear-interpolated percentile. fraction is 0.0 to 1.0.

    Returns 0.0 for empty input. Does not mutate the caller's list.
    """
    if not values:
        return 0.0
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    pos = (len(ordered) - 1) * fraction
    low = math.floor(pos)
    high = math.ceil(pos)
    if low == high:
        return ordered[int(pos)]
    return ordered[low] * (high - pos) + ordered[high] * (pos - low)


def distribution(values: Sequence[float]) -> Dict[str, float]:
    """Mean, max, and 95th percentile of a magnitude list."""
    if not values:
        return {"mean": 0.0, "max": 0.0, "p95": 0.0}
    return {
        "mean": sum(values) / len(values),
        "max": max(values),
        "p95": percentile(values, 0.95),
    }


def position_steps(points: Sequence[Vec3]) -> List[float]:
    """First-difference magnitudes between consecutive positions."""
    return [vec_length(vec_sub(points[i + 1], points[i])) for i in range(len(points) - 1)]


def rotation_steps(quats: Sequence[Quat]) -> List[float]:
    """Angle in degrees between consecutive rotations."""
    return [quat_angle_degrees(quats[i], quats[i + 1]) for i in range(len(quats) - 1)]


def position_curvature(points: Sequence[Vec3]) -> List[float]:
    """Second-difference magnitudes: |P[k+1] - 2*P[k] + P[k-1]|."""
    out: List[float] = []
    for i in range(1, len(points) - 1):
        prev, cur, nxt = points[i - 1], points[i], points[i + 1]
        out.append(vec_length((
            nxt[0] - 2.0 * cur[0] + prev[0],
            nxt[1] - 2.0 * cur[1] + prev[1],
            nxt[2] - 2.0 * cur[2] + prev[2],
        )))
    return out


def roughness(points: Sequence[Vec3]) -> float:
    """mean(second differences) / mean(first differences). Dimensionless.

    Near 0 means consecutive steps point the same way, i.e. smooth authored
    sway that reads as sliding. Near or above 1 means direction reverses most
    frames, i.e. quantization stepping or noise that reads as jiggle.

    Returns 0.0 when there is no motion to divide by, which callers should
    pair with the step mean to distinguish "smooth" from "static".
    """
    steps = position_steps(points)
    if not steps:
        return 0.0
    mean_step = sum(steps) / len(steps)
    if mean_step < STATIC_STEP_EPSILON:
        return 0.0
    curves = position_curvature(points)
    if not curves:
        return 0.0
    return (sum(curves) / len(curves)) / mean_step


def classify(step_mean: float, roughness_value: float) -> str:
    """Summarize a track as static, smooth_drift, stepped, or noisy.

    This restates the two numbers passed in; it is not an independent
    measurement. Thresholds are empirical and expected to be tuned.
    """
    if step_mean < STATIC_STEP_EPSILON:
        return "static"
    if roughness_value < ROUGHNESS_SMOOTH:
        return "smooth_drift"
    if roughness_value < ROUGHNESS_STEPPED:
        return "stepped"
    return "noisy"
