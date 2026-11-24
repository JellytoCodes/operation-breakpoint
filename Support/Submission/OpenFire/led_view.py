"""Sensor-plane view and fixed installation diagram for measured LED telemetry."""
import math
from led_detection import LED_NAMES, RAW_WIDTH, RAW_HEIGHT
from led_layout import OUTLINE_ORDER, square_targets
from aim_analysis import ObservationGeometry, aim_for_frame

GREEN, YELLOW, GRAY = '#49e89c', '#ffd166', '#8c9aac'
CYAN = '#60dcff'


def _draw_screen_map(canvas, frame, tracker, *, width, height, panel, top, bottom,
                     large, fresh, sample, sample_fresh, display_aspect, demo, adjustment=None):
    px = width-panel+20
    count = len(frame.points) if fresh else None
    prefix = '모의 검출' if demo else '실제 검출'
    canvas.create_text(px, top, anchor='nw', text=f'{prefix} {count} / 4' if fresh else '수신 끊김 · 검출 미확인',
                       fill=GREEN if count == 4 else YELLOW, font=('맑은 고딕', 16 if large else 10, 'bold'), tags='led_count')
    if large:
        mw, my = min(panel-88, 200*display_aspect), top+110
        canvas.create_text(px, my-70, anchor='nw', text='화면에서의 조준점', fill='#e5edf5',
                           font=('맑은 고딕', 13, 'bold'), tags='aim_screen_title')
    else:
        mw, my = min(panel-40, max(28, height-top-bottom-30)*display_aspect), top+28
    mh, mx = mw/display_aspect, width-panel/2-mw/2
    canvas.create_rectangle(mx, my, mx+mw, my+mh, fill='#172b3d', outline='#6e8299', tags='aim_screen')
    canvas.create_line(mx+mw/2, my, mx+mw/2, my+mh, fill='#30465b')
    canvas.create_line(mx, my+mh/2, mx+mw, my+mh/2, fill='#30465b')
    for i, ((dx, dy), name) in enumerate(zip(square_targets(mw, mh), LED_NAMES)):
        lx, ly = mx+dx, my+dy
        known = fresh and tracker.registered and frame.layout == 0
        detected = known and tracker.visible[i]
        color = GREEN if detected else GRAY if known else YELLOW
        state = '검출' if detected else '미검출' if known else '미확인'
        canvas.create_oval(lx-4, ly-4, lx+4, ly+4, fill=color, outline=color,
                           tags=('led_indicator', f'led_indicator_{i}'))
        label_y = ly-12 if large and i < 2 else ly+12 if large else ly+6 if i < 2 else ly-6
        anchor = ('s' if i < 2 else 'n') if large else ('n' if i < 2 else 's')
        canvas.create_text(lx, label_y, anchor=anchor, text=str(i+1), fill=color,
                           font=('맑은 고딕', 11 if large else 7), tags=f'led_number_{i}')
        if large:
            canvas.create_text(px, my+mh+115+i*30, anchor='nw', text=f'{name} · {state}', fill=color,
                               font=('맑은 고딕', 12), tags=f'led_state_{i}')
    aim, status = aim_for_frame(frame, sample, fresh=fresh, sample_fresh=sample_fresh,adjustment=adjustment)
    if aim is not None and 0 <= aim[0] <= 1 and 0 <= aim[1] <= 1:
        ax, ay = mx+aim[0]*mw, my+aim[1]*mh
        radius = 10 if large else 4
        canvas.create_line(ax-radius, ay, ax+radius, ay, fill=CYAN, width=2, tags='screen_aim_cross')
        canvas.create_line(ax, ay-radius, ax, ay+radius, fill=CYAN, width=2, tags='screen_aim_cross')
        canvas.create_oval(ax-radius/2, ay-radius/2, ax+radius/2, ay+radius/2, outline=CYAN,
                           width=2, tags='screen_aim')
    elif aim is not None:
        # A direction indicator at the border is not an in-screen aim point.
        dx, dy = (aim[0]-.5)*mw, (aim[1]-.5)*mh
        inset = min(10, mw*.1, mh*.1)
        factor = min((mw/2-inset)/abs(dx) if dx else float('inf'),
                     (mh/2-inset)/abs(dy) if dy else float('inf'))
        ex, ey = mx+mw/2+dx*factor, my+mh/2+dy*factor
        norm = math.hypot(dx, dy)
        length = min(28 if large else 14, min(mw, mh)*.25)
        canvas.create_line(ex-dx/norm*length, ey-dy/norm*length, ex, ey,
                           fill=CYAN, width=3, arrow='last', tags='offscreen_aim')
        canvas.create_text(mx+mw/2, my+mh/2, text='화면 밖', fill=CYAN,
                           font=('맑은 고딕', 11 if large else 7), tags='offscreen_label')
    elif large:
        canvas.create_text(mx+mw/2, my+mh/2, text=status, fill=YELLOW,
                           font=('맑은 고딕', 11), width=max(80, mw-20), justify='center', tags='aim_unavailable')
    if large:
        canvas.create_text(px, my+mh+43, anchor='nw', text=('모의 조준 · ' if demo else '보드 보정 조준점 · ')+status,
                           fill=CYAN if aim is not None else YELLOW, font=('맑은 고딕', 11),
                           width=panel-35, tags='aim_status')
        geometry = ObservationGeometry.measure(frame)
        if adjustment is not None:
            canvas.create_text(px,my+mh+235,anchor='nw',text=f'Y 위쪽 {adjustment.total_mm:+.1f} mm · 미세 {adjustment.trim_mm:+.1f}',
                               fill=CYAN,font=('맑은 고딕',10),width=panel-35,tags='y_adjustment')
        gy = my+mh+263
        canvas.create_text(px, gy, anchor='nw', text='LED 간격 / 시야 여유', fill='#e5edf5',
                           font=('맑은 고딕', 12, 'bold'), tags='geometry_title')
        if fresh and geometry.count == 4:
            lines = (f'센서 영역: 가로 {geometry.span_x*100:.1f}% · 세로 {geometry.span_y*100:.1f}%',
                     f'최소 점 간격: {geometry.min_separation:.1f} px', f'시야 끝 여유: {geometry.edge_margin:.1f} px')
            for i, line in enumerate(lines):
                canvas.create_text(px, gy+36+i*27, anchor='nw', text=line, fill='#bac9dc',
                                   font=('맑은 고딕', 10), tags='geometry_metric')
            text = '\n'.join(geometry.notes)
        else:
            text = '네 점이 모두 검출되면\n센서상의 간격과 여유를 표시합니다.'
        canvas.create_text(px, gy+124, anchor='nw', text=text, fill=YELLOW,
                           font=('맑은 고딕', 10), width=panel-35, tags='geometry_note')


