#ifndef MIDI_TASK_H
#define MIDI_TASK_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <array>

#define MIN_BRIGHTNESS 20
#define MAX_BRIGHTNESS 255

struct LEDCommand {
    int ledIndex;
    uint32_t color;
    uint8_t brightness;
    bool isNoteOn;
};

void init_midi_task(QueueHandle_t led_command_queue);

// Declare these functions here if they're used in midi_task.cpp
uint32_t interpolateColor(uint32_t color1, uint32_t color2, float ratio);
int mapNoteToLED(int note);

#endif // MIDI_TASK_H