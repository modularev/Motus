"""Labels for the Motus panel, for a Brother P-touch PT-P700 on 6 mm tape.

    python3 docs/labels/make_labels.py

Writes, next to this script, for the buttons and for the jacks:
  motus-<strip>-180dpi.png   the strip to print: one pixel per printer dot (180 dpi), 32 dots
                             high, the printable height of 6 mm tape; black prints
  motus-<strip>-preview.png  the same strip, 8x, as it comes out on white-on-black tape
and docs/panel-labelled.png: the panel with the strips on, trimmed to fit, for the manual.

Needs Pillow and Arial Narrow Bold Italic from macOS (the panel's own lettering is Arial Bold Italic).
"""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
DOCS = os.path.dirname(HERE)
FONT = '/System/Library/Fonts/Supplemental/Arial Narrow Bold Italic.ttf'

DPI = 180
DOTS_PER_MM = DPI / 25.4
PRINT_DOTS = 32  # printable height of 6 mm tape on the PT-P700, centered on the tape
TAPE_MM = 6.0
TICK_DOTS = 3    # cut marks at the top and bottom edge between labels
PANEL_PX_PER_MM = 1518 / 128.5  # panel.png is 3U high


class Strip:
    def __init__(self, name, texts, label_mm, cap_mm):
        self.name = name
        self.texts = texts
        self.label_mm = label_mm  # each label's length along the tape
        self.cap_mm = cap_mm      # capital height; one size for every label of the strip

    def font(self, px_per_mm):
        """cap_mm capitals, smaller only if the longest label would not fit"""
        room = (self.label_mm - 1.2) * px_per_mm
        size = self.cap_mm * px_per_mm / 0.716  # Arial's capitals are 0.716 of the size
        while True:
            font = ImageFont.truetype(FONT, round(size))
            if max(font.getlength(t) for t in self.texts) <= room:
                return font
            size *= 0.97

    def draw_label(self, draw, text, x0, y_mid, px_per_mm, fill):
        """the text centered on a label starting at x0, its capitals centered on y_mid"""
        font = self.font(px_per_mm)
        cap_top, cap_bottom = font.getbbox('H')[1], font.getbbox('H')[3]
        x = x0 + (self.label_mm * px_per_mm - font.getlength(text)) / 2
        draw.text((x, y_mid - (cap_bottom - cap_top) / 2 - cap_top), text, font=font, fill=fill)

    def print_strip(self):
        label = round(self.label_mm * DOTS_PER_MM)
        lead = label // 2  # a little tape before the first and after the last label
        width = lead * 2 + label * len(self.texts)
        strip = Image.new('L', (width, PRINT_DOTS), 255)
        draw = ImageDraw.Draw(strip)
        for i, text in enumerate(self.texts):
            self.draw_label(draw, text, lead + i * label, PRINT_DOTS / 2, DOTS_PER_MM, 0)
        for i in range(len(self.texts) + 1):
            x = lead + i * label
            draw.line([(x, 0), (x, TICK_DOTS - 1)], fill=0)
            draw.line([(x, PRINT_DOTS - TICK_DOTS), (x, PRINT_DOTS - 1)], fill=0)
        strip = strip.point(lambda v: 0 if v < 128 else 255).convert('1')  # the printer has no grey
        strip.save(os.path.join(HERE, f'motus-{self.name}-180dpi.png'), dpi=(DPI, DPI))

        scale = 8
        tape = round(TAPE_MM * DOTS_PER_MM)
        preview = Image.new('RGB', (width * scale, tape * scale), (20, 20, 20))
        dots = strip.convert('L').resize((width * scale, PRINT_DOTS * scale), Image.NEAREST)
        ink = Image.new('RGB', dots.size, (245, 245, 240))
        preview.paste(ink, (0, (tape - PRINT_DOTS) * scale // 2), dots.point(lambda v: 255 - v))
        preview.save(os.path.join(HERE, f'motus-{self.name}-preview.png'))
        return width / DOTS_PER_MM


# the buttons: the row above the display, then the row below it
BUTTONS = Strip('buttons', ['A·PLAY', 'B·OUT', 'C·TIME', 'D·SEQ',  # SET 1-4
                            'EDIT', 'REC', 'RUN', 'PATCH'],       # FINE, HOLD, LOAD, SAVE
                label_mm=9.5, cap_mm=2.3)                         # the buttons are 10.2-10.8 mm apart
# the jacks: force and position of sliders 1-8, then the master's m.force and m.pos
JACKS = Strip('jacks', ['mod'] * 8 + ['cv'] * 8 + ['m.mod', 'm.cv'],
              label_mm=12.0, cap_mm=2.0)                          # the jacks are 13.3-15.9 mm apart

SLIDER_X = [535, 723, 910, 1098, 1285, 1473, 1660, 1848]  # jack centers in panel.png pixels

# where each label goes on panel.png: center x, center y and the tape height
# there, trimmed to the room between the jacks and lines (measured)
PLACES = [
    # buttons: between the var jacks and the buttons (5.7 mm), between the display and the buttons (6.5 mm)
    (BUTTONS, 'A·PLAY', 2020, 440, 5.6), (BUTTONS, 'B·OUT', 2148, 440, 5.6),
    (BUTTONS, 'C·TIME', 2276, 440, 5.6), (BUTTONS, 'D·SEQ', 2403, 440, 5.6),
    (BUTTONS, 'EDIT', 2036, 925, 6.0), (BUTTONS, 'REC', 2156, 925, 6.0),
    (BUTTONS, 'RUN', 2276, 925, 6.0), (BUTTONS, 'PATCH', 2396, 925, 6.0),
] + [
    # force: between the panel's top line (y 71) and the jacks (135): 5.4 mm
    (JACKS, 'mod', x, 103, 5.3) for x in SLIDER_X
] + [
    # position: between the force jacks (229) and the position jacks (281): 4.4 mm
    (JACKS, 'cv', x, 255, 4.3) for x in SLIDER_X
] + [
    # master: between the top line and the jacks (137): 5.6 mm; m.gate stays
    (JACKS, 'm.mod', 2052, 104, 5.5), (JACKS, 'm.cv', 2210, 104, 5.5),
]


def labelled_panel():
    panel = Image.open(os.path.join(DOCS, 'panel.png')).convert('RGB')
    draw = ImageDraw.Draw(panel)
    for strip, text, x, y, tape_mm in PLACES:
        w = strip.label_mm * PANEL_PX_PER_MM
        h = tape_mm * PANEL_PX_PER_MM
        draw.rounded_rectangle([x - w / 2, y - h / 2, x + w / 2, y + h / 2], radius=3, fill=(14, 14, 14),
                               outline=(60, 60, 60))
        strip.draw_label(draw, text, x - w / 2, y, PANEL_PX_PER_MM, (255, 255, 255))
    panel.save(os.path.join(DOCS, 'panel-labelled.png'))


if __name__ == '__main__':
    for strip in (BUTTONS, JACKS):
        length = strip.print_strip()
        print(f'{strip.name}: {length:.1f} mm, {len(strip.texts)} labels of {strip.label_mm} mm')
    labelled_panel()
