#include "menu.h"

#include <Arduino.h>
#include <Encoder.h>

#include "config.h"
#include "display.h"
#include "modes.h"
#include "presets.h"
#include "storage.h"
#include "system.h"

static const uint32_t MENU_TIMEOUT_MS = 5000; // back to the play screen without menu input

// Encoder: each detent changes the value by a step
static const int ENC_COUNTS_PER_DETENT = 4;
static const int VAR_COARSE_STEP = SEMITONE_CENTS; // var presets: a semitone per detent
static const int VAR_FINE_STEP = 12;               // or 0.01 V with FINE
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

// Settings on the EDIT page, in their order for each mode
enum Setting
{
    SETTING_QUANT,
    SETTING_ROOT,
    SETTING_BASE,
    SETTING_RANGE,
    SETTING_PLAY,
    SETTING_SLEW,
    SETTING_SLIDE_GATE,
    SETTING_SHAPE,
    SETTING_RATE,
    SETTING_DEPTH,
    SETTING_RESET,
    SETTING_LFO_GATE,
};
static const Setting SLIDE_SETTINGS[] = {SETTING_QUANT, SETTING_ROOT, SETTING_BASE, SETTING_RANGE,
                                         SETTING_PLAY, SETTING_SLEW, SETTING_SLIDE_GATE};
static const Setting LFO_SETTINGS[] = {SETTING_SHAPE, SETTING_RATE, SETTING_BASE, SETTING_RANGE, SETTING_DEPTH,
                                       SETTING_QUANT, SETTING_ROOT, SETTING_RESET, SETTING_LFO_GATE};
static const int NUM_SLIDE_SETTINGS = sizeof(SLIDE_SETTINGS) / sizeof(SLIDE_SETTINGS[0]);
static const int NUM_LFO_SETTINGS = sizeof(LFO_SETTINGS) / sizeof(LFO_SETTINGS[0]);

// BASE and RANGE steps, in cents: quantized by octaves (semitones with FINE),
// unquantized by 1 V (0.1 V with FINE)
static const int OCTAVE_CENTS = 12 * SEMITONE_CENTS;
static const int VOLT_COARSE_STEP = CENTS_PER_VOLT;
static const int VOLT_FINE_STEP = CENTS_PER_VOLT / 10;

// SLEW: coarse steps through these times, FINE in steps of about 1%
static const int SLEW_STEPS[] = {0, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000};
static const int NUM_SLEW_STEPS = sizeof(SLEW_STEPS) / sizeof(SLEW_STEPS[0]);

// settings on the SYS page
enum SystemItem
{
    SYS_SMOOTH,
    SYS_DEADBAND,
    SYS_PRESSURE,
    SYS_TOUCH,
    SYS_BRIGHT,
    SYS_TRIG,
    SYS_LOOP_TRIG,
    NUM_SYS_ITEMS
};
static const char *SYS_NAMES[NUM_SYS_ITEMS] = {"SMOOTH", "DEADBAND", "PRESSURE", "TOUCH", "BRIGHT", "TRIG LEN", "LOOP TRG"};

// SAVE and LOAD: a tap opens a prompt, the encoder picks the slot, a second
// tap of the same pad confirms; SET, FINE or a pause cancels
enum Prompt
{
    PROMPT_NONE,
    PROMPT_SAVE,
    PROMPT_LOAD
};
static Prompt prompt = PROMPT_NONE;
static int promptSlot = 0;
static uint32_t promptInput = 0;

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
        return sliderSettings[slider].mode == MODE_LFO ? NUM_LFO_SETTINGS : NUM_SLIDE_SETTINGS;
    case PAGE_SYS:
        return NUM_SYS_ITEMS;
    default:
        return 1;
    }
}

