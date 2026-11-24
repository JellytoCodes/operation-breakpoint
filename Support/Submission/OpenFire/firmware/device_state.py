"""Read-only settings snapshot; optional RP2040 1200-baud BOOTSEL handoff.

Never sends save, erase, commit, pin changes or feedback commands. The snapshot
uses OpenFIRE's echoed key/length/value protocol at the pinned 6.1 revision.
"""
import argparse
import json
from pathlib import Path
import sys
import time
import serial
from serial.tools import list_ports

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from diagnostics import identify


def exact(port, count):
    data = port.read(count)
    if len(data) != count:
        raise RuntimeError(f'Incomplete settings reply ({len(data)}/{count})')
    return data


def snapshot(name):
    device = next((p for p in list_ports.comports() if p.device == name and p.vid == 0xf143), None)
    if device is None:
        raise RuntimeError('The selected port is not an OpenFIRE device')
    with serial.Serial(name, 9600, timeout=2, write_timeout=1) as port:
        port.dtr = True
        port.reset_input_buffer()
        port.write(b'\x01\x02')
        header = bytearray()
        deadline = time.monotonic()+3
        port.timeout = .15
        while time.monotonic() < deadline:
            data = port.read(max(1, min(256, port.in_waiting)))
            if data: header.extend(data)
            elif header: break
        version, board = identify(bytes(header))
        if board != 'rpipico':
            port.write(b'\xfe')
            raise RuntimeError(f'Expected rpipico, got {board}')
        result = dict(version=version, board=board, usb=bytes(header).split(b'\xfe', 2)[2].hex(),
                      serial=device.serial_number, settings={})
        port.timeout = 2
        try:
            for group, command in (('pins', 0xc8), ('toggles', 0xc9), ('settings', 0xca), ('profiles', 0xcb), ('buttons', 0xcc)):
                port.write(bytes((command,)))
                records = {}
                for _ in range(1000):
                    first = exact(port, 1)
                    if first == b'\xfe': break
                    buf = bytearray(first)
                    while buf[-1] != 0:
                        if len(buf) > 64: raise ValueError('Invalid settings key')
                        buf.extend(exact(port, 1))
                    key = bytes(buf[:-1]).decode('ascii')
                    length_byte = exact(port, 1)
                    size = length_byte[0]
                    if size > 64: raise ValueError('Invalid settings value size')
                    buf.extend(length_byte)
                    if group == 'profiles' and key != 'CurrentProf':
                        profile = exact(port, 1)
                        buf.extend(profile)
                        key += '/'+str(profile[0])
                    value = exact(port, size)
                    buf.extend(value)
                    records[key] = value.hex()
                    port.write(buf)  # Acknowledge verbatim, not a setting write.
                else: raise ValueError('Too many settings records')
                result['settings'][group] = records
        finally:
            port.write(b'\xfe')
            port.flush()
        return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--bootloader', action='store_true')
    args = parser.parse_args()
    result = snapshot(args.port)
    if args.output.exists():
        raise FileExistsError('Refusing to overwrite an existing device snapshot')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(dict(version=result['version'], board=result['board'],
                          records={k: len(v) for k, v in result['settings'].items()})))
    if args.bootloader:
        with serial.Serial(args.port, 1200, timeout=.2) as port:
            port.dtr = True
            time.sleep(.1)
            port.dtr = False
        print('Requested BOOTSEL; no flash data has been written.')
