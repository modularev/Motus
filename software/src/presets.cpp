#include "presets.h"

#include <Arduino.h>
#include <EEPROM.h>

#include "analogout.h"
#include "config.h"

static const int STORAGE_ADDRESS = 0;
static const uint32_t STORAGE_MAGIC = 0x4D4F5431; // "MOT1"

struct Storage
{
    uint32_t magic;
    uint16_t values[NUM_PRESETS][NUM_VARS];
};

static uint16_t presets[NUM_PRESETS][NUM_VARS];
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
            setVarDAC(var, outputs[var]);
        }
    }
}

void presetsLoad()
{
    Storage storage;
    EEPROM.get(STORAGE_ADDRESS, storage);
    if (storage.magic == STORAGE_MAGIC)
        memcpy(presets, storage.values, sizeof(presets));
    else
        memset(presets, 0, sizeof(presets));
    writeOutputs(false);
}

void presetsSave()
{
    Storage storage;
    storage.magic = STORAGE_MAGIC;
    memcpy(storage.values, presets, sizeof(presets));
    EEPROM.put(STORAGE_ADDRESS, storage);
}

void presetsSetup()
{
    presetsLoad();
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
    presets[preset][var] = constrain(value, 0, 4095);
    writeOutputs(false);
}
