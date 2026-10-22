# Motus Touch Controller

![Motus panel](docs/panel.png)

Motus is a capacitive touch controller for modular synthesizers. It turns finger position and pressure into control voltages and gates: eight touch sliders, two XY pads, a master output that mixes the active sliders, and four preset voltages switched by the sliders.

## Controls and outputs

| Section | Outputs | What they do |
|---|---|---|
| Sliders 1–8 | `position`, `force`, `gate` | The slider's output (a voltage, a note or an LFO, see slider modes), pressure of the finger, gate while touched. The output and LEDs hold their value after release. |
| XY pads 1–2 | `X1` `Y1` `Z1`, `X2` `Y2` `Z2` | Finger position on the pad and its pressure (Z). The pad LEDs glow around the finger. |
| Master | `m.pos`, `m.force`, `m.gate` | Average position output and pressure of all sliders that are touched, looping or running an LFO, so it stays in range; holds its value when none are. Gate while any slider gate is on, or the sequencer's step trigger while it runs. |
| Sequencer | `var 1`–`var 4` | The four rows of the step sequencer, see below. |

The display shows the touched slider or pad with its value (a voltage, note or LFO rate), the menu while it is open, or the sequencer state when idle.

### Menu

| `SET 1`–`SET 4` | Result |
|---|---|
| Tap | open the settings page below; tap again to close it. In a step view or a row's settings, a tap on any SET pad closes it |
| Double tap | start or stop sequencer row A–D (`SET 1` is row A), from anywhere |
| Hold (0.5 s) | open the step view of row A–D (see Sequencer); hold it there again for the row's settings |
| With settings open: encoder | scroll through the settings |
| With settings open: tap the same pad | close the settings |
| With settings open: hold `FINE` and turn | change the value, both ways |
| With settings open: tap another pad | switch to that pad's settings page |

A SET pad held while the encoder turns or a slider is touched acts as a modifier, so letting go is neither a tap nor a hold. Touching a slider selects which slider is edited: normally it keeps playing as usual, while a SET pad is held the touch only selects and leaves the slider's outputs unchanged. Settings close after 5 seconds without input (a row's settings back to its step view); a step view stays open until you tap a SET pad.

A slot holds a patch: every slider's settings, the sequencer's stage values and settings. The SYS settings are global and saved with every save. The idle screen shows whether the sequencer runs, with its tempo or clock slider (`RUN 120`, `RUN  #3`, `STOP 120`). While running, the second line has a tube per row A–D, one pixel row per step and as tall as the row's length, filled up to its current step (empty while that row is stopped); when stopped, it shows the current slot (`Slot 2`). A dot shows while recording and `*` while there are unsaved changes. The unit starts with the slot saved or loaded last and shows it for a second, as does LOAD. Recordings are not saved.

| Tap | Page | Settings |
|---|---|---|
| `SET 1` | SLIDER | the touched slider's settings: LFO off or on, then the settings of that mode (see Slider modes); hold `SET 1` and turn to choose the slider without touching it |
| `SET 2` | SEQ | the sequencer: run, clock, tempo, touch |
| `SET 4` | SYS | global settings, see below |
| `FINE` | | tap: toggle small encoder steps for exact values (± on the display); double tap: start or stop all rows, from anywhere; hold and turn: change a setting's value |
| `SAVE` | | tap: `SAVE 2?`, turn the encoder to pick one of 4 slots, tap again to store (the second line says `replace`, `empty` or `current`) |
| `LOAD` | | tap: `LOAD 2?`, pick the slot, tap again to load it. After the slots come four templates for a fresh start (`INIT?`): faders (8 plain 0–8 V faders), lfos (8 sine LFOs with clock gates, their phases spread 45° apart), keys (8 quantized keyboards, C major over two octaves) and split (4 keys, 4 LFOs 90° apart); a template also clears the sequencer and its values, and stays unsaved until you save it. A SET or `FINE` pad, or 5 s without input, cancels either prompt |

### System settings

