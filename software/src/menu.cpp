#include "menu.h"

#include <Arduino.h>
#include <Encoder.h>

#include "config.h"
#include "display.h"
#include "modes.h"
#include "pads.h"
#include "presets.h"
#include "sequencer.h"
#include "storage.h"
#include "system.h"

static const uint32_t MENU_TIMEOUT_MS = 5000; // back to the play screen without menu input

// Encoder: each detent changes the value by a step
static const int ENC_COUNTS_PER_DETENT = 4;
static const int VAR_COARSE_STEP = SEMITONE_CENTS; // var presets: a semitone per detent
static const int VAR_FINE_STEP = 12;               // or 0.01 V with fine steps
static const uint32_t FAST_TURN_MS = 40; // detents closer together than this sweep faster
static const int FAST_FACTOR = 4;

enum Page
{
    PAGE_NONE,
    PAGE_PLAY, // A: how the finger plays the slider (its mode and that mode's settings), or a pad's mode
    PAGE_OUT,  // B: the slider's output range and quantizer
    PAGE_TIME, // C: the slider's output processing in time: slew, delay, loop
    PAGE_SEQ,  // D: the sequencer
    PAGE_SYS,  // RUN held: the global settings
    PAGE_ROW   // a row's settings, from its step view
};

static bool sliderPage(Page p)
{
    return p == PAGE_PLAY || p == PAGE_OUT || p == PAGE_TIME;
}

// Settings on the slider pages, in their order: PLAY starts with whether the
// slider runs an LFO, then the settings of that mode
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
    SETTING_SLANT,
    SETTING_DELAY,
    SETTING_LOOP,
    SETTING_FREEZE,
    SETTING_PAD_MODE, // an XY pad's: what it does, xy or an attractor
    SETTING_PAD_RATE,
    SETTING_PAD_CHAOS,
    SETTING_PAD_NONE, // a pad on OUT or TIME: it has nothing there
    SETTING_MOD,      // OUT: what the mod jack puts out
    SETTING_ENV_LEN,  // MOD env: its settings
    SETTING_ENV_RISE,
    SETTING_ENV_CURVE,
    SETTING_ENV_HOLD,
    SETTING_ENV_VEL,
};
#define COUNT(list) (int)(sizeof(list) / sizeof(list[0]))
static const Setting PLAY_SLIDE[] = {SETTING_LFO, SETTING_PLAY, SETTING_SLIDE_GATE};
static const Setting PLAY_LFO[] = {SETTING_LFO, SETTING_SHAPE, SETTING_CTRL, SETTING_RATE, SETTING_PHASE,
                                   SETTING_DEPTH, SETTING_RESET, SETTING_LFO_GATE};
// the slider sets the depth: FREQ sets the rate within the RATE range
static const Setting PLAY_LFO_DEPTH[] = {SETTING_LFO, SETTING_SHAPE, SETTING_CTRL, SETTING_RATE, SETTING_FREQ, SETTING_PHASE,
                                         SETTING_DEPTH, SETTING_RESET, SETTING_LFO_GATE};
// OUT: the cv's range and quantizer, then MOD, and with MOD env its settings
// (HOLD and VEL only in SLIDE: an LFO sets the envelope off every cycle)
static const Setting OUT_SETTINGS[] = {SETTING_QUANT, SETTING_ROOT, SETTING_BASE, SETTING_RANGE, SETTING_MOD};
static const Setting OUT_ENV_SLIDE[] = {SETTING_QUANT, SETTING_ROOT, SETTING_BASE, SETTING_RANGE, SETTING_MOD,
                                        SETTING_ENV_LEN, SETTING_ENV_RISE, SETTING_ENV_CURVE, SETTING_ENV_HOLD, SETTING_ENV_VEL};
static const Setting OUT_ENV_LFO[] = {SETTING_QUANT, SETTING_ROOT, SETTING_BASE, SETTING_RANGE, SETTING_MOD,
                                      SETTING_ENV_LEN, SETTING_ENV_RISE, SETTING_ENV_CURVE};
