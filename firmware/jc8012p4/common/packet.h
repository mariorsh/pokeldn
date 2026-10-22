#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One SDIO packet is version, type, u16 payload length (LE), payload.
 * USB framing and credits terminate on the P4; SDIO has its own CRC and flow control. */
#define JC_PACKET_VERSION 1
#define JC_PACKET_PAYLOAD 1604 /* a 1600-byte host DISPLAY plus its request token */
#define JC_SDIO_BUFFER 2048
#define JC_SDIO_RX_BUFFERS 16
#define JC_SDIO_MAGIC "PKLD"
/* C6 -> P4: payload 0/1 selects USB or Bluetooth ownership; never a host command. */
#define JC_MSG_HOST_TRANSPORT 0xf0
#define JC_MSG_DISPLAY_REQUEST 0xf1 /* u32 token, original DISPLAY payload */
#define JC_MSG_DISPLAY_RESULT 0xf2  /* u32 token, i32 esp_err_t */
size_t jc_packet_pack(uint8_t *out, size_t capacity, uint8_t type, const void *head,
                      size_t head_len, const void *body, size_t body_len);
bool jc_packet_unpack(const uint8_t *packet, size_t size, uint8_t *type,
                      const uint8_t **payload, size_t *length);
