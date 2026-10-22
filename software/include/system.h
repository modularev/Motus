#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>

// Global settings, edited on the SYS page and stored with SAVE

struct SystemSettings
{
    uint8_t smooth;   // 1 light - 10 heavy smoothing of slow finger movement
    uint8_t deadband; // counts a resting finger can wobble without moving the output, 0-20
    uint8_t pressure; // percent: how hard counts as a firm press, 50-200
    uint8_t touch;    // percent: touch sensitivity, 50-200, higher reacts to lighter touches
    uint8_t bright;   // percent: LED brightness, 10-100
    uint8_t trigMs;   // trigger length, 1-20 ms
    uint8_t loopTrig; // a gate held across the loop start drops briefly: 0 off, 1 on
    uint8_t autosave; // the current slot is saved after a pause in playing: 0 off, 1 on
};

extern SystemSettings systemSettings;

// the single save of earlier firmware, read once when converting it
void systemLoadLegacy();

#endif
