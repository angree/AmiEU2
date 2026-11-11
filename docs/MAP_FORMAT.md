# EU2 map tables (`Map/*.tbl`, v1.09)

Reference reader: `tools/maptool.py` (host Python 3 + Pillow + numpy). Everything below was
worked out from the files themselves and from `eu2.exe` read as a specification (addresses are
given so the claim can be re-checked; no code was copied). Markers: **NIEPEWNE** = believed but
not proven, **NIEZNANE** = not decoded.

All numbers are **little-endian**. The map is **18944 x 7296** pixels at full resolution
(`eu2.exe` constants `0x4a00` / `0x1c80`; also record 0 of `boundbox.tbl`). X wraps around
(the code does `x += 0x4a00` / `x -= 0x4a00`), Y does not.

## Which files the game really uses

`eu2.exe` opens `map\lightmap%d.tbl` for d = 1..3 only (loop at `0x476bd4`, three 12-byte slots
at `0x801540`), plus `map\idgrid.tbl`, `map\incgrid.tbl`, `map\adjacent.tbl`, `map\boundbox.tbl`
and `map\index.tbl` (not shipped). **`id.tbl`, `lightmap4.tbl`, `lightmap5.tbl` and
`incognita.tbl` are not referenced by name** - they are editor/tool leftovers (lightmap4/5 are
nevertheless valid and decode with the same code; `incognita.tbl` NIEZNANE). The game takes
province ids from `lightmap1..3` themselves.

## lightmapN.tbl - province ids + shading, quadtree-compressed 32x32 tiles

Level N (1..5) is the full map shifted right by N-1: 18944x7296, 9472x3648, 4736x1824,
2368x912, 1184x456 px. Tiles are 32x32, row-major, `tiles_x = (18944>>(N-1))/32`.

```
0          u24 offset[ntiles]       offset of each tile, relative to the data block
3*ntiles   u32 datasize             = file size - 3*ntiles - 4  (this identifies ntiles)
3*ntiles+4 u8  data[datasize]
```

| file | px | tiles | ntiles | on disk | data block | decoded 1 B/px | ids 2 B/px |
|---|---|---|---|---|---|---|---|
| lightmap1 | 18944x7296 | 592x228 | 134976 | 11 749 936 | 11 345 004 | 135 MB | 270 MB |
| lightmap2 | 9472x3648 | 296x114 | 33744 | 4 059 884 | 3 958 648 | 33.7 MB | 67 MB |
| lightmap3 | 4736x1824 | 148x57 | 8436 | 1 689 938 | 1 664 626 | 8.4 MB | 16.9 MB |
| lightmap4 | 2368x928 (912 used) | 74x29 | 2146 | 510 693 | 504 251 | 2.1 MB | 4.3 MB |
| lightmap5 | 1184x480 (456 used) | 37x15 | 555 | 259 619 | 257 950 | 0.55 MB | 1.1 MB |

The game keeps levels 1-3 **compressed in RAM** (17.5 MB) and decodes one tile at a time
(`0x476dc0` header, `0x4784f0` ids, `0x4777b0` shading). Decoded full levels are out of the
question on a 64 MB Amiga except level 3 (8.4 MB shade) and below.

### Tile

```
u16 pal[n]        palette; read until an entry has bit 15 set (that one is the last);
                  value = entry & 0x7fff
bits  tree        quadtree, 1 bit per node, LSB-first within each byte, padded to a byte
u8    idx[]       one palette index per leaf, packed LSB-first; bits per index:
                  n==1: 0 (no bytes), n==2: 1, n<=4: 2, n<=16: 4, else 8
u8    shade[]     one 6-bit value per leaf, 4 values in 3 bytes:
                  v0=b0&63, v1=b0>>6|(b1&15)<<2, v2=b1>>4|(b2&3)<<4, v3=b2>>2
```

Quadtree, starting with a 32x32 node (log size 5) at (0,0):

