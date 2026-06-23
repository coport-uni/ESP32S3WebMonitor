idf_component_register(
    SRCS
        "main.c"
        "ui.c"
        "buttons_check.c"
        "network.c"
        "hotplate_client.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_http_client
        esp-tls
        driver
        esp_timer
)
