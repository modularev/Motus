#include "menu.h"

#include <Arduino.h>
#include <Encoder.h>

#include "config.h"
#include "display.h"
#include "modes.h"
#include "presets.h"

static const uint32_t MENU_TIMEOUT_MS = 5000; // back to the play screen without menu input

// Encoder: each detent changes the value by a step
static const int ENC_COUNTS_PER_DETENT = 4;
static const int VAR_COARSE_STEP = 16;   // DAC counts (of 4095) per detent
static const int VAR_FINE_STEP = 1;
static const uint32_t FAST_TURN_MS = 40; // detents closer together than this sweep faster
static const int FAST_FACTOR = 4;

enum Page
{
    PAGE_NONE,
    PAGE_MODE,
    PAGE_VAR,
    PAGE_EDIT,
    PAGE_SYS
};

// settings on the EDIT page of a NOTE slider
enum NoteItem
{
    NOTE_SCALE,
    NOTE_ROOT,
    NOTE_OCTAVE,
    NOTE_RANGE,
    NOTE_PLAY,
    NOTE_SLEW,
    NOTE_GATE,
    NUM_NOTE_ITEMS
};

static Encoder enc(ENC_PIN_A, ENC_PIN_B);
static int32_t encConsumed = 0;
static elapsedMillis sinceDetent;

static Page page = PAGE_NONE;
static int item = 0;
static int slider = 0;
static bool fine = false;
static uint32_t lastInput = 0;
static uint8_t lastButtons = 0;

static int itemCount(Page p)
{
    switch (p)
    {
    case PAGE_VAR:
        return NUM_VARS;
    case PAGE_EDIT:
        return sliderSettings[slider].mode == MODE_NOTE ? NUM_NOTE_ITEMS : 1;
    default:
        return 1;
    }
}

// whole detents turned since the last call
static int readDetents()
{
    int32_t counts = enc.read();
    int detents = (counts - encConsumed) / ENC_COUNTS_PER_DETENT;
    encConsumed += detents * ENC_COUNTS_PER_DETENT;
    return detents;
}

static void stepSetting(uint8_t &value, int detents, int lowest, int highest)
{
    value = constrain((int)value + detents, lowest, highest);
}

static void change(int detents)
{
    SliderSettings &s = sliderSettings[slider];

    switch (page)
    {
    case PAGE_MODE:
        stepSetting(s.mode, detents, 0, NUM_MODES - 1);
        break;

    case PAGE_VAR:
    {
        int step = fine ? VAR_FINE_STEP : VAR_COARSE_STEP * (sinceDetent < FAST_TURN_MS ? FAST_FACTOR : 1);
        presetSetValue(slider, item, presetValue(slider, item) + detents * step);
        break;
    }

    case PAGE_EDIT:
        if (s.mode != MODE_NOTE)
            break;
        switch (item)
        {
        case NOTE_SCALE:
            stepSetting(s.scale, detents, 0, NUM_SCALES - 1);
            break;
        case NOTE_ROOT:
            stepSetting(s.root, detents, 0, 11);
            break;
        case NOTE_OCTAVE:
            stepSetting(s.octave, detents, 0, 7);
            s.range = min((int)s.range, 8 - s.octave);
            break;
        case NOTE_RANGE:
            stepSetting(s.range, detents, 1, 8 - s.octave);
            break;
        case NOTE_PLAY:
            stepSetting(s.latch, detents, 0, 1);
            break;
        case NOTE_SLEW:
            stepSetting(s.slew, detents, 0, NUM_SLEW_TIMES - 1);
            break;
        case NOTE_GATE:
            stepSetting(s.retrigger, detents, 0, 1);
            break;
        }
        break;

    default:
        break;
    }
    sinceDetent = 0;
}

