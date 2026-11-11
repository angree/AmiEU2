#!/usr/bin/env python3
"""maptool.py - reference reader/renderer for the Europa Universalis II (v1.09) map tables.

Clean-room tool: formats worked out from the data files and from eu2.exe used as a
specification only (see docs/MAP_FORMAT.md).  Reads the ORIGINAL game files from the
player's own installation; nothing it writes may be committed (PNG output is derived data).

    maptool.py [--game DIR] [--out DIR] <command> [args]

    info                         sizes, tile counts, memory per zoom level
    idtbl   [SCALE]              province map from Map/id.tbl  -> idtbl_sSCALE.png
    lmid    LEVEL [SCALE]        province map from lightmapN    -> lmid_LEVEL_sSCALE.png
    shade   LEVEL [x0 y0 x1 y1]  shaded lightmap (level 1..5)   -> shade_LEVEL[_crop].png
    terrain LEVEL [x0 y0 x1 y1]  shade x province terrain colour, with borders -> terrain_*.png
    overlay [x0 y0 x1 y1]        id.tbl borders over lightmap1 shading + sprite/city marks
    compare                      lightmap1 province ids vs id.tbl, pixel by pixel
    sprites                      SpriteDB.csv / province.csv city positions vs decoded ids
    adj     PROV                 ADJACENT.TBL list for one province
    misc                         dump of boundbox/idgrid/incgrid/incognita checks

Coordinates of a crop are always level-1 (full resolution, 18944x7296) pixels.
LEVEL is the file number 1..5 (lightmap1 = full resolution, each next one halves).
"""
import os, sys, struct, zlib
from multiprocessing import Pool

GAME = os.environ.get('EU2_GAME', '/home/angree/eu2x/app')
OUT = os.environ.get('EU2_OUT', '.')
W0, H0 = 18944, 7296          # full map size (boundbox.tbl record 0, eu2.exe constants 0x4a00/0x1c80)
SPECIAL = 0x7e4               # palette entries >= this are border pixels, not plain provinces

def P(*a):
    return os.path.join(GAME, *a)

# ---------------------------------------------------------------- CSV ----------
def load_provinces():
    """id -> dict(name, terrain, city=(x,y), army, port, manu)"""
    prov = {}
    with open(P('DB', 'province.csv'), 'rb') as f:
        lines = f.read().decode('latin-1').splitlines()
    for ln in lines[1:]:
        c = ln.split(';')
        try:
            pid = int(c[0])
        except (ValueError, IndexError):
            continue
        if pid < 0:
            continue
        def xy(i):
            try:
                return (int(c[i]), int(c[i + 1]))
            except (ValueError, IndexError):
                return None
        try:
            ter = int(c[13])
        except ValueError:
            ter = 9
        prov[pid] = dict(name=c[1], terrain=ter, city=xy(28), army=xy(30), port=xy(32), manu=xy(34))
    return prov

# ---------------------------------------------------------------- id.tbl -------
def load_idtbl():
    """id.tbl: u32 nruns, u32 rowstart[7296], then nruns x (u16 x, u16 id).  Row y = runs
    rowstart[y] .. rowstart[y+1]-1 (last row ends at nruns).  A run lasts until the next x."""
    d = open(P('Map', 'id.tbl'), 'rb').read()
    n = struct.unpack_from('<I', d, 0)[0]
    rows = list(struct.unpack_from('<%dI' % H0, d, 4))
    base = 4 + 4 * H0
    assert base + 4 * n == len(d), (base, n, len(d))
    rows.append(n)
    return d, base, rows

def idtbl_row(t, y):
    d, base, rows = t
    a, b = rows[y], rows[y + 1]
    v = struct.unpack_from('<%dH' % (2 * (b - a)), d, base + 4 * a)
    return [(v[2 * i], v[2 * i + 1]) for i in range(b - a)]

