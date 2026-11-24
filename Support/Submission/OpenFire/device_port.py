"""Find the OpenFIRE USB serial interface without opening or changing it."""
import argparse
import json
import time

from serial.tools import list_ports

OPENFIRE_USB_ID = (0xF143, 0x0001)


class PortSelectionError(RuntimeError):
    pass


def select_port(ports, requested='AUTO'):
    ports = list(ports)
    candidates = [p for p in ports if (p.vid, p.pid) == OPENFIRE_USB_ID]
    requested = requested.upper()
    available = ', '.join(p.device for p in ports) or '없음'
    if requested != 'AUTO':
        selected = next((p for p in ports if p.device.upper() == requested), None)
        if selected is None:
            detected = ', '.join(p.device for p in candidates) or '없음'
            raise PortSelectionError(
                f'{requested} 포트가 없습니다. OpenFIRE 포트: {detected}. '
                '자동 검색으로 실행하거나 실제 포트 번호를 지정하세요.')
        if selected not in candidates:
            raise PortSelectionError(f'{requested}는 OpenFIRE USB 장치가 아닙니다.')
        return selected.device
    if len(candidates) == 1:
        return candidates[0].device
    if len(candidates) > 1:
        raise PortSelectionError('OpenFIRE 보드가 여러 개입니다: '
            + ', '.join(p.device for p in candidates) + '. -Port COM번호를 지정하세요.')
    raise PortSelectionError(
        'Windows에서 OpenFIRE USB 직렬 포트를 찾지 못했습니다. '
        'BOOTSEL을 누르지 않고 보드 USB를 다시 연결하세요. '
        'RPI-RP2 드라이브가 보이면 BOOTSEL 모드입니다. '
        f'현재 직렬 포트: {available}.')


def wait_for_port(requested='AUTO', wait_seconds=5):
    deadline = time.monotonic() + wait_seconds
    while True:
        try:
            return select_port(list_ports.comports(), requested)
        except PortSelectionError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(.2)


def reconnect_device(ports, preferred, serial_number=None):
    ports = [port for port in ports if (port.vid, port.pid) == OPENFIRE_USB_ID]
    if serial_number:
        ports = [port for port in ports if port.serial_number == serial_number]
    try:
        name = select_port(ports, preferred)
    except PortSelectionError:
        name = select_port(ports)
    return next(port for port in ports if port.device == name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='AUTO')
    parser.add_argument('--wait-seconds', type=float, default=5)
    args = parser.parse_args()
    if not 0 <= args.wait_seconds <= 15:
        parser.error('wait-seconds must be 0..15')
    try:
        result = dict(port=wait_for_port(args.port, args.wait_seconds), error='')
    except PortSelectionError as exc:
        result = dict(port=None, error=str(exc))
    # A single UTF-8 JSON result lets PowerShell preserve the actual error.
    print(json.dumps(result, ensure_ascii=False))
    return 1 if result['error'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
