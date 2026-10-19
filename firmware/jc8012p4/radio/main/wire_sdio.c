/* Native C6 radio transport. Only the link changes: radio.c retains all LDN Wi-Fi hooks. */
#include "wire.h"
#include "packet.h"
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "driver/sdio_slave.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static DMA_ATTR uint8_t s_rx[JC_SDIO_RX_BUFFERS][JC_SDIO_BUFFER];
static DMA_ATTR uint8_t s_tx[JC_SDIO_BUFFER];
typedef struct { size_t size; uint8_t bytes[JC_PACKET_PAYLOAD + 4]; } message_t;
static QueueHandle_t s_out;
static wire_handler_t s_handler;
static atomic_bool s_away;
static atomic_uint s_dropped, s_bad, s_consumed;

bool wire_send_wait(uint8_t type, const void *head, size_t hn, const void *body, size_t bn, uint32_t ticks)
{
    if (atomic_load(&s_away)) return true;
    message_t msg;
    msg.size = jc_packet_pack(msg.bytes, sizeof(msg.bytes), type, head, hn, body, bn);
    return msg.size && xQueueSend(s_out, &msg, ticks) == pdTRUE;
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
void wire_credit_reset(void) {} /* P4 credits bytes after forwarding each command. */
void wire_set_host_away(bool away) { atomic_store(&s_away, away); }
uint32_t wire_consumed(void) { return atomic_load(&s_consumed); }
uint32_t wire_dropped(void) { return atomic_load(&s_dropped); }
uint32_t wire_rx_bad(void) { return atomic_load(&s_bad); }
uint32_t wire_rx_fifo_ovf(void) { return 0; }
uint32_t wire_rx_buffer_full(void) { return 0; }
uint32_t wire_rx_frame_err(void) { return 0; }
uint32_t wire_events_full(void) { return 0; }
int wire_stats(char *text, size_t size)
{
    return snprintf(text, size, "link=sdio sdio_queue=%u", (unsigned)uxQueueMessagesWaiting(s_out));
}

static void writer(void *arg)
{
    message_t msg;
    for (;;) {
        if (xQueueReceive(s_out, &msg, portMAX_DELAY) != pdTRUE) continue;
        if (atomic_load(&s_away)) continue;
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
                s_handler(type, payload, length);
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
    s_out = xQueueCreate(48, sizeof(message_t));
    configASSERT(s_out);
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
}
