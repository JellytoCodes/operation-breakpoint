from pathlib import Path
import struct
import sys
import threading
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from diagnostics import DiagnosticParser, run_diagnostics
from led_detection import DetectionFrame, LEDTracker, crc16, unique_match
from led_view import draw_detection_scene
from test_camera_view import Canvas

POINTS = ((280, 150), (750, 150), (280, 610), (750, 610))


def frame(visible=(0, 1, 2, 3), order=(2, 0, 3, 1), delta=(0, 0), seq=1):
    points = tuple((POINTS[i][0]+delta[0], POINTS[i][1]+delta[1]) for i in order)
    seen = sum(1 << slot for slot, led in enumerate(order) if led in visible)
    return DetectionFrame(seq, seen, 0, points)


def packet(value):
    data = b'OBIR\x01\x16' + struct.pack('<IBB8H', value.sequence, value.seen, value.layout,
                                         *(v for p in value.slots for v in p))
    return b'\x98'+data+struct.pack('<H', crc16(data))


class DetectionTests(unittest.TestCase):
    def test_custom_firmware_opt_in_and_actual_detection_event(self):
        stop, events = threading.Event(), []
        value = frame((0, 2, 3))
        class Port:
            def __init__(self): self.buffer = bytearray(); self.writes = []; self.closed = False
            @property
            def in_waiting(self): return len(self.buffer)
            def reset_input_buffer(self): self.buffer.clear()
            def read(self, count):
                if not self.buffer:
                    time.sleep(.01)
                    return b''
                data = bytes(self.buffer[:min(count, 3)])
                del self.buffer[:len(data)]
                return data
            def write(self, data):
                self.writes.append(bytes(data))
                if data == b'\x01\x02': self.buffer.extend(b'6.1-ca30987-obir1\xferpipico\xfeUSB')
                if data == b'\x05\x01': self.buffer.extend(packet(value))
            def flush(self): pass
            def close(self): self.closed = True
        port = Port()
        def emit(kind, data):
            events.append((kind, data))
            if kind == 'detection': stop.set()
        with patch('serial.Serial', return_value=port):
            run_diagnostics('COM9', stop, emit)
        self.assertEqual(events, [('identity', ('6.1-ca30987-obir1', 'rpipico')), ('detection', value)])
        self.assertEqual(port.writes, [b'\x01\x02', b'\x0a', b'\x05\x01', b'\xfe'])
        self.assertTrue(port.closed)

    def test_crc_known_vector_and_packet_bytes(self):
        self.assertEqual(crc16(b'123456789'), 0x29b1)
        value = frame(seq=0x94939291)
        self.assertEqual(DetectionFrame.decode(packet(value)), value)
        self.assertEqual(len(packet(value)), 31)

    def test_split_coalesced_extension_with_legacy_and_marker_bytes(self):
        value = frame(seq=0x98979695)
        legacy = b'\x96'+struct.pack('<12i', *range(12))
        stream = legacy+packet(value)+b'\x90\x03'+packet(frame((), seq=2))
        for split in range(len(stream)+1):
            parser = DiagnosticParser()
            events = parser.feed(stream[:split])+parser.feed(stream[split:])
            self.assertEqual(events, [('camera', tuple(range(12))), ('detection', value),
                                      ('button', (3, True)), ('detection', frame((), seq=2))])
            self.assertFalse(parser.buffer)

    def test_corrupt_frame_is_rejected_and_next_frame_survives(self):
        broken = bytearray(packet(frame()))
        broken[11] ^= 1
        parser = DiagnosticParser()
        events = parser.feed(broken+packet(frame((), seq=2)))
        self.assertEqual(events[0][0], 'detection_error')
        self.assertEqual(events[1], ('detection', frame((), seq=2)))
        self.assertEqual(parser.corrupt, 1)

    def test_mask_filters_retained_coordinates_and_zero_is_valid(self):
        value = DetectionFrame(1, 1, 1, ((0, 0), (100, 200), (200, 300), (300, 400)))
        self.assertEqual(value.points, ((0, 0),))
        self.assertEqual(frame(()).points, ())
        for seen, layout, slots in ((16, 1, POINTS), (1, 2, POINTS),
                                    (1, 1, ((0, 900),)*4)):
            with self.assertRaises(ValueError): DetectionFrame(0, seen, layout, slots)


