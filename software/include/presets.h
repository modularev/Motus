#ifndef PRESETS_H
#define PRESETS_H

#include <stdint.h>

void presetsSetup();

// a slider was touched: the var outputs switch to its preset
void presetsSelect(int slider);

// call once per loop with the touched buttons (bit per Button)
void presetsUpdate(uint8_t buttons);

#endif
