#pragma once
#include <stddef.h>
#include <stdint.h>
namespace sf64 {
// Decodes into caller-owned memory. Rejects truncated streams and invalid references.
bool decode_mio0(const uint8_t *source, size_t source_size, uint8_t *dest, size_t dest_size);
}
