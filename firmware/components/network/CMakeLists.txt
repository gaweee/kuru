# wifi_creds.c, mock_ha.c, mock_link.c are plain C (host-tested); wifi_link.c and kuru_console.c need ESP-IDF.
idf_component_register(SRCS "mock_ha.c" "mock_link.c" "wifi_creds.c" "wifi_link.c" "kuru_console.c"
                       INCLUDE_DIRS "include"
                       REQUIRES controller
                       PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash console esp_timer)
