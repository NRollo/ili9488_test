#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "ili9488.h"
#include "lvgl_port.h"
#ifdef CONFIG_XPT2046_ENABLE
#include "xpt2046.h"
#endif

static const char *TAG = "main";

#define UI_TASK_STACK_BYTES 8192

#define C_BLACK   lv_color_hex(0x000000)
#define C_WHITE   lv_color_hex(0xFFFFFF)
#define C_RED     lv_color_hex(0xFF0000)
#define C_GREEN   lv_color_hex(0x00FF00)
#define C_BLUE    lv_color_hex(0x0000FF)
#define C_YELLOW  lv_color_hex(0xFFFF00)
#define C_CYAN    lv_color_hex(0x00FFFF)
#define C_MAGENTA lv_color_hex(0xFF00FF)
#define C_ORANGE  lv_color_hex(0xFF8000)
#define C_GRAY    lv_color_hex(0x808080)

#define LOCK()   lvgl_port_lock(-1)
#define UNLOCK() lvgl_port_unlock()

static lv_obj_t *s_scr;     // the single screen every test draws on

static void delay_ms(int ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

static void screen_reset(lv_color_t bg)
{
    lv_obj_clean(s_scr);
    lv_obj_set_style_bg_color(s_scr, bg, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
}

// Plain filled rectangle without any theme styling
static lv_obj_t *rect(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t c)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

// 1 px outline, transparent inside
static lv_obj_t *frame(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t c)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, c, 0);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
    return o;
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, lv_color_t c, const char *str)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, str);
    return l;
}

static void caption(const char *str)
{
    int w = lv_obj_get_width(s_scr);
    rect(s_scr, 0, 0, w, 24, C_BLACK);
    lv_obj_t *l = text(s_scr, &lv_font_montserrat_20, C_WHITE, str);
    lv_obj_set_pos(l, 4, 1);
}

static void test_solid_fills(void)
{
    static const struct { uint32_t c; const char *n; } t[] = {
        {0xFF0000, "RED"}, {0x00FF00, "GREEN"}, {0x0000FF, "BLUE"},
        {0xFFFFFF, "WHITE"}, {0x000000, "BLACK"},
    };
    for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        LOCK();
        screen_reset(lv_color_hex(t[i].c));
        lv_obj_t *l = text(s_scr, &lv_font_montserrat_28, i == 3 ? C_BLACK : C_WHITE, t[i].n);
        lv_obj_set_pos(l, 10, 10);
        UNLOCK();
        delay_ms(700);
    }
}

static void test_color_bars(void)
{
    static const uint32_t bars[] = {
        0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0x0000FF, 0x000000,
    };
    LOCK();
    screen_reset(C_BLACK);
    int w = lv_obj_get_width(s_scr), h = lv_obj_get_height(s_scr);
    int bw = w / 8;
    for (int i = 0; i < 8; i++) {
        rect(s_scr, i * bw, 0, (i == 7) ? w - i * bw : bw, h, lv_color_hex(bars[i]));
    }
    caption("COLOR BARS");
    UNLOCK();
    delay_ms(2000);
}

static void test_gradients(void)
{
    static const uint32_t ends[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF};
    LOCK();
    screen_reset(C_BLACK);
    int w = lv_obj_get_width(s_scr), h = lv_obj_get_height(s_scr);
    int band = h / 4;
    for (int b = 0; b < 4; b++) {
        lv_obj_t *o = rect(s_scr, 0, b * band, w, band, C_BLACK);
        lv_obj_set_style_bg_grad_color(o, lv_color_hex(ends[b]), 0);
        lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_HOR, 0);
    }
    caption("GRADIENTS");
    UNLOCK();
    delay_ms(2000);
}

