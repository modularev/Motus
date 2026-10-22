#include "sequencer.h"

#include <Arduino.h>

#include "config.h"
#include "modes.h"
#include "presets.h"

struct Division
{
    const char *name;
    float notes; // of a whole note
};

static const Division DIVISIONS[NUM_SEQ_DIVISIONS] = {
    {"1/1", 1.0f},
    {"1/2", 0.5f},
    {"1/4", 0.25f},
    {"1/4T", 0.25f * 2 / 3},
    {"1/8", 0.125f},
    {"1/8T", 0.125f * 2 / 3},
    {"1/16", 0.0625f},
    {"1/16T", 0.0625f * 2 / 3},
    {"1/32", 0.03125f},
};
static const char *DIRECTION_NAMES[NUM_SEQ_DIRECTIONS] = {"forward", "backward", "pendulum", "random"};

// a row of 8 stages, forward, unquantized over the whole output range
static const SeqRow DEFAULT_ROW = {7, 0, SEQ_FORWARD, 0, 0, 0, MAX_CENTS, 0, 0};
static const SeqSettings DEFAULT_SETTINGS = {0, 4, 0, 0, 120, {DEFAULT_ROW, DEFAULT_ROW, DEFAULT_ROW, DEFAULT_ROW}};
static const float CLOCK_SMOOTHING = 0.3f;  // how quickly a measured slider clock tempo follows
static const uint32_t CLOCK_TIMEOUT_MS = 4000; // a slider clock slower than this is not measured

SeqSettings seqSettings = DEFAULT_SETTINGS;

static uint8_t running = 0; // a bit per row
static int step[NUM_VARS]; // every row's step within its length, counted from its start
static bool pendulumUp[NUM_VARS];
static int writeRow = -1;
static uint32_t nextStepUs = 0;

// row speeds: a slow row counts pulses, a fast one steps again between them
static int pulseCount[NUM_VARS];
static int extraSteps[NUM_VARS]; // still to come before the next pulse
static uint32_t extraNextUs[NUM_VARS];
static uint32_t extraIntervalUs[NUM_VARS];

// slider clock: rising gates seen, and the measured time between them
static uint32_t seenRises = 0;
static uint32_t lastRiseMs = 0;
static float measuredStepSeconds = 0;

void sequencerDefaults()
{
    seqSettings = DEFAULT_SETTINGS;
}

// one step, at the division, in seconds
static float stepSeconds()
{
    return 60.0f / constrain((int)seqSettings.bpm, 20, 300) * 4 * DIVISIONS[seqSettings.div].notes;
}

float sequencerBeatSeconds()
{
    // with a slider clock, a pulse counts as one step of the division
    if (seqSettings.clock && measuredStepSeconds > 0)
        return measuredStepSeconds / (4 * DIVISIONS[seqSettings.div].notes);
    return 60.0f / constrain((int)seqSettings.bpm, 20, 300);
}

SeqSettings seqFromV4(const SeqSettingsV4 &old)
{
    SeqSettings s = DEFAULT_SETTINGS;
    s.clock = old.clock;
    s.div = old.div;
    s.touch = old.touch;
    s.bpm = old.bpm;
    for (int row = 0; row < NUM_VARS; row++)
    {
        const SeqRowV4 &o = old.rows[row];
        s.rows[row] = {o.length, o.start, o.dir, o.scale, o.root, o.base, o.range, 0, 0};
    }
    return s;
}

SeqSettings seqFromV3(const SeqSettingsV3 &old)
{
    SeqSettings s = DEFAULT_SETTINGS;
    s.clock = old.clock;
    s.div = old.div;
    s.touch = old.touch;
    s.bpm = old.bpm;
    for (int row = 0; row < NUM_VARS; row++)
    {
        SeqRow &r = s.rows[row];
        r.length = old.rows[row].length;
        r.start = old.rows[row].start;
        r.dir = old.rows[row].dir;
        // the one quantizer goes to the rows it applied to
        if (old.quantRows & (1 << row))
        {
            r.scale = old.scale;
            r.root = old.root;
        }
    }
    return s;
}

SeqSettings seqFromV2(const SeqSettingsV2 &old)
{
    SeqSettingsV3 v3 = {old.clock, old.div, old.touch, 0, 0, old.bpm, {}, 0, 0};
    for (int row = 0; row < NUM_VARS; row++)
        v3.rows[row] = {(uint8_t)(constrain((int)old.steps[row], 1, NUM_SLIDERS) - 1), 0, (uint8_t)(old.dir % NUM_SEQ_DIRECTIONS)};
    return seqFromV3(v3);
}

