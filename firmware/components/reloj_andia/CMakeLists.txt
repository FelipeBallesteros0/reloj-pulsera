idf_component_register(SRCS "andia.c"
                       INCLUDE_DIRS "include"
                       REQUIRES espressif__esp_websocket_client json reloj_voz esp_timer esp_wifi
                       PRIV_REQUIRES reloj_red)
