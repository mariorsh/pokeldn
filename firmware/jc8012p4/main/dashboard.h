#pragma once
#include <stdint.h>
#include "events.h"
#define JC_PANEL_WIDTH 800
#define JC_PANEL_HEIGHT 1280
void jc_dashboard_render(uint16_t *pixels, const jc_events_t *state, const uint8_t *scene);
