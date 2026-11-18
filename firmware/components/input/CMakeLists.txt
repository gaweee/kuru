idf_component_register(SRCS "input.c" "encoder.c" "touch.c" INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "."
                       REQUIRES driver esp_timer freertos board)
