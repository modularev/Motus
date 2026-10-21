#ifndef MODES_H
#define MODES_H

#include <stdint.h>

// Slider modes: what a slider's position output does. Both modes share the
// same language: BASE and RANGE set the output range, QUANT and ROOT snap it
// to a scale.

enum SliderMode : uint8_t
{
    MODE_SLIDE, // the finger sets the output
    MODE_LFO,   // an LFO, the finger sets its rate
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

enum LfoShape : uint8_t
{
    LFO_SINE,   // starts halfway, rising
    LFO_TRI,    // starts halfway, rising
    LFO_RAMP,   // rises from base
    LFO_SAW,    // falls to base
    LFO_SQUARE, // high for the first half
    LFO_RANDOM, // a new random level every cycle
    NUM_LFO_SHAPES
};

constexpr int NUM_LFO_RATES = 4; // slow, mid, fast, sync
constexpr int LFO_RATE_SYNC = 3;

// Base and range are in cents of a volt (1/1200 V): a semitone is 100, an
// octave 1200 at 1 V/octave, and 0.01 V is 12.
constexpr int CENTS_PER_VOLT = 1200;
constexpr int SEMITONE_CENTS = 100;
constexpr int MAX_CENTS = 9804; // the outputs reach about 8.17 V

struct SliderSettings
{
    uint8_t mode;
    uint8_t scale;     // quantizer: 0 off, else the scale + 1
    uint8_t root;      // 0 = C .. 11 = B
    uint8_t latch;     // SLIDE: 0 the output follows the finger, 1 it is set where the finger lands
    uint8_t legacySlew; // earlier glide time setting, only read when converting old settings
    uint8_t slideGate; // SLIDE: 0 gate while touched, 1 gate drops briefly on every new note, 2 trigger on touch and every new note
    uint8_t lfoShape;
    uint8_t lfoRate;   // LFO: rate range the slider spans, index into the rate ranges
    uint8_t lfoDepth;  // LFO: 0 fixed, 1 pressure sets the depth
    uint8_t lfoReset;  // LFO: 0 free running, 1 restarts on touch
    uint8_t lfoGate;   // LFO: 0 gate while touched, 1 high for the first half of every cycle, 2 trigger every cycle, 3 trigger on every new note
    uint8_t lfoPhase;  // LFO: phase offset, 0-255 for 0-360 degrees
    int16_t base;      // the output at the bottom of the slider, or the LFO's low point
    int16_t range;     // from base to the top of the slider, or to the LFO's high point; negative inverts
    uint16_t slewMs;   // SLIDE: glide time, 0-10000 ms
};

constexpr int MAX_SLEW_MS = 10000;

extern SliderSettings sliderSettings[];

void modesSetup();

// the single save of earlier firmware, read once when converting it
void modesLoadLegacy();
void modesDefaults();

// call often (between sensor reads): glides keep moving after the finger
// lifts, LFOs run, settings changes reach held outputs
void modesUpdate();

// the slider's position output for its mode, from the finger or a loop:
// writes it and returns the DAC value (12 bit); the gate can change, to
// retrigger a note or to follow an LFO cycle
int modesSliderOutput(int slider, int position, int pressure, bool &gate);

// gate of an untouched slider: off, or the LFO cycle
bool modesGate(int slider, bool touchGate);

// trigger gates are written by a timer, not by the main loop
bool modesOwnsGate(int slider);

// counts every time the slider's gate output turns on, for clocking
uint32_t modesGateRises(int slider);

// a trigger on m.gate, timed like the slider triggers
void modesFireMasterTrig();

// the SLIDE value (cents) a slider gives at a position, with its BASE,
// RANGE and QUANT, whatever its mode: what the sequencer's WRITE stores
int modesValueAt(int slider, int position);

// the other way round: where on the slider a value (cents) lies, 10 bit
int modesPositionOf(int slider, int cents);

// a value (cents) in 8 characters as the slider shows it: the note and the
// volts when quantized ("C#2 2.08"), else the volts ("   2.08V")
const char *modesValueText(int slider, int cents);

// the nearest note (cents) of a scale (index, 0 = chromatic) from a root
int modesQuantizeCents(int scale, int root, int cents);

// tempo-synced LFOs start their cycle again, in phase with the sequencer
void modesSyncRestart();

// LFO: the output runs on its own; its current DAC value, and where it is
// within its swing for the LEDs (10 bit)
bool modesRunning(int slider);
int modesOutputCode(int slider);
int modesLfoLevel(int slider);

// quantized SLIDE: the LEDs show a dot at the current note, 10 bit
bool modesShowsDot(int slider);
int modesNotePosition(int slider);

// value for the play screen: the note, the voltage or the LFO rate
const char *modesSliderText(int slider);

const char *modeName(int mode);
const char *scaleName(int scale);
const char *noteName(int semitone); // without octave
const char *lfoShapeName(int shape);
const char *lfoRateName(int rate);

#endif
