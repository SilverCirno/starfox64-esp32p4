#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
// N64 controller bit positions, independent of touch/USB/GPIO drivers.
enum {
    SF64_A=0x8000, SF64_B=0x4000, SF64_Z=0x2000, SF64_START=0x1000,
    SF64_D_UP=0x0800, SF64_D_DOWN=0x0400, SF64_D_LEFT=0x0200, SF64_D_RIGHT=0x0100,
    SF64_L=0x0020, SF64_R=0x0010,
    SF64_C_UP=0x0008, SF64_C_DOWN=0x0004, SF64_C_LEFT=0x0002, SF64_C_RIGHT=0x0001
};
typedef struct {
    int8_t stick_x, stick_y; // [-80,80], positive y is up
    uint16_t held, pressed, released;
} sf64_input_state_t;
// Providers return instantaneous held/stick values. The input layer computes edges.
// Register before starting sampling; provider/context must outlive registration.
// Poll/current belong to the game task; only one task may call sample.
typedef bool (*sf64_input_poll_fn)(void *context, sf64_input_state_t *state);
void sf64_input_set_provider(sf64_input_poll_fn poll, void *context);
sf64_input_state_t sf64_input_poll(void);
// Read the last sample without touching the driver or consuming its report.
sf64_input_state_t sf64_input_current(void);
// Optional single sampling task: latch presses between slow game frames.
void sf64_input_sample(void);
void sf64_input_set_background(bool enabled);
esp_err_t sf64_touch_init(void);
#ifdef __cplusplus
}
#endif
