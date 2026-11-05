#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "ili9488.h"

static const char *TAG = "ili9488";

#define PANEL_W_NATIVE   320
#define PANEL_H_NATIVE   480
#define CHUNK_LINES      16
#define CHUNK_PIXELS     (480 * CHUNK_LINES)
#define CHUNK_BYTES      (CHUNK_PIXELS * 3)   // SPI interface only supports 18-bit colour: 3 bytes/pixel

#define CMD_SWRESET 0x01
#define CMD_SLPOUT  0x11
#define CMD_DISPON  0x29
#define CMD_CASET   0x2A
#define CMD_PASET   0x2B
#define CMD_RAMWR   0x2C
#define CMD_MADCTL  0x36
#define CMD_COLMOD  0x3A

static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_done;
static uint8_t *s_buf;          // DMA-capable scratch buffer
static int s_w = PANEL_W_NATIVE;
static int s_h = PANEL_H_NATIVE;
static ili9488_config_t s_cfg;

typedef struct {
    uint8_t cmd;
    uint8_t data[15];
    uint8_t len;
    uint16_t delay_ms;
} init_cmd_t;

static const init_cmd_t s_init_cmds[] = {
    {0xE0, {0x00, 0x03, 0x09, 0x08, 0x16, 0x0A, 0x3F, 0x78, 0x4C, 0x09, 0x0A, 0x08, 0x16, 0x1A, 0x0F}, 15, 0}, // positive gamma
    {0xE1, {0x00, 0x16, 0x19, 0x03, 0x0F, 0x05, 0x32, 0x45, 0x46, 0x04, 0x0E, 0x0D, 0x35, 0x37, 0x0F}, 15, 0}, // negative gamma
    {0xC0, {0x17, 0x15}, 2, 0},                 // power control 1
    {0xC1, {0x41}, 1, 0},                       // power control 2
    {0xC5, {0x00, 0x12, 0x80}, 3, 0},           // VCOM
    {CMD_COLMOD, {0x66}, 1, 0},                 // 18 bits/pixel
    {0xB0, {0x00}, 1, 0},                       // interface mode control
    {0xB1, {0xA0}, 1, 0},                       // frame rate 60 Hz
    {0xB4, {0x02}, 1, 0},                       // display inversion: 2-dot
    {0xB6, {0x02, 0x02}, 2, 0},                 // display function control
    {0xE9, {0x00}, 1, 0},                       // set image function (24-bit off)
    {0xF7, {0xA9, 0x51, 0x2C, 0x82}, 4, 0},     // adjust control 3
};

static void write_cmd(uint8_t cmd, const void *data, size_t len)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, cmd, data, len));
}

static bool on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    return woken == pdTRUE;
}

// Sends the contents of s_buf (len bytes) as pixel data and waits until the DMA transfer is finished.
static void send_pixels(size_t len)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(s_io, CMD_RAMWR, s_buf, len));
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "SPI transfer timeout");
    }
}

static void set_window(int x0, int y0, int x1, int y1)
{
    uint8_t col[4] = {x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF};
    uint8_t row[4] = {y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF};
    write_cmd(CMD_CASET, col, 4);
    write_cmd(CMD_PASET, row, 4);
}

static inline void put_px(uint8_t *p, uint16_t c)
{
    p[0] = (c >> 8) & 0xF8;          // R
    p[1] = (c >> 3) & 0xFC;          // G
    p[2] = (c << 3) & 0xF8;          // B
}

