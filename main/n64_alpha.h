#pragma once
#include <stdint.h>

namespace sf64 {
// RDP alpha mux: (A-B)*C+D, with separate mux meanings for multiplier C.
inline uint8_t rdp_alpha(uint32_t high,uint32_t low,unsigned cycle,
                         unsigned texel,unsigned primitive,unsigned environment,
                         unsigned shade,unsigned lod=0) {
    int combined=0;
    auto input=[&](unsigned mux,bool multiplier) -> int {
        switch(mux) {
        case 0: return combined;
        case 1: case 2: return texel;
        case 3: return primitive;
        case 4: return shade;
        case 5: return environment;
        case 6: return multiplier ? int(lod) : 255;
        default: return 0;
        }
    };
    auto evaluate=[&](unsigned a,unsigned b,unsigned c,unsigned d) {
        int value=(input(a,false)-input(b,false))*input(c,true)+input(d,false)*255;
        value=(value+127)/255;
        combined=value<0 ? 0 : value>255 ? 255 : value;
    };
    evaluate((high>>12)&7,(low>>12)&7,(high>>9)&7,(low>>9)&7);
    if(cycle==1) evaluate((low>>21)&7,(low>>3)&7,(low>>18)&7,low&7);
    return uint8_t(combined);
}
}
