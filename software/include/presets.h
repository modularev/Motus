#ifndef PRESETS_H
#define PRESETS_H

#include <stdint.h>

#include "config.h"

// Voltage presets: one per slider (a stage of the sequencer), with the four
// var rows. A stage holds a position in its row's range, so the row's BASE,
// RANGE and QUANT apply to every stage at once.

struct SeqSettings;
constexpr int PRESET_FULL = 65535; // the top of the row's range

void presetsSetup();

// the var outputs switch to a slider's preset: all of them, or one row
void presetsSelect(int slider);
void presetsSelectRow(int var, int slider);

// the outputs again, e.g. after the sequencer's quantizer changed
void presetsRefresh();
int presetsSelected(); // row 1's

// a stage's position in its row's range (0-PRESET_FULL), and the value it
// gives there, in cents of a volt (1/1200 V) like BASE and RANGE
int presetPosition(int preset, int var);
void presetSetPosition(int preset, int var, int position);
int presetValue(int preset, int var);

// edit a stage: quantized rows move a note per detent, others this many cents
void presetNudge(int preset, int var, int detents, int cents);

// all preset values, for the storage
void presetsGetAll(uint16_t values[][NUM_VARS]);
void presetsSetAll(const uint16_t values[][NUM_VARS]);

// the single save of earlier firmware, read once when converting it
void presetsLoadLegacy();

// earlier saves stored DAC values (0-4095), then cents: converted in place
void presetsCodesToCents(uint16_t values[][NUM_VARS]);
void presetsCentsToPositions(uint16_t values[][NUM_VARS], const SeqSettings &sequencer);

#endif
