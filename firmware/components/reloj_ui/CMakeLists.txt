file(GLOB FUENTES ${CMAKE_CURRENT_SOURCE_DIR}/fuentes/*.c)
idf_component_register(SRCS "ui.c" ${FUENTES}
                       INCLUDE_DIRS "include"
                       REQUIRES lvgl__lvgl reloj_ble esp_app_format)
