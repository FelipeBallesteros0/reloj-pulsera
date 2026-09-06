idf_component_register(SRCS "bateria_registro.c" "energia_politica.c" "energia.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_timer reloj_red
                       PRIV_REQUIRES esp_pm)