```
node(x,y,lg): bit = next()
  bit==0           -> leaf covering (1<<lg)^2 pixels at (x,y)
  bit==1, lg>1     -> node(x+h,y+h,lg-1), node(x,y+h,lg-1), node(x+h,y,lg-1), node(x,y,lg-1)  (h=1<<(lg-1))
  bit==1, lg==1    -> four 1-pixel leaves, order (x+1,y+1), (x,y+1), (x+1,y), (x,y)
```

i.e. children in order **bottom-right, bottom-left, top-right, top-left**. Leaves are numbered
in that order; leaf k uses `idx[k]` and `shade[k]`. Bytes per tile = 2n + ceil(bits/8) +
ceil(leaves*bits_per_idx/8) + 3*ceil(leaves/4).

**Proof:** with this rule every one of the 179 857 tiles in all five files consumes exactly
`offset[i+1]-offset[i]` bytes (the last one exactly `datasize`) - `t1.py` style check, zero
mismatches. Leaves: 11.2 M (L1), 3.6 M, 1.36 M, 0.40 M, 0.19 M.

### Palette values

- `< 0x7e4` (2020): a province id (`DB/province.csv` Id 0..1614; 0 = Terra Incognita).
- `>= 0x7e4`: **border/river pixel** (only in levels 1-3; 4 and 5 have none). Decoded by
  `0x476e98`: `owner = pal[((v>>9)&0x3f) - 4]` (another entry of the same palette),
  `(v>>1)&15` and `(v>>5)&15` index the owner's adjacency list (struct at province+0x11c,
  12-byte entries, + base at province+0x1e); at run time the pixel becomes border colour
  0x7e6/0x7e4 when the two sides have different owners, else a province id. maptool resolves
  such pixels to `owner`. Exact meaning of bits 0, 1-4, 5-8: **NIEPEWNE**.

Proof that the ids are province ids: `maptool.py compare` against `id.tbl` (all 138 215 424
pixels): 83.93 % equal; 15.17 % are Terra Incognita (id.tbl writes 1615 where lightmap1 has 0);
0.12 % are pixels that id.tbl assigns to river pseudo-provinces (terrain 6, e.g. 1379 Columbia River, 1418 Seine River)
and lightmap1 to the land around them; 0.78 % are id.tbl's 0 border lines; **0 pixels of any
other disagreement**. `province.csv` City X/Y of all 967 land provinces fall inside their own
province in lightmap1 except one (Bermuda, a speck of an island). Colouring provinces by their
`province.csv` terrain gives the right deserts/forests/mountains (`terrain_*.png`).

### Shading

`shade` is a 6-bit light value (0..63; `colorscales.csv` uses the same 0..64 index). It is not
relief: it holds the bevel/glow along province borders, coasts, rivers and the **province
name lettering** (visible in `shade_1_crop.png`). Final colour in the game =
`table_0x66c5e0[ lut_0x80462b[shade - texture(x,y) - prov.field_0x5c] << 6 | prov.field_0x60 ]`
(16-bit pixels) - the province/terrain colouring and the texture term are **NIEPEWNE**
(not reproduced; maptool multiplies a fixed per-terrain RGB by the shade instead).

A leaf's value is the shade at its **top-left corner**; pixels are bilinear between the four
corners (`0x4777b0`), with a 33x33 corner buffer per tile:

1. `buf[32][32]` = last leaf value of the tile diagonally down-right (`0x477280`; the last leaf
   is always the top-left one).
2. Row 32 = top edge of the tile below (`0x477530`); column 32 = left edge of the tile to the
   right (`0x4772a0`). Edge leaves interpolate `v + j*((next-v)<<24 >> lg) >> 24`. Below the
   last tile row the buffer is filled with 6.
3. Walk the tile (same order). 1-pixel leaves: pixel = value. Leaf of size s at (x,y):
   TL=v, TR=buf[y][x+s], BL=buf[y+s][x], BR=buf[y+s][x+s] (16.16 fixed point); write the top
   row and left column into buf; row j: L=TL+j*(BL-TL)/s, R=TR+j*(BR-TR)/s,
   pixel x+i = (R + 0x8000 + (s-1-i)*((L-R)>>lg)) >> 16 (yes, pixel x+s-1 gets R).