def idtbl_raster(scale=1):
    import numpy as np
    t = load_idtbl()
    h = H0 // scale
    img = np.zeros((h, W0 // scale), np.uint16)
    for yy in range(h):
        runs = idtbl_row(t, yy * scale)
        row = np.zeros(W0, np.uint16)
        for i, (x, pid) in enumerate(runs):
            x1 = runs[i + 1][0] if i + 1 < len(runs) else W0
            row[x:x1] = pid
        img[yy] = row[::scale]
    return img

# ---------------------------------------------------------------- lightmaps ----
class Lightmap:
    """lightmapN.tbl: u24 offset[ntiles] (LE), u32 datasize, data[datasize].
    ntiles = tiles_x * tiles_y, tiles are 32x32 px, row-major, tiles_x = (18944>>l)/32."""
    def __init__(self, level):
        self.level = level
        self.shift = level - 1
        d = open(P('Map', 'lightmap%d.tbl' % level), 'rb').read()
        self.tw = (W0 >> self.shift) // 32
        self.th = -(-(H0 >> self.shift) // 32)
        # find ntiles: the u32 after the table must equal the remaining length
        n = None
        for cand in (self.tw * self.th, self.tw * (self.th - 1), self.tw * (self.th + 1)):
            if 3 * cand + 4 <= len(d) and struct.unpack_from('<I', d, 3 * cand)[0] == len(d) - 3 * cand - 4:
                n = cand
        assert n is not None, 'lightmap%d: table size not found' % level
        self.th = n // self.tw
        self.n = n
        self.raw = d
        self.base = 3 * n + 4
        self.size = len(d) - self.base
        self.w = self.tw * 32
        self.h = self.th * 32

    def off(self, i):
        d = self.raw
        return d[3 * i] | d[3 * i + 1] << 8 | d[3 * i + 2] << 16

    def tile(self, tx, ty):
        tx %= self.tw
        return decode_tile(self.raw, self.base + self.off(ty * self.tw + tx))

def decode_tile(d, p):
    """-> (palette, leaves, nbytes).  palette = list of u15 values.
    leaves = list of (x, y, lg, pal_index, shade) in file order (BR, BL, TR, TL recursion);
    lg = log2(size); lg 0 = single pixel from a 2x2 split (stored directly, not interpolated)."""
    p0 = p
    pal = []
    while True:
        v = d[p] | d[p + 1] << 8
        p += 2
        pal.append(v & 0x7fff)
        if v & 0x8000:
            break
    # quadtree, 1 bit per node, LSB first
    bitpos = [0]
    start = p
    def bit():
        k = bitpos[0]
        bitpos[0] = k + 1
        return (d[start + (k >> 3)] >> (k & 7)) & 1
    nodes = []
    def walk(x, y, lg):
        if bit():
            h = 1 << (lg - 1)
            if lg > 1:
                walk(x + h, y + h, lg - 1)
                walk(x, y + h, lg - 1)
                walk(x + h, y, lg - 1)
                walk(x, y, lg - 1)
            else:
                nodes.extend(((x + 1, y + 1, 0), (x, y + 1, 0), (x + 1, y, 0), (x, y, 0)))
        else:
            nodes.append((x, y, lg))
    walk(0, 0, 5)
    p = start + ((bitpos[0] + 7) >> 3)
    cnt = len(nodes)
    npal = len(pal)
    nb = 0 if npal == 1 else 1 if npal == 2 else 2 if npal <= 4 else 4 if npal <= 16 else 8
    idx = []
    if nb == 0:
        idx = [0] * cnt
    else:
        per = 8 // nb
        m = (1 << nb) - 1
        nbytes = -(-cnt // per)
        for i in range(nbytes):
            b = d[p + i]
            for k in range(per):
                idx.append((b >> (k * nb)) & m)
        idx = idx[:cnt]
        p += nbytes
    sh = []
    ngroups = -(-cnt // 4)
    for g in range(ngroups):
        b0, b1, b2 = d[p], d[p + 1], d[p + 2]
        p += 3
        sh.extend((b0 & 63, (b0 >> 6) | (b1 & 15) << 2, (b1 >> 4) | (b2 & 3) << 4, b2 >> 2))
    leaves = [(x, y, lg, idx[i], sh[i]) for i, (x, y, lg) in enumerate(nodes)]
    return pal, leaves, p - p0

def tile_ids(pal, leaves, resolve=None):
    """32x32 list-of-rows of palette values (resolve(v) maps special values)."""
    t = [[0] * 32 for _ in range(32)]
    for x, y, lg, i, s in leaves:
        v = pal[i]
        if resolve:
            v = resolve(pal, v)
        n = 1 << lg
        for yy in range(y, y + n):
            r = t[yy]
            r[x:x + n] = [v] * n
    return t

def resolve_owner(pal, v):
    """Special value (>= 0x7e4): border pixel of province pal[(v>>9)-4] (eu2.exe 0x476ebe).
    Bits 1..4 / 5..8 select neighbours in that province's adjacency list (NIEPEWNE)."""
    if v < SPECIAL:
        return v
    k = ((v >> 9) & 0x3f) - 4
    if 0 <= k < len(pal) and pal[k] < SPECIAL:
        return pal[k]
    return 0

# ---- shading (eu2.exe 0x479c00 .. 0x477e87) -----------------------------------
def edge_left(leaves, bottom):
    """Column 0 of a tile as the game writes it into column 32 of its left neighbour."""
    col = [0] * 33
    col[32] = bottom
    for x, y, lg, i, s in leaves:
        if x != 0:
            continue
        if lg == 0:
            col[y] = s
            continue
        n = 1 << lg
        a = s << 24
        st = ((col[y + n] << 24) - a) >> lg
        for j in range(n):
            col[y + j] = (a + j * st) >> 24
    return col

def edge_top(leaves, right):
    row = [0] * 33
    row[32] = right
    for x, y, lg, i, s in leaves:
        if y != 0:
            continue
        if lg == 0:
            row[x] = s
            continue
        n = 1 << lg
        a = s << 24
        st = ((row[x + n] << 24) - a) >> lg
        for j in range(n):
            row[x + j] = (a + j * st) >> 24
    return row

def tile_shade(leaves, rightcol, bottomrow):
    """32x32 shade values (0..63) of one tile, bilinear over the quadtree (eu2.exe 0x4777b0).
    buf holds the corner samples; each leaf writes its top row + left column into buf."""
    buf = [[0] * 33 for _ in range(33)]
    for j in range(33):
        buf[j][32] = rightcol[j]
    buf[32][:] = bottomrow[:33]
    out = [[0] * 32 for _ in range(32)]
    for x, y, lg, i, s in leaves:
        if lg == 0:
            buf[y][x] = s
            out[y][x] = s
            continue
        n = 1 << lg
        tl = s << 16
        tr = buf[y][x + n] << 16
        bl = buf[y + n][x] << 16
        br = buf[y + n][x + n] << 16
        hs = (tr - tl) >> lg
        brow = buf[y]
        for k in range(n):
            brow[x + k] = (tl + k * hs) >> 16
        ls = (bl - tl) >> lg
        rs = (br - tr) >> lg
        L, R = tl, tr
        for j in range(n):
            buf[y + j][x] = L >> 16
            st = (L - R) >> lg
            o = out[y + j]
            v = R + 0x8000
            for k in range(n - 1, -1, -1):
                o[x + k] = v >> 16
                v += st
            L += ls
            R += rs
    return out

_LM = None
def _lm(level):
    global _LM
    if _LM is None or _LM.level != level:
        _LM = Lightmap(level)
    return _LM

def render_strip(args):
    """Render tile row ty, tile columns tx0..tx1-1 -> (shade bytes, id u16 bytes)."""
    level, ty, tx0, tx1, want_shade = args
    lm = _lm(level)
    w = (tx1 - tx0) * 32
    sh = bytearray(w * 32)
    ids = bytearray(w * 64)
    for tx in range(tx0, tx1):
        pal, leaves, _ = lm.tile(tx, ty)
        t = tile_ids(pal, leaves, resolve_owner)
        if want_shade:
            if ty + 1 < lm.th:
                dpal, dleaves, _ = lm.tile(tx + 1, ty + 1)
                diag = dleaves[-1][4]                     # last leaf = top-left corner
                bpal, bleaves, _ = lm.tile(tx, ty + 1)
                bottom = edge_top(bleaves, diag)
            else:
                diag = 6
                bottom = [6] * 33                          # game fills 6 below the map
            rpal, rleaves, _ = lm.tile(tx + 1, ty)
            right = edge_left(rleaves, diag)
            s = tile_shade(leaves, right, bottom)
        xo = (tx - tx0) * 32
        for y in range(32):
            ro = y * w + xo
            if want_shade:
                sh[ro:ro + 32] = bytes(s[y])
            ids[2 * ro:2 * ro + 64] = struct.pack('<32H', *t[y])
    return ty, bytes(sh), bytes(ids)

def render_level(level, crop=None, want_shade=True, procs=14):
    """-> (numpy shade HxW uint8, numpy ids HxW uint16, x0, y0) in level pixels."""
    import numpy as np
    lm = Lightmap(level)
    if crop:
        sc = lm.shift
        x0, y0, x1, y1 = [c >> sc for c in crop]
        tx0, ty0 = x0 // 32, y0 // 32
        tx1, ty1 = -(-x1 // 32), -(-y1 // 32)
    else:
        tx0, ty0, tx1, ty1 = 0, 0, lm.tw, lm.th
    jobs = [(level, ty, tx0, tx1, want_shade) for ty in range(ty0, ty1)]
    W, H = (tx1 - tx0) * 32, (ty1 - ty0) * 32
    shade = np.zeros((H, W), np.uint8)
    ids = np.zeros((H, W), np.uint16)
    with Pool(procs) as pool:
        for ty, s, i in pool.imap_unordered(render_strip, jobs, chunksize=1):
            r = (ty - ty0) * 32
            shade[r:r + 32] = np.frombuffer(s, np.uint8).reshape(32, W)
            ids[r:r + 32] = np.frombuffer(i, np.uint16).reshape(32, W)
    return shade, ids, tx0 * 32, ty0 * 32

# ---------------------------------------------------------------- colours ------
def id_colour_lut(prov):
    import numpy as np
    lut = np.zeros((65536, 3), np.uint8)
    for pid in range(1, 65536):
        h = (pid * 2654435761) & 0xffffffff
        r, g, b = 60 + (h & 0x7f) + 40, 60 + ((h >> 8) & 0x7f) + 40, 60 + ((h >> 16) & 0x7f) + 40
        p = prov.get(pid)
        if p and p['terrain'] == 5:
            r, g, b = 20 + (h & 31), 50 + ((h >> 8) & 31), 140 + ((h >> 16) & 63)
        lut[pid] = (min(r, 255), min(g, 255), min(b, 255))
    lut[0] = (0, 0, 0)
    return lut

TERRAIN_RGB = {0: (170, 190, 110), 1: (70, 130, 60), 2: (150, 130, 110), 3: (225, 200, 130),
               4: (110, 140, 110), 5: (60, 100, 170), 6: (90, 150, 200), 8: (40, 40, 40), 9: (255, 0, 255)}

def borders(ids):
    import numpy as np
    b = np.zeros(ids.shape, bool)
    b[:, 1:] |= ids[:, 1:] != ids[:, :-1]
    b[1:, :] |= ids[1:, :] != ids[:-1, :]
    return b

def save(arr, name, mode=None):
    from PIL import Image
    im = Image.fromarray(arr, mode) if mode else Image.fromarray(arr)
    path = os.path.join(OUT, name)
    im.save(path)
    print('wrote', path, im.size)
    return path

def parse_crop(a):
    return tuple(int(v) for v in a[:4]) if len(a) >= 4 else None

# ---------------------------------------------------------------- commands -----
def cmd_info(a):
    tot = 0
    for lv in range(1, 6):
        lm = Lightmap(lv)
        full = lm.w * lm.h
        print('lightmap%d: %dx%d px, %dx%d tiles (%d), table %d B + data %d B = %d B on disk;'
              ' decoded: shade %d KB (1 B/px), ids %d KB (2 B/px)' % (
                  lv, lm.w, lm.h, lm.tw, lm.th, lm.n, 3 * lm.n + 4, lm.size, len(lm.raw),
                  full // 1024, full * 2 // 1024))
    t = load_idtbl()
    print('id.tbl: %d runs over %d rows, %d B' % (t[2][-1], H0, len(t[0])))

def cmd_idtbl(a):
    import numpy as np
    sc = int(a[0]) if a else 8
    img = idtbl_raster(sc)
    lut = id_colour_lut(load_provinces())
    save(lut[img], 'idtbl_s%d.png' % sc)

def cmd_lmid(a):
    import numpy as np
    lv = int(a[0]); sc = int(a[1]) if len(a) > 1 else 1
    sh, ids, x0, y0 = render_level(lv, want_shade=False)
    ids = ids[::sc, ::sc]
    lut = id_colour_lut(load_provinces())
    save(lut[ids], 'lmid_%d_s%d.png' % (lv, sc))

def cmd_shade(a):
    import numpy as np
    lv = int(a[0]); crop = parse_crop(a[1:])
    sh, ids, x0, y0 = render_level(lv, crop)
    g = (sh.astype(np.uint16) * 4).clip(0, 255).astype(np.uint8)
    save(g, 'shade_%d%s.png' % (lv, '_crop' if crop else ''))

def terrain_rgb(sh, ids, prov):
    import numpy as np
    lut = np.zeros((65536, 3), np.uint16)
    for pid, p in prov.items():
        lut[pid] = TERRAIN_RGB.get(p['terrain'], (255, 0, 255))
    rgb = lut[ids]
    f = (sh.astype(np.uint32) * 4 + 40).clip(0, 300)[..., None]
    rgb = (rgb * f // 220).clip(0, 255).astype(np.uint8)
    rgb[borders(ids)] = (200, 0, 0)
    return rgb

def cmd_terrain(a):
    lv = int(a[0]); crop = parse_crop(a[1:])
    sh, ids, x0, y0 = render_level(lv, crop)
    save(terrain_rgb(sh, ids, load_provinces()), 'terrain_%d%s.png' % (lv, '_crop' if crop else ''))

def cmd_overlay(a):
    import numpy as np
    crop = parse_crop(a) or (8800, 1100, 10400, 2300)
    sh, ids, x0, y0 = render_level(1, crop)
    t = load_idtbl()
    H, W = ids.shape
    ref = np.zeros((H, W), np.uint16)
    for r in range(H):
        y = y0 + r
        if y >= H0:
            break
        runs = idtbl_row(t, y)
        row = np.zeros(W0, np.uint16)
        for i, (x, pid) in enumerate(runs):
            x1 = runs[i + 1][0] if i + 1 < len(runs) else W0
            row[x:x1] = pid
        ref[r] = row[x0:x0 + W]
    g = (sh.astype(np.uint16) * 4).clip(0, 255).astype(np.uint8)
    rgb = np.stack([g, g, g], -1)
    rgb[borders(ref)] = (255, 40, 40)        # id.tbl borders in red
    rgb[ref == 0] = (255, 255, 0)            # id 0 in id.tbl (= drawn border line) in yellow
    from PIL import Image, ImageDraw
    im = Image.fromarray(rgb)
    dr = ImageDraw.Draw(im)
    prov = load_provinces()
    for pid, p in prov.items():
        if p['city']:
            cx, cy = p['city'][0] - x0, p['city'][1] - y0
            if 0 <= cx < W and 0 <= cy < H:
                dr.rectangle((cx - 2, cy - 2, cx + 2, cy + 2), outline=(0, 255, 0))
                dr.text((cx + 4, cy - 5), p['name'], fill=(0, 255, 255))
    path = os.path.join(OUT, 'overlay.png')
    im.save(path)
    print('wrote', path, im.size)

def cmd_compare(a):
    import numpy as np
    procs = 14
    sh, ids, x0, y0 = render_level(1, want_shade=False)
    prov = load_provinces()
    river = np.zeros(65536, bool)
    for pid, p in prov.items():
        if p['terrain'] == 6:
            river[pid] = True
    t = load_idtbl()
    same = diff = zero = tinc = riv = 0
    from collections import Counter
    hist = Counter()
    for y in range(H0):
        runs = idtbl_row(t, y)
        row = np.zeros(W0, np.uint16)
        for i, (x, pid) in enumerate(runs):
            x1 = runs[i + 1][0] if i + 1 < len(runs) else W0
            row[x:x1] = pid
        lr = ids[y]
        z = row == 0
        zero += int(z.sum())
        e = (row == lr) & ~z
        same += int(e.sum())
        ti = (row == 1615) & (lr == 0)
        tinc += int(ti.sum())
        rv = river[row] & ~e
        riv += int(rv.sum())
        dd = ~e & ~z & ~ti & ~rv
        diff += int(dd.sum())
        if dd.any():
            for a, b in zip(row[dd][::7], lr[dd][::7]):
                hist[(int(a), int(b))] += 1
    tot = W0 * H0
    pr = lambda s, n: print('  %-58s %10d  %8.4f%%' % (s, n, 100.0 * n / tot))
    print('lightmap1 province ids vs id.tbl, %d pixels:' % tot)
    pr('equal', same)
    pr('id.tbl 1615 (terra incognita fill) where lightmap1 has 0', tinc)
    pr('id.tbl river province (terrain 6), lightmap1 the land', riv)
    pr('id.tbl 0 (border line drawn by the editor)', zero)
    pr('other differences', diff)
    for (a, b), n in hist.most_common(12):
        print('     id.tbl %4d %-24s lightmap1 %4d %-24s ~%d px' % (
            a, prov.get(a, {}).get('name', '?'), b, prov.get(b, {}).get('name', '?'), n * 7))

def cmd_sprites(a):
    prov = load_provinces()
    t = load_idtbl()
    names = {p['name'].strip().lower(): pid for pid, p in prov.items()}
    rows_cache = {}
    def idat(x, y):
        if not (0 <= y < H0):
            return -1
        if y not in rows_cache:
            rows_cache[y] = idtbl_row(t, y)
        r = rows_cache[y]
        v = 0
        for xx, pid in r:
            if xx > x:
                break
            v = pid
        return v
    ok = bad = near = 0
    badl = []
    with open(P('Map', 'SpriteDB.csv'), 'rb') as f:
        lines = f.read().decode('latin-1').splitlines()[1:]
    for ln in lines:
        c = ln.split(',')
        try:
            x, y, pid = int(c[1]), int(c[2]), int(c[3])
        except (ValueError, IndexError):
            continue
        v = idat(x, y)
        nm = c[4].strip().lower() if len(c) > 4 else ''
        if v == pid or (nm and prov.get(v, {}).get('name', '').strip().lower() == nm):
            ok += 1
        else:
            hit = any(idat(x + dx, y + dy) == pid for dx in range(-3, 4) for dy in range(-3, 4))
            if hit:
                near += 1
            else:
                bad += 1
                badl.append((c[0], x, y, pid, c[4] if len(c) > 4 else '', v))
    print('SpriteDB.csv: %d sprites inside their province (id or name), %d within 3 px, %d elsewhere' % (ok, near, bad))
    for b in badl[:15]:
        print('   off:', b)
    ok = bad = 0
    badl = []
    for pid, p in prov.items():
        if pid == 0 or p['terrain'] in (5, 6, 8) or not p['city'] or p['city'] == (0, 0):
            continue
        v = idat(*p['city'])
        if v == pid:
            ok += 1
        else:
            bad += 1
            badl.append((pid, p['name'], p['city'], v, prov.get(v, {}).get('name')))
    print('province.csv City XPos/YPos: %d inside own province, %d not' % (ok, bad))
    for b in badl[:15]:
        print('   off:', b)
    # the same through lightmap1 (the file the game really uses)
    lm = Lightmap(1)
    ok = bad = 0
    for pid, p in prov.items():
        if pid == 0 or p['terrain'] in (5, 6, 8) or not p['city'] or p['city'] == (0, 0):
            continue
        x, y = p['city']
        pal, leaves, _ = lm.tile(x // 32, y // 32)
        tid = tile_ids(pal, leaves, resolve_owner)
        if tid[y % 32][x % 32] == pid:
            ok += 1
        else:
            bad += 1
    print('lightmap1 province at City XPos/YPos: %d inside own province, %d not' % (ok, bad))

def load_adjacent():
    """ADJACENT.TBL: u32 start[N+1] (index into the record array), then 12-byte records
    (u32 province, u32 type 0/1, u32 unused = 0xCDCDCDCD) - see MAP_FORMAT.md."""
    d = open(P('Map', 'ADJACENT.TBL'), 'rb').read()
    return d

def cmd_adj(a):
    pid = int(a[0])
    prov = load_provinces()
    d = load_adjacent()
    lst = adj_list(d, pid)
    print('province %d %s: %d neighbours' % (pid, prov[pid]['name'], len(lst)))
    for e in lst:
        print('   ', e, prov.get(e[0], {}).get('name'))

NPROV = 1615
def adj_list(d, pid):
    hdr = 4 * (NPROV + 1)
    a, b = struct.unpack_from('<II', d, 4 * pid)
    return [struct.unpack_from('<III', d, hdr + 12 * k) for k in range(a, b)]

def cmd_misc(a):
    d = open(P('Map', 'boundbox.tbl'), 'rb').read()
    print('boundbox.tbl', len(d), 'records', len(d) // 16)
    prov = load_provinces()
    for pid in (0, 1, 240, 1300):
        print('  ', pid, prov[pid]['name'], struct.unpack_from('<4i', d, 16 * pid))

def main():
    global GAME, OUT
    a = sys.argv[1:]
    while a and a[0].startswith('--'):
        if a[0] == '--game':
            GAME = a[1]; a = a[2:]
        elif a[0] == '--out':
            OUT = a[1]; a = a[2:]
        else:
            break
    if not a:
        print(__doc__); return
    fn = globals().get('cmd_' + a[0])
    if not fn:
        print(__doc__); sys.exit(1)
    fn(a[1:])

if __name__ == '__main__':
    main()
