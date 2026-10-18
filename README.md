# Motus Touch Controller

![Motus panel](docs/panel.png)

Motus is a capacitive touch controller for modular synthesizers. It turns finger position and pressure into control voltages and gates: eight touch sliders, two XY pads, a master output that mixes the active sliders, and four preset voltages switched by the sliders.

## Controls and outputs

| Section | Outputs | What they do |
|---|---|---|
| Sliders 1–8 | `position`, `force`, `gate` | The slider's output (a voltage, a note or an LFO, see slider modes), pressure of the finger, gate while touched. The output and LEDs hold their value after release. |
| XY pads 1–2 | `X1` `Y1` `Z1`, `X2` `Y2` `Z2` | Finger position on the pad and its pressure (Z). The pad LEDs glow around the finger. |
| Master | `m.pos`, `m.force`, `m.gate` | Average position output and pressure of all sliders that are touched, looping or running an LFO, so it stays in range; holds its value when none are. Gate while any slider gate is on. |
| Presets | `var 1`–`var 4` | Four voltages from one of eight presets, see below. |

The display shows the touched slider or pad with its value (a voltage, note or LFO rate), the menu while it is open, or the selected preset when idle.

### Menu

`SET 1`–`SET 4` open the four menu pages. Tapping the same pad again steps to the next setting, or hold it and turn the encoder to scroll through the settings; otherwise the encoder changes the value. Touching a slider selects which slider is edited: normally it keeps playing as usual, while a SET pad is held the touch only selects and leaves the slider's outputs unchanged. The menu closes after 5 seconds without input.

A slot holds a patch: every slider's settings and the var presets. The SYS settings are global and saved with every save. The idle screen shows the current slot, with `*` while there are unsaved changes (`Slot 2*`), and the unit starts with the slot saved or loaded last. Recordings are not saved.

| Pad | Page | Settings |
|---|---|---|
| `SET 1` | MODE | the slider's mode: SLIDE or LFO |
| `SET 2` | VAR | var 1–4 of the slider's preset, shown as note and volts: semitone steps (faster when turning fast), 0.01 V with `FINE` |
| `SET 3` | EDIT | the settings of the slider's mode |
| `SET 4` | SYS | global settings, see below |
| `FINE` | | toggle small encoder steps for exact values (± on the display) |
| `SAVE` | | tap: `SAVE 2?`, turn the encoder to pick one of 4 slots, tap again to store (the second line says `replace`, `empty` or `current`) |
| `LOAD` | | tap: `LOAD 2?`, pick the slot, tap again to load it; a SET or `FINE` pad, or 5 s without input, cancels either prompt |

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

### Voltage presets

Each slider has its own preset of four voltages. Touching a slider switches `var 1`–`var 4` to that slider's preset.

### Slider modes

Every slider has two modes that share the same settings for their output range and quantizer:

- **SLIDE**: the finger sets the output. Without a quantizer the output spans the voltage range, like a fader; with one it plays the scale notes in the range, like a keyboard strip (and, swept, an arpeggio).
- **LFO**: the output runs an LFO and the finger sets its rate, which keeps running after release.

| Setting | Modes | Values |
|---|---|---|
| BASE | both | the output at the bottom of the slider, or the LFO's low point, in volts (with the note name when quantized) |
| RANGE | both | from BASE to the top of the slider or the LFO's high point: volts, or semitones when quantized (24 st = two octaves); negative inverts |
| QUANT | both | off, or a scale: chromatic, major, minor, major and minor pentatonic, dorian, harmonic minor, blues, whole tone (1 V/octave) |
| ROOT | both | the key of the scale |
| PLAY | SLIDE | slide (the output follows the finger) or touch (set where the finger lands; a second finger sets a new value) |
| SLEW | SLIDE | glide between values, off to 10 s, continuing after release; coarse steps 5 ms … 10 s, about 1 % steps with `FINE` |
| GATE | SLIDE | gate while touched, retrigger (a short drop on every new note) or trigger (on touch and every new note) |
| SHAPE | LFO | sine and triangle start halfway and rise, ramp rises from BASE, saw falls to it, square starts high, S&H holds a new random level every cycle |
| RATE | LFO | slow 0.02–1 Hz, mid 0.2–5 Hz, fast 1–25 Hz, spread exponentially along the slider |
| DEPTH | LFO | fixed, or set by pressure (held on release); less depth stays closer to BASE |
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
  src/              main.cpp, sensors, menu, modes, presets, recorder, analogout (DACs and gates), led, display
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
