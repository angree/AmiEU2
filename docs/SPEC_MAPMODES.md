# EU2 v1.09 map modes - province colouring (spec, clean room)

Source: `eu2.exe` read as a specification only. Addresses are the exe's. **NIEPEWNE** = guess,
**NIEZNANE** = not found. Raw listings used: `C:\temp\amiga_eu2\re\mm_*.txt`.

## 1. The setter `0x478950(country_id, mode)`

Globals:

| addr | meaning |
|---|---|
| `0x7fb8a8` | current map mode (0..10) |
| `0x7fb8b0` | focus country id the mode was drawn for |
| `0x7fb8ac` | overlay mask for the mode (what the map draws on top; table §4) |
| `0x80a528` | 1 only in mode 7 (changes the border test, §3) |
| `0x61f418` | selected province id (0 = none) |
| `0x8bfa50` | the local player's country object (`+6` = id) |

Steps:

1. If `countries[country_id]` (`0x64e140[]`) is empty and `0x8bfa50` is set, use the local
   player's id instead. Store mode, id; clear `0x80a528`; bump the redraw counter (`0x4764c0`).
2. **Every** province record (0..2015, stride 0x4A0 at `[0x8077d4]`) gets **shade = 15**
   (`0x482840`, field `+0x5C`, clamped 0..31).
3. Switch on mode (`jmp [mode*4+0x4794c0]`, 11 entries). Mode 9 and >10 log
   `"Illegal color code!"` and colour nothing.
4. In every mode the land/sea test is `terrain (+0xCA) == 5 (Ocean) or 6 (River)` → colorscale
   **23 `water`** (shade stays 15). Nearly every mode loops provinces 1..2015 (modes 1, 2, 6 also
   touch record 0).
5. Epilogue: pseudo-provinces 2016..2019 get colorscales 24 `border`, 25 `border outline`, 24, 25,
   visibility (§3) 1,1,0,0, shade 31; record 0 visibility 0. Then, **if a province is selected
   and mode ≠ 4**, its shade -= 16 (`0x482870`, clamped at 0) → the selected province draws
   darker (§2).

Province fields written: `+0x60` colorscale index (bits 0..4) and `+0x5C` shade (0..31) via
`0x4828d0(scale)` and `0x482840(shade)`. `0x4828d0` keeps bit 0x20 of `+0x60` = "not visible"
(`+0x420 == 0`, §3).

## 2. Colorscales and how shade turns into colour

`Map\colorscales.csv` is read in file order; the index used by the code = the block's position.
Country colours in `DB\country.csv` ("main color", "fantasia color") are mapped by name in
`0x4690e0` to the same indices (`LightOrange`→18, `Green`→1, ... `DarkOrange`→22; i.e. the
CamelCase names of the blocks, `Gray` spelled with *a*).

