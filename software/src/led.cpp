// led.cpp
#include "led.h"

uint16_t padLedVal[8] = {0, 0, 0, 0, 0, 0, 0, 0};

PCA9685 ledDriver[4] = {
    PCA9685(0x40, PCA9685_MODE_LED_DIRECT, 800.0),
    PCA9685(0x41, PCA9685_MODE_LED_DIRECT, 800.0),
    PCA9685(0x42, PCA9685_MODE_LED_DIRECT, 800.0),
    PCA9685(0x43, PCA9685_MODE_LED_DIRECT, 800.0)};

void led_setup()
{
    // initialize LED drivers
    for (int i = 0; i < 4; i++)
    {

        ledDriver[i].setup();

        for (int j = 0; j < 16; j++)
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
    int ledStart = slider_channel * 6;
    int ledEnd = ledStart + fullLed;

    // clear slider leds
    for (int pos = ledStart; pos < (ledStart + 6); pos++)
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

void setXYPadLED(int sensorChan, int xAxis, int yAxis)
{
    // account pca pin offset in second XY pad
    int padLedOffset = 0;
    if (sensorChan == 9)
        padLedOffset = 8;

    memset(padLedVal, 0, sizeof(padLedVal));

    // bottom left
    if (xAxis < 256 && yAxis < 256)
    {
        padLedVal[1] = (255 - xAxis) * (255 - yAxis);
        padLedVal[0] = (255 - xAxis) * (yAxis);
        padLedVal[2] = (xAxis) * (255 - yAxis);
    }

    // top left
    else if (xAxis < 256 && yAxis > 255)
    {
        yAxis = (yAxis >> 1);

        padLedVal[5] = (255 - xAxis) * (yAxis);
        padLedVal[0] = (255 - xAxis) * (255 - yAxis);
        padLedVal[7] = (xAxis) * (yAxis);
    }

    // bottom right
    else if (xAxis > 255 && yAxis < 256)
    {
        xAxis = (xAxis >> 1);

        padLedVal[3] = (xAxis) * (255 - yAxis);
        padLedVal[4] = (xAxis) * (yAxis);
        padLedVal[2] = (255 - xAxis) * (255 - yAxis);
    }

    // top right
    else if (xAxis > 255 && yAxis > 255)
    {
        xAxis = (xAxis >> 1);
        yAxis = (yAxis >> 1);

        padLedVal[6] = (xAxis) * (yAxis);
        padLedVal[4] = (255 - xAxis) * (255 - yAxis);
        padLedVal[7] = (xAxis) * (yAxis);
    }

    for (int i = 0; i < 8; i++)
    {
        ledDriver[3].getPin(i + padLedOffset).setValueAndWrite(padLedVal[i] >> 5);
    }
}