Rendered result has no tile seams and aligns with id.tbl borders to the pixel
(`overlay.png`). Not compared against a screenshot of the running game: **NIEPEWNE** at the
level of single-pixel rounding. A second renderer `0x477e90` (chosen by `0x479800`, probably
another map mode) was not read: **NIEZNANE**.

## id.tbl - province id per pixel, row RLE (editor file, not read by eu2.exe)

```
0            u32 nruns                 (1 114 812)
4            u32 rowstart[7296]        index of the first run of row y
29188        { u16 x; u16 id; } run[nruns]
```

Row y = runs `rowstart[y] .. rowstart[y+1]-1` (last row ends at nruns); a run lasts until the
next run's x (or 18944). Size check: 4 + 4*7296 + 4*nruns == 4 488 436 exactly. Id 0 is a
one-pixel black border line between provinces, 1615 = Terra Incognita, river provinces are
drawn as their own ids.

## ADJACENT.TBL - province adjacency

```
0       u32 start[1616]            start[p] .. start[p+1]-1 = records of province p (start[1615] = 8878)
6464    { u32 province; u32 type; u32 unused; } rec[8878]
```

Size check exact (6464 + 12*8878 = 113 000). `unused` is always 0xCDCDCDCD (MSVC debug-heap
fill: the table was dumped from memory). `type`: 0 (8193 records) or 1 (685). Type 1 links
neighbouring land provinces across a river (Ile de France-Orleanais/Nivernais, Sacramento-
Monterey, Yukon-Sitka) - **NIEPEWNE**. Sea/river pseudo-provinces appear as ordinary
neighbours. `adj-defs.csv` adds extra links (sea straits) on top. Example:
`maptool.py adj 385` -> Ile de France: Champagne, Artois, Picardie, Caux (0), Nivernais,
Orleanais (1), Seine River (0).

## boundbox.tbl - province bounding boxes

`{ i32 x1, y1, x2, y2 }[1615]`, index = province id. Record 0 = (18944, 7296, 0, 0) (empty
box for Terra Incognita, doubles as the map size). Yukon (2666,0)-(3057,339) contains its city
(2742,131). Inclusive/exclusive x2/y2: **NIEPEWNE**.

## idgrid.tbl - provinces per 2048x2048 cell

`u32 [40][256]` (40 960 B, read as `fread(obj+6, 4, 0x2800)` at `0x476d1c`). Cell index =
`(x>>11)*4 + (y>>11)` (10 x 4 cells); each cell = ascending list of the province ids that
occur in it, zero-padded (accessor `0x476d70(x, y, k)`).

## incgrid.tbl - 8 bytes per lightmap1 tile (NIEZNANE)

1 084 544 B = 134 976 x 8, read as `fread(buf, 8, 0x20f40)` at `0x47ad60`, i.e. one 8-byte
record per lightmap1 tile. Common values: `00 00 00 00 ff ff ff ff` (open sea),
`02 00 00 00 0d ff ff ff`. Presumably Terra Incognita / fog-of-war coverage of the tile.
Not decoded.

## incognita.tbl (NIEZNANE, not referenced by eu2.exe)

`u32 offset[1377]` (first = 0xFFFFFFFF, then increasing from 0), followed by byte data with
long runs of 0x50 and 0x00 (0x50 is also the fill value at `0x47ae10`). Not decoded.

## Not checked

`lightmap0.tbl` is named in `eu2.exe` at `0x59c92f` (not shipped; probably a tool path).
`index.tbl` is opened at `0x475663` (not shipped; probably optional).

## Tool

```
maptool.py info | idtbl [S] | lmid L [S] | shade L [x0 y0 x1 y1] | terrain L [crop]
           overlay [crop] | compare | sprites | adj PROV | misc
```

Defaults read the game from `/home/angree/eu2x/app` (`--game DIR`), write PNGs to the
current directory (`--out DIR`). Full lightmap1 id decode takes ~30 s on 14 processes; the
PNGs are derived from Paradox data and must never be committed.
