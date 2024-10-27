#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "led_strip.h"
#include "usb/usb_host.h"

static const char *TAG = "USB_MIDI_LED";

#define LED_PIN 12
#define MIN_BRIGHTNESS 20
#define MAX_BRIGHTNESS 255

#define NUM_MIDI_NOTES 88
#define NUM_COLORS 7
#define NUM_LEDS 93  // Define the number of LEDs in the strip
#define KEYBOARD_LENGTH 1220  // Define the length of the keyboard in mm

static led_strip_handle_t led_strip;
static usb_host_client_handle_t client_hdl;
static bool device_connected = false;
static uint8_t dev_addr = 0;
static usb_device_handle_t dev_hdl; // Declare globally


const uint32_t colorSteps[NUM_COLORS] = {
    0xFF0000, 0xFF7F00, 0xFFFF00, 0x00FF00, 0x0000FF, 0x4B0082, 0x9400D3
};

uint32_t colors[NUM_MIDI_NOTES];

// Function declarations
void init_led_strip(void);
void init_colors(void);
void update_led(uint8_t note, uint8_t velocity);
void midi_task(void *arg);
void usb_lib_task(void *arg);
void open_dev(uint8_t dev_addr);  // Add this prototype above its usage


// Interpolates between two RGB colors
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
        // .led_pixel_format = LED_PIXEL_FORMAT_GRB,
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

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    if (event_msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        ESP_LOGI(TAG, "New device connected, address %d", event_msg->new_dev.address);
        device_connected = true;
        dev_addr = event_msg->new_dev.address;
        open_dev(dev_addr);  // Call to open_dev here
    } else if (event_msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        ESP_LOGI(TAG, "Device disconnected");
        device_connected = false;
        dev_addr = 0;
    }
}

void open_dev(uint8_t dev_addr)
{
    esp_err_t err = usb_host_device_open(client_hdl, dev_addr, &dev_hdl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open device: %s", esp_err_to_name(err));
        return;
    }

    // Here you would typically claim the interface and set up endpoints
}



void midi_task(void *arg)
{
    TickType_t last_wake_time = xTaskGetTickCount();
    while (1) {
        if (device_connected) {
            // Create a transfer
            usb_transfer_t *transfer = NULL;
            esp_err_t ret = usb_host_transfer_alloc(128, 0, &transfer);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to allocate transfer");
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }

            // Set up the transfer
            transfer->num_bytes = 4; // MIDI packet size
            transfer->bEndpointAddress = 0x81; // Adjust this based on your MIDI device
            transfer->device_handle = dev_hdl; // Use the global dev_hdl

            // Submit the transfer
            ret = usb_host_transfer_submit(transfer);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to submit transfer");
                usb_host_transfer_free(transfer);
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }

            // Wait for the transfer to complete
            ret = usb_host_client_handle_events(client_hdl, portMAX_DELAY);
            if (ret == ESP_OK) {
                // Process the MIDI data
                uint8_t *data = transfer->data_buffer;
                uint8_t status = data[0] & 0xF0;
                uint8_t note = data[1];
                uint8_t velocity = data[2];
                
                ESP_LOGI(TAG, "MIDI packet received: status=0x%02x, note=%d, velocity=%d", status, note, velocity);
                
                if (status == 0x90 && velocity > 0) {
                    update_led(note, velocity);
                } else if ((midi_status & 0xF0) == 0x80) {  // Note Off
                    led_strip_clear(led_strip);
                    led_strip_refresh(led_strip);
                }
            }

            // Free the transfer
            usb_host_transfer_free(transfer);
        }
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(10));
    }
}


void usb_host_task(void *arg)
{
    while (1) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        
        // Handle any USB host events here if needed
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "USB MIDI LED Example");
    
    init_led_strip();
    init_colors();
    
    ESP_LOGI(TAG, "USB initialization");
    
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));
    
    usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = NULL,
        }
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &client_hdl));
    
    xTaskCreate(midi_task, "midi_task", 4096 * 2, NULL, 5, NULL);
    
    // Start the FreeRTOS scheduler
    // vTaskStartScheduler();
}