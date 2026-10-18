#ifndef RECORDER_H
#define RECORDER_H

#include <stdint.h>

#include "sensors.h"

// Touch recorder. Tap HOLD to arm; the first touch on a slider or pad starts
// recording it, including lifts and pauses, until HOLD is tapped again (a
// tap while armed disarms). Or hold HOLD and touch a control: it records for
// as long as HOLD is held. With recording armed, hold HOLD and touch a loop to
// stop it, or keep holding for the stop-all progress bar.

struct Playback
{
    int position; // slider position, or pad X, 10 bit
    int y;        // pad Y, 10 bit
    int pressure; // 0-511
    bool gate;
};

// call once per loop with the touched buttons (bit per Button)
void recorderUpdate(uint8_t buttons);

enum Source
{
    SOURCE_LIVE, // the touch drives the outputs
    SOURCE_LOOP, // the recording drives the outputs, values in playback
    SOURCE_NONE  // a touch that stopped a loop: ignored until it lifts
};

// call for every sensor reading; returns what drives the control's outputs.
// A touch plays live over a loop when liveOverLoop is set, otherwise the
// loop keeps playing.
Source recorderChannel(int channel, const TouchReading &reading, bool liveOverLoop, Playback &playback);

bool recorderArmed();
bool recorderIsRecording(int channel);
bool recorderIsLooping(int channel);
bool recorderLoopStart(int channel); // true for a moment at the start of every loop

#endif
