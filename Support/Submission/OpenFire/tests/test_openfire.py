import json
import math
from pathlib import Path
import socket
import struct
import sys
import threading
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from protocol import InputState, Sender, IDENTITY, TARGETS, encode_frame, fit_calibration, project
from diagnostics import DiagnosticParser, identify, run_diagnostics


class CalibrationTests(unittest.TestCase):
    def test_identity_and_perspective(self):
        self.assertEqual(project(IDENTITY, (.3, .7)), (.3, .7))
        for points in (TARGETS, ((.2, .2), (.85, .15), (.8, .9), (.1, .8))):
            matrix = fit_calibration(points)
            for point, expected in zip(points, TARGETS):
                actual = project(matrix, point)
                self.assertAlmostEqual(actual[0], expected[0])
                self.assertAlmostEqual(actual[1], expected[1])
        matrix = fit_calibration(((.2, .2), (.8, .2), (.8, .8), (.2, .8)))
        self.assertAlmostEqual(project(matrix, (.5, .5))[0], .5)
        self.assertLess(project(matrix, (0., 0.))[0], 0.)  # Do not clamp an off-screen shot onto a target.

    def test_invalid_capture_sets(self):
        for points in ([(.5, .5)]*4, ((0., 0.), (1., 1.), (1., 0.), (0., 1.)),
                       ((0., 0.), (.1, .1), (.2, .2), (.3, .3)),
                       ((0., 0.), (math.nan, .1), (.8, .8), (0., 1.)),
                       ((0., 0.), (1.1, 0.), (1., 1.), (0., 1.))):
            with self.subTest(points=points), self.assertRaises(ValueError):
                fit_calibration(points)
        with self.assertRaises(ValueError):
            project((1, 0, 0, 0, 1, 0, -2, 0), (.5, .5))


