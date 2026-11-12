#include <math.h>
#include "driver/i2c_master.h"
#include "esp_lcd_touch_st7123.h"
#include "esp_log.h"
#include "virtual_controls.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sf64_touch";
static esp_lcd_touch_handle_t s_touch;
static i2c_master_bus_handle_t s_bus;
static esp_lcd_panel_io_handle_t s_io;
static void touch_sampling_task(void *context) {
    (void)context;
    for(;;) { sf64_input_sample(); vTaskDelay(pdMS_TO_TICKS(10)); }
}

static bool touch_poll(void *context, sf64_input_state_t *state) {
    (void)context;
    if (esp_lcd_touch_read_data(s_touch) != ESP_OK) return false;
    uint16_t x[5], y[5], strength[5];
    uint8_t count = 0;
    if (!esp_lcd_touch_get_coordinates(s_touch, x, y, strength, &count, 5)) return true;
    bool stick_assigned = false;
    for (int p=0; p<count; ++p) {
        for (unsigned b=0; b<sizeof(SF64_BUTTONS)/sizeof(SF64_BUTTONS[0]); ++b) {
            int dx=(int)x[p]-SF64_BUTTONS[b].x, dy=(int)y[p]-SF64_BUTTONS[b].y;
            if (dx*dx + dy*dy <= SF64_BUTTONS[b].radius*SF64_BUTTONS[b].radius)
                state->held |= SF64_BUTTONS[b].mask;
        }
        // Reserve left bottom area for the stick; a released touch produces zero.
        if (!stick_assigned && x[p]<150 && y[p]>=420 && y[p]<480) {
            float dx=(int)x[p]-SF64_STICK_X, dy=SF64_STICK_Y-(int)y[p];
            float distance=sqrtf(dx*dx + dy*dy);
            if (distance>4) {
                float magnitude=fminf(1.0f, (distance-4)/(SF64_STICK_R-4));
                state->stick_x=(int8_t)lroundf(80*magnitude*dx/distance);
                state->stick_y=(int8_t)lroundf(80*magnitude*dy/distance);
            }
            stick_assigned=true;
        }
    }
    return true;
}

esp_err_t sf64_touch_init(void) {
    i2c_master_bus_config_t bus = {
        .i2c_port=I2C_NUM_0, .sda_io_num=GPIO_NUM_7, .scl_io_num=GPIO_NUM_8,
        .clk_source=I2C_CLK_SRC_DEFAULT,
        .flags.enable_internal_pullup=true,
    };
    esp_err_t err=i2c_new_master_bus(&bus, &s_bus);
    if (err!=ESP_OK) return err;
    esp_lcd_panel_io_i2c_config_t io=ESP_LCD_TOUCH_IO_I2C_ST7123_CONFIG();
    io.scl_speed_hz=400000;
    err=esp_lcd_new_panel_io_i2c(s_bus, &io, &s_io);
    if (err!=ESP_OK) goto cleanup;
    const esp_lcd_touch_config_t config={
        .x_max=480, .y_max=480, .rst_gpio_num=GPIO_NUM_NC, .int_gpio_num=GPIO_NUM_NC,
        .levels={.reset=0, .interrupt=0},
        .flags={.swap_xy=false, .mirror_x=false, .mirror_y=false},
    };
    esp_lcd_touch_config_t calibrated=config;
#ifdef CONFIG_SF64_TOUCH_SWAP_XY
    calibrated.flags.swap_xy=true;
#endif
#ifdef CONFIG_SF64_TOUCH_MIRROR_X
    calibrated.flags.mirror_x=true;
#endif
#ifdef CONFIG_SF64_TOUCH_MIRROR_Y
    calibrated.flags.mirror_y=true;
#endif
    err=esp_lcd_touch_new_i2c_st7123(s_io, &calibrated, &s_touch);
    if (err!=ESP_OK) goto cleanup;
    sf64_input_set_provider(touch_poll, NULL);
    if(xTaskCreatePinnedToCore(touch_sampling_task,"sf64_input",4096,NULL,3,NULL,1)!=pdPASS) {
        sf64_input_set_provider(NULL,NULL);
        esp_lcd_touch_del(s_touch); s_touch=NULL;
        err=ESP_ERR_NO_MEM; goto cleanup;
    }
    sf64_input_set_background(true);
    ESP_LOGI(TAG, "ST7123 virtual controller on SDA7/SCL8, replaceable input provider");
    return ESP_OK;
cleanup:
    if (s_io) { esp_lcd_panel_io_del(s_io); s_io=NULL; }
    i2c_del_master_bus(s_bus);
    s_bus=NULL;
    return err;
}
