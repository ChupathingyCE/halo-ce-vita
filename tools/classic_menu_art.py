#!/usr/bin/env python3
"""The Halo CE menus' additions' own art (port/vita/app0/menus/classic), drawn
here, not taken from any game (the font is Saira Semi Condensed, SIL OFL,
tools/fonts/OFL.txt). The same bytes each run.

- pausebox.png: the multiplayer pause's box for its four rows, in the look of
  the game's own pause box (its blue border, dark blue glass and the line
  over the button key).
- <lang>/main_<row>_0.png and _1.png: the main menu's new rows (MEDIA,
  FORGE) as words in the look of the game's own
  CAMPAIGN, MULTIPLAYER and SETTINGS (a picture 256 x 64, capitals 20
  pixels high from its 6th row, about 17.5 pixels a letter): 0 not focused
  (a solid mid blue), 1 focused (white with a blue glow), the two frames the
  game's column list shows by focus. In English they are only the fallback:
  the game makes them of the player's ui.map's own letters as the menus load
  (port/linux/game/menu_words.c).
- <lang>/header_lobby_options.png, header_file_share.png,
  header_mod_manager.png: LOBBY OPTIONS's, FILE SHARE's and MOD MANAGER's
  headings in the look of the Multiplayer screens' (512 x 64 at 35,11: blue
  capitals 23 pixels high); header_friends, header_party, header_add_friend,
  header_invite_friends the same for FRIENDS, PARTY, ADD FRIEND and INVITE
  FRIENDS (classic/friends.xml).
- For an MCC-only install, whose ui.map (Halo PC's) has not these of the
  Xbox's (classic/xbox_screens.xml): y_butn.png, a_butn_sm.png,
  b_butn_sm.png, the button key's Y and the small A and B in the look of the
  game's A, B and X (a coloured disc, a pale rim, the letter);
  arrow_big_<side>_<0|1>.png, the 3-wide lists' arrows; split_quarter.png
  and split_quarter_border.png, split screen's START TO JOIN quarters.
- In English (en/) and in each language of port/vita/app0/lang (es/), the
  words its file gives them; menu_tags.c picks the language's ({lang}).

    python3 tools/classic_menu_art.py port/vita/app0/menus/classic
"""
import os
import sys

import re

from PIL import Image, ImageDraw, ImageFilter, ImageFont

FONT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fonts', 'SairaSemiCondensed-Medium.ttf')
LANG = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'port', 'vita', 'app0', 'lang')
# the main menu's rows (their files' names) and the heading, in English
# (1.2.0 menu round 3, owner Nov 8: MEDIA - File Share, Friends, Game Manager together as ElDewrito's START >
# MEDIA - and FORGE at the bottom; 1.2.0's EXTRAS comes with Command in 1.1.4)
ROWS = (('media', 'MEDIA'), ('forge', 'FORGE'))
HEADINGS = (('header_lobby_options', 'LOBBY OPTIONS'),
            # (Classic+'s screens for the port's features: classic/forge.xml, customize.xml)
            ('header_forge', 'FORGE'), ('header_extra_objects', 'EXTRA OBJECTS'),
            ('header_customization', 'CUSTOMIZATION'),
            # (classic/fileshare.xml, mods.xml)
            ('header_file_share', 'FILE SHARE'), ('header_mod_manager', 'MOD MANAGER'),
            # (classic/friends.xml)
            ('header_friends', 'FRIENDS'), ('header_party', 'PARTY'),
            ('header_add_friend', 'ADD FRIEND'), ('header_invite_friends', 'INVITE FRIENDS'),
            ('header_quick_chat', 'QUICK CHAT'), ('header_select_map', 'SELECT MAP'),
            # (classic/standins.xml: the Halo 3 set's NETWORK, PLAYERS and GAME TYPES in this look)
            ('header_select_network', 'SELECT NETWORK'), ('header_players', 'PLAYERS'),
            ('header_game_types', 'GAME TYPES'), ('header_choose_a_base', 'CHOOSE A BASE'),
            ('header_edit_game_type', 'EDIT GAME TYPE'),
            # (classic/fileshare_friends.xml: File Share with friends)
            ('header_sharing', 'SHARING'), ('header_send_to_a_friend', 'SEND TO A FRIEND'),
            # (classic/modes.xml: the main menu's MEDIA)
            ('header_media', 'MEDIA'),
            )
