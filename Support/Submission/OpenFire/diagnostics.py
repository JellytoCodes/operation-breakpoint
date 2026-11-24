"""OpenFIRE binary diagnostics, also used by the isolated IR aim-only test.

The regular gameplay bridge continues to use HID, independently of this stream.

Protocol references are pinned in Support/Archive/OpenFire/RESEARCH.md. Unknown firmware is rejected.
No flash writes, pin changes, feedback pulses, or bootloader commands are sent.
"""
import re
import struct
import time
from led_detection import DetectionFrame, PACKET_SIZE
from camera_view import CameraFrame
from aim_analysis import AimSample
from encoder import EncoderFrame, PACKET_SIZE as ENCODER_PACKET_SIZE

SUPPORTED_VERSIONS = frozenset(('6.1', '6.2'))
CAMERA_UNAVAILABLE_ERROR = ('보드가 IR 카메라 통신 오류 상태를 보고했습니다. '
    'USB를 다시 연결하고 카메라 전원/GND와 SDA/SCL 배선을 확인하세요.')


class DiagnosticParser:
    LENGTHS = {0x90: 2, 0x91: 2, 0x92: 5, 0x93: 2, 0x94: 2,
               0x95: 6, 0x96: 49, 0x97: 2, 0xFA: 2}

    def __init__(self):
        self.buffer = bytearray()
        self.discarded = 0
        self.corrupt = 0
        self.pending_detection = None

    def feed(self, data):
        self.buffer.extend(data)
        output = []
        while self.buffer:
            kind = self.buffer[0]
            if kind == 0x99:
                if len(self.buffer)<ENCODER_PACKET_SIZE: break
                packet=bytes(self.buffer[:ENCODER_PACKET_SIZE])
                if packet[1:5]!=b'OBYE':
                    self.pending_detection=None; del self.buffer[0]; self.discarded+=1; continue
                del self.buffer[:ENCODER_PACKET_SIZE]
                self.pending_detection=None
                try: output.append(('encoder',EncoderFrame.decode(packet)))
                except ValueError: self.corrupt+=1
                continue
            if kind == 0x98:
                if len(self.buffer) < PACKET_SIZE:
                    break
                packet = bytes(self.buffer[:PACKET_SIZE])
                if packet[1:5] != b'OBIR':
                    self.pending_detection = None
                    del self.buffer[0]
                    self.discarded += 1
                    continue
                del self.buffer[:PACKET_SIZE]
                try:
                    self.pending_detection = DetectionFrame.decode(packet)
                    output.append(('detection', self.pending_detection))
                except ValueError:
                    self.pending_detection = None
                    self.corrupt += 1
                    output.append(('detection_error', 'LED 검출 패킷 오류 · 기준점을 다시 등록하세요.'))
                continue
            length = self.LENGTHS.get(kind)
            if length is None:
                self.pending_detection = None
                del self.buffer[0]
                self.discarded += 1
                continue
            if len(self.buffer) < length:
                break
            payload = bytes(self.buffer[1:length])
            del self.buffer[:length]
            if kind == 0x96:
                coords = struct.unpack('<12i', payload)
                output.append(('camera', coords))
                if self.pending_detection is not None:
                    output.append(('sample', AimSample(self.pending_detection, CameraFrame(coords))))
            elif kind in (0x90, 0x91):
                output.append(('button', (payload[0], kind == 0x90)))
            elif kind == 0x97:
                output.append(('profile',payload[0]))
            elif kind == 0xFA:
                output.append(('error', payload[0]))
            self.pending_detection = None
        return output


