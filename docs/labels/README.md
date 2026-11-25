# Button labels

Tape strips that relabel the eight touch buttons for the current firmware. They go over the old panel lettering:

| Button | Old print | Label |
|---|---|---|
| top row, under `var 1`–`var 4` | `SET 1`–`SET 4` | `A·PLAY` `B·OUT` `C·TIME` `D·SEQ` |
| bottom row, under the display | `FINE` `HOLD` `LOAD` `SAVE` | `EDIT` `REC` `RUN` `PATCH` |

`../panel-labelled.png` shows the panel with the strips on.

## Files

- `motus-labels-180dpi.png`: the strip to print. One pixel per printer dot (180 dpi), 32 dots high, which is the printable height of 6 mm tape on the PT-P700. Black pixels print. 8 labels of 9.5 mm, about 85 mm in all, with cut marks at the top and bottom edges between them.
- `motus-labels-preview.png`: the same strip enlarged, as it comes out on white-on-black tape.
- `make_labels.py`: makes all three images (`python3 docs/labels/make_labels.py`). Names, sizes and positions are at the top of the script.

## Printing on the PT-P700

- Tape: 6 mm TZe. White on black (TZe-315) matches the black panel.
- Print the whole strip in one go. The printer feeds about 25 mm of blank tape before each print, so single labels waste tape.
- The image must print at 100 %, one pixel per dot:
  - **P-touch Editor (macOS)**: open a new 6 mm label, insert the PNG, and set its size to 84.9 × 4.5 mm (or its scale to 100 %) with the label length on auto. Set the margins to the smallest setting.
  - **`ptouch-print`** (open source, works with the PT-P700 over USB): `ptouch-print --image motus-labels-180dpi.png`. Switch the printer's Editor Lite mode off first (its LED off), or it shows up as a USB drive instead of a printer.
- Cut at the marks. Each label is 9.5 mm wide, and the buttons are 10.2–10.8 mm apart.

## Fitting

- **Bottom row:** there is 6.5 mm between the display frame and the buttons, so the 6 mm tape fits as it is.
- **Top row:** there is only about 5.7 mm between the `var` jack rings and the buttons. Trim about 0.5 mm off the top edge of each label, or let the tape slightly overlap the top edge of the button. Touch still works through tape, but the trimmed version looks cleaner. The print is centered on the tape with about 0.75 mm free at each edge, so trimming 0.5 mm doesn't touch the letters.
- Clean the panel with a little alcohol first. Then press each label on centered over the old text, which it covers completely (the old text is at most 7.1 mm wide).
