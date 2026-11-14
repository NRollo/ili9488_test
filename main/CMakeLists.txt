idf_component_register(SRCS "main.c" "lvgl_port.c" "ili9488.c" "xpt2046.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_lcd esp_driver_spi esp_driver_gpio esp_timer)
