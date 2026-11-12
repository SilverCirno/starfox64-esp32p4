#include "mio0.h"
namespace sf64 {
static uint32_t be32(const uint8_t *p) {
    return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];
}
bool decode_mio0(const uint8_t *src, size_t source_size, uint8_t *dest, size_t dest_size) {
    if (!src || !dest || source_size<16 || be32(src)!=0x4d494f30 || be32(src+4)!=dest_size) return false;
    size_t pair=be32(src+8), literal=be32(src+12), mask_pos=16, out=0;
    const size_t pair_end=literal, mask_end=pair;
    if (pair<16 || literal<pair || literal>source_size) return false;
    uint8_t mask=0;
    unsigned bits=0;
    while (out<dest_size) {
        if (!bits) {
            if (mask_pos>=mask_end) return false;
            mask=src[mask_pos++]; bits=8;
        }
        if (mask&0x80) {
            if (literal>=source_size) return false;
            dest[out++]=src[literal++];
        } else {
            if (pair>pair_end || pair_end-pair<2) return false;
            uint16_t token=uint16_t(src[pair])<<8|src[pair+1]; pair+=2;
            size_t length=(token>>12)+3, distance=(token&4095)+1;
            if (distance>out || length>dest_size-out) return false;
            for (size_t i=0;i<length;++i,++out) dest[out]=dest[out-distance];
        }
        mask<<=1; --bits;
    }
    return true;
}
}