static void test_geometry(void)
{
    static lv_point_precise_t diagonal[2];

    LOCK();
    screen_reset(C_BLACK);
    int w = lv_obj_get_width(s_scr), h = lv_obj_get_height(s_scr);

    for (int x = 0; x < w; x += 40) rect(s_scr, x, 0, 1, h, C_GRAY);
    for (int y = 0; y < h; y += 40) rect(s_scr, 0, y, w, 1, C_GRAY);

    // Border and corner markers verify the full active area and orientation
    frame(s_scr, 0, 0, w, h, C_WHITE);
    rect(s_scr, 1, 1, 20, 20, C_RED);
    rect(s_scr, w - 21, 1, 20, 20, C_GREEN);
    rect(s_scr, 1, h - 21, 20, 20, C_BLUE);
    rect(s_scr, w - 21, h - 21, 20, 20, C_YELLOW);

    diagonal[1].x = w - 1;
    diagonal[1].y = h - 1;
    lv_obj_t *line = lv_line_create(s_scr);
    lv_line_set_points(line, diagonal, 2);
    lv_obj_set_style_line_color(line, C_ORANGE, 0);
    lv_obj_set_style_line_width(line, 1, 0);

    char buf[32];
    snprintf(buf, sizeof(buf), "%dx%d", w, h);
    lv_obj_t *title = text(s_scr, &lv_font_montserrat_28, C_CYAN, "ILI9488");
    lv_obj_t *dims = text(s_scr, &lv_font_montserrat_28, C_YELLOW, buf);
    lv_obj_t *labels[] = {title, dims};
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_bg_color(labels[i], C_BLACK, 0);
        lv_obj_set_style_bg_opa(labels[i], LV_OPA_COVER, 0);
    }
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -20);
    lv_obj_align(dims, LV_ALIGN_CENTER, 0, 20);
    UNLOCK();
    delay_ms(3000);
}

static void test_text(void)
{
    static const char *sample = "The quick brown fox 0123456789";
    LOCK();
    screen_reset(C_BLUE);
    lv_obj_t *l1 = text(s_scr, &lv_font_montserrat_14, C_YELLOW, sample);
    lv_obj_t *l2 = text(s_scr, &lv_font_montserrat_20, C_YELLOW, sample);
    lv_obj_t *l3 = text(s_scr, &lv_font_montserrat_28, C_YELLOW, sample);
    lv_obj_t *l4 = text(s_scr, &lv_font_montserrat_28, C_WHITE,
                        LV_SYMBOL_OK " " LV_SYMBOL_WIFI " " LV_SYMBOL_SETTINGS " " LV_SYMBOL_HOME " " LV_SYMBOL_BATTERY_FULL);
    lv_obj_t *l5 = text(s_scr, &lv_font_montserrat_14, C_WHITE,
                        "Long text is wrapped automatically by the label when a fixed width is set, "
                        "so no manual line breaking is needed.");
    lv_obj_set_width(l5, lv_obj_get_width(s_scr) - 16);
    lv_label_set_long_mode(l5, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(l1, 8, 8);
    lv_obj_set_pos(l2, 8, 32);
    lv_obj_set_pos(l3, 8, 64);
    lv_obj_set_pos(l4, 8, 110);
    lv_obj_set_pos(l5, 8, 160);
    UNLOCK();
    delay_ms(2500);
}

static void bar_anim_cb(void *bar, int32_t v)
{
    lv_bar_set_value((lv_obj_t *)bar, v, LV_ANIM_OFF);
}

static void test_widgets(void)
{
    LOCK();
    screen_reset(lv_color_hex(0x202830));
    caption("WIDGETS");

    lv_obj_t *btn = lv_button_create(s_scr);
    lv_obj_set_pos(btn, 16, 44);
    lv_obj_set_size(btn, 120, 44);
    lv_obj_center(text(btn, &lv_font_montserrat_20, C_WHITE, "Button"));

    lv_obj_t *cb = lv_checkbox_create(s_scr);
    lv_checkbox_set_text(cb, "Checkbox");
    lv_obj_add_state(cb, LV_STATE_CHECKED);
    lv_obj_set_style_text_color(cb, C_WHITE, 0);
    lv_obj_set_pos(cb, 16, 104);

    lv_obj_t *sw = lv_switch_create(s_scr);
    lv_obj_set_pos(sw, 16, 148);
    lv_obj_add_state(sw, LV_STATE_CHECKED);

    lv_obj_t *slider = lv_slider_create(s_scr);
    lv_obj_set_pos(slider, 180, 60);
    lv_obj_set_width(slider, 260);
    lv_slider_set_value(slider, 65, LV_ANIM_OFF);

    lv_obj_t *bar = lv_bar_create(s_scr);
    lv_obj_set_pos(bar, 180, 110);
    lv_obj_set_size(bar, 260, 20);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, bar);
    lv_anim_set_exec_cb(&a, bar_anim_cb);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_duration(&a, 1500);
    lv_anim_set_reverse_duration(&a, 1500);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);

    lv_obj_t *spinner = lv_spinner_create(s_scr);
    lv_obj_set_pos(spinner, 200, 160);
    lv_obj_set_size(spinner, 90, 90);
    lv_obj_t *arc = lv_arc_create(s_scr);
    lv_obj_set_pos(arc, 330, 160);
    lv_obj_set_size(arc, 90, 90);
    lv_arc_set_value(arc, 70);
    UNLOCK();
    delay_ms(4000);
}

