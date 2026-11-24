"""Operation Breakpoint's local input protocol and screen calibration math.

Raw Input coordinates use Windows' 0..65535 range, not the firmware's
0..32767 HID descriptor range. No desktop cursor position is sampled.
"""
from dataclasses import dataclass, field
import json
import math
import socket
import uuid

ADDRESS = '/ob/openfire/input'
PORT = 9011
TARGETS = ((.1, .1), (.9, .1), (.9, .9), (.1, .9))
IDENTITY = (1., 0., 0., 0., 1., 0., 0., 0.)


def project(matrix, point):
    if len(matrix) != 8 or not all(math.isfinite(v) for v in (*matrix, *point)):
        raise ValueError('Invalid calibration values')
    a, b, c, d, e, f, g, h = matrix
    x, y = point
    divisor = g*x + h*y + 1
    if abs(divisor) < 1e-8:
        raise ValueError('Calibration has a singular projection')
    result = ((a*x+b*y+c)/divisor, (d*x+e*y+f)/divisor)
    if not all(math.isfinite(v) for v in result):
        raise ValueError('Calibration projection is not finite')
    return result


def fit_calibration(points):
    """Solve four point correspondences; reject crossed or collapsed captures."""
    if len(points) != 4 or any(len(p) != 2 for p in points):
        raise ValueError('Capture four corners in order')
    if not all(math.isfinite(v) and 0 <= v <= 1 for p in points for v in p):
        raise ValueError('Capture points must be normalized coordinates')
    crosses = []
    for i in range(4):
        a, b, c = points[i], points[(i+1)%4], points[(i+2)%4]
        crosses.append((b[0]-a[0])*(c[1]-b[1])-(b[1]-a[1])*(c[0]-b[0]))
    if not (all(c > .002 for c in crosses) or all(c < -.002 for c in crosses)):
        raise ValueError('Corners overlap, cross, or cover too little of the screen')
    rows = []
    for (x, y), (u, v) in zip(points, TARGETS):
        rows.extend(([x, y, 1., 0., 0., 0., -u*x, -u*y, u],
                     [0., 0., 0., x, y, 1., -v*x, -v*y, v]))
    for col in range(8):
        pivot = max(range(col, 8), key=lambda i: abs(rows[i][col]))
        rows[col], rows[pivot] = rows[pivot], rows[col]
        divisor = rows[col][col]
        if abs(divisor) < 1e-9:
            raise ValueError('Calibration points are degenerate')
        rows[col] = [v/divisor for v in rows[col]]
        for row in range(8):
            if row != col:
                factor = rows[row][col]
                rows[row] = [a-factor*b for a, b in zip(rows[row], rows[col])]
    result = tuple(row[8] for row in rows)
    # A pole between the captured corners would make interior aiming unusable.
    divisors = [result[6]*x+result[7]*y+1 for x, y in points]
    if min(divisors)*max(divisors) <= 0:
        raise ValueError('Calibration crosses a projection pole')
    return result


def osc_string(value):
    data = value.encode('utf-8') + b'\0'
    return data + b'\0'*((-len(data)) % 4)


def encode_frame(value):
    return osc_string(ADDRESS) + osc_string(',s') + osc_string(
        json.dumps(value, ensure_ascii=True, separators=(',', ':'), allow_nan=False))


@dataclass
class InputState:
    device: str = ''
    session: str = field(default_factory=lambda: uuid.uuid4().hex)
    sequence: int = 0
    point: tuple | None = None
    buttons: list = field(default_factory=lambda: [False]*5)
    counts: list = field(default_factory=lambda: [0]*3)
    interaction_trigger: bool | None = None
    interaction_trigger_count: int | None = None
    mapping: tuple = (0, 1, 2)
    matrix: tuple = IDENTITY
    connected: bool = False
    enabled: bool = False
    demo: bool = False
    calibrated: bool = False
    output_y_shift: float = 0.
    output_x_shift: float = 0.
    applied_y_shift: float = 0.

    def raw_mouse(self, absolute, x, y, flags):
        if absolute:
            if not (0 <= x <= 65535 and 0 <= y <= 65535):
                return
            self.point = (x/65535., y/65535.)
        # A report may carry a complete down/up tap: retain its press counter.
        for i in range(5):
            if flags & (1 << (2*i)):
                if not self.buttons[i] and self.enabled:
                    for action, button in enumerate(self.mapping):
                        if i == button:
                            self.counts[action] += 1
                self.buttons[i] = True
            if flags & (2 << (2*i)):
                self.buttons[i] = False

    def disconnect(self):
        self.connected = False
        self.point = None
        self.buttons = [False]*5
        if self.interaction_trigger is not None: self.interaction_trigger = False

    def frame(self):
        self.sequence += 1
        point = (.5, .5)
        valid = self.enabled and self.connected and self.point is not None
        if valid:
            try:
                point = project(self.matrix, self.point)
                point = (point[0]+self.output_x_shift,point[1]-self.output_y_shift)
                valid = all(-2 <= p <= 3 for p in point)
            except ValueError:
                valid = False
        return dict(v=1, session=self.session, sequence=self.sequence,
                    device=self.device, x=point[0], y=point[1],aim_y_up=self.output_y_shift+self.applied_y_shift,
                    connected=self.connected and self.enabled, aim_valid=valid,
                    demo=self.demo, calibrated=self.calibrated,
                    trigger=self.enabled and self.buttons[self.mapping[0]],
                    reload=self.enabled and self.buttons[self.mapping[1]],
                    calibrate=self.enabled and self.buttons[self.mapping[2]],
                    trigger_count=self.counts[0], reload_count=self.counts[1],
                    interaction_trigger=self.enabled and (self.buttons[self.mapping[0]] if self.interaction_trigger is None else self.interaction_trigger),
                    interaction_trigger_count=self.counts[0] if self.interaction_trigger_count is None else self.interaction_trigger_count,
                    calibrate_count=self.counts[2])


class Sender:
    def __init__(self, port=PORT):
        if not 1024 <= port <= 65535:
            raise ValueError('Port must be 1024..65535')
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.target = ('127.0.0.1', port)
        self.sent = 0

    def send(self, state):
        value = state.frame()
        self.socket.sendto(encode_frame(value), self.target)
        self.sent += 1
        return value

    def close(self):
        self.socket.close()