| # | csv name | knots `index:(r,g,b)` (linear interpolation 0..64) |
|---|---|---|
| 0 | Black | 0:(255,245,220) 25:(255,245,220) 64:(128,122,110) |
| 1 | Green | 0:(255,250,230) 20:(220,240,180) 50:(80,150,90) 64:(40,110,70) |
| 2 | Orange | 0:(255,255,230) 17:(246,236,216) 37:(213,200,171) 64:(40,40,25) |
| 3 | light blue | 0:(180,210,255) 12:(80,150,225) 40:(60,100,190) 64:(10,20,40) |
| 4 | light grey | 0:(235,235,235) 9:(210,210,210) 24:(175,175,175) 64:(80,80,80) |
| 5 | light brown | 0:(0,200,200) 20:(0,150,150) 50:(0,100,100) 64:(0,75,75) |
| 6 | dark yellow | 0:(240,195,125) 30:(210,130,30) 50:(150,100,20) 64:(90,60,10) |
| 7 | yellow | 0:(255,245,220) 17:(255,220,100) 37:(170,160,20) 64:(10,10,0) |
| 8 | dark grey | 0:(240,240,170) 29:(180,160,100) 50:(120,80,30) 64:(90,60,10) |
| 9 | light yellow | 3:(255,140,0) 30:(180,100,0) 55:(130,70,0) 64:(100,50,0) |
| 10 | grey | 0:(220,220,220) 10:(170,170,170) 35:(110,110,110) 64:(100,100,100) |
| 11 | dark green | 0:(140,210,130) 20:(100,180,80) 50:(60,80,50) 64:(10,60,30) |
| 12 | blue | 0:(50,220,255) 10:(10,170,245) 25:(0,85,200) 60:(0,10,40) |
| 13 | light red | 0:(255,170,170) 20:(255,128,64) 35:(200,80,40) 64:(80,20,10) |
| 14 | white | 0:(255,255,255) 25:(255,255,255) 35:(200,200,220) 64:(140,140,190) |
| 15 | dark brown | 0:(220,10,255) 10:(188,0,230) 24:(130,0,180) 64:(70,0,100) |
| 16 | red | 0:(250,220,200) 20:(250,60,30) 30:(200,30,15) 64:(80,15,7) |
| 17 | light green | 0:(250,250,220) 25:(190,220,190) 35:(140,180,155) 64:(60,60,60) |
| 18 | light orange | 0:(51,255,15) 6:(44,255,6) 10:(24,164,0) 64:(10,66,0) |
| 19 | brown | 0:(179,182,130) 10:(180,132,80) 24:(155,111,66) 64:(86,52,0) |
| 20 | dark blue | 0:(0,180,255) 0:(0,130,240) 43:(0,20,120) 64:(0,10,40) |
| 21 | dark red | 0:(255,0,64) 13:(220,0,64) 24:(180,0,64) 64:(76,0,64) |
| 22 | dark orange | 0:(255,243,200) 20:(255,220,140) 30:(229,172,105) 64:(105,60,25) |
| 23 | water | 0..4:(111,168,223) 35:(50,50,140) 64:(20,20,70) |
| 24 | border | constant (220,0,0) |
| 25 | border outline | constant (50,0,0) |

(The names lie: "light brown" is teal, "light orange" is bright green, "dark brown" is purple.
Only the index matters.)

Colour table `0x66c5e0` (built in `0x475b40`): 16-bit pixels, column = colorscale index (0..25),
column `k|0x20` = the same scale at **×0.5 brightness** (const `0x5e42c8` = 0.5). Index 0 of a
scale is the light end, 64 the dark end. Per pixel (MAP_FORMAT.md):
`colour = table[ lut(pixel_shade − texture − province.shade) ][ province.scale ]`.
So **a larger province shade = lighter**; shade 15 is the normal level, 0 the darkest, 31 the
lightest. All "shade" numbers below are this province value.

## 3. Borders and visibility (fog of war)

- **Country borders** exist only when mask bit **0x4000** is set (`0x476f05`). Border pixels of
  the lightmap (pseudo ids 0x7E4..0x7E7) compare the two provinces' owners (`+0x3C4`); if they
  differ the pixel becomes the `border` pseudo-province (red line, 0x7E6 if the province is
  visible, 0x7E4 if not); if equal, or bit 0x4000 is clear, the pixel takes the province's own
  colour (no line). In mode 7 (`0x80a528` = 1) the compared value is `(province+0x78 != 0)`
  instead of the owner (**NIEZNANE** what `+0x78` is). Province-to-province outlines inside one
  country come only from the lightmap shade bevel (always drawn).
  Modes with country borders: **0, 1, 5, 6, 7, 8**. Without: **2, 3, 4, 10**.
- **Visibility** `+0x420` (setter `0x4828a0`; recomputed by `0x531460`, called by the windows
  that change the mode): not visible → scale bit 0x20 → the province is drawn at half brightness.
  - option byte `0x8d0d74` = 1 (**NIEZNANE** which option/cheat): everything visible;
  - otherwise in every mode **except 1** everything is visible;
  - in **mode 1** (normal map): visible = provinces owned or controlled (`+0x3C8`) by the player,
    their land neighbours (sea neighbours only with naval tech ≥ 18, `0x432e30`, or province
    flag `+0x489`), provinces owned/controlled by countries the player has a tie with
    (`0x4cb900`: royal marriage / alliance / war ally / vassal), and provinces holding units
    of the player or of war allies (`+0x3D`) together with their neighbours. **NIEPEWNE** in
    detail (only the first half of `0x531460` was read).