# (classic/xbox_screens.xml: an Xbox screen's heading an MCC-only install's ui.map has not, at the
# size of the Xbox's own headings - capitals about 20 high, 18 a letter)
XBOX_HEADINGS = (('header_select_profile_to_edit', 'SELECT PROFILE TO EDIT'),
                 ('header_advanced_controls', 'ADVANCED CONTROLS'))

# (the widget it is drawn into, fit="widget": 226 x 229; the PNG a power of
# two, so drawn at that scale and stretched back by the game)
WIDGET = (226, 229)
SIZE = (512, 512)
BORDER = (40, 150, 255, 255)
SEPARATOR = (32, 94, 154, 255)
# (where the line over the button key falls, in the widget's pixels)
SEPARATOR_Y = 200


def pausebox():
    sx, sy = SIZE[0] / WIDGET[0], SIZE[1] / WIDGET[1]
    image = Image.new('RGBA', SIZE, (0, 0, 0, 0))
    # the glass: lighter at the top, darkest under the line
    glass = Image.new('RGBA', SIZE, (0, 0, 0, 0))
    pixels = glass.load()
    for y in range(SIZE[1]):
        t = y / sy / WIDGET[1]
        if y / sy < SEPARATOR_Y:
            colour = tuple(round(a + (b - a) * (y / sy / SEPARATOR_Y)) for a, b in zip((20, 40, 66), (10, 20, 36)))
            alpha = 200
        else:
            colour, alpha = (6, 10, 16), 215
        for x in range(SIZE[0]):
            pixels[x, y] = colour + (alpha,)
    mask = Image.new('L', SIZE, 0)
    radius = (round(9 * sx), round(9 * sy))
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, SIZE[0] - 1, SIZE[1] - 1), radius=min(radius), fill=255)
    image.paste(glass, (0, 0), mask)
    draw = ImageDraw.Draw(image)
    width = round(2 * sx)
    draw.rounded_rectangle((width // 2, width // 2, SIZE[0] - 1 - width // 2, SIZE[1] - 1 - width // 2),
                           radius=min(radius), outline=BORDER, width=width)
    line_y = round(SEPARATOR_Y * sy)
    draw.rectangle((width, line_y, SIZE[0] - 1 - width, line_y + round(sy) - 1), fill=SEPARATOR)
    return image


# (room over the capitals for a translation's marks - the Spanish N's tilde, an accent -: a share of
# the capitals' height; before it the marks were cut off, "AÑADIR" drawn "ANADIR")
MARK_ROOM = 0.4


def letters(text, cap, scale=4):
    """the text's shape, white on black, capitals cap pixels high (drawn at
    scale times and left so), the capitals' top MARK_ROOM of their height
    under its top row; with the rows over the capitals' top"""
    probe = ImageFont.truetype(FONT, 100)
    box = probe.getbbox('H')
    font = ImageFont.truetype(FONT, round(100 * cap * scale / (box[3] - box[1])))
    stroke = scale
    # (only for a word with a mark: the others drawn as they always were)
    room = scale * round(MARK_ROOM * cap) if any(ord(c) > 0x7F for c in text) else 0
    bounds = font.getbbox(text, stroke_width=stroke)
    top = font.getbbox('H')[1]
    image = Image.new('L', (bounds[2] - bounds[0] + 2 * stroke, room + round(cap * scale) + 4 * stroke), 0)
    ImageDraw.Draw(image).text((stroke - bounds[0], room + stroke - top), text, font=font, fill=255,
                               stroke_width=stroke, stroke_fill=255)
    return image, stroke + room


def word_mask(text, cap, per_letter, most):
    """the text's shape at its size: capitals cap high, per_letter wide each
    (a space under half), no wider than most"""
    scale = 4
    image, stroke = letters(text, cap, scale)
    want = per_letter * len(text.replace(' ', '')) + per_letter * 0.45 * text.count(' ')
    width = max(1, round(min(want, most)))
    height = round(image.size[1] / scale)
    return image.resize((width, height), Image.LANCZOS), round(stroke / scale)


def main_row(text, focused):
    """a main menu row: 256 x 64 as the game's own rows' pictures, the word
    centred, its capitals 20 high from the 6th row"""
    width, height = 256, 64
    mask, margin = word_mask(text, 20, 17.5, width - 12)
    shape = Image.new('L', (width, height), 0)
    shape.paste(mask, ((width - mask.size[0]) // 2, 6 - margin))
    image = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    if focused:
        glow = shape.filter(ImageFilter.GaussianBlur(2.2)).point(lambda v: min(255, int(v * 2.2)))
        image.paste(Image.new('RGBA', (width, height), (40, 150, 255, 255)), (0, 0), glow)
        image.paste(Image.new('RGBA', (width, height), (250, 252, 255, 255)), (0, 0), shape)
    else:
        image.paste(Image.new('RGBA', (width, height), (48, 128, 205, 255)), (0, 0),
                    shape.point(lambda v: int(v * 0.8)))
    return image


def heading(text, cap=23, per_letter=23):
    """a screen's heading: 512 x 64, blue capitals 23 high from the 33rd row,
    35 pixels in, as the Multiplayer screens' headings (cap and per_letter:
    the Xbox screens' own headings' size, smaller, for those that stand for
    one: XBOX_HEADINGS)"""
    width, height = 512, 64
    mask, margin = word_mask(text, cap, per_letter, width - 40)
    image = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    shape = Image.new('L', (width, height), 0)
    shape.paste(mask, (35, 33 - margin))
    image.paste(Image.new('RGBA', (width, height), (40, 150, 255, 255)), (0, 0), shape)
    return image


# (the button key's discs: lit colour, shaded colour, rim)
BUTTONS = {'y': ((250, 205, 70), (150, 92, 8), (255, 238, 190)),
           'a': ((90, 200, 90), (10, 100, 20), (200, 240, 200)),
           'b': ((240, 70, 60), (140, 10, 10), (250, 200, 195))}


def button(letter, small=False):
    """a button of the button key (classic/xbox_screens.xml: an MCC-only install's ui.map has not
    the Xbox's Y, nor its small A and B): 32 x 32, drawn 1:1 into its widget (20 x 25, the small
    ones 16 x 20) - a disc a little taller than wide, lit from the top left, a pale rim, the white
    letter; the port's own drawing"""
    lit, shade, rim = BUTTONS[letter]
    scale = 8
    size = 32 * scale
    image = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    box = (1 * scale, 3 * scale, 19 * scale, 23 * scale) if not small else (1 * scale, 2 * scale, 15 * scale, 18 * scale)
    disc = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    pixels = disc.load()
    cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
    rx, ry = (box[2] - box[0]) / 2, (box[3] - box[1]) / 2
    for y in range(box[1], box[3]):
        for x in range(box[0], box[2]):
            # (light from the top left: bright gold to a deep amber)
            t = max(0.0, min(1.0, ((x - box[0]) / (2 * rx) * 0.4 + (y - box[1]) / (2 * ry) * 0.8)))
            colour = tuple(round(a + (b - a) * t) for a, b in zip(lit, shade))
            pixels[x, y] = colour + (255,)
    mask = Image.new('L', (size, size), 0)
    ImageDraw.Draw(mask).ellipse(box, fill=255)
    image.paste(disc, (0, 0), mask)
    draw = ImageDraw.Draw(image)
    draw.ellipse(box, outline=rim + (255,), width=round((1.1 if small else 1.4) * scale))
    font = ImageFont.truetype(FONT, round((12 if small else 15) * scale))
    bounds = draw.textbbox((0, 0), letter.upper(), font=font)
    draw.text((cx - (bounds[0] + bounds[2]) / 2, cy - (bounds[1] + bounds[3]) / 2), letter.upper(), font=font,
              fill=(255, 255, 255, 255))
    return image.resize((32, 32), Image.LANCZOS)


def big_arrow(right, lit):
    """a 3-wide list's arrow at its side (32 x 256, drawn into 18 x 170): a tall thin wedge of
    see-through blue with a brighter edge, pointing out; frame 1 lit"""
    scale = 4
    width, height = 32 * scale, 256 * scale
    image = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    tip, back = (2 * scale, 30 * scale) if not right else (30 * scale, 2 * scale)
    points = [(back, 4 * scale), (tip, height // 2), (back, height - 4 * scale)]
    alpha = 150 if lit else 90
    draw.polygon(points, fill=(40, 120, 220, alpha))
    draw.line(points, fill=(90, 170, 255, 230 if lit else 170), width=2 * scale)
    return image.resize((32, 256), Image.LANCZOS)


def quarter_panel():
    """split screen's START TO JOIN quarter (256 x 256, its widget 255 x 165): dark see-through
    blue, lighter at the top, in a light blue rounded frame"""
    scale = 4
    panel = Image.new('RGBA', (256, 256), (0, 0, 0, 0))
    pixels = panel.load()
    for y in range(165):
        colour = tuple(round(a + (b - a) * (y / 165)) for a, b in zip((18, 44, 82), (8, 22, 46)))
        for x in range(255):
            pixels[x, y] = colour + (150,)
    mask = Image.new('L', (256 * scale, 256 * scale), 0)
    ImageDraw.Draw(mask).rounded_rectangle((1 * scale, 1 * scale, 253 * scale, 163 * scale), radius=8 * scale, fill=255)
    image = Image.new('RGBA', (256, 256), (0, 0, 0, 0))
    image.paste(panel, (0, 0), mask.resize((256, 256), Image.LANCZOS))
    frame = Image.new('RGBA', (256 * scale, 256 * scale), (0, 0, 0, 0))
    ImageDraw.Draw(frame).rounded_rectangle((1 * scale, 1 * scale, 253 * scale, 163 * scale), radius=8 * scale,
                                            outline=(60, 150, 240, 230), width=2 * scale)
    return Image.alpha_composite(image, frame.resize((256, 256), Image.LANCZOS))


def quarter_border():
    """the border of a quarter a player has joined (256 x 256, its widget 254 x 164): a light blue
    rounded frame"""
    scale = 4
    image = Image.new('RGBA', (256 * scale, 256 * scale), (0, 0, 0, 0))
    ImageDraw.Draw(image).rounded_rectangle((1 * scale, 1 * scale, 253 * scale, 163 * scale), radius=8 * scale,
                                            outline=(80, 170, 255, 255), width=2 * scale)
    return image.resize((256, 256), Image.LANCZOS)


def translations(code):
    """the language file's words for the English (port/vita/app0/lang/<code>.txt)"""
    words = {}
    with open(os.path.join(LANG, code + '.txt'), encoding='utf-8') as file:
        for line in file:
            match = re.match(r'^"((?:[^"\\]|\\.)*)" = "((?:[^"\\]|\\.)*)"', line)
            if match:
                words[match.group(1)] = match.group(2)
    return words


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else 'port/vita/app0/menus/classic'
    pausebox().save(os.path.join(folder, 'pausebox.png'), optimize=True)
    button('y').save(os.path.join(folder, 'y_butn.png'), optimize=True)
    button('a', True).save(os.path.join(folder, 'a_butn_sm.png'), optimize=True)
    button('b', True).save(os.path.join(folder, 'b_butn_sm.png'), optimize=True)
    for side in ('left', 'right'):
        for lit in (0, 1):
            big_arrow(side == 'right', lit).save(os.path.join(folder, 'arrow_big_%s_%d.png' % (side, lit)), optimize=True)
    quarter_panel().save(os.path.join(folder, 'split_quarter.png'), optimize=True)
    quarter_border().save(os.path.join(folder, 'split_quarter_border.png'), optimize=True)
    languages = {'en': {}}
    for name in sorted(os.listdir(LANG)):
        if name.endswith('.txt'):
            languages[name[:-4]] = translations(name[:-4])
    for code, words in languages.items():
        os.makedirs(os.path.join(folder, code), exist_ok=True)
        for name, english in ROWS:
            text = words.get(english, english)
            for focused in (0, 1):
                main_row(text, focused).save(os.path.join(folder, code, 'main_%s_%d.png' % (name, focused)),
                                             optimize=True)
        for name, english in HEADINGS:
            heading(words.get(english, english)).save(os.path.join(folder, code, name + '.png'), optimize=True)
        for name, english in XBOX_HEADINGS:
            heading(words.get(english, english), 20, 18).save(os.path.join(folder, code, name + '.png'),
                                                              optimize=True)


if __name__ == '__main__':
    main()
