#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "tinyusb.h"
#include "tusb_midi.h"
#include "led_strip.h"

#define LED_STRIP_GPIO 48
#define LED_STRIP_NUM_LEDS 1
#define MIN_BRIGHTNESS 20
#define NUM_MIDI_NOTES 128
#define NUM_COLORS 7

static const char *TAG = "MIDI_LED";

led_strip_handle_t led_strip;
uint32_t colors[NUM_MIDI_NOTES];

const uint32_t colorSteps[NUM_COLORS] = {
    0xFF0000, 0xFF7F00, 0xFFFF00, 0x00FF00, 0x0000FF, 0x4B0082, 0x9400D3
};

uint32_t interpolateColor(uint32_t color1, uint32_t color2, float ratio) {
    uint8_t r1 = (color1 >> 16) & 0xFF;
    uint8_t g1 = (color1 >> 8) & 0xFF;
    uint8_t b1 = color1 & 0xFF;
    uint8_t r2 = (color2 >> 16) & 0xFF;
    uint8_t g2 = (color2 >> 8) & 0xFF;
    uint8_t b2 = color2 & 0xFF;
    
    uint8_t r = r1 * (1 - ratio) + r2 * ratio;
    uint8_t g = g1 * (1 - ratio) + g2 * ratio;
    uint8_t b = b1 * (1 - ratio) + b2 * ratio;
    
    return (r << 16) | (g << 8) | b;
}

void initialize_colors() {
    for (int i = 0; i < NUM_MIDI_NOTES; i++) {
        if (i < 21 || i > 108) {
            colors[i] = 0;
        } else {
            int colorIndex = ((i - 21) * (NUM_COLORS - 1)) / (108 - 21);
            float ratio = ((i - 21) % ((108 - 21) / (NUM_COLORS - 1))) / (float)((108 - 21) / (NUM_COLORS - 1));
            colors[i] = interpolateColor(colorSteps[colorIndex], colorSteps[(colorIndex + 1) % NUM_COLORS], ratio);
        }
    }
}

void initialize_led_strip() {
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = LED_STRIP_NUM_LEDS,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
}

void update_led(uint8_t note, uint8_t velocity, bool note_on) {
    uint32_t color = colors[note];
    
    if (note_on && velocity > 0) {
        uint8_t brightness = (velocity * (255 - MIN_BRIGHTNESS) / 127) + MIN_BRIGHTNESS;
        uint8_t r = (((color >> 16) & 0xFF) * brightness / 255);
        uint8_t g = (((color >> 8) & 0xFF) * brightness / 255);
        uint8_t b = ((color & 0xFF) * brightness / 255);
        
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, r, g, b));
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));
        
        ESP_LOGI(TAG, "Note On: %d, Velocity: %d, Color: #%06X, Brightness: %d", note, velocity, color, brightness);
    } else {
        ESP_ERROR_CHECK(led_strip_clear(led_strip));
        ESP_LOGI(TAG, "Note Off: %d", note);
    }
}

void midi_task(void *param) {
    while (1) {
        uint8_t packet[4];
        if (tud_midi_packet_read(packet)) {
            uint8_t cable_num = packet[0] >> 4;
            uint8_t code_index_num = packet[0] & 0xF;
            uint8_t midi_status = packet[1];
            uint8_t note = packet[2];
            uint8_t velocity = packet[3];

            if (code_index_num == MIDI_CIN_NOTE_ON) {
                update_led(note, velocity, true);
            } else if (code_index_num == MIDI_CIN_NOTE_OFF) {
                update_led(note, velocity, false);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "USB MIDI LED Strip Example");

    initialize_colors();
    initialize_led_strip();

    ESP_LOGI(TAG, "USB initialization");
    tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = NULL,
        .external_phy = false,
        .configuration_descriptor = NULL,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    xTaskCreate(midi_task, "midi_task", 4096, NULL, 5, NULL);
}