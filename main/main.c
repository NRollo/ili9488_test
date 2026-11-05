#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "ili9488.h"
#ifdef CONFIG_XPT2046_ENABLE
#include "xpt2046.h"
#endif

static const char *TAG = "main";

static void delay_ms(int ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

static void label(const char *text)
{
    int w = ili9488_width();
    ili9488_fill_rect(0, 0, w, 20, COLOR_BLACK);
    ili9488_draw_text(4, 2, text, COLOR_WHITE, COLOR_BLACK, 2);
}

static void test_solid_fills(void)
{
    static const struct { uint16_t c; const char *n; } t[] = {
        {COLOR_RED, "RED"}, {COLOR_GREEN, "GREEN"}, {COLOR_BLUE, "BLUE"},
        {COLOR_WHITE, "WHITE"}, {COLOR_BLACK, "BLACK"},
    };
    for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        ili9488_fill_screen(t[i].c);
        ili9488_draw_text(10, 10, t[i].n, t[i].c == COLOR_WHITE ? COLOR_BLACK : COLOR_WHITE, t[i].c, 4);
        delay_ms(700);
    }
}

static void test_color_bars(void)
{
    static const uint16_t bars[] = {
        COLOR_WHITE, COLOR_YELLOW, COLOR_CYAN, COLOR_GREEN,
        COLOR_MAGENTA, COLOR_RED, COLOR_BLUE, COLOR_BLACK,
    };
    int w = ili9488_width(), h = ili9488_height();
    int bw = w / 8;
    for (int i = 0; i < 8; i++) {
        ili9488_fill_rect(i * bw, 0, (i == 7) ? w - i * bw : bw, h, bars[i]);
    }
    label("COLOR BARS");
    delay_ms(2000);
}

static void test_gradients(void)
{
    int w = ili9488_width(), h = ili9488_height();
    static uint16_t line[480];
    int band = h / 4;

    ili9488_fill_screen(COLOR_BLACK);
    for (int b = 0; b < 4; b++) {
        for (int x = 0; x < w; x++) {
            uint8_t v = (uint8_t)(x * 255 / (w - 1));
            switch (b) {
            case 0: line[x] = RGB565(v, 0, 0); break;
            case 1: line[x] = RGB565(0, v, 0); break;
            case 2: line[x] = RGB565(0, 0, v); break;
            default: line[x] = RGB565(v, v, v); break;
            }
        }
        for (int y = b * band; y < (b + 1) * band; y++) {
            ili9488_draw_bitmap(0, y, w, 1, line);
        }
    }
    label("GRADIENTS");
    delay_ms(2000);
}

static void test_geometry(void)
{
    int w = ili9488_width(), h = ili9488_height();
    ili9488_fill_screen(COLOR_BLACK);

    for (int x = 0; x < w; x += 40) ili9488_draw_vline(x, 0, h, COLOR_GRAY);
    for (int y = 0; y < h; y += 40) ili9488_draw_hline(0, y, w, COLOR_GRAY);

    // Border and corner markers verify the full active area and orientation
    ili9488_draw_rect(0, 0, w, h, COLOR_WHITE);
    ili9488_fill_rect(1, 1, 20, 20, COLOR_RED);
    ili9488_fill_rect(w - 21, 1, 20, 20, COLOR_GREEN);
    ili9488_fill_rect(1, h - 21, 20, 20, COLOR_BLUE);
    ili9488_fill_rect(w - 21, h - 21, 20, 20, COLOR_YELLOW);

    // Diagonal
    for (int i = 0; i < h; i++) {
        ili9488_draw_pixel(i * (w - 1) / (h - 1), i, COLOR_ORANGE);
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%dX%d", w, h);
    ili9488_draw_text(w / 2 - 7 * 6 * 4 / 2, h / 2 - 16, "ILI9488", COLOR_CYAN, COLOR_BLACK, 4);
    ili9488_draw_text(w / 2 - (int)strlen(buf) * 6 * 3 / 2, h / 2 + 24, buf, COLOR_YELLOW, COLOR_BLACK, 3);
    delay_ms(3000);
}

static void test_text(void)
{
    ili9488_fill_screen(COLOR_BLACK);
    int y = 8;
    for (int s = 1; s <= 6; s++) {
        ili9488_draw_text(8, y, "ABC XYZ 0123456789", COLOR_WHITE, COLOR_BLACK, s);
        y += 8 * s + 4;
    }
    delay_ms(2000);
}

static void test_speed(void)
{
    static const uint16_t c[] = {COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_WHITE};
    const int frames = 8;
    int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < frames; i++) {
        ili9488_fill_screen(c[i & 3]);
    }
    int64_t us = esp_timer_get_time() - t0;
    float fps = frames * 1e6f / (float)us;

    char buf[32];
    snprintf(buf, sizeof(buf), "FILL: %d FPS", (int)(fps + 0.5f));
    ili9488_fill_screen(COLOR_BLACK);
    ili9488_draw_text(20, 20, buf, COLOR_GREEN, COLOR_BLACK, 4);
    ESP_LOGI(TAG, "Full-screen fill: %.1f fps (%.1f ms/frame)", fps, us / 1000.0f / frames);
    delay_ms(2500);
}

