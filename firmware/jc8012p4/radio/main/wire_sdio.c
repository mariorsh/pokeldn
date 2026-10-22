/* Native C6 radio transport. Only the link changes: radio.c retains all LDN Wi-Fi hooks. */
#include "wire.h"
#include "packet.h"
#include "ota_guard.h"
#include "ble.h"
#include "wire_ble.h"
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "driver/sdio_slave.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static DMA_ATTR uint8_t s_rx[JC_SDIO_RX_BUFFERS][JC_SDIO_BUFFER];
static DMA_ATTR uint8_t s_tx[JC_SDIO_BUFFER];
typedef struct { size_t size; uint8_t bytes[JC_PACKET_PAYLOAD + 4]; } message_t;
static QueueHandle_t s_out;
static SemaphoreHandle_t s_dispatch;
static wire_handler_t s_handler;
static atomic_bool s_away;
static atomic_uint s_dropped, s_bad, s_consumed;
static atomic_uint s_mirror_dropped;
static atomic_bool s_ble_mode;
static uint32_t s_display_token;
static bool s_display_pending;
static int64_t s_display_until;

static bool sdio_post(uint8_t type, const void *head, size_t hn, const void *body, size_t bn, uint32_t ticks)
{
    message_t msg;
    msg.size = jc_packet_pack(msg.bytes, sizeof(msg.bytes), type, head, hn, body, bn);
    return msg.size && xQueueSend(s_out, &msg, ticks) == pdTRUE;
}
bool wire_send_wait(uint8_t type, const void *head, size_t hn, const void *body, size_t bn, uint32_t ticks)
{
    if (atomic_load(&s_away)) return true;
    if (!ble_linked()) return sdio_post(type, head, hn, body, bn, ticks);
    bool ok = jc_ble_send_wait(type, head, hn, body, bn, ticks);
    /* P4 observes state and counts; bulk data travels directly to the iPhone. */
    if (type == 0x85) { head = NULL; hn = 0; body = NULL; bn = 0; }
    if ((type == 0x81 || type == 0x82 || (type >= 0x85 && type <= 0x89)) &&
        !sdio_post(type, head, hn, body, bn, 0)) atomic_fetch_add(&s_mirror_dropped, 1);
    return ok;
}
void wire_send(uint8_t type, const void *head, size_t hn, const void *body, size_t bn)
{
    if (!wire_send_wait(type, head, hn, body, bn, 0)) atomic_fetch_add(&s_dropped, 1);
}
void wire_log(const char *format, ...)
{
    char line[160];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (n > 0) wire_send(0x83, line, n < sizeof(line) ? n : sizeof(line) - 1, NULL, 0);
}
void wire_set_baud(uint32_t baud) { (void)baud; } /* P4 owns the USB/UART rate. */
void wire_credit_reset(void) { if (ble_linked()) jc_ble_credit_reset(); }
void wire_set_host_away(bool away) { atomic_store(&s_away, away); jc_ble_set_host_away(away); }
uint32_t wire_consumed(void) { return ble_linked() ? jc_ble_consumed() : atomic_load(&s_consumed); }
uint32_t wire_dropped(void) { return atomic_load(&s_dropped) + jc_ble_dropped(); }
uint32_t wire_rx_bad(void) { return atomic_load(&s_bad) + jc_ble_rx_bad(); }
uint32_t wire_rx_fifo_ovf(void) { return 0; }
uint32_t wire_rx_buffer_full(void) { return 0; }
uint32_t wire_rx_frame_err(void) { return 0; }
uint32_t wire_events_full(void) { return 0; }
int wire_stats(char *text, size_t size)
{
    return snprintf(text, size, "link=%s sdio_queue=%u ble_ready=%u ble_itvl_us=%u p4_mirror_dropped=%u ota_pending=%u ota_fallback=%#x", ble_linked() ? "ble" : "sdio", (unsigned)uxQueueMessagesWaiting(s_out), ble_ready(), (unsigned)ble_interval_us(), atomic_load(&s_mirror_dropped), ota_guard_pending(), ota_guard_fallback());
}

