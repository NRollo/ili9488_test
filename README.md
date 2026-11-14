# ILI9488 480x320 SPI TFT test with LVGL (ESP32-WROOM, ESP-IDF 6.1)

Self-contained test of an ILI9488 TFT over SPI using [LVGL](https://lvgl.io) 9.x. The display driver (`main/ili9488.c`) sits on top of `esp_lcd` SPI panel IO and only provides init and bitmap transfer; `main/lvgl_port.c` connects it (and the XPT2046 touch) to LVGL. LVGL is pulled in through the ESP-IDF component manager (`main/idf_component.yml`) on the first build, so internet access is needed once.

Tests, looping forever (all built from LVGL objects): solid fills, colour bars, RGB/gray gradients, grid + corner markers (orientation check), fonts/symbols/wrapped text, a widget page (button, checkbox, switch, slider, animated bar, spinner, arc), and a full-screen refresh speed measurement (printed on screen and to the log).

## Default wiring (VSPI)

| Display | ESP32 GPIO |
|---------|-----------|
| SDI/MOSI | 23 |
| SCK | 18 |
| CS | 5 |
| DC/RS | 21 |
| RESET | 22 |
| LED/BL | 32 (or 3V3) |
| VCC/GND | 3V3 / GND |

SDO/MISO is only needed for touch. Pins, SPI clock (default 40 MHz) and rotation are configurable under `pio run -t menuconfig` (or `idf.py menuconfig`) -> *ILI9488 Display Configuration*.

## Touch test (XPT2046)

Touch shares MOSI/SCLK with the display. Extra wiring (all configurable under *XPT2046 Touch Configuration*, or disable it with `CONFIG_XPT2046_ENABLE=n`):

| Display | ESP32 GPIO |
|---------|-----------|
| T_DO (MISO) | 19 |
| T_CS | 4 |
| T_IRQ | 27 (set to -1 if not wired) |
| T_DIN / T_CLK | same GPIOs as MOSI / SCLK (separate wires are required; the module does not link them internally) |

Leave the display's own SDO/MISO pin unconnected; only T_DO goes to GPIO 19, otherwise the two outputs can conflict.

At startup: 1) tap the three red crosses (top-left, top-right, bottom-left; axis swap/inversion is detected automatically, so any rotation works), 2) paint with your finger on an LVGL canvas (the pen colour changes on every new touch). The header shows the touch coordinates; **CLEAR** wipes the canvas and **EXIT** (or 30 s without touch) continues to the display tests. After calibration the touch controller is registered with LVGL as a pointer input device. Raw calibration values are logged. If calibration fails (no touch, or the controller returns only zeros) the screen shows "TOUCH CAL FAILED" and the touch test is skipped; check the T_CLK/T_DIN/T_DO/T_CS wiring.

## Build

PlatformIO (VS Code): open the folder and use the PlatformIO toolbar, or from a terminal:

```
pio run                # build
pio run -t upload      # flash
pio device monitor     # serial monitor
pio run -t menuconfig  # pins, SPI clock, rotation
```

`platformio.ini` sets `src_dir = main`, so the sources stay in `main/` as an ESP-IDF component. Plain `idf.py build flash monitor` still works too.

## Notes

- ILI9488 over SPI only accepts 18-bit colour, so 3 bytes/pixel are sent (LVGL renders RGB565 into a 480x40 partial buffer, the driver expands it). A full frame is ~460 KB, hence ~10-15 fps for full-screen refreshes at 40 MHz.
- LVGL uses the C heap (`CONFIG_LV_USE_CLIB_MALLOC`) and the Montserrat 14/20/28 fonts (see `sdkconfig.defaults`). All LVGL calls and SPI access go through `lvgl_port_lock()` once the LVGL task is running.
- Garbage/noise: lower the SPI clock (e.g. 20-26 MHz). Wrong colours/inverted: toggle BGR (0x08) in the `madctl` table in `ili9488.c`. Mirrored image: adjust the rotation value.
- Written against the ESP-IDF 6.x component layout (`esp_driver_spi`, `esp_driver_gpio`, `esp_lcd`);
