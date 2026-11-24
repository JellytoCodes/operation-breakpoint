from __future__ import annotations

import statistics
import time
import math
from collections import deque
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path
from typing import Any, Sequence

try:
    import msvcrt
except ImportError:
    msvcrt = None

import cv2
import mediapipe as mp
from pythonosc.udp_client import SimpleUDPClient

from common import load_config, resolve_osc_ip
from breach_guide import ADDRESS as BREACH_GUIDE_ADDRESS, make_breach_guide_payload
from preview_overlay import put_text
from obs_camera import shared_camera_config


BASE_DIR = Path(__file__).resolve().parent
MODEL_PATH = BASE_DIR / "pose_landmarker_lite.task"
WINDOW_NAME = "MediaPipe Motion Bridge"

NOSE_INDEX = 0
LEFT_EAR_INDEX = 7
RIGHT_EAR_INDEX = 8

LEFT_SHOULDER_INDEX = 11
RIGHT_SHOULDER_INDEX = 12

LEFT_WRIST_INDEX = 15
RIGHT_WRIST_INDEX = 16

LEFT_HIP_INDEX = 23
RIGHT_HIP_INDEX = 24

LEFT_ANKLE_INDEX = 27
RIGHT_ANKLE_INDEX = 28
LEFT_HEEL_INDEX = 29
RIGHT_HEEL_INDEX = 30
LEFT_FOOT_INDEX = 31
RIGHT_FOOT_INDEX = 32

HEAD_INDICES = (
    NOSE_INDEX,
    LEFT_EAR_INDEX,
    RIGHT_EAR_INDEX,
)

SHOULDER_INDICES = (
    LEFT_SHOULDER_INDEX,
    RIGHT_SHOULDER_INDEX,
)

HIP_INDICES = (
    LEFT_HIP_INDEX,
    RIGHT_HIP_INDEX,
)

LEFT_FOOT_INDICES = (
    LEFT_ANKLE_INDEX,
    LEFT_HEEL_INDEX,
    LEFT_FOOT_INDEX,
)

RIGHT_FOOT_INDICES = (
    RIGHT_ANKLE_INDEX,
    RIGHT_HEEL_INDEX,
    RIGHT_FOOT_INDEX,
)

BACKENDS = {
    "AUTO": cv2.CAP_ANY,
    "DSHOW": cv2.CAP_DSHOW,
    "MSMF": cv2.CAP_MSMF,
}


def open_motion_camera(config: dict[str, Any]):
    """Prefer the configured camera; accept only a capture delivering frames."""
    config = shared_camera_config(config)
    preferred_index = int(config.get("camera_index", 0))
    preferred_backend = str(config.get("camera_backend", "DSHOW")).upper()
    if preferred_backend not in BACKENDS:
        raise ValueError(f"Unknown camera_backend: {preferred_backend}")
    candidates = [(preferred_index, preferred_backend)]
    if bool(config.get("camera_auto_fallback", True)):
        indices = list(dict.fromkeys([preferred_index, *range(
            max(0, int(config.get("camera_scan_max_index", 5))) + 1)]))
        for backend_name in dict.fromkeys([preferred_backend, "DSHOW", "MSMF", "AUTO"]):
            candidates.extend((index, backend_name) for index in indices
                              if (index, backend_name) not in candidates)

    for index, backend_name in candidates:
        capture = cv2.VideoCapture(index, BACKENDS[backend_name])
        accepted = False
        try:
            if not capture.isOpened():
                continue
            capture.set(cv2.CAP_PROP_FRAME_WIDTH, int(config.get("frame_width", 640)))
            capture.set(cv2.CAP_PROP_FRAME_HEIGHT, int(config.get("frame_height", 480)))
            capture.set(cv2.CAP_PROP_FPS, int(config.get("frame_fps", 30)))
            # DirectShow renegotiates the format when dimensions/FPS change.
            # Apply MJPEG LAST: XWF otherwise delivers YUY2 at 720p/10 or 1080p/5.
            fourcc = str(config.get("camera_fourcc", "")).upper()
            if fourcc:
                if len(fourcc) != 4:
                    raise ValueError("camera_fourcc must contain four characters")
                capture.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*fourcc))
            # Some USB cameras need a few reads before their first valid frame.
            for _ in range(5):
                success, frame = capture.read()
                if success and frame is not None and frame.size > 0:
                    accepted = True
                    print(f"Camera selected: index={index}, backend={capture.getBackendName()}, "
                          f"frame={frame.shape[1]}x{frame.shape[0]}", flush=True)
                    return capture
                time.sleep(0.05)
        except cv2.error:
            continue
        finally:
            if not accepted:
                capture.release()

    raise RuntimeError(
        "사용 가능한 카메라 프레임을 찾지 못했습니다. "
        "다른 앱의 카메라 점유와 Windows 카메라 권한을 확인하십시오. "
        "camera_scan.py로 카메라 번호를 확인할 수 있습니다."
    )


class MotionMode(IntEnum):
    BREACH = 1
    HAMMER = 2
    KICK = 3


class LiveFrameRate:
    """Measure delivered processing frames; never trust the driver FPS value."""

    def __init__(self):
        self.times = deque(maxlen=90)

    def update(self, now):
        self.times.append(now)
        elapsed = self.times[-1] - self.times[0]
        return (len(self.times) - 1) / elapsed if elapsed > 0 else 0.0


class CrouchStatePublisher:
    """Refresh held posture so packet loss and a late game start recover."""

    def __init__(self, client, address, interval=0.2):
        self.client, self.address, self.interval = client, address, interval
        self.last_state = None
        self.last_time = float("-inf")

    def publish(self, is_crouching, now):
        state = int(bool(is_crouching))
        if state != self.last_state or now - self.last_time >= self.interval:
            self.client.send_message(self.address, state)
            self.last_state, self.last_time = state, now


class HammerPhase(IntEnum):
    WAITING_START = 0
    ARMED = 1
    WAITING_CLEAR = 2


class KickPhase(IntEnum):
    WAITING_READY = 0
    ARMED = 1
    WAITING_CLEAR = 2


@dataclass(frozen=True)
class PosePoint:
    x: float
    y: float
    z: float
    visibility: float

    @classmethod
    def from_landmark(cls, landmark: Any) -> "PosePoint":
        return cls(
            x=float(landmark.x),
            y=float(landmark.y),
            z=float(landmark.z),
            visibility=float(landmark.visibility or 0.0),
        )

    def to_osc_payload(self) -> list[float]:
        return [
            self.x,
            self.y,
            self.z,
            self.visibility,
        ]


@dataclass(frozen=True)
class CrouchResult:
    valid: bool
    calibrated: bool
    calibration_completed: bool
    state_changed: bool
    is_crouching: bool
    drop_ratio: float
    shoulder_drop_ratio: float | None
    head_drop_ratio: float | None
    calibration_progress: float


@dataclass(frozen=True)
class HammerResult:
    start_inside: bool
    target_inside: bool
    status_text: str
    swing_ok: bool


@dataclass(frozen=True)
class KickResult:
    left_inside: bool
    right_inside: bool
    status_text: str
    kick_ok: bool


