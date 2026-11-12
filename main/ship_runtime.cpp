#include "ship_runtime.h"
#include "../native/ship_api.h"
#include "rom_assets.h"
#include "n64_alpha.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "tgx.h"
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <cstdio>
esp_err_t sf64_ship_alpha_self_test();

namespace {
const char *TAG="starship";
struct ShipResource { const char *name,*type,*format; unsigned index,offset,width,height,count,vram; };
#include "ship_resources.inc"
sf64::RomAssets rom;
sf64::RomSegment segments[64];
std::unordered_map<uint64_t,void*> converted;
unsigned segment_binding[16];
uint16_t be16(const uint8_t *p) { return uint16_t(p[0])<<8|p[1]; }
uint32_t be32(const uint8_t *p) { return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3]; }
float bef32(const uint8_t *p) { uint32_t bits=be32(p); float value; memcpy(&value,&bits,4); return value; }
void *allocate(size_t size) {
    void *result=heap_caps_calloc(1,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!result) sf64_ship_fail("resource allocation failed");
    return result;
}
const uint8_t *raw(unsigned index,unsigned offset,size_t size) {
    if(index>=64) sf64_ship_fail("bad resource group");
    if(!segments[index].data) {
        ESP_ERROR_CHECK(rom.load(index,segments[index]));
        if(SHIP_SEGMENT_IDS[index]<16) segment_binding[SHIP_SEGMENT_IDS[index]]=index;
    }
    if(offset>segments[index].size || size>segments[index].size-offset) sf64_ship_fail("resource read out of range");
    return segments[index].data+offset;
}
unsigned pointed_group(unsigned parent,uint32_t pointer) {
    unsigned seg=pointer>>24;
    if(seg==SHIP_SEGMENT_IDS[parent]) return parent;
    if(seg==1) return 6; // ast_common is the game's shared segment 1.
    if(seg<16 && segment_binding[seg]<64) return segment_binding[seg];
    sf64_ship_fail("unbound resource pointer segment"); return parent;
}
void *convert(unsigned index,unsigned offset,unsigned kind,unsigned count=0);
void *pointed(unsigned parent,uint32_t pointer,unsigned kind,unsigned count=0) {
    return pointer ? convert(pointed_group(parent,pointer),pointer&0xffffff,kind,count) : nullptr;
}
// kind: 0 raw, 1 native GBI, 2 vertices, 3 animation, 4 skeleton,
// 5 limb, 6 matrix, 7 viewport, 8 native u16 stream,
// 9 environment settings, 10 level object placements,
// 11 shared native u16 view, 12 shared native u32/float view, 13 script table,
// 14 message lookup table (Starship adds a resource-path field).
void *convert(unsigned index,unsigned offset,unsigned kind,unsigned count) {
    if(!kind) return const_cast<uint8_t *>(raw(index,offset,1));
    uint64_t key=uint64_t(index)<<56|uint64_t(kind)<<48|uint64_t(count)<<24|offset;
    if(auto found=converted.find(key);found!=converted.end()) return found->second;
    const uint8_t *source=raw(index,offset,1);
    if(kind==14) {
        const ShipResource *metadata=nullptr;
        for(const auto &entry:SHIP_RESOURCES)
            if(entry.index==index && entry.offset==offset && !strcmp(entry.type,"SF64:MSG_TABLE")) { metadata=&entry; break; }
        if(!metadata || !metadata->vram || !count) sf64_ship_fail("missing message lookup metadata");
        source=raw(index,offset,count*8);
        struct MessageLookup { int32_t id; uint16_t *message; const char *path; };
        static_assert(sizeof(MessageLookup)==12,"Starship message lookup ABI mismatch");
        auto *out=static_cast<MessageLookup *>(allocate(count*sizeof(MessageLookup))); converted[key]=out;
        for(unsigned i=0;i<count;++i) {
            out[i].id=int32_t(be32(source+i*8));
            if(out[i].id==-1) break;
            uint32_t pointer=be32(source+i*8+4);
            if(pointer<metadata->vram) sf64_ship_fail("invalid message lookup pointer");
            out[i].message=static_cast<uint16_t *>(convert(index,pointer-metadata->vram,11));
            char name[64]; snprintf(name,sizeof(name),"ast_radio/gMsg_ID_%ld",long(out[i].id));
            auto *path=static_cast<char *>(allocate(strlen(name)+1)); strcpy(path,name); out[i].path=path;
        }
        return out;
    }
    if(kind==11 || kind==12) {
        // Scripts and hitboxes reference many interior offsets. Share one
        // endian-converted view instead of duplicating the segment per pointer.
        unsigned stride=kind==11 ? 2 : 4;
        auto *base=static_cast<uint8_t *>(offset ? convert(index,0,kind) : nullptr);
        if(offset) {
            if(offset%stride) sf64_ship_fail("misaligned native resource view");
            return base+offset;
        }
        size_t length=segments[index].size;
        auto *out=static_cast<uint8_t *>(allocate(length)); converted[key]=out;
        for(size_t at=0;at+stride<=length;at+=stride) {
            if(stride==2) { uint16_t word=be16(source+at); memcpy(out+at,&word,2); }
            else { uint32_t word=be32(source+at); memcpy(out+at,&word,4); }
        }
        return out;
    }
    if(kind==13) {
        if(!count || count>1024) sf64_ship_fail("invalid script table length");
        source=raw(index,offset,count*4);
        auto **out=static_cast<uint16_t **>(allocate(count*sizeof(uint16_t *))); converted[key]=out;
        for(unsigned i=0;i<count;++i) out[i]=static_cast<uint16_t *>(pointed(index,be32(source+i*4),11));
        ESP_LOGI(TAG,"Loaded %u original event scripts",count);
        return out;
    }
    if(kind==9) {
        // Environment is 0x44 bytes on both N64 and RV32. The two u16
        // fields at 8/10 must not be swapped as a single 32-bit word.
        source=raw(index,offset,0x44);
        auto *out=static_cast<uint8_t *>(allocate(0x44)); converted[key]=out;
        for(unsigned at=0;at<0x44;at+=4) {
            if(at==8) {
                uint16_t bg=be16(source+8),seq=be16(source+10);
                memcpy(out+8,&bg,2); memcpy(out+10,&seq,2);
            } else {
                uint32_t word=be32(source+at); memcpy(out+at,&word,4);
            }
        }
        return out;
    }
    if(kind==10) {
        // ObjectInit has a float, seven s16 fields, and two padding bytes.
        // Include the negative-ID terminator consumed by the original loader.
        unsigned length=0;
        for(;;) {
            const auto *entry=raw(index,offset+length*20,20);
            ++length;
            if(int16_t(be16(entry+16))<0) break;
            if(length>=8192) sf64_ship_fail("unterminated level object list");
        }
        auto *out=static_cast<uint8_t *>(allocate(length*20)); converted[key]=out;
        for(unsigned i=0;i<length;++i) {
            const auto *entry=raw(index,offset+i*20,20);
            uint32_t z=be32(entry); memcpy(out+i*20,&z,4);
            for(unsigned at=4;at<18;at+=2) {
                uint16_t value=be16(entry+at); memcpy(out+i*20+at,&value,2);
            }
        }
        ESP_LOGI(TAG,"Loaded %u original level objects (including terminator)",length);
        return out;
    }
    if(kind==1) {
        unsigned length=0;
        for(;length<8192;++length) {
            const auto *p=raw(index,offset+length*8,8);
            uint32_t word=be32(p); unsigned opcode=word>>24;
            if(opcode==0xb8 || (opcode==6 && ((word>>16)&1))) { ++length; break; }
        }
        if(length==8192) sf64_ship_fail("unterminated resource display list");
        auto *out=static_cast<uint32_t *>(allocate(length*8)); converted[key]=out;
        for(unsigned i=0;i<length;++i) {
            uint32_t a=be32(source+i*8),b=be32(source+i*8+4); unsigned op=a>>24;
            out[i*2]=a;
            if(op==6) b=reinterpret_cast<uintptr_t>(pointed(index,b,1));
            else if(op==4) b=reinterpret_cast<uintptr_t>(pointed(index,b,2,(a>>10)&63));
            else if(op==0xfd) b=reinterpret_cast<uintptr_t>(pointed(index,b,0));
            else if(op==1) b=reinterpret_cast<uintptr_t>(pointed(index,b,6));
            else if(op==3) b=reinterpret_cast<uintptr_t>(pointed(index,b,((a>>16)&255)==0x80 ? 7 : 0));
            out[i*2+1]=b;
        }
        return out;
    }
    if(kind==2) {
        if(!count) count=std::min(64u,unsigned((segments[index].size-offset)/16));
        raw(index,offset,count*16);
        auto *out=static_cast<uint8_t *>(allocate(count*16)); converted[key]=out;
        memcpy(out,source,count*16);
        for(unsigned i=0;i<count;++i) for(unsigned f=0;f<6;++f) {
            uint16_t value=be16(source+i*16+f*2); memcpy(out+i*16+f*2,&value,2);
        }
        return out;
    }
    if(kind==3) {
        raw(index,offset,12);
        struct Animation { int16_t frames,limbs; uint16_t *data,*keys; };
        auto *out=static_cast<Animation *>(allocate(sizeof(Animation))); converted[key]=out;
        out->frames=be16(source); out->limbs=be16(source+2);
        if(out->limbs<0 || out->limbs>128) sf64_ship_fail("invalid animation limb count");
        uint32_t key_pointer=be32(source+8),frame_pointer=be32(source+4);
        unsigned keys=(out->limbs+1)*6;
        out->keys=static_cast<uint16_t *>(pointed(index,key_pointer,8,keys));
        unsigned frame_size=0;
        for(unsigned i=0;i<keys;i+=2) frame_size=std::max(frame_size,unsigned(out->keys[i+1])+std::max(1u,unsigned(out->keys[i])));
        out->data=static_cast<uint16_t *>(pointed(index,frame_pointer,8,frame_size));
        return out;
    }
    if(kind==4) {
        unsigned length=0;
        while(length<256 && be32(raw(index,offset+length*4,4))) ++length;
        if(length==256) sf64_ship_fail("unterminated skeleton");
        auto **out=static_cast<void **>(allocate((length+1)*4)); converted[key]=out;
        for(unsigned i=0;i<length;++i) out[i]=pointed(index,be32(source+i*4),5);
        return out;
    }
    if(kind==5) {
        raw(index,offset,32);
        struct Limb { void *list; float pos[3]; int16_t rot[3],pad; void *sibling,*child; };
        static_assert(sizeof(Limb)==32);
        auto *out=static_cast<Limb *>(allocate(sizeof(Limb))); converted[key]=out;
        out->list=pointed(index,be32(source),1);
        for(int i=0;i<3;++i) { out->pos[i]=bef32(source+4+i*4); out->rot[i]=be16(source+16+i*2); }
        out->sibling=pointed(index,be32(source+24),5); out->child=pointed(index,be32(source+28),5);
        return out;
    }
    if(kind==6) {
        raw(index,offset,64);
        auto *out=static_cast<float *>(allocate(64)); converted[key]=out;
        for(unsigned i=0;i<16;++i) {
            int32_t value=int32_t(uint32_t(be16(source+i*2))<<16|be16(source+32+i*2));
            out[i]=value/65536.0f;
        }
        return out;
    }
    if(kind==7 || kind==8) {
        unsigned length=kind==7 ? 8 : count;
        raw(index,offset,length*2);
        auto *out=static_cast<uint16_t *>(allocate(length*2)); converted[key]=out;
        for(unsigned i=0;i<length;++i) out[i]=be16(source+i*2);
        return out;
    }
    sf64_ship_fail("unsupported resource conversion"); return nullptr;
}
const ShipResource *find(const char *name) {
    // This is called repeatedly by animated model drawing, not just at load.
    // The generated table is sorted so each lookup takes at most 13 compares.
    if(!strncmp(name,"__OTR__",7)) name+=7;
    auto item=std::lower_bound(std::begin(SHIP_RESOURCES),std::end(SHIP_RESOURCES),name,
        [](const ShipResource &entry,const char *key) { return strcmp(entry.name+7,key)<0; });
    if(item!=std::end(SHIP_RESOURCES) && !strcmp(item->name+7,name)) return item;
    ESP_LOGE(TAG,"Resource metadata missing: %s",name); sf64_ship_fail(name); return nullptr;
}
struct Packet { uint32_t w0,w1; };
constexpr auto SHIP_SHADERS=tgx::SHADER_PERSPECTIVE|tgx::SHADER_ORTHO|
    tgx::SHADER_ZBUFFER|tgx::SHADER_GOURAUD|tgx::SHADER_NOTEXTURE|tgx::SHADER_TEXTURE|
    tgx::SHADER_TEXTURE_NEAREST|tgx::SHADER_TEXTURE_CLAMP|tgx::SHADER_TEXTURE_WRAP_POW2;
