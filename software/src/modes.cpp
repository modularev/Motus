#include "modes.h"

#include <Arduino.h>
#include <EEPROM.h>

#include "analogout.h"
#include "config.h"
#include "sequencer.h"
#include "system.h"

static const int STORAGE_ADDRESS = 128;           // the single save of earlier firmware
static const uint32_t STORAGE_MAGIC = 0x4D4F5337; // "MOS7"
static const uint32_t STORAGE_MAGIC_V6 = 0x4D4F5336; // "MOS6": the same fields up to range, the glide time as an index
static const int V6_SIZE = 16;

static const float NOTE_HYSTERESIS = 0.2f; // of a note step: a resting finger does not flicker between notes
static const uint32_t RETRIGGER_MS = 5;    // gate low on a new note
static const int MAX_SEMITONE = MAX_CENTS / SEMITONE_CENTS;
static const int JUMP = 100;               // a position step this large is a second-finger jump: a new note in touch mode
static const int MAX_NOTES = MAX_SEMITONE + 1;

// glide times of the earlier settings, by index
static const int OLD_SLEW_TIMES[9] = {0, 5, 10, 20, 50, 100, 200, 500, 1000};

// LFO rate ranges: the slider spans its range exponentially, bottom to top.
struct RateRange
{
    const char *name;
    float low;  // Hz
    float high; // Hz
};
static const RateRange LFO_RATES[NUM_LFO_RATES] = {
    {"slow", 0.02f, 1.0f},
    {"mid", 0.2f, 5.0f},
    {"fast", 1.0f, 25.0f},
};
// tempo sync: the slider picks one of these cycle lengths, in beats
struct SyncDivision
{
    const char *name;
    float beats;
};
static const SyncDivision SYNC_DIVISIONS[] = {
    {"4 bar", 16}, {"2 bar", 8}, {"1 bar", 4}, {"1/2", 2}, {"1/4", 1}, {"1/8", 0.5f}, {"1/16", 0.25f}, {"1/32", 0.125f},
};
static const int NUM_SYNC_DIVISIONS = sizeof(SYNC_DIVISIONS) / sizeof(SYNC_DIVISIONS[0]);

static const char *LFO_SHAPE_NAMES[NUM_LFO_SHAPES] = {"sine", "tri", "ramp", "saw", "square", "s&h"};
static const uint32_t DEPTH_HOLD_MS = 30;       // pressure is sampled this often, so a lift does not set the depth
static const uint32_t OUTPUT_INTERVAL_US = 500; // LFOs and glides are written at most this often, between the sensor reads

// gate settings
static const int SLIDE_GATE_RETRIG = 1;
static const int SLIDE_GATE_TRIG = 2;
static const int LFO_GATE_CYCLE = 1;
static const int LFO_GATE_TRIG = 2;
static const int LFO_GATE_NOTE = 3;
static const uint32_t TRIG_GAP_MS = 10;   // at least this between triggers, so one touch never fires twice
static const uint32_t TRIG_TICK_US = 100; // trigger gates are switched by a timer this often

struct ScaleInfo
{
    const char *name;
    uint8_t count;
    uint8_t intervals[12];
};

