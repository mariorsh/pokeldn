/* Portrait event monitor. Rendering never runs in a Wi-Fi/SDIO/USB callback. */
#include "panel.h"
#include "events.h"
#include "screen.h"
#include "dashboard.h"
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_heap_caps.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_jd9365.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "panel_registers.h"

#define WIDTH 800
#define HEIGHT 1280
static SemaphoreHandle_t s_lock;
static jc_events_t s_events;
static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_canvas;
static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

void jc_panel_note(const char *text)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    jc_events_add(&s_events, text, now_ms());
    xSemaphoreGive(s_lock);
}
void jc_panel_message(bool from_radio, uint8_t type, const uint8_t *p, size_t n)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (from_radio) jc_events_radio(&s_events, type, p, n, now_ms());
    else {
        if (type == 0x01) scene_reset();
        jc_events_command(&s_events, type, p, n, now_ms());
    }
    xSemaphoreGive(s_lock);
}
bool jc_panel_display(const uint8_t *p, size_t n)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ok = scene_command(p, n, now_ms());
    if (ok) jc_events_command(&s_events, 0x0e, p, n, now_ms());
    xSemaphoreGive(s_lock);
    return ok;
}
static void display_task(void *arg)
{
    uint8_t frame[SCREEN_BYTES];
    for (;;) {
        jc_events_t snapshot;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        jc_events_tick(&s_events, now_ms());
        const uint8_t power = scene_draw(frame, &s_events.radio, now_ms());
        snapshot = s_events;
        xSemaphoreGive(s_lock);
        jc_dashboard_render(s_canvas, &snapshot, frame);
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, 0, 0, WIDTH, HEIGHT, s_canvas));
        /* No touch interactions are required for a passive monitor. BOOT wakes the scene. */
        if (gpio_get_level(GPIO_NUM_35) == 0) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            ++s_events.radio.presses;
            xSemaphoreGive(s_lock);
        }
        uint32_t duty = power == SCENE_OFF ? 0 : power == SCENE_DIM ? 900 : 6500;
        if (!snapshot.ready) duty = 6500;
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
void jc_panel_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock);
    jc_events_init(&s_events);
    scene_panel(PANEL_128X64);
    scene_reset();
    s_canvas = heap_caps_malloc(WIDTH * HEIGHT * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    configASSERT(s_canvas);
    const gpio_config_t button = {.pin_bit_mask = 1ULL << 35, .mode = GPIO_MODE_INPUT,
                                 .pull_up_en = GPIO_PULLUP_ENABLE};
    ESP_ERROR_CHECK(gpio_config(&button));
    const ledc_timer_config_t timer = {.speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_13_BIT, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK};
    const ledc_channel_config_t backlight = {.gpio_num = GPIO_NUM_23, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0, .duty = 0};
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ESP_ERROR_CHECK(ledc_channel_config(&backlight));
    static esp_ldo_channel_handle_t ldo;
    const esp_ldo_channel_config_t power = {.chan_id = 3, .voltage_mv = 2500};
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&power, &ldo));
    esp_lcd_dsi_bus_handle_t bus;
    const esp_lcd_dsi_bus_config_t dsi = JD9365_PANEL_BUS_DSI_2CH_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&dsi, &bus));
    esp_lcd_panel_io_handle_t io;
    const esp_lcd_dbi_io_config_t dbi = JD9365_PANEL_IO_DBI_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(bus, &dbi, &io));
    const esp_lcd_dpi_panel_config_t dpi = {
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT, .dpi_clock_freq_mhz = 63,
        .virtual_channel = 0, .in_color_format = LCD_COLOR_FMT_RGB565, .num_fbs = 1,
        .video_timing = {.h_size = WIDTH, .v_size = HEIGHT, .hsync_back_porch = 20,
            .hsync_pulse_width = 20, .hsync_front_porch = 40, .vsync_back_porch = 8,
            .vsync_pulse_width = 4, .vsync_front_porch = 20},
    };
    jd9365_vendor_config_t vendor = {.init_cmds = panel_registers,
        .init_cmds_size = sizeof(panel_registers) / sizeof(panel_registers[0]),
        .mipi_config = {.dsi_bus = bus, .dpi_config = &dpi, .lane_num = 2}};
    const esp_lcd_panel_dev_config_t panel = {.reset_gpio_num = GPIO_NUM_27,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, .bits_per_pixel = 16, .vendor_config = &vendor};
    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9365(io, &panel, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    configASSERT(xTaskCreatePinnedToCore(display_task, "panel", 8192, NULL, 1, NULL, 0) == pdPASS);
}
