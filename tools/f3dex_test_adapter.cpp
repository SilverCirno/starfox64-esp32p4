#include "f3dex.h"
#include "input.h"
#include "mio0.h"
#include "audio_output.h"
#include "n64_alpha.h"
static uint8_t input[128*1024];
static int16_t triangles[4096*9];
static sf64::DlResult result;
static unsigned count;
static uint8_t packed_mio0[2*1024*1024], decoded_mio0[4*1024*1024];
static sf64_input_state_t injected{}, last_input{};
static bool valid_input;
static bool pcm_accept;
static int pcm_sum;
static bool pcm_sink(void *, const int16_t *samples,size_t frames,unsigned rate) {
    if (frames!=1 || rate!=32000) return false;
    pcm_sum=samples[0]+samples[1];
    return pcm_accept;
}
static bool poll_input(void *, sf64_input_state_t *out) { *out=injected; return valid_input; }
static void emit(void *, const sf64::Vertex &a, const sf64::Vertex &b, const sf64::Vertex &c) {
    if (count>=4096) return;
    auto *out=triangles+count*9;
    const sf64::Vertex points[3]={a,b,c};
    for (const auto &v:points) { *out++=v.x; *out++=v.y; *out++=v.z; }
    ++count;
}
extern "C" {
unsigned test_rdp_alpha(unsigned high,unsigned low,unsigned channels,unsigned mode) {
    return sf64::rdp_alpha(high,low,mode&3,channels>>24,(channels>>16)&255,
        (channels>>8)&255,channels&255,(mode>>8)&255);
}
int test_pcm(unsigned mode,unsigned frames,unsigned rate) {
    static const int16_t samples[2]={32767,-32768};
    pcm_accept=mode==1;
    sf64_audio_set_sink(mode ? pcm_sink : nullptr,nullptr);
    return sf64_audio_submit(samples,frames,rate);
}
int test_pcm_sum() { return pcm_sum; }
uint8_t *test_mio_source() { return packed_mio0; }
uint8_t *test_mio_dest() { return decoded_mio0; }
int test_mio_decode(unsigned source_size,unsigned dest_size) {
    if (source_size>sizeof(packed_mio0) || dest_size>sizeof(decoded_mio0)) return -1;
    return sf64::decode_mio0(packed_mio0,source_size,decoded_mio0,dest_size);
}
void test_input_step(int x, int y, unsigned held, unsigned valid) {
    injected={int8_t(x),int8_t(y),uint16_t(held),0,0};
    valid_input=valid;
    sf64_input_set_provider(poll_input,nullptr);
    last_input=sf64_input_poll();
}
void test_input_disconnect() {
    sf64_input_set_provider(nullptr,nullptr);
    last_input=sf64_input_poll();
}
unsigned test_input_held() { return last_input.held; }
unsigned test_input_pressed() { return last_input.pressed; }
unsigned test_input_released() { return last_input.released; }
void test_input_background(unsigned enabled) { sf64_input_set_background(enabled); }
void test_input_report(unsigned held) {
    injected={0,0,uint16_t(held),0,0}; valid_input=true;
    sf64_input_set_provider(poll_input,nullptr); sf64_input_sample();
}
void test_input_consume() { last_input=sf64_input_poll(); }
unsigned test_input_snapshot() { return sf64_input_current().held; }
int test_input_x() { return last_input.stick_x; }
int test_input_y() { return last_input.stick_y; }
void *memset(void *dest, int value, size_t bytes) {
    volatile auto *out=static_cast<volatile uint8_t *>(dest);
    for (size_t i=0;i<bytes;++i) out[i]=uint8_t(value);
    return dest;
}
void *memcpy(void *dest, const void *src, size_t bytes) {
    auto *out=static_cast<uint8_t *>(dest);
    const auto *in=static_cast<const uint8_t *>(src);
    for (size_t i=0;i<bytes;++i) out[i]=in[i];
    return dest;
}
uint8_t *test_input() { return input; }
int16_t *test_triangles() { return triangles; }
unsigned test_count() { return count; }
unsigned test_commands() { return result.instructions; }
int test_run(unsigned size, unsigned address) {
    if (size>sizeof(input)) return -1;
    sf64::F3dex interpreter;
    interpreter.set_segment(3,input,size);
    count=0;
    result=interpreter.run(address,emit,nullptr);
    return int(result.error);
}
}