class TrackerTests(unittest.TestCase):
    def setUp(self):
        self.tracker = LEDTracker()
        self.now = 10.

    def update(self, visible=(0, 1, 2, 3), order=(3, 2, 1, 0), delta=(0, 0), count=1):
        for _ in range(count):
            self.now += .05
            self.tracker.update(frame(visible, order, delta), self.now)

    def register(self):
        self.assertTrue(self.tracker.begin(frame(), self.now))
        for led in range(4):
            self.update(tuple(i for i in range(4) if i != led), count=5)
            self.update(count=5)
        self.assertTrue(self.tracker.registered)
        self.assertEqual(self.tracker.positions, list(POINTS))

    def test_cover_registration_and_permuting_slots_preserve_physical_ids(self):
        self.register()
        for delta, order in (((5, -2), (0, 3, 1, 2)), ((12, 8), (3, 1, 0, 2)), ((20, 12), (2, 1, 3, 0))):
            self.update(order=order, delta=delta)
            self.assertEqual(self.tracker.positions, [(x+delta[0], y+delta[1]) for x, y in POINTS])
            self.assertEqual(self.tracker.visible, [True]*4)

    def test_partial_occlusion_and_short_reacquisition_do_not_renumber(self):
        self.register()
        self.update((0, 2, 3), delta=(4, 3), count=3)
        self.assertEqual(self.tracker.visible, [True, False, True, True])
        self.assertEqual(self.tracker.positions[1], POINTS[1])
        self.update(delta=(8, 4))
        self.assertEqual(self.tracker.visible, [True]*4)
        self.assertEqual(self.tracker.positions[1], (758, 154))

    def test_complete_loss_and_large_jump_require_reference(self):
        for failure in ('loss', 'jump', 'time_gap'):
            self.setUp()
            self.register()
            if failure == 'loss': self.update(())
            elif failure == 'jump': self.update(delta=(180, 0))
            else:
                self.now += 1.
                self.update()
            self.assertFalse(self.tracker.registered, failure)
            self.update()
            self.assertFalse(self.tracker.registered, 'Never silently relabel after reference loss')

    def test_long_missing_led_cannot_take_a_new_blob_identity(self):
        self.register()
        self.update((0, 2, 3), count=20)
        self.assertTrue(self.tracker.registered)
        self.update()
        self.assertFalse(self.tracker.registered)

    def test_wrong_or_multiple_covers_do_not_advance_registration(self):
        self.assertTrue(self.tracker.begin(frame(), self.now))
        self.update((0, 2), count=10)
        self.assertEqual(self.tracker.mapping, [])
        self.update((1, 2, 3), count=5)
        self.update(count=5)
        self.update((1, 2, 3), count=10)
        self.assertEqual(len(self.tracker.mapping), 1)
        self.assertIn('이미 등록', self.tracker.message)

    def test_unregistered_or_ambiguous_points_are_never_assigned_by_sorting(self):
        self.update()
        self.assertFalse(self.tracker.registered)
        self.assertEqual(len(self.tracker.unassigned), 4)
        self.assertIsNone(unique_match(((50, 50),), ((45, 50), (55, 50)), 20))

    def test_registered_view_count_states_and_non_crossing_outline(self):
        self.register()
        canvas = Canvas()
        draw_detection_scene(canvas, frame(), self.tracker, fresh=True, fullscreen=True)
        self.assertEqual(sum('detected_point' in i[2].get('tags', ()) for i in canvas.items), 4)
        self.assertEqual(sum(i[2].get('tags') == 'registered_outline' for i in canvas.items), 4)
        # The physical row-major order must not connect opposite corners.
        edges = [i[1] for i in canvas.items if i[2].get('tags') == 'registered_outline']
        self.assertTrue(all(x0 == x1 or y0 == y1 for x0, y0, x1, y1 in edges))
        self.update((0, 2, 3))
        draw_detection_scene(canvas, frame((0, 2, 3)), self.tracker, fresh=True, fullscreen=True)
        self.assertTrue(any(i[2].get('text') == '실제 검출 3 / 4' for i in canvas.items))
        self.assertTrue(any(i[2].get('text') == '2 상단 오른쪽 · 미검출' for i in canvas.items))
        self.assertEqual(sum('detected_point' in i[2].get('tags', ()) for i in canvas.items), 3)
        self.assertEqual(sum('lost_point' in i[2].get('tags', ()) for i in canvas.items), 1)
        draw_detection_scene(canvas, frame(), self.tracker, fresh=False, fullscreen=True)
        self.assertEqual(sum('detected_point' in i[2].get('tags', ()) for i in canvas.items), 0)
        self.assertFalse(any(i[2].get('text', '').endswith(' · 검출') for i in canvas.items))


if __name__ == '__main__':
    unittest.main()
