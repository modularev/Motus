#include "presets.h"

#include <Arduino.h>
#include <EEPROM.h>

#include "analogout.h"
#include "config.h"
#include "modes.h"

// the single save of earlier firmware
static const int STORAGE_ADDRESS = 0;
static const uint32_t STORAGE_MAGIC = 0x4D4F5431; // "MOT1"

struct Storage
{
    uint32_t magic;
    uint16_t values[NUM_PRESETS][NUM_VARS];
};

static uint16_t presets[NUM_PRESETS][NUM_VARS]; // cents
static uint16_t outputs[NUM_VARS];
static int selected = 0;

// set the var outputs to the selected preset
static void writeOutputs(bool force)
{
    for (int var = 0; var < NUM_VARS; var++)
    {
        if (force || outputs[var] != presets[selected][var])
        {
            outputs[var] = presets[selected][var];
            setVarVolts(var, (float)outputs[var] / CENTS_PER_VOLT);
        }
    }
}

void presetsLoadLegacy()
{
    Storage storage;
    EEPROM.get(STORAGE_ADDRESS, storage);
    if (storage.magic == STORAGE_MAGIC)
    {
        memcpy(presets, storage.values, sizeof(presets));
        presetsCodesToCents(presets);
    }
}

void presetsCodesToCents(uint16_t values[][NUM_VARS])
{
    // about 2 mV per DAC value: the outputs span 0-8.19 V
    for (int preset = 0; preset < NUM_PRESETS; preset++)
        for (int var = 0; var < NUM_VARS; var++)
            values[preset][var] = min(lroundf(values[preset][var] * 8.19f / 4095.0f * CENTS_PER_VOLT), (long)MAX_CENTS);
}

void presetsGetAll(uint16_t values[][NUM_VARS])
{
    memcpy(values, presets, sizeof(presets));
}

void presetsSetAll(const uint16_t values[][NUM_VARS])
{
    memcpy(presets, values, sizeof(presets));
    writeOutputs(false);
}

void presetsSetup()
{
    writeOutputs(true);
}

void presetsSelect(int slider)
{
    if (slider == selected)
        return;
    selected = slider;
    writeOutputs(false);
}

int presetsSelected()
{
    return selected;
}

int presetValue(int preset, int var)
{
    return presets[preset][var];
}

void presetSetValue(int preset, int var, int value)
{
    presets[preset][var] = constrain(value, 0, MAX_CENTS);
    writeOutputs(false);
}
