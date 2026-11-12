#include "f3dex.h"
namespace sf64 {
static uint16_t be16(const uint8_t *p) { return uint16_t(p[0])<<8 | p[1]; }
static uint32_t be32(const uint8_t *p) {
    return uint32_t(p[0])<<24 | uint32_t(p[1])<<16 | uint32_t(p[2])<<8 | p[3];
}
void F3dex::set_segment(unsigned index, const void *data, size_t size) {
    if (index<16) segments_[index]={static_cast<const uint8_t *>(data),size};
}
const uint8_t *F3dex::resolve(uint32_t address, size_t bytes) {
    unsigned seg=address>>24;
    size_t offset=address&0xffffff;
    if (seg>=16 || !segments_[seg].data || offset>segments_[seg].size ||
        bytes>segments_[seg].size-offset) {
        result_.error=DlError::Unmapped;
        return nullptr;
    }
    return segments_[seg].data+offset;
}
bool F3dex::triangle(uint32_t word) {
    unsigned ids[3]={(word>>16)&255,(word>>8)&255,word&255};
    for (auto &id:ids) {
        if ((id&1) || id/2>=64 || !loaded_[id/2]) {
            result_.error=DlError::VertexIndex;
            return false;
        }
        id/=2;
    }
    if (emit_) emit_(context_,vertices_[ids[0]],vertices_[ids[1]],vertices_[ids[2]]);
    ++result_.triangles;
    return true;
}
bool F3dex::execute(uint32_t address, unsigned depth) {
    if (depth>=16) { result_.error=DlError::Recursion; return false; }
    for (unsigned i=0;i<depth;++i) {
        if (active_[i]==address) { result_.error=DlError::Recursion; return false; }
    }
    active_[depth]=address;
    for (;;) {
        result_.address=address;
        if (++result_.instructions>8192) { result_.error=DlError::Budget; return false; }
        const auto *p=resolve(address,8);
        if (!p) return false;
        uint32_t w0=be32(p),w1=be32(p+4);
        uint8_t op=w0>>24;
        result_.opcode=op;
        // Never wrap the offset into a different segment on malformed input.
        if ((address&0xffffff)>0xfffff7) { result_.error=DlError::Unmapped; return false; }
        address+=8;
        switch (op) {
        case 0xb8: return true;
        case 4: {
            unsigned raw=(w0>>16)&255, first=raw/2, count=(w0>>10)&63;
            if ((raw&1) || !count || first+count>64 || (w0&1023)!=count*16-1) {
                result_.error=DlError::VertexLoad; return false;
            }
            const auto *v=resolve(w1,count*16);
            if (!v) return false;
            for (unsigned i=0;i<count;++i,v+=16) {
                vertices_[first+i]={int16_t(be16(v)),int16_t(be16(v+2)),int16_t(be16(v+4))};
                loaded_[first+i]=true;
            }
            result_.vertices_loaded+=count;
            break;
        }
        case 0xbf:
            if (!triangle(w1)) return false;
            break;
        case 0xb1:
            if (!triangle(w0) || !triangle(w1)) return false;
            break;
        case 6: {
            unsigned flag=(w0>>16)&255;
            if (flag>1) { result_.error=DlError::Unsupported; return false; }
            if (!execute(w1,depth+1)) return false;
            if (flag) return true;
            break;
        }
        // Known RDP texture/material commands in the test body display list.
        case 0xfd: case 0xf5: case 0xe6: case 0xf3: case 0xe7: case 0xf2:
            ++result_.material_commands;
            break;
        case 0: break;
        default:
            result_.error=DlError::Unsupported;
            return false;
        }
    }
}
DlResult F3dex::run(uint32_t address, TriangleFn emit, void *context) {
    result_={};
    for (auto &loaded:loaded_) loaded=false;
    emit_=emit;
    context_=context;
    execute(address,0);
    return result_;
}
}