// MOD's choices: QUAD only on an LFO
static const ModSource MOD_SLIDE_CHOICES[] = {MOD_PRESS, MOD_ENV, MOD_INV};
static const ModSource MOD_LFO_CHOICES[] = {MOD_PRESS, MOD_ENV, MOD_QUAD, MOD_INV};
static const int ENV_LEN_COARSE_STEP = 4; // of 127: about 1.4 times longer per detent, 1 with fine steps
static const int ENV_RISE_STEP = 10;      // %, 1 with fine steps
static const Setting TIME_SETTINGS[] = {SETTING_SLEW, SETTING_SLANT, SETTING_DELAY, SETTING_LOOP, SETTING_FREEZE};
// an XY pad selected: its settings are on PLAY
static const Setting PAD_XY_SETTINGS[] = {SETTING_PAD_MODE};
static const Setting PAD_RUN_SETTINGS[] = {SETTING_PAD_MODE, SETTING_PAD_RATE, SETTING_PAD_CHAOS};
static const Setting PAD_NONE[] = {SETTING_PAD_NONE};
static const int PAD_RATE_FINE_STEP = 1;   // of 100 along the rate range
static const int PAD_RATE_COARSE_STEP = 5;
static const int PAD_CHAOS_STEP = 5;       // %, 1 with fine steps
static const int FREQ_COARSE_STEP = 8;  // FREQ: of 255 along the RATE range, 1 with fine steps
static const int FREQ_SYNC_STEP = 32;   // one sync division

// BASE and RANGE steps, in cents: quantized by octaves (semitones with fine steps),
// unquantized by 1 V (0.1 V with fine steps)
static const int OCTAVE_CENTS = 12 * SEMITONE_CENTS;
static const int VOLT_COARSE_STEP = CENTS_PER_VOLT;
static const int VOLT_FINE_STEP = CENTS_PER_VOLT / 10;

// SLEW and DELAY: coarse steps through these times, fine steps of about 1%
static const int SLEW_STEPS[] = {0, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000};
static const int DELAY_STEPS[] = {0, 10, 20, 50, 100, 200, 250, 500, 750, 1000, 1500, 2000, 3000, 4000, 6000, 8000};
static const int SLANT_STEP = 10; // %, 1 with fine steps
static const int LOOP_STEP = 5;   // %, 1 with fine steps

// settings on the SEQ page
enum SeqItem
{
    SEQ_CLOCK,
    SEQ_BPM,
    SEQ_DIV,
    SEQ_TOUCH,
    NUM_SEQ_ITEMS
};
static const char *SEQ_NAMES[NUM_SEQ_ITEMS] = {"CLOCK", "BPM", "DIV", "TOUCH"};

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

// PATCH: a tap opens the LOAD prompt, a hold the SAVE prompt; the encoder
// picks the slot, a tap of PATCH confirms; A-D, EDIT or a pause cancels
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
static const uint32_t PAD_HOLD_MS = 500;   // a button held this long, untouched otherwise: a hold (a step view, SYS, SAVE)

static Page page = PAGE_NONE; // the settings page open
static int stepView = -1;     // or the step view of a row (0-3) open, also under its row settings
static int item = 0;
static int slider = 0; // the slider edited, or an XY pad (NUM_SLIDERS and up)
static bool fine = false;
static uint32_t lastInput = 0;
static uint8_t lastButtons = 0;
static bool padUsed[4]; // the encoder turned or a slider was touched while the row button was held: no tap or hold

// the settings of a slider page, for the slider edited; returns how many
static bool padSelected()
{
    return slider >= NUM_SLIDERS;
}

// the slider edited, or the last one while a pad is: pad settings are elsewhere
static SliderSettings &editedSlider()
{
    return sliderSettings[min(slider, NUM_SLIDERS - 1)];
}

static int pageSettings(Page p, const Setting *&list)
{
    if (padSelected() && p != PAGE_PLAY)
    {
        list = PAD_NONE;
        return COUNT(PAD_NONE);
    }
    if (padSelected())
    {
        bool running = padSettings[slider - NUM_SLIDERS].mode != PAD_XY;
        list = running ? PAD_RUN_SETTINGS : PAD_XY_SETTINGS;
        return running ? COUNT(PAD_RUN_SETTINGS) : COUNT(PAD_XY_SETTINGS);
    }
    const SliderSettings &s = editedSlider();
    switch (p)
    {
    case PAGE_OUT:
        if ((s.mode == MODE_LFO ? s.modLfo : s.modSlide) != MOD_ENV)
        {
            list = OUT_SETTINGS;
            return COUNT(OUT_SETTINGS);
        }
        if (s.mode == MODE_LFO)
        {
            list = OUT_ENV_LFO;
            return COUNT(OUT_ENV_LFO);
        }
        list = OUT_ENV_SLIDE;
        return COUNT(OUT_ENV_SLIDE);
    case PAGE_TIME:
        list = TIME_SETTINGS;
        return COUNT(TIME_SETTINGS);
    default:
        if (s.mode != MODE_LFO)
        {
            list = PLAY_SLIDE;
            return COUNT(PLAY_SLIDE);
        }
        if (s.lfoDepth & LFO_DEPTH_SLIDER)
        {
            list = PLAY_LFO_DEPTH;
            return COUNT(PLAY_LFO_DEPTH);
        }
        list = PLAY_LFO;
        return COUNT(PLAY_LFO);
    }
}