/* Called with s_dispatch held: state precedes its observed commands on SDIO. */
static void sync_mode(void)
{
    bool linked = ble_linked();
    if (linked == atomic_load(&s_ble_mode)) return;
    uint8_t state = linked;
    if (sdio_post(JC_MSG_HOST_TRANSPORT, &state, 1, NULL, 0, 0)) {
        atomic_store(&s_ble_mode, linked);
        s_display_pending = false;
        /* A newly selected controller starts with a closed wireless session. */
        s_handler(0x05, NULL, 0);
    }
}
static void bluetooth_command(uint8_t type, const uint8_t *p, size_t n)
{
    xSemaphoreTake(s_dispatch, portMAX_DELAY);
    sync_mode();
    if (type == 0x0e) {
        int32_t error = ESP_ERR_INVALID_STATE;
        if (!s_display_pending) {
            uint32_t token = ++s_display_token;
            if (sdio_post(JC_MSG_DISPLAY_REQUEST, &token, 4, p, n, 0)) {
                s_display_pending = true;
                s_display_until = esp_timer_get_time() + 2000000;
                xSemaphoreGive(s_dispatch);
                return;
            }
            error = ESP_ERR_NO_MEM;
        }
        uint8_t reply[5] = {type}; memcpy(reply + 1, &error, 4);
        jc_ble_send(0x82, reply, sizeof(reply), NULL, 0);
    } else {
        /* Only the channel byte is needed by the monitor; never mirror keys or frames. */
        size_t observed = (type == 0x03 || type == 0x04 || type == 0x06 || type == 0x0a) && n ? 1 : 0;
        if (!sdio_post(type, p, observed, NULL, 0, 0)) atomic_fetch_add(&s_mirror_dropped, 1);
        if (!ota_guard_command(type, p, n)) s_handler(type, p, n);
    }
    xSemaphoreGive(s_dispatch);
}
static void mode_task(void *arg)
{
    for (;;) {
        xSemaphoreTake(s_dispatch, portMAX_DELAY);
        sync_mode();
        if (s_display_pending && esp_timer_get_time() > s_display_until) {
            int32_t error = ESP_ERR_TIMEOUT;
            uint8_t reply[5] = {0x0e}; memcpy(reply + 1, &error, 4);
            jc_ble_send(0x82, reply, sizeof(reply), NULL, 0);
            s_display_pending = false;
        }
        xSemaphoreGive(s_dispatch);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void writer(void *arg)
{
    message_t msg;
    for (;;) {
        if (xQueueReceive(s_out, &msg, portMAX_DELAY) != pdTRUE) continue;
        if (atomic_load(&s_away) && msg.bytes[1] != JC_MSG_HOST_TRANSPORT &&
            msg.bytes[1] != JC_MSG_DISPLAY_REQUEST) continue;
        memcpy(s_tx, msg.bytes, msg.size);
        ESP_ERROR_CHECK(sdio_slave_send_queue(s_tx, msg.size, NULL, portMAX_DELAY));
        /* DMA owns s_tx until the P4 reads it. Never reuse on a timeout. */
        void *finished;
        ESP_ERROR_CHECK(sdio_slave_send_get_finished(&finished, portMAX_DELAY));
    }
}

static void reader(void *arg)
{
    for (;;) {
        sdio_slave_buf_handle_t handle;
        esp_err_t err = sdio_slave_recv_packet(&handle, portMAX_DELAY);
        if (err != ESP_OK && err != ESP_ERR_NOT_FINISHED) continue;
        bool fragmented = err == ESP_ERR_NOT_FINISHED;
        /* Valid records always fit in one receive buffer. Drain a malformed oversized packet. */
        for (;;) {
            size_t size;
            const uint8_t *data = sdio_slave_recv_get_buf(handle, &size);
            uint8_t type;
            const uint8_t *payload;
            size_t length;
            if (!fragmented && jc_packet_unpack(data, size, &type, &payload, &length)) {
                atomic_fetch_add(&s_consumed, size);
                xSemaphoreTake(s_dispatch, portMAX_DELAY);
                sync_mode();
                if (type == JC_MSG_DISPLAY_RESULT && length == 8) {
                    uint32_t token; memcpy(&token, payload, 4);
                    if (s_display_pending && token == s_display_token && ble_linked()) {
                        uint8_t reply[5] = {0x0e}; memcpy(reply + 1, payload + 4, 4);
                        jc_ble_send(0x82, reply, sizeof(reply), NULL, 0);
                        s_display_pending = false;
                    }
                } else if (ble_linked()) {
                    int32_t error = ESP_ERR_INVALID_STATE;
                    uint8_t reply[5] = {type}; memcpy(reply + 1, &error, 4);
                    sdio_post(0x82, reply, sizeof(reply), NULL, 0, 0);
                } else if (!ota_guard_command(type, payload, length)) s_handler(type, payload, length);
                xSemaphoreGive(s_dispatch);
            } else atomic_fetch_add(&s_bad, 1);
            ESP_ERROR_CHECK(sdio_slave_recv_load_buf(handle));
            if (err == ESP_OK) break;
            err = sdio_slave_recv_packet(&handle, portMAX_DELAY);
            ESP_ERROR_CHECK(err == ESP_ERR_NOT_FINISHED ? ESP_OK : err);
        }
    }
}

void wire_start(wire_handler_t handler)
{
    s_handler = handler;
    s_out = xQueueCreate(12, sizeof(message_t));
    s_dispatch = xSemaphoreCreateMutex();
    configASSERT(s_out);
    configASSERT(s_dispatch);
    sdio_slave_config_t cfg = {
        .sending_mode = SDIO_SLAVE_SEND_PACKET,
        .send_queue_size = 4,
        .recv_buffer_size = JC_SDIO_BUFFER,
    };
    ESP_ERROR_CHECK(sdio_slave_initialize(&cfg));
    for (int i = 0; i < JC_SDIO_RX_BUFFERS; ++i) {
        sdio_slave_buf_handle_t handle = sdio_slave_recv_register_buf(s_rx[i]);
        configASSERT(handle);
        ESP_ERROR_CHECK(sdio_slave_recv_load_buf(handle));
    }
    for (int i = 0; i < 4; ++i) sdio_slave_write_reg(i, JC_SDIO_MAGIC[i]);
    sdio_slave_set_host_intena(SDIO_SLAVE_HOSTINT_SEND_NEW_PACKET);
    ESP_ERROR_CHECK(sdio_slave_start());
    configASSERT(xTaskCreate(writer, "sdio_tx", 4096, NULL, 20, NULL) == pdPASS);
    configASSERT(xTaskCreate(reader, "sdio_rx", 8192, NULL, 19, NULL) == pdPASS);
    jc_ble_start(bluetooth_command);
    configASSERT(xTaskCreate(mode_task, "host_mode", 6144, NULL, 18, NULL) == pdPASS);
}