using ShipRenderer=tgx::Renderer3D<tgx::RGB565,SHIP_SHADERS>;
struct CachedVertex { tgx::fVec3 position; tgx::RGBf color; tgx::fVec2 uv; uint8_t alpha=255; bool loaded=false; };
struct DrawState {
    uint32_t texture=0,color_target=0,depth_target=0;
    int image_width=0,tex_width=0,tex_height=0,fmt=0,bits=0,cycle=3;
    uint32_t primitive=0xffffffff,fill=1;
    int sx0=0,sy0=0,sx1=320,sy1=240;
    tgx::fMat4 model[32],projection;
    unsigned model_top=0,geometry=0;
    CachedVertex vertices[64];
    uint8_t lights[9][12]{};
    unsigned light_count=2;
    const uint8_t *palette=nullptr;
    unsigned palette_size=0,texture_lut=2;
    ShipRenderer *renderer=nullptr;
    unsigned triangles=0;
    bool texture_enabled=false,primitive_only=false,combine_texture=false;
    float texture_s=1,texture_t=1;
    float texture_origin_s=0,texture_origin_t=0;
    int render_fmt=-1,render_bits=-1,palette_bank=0;
    int tile_width=0,tile_height=0;
    bool texture_wrap=false;
    unsigned interpolation_target=0;
    uint32_t other_low=0,combine_high=0,combine_low=0,environment=0xffffffff;
    unsigned primitive_lod=0,alpha_cutoff=1;
    bool combine_set=false;
    DrawState() { model[0].setIdentity(); projection.setIdentity(); }
};
uint8_t fragment_alpha(const DrawState &state,unsigned texel,unsigned shade) {
    if(state.cycle==2) return texel; // RDP copy cycle bypasses the combiner.
    if(!state.combine_set) return texel*(state.primitive&255)/255;
    return sf64::rdp_alpha(state.combine_high,state.combine_low,state.cycle,texel,
        state.primitive&255,state.environment&255,shade,state.primitive_lod);
}
void fragment_depth(DrawState &state) {
    bool z_enabled=(state.geometry&1)!=0;
    state.renderer->setFragmentDepth(z_enabled && (state.other_low&0x10),z_enabled && (state.other_low&0x20));
}
bool textured_triangle(const CachedVertex &,const CachedVertex &,const CachedVertex &,DrawState &);
bool triangle(uint32_t indices,DrawState &state) {
    unsigned ids[3]={(indices>>16)&255,(indices>>8)&255,indices&255};
    for(auto &id:ids) {
        if((id&1) || id/2>=64 || !state.vertices[id/2].loaded) return false;
        id/=2;
    }
    const auto &v0=state.vertices[ids[0]],&v1=state.vertices[ids[1]],&v2=state.vertices[ids[2]];
    state.renderer->setProjectionMatrix(state.projection);
    if(std::fabs(state.projection.M[15])<0.00001f) state.renderer->usePerspectiveProjection();
    else state.renderer->useOrthographicProjection();
    state.renderer->setCulling((state.geometry&0x2000) ? 1 : (state.geometry&0x1000) ? -1 : 0);
    const tgx::fVec3 normal(0,0,1);
    unsigned shade_alpha=(unsigned(v0.alpha)+v1.alpha+v2.alpha+1)/3;
    auto alpha0=fragment_alpha(state,0,shade_alpha),alpha255=fragment_alpha(state,255,shade_alpha);
    fragment_depth(state);
    if(state.texture_enabled && (state.combine_texture || alpha0!=alpha255)) {
        if(!textured_triangle(v0,v1,v2,state)) return false;
    } else {
        state.renderer->setShaders(tgx::SHADER_PERSPECTIVE|tgx::SHADER_ZBUFFER|tgx::SHADER_GOURAUD|tgx::SHADER_NOTEXTURE);
        state.renderer->setLightAmbiant({1,1,1});
        state.renderer->setFragmentAlpha(nullptr,alpha255,alpha255,(state.other_low&3)==1 ? state.alpha_cutoff : 1);
        tgx::RGBf primitive((state.primitive>>24)/255.0f,((state.primitive>>16)&255)/255.0f,((state.primitive>>8)&255)/255.0f);
        state.renderer->drawTriangleWithVertexColor(v0.position,v1.position,v2.position,
            state.primitive_only?primitive:v0.color,state.primitive_only?primitive:v1.color,state.primitive_only?primitive:v2.color,
            &normal,&normal,&normal);
    }
    ++state.triangles;
    return true;
}
static uint32_t sign24(uint32_t value) { return value&0x800000 ? value|0xff000000 : value; }
const uint8_t *texture_data(uint32_t pointer) {
    const auto *data=reinterpret_cast<const uint8_t *>(pointer);
    if(data && !memcmp(data,"__OTR__",7)) data=static_cast<const uint8_t *>(sf64_ship_resource((const char *)data));
    return data;
}
bool texture_pixel(const uint8_t *data,unsigned pixel,const DrawState &state,uint8_t rgba[4]) {
    const int fmt=state.render_fmt>=0 ? state.render_fmt : state.fmt;
    const int bits=state.render_fmt>=0 ? state.render_bits : state.bits;
    auto color16=[&](uint16_t value,bool ia) {
        if(ia) { rgba[0]=rgba[1]=rgba[2]=value>>8; rgba[3]=value&255; }
        else {
            rgba[0]=((value>>11)&31)*255/31; rgba[1]=((value>>6)&31)*255/31;
            rgba[2]=((value>>1)&31)*255/31; rgba[3]=(value&1)*255;
        }
    };
    if(fmt==0 && bits==2) color16(be16(data+pixel*2),false);
    else if(fmt==0 && bits==3) memcpy(rgba,data+pixel*4,4);
    else if(fmt==3 && bits==2) color16(be16(data+pixel*2),true);
    else if((fmt==3 || fmt==4) && bits<=1) {
        unsigned value=bits ? data[pixel] : (data[pixel/2]>>((pixel&1)?0:4))&15;
        if(fmt==4) { rgba[0]=rgba[1]=rgba[2]=rgba[3]=value*(bits?1:17); }
        else if(bits) { rgba[0]=rgba[1]=rgba[2]=(value>>4)*17; rgba[3]=(value&15)*17; }
        else { rgba[0]=rgba[1]=rgba[2]=(value>>1)*255/7; rgba[3]=(value&1)*255; }
    } else if(fmt==2 && bits<=1) {
        unsigned entry=bits ? data[pixel] : (data[pixel/2]>>((pixel&1)?0:4))&15;
        if(!bits && state.palette_size>16) entry+=state.palette_bank*16;
        if(!state.palette || entry>=state.palette_size) return false;
        color16(be16(state.palette+entry*2),state.texture_lut==3);
    } else return false;
    return true;
}
bool texture_alpha_self_test() {
    DrawState state;
    uint8_t rgba[4];
    const uint8_t direct[]={0xf8,0x00,0x00,0x01};
    state.fmt=0; state.bits=2;
    if(!texture_pixel(direct,0,state,rgba) || rgba[0]!=255 || rgba[1] || rgba[2] || rgba[3]) return false;
    if(!texture_pixel(direct,1,state,rgba) || rgba[0] || rgba[1] || rgba[2] || rgba[3]!=255) return false;
    const uint8_t ia8[]={0xf8}; state.fmt=3; state.bits=1;
    if(!texture_pixel(ia8,0,state,rgba) || rgba[0]!=255 || rgba[3]!=136) return false;
    const uint8_t ia16[]={0xab,0x40}; state.bits=2;
    if(!texture_pixel(ia16,0,state,rgba) || rgba[0]!=171 || rgba[3]!=64) return false;
    const uint8_t intensity[]={0x0f}; state.fmt=4; state.bits=0;
    if(!texture_pixel(intensity,0,state,rgba) || rgba[3]) return false;
    if(!texture_pixel(intensity,1,state,rgba) || rgba[3]!=255) return false;
    const uint8_t indices[]={0x01},palette[]={0,1,0xf8,0};
    state.fmt=2; state.bits=2; state.render_fmt=2; state.render_bits=0;
    state.palette=palette; state.palette_size=2;
    if(!texture_pixel(indices,0,state,rgba) || rgba[0] || rgba[1] || rgba[2] || rgba[3]!=255) return false;
    if(!texture_pixel(indices,1,state,rgba) || rgba[0]!=255 || rgba[3]) return false;
    return true;
}
struct DecodedTexture {
    const uint8_t *source,*palette;
    int width,height,fmt,bits,bank,lut,palette_count;
    tgx::RGB565 *pixels;
    uint8_t *alpha;
};
std::vector<DecodedTexture> texture_cache;
void clear_texture_cache() {
    for(auto &entry:texture_cache) { heap_caps_free(entry.pixels); heap_caps_free(entry.alpha); }
    texture_cache.clear();
}
void invalidate_texture_cache(uint32_t pointer) {
    if(!pointer) { clear_texture_cache(); return; }
    uintptr_t changed=reinterpret_cast<uintptr_t>(texture_data(pointer));
    for(auto it=texture_cache.begin();it!=texture_cache.end();) {
        uintptr_t image=reinterpret_cast<uintptr_t>(it->source),palette=reinterpret_cast<uintptr_t>(it->palette);
        size_t bytes=(size_t(it->width)*it->height*(4u<<it->bits)+7)/8;
        bool affected=(changed>=image && changed-image<bytes) ||
            (palette && changed>=palette && changed-palette<size_t(it->palette_count)*2);
        if(affected) { heap_caps_free(it->pixels); heap_caps_free(it->alpha); it=texture_cache.erase(it); }
        else ++it;
    }
}
bool textured_triangle(const CachedVertex &a,const CachedVertex &b,const CachedVertex &c,DrawState &state) {
    const auto *source=texture_data(state.texture);
    int width=state.tile_width ? state.tile_width : state.tex_width;
    int height=state.tile_height ? state.tile_height : state.tex_height;
    if(!source || width<=0 || height<=0 || width>1024 || height>1024 || width*height>262144) return false;
    int old_fmt=state.fmt,old_bits=state.bits;
    if(state.render_fmt>=0) { state.fmt=state.render_fmt; state.bits=state.render_bits; }
    tgx::RGB565 *pixels=nullptr;
    uint8_t *alpha=nullptr;
    for(const auto &entry:texture_cache)
        if(entry.source==source && entry.palette==state.palette && entry.width==width && entry.height==height &&
           entry.fmt==state.fmt && entry.bits==state.bits && entry.bank==state.palette_bank && entry.lut==int(state.texture_lut) &&
           entry.palette_count==int(state.palette_size)) {
            pixels=entry.pixels; alpha=entry.alpha; break;
        }
    if(!pixels) {
        if(texture_cache.size()>=128) clear_texture_cache();
        pixels=static_cast<tgx::RGB565 *>(allocate(width*height*sizeof(tgx::RGB565)));
        alpha=static_cast<uint8_t *>(allocate(width*height));
        bool opaque=true;
        for(int i=0;i<width*height;++i) {
            uint8_t rgba[4];
            if(!texture_pixel(source,i,state,rgba)) {
                ESP_LOGE(TAG,"3D texture unsupported fmt=%d bits=%d",state.fmt,state.bits);
                heap_caps_free(pixels); heap_caps_free(alpha); state.fmt=old_fmt; state.bits=old_bits; return false;
            }
            pixels[i]=tgx::RGB565(rgba[0]/8,rgba[1]/4,rgba[2]/8);
            alpha[i]=rgba[3]; opaque=opaque && rgba[3]==255;
        }
        if(opaque) { heap_caps_free(alpha); alpha=nullptr; }
        texture_cache.push_back({source,state.palette,width,height,state.fmt,state.bits,state.palette_bank,int(state.texture_lut),int(state.palette_size),pixels,alpha});
    }
    state.fmt=old_fmt; state.bits=old_bits;
    tgx::Image<tgx::RGB565> texture(pixels,width,height);
    auto uv=[&](const CachedVertex &v) { return tgx::fVec2((v.uv.x-state.texture_origin_s)/width,
        (v.uv.y-state.texture_origin_t)/height); };
    auto uv0=uv(a),uv1=uv(b),uv2=uv(c);
    const tgx::fVec3 normal(0,0,1);
    unsigned shade_alpha=(unsigned(a.alpha)+b.alpha+c.alpha+1)/3;
    auto alpha0=fragment_alpha(state,0,shade_alpha),alpha255=fragment_alpha(state,255,shade_alpha);
    state.renderer->setFragmentAlpha(alpha0!=alpha255 ? alpha : nullptr,alpha0,alpha255,
        (state.other_low&3)==1 ? state.alpha_cutoff : 1);
    state.renderer->setTextureColorOverride(state.primitive_only,tgx::RGB565(
        int(state.primitive>>24)/8,int((state.primitive>>16)&255)/4,int((state.primitive>>8)&255)/8));
    state.renderer->setShaders(tgx::SHADER_PERSPECTIVE|tgx::SHADER_ZBUFFER|tgx::SHADER_GOURAUD|tgx::SHADER_TEXTURE);
    state.renderer->setTextureWrappingMode(state.texture_wrap && !(width&(width-1)) && !(height&(height-1)) ?
        tgx::SHADER_TEXTURE_WRAP_POW2 : tgx::SHADER_TEXTURE_CLAMP);
    unsigned color_multiplier=(state.combine_high>>15)&31;
    if(color_multiplier==3) state.renderer->setLightAmbiant({(state.primitive>>24)/255.0f,
        ((state.primitive>>16)&255)/255.0f,((state.primitive>>8)&255)/255.0f});
    else state.renderer->setLightAmbiant({(a.color.R+b.color.R+c.color.R)/3,
        (a.color.G+b.color.G+c.color.G)/3,(a.color.B+b.color.B+c.color.B)/3});
    state.renderer->drawTriangle(a.position,b.position,c.position,&normal,&normal,&normal,&uv0,&uv1,&uv2,&texture);
    return true;
}
tgx::RGB565 rgba16(uint16_t value) {
    return tgx::RGB565(int((value>>11)&31),int(((value>>6)&31)*2),int((value>>1)&31));
}
bool draw_list(const Packet *commands,tgx::Image<tgx::RGB565> &image,DrawState &state,int depth,unsigned &budget) {
    if(depth>32) return false;
    for(;budget<100000;++budget,++commands) {
        uint32_t a=commands->w0,b=commands->w1;
        unsigned op=a>>24;
        switch(op) {
        case 0xb8: return true;
        case 1: {
            if(!b) return false;
            const tgx::fMat4 matrix(reinterpret_cast<const float *>(b));
            unsigned flags=(a>>16)&255;
            if(flags&1) state.projection=(flags&2) ? matrix : state.projection*matrix;
            else {
                if(flags&4) {
                    if(state.model_top==31) return false;
                    state.model[state.model_top+1]=state.model[state.model_top]; ++state.model_top;
                }
                auto &current=state.model[state.model_top];
                current=(flags&2) ? matrix : current*matrix;
            }
            break;
        }
        case 0xbd:
            if(!state.model_top) return false;
            --state.model_top; break;
        case 0xb6: state.geometry&=~b; break;
        case 0xb7: state.geometry|=b; break;
        case 3: {
            unsigned slot=(a>>16)&255;
            if(slot>=0x86 && slot<=0x96 && !(slot&1)) {
                if(!b) return false;
                memcpy(state.lights[(slot-0x86)/2],reinterpret_cast<const void *>(b),12);
            }
            break;
        }
        case 0xbc:
            if((a&255)==2) {
                unsigned count=(b-0x80000000u)/32;
                if(count<1 || count>9) return false;
                state.light_count=count;
            } else if((a&255)==10) {
                unsigned slot=((a>>8)&65535)/24;
                if(slot<9) for(unsigned c=0;c<3;++c) state.lights[slot][c]=(b>>(24-c*8))&255;
            }
            break;
        case 0xbb:
            state.texture_enabled=(a&255)!=0;
            state.texture_s=(b>>16)/65536.0f; state.texture_t=(b&65535)/65536.0f; break;
        case 4: {
            unsigned first=((a>>16)&255)/2,count=(a>>10)&63;
            if(!b || !count || first+count>64 || ((a>>16)&1)) return false;
            const auto *data=reinterpret_cast<const uint8_t *>(b);
            for(unsigned i=0;i<count;++i) {
                int16_t coordinates[3]; memcpy(coordinates,data+i*16,6);
                auto p=state.model[state.model_top].mult(tgx::fVec3(coordinates[0],coordinates[1],coordinates[2]),1);
                auto &vertex=state.vertices[first+i];
                vertex.position={p.x,p.y,p.z};
                int16_t uv[2]; memcpy(uv,data+i*16+8,4);
                vertex.uv={uv[0]*state.texture_s/32,uv[1]*state.texture_t/32};
                const auto *color=data+i*16+12;
                vertex.alpha=color[3];
                if(state.geometry&0x20000) {
                    float rgb[3];
                    for(unsigned c=0;c<3;++c) rgb[c]=state.lights[state.light_count-1][c]/255.0f;
                    const float *m=state.model[state.model_top].M;
                    for(unsigned light=0;light+1<state.light_count;++light) {
                        const auto *l=state.lights[light];
                        float coeff[3];
                        for(unsigned j=0;j<3;++j)
                            coeff[j]=int8_t(l[8])*m[j*4]+int8_t(l[9])*m[j*4+1]+int8_t(l[10])*m[j*4+2];
                        float length=std::sqrt(coeff[0]*coeff[0]+coeff[1]*coeff[1]+coeff[2]*coeff[2]);
                        float amount=length>0 ? std::max(0.0f,(int8_t(color[0])*coeff[0]+
                            int8_t(color[1])*coeff[1]+int8_t(color[2])*coeff[2])/(127*length)) : 0;
                        for(unsigned c=0;c<3;++c) rgb[c]+=amount*l[c]/255.0f;
                    }
                    vertex.color=tgx::RGBf(std::min(rgb[0],1.0f),std::min(rgb[1],1.0f),std::min(rgb[2],1.0f));
                } else {
                    vertex.color=tgx::RGBf(color[0]/255.0f,color[1]/255.0f,color[2]/255.0f);
                }
                vertex.loaded=true;
            }
            break;
        }
        case 0xbf: if(!triangle(b,state)) return false; break;
        case 0xb1: if(!triangle(a,state) || !triangle(b,state)) return false; break;
        case 6:
            if(!draw_list(reinterpret_cast<const Packet *>(b),image,state,depth+1,budget)) return false;
            if((a>>16)&1) return true;
            break;
        case 0xff: state.color_target=b; break;
        case 0xfe: state.depth_target=b; break;
        case 0xfd:
            state.texture=b; state.fmt=(a>>21)&7; state.bits=(a>>19)&3; state.image_width=(a&4095)+1;
            break;
        case 0xf2:
            if(((b>>24)&7)==0) {
                int u0=(a>>12)&4095,v0=a&4095;
                state.texture_origin_s=u0/4.0f; state.texture_origin_t=v0/4.0f;
                state.tex_width=(int((b>>12)&4095)-u0)/4+1; state.tex_height=(int(b&4095)-v0)/4+1;
            }
            break;
        case 0x45: state.interpolation_target=b; break;
        case 0x44:
            if(!state.interpolation_target && ((b>>24)&7)==0) {
                float values[4]; memcpy(values,commands+1,sizeof(values));
                state.texture_origin_s=values[0]/4; state.texture_origin_t=values[1]/4;
                state.tex_width=std::max(1,int(values[2]/4)); state.tex_height=std::max(1,int(values[3]/4));
            }
            commands+=2; budget+=2; break;
        case 0xf5:
            if(((b>>24)&7)==0) {
                state.render_fmt=(a>>21)&7; state.render_bits=(a>>19)&3; state.palette_bank=(b>>20)&15;
                unsigned mask_s=(b>>4)&15,mask_t=(b>>14)&15;
                state.tile_width=mask_s ? 1<<mask_s : 0; state.tile_height=mask_t ? 1<<mask_t : 0;
                state.texture_wrap=!(b&(2<<8)) && !(b&(2<<18));
            }
            break;
        case 0xfc:
            state.combine_high=a; state.combine_low=b; state.combine_set=true;
            state.primitive_only=(((b>>15)&7)==3 && ((a>>15)&31)==31);
            {
                auto uses_texture=[](unsigned aa,unsigned bb,unsigned cc,unsigned dd) {
                    return dd==1 || dd==2 || (aa!=bb && cc!=31 &&
                        (aa==1 || aa==2 || bb==1 || bb==2 || cc==1 || cc==2 || cc==8 || cc==9));
                };
                state.combine_texture=uses_texture((a>>20)&15,(b>>28)&15,(a>>15)&31,(b>>15)&7) ||
                    uses_texture((a>>5)&15,(b>>24)&15,a&31,(b>>6)&7);
            }
            break;
        case 0xfa: state.primitive=b; state.primitive_lod=a&255; break;
        case 0xfb: state.environment=b; break;
        case 0xf9: state.alpha_cutoff=b&255; break;
        case 0xf7: state.fill=b; break;
        case 0xef:
            state.cycle=(a>>20)&3; state.texture_lut=(a>>14)&3; state.other_low=b; break;
        case 0xb9: {
            unsigned shift=(a>>8)&255,length=a&255;
            if(shift>=32 || !length || length>32-shift) return false;
            uint32_t mask=length==32 ? 0xffffffffu : ((1u<<length)-1)<<shift;
            state.other_low=(state.other_low&~mask)|(b&mask); break;
        }
        case 0xf0:
            state.palette=texture_data(state.texture); state.palette_size=((b>>14)&1023)+1; break;
        case 0xba:
            if(((a>>8)&255)==20) state.cycle=(b>>20)&3;
            if(((a>>8)&255)==14) state.texture_lut=(b>>14)&3;
            break;
        case 0xed:
            state.sx0=((a>>12)&4095)/4; state.sy0=(a&4095)/4;
            state.sx1=((b>>12)&4095)/4; state.sy1=(b&4095)/4;
            break;
        case 0xf6: case 0x38: {
            int x0,y0,x1,y1;
            if(op==0x38) {
                x1=int(sign24(a&0xffffff))/4; y1=int(sign24(b&0xffffff))/4;
                ++commands; ++budget;
                x0=int(sign24(commands->w0&0xffffff))/4; y0=int(sign24(commands->w1&0xffffff))/4;
            } else {
                x0=((b>>12)&4095)/4; y0=(b&4095)/4;
                x1=((a>>12)&4095)/4; y1=(a&4095)/4;
            }
            // The game first binds Z as the color target to clear depth.
            // It must not paint that fill into the displayed color buffer.
            if(state.depth_target && state.color_target==state.depth_target) break;
            tgx::RGB565 color=state.cycle==3 ? rgba16(state.fill) :
                tgx::RGB565(int((state.primitive>>24)&255)/8,int((state.primitive>>16)&255)/4,int((state.primitive>>8)&255)/8);
            float opacity=state.cycle==3 ? 1.0f : fragment_alpha(state,255,255)/255.0f;
            image.fillRect({std::max(x0,state.sx0),std::min(x1,state.sx1-1),std::max(y0,state.sy0),std::min(y1,state.sy1-1)},color,opacity);
            break;
        }
        case 0x37: {
            int right=int(sign24(a&0xffffff))/4,bottom=int(sign24(b&0xffffff))/4;
            int left=int(sign24(commands[1].w0&0xffffff))/4,top=int(sign24(commands[1].w1&0xffffff))/4;
            int s=int16_t(commands[2].w0>>16),t=int16_t(commands[2].w0);
            int ds=int16_t(commands[2].w1>>16),dt=int16_t(commands[2].w1);
            const auto *texture=texture_data(state.texture);
            if(!texture) return false;
            int stride=state.tex_width ? state.tex_width : state.image_width,height=state.tex_height;
            for(int y=std::max(top,state.sy0);y<std::min(bottom,state.sy1);++y)
                for(int x=std::max(left,state.sx0);x<std::min(right,state.sx1);++x) {
                    int u=s/32+(x-left)*ds/1024,v=t/32+(y-top)*dt/1024;
                    if(u<0 || u>=stride || v<0 || v>=height) continue;
                    uint8_t rgba[4];
                    if(!texture_pixel(texture,v*stride+u,state,rgba)) {
                        ESP_LOGE(TAG,"Texture rect unsupported fmt=%d bits=%d",state.fmt,state.bits); return false;
                    }
                    float alpha=fragment_alpha(state,rgba[3],255)/255.0f;
                    tgx::RGB565 color(int(rgba[0])*int((state.primitive>>24)&255)/255/8,
                                      int(rgba[1])*int((state.primitive>>16)&255)/255/4,
                                      int(rgba[2])*int((state.primitive>>8)&255)/255/8);
                    image.drawPixel({x,y},color,alpha);
                }
            commands+=2; budget+=2; break;
        }
        // 2D boot uses these state commands without a triangle raster operation.
        case 0x34: invalidate_texture_cache(b); break;
        case 0:
        case 0xe6: case 0xe7: case 0xe8: case 0xe9: case 0xf3: case 0xf4:
        case 0xf8: break;
        default: ESP_LOGE(TAG,"Unimplemented render opcode 0x%02x",op); return false;
        }
    }
    return false;
}
}