- Terra incognita (provinces the player has not discovered, per-province byte
  `+0x256+country`) is **not** coloured by this setter. Terrain type 8 "Terra incognito" only
  gets colorscale 0 in mode 1. How undiscovered land is masked in the other modes is
  **NIEZNANE** here (renderer `0x479800` / `incgrid.tbl`).

## 4. Overlay mask `0x7fb8ac` per mode

| mode | mask | identified bits |
|---|---|---|
| 0 | 0x443FC | 0x4000 borders; 0x40000 core-province markers (core-set test `vfunc+0x24`, `0x47fccf`) NIEPEWNE; 0x3FC = symbols 0x4..0x200 |
| 1 | 0x243FF | 0x4000 borders; 0x3FF all standard symbols (0x2 = units list `+0x24A`, 0x10 = settlements) NIEPEWNE; 0x20000 NIEZNANE |
| 2, 3 | 0x800 | 0x800 = goods/resource icons (`0x47ff63`) NIEPEWNE |
| 4 | 0x2000 | 0x2000 = centre-of-trade markers (`0x47fbf8`, `0x48014a`) NIEPEWNE |
| 5, 6 | 0x5000 | 0x4000 borders; 0x1000 = diplomatic markers (vassal test `0x4cbe90` near `0x47fd25`) NIEPEWNE |
| 7 | 0x14000 | 0x4000 borders; 0x10000 = city/religion symbol (`0x47f70f`) NIEPEWNE |
| 8 | 0x4000 | borders only |
| 10 | 0xA0810 | 0x800 icons; 0x10 settlements; 0x80000 colonisation symbols (`0x47ffa5`) NIEPEWNE; 0x20000 NIEZNANE |

Units, cities etc. are outside this document; for a first implementation only bit 0x4000 matters.

## 5. Modes

In every row: sea/river → 23 `water`; shade 15 unless stated. "owner" = `+0x3C4` (NULL = unowned).
F = the focus country passed to the setter.

### Mode 0 - political (`0x478a7e`)
- land, unowned → **2 Orange** (beige; uncolonised land);
- owned → the owner's **"main color"** from `DB\country.csv` (static table `0x64f834 + id*0x144`),
  or **"fantasia color"** (`0x64f838`) when the scenario flag `0x8d0d7a` is set (set by the
  scenario key `fantasy`, `0x5b19a3`).
- Country borders on, everything visible, shade 15.

### Mode 1 - normal / terrain (`0x4789c5`) - **the start mode**
Every province by terrain (`Map\Terrain types.csv` order):

| terrain | colorscale |
|---|---|
| any land with the winter flag `+0x3E2` (`0x481910`; SPEC_WAR_DIPLOMACY "winter") and terrain < 5 | 14 white |
| 0 Plains | 2 Orange |
| 1 Forest | 1 Green |
| 2 Mountain | 22 dark orange |
| 3 Desert | 7 yellow |
| 4 Marsh | 17 light green |
| 5 Ocean, 6 River | 23 water |
| 7 Border | error `"Invalid terrain type!"` |
| 8 Terra incognito | 0 Black |

No country colours at all. **Country borders are drawn** (bit 0x4000) and the **fog of war**
(§3) halves the brightness of what the player cannot see. This is the "normal map".

### Mode 2 - trade-goods map (`0x478af6`)
g = goods (`+0x54`, `0x481000`) of the **selected province** `0x61f418`. Land with goods == g
→ **17 light green**, other land → **2 Orange**. No borders. Selecting another province redraws
(`0x4763c6`). Set by the resource window (`0x52bc49`, mode 2 when its click test hits, else 3;
SPEC_UI_WINDOWS4 §"resource window").

### Mode 3 - economic map, button "e" (`0x478b73`)
All land **17 light green**, shade `s = (50 − min(v,50))·31 / 50` (integer, toward zero;
clamped 0..31), v = `int(0x47d970(province))`: province base tax `+0xB6` (≥0), 0 for a colony,
+1 for a city with flag `+0x6A`, +1 if `0x47cb70`, × factor `0x47e0b0`, ×0.5 if `+0x489`
(**NIEPEWNE** naming: "tax value"). v ≥ 50 → darkest; v = 0 → lightest. Unowned land is coloured
the same way. No borders.

