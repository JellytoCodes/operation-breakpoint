// KY-040: CLK=GP26, DT=GP27, SW=GP22, +=GP28. Verified two 10k pull-ups.
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <Arduino.h>
#include "OBIRTelemetry.h"
#include "OBIRQuadrature.h"

namespace OBIREncoder {
constexpr uint8_t clkPin=26,dtPin=27,swPin=22,powerPin=28;
inline OBIRQuadrature counter;
inline uint32_t presses=0,changedAt=0;
inline bool switchRaw=true,switchStable=true,ready=false;
inline uint8_t state() { return (digitalRead(clkPin)?2:0)|(digitalRead(dtPin)?1:0); }
inline void edge() {
    // ISR is on the initialization core; only this ISR writes the decoder.
    counter.edge(state());
}
inline void begin() {
    // Two 10k pull-ups draw <=0.66mA. This output powers only this encoder.
    pinMode(powerPin,OUTPUT); digitalWrite(powerPin,HIGH);
    pinMode(clkPin,INPUT_PULLUP); pinMode(dtPin,INPUT_PULLUP); pinMode(swPin,INPUT_PULLUP);
    counter.last=state(); switchRaw=switchStable=digitalRead(swPin); changedAt=millis();
    attachInterrupt(digitalPinToInterrupt(clkPin),edge,CHANGE);
    attachInterrupt(digitalPinToInterrupt(dtPin),edge,CHANGE);
    ready=true;
}
inline void poll() {
    if(!ready) return;
    const bool raw=digitalRead(swPin); const uint32_t now=millis();
    if(raw!=switchRaw) { switchRaw=raw; changedAt=now; }
    if(raw!=switchStable && uint32_t(now-changedAt)>=30) {
        switchStable=raw;
        if(!raw) ++presses;
    }
}
inline void send() {
    if(!ready || !OBIRTelemetry::enabled) return;
    uint8_t buf[17]={0x99,'O','B','Y','E',1,8};
    noInterrupts();
    const uint32_t position=counter.position;
    interrupts();
    for(int i=0;i<4;++i) { buf[7+i]=uint8_t(position>>(8*i)); buf[11+i]=uint8_t(presses>>(8*i)); }
    const uint16_t crc=OBIRTelemetry::crc16(buf+1,14);
    buf[15]=uint8_t(crc); buf[16]=uint8_t(crc>>8);
    Serial.write(buf,sizeof(buf));
}
}