@dataclass(frozen=True)
class PlayerROI:
    left: float
    top: float
    right: float
    bottom: float

    @classmethod
    def from_config(cls, config: dict[str, Any]) -> "PlayerROI":
        roi_config = config.get("player_roi", {})

        left = float(roi_config.get("left", 0.375))
        top = float(roi_config.get("top", 0.05))
        right = float(roi_config.get("right", 0.625))
        bottom = float(roi_config.get("bottom", 0.95))

        left = min(max(left, 0.0), 1.0)
        top = min(max(top, 0.0), 1.0)
        right = min(max(right, 0.0), 1.0)
        bottom = min(max(bottom, 0.0), 1.0)

        if left >= right:
            raise ValueError("player_roi: left는 right보다 작아야 합니다.")

        if top >= bottom:
            raise ValueError("player_roi: top은 bottom보다 작아야 합니다.")

        return cls(
            left=left,
            top=top,
            right=right,
            bottom=bottom,
        )

    @property
    def center(self) -> tuple[float, float]:
        return (
            (self.left + self.right) * 0.5,
            (self.top + self.bottom) * 0.5,
        )

    def contains(self, x: float, y: float) -> bool:
        return (
            self.left <= x <= self.right
            and self.top <= y <= self.bottom
        )


def get_number(
    config: dict[str, Any],
    key: str,
    default: float,
) -> float:
    return float(config.get(key, default))


def get_roi(
    config: dict[str, Any],
    key: str,
    default: tuple[float, float, float, float],
) -> PlayerROI:
    roi_config = config.get(key, {})

    left = min(max(float(roi_config.get("left", default[0])), 0.0), 1.0)
    top = min(max(float(roi_config.get("top", default[1])), 0.0), 1.0)
    right = min(max(float(roi_config.get("right", default[2])), 0.0), 1.0)
    bottom = min(max(float(roi_config.get("bottom", default[3])), 0.0), 1.0)

    if left >= right:
        raise ValueError(f"{key}: left는 right보다 작아야 합니다.")

    if top >= bottom:
        raise ValueError(f"{key}: top은 bottom보다 작아야 합니다.")

    return PlayerROI(
        left=left,
        top=top,
        right=right,
        bottom=bottom,
    )


def get_visibility(landmark: Any) -> float:
    return float(landmark.visibility or 0.0)


def average_visible_y(
    landmarks: Sequence[Any],
    indices: Sequence[int],
    minimum_visibility: float,
) -> float | None:
    values = [
        float(landmarks[index].y)
        for index in indices
        if get_visibility(landmarks[index]) >= minimum_visibility
    ]

    if not values:
        return None

    return statistics.fmean(values)


def average_visible_center(
    landmarks: Sequence[Any],
    indices: Sequence[int],
    minimum_visibility: float,
) -> tuple[float, float] | None:
    points = [
        landmarks[index]
        for index in indices
        if get_visibility(landmarks[index]) >= minimum_visibility
    ]

    if not points:
        return None

    return (
        statistics.fmean(float(point.x) for point in points),
        statistics.fmean(float(point.y) for point in points),
    )


def average_pose_point(
    landmarks: Sequence[Any],
    indices: Sequence[int],
    minimum_visibility: float,
) -> PosePoint | None:
    points = [
        landmarks[index]
        for index in indices
        if get_visibility(landmarks[index]) >= minimum_visibility
    ]

    if not points:
        return None

    return PosePoint(
        x=statistics.fmean(float(point.x) for point in points),
        y=statistics.fmean(float(point.y) for point in points),
        z=statistics.fmean(float(point.z) for point in points),
        visibility=statistics.fmean(get_visibility(point) for point in points),
    )


def squared_distance(
    first: tuple[float, float],
    second: tuple[float, float],
) -> float:
    return (
        (first[0] - second[0]) ** 2
        + (first[1] - second[1]) ** 2
    )


def draw_point(
    frame_bgr: Any,
    point: PosePoint,
    color: tuple[int, int, int],
    radius: int = 10,
) -> None:
    height, width = frame_bgr.shape[:2]

    pixel_x = int(point.x * width)
    pixel_y = int(point.y * height)

    if 0 <= pixel_x < width and 0 <= pixel_y < height:
        cv2.circle(
            frame_bgr,
            (pixel_x, pixel_y),
            radius,
            color,
            -1,
        )


def draw_roi(
    frame_bgr: Any,
    roi: PlayerROI,
    is_player_detected: bool,
) -> None:
    height, width = frame_bgr.shape[:2]

    top_left = (
        int(roi.left * width),
        int(roi.top * height),
    )
    bottom_right = (
        int(roi.right * width),
        int(roi.bottom * height),
    )

    color = (
        (0, 255, 0)
        if is_player_detected
        else (0, 180, 255)
    )

    cv2.rectangle(
        frame_bgr,
        top_left,
        bottom_right,
        color,
        2,
    )

    label_y = max(top_left[1] - 10, 20)

    put_text(
        frame_bgr,
        "플레이어 영역",
        (top_left[0], label_y),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.55,
        color,
        2,
    )


def draw_hand_target(
    frame_bgr: Any,
    roi: PlayerROI,
    label: str,
    is_inside: bool,
) -> None:
    height, width = frame_bgr.shape[:2]

    top_left = (
        int(roi.left * width),
        int(roi.top * height),
    )
    bottom_right = (
        int(roi.right * width),
        int(roi.bottom * height),
    )

    color = (0, 255, 0) if is_inside else (0, 0, 255)

    cv2.rectangle(
        frame_bgr,
        top_left,
        bottom_right,
        color,
        3,
    )

    put_text(
        frame_bgr,
        label,
        (top_left[0], max(top_left[1] - 8, 20)),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.55,
        color,
        2,
    )


def set_window_always_on_top(enabled=True) -> bool:
    try:
        cv2.setWindowProperty(
            WINDOW_NAME,
            cv2.WND_PROP_TOPMOST,
            int(enabled),
        )
        return True
    except (AttributeError, cv2.error):
        return False


def format_ratio(value: float | None) -> str:
    if value is None:
        return "-"

    return f"{value*100:.0f}%"


def read_console_key() -> str | None:
    """Read a Windows console key without blocking the camera loop."""
    if msvcrt is None or not msvcrt.kbhit():
        return None

    key = msvcrt.getwch()

    if key in ("\x00", "\xe0"):
        if msvcrt.kbhit():
            msvcrt.getwch()
        return None

    return key.lower()


class BreachDetector:
    """Detect both wrists staying inside their configured target boxes."""

    def __init__(
        self,
        *,
        left_hand_roi: PlayerROI,
        right_hand_roi: PlayerROI,
        minimum_visibility: float,
        stable_frames: int = 4,
    ) -> None:
        self.left_hand_roi = left_hand_roi
        self.right_hand_roi = right_hand_roi
        self.minimum_visibility = minimum_visibility
        self.stable_frames = max(stable_frames, 1)
        self.reset()

    def reset(self) -> None:
        self._inside_frame_count = 0
        self._is_ready = False

    @property
    def progress(self) -> float:
        return min(self._inside_frame_count / self.stable_frames, 1.0)

    def update(
        self,
        left_wrist: PosePoint,
        right_wrist: PosePoint,
    ) -> tuple[bool, bool, bool]:
        left_inside = (
            left_wrist.visibility >= self.minimum_visibility
            and self.left_hand_roi.contains(left_wrist.x, left_wrist.y)
        )
        right_inside = (
            right_wrist.visibility >= self.minimum_visibility
            and self.right_hand_roi.contains(right_wrist.x, right_wrist.y)
        )

        was_ready = self._is_ready

        if left_inside and right_inside:
            self._inside_frame_count += 1
            self._is_ready = self._inside_frame_count >= self.stable_frames
        else:
            self.reset()

        if self._is_ready and not was_ready:
            print("[BREACH] OK - BOTH HANDS IN TARGET BOXES")

        return left_inside, right_inside, self._is_ready


