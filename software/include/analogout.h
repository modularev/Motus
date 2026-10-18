#ifndef ANALOGOUT_H
#define ANALOGOUT_H

void dacSetup();
void setSliderDAC(int channel, int value_10bit);
void setPressDAC(int channel, int value_9bit);
void setXYPadDAC(int channel, int x_value_10bit, int y_value_10bit);
void setMaster(int pos_12bit, int force_9bit);
void setGate(int channel, bool touchState);
void setMasterGate(bool touchState); // high while any slider gate is on
void setVarDAC(int var, int value_12bit);
void setAllDACs(int value_12bit); // every channel of every DAC, for measuring the outputs

// calibrated voltages (0 - about 8.2 V), for 1 V/octave
int setSliderVolts(int channel, float volts); // returns the DAC value written
int sliderVoltsToCode(int channel, float volts);
void setSliderDAC12(int channel, int value_12bit);
void setVarVolts(int var, float volts);

#endif
