#ifndef PRESETS_H
#define PRESETS_H

#include <stdint.h>

void presetsSetup();

// a slider was touched: the var outputs switch to its preset
void presetsSelect(int slider);

// call once per loop with the button sensor's touched electrodes
void presetsUpdate(uint16_t buttonElectrodes);

#endif
