idf_component_register(SRCS "rtc_pcf85063.c" "pmic.cpp" "pantalla.c" "boton_boot.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c esp_driver_gpio esp_lcd espressif__esp_lcd_touch lvgl__lvgl espressif__esp_lvgl_port
                                waveshare__esp32_c6_touch_amoled_2_06
                       PRIV_REQUIRES cube32esp__xpowerslib)
