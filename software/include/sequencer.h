#ifndef SEQUENCER_H
#define SEQUENCER_H

#include <stdint.h>

#include "config.h"

// Step sequencer in the spirit of the Serge TKB: the 8 sliders are 8 stages,
// each with the four var voltages of its preset. Every row runs on its own
// (they all step on the same clock). Touching a slider can jump to its stage;
// WRITE stores a touched slider's position, in the row's BASE and RANGE, into
// the row. While a row runs, m.gate sends a trigger on every step.

enum SeqDirection : uint8_t
{
    SEQ_FORWARD,
    SEQ_BACKWARD,
    SEQ_PENDULUM,
    SEQ_RANDOM,
    NUM_SEQ_DIRECTIONS
};

constexpr int NUM_SEQ_DIVISIONS = 9;

// every row: its length, its first stage and its direction; its quantizer,
// and the range a slider's position is written in
struct SeqRow
{
    uint8_t length : 3; // 0-7: 1-8 stages
    uint8_t start : 3;  // 0-7: the stage it starts on; it runs on from there, past 8 back to 1
    uint8_t dir : 2;
    uint8_t scale : 4;  // quantizer of the output: 0 off, else the scale + 1
    uint8_t root : 4;   // its key, 0 = C .. 11 = B
    int16_t base;       // cents: the bottom of the row's range
    int16_t range;      // cents: from base to the top of the range; negative inverts
    int8_t speed;       // steps per clock pulse: 0 x1, 1-3 x2-x4, -1 to -3 every 2nd to 4th pulse
    uint8_t reserved;
};

constexpr int SEQ_SPEED_MIN = -3, SEQ_SPEED_MAX = 3;
const char *seqSpeedName(int speed); // "/4" .. "x1" .. "x4"

struct SeqSettings
{
    uint8_t clock; // 0 internal, 1-8: a step on every rising gate of that slider
    uint8_t div;   // index into the divisions: what one step is
    uint8_t touch; // 0 a touch moves every row's start to the slider's stage, 1 touches are ignored
    uint8_t reserved;
    uint16_t bpm; // 20-300
    SeqRow rows[NUM_VARS];
};

extern SeqSettings seqSettings;

void sequencerDefaults();

// call once per loop: the internal clock, slider clocks, steps
void sequencerUpdate();

// a slider touch started: every row starts at its stage from now on, unless
// touches are ignored or a step view is open
void sequencerTouch(int slider);

// every stopped row moves this many stages within its length, wrapping
void sequencerNudge(int steps);

// a row's start (and with it its end) moves this many stages, wrapping past 8
void sequencerShiftRow(int row, int stages);

// a row's length changes by this many stages (1-8), and its direction steps on
void sequencerChangeLength(int row, int stages);
void sequencerNextDirection(int row);

// whether a stage is inside a row's window (from its start, its length long)
bool sequencerInWindow(int row, int stage);

// rows run and stop on their own; a row starts on its first stage
bool sequencerRunning(); // any row
bool sequencerRowRunning(int row);
void sequencerSetRunning(bool running); // all rows
void sequencerSetRowRunning(int row, bool running);

// a row's range as slider settings (QUANT, ROOT, BASE, RANGE), for writing
// a position into it and showing its values
struct SliderSettings;
void sequencerRowRange(int row, SliderSettings &range);
int sequencerStage();         // the stage shown on the LEDs: the WRITE row's, else the first running row's
int sequencerRowStage(int row);
int sequencerRowStep(int row); // the step within the row's length, 0 at its start

// WRITE: the var row (0-3) touches write into, or -1
int sequencerWriteRow();
void sequencerSetWriteRow(int row);

// seconds per beat (a quarter note): from BPM, or measured from a slider clock
float sequencerBeatSeconds();

const char *seqDivisionName(int div);
const char *seqDirectionName(int dir);
const char *seqDirectionShortName(int dir); // fwd, bwd, pend, rnd

#endif
