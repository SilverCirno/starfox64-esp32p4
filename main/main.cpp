#include <cmath>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "display_st7102.h"
#include "tgx.h"
#include "font_tgx_Arial.h"
#include "virtual_controls.h"
#include "f3dex.h"
#include "rom_assets.h"
#include "../native/host.h"
#ifndef CONFIG_SF64_STARSHIP_RUNTIME
#define CONFIG_SF64_STARSHIP_RUNTIME 0
#endif
#if CONFIG_SF64_STARSHIP_RUNTIME
#include "ship_runtime.h"
#endif
#ifdef SF64_HAVE_ARWING_DL
extern const uint8_t arwing_segment_start[] asm("_binary_ast_arwing_bin_start");
extern const uint8_t arwing_segment_end[] asm("_binary_ast_arwing_bin_end");
#endif
#ifdef SF64_HAVE_ARWING_MODEL
#include "arwing_body.inc"
#endif

namespace {
constexpr int W = 320, H = 240, LCD_W = 480, LCD_H = 480;
constexpr auto SHADERS = tgx::SHADER_PERSPECTIVE | tgx::SHADER_ZBUFFER |
                         tgx::SHADER_UNLIT | tgx::SHADER_NOTEXTURE;
using Renderer = tgx::Renderer3D<tgx::RGB565, SHADERS>;
struct TriangleContext { Renderer *renderer; bool highlight; };
#ifdef SF64_HAVE_ARWING_DL
sf64::F3dex display_list;
sf64::RomAssets rom_assets;
sf64::RomSegment runtime_arwing;
void emit_triangle(void *context, const sf64::Vertex &av,
                   const sf64::Vertex &bv, const sf64::Vertex &cv) {
    auto &ctx=*static_cast<TriangleContext *>(context);
    const auto convert=[](const sf64::Vertex &v) {
        return tgx::fVec3{(v.x-66.5f)*2/133, v.y*2.0f/133, (v.z-5.0f)*2/133};
    };
    const auto a=convert(av), b=convert(bv), c=convert(cv);
    const float ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z;
    const float vx=c.x-a.x,vy=c.y-a.y,vz=c.z-a.z;
    const float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
    const float length=sqrtf(nx*nx+ny*ny+nz*nz);
    const float shade=length>0 ? 0.25f+0.7f*fabsf(nz)/length : 0.25f;
    ctx.renderer->setMaterialColor(ctx.highlight ? tgx::RGBf(1,1,1) :
                                  tgx::RGBf(shade*0.8f,shade*0.9f,shade));
    ctx.renderer->drawTriangle(a,b,c);
}
#endif
const char *TAG = "sf64_port";
SemaphoreHandle_t refresh;

bool IRAM_ATTR on_refresh(esp_lcd_panel_handle_t,
                         esp_lcd_dpi_panel_event_data_t *, void *) {
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(refresh, &wake);
    return wake == pdTRUE;
}

void inspect_rom() {
    const auto *rom = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
        static_cast<esp_partition_subtype_t>(0x40), "sf64rom");
    uint8_t header[64];
    if (!rom || esp_partition_read(rom, 0, header, sizeof(header)) != ESP_OK ||
        memcmp(header, "\x80\x37\x12\x40", 4)) {
        ESP_LOGW(TAG, "No big-endian ROM provisioned in sf64rom partition");
        return;
    }
    char title[21] = {};
    memcpy(title, header + 0x20, 20);
    ESP_LOGI(TAG, "ROM header: %s, revision %u (header only; host tool verifies SHA-1)",
             title, header[0x3f]);
}

// Preserve 4:3: scale 320x240 to 480x360, centered with 60-pixel letterboxes.
void scale_frame(const tgx::RGB565 *source, uint16_t *dest) {
    for (int y = 0; y < 360; ++y) {
        const auto *row = source + (y * 2 / 3) * W;
        auto *out = dest + (y + 60) * LCD_W;
        for (int x = 0; x < LCD_W; ++x) out[x] = row[x * 2 / 3].val;
    }
}

