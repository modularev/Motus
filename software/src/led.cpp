#include "led.h"

#include <Arduino.h>
#include <PCA9685.h>

#include "config.h"

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

// Last value written to every LED: 0 off, 4096 full on, else 12 bit PWM.
// Only changed values are written, so lit LEDs are never switched off and
// on again between scans, which would flicker.
static const uint16_t LED_FULL_ON = 4096;
static const uint16_t LED_UNKNOWN = 0xFFFF;
static uint16_t ledValue[NUM_LED_DRIVERS][LEDS_PER_DRIVER];

static void writeLed(int driver, int pin, uint16_t value)
{
    if (ledValue[driver][pin] == value)
        return;
    ledValue[driver][pin] = value;

    if (value == 0)
        ledDriver[driver].getPin(pin).fullOffAndWrite();
    else if (value >= LED_FULL_ON)
        ledDriver[driver].getPin(pin).fullOnAndWrite();
    else
        ledDriver[driver].getPin(pin).setValueAndWrite(value);
}

void ledSetup()
{
    // initialize LED drivers
    for (int i = 0; i < NUM_LED_DRIVERS; i++)
    {

        ledDriver[i].setup();

        for (int j = 0; j < LEDS_PER_DRIVER; j++)
        {
            ledValue[i][j] = LED_UNKNOWN;
            writeLed(i, j, 0);
        }
    }
}

void setSliderLED(int slider_channel, int value, float brightness, int top, bool dot)
{
    static const float div_const = 0.005847953216; // 10bit value divided into 6 parts (1024 / 170.66 )

    float divValue = (float)value * div_const;
    int fullLed = value >= 1023 ? LEDS_PER_SLIDER : (int)divValue;
    float frac = divValue - (int)divValue;
    int dimLed = frac * (float)4095; // scale fractional part to 12bit PWM value
    int ledStart = slider_channel * LEDS_PER_SLIDER;

    // dot: the two LEDs around the position share the light
    float dotAt = constrain(value, 0, 1023) / 1023.0f * (LEDS_PER_SLIDER - 1);

    // bar: full LEDs below the position, one dimmed LED at it, the rest off
    for (int i = 0; i < LEDS_PER_SLIDER; i++)
    {
        float level = i < fullLed ? 4095 : (i == fullLed ? dimLed : 0);
        if (dot)
            level = max(0.0f, 1.0f - fabsf(i - dotAt)) * 4095;
        if (i == LEDS_PER_SLIDER - 1 && top >= 0)
            level = top ? 4095 : 0;
        else
            level *= brightness;

        int pos = ledStart + i;
        writeLed(ledBus[pos], ledEnum[pos], level >= 4095 ? LED_FULL_ON : (uint16_t)level);
    }
}

// x and y are pad-local, 10 bit
void setXYPadLED(int channel, int xAxis, int yAxis, float brightness, bool all)
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
        float level = all ? 1.0f : 1.0f - sqrtf(dx * dx + dy * dy) / PAD_LED_RADIUS;
        if (level < 0)
            level = 0;

        // squared for a more even perceived fade
        int value = level * level * PAD_LED_MAX * brightness;
        writeLed(PAD_LED_DRIVER, i + padLedOffset, value);
    }
}
