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

struct PadSettings
{
    uint8_t mode;
    uint8_t rate;  // 0-100 along PAD_RATE_MIN_HZ .. PAD_RATE_MAX_HZ, exponential: loops per second
    uint8_t chaos; // 0-100 %: LORENZ rho 24.8-60, ROSSLER c 2.5-9
    uint8_t reserved;
};

constexpr int PAD_RATE_STEPS = 100;
constexpr int PAD_CHAOS_MAX = 100;

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

const char *padModeName(int mode);
const char *padRateText(int rate); // "0.50Hz"
float padRateHz(int rate);

#endif
