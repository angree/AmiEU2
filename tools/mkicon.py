#!/usr/bin/env python3
"""Workbench icons (.info) for AmiEU2 68K - our own drawing, no game asset.

    python3 tools/mkicon.py <file.info> game|setup

Two images in one file:

1. A NewIcons image (tooltypes IM1= / IM2=) with its own palette. newicon.library (and the icon.library
   of OS 3.5+, AROS and MorphOS, which read NewIcons too) remaps it to whatever palette the Workbench
   has. A plain planar icon cannot do that: it shows pens 0..7 of the screen as they are, and on a
   Workbench whose pens are not the stock grey/black/white/blue it turns into mush (user 2026-11-11,
   Workbench 3.1 + NewIcons + wbpattern - the first version of this file did exactly that).
2. The classic planar image as the fallback for a bare OS 3.1 without NewIcons (rules from the AmiSC
   generator: 36x40, depth 3 with PlanePick 7, no pen 0, a selected image, WBTOOL, 2 MB stack).

NewIcons encoding (each IMn= tooltype is at most 127 characters after the prefix): first line
'B' (pen 0 transparent) / 'C', width + 0x21, height + 0x21, two characters of colour count
((a - 0x21) << 6 | (b - 0x21)), then the palette as RGB bytes; the image starts on the next line. Both
are bit streams of 7-bit characters (0x20..0x6F = 0..0x4F, 0xA1..0xD0 = 0x50..0x7F), MSB first, one
pixel = ceil(log2(colours)) bits. Every line here holds a whole number of characters AND of pixels
(7 pixels of 3 bits = 3 characters), so it decodes the same whether a reader carries bits across lines
or not.
"""
import struct
import sys

W, H = 36, 40

