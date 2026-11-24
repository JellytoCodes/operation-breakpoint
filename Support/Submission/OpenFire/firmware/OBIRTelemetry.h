// Operation Breakpoint raw IR diagnostic extension for OpenFIRE 6.1 ca30987.
// SPDX-License-Identifier: LGPL-2.1-or-later
// Does not alter tracking, HID reports, pins, profiles, or flash settings.
#pragma once
#include <Arduino.h>

namespace OBIRTelemetry {
inline bool enabled = false;
inline uint32_t sequence = 0;

inline uint16_t crc16(const uint8_t* data, size_t length) {
    uint16_t crc = 0xffff;
    for(size_t i = 0; i < length; ++i) {
        crc ^= uint16_t(data[i]) << 8;
        for(int bit = 0; bit < 8; ++bit)
            crc = uint16_t((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
    }
    return crc;
}

inline void send(uint8_t seen, const int* xs, const int* ys, bool diamond) {
    if(!enabled) return;
    uint8_t buf[31] = {0x98, 'O', 'B', 'I', 'R', 1, 22};
    const uint32_t seq = sequence++;
    for(int i = 0; i < 4; ++i) buf[7+i] = uint8_t(seq >> (8*i));
    buf[11] = seen & 15;
    buf[12] = diamond ? 1 : 0;
    for(int i = 0; i < 4; ++i) {
        // Missing raw slots can contain old/uninitialized data. Encode zero;
        // consumers must still filter by seen, never by coordinate value.
        const uint16_t x = (seen & (1 << i)) ? xs[i] : 0;
        const uint16_t y = (seen & (1 << i)) ? ys[i] : 0;
        buf[13+i*4] = uint8_t(x); buf[14+i*4] = uint8_t(x >> 8);
        buf[15+i*4] = uint8_t(y); buf[16+i*4] = uint8_t(y >> 8);
    }
    const uint16_t crc = crc16(buf+1, 28);
    buf[29] = uint8_t(crc); buf[30] = uint8_t(crc >> 8);
    Serial.write(buf, sizeof(buf));
}
}
