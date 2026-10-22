/* USB/UART pokeldn protocol <-> SDIO records. One radio, one USB session. */
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_serial_slave_link/essl_sdio.h"
#include "esp_timer.h"
#include "esp_chip_info.h"
#include "esp_attr.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "packet.h"
#include "panel.h"
#include "wire.h"

static essl_handle_t s_link;
static SemaphoreHandle_t s_bus;
static atomic_bool s_ready;
static atomic_bool s_bluetooth;
static atomic_uint s_stage, s_signature, s_sdio_error;
static sdmmc_card_t s_card;

static void result(uint8_t type, esp_err_t error)
{
    uint8_t p[5] = {type};
    memcpy(p + 1, &error, 4);
    jc_panel_message(true, 0x82, p, sizeof(p));
    wire_send(0x82, p, sizeof(p), NULL, 0);
}
/* A STATUS reply is available even with factory C6 firmware. It confirms that
 * the P4 passed PSRAM/panel initialization without inventing a radio identity. */
static void panel_status(const uint8_t *radio, size_t length)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    char text[1280];
    int n = snprintf(text, sizeof(text),
        "%.*s bridge=jc8012p4 bridge_ready=%u bridge_stage=%u c6_signature=%08x "
        "p4_revision=%u psram_bytes=%u p4_heap=%u p4_wire_dropped=%u p4_wire_bad=%u lcd_frames=%u sdio_error=%#x ble_active=%u",
        (int)length, radio ? (const char *)radio : "", (unsigned)atomic_load(&s_ready),
        atomic_load(&s_stage), atomic_load(&s_signature), (unsigned)chip.revision,
        (unsigned)esp_psram_get_size(), (unsigned)esp_get_free_heap_size(), (unsigned)wire_dropped(), (unsigned)wire_rx_bad(), jc_panel_frames(), atomic_load(&s_sdio_error), (unsigned)atomic_load(&s_bluetooth));
    if (n > 0) wire_send(0x89, text, (size_t)n < sizeof(text) ? n : sizeof(text) - 1, NULL, 0);
}
static void command(uint8_t type, const uint8_t *p, size_t n)
{
    if (atomic_load(&s_bluetooth)) {
        if (type == 0x0b) panel_status(NULL, 0);
        else result(type, ESP_ERR_INVALID_STATE);
        return;
    }
    if (type == 0x01) wire_credit_reset();
    if (type == 0x0b && !atomic_load(&s_ready)) {
        panel_status(NULL, 0);
        return;
    }
    if (type == 0x02) {
        if (n != 4) { result(type, ESP_ERR_INVALID_ARG); return; }
        uint32_t baud;
        memcpy(&baud, p, 4);
        if (baud < 115200 || baud > 2000000) { result(type, ESP_ERR_INVALID_ARG); return; }
        result(type, ESP_OK);
        wire_set_baud(baud);
        return;
    }
    if (type == 0x0e) {
        result(type, jc_panel_display(p, n) ? ESP_OK : ESP_ERR_INVALID_ARG);
        return;
    }
    jc_panel_message(false, type, p, n);
    if (!atomic_load(&s_ready)) {
        result(type, ESP_ERR_INVALID_STATE);
        return;
    }
    /* The serial reader owns this buffer. ESSL waits for C6 receive-buffer credits before sending. */
    static DMA_ATTR uint8_t tx[JC_SDIO_BUFFER] __attribute__((aligned(64)));
    size_t size = jc_packet_pack(tx, sizeof(tx), type, p, n, NULL, 0);
    if (!size) { result(type, ESP_ERR_INVALID_SIZE); return; }
    xSemaphoreTake(s_bus, portMAX_DELAY);
    esp_err_t err = essl_send_packet(s_link, tx, size, 100);
    xSemaphoreGive(s_bus);
    if (err != ESP_OK) {
        result(type, err);
        jc_panel_note("Error enviando al C6");
    }
}

