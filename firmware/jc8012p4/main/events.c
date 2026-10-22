/* Pure event reducer, also compiled on the development machine for protocol tests. */
#include "events.h"
#include "packet.h"
#include <stdio.h>
#include <string.h>

void jc_events_add(jc_events_t *s, const char *text, uint32_t now)
{
    jc_event_t *e = &s->events[s->next];
    e->at = now;
    snprintf(e->text, sizeof(e->text), "%s", text);
    s->next = (s->next + 1) % JC_EVENTS;
    if (s->count < JC_EVENTS) ++s->count;
}
void jc_events_init(jc_events_t *s)
{
    memset(s, 0, sizeof(*s));
    jc_events_add(s, "Esperando radio C6 por SDIO", 0);
}
void jc_events_command(jc_events_t *s, uint8_t type, const uint8_t *p, size_t n, uint32_t now)
{
    s->host = true;
    s->last_host = now;
    if (type == 0x01) {
        s->watched = false;
        jc_events_add(s, s->bluetooth ? "App conectada por Bluetooth" : "App conectada por USB", now);
    } else if (type == 0x0f) s->watched = true;
    else if (type == 0x03 && n == 1) s->channel = p[0];
    else if ((type == 0x04 || type == 0x06 || type == 0x0a) && n) {
        s->channel = p[0];
        s->pending = type;
        s->before_mode = s->radio.mode;
        jc_events_add(s, type == 0x04 ? "Conectando a la consola..." :
                         type == 0x06 ? "Iniciando red local..." : "Iniciando captura...", now);
    } else if (type == 0x05) {
        s->pending = type;
        s->before_mode = s->radio.mode;
    } else if (type == 0x08) ++s->radio.tx;
    else if (type == 0x0e && n >= 4 && p[0] == DISPLAY_OP_SHOW && p[1] < SHOWS) {
        static const char *names[] = {"Estado de la radio", "Intercambio preparado", "Intercambio completado",
            "Evento / regalo preparado", "Regalo entregado", "Pokemon recibido"};
        jc_events_add(s, names[p[1]], now);
    }
}
void jc_events_radio(jc_events_t *s, uint8_t type, const uint8_t *p, size_t n, uint32_t now)
{
    if (type == JC_MSG_HOST_TRANSPORT && n == 1) {
        s->bluetooth = p[0] != 0;
        s->host = false;
        s->watched = false;
        s->pending = 0;
        s->radio.mode = SCENE_IDLE;
        s->radio.stations = 0;
        jc_events_add(s, s->bluetooth ? "Control desde iPhone / Bluetooth" : "Control USB disponible", now);
    } else if (type == 0xff) {
        s->ready = false;
        s->radio.mode = SCENE_IDLE;
        s->radio.stations = 0;
    } else if (type == 0x81 && n >= 14) {
        s->ready = true;
        jc_events_add(s, "Radio C6 lista", now);
    } else if (type == 0x82 && n == 5) {
        const bool ok = !(p[1] | p[2] | p[3] | p[4]);
        if (!ok) {
            ++s->errors;
            char line[JC_EVENT_TEXT];
            snprintf(line, sizeof(line), "Error cmd %02X: %02X%02X%02X%02X", p[0], p[4], p[3], p[2], p[1]);
            jc_events_add(s, line, now);
        }
        if (s->pending == p[0]) {
            s->pending = 0;
            if (!ok) s->radio.mode = s->before_mode;
            else if (p[0] == 0x04) s->radio.mode = SCENE_JOINING;
            else if (p[0] == 0x06) s->radio.mode = SCENE_HOSTING;
            else if (p[0] == 0x0a) s->radio.mode = SCENE_SNIFFING;
            else if (p[0] == 0x05) {
                s->radio.mode = SCENE_IDLE;
                s->radio.stations = 0;
                jc_events_add(s, "Sesion detenida", now);
            }
        }
    } else if (type == 0x85) ++s->radio.rx;
    else if (type == 0x86 && n == 9) {
        if (s->radio.mode != SCENE_HOSTING) s->radio.mode = p[0] ? SCENE_JOINED : SCENE_IDLE;
        jc_events_add(s, p[0] ? "Enlace con consola activo" : "Enlace con consola cerrado", now);
    } else if (type == 0x87 && n == 9) {
        if (!p[7] && p[8]) {
            if (s->radio.stations < 255) ++s->radio.stations;
            jc_events_add(s, "Consola conectada", now);
        } else { ++s->errors; jc_events_add(s, "Error autenticando consola", now); }
    } else if (type == 0x88 && n == 8) {
        if (s->radio.stations) --s->radio.stations;
        jc_events_add(s, "Consola desconectada", now);
    }
}
void jc_events_tick(jc_events_t *s, uint32_t now)
{
    if (s->host && s->watched && (uint32_t)(now - s->last_host) > 5000) {
        s->host = false;
        s->radio.mode = SCENE_IDLE;
        s->radio.stations = 0;
        s->pending = 0;
        jc_events_add(s, "App desconectada / sesion cerrada", now);
    }
}
