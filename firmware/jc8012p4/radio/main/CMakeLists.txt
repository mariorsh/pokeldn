idf_component_register(
    SRCS "../../../esp32/main/radio.c" "../../../esp32/main/ble.c" "wire_ble.c" "wire_sdio.c" "board.c" "ota_guard.c" "../../common/packet.c"
    INCLUDE_DIRS "." "../../common" "../../../esp32/main"
    PRIV_REQUIRES esp_app_format esp_wifi wpa_supplicant esp_driver_gpio esp_driver_sdio esp_timer nvs_flash esp_event app_update bootloader_support bt)
if(NOT IDF_TARGET STREQUAL "esp32c6")
    message(FATAL_ERROR "The JC8012 radio firmware requires esp32c6")
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE POKELDN_JC8012_C6=1 POKELDN_BLE=1)
