"""Atomic OSC feedback in the same post-mirror camera space as breach detection."""
from __future__ import annotations
import math
from typing import Sequence

ADDRESS = "/mediapipe/breach/guide"
VERSION = 1.0

def make_breach_guide_payload(
    *, frame_size: tuple[int, int], mirrored: bool, mode: int, player_tracked: bool,
    left_roi: Sequence[float], right_roi: Sequence[float],
    left_wrist: Sequence[float] | None, right_wrist: Sequence[float] | None,
    minimum_visibility: float, left_inside: bool, right_inside: bool,
    progress: float, ready: bool,
) -> list[float]:
    width, height = frame_size
    if width <= 0 or height <= 0:
        raise ValueError("Camera dimensions must be positive")
    for roi in (left_roi, right_roi):
        if len(roi) != 4 or not all(math.isfinite(x) and 0 <= x <= 1 for x in roi) or not (roi[0] < roi[2] and roi[1] < roi[3]):
            raise ValueError("Invalid normalized breach ROI")
    active = player_tracked and mode == 1
    def wrist(value):
        if not active or value is None or len(value) != 3 or not all(math.isfinite(x) for x in value) or value[2] < minimum_visibility:
            return [0., 0., 0.]
        return [float(value[0]), float(value[1]), min(max(float(value[2]), 0.), 1.)]
    left, right = wrist(left_wrist), wrist(right_wrist)
    left_inside = active and left[2] > 0 and left_inside
    right_inside = active and right[2] > 0 and right_inside
    progress = min(max(progress, 0.), 1.) if left_inside and right_inside and math.isfinite(progress) else 0.
    ready = bool(ready and left_inside and right_inside)
    # All values are OSC floats, including flags, for one coherent decoder schema.
    return [float(x) for x in (
        VERSION, width, height, mirrored, mode, player_tracked,
        *left_roi, *right_roi, *left, *right, left_inside, right_inside, progress, ready)]
