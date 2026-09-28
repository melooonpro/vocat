idf_component_register(
    SRCS "hello_vocat.c" "clock_ui.c"
    INCLUDE_DIRS "."
    REQUIRES esp_vocat_v1_1 esp_event esp_netif esp_wifi nvs_flash
    EMBED_FILES "clock_digits.bin"
)
