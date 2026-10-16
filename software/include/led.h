#ifndef LED_H
#define LED_H

void ledSetup();

// value 10 bit, as a bar (1023 lights all six) or as a dot moving between
// the LEDs; brightness 0-1; top: -1 as drawn, 0 or 1 to force the top LED
void setSliderLED(int slider_channel, int value, float brightness = 1.0f, int top = -1, bool dot = false);

// x and y pad-local, 10 bit; all lights every LED of the pad
void setXYPadLED(int channel, int x_value, int y_value, float brightness = 1.0f, bool all = false);

#endif
