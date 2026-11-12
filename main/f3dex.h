#pragma once
#include <stddef.h>
#include <stdint.h>

namespace sf64 {
struct Vertex { int16_t x,y,z; };
struct Segment { const uint8_t *data=nullptr; size_t size=0; };
using TriangleFn = void (*)(void *, const Vertex &, const Vertex &, const Vertex &);
enum class DlError { None, Unmapped, VertexLoad, VertexIndex, Unsupported, Recursion, Budget };
struct DlResult {
    DlError error=DlError::None;
    uint32_t instructions=0, vertices_loaded=0, triangles=0, material_commands=0;
    uint32_t address=0;
    uint8_t opcode=0;
};
// Initial F3DEX_GBI geometry subset, reading original big-endian N64 words.
// Unsupported commands fail explicitly. Texture state is counted, not rendered.
class F3dex {
public:
    void set_segment(unsigned index, const void *data, size_t size);
    DlResult run(uint32_t address, TriangleFn emit, void *context);
private:
    Segment segments_[16];
    Vertex vertices_[64]{};
    bool loaded_[64]{};
    uint32_t active_[16]{};
    TriangleFn emit_=nullptr;
    void *context_=nullptr;
    DlResult result_;
    const uint8_t *resolve(uint32_t address, size_t bytes);
    bool execute(uint32_t address, unsigned depth);
    bool triangle(uint32_t word);
};
}
