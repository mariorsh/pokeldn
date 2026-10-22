#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
void jc_panel_start(void);
void jc_panel_note(const char *text);
void jc_panel_message(bool from_radio, uint8_t type, const uint8_t *p, size_t n);
bool jc_panel_display(const uint8_t *p, size_t n);
unsigned jc_panel_frames(void);
