#ifndef RECORDER_H
#define RECORDER_H

#include <stdint.h>

#include "sensors.h"

// Touch recorder: tap HOLD to arm, the next slider or pad touch is recorded
// until the last finger lifts, then it loops on that control. Touching a
// looping control stops its loop.

struct Playback
{
    int position; // slider position, or pad X, 10 bit
    int y;        // pad Y, 10 bit
    int pressure; // 0-511
    bool gate;    // sliders: off for a moment at the start of every loop
};

// call once per loop with the touched buttons (bit per Button)
void recorderUpdate(uint8_t buttons);

// call for every sensor reading; returns true while the control loops, with
// the values to output in playback, instead of the live reading
bool recorderChannel(int channel, const TouchReading &reading, Playback &playback);

bool recorderIsRecording(int channel);

#endif