def draw_detection_scene(canvas, frame, tracker, *, fresh, fullscreen=False,
                         demo=False, subtitle='', sample=None, sample_fresh=False, display_aspect=16/9, adjustment=None):
    canvas.delete('all')
    width, height = max(1, canvas.winfo_width()), max(1, canvas.winfo_height())
    large = fullscreen and height >= 1000
    panel = 440 if large else 270
    top, bottom = (115, 150) if large else (52, 54)
    scale = max(.01, min((width-panel-48)/RAW_WIDTH, (height-top-bottom)/RAW_HEIGHT))
    pw, ph = RAW_WIDTH*scale, RAW_HEIGHT*scale
    x, y = 16+(width-panel-32-pw)/2, top+(height-top-bottom-ph)/2
    canvas.create_rectangle(x, y, x+pw, y+ph, fill='#10103a', outline='#43556b', tags='camera_plot')
    canvas.create_line(x+pw/2, y, x+pw/2, y+ph, fill='#24244b')
    canvas.create_line(x, y+ph/2, x+pw, y+ph/2, fill='#24244b')
    title = '모의 LED 검출 · 실물 데이터가 아닙니다' if demo else 'IR LED 검출 · 카메라 센서 좌표'
    canvas.create_text(16, 12, anchor='nw', text=title, fill=YELLOW if demo else '#e5edf5',
                       font=('맑은 고딕', 18 if large else 10, 'bold'), tags='camera_title')
    if large:
        canvas.create_text(16, 50, anchor='nw', text=subtitle, fill='#bac9dc', font=('맑은 고딕', 11))
        canvas.create_text(x, y-22, anchor='w', text='1024 × 768 센서 평면 · 회색 십자: 센서 중심(고정)',
                           fill='#bac9dc', font=('맑은 고딕', 10))
    cx, cy = x+pw/2, y+ph/2
    arm = 14 if large else 5
    canvas.create_line(cx-arm, cy, cx+arm, cy, fill='#d2d9e4', width=2, tags='camera_axis')
    canvas.create_line(cx, cy-arm, cx, cy+arm, fill='#d2d9e4', width=2, tags='camera_axis')
    _draw_screen_map(canvas, frame, tracker, width=width, height=height, panel=panel,
                     top=top, bottom=bottom, large=large, fresh=fresh, sample=sample,
                     sample_fresh=sample_fresh, display_aspect=display_aspect, demo=demo,adjustment=adjustment)
    if fresh:
        def draw(point, color, label, tags, lost=False):
            sx, sy = x+point[0]*scale, y+point[1]*scale
            radius = 13 if large else 4
            canvas.create_oval(sx-radius, sy-radius, sx+radius, sy+radius, outline=color,
                               width=3 if large else 2, dash=(4, 3) if lost else (), tags=tags)
            if large and label:
                anchor = 'e' if sx > x+pw*.8 else 'w'
                tx = sx-radius-8 if anchor == 'e' else sx+radius+8
                canvas.create_text(tx, max(y+12, min(y+ph-12, sy)), text=label, anchor=anchor,
                                   fill=color, font=('맑은 고딕', 12), tags='blob_label')
        if tracker.registered and frame.layout == 0:
            # Physical row-major IDs need perimeter order 1 -> 2 -> 4 -> 3.
            if all(tracker.visible):
                polygon = [tracker.positions[i] for i in OUTLINE_ORDER]
                for a, b in zip(polygon, polygon[1:]):
                    canvas.create_line(x+a[0]*scale, y+a[1]*scale, x+b[0]*scale, y+b[1]*scale,
                                       fill='#43556b', width=2, tags='registered_outline')
            for i, point in enumerate(tracker.positions):
                detected = tracker.visible[i]
                draw(point, GREEN if detected else GRAY,
                     LED_NAMES[i] + (' · 검출' if detected else ' · 미검출, 이전 위치'),
                     ('detected_point' if detected else 'lost_point', f'led_point_{i}'), not detected)
        else:
            for point in frame.points:
                draw(point, YELLOW, '', ('detected_point', 'unassigned_point'))
        geometry = ObservationGeometry.measure(frame)
        if geometry.count == 4 and geometry.valid_quad:
            xs, ys = [p[0] for p in frame.points], [p[1] for p in frame.points]
            canvas.create_rectangle(x+min(xs)*scale, y+min(ys)*scale, x+max(xs)*scale, y+max(ys)*scale,
                                    outline='#526d85', dash=(5, 5), tags='sensor_span')
    else:
        canvas.create_text(x+pw/2, y+ph/2, text='LED 검출 데이터 수신 대기', fill='#e5edf5',
                           font=('맑은 고딕', 18 if large else 10), tags='camera_waiting')
    message = tracker.message if fresh else '수신이 끊겼습니다. 이전 검출 상태를 현재 상태로 표시하지 않습니다.'
    if demo:
        message = '모의 LED 배치 표시 · 실물 검출·물리 번호 등록은 연결된 보드에서 확인하세요.'
    canvas.create_text(16, height-bottom+18, anchor='nw', text=message, width=width-32,
                       fill=YELLOW if tracker.registering or not tracker.registered else '#e5edf5',
                       font=('맑은 고딕', 15 if large else 9), tags='registration_prompt')
    if large:
        layout = ('보드 배치가 Diamond입니다. 상단 2개·하단 2개 설치에는 Square 설정이 필요합니다.' if frame.layout else
                  '보드 배치: Square · 상단 2개 / 하단 2개')
        canvas.create_text(16, height-75, anchor='nw', text=layout,
                           fill=YELLOW if frame.layout else '#bac9dc', font=('맑은 고딕', 11))
        canvas.create_text(16, height-42, anchor='nw',
                           text='청록 십자: 조준점 / 화살표: 화면 밖 · G: 설치 가이드 · R: 위치 번호(선택) · Tab: 원본 · Esc: 종료',
                           fill='#bac9dc', font=('맑은 고딕', 12))