static const ScaleInfo SCALES[NUM_SCALES] = {
    {"chrom", 12, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
    {"major", 7, {0, 2, 4, 5, 7, 9, 11}},
    {"minor", 7, {0, 2, 3, 5, 7, 8, 10}},
    {"maj pent", 5, {0, 2, 4, 7, 9}},
    {"min pent", 5, {0, 3, 5, 7, 10}},
    {"dorian", 7, {0, 2, 3, 5, 7, 9, 10}},
    {"harm min", 7, {0, 2, 3, 5, 7, 8, 11}},
    {"blues", 6, {0, 3, 5, 6, 7, 10}},
    {"whole", 6, {0, 2, 4, 6, 8, 10}},
};

static const char *MODE_NAMES[NUM_MODES] = {"slide", "lfo"};
static const char *NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static const SliderSettings DEFAULT_SETTINGS = {MODE_SLIDE, 0, 0, 0, 0, 0, LFO_SINE, 1, 0, 0, 0, 0, 0, 8 * CENTS_PER_VOLT, 0};

struct Storage // as saved by earlier firmware
{
    uint32_t magic;
    SliderSettings settings[NUM_SLIDERS];
};

// Settings as saved before BASE and RANGE (POS, NOTE and LFO modes). Older
// layouts only have the first fields, the rest keep these defaults.
struct OldSettings
{
    uint8_t mode; // 0 POS, 1 NOTE, 2 LFO
    uint8_t scale, root, octave, range, retrigger, latch, slew;
    uint8_t lfoShape, lfoRate, lfoLevel, lfoDepth, lfoReset, lfoGate, posGate, lfoScale;
};
static const OldSettings OLD_DEFAULTS = {0, SCALE_MAJOR, 0, 2, 2, 0, 0, 0, LFO_SINE, 1, 5, 0, 0, 0, 0, 0};

struct OldLayout
{
    uint32_t magic;
    int size; // bytes per slider
};
static const OldLayout OLD_LAYOUTS[] = {
    {0x4D4F5335, 16}, // "MOS5"
    {0x4D4F5334, 15}, // "MOS4": without the LFO quantizer
    {0x4D4F5333, 14}, // "MOS3": without the POS gate setting
    {0x4D4F5332, 8},  // "MOS2": without the LFO settings
    {0x4D4F5331, 6},  // "MOS1": without latch and slew
};

SliderSettings sliderSettings[NUM_SLIDERS];

// keep base + range inside the outputs
static void clampRange(SliderSettings &s)
{
    s.base = constrain(s.base, 0, MAX_CENTS);
    s.range = constrain(s.range, -s.base, MAX_CENTS - s.base);
}

// the old modes in the new language: POS and NOTE become SLIDE, OCT and
// RANGE (or the LFO level) become BASE and RANGE
static SliderSettings fromOld(const OldSettings &o)
{
    SliderSettings s = DEFAULT_SETTINGS;
    s.root = o.root;
    s.latch = o.latch;
    s.slewMs = OLD_SLEW_TIMES[min((int)o.slew, 8)];
    s.lfoShape = o.lfoShape;
    s.lfoRate = o.lfoRate;
    s.lfoDepth = o.lfoDepth;
    s.lfoReset = o.lfoReset;
    s.lfoGate = o.lfoGate;

    switch (o.mode)
    {
    case 1: // NOTE
        s.mode = MODE_SLIDE;
        s.scale = o.scale + 1;
        s.base = o.octave * CENTS_PER_VOLT + o.root * SEMITONE_CENTS;
        s.range = o.range * CENTS_PER_VOLT;
        s.slideGate = o.retrigger;
        break;
    case 2: // LFO
        s.mode = MODE_LFO;
        s.scale = o.lfoScale;
        s.base = 0;
        s.range = o.lfoLevel * CENTS_PER_VOLT;
        break;
    default: // POS
        s.mode = MODE_SLIDE;
        s.scale = 0;
        s.base = 0;
        s.range = MAX_CENTS;
        s.slideGate = o.posGate ? SLIDE_GATE_TRIG : 0;
        break;
    }
    clampRange(s);
    return s;
}

static int currentStep[NUM_SLIDERS];
static int currentSemitone[NUM_SLIDERS];
static int noteCount[NUM_SLIDERS]; // notes in the slider's range, for the LED dot
static uint32_t gateLowUntil[NUM_SLIDERS];
static bool lastGate[NUM_SLIDERS];
static int lastPosition[NUM_SLIDERS];

// what a SLIDE output was taken from, so a change of the settings can move it at once
struct SlideSource
{
    bool valid;
    int position;
    uint8_t scale;
    uint8_t root;
    int16_t base;
    int16_t range;
};
static SlideSource source[NUM_SLIDERS];

static bool sourceChanged(int slider)
{
    const SliderSettings &s = sliderSettings[slider];
    const SlideSource &src = source[slider];
    return src.scale != s.scale || src.root != s.root || src.base != s.base || src.range != s.range;
}

// glide from one value to the next
struct Glide
{
    float from;
    float to;
    float volts; // current output
    uint32_t startMs;
};
static Glide glide[NUM_SLIDERS];

struct Lfo
{
    float phase;    // 0-1
    int position;   // the slider position that sets the rate
    float depth;    // 0-1, from the pressure
    float value;    // 0-1, random level for S&H
    float pressure; // recent pressure, a little behind, for the depth on release
    float earlier;
    uint32_t sampledMs;
    uint32_t lastUs;
    uint32_t writtenUs;
    int code;     // DAC value written
    int semitone; // quantized note, -1 before the first
};
static Lfo lfo[NUM_SLIDERS];

// Triggers are shorter than a sensor scan, so their gates are not written by
// the main loop but by a timer interrupt, which switches them on and off on
// time. Gate pins are plain GPIO, so this is safe next to the I2C traffic.
static IntervalTimer trigTimer;
// one more than the sliders: m.gate for the sequencer
static const int MASTER_TRIG = NUM_SLIDERS;
static volatile bool trigActive[NUM_SLIDERS + 1];
static volatile uint32_t trigEndUs[NUM_SLIDERS + 1];
static uint32_t lastTrigMs[NUM_SLIDERS + 1];
static volatile uint32_t gateRises[NUM_SLIDERS];

static bool usesTrig(const SliderSettings &s)
{
    if (s.mode == MODE_LFO)
        return s.lfoGate == LFO_GATE_TRIG || s.lfoGate == LFO_GATE_NOTE;
    return s.slideGate == SLIDE_GATE_TRIG;
}

static void fireTrig(int slider)
{
    if (millis() - lastTrigMs[slider] < TRIG_GAP_MS)
        return;
    lastTrigMs[slider] = millis();
    trigEndUs[slider] = micros() + systemSettings.trigMs * 1000; // TRIG LEN system setting
    trigActive[slider] = true;
    if (slider < NUM_SLIDERS)
        gateRises[slider]++;
}

void modesFireMasterTrig()
{
    fireTrig(MASTER_TRIG);
}

uint32_t modesGateRises(int slider)
{
    return gateRises[slider];
}

static void trigTick()
{
    static bool owned[NUM_SLIDERS];
    static bool pin[NUM_SLIDERS];

    uint32_t now = micros();
    for (int slider = 0; slider < NUM_SLIDERS; slider++)
    {
        if (!usesTrig(sliderSettings[slider]))
        {
            owned[slider] = false;
            continue;
        }

        if (trigActive[slider] && (int32_t)(now - trigEndUs[slider]) >= 0)
            trigActive[slider] = false;
        bool on = trigActive[slider];

        // write on a change, and once when the slider switches to triggers
        if (!owned[slider] || on != pin[slider])
        {
            setGate(slider, on);
            pin[slider] = on;
        }
        owned[slider] = true;
    }

    // m.gate: the sequencer's triggers while it runs
    static bool masterOwned = false;
    if (sequencerRunning())
    {
        if (trigActive[MASTER_TRIG] && (int32_t)(now - trigEndUs[MASTER_TRIG]) >= 0)
            trigActive[MASTER_TRIG] = false;
        setMasterGate(trigActive[MASTER_TRIG]);
        masterOwned = true;
    }
    else if (masterOwned)
    {
        trigActive[MASTER_TRIG] = false;
        masterOwned = false;
    }
}

bool modesOwnsGate(int slider)
{
    return usesTrig(sliderSettings[slider]);
}

// LFO rate from the slider position and the rate range, so a new range applies at once
// tempo sync: the cycle length the slider position picks
static int syncDivision(int slider)
{
    return constrain(lfo[slider].position * NUM_SYNC_DIVISIONS / 1024, 0, NUM_SYNC_DIVISIONS - 1);
}

static float lfoRate(int slider)
{
    if (sliderSettings[slider].lfoRate == LFO_RATE_SYNC)
        return 1.0f / (SYNC_DIVISIONS[syncDivision(slider)].beats * sequencerBeatSeconds());
    const RateRange &r = LFO_RATES[min((int)sliderSettings[slider].lfoRate, 2)];
    return r.low * powf(r.high / r.low, constrain(lfo[slider].position, 0, 1023) / 1023.0f);
}

void modesSyncRestart()
{
    for (int slider = 0; slider < NUM_SLIDERS; slider++)
    {
        if (sliderSettings[slider].mode == MODE_LFO && sliderSettings[slider].lfoRate == LFO_RATE_SYNC)
            lfo[slider].phase = 0;
    }
}

void modesDefaults()
{
    for (int i = 0; i < NUM_SLIDERS; i++)
        sliderSettings[i] = DEFAULT_SETTINGS;
}

void modesLoadLegacy()
{
    Storage storage;
    EEPROM.get(STORAGE_ADDRESS, storage);
    if (storage.magic == STORAGE_MAGIC)
    {
        memcpy(sliderSettings, storage.settings, sizeof(sliderSettings));
        return;
    }

    // the previous layout: the same up to range, the glide time from its index
    if (storage.magic == STORAGE_MAGIC_V6)
    {
        for (int i = 0; i < NUM_SLIDERS; i++)
        {
            sliderSettings[i] = DEFAULT_SETTINGS;
            for (int b = 0; b < V6_SIZE; b++)
                ((uint8_t *)&sliderSettings[i])[b] = EEPROM.read(STORAGE_ADDRESS + sizeof(uint32_t) + i * V6_SIZE + b);
            sliderSettings[i].slewMs = OLD_SLEW_TIMES[min((int)sliderSettings[i].legacySlew, 8)];
        }
        return;
    }

    // an older layout: converted, missing fields get defaults
    int size = 0;
    for (const OldLayout &layout : OLD_LAYOUTS)
    {
        if (storage.magic == layout.magic)
            size = layout.size;
    }
    for (int i = 0; i < NUM_SLIDERS; i++)
    {
        OldSettings old = OLD_DEFAULTS;
        for (int b = 0; b < size; b++)
            ((uint8_t *)&old)[b] = EEPROM.read(STORAGE_ADDRESS + sizeof(uint32_t) + i * size + b);
        sliderSettings[i] = size ? fromOld(old) : DEFAULT_SETTINGS;
    }
}

void modesSetup()
{
    for (int i = 0; i < NUM_SLIDERS; i++)
    {
        currentStep[i] = -1;
        currentSemitone[i] = -1;
        lfo[i].depth = 1.0f;
        lfo[i].semitone = -1;
        lfo[i].lastUs = micros();
    }
    trigTimer.begin(trigTick, TRIG_TICK_US);
}

static bool inScale(const ScaleInfo &scale, int root, int semitone)
{
    int degree = ((semitone - root) % 12 + 12) % 12;
    for (int i = 0; i < scale.count; i++)
    {
        if (scale.intervals[i] == degree)
            return true;
    }
    return false;
}

// nearest note of a scale (from the root) to a voltage, at 1 V/octave
static int nearestNote(const ScaleInfo &scale, int root, float volts)
{
    float target = volts * 12.0f;
    int best = 0;
    float bestDistance = 1e9f;
    for (int note = max(0, (int)target - 12); note <= min(MAX_SEMITONE, (int)target + 12); note++)
    {
        float distance = fabsf(note - target);
        if (inScale(scale, root, note) && distance < bestDistance)
        {
            best = note;
            bestDistance = distance;
        }
    }
    return best;
}

// every scale note between base and base + range, lowest first
static int notesInRange(const SliderSettings &s, int notes[])
{
    const ScaleInfo &scale = SCALES[s.scale - 1];
    int low = min(s.base, s.base + s.range);
    int high = max(s.base, s.base + s.range);
    int count = 0;
    for (int note = (low + SEMITONE_CENTS - 1) / SEMITONE_CENTS; note <= high / SEMITONE_CENTS && note <= MAX_SEMITONE; note++)
    {
        if (inScale(scale, s.root, note))
            notes[count++] = note;
    }
    return count;
}

// the scale note at a slider position: the slider spans every scale note
// between base and base + range, with a little hysteresis at the borders
static int quantize(int slider, int position)
{
    const SliderSettings &s = sliderSettings[slider];
    const ScaleInfo &scale = SCALES[s.scale - 1];

    int notes[MAX_NOTES];
    int count = notesInRange(s, notes);
    noteCount[slider] = count;
    if (count == 0)
    {
        currentStep[slider] = -1;
        return nearestNote(scale, s.root, (float)s.base / CENTS_PER_VOLT);
    }

    // a negative range runs the notes from the top of the slider down
    int p = s.range < 0 ? 1023 - position : position;
    float x = p / 1024.0f * count;

    int step = currentStep[slider];
    if (step < 0 || step >= count || x < step - NOTE_HYSTERESIS || x > step + 1 + NOTE_HYSTERESIS)
        step = constrain((int)x, 0, count - 1);
    currentStep[slider] = step;
    return notes[step];
}

int modesValueAt(int slider, int position)
{
    const SliderSettings &s = sliderSettings[slider];
    position = constrain(position, 0, 1023);
    if (!s.scale)
        return constrain(s.base + lroundf(position / 1023.0f * s.range), 0, MAX_CENTS);

    int notes[MAX_NOTES];
    int count = notesInRange(s, notes);
    if (count == 0)
        return nearestNote(SCALES[s.scale - 1], s.root, (float)s.base / CENTS_PER_VOLT) * SEMITONE_CENTS;
    int p = s.range < 0 ? 1023 - position : position;
    return notes[constrain(p * count / 1024, 0, count - 1)] * SEMITONE_CENTS;
}

int modesPositionOf(int slider, int cents)
{
    const SliderSettings &s = sliderSettings[slider];
    if (!s.scale)
    {
        if (s.range == 0)
            return 0;
        return constrain(lroundf((float)(cents - s.base) / s.range * 1023.0f), 0, 1023);
    }

    // the nearest note's place among the notes in the range
    int notes[MAX_NOTES];
    int count = notesInRange(s, notes);
    if (count == 0)
        return 0;
    int best = 0;
    for (int i = 1; i < count; i++)
    {
        if (abs(notes[i] * SEMITONE_CENTS - cents) < abs(notes[best] * SEMITONE_CENTS - cents))
            best = i;
    }
    int position = lroundf((best + 0.5f) / count * 1023.0f);
    return s.range < 0 ? 1023 - position : position;
}

const char *modesValueText(int slider, int cents)
{
    static char text[9];
    if (sliderSettings[slider].scale)
    {
        int semitone = constrain((int)lroundf((float)cents / SEMITONE_CENTS), 0, MAX_SEMITONE);
        char note[5];
        snprintf(note, sizeof(note), "%s%u", NOTE_NAMES[semitone % 12], (unsigned)(semitone / 12) % 10);
        snprintf(text, sizeof(text), "%-4s%4.2f", note, (float)cents / CENTS_PER_VOLT);
    }
    else
        snprintf(text, sizeof(text), "%7.2fV", (float)cents / CENTS_PER_VOLT);
    return text;
}

// move the glide on and write the output; returns the DAC value
static int glideOutput(int slider)
{
    Glide &g = glide[slider];
    int time = sliderSettings[slider].slewMs;
    float t = time > 0 ? (float)(millis() - g.startMs) / time : 1.0f;
    g.volts = t >= 1.0f ? g.to : g.from + t * (g.to - g.from);

    return setSliderVolts(slider, g.volts);
}

static void glideTo(int slider, float volts)
{
    Glide &g = glide[slider];
    if (volts == g.to)
        return;
    g.from = g.volts;
    g.to = volts;
    g.startMs = millis();
}

// SLIDE target for a position: the scale note, or the voltage between base
// and base + range. Returns the semitone, or -1 when not quantized.
static int slideTarget(int slider, int position, float &volts)
{
    const SliderSettings &s = sliderSettings[slider];
    SlideSource &src = source[slider];
    src = {true, position, s.scale, s.root, s.base, s.range};

    if (s.scale)
    {
        int semitone = quantize(slider, position);
        volts = semitone / 12.0f;
        return semitone;
    }
    volts = (s.base + constrain(position, 0, 1023) / 1023.0f * s.range) / CENTS_PER_VOLT;
    return -1;
}

// LFO waveform at a phase, 0-1
static float lfoShape(int slider, float phase)
{
    switch (sliderSettings[slider].lfoShape)
    {
    case LFO_SINE:
        return 0.5f + 0.5f * sinf(2 * PI * phase);
    case LFO_TRI:
        if (phase < 0.25f)
            return 0.5f + 2 * phase;
        if (phase < 0.75f)
            return 1.5f - 2 * phase;
        return 2 * phase - 1.5f;
    case LFO_RAMP:
        return phase;
    case LFO_SAW:
        return 1 - phase;
    case LFO_SQUARE:
        return phase < 0.5f ? 1 : 0;
    default:
        return lfo[slider].value; // random: held for the whole cycle
    }
}

// the finger (or a loop) on an LFO slider: position sets the rate, pressure
// the depth, a landing finger can restart the cycle
static void lfoInput(int slider, int position, int pressure, bool gate)
{
    const SliderSettings &s = sliderSettings[slider];
    Lfo &l = lfo[slider];

    if (gate)
        l.position = position;

    bool landed = gate && !lastGate[slider];
    if (landed && s.lfoGate == 0)
        gateRises[slider]++; // touch gate
    if (landed && s.lfoReset)
    {
        l.phase = 0;
        l.value = random(1001) / 1000.0f;
        if (s.lfoGate == LFO_GATE_TRIG)
            fireTrig(slider);
        if (s.lfoGate == LFO_GATE_CYCLE)
            gateRises[slider]++;
    }

    // the pressure depth is always tracked, so switching DEPTH applies at
    // once: follow the pressure while touched; on release keep the pressure
    // from a little earlier, before the finger started lifting
    if (gate && millis() - l.sampledMs >= DEPTH_HOLD_MS)
    {
        l.earlier = l.pressure;
        l.pressure = pressure / 511.0f;
        l.sampledMs = millis();
    }
    if (gate)
        l.depth = pressure / 511.0f;
    else if (lastGate[slider])
        l.depth = l.earlier;
}

// depth in use: full, or the pressure depth
static float lfoDepth(int slider)
{
    return sliderSettings[slider].lfoDepth ? lfo[slider].depth : 1.0f;
}

bool modesGate(int slider, bool touchGate)
{
    const SliderSettings &s = sliderSettings[slider];
    if (usesTrig(s))
        return trigActive[slider];
    if (s.mode == MODE_LFO && s.lfoGate == LFO_GATE_CYCLE)
        return lfo[slider].phase < 0.5f;
    return touchGate;
}

int modesSliderOutput(int slider, int position, int pressure, bool &gate)
{
    const SliderSettings &s = sliderSettings[slider];
    if (s.mode == MODE_LFO)
    {
        lfoInput(slider, position, pressure, gate);
        lastGate[slider] = gate;
        gate = modesGate(slider, gate);
        return lfo[slider].code; // written by modesUpdate
    }

    // slide: the output follows the finger. Touch: it is set where the
    // finger lands, or jumps with a second finger, and stays while sliding.
    bool landed = gate && !lastGate[slider];
    bool jumped = abs(position - lastPosition[slider]) > JUMP;
    lastPosition[slider] = position;

    int semitone = currentSemitone[slider];
    if (!s.latch || landed || (gate && jumped) || !source[slider].valid)
    {
        if (s.latch)
            currentStep[slider] = -1; // no hysteresis around the old note
        float volts;
        semitone = slideTarget(slider, position, volts);
        glideTo(slider, volts);
    }

    // a new note while the gate is already on retriggers it, or fires a trigger
    bool newNote = semitone >= 0 && semitone != currentSemitone[slider];
    currentSemitone[slider] = semitone;
    if (newNote && gate && lastGate[slider] && s.slideGate == SLIDE_GATE_RETRIG)
    {
        gateLowUntil[slider] = millis() + RETRIGGER_MS;
        gateRises[slider]++;
    }
    if (landed && !usesTrig(s))
        gateRises[slider]++;
    if (usesTrig(s) && gate && (landed || newNote))
        fireTrig(slider);
    lastGate[slider] = gate;
    int code = glideOutput(slider);

    if (usesTrig(s))
        gate = modesGate(slider, gate);
    else if ((int32_t)(millis() - gateLowUntil[slider]) < 0)
        gate = false;
    return code;
}

void modesUpdate()
{
    static uint8_t lastMode[NUM_SLIDERS];
    static bool resend[NUM_SLIDERS];

    uint32_t now = micros();
    for (int slider = 0; slider < NUM_SLIDERS; slider++)
    {
        const SliderSettings &s = sliderSettings[slider];
        Lfo &l = lfo[slider];

        if (s.mode != lastMode[slider] && s.mode == MODE_SLIDE)
            resend[slider] = true;
        lastMode[slider] = s.mode;

        float dt = (now - l.lastUs) * 1e-6f;
        l.lastUs = now;
        if (s.mode == MODE_LFO)
        {
            l.phase += lfoRate(slider) * dt;
            if (l.phase >= 1.0f)
            {
                l.phase -= floorf(l.phase);
                l.value = random(1001) / 1000.0f;
                if (s.lfoGate == LFO_GATE_TRIG)
                    fireTrig(slider);
                if (s.lfoGate == LFO_GATE_CYCLE)
                    gateRises[slider]++;
            }
        }

        // writes are spaced out to keep the I2C bus free for the sensors
        if (now - l.writtenUs < OUTPUT_INTERVAL_US)
            continue;

        if (s.mode == MODE_SLIDE)
        {
            // back from LFO: the held SLIDE value goes out again
            if (resend[slider] && source[slider].valid)
            {
                glideOutput(slider);
                l.writtenUs = now;
            }
            resend[slider] = false;

            // a held output follows a change of its quantizer or range at once
            if (source[slider].valid && sourceChanged(slider))
            {
                currentStep[slider] = -1;
                float volts;
                currentSemitone[slider] = slideTarget(slider, source[slider].position, volts);
                glideTo(slider, volts);
            }

            const Glide &g = glide[slider];
            if (g.volts != g.to)
            {
                glideOutput(slider);
                l.writtenUs = now;
            }
            continue;
        }

        // LFO: swings from base to base + range, quantized to the nearest
        // scale note if a scale is set, with a trigger on every new note
        float volts = (s.base + lfoShape(slider, l.phase) * lfoDepth(slider) * s.range) / CENTS_PER_VOLT;
        if (s.scale)
        {
            int semitone = nearestNote(SCALES[s.scale - 1], s.root, volts);
            if (semitone != l.semitone && s.lfoGate == LFO_GATE_NOTE)
                fireTrig(slider);
            l.semitone = semitone;
            volts = semitone / 12.0f;
        }
        int code = sliderVoltsToCode(slider, volts);
        if (code != l.code)
        {
            setSliderDAC12(slider, code);
            l.code = code;
            l.writtenUs = now;
        }
    }
}

bool modesRunning(int slider)
{
    return sliderSettings[slider].mode == MODE_LFO;
}

int modesOutputCode(int slider)
{
    return lfo[slider].code;
}

int modesLfoLevel(int slider)
{
    return lroundf(lfoShape(slider, lfo[slider].phase) * lfoDepth(slider) * 1023.0f);
}

bool modesShowsDot(int slider)
{
    const SliderSettings &s = sliderSettings[slider];
    return s.mode == MODE_SLIDE && s.scale;
}

int modesNotePosition(int slider)
{
    int count = max(noteCount[slider], 1);
    int step = max(currentStep[slider], 0);
    int position = lroundf((step + 0.5f) / count * 1023.0f);
    return sliderSettings[slider].range < 0 ? 1023 - position : position;
}

const char *modesSliderText(int slider)
{
    static char text[7];
    const SliderSettings &s = sliderSettings[slider];
    if (s.mode == MODE_LFO && s.lfoRate == LFO_RATE_SYNC)
        snprintf(text, sizeof(text), "%s", SYNC_DIVISIONS[syncDivision(slider)].name);
    else if (s.mode == MODE_LFO)
    {
        float rate = lfoRate(slider);
        snprintf(text, sizeof(text), rate < 10 ? "%.2fHz" : "%.1fHz", rate);
    }
    else if (s.scale && currentSemitone[slider] >= 0)
    {
        int semitone = currentSemitone[slider];
        snprintf(text, sizeof(text), "%s%u", NOTE_NAMES[semitone % 12], (unsigned)(semitone / 12) % 10);
    }
    else
        snprintf(text, sizeof(text), "%.2fV", glide[slider].to);
    return text;
}

const char *modeName(int mode)
{
    return MODE_NAMES[mode];
}

const char *scaleName(int scale)
{
    return SCALES[scale].name;
}

const char *lfoShapeName(int shape)
{
    return LFO_SHAPE_NAMES[shape];
}

const char *lfoRateName(int rate)
{
    return rate == LFO_RATE_SYNC ? "sync" : LFO_RATES[rate].name;
}


const char *noteName(int semitone)
{
    return NOTE_NAMES[semitone % 12];
}
