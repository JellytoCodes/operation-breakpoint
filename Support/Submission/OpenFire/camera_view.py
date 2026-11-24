"""OpenFIRE's six-point diagnostic scene, rendered with native Tk canvas items.

Protocol/point correspondence inspected at OpenFIRE-App d0f3334:
four LED positions (TL, TR, BL, BR), mapped gun aim, and array median.
The firmware has already ordered/extrapolated the points. Never infer a visible
LED count or a physical XYZ camera pose from this packet.
"""
from dataclasses import dataclass
import math

REFERENCE_WIDTH, REFERENCE_HEIGHT = 1920., 1080.
CAMERA_TIMEOUT = .75
COLORS = ('#00ff00', '#00ff00', '#00ffff', '#00ffff', '#a0a0a0', '#ff4545')
LABELS = ('보정점 A', '보정점 B', '보정점 C', '보정점 D', '카메라 조준', '배열 중심')


@dataclass(frozen=True)
class CameraFrame:
    coords: tuple

    def __post_init__(self):
        if len(self.coords) != 12 or any(type(v) is not int or not -(2**31) <= v < 2**31 for v in self.coords):
            raise ValueError('Camera frame must contain twelve signed int32 coordinates')

    @property
    def points(self):
        return tuple(zip(self.coords[::2], self.coords[1::2]))

    @property
    def polygon(self):
        # Packet order is TL, TR, BL, BR. Drawing 0,1,2,3 would cross the box.
        points = self.points
        return tuple(points[i] for i in (0, 1, 3, 2, 0))


@dataclass(frozen=True)
class CameraProjection:
    width: float
    height: float
    fullscreen: bool = False

    @property
    def rect(self):
        if self.fullscreen:
            # Match OpenFIREApp: scale 1920x1080 independently to the display.
            return 0., 0., self.width, self.height
        scale = min(self.width/REFERENCE_WIDTH, self.height/REFERENCE_HEIGHT)
        w, h = REFERENCE_WIDTH*scale, REFERENCE_HEIGHT*scale
        return (self.width-w)/2, (self.height-h)/2, w, h

    def point(self, point):
        x, y, w, h = self.rect
        return x + point[0]/REFERENCE_WIDTH*w, y + point[1]/REFERENCE_HEIGHT*h

    def clipped_line(self, first, second):
        """Clip extrapolated positions to the camera plot without clamping data."""
        x0, y0 = first
        dx, dy = second[0]-x0, second[1]-y0
        lower, upper = 0., 1.
        for p, q in ((-dx, x0), (dx, REFERENCE_WIDTH-x0),
                     (-dy, y0), (dy, REFERENCE_HEIGHT-y0)):
            if p == 0:
                if q < 0:
                    return None
                continue
            ratio = q/p
            if p < 0:
                lower = max(lower, ratio)
            else:
                upper = min(upper, ratio)
            if lower > upper:
                return None
        return (*self.point((x0+lower*dx, y0+lower*dy)),
                *self.point((x0+upper*dx, y0+upper*dy)))


