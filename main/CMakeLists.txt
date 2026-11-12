set(NATIVE_OS_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../native/n64_os.c")
idf_component_register(SRCS "main.cpp" "display_st7102.c" "input.c" "input_touch.c" "f3dex.cpp"
                           "audio_output.c"
                           "native_host.cpp"
                           "${NATIVE_OS_SOURCE}"
    INCLUDE_DIRS "."
    PRIV_REQUIRES tgx esp_lcd_st7102 esp_lcd esp_driver_gpio esp_hw_support
                  esp_timer esp_psram esp_partition spi_flash esp_driver_i2c mbedtls nvs_flash starship sf64_rom)
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../third_party/sf64/include"
    "${CMAKE_CURRENT_LIST_DIR}/../native")
set_source_files_properties("${NATIVE_OS_SOURCE}" PROPERTIES COMPILE_DEFINITIONS
    "_LANGUAGE_C;LANGUAGE_C;_MIPS_SZLONG=32;_MIPS_SZINT=32;_MIPS_SZPTR=32;F3DEX_GBI;VERSION_US=1;NON_MATCHING;AVOID_UB;NDEBUG")
set_property(SOURCE "${NATIVE_OS_SOURCE}" APPEND PROPERTY COMPILE_OPTIONS "-Wno-error=unknown-pragmas")
