#include "menu.h"

#include <Arduino.h>
#include <Encoder.h>

#include "config.h"
#include "display.h"
#include "modes.h"
#include "presets.h"
#include "sequencer.h"
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
    PAGE_SLIDER, // SET 1: the slider's settings
    PAGE_SEQ,    // SET 2: the sequencer
    PAGE_SYS,    // SET 4
    PAGE_ROW     // a row's settings, from its step view
};

// Settings on the slider page, in their order for each mode: first whether
// the slider runs an LFO, then the settings of that mode
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
    SETTING_LFO,
    SETTING_PHASE,
    SETTING_CTRL, // LFO: what the slider sets, the rate or the depth
    SETTING_FREQ, // LFO, the slider sets the depth: the rate
};
static const Setting SLIDE_SETTINGS[] = {SETTING_LFO, SETTING_QUANT, SETTING_ROOT, SETTING_BASE, SETTING_RANGE,
                                         SETTING_PLAY, SETTING_SLEW, SETTING_SLIDE_GATE};
static const Setting LFO_SETTINGS[] = {SETTING_LFO, SETTING_SHAPE, SETTING_CTRL, SETTING_RATE, SETTING_PHASE, SETTING_BASE, SETTING_RANGE,
                                       SETTING_DEPTH, SETTING_QUANT, SETTING_ROOT, SETTING_RESET, SETTING_LFO_GATE};
// the slider sets the depth: FREQ sets the rate within the RATE range
static const Setting LFO_DEPTH_SETTINGS[] = {SETTING_LFO, SETTING_SHAPE, SETTING_CTRL, SETTING_RATE, SETTING_FREQ, SETTING_PHASE, SETTING_BASE,
                                             SETTING_RANGE, SETTING_DEPTH, SETTING_QUANT, SETTING_ROOT, SETTING_RESET, SETTING_LFO_GATE};
static const int NUM_SLIDE_SETTINGS = sizeof(SLIDE_SETTINGS) / sizeof(SLIDE_SETTINGS[0]);
static const int NUM_LFO_SETTINGS = sizeof(LFO_SETTINGS) / sizeof(LFO_SETTINGS[0]);
static const int NUM_LFO_DEPTH_SETTINGS = sizeof(LFO_DEPTH_SETTINGS) / sizeof(LFO_DEPTH_SETTINGS[0]);
static const int FREQ_COARSE_STEP = 8;  // FREQ: of 255 along the RATE range, 1 with FINE
static const int FREQ_SYNC_STEP = 32;   // one sync division

// BASE and RANGE steps, in cents: quantized by octaves (semitones with FINE),
// unquantized by 1 V (0.1 V with FINE)
static const int OCTAVE_CENTS = 12 * SEMITONE_CENTS;
static const int VOLT_COARSE_STEP = CENTS_PER_VOLT;
static const int VOLT_FINE_STEP = CENTS_PER_VOLT / 10;

// SLEW: coarse steps through these times, FINE in steps of about 1%
static const int SLEW_STEPS[] = {0, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000};
static const int NUM_SLEW_STEPS = sizeof(SLEW_STEPS) / sizeof(SLEW_STEPS[0]);

// settings on the SEQ page
enum SeqItem
{
    SEQ_RUN,
    SEQ_CLOCK,
    SEQ_BPM,
    SEQ_DIV,
    SEQ_TOUCH,
    NUM_SEQ_ITEMS
};
static const char *SEQ_NAMES[NUM_SEQ_ITEMS] = {"RUN", "CLOCK", "BPM", "DIV", "TOUCH"};

// settings of a row, held open from its step view
enum RowItem
{
    ROW_DIR,
    ROW_SHIFT,
    ROW_LEN,
    ROW_SPEED,
    ROW_QUANT,
    ROW_ROOT,
    ROW_BASE,
    ROW_RANGE,
    NUM_ROW_ITEMS
};
static const char *ROW_NAMES[NUM_ROW_ITEMS] = {"DIR", "SHIFT", "LEN", "SPEED", "QUANT", "ROOT", "BASE", "RANGE"};

