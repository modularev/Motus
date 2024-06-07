#ifndef ANALOGOUT_H
#define ANALOGOUT_H

#include <Smoothed.h>
#include <TCA9548A.h>
#include <Adafruit_MCP4728.h>

extern Smoothed<unsigned int> interpol_pos[12];
extern Smoothed<unsigned int> interpol_pressure[10];

extern TCA9548A dacMux;
extern Adafruit_MCP4728 dac;

extern int gate_pin[];

void dac_setup();
void setSliderDAC(int channel, int value_10bit);
void setPressDAC(int channel, int value_8bit);
void setXYPadDAC(int channel, int x_value_9bit, int y_value_9bit);
void setMaster(int pos_10bit, int force_8bit);
void setGate(int channel, bool touchState);

#endif