class HammerSwingDetector:
    """Recognize a two-hand diagonal hammer swing within a time limit."""

    def __init__(
        self,
        *,
        start_roi: PlayerROI,
        target_roi: PlayerROI,
        minimum_visibility: float,
        stable_frames: int,
        swing_timeout_seconds: float,
        result_display_seconds: float,
    ) -> None:
        self.start_roi = start_roi
        self.target_roi = target_roi
        self.minimum_visibility = minimum_visibility
        self.stable_frames = max(stable_frames, 1)
        self.swing_timeout_seconds = max(swing_timeout_seconds, 0.1)
        self.result_display_seconds = max(result_display_seconds, 0.1)
        self.reset()

    def reset(self) -> None:
        self._phase = HammerPhase.WAITING_START
        self._stable_frame_count = 0
        self._clear_frame_count = 0
        self._deadline = 0.0
        self._outcome_text = ""
        self._outcome_until = 0.0

    def _is_visible(self, wrist: PosePoint) -> bool:
        return wrist.visibility >= self.minimum_visibility

    def _both_inside(
        self,
        roi: PlayerROI,
        left_wrist: PosePoint,
        right_wrist: PosePoint,
    ) -> bool:
        return (
            self._is_visible(left_wrist)
            and self._is_visible(right_wrist)
            and roi.contains(left_wrist.x, left_wrist.y)
            and roi.contains(right_wrist.x, right_wrist.y)
        )

    def _hands_clear(
        self,
        left_wrist: PosePoint,
        right_wrist: PosePoint,
    ) -> bool:
        def is_inside_any_roi(wrist: PosePoint) -> bool:
            if not self._is_visible(wrist):
                return False

            return (
                self.start_roi.contains(wrist.x, wrist.y)
                or self.target_roi.contains(wrist.x, wrist.y)
            )

        return not is_inside_any_roi(left_wrist) and not is_inside_any_roi(right_wrist)

    def update(
        self,
        left_wrist: PosePoint,
        right_wrist: PosePoint,
        now: float,
    ) -> HammerResult:
        start_inside = self._both_inside(
            self.start_roi,
            left_wrist,
            right_wrist,
        )
        target_inside = self._both_inside(
            self.target_roi,
            left_wrist,
            right_wrist,
        )
        swing_ok = False
        status_text = "해머: 양손을 시작 영역에 놓으세요"

        if self._phase == HammerPhase.WAITING_START:
            self._stable_frame_count = (
                self._stable_frame_count + 1
                if start_inside
                else 0
            )

            if self._stable_frame_count >= self.stable_frames:
                self._phase = HammerPhase.ARMED
                self._stable_frame_count = 0
                self._deadline = now + self.swing_timeout_seconds
                status_text = f"해머: 휘두르세요! 남은 시간 {self.swing_timeout_seconds:.1f}초"
                print(
                    f"[HAMMER] ARMED - SWING WITHIN "
                    f"{self.swing_timeout_seconds:.1f} SECONDS"
                )

        elif self._phase == HammerPhase.ARMED:
            remaining_seconds = max(self._deadline - now, 0.0)
            status_text = f"해머: 휘두르세요! 남은 시간 {remaining_seconds:.1f}초"

            if now <= self._deadline:
                self._stable_frame_count = (
                    self._stable_frame_count + 1
                    if target_inside
                    else 0
                )

                if self._stable_frame_count >= self.stable_frames:
                    self._phase = HammerPhase.WAITING_CLEAR
                    self._stable_frame_count = 0
                    self._outcome_text = "해머 동작 성공"
                    self._outcome_until = now + self.result_display_seconds
                    swing_ok = True
                    status_text = self._outcome_text
                    print("[HAMMER] SWING OK")
            else:
                self._phase = HammerPhase.WAITING_CLEAR
                self._stable_frame_count = 0
                self._outcome_text = "해머: 시간이 초과됐습니다. 다시 시도하세요"
                self._outcome_until = now + self.result_display_seconds
                status_text = self._outcome_text
                print("[HAMMER] TIMEOUT - TRY AGAIN")

        else:
            if now < self._outcome_until:
                status_text = self._outcome_text
                swing_ok = self._outcome_text == "해머 동작 성공"
            else:
                status_text = "해머: 양손을 영역 밖으로 빼세요"

            self._clear_frame_count = (
                self._clear_frame_count + 1
                if self._hands_clear(left_wrist, right_wrist)
                else 0
            )

            if (
                now >= self._outcome_until
                and self._clear_frame_count >= self.stable_frames
            ):
                self.reset()
                status_text = "해머: 양손을 시작 영역에 놓으세요"

        return HammerResult(
            start_inside=start_inside,
            target_inside=target_inside,
            status_text=status_text,
            swing_ok=swing_ok,
        )


class KickDetector:
    """Recognize either foot entering a kick target after a neutral ready pose."""

    def __init__(
        self,
        *,
        target_roi: PlayerROI,
        minimum_visibility: float,
        ready_frames: int,
        stable_frames: int,
        result_display_seconds: float,
    ) -> None:
        self.target_roi = target_roi
        self.minimum_visibility = minimum_visibility
        self.ready_frames = max(ready_frames, 1)
        self.stable_frames = max(stable_frames, 1)
        self.result_display_seconds = max(result_display_seconds, 0.1)
        self.reset()

    def reset(self) -> None:
        self._phase = KickPhase.WAITING_READY
        self._ready_frame_count = 0
        self._inside_frame_count = 0
        self._clear_frame_count = 0
        self._result_until = 0.0

    def _is_inside(self, foot: PosePoint | None) -> bool:
        return (
            foot is not None
            and foot.visibility >= self.minimum_visibility
            and self.target_roi.contains(foot.x, foot.y)
        )

    def update(
        self,
        left_foot: PosePoint | None,
        right_foot: PosePoint | None,
        now: float,
    ) -> KickResult:
        left_inside = self._is_inside(left_foot)
        right_inside = self._is_inside(right_foot)
        any_inside = left_inside or right_inside
        kick_ok = False
        status_text = "발차기: 먼저 두 발을 목표 영역 밖에 두세요"

        if self._phase == KickPhase.WAITING_READY:
            both_feet_visible = left_foot is not None and right_foot is not None
            ready_pose = both_feet_visible and not any_inside

            self._ready_frame_count = (
                self._ready_frame_count + 1
                if ready_pose
                else 0
            )

            if self._ready_frame_count >= self.ready_frames:
                self._phase = KickPhase.ARMED
                self._ready_frame_count = 0
                status_text = "발차기 준비 완료: 목표 영역을 차세요"
                print("[KICK] READY - KICK THE TARGET BOX")

        elif self._phase == KickPhase.ARMED:
            status_text = "발차기 준비 완료: 목표 영역을 차세요"
            self._inside_frame_count = (
                self._inside_frame_count + 1
                if any_inside
                else 0
            )

            if self._inside_frame_count >= self.stable_frames:
                self._phase = KickPhase.WAITING_CLEAR
                self._inside_frame_count = 0
                self._result_until = now + self.result_display_seconds
                kick_ok = True
                status_text = "발차기 성공"
                print("[KICK] KICK OK")

        else:
            if now < self._result_until:
                status_text = "발차기 성공"
                kick_ok = True
            else:
                status_text = "발차기: 발을 영역 밖으로 빼세요"

            self._clear_frame_count = (
                self._clear_frame_count + 1
                if not any_inside
                else 0
            )

            if (
                now >= self._result_until
                and self._clear_frame_count >= self.ready_frames
            ):
                self.reset()

        return KickResult(
            left_inside=left_inside,
            right_inside=right_inside,
            status_text=status_text,
            kick_ok=kick_ok,
        )