# ---------------------------------------------------------------- the drawing (8 colours, 0 transparent)
PAL = [
    (0, 0, 0),          # 0 transparent
    (16, 16, 24),       # 1 outline / shadow
    (250, 240, 200),    # 2 parchment light
    (214, 190, 130),    # 3 parchment dark
    (30, 60, 140),      # 4 royal blue
    (70, 110, 200),     # 5 light blue
    (235, 190, 40),     # 6 gold
    (170, 30, 30),      # 7 red
]
PAL_SEL = [(0, 0, 0)] + [tuple(min(255, c + (255 - c) // 3) for c in rgb) for rgb in PAL[1:]]   # selected: lighter

SMALL = {
    'A': ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    'M': ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
    'I': ["11111", "00100", "00100", "00100", "00100", "00100", "11111"],
    'S': ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    'E': ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    'T': ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    'U': ["10001", "10001", "10001", "10001", "10001", "10001", "01110"],
    'P': ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    '6': ["00110", "01000", "10000", "11110", "10001", "10001", "01110"],
    '8': ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
    'K': ["10001", "10010", "10100", "11000", "10100", "10010", "10001"],
}
BIG = {
    'E': ["1111", "1000", "1000", "1110", "1000", "1000", "1111"],
    'U': ["1001", "1001", "1001", "1001", "1001", "1001", "0110"],
    '2': ["0110", "1001", "0001", "0010", "0100", "1000", "1111"],
}


def text(px, s, font, scale, gap, top, colour, shadow=None):
    w = sum(len(font[c][0]) * scale for c in s) + gap * (len(s) - 1)
    x0 = (W - w) // 2
    for c in s:
        g = font[c]
        for gy, row in enumerate(g):
            for gx, bit in enumerate(row):
                if bit == '1':
                    for sy in range(scale):
                        for sx in range(scale):
                            x, y = x0 + gx * scale + sx, top + gy * scale + sy
                            if shadow is not None and px[y + 1][x + 1] not in (colour,):
                                px[y + 1][x + 1] = shadow
                            px[y][x] = colour
        x0 += len(g[0]) * scale + gap


def colour_pixels(kind):
    """A parchment shield: blue field with a gold EU2, AMI above, 68K / SETUP on a red band."""
    px = [[0] * W for _ in range(H)]
    for y in range(1, H - 1):
        # a shield outline: straight sides, rounded bottom
        inset = 0
        if y > 34:                                      # the point starts below the label band
            inset = (y - 34) * (y - 34) // 2
        for x in range(2 + inset, W - 2 - inset):
            px[y][x] = 2 if y < 20 else 3                 # parchment, darker towards the point
    for y in range(1, H - 1):                           # outline
        for x in range(W):
            if px[y][x] and (px[y - 1][x] == 0 or px[y + 1][x] == 0 or px[y][x - 1] == 0 or px[y][x + 1] == 0):
                px[y][x] = 1
    for y in range(10, 26):                             # the blue field
        for x in range(5, W - 5):
            px[y][x] = 5 if y < 12 else 4
    text(px, "AMI", SMALL, 1, 1, 2, 4)
    text(px, "EU2", BIG, 2, 2, 11, 6, shadow=1)
    for y in range(27, 36):                             # the red band, the full width (SETUP is 29 px)
        for x in range(3, W - 3):
            if px[y][x] not in (0, 1):
                px[y][x] = 7
    text(px, "68K" if kind == "game" else "SETUP", SMALL, 1, 1, 28, 2)
    return px


# ---------------------------------------------------------------- NewIcons encoding
def enc7(v):
    return chr(v + 0x20) if v < 0x50 else chr(v - 0x50 + 0xA1)


def bits_to_chars(bits):
    while len(bits) % 7:
        bits.append(0)
    out = []
    for i in range(0, len(bits), 7):
        v = 0
        for b in bits[i:i + 7]:
            v = (v << 1) | b
        out.append(enc7(v))
    return ''.join(out)


def newicon_lines(px, pal, prefix):
    n = len(pal)
    bpp = max(1, (n - 1).bit_length())
    assert bpp == 3, "the line split below assumes 3 bits a pixel"
    head = 'B' + chr(W + 0x21) + chr(H + 0x21) + chr((n >> 6) + 0x21) + chr((n & 63) + 0x21)
    pbits = []
    for rgb in pal:
        for c in rgb:
            pbits += [(c >> (7 - k)) & 1 for k in range(8)]
    lines = [head + bits_to_chars(pbits)]
    flat = [p for row in px for p in row]
    per_line = 7 * 38                                    # 266 pixels = 798 bits = 114 characters
    for i in range(0, len(flat), per_line):
        bits = []
        for p in flat[i:i + per_line]:
            bits += [(p >> (bpp - 1 - k)) & 1 for k in range(bpp)]
        lines.append(bits_to_chars(bits))
    for l in lines:
        assert len(l) <= 127, len(l)
    return [prefix + l for l in lines]


# ---------------------------------------------------------------- the planar fallback (stock WB 3.x pens)
def planar_pixels(kind, selected):
    dark, light, fill = 1, 2, 3
    ink, bg = (fill, light) if selected else (light, fill)
    px = [[bg] * W for _ in range(H)]
    for x in range(W):
        px[0][x] = px[H - 1][x] = dark
        px[1][x] = px[H - 2][x] = ink
    for y in range(H):
        px[y][0] = px[y][W - 1] = dark
        px[y][1] = px[y][W - 2] = ink
    for y in (1, H - 2):
        px[y][0] = px[y][W - 1] = dark
    text(px, "AMI", SMALL, 1, 1, 4, ink)
    text(px, "EU2", BIG, 2, 2, 13, ink)
    text(px, "68K" if kind == "game" else "SETUP", SMALL, 1, 1, 30, ink)
    return px


def image(px, depth=3):
    words = (W + 15) // 16
    data = bytearray()
    for plane in range(depth):
        for y in range(H):
            bits = 0
            for x in range(W):
                bits = (bits << 1) | ((px[y][x] >> plane) & 1)
            data += (bits << (words * 16 - W)).to_bytes(words * 2, 'big')
    return struct.pack('>hhhhhIBBI', 0, 0, W, H, depth, 1, (1 << depth) - 1, 0, 0) + bytes(data)


def make_info(kind):
    tooltypes = ["*** DON'T EDIT THE FOLLOWING LINES!! ***"]
    tooltypes += newicon_lines(colour_pixels(kind), PAL, "IM1=")
    tooltypes += newicon_lines(colour_pixels(kind), PAL_SEL, "IM2=")
    gadget = struct.pack('>IhhhhHHHIIIiIHI', 0, 0, 0, W, H, 0x0006, 0x0003, 1, 1, 1, 0, 0, 0, 0, 0)
    head = struct.pack('>HH', 0xE310, 1) + gadget + struct.pack(
        '>BBIIIIIII', 3, 0, 0, 1, 0x80000000, 0x80000000, 0, 0, 2000000)   # WBTOOL, tooltypes follow, 2 MB stack
    assert len(head) == 78
    out = head + image(planar_pixels(kind, False)) + image(planar_pixels(kind, True))
    out += struct.pack('>I', (len(tooltypes) + 1) * 4)                    # the tooltype array's size in bytes
    for t in tooltypes:
        b = t.encode('latin-1') + b'\0'
        out += struct.pack('>I', len(b)) + b
    return out


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ("game", "setup"):
        raise SystemExit("usage: mkicon.py <file.info> game|setup")
    data = make_info(sys.argv[2])
    open(sys.argv[1], 'wb').write(data)
    print("mkicon: %s (%d bytes, %dx%d, %s, NewIcons + planar, 2 MB stack)" % (sys.argv[1], len(data), W, H, sys.argv[2]))


if __name__ == '__main__':
    main()
