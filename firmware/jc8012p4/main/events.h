#pragma once
#include "scene.h"
#define JC_EVENTS 8
#define JC_EVENT_TEXT 48
typedef struct { uint32_t at; char text[JC_EVENT_TEXT]; } jc_event_t;
typedef struct {
    scene_radio_t radio;
    bool ready, host, watched, bluetooth;
    uint8_t channel, pending, before_mode;
    uint32_t last_host, errors;
    jc_event_t events[JC_EVENTS];
    unsigned next, count;
} jc_events_t;
void jc_events_init(jc_events_t *s);
void jc_events_add(jc_events_t *s, const char *text, uint32_t now);
void jc_events_command(jc_events_t *s, uint8_t type, const uint8_t *p, size_t n, uint32_t now);
void jc_events_radio(jc_events_t *s, uint8_t type, const uint8_t *p, size_t n, uint32_t now);
void jc_events_tick(jc_events_t *s, uint32_t now);