### Mode 4 - trade map, BUTTON_MERCHANT (`0x478cd7`)
cot = centre-of-trade object of the province (`+0xAA`); none → **2 Orange**; else
`c = cot[+0x1C] % 22 + 1`, and `c == 2` becomes **16** (so Orange stays "no centre").
`cot+0x1C` = an id of the centre (**NIEPEWNE**: its province id). One colour per trade zone,
from scales 1, 3..22 (0 and 2 never used). No borders; the selected province is **not**
darkened in this mode. Set by `0x5154c2` / `0x4fd04f` (merchant panel).

### Mode 5 - diplomatic, ties / alliances view (`0x479134`)
Base: land **2 Orange, shade 0** (also unowned). For an owned province, owner O, focus F,
`R(A,B)` = record B in A's diplomacy object (`A+0x38E`, SPEC_WAR_DIPLOMACY §1.2). First match:

| # | test | colorscale | shade |
|---|---|---|---|
| 1 | O == F | 1 Green | 0 |
| 2 | `R(F,O)+0x36` (at war) | 16 red | 0 |
| 3 | `R(F,O)+0x3D` (same side of a running war; set from the war list `0x8ff614` in `0x4ce1d1`) | 1 Green | 6 |
| 4 | F is a vassal and O is its overlord (`0x4cc1b0`), or O is F's vassal (`0x4cbe90`) | 1 Green | 12 |
| 5 | O has a CB on F (`R(O,F)+0x3F` permanent, or its temporary-CB list `+0x10` not empty) | 16 red | 12 |
| 6 | `R(F,O)+0x3C` (alliance) | 1 Green | 18 |
| 7 | F has a CB on O (same two tests on `R(F,O)`) | 16 red | 18 |
| 8 | `R(F,O)+0x3B` (royal marriage) | 1 Green | 24 |
| 9 | otherwise | 2 Orange | 0 |

Darker green = closer tie, darker red = hotter conflict. Borders on.

### Mode 6 - diplomatic, relations view (`0x478c0c`)
Owned land by `v = int(relation F→O)` (`0x4cdf70` on `R(F,O)`, float −200..200):

| v | colorscale |
|---|---|
| > 149 | 11 dark green |
| 76..149 | 17 light green |
| −74..75 | 2 Orange |
| −149..−75 | 16 red |
| ≤ −150 | 21 dark red |

Unowned land → **0 Black** (cream→grey). There is no "own country" case: F's own provinces use
F's relation record for itself (**NIEPEWNE**, most likely 0 → Orange). Borders on.

Which radio is which: the diplomacy window computes `mode = 5 + (radio_object+0x10 != 0)`
(`0x50aae6`, `0x50c160`, `0x50c900`). SPEC_UI says "View Alliances / View Relations → 6/5";
by content mode 6 is the relation-value map and mode 5 the ties map, so **mode 5 = View
Alliances, mode 6 = View Relations** (the opposite of the task brief; NIEPEWNE until the radio's
`+0x10` value is traced). Diplomacy actions refresh the map when mode == 5 (`0x44a63f`,
`0x44c952`, ...).

### Mode 7 - religion map, BUTTON_MISSIONARY (`0x478e10`)
Colour = **state religion of the owner** (`owner+0x254`), **not** the province's religion:

| religion | colorscale |
|---|---|
| 0 Protestant | 3 light blue |
| 1 Reformed | 12 blue |
| 2 Counter-Reform | 14 white |
| 3 Catholic | 8 dark grey (tan) |
| 4 Orthodox | 19 brown |
| 5 Sunni | 1 Green |
| 6 Shiite | 17 light green |
| 7 Pagan, unowned land | 2 Orange |
| 8 Confucian | 16 red |
| 9 Buddhist | 7 yellow |
| 10 Hindu | 22 dark orange |

Owned provinces: shade = `16 − min(2·RR, 16)` (RR = `0x481a30`, revolt risk; clamped 0..31):
the higher the revolt risk the darker. Unowned keep 15. Borders on, with the `+0x78` test (§3).
Set by `0x4fdb64` (missionary panel) and by the window of SPEC_UI_WINDOWS4 §234.