#ifdef CONFIG_XPT2046_ENABLE
#define CAL_TIMEOUT_MS   15000
#define PAINT_IDLE_MS    30000
#define HEADER_H         24
#define BTN_EXIT_W       64
#define BTN_CLEAR_W      76

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

// Waits for a touch (up to timeout_ms), returns the raw reading, then waits for release.
static bool wait_touch_raw(int *rx, int *ry, uint32_t timeout_ms)
{
    uint32_t t0 = now_ms(), last_log = 0;
    while (!xpt2046_read_raw(rx, ry, NULL)) {
        if (now_ms() - t0 > timeout_ms) return false;
        if (now_ms() - last_log > 1000) {
            xpt2046_log_status();
            last_log = now_ms();
        }
        delay_ms(10);
    }
    int released = 0;
    while (released < 5) {
        released = xpt2046_read_raw(NULL, NULL, NULL) ? 0 : released + 1;
        delay_ms(10);
    }
    if (*rx == 0 && *ry == 0) {
        // IRQ says touched but the ADC returns nothing: the SPI data path to the controller is broken
        ESP_LOGE(TAG, "Touch detected but ADC readings are all zero - check T_DO (MISO), T_CS, T_CLK and T_DIN wiring");
        return false;
    }
    return true;
}

static void draw_cross(int x, int y, uint16_t c)
{
    ili9488_draw_hline(x - 12, y, 25, c);
    ili9488_draw_vline(x, y - 12, 25, c);
    ili9488_draw_rect(x - 5, y - 5, 11, 11, c);
}

static bool touch_calibrate(void)
{
    int w = ili9488_width(), h = ili9488_height();
    const int m = 24;
    int scr[3][2] = {{m, m}, {w - 1 - m, m}, {m, h - 1 - m}};
    int raw[3][2];
    bool ok = true;

    for (int i = 0; i < 3 && ok; i++) {
        ili9488_fill_screen(COLOR_BLACK);
        ili9488_draw_text(w / 2 - 15 * 6 * 2 / 2, h / 2 - 8, "TOUCH THE CROSS", COLOR_WHITE, COLOR_BLACK, 2);
        draw_cross(scr[i][0], scr[i][1], COLOR_RED);
        ok = wait_touch_raw(&raw[i][0], &raw[i][1], CAL_TIMEOUT_MS);
        if (ok) ESP_LOGI(TAG, "Cal point %d: raw %d,%d", i, raw[i][0], raw[i][1]);
    }

    xpt2046_cal_t cal;
    if (!ok || !xpt2046_compute_calibration(raw, scr, &cal)) {
        ESP_LOGE(TAG, "Calibration failed - skipping touch test");
        ili9488_fill_screen(COLOR_BLACK);
        ili9488_draw_text(10, 10, "TOUCH CAL FAILED", COLOR_RED, COLOR_BLACK, 3);
        ili9488_draw_text(10, 50, "CHECK T_CLK T_DIN T_DO T_CS", COLOR_WHITE, COLOR_BLACK, 2);
        delay_ms(4000);
        return false;
    }
    ESP_LOGI(TAG, "Calibration: swap=%d x=%.4f*a%+.1f y=%.4f*b%+.1f", cal.swap_xy, cal.sx, cal.ox, cal.sy, cal.oy);
    xpt2046_set_calibration(&cal);
    return true;
}

static void paint_header(int x, int y, bool touched)
{
    int w = ili9488_width();
    int exit_x = w - BTN_EXIT_W - 4;
    int clear_x = exit_x - BTN_CLEAR_W - 8;
    char buf[24];

    ili9488_fill_rect(0, 0, clear_x - 4, HEADER_H, COLOR_BLACK);
    if (touched) {
        snprintf(buf, sizeof(buf), "X%d Y%d", x, y);
    } else {
        snprintf(buf, sizeof(buf), "TOUCH TO DRAW");
    }
    ili9488_draw_text(4, 4, buf, COLOR_WHITE, COLOR_BLACK, 2);
}

