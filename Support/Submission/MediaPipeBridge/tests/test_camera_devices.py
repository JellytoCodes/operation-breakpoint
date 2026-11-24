import unittest
from unittest.mock import patch

from camera_devices import resolve_camera, selected_camera_config, configured_camera_config


class CameraIdentityTests(unittest.TestCase):
    def test_xwf_name_resolves_new_index_without_opening_droidcam(self):
        devices = [dict(index=0, name='DroidCam Video', id='droid'),
                   dict(index=4, name='XWF-1080p6 Camera', id='xwf-port-2')]
        with patch('camera_devices.enumerate_cameras', return_value=devices):
            result = configured_camera_config(dict(camera_name='XWF-1080p6 Camera', camera_index=2))
        self.assertEqual(result['camera_index'], 4)
        self.assertFalse(result['camera_auto_fallback'])

    def test_missing_or_ambiguous_xwf_never_falls_back(self):
        for devices in ([], [dict(index=i, name='XWF-1080p6 Camera', id=str(i)) for i in (1, 2)]):
            with self.subTest(devices=devices), patch('camera_devices.enumerate_cameras', return_value=devices):
                with self.assertRaises(RuntimeError):
                    configured_camera_config(dict(camera_name='XWF-1080p6 Camera'))

    def test_explicit_identity_remains_strict_even_when_model_name_matches(self):
        with patch('camera_devices.enumerate_cameras', return_value=[dict(index=2, name='XWF-1080p6 Camera', id='other')]):
            with self.assertRaises(RuntimeError):
                configured_camera_config(dict(camera_name='XWF-1080p6 Camera', camera_id='chosen'))

    def test_identity_survives_index_changes_and_duplicate_friendly_names(self):
        devices = [dict(index=0, name='USB Camera', id='other'),
                   dict(index=3, name='USB Camera', id='chosen')]
        self.assertEqual(resolve_camera('chosen', devices), devices[1])
        with patch('camera_devices.enumerate_cameras', return_value=devices):
            original = dict(camera_index=2, camera_backend='MSMF', camera_auto_fallback=True, osc_port=9000)
            selected = selected_camera_config(original, 'chosen')
        self.assertEqual(selected, dict(camera_index=3, camera_backend='DSHOW', camera_auto_fallback=False, osc_port=9000))
        self.assertEqual(original['camera_index'], 2)
        self.assertTrue(original['camera_auto_fallback'])

    def test_disconnected_identity_fails_instead_of_opening_same_index(self):
        with patch('camera_devices.enumerate_cameras', return_value=[dict(index=2, name='Other', id='other')]):
            with self.assertRaisesRegex(RuntimeError, '카메라'):
                selected_camera_config(dict(camera_index=2), 'disconnected')
