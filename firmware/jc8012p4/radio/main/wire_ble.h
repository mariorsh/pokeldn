#pragma once
#include "wire.h"
void jc_ble_start(wire_handler_t handler);
bool jc_ble_send_wait(uint8_t type, const void *head, size_t hn, const void *body, size_t bn, uint32_t ticks);
void jc_ble_send(uint8_t type, const void *head, size_t hn, const void *body, size_t bn);
void jc_ble_credit_reset(void);
void jc_ble_set_host_away(bool away);
uint32_t jc_ble_consumed(void);
uint32_t jc_ble_dropped(void);
uint32_t jc_ble_rx_bad(void);
