#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "led_strip.h"
#include "nvs_flash.h"
#include "midi_task.h"

#define RMT_LED_STRIP_RESOLUTION_HZ 10000000 // 10MHz resolution
#define DATA_PIN 6
#define NUM_LEDS 88

#define MIN_BRIGHTNESS 20
#define MAX_BRIGHTNESS 255

static const char *TAG = "MIDI_LED_STRIP";

// LED strip handle
led_strip_handle_t led_strip;

// Queue handle for LED commands
QueueHandle_t ledCommandQueue;

// Function prototypes
void TaskLEDControl(void *pvParameters);
uint32_t interpolateColor(uint32_t color1, uint32_t color2, float ratio);
int mapNoteToLED(int note);
void updateSurroundingLED(int ledIndex, uint32_t color, uint8_t brightness, int distance);

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // LED strip configuration
    led_strip_config_t strip_config = {
        .strip_gpio_num = DATA_PIN,
        .max_leds = NUM_LEDS,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags = {
            .invert_out = false
        }
    };

    // RMT configuration for LED strip
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
        .flags = {
            .with_dma = true
        }
    };

    // Create LED strip
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));

    // Create the LED command queue
    ledCommandQueue = xQueueCreate(10, sizeof(LEDCommand));

    // Initialize MIDI task
    init_midi_task(ledCommandQueue);

    // Create LED control task
    xTaskCreate(TaskLEDControl, "LED Control", 4096, NULL, 4, NULL);
}

void TaskLEDControl(void *pvParameters)
{
    (void) pvParameters;

    for (;;) {
        LEDCommand cmd;
        if (xQueueReceive(ledCommandQueue, &cmd, portMAX_DELAY) == pdPASS) {
            if (cmd.isNoteOn) {
                led_strip_set_pixel(led_strip, cmd.ledIndex, (cmd.color >> 16) & 0xFF, (cmd.color >> 8) & 0xFF, cmd.color & 0xFF);
                for (int i = 1; i <= 2; i++) {
                    if (cmd.ledIndex - i >= 0) updateSurroundingLED(cmd.ledIndex - i, cmd.color, cmd.brightness, i);
                    if (cmd.ledIndex + i < NUM_LEDS) updateSurroundingLED(cmd.ledIndex + i, cmd.color, cmd.brightness, i);
                }
            } else {
                led_strip_set_pixel(led_strip, cmd.ledIndex, 0, 0, 0);
                for (int i = 1; i <= 2; i++) {
                    if (cmd.ledIndex - i >= 0) led_strip_set_pixel(led_strip, cmd.ledIndex - i, 0, 0, 0);
                    if (cmd.ledIndex + i < NUM_LEDS) led_strip_set_pixel(led_strip, cmd.ledIndex + i, 0, 0, 0);
                }
            }

            // Refresh the strip to send data to LEDs
            led_strip_refresh(led_strip);
        }
    }
}

uint32_t interpolateColor(uint32_t color1, uint32_t color2, float ratio)
{
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

int mapNoteToLED(int note)
{
    if (note < 21 || note > 108) return -1;
    float notePosition = (note - 21) / 87.0;
    return int(notePosition * NUM_LEDS);
}

void updateSurroundingLED(int ledIndex, uint32_t color, uint8_t brightness, int distance)
{
    float dimFactor = 1.0 / (distance + 1);
    uint8_t dimmedBrightness = brightness * dimFactor;

    uint8_t r = ((color >> 16) & 0xFF) * dimmedBrightness / 255;
    uint8_t g = ((color >> 8) & 0xFF) * dimmedBrightness / 255;
    uint8_t b = (color & 0xFF) * dimmedBrightness / 255;

    led_strip_set_pixel(led_strip, ledIndex, r, g, b);
    ESP_LOGI(TAG, "Surrounding LED: %d, Color: RGB(%d, %d, %d), Dimming Factor: %.2f", ledIndex, r, g, b, dimFactor);
}