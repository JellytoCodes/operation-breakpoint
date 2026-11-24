"""Operation Breakpoint IR Controller: native Windows device monitor and bridge."""
import argparse
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import queue
import sys
import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox
import uuid

from diagnostics import run_diagnostics
from protocol import InputState, Sender, IDENTITY, PORT, TARGETS, fit_calibration, project
from rawinput import RawInput
from camera_view import CameraFrame, CAMERA_TIMEOUT, draw_camera_scene, demo_frame
from led_detection import LEDTracker, demo_detection
from led_view import draw_detection_scene
from led_layout import draw_alignment_guide
from aim_analysis import AimSample
from aim_adjustment import load_adjustment

BG, PANEL, TEXT, MUTED, ACCENT = '#101923', '#192736', '#e5edf5', '#9baec0', '#49d6b0'
TOOL_VERSION = '2026.11.05.2'


def config_path():
    return Path(os.environ.get('LOCALAPPDATA', str(Path.home()))) / 'OperationBreakpoint' / 'OpenFire' / 'settings.json'


def monitors():
    from ctypes import wintypes as w
    found = []
    callback_type = ctypes.WINFUNCTYPE(w.BOOL, w.HANDLE, w.HDC, ctypes.POINTER(w.RECT), w.LPARAM)
    def receive(_monitor, _dc, rect, _data):
        r = rect.contents
        found.append((r.left, r.top, r.right-r.left, r.bottom-r.top))
        return True
    callback = callback_type(receive)
    user = ctypes.WinDLL('user32')
    user.EnumDisplayMonitors.argtypes = [w.HDC, ctypes.POINTER(w.RECT), callback_type, w.LPARAM]
    if not user.EnumDisplayMonitors(None, None, callback, 0):
        raise ctypes.WinError()
    return found


from device_settings import migrate_section, save_section