SeqSettings seqFromV1(const SeqSettingsV1 &old)
{
    SeqSettingsV2 v2 = {old.clock, old.div, old.dir, old.touch, {old.steps, old.steps, old.steps, old.steps}, old.bpm};
    return seqFromV2(v2);
}

int sequencerRowStep(int row)
{
    return step[row];
}

int sequencerRowStage(int row)
{
    return (seqSettings.rows[row].start + step[row]) % NUM_SLIDERS;
}

static void goTo(int row, int next)
{
    step[row] = next;
    presetsSelectRow(row, sequencerRowStage(row));
}

// one row one step on, within its length and in its direction
static void advanceRow(int row)
{
    const SeqRow &r = seqSettings.rows[row];
    int steps = r.length + 1;
    int at = step[row];
    switch (r.dir)
    {
    case SEQ_BACKWARD:
        goTo(row, at <= 0 || at >= steps ? steps - 1 : at - 1);
        break;
    case SEQ_PENDULUM:
        if (steps == 1)
            goTo(row, 0);
        else if (at >= steps - 1)
        {
            pendulumUp[row] = false;
            goTo(row, min(at, steps - 1) - 1);
        }
        else if (at <= 0)
        {
            pendulumUp[row] = true;
            goTo(row, 1);
        }
        else
            goTo(row, pendulumUp[row] ? at + 1 : at - 1);
        break;
    case SEQ_RANDOM:
        goTo(row, random(steps));
        break;
    default:
        goTo(row, at + 1 >= steps ? 0 : at + 1);
        break;
    }
}

// a clock pulse, a step's time (us, 0 if not known yet) after the last one
static void advance(uint32_t intervalUs)
{
    uint32_t now = micros();
    for (int row = 0; row < NUM_VARS; row++)
    {
        if (!(running & (1 << row)))
            continue;
        int speed = seqSettings.rows[row].speed;
        extraSteps[row] = 0;
        if (speed < 0)
        {
            // every 2nd to 4th pulse
            if (++pulseCount[row] < 1 - speed)
                continue;
            pulseCount[row] = 0;
        }
        else if (speed > 0 && intervalUs > 0)
        {
            // and evenly between the pulses
            extraIntervalUs[row] = intervalUs / (speed + 1);
            extraNextUs[row] = now + extraIntervalUs[row];
            extraSteps[row] = speed;
        }
        advanceRow(row);
    }
    modesFireMasterTrig();
}

// the steps of fast rows between the pulses
static void advanceExtra()
{
    uint32_t now = micros();
    for (int row = 0; row < NUM_VARS; row++)
    {
        if (extraSteps[row] <= 0 || (int32_t)(now - extraNextUs[row]) < 0)
            continue;
        extraSteps[row]--;
        extraNextUs[row] += extraIntervalUs[row];
        if (running & (1 << row))
            advanceRow(row);
    }
}

void sequencerUpdate()
{
    // a slider clock: measure the time between its rising gates, and step on each
    int clockSlider = seqSettings.clock - 1;
    bool rose = false;
    if (clockSlider >= 0 && clockSlider < NUM_SLIDERS)
    {
        uint32_t rises = modesGateRises(clockSlider);
        if (rises != seenRises)
        {
            seenRises = rises;
            rose = true;
            uint32_t now = millis();
            float interval = (now - lastRiseMs) / 1000.0f;
            if (lastRiseMs && now - lastRiseMs < CLOCK_TIMEOUT_MS)
                measuredStepSeconds = measuredStepSeconds > 0 ? measuredStepSeconds + CLOCK_SMOOTHING * (interval - measuredStepSeconds) : interval;
            lastRiseMs = now;
        }
    }
    else
        measuredStepSeconds = 0;

    if (!running)
        return;
    advanceExtra();

    if (clockSlider >= 0)
    {
        if (rose)
            advance((uint32_t)(measuredStepSeconds * 1e6f));
        return;
    }

    // internal clock
    uint32_t now = micros();
    if ((int32_t)(now - nextStepUs) >= 0)
    {
        advance((uint32_t)(stepSeconds() * 1e6f));
        nextStepUs += (uint32_t)(stepSeconds() * 1e6f);
        if ((int32_t)(now - nextStepUs) > 0) // fell behind, e.g. after a tempo change
            nextStepUs = now + (uint32_t)(stepSeconds() * 1e6f);
    }
}

void sequencerTouch(int slider)
{
    // while writing, a touch only writes and the sequence keeps its step;
    // otherwise every row starts at the touched stage from now on
    if (seqSettings.touch || writeRow >= 0)
        return;
    for (int row = 0; row < NUM_VARS; row++)
    {
        seqSettings.rows[row].start = slider;
        pendulumUp[row] = true;
        goTo(row, 0);
    }
}