esp_err_t ili9488_init(const ili9488_config_t *cfg)
{
    s_cfg = *cfg;

    s_done = xSemaphoreCreateBinary();
    s_buf = heap_caps_malloc(CHUNK_BYTES, MALLOC_CAP_DMA);
    if (!s_done || !s_buf) {
        return ESP_ERR_NO_MEM;
    }

    if (cfg->pin_bl >= 0) {
        gpio_config_t bl = {
            .pin_bit_mask = 1ULL << cfg->pin_bl,
            .mode = GPIO_MODE_OUTPUT,
        };
        ESP_ERROR_CHECK(gpio_config(&bl));
        ili9488_set_backlight(false);
    }
    if (cfg->pin_rst >= 0) {
        gpio_config_t rst = {
            .pin_bit_mask = 1ULL << cfg->pin_rst,
            .mode = GPIO_MODE_OUTPUT,
        };
        ESP_ERROR_CHECK(gpio_config(&rst));
    }

    spi_bus_config_t bus = {
        .mosi_io_num = cfg->pin_mosi,
        .miso_io_num = cfg->pin_miso,
        .sclk_io_num = cfg->pin_sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CHUNK_BYTES,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(cfg->spi_host, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = cfg->pin_cs,
        .dc_gpio_num = cfg->pin_dc,
        .spi_mode = 0,
        .pclk_hz = cfg->clock_hz,
        .trans_queue_depth = 1,
        .on_color_trans_done = on_color_done,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)cfg->spi_host, &io_cfg, &s_io));

    // Hardware reset (if wired), then software reset
    if (cfg->pin_rst >= 0) {
        gpio_set_level(cfg->pin_rst, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(cfg->pin_rst, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        gpio_set_level(cfg->pin_rst, 1);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    write_cmd(CMD_SWRESET, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(150));

    for (size_t i = 0; i < sizeof(s_init_cmds) / sizeof(s_init_cmds[0]); i++) {
        write_cmd(s_init_cmds[i].cmd, s_init_cmds[i].data, s_init_cmds[i].len);
        if (s_init_cmds[i].delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(s_init_cmds[i].delay_ms));
        }
    }

    // MADCTL: MY=0x80 MX=0x40 MV=0x20 BGR=0x08
    static const uint8_t madctl[4] = {0x48, 0x28, 0x88, 0xE8};
    int rot = cfg->rotation & 3;
    write_cmd(CMD_MADCTL, &madctl[rot], 1);
    bool landscape = rot & 1;
    s_w = landscape ? PANEL_H_NATIVE : PANEL_W_NATIVE;
    s_h = landscape ? PANEL_W_NATIVE : PANEL_H_NATIVE;

    write_cmd(CMD_SLPOUT, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));
    write_cmd(CMD_DISPON, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    ili9488_fill_screen(COLOR_BLACK);
    ili9488_set_backlight(true);
    ESP_LOGI(TAG, "Initialised %dx%d, SPI %d MHz", s_w, s_h, cfg->clock_hz / 1000000);
    return ESP_OK;
}

void ili9488_set_backlight(bool on)
{
    if (s_cfg.pin_bl < 0) {
        return;
    }
    gpio_set_level(s_cfg.pin_bl, (on == s_cfg.bl_active_high) ? 1 : 0);
}

int ili9488_width(void) { return s_w; }
int ili9488_height(void) { return s_h; }

void ili9488_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s_w) { w = s_w - x; }
    if (y + h > s_h) { h = s_h - y; }
    if (w <= 0 || h <= 0) {
        return;
    }

    int rows_per_chunk = CHUNK_PIXELS / w;
    if (rows_per_chunk > h) {
        rows_per_chunk = h;
    }
    uint8_t px[3];
    put_px(px, color);
    for (int i = 0; i < rows_per_chunk * w; i++) {
        memcpy(&s_buf[i * 3], px, 3);
    }

    for (int row = 0; row < h; row += rows_per_chunk) {
        int rows = (h - row < rows_per_chunk) ? (h - row) : rows_per_chunk;
        set_window(x, y + row, x + w - 1, y + row + rows - 1);
        send_pixels((size_t)rows * w * 3);
    }
}

void ili9488_fill_screen(uint16_t color)
{
    ili9488_fill_rect(0, 0, s_w, s_h, color);
}

void ili9488_draw_pixel(int x, int y, uint16_t color)
{
    ili9488_fill_rect(x, y, 1, 1, color);
}

void ili9488_draw_hline(int x, int y, int w, uint16_t color)
{
    ili9488_fill_rect(x, y, w, 1, color);
}

void ili9488_draw_vline(int x, int y, int h, uint16_t color)
{
    ili9488_fill_rect(x, y, 1, h, color);
}

void ili9488_draw_rect(int x, int y, int w, int h, uint16_t color)
{
    ili9488_draw_hline(x, y, w, color);
    ili9488_draw_hline(x, y + h - 1, w, color);
    ili9488_draw_vline(x, y, h, color);
    ili9488_draw_vline(x + w - 1, y, h, color);
}

void ili9488_draw_bitmap(int x, int y, int w, int h, const uint16_t *pixels)
{
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > s_w || y + h > s_h) {
        return;
    }
    int rows_per_chunk = CHUNK_PIXELS / w;
    for (int row = 0; row < h; row += rows_per_chunk) {
        int rows = (h - row < rows_per_chunk) ? (h - row) : rows_per_chunk;
        const uint16_t *src = pixels + (size_t)row * w;
        for (int i = 0; i < rows * w; i++) {
            put_px(&s_buf[i * 3], src[i]);
        }
        set_window(x, y + row, x + w - 1, y + row + rows - 1);
        send_pixels((size_t)rows * w * 3);
    }
}

// ---- 5x7 font: 5 column bytes per glyph, bit0 = top row ----
static const char s_font_chars[] = " -./0123456789:ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const uint8_t s_font[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // ' '
    {0x08, 0x08, 0x08, 0x08, 0x08}, // '-'
    {0x00, 0x60, 0x60, 0x00, 0x00}, // '.'
    {0x20, 0x10, 0x08, 0x04, 0x02}, // '/'
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // ':'
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
};

static const uint8_t *glyph_for(char c)
{
    if (c >= 'a' && c <= 'z') {
        c -= 32;
    }
    const char *p = strchr(s_font_chars, c);
    if (!p || c == '\0') {
        p = s_font_chars; // unknown -> space
    }
    return s_font[p - s_font_chars];
}

void ili9488_draw_text(int x, int y, const char *text, uint16_t fg, uint16_t bg, int scale)
{
    if (scale < 1) scale = 1;
    if (scale > 10) scale = 10;
    const int cw = 6 * scale;   // 5 columns + 1 spacing
    const int ch = 8 * scale;   // 7 rows + 1 spacing

    for (; *text; text++, x += cw) {
        if (x < 0 || y < 0 || x + cw > s_w || y + ch > s_h) {
            continue;
        }
        const uint8_t *g = glyph_for(*text);
        for (int py = 0; py < ch; py++) {
            int gy = py / scale;
            for (int px = 0; px < cw; px++) {
                int gx = px / scale;
                bool on = (gx < 5 && gy < 7) && ((g[gx] >> gy) & 1);
                put_px(&s_buf[(py * cw + px) * 3], on ? fg : bg);
            }
        }
        set_window(x, y, x + cw - 1, y + ch - 1);
        send_pixels((size_t)cw * ch * 3);
    }
}
