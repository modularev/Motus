#ifndef MENU_H
#define MENU_H

#include <stdint.h>

// Menu: a tap on A-D opens the PLAY, OUT, TIME and SEQ pages, a double tap
// starts or stops that row, and holding it opens the row's step view; holding
// it there opens the row's settings. The encoder scrolls, EDIT held while
// turning changes a value (a tap toggles fine steps), and touching a slider or
// pad selects what is edited. RUN starts or stops all rows, held it opens SYS.
// PATCH loads (tap) and saves (hold).

// a slider was touched: it becomes the one edited
void menuSelect(int slider);

// call once per loop with the touched buttons (bit per Button)
void menuUpdate(uint8_t buttons);

bool menuActive();
int menuSelected(); // the slider being edited
bool menuPadHeld(); // a row button A-D is held: a slider or pad touch only selects

// the row's window is shown: shifting it with its row button held, or its DIR,
// SHIFT or LEN open in the row's settings
bool menuShifting();

// WRITE: report a slider's touch; while it is held the encoder edits its
// stage value, and once it has, the finger stops writing for that touch
void menuWriteTouch(int slider, bool touching);
bool menuWriteEdited(int slider);

#endif