void sequencerNudge(int steps)
{
    for (int row = 0; row < NUM_VARS; row++)
    {
        if (running & (1 << row))
            continue;
        int length = seqSettings.rows[row].length + 1;
        goTo(row, ((step[row] + steps) % length + length) % length);
    }
}

void sequencerShiftRow(int row, int stages)
{
    SeqRow &r = seqSettings.rows[row];
    r.start = ((r.start + stages) % NUM_SLIDERS + NUM_SLIDERS) % NUM_SLIDERS;
    presetsSelectRow(row, sequencerRowStage(row));
}

void sequencerChangeLength(int row, int stages)
{
    SeqRow &r = seqSettings.rows[row];
    r.length = constrain((int)r.length + stages, 0, NUM_SLIDERS - 1);
    if (step[row] > r.length)
        goTo(row, r.length);
}

void sequencerNextDirection(int row)
{
    SeqRow &r = seqSettings.rows[row];
    r.dir = (r.dir + 1) % NUM_SEQ_DIRECTIONS;
}

bool sequencerInWindow(int row, int stage)
{
    const SeqRow &r = seqSettings.rows[row];
    return ((stage - r.start) % NUM_SLIDERS + NUM_SLIDERS) % NUM_SLIDERS <= r.length;
}

bool sequencerRunning()
{
    return running != 0;
}

bool sequencerRowRunning(int row)
{
    return running & (1 << row);
}

// a row starts on its first stage; with pulse, a clock pulse is now (the clock
// starts), so a fast row steps again before the next one
static void startRow(int row, bool pulse)
{
    pendulumUp[row] = true;
    pulseCount[row] = 0;
    extraSteps[row] = 0;
    goTo(row, 0);
    int speed = seqSettings.rows[row].speed;
    uint32_t intervalUs = seqSettings.clock ? (uint32_t)(measuredStepSeconds * 1e6f) : (uint32_t)(stepSeconds() * 1e6f);
    if (pulse && speed > 0 && intervalUs > 0)
    {
        extraIntervalUs[row] = intervalUs / (speed + 1);
        extraNextUs[row] = micros() + extraIntervalUs[row];
        extraSteps[row] = speed;
    }
    running |= 1 << row;
}

// the first row to start also starts the clock, with synced LFOs in phase
static void startClock()
{
    modesFireMasterTrig();
    modesSyncRestart();
    nextStepUs = micros() + (uint32_t)(stepSeconds() * 1e6f);
}

void sequencerSetRowRunning(int row, bool run)
{
    uint8_t bit = 1 << row;
    if (run == (bool)(running & bit))
        return;
    if (!run)
    {
        running &= ~bit;
        return;
    }
    // a row joining others steps with them from their next pulse
    bool first = !running;
    startRow(row, first);
    if (first)
        startClock();
}

void sequencerSetRunning(bool run)
{
    if (!run)
    {
        running = 0;
        return;
    }
    // all rows start again together
    running = 0;
    for (int row = 0; row < NUM_VARS; row++)
        startRow(row, true);
    startClock();
}

void sequencerRowRange(int row, SliderSettings &range)
{
    const SeqRow &r = seqSettings.rows[row];
    memset(&range, 0, sizeof(range));
    range.scale = r.scale;
    range.root = r.root;
    range.base = r.base;
    range.range = r.range;
}

int sequencerStage()
{
    if (writeRow >= 0)
        return sequencerRowStage(writeRow);
    for (int row = 0; row < NUM_VARS; row++)
        if (running & (1 << row))
            return sequencerRowStage(row);
    return sequencerRowStage(0);
}

int sequencerWriteRow()
{
    return writeRow;
}

void sequencerSetWriteRow(int row)
{
    writeRow = row;
}

const char *seqDivisionName(int div)
{
    return DIVISIONS[div].name;
}

const char *seqSpeedName(int speed)
{
    static const char *NAMES[SEQ_SPEED_MAX - SEQ_SPEED_MIN + 1] = {"/4", "/3", "/2", "x1", "x2", "x3", "x4"};
    return NAMES[constrain(speed, SEQ_SPEED_MIN, SEQ_SPEED_MAX) - SEQ_SPEED_MIN];
}

const char *seqDirectionShortName(int dir)
{
    static const char *NAMES[NUM_SEQ_DIRECTIONS] = {"fwd", "bwd", "pend", "rnd"};
    return NAMES[dir % NUM_SEQ_DIRECTIONS];
}

const char *seqDirectionName(int dir)
{
    return DIRECTION_NAMES[dir];
}
