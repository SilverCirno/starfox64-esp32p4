idf_component_register(SRCS "../../main/rom_assets.cpp" "../../main/mio0.cpp"
    INCLUDE_DIRS "../../main"
    REQUIRES esp_partition spi_flash mbedtls esp_psram)
