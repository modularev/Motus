#ifndef DISPLAY_H
#define DISPLAY_H

void displaySetup();

// report a touched control; the screen follows the most recently touched one
// text replaces the position number when not empty, e.g. a note name
void displaySlider(int channel, int position, int pressure, bool recording = false, const char *text = "");
void displayPad(int channel, int x, int y, bool recording = false);

// a touched slider with two lines of its own, e.g. the value WRITE stores
void displaySliderText(int channel, const char *top, const char *bottom);

// custom characters for text: a recording dot and the FINE indicator
constexpr char GLYPH_DOT = 6;
constexpr char GLYPH_FINE = 7;

// menu, state for the idle screen, and short messages
// a menu screen; call every loop while the menu is open
void displayMenu(const char *top, const char *bottom);

// a title over a progress bar (0-1); call every loop while it runs
void displayProgress(const char *top, float fraction);
// idle screen: the sequencer state with its tempo ("120" or "S3"); while
// running every row's progress through its length (1-5, a bar each), when
// stopped the slot; and the unsaved marker
void displayStatus(const int progress[], bool running, const char *tempo, int slot, bool unsaved);
void displayRecordState(bool armed, bool recording);
void displayMessage(const char *text, const char *second = "");

// call once per loop, redraws only what changed
void displayUpdate();

#endif
