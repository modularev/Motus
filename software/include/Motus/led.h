#ifndef LED_H
#define LED_H

void ledSetup();
void setSliderLED(int slider_channel, int value);
void setXYPadLED(int channel, int x_value, int y_value);

#endif
