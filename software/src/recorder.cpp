#include "recorder.h"

#include <Arduino.h>

#include "config.h"
#include "display.h"
#include "system.h"

static const uint32_t SAMPLE_MS = 10;       // one sample every 10 ms, interpolated on playback
static const int MAX_SAMPLES = 2000;        // 20 s per control
static const int MIN_SAMPLES = 5;           // shorter recordings are dropped
static const uint32_t RETRIGGER_MS = 10;    // a gate held across the loop start drops for this long
static const int JUMP = 100;                // a step this large between samples is a jump: played as a step
static const uint32_t LONG_PRESS_DELAY_MS = 800;     // holding HOLD this long shows the stop-all bar
static const uint32_t LONG_PRESS_PROGRESS_MS = 1500; // the bar fills in this time, then all loops stop
static const uint32_t LOOP_START_MS = 80;   // how long recorderLoopStart reports a loop start

static const uint16_t GATE_BIT = 0x8000;    // stored in the pressure field, which only uses 9 bits

struct Sample
{
    uint16_t position;
    uint16_t y;
    uint16_t pressure; // with GATE_BIT
};

enum State
{
    LIVE,
    RECORDING,
    LOOPING
};

// 120 KB, kept in the second RAM bank
DMAMEM static Sample samples[NUM_SENSORS][MAX_SAMPLES];

static State state[NUM_SENSORS];
static int length[NUM_SENSORS];       // recorded samples
static uint32_t startMs[NUM_SENSORS]; // start of the recording, or of the loop
static Sample last[NUM_SENSORS];      // last values while recording, kept through pauses

static bool armed = false;
static int recording = -1; // the channel being recorded

static bool holdDown = false;
static bool holdUsed = false;  // the HOLD press did something else, so it is not a tap
static bool holdArmed = false; // HOLD was pressed while armed or recording
static bool momentary = false; // the recording runs while HOLD is held
static uint32_t holdDownMs = 0;

static bool ignoring[NUM_SENSORS]; // a touch that stopped a loop, until it lifts

static void startLoop(int channel)
{
    state[channel] = LOOPING;
    startMs[channel] = millis();
}

// end the recording: loop it, or drop it if it is too short
static void finishRecording()
{
    if (recording < 0)
        return;
    if (length[recording] >= MIN_SAMPLES)
        startLoop(recording);
    else
        state[recording] = LIVE;
    recording = -1;
}

static void stopAllLoops()
{
    for (int channel = 0; channel < NUM_SENSORS; channel++)
    {
        if (state[channel] == LOOPING)
            state[channel] = LIVE;
    }
}

static void startRecording(int channel)
{
    armed = false;
    recording = channel;
    state[channel] = RECORDING;
    length[channel] = 0;
    startMs[channel] = millis();
    last[channel] = {0, 0, 0};
}

// HOLD: a tap arms, and when armed or recording exits (disarms, or closes the
// loop). Held while not armed, a touched control records for as long as HOLD
// is held. Held while armed or recording, a touched loop stops, and holding
// on shows the stop-all bar.
void recorderUpdate(uint8_t buttons)
{
    bool down = buttons & (1 << BUTTON_HOLD);

    if (down && !holdDown)
    {
        holdDownMs = millis();
        holdUsed = false;
        holdArmed = armed || recording >= 0;
    }
    uint32_t held = millis() - holdDownMs;

    if (down && holdArmed && !holdUsed && held >= LONG_PRESS_DELAY_MS)
    {
        float progress = (float)(held - LONG_PRESS_DELAY_MS) / LONG_PRESS_PROGRESS_MS;
        if (progress < 1.0f)
            displayProgress("STOP ALL", progress);
        else
        {
            // long press: stop all loops, cancel arming and recording
            stopAllLoops();
            if (recording >= 0)
            {
                state[recording] = LIVE;
                recording = -1;
            }
            armed = false;
            holdUsed = true;
            displayMessage("LOOPS", "STOPPED");
        }
    }

    if (!down && holdDown)
    {
        if (momentary)
        {
            // the held recording ends with HOLD
            finishRecording();
            momentary = false;
        }
        else if (!holdUsed && held < LONG_PRESS_DELAY_MS)
        {
            // tap: close the recording, or arm / disarm. A press let go
            // later does nothing.
            if (recording >= 0)
                finishRecording();
            else
                armed = !armed;
        }
    }
    holdDown = down;

    displayRecordState(armed, recording >= 0);
}

