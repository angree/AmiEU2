#!/usr/bin/env python3
"""Reference reader for the eu2conv data set (docs/DATA_FORMAT.md). Developer tool.

  paktool.py list  <pak>                      list entries
  paktool.py get   <pak> <name> <outfile>     raw entry bytes
  paktool.py png   <pak> <name> <out.png>     decode an EUI1 image (needs Pillow)
  paktool.py check <pak>                      decode every EUI1 / EUS1 entry, report errors
"""
import struct, sys

def read_pak(path):
    f = open(path, 'rb')
    magic, ver, count, doff, dsize = struct.unpack('>4sIIII', f.read(20))
    assert magic == b'EU2P' and ver == 1, 'not an EU2P v1 pack'
    f.seek(doff)
    d = f.read(dsize)
    ents, p = {}, 0
    for _ in range(count):
        off, size, nl = struct.unpack('>IIB', d[p:p + 9])
        name = d[p + 9:p + 9 + nl].decode('latin-1')
        p += 9 + nl
        p += p & 1
        ents[name] = (off, size)
    return f, ents

def unpackbits(data, units, u):
    out, p = bytearray(), 0
    while len(out) < units * u:
        n = data[p]; p += 1
        if n < 128:
            out += data[p:p + (n + 1) * u]; p += (n + 1) * u
        else:
            out += data[p:p + u] * (257 - n); p += u
    return bytes(out[:units * u])

def decode_eui(b):
    assert b[:4] == b'EUI1', 'not EUI1'
    w, h, fmt, flags, nc = struct.unpack('>HHBBH', b[4:12])
    pal = b[12:12 + nc * 3]
    p = 12 + nc * 3
    dl, = struct.unpack('>I', b[p:p + 4]); p += 4
    data = b[p:p + dl]
    u = 2 if fmt == 2 else 1
    pix = unpackbits(data, w * h, u) if flags & 1 else data
    assert len(pix) == w * h * u, 'short pixel data'
    return w, h, fmt, pal, pix

def main():
    cmd, pak = sys.argv[1], sys.argv[2]
    f, ents = read_pak(pak)
    if cmd == 'list':
        for n, (o, s) in sorted(ents.items()):
            print('%10d %s' % (s, n))
    elif cmd in ('get', 'png'):
        o, s = ents[sys.argv[3].lower()]
        f.seek(o); b = f.read(s)
        if cmd == 'get':
            open(sys.argv[4], 'wb').write(b)
        else:
            from PIL import Image
            w, h, fmt, pal, pix = decode_eui(b)
            if fmt == 1:
                im = Image.frombytes('P', (w, h), pix); im.putpalette(pal + bytes(768 - len(pal)))
            else:
                rgb = bytearray()
                for i in range(0, len(pix), 2):
                    v = pix[i] << 8 | pix[i + 1]
                    r, g, bl = v >> 11, (v >> 5) & 63, v & 31
                    rgb += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (bl << 3) | (bl >> 2)))
                im = Image.frombytes('RGB', (w, h), bytes(rgb))
            im.save(sys.argv[4])
            print('%s: %dx%d %s' % (sys.argv[3], w, h, 'PAL8' if fmt == 1 else 'RGB565'))
    elif cmd == 'check':
        bad = 0
        for n, (o, s) in sorted(ents.items()):
            f.seek(o); b = f.read(s)
            try:
                if b[:4] == b'EUI1':
                    decode_eui(b)
                elif b[:4] == b'EUS1':
                    rate, ln = struct.unpack('>II', b[4:12]); assert len(b) == 12 + ln
            except Exception as e:
                bad += 1; print('BAD', n, e)
        print('%d entries, %d bad' % (len(ents), bad))

if __name__ == '__main__':
    main()
