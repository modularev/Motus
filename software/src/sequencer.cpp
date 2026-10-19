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

static const SeqSettings DEFAULT_SETTINGS = {0, 4, SEQ_FORWARD, 0, {8, 8, 8, 8}, 120};
static const float CLOCK_SMOOTHING = 0.3f;  // how quickly a measured slider clock tempo follows
static const uint32_t CLOCK_TIMEOUT_MS = 4000; // a slider clock slower than this is not measured

SeqSettings seqSettings = DEFAULT_SETTINGS;

static bool running = false;
static int stage[NUM_VARS];
static bool pendulumUp[NUM_VARS];
static int writeRow = -1;
static uint32_t nextStepUs = 0;

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

SeqSettings seqFromV1(const SeqSettingsV1 &old)
{
    SeqSettings s = DEFAULT_SETTINGS;
    s.clock = old.clock;
    s.div = old.div;
    s.dir = old.dir;
    s.touch = old.touch;
    for (int row = 0; row < NUM_VARS; row++)
        s.steps[row] = old.steps;
    s.bpm = old.bpm;
    return s;
}

static void goTo(int row, int next)
{
    stage[row] = next;
    presetsSelectRow(row, next);
}

// one row one step on, within its own length
static void advanceRow(int row)
{
    int steps = constrain((int)seqSettings.steps[row], 1, NUM_SLIDERS);
    int at = stage[row];
    switch (seqSettings.dir)
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

static void advance()
{
    for (int row = 0; row < NUM_VARS; row++)
        advanceRow(row);
    modesFireMasterTrig();
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

    if (clockSlider >= 0)
    {
        if (rose)
            advance();
        return;
    }

    // internal clock
    uint32_t now = micros();
    if ((int32_t)(now - nextStepUs) >= 0)
    {
        advance();
        nextStepUs += (uint32_t)(stepSeconds() * 1e6f);
        if ((int32_t)(now - nextStepUs) > 0) // fell behind, e.g. after a tempo change
            nextStepUs = now + (uint32_t)(stepSeconds() * 1e6f);
    }
}

void sequencerTouch(int slider)
{
    // while writing, a touch only writes and the sequence keeps its step
    if (!seqSettings.touch && writeRow < 0)
        for (int row = 0; row < NUM_VARS; row++)
            goTo(row, slider);
}

bool sequencerRunning()
{
    return running;
}

void sequencerSetRunning(bool run)
{
    if (run == running)
        return;
    running = run;
    if (running)
    {
        // start on the first stage, with synced LFOs in phase
        for (int row = 0; row < NUM_VARS; row++)
        {
            pendulumUp[row] = true;
            goTo(row, 0);
        }
        modesFireMasterTrig();
        modesSyncRestart();
        nextStepUs = micros() + (uint32_t)(stepSeconds() * 1e6f);
    }
}

int sequencerStage()
{
    return stage[writeRow >= 0 ? writeRow : 0];
}

int sequencerRowStage(int row)
{
    return stage[row];
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

const char *seqDirectionName(int dir)
{
    return DIRECTION_NAMES[dir];
}
