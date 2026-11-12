#include "tgx.h"
#include "esp_err.h"
#include "esp_log.h"
#include <cstring>
#include <cstdio>

namespace {
constexpr auto FLAGS=tgx::SHADER_PERSPECTIVE|tgx::SHADER_ORTHO|tgx::SHADER_ZBUFFER|
    tgx::SHADER_GOURAUD|tgx::SHADER_NOTEXTURE|tgx::SHADER_TEXTURE|
    tgx::SHADER_TEXTURE_NEAREST|tgx::SHADER_TEXTURE_CLAMP;
using Renderer=tgx::Renderer3D<tgx::RGB565,FLAGS>;
bool exercise(bool perspective) {
    tgx::RGB565 pixels[16*16],before[16*16];
    uint16_t depth[16*16],background_depth[16*16];
    tgx::Image<tgx::RGB565> image(pixels,16,16);
    Renderer renderer({16,16},&image,depth);
    renderer.setCulling(0);
    renderer.setLight({0,0,-1},{1,1,1},{0,0,0},{0,0,0});
    renderer.setMaterial({1,1,1},1,0,0,0);
    if(perspective) renderer.setPerspective(90,1,1,10);
    else renderer.setOrtho(-1,1,-1,1,1,10);
    const tgx::fVec3 normal(0,0,1);
    const tgx::fVec2 uv[4]={{0,1},{1,1},{1,0},{0,0}};
    auto quad=[&](float z,const tgx::Image<tgx::RGB565> *texture=nullptr) {
        float extent=perspective ? -z : 1;
        tgx::fVec3 p[4]={{-extent,-extent,z},{extent,-extent,z},{extent,extent,z},{-extent,extent,z}};
        renderer.drawTriangle(p[0],p[1],p[2],&normal,&normal,&normal,
            texture?&uv[0]:nullptr,texture?&uv[1]:nullptr,texture?&uv[2]:nullptr,texture);
        renderer.drawTriangle(p[0],p[2],p[3],&normal,&normal,&normal,
            texture?&uv[0]:nullptr,texture?&uv[2]:nullptr,texture?&uv[3]:nullptr,texture);
    };
    auto background=[&]() {
        image.fillScreen(tgx::RGB565(0,0,0)); renderer.clearZbuffer();
        renderer.setFragmentAlpha(); renderer.setFragmentDepth();
        renderer.setShaders(tgx::SHADER_PERSPECTIVE|tgx::SHADER_ZBUFFER|tgx::SHADER_GOURAUD|tgx::SHADER_NOTEXTURE);
        renderer.setMaterialColor({0,0,1}); quad(-6);
        memcpy(background_depth,depth,sizeof(depth));
    };
    tgx::RGB565 texels[4]={{0,63,0},{31,0,0},{0,0,0},{31,0,0}};
    const uint8_t alpha[4]={0,128,255,255};
    tgx::Image<tgx::RGB565> texture(texels,2,2);
    auto effect=[&](bool write,uint8_t cutoff=1) {
        renderer.setMaterialColor({1,1,1});
        renderer.setShaders(tgx::SHADER_PERSPECTIVE|tgx::SHADER_ZBUFFER|tgx::SHADER_GOURAUD|
            tgx::SHADER_TEXTURE|tgx::SHADER_TEXTURE_NEAREST|tgx::SHADER_TEXTURE_CLAMP);
        renderer.setFragmentAlpha(alpha,0,255,cutoff); renderer.setFragmentDepth(true,write); quad(-2,&texture);
    };
    background(); effect(false);
    unsigned blue=0,half=0,black=0,red=0;
    for(const auto &pixel:pixels) {
        blue+=pixel.R==0 && pixel.G==0 && pixel.B==31;
        black+=pixel.R==0 && pixel.G==0 && pixel.B==0;
        red+=pixel.R==31 && pixel.G==0 && pixel.B==0;
        half+=pixel.R>=13 && pixel.R<=18 && pixel.G==0 && pixel.B>=13 && pixel.B<=18;
    }
    if(!blue || !half || !black || !red || blue+half+black+red!=256 || memcmp(depth,background_depth,sizeof(depth))) {
        ESP_LOGE("alpha_test","%s coverage/depth failed: blue=%u half=%u black=%u red=%u",
            perspective?"perspective":"ortho",blue,half,black,red); return false;
    }
    memcpy(before,pixels,sizeof(pixels));
    // A farther object must still fail depth after a translucent draw.
    renderer.setFragmentAlpha(); renderer.setFragmentDepth();
    renderer.setShaders(tgx::SHADER_PERSPECTIVE|tgx::SHADER_ZBUFFER|tgx::SHADER_GOURAUD|tgx::SHADER_NOTEXTURE);
    renderer.setMaterialColor({0,1,0}); quad(-8);
    if(memcmp(before,pixels,sizeof(pixels))) return false;
    if(!perspective) {
        printf("SF64_FRAME_BEGIN 16 16 state=99\n");
        for(int y=0;y<16;++y) {
            char row[65];
            for(int x=0;x<16;++x) { uint16_t raw; memcpy(&raw,&pixels[y*16+x],2); snprintf(row+x*4,5,"%04x",raw); }
            printf("SF64_ROW %s\n",row);
        }
        printf("SF64_FRAME_END\n");
    }
    background(); effect(true);
    for(unsigned i=0;i<256;++i) {
        bool transparent=pixels[i].R==0 && pixels[i].G==0 && pixels[i].B==31;
        if(transparent ? depth[i]!=background_depth[i] : depth[i]<=background_depth[i]) return false;
    }
    renderer.setFragmentAlpha(); renderer.setFragmentDepth();
    renderer.setShaders(tgx::SHADER_PERSPECTIVE|tgx::SHADER_ZBUFFER|tgx::SHADER_GOURAUD|tgx::SHADER_NOTEXTURE);
    renderer.setMaterialColor({0,1,0}); quad(-4);
    unsigned green=0;
    for(const auto &pixel:pixels) green+=pixel.R==0 && pixel.G==63 && pixel.B==0;
    if(green!=blue) return false;
    background(); effect(true,200);
    unsigned skipped=0;
    for(const auto &pixel:pixels) skipped+=pixel.R==0 && pixel.G==0 && pixel.B==31;
    return skipped==blue+half;
}
}
esp_err_t sf64_ship_alpha_self_test() {
    if(!exercise(false) || !exercise(true)) return ESP_FAIL;
    ESP_LOGI("alpha_test","TGX alpha passed: perspective/ortho, zero/half/opaque-black, read-only Z, cutout Z and cutoff");
    return ESP_OK;
}