class App:
    def __init__(self, root, port=PORT, settings=None):
        self.root = root
        self.path = Path(settings) if settings else config_path()
        self.config = {}
        try:
            self.config = json.loads(self.path.read_text(encoding='utf-8'))
            if not isinstance(self.config, dict):
                self.config = {}
        except (OSError, ValueError):
            pass
        if settings is None:
            self.config = migrate_section('GunProfiles', self.config)
        self.shared_settings = settings is None
        self.state = InputState()
        self.adjustment=load_adjustment()
        self.sender = Sender(port)
        self.selected = None
        self.devices = []
        self.reports = 0
        self.last_absolute = 0.
        self.last_rate = time.monotonic()
        self.last_reports = 0
        self.cal_window = None
        self.cal_points = []
        self.cal_matrix = None
        self.diagnostic_stop = threading.Event()
        self.diagnostic_thread = None
        self.messages = queue.SimpleQueue()
        self.diagnostic_active = False
        self.camera_window = None
        self.alignment_window = None
        self.alignment_restore_camera = False
        self.camera_frame = None
        self.camera_received = 0.
        self.camera_frames = 0
        self.camera_started = 0.
        self.camera_identity = ''
        self.detection_frame = None
        self.detection_received = 0.
        self.detection_sequence = None
        self.aim_sample = None
        self.aim_received = 0.
        self.led_tracker = LEDTracker()
        self.camera_aim_view = False
        self.camera_demo = False
        self.camera_demo_started = 0.
        self.closed = False
        self.root.title(f'Operation Breakpoint · IR Controller [{TOOL_VERSION}]')
        self.root.geometry('940x760')
        self.root.minsize(820, 680)
        self.root.configure(bg=BG)
        style = ttk.Style()
        style.theme_use('clam')
        style.configure('.', background=BG, foreground=TEXT, font=('맑은 고딕', 10))
        style.configure('TButton', background=PANEL, foreground=TEXT, padding=(12, 8))
        style.map('TButton', background=[('active', '#2c4358')])
        style.configure('TCombobox', fieldbackground=PANEL, background=PANEL, foreground=TEXT, padding=5)
        style.map('TCombobox', fieldbackground=[('readonly', PANEL)], foreground=[('readonly', TEXT)])
        body = ttk.Frame(root, padding=24)
        body.pack(fill='both', expand=True)
        ttk.Label(body, text='IR CONTROLLER · 펌웨어 6.1 / 6.2', foreground=ACCENT, font=('Segoe UI', 12, 'bold')).pack(anchor='w')
        ttk.Label(body, text='Operation Breakpoint', font=('Segoe UI', 25, 'bold')).pack(anchor='w', pady=(2, 4))
        ttk.Label(body, text='건컨트롤러를 선택하고 조준을 확인한 뒤 언리얼 연동을 시작하세요.', foreground=MUTED).pack(anchor='w', pady=(0, 18))
        row = ttk.Frame(body)
        row.pack(fill='x')
        self.device_box = ttk.Combobox(row, state='readonly')
        self.device_box.pack(side='left', fill='x', expand=True)
        self.device_box.bind('<<ComboboxSelected>>', self.select_device)
        ttk.Button(row, text='장치 새로고침', command=self.refresh).pack(side='left', padx=(8, 0))
        self.status = tk.StringVar(value='장치를 선택하세요. 아직 언리얼로 입력을 보내지 않습니다.')
        tk.Label(body, textvariable=self.status, fg=ACCENT, bg=BG, wraplength=860,
                 font=('맑은 고딕', 10), height=3, anchor='nw', justify='left').pack(anchor='w', fill='x', pady=(10, 8))
        self.canvas = tk.Canvas(body, height=220, bg=PANEL, highlightthickness=0)
        self.canvas.pack(fill='both', expand=True, pady=(0, 8))
        ttk.Label(body, text='Square · 1 상단 왼쪽 / 2 상단 오른쪽 / 3 하단 왼쪽 / 4 하단 오른쪽',
                  foreground=MUTED, wraplength=860).pack(anchor='w', pady=(0, 4))
        self.metrics = tk.StringVar(value='장치 입력 0 Hz · 전송 대기')
        ttk.Label(body, textvariable=self.metrics, foreground=MUTED).pack(anchor='w')
        self.buttons_text = tk.StringVar(value='방아쇠 —   재장전 —   보정 완료 —')
        ttk.Label(body, textvariable=self.buttons_text).pack(anchor='w', pady=(5, 8))
        self.offset_status=tk.StringVar(value=self.adjustment.label)
        ttk.Label(body,textvariable=self.offset_status,foreground=ACCENT,wraplength=860).pack(anchor='w',pady=(0,4))
        mapping = ttk.Frame(body)
        mapping.pack(fill='x', pady=4)
        self.mapping_boxes = []
        names = ('1 · 왼쪽', '2 · 오른쪽', '3 · 가운데', '4 · 보조 1', '5 · 보조 2')
        for i, name in enumerate(('방아쇠', '재장전', '보정 완료')):
            ttk.Label(mapping, text=name).pack(side='left', padx=(0 if i == 0 else 14, 6))
            box = ttk.Combobox(mapping, state='readonly', values=names, width=12)
            box.current(i)
            box.pack(side='left')
            self.mapping_boxes.append(box)
        actions = ttk.Frame(body)
        actions.pack(fill='x', pady=(12, 8))
        ttk.Button(actions, text='조준 보정', command=self.start_calibration).pack(side='left')
        self.start_button = ttk.Button(actions, text='언리얼 연동 시작', command=self.toggle)
        self.start_button.pack(side='left', padx=8)
        ttk.Button(actions, text='모의 입력', command=self.toggle_demo).pack(side='left')
        self.monitor_rects = monitors()
        self.monitor_box = ttk.Combobox(actions, state='readonly', width=22,
            values=[f'화면 {i+1} · {r[2]}×{r[3]} ({r[0]}, {r[1]})' for i, r in enumerate(self.monitor_rects)])
        if self.monitor_rects:
            self.monitor_box.current(0)
        self.monitor_box.pack(side='right')
        self.monitor_box.bind('<<ComboboxSelected>>', lambda _e: self.load_profile())
        diagnostics = ttk.Frame(body)
        diagnostics.pack(fill='x', pady=(8, 4))
        self.serial_box = ttk.Combobox(diagnostics, state='readonly', width=22)
        self.serial_box.pack(side='left')
        ttk.Button(diagnostics, text='IR 카메라 점검', command=self.start_diagnostics).pack(side='left', padx=8)
        ttk.Button(diagnostics, text='점검 종료', command=self.stop_diagnostics).pack(side='left')
        ttk.Button(diagnostics, text='전체 화면', command=self.open_camera_window).pack(side='left', padx=8)
        ttk.Button(diagnostics, text='IR 화면 미리보기', command=self.start_camera_preview).pack(side='left')
        layout_actions = ttk.Frame(body)
        layout_actions.pack(fill='x', pady=(4, 4))
        ttk.Button(layout_actions, text='LED 설치 가이드 (G)', command=self.open_alignment).pack(side='left')
        ttk.Label(layout_actions, text='선택한 화면에 상단 2개·하단 2개의 설치 위치를 표시합니다.',
                  foreground=MUTED).pack(side='left', padx=10)
        led_actions = ttk.Frame(body)
        led_actions.pack(fill='x', pady=(2, 4))
        ttk.Button(led_actions, text='위치 번호 확인 (선택·R)', command=self.register_leds).pack(side='left')
        self.led_status = tk.StringVar(value='LED 검출: 점검 대기 · 상단 2개 / 하단 2개')
        ttk.Label(led_actions, textvariable=self.led_status, foreground=MUTED).pack(side='left', padx=10)
        ttk.Button(led_actions, text='검출 / 원본 보기 (Tab)', command=self.switch_camera_view).pack(side='right')
        ttk.Label(body, text='실행 옵션: -OBOpenFire   |   모의 입력: -OBOpenFire -OBOpenFireDemo', foreground=MUTED).pack(anchor='w', pady=(10, 3))
        ttk.Label(body, text='화면 좌표 기준: 왼쪽 위 (0, 0) → 오른쪽 아래 (1, 1)\nHID 입력에는 IR 점 유실 상태가 없습니다. 점검 화면의 좌표로 추적 성공을 판정하지 않습니다.',
                  foreground=MUTED, wraplength=860).pack(anchor='w')
        self.raw = RawInput(self.on_raw)
        self.refresh()
        # Point-sized fonts grow with Windows DPI; size the window to its actual
        # content so diagnostics and instructions remain visible at 150/200%.
        root.update_idletasks()
        required_width = max(940, root.winfo_reqwidth())
        required_height = max(760, root.winfo_reqheight())
        root.geometry(f'{required_width}x{required_height}')
        root.minsize(required_width, required_height)
        self.root.protocol('WM_DELETE_WINDOW', self.close)
        # An override-redirect fullscreen can leave Windows keyboard focus on
        # its owner. Handle Escape across our Tk windows as well as the viewer.
        self.root.bind_all('<Escape>', self.handle_escape)
        self.root.bind_all('<KeyPress-r>', self.register_leds)
        self.root.bind_all('<KeyPress-R>', self.register_leds)
        self.root.bind_all('<KeyPress-g>', self.open_alignment)
        self.root.bind_all('<KeyPress-G>', self.open_alignment)
        self.root.bind_all('<Tab>', self.switch_camera_view)
        # Widget/class focus traversal can consume Tab before bind_all.
        self.root.bind('<Tab>', self.switch_camera_view)
        self.root.after(8, self.tick)

    def profile_key(self):
        monitor = self.monitor_rects[self.monitor_box.current()] if self.monitor_box.current() >= 0 else ()
        return hashlib.sha256((self.state.device + str(monitor)).encode()).hexdigest()

    def load_profile(self):
        if self.state.enabled:
            self.pause()
        self.state.matrix, self.state.calibrated = IDENTITY, False
        profiles = self.config.get('profiles', {})
        profile = profiles.get(self.profile_key(), {}) if isinstance(profiles, dict) else {}
        try:
            matrix = tuple(float(x) for x in profile['matrix'])
            for p in (*TARGETS, (.5, .5)):
                project(matrix, p)
            self.state.matrix, self.state.calibrated = matrix, True
        except (KeyError, ValueError, TypeError, OverflowError):
            pass
        self.status.set('저장된 화면 보정을 불러왔습니다.' if self.state.calibrated else '보드의 기존 좌표를 사용합니다. 필요하면 이 화면에서 보정하세요.')

    def refresh(self):
        self.devices = [d for d in self.raw.devices() if 'VID_F143&' in d.path.upper()]
        self.device_box['values'] = [f'{d.label}  ·  {d.path.split("#")[-2]}' for d in self.devices]
        if self.selected is not None:
            matching = next((i for i, d in enumerate(self.devices) if d.path == self.state.device), None)
            if matching is None:
                self.state.disconnect()
                self.pause()
                self.selected = None
                self.device_box.set('장치 연결이 해제됐습니다')
                self.status.set('건컨트롤러 연결이 해제됐습니다. 다시 연결한 뒤 장치를 선택하세요.')
            else:
                self.selected = self.devices[matching].handle
                self.device_box.current(matching)
        elif not self.state.demo and not self.devices:
            self.device_box.set('OpenFIRE 장치 없음 · VID F143')
        try:
            from serial.tools import list_ports
            ports = [p.device for p in list_ports.comports() if p.vid == 0xF143]
        except ImportError:
            ports = []
        old = self.serial_box.get()
        self.serial_box['values'] = ports
        if old in ports:
            self.serial_box.set(old)
        elif ports:
            self.serial_box.current(0)
        else:
            self.serial_box.set('OpenFIRE COM 포트 없음')

    def select_device(self, _event=None):
        if self.diagnostic_thread and self.diagnostic_thread.is_alive():
            self.status.set('장치를 바꾸기 전에 점검을 종료하세요.')
            return
        self.pause()
        device = self.devices[self.device_box.current()]
        self.selected = device.handle
        self.state = InputState(device=device.path, connected=True)
        self.last_absolute = 0
        self.load_profile()

    def on_raw(self, handle, absolute, x, y, flags):
        if handle != self.selected or self.state.demo:
            return
        previous = self.state.buttons[0]
        self.state.raw_mouse(absolute, x, y, flags)
        self.reports += 1
        if absolute:
            self.last_absolute = time.monotonic()
        if self.cal_window is not None and flags & 1 and not previous:
            self.root.after_idle(self.capture)
        if self.state.enabled and flags:
            self.sender.send(self.state)  # Send short taps immediately, in addition to heartbeat.

    def pause(self):
        self.state.enabled = False
        self.start_button.configure(text='언리얼 연동 시작')
        # Repeated released snapshots repair a lost UDP stop packet.
        for _ in range(3):
            self.sender.send(self.state)

    def toggle(self):
        if self.state.enabled:
            self.pause()
            self.status.set('언리얼 연동을 중지했습니다.')
            return
        if self.cal_window is not None or self.diagnostic_active or self.camera_demo or self.alignment_window is not None:
            self.status.set('보정 또는 장치 점검을 종료한 뒤 시작하세요.')
            return
        if not self.state.connected or (self.state.point is None and not self.state.demo):
            self.status.set('장치를 선택하고 화면을 향해 움직여 절대좌표 입력을 먼저 확인하세요.')
            return
        mapping = tuple(box.current() for box in self.mapping_boxes)
        if len(set(mapping)) != 3:
            self.status.set('방아쇠·재장전·보정 완료 버튼을 서로 다르게 선택하세요.')
            return
        self.state.mapping = mapping
        self.state.session = uuid.uuid4().hex
        self.state.sequence = 0
        self.state.counts = [0]*3
        self.state.enabled = True
        self.start_button.configure(text='언리얼 연동 중지')
        self.status.set(f'{"모의 입력" if self.state.demo else "건컨트롤러"} → 127.0.0.1:{self.sender.target[1]} · 언리얼 실행 옵션을 확인하세요.')

    def toggle_demo(self):
        if self.cal_window is not None or self.diagnostic_active or self.camera_demo or self.alignment_window is not None:
            self.status.set('보정 또는 점검을 종료한 뒤 모의 입력을 선택하세요.')
            return
        self.pause()
        if self.state.demo:
            self.state = InputState()
            self.selected = None
            self.refresh()
            self.status.set('모의 입력을 종료했습니다. 실물 장치를 다시 선택하세요.')
        else:
            self.selected = None
            self.state = InputState(device='DEMO', connected=True, demo=True)
            self.status.set('모의 입력입니다. 실제 장치·IR 추적 검증이 아닙니다. 연동 시작을 눌러 전송하세요.')

    def start_calibration(self):
        if self.state.demo or self.selected is None or self.state.point is None:
            self.status.set('실물 장치의 절대좌표를 확인한 뒤 보정하세요.')
            return
        if self.cal_window is not None or self.diagnostic_active or self.camera_demo or self.alignment_window is not None:
            return
        self.pause()
        self.cal_points, self.cal_matrix = [], None
        win = self.cal_window = tk.Toplevel(self.root)
        rect = self.monitor_rects[self.monitor_box.current()]
        win.geometry(f'{rect[2]}x{rect[3]}{rect[0]:+d}{rect[1]:+d}')
        win.overrideredirect(True)
        win.attributes('-topmost', True)
        self.cal_canvas = tk.Canvas(win, bg=BG, highlightthickness=0)
        self.cal_canvas.pack(fill='both', expand=True)
        win.bind('<Escape>', lambda _e: self.end_calibration())
        win.bind('<Return>', lambda _e: self.capture())
        win.bind('<r>', lambda _e: self.reset_calibration())
        win.focus_force()
        self.root.after(100, self.draw_calibration)

    def reset_calibration(self):
        self.cal_points, self.cal_matrix = [], None
        self.draw_calibration()

    def draw_calibration(self, feedback=''):
        if self.cal_window is None:
            return
        canvas = self.cal_canvas
        w, h = canvas.winfo_width(), canvas.winfo_height()
        canvas.delete('all')
        target = TARGETS[len(self.cal_points)] if len(self.cal_points) < 4 else (.5, .5)
        x, y = target[0]*(w-1), target[1]*(h-1)
        canvas.create_oval(x-28, y-28, x+28, y+28, outline=ACCENT, width=3)
        canvas.create_line(x-40, y, x+40, y, fill=TEXT, width=2)
        canvas.create_line(x, y-40, x, y+40, fill=TEXT, width=2)
        label = f'모서리 보정 {len(self.cal_points)+1} / 4' if len(self.cal_points) < 4 else '중앙 검증'
        canvas.create_text(w/2, h*.35, text=label, fill=TEXT, font=('맑은 고딕', 26, 'bold'))
        canvas.create_text(w/2, h*.65, text='표적을 조준하고 왼쪽 방아쇠 또는 Enter를 누르세요.\nEsc 취소 · R 다시 시작\n' + feedback,
                           fill=TEXT, font=('맑은 고딕', 16), justify='center')

    def capture(self):
        if self.cal_window is None:
            return
        if not self.state.connected or self.state.point is None or time.monotonic()-self.last_absolute > 1.:
            self.draw_calibration('새 좌표가 없습니다. 조준을 조금 움직인 뒤 다시 누르세요.')
            return
        try:
            if len(self.cal_points) < 4:
                self.cal_points.append(self.state.point)
                if len(self.cal_points) == 4:
                    try:
                        self.cal_matrix = fit_calibration(self.cal_points)
                    except ValueError:
                        self.cal_points, self.cal_matrix = [], None
                        raise
                self.draw_calibration()
            else:
                mapped = project(self.cal_matrix, self.state.point)
                error = math.hypot(mapped[0]-.5, mapped[1]-.5)
                if error > .035:
                    self.draw_calibration(f'중앙 오차 {error*100:.1f}% · 다시 조준하거나 R로 재보정하세요.')
                    return
                self.state.matrix, self.state.calibrated = self.cal_matrix, True
                profiles = self.config.setdefault('profiles', {})
                if not isinstance(profiles, dict):
                    profiles = self.config['profiles'] = {}
                profiles[self.profile_key()] = dict(matrix=self.cal_matrix, device=self.state.device,
                                                   center_error=error, points=self.cal_points)
                if self.shared_settings:
                    save_section('GunProfiles', self.config)
                else:
                    self.path.parent.mkdir(parents=True, exist_ok=True)
                    temporary = self.path.with_suffix('.tmp')
                    temporary.write_text(json.dumps(self.config, ensure_ascii=False, indent=2), encoding='utf-8')
                    temporary.replace(self.path)
                self.end_calibration()
                self.status.set(f'화면 보정을 저장했습니다. 중앙 오차 {error*100:.2f}% · 언리얼 연동을 시작하세요.')
        except (ValueError, OSError) as error:
            self.draw_calibration(str(error) + '\nR로 다시 시작할 수 있습니다.')

    def end_calibration(self):
        if self.cal_window is not None:
            self.cal_window.destroy()
            self.cal_window = None

    def start_diagnostics(self):
        if self.cal_window is not None or (self.diagnostic_thread and self.diagnostic_thread.is_alive()):
            return
        port = self.serial_box.get()
        if port not in self.serial_box['values']:
            self.status.set('OpenFIRE COM 포트를 선택하세요.')
            return
        self.pause()
        self.adjustment.reset_session()
        self.state.point = None
        self.camera_demo = False
        self.camera_frame = None
        self.camera_received = 0.
        self.camera_frames = 0
        self.camera_started = time.monotonic()
        self.camera_identity = ''
        self.detection_frame = None
        self.detection_received = 0.
        self.detection_sequence = None
        self.aim_sample = None
        self.aim_received = 0.
        self.led_tracker.reset()
        self.camera_aim_view = False
        self.diagnostic_active = True
        self.diagnostic_stop.clear()
        def emit(kind, value):
            # Keep the receive time from the serial thread; UI backlog must not
            # make old queued coordinates appear freshly captured.
            self.messages.put((kind, value, time.monotonic()))
        def worker():
            try:
                run_diagnostics(port, self.diagnostic_stop, emit)
            except Exception as error:
                emit('info', f'점검을 진행하지 못했습니다: {error}')
            finally:
                emit('finished', None)
        self.status.set('보드 응답을 확인하고 있습니다…')
        self.diagnostic_thread = threading.Thread(target=worker, daemon=True)
        self.diagnostic_thread.start()

    def stop_diagnostics(self):
        self.diagnostic_stop.set()
        self.camera_demo = False
        self.camera_frame = None
        self.detection_frame = None
        self.aim_sample = None
        self.led_tracker.reset()
        self.led_status.set('LED 검출: 점검 종료 · 검출 미확인')
        self.close_camera_window()
        if not self.diagnostic_active:
            self.status.set('IR 화면 미리보기를 종료했습니다.')

    def start_camera_preview(self):
        if self.cal_window is not None or self.diagnostic_active:
            self.status.set('실물 점검이나 보정을 종료한 뒤 미리보기를 열어주세요.')
            return
        self.pause()
        self.camera_demo = True
        self.camera_demo_started = time.monotonic()
        self.camera_frame = demo_frame(0.)
        self.detection_frame = demo_detection(0.)
        self.aim_sample = AimSample(self.detection_frame, self.camera_frame)
        self.aim_received = self.camera_demo_started
        self.detection_received = self.camera_demo_started
        self.led_tracker.reset()
        self.led_tracker.positions = list(self.detection_frame.points)
        self.led_tracker.last_seen = [self.camera_demo_started]*4
        self.led_tracker.update(self.detection_frame, self.camera_demo_started)
        self.camera_aim_view = False
        self.camera_received = self.camera_demo_started
        self.camera_identity = ''
        self.status.set('모의 LED 화면입니다. 상단 2개·하단 2개 설치 위치를 표시합니다.')
        self.open_camera_window()

    def open_camera_window(self):
        if self.alignment_window is not None:
            self.alignment_restore_camera = True
            return
        if not self.diagnostic_active and not self.camera_demo:
            self.status.set('IR 카메라 점검 또는 IR 화면 미리보기를 먼저 시작하세요.')
            return
        if self.camera_window is not None:
            self.camera_window.lift()
            return
        win = self.camera_window = tk.Toplevel(self.root)
        win.title('Operation Breakpoint · IR 카메라 점검')
        rect = self.monitor_rects[max(0, self.monitor_box.current())]
        win.geometry(f'{rect[2]}x{rect[3]}{rect[0]:+d}{rect[1]:+d}')
        win.overrideredirect(True)
        win.attributes('-topmost', True)
        self.camera_canvas = tk.Canvas(win, bg='#10103a', highlightthickness=0)
        self.camera_canvas.pack(fill='both', expand=True)
        win.bind('<Escape>', lambda _event: self.stop_diagnostics())
        win.bind('<Tab>', self.switch_camera_view)
        win.protocol('WM_DELETE_WINDOW', self.stop_diagnostics)
        win.focus_force()
        self.render_camera(time.monotonic())

    def close_camera_window(self):
        if self.camera_window is not None:
            self.camera_window.destroy()
            self.camera_window = None

    def handle_escape(self, _event=None):
        if self.alignment_window is not None:
            self.close_alignment()
        elif self.cal_window is not None:
            self.end_calibration()
        elif self.camera_window is not None or self.diagnostic_active or self.camera_demo:
            self.stop_diagnostics()
        return 'break'

    def open_alignment(self, _event=None):
        if self.cal_window is not None:
            self.status.set('조준 보정을 종료한 뒤 설치 가이드를 열어주세요.')
            return 'break'
        if self.alignment_window is not None:
            self.alignment_window.lift()
            return 'break'
        index = self.monitor_box.current()
        if not 0 <= index < len(self.monitor_rects):
            self.status.set('설치 가이드를 표시할 화면을 선택하세요.')
            return 'break'
        self.pause()
        self.alignment_restore_camera = self.camera_window is not None
        self.close_camera_window()
        win = self.alignment_window = tk.Toplevel(self.root)
        win.title('Operation Breakpoint · Square LED 설치 가이드')
        rect = self.monitor_rects[index]
        win.geometry(f'{rect[2]}x{rect[3]}{rect[0]:+d}{rect[1]:+d}')
        win.overrideredirect(True)
        win.attributes('-topmost', True)
        canvas = self.alignment_canvas = tk.Canvas(win, bg=BG, highlightthickness=0, borderwidth=0)
        canvas.pack(fill='both', expand=True)
        canvas.bind('<Configure>', lambda _e: draw_alignment_guide(canvas))
        win.bind('<Escape>', self.close_alignment)
        win.bind('<Button-1>', self.close_alignment)
        win.protocol('WM_DELETE_WINDOW', self.close_alignment)
        win.focus_force()
        win.update_idletasks()
        draw_alignment_guide(canvas)
        self.status.set('LED 설치 가이드 · Square · 상단 2개 / 하단 2개 · Esc로 닫기')
        return 'break'

    def close_alignment(self, _event=None, *, restore=True):
        was_open = self.alignment_window is not None
        if self.alignment_window is not None:
            self.alignment_window.destroy()
            self.alignment_window = None
        reopen = self.alignment_restore_camera
        self.alignment_restore_camera = False
        if restore and reopen and not self.closed and (self.diagnostic_active or self.camera_demo):
            self.open_camera_window()
            self.status.set('IR 점검 화면으로 돌아왔습니다.' if self.diagnostic_active else '모의 IR 화면으로 돌아왔습니다.')
        elif restore and was_open:
            self.status.set('LED 설치 가이드를 닫았습니다. 설치 후 IR 카메라 점검으로 검출을 확인하세요.')
        return 'break'

    def render_camera(self, now):
        if self.detection_frame is not None:
            fresh = 0 <= now-self.detection_received <= CAMERA_TIMEOUT
            if not fresh and (self.led_tracker.registered or self.led_tracker.registering):
                self.led_tracker.reset('수신 끊김 · R로 기준점을 다시 등록하세요.')
            prefix = '모의 검출' if self.camera_demo else '실제 검출'
            self.led_status.set(f'{prefix} {len(self.detection_frame.points)} / 4 · ' +
                                ('위치 등록됨' if self.led_tracker.registered else '위치 미등록') if fresh else
                                '수신 끊김 · 검출 미확인')
            if not self.camera_aim_view:
                index = self.monitor_box.current()
                rect = self.monitor_rects[index] if 0 <= index < len(self.monitor_rects) else (0, 0, 16, 9)
                sample_fresh = self.aim_sample is not None and 0 <= now-self.aim_received <= CAMERA_TIMEOUT
                for canvas, fullscreen in ((self.canvas, False), (getattr(self, 'camera_canvas', None), True)):
                    if canvas is not None and (not fullscreen or self.camera_window is not None):
                        draw_detection_scene(canvas, self.detection_frame, self.led_tracker, fresh=fresh,
                                             fullscreen=fullscreen, demo=self.camera_demo, subtitle=self.camera_identity,
                                             sample=self.aim_sample, sample_fresh=sample_fresh, display_aspect=rect[2]/rect[3],
                                             adjustment=None if self.camera_demo else self.adjustment)
                return
        fresh = self.camera_frame is not None and 0 <= now-self.camera_received <= CAMERA_TIMEOUT
        status = '카메라 좌표 수신 대기'
        if self.camera_frame is not None and not fresh:
            status = '카메라 좌표 수신이 끊겼습니다.\n이전 점 위치는 숨겼습니다.'
        subtitle = self.camera_identity
        if not fresh and self.camera_frame is None and now-self.camera_started > CAMERA_TIMEOUT:
            status = '카메라 좌표가 들어오지 않습니다.\n카메라 연결·전원과 LED 방향을 확인하세요.'
        draw_camera_scene(self.canvas, self.camera_frame, fresh=fresh, demo=self.camera_demo,
                          subtitle=subtitle, status=status, detection_available=self.detection_frame is not None)
        if self.camera_window is not None:
            draw_camera_scene(self.camera_canvas, self.camera_frame, fresh=fresh, fullscreen=True,
                              demo=self.camera_demo, subtitle=subtitle, status=status,
                              detection_available=self.detection_frame is not None)

    def switch_camera_view(self, _event=None):
        if not self.diagnostic_active and not self.camera_demo:
            return
        self.camera_aim_view = not self.camera_aim_view
        return 'break'

    def register_leds(self, _event=None):
        if self.cal_window is not None or self.alignment_window is not None:
            return
        now = time.monotonic()
        if self.camera_demo:
            self.status.set('모의 화면에서는 실물 기준점을 등록하지 않습니다.')
        elif self.detection_frame is None or now-self.detection_received > CAMERA_TIMEOUT:
            self.status.set('실제 LED 검출 데이터가 필요합니다. 검출 확장 펌웨어로 IR 점검을 시작하세요.')
        elif self.led_tracker.begin(self.detection_frame, self.detection_received):
            self.camera_aim_view = False
            self.status.set('기준점 등록: 카메라를 고정하고 화면 안내에 따라 LED를 하나씩 가려주세요.')
            self.open_camera_window()
        else:
            self.status.set(self.led_tracker.message)
        return 'break'

    def tick(self):
        if self.closed:
            return
        now = time.monotonic()
        if self.state.demo and not self.diagnostic_active and not self.camera_demo:
            self.state.point = (.5+.32*math.sin(now), .5+.25*math.cos(now*.7))
        if self.camera_demo:
            self.camera_frame = demo_frame(now-self.camera_demo_started)
            self.camera_received = now
            self.detection_frame = demo_detection(now-self.camera_demo_started)
            self.aim_sample = AimSample(self.detection_frame, self.camera_frame)
            self.aim_received = now
            self.detection_received = now
            self.led_tracker.update(self.detection_frame, now)
        try:
            self.state.output_y_shift=0. if self.state.demo else self.adjustment.total_mm/self.adjustment.height_mm
            self.state.output_x_shift=0. if self.state.demo else self.adjustment.x_shift
            self.sender.send(self.state)
            while not self.messages.empty():
                kind, value, received = self.messages.get_nowait()
                if kind == 'camera':
                    self.aim_sample = None  # Replaced only by an adjacent raw+camera pair.
                    self.camera_frame = CameraFrame(tuple(value))
                    self.camera_received = received
                    self.camera_frames += 1
                    if self.camera_frames == 1:
                        self.status.set(self.camera_identity + ' · IR 좌표 수신 중 · Esc로 점검 종료')
                elif kind == 'identity':
                    version, board = value
                    self.camera_identity = f'펌웨어 {version} / {board}'
                    self.status.set(self.camera_identity + ' · 보드 연결됨 · IR 좌표 수신 대기')
                    self.led_status.set('LED 검출 확장 연결 중' if version.endswith(('-obir1','-obir2','-obir3')) else
                                        '원본 펌웨어 · 실제 검출 확인 불가 · 검출 확장 펌웨어 필요')
                    self.open_camera_window()
                elif kind == 'detection':
                    self.aim_sample = None
                    if self.detection_sequence is not None and value.sequence != ((self.detection_sequence+1) & 0xffffffff):
                        self.led_tracker.reset('검출 데이터 순서가 끊겼습니다. R로 다시 등록하세요.')
                    self.detection_sequence = value.sequence
                    self.detection_frame = value
                    self.detection_received = received
                    if value.layout == 0:
                        self.led_tracker.update(value, received)
                    else:
                        self.led_tracker.reset('보드 배치를 Square로 설정해야 위치 번호를 확인할 수 있습니다.')
                elif kind == 'sample':
                    if value.detection == self.detection_frame:
                        self.aim_sample = value
                        self.aim_received = received
                elif kind == 'detection_error':
                    self.aim_sample = None
                    self.detection_frame = None
                    self.led_tracker.reset(value)
                    self.led_status.set(value)
                    self.status.set(value)
                elif kind == 'button':
                    self.buttons_text.set(f'점검 버튼 {value[0]} · {"눌림" if value[1] else "놓음"}')
                elif kind == 'encoder':
                    self.adjustment.accept_encoder(value,received)
                elif kind == 'error':
                    self.status.set(f'보드 오류: 0x{value:02X}')
                elif kind == 'finished':
                    self.diagnostic_active = False
                    self.camera_frame = None
                    self.detection_frame = None
                    self.aim_sample = None
                    self.led_tracker.reset()
                    self.led_status.set('점검 종료 · 검출 미확인')
                    self.close_camera_window()
                    self.state.point = None
                    if '진행하지 못했습니다' not in self.status.get() and '오류' not in self.status.get():
                        self.status.set(self.camera_identity + ' · 점검 종료 · 실물 좌표를 다시 확인하세요.')
                else:
                    self.status.set(value)
            self.adjustment.flush()
            self.adjustment.reload_shared()
            self.offset_status.set(self.adjustment.label)
            if now-self.last_rate >= 1.:
                elapsed = now-self.last_rate
                rate = (self.reports-self.last_reports)/elapsed
                if self.diagnostic_active:
                    elapsed = max(.001, now-self.camera_started)
                    self.metrics.set(f'IR 좌표 {self.camera_frames/elapsed:.1f} Hz · {self.camera_frames}프레임 · ' +
                                     ('센서 감지 플래그로 실제 검출 판정' if self.detection_frame is not None else
                                      '기존 보정 좌표 · 실제 검출 개수 미지원'))
                elif self.camera_demo:
                    self.metrics.set('모의 IR 화면 · 실물 카메라와 무관한 표시 확인입니다.')
                else:
                    self.metrics.set(f'장치 입력 {rate:.0f} Hz · UDP 누적 {self.sender.sent}개 · '
                                     f'{"모의 입력" if self.state.demo else "화면 보정 적용" if self.state.calibrated else "기존 보드 좌표"}')
                self.last_rate, self.last_reports = now, self.reports
                self.refresh()
            if self.diagnostic_active or self.camera_demo:
                self.render_camera(now)
            else:
                self.draw_aim()
        except OSError as error:
            self.state.enabled = False
            self.status.set(str(error))
        if self.raw.error:
            self.pause()
            self.status.set('입력 처리 오류: ' + self.raw.error)
            self.raw.error = ''
        self.root.after(8, self.tick)

    def draw_aim(self):
        canvas = self.canvas
        w, h = canvas.winfo_width(), canvas.winfo_height()
        canvas.delete('all')
        canvas.create_line(w/2, 12, w/2, h-12, fill='#2b4052')
        canvas.create_line(12, h/2, w-12, h/2, fill='#2b4052')
        if self.state.point is None:
            canvas.create_text(w/2, h/2, text='건컨트롤러의 절대좌표 입력을 기다리고 있습니다.', fill=MUTED, font=('맑은 고딕', 13))
        else:
            try:
                x, y = project(self.state.matrix, self.state.point)
                x+=self.state.output_x_shift
                y-=self.state.output_y_shift
                px, py = x*(w-1), y*(h-1)
                canvas.create_oval(px-9, py-9, px+9, py+9, outline=ACCENT, width=3)
                canvas.create_text(15, 18, anchor='w', text=f'{"DEMO · " if self.state.demo else ""}X {x:.4f}   Y {y:.4f}', fill=TEXT, font=('Consolas', 12))
            except ValueError as error:
                self.status.set(str(error))
        self.buttons_text.set('   '.join(f'{name} {"눌림" if self.state.buttons[index] else "놓음"}'
            for name, index in zip(('방아쇠', '재장전', '보정 완료'), self.state.mapping)))

    def close(self):
        if self.closed:
            return
        self.close_alignment(restore=False)
        self.end_calibration()
        self.pause()
        self.stop_diagnostics()
        if self.diagnostic_thread:
            self.diagnostic_thread.join(timeout=2.)
        self.closed = True
        self.raw.close()
        self.sender.close()
        self.adjustment.flush(force=True)
        self.root.destroy()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=PORT)
    parser.add_argument('--settings', type=Path)
    parser.add_argument('--smoke-test', type=Path, help='Boot the actual UI, save an inspection result, and exit')
    parser.add_argument('--demo-stream', type=float, metavar='SECONDS', help='Headless, explicitly marked synthetic UDP input')
    args = parser.parse_args(argv)
    if args.demo_stream is not None:
        if not math.isfinite(args.demo_stream) or not 0 < args.demo_stream <= 3600:
            parser.error('--demo-stream must be finite and between 0 and 3600')
        sender = Sender(args.port)
        state = InputState(device='DEMO', connected=True, enabled=True, demo=True)
        start = time.monotonic()
        try:
            while time.monotonic()-start < args.demo_stream:
                t = time.monotonic()-start
                state.point = (.5+.3*math.sin(t), .5+.25*math.cos(t))
                sender.send(state)
                time.sleep(.008)
        finally:
            state.enabled = False
            sender.send(state)
            sender.close()
        return
    # Use physical pixels for calibration and multi-monitor geometry.
    try:
        ctypes.WinDLL('user32').SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
    except (AttributeError, OSError):
        pass
    root = tk.Tk()
    app = App(root, args.port, args.settings)
    if args.smoke_test:
        # Run the real periodic diagnostic path, not just static canvas draws.
        # This catches callback failures in metrics/refresh after the first second.
        smoke_started = time.monotonic()
        app.last_rate = smoke_started
        callback_errors = []
        root.report_callback_exception = lambda kind, error, trace: callback_errors.append(str(error))
        app.diagnostic_active = True
        app.camera_started = smoke_started
        app.camera_frame = demo_frame(0.)
        app.camera_received = smoke_started
        app.detection_frame = demo_detection(0.)
        app.detection_received = smoke_started
        app.aim_sample = AimSample(app.detection_frame, app.camera_frame)
        app.aim_received = smoke_started
        def refresh_fixture():
            app.detection_received = app.camera_received = app.aim_received = time.monotonic()
        root.after(900, refresh_fixture)
        def report():
            live_aim_points = len(app.canvas.find_withtag('screen_aim'))
            fixture = CameraFrame((150, 200, 1800, 200, 150, 900, 1800, 900, 1200, 650, 960, 550))
            draw_camera_scene(app.canvas, fixture, fresh=True)
            camera_points = len(app.canvas.find_withtag('camera_point'))
            camera_edges = len(app.canvas.find_withtag('camera_box'))
            detector = demo_detection(0.)
            tracker = LEDTracker()
            tracker.positions = list(detector.points)
            tracker.visible = [True]*4
            sample = AimSample(detector, fixture)
            draw_detection_scene(app.canvas, detector, tracker, fresh=True, sample=sample, sample_fresh=True)
            detected_points = len(app.canvas.find_withtag('detected_point'))
            led_indicators = len(app.canvas.find_withtag('led_indicator'))
            aim_points = len(app.canvas.find_withtag('screen_aim'))
            draw_detection_scene(app.canvas, detector, tracker, fresh=False, sample=sample, sample_fresh=True)
            stale_detected = len(app.canvas.find_withtag('detected_point'))
            stale_aim_points = len(app.canvas.find_withtag('screen_aim'))
            outside = CameraFrame(fixture.coords[:8]+(2200, 1400)+fixture.coords[10:])
            draw_detection_scene(app.canvas, detector, tracker, fresh=True,
                                 sample=AimSample(detector, outside), sample_fresh=True)
            offscreen_arrows = len(app.canvas.find_withtag('offscreen_aim'))
            false_inside_aim = len(app.canvas.find_withtag('screen_aim'))
            guide_positions = draw_alignment_guide(app.canvas)
            guide_targets = len(app.canvas.find_withtag('guide_target'))
            draw_camera_scene(app.canvas, fixture, fresh=False)
            stale_points = len(app.canvas.find_withtag('camera_point'))
            args.smoke_test.parent.mkdir(parents=True, exist_ok=True)
            args.smoke_test.write_text(json.dumps(dict(window=root.title(), version=TOOL_VERSION,
                raw_window=bool(app.raw.window), devices=len(app.devices),
                monitors=app.monitor_rects, sending=app.state.enabled,
                camera_points=camera_points, camera_edges=camera_edges, stale_camera_points=stale_points,
                detected_points=detected_points, led_indicators=led_indicators, stale_detected=stale_detected,
                diagnostic_metrics_checked=app.last_rate-smoke_started >= 1., callback_errors=callback_errors,
                guide_targets=guide_targets, guide_positions=guide_positions,
                live_aim_points=live_aim_points, aim_points=aim_points, stale_aim_points=stale_aim_points,
                offscreen_arrows=offscreen_arrows, false_inside_aim=false_inside_aim,
                window_size=[root.winfo_width(), root.winfo_height()],
                required_size=[root.winfo_reqwidth(), root.winfo_reqheight()],
                canvas=[app.canvas.winfo_width(), app.canvas.winfo_height()],
                raw_error=app.raw.error), ensure_ascii=False, indent=2), encoding='utf-8')
            app.close()
        root.after(1200, report)
    root.mainloop()


if __name__ == '__main__':
    main()
