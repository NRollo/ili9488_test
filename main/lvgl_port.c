#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "ili9488.h"
#include "lvgl_port.h"
#ifdef CONFIG_XPT2046_ENABLE
#include "xpt2046.h"
#endif

#define DRAW_BUF_LINES   40
#define TASK_STACK_BYTES 8192
#define TASK_PRIORITY    2
#define MAX_SLEEP_MS     20  // Reverted to original

static const char *TAG = "lvgl_port";

static SemaphoreHandle_t s_mutex;
static lv_display_t *s_disp;

static uint32_t tick_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// The driver converts and sends the pixels synchronously, so the buffer is free again on return.
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    ili9488_draw_bitmap(area->x1, area->y1, lv_area_get_width(area), lv_area_get_height(area), (const uint16_t *)px_map);
    lv_display_flush_ready(disp);
}

#ifdef CONFIG_XPT2046_ENABLE
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    int x, y;
    if (xpt2046_read(ili9488_width(), ili9488_height(), &x, &y, NULL)) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
#endif

static void lvgl_task(void *arg)
{
    while (1) {
        lvgl_port_lock(-1);
        uint32_t sleep_ms = lv_timer_handler();
        lvgl_port_unlock();
        if (sleep_ms < 1) sleep_ms = 1;
        if (sleep_ms > MAX_SLEEP_MS) sleep_ms = MAX_SLEEP_MS;
        vTaskDelay(pdMS_TO_TICKS(sleep_ms));
    }
}

esp_err_t lvgl_port_init(void)
{
    s_mutex = xSemaphoreCreateRecursiveMutex();
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }

    lv_init();
    lv_tick_set_cb(tick_cb);

    int w = ili9488_width(), h = ili9488_height();
    size_t buf_bytes = (size_t)w * DRAW_BUF_LINES * sizeof(uint16_t);
    void *buf = heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }

    s_disp = lv_display_create(w, h);
    if (!s_disp) {
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_disp, buf, NULL, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_disp, flush_cb);

    ESP_LOGI(TAG, "LVGL %d.%d.%d, %dx%d, %u byte draw buffer", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR,
             LVGL_VERSION_PATCH, w, h, (unsigned)buf_bytes);
    return ESP_OK;
}

esp_err_t lvgl_port_start(void)
{
#ifdef CONFIG_XPT2046_ENABLE
    lv_indev_t *indev = lv_indev_create();
    if (!indev) {
        return ESP_ERR_NO_MEM;
    }
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_display(indev, s_disp);
#endif
    if (xTaskCreate(lvgl_task, "lvgl", TASK_STACK_BYTES, NULL, TASK_PRIORITY, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool lvgl_port_lock(int timeout_ms)
{
    TickType_t ticks = timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_mutex, ticks) == pdTRUE;
}

void lvgl_port_unlock(void)
{
    xSemaphoreGiveRecursive(s_mutex);
}
