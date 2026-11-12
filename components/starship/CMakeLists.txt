idf_component_register(SRCS "../../main/ship_runtime.cpp" "../../main/ship_alpha_test.cpp"
    INCLUDE_DIRS "../../main" "../../native" "../../assets/metadata"
    PRIV_REQUIRES tgx esp_psram esp_partition spi_flash mbedtls sf64_rom esp_timer)
if(CONFIG_SF64_STARSHIP_RUNTIME)
    add_prebuilt_library(starship_runtime "${CMAKE_CURRENT_LIST_DIR}/../../build-starship/runtime/libstarship-runtime.a")
    target_link_libraries(${COMPONENT_LIB} PRIVATE starship_runtime)
endif()
