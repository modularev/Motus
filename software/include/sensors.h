#ifndef SENSORS_H
#define SENSORS_H

#include "config.h"

struct TouchReading
{
    bool touched;
    bool started;  // touch began this scan
    bool released; // touch ended this scan; the position fields hold the value from before lifting
    int liftMs;    // on release: how long the finger was lifting, the time the position was taken back
    int position; // slider position, 10 bit
    int x;        // XY pad position per axis, 10 bit, pad-local (0,0 = bottom left of the pad itself)
    int y;
    int pressure; // 0-511
};

inline bool isSlider(int channel) { return channel < NUM_SLIDERS; }

void sensorSetup();
TouchReading sensorRead(int channel);
uint8_t buttonsRead(); // bit per Button that is touched

#endif
