#!/usr/bin/env python3
"""bdf2euf.py - an X11 BDF bitmap font (e.g. the public-domain misc-fixed 6x10, 6x13B) into the port's
EUF1 font. Host tool, plain Python 3.

    bdf2euf.py <font.bdf> <out.euf> [--prop] [--gap n] [--preview out.pgm]

--prop: proportional - a glyph's advance is its ink width + gap (default 1), space = half the cell;
otherwise the BDF's own DWIDTH (monospaced). EUF1: "EUF1", cell, first (32), count (224), ascent,
advances[count], count glyphs of cell x cell bytes (0/1); Latin-1. cell = max(font height, width)."""
import sys

def load(path):
    g, fbb, asc = {}, None, 0
    cur = None
    with open(path, encoding='latin-1') as f:
        lines = f.read().splitlines()
    i = 0
    while i < len(lines):
        t = lines[i].split()
        if not t: i += 1; continue
        if t[0] == 'FONTBOUNDINGBOX': fbb = list(map(int, t[1:5]))
        elif t[0] == 'FONT_ASCENT': asc = int(t[1])
        elif t[0] == 'STARTCHAR': cur = {'enc': -1}
        elif t[0] == 'ENCODING': cur['enc'] = int(t[1])
        elif t[0] == 'DWIDTH': cur['dw'] = int(t[1])
        elif t[0] == 'BBX': cur['bbx'] = list(map(int, t[1:5]))
        elif t[0] == 'BITMAP':
            w, h, ox, oy = cur['bbx']
            rows = []
            for k in range(h):
                v = int(lines[i + 1 + k], 16); nb = len(lines[i + 1 + k]) * 4
                rows.append([(v >> (nb - 1 - x)) & 1 for x in range(w)])
            cur['rows'] = rows
            if 0 <= cur['enc'] < 256: g[cur['enc']] = cur
            i += h
        i += 1
    return g, fbb, asc

def main():
    a = sys.argv[1:]
    if len(a) < 2: print(__doc__); return 2
    g, (fw, fh, fx, fy), asc = load(a[0])
    prop = '--prop' in a
    gap = int(a[a.index('--gap') + 1]) if '--gap' in a else 1
    prev = a[a.index('--preview') + 1] if '--preview' in a else None
    cell = max(fh, fw)
    first, count = 32, 224
    bits = bytearray(count * cell * cell); adv = bytearray(count)
    for c in range(first, 256):
        k = c - first
        if c not in g or 127 <= c < 160: continue
        ch = g[c]; w, h, ox, oy = ch['bbx']
        top = asc - (oy + h)                       # rows from the cell top
        xs = []
        for y, row in enumerate(ch['rows']):
            for x, b in enumerate(row):
                if b:
                    cx, cy = ox + x, top + y
                    if 0 <= cx < cell and 0 <= cy < cell: bits[(k * cell + cy) * cell + cx] = 1; xs.append(cx)
        if prop:
            if c == 32: adv[k] = max(2, fw // 2)
            elif xs:                                  # shift the ink to column 0, advance = ink width + gap
                lo, hi = min(xs), max(xs)
                if lo:
                    for y in range(cell):
                        base = (k * cell + y) * cell
                        row = bits[base:base + cell]; bits[base:base + cell] = row[lo:] + bytearray(lo)
                adv[k] = hi - lo + 1 + gap
            else: adv[k] = ch.get('dw', fw)
        else: adv[k] = ch.get('dw', fw)
    if '--bold' in a:                                     # 1-px smear to the right; one pixel wider each
        for c in range(first + 1, 256):
            k = c - first
            if not adv[k]: continue
            for y in range(cell):
                base = (k * cell + y) * cell
                row = bits[base:base + cell]
                for x in range(cell - 1, 0, -1):
                    if row[x - 1]: bits[base + x] = 1
            adv[k] += 1
    # '$' is the ducat in EU2's texts (the game's own faces draw it as a coin): a coin instead of the
    # dollar sign - a full disc as tall as the capitals, standing on the baseline (a ring read as a zero)
    k = ord('$') - first
    d = max(5, asc - 1)                                   # diameter
    top = asc - d
    for i in range(cell * cell): bits[k * cell * cell + i] = 0
    r2o = (d / 2.0) ** 2; r2i = (d / 2.0 - 1.2) ** 2; c0 = (d - 1) / 2.0
    for y in range(d):
        for x in range(d):
            dd = (x - c0) ** 2 + (y - c0) ** 2
            if dd <= r2o and not (abs(x - c0 + 1) < 0.6 and abs(y - c0 + 1) < 0.6 and d >= 7):   # a full disc (a 7+ one gets a highlight hole)
                if 0 <= top + y < cell and x < cell: bits[(k * cell + top + y) * cell + x] = 1
    adv[k] = d + gap
    with open(a[1], 'wb') as f:
        f.write(b'EUF1' + bytes([cell, first, count, asc]) + bytes(adv) + bytes(bits))
    print('%s: cell %d, ascent %d, %s' % (a[1], cell, asc, 'proportional' if prop else 'monospaced'))
    if prev:
        t = ['The Grand Campaign 1419 - Ottoman Empire', 'Zürich, Köln, Malmö, Göteborg, Besançon, Ålborg',
             'Choose a country: click a shield. 1 January 1500', 'The Age of Exploration']
        W = 330; H = len(t) * (cell + 2) + 2
        img = bytearray(W * H)
        for l, s in enumerate(t):
            X = 2
            for chh in s:
                k = ord(chh) - first
                if not 0 <= k < count: continue
                for y in range(cell):
                    for x in range(cell):
                        if bits[(k * cell + y) * cell + x] and X + x < W: img[(2 + l * (cell + 2) + y) * W + X + x] = 255
                X += adv[k]
            print('  "%s": %d px' % (s, X - 2))
        with open(prev, 'wb') as f: f.write(b'P5 %d %d 255\n' % (W, H) + bytes(img))

if __name__ == '__main__':
    sys.exit(main())
