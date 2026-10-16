#include <Arduino.h>
#include <Wire.h>

#include "analogout.h"
#include "config.h"
#include "display.h"
#include "led.h"
#include "menu.h"
#include "modes.h"
#include "presets.h"
#include "recorder.h"
#include "sensors.h"

#ifdef MOTUS_DAC_TEST
void dacTest();
#endif

void setup()
{
    Wire.begin();
    dacSetup();
    ledSetup();
    sensorSetup();
    displaySetup();
    Wire.setClock(I2C_CLOCK_HZ);
#ifdef MOTUS_DAC_TEST
    dacTest();
#endif
    presetsSetup();
    modesSetup();
}

static const uint32_t LED_INTERVAL_MS = 20; // LEDs are redrawn 50 times a second

// last position of every control, drawn by updateLeds
static int ledX[NUM_SENSORS];
static int ledY[NUM_SENSORS];
static bool padLit[NUM_SENSORS]; // pads stay dark until first touched

// Master outputs: the average of all sliders that are touched or looping,
// so they stay in range; with none active they keep their last value
struct MasterInput
{
    bool active;
    int code;     // position output, 12 bit
    int pressure; // 0-511
};
static MasterInput masterInput[NUM_SLIDERS];


static void outputSlider(int channel, int position, int pressure, bool gate)
{
    ledX[channel] = position;
    int code = modesSliderOutput(channel, position, gate);
    masterInput[channel] = {true, code, pressure};
    setGate(channel, gate);
    setPressDAC(channel, pressure);
}

// x and y are pad-local; the outputs are as seen from the playing position
static void outputPad(int channel, int x, int y, int pressure)
{
    ledX[channel] = x;
    ledY[channel] = y;
    padLit[channel] = true;

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

    // with the menu open a touch only selects, so a loop keeps playing
    Playback playback;
    Source source = recorderChannel(channel, reading, !menuActive(), playback);

    if (source == SOURCE_NONE)
    {
        // the touch stopped a loop: outputs stay quiet until it lifts
        setGate(channel, false);
        setPressDAC(channel, 0);
        if (slider)
            masterInput[channel].active = false;
        return false;
    }

    if (reading.started && slider)
    {
        presetsSelect(channel);
        menuSelect(channel);
    }

    if (source == SOURCE_LOOP)
    {
        if (slider)
            outputSlider(channel, playback.position, playback.pressure, playback.gate);
        else
            outputPad(channel, playback.position, playback.y, playback.pressure);
        return slider && playback.gate;
    }

    // on release the outputs are set once more, to the value from before lifting
    if (!reading.touched && !reading.released)
    {
        setGate(channel, false);
        setPressDAC(channel, 0);
        if (slider)
            masterInput[channel].active = false;
        return false;
    }

    int pressure = reading.touched ? reading.pressure : 0;
    bool recording = recorderIsRecording(channel);
    if (slider)
    {
        outputSlider(channel, reading.position, pressure, reading.touched);
        masterInput[channel].active = reading.touched;
        displaySlider(channel, reading.position, pressure, recording, modesSliderText(channel));
    }
    else
    {
        outputPad(channel, reading.x, reading.y, pressure);
        bool flipped = channel == NUM_SLIDERS + 1;
        displayPad(channel, flipped ? 1023 - reading.x : reading.x, flipped ? 1023 - reading.y : reading.y, recording);
    }
    // usbMIDI.sendControlChange(channel, reading.position / 8, 1);
    return slider && reading.touched;
}

// LEDs: the bar or glow of every control, with the recording blinking, a
// flash at every loop start, the slider being edited breathing while the menu
// is open, and the top LEDs blinking while recording is armed
static void updateLeds()
{
    static elapsedMillis sinceDraw;
    if (sinceDraw < LED_INTERVAL_MS)
        return;
    sinceDraw = 0;

    uint32_t now = millis();
    bool blinkFast = (now / 125) % 2 == 0;
    bool blinkSlow = (now / 500) % 2 == 0;
    float breath = 0.2f + 0.8f * (0.5f - 0.5f * cosf(2 * PI * now / 2000.0f));

    for (int channel = 0; channel < NUM_SENSORS; channel++)
    {
        float brightness = 1.0f;
        bool flash = false;
        if (recorderIsRecording(channel))
            brightness = blinkFast ? 1.0f : 0.0f;
        else if (recorderLoopStart(channel))
            flash = true;
        else if (menuActive() && channel == menuSelected())
            brightness = breath;

        if (isSlider(channel))
        {
            int top = recorderArmed() ? (blinkSlow ? 1 : 0) : -1;
            bool note = sliderSettings[channel].mode == MODE_NOTE;
            if (flash)
                setSliderLED(channel, 1023, brightness, top);
            else if (note)
                setSliderLED(channel, modesNotePosition(channel), brightness, top, true);
            else
                setSliderLED(channel, ledX[channel], brightness, top);
        }
        else
            setXYPadLED(channel, ledX[channel], ledY[channel], padLit[channel] ? brightness : 0.0f, flash);
    }
}

static void updateMaster()
{
    int count = 0;
    long code = 0;
    long pressure = 0;
    for (int i = 0; i < NUM_SLIDERS; i++)
    {
        if (!masterInput[i].active)
            continue;
        count++;
        code += masterInput[i].code;
        pressure += masterInput[i].pressure;
    }
    if (count > 0)
        setMaster(code / count, pressure / count);
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
    modesUpdate();
    updateMaster();
    updateLeds();

    uint8_t buttons = buttonsRead();
    menuUpdate(buttons);
    recorderUpdate(buttons);
    displayUpdate();
}
