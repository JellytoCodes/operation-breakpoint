"""Opt-in KY-040 counters; cumulative counts survive dropped serial packets."""
from dataclasses import dataclass
import struct
from led_detection import crc16

PACKET_SIZE=17

@dataclass(frozen=True)
class EncoderFrame:
    position: int
    presses: int

    @classmethod
    def decode(cls,packet):
        if len(packet)!=PACKET_SIZE or packet[:7]!=b'\x99OBYE\x01\x08':
            raise ValueError('Unknown encoder packet')
        if crc16(packet[1:-2])!=int.from_bytes(packet[-2:],'little'):
            raise ValueError('Encoder checksum mismatch')
        return cls(*struct.unpack('<II',packet[7:-2]))


def counter_delta(current,previous):
    return ((current-previous+0x80000000)&0xffffffff)-0x80000000
