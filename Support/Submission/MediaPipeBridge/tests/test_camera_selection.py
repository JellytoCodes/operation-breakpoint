import sys
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import mediapipe_motion_bridge as bridge


def camera(opened=True, readable=True):
    capture = Mock()
    capture.isOpened.return_value = opened
    capture.read.return_value = (readable, np.zeros((480, 640, 3), dtype=np.uint8) if readable else None)
    capture.getBackendName.return_value = "DSHOW"
    return capture


class CameraSelectionTests(unittest.TestCase):
    @patch.object(bridge.cv2, "VideoCapture")
    def test_mjpeg_is_negotiated_after_dimensions_and_fps(self, factory):
        capture = camera()
        factory.return_value = capture
        bridge.open_motion_camera(dict(camera_index=2, camera_fourcc='MJPG', frame_width=1280, frame_height=720))
        self.assertEqual([call.args[0] for call in capture.set.call_args_list], [
            bridge.cv2.CAP_PROP_FRAME_WIDTH, bridge.cv2.CAP_PROP_FRAME_HEIGHT,
            bridge.cv2.CAP_PROP_FPS, bridge.cv2.CAP_PROP_FOURCC])

    @patch.object(bridge.cv2, "VideoCapture")
    def test_preferred_camera_is_retained(self, factory):
        preferred = camera()
        factory.return_value = preferred
        self.assertIs(bridge.open_motion_camera({"camera_index": 2}), preferred)
        factory.assert_called_once_with(2, bridge.cv2.CAP_DSHOW)
        preferred.release.assert_not_called()

    @patch.object(bridge.time, "sleep")
    @patch.object(bridge.cv2, "VideoCapture")
    def test_unreadable_preferred_camera_falls_back_and_is_released(self, factory, sleep):
        broken, working = camera(readable=False), camera()
        factory.side_effect = [broken, working]
        self.assertIs(bridge.open_motion_camera({"camera_index": 2}), working)
        broken.release.assert_called_once()
        self.assertEqual(factory.call_args.args, (0, bridge.cv2.CAP_DSHOW))

    @patch.object(bridge.cv2, "VideoCapture")
    def test_alternate_backend_can_deliver_frames(self, factory):
        broken, working = camera(opened=False), camera()
        factory.side_effect = [broken, working]
        self.assertIs(bridge.open_motion_camera({"camera_index": 0, "camera_scan_max_index": 0}), working)
        self.assertEqual(factory.call_args.args, (0, bridge.cv2.CAP_MSMF))
        broken.release.assert_called_once()

    @patch.object(bridge.cv2, "VideoCapture")
    def test_strict_selection_does_not_use_another_camera(self, factory):
        broken = camera(opened=False)
        factory.return_value = broken
        with self.assertRaises(RuntimeError):
            bridge.open_motion_camera({"camera_index": 2, "camera_auto_fallback": False})
        factory.assert_called_once()
        broken.release.assert_called_once()


if __name__ == "__main__":
    unittest.main()