class PlayerPoseSelector:
    """
    ROI 내부 Pose 중 실제 플레이어 하나를 선택한다.

    이전 프레임의 위치와 가까운 Pose를 우선하므로
    MediaPipe가 Pose 배열 순서를 바꾸더라도 대상 전환을 줄인다.
    """

    def __init__(
        self,
        *,
        roi: PlayerROI,
        minimum_visibility: float,
        maximum_tracking_jump: float,
        tracking_lock_timeout_seconds: float,
    ) -> None:
        self.roi = roi
        self.minimum_visibility = minimum_visibility
        self.maximum_tracking_jump = maximum_tracking_jump
        self.tracking_lock_timeout_seconds = tracking_lock_timeout_seconds

        self._last_anchor: tuple[float, float] | None = None
        self._last_seen_time = 0.0

    def reset(self) -> None:
        self._last_anchor = None
        self._last_seen_time = 0.0

    def _get_anchor(
        self,
        landmarks: Sequence[Any],
    ) -> tuple[float, float] | None:
        # 어깨 중심을 우선 사용한다.
        shoulder_center = average_visible_center(
            landmarks,
            SHOULDER_INDICES,
            self.minimum_visibility,
        )

        if shoulder_center is not None:
            return shoulder_center

        # 깊게 앉아 어깨가 가려졌다면 머리 중심을 대신 사용한다.
        return average_visible_center(
            landmarks,
            HEAD_INDICES,
            self.minimum_visibility,
        )

    def select(
        self,
        poses: Sequence[Sequence[Any]],
        now: float,
    ) -> Sequence[Any] | None:
        candidates: list[
            tuple[tuple[float, float], Sequence[Any]]
        ] = []

        for landmarks in poses:
            anchor = self._get_anchor(landmarks)

            if anchor is None:
                continue

            if not self.roi.contains(anchor[0], anchor[1]):
                continue

            candidates.append((anchor, landmarks))

        if not candidates:
            if (
                self._last_anchor is not None
                and now - self._last_seen_time
                >= self.tracking_lock_timeout_seconds
            ):
                self.reset()

            return None

        selected: tuple[
            tuple[float, float],
            Sequence[Any],
        ] | None = None

        if self._last_anchor is not None:
            nearest = min(
                candidates,
                key=lambda candidate: squared_distance(
                    candidate[0],
                    self._last_anchor,
                ),
            )

            maximum_jump_squared = (
                self.maximum_tracking_jump
                * self.maximum_tracking_jump
            )

            if (
                squared_distance(
                    nearest[0],
                    self._last_anchor,
                )
                <= maximum_jump_squared
            ):
                selected = nearest
            elif (
                now - self._last_seen_time
                < self.tracking_lock_timeout_seconds
            ):
                # 기존 플레이어를 잠시 놓친 상황.
                # 즉시 다른 사람에게 대상이 넘어가지 않게 한다.
                return None
            else:
                self.reset()

        if selected is None:
            roi_center = self.roi.center

            selected = min(
                candidates,
                key=lambda candidate: squared_distance(
                    candidate[0],
                    roi_center,
                ),
            )

        self._last_anchor = selected[0]
        self._last_seen_time = now

        return selected[1]


