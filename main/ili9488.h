#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int spi_host;       // spi_host_device_t value (SPI2_HOST / SPI3_HOST)
    int pin_mosi;
    int pin_miso;       // -1 if unused (needed only when another device on the bus, e.g. touch, is read)
    int pin_sclk;
    int pin_cs;
    int pin_dc;
    int pin_rst;        // -1 if unused
    int pin_bl;         // -1 if unused
    bool bl_active_high;
    int clock_hz;
    int rotation;       // 0..3
} ili9488_config_t;

esp_err_t ili9488_init(const ili9488_config_t *cfg);
void ili9488_set_backlight(bool on);

int ili9488_width(void);
int ili9488_height(void);

// Fills the whole screen with an RGB565 colour. Not thread-safe (shared DMA buffer).
void ili9488_fill_screen(uint16_t color);

// Draws an RGB565 bitmap (host endian uint16_t array, w*h pixels) and blocks until it has been sent.
// Ignored unless fully inside the screen. Not thread-safe (shared DMA buffer).
void ili9488_draw_bitmap(int x, int y, int w, int h, const uint16_t *pixels);

#ifdef __cplusplus
}
#endif
