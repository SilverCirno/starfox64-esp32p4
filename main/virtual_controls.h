#pragma once
#include <stdint.h>
#include "input.h"
// Screen-space layout shared by hit testing and drawing. No LVGL dependency.
typedef struct { int x, y, radius; uint16_t mask; const char *label; } sf64_button_t;
static const sf64_button_t SF64_BUTTONS[] = {
    {32,30,23,SF64_L,"L"}, {96,30,23,SF64_Z,"Z"},
    {240,30,26,SF64_START,"START"}, {448,30,23,SF64_R,"R"},
    {200,448,13,SF64_C_LEFT,"<"}, {230,433,13,SF64_C_UP,"^"},
    {260,448,13,SF64_C_RIGHT,">"}, {230,466,12,SF64_C_DOWN,"v"},
    {350,448,23,SF64_B,"B"}, {420,448,23,SF64_A,"A"}
};
enum { SF64_STICK_X=70, SF64_STICK_Y=449, SF64_STICK_R=24 };
