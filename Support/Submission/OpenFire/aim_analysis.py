"""Screen aim and measured sensor geometry, without estimating distance in m.

The 0x96 aim is already perspective/calibration corrected (1920x1080 plane).
Raw 0x98 blobs use a different, 1024x768 sensor plane. Never mix the two.
"""
from dataclasses import dataclass
from itertools import combinations
import math
from camera_view import CameraFrame, REFERENCE_WIDTH, REFERENCE_HEIGHT
from led_detection import DetectionFrame, RAW_WIDTH, RAW_HEIGHT


@dataclass(frozen=True)
class AimSample:
    detection: DetectionFrame
    camera: CameraFrame

    @property
    def normalized(self):
        x, y = self.camera.points[4]
        return x/REFERENCE_WIDTH, y/REFERENCE_HEIGHT


def _cross(a, b, c):
    return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])


@dataclass(frozen=True)
class ObservationGeometry:
    count: int
    span_x: float = 0.
    span_y: float = 0.
    min_separation: float = 0.
    edge_margin: float = 0.
    valid_quad: bool = False

    @classmethod
    def measure(cls, frame):
        points = frame.points
        if len(points) != 4:
            return cls(len(points))
        ordered = sorted(set(points))
        lower, upper = [], []
        for point in ordered:
            while len(lower) >= 2 and _cross(lower[-2], lower[-1], point) <= 0: lower.pop()
            lower.append(point)
        for point in reversed(ordered):
            while len(upper) >= 2 and _cross(upper[-2], upper[-1], point) <= 0: upper.pop()
            upper.append(point)
        hull = lower[:-1]+upper[:-1]
        valid = len(hull) == 4 and min(abs(_cross(*triangle)) for triangle in combinations(points, 3)) > 1
        xs, ys = [p[0] for p in points], [p[1] for p in points]
        return cls(4, (max(xs)-min(xs))/RAW_WIDTH, (max(ys)-min(ys))/RAW_HEIGHT,
                   min(math.dist(a, b) for a, b in combinations(points, 2)),
                   min(min(xs), RAW_WIDTH-1-max(xs), min(ys), RAW_HEIGHT-1-max(ys)), valid)

    @property
    def notes(self):
        # Advisory UI thresholds, not measured optical/working-distance limits.
        if self.count != 4: return ('LED 네 점이 모두 보여야 조준점을 확인할 수 있습니다.',)
        if not self.valid_quad: return ('점이 겹치거나 배치가 퇴화했습니다. 조준점 미확인',)
        notes = []
        if self.min_separation < 12 or min(self.span_x, self.span_y) < .08:
            notes.append('점 간격이 작습니다. 조준 흔들림을 확인하세요.')
        if self.edge_margin < 20:
            notes.append('점이 시야 경계에 가깝습니다. 거리·방향을 확인하세요.')
        return tuple(notes) or ('네 점이 분리되어 보입니다.',)


def aim_for_frame(frame, sample, *, fresh, sample_fresh, adjustment=None):
    """Only show a paired, current, non-degenerate four-point estimate."""
    if not fresh: return None, '수신 끊김 · 조준 미확인'
    if frame.layout != 0: return None, 'Square 설정 필요 · 조준 미확인'
    geometry = ObservationGeometry.measure(frame)
    if geometry.count != 4: return None, f'조준 미확인 · LED {geometry.count}/4'
    if not geometry.valid_quad: return None, '점 배치 불확실 · 조준 미확인'
    if not sample_fresh or sample is None or sample.detection != frame:
        return None, '보드 조준 데이터 수신 대기'
    point = sample.normalized
    if adjustment is not None: point=adjustment.apply(point)
    if not (0 <= point[0] <= 1 and 0 <= point[1] <= 1):
        direction = ('왼쪽' if point[0] < 0 else '오른쪽' if point[0] > 1 else '') + (' 위' if point[1] < 0 else ' 아래' if point[1] > 1 else '')
        return point, f'화면 밖 · {direction.strip()} · X {point[0]*100:.1f}% · Y {point[1]*100:.1f}%'
    return point, f'X {point[0]*100:.1f}% · Y {point[1]*100:.1f}%'
