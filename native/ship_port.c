#include "global.h"
#include "port/Engine.h"
#include "port/interpolation/FrameInterpolation.h"
#include "ship_api.h"
#include "host.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

float GameEngine_GetAspectRatio(void) { return 4.0f/3.0f; }
uint32_t GameEngine_GetInterpolationFrameCount(void) { return 1; }
bool GameEngine_HasVersion(SF64Version version) { return version==SF64_VER_US; }
void *GameEngine_Malloc(size_t bytes) { return malloc(bytes); }
uint8_t GameEngine_OTRSigCheck(const char *name) {
    return name && !memcmp(name,"__OTR__",7);
}
void *ResourceGetDataByName(const char *name) { return sf64_ship_resource(name); }
void GameEngine_GetTextureInfo(const char *name,int32_t *width,int32_t *height,float *scale,bool *custom) {
    sf64_ship_resource_info(name,width,height); *scale=1; *custom=false;
}
float OTRGetHUDAspectRatio(void) { return 4.0f/3.0f; }
float OTRGetDimensionFromLeftEdge(float x) { return x; }
float OTRGetDimensionFromRightEdge(float x) { return x; }
float OTRGetDimensionFromRightEdgeOverride(float x) { return x; }
int16_t OTRGetRectDimensionFromLeftEdge(float x) { return x; }
int16_t OTRGetRectDimensionFromRightEdge(float x) { return x; }
int16_t OTRGetRectDimensionFromLeftEdgeOverride(float x) { return x; }
int16_t OTRGetRectDimensionFromRightEdgeOverride(float x) { return x; }
bool GfxDebuggerIsDebugging(void) { return false; }
bool GfxDebuggerIsDebuggingRequested(void) { return false; }
void GfxDebuggerDebugDisplayList(void *commands) { (void)commands; }

void gSPDisplayList(Gfx *pkt,Gfx *list) {
    if (GameEngine_OTRSigCheck((char*)list)) list=sf64_ship_resource((char*)list);
    __gSPDisplayList(pkt,list);
}
void gSPVertex(Gfx *pkt,uintptr_t vertices,int n,int first) {
    if (GameEngine_OTRSigCheck((char*)vertices)) vertices=(uintptr_t)sf64_ship_resource((char*)vertices);
    __gSPVertex(pkt,vertices,n,first);
}
void gDPSetTileSizeInterp(Gfx *pkt,int tile,float a,float b,float c,float d) {
    __gDPSetTileSizeInterp(pkt,tile,0,0,0,0);
    memcpy(&pkt[1].words.w0,&a,4); memcpy(&pkt[1].words.w1,&b,4);
    memcpy(&pkt[2].words.w0,&c,4); memcpy(&pkt[2].words.w1,&d,4);
}
void gSPInvalidateTexCache(Gfx *pkt,uintptr_t address) { __gSPInvalidateTexCache(pkt,address); }

// Native 30 Hz mode has no extra interpolated render frames. Matrix operations
// execute in the upstream game; these optional recorders only capture extra frames.
void FrameInterpolation_StartRecord(void) {}
void FrameInterpolation_StopRecord(void) {}
void FrameInterpolation_ShouldInterpolateFrame(bool value) { (void)value; }
void FrameInterpolation_RecordMarker(const char *file,int line) { (void)file;(void)line; }
void FrameInterpolation_RecordOpenChild(const void *key,int id) { (void)key;(void)id; }
void FrameInterpolation_RecordCloseChild(void) {}
void FrameInterpolation_RecordMatrixPush(Matrix **m) { (void)m; }
void FrameInterpolation_RecordMatrixPop(Matrix **m) { (void)m; }
void FrameInterpolation_RecordMatrixMult(Matrix *m,MtxF *other,u8 mode) {(void)m;(void)other;(void)mode;}
void FrameInterpolation_RecordMatrixTranslate(Matrix *m,float x,float y,float z,u8 mode) {(void)m;(void)x;(void)y;(void)z;(void)mode;}
void FrameInterpolation_RecordMatrixScale(Matrix *m,float x,float y,float z,u8 mode) {(void)m;(void)x;(void)y;(void)z;(void)mode;}
void FrameInterpolation_RecordMatrixRotate1Coord(Matrix *m,u32 axis,float angle,u8 mode) {(void)m;(void)axis;(void)angle;(void)mode;}
void FrameInterpolation_RecordMatrixMtxFToMtx(MtxF *source,Mtx *dest) {(void)source;(void)dest;}
void FrameInterpolation_RecordMatrixMultVec3f(Matrix *m,Vec3f source,Vec3f dest) {(void)m;(void)source;(void)dest;}
void FrameInterpolation_RecordMatrixMultVec3fNoTranslate(Matrix *m,Vec3f source,Vec3f dest) {(void)m;(void)source;(void)dest;}

float __sinf(float x) { return sinf(x); }
float __cosf(float x) { return cosf(x); }
float guSqrtf(float x) { return sqrtf(x); }
float SIN_DEG(float x) { return sinf(x*0.0174532925199433f); }
float COS_DEG(float x) { return cosf(x*0.0174532925199433f); }
void guMtxIdentF(float out[4][4]) {
    for (int i=0;i<4;++i) for (int j=0;j<4;++j) out[i][j]=i==j;
}
void guMtxF2L(float source[4][4],Mtx *dest) { memcpy(dest,source,64); }
void guOrtho(Mtx *out,float left,float right,float bottom,float top,float near,float far,float scale) {
    float matrix[4][4]; guMtxIdentF(matrix);
    matrix[0][0]=2/(right-left); matrix[1][1]=2/(top-bottom); matrix[2][2]=-2/(far-near);
    matrix[3][0]=-(right+left)/(right-left); matrix[3][1]=-(top+bottom)/(top-bottom); matrix[3][2]=-(far+near)/(far-near);
    for(int i=0;i<4;++i) for(int j=0;j<4;++j) matrix[i][j]*=scale;
    guMtxF2L(matrix,out);
}

typedef struct { uint64_t at; void (*action)(s32*,s32); s32 *address,value; bool active; } ShipTimer;
static ShipTimer timers[16];
s32 Timer_CreateTask(u64 cycles,TimerAction action,s32 *address,s32 value) {
    for (int i=0;i<16;++i) if (!timers[i].active) {
        timers[i]=(ShipTimer){sf64_host_time_us()+cycles*1000/46875,action,address,value,true};
        return 0;
    }
    return -1;
}
void Timer_Increment(s32 *address,s32 value) { *address+=value; }
void sf64_ship_timer_poll(void) {
    uint64_t now=sf64_host_time_us();
    for(int i=0;i<16;++i) if(timers[i].active && now>=timers[i].at) {
        timers[i].active=false; timers[i].action(timers[i].address,timers[i].value);
    }
}

// Video is presented by the ESP MIPI backend, not an N64 VI peripheral.
void osViBlack(u8 value) { (void)value; }
void osViSwapBuffer(void *buffer) { (void)buffer; }
void osViSetSpecialFeatures(u32 features) { (void)features; }
void osViSetEvent(OSMesgQueue *queue,OSMesg message,u32 count) { (void)queue;(void)message;(void)count; }
void osSetEventMesg(OSEvent event,OSMesgQueue *queue,OSMesg message) { (void)event;(void)queue;(void)message; }
// CPU-decoded data is coherent; MIPI cache maintenance remains in esp_lcd.
void osWritebackDCacheAll(void) {}
void osInvalDCache(void *pointer,s32 size) {(void)pointer;(void)size;}
void osInvalICache(void *pointer,s32 size) {(void)pointer;(void)size;}
