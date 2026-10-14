#include <Arduino.h>
#include <Wire.h>

#include "analogout.h"
#include "config.h"
#include "display.h"
#include "led.h"
#include "presets.h"
#include "recorder.h"
#include "sensors.h"

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

static const uint32_t LOOP_LED_INTERVAL_MS = 20; // LED updates of looping controls, to keep the I2C bus free

// the slider on the master outputs: the last one touched
static int masterSlider = -1;

static bool loopLedDue(int channel)
{
    static uint32_t lastLedMs[NUM_SENSORS];
    if (millis() - lastLedMs[channel] < LOOP_LED_INTERVAL_MS)
        return false;
    lastLedMs[channel] = millis();
    return true;
}

static void outputSlider(int channel, int position, int pressure, bool gate, bool updateLed)
{
    setGate(channel, gate);
    setSliderDAC(channel, position);
    setPressDAC(channel, pressure);
    if (updateLed)
        setSliderLED(channel, position);
    if (channel == masterSlider)
        setMaster(position, pressure);
}

// x and y are pad-local; the outputs are as seen from the playing position
static void outputPad(int channel, int x, int y, int pressure, bool updateLed)
{
    if (updateLed)
        setXYPadLED(channel, x, y);

    // INVERT BECAUSE OF CHANNEL LAYOUT (second pad is 180° turned)
    if (channel == NUM_SLIDERS + 1)
    {
        x = 1023 - x;
        y = 1023 - y;
    }
    setXYPadDAC(channel, x, y);
    setPressDAC(channel, pressure); // Z1 / Z2
}

// read one sensor and route its values, live or from a recording, to the
// outputs, LEDs and display; returns whether its gate is on
static bool updateChannel(int channel)
{
    TouchReading reading = sensorRead(channel);
    bool slider = isSlider(channel);

    if (reading.started && slider)
    {
        presetsSelect(channel);
        masterSlider = channel;
    }

    Playback playback;
    if (recorderChannel(channel, reading, playback))
    {
        bool led = loopLedDue(channel);
        if (slider)
            outputSlider(channel, playback.position, playback.pressure, playback.gate, led);
        else
            outputPad(channel, playback.position, playback.y, playback.pressure, led);
        return slider && playback.gate;
    }

    // on release the outputs are set once more, to the value from before lifting
    if (!reading.touched && !reading.released)
    {
        setGate(channel, false);
        setPressDAC(channel, 0);
        return false;
    }

    int pressure = reading.touched ? reading.pressure : 0;
    bool recording = recorderIsRecording(channel);
    if (slider)
    {
        outputSlider(channel, reading.position, pressure, reading.touched, true);
        displaySlider(channel, reading.position, pressure, recording);
    }
    else
    {
        outputPad(channel, reading.x, reading.y, pressure, true);
        bool flipped = channel == NUM_SLIDERS + 1;
        displayPad(channel, flipped ? 1023 - reading.x : reading.x, flipped ? 1023 - reading.y : reading.y, recording);
    }
    // usbMIDI.sendControlChange(channel, reading.position / 8, 1);
    return slider && reading.touched;
}

void loop()
{
    // master gate: on while any slider is touched or looping with its gate on
    bool anySliderGate = false;
    for (int channel = 0; channel < NUM_SENSORS; channel++)
    {
        bool gate = updateChannel(channel);
        if (isSlider(channel) && gate)
            anySliderGate = true;
    }
    setMasterGate(anySliderGate);

    uint8_t buttons = buttonsRead();
    presetsUpdate(buttons);
    recorderUpdate(buttons);
    displayUpdate();
}
