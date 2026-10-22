cmake_minimum_required(VERSION 3.22)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
set(PROJECT_VER "1.6.3")
project(pokechanger_panel)
# ESSL 1.1 predates ESP-IDF's driver component split. Declare its actual
# dependencies here without changing downloaded registry sources.
idf_component_get_property(essl_lib espressif__esp_serial_slave_link COMPONENT_LIB)
target_link_libraries(${essl_lib} PRIVATE idf::esp_driver_sdmmc idf::esp_driver_spi)
# ESSL also relied on a transitive heap header removed in IDF 6.
target_compile_options(${essl_lib} PRIVATE -include esp_heap_caps.h)