class CrouchDetector:
    """
    고정 카메라용 앉기 판정기.

    앉기 진입:
        머리와 어깨가 모두 기준 높이보다 내려가야 한다.

    일어서기:
        머리가 선 자세 위치로 돌아와야 한다.
        어깨가 보이는 경우 어깨도 같이 돌아와야 한다.

    깊게 앉아서 일부 Landmark가 사라지더라도
    즉시 일어서기로 변경하지 않는다.
    """

    def __init__(
        self,
        *,
        minimum_visibility: float,
        calibration_frames: int = 45,
        shoulder_enter_ratio: float = 0.45,
        shoulder_exit_ratio: float = 0.25,
        head_enter_ratio: float = 0.40,
        head_exit_ratio: float = 0.20,
        stable_frames: int = 4,
        tracking_timeout_seconds: float = 2.0,
    ) -> None:
        self.minimum_visibility = minimum_visibility
        self.calibration_frames = max(calibration_frames, 10)

        self.shoulder_enter_ratio = shoulder_enter_ratio
        self.shoulder_exit_ratio = shoulder_exit_ratio
        self.head_enter_ratio = head_enter_ratio
        self.head_exit_ratio = head_exit_ratio

        self.stable_frames = max(stable_frames, 1)
        self.tracking_timeout_seconds = tracking_timeout_seconds

        self.reset()

    @property
    def is_calibrated(self) -> bool:
        return (
            self._baseline_shoulder_y is not None
            and self._baseline_head_y is not None
            and self._baseline_torso_length is not None
        )

    @property
    def calibration_progress(self) -> float:
        return min(
            len(self._shoulder_samples)
            / self.calibration_frames,
            1.0,
        )

    def reset(self) -> None:
        self._shoulder_samples: list[float] = []
        self._head_samples: list[float] = []
        self._torso_samples: list[float] = []

        self._baseline_shoulder_y: float | None = None
        self._baseline_head_y: float | None = None
        self._baseline_torso_length: float | None = None

        self._is_crouching = False
        self._enter_count = 0
        self._exit_count = 0
        self._last_valid_time = time.perf_counter()

    def _make_result(
        self,
        *,
        valid: bool,
        calibration_completed: bool = False,
        state_changed: bool = False,
        shoulder_drop_ratio: float | None = None,
        head_drop_ratio: float | None = None,
    ) -> CrouchResult:
        available_drops = [
            value
            for value in (
                shoulder_drop_ratio,
                head_drop_ratio,
            )
            if value is not None
        ]

        return CrouchResult(
            valid=valid,
            calibrated=self.is_calibrated,
            calibration_completed=calibration_completed,
            state_changed=state_changed,
            is_crouching=self._is_crouching,
            drop_ratio=max(available_drops, default=0.0),
            shoulder_drop_ratio=shoulder_drop_ratio,
            head_drop_ratio=head_drop_ratio,
            calibration_progress=self.calibration_progress,
        )

    def update_missing(
        self,
        now: float,
    ) -> CrouchResult:
        state_changed = False

        should_release_crouch = (
            self._is_crouching
            and self.tracking_timeout_seconds > 0.0
            and now - self._last_valid_time
            >= self.tracking_timeout_seconds
        )

        if should_release_crouch:
            self._is_crouching = False
            self._enter_count = 0
            self._exit_count = 0
            state_changed = True

        return self._make_result(
            valid=False,
            state_changed=state_changed,
        )

    def update(
        self,
        landmarks: Sequence[Any],
        now: float,
    ) -> CrouchResult:
        shoulder_y = average_visible_y(
            landmarks,
            SHOULDER_INDICES,
            self.minimum_visibility,
        )

        hip_y = average_visible_y(
            landmarks,
            HIP_INDICES,
            self.minimum_visibility,
        )

        head_y = average_visible_y(
            landmarks,
            HEAD_INDICES,
            self.minimum_visibility,
        )

        if not self.is_calibrated:
            return self._update_calibration(
                shoulder_y=shoulder_y,
                hip_y=hip_y,
                head_y=head_y,
                now=now,
            )

        # 캘리브레이션 이후에는 골반을 사용하지 않는다.
        # 깊게 앉았을 때 골반이 프레임 밖으로 사라져도
        # 머리 또는 어깨가 보이면 추적을 유지한다.
        if shoulder_y is None and head_y is None:
            return self.update_missing(now)

        self._last_valid_time = now

        body_scale = self._baseline_torso_length or 0.05

        shoulder_drop = (
            (
                shoulder_y
                - (self._baseline_shoulder_y or 0.0)
            )
            / body_scale
            if shoulder_y is not None
            else None
        )

        head_drop = (
            (
                head_y
                - (self._baseline_head_y or 0.0)
            )
            / body_scale
            if head_y is not None
            else None
        )

        state_changed = False

        if self._is_crouching:
            returned_to_stand = (
                head_drop is not None
                and head_drop <= self.head_exit_ratio
                and (
                    shoulder_drop is None
                    or shoulder_drop
                    <= self.shoulder_exit_ratio
                )
            )

            if returned_to_stand:
                self._exit_count += 1
            else:
                self._exit_count = 0

            if self._exit_count >= self.stable_frames:
                self._is_crouching = False
                self._enter_count = 0
                self._exit_count = 0
                state_changed = True

        else:
            entered_crouch = (
                shoulder_drop is not None
                and head_drop is not None
                and shoulder_drop
                >= self.shoulder_enter_ratio
                and head_drop
                >= self.head_enter_ratio
            )

            if entered_crouch:
                self._enter_count += 1
            else:
                self._enter_count = 0

            if self._enter_count >= self.stable_frames:
                self._is_crouching = True
                self._enter_count = 0
                self._exit_count = 0
                state_changed = True

        return self._make_result(
            valid=True,
            state_changed=state_changed,
            shoulder_drop_ratio=shoulder_drop,
            head_drop_ratio=head_drop,
        )

    def _update_calibration(
        self,
        *,
        shoulder_y: float | None,
        hip_y: float | None,
        head_y: float | None,
        now: float,
    ) -> CrouchResult:
        if (
            shoulder_y is None
            or hip_y is None
            or head_y is None
        ):
            return self._make_result(valid=False)

        torso_length = hip_y - shoulder_y

        if torso_length <= 0.05:
            return self._make_result(valid=False)

        self._last_valid_time = now

        self._shoulder_samples.append(shoulder_y)
        self._head_samples.append(head_y)
        self._torso_samples.append(torso_length)

        calibration_completed = False

        if (
            len(self._shoulder_samples)
            >= self.calibration_frames
        ):
            self._baseline_shoulder_y = statistics.median(
                self._shoulder_samples
            )

            self._baseline_head_y = statistics.median(
                self._head_samples
            )

            self._baseline_torso_length = max(
                statistics.median(self._torso_samples),
                0.05,
            )

            calibration_completed = True

        return self._make_result(
            valid=True,
            calibration_completed=calibration_completed,
        )


