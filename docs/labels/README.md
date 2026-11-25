# Panel labels

Tape strips that relabel the touch buttons and the jacks for the current firmware. They go over the old panel lettering:

| Where | Old print | Label |
|---|---|---|
| buttons, top row, under `var 1`–`var 4` | `SET 1`–`SET 4` | `A·PLAY` `B·OUT` `C·TIME` `D·SEQ` |
| buttons, bottom row, under the display | `FINE` `HOLD` `LOAD` `SAVE` | `EDIT` `REC` `RUN` `PATCH` |
| top jack of each slider | `force` | `mod` |
| middle jack of each slider | `position` | `cv` |
| master jacks | `m.force` `m.pos` | `m.mod` `m.cv` (`m.gate` and the slider `gate` jacks stay) |

`../panel-labelled.png` shows the panel with every strip on, trimmed as described below.

## Files

- `motus-buttons-180dpi.png`: 8 button labels of 9.5 mm, 85 mm in all. Capitals 2.3 mm high.
- `motus-jacks-180dpi.png`: 18 jack labels of 12 mm (8 × `mod`, 8 × `cv`, `m.mod`, `m.cv`), 228 mm in all. Capitals 2.0 mm high, so they still fit after the jack labels are trimmed.
- Both are one pixel per printer dot (180 dpi) and 32 dots high, the printable height of 6 mm tape on the PT-P700. Black pixels print. There are cut marks at the top and bottom edges between the labels.
- `motus-*-preview.png`: the same strips enlarged, as they come out on white-on-black tape.
- `make_labels.py`: makes all the images (`python3 docs/labels/make_labels.py`). Names, sizes and positions are in the script.

## Printing on the PT-P700

- Tape: 6 mm TZe. White on black (TZe-315) matches the black panel.
- Print each strip in one go. The printer feeds about 25 mm of blank tape before each print, so single labels waste tape.
- The image must print at 100 %, one pixel per dot:
  - **P-touch Editor (macOS)**: open a new 6 mm label, insert the PNG, and set its height to 4.5 mm (its width follows: 84.9 mm for the buttons, 227.8 mm for the jacks), or its scale to 100 %. Leave the label length on auto and set the margins to the smallest setting.
  - **`ptouch-print`** (open source, works with the PT-P700 over USB): `ptouch-print --image motus-buttons-180dpi.png`. Switch the printer's Editor Lite mode off first (its LED off), or it shows up as a USB drive instead of a printer.
- Cut at the marks.

## Trimming and fitting

The lettering sits in the middle of the tape, inside its 4.5 mm printable band. So the tape can be trimmed evenly from both long edges without cutting into the letters:

| Labels | Room on the panel | Trim the 6 mm tape to |
|---|---|---|
| bottom row of buttons (under the display) | 6.5 mm | nothing to trim |
| top row of buttons (under `var 1`–`4`) | 5.7 mm | 5.6 mm: about 0.2 mm off each edge, or let it touch the button edge |
| `m.mod`, `m.cv` (between the top line and the jacks) | 5.6 mm | 5.5 mm: 0.25 mm off each edge |
| `mod` (between the top line and the jacks) | 5.4 mm | 5.3 mm: 0.35 mm off each edge |
| `cv` (between the `mod` and `cv` jacks) | 4.4 mm | 4.3 mm: 0.85 mm off each edge, which leaves the 2 mm letters with 1.1 mm above and below |

A metal ruler and a sharp blade on a cutting mat work better than scissors for the `cv` labels. Clean the panel with a little alcohol first. Then press each label on centered over the old text, which it covers completely.
