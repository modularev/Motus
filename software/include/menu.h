#ifndef MENU_H
#define MENU_H

#include <stdint.h>

// Menu: a tap on SET 1, 2 and 4 opens the SLIDER, SEQ and SYS pages, a double
// tap starts or stops that pad's sequencer row, and holding it opens the row's
// step view; holding it there opens the row's settings. The encoder scrolls,
// FINE held while turning changes a value, and touching a slider selects which
// slider is edited. FINE toggles small steps, a double tap runs or stops all
// rows. LOAD and SAVE restore and store patches.

// a slider was touched: it becomes the one edited
void menuSelect(int slider);

// call once per loop with the touched buttons (bit per Button)
void menuUpdate(uint8_t buttons);

bool menuActive();
int menuSelected(); // the slider being edited
bool menuPadHeld(); // a SET pad is held: a slider touch only selects

// the row's window is shown: shifting it with its SET pad held, or its DIR,
// SHIFT or LEN open in the row's settings
bool menuShifting();

// WRITE: report a slider's touch; while it is held the encoder edits its
// stage value, and once it has, the finger stops writing for that touch
void menuWriteTouch(int slider, bool touching);
bool menuWriteEdited(int slider);

#endif
