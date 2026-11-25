#ifndef PADS_H
#define PADS_H

#include <stdint.h>

#include "config.h"

// XY pad modes: what a pad's X, Y and Z outputs do. XY follows the finger;
// the attractors run on their own, and the finger pulls the trajectory
// toward itself, the harder the press the stronger, so a firm press pins it
// under the finger and a lift lets it fly back onto the attractor.
//
// The outputs, as seen from the playing position:
//   LORENZ  X = x, Y = z (the butterfly), Z = y
//   ROSSLER X = x, Y = y (the spiral),    Z = z (the spikes)

enum PadMode : uint8_t
{
    PAD_XY,
    PAD_LORENZ,
    PAD_ROSSLER,
    NUM_PAD_MODES
};

// PLAY: MODE, RATE, CHAOS · OUT: QUANT, ROOT, AXES and each axis' BASE and
// RANGE, as a slider's · TIME: SLEW. They shape X and Y whatever the mode, so
// a quantized attractor plays notes; Z stays the pressure (or the attractor's z).
struct PadSettings
{
    uint8_t mode;
    uint8_t rate;  // 0-100 along PAD_RATE_MIN_HZ .. PAD_RATE_MAX_HZ, exponential: loops per second
    uint8_t chaos; // 0-100 %: LORENZ rho 24.8-60, ROSSLER c 2.5-9
    uint8_t scale; // quantizer: 0 off, else the scale + 1
    uint8_t root;  // 0 = C .. 11 = B
    uint8_t axes;  // which axes the quantizer snaps: PAD_AXIS_X, PAD_AXIS_Y or both
    uint8_t slew;  // 0 off, 1-127: 1 ms to 10 s exponentially, to reach a new value
    int16_t xBase, xRange, yBase, yRange; // cents, as a slider's BASE and RANGE, in steps of PAD_CENTS_STEP
};

constexpr int PAD_RATE_STEPS = 100;
constexpr int PAD_CHAOS_MAX = 100;
constexpr uint8_t PAD_AXIS_X = 1, PAD_AXIS_Y = 2;
constexpr int PAD_SLEW_STEPS = 127;
constexpr int PAD_CENTS_STEP = 20; // a semitone, 0.1 V and 1 V are whole steps; stored in these

extern PadSettings padSettings[NUM_PADS];

void padsDefaults();

// call often (between sensor reads): the attractors move on
void padsUpdate();

// the pad runs an attractor rather than following the finger
bool padsRunning(int pad);

// the finger on the pad, or not, in output coordinates (10 bit, pressure 0-511)
void padsFinger(int pad, bool touched, int x, int y, int pressure);

// the attractor's outputs: X and Y 12 bit, Z 0-511 as the pressure outputs
void padsOutput(int pad, int &x, int &y, int &z);

// X and Y (0-1 along the pad, as the outputs see it, or the attractor's)
// through BASE, RANGE, the quantizer and SLEW: the DAC values (12 bit). Call
// once a scan, touched or not, so a slew glides on after the lift.
void padsShape(int pad, float x, float y, int &codeX, int &codeY);
// where an axis' output lies in its range, 0-1, for the LEDs (axis 0 X, 1 Y)
float padsLevel(int pad, int axis);
// an axis' output for the screen, 4 characters: "C#3" or "2.08"
const char *padsValueText(int pad, int axis);
const char *padSlewText(int slew); // "  off", "  40 ms", " 2.5 s"
const char *padAxesName(int axes);

const char *padModeName(int mode);
const char *padRateText(int rate); // "0.50Hz"
float padRateHz(int rate);

#endif
