#!/usr/bin/env python3
"""mkfont_px.py - a pixel TrueType font (drawn on a pixel grid, e.g. Not Jam, CC0) at its own size into
the port's EUF1 bitmap font. Host tool (Python 3 + Pillow).

    mkfont_px.py <font.ttf> <size> <out.euf> [--bold] [--track n] [--preview out.png]

EUF1: "EUF1", cell, first (32), count (224), ascent, advances[count], then count glyphs of cell x cell
bytes (0/1), rows top to bottom; Latin-1. The cell is as tall as the ink of all glyphs (tight).
Latin-1 letters the font lacks (many pixel fonts stop at ASCII) are built from the base letter and a
drawn accent (grave, acute, circumflex, tilde, diaeresis, ring, cedilla, stroke), the letter pushed
down or the accent squeezed so it fits the cell. --bold smears every glyph one pixel to the right."""
import sys, unicodedata
from PIL import Image, ImageDraw, ImageFont

ACC = {  # accent bitmaps, rows top to bottom, '#' = ink, centred over the letter
    '0300': ['#.', '.#'], '0301': ['.#', '#.'], '0302': ['.#.', '#.#'], '0303': ['.#.#', '#.#.'],
    '0308': ['#.#'], '030A': ['.#.', '#.#', '.#.'],
}

def render(font, ch, W, H, base):
    im = Image.new('1', (W, H), 0)
    d = ImageDraw.Draw(im)
    d.fontmode = '1'
    d.text((2, base), ch, font=font, fill=1, anchor='ls')
    return im

def ink(im):
    return im.getbbox()

def main():
    a = sys.argv[1:]
    if len(a) < 3:
        print(__doc__); return 2
    path, size, out = a[0], int(a[1]), a[2]
    bold = '--bold' in a
    track = int(a[a.index('--track') + 1]) if '--track' in a else 0   # letter spacing change (e.g. -1)
    prev = a[a.index('--preview') + 1] if '--preview' in a else None
    font = ImageFont.truetype(path, size)
    asc, desc = font.getmetrics()
    W, H = size * 3, size * 3
    base = size * 2                              # baseline row in the scratch image
    notdef = render(font, '', W, H, base).tobytes()
    glyphs, adv = {}, {}
    for c in range(32, 256):
        if 127 <= c < 160:
            continue
        ch = chr(c)
        im = render(font, ch, W, H, base)
        missing = c > 32 and (ink(im) is None or im.tobytes() == notdef)
        if missing and c >= 192:
            dec = unicodedata.decomposition(ch).split()
            if len(dec) == 2 and dec[0] in [chr(x) for x in range(65, 123)] or (len(dec) == 2 and len(dec[0]) == 4):
                b = chr(int(dec[0], 16)); m = dec[1]
                im = render(font, b, W, H, base)
                bb = ink(im)
                if bb and m in ACC:
                    pat = ACC[m]
                    pw = len(pat[0]); ph = len(pat)
                    cx = (bb[0] + bb[2]) // 2 - pw // 2
                    top = bb[1] - ph - 1                 # one row of air over the letter
                    for y, row in enumerate(pat):
                        for x, p in enumerate(row):
                            if p == '#':
                                im.putpixel((cx + x, top + y), 1)
                elif bb and m == '0327':                 # cedilla: a hook under the letter
                    cx = (bb[0] + bb[2]) // 2
                    for (x, y) in ((cx, bb[3]), (cx + 1, bb[3] + 1), (cx, bb[3] + 2)):
                        im.putpixel((x, y), 1)
                missing = bb is None
        if missing:
            im = Image.new('1', (W, H), 0)
        if bold and c > 32:
            im2 = im.copy()
            im2.paste(1, (1, 0), mask=im.crop((0, 0, W - 1, H)))
            im = im2
        glyphs[c] = im
        adv[c] = max(1, int(round(font.getlength(ch if not missing or c == 32 else 'e'))) + (1 if bold else 0) + (track if c > 32 else 0))
        if c >= 192 and adv[c] == 0:
            adv[c] = adv.get(ord(unicodedata.decomposition(ch)[0:4] and chr(int(unicodedata.decomposition(ch).split()[0], 16))), size // 2)
    asc = [g for c, g in glyphs.items() if c < 127 and ink(g)]
    top = min(ink(g)[1] for g in asc)                 # the ASCII glyphs decide the line; accented
    bot = max(ink(g)[3] for g in asc)                 # capitals that reach higher are moved down
    for c, g in glyphs.items():
        b = ink(g)
        if b and b[1] < top:
            d = top - b[1]
            g2 = Image.new('1', g.size, 0); g2.paste(g.crop((0, 0, W, H - d)), (0, d)); glyphs[c] = g2
    cell = max(bot - top, max(ink(g)[2] for g in glyphs.values() if ink(g)) - 2)
    first, count = 32, 224
    bits = bytearray(count * cell * cell)
    for c, g in glyphs.items():
        k = c - first
        for y in range(cell):
            for x in range(cell):
                if 2 + x < W and top + y < H and g.getpixel((2 + x, top + y)):
                    bits[(k * cell + y) * cell + x] = 1
    advs = bytes(min(255, adv.get(first + k, 0)) for k in range(count))
    with open(out, 'wb') as f:
        f.write(b'EUF1' + bytes([cell, first, count, base - top]) + advs + bytes(bits))
    print('%s: cell %d, ascent %d, space %d' % (out, cell, base - top, adv[32]))
    if prev:
        t = ['The Grand Campaign 1419 - Ottoman Empire', 'Zürich, Köln, Malmö, Göteborg, Besançon, Ålborg',
             'Choose a country: click a shield. 1 January 1500']
        P = Image.new('L', (420, len(t) * (cell + 3) + 2), 30)
        for l, s in enumerate(t):
            X = 2
            for ch in s:
                k = ord(ch) - first
                if 0 <= k < count:
                    for y in range(cell):
                        for x in range(cell):
                            if bits[(k * cell + y) * cell + x] and X + x < 420:
                                P.putpixel((X + x, 2 + l * (cell + 3) + y), 255)
                    X += advs[k]
        P.save(prev)

if __name__ == '__main__':
    sys.exit(main())