// Round lv_scale with a line needle, plus an lv_chart plotting the needle value over time.
// The anim var is the needle, a child of the scale,
// so LVGL drops the animation automatically when the screen is cleaned.
typedef struct {
    int32_t lo, hi;
    uint32_t period_ms;
} gauge_anim_t;

static int32_t s_gauge_value;

static void gauge_needle_cb(void *var, int32_t v)
{
    s_gauge_value = v;
    lv_obj_t *needle = (lv_obj_t *)var;
    lv_obj_t *scale = lv_obj_get_parent(needle);
    lv_scale_set_line_needle_value(scale, needle, -18, v);
    lv_obj_t *label = (lv_obj_t *)lv_obj_get_user_data(scale);
    lv_label_set_text_fmt(label, "%d", (int)v);
}

static void make_gauge(int x, int y, int size, int32_t min, int32_t max, int32_t red_from,
                       lv_color_t needle_color, const char *title, const gauge_anim_t *an)
{
    static lv_style_t st_red_ticks, st_red_arc;
    static bool styles_ready;
    if (!styles_ready) {
        lv_style_init(&st_red_ticks);
        lv_style_set_line_color(&st_red_ticks, C_RED);
        lv_style_set_text_color(&st_red_ticks, C_RED);
        lv_style_init(&st_red_arc);
        lv_style_set_arc_color(&st_red_arc, C_RED);
        styles_ready = true;
    }

    lv_obj_t *scale = lv_scale_create(s_scr);
    lv_obj_set_pos(scale, x, y);
    lv_obj_set_size(scale, size, size);
    lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_range(scale, min, max);
    lv_scale_set_total_tick_count(scale, 21);
    lv_scale_set_major_tick_every(scale, 4);
    lv_scale_set_angle_range(scale, 270);
    lv_scale_set_rotation(scale, 135);
    lv_scale_set_label_show(scale, true);
    lv_obj_set_style_text_font(scale, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(scale, C_WHITE, 0);
    lv_obj_set_style_line_color(scale, C_WHITE, LV_PART_ITEMS);
    lv_obj_set_style_line_color(scale, C_WHITE, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(scale, lv_color_hex(0x606870), LV_PART_MAIN);
    lv_obj_set_style_arc_width(scale, 4, LV_PART_MAIN);
    lv_obj_set_style_length(scale, 6, LV_PART_ITEMS);
    lv_obj_set_style_length(scale, 12, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(scale, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scale, lv_color_hex(0x10161C), 0);
    lv_obj_set_style_radius(scale, LV_RADIUS_CIRCLE, 0);

    lv_scale_section_t *red = lv_scale_add_section(scale);
    lv_scale_set_section_range(scale, red, red_from, max);
    lv_scale_set_section_style_main(scale, red, &st_red_arc);
    lv_scale_set_section_style_indicator(scale, red, &st_red_ticks);
    lv_scale_set_section_style_items(scale, red, &st_red_ticks);

    lv_obj_t *value = text(scale, &lv_font_montserrat_28, needle_color, "0");
    lv_obj_align(value, LV_ALIGN_CENTER, 0, size / 4);
    lv_obj_set_user_data(scale, value);

    lv_obj_t *name = text(scale, &lv_font_montserrat_14, C_GRAY, title);
    lv_obj_align(name, LV_ALIGN_CENTER, 0, size / 4 + 26);

    lv_obj_t *needle = lv_line_create(scale);
    lv_obj_set_style_line_width(needle, 4, 0);
    lv_obj_set_style_line_color(needle, needle_color, 0);
    lv_obj_set_style_line_rounded(needle, true, 0);
    lv_obj_t *hub = rect(scale, 0, 0, 14, 14, needle_color);
    lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_center(hub);
    lv_obj_move_foreground(needle);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, needle);
    lv_anim_set_exec_cb(&a, gauge_needle_cb);
    lv_anim_set_values(&a, an->lo, an->hi);
    lv_anim_set_duration(&a, an->period_ms);
    lv_anim_set_reverse_duration(&a, an->period_ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

#define CHART_POINTS 100
#define CHART_SAMPLE_MS 100
#define X_LABELS 6      // vertical grid lines incl. both edges: 5 intervals of 2 s

static lv_obj_t *s_chart;
static lv_chart_series_t *s_chart_ser;

// Samples the gauge value into the chart; runs inside lv_timer_handler with the LVGL lock held
static void chart_sample_cb(lv_timer_t *t)
{
    (void)t;
    lv_chart_set_next_value(s_chart, s_chart_ser, s_gauge_value);
}

static void test_gauge(void)
{
    static const gauge_anim_t speed = {0, 200, 2500};
    static const int32_t speed_max = 200;

    LOCK();
    screen_reset(lv_color_hex(0x202830));
    caption("LV_SCALE GAUGE + LV_CHART");

    int w = lv_obj_get_width(s_scr), h = lv_obj_get_height(s_scr);
    int margin = 8, top = 24;
    int size = h - top - 2 * margin;
    if (size > w * 45 / 100) size = w * 45 / 100;
    int y = top + (h - top - size) / 2;
    make_gauge(margin, y, size, 0, speed_max, 160, lv_color_hex(0xFF8000), "km/h", &speed);

    int axis_w = 32;                        // room for the min/max labels left of the plot
    int cx = margin + size + margin + axis_w;
    int cw = w - cx - margin - 12;          // leaves room for the last x label to hang past the plot
    int ch = size - 44;                     // leaves room for the x labels below the plot
    int cy = top + (h - top - ch) / 2 + 6;

    lv_obj_t *title = text(s_scr, &lv_font_montserrat_14, C_GRAY, "Speed history");
    lv_obj_set_pos(title, cx, cy - 22);

    s_chart = lv_chart_create(s_scr);
    lv_obj_set_pos(s_chart, cx, cy);
    lv_obj_set_size(s_chart, cw, ch);
    lv_obj_set_style_pad_all(s_chart, 0, 0);    // plot spans the full object so labels line up with grid lines
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, CHART_POINTS);
    lv_chart_set_axis_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, speed_max);
    lv_chart_set_div_line_count(s_chart, 5, X_LABELS);
    lv_chart_set_update_mode(s_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_bg_color(s_chart, lv_color_hex(0x10161C), 0);
    lv_obj_set_style_border_color(s_chart, lv_color_hex(0x606870), 0);
    lv_obj_set_style_line_color(s_chart, lv_color_hex(0x384048), LV_PART_MAIN);
    lv_obj_set_style_size(s_chart, 0, 0, LV_PART_INDICATOR);     // no point markers
    lv_obj_set_style_line_width(s_chart, 3, LV_PART_ITEMS);
    s_chart_ser = lv_chart_add_series(s_chart, lv_color_hex(0xFF8000), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(s_chart, s_chart_ser, LV_CHART_POINT_NONE);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", (int)speed_max);
    lv_obj_t *l_max = text(s_scr, &lv_font_montserrat_14, C_WHITE, buf);
    lv_obj_set_pos(l_max, cx - axis_w, cy);
    lv_obj_t *l_min = text(s_scr, &lv_font_montserrat_14, C_WHITE, "0");
    lv_obj_set_pos(l_min, cx - axis_w, cy + ch - 18);
    lv_obj_t *y_unit = text(s_scr, &lv_font_montserrat_14, C_GRAY, "km/h");
    // Rotated 270 deg around its centre, which is placed midway between the 0 and max labels
    lv_obj_set_size(y_unit, 40, 18);
    lv_obj_set_style_text_align(y_unit, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_transform_pivot_x(y_unit, 20, 0);
    lv_obj_set_style_transform_pivot_y(y_unit, 9, 0);
    lv_obj_set_style_transform_rotation(y_unit, 2700, 0);
    lv_obj_set_pos(y_unit, cx - axis_w / 2 - 20, cy + ch / 2 - 9);

    // X axis: seconds before "now", one label per vertical grid line
    int span_s = CHART_POINTS * CHART_SAMPLE_MS / 1000;
    for (int i = 0; i < X_LABELS; i++) {
        snprintf(buf, sizeof(buf), "%d", -span_s + i * span_s / (X_LABELS - 1));
        lv_obj_t *lx = text(s_scr, &lv_font_montserrat_14, C_WHITE, buf);
        lv_obj_set_width(lx, 36);
        lv_obj_set_style_text_align(lx, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(lx, cx + i * (cw - 1) / (X_LABELS - 1) - 18, cy + ch + 4);
    }
    lv_obj_t *x_unit = text(s_scr, &lv_font_montserrat_14, C_GRAY, "Time (s)");
    lv_obj_set_width(x_unit, cw);
    lv_obj_set_style_text_align(x_unit, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(x_unit, cx, cy + ch + 22);

    lv_timer_t *timer = lv_timer_create(chart_sample_cb, CHART_SAMPLE_MS, NULL);
    UNLOCK();

    delay_ms(16000);

    LOCK();
    lv_timer_delete(timer);
    UNLOCK();
}

static void test_speed(void)
{
    static const uint32_t c[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF};
    const int frames = 8;

    LOCK();
    screen_reset(C_BLACK);
    lv_refr_now(NULL);
    int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < frames; i++) {
        lv_obj_set_style_bg_color(s_scr, lv_color_hex(c[i & 3]), 0);
        lv_refr_now(NULL);
    }
    int64_t us = esp_timer_get_time() - t0;
    float fps = frames * 1e6f / (float)us;

    char buf[32];
    snprintf(buf, sizeof(buf), "FILL: %d FPS", (int)(fps + 0.5f));
    screen_reset(C_BLACK);
    lv_obj_t *l = text(s_scr, &lv_font_montserrat_28, C_GREEN, buf);
    lv_obj_set_pos(l, 20, 20);
    UNLOCK();
    ESP_LOGI(TAG, "Full-screen refresh: %.1f fps (%.1f ms/frame)", fps, us / 1000.0f / frames);
    delay_ms(2500);
}

#ifdef CONFIG_XPT2046_ENABLE
#define CAL_TIMEOUT_MS   15000
#define PAINT_IDLE_MS    30000
#define HEADER_H         24
#define BTN_EXIT_W       64
#define BTN_CLEAR_W      76
#define PEN_COUNT        6

static lv_obj_t *s_canvas;
static lv_draw_buf_t *s_canvas_buf;
static lv_obj_t *s_hdr_label;
static volatile bool s_exit_requested;
static int s_pen = 1;           // palette index 1..PEN_COUNT, index 0 is the black background

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

// LVGL is not running its own task during calibration, so it is pumped by hand while waiting.
static void pump_ms(int ms)
{
    lv_timer_handler();
    delay_ms(ms);
}

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
        pump_ms(10);
    }
    int released = 0;
    while (released < 5) {
        released = xpt2046_read_raw(NULL, NULL, NULL) ? 0 : released + 1;
        pump_ms(10);
    }
    if (*rx == 0 && *ry == 0) {
        // IRQ says touched but the ADC returns nothing: the SPI data path to the controller is broken
        ESP_LOGE(TAG, "Touch detected but ADC readings are all zero - check T_DO (MISO), T_CS, T_CLK and T_DIN wiring");
        return false;
    }
    return true;
}

static void draw_cross(int x, int y, lv_color_t c)
{
    rect(s_scr, x - 12, y, 25, 1, c);
    rect(s_scr, x, y - 12, 1, 25, c);
    frame(s_scr, x - 5, y - 5, 11, 11, c);
}

static bool touch_calibrate(void)
{
    int w = ili9488_width(), h = ili9488_height();
    const int m = 24;
    int scr[3][2] = {{m, m}, {w - 1 - m, m}, {m, h - 1 - m}};
    int raw[3][2];
    bool ok = true;

    for (int i = 0; i < 3 && ok; i++) {
        screen_reset(C_BLACK);
        lv_obj_center(text(s_scr, &lv_font_montserrat_20, C_WHITE, "TOUCH THE CROSS"));
        draw_cross(scr[i][0], scr[i][1], C_RED);
        pump_ms(10);
        ok = wait_touch_raw(&raw[i][0], &raw[i][1], CAL_TIMEOUT_MS);
        if (ok) ESP_LOGI(TAG, "Cal point %d: raw %d,%d", i, raw[i][0], raw[i][1]);
    }

    xpt2046_cal_t cal;
    if (!ok || !xpt2046_compute_calibration(raw, scr, &cal)) {
        ESP_LOGE(TAG, "Calibration failed - skipping touch test");
        screen_reset(C_BLACK);
        lv_obj_t *l1 = text(s_scr, &lv_font_montserrat_28, C_RED, "TOUCH CAL FAILED");
        lv_obj_t *l2 = text(s_scr, &lv_font_montserrat_20, C_WHITE, "CHECK T_CLK T_DIN T_DO T_CS");
        lv_obj_set_pos(l1, 10, 10);
        lv_obj_set_pos(l2, 10, 50);
        for (int i = 0; i < 400; i++) pump_ms(10);
        return false;
    }
    ESP_LOGI(TAG, "Calibration: swap=%d x=%.4f*a%+.1f y=%.4f*b%+.1f", cal.swap_xy, cal.sx, cal.ox, cal.sy, cal.oy);
    xpt2046_set_calibration(&cal);
    return true;
}

static void canvas_event_cb(lv_event_t *e)
{
    static int last_x = -1, last_y = -1;
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    if (code == LV_EVENT_RELEASED) {
        lv_label_set_text(s_hdr_label, "TOUCH TO DRAW");
        return;
    }
    if (code == LV_EVENT_PRESSED) {
        s_pen = s_pen % PEN_COUNT + 1;
    }

    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_area_t a;
    lv_obj_get_coords(s_canvas, &a);
    int cx = p.x - a.x1, cy = p.y - a.y1;
    int cw = lv_obj_get_width(s_canvas), ch = lv_obj_get_height(s_canvas);
    lv_display_enable_invalidation(NULL, false);
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            if (cx + dx >= 0 && cx + dx < cw && cy + dy >= 0 && cy + dy < ch) {
                lv_canvas_set_px(s_canvas, cx + dx, cy + dy, lv_color_hex(s_pen), LV_OPA_COVER);
            }
        }
    }
    lv_display_enable_invalidation(NULL, true);
    lv_area_t dirty = {p.x - 2, p.y - 2, p.x + 2, p.y + 2};    // invalidation takes screen coordinates
    lv_obj_invalidate_area(s_canvas, &dirty);
    lv_label_set_text_fmt(s_hdr_label, "X%d Y%d", (int)p.x, (int)p.y);
    if (p.x != last_x || p.y != last_y) {
        ESP_LOGI(TAG, "touch %d,%d", (int)p.x, (int)p.y);
        last_x = p.x;
        last_y = p.y;
    }
}

static void clear_cb(lv_event_t *e)
{
    lv_draw_buf_clear(s_canvas_buf, NULL);
    lv_obj_invalidate(s_canvas);
}

static void exit_cb(lv_event_t *e)
{
    s_exit_requested = true;
}

static lv_obj_t *header_button(int x, int w, lv_color_t c, const char *str, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(s_scr);
    lv_obj_set_pos(b, x, 1);
    lv_obj_set_size(b, w, HEADER_H - 2);
    lv_obj_set_style_bg_color(b, c, 0);
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_center(text(b, &lv_font_montserrat_14, C_WHITE, str));
    return b;
}

static bool paint_create(void)
{
    static const uint32_t pens[PEN_COUNT] = {0xFFFFFF, 0x00FF00, 0x00FFFF, 0xFFFF00, 0xFF8000, 0xFF00FF};
    int w = ili9488_width(), h = ili9488_height();
    int exit_x = w - BTN_EXIT_W - 4;
    int clear_x = exit_x - BTN_CLEAR_W - 8;
    int canvas_y = HEADER_H + 1;

    // 4 bit indexed canvas keeps the paint surface small enough for internal RAM
    s_canvas_buf = lv_draw_buf_create(w, h - canvas_y, LV_COLOR_FORMAT_I4, LV_STRIDE_AUTO);
    if (!s_canvas_buf) {
        ESP_LOGE(TAG, "Not enough memory for the paint canvas");
        return false;
    }
    lv_draw_buf_clear(s_canvas_buf, NULL);

    screen_reset(C_BLACK);
    s_canvas = lv_canvas_create(s_scr);
    lv_canvas_set_draw_buf(s_canvas, s_canvas_buf);
    lv_canvas_set_palette(s_canvas, 0, lv_color32_make(0, 0, 0, 255));
    for (int i = 0; i < PEN_COUNT; i++) {
        lv_color32_t c = lv_color32_make((pens[i] >> 16) & 0xFF, (pens[i] >> 8) & 0xFF, pens[i] & 0xFF, 255);
        lv_canvas_set_palette(s_canvas, i + 1, c);
    }
    lv_obj_set_pos(s_canvas, 0, canvas_y);
    lv_obj_set_clickable(s_canvas, true);
    lv_obj_add_event_cb(s_canvas, canvas_event_cb, LV_EVENT_ALL, NULL);

    rect(s_scr, 0, HEADER_H, w, 1, C_GRAY);
    s_hdr_label = text(s_scr, &lv_font_montserrat_20, C_WHITE, "TOUCH TO DRAW");
    lv_obj_set_pos(s_hdr_label, 4, 1);
    header_button(clear_x, BTN_CLEAR_W, C_BLUE, "CLEAR", clear_cb);
    header_button(exit_x, BTN_EXIT_W, C_RED, "EXIT", exit_cb);
    return true;
}

// Touch paint: draws dots under the finger. CLEAR wipes the canvas, EXIT (or 30 s without touch) ends the test.
static void test_touch(void)
{
    s_exit_requested = false;
    LOCK();
    bool ok = paint_create();
    lv_display_trigger_activity(NULL);
    UNLOCK();

    while (ok && !s_exit_requested) {
        delay_ms(50);
        LOCK();
        bool idle = lv_display_get_inactive_time(NULL) > PAINT_IDLE_MS;
        UNLOCK();
        if (idle) {
            ESP_LOGW(TAG, "No touch for %d s, leaving touch test", PAINT_IDLE_MS / 1000);
            break;
        }
    }

    LOCK();
    screen_reset(C_BLACK);      // deletes the canvas before its buffer is released
    UNLOCK();
    if (s_canvas_buf) {
        lv_draw_buf_destroy(s_canvas_buf);
        s_canvas_buf = NULL;
    }
}
#endif

static void ui_task(void *arg)
{
    s_scr = lv_screen_active();
    lv_obj_set_scrollable(s_scr, false);
    lv_obj_set_style_pad_all(s_scr, 0, 0);

#ifdef CONFIG_XPT2046_ENABLE
    bool touch_ok = touch_calibrate();
#endif
    ESP_ERROR_CHECK(lvgl_port_start());

#ifdef CONFIG_XPT2046_ENABLE
    if (touch_ok) {
        test_touch();
    }
#endif

    while (1) {
        //test_solid_fills();
        //test_color_bars();
        //test_gradients();
        //test_geometry();
        //test_text();
        //test_widgets();
        test_gauge();
        //test_speed();
    }
}

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
#endif

    ESP_ERROR_CHECK(lvgl_port_init());
    if (xTaskCreate(ui_task, "ui", UI_TASK_STACK_BYTES, NULL, 1, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UI task");
    }
}
