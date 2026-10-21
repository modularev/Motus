#include "presets.h"

#include <Arduino.h>
#include <EEPROM.h>

#include "analogout.h"
#include "config.h"
#include "modes.h"
#include "sequencer.h"

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
static int selected[NUM_VARS]; // the stage each row plays

// what a row outputs: its value, snapped to the sequencer's scale if the
// quantizer applies to the row (the stored value stays as it is)
static int outputCents(int var)
{
    int cents = presets[selected[var]][var];
    if (seqSettings.scale && (seqSettings.quantRows & (1 << var)))
        cents = modesQuantizeCents(seqSettings.scale - 1, seqSettings.root, cents);
    return cents;
}

// set every var output to its row's selected stage
static void writeOutputs(bool force)
{
    for (int var = 0; var < NUM_VARS; var++)
    {
        int cents = outputCents(var);
        if (force || outputs[var] != cents)
        {
            outputs[var] = cents;
            setVarVolts(var, (float)cents / CENTS_PER_VOLT);
        }
    }
}

void presetsRefresh()
{
    writeOutputs(false);
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
    for (int var = 0; var < NUM_VARS; var++)
        selected[var] = slider;
    writeOutputs(false);
}

void presetsSelectRow(int var, int slider)
{
    if (selected[var] == slider)
        return;
    selected[var] = slider;
    writeOutputs(false);
}

int presetsSelected()
{
    return selected[0];
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
