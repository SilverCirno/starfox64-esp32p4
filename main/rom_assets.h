#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_partition.h"
namespace sf64 {
struct RomSegment { uint8_t *data=nullptr; size_t size=0; uint32_t virtual_offset=0; };
class RomAssets {
public:
    esp_err_t init(); // Streams SHA-1 over the full ROM before trusting its DMA table.
    esp_err_t load(unsigned index, RomSegment &segment) const;
    static void release(RomSegment &segment);
    bool ready() const { return partition_!=nullptr; }
private:
    const esp_partition_t *partition_=nullptr;
    uint32_t table_=0;
};
}
