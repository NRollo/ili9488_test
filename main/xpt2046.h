#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int spi_host;       // same host the display is on (bus must have MISO configured)
    int pin_cs;
    int pin_irq;        // -1 if unused (pressure alone is used to detect touch)
    int clock_hz;       // XPT2046 tops out around 2 MHz
    int z_threshold;    // minimum pressure value that counts as a touch
} xpt2046_config_t;

// Maps raw 12-bit readings to screen pixels: optionally swap axes, then x = sx * a + ox, y = sy * b + oy
typedef struct {
    bool swap_xy;
    float sx, ox, sy, oy;
} xpt2046_cal_t;

esp_err_t xpt2046_init(const xpt2046_config_t *cfg);

// Returns true while touched. Raw values are the median of several samples. Any output pointer may be NULL.
bool xpt2046_read_raw(int *raw_x, int *raw_y, int *pressure);

void xpt2046_set_calibration(const xpt2046_cal_t *cal);

// Logs the raw pressure/IRQ/position readings once, for wiring and threshold diagnosis.
void xpt2046_log_status(void);

// Derives a calibration from raw readings taken at three known screen points:
// top-left (p[0]), top-right (p[1]) and bottom-left (p[2]). Axis swap is detected automatically.
bool xpt2046_compute_calibration(const int raw[3][2], const int screen[3][2], xpt2046_cal_t *out);

// Calibrated read; coordinates are clamped to [0, width) x [0, height).
bool xpt2046_read(int width, int height, int *x, int *y, int *pressure);

#ifdef __cplusplus
}
#endif
