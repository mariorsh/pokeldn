#include "ota_guard.h"
#include "wire.h"
#include <stdatomic.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#define TRIAL_MS 120000
static const esp_partition_t *s_fallback;
static atomic_bool s_pending;
static nvs_handle_t s_nvs;
static bool s_nvs_open;

static void reboot_previous(void)
{
    if (s_fallback && esp_ota_set_boot_partition(s_fallback) == ESP_OK) {
        if (s_nvs_open) { nvs_set_u8(s_nvs, "tries", 0); nvs_commit(s_nvs); }
        esp_restart();
    }
}
static void trial_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(TRIAL_MS));
    if (atomic_load(&s_pending)) reboot_previous();
    vTaskDelete(NULL);
}
static void rollback_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(250)); /* let RESULT leave the C6 */
    reboot_previous();
    vTaskDelete(NULL);
}
void ota_guard_start(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running || running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) return;
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it) {
        const esp_partition_t *part = esp_partition_get(it);
        esp_app_desc_t desc;
        if (part->address != running->address && esp_ota_get_partition_description(part, &desc) == ESP_OK) {
            const esp_partition_pos_t pos = {.offset = part->address, .size = part->size};
            esp_image_metadata_t metadata;
            if (esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &metadata) == ESP_OK) {
                s_fallback = part;
                /* Prefer the untouched factory app when the table has one. */
                if (part->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) break;
            }
        }
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
    if (!s_fallback) return; /* direct serial install, or no recoverable app */
    if (nvs_open("pchg_boot", NVS_READWRITE, &s_nvs) != ESP_OK) { reboot_previous(); return; }
    s_nvs_open = true;
    uint8_t saved[32]; size_t length = sizeof(saved);
    const uint8_t *digest = esp_app_get_description()->app_elf_sha256;
    if (nvs_get_blob(s_nvs, "confirmed", saved, &length) == ESP_OK &&
        length == sizeof(saved) && !memcmp(saved, digest, sizeof(saved))) return;
    uint8_t tries = 0;
    nvs_get_u8(s_nvs, "tries", &tries);
    if (tries >= 8) { reboot_previous(); return; }
    if (nvs_set_u8(s_nvs, "tries", tries + 1) != ESP_OK || nvs_commit(s_nvs) != ESP_OK) {
        reboot_previous(); return;
    }
    atomic_store(&s_pending, true);
    if (xTaskCreate(trial_task, "ota_trial", 4096, NULL, 1, NULL) != pdPASS) reboot_previous();
}
unsigned ota_guard_pending(void) { return atomic_load(&s_pending); }
unsigned ota_guard_fallback(void) { return s_fallback ? s_fallback->address : 0; }

bool ota_guard_command(uint8_t type, const uint8_t *p, size_t n)
{
    if (type != 0x10 && type != 0x11) return false;
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (n == 4 && !memcmp(p, "PKLD", 4)) {
        if (type == 0x10) {
            if (!s_fallback) err = ESP_ERR_NOT_SUPPORTED;
            else {
                err = esp_ota_mark_app_valid_cancel_rollback();
                if (err == ESP_OK) err = nvs_set_blob(s_nvs, "confirmed", esp_app_get_description()->app_elf_sha256, 32);
                if (err == ESP_OK) err = nvs_set_u8(s_nvs, "tries", 0);
                if (err == ESP_OK) err = nvs_commit(s_nvs);
                if (err == ESP_OK) atomic_store(&s_pending, false);
            }
        } else {
            err = s_fallback ? ESP_OK : ESP_ERR_NOT_FOUND;
            if (err == ESP_OK && xTaskCreate(rollback_task, "ota_return", 4096, NULL, 1, NULL) != pdPASS)
                err = ESP_ERR_NO_MEM;
        }
    }
    uint8_t reply[5] = {type};
    memcpy(reply + 1, &err, 4);
    wire_send(0x82, reply, sizeof(reply), NULL, 0);
    return true;
}
