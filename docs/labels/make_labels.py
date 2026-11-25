"""Button labels for the Motus panel, for a Brother P-touch PT-P700 on 6 mm tape.

    python3 docs/labels/make_labels.py

Writes, next to this script:
  motus-labels-180dpi.png  the strip to print: one pixel per printer dot (180 dpi), 32 dots high,
                           the printable height of 6 mm tape; black prints
  motus-labels-preview.png the same strip, 8x, to look at
and docs/panel-labelled.png: the panel with the strips on, for the manual.

Needs Pillow and Arial Narrow Bold Italic from macOS (the panel's own lettering is Arial Bold Italic).
"""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
DOCS = os.path.dirname(HERE)

# the buttons, in panel order: the row above the display, then the row below it
TOP = ['A·PLAY', 'B·OUT', 'C·TIME', 'D·SEQ']   # SET 1-4
BOTTOM = ['EDIT', 'REC', 'RUN', 'PATCH']        # FINE, HOLD, LOAD, SAVE
LABELS = TOP + BOTTOM

FONT_NARROW = '/System/Library/Fonts/Supplemental/Arial Narrow Bold Italic.ttf'

DPI = 180
DOTS_PER_MM = DPI / 25.4
PRINT_DOTS = 32       # printable height of 6 mm tape on the PT-P700
TAPE_MM = 6.0
LABEL_MM = 9.5        # each label; the buttons are 10.2-10.8 mm apart
CAP_MM = 2.3          # capital height, a little larger than the panel's own 1.7 mm
MARGIN_MM = 0.6       # free at each end of a label, for cutting
TICK_DOTS = 4         # cut marks at the top and bottom edge between labels


def fit_font(px_per_mm):
    """one font and size for every label, so they all match: CAP_MM capitals in
    Arial Narrow Bold Italic, smaller only if the longest label would not fit"""
    room = (LABEL_MM - 2 * MARGIN_MM) * px_per_mm
    size = CAP_MM * px_per_mm / 0.716  # Arial's capitals are 0.716 of the size
    while True:
        font = ImageFont.truetype(FONT_NARROW, round(size))
        if max(font.getlength(text) for text in LABELS) <= room:
            return font
        size *= 0.97


def draw_label(draw, text, x0, y_mid, px_per_mm, fill):
    """the text centered on a label starting at x0, its capitals centered on y_mid"""
    font = fit_font(px_per_mm)
    width = font.getlength(text)
    cap_top = font.getbbox('H')[1]
    cap_height = font.getbbox('H')[3] - cap_top
    x = x0 + (LABEL_MM * px_per_mm - width) / 2
    y = y_mid - cap_height / 2 - cap_top
    draw.text((x, y), text, font=font, fill=fill)


def print_strip():
    label = round(LABEL_MM * DOTS_PER_MM)
    lead = label // 2  # a little tape before the first and after the last label
    width = lead * 2 + label * len(LABELS)
    strip = Image.new('L', (width, PRINT_DOTS), 255)
    draw = ImageDraw.Draw(strip)
    for i, text in enumerate(LABELS):
        x0 = lead + i * label
        draw_label(draw, text, x0, PRINT_DOTS / 2, DOTS_PER_MM, 0)
    for i in range(len(LABELS) + 1):
        x = lead + i * label
        draw.line([(x, 0), (x, TICK_DOTS - 1)], fill=0)
        draw.line([(x, PRINT_DOTS - TICK_DOTS), (x, PRINT_DOTS - 1)], fill=0)
    strip = strip.point(lambda v: 0 if v < 128 else 255).convert('1')  # the printer has no grey
    strip.save(os.path.join(HERE, 'motus-labels-180dpi.png'), dpi=(DPI, DPI))

    # preview: the tape as it comes out of the printer (white on black tape)
    scale = 8
    tape = round(TAPE_MM * DOTS_PER_MM)
    preview = Image.new('RGB', (width * scale, tape * scale), (20, 20, 20))
    dots = strip.convert('L').resize((width * scale, PRINT_DOTS * scale), Image.NEAREST)
    ink = Image.new('RGB', dots.size, (245, 245, 240))
    preview.paste(ink, (0, (tape - PRINT_DOTS) * scale // 2), dots.point(lambda v: 255 - v))
    preview.save(os.path.join(HERE, 'motus-labels-preview.png'))
    return width / DOTS_PER_MM


def labelled_panel():
    """the panel image with the tape strips on, where the old labels were"""
    panel = Image.open(os.path.join(DOCS, 'panel.png')).convert('RGB')
    px_per_mm = panel.height / 128.5  # 3U
    draw = ImageDraw.Draw(panel)
    # label centers in panel.png pixels, measured from the old labels, buttons and jacks
    top_x = [2020, 2148, 2276, 2403]
    top_y = 440    # between the var jacks (bottom 406) and the buttons (top 473)
    bottom_x = [2036, 2156, 2276, 2396]
    bottom_y = 925  # between the display frame (886) and the buttons (963)
    w = LABEL_MM * px_per_mm
    h = TAPE_MM * px_per_mm
    for texts, xs, y in ((TOP, top_x, top_y), (BOTTOM, bottom_x, bottom_y)):
        for text, x in zip(texts, xs):
            draw.rounded_rectangle([x - w / 2, y - h / 2, x + w / 2, y + h / 2], radius=3, fill=(14, 14, 14),
                                   outline=(60, 60, 60))
            draw_label(draw, text, x - w / 2, y, px_per_mm, (255, 255, 255))
    panel.save(os.path.join(DOCS, 'panel-labelled.png'))


if __name__ == '__main__':
    length = print_strip()
    labelled_panel()
    print(f'strip: {length:.1f} mm, {len(LABELS)} labels of {LABEL_MM} mm')