static void paint_buttons(void)
{
    int w = ili9488_width();
    int exit_x = w - BTN_EXIT_W - 4;
    int clear_x = exit_x - BTN_CLEAR_W - 8;
    ili9488_fill_rect(clear_x, 1, BTN_CLEAR_W, HEADER_H - 2, COLOR_BLUE);
    ili9488_draw_text(clear_x + 8, 4, "CLEAR", COLOR_WHITE, COLOR_BLUE, 2);
    ili9488_fill_rect(exit_x, 1, BTN_EXIT_W, HEADER_H - 2, COLOR_RED);
    ili9488_draw_text(exit_x + 8, 4, "EXIT", COLOR_WHITE, COLOR_RED, 2);
}

static void paint_clear(void)
{
    int w = ili9488_width(), h = ili9488_height();
    ili9488_fill_rect(0, HEADER_H, w, h - HEADER_H, COLOR_BLACK);
    ili9488_draw_hline(0, HEADER_H, w, COLOR_GRAY);
    paint_header(0, 0, false);
    paint_buttons();
}

// Touch paint: draws dots under the finger. CLEAR wipes the canvas, EXIT (or 30 s without touch) ends the test.
static void test_touch(void)
{
    int w = ili9488_width(), h = ili9488_height();
    int exit_x = w - BTN_EXIT_W - 4;
    int clear_x = exit_x - BTN_CLEAR_W - 8;
    static const uint16_t pen[] = {COLOR_WHITE, COLOR_GREEN, COLOR_CYAN, COLOR_YELLOW, COLOR_ORANGE, COLOR_MAGENTA};
    int pen_i = 0;
    bool was_down = false;
    int last_x = -1, last_y = -1;
    uint32_t last_hdr = 0, last_touch = now_ms(), last_log = 0;

    paint_clear();
    while (now_ms() - last_touch < PAINT_IDLE_MS) {
        int x, y, z;
        if (!xpt2046_read(w, h, &x, &y, &z)) {
            was_down = false;
            if (now_ms() - last_log > 2000) {
                xpt2046_log_status();
                last_log = now_ms();
            }
            delay_ms(5);
            continue;
        }
        last_touch = now_ms();

        if (y < HEADER_H) {
            if (!was_down) {
                if (x >= exit_x) return;
                if (x >= clear_x) paint_clear();
            }
        } else {
            if (!was_down) pen_i = (pen_i + 1) % (sizeof(pen) / sizeof(pen[0]));
            ili9488_fill_rect(x - 2, y - 2, 5, 5, pen[pen_i]);
            if (now_ms() - last_hdr > 80) {
                paint_header(x, y, true);
                last_hdr = now_ms();
            }
            if (x != last_x || y != last_y) {
                ESP_LOGI(TAG, "touch %d,%d z=%d", x, y, z);
                last_x = x;
                last_y = y;
            }
        }
        was_down = true;
    }
    ESP_LOGW(TAG, "No touch for %d s, leaving touch test", PAINT_IDLE_MS / 1000);
}
#endif

void app_main(void)
{
    ili9488_config_t cfg = {
        .spi_host = CONFIG_ILI9488_SPI_HOST,
        .pin_mosi = CONFIG_ILI9488_PIN_MOSI,
#ifdef CONFIG_XPT2046_ENABLE
        .pin_miso = CONFIG_XPT2046_PIN_MISO,
#else
        .pin_miso = -1,
#endif
        .pin_sclk = CONFIG_ILI9488_PIN_SCLK,
        .pin_cs = CONFIG_ILI9488_PIN_CS,
        .pin_dc = CONFIG_ILI9488_PIN_DC,
        .pin_rst = CONFIG_ILI9488_PIN_RST,
        .pin_bl = CONFIG_ILI9488_PIN_BL,
#ifdef CONFIG_ILI9488_BL_ACTIVE_HIGH
        .bl_active_high = true,
#else
        .bl_active_high = false,
#endif
        .clock_hz = CONFIG_ILI9488_SPI_CLOCK_MHZ * 1000 * 1000,
        .rotation = CONFIG_ILI9488_ROTATION,
    };

    ESP_ERROR_CHECK(ili9488_init(&cfg));

#ifdef CONFIG_XPT2046_ENABLE
    xpt2046_config_t tcfg = {
        .spi_host = CONFIG_ILI9488_SPI_HOST,
        .pin_cs = CONFIG_XPT2046_PIN_CS,
        .pin_irq = CONFIG_XPT2046_PIN_IRQ,
        .clock_hz = CONFIG_XPT2046_SPI_CLOCK_KHZ * 1000,
        .z_threshold = CONFIG_XPT2046_Z_THRESHOLD,
    };
    ESP_ERROR_CHECK(xpt2046_init(&tcfg));
    if (touch_calibrate()) {
        test_touch();
    }
#endif

    while (1) {
        test_solid_fills();
        test_color_bars();
        test_gradients();
        test_geometry();
        test_text();
        test_speed();
    }
}