### Mode 8 - scenario-mission map (`0x478efe`, MISS_INFO window `0x5714a0`)
All land **2 Orange**. Then by F's scenario mission type `F+0x1016` (jump `0x47953c`):

| type | highlighted (scale **1 Green**) |
|---|---|
| 0, 1, 2, 8 | provinces of the list `F+0x1008`; shade = 5 the first time (15 → 5), halved on a repeat |
| 3 | every province of each country in the list `F+0x1008` (their province list `+0x131E`) |
| 4 | provinces of the global list `0x647d40` (count `+0x114`, item `+4` = province; **NIEPEWNE**: centres of trade) |
| 5 | provinces where F `vfunc+0x50` (`0x45c110`, "colonist can go") is true |
| 6 | provinces where F `vfunc+0x54` (`0x45c2e0`, "trading post possible") is true |
| 7 | if F's religion is 0..4: provinces whose owner's religion is 0 or 1; else whose owner's religion is 6 |

Text keys next to it: DEFEAT, MONOPOLY, COLOEMPIRE, TRADEEMPIRE, CATHOLIC (`0x57132f..0x571453`);
the type ↔ key mapping is **NIEPEWNE**. Borders on.

### Mode 9 - unused ("Illegal color code!").

### Mode 10 - colonisation map, BUTTON_COLONIST (`0x478d41`)
Land **2 Orange**. Province i where F `vfunc+0x50(i)` or `vfunc+0x54(i)` is true (a colonist or
a trader can be sent: land, known to F, ... SPEC_ENVOYS) → **17 light green** with shade:
31 (lightest) when it has no settlement (`0x47f6c0` = 0), 15 when it has a trading post
(`+0x40C`), 0 (darkest) when it has a colony/city (`+0x408`). No borders. Set by `0x4fc724`.

## 6. Who sets which mode

| trigger | mode | where |
|---|---|---|
| main game screen created | **1** | `0x524de3` |
| BUTTON_MAPMODE_NORMAL "n" (button id 63) | 1 | `0x52e1ed` |
| BUTTON_MAPMODE_POLITICAL "p" (id 62) | 0 | `0x52e184` |
| BUTTON_MAPMODE_RESOURCE "e" (id 61) | 3 | `0x52e0eb` |
| resource window | 2 / 3 | `0x52bc49`, `0x52bc5f` |
| BUTTON_MERCHANT | 4 | `0x5154c2`, `0x4fd04f` |
| BUTTON_DIPLOMAT / diplomacy radio | 5 / 6 | `0x50ab08`, `0x50c184`, `0x50c92b` |
| BUTTON_MISSIONARY | 7 | `0x4fdb64` |
| MISS_INFO (scenario mission) | 8 | `0x5714a0` |
| BUTTON_COLONIST | 10 | `0x4fc724` |
| redraw after state changes | current | `0x44786d`, `0x4499b3`, `0x475971`, `0x4763c6` (province selected), `0x4764a5` (deselect), ... |
| restore saved (mode, mask, country) | saved | `0x530f2f` (closing a window restores the previous map) |
| command with a number string | atoi(arg) | `0x4a3d09` (**NIEPEWNE**: debug/console) |

**Diplomat with nothing selected:** the map shows mode 5 or 6 (per radio) **for the player's own
country** (`+0x82`); with a target selected (`+0xDA`) it is drawn for the target.

## 7. Differences from `src/mapmode.c`

- Our "political" = mode 0, but we start in political; the exe starts in **mode 1** (terrain
  colours + borders + fog of war, no country colours).
- Normal: our 5 tints are invented; use the colorscale table above (Plains Orange, Forest Green,
  Mountain dark orange, Desert yellow, Marsh light green, winter white), add borders and fog.
- Economic: not quartile bands; one light-green scale with shade from the tax value (§5 mode 3).
- Religion: colour by **owner's state religion** with RR shading; our palette differs.
- Diplomacy: two modes (ties 5, relations 6) with the tables above; ours mixes them.
- Trade: colour = `cot id % 22 + 1` (2→16), unowned/no centre Orange.
- Colonial: candidates (vfunc 0x50/0x54) light green shaded by settlement; not "free/ours".
- Our fog uses the black scale for unknown provinces; the exe's fog is half brightness (×0.5)
  and only in mode 1.
