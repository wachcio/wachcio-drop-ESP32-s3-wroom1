idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES wifi_provisioning protocomm esp_netif esp_wifi nvs_flash esp_event esp_http_server driver
)