// WRITE: a slider held while the encoder turns gets its stage value edited
static int writeHeld = -1;
static bool writeEdited = false;

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
    SYS_AUTOSAVE,
    NUM_SYS_ITEMS
};
static const char *SYS_NAMES[NUM_SYS_ITEMS] = {"SMOOTH", "DEADBAND", "PRESSURE", "TOUCH", "BRIGHT", "TRIG LEN", "LOOP TRG", "AUTOSAVE"};

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

static const uint32_t DOUBLE_TAP_MS = 300; // a second tap this soon is a double tap
static const uint32_t PAD_HOLD_MS = 500;   // a SET pad held this long, untouched otherwise, opens a step view

static Page page = PAGE_NONE; // the settings page open
static int stepView = -1;     // or the step view of a row (0-3) open, also under its row settings
static int item = 0;
static int slider = 0;
static bool fine = false;
static uint32_t lastInput = 0;
static uint8_t lastButtons = 0;
static bool padUsed[4]; // the encoder turned or a slider was touched while the SET pad was held: no tap or hold

static int itemCount(Page p)
{
    switch (p)
    {
    case PAGE_SEQ:
        return NUM_SEQ_ITEMS;
    case PAGE_SLIDER:
        if (sliderSettings[slider].mode != MODE_LFO)
            return NUM_SLIDE_SETTINGS;
        return (sliderSettings[slider].lfoDepth & LFO_DEPTH_SLIDER) ? NUM_LFO_DEPTH_SETTINGS : NUM_LFO_SETTINGS;
    case PAGE_SYS:
        return NUM_SYS_ITEMS;
    case PAGE_ROW:
        return NUM_ROW_ITEMS;
    default:
        return 1;
    }
}

static Setting currentSetting()
{
    if (sliderSettings[slider].mode == MODE_LFO && (sliderSettings[slider].lfoDepth & LFO_DEPTH_SLIDER))
        return LFO_DEPTH_SETTINGS[min(item, NUM_LFO_DEPTH_SETTINGS - 1)];
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
    case SYS_AUTOSAVE:
        stepSetting(y.autosave, detents, 0, 1);
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
    case SYS_AUTOSAVE:
        snprintf(bottom, size, "%8s", y.autosave ? "on" : "off");
        break;
    }
}