static int itemCount(Page p)
{
    const Setting *list;
    switch (p)
    {
    case PAGE_PLAY:
    case PAGE_OUT:
    case PAGE_TIME:
        return pageSettings(p, list);
    case PAGE_SEQ:
        return NUM_SEQ_ITEMS;
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
    const Setting *list;
    int count = pageSettings(page, list);
    return list[min(item, count - 1)];
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

// SLEW and DELAY: the next coarse time up or down, or about 1% steps when fine
template <int N>
static void stepTime(uint16_t &value, int detents, const int (&steps)[N])
{
    if (fine)
    {
        int step = value < 100 ? 1 : (value < 1000 ? 10 : 100);
        value = constrain((int)value + detents * step * fastFactor(), 0, steps[N - 1]);
        return;
    }
    for (int d = 0; d < abs(detents); d++)
    {
        int next = value;
        if (detents > 0)
        {
            for (int i = 0; i < N; i++)
                if (steps[i] > value)
                {
                    next = steps[i];
                    break;
                }
        }
        else
        {
            for (int i = N - 1; i >= 0; i--)
                if (steps[i] < value)
                {
                    next = steps[i];
                    break;
                }
        }
        value = next;
    }
}

static void changeSystem(int sysItem, int detents)
{
    SystemSettings &y = systemSettings;
    switch (sysItem)
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

static void formatSystem(int sysItem, char *bottom, size_t size)
{
    const SystemSettings &y = systemSettings;
    switch (sysItem)
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
    SliderSettings &s = editedSlider();

    switch (page)
    {
    case PAGE_SYS:
        changeSystem(item, detents);
        break;

    case PAGE_SEQ:
    {
        SeqSettings &q = seqSettings;
        switch (item)
        {
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

    case PAGE_PLAY:
    case PAGE_OUT:
    case PAGE_TIME:
    {
        // big steps stay single, small steps speed up when turning fast
        int voltStep = s.scale ? (fine ? SEMITONE_CENTS * fastFactor() : OCTAVE_CENTS)
                               : (fine ? VOLT_FINE_STEP * fastFactor() : VOLT_COARSE_STEP);
        switch (currentSetting())
        {
        case SETTING_PAD_NONE:
            break;
        case SETTING_MOD:
        {
            // through the mode's choices
            bool lfo = s.mode == MODE_LFO;
            const ModSource *choices = lfo ? MOD_LFO_CHOICES : MOD_SLIDE_CHOICES;
            int count = lfo ? COUNT(MOD_LFO_CHOICES) : COUNT(MOD_SLIDE_CHOICES);
            uint8_t &mod = lfo ? s.modLfo : s.modSlide;
            int at = 0;
            for (int i = 0; i < count; i++)
                if (choices[i] == mod)
                    at = i;
            mod = choices[constrain(at + detents, 0, count - 1)];
            break;
        }
        case SETTING_ENV_LEN:
            stepSetting(s.envLength, detents * (fine ? fastFactor() : ENV_LEN_COARSE_STEP), 0, ENV_LENGTH_STEPS);
            break;
        case SETTING_ENV_RISE:
            stepSetting(s.envRise, detents * (fine ? fastFactor() : ENV_RISE_STEP), 0, 100);
            break;
        case SETTING_ENV_CURVE:
            s.envCurve = constrain(s.envCurve + detents * ENV_CURVE_STEP, -100, 100);
            break;
        case SETTING_ENV_HOLD:
            stepSetting(s.envHold, detents, 0, 1);
            break;
        case SETTING_ENV_VEL:
            stepSetting(s.envVel, detents, 0, 1);
            break;
        case SETTING_PAD_MODE:
            stepSetting(padSettings[slider - NUM_SLIDERS].mode, detents, 0, NUM_PAD_MODES - 1);
            break;
        case SETTING_PAD_RATE:
            stepSetting(padSettings[slider - NUM_SLIDERS].rate, detents * (fine ? PAD_RATE_FINE_STEP * fastFactor() : PAD_RATE_COARSE_STEP),
                        0, PAD_RATE_STEPS);
            break;
        case SETTING_PAD_CHAOS:
            stepSetting(padSettings[slider - NUM_SLIDERS].chaos, detents * (fine ? fastFactor() : PAD_CHAOS_STEP), 0, PAD_CHAOS_MAX);
            break;
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
            // the item stays on SETS, which is at the same place in both lists
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
            stepTime(s.slewMs, detents, SLEW_STEPS);
            break;
        case SETTING_SLANT:
            s.slant = constrain(s.slant + detents * (fine ? fastFactor() : SLANT_STEP), -100, 100);
            break;
        case SETTING_DELAY:
            stepTime(s.delayMs, detents, DELAY_STEPS);
            break;
        case SETTING_LOOP:
            s.loop = constrain((int)s.loop + detents * (fine ? fastFactor() : LOOP_STEP), 0, 100);
            break;
        case SETTING_FREEZE:
            stepSetting(s.freeze, detents, 0, NUM_FREEZE - 1);
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
    case SETTING_SLANT:
        snprintf(bottom, size, "%6d %%", constrain((int)s.slant, -100, 100));
        break;
    case SETTING_DELAY:
        if (s.delayMs == 0)
            snprintf(bottom, size, "%8s", "off");
        else
            snprintf(bottom, size, "%5u ms", (unsigned)s.delayMs % 100000);
        break;
    case SETTING_LOOP:
        snprintf(bottom, size, "%6u %%", (unsigned)s.loop % 1000);
        break;
    case SETTING_FREEZE:
        snprintf(bottom, size, "%8s", freezeName(s.freeze));
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
    case SETTING_MOD:
        snprintf(bottom, size, "%8s", modSourceName(s.mode == MODE_LFO ? s.modLfo : s.modSlide));
        break;
    case SETTING_ENV_LEN:
        snprintf(bottom, size, "%s", envLengthText(s.envLength));
        break;
    case SETTING_ENV_RISE:
        snprintf(bottom, size, "%6u %%", (unsigned)s.envRise % 1000);
        break;
    case SETTING_ENV_CURVE:
        // "exp  50" / "linear" / "log  50"
        if (s.envCurve == 0)
            snprintf(bottom, size, "%8s", "linear");
        else
            snprintf(bottom, size, "%s %3d%%", s.envCurve > 0 ? "exp" : "log", abs((int)s.envCurve) % 1000);
        break;
    case SETTING_ENV_HOLD:
    case SETTING_ENV_VEL:
        snprintf(bottom, size, "%8s", (setting == SETTING_ENV_HOLD ? s.envHold : s.envVel) ? "on" : "off");
        break;
    default: // the pads' settings, shown on their own
        break;
    }
}

// the two screen lines for the current page and setting
static void show()
{
    // the LFO setting of what the slider sets shows as SETS
    static const char *SETTING_NAMES[] = {"QUANT", "ROOT", "BASE", "RANGE", "PLAY", "SLEW", "GATE", "SHAPE", "RATE", "DEPTH",
                                          "RESET", "GATE", "LFO", "PHASE", "SETS", "FREQ", "SLANT", "DELAY", "LOOP", "FREEZE",
                                          "MODE", "RATE", "CHAOS", "", "MOD", "LEN", "RISE", "CURVE", "HOLD", "VEL"};
    char top[9];
    char bottom[9];
    const SliderSettings &s = editedSlider();
    unsigned number = (unsigned)(slider + 1) % 10;

    switch (page)
    {
    case PAGE_SYS:
        snprintf(top, sizeof(top), "%s", SYS_NAMES[item]);
        formatSystem(item, bottom, sizeof(bottom));
        break;

    case PAGE_SEQ:
    {
        const SeqSettings &q = seqSettings;
        snprintf(top, sizeof(top), "%s", SEQ_NAMES[item]);
        switch (item)
        {
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

    case PAGE_PLAY:
    case PAGE_OUT:
    case PAGE_TIME:
    {
        Setting setting = currentSetting();
        if (padSelected())
        {
            // "P1 MODE" over "lorenz"
            const PadSettings &p = padSettings[slider - NUM_SLIDERS];
            snprintf(top, sizeof(top), "P%u%c%s", (unsigned)(slider - NUM_SLIDERS + 1) % 10, fine ? GLYPH_FINE : ' ', SETTING_NAMES[setting]);
            if (setting == SETTING_PAD_NONE)
            {
                // nothing on OUT or TIME: the pad's settings are on PLAY
                snprintf(top, sizeof(top), "P%u %s", (unsigned)(slider - NUM_SLIDERS + 1) % 10, page == PAGE_OUT ? "OUT" : "TIME");
                snprintf(bottom, sizeof(bottom), "%8s", "on PLAY");
            }
            else if (setting == SETTING_PAD_MODE)
                snprintf(bottom, sizeof(bottom), "%8s", padModeName(p.mode));
            else if (setting == SETTING_PAD_RATE)
                snprintf(bottom, sizeof(bottom), "%s", padRateText(p.rate));
            else
                snprintf(bottom, sizeof(bottom), "%6u %%", (unsigned)p.chaos % 1000);
            break;
        }
        snprintf(top, sizeof(top), "#%u%c%s", number, fine ? GLYPH_FINE : ' ', SETTING_NAMES[setting]);
        formatSetting(bottom, sizeof(bottom), setting, s);
        break;
    }

    default:
        return;
    }
    displayMenu(top, bottom);
}

void menuSelect(int touched)
{
    // a step view edits the stages: a pad is not one
    if (touched >= NUM_SLIDERS && stepView >= 0)
        return;

    // a touch while a row button is held makes it a modifier: no tap, no hold
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

// a single tap on a row button A-D: its settings page; in a step view or a
// row's settings, or on the page itself, closing it
static void padTap(int pad, Page padPage)
{
    if (stepView >= 0 || page == padPage)
    {
        closeViews();
        return;
    }
    closeViews();
    page = padPage;
    item = 0;
}

// RUN and PATCH: a tap acts on lift, a hold once it has been held long enough
struct TapHold
{
    uint32_t pressMs;
    bool holdDone;
};

// returns 1 for a tap, 2 for a hold, else 0
static int tapOrHold(TapHold &b, uint8_t bit, uint8_t buttons, uint8_t pressed, uint8_t released, uint32_t now)
{
    if (pressed & bit)
    {
        b.pressMs = now;
        b.holdDone = false;
    }
    if ((buttons & bit) && !b.holdDone && now - b.pressMs >= PAD_HOLD_MS)
    {
        b.holdDone = true;
        return 2;
    }
    if ((released & bit) && !b.holdDone)
        return 1;
    return 0;
}

void menuUpdate(uint8_t buttons)
{
    uint8_t pressed = buttons & ~lastButtons;
    uint8_t released = ~buttons & lastButtons;
    lastButtons = buttons;
    uint32_t now = millis();
    int detents = readDetents();

    // A-D, from anywhere: a tap opens the button's settings page (or closes
    // it, as it closes a step view and a row's settings), a double tap starts
    // or stops its row, and holding opens the row's step view. Holding it in
    // its step view opens the row's settings, holding it there again goes
    // back. A button held while the encoder turns or a slider is touched is a
    // modifier: no tap and no hold.
    static const Page PAGES[4] = {PAGE_PLAY, PAGE_OUT, PAGE_TIME, PAGE_SEQ};
    static uint32_t pressMs[4];
    static bool holdDone[4];
    static int tapPad = -1; // the last single tap, which a second one soon after makes a double tap
    static uint32_t tapMs = 0;
    // the views as they were before that tap: a double tap only runs or stops
    // the row, so the first tap's page does not stay open
    static Page tapPage = PAGE_NONE;
    static int tapStepView = -1;
    static int tapItem = 0;
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
            // a double tap: the row runs or stops, and the views are back as
            // they were before the first tap, which acted at once
            tapPad = -1;
            page = tapPage;
            stepView = tapStepView;
            item = tapItem;
            bool run = !sequencerRowRunning(i);
            sequencerSetRowRunning(i, run);
            char title[9];
            snprintf(title, sizeof(title), "%c %s", rowName(i), run ? "RUN" : "STOP");
            displayMessage(title);
        }
        else
        {
            // a single tap acts at once, without waiting for a second one
            tapPage = page;
            tapStepView = stepView;
            tapItem = item;
            padTap(i, PAGES[i]);
            tapPad = i;
            tapMs = now;
        }
        lastInput = now;
    }

    // EDIT: a tap (on lift) toggles fine steps; held while the encoder turns,
    // it changes the value instead and the toggle stays
    static bool editTurned = false;
    uint8_t editBit = 1 << BUTTON_EDIT;
    bool editHeld = buttons & editBit;
    if (pressed & editBit)
        editTurned = false;
    if ((released & editBit) && !editTurned)
        fine = !fine;

    // RUN: a tap starts all rows, or stops them if any runs; holding it opens
    // the SYS settings, or closes them
    static TapHold run;
    switch (tapOrHold(run, 1 << BUTTON_RUN, buttons, pressed, released, now))
    {
    case 1:
        sequencerSetRunning(!sequencerRunning());
        displayMessage(sequencerRunning() ? "RUN" : "STOP");
        break;
    case 2:
        if (page == PAGE_SYS)
            closeViews();
        else
        {
            closeViews();
            page = PAGE_SYS;
            item = 0;
        }
        lastInput = now;
        break;
    }

    // PATCH: a tap opens the LOAD prompt, a hold the SAVE prompt; in either, a
    // tap confirms (a hold in the LOAD prompt switches to SAVE)
    uint8_t cancelPads = (1 << BUTTON_SET_1) | (1 << BUTTON_SET_2) | (1 << BUTTON_SET_3) | (1 << BUTTON_SET_4) | (1 << BUTTON_EDIT);
    if (prompt != PROMPT_NONE && ((pressed & cancelPads) || now - promptInput > MENU_TIMEOUT_MS))
        prompt = PROMPT_NONE;
    static TapHold patch;
    int patchAction = tapOrHold(patch, 1 << BUTTON_PATCH, buttons, pressed, released, now);
    if (patchAction == 2 && prompt != PROMPT_SAVE)
    {
        prompt = PROMPT_SAVE;
        promptSlot = storageCurrentSlot();
        promptInput = now;
    }
    else if (patchAction == 1 && prompt == PROMPT_NONE)
    {
        prompt = PROMPT_LOAD;
        promptSlot = storageCurrentSlot();
        promptInput = now;
    }
    else if (patchAction == 1 && prompt == PROMPT_LOAD && promptSlot >= NUM_SLOTS)
    {
        // a template: a fresh start, unsaved
        storageLoadTemplate(promptSlot - NUM_SLOTS);
        displayMessage("INIT", storageTemplateName(promptSlot - NUM_SLOTS));
        prompt = PROMPT_NONE;
    }
    else if (patchAction == 1)
    {
        char line[9];
        snprintf(line, sizeof(line), "slot %u", (unsigned)(promptSlot + 1) % 10);
        if (prompt == PROMPT_SAVE)
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

    if (detents != 0)
    {
        if (heldPad >= 0)
            padUsed[heldPad] = true;
        if (editHeld)
            editTurned = true; // EDIT held while turning is no tap, wherever it turned

        if (stepView >= 0 && page == PAGE_NONE && writeHeld >= 0)
        {
            // a held slider in a step view: its step value, semitones or 0.01 V with fine steps
            int step = fine ? VAR_FINE_STEP : VAR_COARSE_STEP * fastFactor();
            presetNudge(writeHeld, stepView, detents, step);
            writeEdited = true;
            sinceDetent = 0;
            lastInput = now;
        }
        else if (stepView >= 0 && page == PAGE_NONE)
        {
            if (heldPad == stepView && editHeld)
            {
                // its row button and EDIT held: the row's length
                sequencerChangeLength(stepView, detents);
                editTurned = true;
            }
            else if (heldPad == stepView)
            {
                // its row button held: the row's start and end move together, wrapping
                sequencerShiftRow(stepView, detents);
                slider = sequencerRowStage(stepView);
            }
            else if (editHeld)
            {
                // EDIT held: the shown stage's value, semitones or 0.01 V with fine steps
                int step = fine ? VAR_FINE_STEP : VAR_COARSE_STEP * fastFactor();
                presetNudge(slider, stepView, detents, step);
                editTurned = true;
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
            if (editHeld)
            {
                change(detents); // EDIT held: the value
                editTurned = true;
            }
            else if (sliderPage(page) && heldPad >= 0 && PAGES[heldPad] == page)
            {
                // the page's own button held on a slider page: which slider is
                // edited, and on PLAY past slider 8 the XY pads
                int count = page == PAGE_PLAY ? NUM_SENSORS : NUM_SLIDERS;
                slider = ((min(slider, count - 1) + detents) % count + count) % count;
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

    // fine steps last one editing session: they end when the menu closes
    if (!menuActive())
        fine = false;

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
        // "C#2 2.08"); the LEDs show which stages. While its row button is held:
        // the row's start, or with EDIT its length
        const SeqRow &r = seqSettings.rows[stepView];
        char row = rowName(stepView);
        char top[9];
        char bottom[9];
        if (heldPad == stepView && padUsed[heldPad] && editHeld)
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