def _main(*, on_ready=None, on_calibration=None, should_stop=None, should_preview=None, camera_id=None, embedded=False, channel=None) -> None:
    config = load_config()
    from device_settings import load_section, save_section
    from camera_devices import configured_camera_config
    config = configured_camera_config(config, camera_id)

    if not MODEL_PATH.exists():
        raise FileNotFoundError(
            "Pose 모델 파일이 없습니다. "
            "01_setup.ps1을 먼저 실행하십시오.\n"
            f"예상 경로: {MODEL_PATH}"
        )

    mirror_input = bool(
        config.get("mirror_input", False)
    )

    maximum_num_poses = max(
        int(config.get("max_num_poses", 3)),
        1,
    )

    minimum_visibility = get_number(
        config,
        "minimum_visibility",
        0.6,
    )

    player_roi = PlayerROI.from_config(config)
    left_hand_roi = get_roi(
        config,
        "breach_left_hand_roi",
        (0.68125, 0.28125, 0.74875, 0.36875),
    )
    right_hand_roi = get_roi(
        config,
        "breach_right_hand_roi",
        (0.25125, 0.28125, 0.31875, 0.36875),
    )
    hammer_start_roi = get_roi(
        config,
        "hammer_start_roi",
        (0.09625, 0.12, 0.37375, 0.36),
    )
    hammer_target_roi = get_roi(
        config,
        "hammer_target_roi",
        (0.62625, 0.6225, 0.90375, 0.8775),
    )
    kick_target_roi = get_roi(
        config,
        "kick_target_roi",
        (0.3875, 0.5175, 0.6125, 0.7425),
    )

    motion_mode = MotionMode.BREACH
    if not embedded:
        try:
            motion_mode = MotionMode(int(config.get("initial_motion_mode", MotionMode.BREACH)))
        except (TypeError, ValueError):
            pass

    c4_preview = False
    pose_selector = PlayerPoseSelector(
        roi=player_roi,
        minimum_visibility=minimum_visibility,
        maximum_tracking_jump=get_number(
            config,
            "player_maximum_tracking_jump",
            0.20,
        ),
        tracking_lock_timeout_seconds=get_number(
            config,
            "player_tracking_lock_timeout_seconds",
            1.0,
        ),
    )

    breach_detector = BreachDetector(
        left_hand_roi=left_hand_roi,
        right_hand_roi=right_hand_roi,
        minimum_visibility=get_number(
            config,
            "breach_minimum_visibility",
            0.6,
        ),
        stable_frames=int(
            config.get("breach_stable_frames", 4)
        ),
    )

    hammer_detector = HammerSwingDetector(
        start_roi=hammer_start_roi,
        target_roi=hammer_target_roi,
        minimum_visibility=get_number(
            config,
            "hammer_minimum_visibility",
            0.6,
        ),
        stable_frames=int(
            config.get("hammer_stable_frames", 4)
        ),
        swing_timeout_seconds=get_number(
            config,
            "hammer_swing_timeout_seconds",
            2.0,
        ),
        result_display_seconds=get_number(
            config,
            "hammer_result_display_seconds",
            1.5,
        ),
    )

    kick_detector = KickDetector(
        target_roi=kick_target_roi,
        minimum_visibility=get_number(
            config,
            "kick_minimum_visibility",
            0.5,
        ),
        ready_frames=int(
            config.get("kick_ready_frames", 6)
        ),
        stable_frames=int(
            config.get("kick_stable_frames", 2)
        ),
        result_display_seconds=get_number(
            config,
            "kick_result_display_seconds",
            1.5,
        ),
    )

    crouch_detector = CrouchDetector(
        minimum_visibility=minimum_visibility,
        calibration_frames=int(
            config.get(
                "crouch_calibration_frames",
                45,
            )
        ),
        shoulder_enter_ratio=get_number(
            config,
            "crouch_shoulder_enter_ratio",
            0.45,
        ),
        shoulder_exit_ratio=get_number(
            config,
            "crouch_shoulder_exit_ratio",
            0.25,
        ),
        head_enter_ratio=get_number(
            config,
            "crouch_head_enter_ratio",
            0.40,
        ),
        head_exit_ratio=get_number(
            config,
            "crouch_head_exit_ratio",
            0.20,
        ),
        stable_frames=int(
            config.get(
                "crouch_stable_frames",
                4,
            )
        ),
        tracking_timeout_seconds=get_number(
            config,
            "crouch_tracking_timeout_seconds",
            2.0,
        ),
    )

    if embedded:
        # Local game IPC must not inherit network settings from old per-PC calibrations.
        config = {**config, "osc_ip": "127.0.0.1", "osc_port": 8000,
                  "crouch_address": "/motion/crouch", "breach_address": "/motion/breach",
                  "heartbeat_address": "/system/mediapipe/heartbeat"}
    osc_ip = resolve_osc_ip(config)
    osc_port = int(
        config.get("osc_port", 8000)
    )

    crouch_address = str(
        config.get(
            "crouch_address",
            "/motion/crouch",
        )
    )
    breach_address = str(config.get("breach_address", "/motion/breach"))
    last_breach_state = None
    last_breach_send_time = 0.0
    last_breach_guide_send_time = -1.0
    heartbeat_address = str(
        config.get(
            "heartbeat_address",
            "/system/mediapipe/heartbeat",
        )
    )
    left_wrist_address = str(
        config.get(
            "left_wrist_address",
            "/mediapipe/left_wrist",
        )
    )
    right_wrist_address = str(
        config.get(
            "right_wrist_address",
            "/mediapipe/right_wrist",
        )
    )

    send_wrist_landmarks = bool(
        config.get(
            "send_wrist_landmarks",
            False,
        )
    )

    wrist_send_hz = max(
        get_number(
            config,
            "wrist_send_hz",
            15.0,
        ),
        1.0,
    )
    heartbeat_hz = max(
        get_number(
            config,
            "heartbeat_hz",
            1.0,
        ),
        0.2,
    )

    wrist_send_interval = 1.0 / wrist_send_hz
    heartbeat_interval = 1.0 / heartbeat_hz

    osc_client = SimpleUDPClient(
        osc_ip,
        osc_port,
    )
    crouch_publisher = CrouchStatePublisher(osc_client, crouch_address)

    capture = open_motion_camera(config)
    rate = LiveFrameRate()
    format_code = int(capture.get(cv2.CAP_PROP_FOURCC))
    actual_format = "".join(chr((format_code >> (8 * i)) & 255) for i in range(4)).strip("\x00") or "unknown"
    requested_fps = int(config.get("frame_fps", 30))
    print(f"[CAMERA] format={actual_format} requested_fps={requested_fps} "
          f"driver_fps={capture.get(cv2.CAP_PROP_FPS):.2f}; LIVE FPS includes inference and preview", flush=True)

    print(f"MediaPipe Motion Bridge 시작 | OSC {osc_ip}:{osc_port}")
    print(
        "플레이어 영역 안에서 선 자세로 "
        "약 1.5초 유지하십시오."
    )
    print(
        "재보정: R / 종료: Q, ESC, 또는 Ctrl+C"
    )
    print("동작 모드: 1 = BREACH / 2 = HAMMER / 3 = KICK")
    print(f"현재 동작 모드: {motion_mode.name}")

    BaseOptions = mp.tasks.BaseOptions
    PoseLandmarker = mp.tasks.vision.PoseLandmarker
    PoseLandmarkerOptions = (
        mp.tasks.vision.PoseLandmarkerOptions
    )
    RunningMode = mp.tasks.vision.RunningMode

    options = PoseLandmarkerOptions(
        base_options=BaseOptions(
            model_asset_path=str(MODEL_PATH)
        ),
        running_mode=RunningMode.VIDEO,
        num_poses=maximum_num_poses,
        min_pose_detection_confidence=0.5,
        min_pose_presence_confidence=0.5,
        min_tracking_confidence=0.5,
        output_segmentation_masks=False,
    )

    start_time = time.perf_counter()

    last_wrist_send_time = -wrist_send_interval
    last_heartbeat_send_time = -heartbeat_interval

    calibration_identity = {k: config.get(k) for k in ('camera_index','camera_name','frame_width','frame_height','mirror_input','player_roi')}
    saved_calibration = load_section('CameraCalibration')
    baseline = saved_calibration.get('baseline')
    if saved_calibration.get('identity') == calibration_identity and isinstance(baseline, list) and len(baseline) == 3:
        if all(type(v) in (int,float) and math.isfinite(v) and 0<v<=2 for v in baseline):
            crouch_detector._baseline_shoulder_y, crouch_detector._baseline_head_y, crouch_detector._baseline_torso_length = baseline
    last_shared_reload = 0.
    try:
        if not embedded:
            cv2.namedWindow(WINDOW_NAME, cv2.WINDOW_NORMAL)
            set_window_always_on_top(False)
        ready_sent = False
        last_calibration_send_time = float("-inf")
        last_fps_log_time = start_time
        preview_enabled = should_preview is None or not embedded
        next_preview_check = start_time
        with PoseLandmarker.create_from_options(
            options
        ) as landmarker:
            while True:
                if should_stop is not None and should_stop():
                    break
                success, frame_bgr = capture.read()

                if not success or frame_bgr is None:
                    raise RuntimeError(
                        "카메라 프레임을 읽지 못했습니다."
                    )

                if mirror_input:
                    frame_bgr = cv2.flip(
                        frame_bgr,
                        1,
                    )

                frame_rgb = cv2.cvtColor(
                    frame_bgr,
                    cv2.COLOR_BGR2RGB,
                )

                mp_image = mp.Image(
                    image_format=mp.ImageFormat.SRGB,
                    data=frame_rgb,
                )

                now = time.perf_counter()
                timestamp_ms = int(
                    (now - start_time) * 1000
                )

                if embedded and should_preview is not None and now >= next_preview_check:
                    preview_enabled = should_preview()
                    next_preview_check = now + 0.1

                result = landmarker.detect_for_video(
                    mp_image,
                    timestamp_ms,
                )

                if (
                    now - last_heartbeat_send_time
                    >= heartbeat_interval
                ):
                    osc_client.send_message(
                        heartbeat_address,
                        1,
                    )
                    last_heartbeat_send_time = now

                landmarks = pose_selector.select(
                    result.pose_landmarks,
                    now,
                )

                player_detected = landmarks is not None
                left_hand_inside = False
                right_hand_inside = False
                breach_ready = False
                left_wrist: PosePoint | None = None
                right_wrist: PosePoint | None = None
                left_foot: PosePoint | None = None
                right_foot: PosePoint | None = None
                hammer_result = HammerResult(
                    start_inside=False,
                    target_inside=False,
                    status_text="해머: 플레이어를 기다리는 중",
                    swing_ok=False,
                )
                kick_result = KickResult(
                    left_inside=False,
                    right_inside=False,
                    status_text="발차기: 플레이어를 기다리는 중",
                    kick_ok=False,
                )

                wrist_text = (
                    "손 추적: 플레이어를 기다리는 중"
                )
                crouch_text = (
                    "자세: 플레이어 영역 안에 서 주세요"
                )
                player_text = (
                    "플레이어: 인식 중"
                    if player_detected
                    else "플레이어: 찾지 못함"
                )

                if landmarks is not None:
                    left_wrist = PosePoint.from_landmark(
                        landmarks[LEFT_WRIST_INDEX]
                    )
                    right_wrist = PosePoint.from_landmark(
                        landmarks[RIGHT_WRIST_INDEX]
                    )

                    if motion_mode == MotionMode.BREACH:
                        (
                            left_hand_inside,
                            right_hand_inside,
                            breach_ready,
                        ) = breach_detector.update(
                            left_wrist,
                            right_wrist,
                        )
                    elif motion_mode == MotionMode.HAMMER:
                        hammer_result = hammer_detector.update(
                            left_wrist,
                            right_wrist,
                            now,
                        )
                    else:
                        left_foot = average_pose_point(
                            landmarks,
                            LEFT_FOOT_INDICES,
                            kick_detector.minimum_visibility,
                        )
                        right_foot = average_pose_point(
                            landmarks,
                            RIGHT_FOOT_INDICES,
                            kick_detector.minimum_visibility,
                        )
                        kick_result = kick_detector.update(
                            left_foot,
                            right_foot,
                            now,
                        )

                    nose = PosePoint.from_landmark(
                        landmarks[NOSE_INDEX]
                    )

                    wrist_text = (
                        f"손 추적: 왼손 {'인식' if left_wrist.visibility >= minimum_visibility else '안 보임'} / "
                        f"오른손 {'인식' if right_wrist.visibility >= minimum_visibility else '안 보임'}"
                    )

                    if (
                        send_wrist_landmarks
                        and now - last_wrist_send_time
                        >= wrist_send_interval
                    ):
                        if (
                            left_wrist.visibility
                            >= minimum_visibility
                        ):
                            osc_client.send_message(
                                left_wrist_address,
                                left_wrist.to_osc_payload(),
                            )

                        if (
                            right_wrist.visibility
                            >= minimum_visibility
                        ):
                            osc_client.send_message(
                                right_wrist_address,
                                right_wrist.to_osc_payload(),
                            )

                        last_wrist_send_time = now

                    crouch = crouch_detector.update(
                        landmarks,
                        now,
                    )

                    if crouch.calibration_completed:
                        save_section('CameraCalibration', dict(identity=calibration_identity, baseline=[
                            crouch_detector._baseline_shoulder_y, crouch_detector._baseline_head_y,
                            crouch_detector._baseline_torso_length]))
                        print(
                            "CROUCH CALIBRATION COMPLETE "
                            "→ STAND(0)"
                        )

                    if crouch.state_changed:
                        state = (
                            1
                            if crouch.is_crouching
                            else 0
                        )

                        state_name = (
                            "CROUCH"
                            if state
                            else "STAND"
                        )

                        print(
                            f"CROUCH STATE → "
                            f"{state} ({state_name})"
                        )

                    if not crouch.calibrated:
                        percent = int(
                            crouch.calibration_progress
                            * 100.0
                        )

                        crouch_text = (
                            "서 있는 높이 측정 중... "
                            f"{percent}%"
                        )
                    elif not crouch.valid:
                        current_state = (
                            "앉음"
                            if crouch.is_crouching
                            else "서 있음"
                        )

                        crouch_text = (
                            "머리·어깨가 잘 안 보입니다 "
                            f"(이전 자세 유지: {current_state})"
                        )
                    else:
                        state_name = (
                            "앉음"
                            if crouch.is_crouching
                            else "서 있음"
                        )

                        shoulder_ratio = format_ratio(
                            crouch.shoulder_drop_ratio
                        )
                        head_ratio = format_ratio(
                            crouch.head_drop_ratio
                        )

                        crouch_text = (
                            f"자세: {state_name} · 내려간 양 "
                            f"어깨 {shoulder_ratio} / "
                            f"머리 {head_ratio}"
                        )

                    if preview_enabled:
                        if (
                            left_wrist.visibility
                            >= minimum_visibility
                        ):
                            draw_point(
                                frame_bgr,
                                left_wrist,
                                (255, 255, 0),
                            )

                        if (
                            right_wrist.visibility
                            >= minimum_visibility
                        ):
                            draw_point(
                                frame_bgr,
                                right_wrist,
                                (0, 255, 0),
                            )

                        if (
                            nose.visibility
                            >= minimum_visibility
                        ):
                            draw_point(
                                frame_bgr,
                                nose,
                                (255, 0, 255),
                                radius=8,
                            )

                        if motion_mode == MotionMode.KICK:
                            if left_foot is not None:
                                draw_point(
                                    frame_bgr,
                                    left_foot,
                                    (255, 180, 0),
                                    radius=9,
                                )

                            if right_foot is not None:
                                draw_point(
                                    frame_bgr,
                                    right_foot,
                                    (0, 120, 255),
                                    radius=9,
                                )

                else:
                    breach_detector.reset()
                    hammer_detector.reset()
                    kick_detector.reset()
                    crouch = (
                        crouch_detector.update_missing(now)
                    )

                    if crouch.state_changed:
                        print(
                            "CROUCH STATE → 0 "
                            "(tracking lost)"
                        )

                    if crouch.calibrated:
                        current_state = (
                            "앉음"
                            if crouch.is_crouching
                            else "서 있음"
                        )

                        crouch_text = (
                            "머리·어깨가 잘 안 보입니다 "
                            f"(이전 자세 유지: {current_state})"
                        )

                # A working camera/model is not a calibrated player. The launcher
                # waits here rather than launching the game on an empty frame.
                if not ready_sent:
                    if player_detected and crouch.calibrated and crouch.valid:
                        if on_ready is not None:
                            on_ready()
                        ready_sent = True
                    elif on_calibration is not None and now - last_calibration_send_time >= 0.5:
                        on_calibration(crouch.calibration_progress)
                        last_calibration_send_time = now

                crouch_publisher.publish(crouch.calibrated and crouch.is_crouching, now)

                # State changes plus periodic refresh recover dropped UDP packets.
                current_breach_state = bool(
                    player_detected and motion_mode == MotionMode.BREACH and breach_ready and not c4_preview
                )
                if current_breach_state != last_breach_state or now - last_breach_send_time >= 0.2:
                    osc_client.send_message(breach_address, current_breach_state)
                    last_breach_state = current_breach_state
                    last_breach_send_time = now

                if now - last_breach_guide_send_time >= 1.0 / 15.0:
                    osc_client.send_message(BREACH_GUIDE_ADDRESS, make_breach_guide_payload(
                        frame_size=(frame_bgr.shape[1], frame_bgr.shape[0]),
                        mirrored=mirror_input, mode=int(motion_mode), player_tracked=player_detected,
                        left_roi=(left_hand_roi.left, left_hand_roi.top, left_hand_roi.right, left_hand_roi.bottom),
                        right_roi=(right_hand_roi.left, right_hand_roi.top, right_hand_roi.right, right_hand_roi.bottom),
                        left_wrist=(left_wrist.x, left_wrist.y, left_wrist.visibility) if left_wrist else None,
                        right_wrist=(right_wrist.x, right_wrist.y, right_wrist.visibility) if right_wrist else None,
                        minimum_visibility=breach_detector.minimum_visibility,
                        left_inside=left_hand_inside, right_inside=right_hand_inside,
                        progress=breach_detector.progress, ready=current_breach_state))
                    last_breach_guide_send_time = now

                live_fps = rate.update(time.perf_counter())
                if now - last_fps_log_time >= 5.0:
                    print(f"[CAMERA] live_fps={live_fps:.2f} format={actual_format}", flush=True)
                    last_fps_log_time = now
                if preview_enabled:
                    draw_roi(
                        frame_bgr,
                        player_roi,
                        player_detected,
                    )

                    if motion_mode == MotionMode.BREACH and c4_preview:
                        action_text = "C4 테스트: 올린 손을 중앙 부착 영역으로 가져다 대세요"
                        action_color = (0, 255, 255)
                    elif motion_mode == MotionMode.BREACH:
                        draw_hand_target(
                            frame_bgr,
                            left_hand_roi,
                            "왼손 위치",
                            left_hand_inside,
                        )
                        draw_hand_target(
                            frame_bgr,
                            right_hand_roi,
                            "오른손 위치",
                            right_hand_inside,
                        )

                        action_text = "브리칭: 양손 인식 완료" if breach_ready else "브리칭: 양손을 각각의 영역에 놓으세요"
                        action_color = (0, 255, 0) if breach_ready else (0, 180, 255)
                    elif motion_mode == MotionMode.HAMMER:
                        draw_hand_target(
                            frame_bgr,
                            hammer_start_roi,
                            "해머 시작 위치",
                            hammer_result.start_inside,
                        )
                        draw_hand_target(
                            frame_bgr,
                            hammer_target_roi,
                            "해머 목표 위치",
                            hammer_result.target_inside,
                        )

                        action_text = hammer_result.status_text
                        action_color = (
                            (0, 255, 0)
                            if hammer_result.swing_ok
                            else (0, 180, 255)
                        )
                    else:
                        draw_hand_target(
                            frame_bgr,
                            kick_target_roi,
                            "발차기 목표 위치",
                            kick_result.left_inside or kick_result.right_inside,
                        )

                        action_text = kick_result.status_text
                        action_color = (
                            (0, 255, 0)
                            if kick_result.kick_ok
                            else (0, 180, 255)
                        )

                    if player_detected:
                        put_text(
                            frame_bgr,
                            action_text,
                            (20, 145),
                            cv2.FONT_HERSHEY_SIMPLEX,
                            0.70,
                            action_color,
                            2,
                        )

                    put_text(
                        frame_bgr,
                        player_text,
                        (20, 35),
                        cv2.FONT_HERSHEY_SIMPLEX,
                        0.65,
                        (
                            (0, 255, 0)
                            if player_detected
                            else (0, 180, 255)
                        ),
                        2,
                    )

                    put_text(
                        frame_bgr,
                        crouch_text,
                        (20, 70),
                        cv2.FONT_HERSHEY_SIMPLEX,
                        0.65,
                        (0, 255, 255),
                        2,
                    )

                    put_text(
                        frame_bgr,
                        wrist_text,
                        (20, 105),
                        cv2.FONT_HERSHEY_SIMPLEX,
                        0.60,
                        (255, 255, 0),
                        2,
                    )

                    mode_name = 'C4 부착 (한 손)' if c4_preview else {MotionMode.BREACH:'브리칭 (양손)',MotionMode.HAMMER:'해머',MotionMode.KICK:'발차기'}[motion_mode]
                    mode_text = f"동작 모드: {mode_name}" + (" · 1/2/3 키로 변경" if not embedded else "")
                    put_text(frame_bgr,
                        f"영상 처리 {live_fps:.1f}fps / 카메라 설정 {requested_fps}fps · {frame_bgr.shape[1]}x{frame_bgr.shape[0]} {actual_format}",
                        (20, frame_bgr.shape[0] - 46), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255, 255, 255), 1)
                    put_text(
                        frame_bgr,
                        mode_text,
                        (20, frame_bgr.shape[0] - 20),
                        cv2.FONT_HERSHEY_SIMPLEX,
                        0.60,
                        (255, 255, 255),
                        2,
                    )

                    channel.publish(dict(player=int(player_detected), calibrated=int(crouch.calibrated),
                        crouch=int(crouch.is_crouching), progress=f'{crouch.calibration_progress:.2f}',
                        breach=int(current_breach_state), mode=int(motion_mode), source_fps=f'{live_fps:.2f}'), frame_bgr)
                if time.monotonic() - last_shared_reload > .25:
                    last_shared_reload = time.monotonic()
                    shared = load_section('Camera', config)
                    for key_name in ('shoulder_enter_ratio','shoulder_exit_ratio','head_enter_ratio','head_exit_ratio'):
                        value = shared.get('crouch_'+key_name)
                        if type(value) in (int,float) and math.isfinite(value) and 0<=value<=2:
                            setattr(crouch_detector,key_name,float(value))
                    visibility = shared.get('minimum_visibility', minimum_visibility)
                    if type(visibility) in (int,float) and math.isfinite(visibility) and .1<=visibility<=1:
                        crouch_detector.minimum_visibility = pose_selector.minimum_visibility = float(visibility)
                preview_key = None
                if not embedded:
                    cv2.imshow(WINDOW_NAME, frame_bgr)
                    preview_key_code = cv2.waitKey(1) & 0xFF
                    preview_key = chr(preview_key_code).lower() if preview_key_code != 255 else None
                commands = channel.commands()
                local_key = (read_console_key() or preview_key) if not embedded else None
                if local_key:
                    commands.append(local_key)
                for key in commands:
                    if key == "p":
                        pose_selector.reset()
                        breach_detector.reset()
                    elif key in ("c4", "1") or (not embedded and key in ("2", "3")):
                        c4_preview = key == "c4"
                        motion_mode = MotionMode.BREACH if c4_preview else MotionMode(int(key))
                        breach_detector.reset()
                        hammer_detector.reset()
                        kick_detector.reset()
                        print(f"[MOTION MODE] {'C4 PREVIEW' if c4_preview else motion_mode.name}")
                    elif key == "r":
                        save_section('CameraCalibration', dict(baseline=None))
                        crouch_detector.reset()
                        pose_selector.reset()
                        breach_detector.reset()
                        hammer_detector.reset()
                        kick_detector.reset()
                        ready_sent = False
                        last_calibration_send_time = float("-inf")
                        osc_client.send_message(crouch_address, 0)
                        print("CROUCH RECALIBRATION STARTED → STAND(0)")
                    elif key in ("q", "\x1b"):
                        return

    finally:
        try:
            osc_client.send_message(
                crouch_address,
                0,
            )
            osc_client.send_message(breach_address, False)
        except Exception:
            pass

        capture.release()
        if not embedded:
            cv2.destroyAllWindows()


def main(**kwargs):
    from device_settings import DeviceChannel
    channel = DeviceChannel('camera')
    try:
        return _main(channel=channel, **kwargs)
    finally:
        channel.close()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("MediaPipe Motion Bridge 종료")
    except Exception as error:
        print(f"[ERROR] {error}")
        raise