def draw_camera_scene(canvas, frame=None, *, fresh=False, fullscreen=False,
                      demo=False, subtitle='', status='카메라 좌표 수신 대기', detection_available=False):
    """Draw the same six coordinates/colors/order as the reference App.

    Tags are used by GUI regression checks and ensure stale data is never drawn
    as a current camera scene. This function never invents missing coordinates.
    """
    canvas.delete('all')
    width, height = max(1, canvas.winfo_width()), max(1, canvas.winfo_height())
    projection = CameraProjection(width, height, fullscreen)
    x, y, w, h = projection.rect
    canvas.create_rectangle(x, y, x+w, y+h, fill='#10103a', outline='#43556b', tags='camera_plot')
    canvas.create_line(x+w/2, y, x+w/2, y+h, fill='#24244b', tags='camera_grid')
    canvas.create_line(x, y+h/2, x+w, y+h/2, fill='#24244b', tags='camera_grid')
    large = fullscreen
    title = '모의 IR 화면 · 실물 데이터가 아닙니다' if demo else 'IR 카메라 점검'
    canvas.create_text(16, 16, anchor='nw', text=title, fill='#f1d16b' if demo else '#e5edf5',
                       font=('맑은 고딕', 18 if large else 11, 'bold'), tags='camera_title')
    if subtitle:
        canvas.create_text(16, 52 if large else 40, anchor='nw', text=subtitle,
                           fill='#bac9dc', font=('맑은 고딕', 12 if large else 9), tags='camera_subtitle')
    if frame is not None and fresh:
        polygon = frame.polygon
        for i, (first, second) in enumerate(zip(polygon, polygon[1:])):
            clipped = projection.clipped_line(first, second)
            if clipped is not None:
                canvas.create_line(*clipped, fill='#a0a0a0', width=2,
                                   tags=('camera_box', f'camera_edge_{i}'))
        # The source App uses radius 25 in the 1920x1080 reference scene.
        rx, ry = max(3., 25*w/REFERENCE_WIDTH), max(3., 25*h/REFERENCE_HEIGHT)
        for i, point in enumerate(frame.points):
            if not (0 <= point[0] <= REFERENCE_WIDTH and 0 <= point[1] <= REFERENCE_HEIGHT):
                continue  # Keep the actual off-screen value in the frame, never pin a dot to an edge.
            px, py = projection.point(point)
            canvas.create_oval(px-rx, py-ry, px+rx, py+ry, outline=COLORS[i], width=3,
                               tags=('camera_point', f'camera_point_{i}'))
            if large:
                label_x, label_y, anchor = px+rx+9, py, 'w'
                if px > width*.85:
                    label_x, anchor = px-rx-9, 'e'
                if py < 45:
                    label_y = py+ry+24
                elif py > height-125:
                    label_y = py-ry-24
                # Aim and median frequently coincide. Keep their actual circle
                # centers unchanged, while placing the two labels apart.
                if i == 4:
                    label_x, label_y, anchor = px-rx-9, max(45, py-ry-20), 'e'
                elif i == 5:
                    label_x, label_y, anchor = px+rx+9, min(height-140, py+ry+20), 'w'
                canvas.create_text(label_x, label_y, anchor=anchor, text=LABELS[i], fill=COLORS[i],
                                   font=('맑은 고딕', 12), tags=f'camera_label_{i}')
    else:
        canvas.create_text(width/2, height/2, text=status, fill='#e5edf5',
                           font=('맑은 고딕', 20 if large else 12),
                           width=max(120, width-40), justify='center', tags='camera_waiting')
    if large:
        canvas.create_text(width/2, height-115,
                           text=('보정점 A~D에는 추정 위치가 포함됩니다. 실제 LED 번호와 검출 상태는 Tab으로 확인하세요.' if detection_available else
                                 '원본 펌웨어: 검출 확인 불가 · 보정점 A~D는 실제 LED 번호가 아닙니다. 검출 확장 펌웨어가 필요합니다.'),
                           fill='#bac9dc', font=('맑은 고딕', 12), tags='camera_note')
        canvas.create_text(width/2, height-78, text='초록·청록: 추정 포함 보정점    회색: 카메라 조준점    빨강: IR 배열 중심',
                           fill='#e5edf5', font=('맑은 고딕', 14), tags='camera_legend')
        canvas.create_text(width/2, height-40,
                           text='점은 조준 반대 방향으로 움직입니다. 회색 원을 조준선과 비교하세요.   Esc: 점검 종료',
                           fill='#bac9dc', font=('맑은 고딕', 12), tags='camera_instructions')


def demo_frame(elapsed):
    """Explicit synthetic scene, only for UI preview/tests, never game output."""
    dx, dy = int(230*math.sin(elapsed*.65)), int(115*math.sin(elapsed*.45))
    points = ((420+dx, 240+dy), (1500+dx, 240+dy),
              (420+dx, 840+dy), (1500+dx, 840+dy),
              (960-dx, 540-dy), (960+dx, 540+dy))
    return CameraFrame(tuple(v for point in points for v in point))
