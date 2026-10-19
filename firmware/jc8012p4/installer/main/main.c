#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_hosted_api_types.h"
#include "esp_hosted_ota.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if CONFIG_POKECHANGER_C6_INSTALLER_WRITE
#include "psa/crypto.h"
#include "esp_image_format.h"
#include "driver/uart.h"
extern const uint8_t c6_start[] asm("_binary_c6_radio_start");
extern const uint8_t c6_end[] asm("_binary_c6_radio_end");

static esp_err_t install(const esp_hosted_coprocessor_fwver_t *version)
{
    const size_t size = c6_end - c6_start;
    const esp_image_header_t *header = (const esp_image_header_t *)c6_start;
    const esp_app_desc_t *app = (const esp_app_desc_t *)(c6_start + sizeof(*header) + sizeof(esp_image_segment_header_t));
    if (size < 512 || header->magic != ESP_IMAGE_HEADER_MAGIC || header->chip_id != 13 ||
        app->magic_word != ESP_APP_DESC_MAGIC_WORD || strcmp(app->project_name, "pokechanger_radio"))
        return ESP_ERR_INVALID_ARG;
    uint8_t hash[32]; char hex[65];
    size_t hash_size = 0;
    if (psa_crypto_init() != PSA_SUCCESS ||
        psa_hash_compute(PSA_ALG_SHA_256, c6_start, size, hash, sizeof(hash), &hash_size) != PSA_SUCCESS ||
        hash_size != sizeof(hash)) return ESP_FAIL;
    for (int i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", hash[i]);
    const uart_config_t uart = {
        .baud_rate = 115200, .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(uart_driver_install(UART_NUM_0, 2048, 0, 0, NULL, 0), "install", "UART input");
    ESP_RETURN_ON_ERROR(uart_param_config(UART_NUM_0, &uart), "install", "UART configuration");
    ESP_RETURN_ON_ERROR(uart_set_pin(UART_NUM_0, 37, 38, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), "install", "UART pins");
    ESP_RETURN_ON_ERROR(uart_set_rx_full_threshold(UART_NUM_0, 32), "install", "UART receive threshold");
    ESP_RETURN_ON_ERROR(uart_set_rx_timeout(UART_NUM_0, 2), "install", "UART receive timeout");
    ESP_RETURN_ON_ERROR(uart_flush_input(UART_NUM_0), "install", "UART startup input");
    ESP_LOGI("install", "C6 image bytes=%u version=%s sha256=%s", (unsigned)size, app->version, hex);
    ESP_LOGI("install", "READY_FOR_INSTALL (120 second serial arming window)");
    char command[96]; size_t used = 0;
    bool armed = false;
    const int64_t until = esp_timer_get_time() + 120000000;
    while (esp_timer_get_time() < until) {
        uint8_t byte;
        int ch = uart_read_bytes(UART_NUM_0, &byte, 1, pdMS_TO_TICKS(10)) == 1 ? byte : -1;
        if (ch == '\n') {
            command[used] = 0;
            ESP_LOGI("install", "Received arming line (%u bytes)", (unsigned)used);
            if (used == 72 && !memcmp(command, "INSTALL ", 8) && !strcmp(command + 8, hex)) { armed = true; break; }
            used = 0;
        } else if (ch >= 0 && ch != '\r') {
            if (used < sizeof(command) - 1) command[used++] = ch;
            else used = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!armed) return ESP_ERR_TIMEOUT;
    ESP_LOGI("install", "INSTALL_ARMED; requesting OTA_BEGIN");
    esp_err_t err = esp_hosted_slave_ota_begin();
    ESP_LOGI("install", "OTA_BEGIN=%s", esp_err_to_name(err));
    if (err != ESP_OK) return err;
    for (size_t at = 0; at < size; ) {
        size_t n = size - at > 1024 ? 1024 : size - at;
        err = esp_hosted_slave_ota_write(c6_start + at, n);
        if (err != ESP_OK) { ESP_LOGE("install", "write at %u failed: %s", (unsigned)at, esp_err_to_name(err)); return err; }
        at += n;
        if (at % 65536 == 0 || at == size) ESP_LOGI("install", "OTA progress %u/%u", (unsigned)at, (unsigned)size);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    err = esp_hosted_slave_ota_end();
    ESP_LOGI("install", "OTA_END=%s", esp_err_to_name(err));
    if (err != ESP_OK) return err;
    /* Old ESP-Hosted activates/reboots from OTA_END; newer versions use ACTIVATE. */
    if (version->major1 > 2 || (version->major1 == 2 && version->minor1 >= 6)) {
        err = esp_hosted_slave_ota_activate();
        if (err != ESP_OK) return err;
    }
    ESP_LOGI("install", "OTA_ACTIVATED: restore P4 monitor, verify HELLO/BENCH, then confirm C6 trial");
    return ESP_OK;
}
#endif

void app_main(void)
{
    ESP_LOGI("probe", "ESP-Hosted C6 migration helper");
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_hosted_init());
    esp_err_t err = esp_hosted_connect_to_slave();
    ESP_LOGI("probe", "connect=%s", esp_err_to_name(err));
    if (err != ESP_OK) { ESP_LOGE("probe", "INSTALLER_STOPPED"); return; }
    esp_hosted_coprocessor_fwver_t version = {0};
    err = esp_hosted_get_coprocessor_fwversion(&version);
    ESP_LOGI("probe", "version_result=%s C6=%lu.%lu.%lu", esp_err_to_name(err),
             (unsigned long)version.major1, (unsigned long)version.minor1, (unsigned long)version.patch1);
#if CONFIG_POKECHANGER_C6_INSTALLER_WRITE
    if (err == ESP_OK) err = install(&version);
    if (err != ESP_OK) ESP_LOGE("install", "INSTALLER_STOPPED error=%s", esp_err_to_name(err));
#else
    ESP_LOGI("probe", "READ_ONLY_PROBE_DONE; OTA writes are not enabled");
#endif
}