| Setting | Values |
|---|---|
| SMOOTH | 1 (light) to 10 (heavy): smoothing of slow finger movement; fast moves always stay fast |
| DEADBAND | 0–20: how much a resting finger can wobble without moving the output |
| PRESSURE | 50–200 %: how hard counts as a firm press, for force and Z |
| TOUCH | 50–200 %: touch sensitivity; higher reacts to lighter touches |
| BRIGHT | 10–100 %: LED brightness |
| TRIG LEN | 1–20 ms: trigger length |
| LOOP TRG | on or off: a gate held across a loop start drops briefly to retrigger |
| AUTOSAVE | on (default) or off: after 10 s without touching anything, changes are saved into the current slot. Not after loading a template, until you SAVE or LOAD yourself |

### Sequencer

A step sequencer in the spirit of the Serge TKB: the 8 sliders are 8 stages, and every stage has four voltages, the rows A–D on `var 1`–`var 4`. On the screen, rows are A–D and sliders #1–#8. Every row starts and stops on its own (double tap its SET pad), and the running rows step together on the clock. While any row runs, `m.gate` sends a trigger on every step and the top LED of the current stage (of the first running row, or of the row being written) is lit.

**Step views:** hold `SET 1`–`SET 4` to open the step view of row A–D. The sliders' LEDs show the row's values, each where it lies in the row's range, and touching a slider stores its position, in the row's BASE, RANGE and QUANT, into the row at that stage; touches do not move the running sequence and leave the slider's own outputs (position, force, gate) and any loop unchanged. While the row is stopped, the encoder steps the stopped rows by hand, as on the idle screen, and the view shows the current step; while running, it browses the stages. Holding `FINE` while turning edits the stage shown (semitones, 0.01 V with `FINE` on). Holding the row's pad while turning shifts the row's start and end together, wrapping past 8 (its SHIFT), and with `FINE` held too it changes the row's length; meanwhile the stages inside the row's window light up fully, the playing one brightest, and the others go dark. Holding the row's pad without turning opens the row's settings (below); holding another row's pad switches to that row's view. Holding a slider and turning edits that slider's step; from then on the finger no longer writes during that touch. The current step has its top LED lit and shows at full brightness, the others are dimmed. While the row runs, the stage shown (picked with the encoder) keeps its top LED lit as well, and that LED dims while the step passes through it. The display shows the row and its direction over the shown stage's value (`A  fwd` over `C#2 2.08`; while writing, `#3 → A`), or the start or length while they are changed.

