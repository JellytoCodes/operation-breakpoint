from pathlib import Path
from types import SimpleNamespace
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from device_port import PortSelectionError, select_port, wait_for_port


def port(name, vid=0xF143, pid=1):
    return SimpleNamespace(device=name, vid=vid, pid=pid)


class DevicePortTests(unittest.TestCase):
    def test_auto_follows_usb_renumbering_and_ignores_unrelated_ports(self):
        self.assertEqual(select_port([port('COM2', 0x1234), port('COM9')]), 'COM9')

    def test_explicit_missing_port_reports_detected_openfire_port(self):
        with self.assertRaisesRegex(PortSelectionError, 'COM6.*COM9'):
            select_port([port('COM9')], 'COM6')

    def test_explicit_matching_port_accepts_lowercase(self):
        self.assertEqual(select_port([port('COM6')], 'com6'), 'COM6')

    def test_unrelated_usb_and_bootloader_serial_are_not_opened(self):
        for device in (port('COM6', 0x1234), port('COM6', 0x2E8A, 3)):
            with self.assertRaises(PortSelectionError):
                select_port([device], 'COM6')
            with self.assertRaisesRegex(PortSelectionError, 'BOOTSEL'):
                select_port([device])

    def test_missing_board_reports_usb_absence(self):
        with self.assertRaisesRegex(PortSelectionError, 'USB.*BOOTSEL'):
            select_port([])

    def test_multiple_boards_require_explicit_selection(self):
        devices = [port('COM6'), port('COM9')]
        with self.assertRaisesRegex(PortSelectionError, '여러 개'):
            select_port(devices)
        self.assertEqual(select_port(devices, 'COM9'), 'COM9')

    def test_wait_allows_usb_enumeration_to_finish(self):
        with patch('device_port.list_ports.comports', side_effect=[[], [port('COM9')]]), \
                patch('device_port.time.sleep'):
            self.assertEqual(wait_for_port(wait_seconds=1), 'COM9')

    def test_wait_deadline_reports_missing_port(self):
        with patch('device_port.list_ports.comports', return_value=[]), \
                patch('device_port.time.monotonic', side_effect=[0., 5.]):
            with self.assertRaises(PortSelectionError):
                wait_for_port(wait_seconds=5)


if __name__ == '__main__':
    unittest.main()