static void receive(void *arg)
{
    static DMA_ATTR uint8_t rx[JC_SDIO_BUFFER] __attribute__((aligned(64)));
    for (;;) {
        size_t size = 0;
        xSemaphoreTake(s_bus, portMAX_DELAY);
        esp_err_t err = essl_get_packet(s_link, rx, sizeof(rx), &size, 5);
        xSemaphoreGive(s_bus);
        if (err == ESP_OK) {
            uint8_t type;
            const uint8_t *p;
            size_t n;
            if (jc_packet_unpack(rx, size, &type, &p, &n)) {
                if (type == JC_MSG_HOST_TRANSPORT && n == 1) {
                    atomic_store(&s_bluetooth, p[0] != 0);
                    jc_panel_message(true, type, p, n);
                    continue;
                }
                if (type == JC_MSG_DISPLAY_REQUEST && n >= 4) {
                    uint8_t reply[8]; memcpy(reply, p, 4);
                    esp_err_t status = jc_panel_display(p + 4, n - 4) ? ESP_OK : ESP_ERR_INVALID_ARG;
                    memcpy(reply + 4, &status, 4);
                    static DMA_ATTR uint8_t ack[64] __attribute__((aligned(64)));
                    size_t length = jc_packet_pack(ack, sizeof(ack), JC_MSG_DISPLAY_RESULT, reply, sizeof(reply), NULL, 0);
                    xSemaphoreTake(s_bus, portMAX_DELAY);
                    esp_err_t sent = essl_send_packet(s_link, ack, length, 100);
                    xSemaphoreGive(s_bus);
                    if (sent != ESP_OK) jc_panel_note("Error confirmando pantalla Bluetooth");
                    continue;
                }
                if (type < 0x80) {
                    jc_panel_message(false, type, p, n);
                    continue;
                }
                jc_panel_message(true, type, p, n);
                if (atomic_load(&s_bluetooth)) continue;
                if (type == 0x81 && n >= 14) {
                    /* MACs/revision belong to the C6; identify the programming target as P4.
                     * Numeric protocol capability version keeps upstream ALIVE support enabled. */
                    const char text[] = "pokeldn-radio esp32p4 version=1.6.3 board=JC8012P4A1C_I_W_Y radio=esp32c6";
                    wire_send(type, p, 14, text, sizeof(text) - 1);
                } else if (type == 0x89) panel_status(p, n);
                else if (type != 0x8b) wire_send(type, p, n, NULL, 0);
            } else jc_panel_note("Paquete SDIO invalido");
        } else if (err != ESP_ERR_TIMEOUT && err != ESP_ERR_NOT_FOUND) {
            atomic_store(&s_sdio_error, err);
            wire_log("SDIO receive failed: %s (%#x)", esp_err_to_name(err), err);
            /* Do not forward fragments or spin on a broken bus. Requires reset of both chips. */
            atomic_store(&s_ready, false);
            atomic_store(&s_stage, 6);
            jc_panel_message(true, 0xff, NULL, 0);
            jc_panel_note("Enlace SDIO perdido: reinicia placa");
            vTaskDelete(NULL);
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void app_main(void)
{
    jc_panel_start();
    s_bus = xSemaphoreCreateMutex();
    configASSERT(s_bus);
    wire_start(command);
    /* Schematic: GPIO54 -> C6 EN; P4 BOOT never selects the C6 ROM loader. */
    const gpio_config_t reset = {.pin_bit_mask = 1ULL << 54, .mode = GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&reset));
    gpio_set_level(54, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(54, 1);
    vTaskDelay(pdMS_TO_TICKS(500));

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_4BIT | SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT; /* 20 MHz bring-up; no speculative overclock */
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = GPIO_NUM_18; slot.cmd = GPIO_NUM_19;
    slot.d0 = GPIO_NUM_14; slot.d1 = GPIO_NUM_15;
    slot.d2 = GPIO_NUM_16; slot.d3 = GPIO_NUM_17;
    atomic_store(&s_stage, 1);
    ESP_ERROR_CHECK(sdmmc_host_init());
    ESP_ERROR_CHECK(sdmmc_host_init_slot(host.slot, &slot));
    while (sdmmc_card_init(&host, &s_card) != ESP_OK) {
        jc_panel_note("C6 sin respuesta: comprobar firmware");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    atomic_store(&s_stage, 2);
    essl_sdio_config_t cfg = {.card = &s_card, .recv_buffer_size = JC_SDIO_BUFFER};
    ESP_ERROR_CHECK(essl_sdio_init_dev(&s_link, &cfg));
    ESP_ERROR_CHECK(essl_init(s_link, 2000));
    ESP_ERROR_CHECK(essl_wait_for_ready(s_link, 2000));
    atomic_store(&s_stage, 3);
    bool signature_matches = false;
    /* Function-ready can precede the C6 application's registers, especially
     * when its bootloader verifies a larger Bluetooth image. */
    for (unsigned attempt = 0; attempt < 100 && !signature_matches; ++attempt) {
        uint32_t signature = 0;
        signature_matches = true;
        for (int i = 0; i < 4; ++i) {
            uint8_t value = 0;
            esp_err_t error = essl_read_reg(s_link, i, &value, 1000);
            signature |= (uint32_t)value << (i * 8);
            if (error != ESP_OK || value != JC_SDIO_MAGIC[i]) signature_matches = false;
        }
        atomic_store(&s_signature, signature);
        if (!signature_matches) vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!signature_matches) {
        atomic_store(&s_stage, 4);
        jc_panel_note("C6 incompatible: cargar radio JC8012");
        return;
    }
    atomic_store(&s_stage, 5);
    atomic_store(&s_ready, true);
    jc_panel_note("SDIO conectado a la radio C6");
    configASSERT(xTaskCreate(receive, "c6_events", 6144, NULL, 18, NULL) == pdPASS);
}