// add the samples that are due since the recording started, so the timing
// stays right even if a scan takes longer than a sample
static void record(int channel, const TouchReading &reading)
{
    Sample &l = last[channel];
    if (reading.touched || reading.released)
    {
        l.position = reading.position;
        l.y = reading.y;
    }
    l.pressure = reading.touched ? (reading.pressure | GATE_BIT) : 0;

    // a lift shifts the position: the restored position replaces the samples taken while lifting
    if (reading.released && reading.liftMs > 0)
    {
        int lifting = min(length[channel], (int)((reading.liftMs + SAMPLE_MS - 1) / SAMPLE_MS));
        for (int i = length[channel] - lifting; i < length[channel]; i++)
        {
            samples[channel][i].position = l.position;
            samples[channel][i].y = l.y;
        }
    }

    int due = (millis() - startMs[channel]) / SAMPLE_MS + 1;
    while (length[channel] < due && length[channel] < MAX_SAMPLES)
        samples[channel][length[channel]++] = l;

    if (length[channel] >= MAX_SAMPLES)
        finishRecording();
}

// interpolate between two samples, but keep jumps sharp
static int interpolate(int a, int b, float f)
{
    if (abs(b - a) > JUMP)
        return a;
    return lroundf(a + f * (b - a));
}

Source recorderChannel(int channel, const TouchReading &reading, bool liveOverLoop, Playback &playback)
{
    if (reading.started)
    {
        if (holdDown && holdArmed)
        {
            // armed, HOLD held + touch: stop this loop; the touch itself is ignored
            if (state[channel] == LOOPING)
                state[channel] = LIVE;
            holdUsed = true;
            ignoring[channel] = true;
        }
        else if (holdDown && recording < 0)
        {
            // not armed, HOLD held + touch: record while HOLD is held
            startRecording(channel);
            momentary = true;
            holdUsed = true;
        }
        else if (armed && recording < 0)
            startRecording(channel);
    }

    if (ignoring[channel])
    {
        if (!reading.touched)
            ignoring[channel] = false;
        return SOURCE_NONE;
    }

    if (state[channel] == RECORDING)
    {
        record(channel, reading);
        // the live touch drives the outputs while recording
        return SOURCE_LIVE;
    }

    if (state[channel] != LOOPING)
        return SOURCE_LIVE;

    // a finger plays live over the loop while it touches
    if (liveOverLoop && (reading.touched || reading.released))
        return SOURCE_LIVE;

    int n = length[channel];
    uint32_t t = (millis() - startMs[channel]) % (n * SAMPLE_MS);
    int i = t / SAMPLE_MS;
    float f = (float)(t % SAMPLE_MS) / SAMPLE_MS;
    const Sample &a = samples[channel][i];
    const Sample &b = samples[channel][min(i + 1, n - 1)];

    playback.position = interpolate(a.position, b.position, f);
    playback.y = interpolate(a.y, b.y, f);
    playback.pressure = interpolate(a.pressure & ~GATE_BIT, b.pressure & ~GATE_BIT, f);
    playback.gate = a.pressure & GATE_BIT;

    // a gate held across the loop start drops briefly to retrigger
    bool heldAcross = (samples[channel][0].pressure & GATE_BIT) && (samples[channel][n - 1].pressure & GATE_BIT);
    if (heldAcross && systemSettings.loopTrig && t < RETRIGGER_MS)
        playback.gate = false;
    return SOURCE_LOOP;
}

bool recorderArmed()
{
    return armed;
}

bool recorderIsRecording(int channel)
{
    return state[channel] == RECORDING;
}

bool recorderIsLooping(int channel)
{
    return state[channel] == LOOPING;
}

bool recorderLoopStart(int channel)
{
    if (state[channel] != LOOPING)
        return false;
    return (millis() - startMs[channel]) % (length[channel] * SAMPLE_MS) < LOOP_START_MS;
}
