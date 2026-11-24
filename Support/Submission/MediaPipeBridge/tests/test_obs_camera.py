import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import obs_camera


class OBSSharingTests(unittest.TestCase):
    def setUp(self):
        self.config = dict(camera_index=2, camera_obs_share=True, camera_fourcc='MJPG', camera_auto_fallback=False)
        self.cameras = [dict(index=2, name='XWF', id='physical'), dict(index=4, name='OBS Virtual Camera', id='virtual')]
        self.scene = {'virtual-camera': {'type2': 2, 'source': 'Player'},
                      'sources': [{'name': 'Player', 'id': 'dshow_input', 'settings': {'video_device_id': 'XWF:path'}}]}

    def route(self):
        with patch.object(obs_camera, 'virtual_camera_ready', return_value=True), \
             patch.object(obs_camera, 'active_collection', return_value=self.scene), \
             patch.object(obs_camera, 'enumerate_cameras', return_value=self.cameras):
            return obs_camera.shared_camera_config(self.config)

    def test_same_selected_camera_uses_live_virtual_output_without_usb_renegotiation(self):
        selected = self.route()
        self.assertEqual(selected['camera_index'], 4)
        self.assertEqual(selected['camera_fourcc'], '')
        self.assertFalse(selected['camera_auto_fallback'])
        self.assertEqual(self.config['camera_index'], 2)  # Original launcher choice remains intact.

    def test_obs_off_keeps_original_camera(self):
        with patch.object(obs_camera, 'virtual_camera_ready', return_value=False):
            self.assertIs(obs_camera.shared_camera_config(self.config), self.config)

    def test_program_or_scene_output_is_never_used_for_pose_tracking(self):
        for kind in (0, 1, 3):
            self.scene['virtual-camera']['type2'] = kind
            self.assertIs(self.route(), self.config)

    def test_other_physical_camera_selection_is_preserved(self):
        self.cameras[0]['name'] = 'Other webcam'
        self.assertIs(self.route(), self.config)

    def test_identical_camera_models_do_not_override_explicit_selection(self):
        self.cameras.append(dict(index=5, name='XWF', id='other physical'))
        self.assertIs(self.route(), self.config)

    def test_display_source_is_rejected(self):
        self.scene['sources'][0]['id'] = 'monitor_capture'
        self.assertIs(self.route(), self.config)

    def test_absent_virtual_device_keeps_original_selection(self):
        self.cameras.pop()
        self.assertIs(self.route(), self.config)

    def test_disabled_sharing_never_checks_obs(self):
        self.config['camera_obs_share'] = False
        with patch.object(obs_camera, 'virtual_camera_ready') as ready:
            self.assertIs(obs_camera.shared_camera_config(self.config), self.config)
            ready.assert_not_called()


if __name__ == '__main__':
    unittest.main()
