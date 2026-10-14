#include "Motus/led.h"

#include <Arduino.h>
#include <PCA9685.h>

#include "Motus/config.h"

// weird pinrouting because of hardware
static const int ledEnum[NUM_SLIDERS * LEDS_PER_SLIDER] = {
    5, 4, 3, 2, 1, 0,
    11, 10, 9, 8, 7, 6,
    1, 0, 15, 14, 13, 12,
    7, 6, 5, 4, 3, 2,
    13, 12, 11, 10, 9, 8,
    3, 2, 1, 0, 15, 14,
    9, 8, 7, 6, 5, 4,
    15, 14, 13, 12, 11, 10};

static const int ledBus[NUM_SLIDERS * LEDS_PER_SLIDER] = {
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0,
    1, 1, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 1, 1,
    2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2};

// LED numeration for XY pad on hardware layout
// note: XY pad #2 is 180° turned
// 5-----7-----6
// |-----|-----|
// 0-----|-----4
// |-----|-----|
// 1-----2-----3
static const float padLedX[LEDS_PER_PAD] = {0, 0, 0.5, 1, 1, 0, 1, 0.5};
static const float padLedY[LEDS_PER_PAD] = {0.5, 0, 0, 0, 0.5, 1, 1, 1};

static const float PAD_LED_RADIUS = 0.8f; // reach of each LED, in pad widths
static const int PAD_LED_MAX = 2047;      // 12bit PWM value at the finger

static PCA9685 ledDriver[NUM_LED_DRIVERS] = {
    PCA9685(LED_DRIVER_ADDR[0], PCA9685_MODE_LED_DIRECT, 800.0),
    PCA9685(LED_DRIVER_ADDR[1], PCA9685_MODE_LED_DIRECT, 800.0),
    PCA9685(LED_DRIVER_ADDR[2], PCA9685_MODE_LED_DIRECT, 800.0),
    PCA9685(LED_DRIVER_ADDR[3], PCA9685_MODE_LED_DIRECT, 800.0)};

void ledSetup()
{
    // initialize LED drivers
    for (int i = 0; i < NUM_LED_DRIVERS; i++)
    {

        ledDriver[i].setup();

        for (int j = 0; j < LEDS_PER_DRIVER; j++)
        {
            ledDriver[i].getPin(j).fullOffAndWrite();
        }
    }
}

void setSliderLED(int slider_channel, int value)
{
    static const float div_const = 0.005847953216; // 10bit value divided into 6 parts (1024 / 170.66 )

    float divValue = (float)value * div_const;
    int fullLed = (int)divValue;
    float frac = divValue - fullLed;
    int dimLed = frac * (float)4095; // scale fractional part to 12bit PWM value
    int ledStart = slider_channel * LEDS_PER_SLIDER;
    int ledEnd = ledStart + fullLed;

    // clear slider leds
    for (int pos = ledStart; pos < (ledStart + LEDS_PER_SLIDER); pos++)
    {
        ledDriver[ledBus[pos]].getPin(ledEnum[pos]).fullOffAndWrite();
    }

    // write full slider leds
    // if (ledEnd <= ledStart + 5);
    // else ledEnd--;
    for (int pos = ledStart; pos < ledEnd; pos++)
    {
        ledDriver[ledBus[pos]].getPin(ledEnum[pos]).fullOnAndWrite();
    }

    // write dim slider leds
    if (ledEnd <= ledStart + 5)
        ledDriver[ledBus[ledEnd]].getPin(ledEnum[ledEnd]).setValueAndWrite(dimLed);
}

// x and y are pad-local, 10 bit
void setXYPadLED(int channel, int xAxis, int yAxis)
{
    // account pca pin offset in second XY pad
    int padLedOffset = 0;
    if (channel == NUM_SLIDERS + 1)
        padLedOffset = LEDS_PER_PAD;

    float x = constrain(xAxis, 0, 1023) / 1023.0f;
    float y = constrain(yAxis, 0, 1023) / 1023.0f;

    // each LED fades with its distance to the finger, so the LEDs around the
    // finger share the light and the center of the pad is a soft even glow
    for (int i = 0; i < LEDS_PER_PAD; i++)
    {
        float dx = x - padLedX[i];
        float dy = y - padLedY[i];
        float level = 1.0f - sqrtf(dx * dx + dy * dy) / PAD_LED_RADIUS;
        if (level < 0)
            level = 0;

        // squared for a more even perceived fade
        int value = level * level * PAD_LED_MAX;
        ledDriver[PAD_LED_DRIVER].getPin(i + padLedOffset).setValueAndWrite(value);
    }
}
