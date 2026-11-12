#include "../native/host.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "input.h"
#include <cstring>

namespace {
struct QueueSlot { void *key=nullptr; QueueHandle_t queue=nullptr; };
QueueSlot slots[64];
SemaphoreHandle_t registry_mutex;
portMUX_TYPE registry_init=portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t lookup(void *key) {
    if (!registry_mutex) return nullptr;
    xSemaphoreTake(registry_mutex,portMAX_DELAY);
    QueueHandle_t handle=nullptr;
    for (auto &slot:slots) if (slot.key==key) { handle=slot.queue; break; }
    xSemaphoreGive(registry_mutex);
    return handle;
}
bool eeprom_ready=false;
uint8_t eeprom[512];
bool open_eeprom(nvs_handle_t &handle) {
    if (!eeprom_ready) {
        if (nvs_flash_init()!=ESP_OK) return false;
        if (nvs_open("sf64",NVS_READWRITE,&handle)!=ESP_OK) return false;
        size_t size=sizeof(eeprom);
        esp_err_t err=nvs_get_blob(handle,"eeprom",eeprom,&size);
        if (err==ESP_ERR_NVS_NOT_FOUND) memset(eeprom,0xff,sizeof(eeprom));
        else if (err!=ESP_OK || size!=sizeof(eeprom)) { nvs_close(handle); return false; }
        eeprom_ready=true;
        return true;
    }
    return nvs_open("sf64",NVS_READWRITE,&handle)==ESP_OK;
}
}
extern "C" unsigned long long sf64_host_time_us() { return esp_timer_get_time(); }
extern "C" int sf64_host_self_test() {
    static int test_key;
    void *first=reinterpret_cast<void *>(0x1234), *second=reinterpret_cast<void *>(0x5678), *received=nullptr;
    int result=-1;
    if (sf64_host_mq_create(&test_key,2)!=0) return -1;
    do {
        if (sf64_host_mq_send(&test_key,first,0)!=0) break;
        if (sf64_host_mq_send(&test_key,second,0)!=0) break;
        if (sf64_host_mq_send(&test_key,first,0)!=-1) break;
        if (sf64_host_mq_count(&test_key)!=2) break;
        if (sf64_host_mq_recv(&test_key,&received,0)!=0 || received!=first) break;
        if (sf64_host_mq_recv(&test_key,&received,0)!=0 || received!=second) break;
        if (sf64_host_mq_recv(&test_key,&received,0)!=-1) break;
        if (sf64_host_mq_count(&test_key)!=0) break;
        if (sf64_host_mq_create(&test_key,1)!=0) break;
        if (sf64_host_mq_send(&test_key,first,0)!=0) break;
        if (sf64_host_mq_recv(&test_key,nullptr,0)!=0) break;
        result=0;
    } while (false);
    xSemaphoreTake(registry_mutex,portMAX_DELAY);
    for (auto &slot:slots) {
        if (slot.key==&test_key) {
            vQueueDelete(slot.queue);
            slot={};
            break;
        }
    }
    xSemaphoreGive(registry_mutex);
    return result;
}
extern "C" int sf64_host_mq_create(void *key,int capacity) {
    if (!key || capacity<1 || capacity>1024) return -1;
    // Queue registry is initialized before native audio/game tasks start.
    if (!registry_mutex) {
        auto *candidate=xSemaphoreCreateMutex();
        if (!candidate) return -1;
        portENTER_CRITICAL(&registry_init);
        if (!registry_mutex) { registry_mutex=candidate; candidate=nullptr; }
        portEXIT_CRITICAL(&registry_init);
        if (candidate) vSemaphoreDelete(candidate);
    }
    xSemaphoreTake(registry_mutex,portMAX_DELAY);
    QueueSlot *available=nullptr;
    for (auto &slot:slots) {
        if (slot.key==key) { available=&slot; break; }
        if (!slot.key && !available) available=&slot;
    }
    if (!available) { xSemaphoreGive(registry_mutex); return -1; }
    auto *new_queue=xQueueCreate(capacity,sizeof(void*));
    if (!new_queue) { xSemaphoreGive(registry_mutex); return -1; }
    // Native queue reinitialization must happen while its users are stopped.
    if (available->queue) vQueueDelete(available->queue);
    *available={key,new_queue};
    xSemaphoreGive(registry_mutex);
    return 0;
}
extern "C" int sf64_host_mq_send(void *key,void *message,int block) {
    auto *queue=lookup(key);
    return queue && xQueueSend(queue,&message,block ? portMAX_DELAY : 0)==pdTRUE ? 0 : -1;
}
extern "C" int sf64_host_mq_recv(void *key,void **message,int block) {
    auto *queue=lookup(key);
    void *discard;
    return queue && xQueueReceive(queue,message ? message : &discard,block ? portMAX_DELAY : 0)==pdTRUE ? 0 : -1;
}
extern "C" int sf64_host_mq_count(void *key) {
    auto *queue=lookup(key);
    return queue ? uxQueueMessagesWaiting(queue) : 0;
}
extern "C" int sf64_host_mq_destroy(void *key) {
    if (!registry_mutex) return -1;
    xSemaphoreTake(registry_mutex,portMAX_DELAY);
    int result=-1;
    for (auto &slot:slots) {
        if (slot.key==key) {
            vQueueDelete(slot.queue);
            slot={};
            result=0;
            break;
        }
    }
    xSemaphoreGive(registry_mutex);
    return result;
}
extern "C" void sf64_host_controller(unsigned short *buttons,signed char *x,signed char *y) {
    const auto state=sf64_input_current();
    *buttons=state.held; *x=state.stick_x; *y=state.stick_y;
}
extern "C" int sf64_host_eeprom_read(unsigned block,unsigned char *data) {
    if (block>=64 || !data) return -1;
    nvs_handle_t handle;
    if (!open_eeprom(handle)) return -1;
    memcpy(data,eeprom+block*8,8);
    nvs_close(handle);
    return 0;
}
extern "C" int sf64_host_eeprom_write(unsigned block,const unsigned char *data) {
    if (block>=64 || !data) return -1;
    nvs_handle_t handle;
    if (!open_eeprom(handle)) return -1;
    uint8_t previous[8];
    memcpy(previous,eeprom+block*8,8);
    memcpy(eeprom+block*8,data,8);
    esp_err_t err=nvs_set_blob(handle,"eeprom",eeprom,sizeof(eeprom));
    if (err==ESP_OK) err=nvs_commit(handle);
    if (err!=ESP_OK) memcpy(eeprom+block*8,previous,8);
    nvs_close(handle);
    return err==ESP_OK ? 0 : -1;
}
