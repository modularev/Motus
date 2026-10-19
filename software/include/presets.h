#ifndef PRESETS_H
#define PRESETS_H

#include <stdint.h>

#include "config.h"

// Voltage presets: one per slider, each with the four var voltages

void presetsSetup();

// the var outputs switch to a slider's preset: all of them, or one row
void presetsSelect(int slider);
void presetsSelectRow(int var, int slider);
int presetsSelected(); // row 1's

// values in cents of a volt (1/1200 V), like BASE and RANGE
int presetValue(int preset, int var);
void presetSetValue(int preset, int var, int value);

// all preset values, for the storage
void presetsGetAll(uint16_t values[][NUM_VARS]);
void presetsSetAll(const uint16_t values[][NUM_VARS]);

// the single save of earlier firmware, read once when converting it
void presetsLoadLegacy();

// earlier saves stored DAC values (0-4095): converted to cents in place
void presetsCodesToCents(uint16_t values[][NUM_VARS]);

#endif
