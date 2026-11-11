# EU2 v1.09 — frontend CREDITS screen (spec)

Source: `eu2.exe` v1.09 disassembly (objdump + annot.py), data files `DB/credits.txt`,
`GFX/Interface/frontend/Credits/*.bmp`. Layout facts only; no code copied.
NIEPEWNE = guess, NIEZNANE = not found.

## 0. Class

| Item | Address | Notes |
|---|---|---|
| Created by | frontend screen factory, case at `0x58f3cc` | `new(0xDE)`, ctor gets `GFX\interface\frontend\bg_start.bmp` (passed to the frontend base ctor; the credits draw overpaints it) |
| Constructor | `0x57dbe0` | base frontend ctor `0x57ced0` first |
| vtables | `0x5f6d9c` (primary), `0x5f6d98` (secondary) | |
| Event handler | `0x57df80` (vtbl +0x04) | base handler `0x57d370` first |
| Draw | `0x57e060` (vtbl +0x0C) | |
| Destructor | `0x57ded0` (deleting wrapper `0x57deb0`) | |
| Text list (word-wrapping box) | ctor `0x41f5b0(620, 460, 0, 0)`; add text `0x420810` → `0x420830`; add row `0x420a80` | |
| Row (single text line) | ctor `0x41f020(font, text, width, colour, centred)`, vtable `0x5e5168`, draw `0x41f230` | |

Object fields (credits screen): `+0x16` text list, `+0x1A` default font (garamond14),
`+0x1E..+0xBD` 40 visible-row slots S[0..39], `+0xBE` blank row, `+0xC2` pixels left before the
next shift, `+0xC6` index of the next list row, `+0xCA`/`+0xCE` bar.bmp (two instances),
`+0xD2` back.bmp, `+0xD6` = 0, `+0xDA` = 0x40 (both unused in the code read; NIEZNANE).

## 1. Video mode and bitmaps

**The credits screen runs at 640x480, not 800x600.** The ctor switches the display
(`0x4c9a50(640, 480, 16, 0)`, 16 bpp); leaving the screen switches back to `800, 600, 16`.
(Reading `0x4c9a50` as "set video mode" is NIEPEWNE but the symmetric 640x480 / 800x600 pair
makes it near certain.) The AGA 640x480 target can therefore show this screen 1:1.

Per-frame draw order (`0x57e060`):

| # | What | Position | Size |
|---|---|---|---|
| 1 | clear back buffer (`0x4055b0(0)`, NIEPEWNE: fill colour 0 = black) | — | 640x480 |
| 2 | `back.bmp` | (0, 20) | 640x440 |
| 3 | scrolling text rows (§3) | x = 0, centred in 620 px → centre x = **310** | — |
| 4 | `bar.bmp` (instance +0xCA) | (0, 0) | 640x20 |
| 5 | `bar.bmp` (instance +0xCE) | (0, 460) | 640x20 |

No background scrolling: back.bmp is static. The bars are drawn after the text, so they mask
rows entering at the bottom and leaving at the top; the visible text window is y = 20..459.
`bink_logo.bmp` (78x80) lies in the same folder but is not referenced anywhere in the exe.

## 2. Parsing `db\credits.txt`

Opened with `fopen("db\credits.txt","rb")`, read with `fgets(buf, 255)` until EOF or the end
marker. Per line:

| Line starts with | Effect |
|---|---|
| `-END` | stop reading (rest of file ignored) |
| any other `-` | line skipped (comment) |
| `#d` (d = ASCII digit) | select font d for this line (table below); the text is the rest after the 2 chars. If that rest starts with `-` the line is skipped but **the font stays selected for the next line** (font is reset only after a line is added) |
| anything else | text in the default font garamond14 |

After each added line the font is reset to garamond14. `#9` or a non-digit reads outside the
table (undefined; only 0..8 are valid).

Font table (`0x6384a4 + 4·ASCII`, i.e. entries from `0x638584`):

| #d | Font | Row height (px, = font+0x10; NIEPEWNE: equals the strip width) |
|---|---|---|
| 0 | arial8 | 8 |
| 1 | arial10 | 10 |
| 2 | garamond14 | 14 |
| 3 | garamond14bold | 14 |
| 4 | garamond14 | 14 |
| 5 | garamond14bold | 14 |
| 6 | garamond18bold | 18 |
| 7 | garamond20bold | 20 |
| 8 | garamond24bold | 24 |

Font strips are N x (223·N) 8-bit bitmaps (garamond14 = 14x3122, 18bold 18x4014, 20bold
20x4460, 24bold 24x5352), so the cell is NxN. The shipped file uses #8 (1 line), #6 (27 headings),
#4 (names), and plain lines (more names, garamond14).

