// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdint.h>

struct OBIRQuadrature {
    uint8_t last=3;
    int8_t partial=0;
    volatile uint32_t position=0;
    void edge(uint8_t state) {
        static constexpr int8_t direction[16]={0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
        if((state^last)==3) partial=0; // Missing/invalid Gray transition.
        else partial+=direction[(last<<2)|state];
        last=state;
        if(state==3) {
            if(partial==4) ++position;
            else if(partial==-4) --position;
            partial=0;
        }
    }
};