class InputTests(unittest.TestCase):
    def state(self):
        return InputState(device='test', connected=True, enabled=True)

    def test_windows_coordinate_range_and_relative_motion(self):
        state = self.state()
        state.raw_mouse(True, 65535, 32767, 0)
        self.assertEqual(state.point[0], 1.)
        self.assertAlmostEqual(state.point[1], .5, places=4)
        state.raw_mouse(False, -200, 600, 0)
        self.assertEqual(state.point[0], 1.)
        state.raw_mouse(True, -1, 0, 0)
        self.assertEqual(state.point[0], 1.)

    def test_short_tap_repeat_release_and_mapping(self):
        state = self.state()
        state.raw_mouse(True, 32767, 32767, 3)  # complete down/up in a single report
        self.assertEqual(state.counts, [1, 0, 0])
        self.assertFalse(state.frame()['trigger'])
        state.raw_mouse(False, 0, 0, 1)
        state.raw_mouse(False, 0, 0, 1)
        self.assertEqual(state.counts[0], 2)
        state.raw_mouse(False, 0, 0, 2)
        state.mapping = (4, 3, 2)
        state.raw_mouse(False, 0, 0, 1 << 8)
        self.assertTrue(state.frame()['trigger'])
        self.assertEqual(state.counts[0], 3)

    def test_pause_and_disconnect_release_all_actions(self):
        state = self.state()
        state.raw_mouse(True, 20000, 20000, 0x15)
        self.assertTrue(state.frame()['trigger'])
        state.enabled = False
        frame = state.frame()
        self.assertFalse(frame['connected'])
        self.assertFalse(frame['aim_valid'])
        self.assertFalse(frame['trigger'])
        counts = state.counts.copy()
        state.raw_mouse(False, 0, 0, 0x2A)
        state.raw_mouse(False, 0, 0, 0x15)
        self.assertEqual(state.counts, counts)
        state.disconnect()
        self.assertIsNone(state.point)
        self.assertFalse(any(state.buttons))

    def test_stationary_gun_uses_heartbeat_without_inventing_ir_status(self):
        state = self.state()
        state.raw_mouse(True, 10000, 10000, 0)
        first, second = state.frame(), state.frame()
        self.assertEqual(first['x'], second['x'])
        self.assertTrue(second['connected'])
        self.assertNotIn('ir_tracked', second)
        self.assertGreater(second['sequence'], first['sequence'])

    def test_actual_udp_osc_encoding(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as receiver:
            receiver.bind(('127.0.0.1', 0))
            receiver.settimeout(1)
            sender = Sender(receiver.getsockname()[1])
            try:
                value = sender.send(self.state())
                data, _ = receiver.recvfrom(4096)
            finally:
                sender.close()
        self.assertEqual(len(data) % 4, 0)
        self.assertTrue(data.startswith(b'/ob/openfire/input\0'))
        address_end = (data.index(0)+4) & ~3
        self.assertEqual(data[address_end:address_end+4], b',s\0\0')
        self.assertEqual(json.loads(data[address_end+4:].rstrip(b'\0')), value)


class DiagnosticsTests(unittest.TestCase):
    def test_fragmented_coalesced_binary_frames(self):
        coords = (150, -3, 900, 1080, 2000, 300, 400, 500, 960, 540, 700, 300)
        stream = b'noise' + b'\x90\x00' + b'\x96' + struct.pack('<12i', *coords) + b'\x91\x00\xfa\x80'
        parser = DiagnosticParser()
        result = []
        for byte in stream:
            result.extend(parser.feed(bytes((byte,))))
        self.assertEqual(result, [('button', (0, True)), ('camera', coords), ('button', (0, False)), ('error', 128)])
        self.assertFalse(parser.buffer)
        self.assertEqual(parser.discarded, 5)

    def test_known_version_only(self):
        self.assertEqual(identify(b'6.2-abc123\xferpipico\xfeUSB'), ('6.2-abc123', 'rpipico'))
        self.assertEqual(identify(b'6.1-ca30987\xferpipico\xfeUSB'), ('6.1-ca30987', 'rpipico'))
        for header in (b'3.0\xferpipico\xfeUSB', b'6.0\xferpipico\xfeUSB', b'6.3\xferpipico\xfeUSB',
                       b'7.0\xferpipico\xfeUSB', b'hello', b'6.2\xfe../bad\xfeUSB'):
            with self.subTest(header=header), self.assertRaises(ValueError):
                identify(header)

    def test_interrupted_handshake_always_undocks_without_flash_write(self):
        class Port:
            in_waiting = 0
            def __init__(self): self.writes = []; self.closed = False
            def reset_input_buffer(self): pass
            def write(self, data):
                self.writes.append(data)
                if data == b'\x01\x02':
                    stop.set()
            def flush(self): pass
            def close(self): self.closed = True
        port = Port()
        stop = threading.Event()
        with patch('serial.Serial', return_value=port):
            run_diagnostics('COM9', stop, lambda *_: None)
        self.assertEqual(port.writes, [b'\x01\x02', b'\xfe'])
        self.assertTrue(port.closed)

    def test_actual_61_handshake_stream_and_undock_without_flash_commands(self):
        # Model the exact version in the user's screenshot, with short serial
        # reads and a full binary frame containing marker-valued payload bytes.
        coords = (150, 200, 1800, 200, 150, 900, 1800, 900, 1200, 650, 960, 550)
        stop = threading.Event()
        events = []
        class Port:
            def __init__(self): self.buffer = bytearray(); self.writes = []; self.closed = False
            @property
            def in_waiting(self): return len(self.buffer)
            def reset_input_buffer(self): self.buffer.clear()
            def read(self, count):
                if not self.buffer:
                    import time
                    time.sleep(.01)
                    return b''
                data = bytes(self.buffer[:min(count, 7)])
                del self.buffer[:len(data)]
                return data
            def write(self, data):
                self.writes.append(data)
                if data == b'\x01\x02': self.buffer.extend(b'6.1-ca30987\xferpipico\xfeUSB')
                if data == b'\x05\x01': self.buffer.extend(b'\x96' + struct.pack('<12i', *coords))
            def flush(self): pass
            def close(self): self.closed = True
        port = Port()
        def emit(kind, value):
            events.append((kind, value))
            if kind == 'camera': stop.set()
        with patch('serial.Serial', return_value=port):
            run_diagnostics('COM9', stop, emit)
        self.assertEqual(events, [('identity', ('6.1-ca30987', 'rpipico')), ('camera', coords)])
        self.assertEqual(port.writes, [b'\x01\x02', b'\x05\x01', b'\xfe'])
        self.assertTrue(port.closed)

    def test_unknown_version_still_undocks_without_starting_camera(self):
        stop = threading.Event()
        class Port:
            def __init__(self): self.buffer = bytearray(b'7.0-test\xferpipico\xfeUSB'); self.writes = []
            @property
            def in_waiting(self): return len(self.buffer)
            def reset_input_buffer(self): pass
            def read(self, count):
                if not self.buffer:
                    import time
                    time.sleep(.01)
                    return b''
                data = bytes(self.buffer[:count]); del self.buffer[:count]; return data
            def write(self, data): self.writes.append(data)
            def flush(self): pass
            def close(self): pass
        port = Port()
        with patch('serial.Serial', return_value=port), self.assertRaises(ValueError):
            run_diagnostics('COM9', stop, lambda *_: None)
        self.assertEqual(port.writes, [b'\x01\x02', b'\xfe'])

    def test_camera_unavailable_header_retries_and_records_both_attempts(self):
        good_header = b'6.1-ca30987-obir3\xferpipico\xfeUSB'
        bad_header = good_header + b'\xfe\xfa'

        class Port:
            def __init__(self, header):
                self.buffer = bytearray(header)
                self.writes = []
                self.closed = False
            @property
            def in_waiting(self): return len(self.buffer)
            def reset_input_buffer(self): pass
            def read(self, count):
                if not self.buffer:
                    import time
                    time.sleep(.01)
                    return b''
                data = bytes(self.buffer[:count]); del self.buffer[:count]; return data
            def write(self, data): self.writes.append(data)
            def flush(self): pass
            def close(self): self.closed = True
        for second_header in (good_header, bad_header):
            with self.subTest(second_header=second_header):
                stop = threading.Event()
                results = []
                first, second = Port(bad_header), Port(second_header)
                ports = iter((first, second))

                def open_port(*_args, **_kwargs):
                    port = next(ports)
                    if port is second:
                        self.assertTrue(first.closed)
                    return port

                def run():
                    with patch('serial.Serial', side_effect=open_port):
                        run_diagnostics('COM5', stop, lambda *_: stop.set(),
                                        attempts=2, on_handshake=results.append)

                if second_header == bad_header:
                    with self.assertRaisesRegex(RuntimeError, 'IR 카메라 통신 오류'):
                        run()
                else:
                    run()
                self.assertEqual(first.writes, [b'\x01\x02', b'\xfe'])
                expected = [b'\x01\x02', b'\x0a', b'\x05\x01', b'\xfe'] if second_header == good_header else first.writes
                self.assertEqual(second.writes, expected)
                self.assertTrue(second.closed)
                self.assertEqual([r['attempt'] for r in results], [1, 2])
                self.assertEqual([r['outcome'] for r in results],
                                 ['failed', 'connected' if second_header == good_header else 'failed'])
                self.assertIn('IR 카메라 통신 오류', results[0]['error'])
                for result, header in zip(results, (bad_header, second_header)):
                    self.assertEqual(result['port'], 'COM5')
                    self.assertEqual(bytes.fromhex(result['raw_response_hex']), header)
                    self.assertGreaterEqual(result['port_open_ms'], 0)
                    self.assertGreaterEqual(result['response_ms'], result['first_byte_ms'])
                    self.assertGreaterEqual(result['elapsed_ms'], result['response_ms'])

    def test_stream_timeout_after_handshake_closes_port_for_recovery(self):
        header = b'6.1-ca30987-obir3\xferpipico\xfeUSB'
        results = []
        port = Mock()
        port.in_waiting = 1

        def read(_count):
            import time
            if port.read.call_count == 1:
                return header
            time.sleep(.02)
            return b''

        port.read.side_effect = read
        with patch('serial.Serial', return_value=port) as open_port:
            with self.assertRaises(TimeoutError):
                run_diagnostics('COM5', threading.Event(), lambda *_: None, attempts=2,
                                stream_timeout=.01, on_handshake=results.append)
        self.assertEqual([result['outcome'] for result in results], ['connected'])
        open_port.assert_called_once()
        port.write.assert_called_with(b'\xfe')
        port.close.assert_called_once()


if __name__ == '__main__':
    unittest.main()