void draw_controls(uint16_t *dest, const sf64_input_state_t &input) {
    tgx::Image<tgx::RGB565> overlay(dest, LCD_W, LCD_H);
    const tgx::RGB565 off(4,8,10), on(5,50,20), ink(28,56,28);
    overlay.fillRect({0,479,0,59}, tgx::RGB565(uint16_t(0)));
    overlay.fillRect({0,479,420,479}, tgx::RGB565(uint16_t(0)));
    for (const auto &button : SF64_BUTTONS) {
        overlay.fillCircle({button.x,button.y}, button.radius,
            (input.held & button.mask) ? on : off, ink);
        overlay.drawText(button.label, {button.x-(int)strlen(button.label)*4,button.y+5},
            font_tgx_Arial_10, ink);
    }
    overlay.fillCircle({SF64_STICK_X,SF64_STICK_Y},SF64_STICK_R,off,ink);
    overlay.fillCircle({SF64_STICK_X+input.stick_x*18/80,
                        SF64_STICK_Y-input.stick_y*18/80},7,on,ink);
    overlay.drawText(CONFIG_SF64_STARSHIP_RUNTIME ? "STARSHIP" : "MODEL TEST",{300,35},font_tgx_Arial_10,ink);
}
}

extern "C" void app_main() {
    ESP_ERROR_CHECK(sf64_host_self_test()==0 ? ESP_OK : ESP_FAIL);
    ESP_LOGI(TAG,"Native platform FreeRTOS queue self-test passed (FIFO/full/empty/recreate)");
    ESP_ERROR_CHECK(sf64_native_os_self_test()==0 ? ESP_OK : ESP_FAIL);
    ESP_LOGI(TAG,"Native N64 ABI self-test passed (32-bit GBI/clock/queues/controller status)");
#if CONFIG_SF64_STARSHIP_RUNTIME
    ESP_LOGI(TAG,"Native Starship original game entry enabled; development renderer");
#else
    ESP_LOGW(TAG, "TGX/ST7102 bring-up ONLY. Star Fox 64 game engine is not linked yet.");
#endif
#ifdef SF64_HAVE_ARWING_MODEL
    ESP_LOGI(TAG, "ROM-derived Arwing BODY geometry test: 37 vertices / 70 triangles; no textures or skeleton");
#endif
#if defined(SF64_HAVE_ARWING_DL) && !CONFIG_SF64_STARSHIP_RUNTIME
    const auto rom_status=rom_assets.init();
    if (rom_status==ESP_OK) {
        ESP_ERROR_CHECK(rom_assets.load(9,runtime_arwing));
        display_list.set_segment(3,runtime_arwing.data,runtime_arwing.size);
        ESP_LOGI(TAG,"Arwing source: real ROM partition, decompressed at runtime");
    } else {
        ESP_LOGW(TAG,"ROM partition unavailable: %s; using embedded local segment",esp_err_to_name(rom_status));
        display_list.set_segment(3, arwing_segment_start, arwing_segment_end-arwing_segment_start);
    }
    const auto probe=display_list.run(0x03009b60,nullptr,nullptr);
    ESP_LOGI(TAG, "Runtime F3DEX subset: %u commands, %u triangles, %u skipped texture states",
             unsigned(probe.instructions),unsigned(probe.triangles),unsigned(probe.material_commands));
    ESP_ERROR_CHECK(probe.error==sf64::DlError::None && probe.triangles==70 ? ESP_OK : ESP_ERR_INVALID_RESPONSE);
#endif
    ESP_LOGI(TAG, "PSRAM heap total=%u free=%u",
             unsigned(heap_caps_get_total_size(MALLOC_CAP_SPIRAM)),
             unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    inspect_rom();
    refresh = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(refresh ? ESP_OK : ESP_ERR_NO_MEM);
    esp_lcd_panel_handle_t panel = nullptr;
    ESP_ERROR_CHECK(sf64_display_init(&panel));
    const auto touch_result = sf64_touch_init();
    if (touch_result != ESP_OK)
        ESP_LOGW(TAG, "Touch unavailable: %s; renderer test continues", esp_err_to_name(touch_result));
    uint16_t *fb[2] = {};
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(panel, 2,
        reinterpret_cast<void **>(&fb[0]), reinterpret_cast<void **>(&fb[1])));
    memset(fb[0], 0, LCD_W * LCD_H * sizeof(uint16_t));
    memset(fb[1], 0, LCD_W * LCD_H * sizeof(uint16_t));
    esp_lcd_dpi_panel_event_callbacks_t callbacks = {};
    callbacks.on_refresh_done = on_refresh;
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(panel, &callbacks, nullptr));

    auto *pixels = static_cast<tgx::RGB565 *>(heap_caps_malloc(W * H * sizeof(tgx::RGB565),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    auto *depth = static_cast<uint16_t *>(heap_caps_malloc(W * H * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ESP_ERROR_CHECK(pixels && depth ? ESP_OK : ESP_ERR_NO_MEM);
    tgx::Image<tgx::RGB565> image(pixels, W, H);
    Renderer renderer({W, H}, &image, depth);
    renderer.setPerspective(60, float(W) / H, 0.1f, 100.0f);
    renderer.setCulling(0);
    renderer.setShaders(SHADERS);
    ESP_LOGI(TAG, "Render/display buffers: %u bytes; no full-ROM PSRAM allocation",
             unsigned(2 * LCD_W * LCD_H * 2 + W * H * 4));
#if CONFIG_SF64_STARSHIP_RUNTIME
    ESP_ERROR_CHECK(sf64_ship_runtime_init());
#endif
    int back = 1;
    unsigned frame = 0;
    float angle = 0;
    bool paused = false;
    bool show_model = true;
    int64_t report_start = esp_timer_get_time(), render_us = 0;
    for (;;) {
        const auto start = esp_timer_get_time();
        const auto input = sf64_input_poll();
#if CONFIG_SF64_STARSHIP_RUNTIME
        ESP_ERROR_CHECK(sf64_ship_runtime_frame(reinterpret_cast<uint16_t *>(pixels),W,H) ? ESP_OK : ESP_ERR_NOT_SUPPORTED);
#else
        if (input.pressed & SF64_START) paused = !paused;
        if (input.pressed & SF64_B) show_model = !show_model;
        if (!paused) angle += 2;
        image.fillScreen(tgx::RGB565(1, 2, 4));
        renderer.clearZbuffer();
        renderer.setModelPosScaleRot({input.stick_x/80.0f, input.stick_y/80.0f, -4},
                                    {1,1,1}, angle, {0,1,0});
#ifdef SF64_HAVE_ARWING_DL
        if (show_model) {
            TriangleContext context{&renderer, bool(input.held & SF64_A)};
            const auto result=display_list.run(0x03009b60,emit_triangle,&context);
            ESP_ERROR_CHECK(result.error==sf64::DlError::None ? ESP_OK : ESP_ERR_INVALID_RESPONSE);
        } else
#elif defined(SF64_HAVE_ARWING_MODEL)
        if (show_model) {
            for (const auto &tri : ARWING_BODY_TRIANGLES) {
                const auto &a=ARWING_BODY_VERTICES[tri[0]],
                           &b=ARWING_BODY_VERTICES[tri[1]],
                           &c=ARWING_BODY_VERTICES[tri[2]];
                const float ux=b.x-a.x, uy=b.y-a.y, uz=b.z-a.z;
                const float vx=c.x-a.x, vy=c.y-a.y, vz=c.z-a.z;
                const float nx=uy*vz-uz*vy, ny=uz*vx-ux*vz, nz=ux*vy-uy*vx;
                const float length=sqrtf(nx*nx+ny*ny+nz*nz);
                const float shade=length>0 ? 0.25f+0.7f*fabsf(nz)/length : 0.25f;
                renderer.setMaterialColor((input.held & SF64_A) ? tgx::RGBf(1,1,1) :
                                          tgx::RGBf(shade*0.8f,shade*0.9f,shade));
                renderer.drawTriangle(a,b,c);
            }
        } else
#endif
        {
        // Synthetic geometry remains available for color/projection diagnostics.
        renderer.setMaterialColor((input.held & SF64_A) ? tgx::RGBf(1,1,1) : tgx::RGBf(0.2f,0.7f,1.0f));
        renderer.drawTriangle({-1, -0.7f, 0}, {1, -0.7f, 0}, {0, 1, 0});
        renderer.setMaterialColor(tgx::RGBf(1.0f, 0.3f, 0.1f));
        renderer.drawTriangle({-0.7f, -0.2f, -0.5f}, {0.7f, -0.2f, -0.5f}, {0, 0.7f, 0.5f});
        }
#endif
        render_us += esp_timer_get_time() - start;
        scale_frame(pixels, fb[back]);
        draw_controls(fb[back], input);
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_W, LCD_H, fb[back]));
        // Discard a possibly pending VSYNC, then wait two refresh boundaries so the
        // old scanout buffer is retired before it is reused (IDF 5.5 DPI semantics).
        xSemaphoreTake(refresh, 0);
        for (int i = 0; i < 2; ++i) {
            if (xSemaphoreTake(refresh, pdMS_TO_TICKS(1000)) != pdTRUE) {
                ESP_LOGE(TAG, "MIPI refresh timed out");
                ESP_ERROR_CHECK(ESP_ERR_TIMEOUT);
            }
        }
        back ^= 1;
        ++frame;
        if (frame % 120 == 0) {
            const auto now = esp_timer_get_time();
            ESP_LOGI(TAG, "Frame loop: %.2f fps, TGX+input %.2f ms/frame, free PSRAM %u",
                     120000000.0 / double(now - report_start), double(render_us) / 120000,
                     unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
            report_start = now;
            render_us = 0;
        }
    }
}