static void change(int detents)
{
    SliderSettings &s = sliderSettings[slider];

    switch (page)
    {
    case PAGE_SEQ:
    {
        SeqSettings &q = seqSettings;
        switch (item)
        {
        case SEQ_RUN:
            sequencerSetRunning(detents > 0);
            break;
        case SEQ_CLOCK:
            stepSetting(q.clock, detents, 0, NUM_SLIDERS);
            break;
        case SEQ_BPM:
            q.bpm = constrain((int)q.bpm + detents * fastFactor(), 20, 300);
            break;
        case SEQ_DIV:
            stepSetting(q.div, detents, 0, NUM_SEQ_DIVISIONS - 1);
            break;
        case SEQ_TOUCH:
            stepSetting(q.touch, detents, 0, 1);
            break;
        }
        break;
    }

    case PAGE_ROW:
    {
        // bitfields: stepped through an int
        SeqRow &r = seqSettings.rows[stepView];
        int voltStep = r.scale ? (fine ? SEMITONE_CENTS * fastFactor() : OCTAVE_CENTS)
                               : (fine ? VOLT_FINE_STEP * fastFactor() : VOLT_COARSE_STEP);
        switch (item)
        {
        case ROW_DIR:
            r.dir = constrain((int)r.dir + detents, 0, NUM_SEQ_DIRECTIONS - 1);
            break;
        case ROW_SHIFT:
            sequencerShiftRow(stepView, detents);
            break;
        case ROW_LEN:
            sequencerChangeLength(stepView, detents);
            break;
        case ROW_SPEED:
            r.speed = constrain(r.speed + detents, SEQ_SPEED_MIN, SEQ_SPEED_MAX);
            break;
        case ROW_QUANT:
            r.scale = constrain((int)r.scale + detents, 0, NUM_SCALES);
            break;
        case ROW_ROOT:
            r.root = constrain((int)r.root + detents, 0, 11);
            break;
        case ROW_BASE:
            // the range keeps its size where it can
            r.base = constrain(r.base + detents * voltStep, 0, MAX_CENTS);
            r.range = constrain(r.range, -r.base, MAX_CENTS - r.base);
            break;
        case ROW_RANGE:
            r.range = constrain(r.range + detents * voltStep, -r.base, MAX_CENTS - r.base);
            break;
        }
        presetsRefresh(); // the output follows a new quantizer at once
        break;
    }

    case PAGE_SLIDER:
    {
        // big steps stay single, small steps speed up when turning fast
        int voltStep = s.scale ? (fine ? SEMITONE_CENTS * fastFactor() : OCTAVE_CENTS)
                               : (fine ? VOLT_FINE_STEP * fastFactor() : VOLT_COARSE_STEP);
        switch (currentSetting())
        {
        case SETTING_LFO:
            stepSetting(s.mode, detents, MODE_SLIDE, MODE_LFO);
            break;
        case SETTING_PHASE:
        {
            // 15 degree steps, wrapping around (stored as 0-255 for 0-360)
            int steps = lroundf(s.lfoPhase * LFO_PHASE_STEPS / 256.0f) + detents;
            steps = (steps % LFO_PHASE_STEPS + LFO_PHASE_STEPS) % LFO_PHASE_STEPS;
            s.lfoPhase = lroundf(steps * 256.0f / LFO_PHASE_STEPS) & 0xFF;
            break;
        }
        case SETTING_CTRL:
        {
            // the item stays on CTRL, which is at the same place in both lists
            uint8_t depth = s.lfoDepth;
            depth = detents > 0 ? (depth | LFO_DEPTH_SLIDER) : (depth & ~LFO_DEPTH_SLIDER);
            s.lfoDepth = depth;
            break;
        }
        case SETTING_FREQ:
        {
            int step = s.lfoRate == LFO_RATE_SYNC ? FREQ_SYNC_STEP : (fine ? 1 : FREQ_COARSE_STEP);
            s.lfoFreq = constrain((int)s.lfoFreq + detents * step, 0, 255);
            break;
        }
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
            s.lfoDepth = detents > 0 ? (s.lfoDepth | LFO_DEPTH_PRESSURE) : (s.lfoDepth & ~LFO_DEPTH_PRESSURE);
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
    case SETTING_LFO:
        snprintf(bottom, size, "%8s", s.mode == MODE_LFO ? "on" : "off");
        break;
    case SETTING_PHASE:
        snprintf(bottom, size, "%7u%c", (unsigned)lroundf(s.lfoPhase * 360.0f / 256) % 1000, 0xDF); // the degree sign
        break;
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
        snprintf(bottom, size, "%8s", (s.lfoDepth & LFO_DEPTH_PRESSURE) ? "press" : "fixed");
        break;
    case SETTING_CTRL:
        snprintf(bottom, size, "%8s", (s.lfoDepth & LFO_DEPTH_SLIDER) ? "depth" : "rate");
        break;
    case SETTING_FREQ:
        snprintf(bottom, size, "%8s", modesFreqText(slider));
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
    static const char *SETTING_NAMES[] = {"QUANT", "ROOT", "BASE", "RANGE", "PLAY", "SLEW", "GATE", "SHAPE",
                                          "RATE", "DEPTH", "RESET", "GATE", "LFO", "PHASE", "CTRL", "FREQ"};
    char top[9];
    char bottom[9];
    const SliderSettings &s = sliderSettings[slider];
    unsigned number = (unsigned)(slider + 1) % 10;

    switch (page)
    {
    case PAGE_SEQ:
    {
        const SeqSettings &q = seqSettings;
        snprintf(top, sizeof(top), "%s", SEQ_NAMES[item]);
        switch (item)
        {
        case SEQ_RUN:
            snprintf(bottom, sizeof(bottom), "%8s", sequencerRunning() ? "run" : "stop"); // any row
            break;
        case SEQ_CLOCK:
            if (q.clock)
            {
                char name[4];
                snprintf(name, sizeof(name), "#%u", (unsigned)q.clock % 10);
                snprintf(bottom, sizeof(bottom), "%8s", name);
            }
            else
                snprintf(bottom, sizeof(bottom), "%8s", "int");
            break;
        case SEQ_BPM:
            snprintf(bottom, sizeof(bottom), "%8u", (unsigned)q.bpm % 1000);
            break;
        case SEQ_DIV:
            snprintf(bottom, sizeof(bottom), "%8s", seqDivisionName(q.div));
            break;
        case SEQ_TOUCH:
            snprintf(bottom, sizeof(bottom), "%8s", q.touch ? "ignore" : "jump");
            break;
        }
        break;
    }

    case PAGE_ROW:
    {
        // "B ±BASE" over "C2  2.00": QUANT to RANGE read like a slider's
        static const Setting RANGE_SETTINGS[] = {SETTING_QUANT, SETTING_ROOT, SETTING_BASE, SETTING_RANGE};
        const SeqRow &r = seqSettings.rows[stepView];
        snprintf(top, sizeof(top), "%c %c%s", rowName(stepView), fine ? GLYPH_FINE : ' ', ROW_NAMES[item]);
        switch (item)
        {
        case ROW_DIR:
            snprintf(bottom, sizeof(bottom), "%8s", seqDirectionName(r.dir));
            break;
        case ROW_SHIFT:
            snprintf(bottom, sizeof(bottom), "from %3u", (unsigned)r.start + 1);
            break;
        case ROW_LEN:
            snprintf(bottom, sizeof(bottom), "%2u steps", (unsigned)r.length + 1);
            break;
        case ROW_SPEED:
            snprintf(bottom, sizeof(bottom), "%8s", seqSpeedName(r.speed));
            break;
        default:
        {
            SliderSettings range;
            sequencerRowRange(stepView, range);
            formatSetting(bottom, sizeof(bottom), RANGE_SETTINGS[item - ROW_QUANT], range);
            break;
        }
        }
        break;
    }

    case PAGE_SLIDER:
    {
        Setting setting = currentSetting();
        snprintf(top, sizeof(top), "#%u%c%s", number, fine ? GLYPH_FINE : ' ', SETTING_NAMES[setting]);
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
    // a touch while a SET pad is held makes the pad a modifier: no tap, no hold
    for (int i = 0; i < 4; i++)
        if (lastButtons & (1 << (BUTTON_SET_1 + i)))
            padUsed[i] = true;

    if (touched != slider)
        item = min(item, itemCount(page) - 1);
    slider = touched;
    if (menuActive())
        lastInput = millis();
}

static void closeViews()
{
    page = PAGE_NONE;
    stepView = -1;
}

// a single tap on a SET pad: its settings page; in a step view or a row's
// settings, on the page itself or on a pad without a page, closing it
static void padTap(int pad, Page padPage)
{
    if (stepView >= 0 || page == padPage || padPage == PAGE_NONE)
    {
        closeViews();
        return;
    }
    closeViews();
    page = padPage;
    item = 0;
}

void menuUpdate(uint8_t buttons)
{
    uint8_t pressed = buttons & ~lastButtons;
    uint8_t released = ~buttons & lastButtons;
    lastButtons = buttons;
    uint32_t now = millis();
    int detents = readDetents();

    // SET pads, from anywhere: a tap opens the pad's settings page (or closes
    // it, as it closes a step view and a row's settings; SET 3 has no page), a double tap starts or stops the pad's row, and holding opens the
    // row's step view. Holding the pad in its step view opens the row's
    // settings, holding it there again goes back. A pad held while the encoder
    // turns or a slider is touched is a modifier: no tap and no hold.
    static const Page PAGES[4] = {PAGE_SLIDER, PAGE_SEQ, PAGE_NONE, PAGE_SYS};
    static uint32_t pressMs[4];
    static bool holdDone[4];
    static int tapPad = -1; // a tap waiting to see whether a second one follows
    static uint32_t tapMs = 0;
    int heldPad = -1;
    for (int i = 0; i < 4; i++)
    {
        uint8_t bit = 1 << (BUTTON_SET_1 + i);
        if (pressed & bit)
        {
            pressMs[i] = now;
            padUsed[i] = false;
            holdDone[i] = false;
        }
        if (buttons & bit)
        {
            heldPad = i;
            if (!padUsed[i] && !holdDone[i] && now - pressMs[i] >= PAD_HOLD_MS)
            {
                holdDone[i] = true;
                if (stepView == i && page == PAGE_NONE)
                {
                    page = PAGE_ROW;
                    item = 0;
                }
                else if (stepView == i && page == PAGE_ROW)
                    page = PAGE_NONE;
                else
                {
                    closeViews();
                    stepView = i;
                    slider = sequencerRowStage(i);
                }
                lastInput = now;
            }
        }
        if (!(released & bit) || padUsed[i] || holdDone[i])
            continue;

        if (tapPad == i && now - tapMs < DOUBLE_TAP_MS)
        {
            // a double tap: the row runs or stops
            tapPad = -1;
            bool run = !sequencerRowRunning(i);
            sequencerSetRowRunning(i, run);
            char title[9];
            snprintf(title, sizeof(title), "%c %s", rowName(i), run ? "RUN" : "STOP");
            displayMessage(title);
        }
        else
        {
            if (tapPad >= 0)
                padTap(tapPad, PAGES[tapPad]);
            tapPad = i;
            tapMs = now;
        }
        lastInput = now;
    }
    if (tapPad >= 0 && now - tapMs >= DOUBLE_TAP_MS)
    {
        padTap(tapPad, PAGES[tapPad]);
        tapPad = -1;
    }

    // FINE: a tap (on lift) toggles fine steps; held while the encoder turns,
    // it edits the value instead and the toggle stays
    static bool fineTurned = false;
    uint8_t fineBit = 1 << BUTTON_FINE;
    bool fineHeld = buttons & fineBit;
    if (pressed & fineBit)
        fineTurned = false;
    static uint32_t fineTapMs = 0;
    if ((released & fineBit) && !fineTurned)
    {
        fine = !fine;
        if (now - fineTapMs < DOUBLE_TAP_MS)
        {
            // a double tap: run or stop, and the fine toggle stays as it was
            fine = !fine;
            sequencerSetRunning(!sequencerRunning()); // all rows: stop if any runs, else start
            displayMessage(sequencerRunning() ? "RUN" : "STOP");
            fineTapMs = 0;
        }
        else
            fineTapMs = now;
    }

    // SAVE / LOAD prompt
    uint8_t cancelPads = (1 << BUTTON_SET_1) | (1 << BUTTON_SET_2) | (1 << BUTTON_SET_3) | (1 << BUTTON_SET_4) | (1 << BUTTON_FINE);
    if (prompt != PROMPT_NONE && ((pressed & cancelPads) || now - promptInput > MENU_TIMEOUT_MS))
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
        else if (p == PROMPT_LOAD && promptSlot >= NUM_SLOTS)
        {
            // a template: a fresh start, unsaved
            storageLoadTemplate(promptSlot - NUM_SLOTS);
            displayMessage("INIT", storageTemplateName(promptSlot - NUM_SLOTS));
            prompt = PROMPT_NONE;
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
        promptInput = now;
    }

    if (detents != 0)
    {
        if (heldPad >= 0)
            padUsed[heldPad] = true;

        if (stepView >= 0 && page == PAGE_NONE && writeHeld >= 0)
        {
            // a held slider in a step view: its step value, semitones or 0.01 V with FINE
            int step = fine ? VAR_FINE_STEP : VAR_COARSE_STEP * fastFactor();
            presetNudge(writeHeld, stepView, detents, step);
            writeEdited = true;
            sinceDetent = 0;
            lastInput = now;
        }
        else if (stepView >= 0 && page == PAGE_NONE)
        {
            if (heldPad == stepView && fineHeld)
            {
                // a SET pad and FINE held: the row's length
                sequencerChangeLength(stepView, detents);
                fineTurned = true;
            }
            else if (heldPad == stepView)
            {
                // its SET pad held: the row's start and end move together, wrapping
                sequencerShiftRow(stepView, detents);
                slider = sequencerRowStage(stepView);
            }
            else if (fineHeld)
            {
                // FINE held: the shown stage's value, semitones or 0.01 V with FINE on
                int step = fine ? VAR_FINE_STEP : VAR_COARSE_STEP * fastFactor();
                presetNudge(slider, stepView, detents, step);
                fineTurned = true;
                sinceDetent = 0;
            }
            else if (!sequencerRowRunning(stepView))
            {
                // stopped: step the sequence by hand, as on the idle screen; the
                // stage shown follows the row's current step
                sequencerNudge(detents);
                slider = sequencerRowStage(stepView);
            }
            else
                slider = ((slider + detents) % NUM_SLIDERS + NUM_SLIDERS) % NUM_SLIDERS; // running: browse the stages
            lastInput = now;
        }
        else if (prompt != PROMPT_NONE)
        {
            // SAVE: the slots; LOAD: the slots and then the templates
            int count = prompt == PROMPT_LOAD ? NUM_SLOTS + NUM_TEMPLATES : NUM_SLOTS;
            promptSlot = ((promptSlot + detents) % count + count) % count;
            promptInput = now;
        }
        else if (!menuActive())
            sequencerNudge(detents); // idle and stopped: step the sequence by hand
        else if (page != PAGE_NONE)
        {
            if (fineHeld)
            {
                change(detents); // FINE held: the value
                fineTurned = true;
            }
            else if (page == PAGE_SLIDER && heldPad >= 0 && PAGES[heldPad] == PAGE_SLIDER)
            {
                // SET 1 held on the slider page: which slider is edited
                slider = ((slider + detents) % NUM_SLIDERS + NUM_SLIDERS) % NUM_SLIDERS;
                item = min(item, itemCount(page) - 1);
            }
            else
            {
                // the encoder alone scrolls through the settings, wrapping around
                int count = itemCount(page);
                item = ((item + detents) % count + count) % count;
            }
            lastInput = now;
        }
    }

    // settings close after a pause; a step view stays until it is closed, and
    // a row's settings time out back to it
    if (page != PAGE_NONE && now - lastInput > MENU_TIMEOUT_MS)
    {
        if (page == PAGE_ROW)
            page = PAGE_NONE;
        else
            closeViews();
    }

    // the step view is where touches write into its row
    if (sequencerWriteRow() != stepView)
        sequencerSetWriteRow(stepView);

    if (prompt != PROMPT_NONE)
    {
        // SAVE 2? / LOAD 2? and what is in the slot, or INIT? and a template
        char top[9];
        char bottom[9];
        if (promptSlot >= NUM_SLOTS)
        {
            snprintf(top, sizeof(top), "INIT?");
            snprintf(bottom, sizeof(bottom), "%8s", storageTemplateName(promptSlot - NUM_SLOTS));
        }
        else
        {
            snprintf(top, sizeof(top), "%s %u?", prompt == PROMPT_SAVE ? "SAVE" : "LOAD", (unsigned)(promptSlot + 1) % 10);
            const char *state = promptSlot == storageCurrentSlot() && storageSlotUsed(promptSlot) ? "current"
                                : storageSlotUsed(promptSlot)                                  ? (prompt == PROMPT_SAVE ? "replace" : "")
                                                                                                : "empty";
            snprintf(bottom, sizeof(bottom), "%8s", state);
        }
        displayMenu(top, bottom);
    }
    else if (stepView >= 0 && page == PAGE_NONE)
    {
        // the row and its direction over the shown stage's value ("A  fwd ±" /
        // "C#2 2.08"); the LEDs show which stages. While a SET pad is held:
        // the row's start, or with FINE its length
        const SeqRow &r = seqSettings.rows[stepView];
        char row = rowName(stepView);
        char top[9];
        char bottom[9];
        if (heldPad == stepView && padUsed[heldPad] && fineHeld)
        {
            snprintf(top, sizeof(top), "%c LEN", row);
            snprintf(bottom, sizeof(bottom), "%2u steps", (unsigned)r.length + 1);
        }
        else if (heldPad == stepView && padUsed[heldPad])
        {
            snprintf(top, sizeof(top), "%c SHIFT", row);
            snprintf(bottom, sizeof(bottom), "from %3u", (unsigned)r.start + 1);
        }
        else
        {
            snprintf(top, sizeof(top), "%c  %-4s%c", row, seqDirectionShortName(r.dir), fine ? GLYPH_FINE : ' ');
            SliderSettings range;
            sequencerRowRange(stepView, range);
            snprintf(bottom, sizeof(bottom), "%s", modesValueTextIn(range, presetValue(slider, stepView)));
        }
        displayMenu(top, bottom);
    }
    else
        show();
    int steps[NUM_VARS], lengths[NUM_VARS];
    for (int row = 0; row < NUM_VARS; row++)
    {
        steps[row] = sequencerRowRunning(row) ? sequencerRowStep(row) : -1; // a stopped row: an empty tube
        lengths[row] = seqSettings.rows[row].length + 1;
    }
    char tempo[4];
    if (seqSettings.clock)
        snprintf(tempo, sizeof(tempo), "#%u", (unsigned)seqSettings.clock % 10);
    else
        snprintf(tempo, sizeof(tempo), "%u", (unsigned)seqSettings.bpm % 1000);
    displayStatus(steps, lengths, sequencerRunning(), tempo, storageCurrentSlot(), storageUnsaved());
}

bool menuActive()
{
    return page != PAGE_NONE || stepView >= 0;
}

int menuSelected()
{
    return slider;
}

void menuWriteTouch(int touched, bool touching)
{
    if (touching)
        lastInput = millis(); // writing keeps the step view open
    if (touching && touched != writeHeld)
    {
        writeHeld = touched;
        writeEdited = false;
    }
    else if (!touching && touched == writeHeld)
        writeHeld = -1;
}

bool menuWriteEdited(int slider)
{
    return slider == writeHeld && writeEdited;
}

bool menuShifting()
{
    if (stepView < 0)
        return false;
    if (page == PAGE_ROW)
        return item <= ROW_LEN;
    uint8_t ownPad = 1 << (BUTTON_SET_1 + stepView);
    return page == PAGE_NONE && (lastButtons & ownPad) && padUsed[stepView];
}

bool menuPadHeld()
{
    uint8_t pads = (1 << BUTTON_SET_1) | (1 << BUTTON_SET_2) | (1 << BUTTON_SET_3) | (1 << BUTTON_SET_4);
    return lastButtons & pads;
}
