#ifndef MENU_H
#define MENU_H

#include <stdint.h>

// Menu: SET 1-4 open the MODE, VAR, EDIT and SYS pages, tapping the same pad
// again steps to the next setting, the encoder changes the value and touching
// a slider selects which slider is edited. FINE toggles small steps, LOAD
// and SAVE restore and store everything.

// a slider was touched: it becomes the one edited
void menuSelect(int slider);

// call once per loop with the touched buttons (bit per Button)
void menuUpdate(uint8_t buttons);

bool menuActive();
int menuSelected(); // the slider being edited
bool menuPadHeld(); // a SET pad is held: a slider touch only selects

#endif
