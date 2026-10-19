#ifndef SEQUENCER_H
#define SEQUENCER_H

#include <stdint.h>

#include "config.h"

// Step sequencer in the spirit of the Serge TKB: the 8 sliders are 8 stages,
// each with the four var voltages of its preset. Touching a slider can jump
// to its stage; WRITE stores what a touched slider would output into a row.
// While running, m.gate sends a trigger on every step.

enum SeqDirection : uint8_t
{
    SEQ_FORWARD,
    SEQ_BACKWARD,
    SEQ_PENDULUM,
    SEQ_RANDOM,
    NUM_SEQ_DIRECTIONS
};

constexpr int NUM_SEQ_DIVISIONS = 9;

struct SeqSettings
{
    uint8_t clock; // 0 internal, 1-8: a step on every rising gate of that slider
    uint8_t div;   // index into the divisions: what one step is
    uint8_t dir;
    uint8_t touch;           // 0 a touch jumps to the slider's stage, 1 touches are ignored
    uint8_t steps[NUM_VARS]; // the length of every row, 1-8
    uint16_t bpm;            // 20-300
};

// the layout saved before every row had its own length
struct SeqSettingsV1
{
    uint8_t clock, div, dir, steps, touch, reserved;
    uint16_t bpm;
};
SeqSettings seqFromV1(const SeqSettingsV1 &old);

extern SeqSettings seqSettings;

void sequencerDefaults();

// call once per loop: the internal clock, slider clocks, steps
void sequencerUpdate();

// a slider touch started: jump to its stage, unless touches are ignored or WRITE is on
void sequencerTouch(int slider);

bool sequencerRunning();
void sequencerSetRunning(bool running);
int sequencerStage();         // the stage shown on the LEDs: the WRITE row's, else row 1's
int sequencerRowStage(int row);

// WRITE: the var row (0-3) touches write into, or -1
int sequencerWriteRow();
void sequencerSetWriteRow(int row);

// seconds per beat (a quarter note): from BPM, or measured from a slider clock
float sequencerBeatSeconds();

const char *seqDivisionName(int div);
const char *seqDirectionName(int dir);

#endif
