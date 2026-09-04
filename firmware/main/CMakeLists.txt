idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES reloj_hw reloj_ble reloj_ui reloj_notif reloj_red reloj_voz reloj_andia nvs_flash)
