from copy import deepcopy
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'firmware'))
from set_square import expected_square, write_square


class SquareSettingsTests(unittest.TestCase):
    def test_only_layout_fields_can_change(self):
        before = dict(version='6.1-ca30987-obir1', board='rpipico', usb='abcd',
                      settings=dict(pins={'Camera SDA': '14000000'}, profiles={
                          'IrLayout/0': '01000000', 'IrLayout/1': '00000000',
                          'IrLayout/2': '00000000', 'IrLayout/3': '00000000', 'AdjX/0': 'deadbeef'}))
        original = deepcopy(before)
        after, profiles = expected_square(before)
        self.assertEqual(before, original)
        self.assertEqual(profiles, [0])
        original['settings']['profiles']['IrLayout/0'] = '00000000'
        self.assertEqual(after, original)

    def test_save_ack_and_failed_echo_abort_without_save(self):
        class Port:
            in_waiting = 0
            def __init__(self, bad=False): self.data = bytearray(); self.writes = []; self.bad = bad
            def reset_input_buffer(self): self.data.clear()
            def write(self, data):
                self.writes.append(bytes(data))
                if data == b'\x01\x02': self.data.extend(b'6.1-ca30987-obir1\xferpipico\xfeUSB')
                elif data == b'\xaa\x01': self.data.extend(b'\xaa')
                elif data[:1] == b'\xae': self.data.extend(data[1:-1]+(b'\x01' if self.bad else data[-1:]))
                elif data == b'\xfc': self.data.extend(b'\xfc\x01')
                elif data[:1] == b'\x06': self.data.extend(b'\x97'+data[1:2])
            def read(self, count):
                value = bytes(self.data[:count]); del self.data[:count]; return value
            def flush(self): pass
        with patch('set_square.time.sleep'):
            port = Port()
            write_square(port, [0], 2)
            self.assertEqual(port.writes, [b'\x01\x02', b'\xaa\x01', b'\xaeIrLayout\0\x04\0\0\0\0\0', b'\xfc', b'\x06\x02\0', b'\xfe'])
            port = Port(bad=True)
            with self.assertRaises(ValueError): write_square(port, [0], 2)
            self.assertNotIn(b'\xfc', port.writes)
            self.assertEqual(port.writes[-2:], [b'\xfe', b'\xfe'])


if __name__ == '__main__': unittest.main()
