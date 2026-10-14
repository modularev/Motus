#include <Arduino.h>
#include <Wire.h>

#include "Motus/analogout.h"
#include "Motus/config.h"
#include "Motus/display.h"
#include "Motus/led.h"
#include "Motus/presets.h"
#include "Motus/sensors.h"

void setup()
{
    Wire.begin();
    dacSetup();
    ledSetup();
    sensorSetup();
    displaySetup();
    presetsSetup();
    Wire.setClock(I2C_CLOCK_HZ);
}

// read one sensor and route its values to gate, DAC, LEDs and display;
// returns whether it is touched
static bool updateChannel(int channel)
{
    TouchReading reading = sensorRead(channel);
    setGate(channel, reading.touched);

    if (!reading.touched)
        setPressDAC(channel, 0);

    // on release the outputs are set once more, to the value from before lifting
    if (!reading.touched && !reading.released)
        return false;

    if (isSlider(channel))
    {
        if (reading.started)
            presetsSelect(channel);
        setSliderDAC(channel, reading.position);
        if (reading.touched)
            setPressDAC(channel, reading.pressure);
        setSliderLED(channel, reading.position);
        setMaster(reading.position, reading.pressure);
        displaySlider(channel, reading.position, reading.pressure);
    }
    else
    {
        setXYPadLED(channel, reading.x, reading.y);

        // INVERT BECAUSE OF CHANNEL LAYOUT (second pad is 180° turned)
        if (channel == NUM_SLIDERS + 1)
        {
            reading.x = 1023 - reading.x;
            reading.y = 1023 - reading.y;
        }
        setXYPadDAC(channel, reading.x, reading.y);
        if (reading.touched)
            setPressDAC(channel, reading.pressure); // Z1 / Z2
        displayPad(channel, reading.x, reading.y);
    }
    // usbMIDI.sendControlChange(channel, reading.position / 8, 1);
    return reading.touched;
}

void loop()
{
    bool anySliderTouched = false;
    for (int channel = 0; channel < NUM_SENSORS; channel++)
    {
        bool touched = updateChannel(channel);
        if (isSlider(channel) && touched)
            anySliderTouched = true;
    }
    setMasterGate(anySliderTouched);
    presetsUpdate(buttonsRead());
    displayUpdate();
}
