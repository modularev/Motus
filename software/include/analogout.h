#ifndef ANALOGOUT_H
#define ANALOGOUT_H

void dacSetup();
void setSliderDAC(int channel, int value_10bit);
void setPressDAC(int channel, int value_9bit);
void setXYPadDAC(int channel, int x_value_10bit, int y_value_10bit);
void setMaster(int pos_10bit, int force_9bit);
void setGate(int channel, bool touchState);
void setMasterGate(bool touchState); // high while any slider gate is on
void setVarDAC(int var, int value_12bit);

#endif
