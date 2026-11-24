import json
import os
from pathlib import Path
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'OpenFire'))
from device_settings import load_section, save_section, migrate_section, settings_path, DeviceChannel
from aim_adjustment import AimAdjustment
from encoder import EncoderFrame


class SharedCalibrationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.environment = patch.dict(os.environ, OB_DEVICE_SETTINGS=str(Path(self.temp.name) / 'DeviceCalibration.ini'))
        self.environment.start()
        self.addCleanup(self.environment.stop)

    def test_migration_never_replaces_user_calibration(self):
        save_section('Camera', {'camera_index': 4, 'mirror_input': True})
        values = migrate_section('Camera', {'camera_index': 0, 'mirror_input': False, 'minimum_visibility': .6})
        self.assertEqual(values, dict(camera_index=4, mirror_input=True, minimum_visibility=.6))

    def test_concurrent_device_writes_preserve_both_sections(self):
        errors = []
        def write(section):
            try:
                for index in range(12):
                    save_section(section, {'index': index})
            except Exception as error:
                errors.append(error)
        threads = [threading.Thread(target=write, args=(name,)) for name in ('Gun:test', 'Camera')]
        for thread in threads: thread.start()
        for thread in threads: thread.join()
        self.assertFalse(errors)
        self.assertEqual(load_section('Camera')['index'], 11)
        self.assertEqual(load_section('Gun:test')['index'], 11)

    def test_legacy_zero_migrates_and_live_edits_reach_both_tools(self):
        legacy = Path(self.temp.name) / 'legacy.json'
        legacy.write_text(json.dumps({'displays': {'screen': {'trim_mm': 22., 'x_shift': .013}}}), encoding='utf-8')
        a = AimAdjustment(390, path=legacy, display_id='screen', shared=True)
        b = AimAdjustment(390, path=legacy, display_id='screen', shared=True)
        save_section(a.shared_section, dict(trim_mm=36., x_shift=-.02, height_mm=420.))
        a.reload_shared(); b.reload_shared()
        self.assertEqual(a.apply((.5, .5)), b.apply((.5, .5)))
        self.assertEqual(a.trim_mm, 36.)
        self.assertEqual(a.height_mm, 420.)
        a.accept_encoder(EncoderFrame(0, 0)); a.accept_encoder(EncoderFrame(1, 0)); a.flush(force=True)
        b.last_reload = 0; b.reload_shared()
        self.assertEqual(b.trim_mm, 38.)
        self.assertEqual(json.loads(legacy.read_text())['displays']['screen']['trim_mm'], 22.)

    def test_ini_written_by_unreal_keeps_json_and_booleans(self):
        settings_path().write_text('[Camera]\nminimum_visibility=0.75\nmirror_input=true\nplayer_roi={"left":0.3,"right":0.7}\n', encoding='utf-8')
        save_section('Gun', dict(port='COM6'))
        self.assertEqual(load_section('Camera')['player_roi']['left'], .3)
        self.assertIs(load_section('Camera')['mirror_input'], True)

    def test_two_helpers_cannot_own_one_camera(self):
        channel = DeviceChannel('camera')
        try:
            with self.assertRaises(RuntimeError): DeviceChannel('camera')
        finally:
            channel.close()
        DeviceChannel('camera').close()

    def test_stale_commands_are_not_replayed(self):
        from device_settings import runtime_dir
        stale = runtime_dir() / 'camera-old.cmd'
        stale.write_text('r', encoding='utf-8')
        os.utime(stale, (1, 1))
        channel = DeviceChannel('camera')
        try:
            current = runtime_dir() / 'camera-current.cmd'
            current.write_text('p', encoding='utf-8')
            self.assertEqual(channel.commands(), ['p'])
            self.assertEqual(channel.commands(), [])
        finally:
            channel.close()

    def test_camera_mode_switches_follow_request_time_instead_of_uuid_order(self):
        from device_settings import runtime_dir
        channel = DeviceChannel('camera')
        try:
            first, last = runtime_dir() / 'camera-zzz.cmd', runtime_dir() / 'camera-aaa.cmd'
            first.write_text('c4', encoding='utf-8')
            last.write_text('1', encoding='utf-8')
            now = time.time()
            os.utime(first, (now - .1, now - .1))
            os.utime(last, (now, now))
            self.assertEqual(channel.commands(), ['c4', '1'])
        finally:
            channel.close()

    def test_unreal_reading_preview_never_terminates_recognition(self):
        from device_settings import _replace
        with patch('device_settings.os.replace', side_effect=PermissionError), patch('device_settings.time.sleep'):
            self.assertFalse(_replace('preview.tmp', 'preview.bmp', optional=True))
            with self.assertRaises(PermissionError):
                _replace('settings.tmp', 'settings.ini')
        with patch('device_settings.os.replace', side_effect=[PermissionError, None]) as replace, patch('device_settings.time.sleep') as sleep:
            self.assertFalse(_replace('preview.tmp', 'preview.bmp', optional=True))
            self.assertEqual(replace.call_count, 1)
            sleep.assert_not_called()
        with patch('device_settings.os.replace', side_effect=[PermissionError, None]) as replace, patch('device_settings.time.sleep'):
            self.assertTrue(_replace('settings.tmp', 'settings.ini'))
            self.assertEqual(replace.call_count, 2)

    def test_thirty_fps_camera_is_not_throttled_to_ten(self):
        from device_settings import runtime_dir
        channel=DeviceChannel('camera')
        try:
            with patch('device_settings.time.monotonic',side_effect=[1+i/30 for i in range(30)]):
                for frame in range(30):
                    channel.publish({'frame':frame})
                    self.assertIn(f'frame={frame}',(runtime_dir()/'camera.ini').read_text())
        finally:
            channel.close()

    def test_frame_ids_distinguish_same_second_and_new_sessions(self):
        import configparser
        import numpy as np
        from device_settings import runtime_dir
        frame=np.zeros((12,12,3),dtype=np.uint8)
        channel=DeviceChannel('camera')
        ids=[]
        try:
            with patch('device_settings.time.monotonic',side_effect=[1,1.034]):
                for _ in range(2):
                    channel.publish({'source_fps':30},frame)
                    state=configparser.ConfigParser()
                    state.read(runtime_dir()/'camera.ini',encoding='utf-8')
                    ids.append(state['Live']['frame_id'])
            self.assertNotEqual(ids[0],ids[1])
            with patch('device_settings._replace',return_value=False),patch('device_settings.time.monotonic',return_value=2):
                channel.publish({},frame)
            self.assertEqual(channel.frame_id,ids[-1],'A dropped preview must not advertise an unpublished frame')
        finally:
            channel.close()
        replacement=DeviceChannel('camera')
        try:
            replacement.publish({},frame)
            self.assertNotEqual(replacement.frame_id,ids[0],'A restarted helper cannot reuse the previous session ID')
        finally:
            replacement.close()


if __name__ == '__main__':
    unittest.main()
