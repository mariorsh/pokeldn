#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Initial OTA trial has 120 seconds to receive explicit PC confirmation over P4.
 * A valid previous app stays in its own partition, for timed or requested rollback. */
void ota_guard_start(void);
bool ota_guard_command(uint8_t type, const uint8_t *payload, size_t length);
unsigned ota_guard_pending(void);
unsigned ota_guard_fallback(void);
