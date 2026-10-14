#ifndef DISPLAY_H
#define DISPLAY_H

void displaySetup();

// report a touched control; the screen follows the most recently touched one
void displaySlider(int channel, int position, int pressure);
void displayPad(int channel, int x, int y);

// var editing, preset state for the idle screen, and short messages
void displayEdit(int preset, int var, int value, bool fine, bool hold);
void displayStatus(int preset, bool fine, bool hold);
void displayMessage(const char *text);

// call once per loop, redraws only what changed
void displayUpdate();

#endif
