#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "led_strip.h"
#include "tinyusb.h"
#include "class/midi/midi_device.h"
#include "esp_mac.h"

// At the top of your file, after the includes
extern const tusb_desc_device_t device_descriptor;
extern const uint8_t configuration_descriptor[];
extern const char *string_descriptor[];

static const char *TAG = "USB_MIDI_LED";

#define LED_PIN GPIO_NUM_48
#define MIN_BRIGHTNESS 20
#define MAX_BRIGHTNESS 255

#define NUM_MIDI_NOTES 128
#define NUM_COLORS 7

static led_strip_handle_t led_strip;

// Color steps (RGB values)
const uint32_t colorSteps[NUM_COLORS] = {
    0xFF0000, 0xFF7F00, 0xFFFF00, 0x00FF00, 0x0000FF, 0x4B0082, 0x9400D3
};

uint32_t colors[NUM_MIDI_NOTES];

// Function declarations
void init_led_strip(void);
void init_colors(void);
void update_led(uint8_t note, uint8_t velocity);
void midi_task(void *arg);

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

void init_led_strip(void) {
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_PIN,
        .max_leds = 1,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags = {
            .invert_out = false
        }
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags = {
            .with_dma = false
        }
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip);
}

void init_colors(void) {
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

void update_led(uint8_t note, uint8_t velocity) {
    uint32_t color = colors[note];
    uint8_t brightness = (velocity * (MAX_BRIGHTNESS - MIN_BRIGHTNESS) / 127) + MIN_BRIGHTNESS;
    
    uint8_t r = (((color >> 16) & 0xFF) * brightness / MAX_BRIGHTNESS);
    uint8_t g = (((color >> 8) & 0xFF) * brightness / MAX_BRIGHTNESS);
    uint8_t b = ((color & 0xFF) * brightness / MAX_BRIGHTNESS);
    
    ESP_LOGD(TAG, "Updating LED: r=%d, g=%d, b=%d", r, g, b);
    led_strip_set_pixel(led_strip, 0, r, g, b);
    led_strip_refresh(led_strip);
    
    ESP_LOGI(TAG, "Note: %d, Velocity: %d, Color: #%06" PRIx32 ", Brightness: %d", note, velocity, color, brightness);
}

void midi_task(void *arg) {
    TickType_t last_wake_time = xTaskGetTickCount();
    while (1) {
        ESP_LOGD(TAG, "MIDI task loop start");
        if (tud_midi_available()) {
            uint8_t packet[4];
            if (tud_midi_packet_read(packet)) {
                uint8_t status = packet[1] & 0xF0;
                uint8_t note = packet[2];
                uint8_t velocity = packet[3];
                
                ESP_LOGI(TAG, "MIDI packet received: status=0x%02x, note=%d, velocity=%d", status, note, velocity);
                
                if (status == 0x90 && velocity > 0) {
                    update_led(note, velocity);
                } else if (status == 0x80 || (status == 0x90 && velocity == 0)) {
                    led_strip_clear(led_strip);
                    led_strip_refresh(led_strip);
                    ESP_LOGI(TAG, "Note Off: %d", note);
                }
            }
        }
        ESP_LOGD(TAG, "MIDI task loop end");
        // Use vTaskDelayUntil for more precise timing
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(10));
    }
}

// USB Device Descriptor
const tusb_desc_device_t device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A,  // This is a test VID. Replace with your own if you have one.
    .idProduct = 0x4000, // This is a test PID. Replace with your own if you have one.
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01
};

// USB Configuration Descriptor
const uint8_t configuration_descriptor[] = {
    // Configuration number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN), 0x00, 100),

    // Interface number, string index, EP Out & EP In address, EP size
    TUD_MIDI_DESCRIPTOR(0, 0, 0x01, 0x81, 64)
};

// String Descriptors
const char *string_descriptor[] = {
    (const char[]) { 0x09, 0x04 }, // 0: is supported language is English (0x0409)
    "TinyUSB",                     // 1: Manufacturer
    "TinyUSB MIDI",                // 2: Product
    "123456",                      // 3: Serials
};

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "USB MIDI LED Example");
    
    init_led_strip();
    init_colors();
    
    ESP_LOGI(TAG, "USB initialization");
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = &device_descriptor,
        .string_descriptor = string_descriptor,
        .string_descriptor_count = sizeof(string_descriptor) / sizeof(string_descriptor[0]),
        .external_phy = false,
        .configuration_descriptor = configuration_descriptor,
        .self_powered = true,
        .vbus_monitor_io = 0
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    
    xTaskCreate(midi_task, "midi_task", 4096 * 2, NULL, 5, NULL);
}