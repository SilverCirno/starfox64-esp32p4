#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void sf64_ship_begin(void);
const void *sf64_ship_step(unsigned *command_count);
int sf64_ship_state(void);
typedef struct {
    int32_t play_state,player_state,shots,frame;
    float path,x,y;
} Sf64GameplayStats;
void sf64_ship_gameplay_stats(Sf64GameplayStats *stats);
void *sf64_ship_resource(const char *name);
void sf64_ship_resource_info(const char *name,int32_t *width,int32_t *height);
void sf64_ship_fail(const char *name);
void sf64_ship_timer_poll(void);
#ifdef __cplusplus
}
#endif
