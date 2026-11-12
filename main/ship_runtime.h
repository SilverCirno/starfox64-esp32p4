#pragma once
#include <stdint.h>
#include "esp_err.h"
esp_err_t sf64_ship_runtime_init(void);
bool sf64_ship_runtime_frame(uint16_t *pixels,int width,int height);