// the two screen lines for the current page and setting
static void show()
{
    char top[9];
    char bottom[9];
    const SliderSettings &s = sliderSettings[slider];
    unsigned number = (unsigned)(slider + 1) % 10;

    switch (page)
    {
    case PAGE_MODE:
        snprintf(top, sizeof(top), "S%u MODE", number);
        snprintf(bottom, sizeof(bottom), "%8s", modeName(s.mode));
        break;

    case PAGE_VAR:
    {
        int value = presetValue(slider, item);
        snprintf(top, sizeof(top), "S%u V%u  %c", number, (unsigned)(item + 1) % 10, fine ? 'F' : ' ');
        snprintf(bottom, sizeof(bottom), "%8d", value);
        break;
    }

    case PAGE_EDIT:
        if (s.mode != MODE_NOTE)
        {
            snprintf(top, sizeof(top), "S%u %s", number, modeName(s.mode));
            snprintf(bottom, sizeof(bottom), "%8s", "NO EDIT");
            break;
        }
        switch (item)
        {
        case NOTE_SCALE:
            snprintf(top, sizeof(top), "S%u SCALE", number);
            snprintf(bottom, sizeof(bottom), "%8s", scaleName(s.scale));
            break;
        case NOTE_ROOT:
            snprintf(top, sizeof(top), "S%u ROOT", number);
            snprintf(bottom, sizeof(bottom), "%8s", noteName(s.root));
            break;
        case NOTE_OCTAVE:
            snprintf(top, sizeof(top), "S%u OCT", number);
            snprintf(bottom, sizeof(bottom), "%8u", (unsigned)s.octave % 10);
            break;
        case NOTE_RANGE:
            snprintf(top, sizeof(top), "S%u RANGE", number);
            snprintf(bottom, sizeof(bottom), "   %u OCT", (unsigned)s.range % 10);
            break;
        case NOTE_PLAY:
            snprintf(top, sizeof(top), "S%u PLAY", number);
            snprintf(bottom, sizeof(bottom), "%8s", s.latch ? "TOUCH" : "SLIDE");
            break;
        case NOTE_SLEW:
            snprintf(top, sizeof(top), "S%u SLEW", number);
            if (slewTime(s.slew) == 0)
                snprintf(bottom, sizeof(bottom), "%8s", "OFF");
            else
                snprintf(bottom, sizeof(bottom), "%5u MS", (unsigned)slewTime(s.slew) % 10000);
            break;
        case NOTE_GATE:
            snprintf(top, sizeof(top), "S%u GATE", number);
            snprintf(bottom, sizeof(bottom), "%8s", s.retrigger ? "RETRIG" : "HOLD");
            break;
        }
        break;

    case PAGE_SYS:
        snprintf(top, sizeof(top), "SYSTEM");
        snprintf(bottom, sizeof(bottom), "%8s", "(LATER)");
        break;

    default:
        return;
    }
    displayMenu(top, bottom);
}

void menuSelect(int touched)
{
    if (touched != slider)
        item = min(item, itemCount(page) - 1);
    slider = touched;
    if (page != PAGE_NONE)
        lastInput = millis();
}

void menuUpdate(uint8_t buttons)
{
    uint8_t pressed = buttons & ~lastButtons;
    lastButtons = buttons;

    // a page pad opens its page, tapping it again steps to the next setting
    static const Page PAGES[4] = {PAGE_MODE, PAGE_VAR, PAGE_EDIT, PAGE_SYS};
    for (int i = 0; i < 4; i++)
    {
        if (!(pressed & (1 << (BUTTON_SET_1 + i))))
            continue;
        if (page == PAGES[i])
            item = (item + 1) % itemCount(page);
        else
        {
            page = PAGES[i];
            item = 0;
        }
        lastInput = millis();
    }

    if (pressed & (1 << BUTTON_FINE))
        fine = !fine;
    if (pressed & (1 << BUTTON_LOAD))
    {
        presetsLoad();
        modesLoad();
        displayMessage(" LOADED ");
    }
    if (pressed & (1 << BUTTON_SAVE))
    {
        presetsSave();
        modesSave();
        displayMessage(" SAVED  ");
    }

    int detents = readDetents();
    if (page != PAGE_NONE && detents != 0)
    {
        change(detents);
        lastInput = millis();
    }

    if (page != PAGE_NONE && millis() - lastInput > MENU_TIMEOUT_MS)
        page = PAGE_NONE;

    show();
    displayStatus(presetsSelected(), fine);
}

bool menuActive()
{
    return page != PAGE_NONE;
}

int menuSelected()
{
    return slider;
}