def identify(header):
    parts = header.split(b'\xfe', 2)
    if len(parts) != 3 or not re.fullmatch(rb'\d+\.\d+(?:-[A-Za-z0-9.-]+)?', parts[0]):
        raise ValueError('지원하지 않는 펌웨어 응답입니다. 원본 App에서 버전을 확인하세요.')
    version = parts[0].decode('ascii')
    # ca30987 (6.1) and 8b651a2 (6.2) use the same Dock / IRTest / 49-byte
    # coordinate protocol. Version strings identify firmware, not desktop App.
    if version.split('-')[0] not in SUPPORTED_VERSIONS:
        raise ValueError(f'펌웨어 {version}: 확인한 카메라 점검 버전은 6.1 / 6.2입니다.')
    board = parts[1].decode('ascii', errors='strict')
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,64}', board):
        raise ValueError('Invalid board identifier')
    return version, board


def run_diagnostics(port_name, stop, emit, *, attempts=1, on_handshake=None, stream_timeout=None):
    for number in range(1, attempts + 1):
        if stop.is_set():
            return
        result = dict(attempt=number, port=port_name, outcome='cancelled',
                      raw_response_hex='', port_open_ms=None, first_byte_ms=None,
                      response_ms=None, error=None)
        try:
            _run_attempt(port_name, stop, emit, result, on_handshake, stream_timeout)
            return
        except (OSError, ValueError, RuntimeError):
            if result['outcome'] != 'failed' or number == attempts:
                raise
            # The previous attempt has exited Docked and closed COM by this point.
            if stop.wait(.35):
                return


def _run_attempt(port_name, stop, emit, result, on_handshake, stream_timeout):
    import serial
    port = None
    docked = False
    header = bytearray()
    started = time.monotonic()
    try:
        try:
            port = serial.Serial(port_name, 9600, timeout=.05, write_timeout=.5)
            result['port_open_ms'] = round((time.monotonic() - started) * 1000, 2)
            port.dtr = True
            port.reset_input_buffer()
            docked = True  # An interrupted handshake may already have entered Docked.
            request_at = time.monotonic()
            port.write(b'\x01\x02')
            deadline = time.monotonic() + 1.5
            last_data = None
            while time.monotonic() < deadline and not stop.is_set():
                data = port.read(max(1, min(port.in_waiting, 256)))
                if data:
                    last_data = time.monotonic()
                    elapsed_ms = round((last_data - request_at) * 1000, 2)
                    if not header:
                        result['first_byte_ms'] = elapsed_ms
                    header.extend(data)
                    result['response_ms'] = elapsed_ms
                elif last_data is not None and time.monotonic() - last_data > .15:
                    break
            if stop.is_set():
                return
            version, board = identify(bytes(header))
            if b'\xfe\xfa' in header:
                raise RuntimeError(CAMERA_UNAVAILABLE_ERROR)
            if version.endswith(('-obir1','-obir2','-obir3')):
                # Private, argument-free command; never sent to stock firmware.
                port.write(b'\x0a')
            port.write(b'\x05\x01')
            result.update(outcome='connected', identity=(version, board))
        except (OSError, ValueError, RuntimeError) as exc:
            result.update(outcome='failed', error=str(exc), error_type=type(exc).__name__)
            raise
        finally:
            result.update(raw_response_hex=header.hex(' '),
                          elapsed_ms=round((time.monotonic() - started) * 1000, 2))
            if on_handshake is not None:
                on_handshake(result)

        emit('identity', (version, board))
        parser = DiagnosticParser()
        last_detection = time.monotonic()
        while not stop.is_set():
            for kind, value in parser.feed(port.read(max(1, min(port.in_waiting, 4096)))):
                if kind == 'detection':
                    last_detection = time.monotonic()
                elif kind == 'error' and stream_timeout is not None:
                    raise RuntimeError(CAMERA_UNAVAILABLE_ERROR if value == 0x80 else f'OpenFIRE error 0x{value:02X}')
                emit(kind, value)
            if stream_timeout is not None and time.monotonic() - last_detection >= stream_timeout:
                raise TimeoutError('OpenFIRE IR 샘플 수신 시간 초과')
    finally:
        if docked:
            try:
                # One FE exits Docked, restoring the profile's run mode.
                port.write(b'\xfe')
                port.flush()
            except (OSError, serial.SerialException):
                pass
        if port is not None:
            port.close()
