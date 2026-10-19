idf_component_register(
    SRCS "bridge.c" "panel.c" "events.c" "dashboard.c" "../common/packet.c"
         "../../esp32/main/wire.c" "../../esp32/main/scene.c" "../../esp32/main/screen.c"
    INCLUDE_DIRS "." "../common" "../../esp32/main"
    PRIV_REQUIRES esp_driver_uart esp_driver_gpio esp_driver_ledc esp_driver_sdmmc esp_driver_spi sdmmc esp_lcd esp_hw_support esp_timer)
if(NOT IDF_TARGET STREQUAL "esp32p4")
    message(FATAL_ERROR "The JC8012 panel firmware requires esp32p4")
endif()
