from pathlib import Path
import sys
import threading
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from aim_analysis import AimSample
from camera_view import CameraFrame
from ir_test_bridge import IRTestInput, maintain_connection
from test_led_detection import frame


class IRTestBridgeTests(unittest.TestCase):
    def test_com_loss_waits_and_recovers_same_controller_on_new_port(self):
        stop, wake = threading.Event(), threading.Event()
        devices = [
            [SimpleNamespace(device='COM5', vid=0xF143, pid=1, serial_number='target')],
            [SimpleNamespace(device='COM5', vid=0xF143, pid=1, serial_number='other')],
            [SimpleNamespace(device='COM8', vid=0xF143, pid=1, serial_number='target')],
        ]
        events, messages = [], []

        def run(port, signal, emit, **_options):
            emit('identity', ('6.1-obir3', 'rpipico'))
            emit('detection', frame(()))
            if port == 'COM5':
                raise OSError('ClearCommError failed')
            stop.set()

        with patch('ir_test_bridge.list_ports.comports', side_effect=devices), \
             patch('ir_test_bridge.run_diagnostics', side_effect=run), patch.object(wake, 'wait'):
            maintain_connection('COM5', stop, wake, lambda *args: messages.append(args), lambda _: None,
                                lambda event, **fields: events.append((event, fields['port'])))
        self.assertEqual(events, [('connecting', 'COM5'), ('connected', 'COM5'), ('disconnected', 'COM5'),
                                  ('port_wait', 'COM5'), ('connecting', 'COM8'), ('reconnected', 'COM8')])
        self.assertIn(('link_lost', ('COM5', 'ClearCommError failed')), messages)

    def test_manual_reconnect_restarts_the_active_worker(self):
        stop, wake = threading.Event(), threading.Event()
        device = SimpleNamespace(device='COM5', vid=0xF143, pid=1, serial_number='target')
        events = []

        def run(port, signal, emit, **_options):
            self.assertFalse(signal.is_set())
            emit('identity', ('6.1-obir3', 'rpipico'))
            emit('detection', frame(()))
            if events[-1] == 'connected':
                signal.set()
            else:
                stop.set()

        with patch('ir_test_bridge.list_ports.comports', return_value=[device]), \
             patch('ir_test_bridge.run_diagnostics', side_effect=run):
            maintain_connection('COM5', stop, wake, lambda *_: None, lambda _: None,
                                lambda event, **_: events.append(event))
        self.assertEqual(events, ['connecting', 'connected', 'manual_restart', 'connecting', 'reconnected'])

    def test_ui_trigger_tap_and_hold_work_without_leds_but_never_queue_a_shot(self):
        bridge=self.ready(frame(()))
        bridge.accept('button',(0,True),10.01)
        packet=bridge.update(10.02).frame()
        self.assertTrue(packet['interaction_trigger'])
        self.assertEqual(packet['interaction_trigger_count'],1)
        self.assertFalse(packet['aim_valid'] or packet['trigger'])
        self.assertEqual(packet['trigger_count'],0)
        for now in (10.03,10.04,10.05):
            bridge.accept('button',(0,True),now)
        self.assertEqual(bridge.input.interaction_trigger_count,1)
        raw=frame(seq=2)
        bridge.accept('detection',raw,10.06)
        bridge.accept('sample',AimSample(raw,CameraFrame((0,)*8+(1000,500,0,0))),10.06)
        packet=bridge.update(10.07).frame()
        self.assertTrue(packet['aim_valid'] and packet['interaction_trigger'])
        self.assertFalse(packet['trigger'])
        self.assertEqual(packet['trigger_count'],0)
        bridge.accept('button',(0,False),10.08)
        bridge.accept('button',(0,True),10.09)
        bridge.accept('button',(0,False),10.10)
        packet=bridge.update(10.11).frame()
        self.assertFalse(packet['interaction_trigger'])
        self.assertEqual((packet['interaction_trigger_count'],packet['trigger_count']),(2,1))

    def test_led_loss_preserves_ui_hold_and_magazine_until_physical_release(self):
        bridge=self.ready()
        bridge.accept('button',(0,True),10.01)
        bridge.accept('button',(1,True),10.02)
        bridge.accept('detection',frame((),seq=2),10.03)
        packet=bridge.update(10.04).frame()
        self.assertTrue(packet['interaction_trigger'] and packet['reload'])
        self.assertFalse(packet['trigger'] or packet['aim_valid'])
        self.assertEqual((packet['interaction_trigger_count'],packet['reload_count']),(1,1))
        bridge.update(10.40)
        self.assertFalse(bridge.input.interaction_trigger)
        bridge.accept('detection',frame((),seq=3),10.41)
        bridge.accept('button',(0,True),10.42)
        self.assertEqual(bridge.input.interaction_trigger_count,1)
        self.assertFalse(bridge.input.interaction_trigger)
        bridge.accept('button',(0,False),10.43)
        bridge.accept('button',(0,True),10.44)
        self.assertEqual(bridge.input.interaction_trigger_count,2)

    def test_magazine_hold_survives_missing_aim_and_requires_removal(self):
        bridge=self.ready(frame((0,1,2)))
        bridge.accept('button',(1,True),10.01)
        for i in range(10):
            bridge.update(10.02+i*.001)
            bridge.accept('button',(1,True),10.02+i*.001)
        self.assertEqual(bridge.input.counts[1],1)
        bridge.update(10.30)  # The physical magazine stays inserted while disconnected.
        bridge.accept('button',(1,True),10.31)
        bridge.accept('detection',frame(seq=2),10.32)
        bridge.accept('button',(1,True),10.33)
        self.assertEqual(bridge.input.counts[1],1)
        bridge.accept('button',(1,False),10.34)
        bridge.accept('button',(1,True),10.35)
        self.assertEqual(bridge.input.counts[1],2)

    def test_offline_magazine_press_is_not_queued(self):
        bridge=self.ready()
        bridge.update(10.30)
        bridge.accept('button',(1,True),10.31)
        bridge.accept('detection',frame(seq=2),10.32)
        bridge.accept('button',(1,True),10.33)
        self.assertEqual(bridge.input.counts[1],0)
        bridge.accept('button',(1,False),10.34)
        bridge.accept('button',(1,True),10.35)
        self.assertEqual(bridge.input.counts[1],1)

    def test_native_reload_and_calibration_counters_work_without_visible_aim(self):
        bridge=self.ready(frame((0,1,2)))
        bridge.update(10.01)
        bridge.accept('button',(1,True),10.02);bridge.accept('button',(1,False),10.03)
        bridge.accept('button',(2,True),10.04);bridge.accept('button',(2,False),10.05)
        result=bridge.update(10.06).frame()
        self.assertEqual(result['reload_count'],1);self.assertEqual(result['calibrate_count'],1)
        self.assertEqual(result['trigger_count'],0);self.assertFalse(result['aim_valid'])
    def ready(self, detection=None, xy=(1000, 500)):
        bridge = IRTestInput('COM6')
        bridge.accept('identity', ('6.1-ca30987-obir1', 'rpipico'), 10.)
        raw = detection if detection is not None else frame()
        bridge.accept('detection', raw, 10.)
        sample = AimSample(raw, CameraFrame((0, 0, 0, 0, 0, 0, 0, 0, *xy, 0, 0)))
        bridge.accept('sample', sample, 10.)
        return bridge

    def test_measured_aim_is_real_input_without_imu_calibration(self):
        bridge = self.ready()
        packet = bridge.update(10.05).frame()
        self.assertEqual((packet['x'], packet['y']), (1000/1920, 500/1080))
        self.assertTrue(packet['connected'] and packet['aim_valid'])
        self.assertFalse(packet['demo'] or packet['calibrated'] or packet['trigger'] or packet['reload'] or packet['calibrate'])
        self.assertEqual(packet['trigger_count'], 0)

    def test_trigger_press_release_and_quick_tap_are_counted_once(self):
        bridge = self.ready()
        bridge.accept('button', (0, True), 10.01)
        bridge.accept('button', (0, True), 10.02)
        packet = bridge.update(10.03).frame()
        self.assertTrue(packet['trigger'])
        self.assertEqual(packet['trigger_count'], 1)
        bridge.accept('button', (0, False), 10.04)
        bridge.accept('button', (0, True), 10.05)
        bridge.accept('button', (0, False), 10.06)
        packet = bridge.update(10.07).frame()
        self.assertFalse(packet['trigger'])
        self.assertEqual(packet['trigger_count'], 2)
        self.assertEqual(packet['reload_count'], 0)

    def test_lost_tracking_blocks_shot_until_release_and_new_press(self):
        bridge = self.ready()
        bridge.accept('button', (0, True), 10.26)
        self.assertEqual(bridge.input.counts[0], 0)
        raw = frame(seq=2)
        bridge.accept('detection', raw, 10.27)
        bridge.accept('sample', AimSample(raw, CameraFrame((0,)*8+(1000,500,0,0))), 10.27)
        bridge.accept('button', (0, True), 10.28)
        self.assertFalse(bridge.update(10.29).frame()['trigger'])
        self.assertEqual(bridge.input.counts[0], 0)
        bridge.accept('button', (0, False), 10.30)
        bridge.accept('button', (0, True), 10.31)
        self.assertEqual(bridge.update(10.32).frame()['trigger_count'], 1)

    def test_partial_offscreen_and_unrelated_buttons_cannot_fire(self):
        for bridge in (self.ready(frame((0,1,2))), self.ready(xy=(-200,1300))):
            bridge.accept('button', (0, True), 10.01)
            self.assertEqual(bridge.update(10.02).frame()['trigger_count'], 0)
        bridge = self.ready()
        bridge.accept('button', (1, True), 10.01)
        bridge.accept('button', (12, True), 10.02)
        self.assertFalse(bridge.update(10.03).frame()['trigger'])

    def test_timeout_releases_trigger_output(self):
        bridge = self.ready()
        bridge.accept('button', (0, True), 10.01)
        self.assertTrue(bridge.update(10.02).frame()['trigger'])
        self.assertFalse(bridge.update(10.26).frame()['trigger'])

    def test_partial_corrupt_stale_or_missing_pair_cannot_drive_crosshair(self):
        self.assertFalse(self.ready(frame((0, 1, 2))).update(10.05).frame()['aim_valid'])
        bridge = self.ready()
        self.assertFalse(bridge.update(10.26).frame()['aim_valid'])
        self.assertFalse(bridge.update(10.26).frame()['connected'])
        bridge = self.ready()
        bridge.accept('detection', frame(seq=2), 10.06)
        self.assertFalse(bridge.update(10.07).frame()['aim_valid'])
        bridge.accept('detection_error', 'CRC failed', 10.08)
        self.assertFalse(bridge.update(10.09).frame()['connected'])

    def test_offscreen_coordinate_is_preserved_and_stock_firmware_is_rejected(self):
        packet = self.ready(xy=(-200, 1300)).update(10.05).frame()
        self.assertLess(packet['x'], 0)
        self.assertGreater(packet['y'], 1)
        with self.assertRaises(ValueError): IRTestInput('COM6').accept('identity', ('6.1-ca30987', 'rpipico'), 10.)


if __name__ == '__main__': unittest.main()
