#include "dashboard.h"
#include "screen.h"
#include <stdio.h>

#define WIDTH 800
#define HEIGHT 1280
#define RGB(r,g,b) (((r) >> 3) << 11 | ((g) >> 2) << 5 | ((b) >> 3))
#define BG RGB(12, 19, 31)
#define CARD RGB(23, 35, 51)
#define INK RGB(232, 240, 248)
#define MUTED RGB(142, 164, 186)
#define ACCENT RGB(89, 226, 173)
#define WARN RGB(255, 185, 92)

static void box(uint16_t *pixels, int x, int y, int w, int h, uint16_t color)
{
    for (int j = y; j < y + h && j < HEIGHT; ++j)
        for (int i = x; i < x + w && i < WIDTH; ++i)
            if (i >= 0 && j >= 0) pixels[j * WIDTH + i] = color;
}
static void text(uint16_t *pixels, int x, int y, const char *line, int scale, uint16_t color)
{
    uint8_t glyph[SCREEN_BYTES];
    for (; *line && x < WIDTH - 20; ++line, x += 6 * scale) {
        fb_clear(glyph);
        char letter[2] = {*line, 0};
        fb_text(glyph, 0, 0, letter, 1);
        for (int j = 0; j < 7; ++j)
            for (int i = 0; i < 5; ++i)
                if (fb_get(glyph, i, j)) box(pixels, x + i * scale, y + j * scale, scale, scale, color);
    }
}
void jc_dashboard_render(uint16_t *pixels, const jc_events_t *s, const uint8_t *frame)
{
    box(pixels, 0, 0, WIDTH, HEIGHT, BG);
    box(pixels, 32, 36, 8, 45, ACCENT);
    text(pixels, 60, 40, "PokeChanger", 5, INK);
    text(pixels, 36, 107, "JC8012 / P4 + C6", 3, MUTED);
    box(pixels, 32, 167, 736, 111, CARD);
    text(pixels, 54, 186, s->ready ? "RADIO C6 LISTA" : "ESPERANDO RADIO C6", 3, s->ready ? ACCENT : WARN);
    text(pixels, 54, 229, s->host ? "USB: APP CONECTADA" : "USB: ESPERANDO APP", 3, MUTED);
    box(pixels, 32, 298, 736, 360, CARD);
    for (int y = 0; y < SCREEN_H; ++y)
        for (int x = 0; x < SCREEN_W; ++x)
            if (fb_get(frame, x, y)) box(pixels, 80 + x * 5, 318 + y * 5, 5, 5, ACCENT);
    char line[96];
    snprintf(line, sizeof(line), "CANAL %u   CONSOLAS %u", s->channel, s->radio.stations);
    text(pixels, 36, 691, line, 3, INK);
    snprintf(line, sizeof(line), "RX %lu   TX %lu   ERRORES %lu", (unsigned long)s->radio.rx,
             (unsigned long)s->radio.tx, (unsigned long)s->errors);
    text(pixels, 36, 731, line, 2, MUTED);
    text(pixels, 36, 790, "EVENTOS DE LA SESION", 3, INK);
    for (unsigned i = 0; i < s->count; ++i) {
        const jc_event_t *e = &s->events[(s->next + JC_EVENTS - 1 - i) % JC_EVENTS];
        int y = 839 + i * 44;
        snprintf(line, sizeof(line), "%02lu:%02lu", (unsigned long)(e->at / 60000),
                 (unsigned long)(e->at / 1000 % 60));
        text(pixels, 36, y, line, 2, MUTED);
        text(pixels, 128, y, e->text, 2, i == 0 ? ACCENT : INK);
    }
    text(pixels, 36, 1229, "APP USB > P4 > SDIO > C6 > SWITCH", 2, MUTED);
}
