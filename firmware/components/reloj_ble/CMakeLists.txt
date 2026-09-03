idf_component_register(SRCS "ble.c" "cts.c" "cts_parse.c"
                       INCLUDE_DIRS "include"
                       REQUIRES bt nvs_flash)
