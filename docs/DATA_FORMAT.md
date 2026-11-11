# Data set written by eu2conv (conversion 4)

`eu2conv` turns the player's GOG copy (v1.09) into a folder the Amiga port reads as
`PROGDIR:data/`. All multi-byte numbers are **big-endian** (68k reads them directly).

```
data/conversion.txt     "EU2 Amiga data conversion 2" + counts; the game refuses version < 2 or errors != 0
data/text.pak           every script/table file, byte-identical to the original (txt csv inc eug ai spr act pal ...)
data/map.pak            Map/*.tbl, byte-identical (little-endian formats, parsed by the port)
data/gfx.pak            every .bmp, converted to EUI1
data/sfx.pak            every .wav, converted to EUS1
data/intro.pak          still frames of the Bink intro videos (PAL8 EUI1) + intro/index.txt (conversion 4)
data/music/mNN.pcm      EUM1: "EUM1", rate BE32 (28000), samples BE32, then signed 8-bit mono PCM
                        (raw on purpose: streaming is a plain Read() into the Paula buffer, no decoding;
                        windowed-sinc resampled, peak normalised, TPDF dither; 53 tracks, 9522 s, 267 MB)
data/music/index.txt    "mNN.pcm <original base name>" - the playlists in music/*.txt use base names
```

Entry names are the original relative paths, **lower case, `/` separators**, e.g.
`gfx/map/units/bmp/t-army a-siegetower.bmp`, `db/province.csv`, `map/id.tbl`.
Packs avoid the 30-character FFS file-name limit (some originals have 37) and keep the
number of disk opens small.

## EU2P pack

```
0   "EU2P"
4   u32 version (1)
8   u32 entry count
12  u32 directory offset
16  u32 directory size
20  entry data, each entry starting on a 4-byte boundary
dir: per entry  u32 offset, u32 size, u8 name length, name bytes, pad to even
```
The directory is sorted by name (byte order), so the reader can binary-search it.

## EUI1 image

```
0   "EUI1"
4   u16 width, u16 height
8   u8  format: 1 = PAL8 (own palette), 2 = RGB565
9   u8  flags: bit 0 = PackBits-compressed pixel data
10  u16 ncolors (PAL8 only, else 0)
12  ncolors * (R, G, B)
..  u32 data length
..  pixels, rows top-down; PAL8 = 1 byte/pixel, RGB565 = big-endian 16-bit word/pixel
```
PackBits unit is 1 byte for PAL8 and one 16-bit word for RGB565: control byte
`n < 128` copies `n+1` units, `n >= 128` repeats the next unit `257-n` times.
24-bit originals become RGB565 (exact for the colour key 255,0,255 -> 0xF81F); the port
maps them to the screen palette or a 16-bit RTG screen at load time.

## EUS1 sound

```
0 "EUS1"   4 u32 rate (<= 22050)   8 u32 length (even)   12 signed 8-bit mono samples
```
Originals are 8000-44100 Hz, 8/16-bit, a few stereo; mixed to mono, rates above 22050
reduced to 22050 with a box filter.

## intro.pak (conversion 4)

The Amiga shows still frames instead of the Bink videos (video playback is too heavy for a 68k).
`tools/eu2conv/bink.c` decodes `avi/*.bik` (Bink 1, `BIKi`; video only) with the format's constant
tables read at conversion time out of the user's own `avi/binkplay.exe` (GOG v1.09, checked by
SHA-1; UPX-unpacked in memory), so no RAD or Paradox data is in the repository.

```
intro/index.txt          one still per line, in showing order: "<entry> <milliseconds>";
                         lines starting with '#' are comments
intro/paradox.eui        paradox.bik frame 112 (640x250), the logo fully lit          3000 ms
intro/intro.eui          intro.bik frame 123 (640x480), the publisher's logo         3000 ms
intro/eu2_intro_01..11.eui  eu2_intro.bik (640x256), one still per caption card:
                         frames 168 456 648 816 1032 1248 1464 1608 1800 2040,
                         and 2280 = the title                       10000 / 6000 ... / 5000 / 4000 ms
intro/dedication.eui     dedication.bik frame 96 (640x480)                           4000 ms
```
Every still is EUI1 format 1 (PAL8) with **its own 256-colour palette**: median cut over the
frame's exact colours to 253 colours, pens 17-19 kept for the AGA pointer (17 black, 18-19
white, as in `amieu2_frontend.pal`), serpentine Floyd-Steinberg dithering, PackBits when it
saves more than 10 % (the dithered stained-glass cards usually stay raw). Native size, about
1.7 MB for the pack. The original installation plays dedication, eu2_intro, paradox
(`avi/eu2_intro.bat`); intro.bik (the publisher's logo) is not in that list.

## Not converted yet
- 640x480 UI set (Lanczos) - planned for conversion 2.
- executables, DLLs, the manual, SourceSafe `vssver.scc` files - never.
