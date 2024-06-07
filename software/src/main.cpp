#include <Arduino.h>
#include <Encoder.h>
#include <LiquidCrystal.h>
#include <Wire.h>
#include "sensor-processing.h"

void setup()
{
    Wire.begin();
    dac_setup();
    led_setup();
    sensorSetup();
    display_setup();
    Wire.setClock(3400000);

    for (int i = 0; i < 9; i++)
    {
        pinMode(gate_pin[i], OUTPUT);
        digitalWrite(gate_pin[i], HIGH);
    }
}

void loop()
{
    enc_update();
    sensorCheck();
}