#include "presets.h"

#include <Arduino.h>
#include <EEPROM.h>
#include <Encoder.h>

#include "analogout.h"
#include "config.h"
#include "display.h"

// Encoder: while a set pad is held, each detent changes that var by a step
static const int ENC_COUNTS_PER_DETENT = 4;
static const int COARSE_STEP = 16;          // DAC counts (of 4095) per detent
static const int FINE_STEP = 1;
static const uint32_t FAST_TURN_MS = 40;     // detents closer together than this sweep faster
static const int FAST_FACTOR = 4;

static const uint32_t STORAGE_MAGIC = 0x4D4F5431; // "MOT1"

struct Storage
{
    uint32_t magic;
    uint16_t values[NUM_PRESETS][NUM_VARS];
};

static Encoder enc(ENC_PIN_A, ENC_PIN_B);

static uint16_t presets[NUM_PRESETS][NUM_VARS];
static uint16_t outputs[NUM_VARS];
static int selected = 0;
static bool fine = false;

static uint8_t lastButtons = 0;
static int32_t encConsumed = 0;
static elapsedMillis sinceDetent;

// set the var outputs to the selected preset
static void writeOutputs(bool force)
{
    for (int var = 0; var < NUM_VARS; var++)
    {
        if (force || outputs[var] != presets[selected][var])
        {
            outputs[var] = presets[selected][var];
            setVarDAC(var, outputs[var]);
        }
    }
}

static void loadPresets()
{
    Storage storage;
    EEPROM.get(0, storage);
    if (storage.magic == STORAGE_MAGIC)
        memcpy(presets, storage.values, sizeof(presets));
    else
        memset(presets, 0, sizeof(presets));
}

static void savePresets()
{
    Storage storage;
    storage.magic = STORAGE_MAGIC;
    memcpy(storage.values, presets, sizeof(presets));
    EEPROM.put(0, storage);
}

void presetsSetup()
{
    loadPresets();
    writeOutputs(true);
    encConsumed = enc.read();
}

void presetsSelect(int slider)
{
    if (slider == selected)
        return;
    selected = slider;
    writeOutputs(false);
}

void presetsUpdate(uint8_t buttons)
{
    uint8_t pressed = buttons & ~lastButtons;
    lastButtons = buttons;

    if (pressed & (1 << BUTTON_FINE))
        fine = !fine;
    if (pressed & (1 << BUTTON_LOAD))
    {
        loadPresets();
        writeOutputs(false);
        displayMessage(" LOADED ");
    }
    if (pressed & (1 << BUTTON_SAVE))
    {
        savePresets();
        displayMessage(" SAVED  ");
    }

    // the var being edited: the first set pad that is held
    int editing = -1;
    for (int var = 0; var < NUM_VARS; var++)
    {
        if (buttons & (1 << (BUTTON_SET_1 + var)))
        {
            editing = var;
            break;
        }
    }

    // whole detents turned since the last call; turning without a set pad held does nothing
    int32_t counts = enc.read();
    int detents = (counts - encConsumed) / ENC_COUNTS_PER_DETENT;
    encConsumed += detents * ENC_COUNTS_PER_DETENT;

    if (editing >= 0 && detents != 0)
    {
        int step = fine ? FINE_STEP : COARSE_STEP * (sinceDetent < FAST_TURN_MS ? FAST_FACTOR : 1);
        sinceDetent = 0;

        uint16_t &value = presets[selected][editing];
        value = constrain(value + detents * step, 0, 4095);
        writeOutputs(false);
    }

    if (editing >= 0)
        displayEdit(selected, editing, presets[selected][editing]);
    displayStatus(selected, fine);
}
