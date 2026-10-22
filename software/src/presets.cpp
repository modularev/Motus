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

static uint16_t presets[NUM_PRESETS][NUM_VARS]; // positions in the row's range, 0-65535
static uint16_t outputs[NUM_VARS];
static int selected[NUM_VARS]; // the stage each row plays

// a position in a row's range as cents: like a slider's, quantized to the
// notes in the range when the row has a scale
static int centsAt(int var, int position)
{
    SliderSettings range;
    sequencerRowRange(var, range);
    if (range.scale)
        return modesValueIn(range, position >> 6);
    return constrain(range.base + lroundf(position / (float)PRESET_FULL * range.range), 0, MAX_CENTS);
}

// and the other way round, for converting saved values
static int positionOf(const SeqRow &r, int cents)
{
    SliderSettings range;
    memset(&range, 0, sizeof(range));
    range.scale = r.scale;
    range.root = r.root;
    range.base = r.base;
    range.range = r.range;
    if (range.scale)
        return modesPositionIn(range, cents) * PRESET_FULL / 1023;
    if (range.range == 0)
        return 0;
    return constrain(lroundf((float)(cents - range.base) / range.range * PRESET_FULL), 0L, (long)PRESET_FULL);
}

// what a row outputs: its stage's position in the row's current range
static int outputCents(int var)
{
    return centsAt(var, presets[selected[var]][var]);
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
        presetsCentsToPositions(presets, seqSettings);
    }
}

void presetsCentsToPositions(uint16_t values[][NUM_VARS], const SeqSettings &sequencer)
{
    for (int preset = 0; preset < NUM_PRESETS; preset++)
        for (int var = 0; var < NUM_VARS; var++)
            values[preset][var] = positionOf(sequencer.rows[var], values[preset][var]);
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

int presetPosition(int preset, int var)
{
    return presets[preset][var];
}

void presetSetPosition(int preset, int var, int position)
{
    presets[preset][var] = constrain(position, 0, PRESET_FULL);
    writeOutputs(false);
}

int presetValue(int preset, int var)
{
    return centsAt(var, presets[preset][var]);
}

void presetNudge(int preset, int var, int detents, int cents)
{
    const SeqRow &r = seqSettings.rows[var];
    int position = presets[preset][var];
    if (r.scale)
    {
        // quantized: on to the next note, in the direction of the turn
        int dir = detents > 0 ? 1 : -1;
        for (int d = 0; d < abs(detents); d++)
        {
            int from = centsAt(var, position);
            while (position + dir * 64 >= 0 && position + dir * 64 <= PRESET_FULL && centsAt(var, position) == from)
                position += dir * 64;
        }
    }
    else if (r.range != 0)
        position += lroundf((float)detents * cents / r.range * PRESET_FULL);
    presetSetPosition(preset, var, position);
}