**Row settings** (hold the row's pad in its step view; hold it again to go back):

| Setting | Values |
|---|---|
| DIR | forward, backward, pendulum, random |
| SHIFT | 1–8: the stage the row starts on; it runs its length from there, past 8 back to 1 |
| LEN | 1–8 stages; rows of different lengths run against each other |
| SPEED | /4, /3, /2, x1, x2, x3, x4: steps per clock pulse. A slow row steps on every 2nd to 4th pulse, a fast one steps on the pulse and evenly in between (timed from the tempo, or from the measured pulses with a slider clock); a row's first step lands on a pulse, so rows stay in line |
| QUANT | off, or a scale: the row plays the scale's notes within its range |
| ROOT | the key of that scale |
| BASE | the bottom of the row's range, in volts (with the note name when quantized) |
| RANGE | from BASE to the top of the row's range: volts, or semitones when quantized; negative inverts |

Every stage holds a position within its row's range, like a slider's: touching a slider stores where you touched it, and the row's BASE, RANGE and QUANT turn that into the output. So changing them reshapes the whole row at once, while it plays, and the stages keep their shape (e.g. turn RANGE up to widen a melody, BASE to transpose it). While DIR, SHIFT or LEN is shown, the LEDs show the row's window; otherwise the row's values in its range. The keys template sets every row to C major over two octaves from C2; the others leave the rows unquantized over 0–8 V.

**Sequencer** (tap `SET 2`):

| Setting | Values |
|---|---|
| RUN | stop or run all rows (or double tap `FINE`); every row starts on its first stage. Turning the encoder on the idle screen steps every stopped row forward or back by hand |
| CLOCK | int (the internal tempo), or #1–#8: a step on every rising gate of that slider, so an LFO slider with a cycle gate is a playable clock, a slider with triggers a tap clock, and a recorded loop a rhythm |
| BPM | 20–300 |
| DIV | what one step is: 1/1, 1/2, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32. With a slider clock, one pulse counts as one step, and the tempo for synced LFOs is measured from it |
| TOUCH | jump: a touch makes every row start at the slider's stage from then on (it sets their SHIFT); ignore: play the sliders freely |

### Slider modes

Every slider has two modes, switched with LFO off or on at the top of its settings (tap `SET 1`), that share the same settings for their output range and quantizer:

- **SLIDE**: the finger sets the output. Without a quantizer the output spans the voltage range, like a fader; with one it plays the scale notes in the range, like a keyboard strip (and, swept, an arpeggio).
- **LFO**: the output runs an LFO, which keeps running after release; the finger sets its rate or, with CTRL depth, its depth.

| Setting | Modes | Values |
|---|---|---|
| LFO | both | off (SLIDE) or on (LFO) |
| BASE | both | the output at the bottom of the slider, or the LFO's low point, in volts (with the note name when quantized) |
| RANGE | both | from BASE to the top of the slider or the LFO's high point: volts, or semitones when quantized (24 st = two octaves); negative inverts |
| QUANT | both | off, or a scale: chromatic, major, minor, major and minor pentatonic, dorian, harmonic minor, blues, whole tone (1 V/octave) |
| ROOT | both | the key of the scale |
| PLAY | SLIDE | slide (the output follows the finger) or touch (set where the finger lands; a second finger sets a new value) |
| SLEW | SLIDE | glide between values, off to 10 s, continuing after release; coarse steps 5 ms … 10 s, about 1 % steps with `FINE` |
| GATE | SLIDE | gate while touched, retrigger (a short drop on every new note) or trigger (on touch and every new note) |
| SHAPE | LFO | sine and triangle start halfway and rise, ramp rises from BASE, saw falls to it, square starts high, S&H holds a new random level every cycle |
| CTRL | LFO | what the slider sets: rate, or depth (bottom: none, top: the full BASE–RANGE swing; the screen shows the depth in %), and FREQ sets the rate |
| RATE | LFO | slow 0.02–1 Hz, mid 0.2–5 Hz, fast 1–25 Hz, spread exponentially along the slider; or sync: the slider picks a cycle of 4 bars, 2 bars, 1 bar, 1/2, 1/4, 1/8, 1/16 or 1/32 at the sequencer tempo, restarting in phase when the sequencer starts |
| FREQ | LFO | with CTRL depth: the rate within the RATE range (or the sync division) |
| PHASE | LFO | 0–359°: an offset of the cycle, in 15° steps; the gate, triggers and LEDs follow it, and restarts keep it |
| DEPTH | LFO | fixed, or set by pressure (held on release; with CTRL depth on top of the slider's); less depth stays closer to BASE |
| RESET | LFO | free running, or restart on touch |
| GATE | LFO | while touched, high for the first half of every cycle (a clock), a trigger at every cycle start, or a trigger on every new quantized note |

BASE and RANGE step by octaves when quantized (semitones with `FINE`), otherwise by 1 V (0.1 V with `FINE`). Small steps go four times further when turning fast. Settings changes reach a held output at once. A little hysteresis keeps a resting finger from flickering between notes. Triggers are short pulses (TRIG LEN on the SYS page, 2 ms by default), timed by an interrupt independent of the sensor scan, and at least 10 ms apart, so one touch never fires twice. The LEDs show a bar, a dot at the note when quantized, or the LFO's waveform; the display shows the voltage, the note or the LFO rate.

### Two fingers

A second finger on a slider or pad makes the value jump to it; lifting it jumps back. Lifting the first finger instead keeps the value at the second one without ending the touch. Use it for square-wave modulation or to jump to a new value mid-gesture. The fingers need at least one electrode of space between them to count as two.

### Recording

| Action | Result |
|---|---|
| Tap `HOLD` | arm recording (a blinking dot on the display, top LEDs of the sliders blink); tap again to disarm |
| Touch a slider or pad while armed | recording starts; everything is recorded, including lifts and pauses, so rhythms loop too. The control's LEDs blink while recording and the dot stays lit. |
| Tap `HOLD` while recording | the recording loops (20 seconds at most) on that control's outputs and LEDs; the LEDs flash at every loop start |
| Hold `HOLD` (not armed) + touch a control | record it for as long as `HOLD` is held; letting go loops it |
| Armed: hold `HOLD` + touch a looping control | stop that loop; the touch itself plays nothing |
| Armed or recording: keep holding `HOLD` | after 0.8 s a bar fills over 1.5 s, then all loops stop; letting go earlier cancels |

A slider records position, force and gate, a pad X, Y and Z, including two-finger jumps. Every slider and pad can loop its own recording at the same time. Touching a looping control plays live over it while you touch; with the menu open, a touch only selects the slider and the loop keeps playing, so its settings can be changed while it loops. A gate held across the loop start drops briefly to retrigger envelopes. Recordings are not kept over power-off.

While the menu is open, the selected slider's LEDs breathe. Quantized sliders show a dot at the current note instead of a bar.

Keep fingers off the touch surfaces while powering up: the firmware measures each electrode's untouched level at start-up.

## Hardware

- Teensy 4.0
- 11 × MPR121 capacitive touch controllers (8 sliders, 2 XY pads, 1 for the buttons), selected through an MCP23018 port expander
- 7 × MCP4728 quad 12-bit DACs behind a TCA9548A I2C multiplexer
- 4 × PCA9685 LED drivers
- 8×2 character LCD and a rotary encoder

The schematic and board are DipTrace files, the panel artwork is an Illustrator file.

## Repository layout

```
docs/               panel image
hardware/           schematic, board, BOM, panel artwork and Gerber files
software/           firmware (PlatformIO project)
  include/          module headers; config.h holds all pins, addresses and channel counts
  src/              main.cpp, sensors, menu, modes, sequencer, presets, recorder, storage, system, analogout (DACs and gates), led, display
```

## Firmware

The firmware is a PlatformIO project in `software/`. Its library dependencies are pinned in `platformio.ini` and installed by PlatformIO.

```sh
cd software
pio run -t upload                      # build and flash
pio run -e teensy40_debug -t upload    # debug build with a serial data stream
pio run -e teensy40_dactest -t upload  # DAC test: SET 1-4 set every output to 0, 1365, 2730 or 4095
```

The output voltages were measured with the DAC test build at those four values; the table in `software/src/analogout.cpp` converts volts to DAC values per output, which keeps quantized notes in tune (each output spans about 0–8.2 V).

### How sensing works

- The MPR121s' own baseline tracking is switched off. It absorbs the partial signal on neighbouring electrodes and makes sliding jumpy, so the firmware keeps its own baseline per electrode and tracks it slowly while untouched.
- Position is the weighted centroid of the electrodes under a finger, so it is continuous across electrode boundaries. Separate groups of electrodes are separate fingers.
- Smoothing adapts to finger speed (a 1€ filter) and adds a little play, so a resting finger stays still while fast moves stay fast. On release, the position from before the finger started lifting is kept.
- Pressure is measured from the size of the contact, calibrated between a light touch and a firm press. On the sliders, the position shift that pressing causes is compensated.

The tuning constants sit at the top of `software/src/sensors.cpp` (sensing, smoothing, pressure calibration), `led.cpp` (pad LED glow), `menu.cpp` (encoder steps) and `modes.cpp` (note hysteresis, retrigger); the button electrodes are in `config.h`.

### Debug stream

The `teensy40_debug` build prints comma-separated lines over USB serial:

| Line | Fields |
|---|---|
| `D` | time in ms, channel, 12 electrode values, raw (slider) or X (pad) position, filtered (slider) or Y (pad) position, pressure |
| `R` | time in ms, channel, position kept on release |
| `B` | time in ms, 12 button electrode values, touched bits |
| `C` | time in ms, electrode configuration register of each MPR121, every 5 s |
| `E` | at start-up, before anything is loaded: EEPROM address and 32 bytes in hex, a backup of everything saved |

Channels 0–7 are the sliders and 8–9 the XY pads. Recording this stream while touching the panel is how the calibration values were measured.
