#include "recorder.h"

#include <Arduino.h>

#include "config.h"
#include "display.h"

static const uint32_t SAMPLE_MS = 10;    // one sample every 10 ms, interpolated on playback
static const int MAX_SAMPLES = 2000;     // 20 s per control
static const int MIN_SAMPLES = 5;        // shorter recordings are dropped
static const uint32_t RETRIGGER_MS = 10; // gate low at the start of every loop
static const int JUMP = 100;              // a step this large between samples is a jump: played as a step

struct Sample
{
    uint16_t position;
    uint16_t y;
    uint16_t pressure;
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
static bool armed = false;
static uint8_t lastButtons = 0;

void recorderUpdate(uint8_t buttons)
{
    uint8_t pressed = buttons & ~lastButtons;
    lastButtons = buttons;

    if (pressed & (1 << BUTTON_HOLD))
        armed = !armed;
    displayRecordArmed(armed);
}

static void startLoop(int channel)
{
    state[channel] = LOOPING;
    startMs[channel] = millis();
}

// add the samples that are due since the recording started, so the timing
// stays right even if a scan takes longer than a sample
static void record(int channel, const TouchReading &reading)
{
    int due = (millis() - startMs[channel]) / SAMPLE_MS + 1;
    while (length[channel] < due && length[channel] < MAX_SAMPLES)
    {
        Sample &s = samples[channel][length[channel]++];
        s.position = reading.position;
        s.y = reading.y;
        s.pressure = reading.pressure;
    }
}

// interpolate between two samples, but keep jumps sharp
static int interpolate(int a, int b, float f)
{
    if (abs(b - a) > JUMP)
        return a;
    return lroundf(a + f * (b - a));
}

bool recorderChannel(int channel, const TouchReading &reading, Playback &playback)
{
    if (reading.started)
    {
        if (armed)
        {
            armed = false;
            state[channel] = RECORDING;
            length[channel] = 0;
            startMs[channel] = millis();
        }
        else if (state[channel] == LOOPING)
        {
            // touching a looping control takes it back
            state[channel] = LIVE;
        }
    }

    if (state[channel] == RECORDING)
    {
        if (reading.touched)
        {
            record(channel, reading);
            if (length[channel] >= MAX_SAMPLES)
                startLoop(channel);
        }
        else if (reading.released)
        {
            // drop the samples taken while the finger was lifting
            int lifting = (reading.liftMs + SAMPLE_MS - 1) / SAMPLE_MS;
            length[channel] = max(0, length[channel] - lifting);

            if (length[channel] >= MIN_SAMPLES)
                startLoop(channel);
            else
                state[channel] = LIVE;
        }
        // the live touch drives the outputs while recording
        return false;
    }

    if (state[channel] != LOOPING)
        return false;

    uint32_t t = (millis() - startMs[channel]) % (length[channel] * SAMPLE_MS);
    int i = t / SAMPLE_MS;
    float f = (float)(t % SAMPLE_MS) / SAMPLE_MS;
    const Sample &a = samples[channel][i];
    const Sample &b = samples[channel][min(i + 1, length[channel] - 1)];

    playback.position = interpolate(a.position, b.position, f);
    playback.y = interpolate(a.y, b.y, f);
    playback.pressure = interpolate(a.pressure, b.pressure, f);
    playback.gate = t >= RETRIGGER_MS;
    return true;
}

bool recorderIsRecording(int channel)
{
    return state[channel] == RECORDING;
}
