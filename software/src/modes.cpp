#include "modes.h"

#include <Arduino.h>
#include <EEPROM.h>

#include "analogout.h"
#include "config.h"

static const int STORAGE_ADDRESS = 128; // after the presets
static const uint32_t STORAGE_MAGIC = 0x4D4F5332;    // "MOS2"
static const uint32_t STORAGE_MAGIC_V1 = 0x4D4F5331; // "MOS1": settings without latch and slew

static const float NOTE_HYSTERESIS = 0.2f; // of a note step: a resting finger does not flicker between notes
static const uint32_t RETRIGGER_MS = 5;    // gate low on a new note
static const int MAX_SEMITONE = 97;        // the outputs reach about 8.17 V
static const int JUMP = 100;               // a position step this large is a second-finger jump: a new note in touch mode

static const int SLEW_TIMES[NUM_SLEW_TIMES] = {0, 5, 10, 20, 50, 100, 200, 500, 1000};

struct ScaleInfo
{
    const char *name;
    uint8_t count;
    uint8_t intervals[12];
};

static const ScaleInfo SCALES[NUM_SCALES] = {
    {"CHROM", 12, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
    {"MAJOR", 7, {0, 2, 4, 5, 7, 9, 11}},
    {"MINOR", 7, {0, 2, 3, 5, 7, 8, 10}},
    {"PENT", 5, {0, 2, 4, 7, 9}},
    {"mPENT", 5, {0, 3, 5, 7, 10}},
    {"DORIAN", 7, {0, 2, 3, 5, 7, 9, 10}},
    {"HARMIN", 7, {0, 2, 3, 5, 7, 8, 11}},
    {"BLUES", 6, {0, 3, 5, 6, 7, 10}},
    {"WHOLE", 6, {0, 2, 4, 6, 8, 10}},
};

static const char *MODE_NAMES[NUM_MODES] = {"POS", "NOTE"};
static const char *NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static const SliderSettings DEFAULT_SETTINGS = {MODE_POS, SCALE_MAJOR, 0, 2, 2, 0, 0, 0};

struct Storage
{
    uint32_t magic;
    SliderSettings settings[NUM_SLIDERS];
};

// settings as saved before latch and slew existed
struct StorageV1
{
    uint32_t magic;
    uint8_t settings[NUM_SLIDERS][6];
};

SliderSettings sliderSettings[NUM_SLIDERS];

static int currentStep[NUM_SLIDERS];
static int currentSemitone[NUM_SLIDERS];
static uint32_t gateLowUntil[NUM_SLIDERS];
static bool lastGate[NUM_SLIDERS];
static int lastPosition[NUM_SLIDERS];

// glide from one note to the next
struct Glide
{
    float from;
    float to;
    float volts; // current output
    uint32_t startMs;
};
static Glide glide[NUM_SLIDERS];

void modesLoad()
{
    Storage storage;
    EEPROM.get(STORAGE_ADDRESS, storage);
    if (storage.magic == STORAGE_MAGIC)
    {
        memcpy(sliderSettings, storage.settings, sizeof(sliderSettings));
        return;
    }

    StorageV1 v1;
    EEPROM.get(STORAGE_ADDRESS, v1);
    for (int i = 0; i < NUM_SLIDERS; i++)
    {
        sliderSettings[i] = DEFAULT_SETTINGS;
        if (v1.magic == STORAGE_MAGIC_V1)
            memcpy(&sliderSettings[i], v1.settings[i], sizeof(v1.settings[i]));
    }
}

void modesSave()
{
    Storage storage;
    storage.magic = STORAGE_MAGIC;
    memcpy(storage.settings, sliderSettings, sizeof(sliderSettings));
    EEPROM.put(STORAGE_ADDRESS, storage);
}

void modesSetup()
{
    modesLoad();
    for (int i = 0; i < NUM_SLIDERS; i++)
        currentStep[i] = -1;
}

// the note at a slider position, with a little hysteresis at the note borders
static int quantize(int slider, int position)
{
    const SliderSettings &s = sliderSettings[slider];
    const ScaleInfo &scale = SCALES[s.scale];

    // every scale note over the range, plus the root on top
    int steps = s.range * scale.count + 1;
    float x = position / 1024.0f * steps;

    int step = currentStep[slider];
    if (step < 0 || step >= steps || x < step - NOTE_HYSTERESIS || x > step + 1 + NOTE_HYSTERESIS)
        step = constrain((int)x, 0, steps - 1);
    currentStep[slider] = step;

    int semitone = 12 * (s.octave + step / scale.count) + s.root + scale.intervals[step % scale.count];
    while (semitone > MAX_SEMITONE)
        semitone -= 12;
    return semitone;
}

// move the glide on and write the output; returns the DAC value
static int glideOutput(int slider)
{
    Glide &g = glide[slider];
    int time = SLEW_TIMES[sliderSettings[slider].slew];
    float t = time > 0 ? (float)(millis() - g.startMs) / time : 1.0f;
    g.volts = t >= 1.0f ? g.to : g.from + t * (g.to - g.from);

    return setSliderVolts(slider, g.volts);
}

static void startNote(int slider, int semitone)
{
    Glide &g = glide[slider];
    g.from = g.volts;
    g.to = semitone / 12.0f;
    g.startMs = millis();
}

int modesSliderOutput(int slider, int position, bool &gate)
{
    const SliderSettings &s = sliderSettings[slider];
    if (s.mode != MODE_NOTE)
    {
        setSliderDAC(slider, position);
        return position << 2;
    }

    // slide: the note follows the finger. Touch: the note is set where the
    // finger lands, or jumps with a second finger, and stays while sliding.
    bool landed = gate && !lastGate[slider];
    bool jumped = abs(position - lastPosition[slider]) > JUMP;
    int semitone = currentSemitone[slider];
    if (!s.latch || landed || (gate && jumped))
    {
        if (s.latch)
            currentStep[slider] = -1; // no hysteresis around the old note
        semitone = quantize(slider, position);
    }
    lastPosition[slider] = position;

    // a new note while the gate is already on retriggers it
    if (semitone != currentSemitone[slider])
    {
        currentSemitone[slider] = semitone;
        startNote(slider, semitone);
        if (gate && lastGate[slider] && s.retrigger)
            gateLowUntil[slider] = millis() + RETRIGGER_MS;
    }
    lastGate[slider] = gate;
    int code = glideOutput(slider);

    if ((int32_t)(millis() - gateLowUntil[slider]) < 0)
        gate = false;
    return code;
}

void modesUpdate()
{
    for (int slider = 0; slider < NUM_SLIDERS; slider++)
    {
        const Glide &g = glide[slider];
        if (sliderSettings[slider].mode == MODE_NOTE && g.volts != g.to)
            glideOutput(slider);
    }
}

int modesNotePosition(int slider)
{
    const SliderSettings &s = sliderSettings[slider];
    int steps = s.range * SCALES[s.scale].count + 1;
    int step = max(currentStep[slider], 0);
    return lroundf((step + 0.5f) / steps * 1023.0f);
}

const char *modesSliderText(int slider)
{
    static char text[6];
    if (sliderSettings[slider].mode != MODE_NOTE)
        return "";
    int semitone = currentSemitone[slider];
    snprintf(text, sizeof(text), "%s%u", NOTE_NAMES[semitone % 12], (unsigned)(semitone / 12) % 10);
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

int slewTime(int index)
{
    return SLEW_TIMES[index];
}

const char *noteName(int semitone)
{
    return NOTE_NAMES[semitone % 12];
}
