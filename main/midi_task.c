#include "midi_task.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "midi.h"
#include "tusb_midi.h"

static const char *TAG = "MIDI_TASK";

static QueueHandle_t led_queue;

static const int NUM_MIDI_NOTES = 128;
static const int NUM_COLORS = 7;
static std::array<uint32_t, NUM_MIDI_NOTES> colors;

static const uint32_t colorSteps[NUM_COLORS] = {
    0xFF0000, 0xFFA500, 0xFFFF00, 0x00FF00, 0x0000FF,
    0x4B0082, // Indigo
    0x9400D3  // Violet
};

static void initialize_colors() {
    for (int i = 0; i < NUM_MIDI_NOTES; i++) {
        if (i < 21 || i > 108) {
            colors[i] = 0;
        } else {
            int colorIndex = ((i - 21) * (NUM_COLORS - 1)) / (108 - 21);
            float ratio = ((i - 21) % ((108 - 21) / (NUM_COLORS - 1))) / float((108 - 21) / (NUM_COLORS - 1));
            colors[i] = interpolateColor(colorSteps[colorIndex], colorSteps[(colorIndex + 1) % NUM_COLORS], ratio);
        }
    }
}

static void midi_task(void *pvParameters) {
    ESP_LOGI(TAG, "MIDI task started");

    while (1) {
        uint8_t packet[4];
        if (tinyusb_midi_receive(packet, 4, pdMS_TO_TICKS(1))) {
            uint8_t status = packet[0] & 0xF0;
            uint8_t channel = packet[0] & 0x0F;
            uint8_t note = packet[2];
            uint8_t velocity = packet[3];

            if (status == 0x90 || status == 0x80) { // Note On or Note Off
                int ledIndex = mapNoteToLED(note);
                if (ledIndex != -1) {
                    LEDCommand cmd;
                    cmd.ledIndex = ledIndex;
                    cmd.color = colors[note];
                    cmd.brightness = (velocity * (MAX_BRIGHTNESS - MIN_BRIGHTNESS) / 127) + MIN_BRIGHTNESS;
                    cmd.isNoteOn = (status == 0x90 && velocity > 0);

                    xQueueSend(led_queue, &cmd, portMAX_DELAY);
                }
            }
        }
        vTaskDelay(1); // Small delay to prevent task starvation
    }
}



void init_midi_task(QueueHandle_t led_command_queue) {
    led_queue = led_command_queue;

    // Initialize TinyUSB
    ESP_LOGI(TAG, "USB initialization");
    tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = NULL,
        .external_phy = false,
        .configuration_descriptor = NULL,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    initialize_colors();

    xTaskCreate(midi_task, "MIDI Task", 4096, NULL, 5, NULL);
}