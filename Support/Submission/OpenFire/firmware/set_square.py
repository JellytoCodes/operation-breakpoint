"""Back up settings, set only IrLayout to Square, save, and compare readback.

Explicit configuration utility for the already verified 6.1-ca30987-obir1 Pico.
Does not flash firmware, reset calibration, alter pins or change USB identity.
"""
import argparse
from copy import deepcopy
import json
from pathlib import Path
import time
import serial
from device_state import snapshot, exact
from diagnostics import identify


def expected_square(before):
    if before['version'] != '6.1-ca30987-obir1' or before['board'] != 'rpipico':
        raise ValueError('Requires the verified obir1 Pico firmware')
    after = deepcopy(before)
    changed = []
    for profile in range(4):
        key = f'IrLayout/{profile}'
        value = before['settings']['profiles'][key]
        if value not in ('00000000', '01000000'):
            raise ValueError('Unknown layout value')
        if value != '00000000':
            changed.append(profile)
            after['settings']['profiles'][key] = '00000000'
    return after, changed


def write_square(port, profiles, current_profile):
    if type(current_profile) is not int or not 0 <= current_profile < 4:
        raise ValueError('Invalid current profile')
    committing = False
    try:
        port.dtr = True
        port.reset_input_buffer()
        port.write(b'\x01\x02')
        header = bytearray()
        port.timeout = .15
        deadline = time.monotonic()+3
        while time.monotonic() < deadline:
            data = port.read(max(1, min(256, port.in_waiting)))
            if data: header.extend(data)
            elif header: break
        if identify(bytes(header)) != ('6.1-ca30987-obir1', 'rpipico'):
            raise ValueError('Unexpected device identity')
        port.timeout = 3
        port.write(b'\xaa\x01')
        committing = True
        if exact(port, 1) != b'\xaa': raise ValueError('Settings transaction was not acknowledged')
        for profile in profiles:
            body = b'IrLayout\0\x04'+bytes((profile,))+b'\0'*4
            port.write(b'\xae'+body)
            if exact(port, len(body)) != body:
                raise ValueError('Square setting echo mismatch')
        port.write(b'\xfc')
        if exact(port, 2) != b'\xfc\x01': raise ValueError('Square setting save failed')
        committing = False
        # Save ACK precedes camera/pin reinitialization and status LED pulses.
        time.sleep(.7)
        # The firmware caches its destination homography. Saving IrLayout alone
        # does not invalidate it. Reapply the same profile in RAM, without
        # starting calibration or saving/changing the profile's values.
        port.write(bytes((0x06, current_profile, 0)))
        if exact(port, 2) != bytes((0x97, current_profile)):
            raise ValueError('Current profile refresh was not acknowledged')
    finally:
        if committing:
            port.write(b'\xfe')  # Abort transaction and reload saved preferences.
            port.flush()
            time.sleep(.1)
        port.write(b'\xfe')      # Undock; restores normal HID operation.
        port.flush()


def configure(name, prefix):
    before_path, after_path = prefix.with_name(prefix.name+'-before.json'), prefix.with_name(prefix.name+'-after.json')
    if before_path.exists() or after_path.exists():
        raise FileExistsError('Refusing to replace a configuration backup')
    before = snapshot(name)
    expected, changed = expected_square(before)
    before_path.parent.mkdir(parents=True, exist_ok=True)
    before_path.write_text(json.dumps(before, indent=2), encoding='utf-8')
    if changed:
        with serial.Serial(name, 9600, timeout=3, write_timeout=1) as port:
            current = bytes.fromhex(before['settings']['profiles']['CurrentProf'])
            if len(current) != 1: raise ValueError('Invalid current profile response')
            write_square(port, changed, current[0])
    after = snapshot(name)
    after_path.write_text(json.dumps(after, indent=2), encoding='utf-8')
    if after != expected:
        raise ValueError('Settings readback differs from the requested layout-only change')
    print(json.dumps(dict(saved=True, changed_profiles=changed, other_settings_unchanged=True)))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', required=True)
    parser.add_argument('--backup-prefix', type=Path, required=True)
    args = parser.parse_args()
    configure(args.port, args.backup_prefix)
