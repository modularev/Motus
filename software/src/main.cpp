#include <Arduino.h>
#include <EEPROM.h>
#include <Wire.h>

#include "analogout.h"
#include "config.h"
#include "display.h"
#include "led.h"
#include "menu.h"
#include "modes.h"
#include "presets.h"
#include "recorder.h"
#include "sequencer.h"
#include "sensors.h"
#include "storage.h"

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
#ifdef MOTUS_DEBUG
    // E,<address>,<32 bytes in hex>: the EEPROM before anything is loaded or converted
    while (!Serial && millis() < 5000)
        ;
    for (int address = 0; address < EEPROM.length(); address += 32)
    {
        Serial.printf("E,%d", address);
        for (int b = 0; b < 32 && address + b < EEPROM.length(); b++)
            Serial.printf(",%02X", EEPROM.read(address + b));
        Serial.println();
    }
#endif
    storageSetup();
    presetsSetup();
    modesSetup();
    // the slot loaded at start, for a second
    char slot[9];
    snprintf(slot, sizeof(slot), "slot %u", (unsigned)(storageCurrentSlot() + 1) % 10);
    displayMessage("MOTUS", slot);
}

static const uint32_t LED_INTERVAL_MS = 20; // LEDs are redrawn 50 times a second
static const float STEP_VIEW_DIM = 0.3f;     // stages other than the current one in a step view
static const float WINDOW_LEVEL = 0.4f;      // stages inside the window while shifting it

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

// gate output, unless it is a trigger, which the modes write on time themselves
static void writeGate(int channel, bool gate)
{
    if (isSlider(channel) && modesOwnsGate(channel))
        return;
    setGate(channel, gate);
}

static void outputSlider(int channel, int position, int pressure, bool gate)
{
    ledX[channel] = position;
    int code = modesSliderOutput(channel, position, pressure, gate);
    masterInput[channel] = {true, code, pressure};
    writeGate(channel, gate);
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

    // A touch while a SET pad is held only selects the slider for the menu:
    // until the finger lifts it is hidden from everything else, so no value,
    // gate or preset changes, nothing is recorded and a loop keeps playing.
    static bool selectOnly[NUM_SLIDERS];
    if (slider && reading.started && menuPadHeld())
    {
        selectOnly[channel] = true;
        menuSelect(channel);
    }
    if (slider && selectOnly[channel])
    {
        if (!reading.touched)
            selectOnly[channel] = false;
        reading = TouchReading{};
    }

    // with the menu open a touch only selects, so a loop keeps playing
    Playback playback;
    Source source = recorderChannel(channel, reading, !menuActive(), playback);

    if (source == SOURCE_NONE)
    {
        // the touch stopped a loop: outputs stay quiet until it lifts
        bool gate = slider && modesGate(channel, false);
        writeGate(channel, gate);
        setPressDAC(channel, 0);
        if (slider)
            masterInput[channel].active = false;
        return gate;
    }

    if (reading.started && slider)
    {
        sequencerTouch(channel);
        menuSelect(channel);
    }

    // WRITE: a touched slider's value goes into the current row of its stage,
    // until the encoder edits it while the slider is held
    if (slider && sequencerWriteRow() >= 0)
    {
        if (reading.started || reading.touched)
            menuWriteTouch(channel, true);
        bool edited = menuWriteEdited(channel);
        if ((reading.touched || reading.released) && !edited)
            presetSetValue(channel, sequencerWriteRow(), modesValueAt(channel, reading.position));
        if (!reading.touched)
            menuWriteTouch(channel, false);
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
        bool gate = slider && modesGate(channel, false);
        writeGate(channel, gate);
        setPressDAC(channel, 0);
        if (slider)
            masterInput[channel].active = false;
        return gate;
    }

    int pressure = reading.touched ? reading.pressure : 0;
    bool recording = recorderIsRecording(channel);
    if (slider)
    {
        outputSlider(channel, reading.position, pressure, reading.touched);
        masterInput[channel].active = reading.touched;
        if (sequencerWriteRow() >= 0)
        {
            // WRITE: the row and the value written, e.g. "S3 V1" over "C#2 2.08"
            char top[9];
            snprintf(top, sizeof(top), "S%u V%u", (unsigned)(channel + 1) % 10, (unsigned)(sequencerWriteRow() + 1) % 10);
            displaySliderText(channel, top, modesValueText(channel, presetValue(channel, sequencerWriteRow())));
        }
        else
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
        else if (sequencerWriteRow() >= 0)
        {
            // step view: the current step stands out, the stage shown breathes
            if (channel == sequencerStage())
                brightness = 1.0f;
            else if (channel == menuSelected())
                brightness = STEP_VIEW_DIM + (1.0f - STEP_VIEW_DIM) * breath;
            else
                brightness = STEP_VIEW_DIM;
        }
        else if (menuActive() && channel == menuSelected())
            brightness = breath;

        if (isSlider(channel))
        {
            // armed recording blinks the top LEDs; a running sequencer lights its stage's
            // the current stage's top LED: while running, and always in a step view
            bool stageMark = (sequencerRunning() || sequencerWriteRow() >= 0) && channel == sequencerStage();
            int top = recorderArmed() ? (blinkSlow ? 1 : 0) : (stageMark ? 1 : -1);
            bool note = modesShowsDot(channel);
            int row = sequencerWriteRow();
            if (flash)
                setSliderLED(channel, 1023, brightness, top);
            else if (row >= 0 && menuShifting())
            {
                // shifting: the stages inside the row's window light up, the playing one brightest
                if (sequencerInWindow(row, channel))
                    setSliderLED(channel, 1023, channel == sequencerStage() ? 1.0f : WINDOW_LEVEL);
                else
                    setSliderLED(channel, 0);
            }
            else if (row >= 0)
            {
                // WRITE: the row's value of this stage, where it lies on the slider
                int value = presetValue(channel, row);
                setSliderLED(channel, modesPositionOf(channel, value), brightness, top, note);
            }
            else if (modesRunning(channel))
                setSliderLED(channel, modesLfoLevel(channel), brightness, top);
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
        // running LFOs join the mix too
        bool running = modesRunning(i);
        if (!masterInput[i].active && !running)
            continue;
        count++;
        code += running ? modesOutputCode(i) : masterInput[i].code;
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

        // LFOs, glides and the sequencer clock run between the sensor reads, not once per scan
        modesUpdate();
        sequencerUpdate();
    }
    // m.gate: the sequencer's triggers while it runs, else the meta gate
    if (!sequencerRunning())
        setMasterGate(anySliderGate);
    updateMaster();
    updateLeds();

    uint8_t buttons = buttonsRead();
    menuUpdate(buttons);
    recorderUpdate(buttons);
    displayUpdate();
}