static Setting currentSetting()
{
    if (sliderSettings[slider].mode == MODE_LFO)
        return LFO_SETTINGS[min(item, NUM_LFO_SETTINGS - 1)];
    return SLIDE_SETTINGS[min(item, NUM_SLIDE_SETTINGS - 1)];
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

static int fastFactor()
{
    return sinceDetent < FAST_TURN_MS ? FAST_FACTOR : 1;
}

// SLEW: the next coarse time up or down, or about 1% steps with FINE
static void stepSlew(uint16_t &value, int detents)
{
    if (fine)
    {
        int step = value < 100 ? 1 : (value < 1000 ? 10 : 100);
        value = constrain((int)value + detents * step * fastFactor(), 0, MAX_SLEW_MS);
        return;
    }
    for (int d = 0; d < abs(detents); d++)
    {
        int next = value;
        if (detents > 0)
        {
            for (int i = 0; i < NUM_SLEW_STEPS; i++)
                if (SLEW_STEPS[i] > value)
                {
                    next = SLEW_STEPS[i];
                    break;
                }
        }
        else
        {
            for (int i = NUM_SLEW_STEPS - 1; i >= 0; i--)
                if (SLEW_STEPS[i] < value)
                {
                    next = SLEW_STEPS[i];
                    break;
                }
        }
        value = next;
    }
}

static void changeSystem(int detents)
{
    SystemSettings &y = systemSettings;
    switch (item)
    {
    case SYS_SMOOTH:
        stepSetting(y.smooth, detents, 1, 10);
        break;
    case SYS_DEADBAND:
        stepSetting(y.deadband, detents, 0, 20);
        break;
    case SYS_PRESSURE:
        stepSetting(y.pressure, detents * 10, 50, 200);
        break;
    case SYS_TOUCH:
        stepSetting(y.touch, detents * 10, 50, 200);
        break;
    case SYS_BRIGHT:
        stepSetting(y.bright, detents * 10, 10, 100);
        break;
    case SYS_TRIG:
        stepSetting(y.trigMs, detents, 1, 20);
        break;
    case SYS_LOOP_TRIG:
        stepSetting(y.loopTrig, detents, 0, 1);
        break;
    }
}

static void formatSystem(char *bottom, size_t size)
{
    const SystemSettings &y = systemSettings;
    switch (item)
    {
    case SYS_SMOOTH:
        snprintf(bottom, size, "%8u", (unsigned)y.smooth % 100);
        break;
    case SYS_DEADBAND:
        snprintf(bottom, size, "%8u", (unsigned)y.deadband % 100);
        break;
    case SYS_PRESSURE:
        snprintf(bottom, size, "%6u %%", (unsigned)y.pressure % 1000);
        break;
    case SYS_TOUCH:
        snprintf(bottom, size, "%6u %%", (unsigned)y.touch % 1000);
        break;
    case SYS_BRIGHT:
        snprintf(bottom, size, "%6u %%", (unsigned)y.bright % 1000);
        break;
    case SYS_TRIG:
        snprintf(bottom, size, "%5u ms", (unsigned)y.trigMs % 100);
        break;
    case SYS_LOOP_TRIG:
        snprintf(bottom, size, "%8s", y.loopTrig ? "on" : "off");
        break;
    }
}

static void change(int detents)
{
    SliderSettings &s = sliderSettings[slider];

    switch (page)
    {
    case PAGE_MODE:
        stepSetting(s.mode, detents, 0, NUM_MODES - 1);
        item = 0;
        break;

    case PAGE_VAR:
    {
        int step = fine ? VAR_FINE_STEP : VAR_COARSE_STEP * fastFactor();
        presetSetValue(slider, item, presetValue(slider, item) + detents * step);
        break;
    }

    case PAGE_EDIT:
    {
        // big steps stay single, small steps speed up when turning fast
        int voltStep = s.scale ? (fine ? SEMITONE_CENTS * fastFactor() : OCTAVE_CENTS)
                               : (fine ? VOLT_FINE_STEP * fastFactor() : VOLT_COARSE_STEP);
        switch (currentSetting())
        {
        case SETTING_QUANT:
            stepSetting(s.scale, detents, 0, NUM_SCALES);
            break;
        case SETTING_ROOT:
            stepSetting(s.root, detents, 0, 11);
            break;
        case SETTING_BASE:
            // the range keeps its size where it can
            s.base = constrain(s.base + detents * voltStep, 0, MAX_CENTS);
            s.range = constrain(s.range, -s.base, MAX_CENTS - s.base);
            break;
        case SETTING_RANGE:
            s.range = constrain(s.range + detents * voltStep, -s.base, MAX_CENTS - s.base);
            break;
        case SETTING_PLAY:
            stepSetting(s.latch, detents, 0, 1);
            break;
        case SETTING_SLEW:
            stepSlew(s.slewMs, detents);
            break;
        case SETTING_SLIDE_GATE:
            stepSetting(s.slideGate, detents, 0, 2);
            break;
        case SETTING_SHAPE:
            stepSetting(s.lfoShape, detents, 0, NUM_LFO_SHAPES - 1);
            break;
        case SETTING_RATE:
            stepSetting(s.lfoRate, detents, 0, NUM_LFO_RATES - 1);
            break;
        case SETTING_DEPTH:
            stepSetting(s.lfoDepth, detents, 0, 1);
            break;
        case SETTING_RESET:
            stepSetting(s.lfoReset, detents, 0, 1);
            break;
        case SETTING_LFO_GATE:
            stepSetting(s.lfoGate, detents, 0, 3);
            break;
        }
        break;
    }

    case PAGE_SYS:
        changeSystem(detents);
        break;

    default:
        break;
    }
    sinceDetent = 0;
}

// a voltage in cents as the nearest note and the volts, e.g. "C#2 2.08"
static void formatNoteVolts(char *text, size_t size, int cents)
{
    int semitone = lroundf((float)cents / SEMITONE_CENTS);
    char note[5];
    snprintf(note, sizeof(note), "%s%u", noteName(semitone), (unsigned)(semitone / 12) % 10);
    snprintf(text, size, "%-4s%4.2f", note, (float)cents / CENTS_PER_VOLT);
}

// a voltage in cents as volts, right-aligned, e.g. "   2.00V" or "  -3.00V"
static void formatVolts(char *text, size_t size, int cents)
{
    snprintf(text, size, "%7.2fV", (float)cents / CENTS_PER_VOLT);
}

// the value of an EDIT page setting
static void formatSetting(char *bottom, size_t size, Setting setting, const SliderSettings &s)
{
    switch (setting)
    {
    case SETTING_QUANT:
        snprintf(bottom, size, "%8s", s.scale ? scaleName(s.scale - 1) : "off");
        break;
    case SETTING_ROOT:
        snprintf(bottom, size, "%8s", noteName(s.root));
        break;
    case SETTING_BASE:
        if (s.scale)
            formatNoteVolts(bottom, size, s.base);
        else
            formatVolts(bottom, size, s.base);
        break;
    case SETTING_RANGE:
        if (s.scale)
        {
            // in semitones
            int semitones = lroundf((float)s.range / SEMITONE_CENTS);
            snprintf(bottom, size, "%5d st", constrain(semitones, -99, 99));
        }
        else
            formatVolts(bottom, size, s.range);
        break;
    case SETTING_PLAY:
        snprintf(bottom, size, "%8s", s.latch ? "touch" : "slide");
        break;
    case SETTING_SLEW:
        if (s.slewMs == 0)
            snprintf(bottom, size, "%8s", "off");
        else
            snprintf(bottom, size, "%5u ms", (unsigned)s.slewMs % 100000);
        break;
    case SETTING_SLIDE_GATE:
    {
        static const char *GATES[3] = {"gate", "retrig", "trig"};
        snprintf(bottom, size, "%8s", GATES[s.slideGate % 3]);
        break;
    }
    case SETTING_SHAPE:
        snprintf(bottom, size, "%8s", lfoShapeName(s.lfoShape));
        break;
    case SETTING_RATE:
        snprintf(bottom, size, "%8s", lfoRateName(s.lfoRate));
        break;
    case SETTING_DEPTH:
        snprintf(bottom, size, "%8s", s.lfoDepth ? "press" : "fixed");
        break;
    case SETTING_RESET:
        snprintf(bottom, size, "%8s", s.lfoReset ? "touch" : "free");
        break;
    case SETTING_LFO_GATE:
    {
        static const char *GATES[4] = {"touch", "cycle", "trig", "note"};
        snprintf(bottom, size, "%8s", GATES[s.lfoGate % 4]);
        break;
    }
    }
}

// the two screen lines for the current page and setting
static void show()
{
    static const char *SETTING_NAMES[] = {"QUANT", "ROOT", "BASE", "RANGE", "PLAY", "SLEW",
                                          "GATE", "SHAPE", "RATE", "DEPTH", "RESET", "GATE"};
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
        snprintf(top, sizeof(top), "S%u%cV%u", number, fine ? GLYPH_FINE : ' ', (unsigned)(item + 1) % 10);
        formatNoteVolts(bottom, sizeof(bottom), presetValue(slider, item));
        break;
    }

    case PAGE_EDIT:
    {
        Setting setting = currentSetting();
        snprintf(top, sizeof(top), "S%u%c%s", number, fine ? GLYPH_FINE : ' ', SETTING_NAMES[setting]);
        formatSetting(bottom, sizeof(bottom), setting, s);
        break;
    }

    case PAGE_SYS:
        snprintf(top, sizeof(top), "%s", SYS_NAMES[item]);
        formatSystem(bottom, sizeof(bottom));
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
    uint8_t released = ~buttons & lastButtons;
    lastButtons = buttons;

    // A page pad opens its page as soon as it is touched. Tapping it again
    // steps to the next setting when it is let go; holding it and turning
    // the encoder scrolls through the settings instead.
    static const Page PAGES[4] = {PAGE_MODE, PAGE_VAR, PAGE_EDIT, PAGE_SYS};
    static bool opened = false;   // this press opened the page, so letting go does not step
    static bool scrolled = false; // the encoder scrolled during this press
    bool padHeld = false;
    for (int i = 0; i < 4; i++)
    {
        uint8_t bit = 1 << (BUTTON_SET_1 + i);
        if (pressed & bit)
        {
            opened = page != PAGES[i];
            scrolled = false;
            if (opened)
            {
                page = PAGES[i];
                item = 0;
            }
            lastInput = millis();
        }
        if ((released & bit) && page == PAGES[i] && !opened && !scrolled)
        {
            item = (item + 1) % itemCount(page);
            lastInput = millis();
        }
        if ((buttons & bit) && page == PAGES[i])
            padHeld = true;
    }

    if (pressed & (1 << BUTTON_FINE))
        fine = !fine;

    // SAVE / LOAD prompt
    uint8_t cancelPads = (1 << BUTTON_SET_1) | (1 << BUTTON_SET_2) | (1 << BUTTON_SET_3) | (1 << BUTTON_SET_4) | (1 << BUTTON_FINE);
    if (prompt != PROMPT_NONE && ((pressed & cancelPads) || millis() - promptInput > MENU_TIMEOUT_MS))
        prompt = PROMPT_NONE;
    for (int i = 0; i < 2; i++)
    {
        Prompt p = i == 0 ? PROMPT_SAVE : PROMPT_LOAD;
        if (!(pressed & (1 << (p == PROMPT_SAVE ? BUTTON_SAVE : BUTTON_LOAD))))
            continue;
        if (prompt != p)
        {
            prompt = p;
            promptSlot = storageCurrentSlot();
        }
        else
        {
            char line[9];
            snprintf(line, sizeof(line), "slot %u", (unsigned)(promptSlot + 1) % 10);
            if (p == PROMPT_SAVE)
            {
                storageSave(promptSlot);
                displayMessage("SAVED", line);
            }
            else if (storageLoad(promptSlot))
                displayMessage("LOADED", line);
            else
                displayMessage("EMPTY", line);
            prompt = PROMPT_NONE;
        }
        promptInput = millis();
    }

    int detents = readDetents();
    if (prompt != PROMPT_NONE && detents != 0)
    {
        promptSlot = ((promptSlot + detents) % NUM_SLOTS + NUM_SLOTS) % NUM_SLOTS;
        promptInput = millis();
    }
    else if (page != PAGE_NONE && detents != 0)
    {
        if (padHeld)
        {
            // scroll through the settings, wrapping around
            int count = itemCount(page);
            item = ((item + detents) % count + count) % count;
            scrolled = true;
        }
        else
            change(detents);
        lastInput = millis();
    }

    if (page != PAGE_NONE && millis() - lastInput > MENU_TIMEOUT_MS)
        page = PAGE_NONE;

    if (prompt != PROMPT_NONE)
    {
        // SAVE 2? / LOAD 2?, and what is in the slot
        char top[9];
        snprintf(top, sizeof(top), "%s %u?", prompt == PROMPT_SAVE ? "SAVE" : "LOAD", (unsigned)(promptSlot + 1) % 10);
        const char *state = promptSlot == storageCurrentSlot() && storageSlotUsed(promptSlot) ? "current"
                            : storageSlotUsed(promptSlot)                                  ? (prompt == PROMPT_SAVE ? "replace" : "")
                                                                                            : "empty";
        char bottom[9];
        snprintf(bottom, sizeof(bottom), "%8s", state);
        displayMenu(top, bottom);
    }
    else
        show();
    displayStatus(presetsSelected(), fine, storageCurrentSlot(), storageUnsaved());
}

bool menuActive()
{
    return page != PAGE_NONE;
}

int menuSelected()
{
    return slider;
}

bool menuPadHeld()
{
    uint8_t pads = (1 << BUTTON_SET_1) | (1 << BUTTON_SET_2) | (1 << BUTTON_SET_3) | (1 << BUTTON_SET_4);
    return lastButtons & pads;
}