extern "C" void sf64_ship_fail(const char *name) {
    ESP_LOGE(TAG,"Native game stopped at missing interface/resource: %s",name); abort();
}
extern "C" void *sf64_ship_resource(const char *name) {
    const auto *item=find(name);
    if(!strcmp(item->type,"TEXTURE")) return convert(item->index,item->offset,0);
    if(!strcmp(item->type,"GFX")) return convert(item->index,item->offset,1);
    if(!strcmp(item->type,"VTX")) return convert(item->index,item->offset,2,item->count);
    if(!strcmp(item->type,"SF64:ANIM")) return convert(item->index,item->offset,3);
    if(!strcmp(item->type,"SF64:SKELETON")) return convert(item->index,item->offset,4);
    if(!strcmp(item->type,"SF64:ENVIRONMENT")) return convert(item->index,item->offset,9);
    if(!strcmp(item->type,"SF64:OBJECT_INIT")) return convert(item->index,item->offset,10);
    if(!strcmp(item->type,"SF64:HITBOX")) return convert(item->index,item->offset,12);
    if(!strcmp(item->type,"VEC3F")) return convert(item->index,item->offset,12);
    if(!strcmp(item->type,"SF64:TRIANGLE") || !strcmp(item->type,"VEC3S")) return convert(item->index,item->offset,11);
    if(!strcmp(item->type,"SF64:MESSAGE")) return convert(item->index,item->offset,11);
    if(!strcmp(item->type,"SF64:MSG_TABLE")) return convert(item->index,item->offset,14,item->count);
    if(!strcmp(item->type,"SF64:SCRIPT")) {
        raw(item->index,item->offset,4);
        unsigned end=segments[item->index].size;
        for(const auto &next:SHIP_RESOURCES)
            if(next.index==item->index && next.offset>item->offset) end=std::min(end,next.offset);
        if((end-item->offset)%4) sf64_ship_fail("unaligned script table extent");
        return convert(item->index,item->offset,13,(end-item->offset)/4);
    }
    sf64_ship_fail(name); return nullptr;
}
extern "C" void sf64_ship_resource_info(const char *name,int32_t *width,int32_t *height) {
    const auto *item=find(name); *width=item->width; *height=item->height;
}
esp_err_t sf64_ship_runtime_init(void) {
    ESP_ERROR_CHECK(sf64_ship_alpha_self_test());
    ESP_ERROR_CHECK(texture_alpha_self_test() ? ESP_OK : ESP_FAIL);
    ESP_LOGI(TAG,"Texture alpha decode passed: RGBA16 opaque black/transparent color, IA8/IA16, I4, CI4 render-tile format");
    esp_err_t error=rom.init(); if(error!=ESP_OK) return error;
    std::fill(std::begin(segment_binding),std::end(segment_binding),~0u);
    ESP_LOGI(TAG,"Executing original Starship Game_Initialize");
    sf64_ship_begin();
    return ESP_OK;
}
bool sf64_ship_runtime_frame(uint16_t *pixels,int width,int height) {
    unsigned count=0,budget=0;
    int64_t begin=esp_timer_get_time();
    const auto *commands=static_cast<const Packet *>(sf64_ship_step(&count));
    int64_t game_done=esp_timer_get_time();
    tgx::Image<tgx::RGB565> image(pixels,width,height);
    image.fillScreen(tgx::RGB565(uint16_t(0)));
    static auto *zbuffer=static_cast<uint16_t *>(allocate(width*height*sizeof(uint16_t)));
    ShipRenderer renderer({width,height},&image,zbuffer);
    renderer.setShaders(tgx::SHADER_PERSPECTIVE|tgx::SHADER_ZBUFFER|tgx::SHADER_GOURAUD|tgx::SHADER_NOTEXTURE);
    // Vertex colors already include the original N64 lighting calculation.
    renderer.setLight({0,0,-1},{1,1,1},{0,0,0},{0,0,0});
    renderer.setMaterial({1,1,1},1,0,0,0);
    renderer.clearZbuffer();
    DrawState state;
    state.renderer=&renderer;
    bool rendered=draw_list(commands,image,state,0,budget);
    static int previous=-1;
    int current=sf64_ship_state();
    static unsigned profile_frames=0,title_frames=0,active_frames=0;
    Sf64GameplayStats gameplay; sf64_ship_gameplay_stats(&gameplay);
    static int64_t game_us=0,render_us=0;
    game_us+=game_done-begin; render_us+=esp_timer_get_time()-game_done;
    if(++profile_frames==30) {
        ESP_LOGI(TAG,"Profile: game %.2f ms, render %.2f ms, %u triangles, %u commands",
            game_us/30000.0,render_us/30000.0,state.triangles,budget);
        if(current==7) ESP_LOGI(TAG,"Gameplay: play=%ld player=%ld frame=%ld path=%.1f pos=(%.1f,%.1f) shots=%ld",
            long(gameplay.play_state),long(gameplay.player_state),long(gameplay.frame),gameplay.path,gameplay.x,gameplay.y,long(gameplay.shots));
        profile_frames=0; game_us=render_us=0;
    }
    bool capture_title=current==2 && rendered && ++title_frames==20;
    bool capture_level=current==7 && rendered && gameplay.player_state==3 && ++active_frames==30;
    if(capture_title || capture_level) {
        // One small framebuffer readback allows offline visual inspection of
        // the actual board output; it does not replace a device input test.
        printf("SF64_FRAME_BEGIN 80 60 state=%d\n",current);
        for(int y=0;y<60;++y) {
            char row[321];
            for(int x=0;x<80;++x) snprintf(row+x*4,5,"%04x",pixels[(y*height/60)*width+x*width/80]);
            printf("SF64_ROW %s\n",row);
        }
        printf("SF64_FRAME_END\n");
    }
    if(current!=previous) {
        ESP_LOGI(TAG,"Original Game_Update state=%d, master commands=%u, decoded=%u, rendered=%d",current,count,budget,rendered);
        previous=current;
    }
    return rendered;
}
