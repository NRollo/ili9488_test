#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialises LVGL, the display (on top of ili9488_*) and the tick source. ili9488_init() must have been called.
// Until lvgl_port_start() is called, LVGL must be driven manually with lv_timer_handler().
esp_err_t lvgl_port_init(void);

// Registers the touch input (if enabled) and starts the task that runs lv_timer_handler().
// From here on every LVGL call (and every access to the display/touch SPI bus) must be wrapped in lvgl_port_lock().
esp_err_t lvgl_port_start(void);

bool lvgl_port_lock(int timeout_ms);   // timeout_ms < 0 waits forever
void lvgl_port_unlock(void);

#ifdef __cplusplus
}
#endif
