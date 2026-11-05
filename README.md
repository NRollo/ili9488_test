# ILI9488 480x320 SPI TFT test (ESP32-WROOM, ESP-IDF 6.1)

Self-contained test of an ILI9488 TFT over SPI. The driver (`main/ili9488.c`) sits on top of `esp_lcd` SPI panel IO, so no external managed components are needed.

Tests, looping forever: solid fills, colour bars, RGB/gray gradients, grid + corner markers (orientation check), text at several scales, and a full-screen fill speed measurement (printed on screen and to the log).

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

At startup: 1) tap the three red crosses (top-left, top-right, bottom-left; axis swap/inversion is detected automatically, so any rotation works), 2) paint with your finger. The header shows the touch coordinates; **CLEAR** wipes the canvas and **EXIT** (or 30 s without touch) continues to the display tests. Raw calibration values are logged. If calibration fails (no touch, or the controller returns only zeros) the screen shows "TOUCH CAL FAILED" and the touch test is skipped; check the T_CLK/T_DIN/T_DO/T_CS wiring.

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

- ILI9488 over SPI only accepts 18-bit colour, so 3 bytes/pixel are sent (RGB565 API colours are expanded). A full frame is ~460 KB, hence ~10-15 fps for full-screen fills at 40 MHz.
- Garbage/noise: lower the SPI clock (e.g. 20-26 MHz). Wrong colours/inverted: toggle BGR (0x08) in the `madctl` table in `ili9488.c`. Mirrored image: adjust the rotation value.
- Written against the ESP-IDF 6.x component layout (`esp_driver_spi`, `esp_driver_gpio`, `esp_lcd`); I could not compile it in this environment, so verify with your ESP-IDF 6.1 install.
