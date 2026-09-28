idf_component_register(
    SRCS "esp_vocat_v1_1.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private"
    REQUIRES esp_driver_gpio esp_driver_spi esp_lcd esp_psram spi_flash
)
