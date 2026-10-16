#ifndef PRESETS_H
#define PRESETS_H

// Voltage presets: one per slider, each with the four var voltages

void presetsSetup();

// a slider was touched: the var outputs switch to its preset
void presetsSelect(int slider);
int presetsSelected();

int presetValue(int preset, int var); // 0-4095
void presetSetValue(int preset, int var, int value);

void presetsLoad();
void presetsSave();

#endif
