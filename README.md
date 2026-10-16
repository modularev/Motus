# Motus Touch Controller

![Motus panel](docs/panel.png)

Motus is a capacitive touch controller for modular synthesizers. It turns finger position and pressure into control voltages and gates: eight touch sliders, two XY pads, a master output that mixes the active sliders, and four preset voltages switched by the sliders.

## Controls and outputs

| Section | Outputs | What they do |
|---|---|---|
| Sliders 1–8 | `position`, `force`, `gate` | Position along the slider (or a quantized note, see slider modes), pressure of the finger, gate while touched. Position and LEDs hold their value after release. |
| XY pads 1–2 | `X1` `Y1` `Z1`, `X2` `Y2` `Z2` | Finger position on the pad and its pressure (Z). The pad LEDs glow around the finger. |
| Master | `m.pos`, `m.force`, `m.gate` | Average position output and pressure of all sliders that are touched or looping, so it stays in range; holds its value when none are. Gate while any slider gate is on. |
| Presets | `var 1`–`var 4` | Four voltages from one of eight presets, see below. |

The display shows the touched slider or pad with its value (a note name in NOTE mode), the menu while it is open, or the selected preset when idle.

### Menu

`SET 1`–`SET 4` open the four menu pages. Tapping the same pad again steps to the next setting, the encoder changes the value, and touching a slider selects which slider is edited (it keeps playing as usual). The menu closes after 5 seconds without input.

| Pad | Page | Settings |
|---|---|---|
| `SET 1` | MODE | the slider's mode: POS or NOTE |
| `SET 2` | VAR | var 1–4 of the slider's preset; turning fast sweeps faster |
| `SET 3` | EDIT | the settings of the slider's mode |
| `SET 4` | SYS | global settings (to come) |
| `FINE` | | toggle small encoder steps for exact values |
| `SAVE` | | store all presets and settings; they are kept over power-off |
| `LOAD` | | restore the saved presets and settings, discarding unsaved edits |

### Voltage presets

Each slider has its own preset of four voltages. Touching a slider switches `var 1`–`var 4` to that slider's preset.

### Slider modes

- **POS**: the position output follows the finger.
- **NOTE**: the position is quantized to a scale and output at 1 V/octave, so the slider plays like a keyboard strip or, swept, an arpeggio. Settings: scale (chromatic, major, minor, major and minor pentatonic, dorian, harmonic minor, blues, whole tone), root, lowest octave (0 V = octave 0), range in octaves, play (slide: the note follows the finger; touch: the note is set where the finger lands and stays while sliding, a second finger picks a new one), slew (glide between notes, off to 1 s, continuing after release), and gate: hold, or retrigger on every new note. A little hysteresis keeps a resting finger from flickering between notes.

### Two fingers

A second finger on a slider or pad makes the value jump to it; lifting it jumps back. Lifting the first finger instead keeps the value at the second one without ending the touch. Use it for square-wave modulation or to jump to a new value mid-gesture. The fingers need at least one electrode of space between them to count as two.

### Recording

| Action | Result |
|---|---|
| Tap `HOLD` | arm recording (`R` on the display, top LEDs of the sliders blink) |
| Touch a slider or pad | recording starts; everything is recorded, including lifts and pauses, so rhythms loop too. The control's LEDs blink while recording. |
| Tap `HOLD` again | the recording loops (20 seconds at most) on that control's outputs and LEDs; the LEDs flash at every loop start |
| Hold `HOLD` + touch a looping control | stop that loop; the touch itself plays nothing |
| Keep holding `HOLD` | after 0.8 s a bar fills over 1.5 s, then all loops stop; letting go earlier cancels |

A slider records position, force and gate, a pad X, Y and Z, including two-finger jumps. Every slider and pad can loop its own recording at the same time. Touching a looping control plays live over it while you touch; with the menu open, a touch only selects the slider and the loop keeps playing, so its settings can be changed while it loops. A gate held across the loop start drops briefly to retrigger envelopes. Recordings are not kept over power-off.

While the menu is open, the selected slider's LEDs breathe. Sliders in NOTE mode show a dot at the current note instead of a bar.

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

The output voltages were measured with the DAC test build at those four values; the table in `software/src/analogout.cpp` converts volts to DAC values per output, which keeps NOTE mode in tune (each output spans about 0–8.2 V).

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

Channels 0–7 are the sliders and 8–9 the XY pads. Recording this stream while touching the panel is how the calibration values were measured.