Text is fed to the list's add-text routine, which **word-wraps** at 620 px: spaces separate
words, CR or LF ends a line, a NUL flushes. A row is created only for a non-empty line, so
**lines that hold only CR/LF produce no row** (the many blank lines in credits.txt add no gap);
a line holding a single space (` \r\n`, five in the file) produces a blank row of the current font
height. (Code-derived; visual confirmation on the real game NIEPEWNE.) A word that does not fit
starts a new row; a single over-wide row would be cut and end in `...` (row ctor).

Every row gets colour = the list's colour **#FFBD00 (gold)** and the centred flag = 1.
Colour escapes are interpreted at draw time by the generic glyph renderer `0x40bb45`
(applies to all game text, not just credits):

| Sequence | Result |
|---|---|
| `0xA7` `Y` | colour #FFBD00 (gold; the same as the default) |
| `0xA7` `W` | colour #FFFFFF |
| `0xA7` d1 d2 d3 | colour R = d3·32, G = d2·32, B = d1·32 (digits; first digit is **blue**) |
| `%` d1 d2 d3 | same three-digit colour code |
| CR / LF (CRLF counted once) | new line: x back to start, y += font height |
| `\` `n` | new line; `\` + other char draws that char |
| space | advance by the space glyph width (or the font default) |

The colour lasts to the end of the string. Headings in the file are `#6§W...` (white 18 bold),
names `#4§Y...` (gold 14); two headings (`#6ORIGINAL MUSIC`, `#6MANUAL`) have no escape and are
therefore gold. Character set: single-byte, Latin-1 as found in the file (0xE4 ä, 0xE5 å, 0xE9 é,
0xF6 ö, 0xF8 ø); glyph lookup is by byte value in a 256-entry table, missing glyphs are skipped.
The shipped file yields 240 rows, 3478 px in total (212×14, 27×18, 1×24).

Glyph clipping is per glyph against the target surface (640x480): a glyph is skipped entirely if
it would cross the left/right edge, start above y = 0, or extend below y = 480.

## 3. Scrolling

State: 40 row slots S[0..39], initially all = the blank row (text `" "`, garamond14, height 14,
colour 0, left-aligned — invisible). `left` (+0xC2) = 0, `next` (+0xC6) = 0.

Update, on every **TIMER_TICK_LOOP (event 0x18, once per main-loop pass = per frame)**:
1. if `left` == 0: shift S[0..38] ← S[1..39]; S[39] ← list row `next`, or, if `next` is past the
   end, the blank row and `next` = −1; `left` = height(S[39]); `next` += 1.
2. `left` −= 1.

So the text moves **up 1 pixel per frame**; the real-time speed depends on the main-loop rate
(no frame limiter found in this pass; NIEZNANE — a port should pick a fixed rate, e.g. 1 px per
1/50 s, NIEPEWNE).

Draw: y = 480 + `left`; for i = 39 down to 1 (S[0] is never drawn): unless the row text starts with
`!` (then the row is a spacer: height counts, nothing drawn), draw S[i] with its top at (0, y),
text centred on x = 310; then y −= height(S[i]).

Consequences for the port:
- Start: the screen begins empty; line 0 enters from the bottom edge (under the lower bar), i.e.
  it first shows above the bar after ~20 + its height frames.
- S[39] is always at y ≥ 480 (invisible); the visible stack ends with S[38] at the bottom edge.
- End: after the last row one blank garamond14 row (14 px) follows, then row 0 again — the
  credits **loop forever**; they never return to the menu on their own.

## 4. Input and exit

`0x57df80`: the base frontend handler `0x57d370` runs first — while its counter `+0x12` is non-zero
it decrements it and swallows the event (NIEPEWNE: a short input delay after opening; where the
counter is set NIEZNANE). Then:
- **MOUSELBUP (0x0E)** or **KEYUP (0x15, any key)** → switch the display back to 800x600x16,
  clear the screen-active byte `+0x10`, return 1 (event consumed) → back to the main menu.
- TIMER_TICK_LOOP (0x18) → the scroll update above.
- Right button, mouse move, etc. → ignored.

The destructor frees the list, back.bmp and both bars, then calls `0x4ca630` on the display
(NIEZNANE, likely mode/palette restore), `0x566b00` on `0x8c140c` (NIEZNANE) and `0x57d170`, which
reloads the frontend button/box shadow bitmaps (they are mode dependent).

## 5. Sound

No sound or music call in the ctor, update, draw or exit path. The screen plays nothing of its
own; whatever frontend music is running continues (NIEPEWNE: not traced in the music player).
