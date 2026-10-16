#ifndef MODES_H
#define MODES_H

#include <stdint.h>

// Slider modes: what a slider's position output does

enum SliderMode : uint8_t
{
    MODE_POS,  // the finger position
    MODE_NOTE, // the position quantized to a scale, 1 V/octave
    NUM_MODES
};

enum Scale : uint8_t
{
    SCALE_CHROMATIC,
    SCALE_MAJOR,
    SCALE_MINOR,
    SCALE_MAJOR_PENTATONIC,
    SCALE_MINOR_PENTATONIC,
    SCALE_DORIAN,
    SCALE_HARMONIC_MINOR,
    SCALE_BLUES,
    SCALE_WHOLE_TONE,
    NUM_SCALES
};

struct SliderSettings
{
    uint8_t mode;
    uint8_t scale;
    uint8_t root;      // 0 = C .. 11 = B
    uint8_t octave;    // lowest octave, 0 = 0 V
    uint8_t range;     // octaves the slider spans
    uint8_t retrigger; // NOTE: gate drops briefly on every new note
    uint8_t latch;     // NOTE: 0 slide (the note follows the finger), 1 touch (the note is set where the finger lands)
    uint8_t slew;      // NOTE: glide time, index into the slew times
};

constexpr int NUM_SLEW_TIMES = 9;

extern SliderSettings sliderSettings[];

void modesSetup();

// call once per loop: glides keep moving after the finger lifts
void modesUpdate();
void modesLoad();
void modesSave();

// writes the slider's position output for its mode and returns the DAC
// value written (12 bit); may hold the gate low for a moment to retrigger on
// a new note
int modesSliderOutput(int slider, int position, bool &gate);

// NOTE: where the current note sits in the slider's range, 10 bit, for the LEDs
int modesNotePosition(int slider);

// value for the play screen, or an empty string to show the position
const char *modesSliderText(int slider);

const char *modeName(int mode);
const char *scaleName(int scale);
const char *noteName(int semitone); // without octave
int slewTime(int index);            // ms, 0 = off

#endif
