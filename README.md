# Motus Touch Controller

![Motus panel](docs/panel.png)

Motus is a capacitive touch controller for modular synthesizers. It turns finger position and pressure into control voltages and gates: eight touch sliders, two XY pads, a master output that follows the last touched slider, and four preset voltages switched by the sliders.

## Controls and outputs

| Section | Outputs | What they do |
|---|---|---|
| Sliders 1–8 | `position`, `force`, `gate` | Position along the slider, pressure of the finger, gate while touched. Position and LEDs hold their value after release. |
| XY pads 1–2 | `X1` `Y1` `Z1`, `X2` `Y2` `Z2` | Finger position on the pad and its pressure (Z). The pad LEDs glow around the finger. |
| Master | `m.pos`, `m.force`, `m.gate` | Position and pressure of the last touched slider (live or looping); gate while any slider gate is on. |
| Presets | `var 1`–`var 4` | Four voltages from one of eight presets, see below. |

The display shows the touched slider or pad with its value, the preset value being edited, or the selected preset when idle.

### Voltage presets

Each slider has its own preset of four voltages. Touching a slider switches `var 1`–`var 4` to that slider's preset.

| Button | Action |
|---|---|
| `SET 1`–`SET 4` | Hold and turn the encoder to change that var in the current preset. Turning fast sweeps faster. |
| `FINE` | Toggle small encoder steps for exact values. |
| `SAVE` | Store all eight presets; they are kept over power-off. |
| `LOAD` | Restore the saved presets, discarding unsaved edits. |

### Two fingers

A second finger on a slider or pad makes the value jump to it; lifting it jumps back. Lifting the first finger instead keeps the value at the second one without ending the touch. Use it for square-wave modulation or to jump to a new value mid-gesture. The fingers need at least one electrode of space between them to count as two.

### Recording

Tap `HOLD` to arm recording (`R` on the display). The next slider or pad you touch is recorded until the last finger lifts, up to 20 seconds: position, force and gate on a slider, X, Y and Z on a pad, including two-finger jumps. It then loops on that control's outputs and LEDs; on a slider the gate drops briefly at the start of every loop to retrigger envelopes. Every slider and pad can loop its own recording at the same time. Touching a looping control stops its loop and it plays live again; tap `HOLD` again while armed to cancel. Recordings are not kept over power-off.

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
  src/              main.cpp, sensors, presets, recorder, analogout (DACs and gates), led, display
```

## Firmware

The firmware is a PlatformIO project in `software/`. Its library dependencies are pinned in `platformio.ini` and installed by PlatformIO.

```sh
cd software
pio run -t upload                      # build and flash
pio run -e teensy40_debug -t upload    # debug build with a serial data stream
```

### How sensing works

- The MPR121s' own baseline tracking is switched off. It absorbs the partial signal on neighbouring electrodes and makes sliding jumpy, so the firmware keeps its own baseline per electrode and tracks it slowly while untouched.
- Position is the weighted centroid of the electrodes under a finger, so it is continuous across electrode boundaries. Separate groups of electrodes are separate fingers.
- Smoothing adapts to finger speed (a 1€ filter) and adds a little play, so a resting finger stays still while fast moves stay fast. On release, the position from before the finger started lifting is kept.
- Pressure is measured from the size of the contact, calibrated between a light touch and a firm press. On the sliders, the position shift that pressing causes is compensated.

The tuning constants sit at the top of `software/src/sensors.cpp` (sensing, smoothing, pressure calibration), `led.cpp` (pad LED glow) and `presets.cpp` (encoder steps, button electrodes).

### Debug stream

The `teensy40_debug` build prints comma-separated lines over USB serial:

| Line | Fields |
|---|---|
| `D` | time in ms, channel, 12 electrode values, raw (slider) or X (pad) position, filtered (slider) or Y (pad) position, pressure |
| `R` | time in ms, channel, position kept on release |
| `B` | time in ms, 12 button electrode values, touched bits |
| `C` | time in ms, electrode configuration register of each MPR121, every 5 s |

Channels 0–7 are the sliders and 8–9 the XY pads. Recording this stream while touching the panel is how the calibration values were measured.
