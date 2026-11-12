#include "input.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
#define LOCK() portENTER_CRITICAL(&s_lock)
#define UNLOCK() portEXIT_CRITICAL(&s_lock)
#else
#define LOCK() ((void)0)
#define UNLOCK() ((void)0)
#endif
static sf64_input_poll_fn s_poll;
static void *s_context;
static uint16_t s_held;
static sf64_input_state_t s_current;
static sf64_input_state_t s_latest;
static uint16_t s_sample_held,s_pending;
static bool s_background;
void sf64_input_set_provider(sf64_input_poll_fn poll, void *context) {
    s_poll = poll;
    s_context = context;
}
void sf64_input_sample(void) {
    sf64_input_state_t state = {0};
    if (s_poll && !s_poll(s_context, &state)) state = (sf64_input_state_t){0};
    if (state.stick_x > 80) state.stick_x = 80;
    if (state.stick_x < -80) state.stick_x = -80;
    if (state.stick_y > 80) state.stick_y = 80;
    if (state.stick_y < -80) state.stick_y = -80;
    LOCK();
    s_pending |= state.held & ~s_sample_held;
    s_sample_held=state.held;
    s_latest=state;
    UNLOCK();
}
void sf64_input_set_background(bool enabled) { s_background=enabled; }
sf64_input_state_t sf64_input_poll(void) {
    if(!s_background) sf64_input_sample();
    LOCK();
    sf64_input_state_t state=s_latest;
    // Deliver a short press for at least one game frame even if it was released
    // while a slow render was still running. Held input retains normal semantics.
    state.held |= s_pending;
    s_pending=0;
    state.pressed = state.held & ~s_held;
    state.released = s_held & ~state.held;
    s_held = state.held;
    s_current = state;
    UNLOCK();
    return state;
}
sf64_input_state_t sf64_input_current(void) {
    LOCK(); sf64_input_state_t state=s_current; UNLOCK(); return state;
}
