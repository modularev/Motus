#include "presets.h"

#include <Arduino.h>

#include "analogout.h"
#include "config.h"
#include "modes.h"
#include "sequencer.h"

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
        // a negative range runs the notes downwards along the positions
        int dir = (detents > 0) == (r.range >= 0) ? 1 : -1;
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
