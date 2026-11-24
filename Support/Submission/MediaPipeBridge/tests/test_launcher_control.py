import json
import os
import uuid
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch
from types import SimpleNamespace

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from launcher_control import LauncherControl
import mediapipe_motion_bridge as bridge


class LauncherControlTests(unittest.TestCase):
    def test_preview_topmost_flag_can_be_explicitly_cleared(self):
        with patch.object(bridge.cv2,"setWindowProperty") as set_property:
            self.assertTrue(bridge.set_window_always_on_top(False))
            set_property.assert_called_once_with(bridge.WINDOW_NAME,bridge.cv2.WND_PROP_TOPMOST,0)
    def test_unmanaged_mode_and_incomplete_arguments(self):
        self.assertFalse(LauncherControl().should_stop())
        with self.assertRaises(ValueError):
            LauncherControl("ready.json")

    def test_atomic_readiness_and_stop_request(self):
        with tempfile.TemporaryDirectory() as folder:
            ready, stop = Path(folder) / "ready.json", Path(folder) / "stop"
            control = LauncherControl(ready, stop, "test-session")
            self.assertFalse(ready.exists())
            control.calibrating(.5)
            value = json.loads(ready.read_text())
            self.assertEqual(value['status'], 'calibrating')
            self.assertFalse(value['calibrated'])
            self.assertEqual(value['calibration_progress'], .5)
            self.assertFalse(control.should_stop())
            control.ready()
            self.assertEqual(json.loads(ready.read_text())["token"], "test-session")
            self.assertFalse(ready.with_suffix(".tmp").exists())
            stop.touch()
            self.assertTrue(control.should_stop())

    def run_bridge(self, inference_error=None, player=True, config_changes=None, keys=None, *, embedded=False):
        events = []
        capture = Mock()
        capture.read.return_value = (True, np.zeros((480, 640, 3), dtype=np.uint8))
        capture.get.return_value = 0.0
        landmarker = Mock()
        def infer(*args):
            events.append("inference")
            if inference_error:
                raise inference_error
            result = Mock()
            landmarks = [SimpleNamespace(x=.5, y=.1, z=0., visibility=1.) for _ in range(33)]
            for i in (11, 12): landmarks[i].y = .3
            for i in (23, 24): landmarks[i].y = .7
            result.pose_landmarks = [landmarks] if player else []
            return result
        landmarker.detect_for_video.side_effect = infer
        def ready():
            events.append("ready")
        factory = bridge.mp.tasks.vision.PoseLandmarker
        settings_path = Path(os.environ.get('TEMP','.')) / ('CameraTest-'+uuid.uuid4().hex+'.ini')
        self.addCleanup(lambda: settings_path.unlink(missing_ok=True))
        with patch.dict(os.environ, OB_DEVICE_SETTINGS=str(settings_path)), patch.object(bridge, 'load_config', return_value={'crouch_calibration_frames':10, **(config_changes or {})}), \
             patch.object(bridge, "open_motion_camera", return_value=capture), \
             patch.object(bridge, "SimpleUDPClient") as osc, \
             patch.object(factory, "create_from_options") as create, \
             patch.object(bridge.cv2, "namedWindow"), patch.object(bridge.cv2, "imshow"), \
             patch.object(bridge.cv2, "waitKey", return_value=-1), \
             patch.object(bridge.cv2, "setWindowProperty") as topmost, \
             patch.object(bridge.cv2, "destroyAllWindows"), patch.object(bridge, "read_console_key", return_value=None, side_effect=keys), \
             patch.object(bridge, "put_text") as text:
            create.return_value.__enter__.return_value = landmarker
            try:
                bridge.main(on_ready=ready, should_stop=lambda: events.count('inference') >= 11,
                            embedded=embedded, should_preview=lambda: False)
            finally:
                capture.release.assert_called_once()
                if embedded:
                    topmost.assert_not_called()
                    osc.assert_called_once_with('127.0.0.1', 8000)
                else:
                    topmost.assert_called_once_with(bridge.WINDOW_NAME, bridge.cv2.WND_PROP_TOPMOST, 0)
                self.osc_messages = osc.return_value.send_message.call_args_list
                self.preview_labels = [call.args[1] for call in text.call_args_list]
        return events

    def test_ready_waits_for_calibration_and_stop_releases_camera(self):
        self.assertEqual(self.run_bridge(), ["inference"] * 10 + ["ready", "inference"])

    def test_embedded_loopback_ignores_old_network_settings_and_sends_without_preview(self):
        self.run_bridge(embedded=True, config_changes={'osc_ip': '192.0.2.1', 'osc_port': 9999,
                        'crouch_address': '/old-crouch', 'heartbeat_address': '/old-heartbeat',
                        'initial_motion_mode': 2})
        addresses = {call.args[0] for call in self.osc_messages}
        self.assertTrue({'/system/mediapipe/heartbeat', '/motion/crouch', '/mediapipe/breach/guide'} <= addresses)
        guide = next(call.args[1] for call in self.osc_messages if call.args[0] == '/mediapipe/breach/guide')
        self.assertEqual(guide[4], 1.)
        self.assertFalse(self.preview_labels)

    def test_c4_preview_suppresses_legacy_breach_and_restores_two_hand_mode(self):
        with patch.object(bridge.BreachDetector, 'update', return_value=(True, True, True)):
            self.run_bridge(keys=['c4'] + [None] * 4 + ['1'] + [None] * 5)
        states = [call.args[1] for call in self.osc_messages if call.args[0] == '/motion/breach']
        self.assertEqual(states, [True, False, True, False])  # Final false is the normal bridge shutdown.
        self.assertTrue(any('C4 부착 (한 손)' in label for label in self.preview_labels))
        self.assertTrue(any('브리칭 (양손)' in label for label in self.preview_labels))

    def test_successful_inference_without_player_never_announces_ready(self):
        self.assertNotIn('ready', self.run_bridge(player=False))

    def test_old_topmost_true_config_is_overridden_in_standalone_mode(self):
        self.run_bridge(config_changes={'always_on_top':True})

    def test_failed_inference_never_announces_ready_and_releases_camera(self):
        with self.assertRaisesRegex(RuntimeError, "model failed"):
            self.run_bridge(RuntimeError("model failed"))


if __name__ == "__main__":
    unittest.main()
