#include <stdlib.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "xpt2046.h"

#define SAMPLES 5

// Control bytes: start bit, channel select, 12-bit, differential, power-down between conversions
#define CMD_Y  0x90
#define CMD_X  0xD0
#define CMD_Z1 0xB0
#define CMD_Z2 0xC0

static const char *TAG = "xpt2046";

static spi_device_handle_t s_dev;
static xpt2046_config_t s_cfg;
static xpt2046_cal_t s_cal = {.swap_xy = false, .sx = 1.0f, .sy = 1.0f};

static int xfer(uint8_t cmd)
{
    spi_transaction_t t = {
        .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
        .length = 24,
        .tx_data = {cmd, 0, 0},
    };
    esp_err_t err = spi_device_polling_transmit(s_dev, &t);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI transfer failed: %s", esp_err_to_name(err));
        return 0;
    }
    return ((((int)t.rx_data[1] << 8) | t.rx_data[2]) >> 3) & 0x0FFF;
}

static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

static int median_of(uint8_t cmd)
{
    int v[SAMPLES];
    for (int i = 0; i < SAMPLES; i++) {
        v[i] = xfer(cmd);
    }
    qsort(v, SAMPLES, sizeof(v[0]), cmp_int);
    return v[SAMPLES / 2];
}

esp_err_t xpt2046_init(const xpt2046_config_t *cfg)
{
    s_cfg = *cfg;

    if (cfg->pin_irq >= 0) {
        gpio_config_t irq = {
            .pin_bit_mask = 1ULL << cfg->pin_irq,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&irq));
    }

    spi_device_interface_config_t dev = {
        .clock_speed_hz = cfg->clock_hz,
        .mode = 0,
        .spics_io_num = cfg->pin_cs,
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(cfg->spi_host, &dev, &s_dev));
    ESP_LOGI(TAG, "Initialised, CS=%d IRQ=%d, %d kHz", cfg->pin_cs, cfg->pin_irq, cfg->clock_hz / 1000);
    return ESP_OK;
}

bool xpt2046_read_raw(int *raw_x, int *raw_y, int *pressure)
{
    if (s_cfg.pin_irq >= 0 && gpio_get_level(s_cfg.pin_irq) != 0) {
        return false;
    }

    int z = xfer(CMD_Z1) + 4095 - xfer(CMD_Z2);
    if (z < s_cfg.z_threshold) {
        return false;
    }

    int x = median_of(CMD_X);
    int y = median_of(CMD_Y);
    if (raw_x) *raw_x = x;
    if (raw_y) *raw_y = y;
    if (pressure) *pressure = z;
    return true;
}

void xpt2046_set_calibration(const xpt2046_cal_t *cal) { s_cal = *cal; }

void xpt2046_log_status(void)
{
    int z1 = xfer(CMD_Z1), z2 = xfer(CMD_Z2);
    ESP_LOGI(TAG, "idle: z1=%d z2=%d z=%d (threshold %d) irq=%d raw x=%d y=%d miso_level=%d", z1, z2, z1 + 4095 - z2,
             s_cfg.z_threshold, s_cfg.pin_irq >= 0 ? gpio_get_level(s_cfg.pin_irq) : -1, xfer(CMD_X), xfer(CMD_Y),
             gpio_get_level(CONFIG_XPT2046_PIN_MISO));
}

bool xpt2046_compute_calibration(const int raw[3][2], const int screen[3][2], xpt2046_cal_t *out)
{
    // Moving along the screen's X axis (TL -> TR) mostly changes one raw channel; that one is screen X.
    int dx_x = raw[1][0] - raw[0][0];
    int dx_y = raw[1][1] - raw[0][1];
    bool swap = abs(dx_y) > abs(dx_x);
    int ax = swap ? 1 : 0;
    int ay = swap ? 0 : 1;

    int da = raw[1][ax] - raw[0][ax];
    int db = raw[2][ay] - raw[0][ay];
    if (abs(da) < 200 || abs(db) < 200) {
        return false;
    }

    out->swap_xy = swap;
    out->sx = (float)(screen[1][0] - screen[0][0]) / (float)da;
    out->ox = screen[0][0] - out->sx * raw[0][ax];
    out->sy = (float)(screen[2][1] - screen[0][1]) / (float)db;
    out->oy = screen[0][1] - out->sy * raw[0][ay];
    return true;
}

bool xpt2046_read(int width, int height, int *x, int *y, int *pressure)
{
    int rx, ry;
    if (!xpt2046_read_raw(&rx, &ry, pressure)) {
        return false;
    }
    int a = s_cal.swap_xy ? ry : rx;
    int b = s_cal.swap_xy ? rx : ry;
    int px = (int)(s_cal.sx * a + s_cal.ox + 0.5f);
    int py = (int)(s_cal.sy * b + s_cal.oy + 0.5f);
    if (px < 0) px = 0;
    if (px >= width) px = width - 1;
    if (py < 0) py = 0;
    if (py >= height) py = height - 1;
    *x = px;
    *y = py;
    return true;
}
