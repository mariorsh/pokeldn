/* Reuse the upstream COBS, CRC, batching and credit implementation unchanged.
 * Only its physical fallback is disabled: the JC8012 USB belongs to the P4. */
#define POKELDN_BLE_LINK_ONLY 1
#define wire_start jc_ble_start
#define wire_send_wait jc_ble_send_wait
#define wire_send jc_ble_send
#define wire_log jc_ble_log
#define wire_set_baud jc_ble_set_baud
#define wire_credit_reset jc_ble_credit_reset
#define wire_set_host_away jc_ble_set_host_away
#define wire_consumed jc_ble_consumed
#define wire_dropped jc_ble_dropped
#define wire_rx_bad jc_ble_rx_bad
#define wire_rx_fifo_ovf jc_ble_rx_fifo_ovf
#define wire_rx_buffer_full jc_ble_rx_buffer_full
#define wire_rx_frame_err jc_ble_rx_frame_err
#define wire_events_full jc_ble_events_full
#define wire_stats jc_ble_stats
#include "../../../esp32/main/wire.c"
