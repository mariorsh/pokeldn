/* Software preview of the actual firmware renderer; writes a PPM to stdout. */
#include <stdio.h>
#include <stdlib.h>
#include "dashboard.h"
#include "screen.h"
int main(void)
{
    jc_events_t state;
    jc_events_init(&state);
    uint8_t info[14] = {1}, ch[] = {6}, ok[] = {6, 0, 0, 0, 0}, console[9] = {0};
    console[8] = 1;
    jc_events_radio(&state, 0x81, info, sizeof(info), 1100);
    jc_events_command(&state, 0x01, NULL, 0, 1300);
    jc_events_command(&state, 0x06, ch, 1, 3000);
    jc_events_radio(&state, 0x82, ok, sizeof(ok), 3100);
    jc_events_radio(&state, 0x87, console, 9, 9000);
    const uint8_t gift[] = {0, SHOW_GIFT, 0, 0, 'E','v','e','n','t','o',0,'M','e','w',0};
    scene_panel(PANEL_128X64);
    scene_reset();
    scene_command(gift, sizeof(gift), 10000);
    jc_events_command(&state, 0x0e, gift, sizeof(gift), 10000);
    state.radio.rx = 1248; state.radio.tx = 916;
    uint8_t frame[SCREEN_BYTES];
    scene_draw(frame, &state.radio, 12000);
    uint16_t *pixels = calloc(JC_PANEL_WIDTH * JC_PANEL_HEIGHT, 2);
    if (!pixels) return 1;
    jc_dashboard_render(pixels, &state, frame);
    printf("P6\n%d %d\n255\n", JC_PANEL_WIDTH, JC_PANEL_HEIGHT);
    for (int i = 0; i < JC_PANEL_WIDTH * JC_PANEL_HEIGHT; ++i) {
        const uint16_t p = pixels[i];
        putchar(((p >> 11) & 31) * 255 / 31);
        putchar(((p >> 5) & 63) * 255 / 63);
        putchar((p & 31) * 255 / 31);
    }
    free(pixels);
}
