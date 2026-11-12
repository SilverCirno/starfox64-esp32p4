#include "rom_assets.h"
#include "mio0.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "mbedtls/sha1.h"
#include <cstring>
namespace sf64 {
static const char *TAG="sf64_rom";
static uint32_t be32(const uint8_t *p) {
    return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];
}
esp_err_t RomAssets::init() {
    partition_=nullptr;
    const auto *part=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
        static_cast<esp_partition_subtype_t>(0x40),"sf64rom");
    if (!part || part->size!=12*1024*1024) return ESP_ERR_NOT_FOUND;
    uint8_t header[64];
    esp_err_t err=esp_partition_read(part,0,header,sizeof(header));
    if (err!=ESP_OK) return err;
    if (be32(header)!=0x80371240) return ESP_ERR_NOT_FOUND;
    if (header[0x3e]!='E' || header[0x3f]>1) return ESP_ERR_NOT_SUPPORTED;
    auto *chunk=static_cast<uint8_t *>(heap_caps_malloc(4096,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    if (!chunk) return ESP_ERR_NO_MEM;
    mbedtls_sha1_context hash;
    mbedtls_sha1_init(&hash);
    int hash_err=mbedtls_sha1_starts(&hash);
    for (size_t offset=0; !hash_err && offset<part->size; offset+=4096) {
        err=esp_partition_read(part,offset,chunk,4096);
        if (err!=ESP_OK) break;
        hash_err=mbedtls_sha1_update(&hash,chunk,4096);
    }
    uint8_t digest[20];
    if (err==ESP_OK && !hash_err) hash_err=mbedtls_sha1_finish(&hash,digest);
    mbedtls_sha1_free(&hash);
    heap_caps_free(chunk);
    if (err!=ESP_OK) return err;
    if (hash_err) return ESP_FAIL;
    const uint8_t us10[]={0xd8,0xb1,0x08,0x85,0x20,0xf7,0xc5,0xf8,0x14,0x33,0x29,0x2a,0x92,0x58,0xc1,0x18,0x4a,0xfa,0x14,0x57};
    const uint8_t us11[]={0x09,0xf0,0xd1,0x05,0xf4,0x76,0xb0,0x0e,0xfa,0x53,0x03,0xa3,0xeb,0xc4,0x2e,0x60,0xa7,0x75,0x3b,0x7a};
    if (std::memcmp(digest,header[0x3f]?us11:us10,20)) return ESP_ERR_INVALID_CRC;
    table_=header[0x3f]?0xde480:0xd9a90;
    partition_=part;
    ESP_LOGI(TAG,"Full 12MiB ROM SHA-1 verified: US 1.%u, DMA at 0x%lx",header[0x3f],(unsigned long)table_);
    return ESP_OK;
}
void RomAssets::release(RomSegment &segment) {
    heap_caps_free(segment.data);
    segment={};
}
esp_err_t RomAssets::load(unsigned index, RomSegment &segment) const {
    if (!partition_) return ESP_ERR_INVALID_STATE;
    if (index>=64 || segment.data) return ESP_ERR_INVALID_ARG;
    uint8_t entry[16];
    esp_err_t err=esp_partition_read(partition_,table_+index*16,entry,16);
    if (err!=ESP_OK) return err;
    uint32_t virtual_offset=be32(entry),begin=be32(entry+4),end=be32(entry+8),flag=be32(entry+12);
    if (flag>1 || begin>=end || end>partition_->size) return ESP_ERR_INVALID_SIZE;
    const size_t packed_size=end-begin;
    auto *packed=static_cast<uint8_t *>(heap_caps_malloc(packed_size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if (!packed) return ESP_ERR_NO_MEM;
    err=esp_partition_read(partition_,begin,packed,packed_size);
    if (err!=ESP_OK) { heap_caps_free(packed); return err; }
    uint8_t *decoded=packed;
    size_t size=packed_size;
    if (flag) {
        if (packed_size<16 || be32(packed)!=0x4d494f30) { heap_caps_free(packed); return ESP_ERR_INVALID_RESPONSE; }
        size=be32(packed+4);
        if (!size || size>16*1024*1024) { heap_caps_free(packed); return ESP_ERR_INVALID_SIZE; }
        decoded=static_cast<uint8_t *>(heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if (!decoded) { heap_caps_free(packed); return ESP_ERR_NO_MEM; }
        bool ok=decode_mio0(packed,packed_size,decoded,size);
        heap_caps_free(packed);
        if (!ok) { heap_caps_free(decoded); return ESP_ERR_INVALID_RESPONSE; }
    }
    segment={decoded,size,virtual_offset};
    ESP_LOGI(TAG,"DMA segment %u loaded: %u -> %u bytes at virtual 0x%lx",
             index,unsigned(packed_size),unsigned(size),(unsigned long)virtual_offset);
    return ESP_OK;
}
}
