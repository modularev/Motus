// led.h
#ifndef LED_H
#define LED_H

#include "PCA9685.h"

extern PCA9685 ledDriver[];
extern uint16_t padLedVal[];

const int ledEnum[48] = {
    5, 4, 3, 2, 1, 0,
    11, 10, 9, 8, 7, 6,
    1, 0, 15, 14, 13, 12,
    7, 6, 5, 4, 3, 2,
    13, 12, 11, 10, 9, 8,
    3, 2, 1, 0, 15, 14,
    9, 8, 7, 6, 5, 4,
    15, 14, 13, 12, 11, 10};

const int ledBus[48] = {
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0,
    1, 1, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 1, 1,
    2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2};

// weird pinrouting because of hardware

// LED numeration for XY pad on hardware layout
// note: XY pad #2 is 180° turned
// 5-----7-----6
// |-----|-----|
// 0-----|-----4
// |-----|-----|
// 1-----2-----3

void led_setup();
void setSliderLED(int slider_channel, int value);
void setXYPadLED(int mpr_channel, int x_value, int y_value);

#endif