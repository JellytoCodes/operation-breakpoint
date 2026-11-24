"""Measured IR blobs and explicitly registered physical LED identities.

The sensor's four slots have no persistent identity. Physical IDs are learned by
covering top-left/top-right/bottom-left/bottom-right, then gated assignment.
An ambiguous match or complete loss discards the reference rather than rotating
the labels to fit a symmetric array. This module never generates gameplay input.
"""
from dataclasses import dataclass
from itertools import permutations
import math
import struct
from led_layout import LED_NAMES

RAW_WIDTH, RAW_HEIGHT = 1024, 768
PACKET_SIZE = 31


def crc16(data):
    value = 0xffff
    for byte in data:
        value ^= byte << 8
        for _ in range(8):
            value = ((value << 1) ^ (0x1021 if value & 0x8000 else 0)) & 0xffff
    return value


@dataclass(frozen=True)
class DetectionFrame:
    sequence: int
    seen: int
    layout: int
    slots: tuple

    def __post_init__(self):
        if not 0 <= self.sequence <= 0xffffffff or not 0 <= self.seen <= 15 or self.layout not in (0, 1):
            raise ValueError('Invalid IR detection header')
        if len(self.slots) != 4 or any(len(p) != 2 or any(type(v) is not int or not 0 <= v <= 1023 for v in p) for p in self.slots):
            raise ValueError('Invalid IR blob coordinates')
        if any(y >= RAW_HEIGHT for _, y in self.points):
            raise ValueError('Visible IR blob is outside the sensor plane')

    @property
    def points(self):
        # Missing slots retain old sensor values. Never draw or match them.
        return tuple(p for i, p in enumerate(self.slots) if self.seen & (1 << i))

    @classmethod
    def decode(cls, packet):
        if len(packet) != PACKET_SIZE or packet[:7] != b'\x98OBIR\x01\x16':
            raise ValueError('Unknown IR detection packet')
        if crc16(packet[1:-2]) != int.from_bytes(packet[-2:], 'little'):
            raise ValueError('IR detection checksum mismatch')
        seq, seen, layout, *values = struct.unpack('<IBB8H', packet[7:-2])
        return cls(seq, seen, layout, tuple(zip(values[::2], values[1::2])))


def assignments(points, reference, gate):
    """All injective matches; sensor slot order never enters identity selection."""
    candidates = []
    for indices in permutations(range(len(reference)), len(points)):
        distances = [math.dist(p, reference[i]) for p, i in zip(points, indices)]
        if all(d <= gate for d in distances):
            candidates.append((sum(d*d for d in distances), indices))
    return sorted(candidates)


def unique_match(points, reference, gate):
    candidates = assignments(points, reference, gate)
    if not candidates or (len(candidates) > 1 and candidates[1][0]-candidates[0][0] < 36*len(points)):
        return None
    return candidates[0][1]


class LEDTracker:
    HOLD = .75
    STABLE_FRAMES = 5  # ~250 ms at the firmware's diagnostic rate

    def __init__(self):
        self.reset()

    def reset(self, reason='상단 2개·하단 2개 배치 · 개별 LED 위치 번호 확인은 선택 기능입니다.'):
        self.positions = None
        self.last_seen = [0.]*4
        self.visible = [False]*4
        self.unassigned = ()
        self.registering = False
        self.baseline = None
        self.mapping = []
        self.restoring = False
        self.stable = 0
        self.candidate = None
        self.last_time = None
        self.message = reason

    @property
    def registered(self):
        return self.positions is not None

    def begin(self, frame, now):
        if frame.layout != 0:
            self.message = '상단 2개·하단 2개 설치에는 보드의 Square 설정이 필요합니다.'
            return False
        points = frame.points
        if len(points) != 4 or min(math.dist(a, b) for a, b in permutations(points, 2)) < 45:
            self.message = 'LED 네 점이 서로 떨어져 모두 검출될 때 등록하세요.'
            return False
        self.reset()
        self.registering = True
        self.baseline = points
        self.last_time = now
        self._prompt()
        return True

    def _prompt(self):
        if self.restoring:
            self.message = '가린 LED를 다시 보이게 하세요. 카메라는 고정해 주세요.'
        else:
            self.message = f'{LED_NAMES[len(self.mapping)]} LED만 잠깐 가리세요. 카메라는 고정해 주세요.'

    def _registration(self, points, now):
        match = unique_match(points, self.baseline, 22) if points else None
        if match is None:
            self.stable = 0
            self.message = '카메라를 등록 시작 위치에 고정하고 LED를 한 개씩만 가려주세요.'
            return
        if self.restoring:
            if len(points) == 4:
                self.stable += 1
                if self.stable >= self.STABLE_FRAMES:
                    self.restoring, self.stable = False, 0
                    if len(self.mapping) == 4:
                        ordered = dict(zip(match, points))
                        self.positions = [ordered[i] for i in self.mapping]
                        self.last_seen = [now]*4
                        self.visible = [True]*4
                        self.registering = False
                        self.message = '위치 번호 확인 완료 · 1 상단 왼쪽 → 2 상단 오른쪽 → 3 하단 왼쪽 → 4 하단 오른쪽'
                        return
                    self._prompt()
            else:
                self.stable = 0
            return
        if len(points) != 3:
            self.stable = 0
            self._prompt()
            return
        missing = next(iter(set(range(4))-set(match)))
        if missing in self.mapping:
            self.stable = 0
            self.message = '이미 등록한 LED입니다. 안내된 설치 위치의 LED를 가려주세요.'
            return
        self.stable = self.stable+1 if missing == self.candidate else 1
        self.candidate = missing
        if self.stable >= self.STABLE_FRAMES:
            self.mapping.append(missing)
            self.restoring, self.stable = True, 0
            self._prompt()

    def update(self, frame, now):
        points = frame.points
        if self.last_time is not None and (now-self.last_time > self.HOLD or now <= self.last_time):
            self.reset('수신이 끊겨 기준점을 해제했습니다. R로 다시 등록하세요.')
        self.last_time = now
        self.unassigned = points
        self.visible = [False]*4
        if self.registering:
            self._registration(points, now)
            return
        if not self.registered:
            return
        if not points:
            self.reset('LED 전체 유실 · 번호 오인을 막기 위해 R로 기준점을 다시 등록하세요.')
            return
        separation = min(math.dist(a, b) for a, b in permutations(self.positions, 2))
        eligible = [i for i in range(4) if now-self.last_seen[i] <= self.HOLD]
        reference = [self.positions[i] for i in eligible]
        match = unique_match(points, reference, min(80, separation*.32))
        if match is None:
            self.reset('점 이동 또는 대응이 불확실합니다. R로 기준점을 다시 등록하세요.')
            self.unassigned = points
            return
        for point, index in zip(points, match):
            led = eligible[index]
            self.positions[led] = point
            self.last_seen[led] = now
            self.visible[led] = True
        self.unassigned = ()
        self.message = '설치 위치 기준으로 추적 중 · 초록 검출 / 회색 미검출'


def demo_detection(elapsed):
    dx, dy = int(65*math.sin(elapsed*.65)), int(35*math.sin(elapsed*.45))
    slots = ((290+dx, 150+dy), (730+dx, 150+dy), (290+dx, 610+dy), (730+dx, 610+dy))
    return DetectionFrame(int(elapsed*20), 15, 0, slots)
