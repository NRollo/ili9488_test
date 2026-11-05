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

// RGB565 helpers
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))

#define COLOR_BLACK   RGB565(0, 0, 0)
#define COLOR_WHITE   RGB565(255, 255, 255)
#define COLOR_RED     RGB565(255, 0, 0)
#define COLOR_GREEN   RGB565(0, 255, 0)
#define COLOR_BLUE    RGB565(0, 0, 255)
#define COLOR_YELLOW  RGB565(255, 255, 0)
#define COLOR_CYAN    RGB565(0, 255, 255)
#define COLOR_MAGENTA RGB565(255, 0, 255)
#define COLOR_ORANGE  RGB565(255, 128, 0)
#define COLOR_GRAY    RGB565(128, 128, 128)

esp_err_t ili9488_init(const ili9488_config_t *cfg);
void ili9488_set_backlight(bool on);

int ili9488_width(void);
int ili9488_height(void);

// Rectangle covers x in [x, x+w), y in [y, y+h). Clipped to screen. Not thread-safe (shared DMA buffer).
void ili9488_fill_rect(int x, int y, int w, int h, uint16_t color);
void ili9488_fill_screen(uint16_t color);
void ili9488_draw_pixel(int x, int y, uint16_t color);
void ili9488_draw_hline(int x, int y, int w, uint16_t color);
void ili9488_draw_vline(int x, int y, int h, uint16_t color);
void ili9488_draw_rect(int x, int y, int w, int h, uint16_t color);

// Draw an RGB565 bitmap (host endian uint16_t array, w*h pixels). Ignored unless fully inside the screen.
void ili9488_draw_bitmap(int x, int y, int w, int h, const uint16_t *pixels);

// Built-in 5x7 font (uppercase letters, digits, a few symbols). scale >= 1.
void ili9488_draw_text(int x, int y, const char *text, uint16_t fg, uint16_t bg, int scale);

#ifdef __cplusplus
}
#endif
