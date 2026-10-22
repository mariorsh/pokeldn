/* This board has no XIAO antenna switch, status LED or OLED on the C6.
 * The P4 handles DISPLAY and renders the radio events; do not probe arbitrary C6 GPIOs. */
#include "display.h"
#include "led.h"
#include "usbwatch.h"
bool display_start(display_state_t state) { (void)state; return false; }
bool display_command(const uint8_t *p, size_t n) { (void)p; (void)n; return false; }
void display_reset(void) {}
void usbwatch_start(void) {}
void usbwatch_report(void) {}
void led_start(led_state_t state, led_button_t button) { (void)state; (void)button; }
bool led_set(uint8_t pattern, uint8_t peak, uint16_t period, uint16_t duration)
{
    (void)peak; (void)period; (void)duration;
    return pattern < LED_PATTERNS;
}
