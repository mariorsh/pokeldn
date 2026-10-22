#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "dashboard.h"
#include "screen.h"
#include <string.h>
#include "packet.h"
#include "events.h"

static void packets(void)
{
    uint8_t packet[JC_SDIO_BUFFER], payload[JC_PACKET_PAYLOAD];
    for (size_t i = 0; i < sizeof(payload); ++i) payload[i] = (uint8_t)(i * 37);
    for (size_t n = 0; n <= sizeof(payload); ++n) {
        size_t size = jc_packet_pack(packet, sizeof(packet), 0x85, payload, n / 2,
                                     payload + n / 2, n - n / 2);
        assert(size == n + 4);
        uint8_t type;
        const uint8_t *out;
        size_t length;
        assert(jc_packet_unpack(packet, size, &type, &out, &length));
        assert(type == 0x85 && length == n && memcmp(out, payload, n) == 0);
        assert(!jc_packet_unpack(packet, size - 1, &type, &out, &length));
        assert(!jc_packet_unpack(packet, size + 1, &type, &out, &length));
        packet[0] = 2;
        assert(!jc_packet_unpack(packet, size, &type, &out, &length));
    }
    assert(!jc_packet_pack(packet, sizeof(packet), 1, payload, sizeof(payload) + 1, NULL, 0));
    assert(!jc_packet_pack(packet, 4, 1, payload, 1, NULL, 0));
    assert(!jc_packet_pack(packet, sizeof(packet), 1, NULL, 1, NULL, 0));
    assert(!jc_packet_pack(packet, sizeof(packet), 1, payload, SIZE_MAX, NULL, 1));
}
static void events(void)
{
    jc_events_t state;
    jc_events_init(&state);
    assert(!state.ready && !state.host);
    uint8_t info[14] = {1};
    jc_events_radio(&state, 0x81, info, sizeof(info), 1);
    assert(state.ready);
    jc_events_command(&state, 0x01, NULL, 0, 2);
    assert(state.host);
    uint8_t channel[] = {6};
    jc_events_command(&state, 0x06, channel, 1, 10);
    uint8_t failed[] = {0x06, 1, 0, 0, 0};
    jc_events_radio(&state, 0x82, failed, sizeof(failed), 11);
    assert(state.radio.mode == SCENE_IDLE && state.errors == 1);
    jc_events_command(&state, 0x06, channel, 1, 20);
    uint8_t ok[] = {0x06, 0, 0, 0, 0};
    jc_events_radio(&state, 0x82, ok, sizeof(ok), 21);
    assert(state.radio.mode == SCENE_HOSTING && state.channel == 6);
    uint8_t joined[9] = {0}; joined[8] = 1;
    jc_events_radio(&state, 0x87, joined, 8, 22); /* malformed, ignored */
    assert(!state.radio.stations);
    jc_events_radio(&state, 0x87, joined, 9, 23);
    assert(state.radio.stations == 1);
    jc_events_radio(&state, 0x88, joined, 8, 24);
    jc_events_radio(&state, 0x88, joined, 8, 25);
    assert(!state.radio.stations); /* no underflow */
    jc_events_command(&state, 0x0f, NULL, 0, UINT32_MAX - 1000);
    jc_events_tick(&state, 3999); /* exactly 5000 ms across rollover */
    assert(state.host);
    jc_events_tick(&state, 4000);
    assert(!state.host && state.radio.mode == SCENE_IDLE);
    for (unsigned i = 0; i < 100; ++i) jc_events_add(&state, "bounded history", i);
    assert(state.count == JC_EVENTS && state.next < JC_EVENTS);
    /* Only event names enter the journal; command payloads can carry CCMP keys. */
    jc_events_init(&state);
    const uint8_t secret[] = "sensitive-key-must-not-appear";
    jc_events_command(&state, 0x04, secret, sizeof(secret), 0);
    for (unsigned i = 0; i < state.count; ++i)
        assert(!strstr(state.events[i].text, "sensitive"));
    uint8_t bluetooth = 1;
    jc_events_radio(&state, JC_MSG_HOST_TRANSPORT, &bluetooth, 1, 10);
    assert(state.bluetooth && !state.host && state.radio.mode == SCENE_IDLE);
    jc_events_command(&state, 0x01, NULL, 0, 11);
    assert(state.host && strstr(state.events[(state.next + JC_EVENTS - 1) % JC_EVENTS].text, "Bluetooth"));
    jc_events_command(&state, 0x06, channel, 1, 12);
    jc_events_radio(&state, 0x82, ok, sizeof(ok), 13);
    assert(state.radio.mode == SCENE_HOSTING);
    bluetooth = 0;
    jc_events_radio(&state, JC_MSG_HOST_TRANSPORT, &bluetooth, 1, 14);
    assert(!state.bluetooth && !state.host && !state.pending && state.radio.mode == SCENE_IDLE);
    jc_events_command(&state, 0x01, NULL, 0, 15);
    assert(state.host && strstr(state.events[(state.next + JC_EVENTS - 1) % JC_EVENTS].text, "USB"));
}
static void render_bounds(void)
{
    const size_t size = JC_PANEL_WIDTH * JC_PANEL_HEIGHT;
    uint16_t *guarded = calloc(size + 2, sizeof(uint16_t));
    assert(guarded);
    guarded[0] = guarded[size + 1] = 0x1234;
    jc_events_t state;
    jc_events_init(&state);
    for (unsigned i = 0; i < JC_EVENTS; ++i)
        jc_events_add(&state, "A long event message that reaches the right edge", UINT32_MAX);
    state.errors = state.radio.rx = state.radio.tx = UINT32_MAX;
    state.channel = state.radio.stations = 255;
    uint8_t frame[SCREEN_BYTES];
    memset(frame, 255, sizeof(frame));
    jc_dashboard_render(guarded + 1, &state, frame);
    assert(guarded[0] == 0x1234 && guarded[size + 1] == 0x1234);
    free(guarded);
}
int main(void) { packets(); events(); render_bounds(); puts("JC8012 packet and event checks passed"); }
