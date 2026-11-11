# SPEC_UI — Europa Universalis II v1.09 user interface layout (800x600)

Clean-room specification of the original interface, written from `eu2.exe` (Ghidra export
`C:\temp\amiga_eu2\ghidra\eu2_all.c` + raw disassembly), the game's bitmaps, `Config\buttons.txt`,
`Config\text.csv`, the PIW layout files and `EU2_manual.pdf`. No decompiled code is reproduced here;
function addresses are given only so every statement can be re-checked.

Markers: **NIEPEWNE** = inferred, plausible but not proven; **NIEZNANE** = not found yet.
All coordinates are screen pixels at 800x600 unless marked *window-relative*.

Mock-ups composed from the real bitmaps at the coordinates below (never commit them):
`C:\temp\amiga_eu2\agent_ui\mock_main.png`, `C:\temp\amiga_eu2\agent_ui\mock_piw.png`
(generator: `C:\temp\amiga_eu2\agent_ui\mock.py`). They match the manual screenshots (manual p.15, p.7).

## Contents

1. [Rendering model](#1-rendering-model)
2. [Main game screen](#2-main-game-screen)
3. [Province window (PIW)](#3-province-window-piw)
4. [Sidebar info windows: budget, domestic policy, ledger, others](#4-sidebar-info-windows)
5. [Diplomacy](#5-diplomacy)
6. [Army / fleet window](#6-army--fleet-window)
7. [Event / message window](#7-event--message-window)
8. [Frontend (main menu, scenario selection)](#8-frontend)
9. [In-game options, save/load, message settings](#9-in-game-options-saveload-message-settings)
10. [Cursors](#10-cursors)
11. [Other resolutions (1024x768, 1280x1024)](#11-other-resolutions)
12. [Bitmaps that v1.09 never loads](#12-bitmaps-never-loaded)
13. [Key addresses](#13-key-addresses)
14. [Open questions](#14-open-questions)

---

## 1. Rendering model

**Colour key.** Every RGB interface bitmap uses pure green `#00FF00` as transparent. Palettised
(`P`) bitmaps (PIW buildings, crest, trees, resource strip, unit strips) use palette index 0
(= green) as transparent and **index 1 (= red `#FF0000`) as a shadow pixel**: the pixel underneath
is darkened instead of overwritten (the mock-up halves the RGB; exact darkening NIEPEWNE). These P
bitmaps are drawn through a remap palette loaded from `gfx\palette\<name>` (e.g. `piw.bmp`,
`crest_palette.bmp`, `resource.bmp`, `anim_puls_palette.bmp`, `piwbg_palette.bmp`; per culture
`piw_china_palette.bmp`, `piw_indianpalette.bmp`, `african_palette.bmp`, `hindu_palette.bmp`,
`PIW_aztekpalette.bmp`) — palette loader `0x40d490`, apply `0x40eed0`.

**Button strips.** Every clickable bitmap in `button area\` and most others is a horizontal strip
of **4 equal frames: 0 normal, 1 mouse-over, 2 pressed/selected, 3 disabled (grey)**
(confirmed visually for all `button area\` files). Frame width = bitmap width / 4.
Building buttons in the PIW use their own frame counts (see §3).

**Draw layers.** The screen is a list of "layers": an object registers itself with a priority and
a small id (`0x40ad40`, list kept sorted by ascending priority; lower priority is drawn first —
NIEPEWNE but consistent with what overlaps what). The layer's draw method (vtable slot +0x0C)
receives the id and draws only that part. Registered layers:

| Object | id | priority | Draws |
|---|---|---|---|
| Sidebar frame (`0x5665e0`) | 0 | 2000 | `sidebar_header` at (0,0), `sidebar_bottom` at (0,447), message log; "WAITING_FOR_PLAYERS" (MP) |
| Sidebar frame | 4 | 2200 | sub-object (buttons area) |
| Sidebar frame | 2 | 2500 | popup/search panel |
| Main interface (`0x524840`, global `0x8bfa50`) | 3 | 400 | army/navy selection animation on the map |
| Main interface | 0 | 1250 | map-level overlays (NIEPEWNE) |
| Main interface | 2 | 2202 | sidebar contents: shield mask, VP number, title lines, banner, current info window |
| Main interface | 4 | 2400 | ledger (EYR) layer (NIEPEWNE) |
| Main interface | 5 | 9000 | top status bar (values, icons, pause) and tooltips |

The main-interface layer callback is `0x526340`; the sidebar-frame callback is `0x567280`.

**Dirty regions.** NIEZNANE in detail. What is visible: top-bar values are re-drawn every time
layer 5 is painted; the whole sidebar is painted by layers 0/2. There is no evidence of per-widget
dirty rectangles in the code read so far — the engine looks like it repaints all layers into a
back buffer every frame and flips. For the Amiga: treat sidebar and top bar as cached surfaces and
re-blit only on change (our choice, not the original's).

**Text.** Fonts are bitmap strips in `gfx\fonts\` (`arial8/10`, `garamond12/12bold/14/14bold/
18bold/20bold/24bold`). Text calls: left-aligned `0x40ccaf`/`0x40cc29`, centred `0x40cd35`,
right-aligned `0x40cdd2`, width `0x40c36e`. Colours used: white `#FFFFFF`, black, grey `#808080`,
"gold" `#FFBD00` (titles), "orange" `#FFC000` (PIW numbers). Strings come from `text.csv`
(`KEY;English;...;id`) via `0x41554b`.

**Popup windows** (generic framed window, `0x428740` → `0x428810`): size rounded down to the tile
size (8 or 16), minimum 3 tiles; background built from `win8_*` (8x8, black-gold) or `win_*`
(16x16) tiles: corners `tl tr bl br`, edges `t b cl cr`, centre `c` tiled. Default position
cascades (+8,+8 per new window, wraps to (200,100) past x=400, bottom kept ≤ 500); then clamped
to **x ≥ 224, y ≥ 32** and inside the screen — i.e. popups never cover the sidebar or the top bar.

## 2. Main game screen

```
 x: 0          224 232                                                  792 800
 y:0 +-----------+ +---------------- top bar 560x24 (win8 frame) ------+   |
     | sidebar   |                                                         |
     | header    |                                                         |
 103 +-----------+            map viewport                                 |
     | info      |            x 224..799, y 0..599                         |
     | window    |            (top bar floats over the map at y 4..27)     |
     | 224x344   |                                                         |
 447 +-----------+                                                         |
     | sidebar   |                                                         |
     | bottom    |                                                         |
 600 +-----------+---------------------------------------------------------+
```

The sidebar is always on the **left**, 224 px wide (manual: "information window on the left side").
The map fills x 224..799. Popups are clamped to x≥224, y≥32 (so the usable map area for windows is
below the bar). Whether the map is drawn under the top bar (bar floating) — yes per the manual
screenshot (map visible at y 0..3 and right of x 792).

### 2.1 Sidebar bitmaps

| Bitmap | Size | Position | Notes |
|---|---|---|---|
| `sidebar_header.bmp` (800x600) / `sidebar_header2.bmp` (other res.) | 224x103 | (0,0) | green hole where the country shield shows through; `0x566b00` picks the file by screen width |
| `sidebar_mask.bmp` | 64x60 | (80,0) | drawn over the shield to cut its outline (layer 2) |
| player shield | — | inside (80..144, 0..60) | button `BUTTON_VP` sits at (84,3) with an empty bitmap; the shield image itself comes from `gfx\map\shields\shield_<TAG>.bmp` (NIEPEWNE where exactly it is blitted) |
| `iw_*.bmp` (current info window) | 224x344 | (0,103) | see §3/§4; `iw_none.bmp` when nothing is selected, with "PLEASESELECT" centred |
| `sidebar_banner1.bmp` | 224x21 | (0,103) | scroll tabs drawn over the top edge of every info window |
| `sidebar_banner2.bmp` | 224x30 | (0,417) | drawn by the PIW over the bottom edge of the info window |
| `sidebar_bottom.bmp` / `sidebar_bottom2.bmp` | 224x153 | (0,447) | constant y=447 (`0x5673a1` pushes 0x1BF) |
| `sidebar_bottom_fill.bmp` | 224x13 | below 600 at higher resolutions (NIEPEWNE) | |
| minimap `gfx\map\minimapfull.bmp` / `minimapempty.bmp` | 160x72 | **(32,504)** | `0x47b5f0`; empty = unexplored |

Sidebar header texts (layer 2, `0x526340`):

| Text | Font | Colour | Position |
|---|---|---|---|
| Victory points / score (integer; in one victory mode "%d%%") | garamond14 | white | centred x=112, y=52 |
| Selected object, line 1 (e.g. province / fleet name) | garamond14bold | black | centred x=112, y=70 |
| Selected object, line 2 (e.g. owner / location) | garamond14bold | black | centred x=112, y=84 |
| "AUTOSAVE" / "QUICKSAVE" / "QUICKLOAD" / "NORMALSAVE" flash | garamond14bold | white | centred x = 224+(W−224)/2, y=56 (y=40 while paused) |

### 2.2 Sidebar buttons (`0x52de10`, absolute screen coordinates, top-left of frame)

Button ids/index: table at `0x62a618` (name → index), definitions in `Config\buttons.txt`
(`background` = strip file in `gfx\interface\button area\`, `upper` = instant tooltip key,
`lower` = delayed tooltip key, `shortcut`).

| Button id | Bitmap (frame size) | Pos | Action | Tooltip keys | Key |
|---|---|---|---|---|---|
| BUTTON_VP | empty (shield) | (84,3) | country & monarch window | BUTTON_0027 | F2 |
| BUTTON_VIEW_LAND | btn_army (32x40) | (38,9) | land military window | BUTTON_0032/0033 | |
| BUTTON_VIEW_NAVAL | btn_navy (28x32) | (6,28) | naval military window | BUTTON_0034/0035 | |
| BUTTON_SET_BUDGET | btn_treasury (32x40) | (156,9) | budgeting (sliders) | BUTTON_0028/0029 | |
| BUTTON_VIEW_FINANCIAL | btn_budget (32x34) | (190,27) | financial summary | BUTTON_0030/0031 | |
| BUTTON_MISSIONARY | btn_Missionary (36x34) | (28,442) | religion / missionary | BUTTON_0998 | |
| BUTTON_DIPLOMAT | btn_Diplomat (38x35) | (70,444) | diplomacy | BUTTON_0062 | |
| BUTTON_MERCHANT | btn_Merchant (32x34) | (118,445) | trade | BUTTON_0063 | |
| BUTTON_COLONIST | btn_Colonist (38x36) | (162,441) | colonisation | BUTTON_0064 | |
| BUTTON_MAPMODE_NORMAL | btn_mapmode_normal (26x27) | (58,480) | normal map | BUTTON_0040 | n |
| BUTTON_MAPMODE_POLITICAL | btn_mapmode_political (33x25) | (94,480) | political map | BUTTON_0039 | p |
| BUTTON_MAPMODE_RESOURCE | btn_mapmode_resource (28x27) | (138,480) | economic map | BUTTON_0038 | e |
| BUTTON_MISSION | btn_missions (46x88) | (0,487) | missions list | BUTTON_0036/0037 | F2 |
| BUTTON_VIEW_STATISTICS | btn_eyr (38x88) | (186,486) | ledger (EYR) | BUTTON_0042 | F6 |
| BUTTON_GAME_SETTING | btn_game_setting (52x29, "MENU") | (88,571) | in-game menu | BUTTON_0041 | F10 |
| BUTTON_ZOOM_IN | btn_zoom_in (36x21) | (20,579) | zoom in | BUTTON_0043 | + |
| BUTTON_ZOOM_OUT | btn_zoom_out (36x21) | (168,579) | zoom out | BUTTON_0044 | − |
| BUTTON_QUICK_SAVE / LOAD | — | (10000,10000) = hidden, keys only | | | F3 / F4 |

Manual mentions religious and diplomatic map modes reached through the missionary / diplomat
icons — those are the bottom-tab buttons above.

### 2.3 Top status bar (layer 5, `0x526340` branch id 5)

All x are `x0 + n`, where **x0 = (screen_width − 800)/2** (0 at 800x600). Frame: a `win8` popup
window **560x24 at (x0+232, 4)** (`0x524840` creates it with width 0x230; position set in `0x526340`).
Values: font **garamond20bold**, white, y=6; icons drawn first.

| Item | Bitmap | Icon pos | Value pos | Value |
|---|---|---|---|---|
| Merchants | `button area\btn_bar_merchant.bmp` 16x20 | (242,5) | 262 | integer |
| Colonists | `button area\btn_bar_colonist.bmp` 32x19 | (284,5) | 316 | integer |
| Diplomats | `diplomat_icon.bmp` 20x19 | (344,7) | 372 | integer |
| Missionaries | `missionar_icon.bmp` 20x20 | (406,7) | 432 | integer |
| Date | — | — | **centred** at 512 | localised date string; white running, grey `#808080` when paused (NIEPEWNE which colour is which) |
| War indicator | `gfx\map\flags\flag_war.bmp` | (604,8) | — | only while at war |
| Stability | — | — | 624 | clamped −3..+3, "+%d" when positive |
| Manpower (NIEPEWNE) | `button area\btn_bar_soldier.bmp` 32x20 | (644,6) | 684 | integer |
| Treasury | `button area\btn_bar_treasury.bmp` 24x21 | (712,5) | 736 | integer (ducats) |
| Pause | BUTTON_PAUSE, `button area\pause_button.bmp` (20x16 frames) | (768,8) | — | toggles pause; tooltip BUTTON_0050 |

Each value has a mouse-over tooltip (manual p.7: "You have 5 Merchants (currently 5.00 per year)
…"). Tooltip box: width = text width + 23 (max 220), height ≤ 590, placed at the mouse and pushed
inside the screen with an 8 px margin, gold (`#FFBD00`) separator line between header and body
(`0x526340`, end of the id-5 branch). Tooltip text keys: NIEZNANE (search `text.csv` for
"currently %" style strings).

### 2.4 Map-area overlays

`selectanim_army.bmp` / `selectanim_navy.bmp` (988x45, 13 frames of 76x45) — selection ring;
`moveanim_*` (988x40, 13 frames) — move order arrows; `moveorderanim.bmp`,
`illegalmoveorder.bmp` (`0x530f70`); `statusbar.bmp` 44x7 / `statusbarselect.bmp` 396x7 (9 frames
of 44x7) — unit strength bars under unit sprites. `searchpanel.bmp` 600x64 with a 550x100 text
field (`0x566200`) — the find-province panel; position NIEZNANE.

## 3. Province window (PIW)

Object `0x502830` (ctor), draw `0x503ff0` (absent from the export, read from disassembly),
background/building setup `0x504ed0`. Window = `iw_province.bmp` 224x344 at (0,103); everything
below is **window-relative** (add (0,103)).

### 3.1 Composition order

1. `iw_province.bmp` frame (marble, gold border).
2. Terrain background **216x344 at (4,0)**, chosen by province state/terrain:
   `piwbg_loot` (province looted) else, unless winter, by terrain 0 plains / 1 forest1 / 2 mountain /
   3 desert / 4 marsh; `piwbg_winter` in winter. (`piwbg_forest2` is never chosen — NIEPEWNE.)
   Palette `piwbg_palette.bmp`.
3. Coastal provinces add `PIW\Backgrounds\coast.bmp` 212x120 at **(4,224)** (the bottom strip).
4. Buildings, in the order of the layout file, each at **window + (x,y) from the layout file**,
   clipped to the window.
5. `crest.bmp` 112x80 at (52,0); resource icon `PIW\Buildings\resource.bmp` (21 frames of 56x60,
   frame = trade good) at (82,4); two orange (`#FFC000`, garamond14bold) numbers centred at
   x=74 and x=153, y=18 (left and right of the resource icon — NIEPEWNE which is which: income/tax
   vs. manpower/production); owner shield (4 frames) at (168,6).
6. If the province is locked/besieged state flag: `piw_lock_left.bmp` at (54,30), `piw_lock_right.bmp`
   at (152,30) (24x28 each).
7. `sidebar_banner2.bmp` at screen (0,417).
8. Province not visible/unknown: `msgbox_empty_province.bmp` 200x100 centred in the window with
   texts PIWEMPTYT (y=20), PIWEMPTY1 (y=40), PIWEMPTY2, PIWEMPTY3 (one line each further),
   garamond14bold, `#FFC000`, centred.

### 3.2 Layout files

`db\province.csv` column 3 "PIW name" selects `gfx\interface\piw\<name>.txt` (`0x484380`); in v1.09
only `inland` (1235 provinces) and `coastal` (407) are used; `city.txt` exists but no province
names it. Format: one `key = {x = N y = N}` per line, `#` comments, `background = "<x>.cty"`
(ignored by the game as far as seen). Negative coordinates are allowed (clipped).

Keys and their bitmaps / frame counts (from `PIW\Buildings\filelist.txt` and the code):

| Key | Bitmap | Frames | Meaning (frame chosen by level/state) | Button |
|---|---|---|---|---|
| barracks | barracks.bmp 184x60 | 2 | conscription centre | BUTTON_BUILD_ARMY (BUTTON_0055) |
| port | port.bmp 312x100 | 3 | port / shipyard | BUTTON_BUILD_NAVY (BUTTON_0056) |
| fort | fort.bmp 868x100 | 7 | fortress level | BUTTON_EXPAND_FORTRESS (BUTTON_0057) |
| FortUtbyggd | FortUtbyggd.bmp 1344x146 | 7 | large fortress variant | |
| church | `<culture>\church.bmp` | 11 | religion/state | BUTTON_CHURCH (BUTTON_0060) |
| trading | trading.bmp 480x54 | 6 | trading post (replaces church row for TP) | |
| bank | bank.bmp 200x55 | 2 | loans | BUTTON_LOANS (BUTTON_0058) |
| embassy | embassy.bmp 224x82 | 2 | diplomacy | BUTTON_DIPLOMACY_MINISTER (BUTTON_0059) |
| merchant | merchant.bmp 160x67 | 2 | place merchant | BUTTON_PLACE_MERCHANT (BUTTON_0061) |
| resource | resource.bmp 1176x60 | 21 | trade good (frame 0 = none, green placeholder) | |
| native | native.bmp 240x32 | 6 | natives | |
| house1..4 | `<culture>\houseN.bmp` | 3 | town growth | |
| tree1..16 | treeN.bmp | 2 | decoration (frame by season — NIEPEWNE) | |
| siege | siege.bmp 44x44 | 1 | siege camp | |
| blockade | blockade.bmp 140x123 | 1 | naval blockade | |
| (manufactory) | manufacturies.bmp 624x75 | 6 | per manufactory type | BUTTON_BUILD_MANUFACTORY (BUTTON_0054) |
| (bailiff/mayor/sheriff) | `<culture>\bailiff/mayor/sheriff.bmp` | 2 | courthouse/city rights/bailiff | BUTTON_BUILD_BAILIFF/CITYRIGHTS/COURTHOUSE (BUTTON_0051–0053) — position NIEZNANE |

Cultures (folder names, table at `0x627f50`, palettes at `0x627f70`): Latin, Orthodox, Muslim,
Hindu, China, Indian, Africa, Aztec.

### 3.3 Build-manufactory sub-panel

`bg_buildmanufact.bmp` 148x218 at window (38,68); five buttons at window x=46,
y = 83, 125, 161, 198, 238: `btn_arts` (144x40 → 36x40), `btn_weapons`, `btn_naval`,
`btn_refinery`, `btn_manufgoods` (144x36 → 36x36). Two-line labels inside the panel at panel x=46:
MANARTS1/2 (y 27/38), MANWEAP1/2 (70/81), MANNAV1/2 (101/112), MANREF1/2 (145/156), MANGOOD1/2
(180/191), garamond14bold `#FFBD00`. A CANCEL text button closes it. Pulsing highlight
`anim_puls.bmp` (16 frames of 24x32) marks buildable buildings (NIEPEWNE).

## 4. Sidebar info windows

All are 224x344 at (0,103) with `sidebar_banner1` over them; *window-relative* coordinates.
Constructed in the main interface ctor `0x524840`:

| Bitmap | Ctor | Window |
|---|---|---|
| iw_none | `0x5148a0` (base class) | nothing selected |
| iw_province | `0x502830` | own province (PIW, §3) |
| iw_hprovince | `0x501110` | foreign province |
| iw_sea / good / bad / seaice | `0x506c60`, `0x506de0` | sea zone (good/bad weather 184x98, seaice) |
| iw_army / iw_navy | `0x4f2de0` / `0x4f3330` | unit window (§6) |
| iw_purchasearmy / iw_purchasenavy | `0x4f41e0` / `0x4f5550` | recruitment |
| iw_battle | `0x4ff110`, `0x4ff550`, `0x500250` | battle / naval battle / siege |
| iw_cot | `0x514dc0` | centre of trade (cotlevel1..6, merchant.bmp) |
| iw_treasury | `0x51c180` | **Budgeting** (sliders, §4.1) |
| iw_budget | `0x518420` | financial summary |
| iw_domestic | `0x517ac0` | domestic policy (§4.2) |
| iw_country | `0x5166e0` | country & monarch (icon_admin/diplomatic/military 40x28, icon_vp, religion shield, vassal arrows) |
| iw_tech | `0x513b90` | land / naval technology (slider) |
| iw_religion | `0x51a030` | religion (slider) |
| iw_resource | `0x51afb0`, `0x51b3a0` | trade good info |
| iw_loans | `0x50fa70` | loans (timglas.bmp 432x59 = 8 hourglass frames, anim_value) |
| iw_peace / iw_peace2 | `0x5105c0` | peace negotiation (demand/offer/no-tribute 192x42, peace_meter, heavens) |
| iw_draw / iw_drlw | `0x50b520`, `0x50a3b0` | diplomacy lists (§5) |
| iw_missionlist | `0x519240` | missions |
| iw_colonist / iw_missionary | `0x4fb550` / `0x4fd0a0` | colonist / missionary dispatch |
| iw_vassal | `0x51e7f0` | vassal |
| iw_mecenatplacement | created inside `0x5148a0` | "MECENATPLACE" message panels, text box 146x230, `#FFBD00` |

### 4.1 Budgeting window (`iw_treasury`, `0x51c180`)

Six rows, step 47 px. Labels (garamond14 white, x=20): TWLANDTECH y=16, TWNAVALTECH 63,
TWSTABILITY 110, TWTRADE 157, TWPRODUCTION 204, TWTREASURY 251.

Sliders: 6 objects (`0x427aa0`), range 0..100, start 50, step 1; track = `sprite_slider.bmp`
132x6 (NIEPEWNE: track is drawn from `sprite_slider`, knob is `button area\btn_slider.bmp`
16x16 x4 frames), positioned at x=62, y = 30, 77, 124, 171, 218, 265. Percent/amount text at
x=194, y = 39, 86, 133, 180, 227 (274). One-time "boost" buttons `btn_boost.bmp` (24x24 x4) at x=26:

| Button | y | Tooltip |
|---|---|---|
| BUTTON_LAND_TECHNOLOGY | 27 | BUTTON_0048 |
| BUTTON_NAVAL_TECHNOLOGY | 74 | BUTTON_0049 |
| BUTTON_INVEST_IN_STABILITY | 121 | BUTTON_0045 |
| BUTTON_INVEST_IN_TRADE_EFFICIENCY | 168 | BUTTON_0047 |
| BUTTON_INVEST_IN_PRODUCTION_EFFICIENCY | 215 | BUTTON_0046 |

Also `help_dot.bmp`, `progress.bmp` 132x14 (progress bars), CLOSE text button. The buttons sit at
y one slider row earlier than the label (land tech button 27 next to label 16): NIEPEWNE exact.

### 4.2 Domestic policy (`iw_domestic`, `0x517ac0`)

8 sliders, rows at **y = 16 + 36·i** (i = 0..7). Range 0..10, start 5, step 1 (each slider:
track `sprite_slider`, knob `btn_slider`, `btn_minus` / `btn_plus` 16x16 x4 frames). Per row the
left policy name is drawn left-aligned at x=16 and the right policy name right-aligned at
x = window_width − 16 (garamond14 white), on row y. The slider/minus/plus positions inside the row:
NIEZNANE (expected just under the labels; check the draw method of vtable `0x5ec15c`).

### 4.2a Sources of 4.3-4.5

Decoded 2026-11-09 from disassembly (objdump + a stack/FPU-tracking helper; the draw methods are
missing from the Ghidra export).

Source: `eu2.exe` v1.09, read from objdump disassembly with a symbolic stack tracker
(`re/symx.py`, which tracks pushes, stack slots and the FPU stack) plus the Ghidra export.
This file records layout facts only. No code was copied.

Conventions, as in SPEC_UI §3/§4:
- The window is 224x344 at screen (0,103). Every coordinate below is **window-relative**.
- Some routines draw onto the screen surface (`ds:0x89b2f4`) at `[this+0x12]`/`[this+0x16]` + const.
  Others draw into the window's own surface `[this+0x62]` with plain constants. Both end up
  window-relative.
- Text calls take (str, x, y, surface, colour, 0) with ecx = font. `0x40ccaf` draws left-aligned,
  `0x40cd35` centred on x, `0x40cdd2` right-aligned (x is the right edge). The pattern
  `x = winx + 200 − width(str)` with `0x40ccaf` is right alignment by hand, and it is listed below
  as "right @200".
- Fonts: g14 = `garamond14`, g14b = `garamond14bold`, g24b = `garamond24bold`.
- Colours: gold `#FFBD00`, white `#FFFFFF`, red `#FF0000`, green `#00FF00`.
- `country` means the country object (`[this+0x82]` in the budget/tech windows, `[this+0xca]` in the
  country window).

---

### 4.3 Financial summary (`iw_Budget.bmp`)

Ctor `0x518420`, vtable `0x5ec1b8`.
- Slot +0x0C = **draw `0x5185c0`**. It is absent from the Ghidra export; the export's
  `0x518600..0x5191c0` lies inside it.
- Slot +0x3C = positioning `0x5191c0`.
- The sidebar header title is `HEAD_FIN` ("Financial Summary") plus the country name
  (`0x52fba0`, `0x5185df`).
- Draws onto the screen surface. The base y is `winY − 1 + k` (`0x51861f dec ebp`), so the y
  values below already include the −1.

Columns:
- Labels: left-aligned at **x = 25** (`+0x19`).
- Middle column: centred at **x = 152** (`+0x98`), g14 white.
- Amount column: **right-aligned at x = 200** (`+0xC8 − width`), g14.

| y | Label key (text) | Label font/colour | Middle column (centred @152, g14 white) | Amount (right @200, g14) | Amount colour | Evidence |
|---|---|---|---|---|---|---|
| 16 | IWBINCOME "INCOME CALCULATION" | g14b white | – | – | – | `0x518728` |
| 32 | IWBTAXATION "Tax Income" | g14 gold | `"%d (x%.2f)"`: int(T / m), m | `"%.1f"` T | gold | `0x518758`, `0x518799`, `0x5187d5` |
| 46 | IWBPRODUCTION "Goods Production" | g14 gold | `"%d (%d%%)"`: int(P / pe), int(pe·100) | `"%.1f"` P | gold | `0x51882b`, `0x5188a5` |
| 60 | IWBTRADE "Trading Income" | g14 gold | `"%d (%d%%)"`: int(e[0x08] / te), int(te·100) | `"%.1f"` e[0x08] | gold | `0x518958`, `0x5189b8` |
| 74 | IWBGOLD "Gold Mines" | g14 gold | – | `"%.1f"` e[0x14] | gold | `0x518a53` |
| 88 | IWBTOTAL "TOTAL:" | g14b gold | – | `"%.1f"` INCOME | **green** (always) | `0x518add`, `0x518b3e` |
| 110 | IWBFEXPENSE "FIXED EXPENSES" | g14b white | – | – | – | `0x518b6a` |
| 126 | IWBMAINTEN "Military Maintenance" | g14 gold | – | `"%.1f"` −(e[0x34]+e[0x38]) | gold | `0x518b98`, `0x518be7` |
| 140 | IWBINTEREST "Loans Interest" | g14 gold | – | `"%.1f"` −e[0x6C] | gold | `0x518c18`, `0x518c72` |
| 154 | IWBTOTAL | g14b gold | – | `"%.1f"` −FIXED | **red if FIXED > 0, else gold** | `0x518ca1`, `0x518cc1..0x518cd9` |
| 171 | IWBBEXP "INVESTMENTS" | g14b white | – | – | – | `0x518d36` |
| 187 | IWBLTECH "Land Technology" | g14 gold | – | `"%.1f"` c[0xEA8]·B | gold | `0x518d63`, `0x518dc0` |
| 201 | IWBNTECH "Naval Technology" | g14 gold | – | `"%.1f"` c[0xEA4]·B | gold | `0x518ded`, `0x518e4a` |
| 215 | IWBSTB "Stability" | g14 gold | – | `"%.1f"` c[0xEB4]·B | gold | `0x518e77` |
| 229 | IWBPE "Infrastructure" | g14 gold | – | `"%.1f"` c[0xEAC]·B | gold | `0x518f01` |
| 243 | IWBTE "Trade" | g14 gold | – | `"%.1f"` c[0xEB0]·B | gold | `0x518f8b` |
| 257 | IWBTOTAL | g14b gold | – | `"%.1f"` −INV | **red if −INV > 0, else gold** (see note) | `0x519017`, `0x51903b..0x519053` |
| 281 | IWBMONTHLY "MONTHLY BALANCE" | **g14** white | – | `"%.1f"` BAL | **green if BAL > 0, else red** | `0x5190ae`, `0x5190d8..0x5190f0` |
| 305 | IWBINFLATION "INFLATION" | g14 white | – | `"%.1f%%"` inflation·100 | white | `0x51914b`, `0x519156..0x5191a1` |
| 319 | CLOSE button (text button, g14b, created in the ctor) | – | centred: x = 112 − w/2 | – | – | `0x5191c0` (`+0x13F`) |

Values:
- e = `*(country+0xE74)`, the monthly income/expense struct (float fields):
  - tax group: e[0x04], e[0x18], e[0x2C], e[0x30];
  - trade: e[0x08];
  - production: e[0x0C], e[0x10];
  - gold: e[0x14];
  - military maintenance: e[0x34], e[0x38] (two parts; land/naval is **NIEPEWNE**);
  - loan interest: e[0x6C].
- T = e[0x30]+e[0x2C]+e[0x18]+e[0x04] (`0x5186ca`).
- m = tax multiplier `0x4389f0(1.0)` = `FUN_00431560` = table `0x64d68c[country→vfunc+0x34]`.
  The index is a per-country level, perhaps difficulty or AI level (**NIEPEWNE**).
- P = int(e[0x10]) + e[0x0C] (the middle column shows int(P / pe)).
- pe = production efficiency `0x43b560`. te = trade efficiency `0x43b610`. Both are clamped to a
  minimum constant.
- INCOME = e[0x04]+e[0x08]+e[0x0C]+e[0x10]+e[0x14]+e[0x18]+e[0x2C]+e[0x30].
- FIXED = e[0x34]+e[0x38]+e[0x6C].
- B = country[0xE80], the budgetable income. The five investment fractions are country[0xEA8]
  land, [0xEA4] naval, [0xEB4] stability, [0xEAC] production/infrastructure, [0xEB0] trade.
- INV = (1 − country[0xEB8])·B, where country[0xEB8] = treasury share.
- BAL = INCOME − FIXED − INV.
- Inflation = `0x43c8e0` = country[0xE84], clamped to ≥ 0.

Note on the totals colouring:
- The fixed-expense total compares the **positive** sum against 0. It is red whenever there are
  expenses.
- The investment total compares the **negated** sum against 0. It is red only when the investment is
  negative.
- This asymmetry is what the code does (`0x518cc1` loads the unnegated slot; `0x51903b` loads the slot
  stored by `0x519022 fst` after `fchs`).

---

### 4.4 Land / naval technology window (`iw_tech.bmp`)

Base ctor `0x513b90`, base vtable `0x5ebf84`. There are two subclasses:

| | Land | Naval |
|---|---|---|
| ctor | `0x513540` | `0x514230` |
| vtable | `0x5ebf28` | `0x5ebfd8` |
| +0x0C draw (values, screen surface) | `0x513570` | `0x514290` |
| +0x3C refresh (window surface) | `0x513850` | `0x514590` |
| +0x40 slider changed | `0x513a30` | `0x514740` |
| +0x44 tooltip | `0x513a60` | `0x514770` |

Shared pieces:
- `0x513e40` positions the button and the slider.
- `0x513ea0` (args: name, description, shock, morale, fire) reloads the background and draws the
  static texts into the window surface.

Window objects:
- `+0xAE` text button MILWCLOSE "CLOSE" (g14b).
- `+0xB2` slider `0x427aa0`:
  - min **0.5**, max **1.0**, initial **1.0**, step **0.01**;
  - track `gfx\interface\sprite_slider.bmp`, knob `button area\btn_slider.bmp`;
  - both ± buttons `gfx\empty.bmp`, so the slider has no ± buttons (`0x513c8b`, ctor args
    min/max/value/step = `[+0x9A]/[+0x9E]/[+0x8E]/[+0x96]`).
- `+0xB6` text box.

| Element | Position | Align / font / colour | Content | Evidence |
|---|---|---|---|---|
| Title | (112,20) | centred, g14b gold | MILLAND "LAND MILITARY" / MILNAVAL "NAVAL MILITARY" | `0x5139cc`, `0x5146da` |
| Tech name | (112,37) | centred, **g24b white** | land/naval tech object → vfunc +0x14 (no args); object = `country+0xE46`+0x0C land / +0x24 naval | `0x51397b`, `0x51409e` |
| SHOCK / MORALE / FIRE | (54,70) / (112,70) / (168,70) | centred, g14 gold | MILWSHOCK, MILWMORALE, MILWFIRE | `0x5140e2..0x514129` |
| Pips | centred at x = 54 / 112 / 168, y = 85 | `prickar.bmp` (5 frames) | shock / morale / fire value (see below) | `0x514000..0x514048` |
| MILWRESEARCH "Research:" | (24,101) | left, g14 gold | | `0x5140c0` |
| Research progress | (140,101) | centred, g14 white | `"(%d$ / %d$)"` = (tech.vf+0x30(), tech.vf+0x24()) when tech.vf+0x0C() is true, else `"---"` | `0x5137e3`, `0x51383a` |
| MILLANDMA / MILNAVALMA "Supportable Amount" | (24,131) | left, g14 gold | | `0x513a1a`, `0x514728` |
| Supportable value | right @196, y = 131 | right, g14 white | `"%d"` land `0x45ab50` / naval `0x45a7f0` (support limit) | `0x5136d5`, `0x5143f4` |
| MILTOTALS "Totals:" | (24,150) | left, g14 gold | | `0x5141d1` |
| Unit counts | right @106 / 150 / 196, y = 150 | right, g14 white | `"%d"` country vfunc +0x388 inf, +0x38C cav, +0x390 art (land); +0x394 war, +0x398 galley, +0x39C transport (naval) | `0x5135fa..0x51368e`, `0x514319..0x5143ad` |
| MILCOST "Monthly Maintenance Cost" | (24,169) | left, g14 gold | | `0x5141ac` |
| Cost value | right @198, y = 169 | right, g14 white | `"%.1f$"` = (Σ unit-type maintenance × slider) / current maintenance level | `0x513781`, `0x5144c1` |
| MILMAIN "MAINTENANCE LEVEL" | (112,189) | centred, g14b gold | | `0x514187` |
| Slider range labels | `"50%"` left at (22,203); `"100%"` right @210, y = 203 | g14 gold | literal strings | `0x514143`, `0x514160` |
| Slider | (48,207) | track `sprite_slider` (132x6 per SPEC_UI) | value = maintenance level | `0x513e40` (`+0x30`, `+0xCF`) |
| Description text box | (32,225), 162x73 | white text (`+0x56`) | LTLEVEL_LTIW / NTLEVEL_NTIW (the "Level 0: Allows…" list). `0x41fec0(winx+207)`: a right edge or scrollbar, **NIEPEWNE** | `0x513f34`, `0x513f6a`, `0x513f7d`, `0x513f90` |
| CLOSE | x = 112 − w/2, y = 319 | text button | | `0x513e40` |

Pips (`0x514c50(x, y, value, scale, slot, centred)`):
- Count N = clamp(1 − trunc(value · (−4.999) / scale), 1, 5), i.e. 1 + trunc(4.999·value/scale).
- Frame N−1 of `prickar.bmp`, drawn N times with a spacing of frame width + 2.
- Centred: the start x is x + 2 − N·(w+2)/2.
- The tech window uses scale 1.0, so N is 1..5 for values 0..1 (`0x514c79..0x514d14`, constant
  `0x5ec07c` = −4.999).

Pip values (computed in refresh). `L(o)` is a float field of the land-tech table entry
(`0x64dad0[level]`); `N(o)` is the table at `0x64dae0` (reached via `country+0xE46`+0x24):
- **Land:**
  - shock = (L(0x24)·0.1 + L(0x20) + L(0x1C)) / 11;
  - morale = `0x438690()`·0.2;
  - fire = (L(0x18)·0.1 + N(0x18) + L(0x14)) / 75.5 (`0x513863..0x5138fc`).
- **Naval:**
  - shock = (`0x4337c0`+`0x433770`)·0.25;
  - morale = `0x438850()`·0.25;
  - fire = (`0x433720`+`0x4336d0`)/3 (`0x5145a3..0x514604`).
- Whether a naval-table field really enters the land fire value is **NIEPEWNE** (`0x4336d0` is
  used in both).

Slider behaviour:
- On refresh the slider value = country[0x12C4] (land) or [0x12C8] (naval), clamped to the slider
  range (`0x513900`, `0x51460e`).
- On a change, both levels are sent with `0x443fb0(land, naval)`. The land window sends
  (slider, current naval) and the naval window sends (current land, slider) (`0x513a52`,
  `0x514762`).

Cost formulas:
- Land: (`0x459c40`·s + `0x459e20`·s + `0x45a000`·s) · (1/country[0x12C4]). The order of the
  three getters is inf/cav/art according to the tooltip; the per-getter mapping is **NIEPEWNE**.
- Naval: the same with `0x45a5f0`/`0x45a3f0`/`0x45a1f0` and 1/[0x12C8]. The divisor is taken only
  if s > 0, otherwise ×1 (`0x514407`).

Tooltip zones (window-relative):

| Zone | Tooltip |
|---|---|
| y 131..149 | `0x45abe0` / `0x45a8a0` |
| y 150..164, x < 110 | MILLAND_INF / MILNAVAL_WAR |
| y 150..164, x < 152 | MILLAND_CAV / MILNAVAL_GAL |
| y 150..164, otherwise | MILLAND_ART / MILNAVAL_TRA |

The tooltips are formatted with the per-unit cost ×1000 (×10 for artillery) and the over-limit
cost (`0x513a75..0x513b4a`).

---

### 4.5 Country & monarch window (`iw_country.bmp`)

Ctor `0x5166e0`, vtable `0x5ec110`:
- +0x04 events `0x516c10`;
- +0x0C draw children `0x516f00`;
- +0x3C positions `0x516fb0` (it first calls setup `0x517180(country)`);
- +0x44 / +0x48 tooltips `0x517860` / `0x517a00`.

Setup `0x517180`:
- reloads `iw_country.bmp` (`0x5171d9`);
- sets the sidebar title HEAD_CIW "Country Information" + country name (`0x5171cb`);
- draws all texts and pips **into the window surface**.

Texts (g14 = `ebp`, gold = labels, white = values):

| Element | Position | Align / font / colour | Content | Evidence |
|---|---|---|---|---|
| Country name | (112,13) | centred, g24b gold | country[0x12] (name string) | `0x5174d5` |
| Rank | (112,61) | centred, g14 white | `"#%d"`, rank (below) | `0x51779b`, `0x5177b5` |
| Score | (112,94) | centred, g14b white | mode `[0x647d48]`: 2 → `"%d"` country[0x1224] (VP); 3 → `"%d%%"` int(country[0x121A]); else `"%d"` int(country[0x20DE]) | `0x517701..0x517787` |
| Monarch label | (112,148) | centred, g14b gold | COWEMP "MONARCH(Emperor):" if country[6] == `[0x8d0e18]` (`0x43ce10`), else COWMONARCH "MONARCH:" | `0x5174e0..0x517512` |
| Monarch name | (112,164) | centred, g14 white | `0x43bee0()`; when `0x42ea80(country+0x1F8E, country[0x20BC])` is true, + `" ("` + string at country+0x1FBC + `")"` (meaning **NIEPEWNE**) | `0x5173e7..0x517530` |
| Pips DIP / ADM / MIL | centred at x = 58 / 112 (= W/2) / 168, y = 211 | `prickar.bmp`, scale **9.0** | `0x43bf40` / `0x43bf10` / `0x43bf90` (monarch stats; 3 when there is no monarch). The DIP/ADM/MIL mapping follows the icon order and is **NIEPEWNE**. `0x43bf40` passes through a modifier `0x42b6a0` | `0x5177ba..0x51784d` |
| COWSTABILITY "STABILITY" | (32,231) | left, g14 gold | | `0x517555` |
| Stability value | right @200, y = 231 | right, g14 white | `"%s%d"`: `"+"` if stability ≥ 0, else `""`; stability = `0x43a230` (int) | `0x517560..0x5175c2` |
| COWLAND "LAND" | (32,245) | left, g14 gold | | `0x5175e7` |
| Land tech | right @200, y = 245 | right, g14 white | land-tech object vfunc +0x14 (the same string as the tech window title) | `0x51761b` |
| COWNAVAL "NAVAL" | (32,259) | left, g14 gold | | `0x517640` |
| Naval tech | right @200, y = 259 | right, g14 white | naval-tech object vfunc +0x14 | `0x517674` |
| COWCULTURE "CULTURES" | (112,283) | centred, g14 gold | | `0x517699` |
| Culture list | (112,297) | centred, g14 white | `0x429fe0(country[0x382])` (string) | `0x5176c0` |

Rank (`0x517227..0x517318`):
- Loop over all 340 country slots `0x64e140..0x64e690`. rank = 1 + the number of countries with a
  greater score. Mode 3 compares [0x121A]; other modes compare [0x20DE].
- Mode 2 adds 1 for a greater [0x1224] **and** 1 for a greater [0x121A], because the code falls
  through.
- The same loop also gathers the maxima of [0x360]/[0x364]/[0x368]/[0x1386]/[0xE98], but the stack
  slots are overwritten before use, so this is dead code.

Bitmaps and buttons (positions from `0x516fb0`; drawn by `0x516f00` in this order: BE, C2, BA, B6,
CE, D2, D6, AE, DE, C6, E2, E6):

| Member | Bitmap | Position | Function / tooltip | Evidence |
|---|---|---|---|---|
| +0xDE | `gfx\map\shields\small\smallshield_<tag><suffix>.bmp` (`0x43b210(country[6])`); frame count set to 4 (which frame is shown: **NIEZNANE**, presumably 0) | (28,63) | country shield | `0x517336..0x51736b` |
| +0xC2 | `GFX\interface\btn_religion_shield.bmp` (button) | (22,54) | over the country shield; click → window id 0x19 (domestic policy); tooltip COWROLLDOM | `0x517173`, `0x516eaa`, `0x51798c` |
| +0xAE | `gfx\interface\religions.bmp`, 11 frames, frame = country[0x254] (religion) | (172,61) | tooltip = religion name `0x431180` | `0x51713e`, `0x5171a8` |
| +0xBE | `btn_religion_shield.bmp` (button) | (162,54) | over the religion icon; click → id 0x18 (religion window) | `0x51715a`, `0x516e86` |
| +0xDA | `gfx\interface\icon_vp.bmp` | (54,47) | tooltip COWROLLVP. **It is positioned but never drawn.** `0x516f00` skips it and setup only calls setpos (vfunc +0x10 = `0x40e790`) twice, so the laurel is presumably part of `iw_country.bmp` (**NIEPEWNE**) | `0x5176cf..0x5176f9`, `0x51793a` |
| +0xC6 | text button COW_VASSAL "Create Vassal" (g14b) | x = 112 − w/2, y = 117 | click → id 0x1b; tooltip COW_VASSAL1_IRO / COW_VASSAL2_IRO; enabled per `0x43d020` (**NIEPEWNE**) | `0x517053`, `0x516ece`, `0x51788d` |
| +0xE2 | `gfx\interface\vassal_left.bmp` | x = button left − width, y = 115 | decoration left of the vassal button | `0x51708c` |
| +0xE6 | `gfx\interface\vassal_right.bmp` | x = button left + button width, y = 115 | decoration right of the vassal button | `0x5170cb` |
| +0xCE | `gfx\interface\icon_diplomatic.bmp` 40x28 | (42,181) | COWROLLDIP1 / DIP2 | `0x5170e7`, `0x5178c8` |
| +0xD2 | `gfx\interface\icon_admin.bmp` 40x28 | (94,181) | COWROLLADM1 / ADM2 | `0x517103`, `0x5178ee` |
| +0xD6 | `gfx\interface\icon_military.bmp` 40x28 | (146,181) | COWROLLMIL1 / MIL2 | `0x517122`, `0x517914` |
| +0xBA | `reorganisation\btn_pil_left.bmp` | (54,319) | previous country; COWROLLPREV | `0x517028`, `0x5179d8` |
| +0xB6 | `reorganisation\btn_pil_right.bmp` | (156,319) | next country; COWROLLNEXT | `0x51700d`, `0x5179b2` |
| +0xB2 | text button "BACK" (g14b) | x = 112 − w/2, y = 319 | close | `0x516ff0` |

`piwbg_palette.bmp` is loaded and applied twice in the ctor (`0x5169d5`, `0x5169f8`), most likely for the
palettised shield and religion strips (**NIEPEWNE**). The prev/next arrows step through
`0x64e140[]` and re-run setup with the new country (`0x516cdc`, `0x516d7f`).

This file records layout facts only, read from the `eu2.exe` v1.09 disassembly with the symbolic
stack tracker `re/symx.py`. No code was copied. The conventions are the same as `layouts_iw.md`:
- Coordinates are **window-relative**. The window is 224x344 at screen (0,103).
- Text calls: `0x40ccaf` / `0x40cc29` draw left-aligned, `0x40cd35` centred, `0x40cdd2`
  right-aligned.
- Fonts: g14 / g14b / g24b. Colours: gold `#FFBD00`, white, red, green, yellow `#FFFF00`.
- `prov` = the province record `0x8077d4 + idx·0x4A0`. The current index is the global `[0x61f418]`.
- `player` = `[this+0x82]`, the player country / main interface.

---

### 4.9 Sidebar header lines (`0x52fba0(line1, line2)`; line 1 is stored at `+0x64D8`)

**Line 1 is always the province name** (`prov[0x04]`, copied with `0x414be0`) for the own-province
window (PIW, `0x5040bf`, `0x504fc0`) and the foreign-province window (`0x50180c`).

**Line 2** comes from `0x52d190(prov)` (`0x52d190..0x52d218`), returned in the buffer `0x8be150`:

| Condition (checked in this order) | Line 2 |
|---|---|
| `prov[0x40C]` ≠ 0 (trading post) | HEAD_TP "Level %d Trading Post", %d = int(tp[0x04]) |
| `prov[0x408]` ≠ 0 (settlement) and `city[0x2C]` set (a city) | HEAD_CITY "City (Population: %d)", int(city[0x7E]) |
| settlement, city vfunc +0x10 true (a colony) | HEAD_COL "Level %d Colony", city[0x72] |
| settlement, otherwise | HEAD_CITY with int(city[0x7E]) |
| no settlement and no TP | empty string |

- **HEAD_CAPITAL and HEAD_NATIONAL are never used.** Neither key string exists in `eu2.exe`; they
  occur only in `text.csv`.
- The map tooltip uses the same line-2 text (`0x532713`).
- For other windows:
  - budget = (country name, HEAD_FIN);
  - country window = (country name, HEAD_CIW);
  - sea = (sea name, weather text, see §2);
  - army/navy = (unit name, location name) (`0x4f2f5f`, `0x4f353b`).

---

### 4.6 Foreign province window (`iw_hprovince.bmp`)

Ctor `0x501110`, vtable `0x5ebaa8`:
- +0x04 events `0x501470`;
- +0x0C draw children `0x5016c0`;
- +0x3C setup `0x5017d0` (texts go into the window surface);
- +0x44 / +0x48 tooltips `0x5022a0` / `0x5025c0`.

**Redirects** (`0x5018a8`):
- If `prov[0x3C4]` (owner) is 0, the window is not shown:
  - `prov[0xCA]` == 6 → window id 0;
  - `prov[0xCA]` < 5 → id 9;
  - otherwise → id 1.
- The meaning of `[0xCA]` (province class) and of these ids is **NIEPEWNE**: probably 6 = terra
  incognita, <5 = an uncolonised land province, 5 = sea.

Members:
- +0xAE `religions.bmp` (11 frames);
- +0xBA button `piw\buildings\resource.bmp` (21 frames, palette `resource.bmp`);
- +0xB2 `anim_value.bmp` (6 frames);
- +0xB6 `cot.bmp`;
- +0xBE text button BACK (g14b);
- +0xC2 / +0xC6 shield objects.

| Element | Position | Font / colour | Content | Evidence |
|---|---|---|---|---|
| Title | centred (112,21) | g14b gold | name of the settlement (`*prov[0x408]`+0x14) if there is one, else the province name `prov[0x04]` (**NIEPEWNE** which name the settlement struct holds) | `0x501760..0x5017bc` |
| Controller shield | (24,19) | `smallshield_<tag>` of `prov[0x3C8]`, 4 frames, frame 0 | | `0x501944..0x501998` |
| Owner shield | (172,19) | `smallshield` of `prov[0x3C4]` | | `0x50199b..0x5019f1` |
| Owner name | centred (112, h(g14b)+21) | g14 gold | `owner[0x12]`. Drawn only when `0x47f6c0(prov)` ≠ 0; the whole block down to Fortress depends on this (**NIEPEWNE**: "has a settlement") | `0x5019f4..0x501a27` |
| Settlement picture | centred at (40,74) | temporary bitmap; the file name comes from `0x47f6c0()`→+8→+0xC | city/culture picture (**NIEPEWNE**) | `0x501a2e..0x501ab2` |
| HPWPOP "Population:" | (66,54) | g14 gold, left | | `0x501b6e` |
| Population value | (146,54) | g14 gold, left | `"%d (%d%%)"` = population, int(`0x47e940`) growth. Population = int(city[0x7E]); for a trading post it is int(tp[0x04])·10 | `0x501ab7..0x501b88` |
| HPWREVOLT "Revolt Risk:" | (66,68) / value (146,68) | g14 gold | `"%d%%"` `0x481a30()` | `0x501b8d..0x501be0` |
| HPWMANPW "Manpower:" | (66,82) / value (146,82) | g14 gold | `"%.1f"` `0x47cc80()` | `0x501be5..0x501c3b` |
| HPWFORT "Fortress:" | (66,96) / value (146,96) | g14 gold | EYRFORT1..6 for level 1..6 (`int(city[0x3A])`), else EYRFORT0 "None" | `0x501c40..0x501cbf` |
| HPWSUPPLY "Supply Limit:" | (66,110) / value (146,110) | label gold; value **green** if `0x484000(player)`, **yellow** if `0x483bf0(player)`, else **red** | `"%d"` `0x483570(player,0)` | `0x501ce1..0x501dae` |
| HPWATTRITION "Max Attrition:" | (66,124) / value (146,124) | g14 gold | `"%d%%"` `0x4837f0(player,0)` | `0x501db3..0x501e0f` |
| Religion icon | (28,100); hit box at (28,102) | `religions.bmp`, frame `0x47ca10()` | click → window id 0x18; tooltips HPREL_IRO / HPREL_DRO | `0x501e14..0x501e6e`, `0x501620` |
| Culture name | x = icon_w/2 + 16, y = icon_h + 100 | g14 gold, `0x40cc29` | `0x429f20(prov[0xD8])`. The alignment of `0x40cc29` is **NIEPEWNE** | `0x501e71..0x501eaf` |
| CoT icon | (24,150) | `cot.bmp` | click → centre the map on the CoT province (`prov[0x3E4]`, `[0x3E8]`) | `0x501eb4..0x501efb`, `0x501639` |
| HPWCOT "CENTER OF TRADE" | centred (132,160) | g14b gold | | `0x501f25` |
| CoT province name | centred (132,177) | g14 gold | name of province `*prov[0xAA]`+4 | `0x501f2a..0x501f5c` |
| HPWTAX "PROVINCE INCOME" | centred (132,216) | g14b gold | | `0x501f84` |
| Income value | centred (132,235) | g14 gold | v = `0x47d970()`, shown as sprintf(key, int(v)): v<1 HPNONE, <6 HPVLOW, <11 HPLOW, <21 HPMEDIUM, <51 HPHIGH, else HPVHIGH (keys "Very Low (%d)" …) | `0x501f89..0x5020e7` |
| Income meter | (16,205) | `anim_value.bmp`, frame = tier 0..5 (same tiers as above) | | `0x5020ec..0x502143` |
| HPWTRA "TRADE REVENUES" | centred (132,276) | g14b gold | | `0x502169` |
| Trade value | centred (132,293) | g14 gold | t = int(`0x480e30()`·12): ≤0 HPNONE, 1–2 HPVLOW, 3–6 HPLOW, 7–12 HPMEDIUM, 13–50 HPHIGH, 51–100 HPVHIGH, ≥101 HPEHIGH; sprintf(key, t) | `0x50216e..0x502223` |
| Resource icon | (16,258) | `resource.bmp` button, frame = `0x481000(prov)`[+4] (trade good) | click → window id 0x11 (trade good info) | `0x5016eb..0x501727`, `0x5015cd` |
| BACK | centred x=112, y=319; when BURN_TP is shown, centred x=66 | text button | → window id 0 | `0x50153f..0x501569` |
| BUTTON_BURN_TP (sidebar button id 0x5F) | centred x=158, y=319 | shown if `0x45b690(player, prov)` (presumably a burnable trading post), else moved to (−1000,−1000) | | `0x501482..0x501523` |

Other events:
- Event types 0x16 / 0x4E → window id 0xA (**NIEPEWNE** what that is).

Tooltip zones (`0x5022a0`, `0x5025c0`):
- the controller/owner shields: HPCONI1/2, HPOWNI1/2;
- HPINFOABOUT, HPTAXINFO, HPCOTINFO;
- rows by y at x ≥ 66:
  - 54–67: HPPOPI1/2;
  - 68–81: HPREVOLT;
  - 82–95: HPWMANPW_I;
  - 96–111: HPFORTI1/2;
  - 110–124: HPSUPPLY_IRO;
  - 124–137: HPATTRITON_IRO;
- y 205–257 (income meter) and 258–319 (resource icon) are separate zones.

---

### 4.7 Sea zone window (`iw_sea.bmp`)

Ctor `0x506c60`, vtable `0x5ebb54`:
- +0x04 events `0x506de0` (it is not a ctor);
- +0x0C draw `0x5074b0` (screen surface);
- +0x3C setup `0x507ad0`;
- +0x44 tooltip `0x507ba0`.

Members:
- +0xAE weather picture: `good.bmp` / `bad.bmp` / `seaice.bmp` (184x98 per SPEC_UI);
- +0xB2 text button SEA_PIRB "Commission Privateers" (g14b).

If `prov[0xCA]` == 6, the event handler returns to window 0 (`0x506e1b`).

The weather condition is shared by the picture (`0x507b2a..0x507b88`, also re-chosen in the event
handler `0x506e5f..0x506eb6`) and the weather text:
- ice if `prov[0x9E]` ≠ 0 and `0x481910()` is true;
- otherwise storm if the byte `prov[0x248]` is set;
- otherwise clear.

| Element | Position | Font / colour | Content | Evidence |
|---|---|---|---|---|
| Sea zone name | centred (112,20) | g24b white | `prov[0x04]` | `0x5075aa` |
| Weather picture | (20,54) | `seaice` / `bad` / `good` (condition above) | | `0x507506`, `0x507b2a..0x507b88` |
| Weather text | centred (112,165) | g14b gold | SEA_ICEY "Icy Waters" / SEA_STORMY "Storm!" / SEA_CLEAR "Clear Waters" (same condition) | `0x5075af..0x50763c` |
| Wind | centred (112,180) | g14b gold | SEA_WIND "Wind Factor: %d m/s", value `prov[0xA2]`·5 | `0x507641..0x5076a6` |
| SEA_NATIONAL "National waters of" | centred (112,220) | g14b gold | | `0x507891` |
| National shields | x = 16 + i·w, y = 230, at most 10 | `smallshield_<tag>`, frame 0, `shield palette.bmp` | the distinct owners of the neighbouring provinces (`prov[0x114]` neighbours, list `prov[0x11C]`, 12-byte entries); hovering shows the country name (`0x507ca5`, y 231..259) | `0x5076c8..0x507844` |
| Pirates present | centred (112,268), g14b gold | SEA_PIRATES "%d pirate vessels in this area.", %d = Σ ships of the units in the zone (`prov[0xAE]` list) owned by the pirate country `[0x64e684]` | | `0x50798f..0x507abf` |
| Pirate shields | x = 16 + i·w, y = 282, at most 8 (one per ship count, capped) | `smallshield` of country slot 0x151 | | `0x5079f8..0x507a81` |
| No pirates | centred (112,284), g14b gold | SEA_NOPIRATES | | `0x507913` |
| SEA_PIRB button | centred x=112, y=319 | text button | | `0x507af8..0x507b11` |

Privateer button:
- It starts enabled. It is disabled when:
  - the cost `0x45b2a0(player)` > treasury `player[0xE70]`;
  - pirates are already in the zone;
  - `player[0x1F84]` == 0 (not a naval power).
- Evidence: `0x506ec6..0x506fbb`.
- Tooltip (`0x507ba0`):
  - SEA_NOTNAVAL when not a naval power;
  - SEA_ALREADY when pirates are present;
  - otherwise SEA_PRIV "It will cost %d$ …" with `0x45b2a0`.

---

### 4.10 Army / fleet window (`iw_army.bmp` / `iw_navy.bmp`)

Classes:
- army: ctor `0x4f2de0`, vtable `0x5eb3ac`;
- navy: ctor `0x4f3330`, vtable `0x5eb40c`;
- shared base: ctor `0x4f8740`, draw `0x4f8e00`, layout `0x4f8ed0`.

The unit details are drawn by a sub-object, the **unit panel** (ctor `0x4f65d0`, layout `0x4f6be0`,
draw `0x4f7860..0x4f8739`), created at the window origin by `0x4f9290`.

**SPEC_UI §6 is incomplete.** It has the bitmap list and button x offsets but no positions. Missing
from it:

#### 3.1 Window layer (`0x4f8ed0`, positions)

| Member | Bitmap | Position | Role |
|---|---|---|---|
| +0x27E / +0x282 / +0x286 | army: `infantry%d` / `cavalry%d` / `artillery%d.bmp` (%d = `0x45b8a0(player)`, the tech class 1..3, `0x4f30cc`); navy: `warships` / `galleys` / `transports.bmp` (`0x4f3790`) | (20,143) / (86,143) / (152,143) | unit-type pictures 52x32 |
| +0x28A / +0x28E / +0x292 | `btn_pil_upp` (**NIEPEWNE**: assignment by order) | (36,119) / (102,119) / (169,119) | increase per type (reorganise) |
| +0x296 / +0x29A / +0x29E | `btn_pil_ner` | (36,179) / (102,179) / (169,179) | decrease per type |
| +0x2A6 / +0x2B2 | `btn_pil_left` / `btn_pil_right` | (6,243) / (204,243) | previous / next unit in the stack |
| +0x2AA / +0x2B6 | left / right | (32,22) / (104,22) | **NIEPEWNE** (upper selector, probably leader) |
| +0x2AE / +0x2BA | left / right | (32,242) / (104,242) | **NIEPEWNE** (lower selector in split/merge mode) |
| +0x2A2 | BACK text button (g14) | | |

Other window-layer items:
- **Title** (reorganise mode, `0x4fab80`): UIW_ARMY / UIW_NAVY in g24b, centred x = W/2, y = 250.
  When it is shown is **NIEPEWNE**.
- **List row text** (`0x4fb0ca`): `"%s (%d/%d/%d)"` = name (inf/cav/art).
- **Strip list** (`0x4fb170`): `crb\aiw_bgstripp.bmp` (13 frames) + `aiw_bg_select.bmp`; the unit
  name is centred in arial10 gold above each strip (`0x4fb461`).
- **Army-only button:** BUTTON_KILL_NATIVES (id 0x2D) centred x=112, y=299 when it applies, else
  hidden (`0x4f2fd6`).
- **Navy-only button:** BUTTON_UNLOAD_ARMY (id 0x15) (`0x4f3572`). When there is no space, the
  message NOUNLOADSPACE is shown.
- **Command buttons** (ids 0xC SPLIT_ARMY, 0xD CREATE_DETACHMENT, 0xE CREATE_UNIT, 0xF REORGANISE,
  0x10 DISBAND, 0x14 MERGE): all on **y = 319**, centred at x = 38 / 112 / 186 (three per row) or
  66 / 158 (two per row). The ones that do not apply go to (−1000,−1000).
  - Evidence: `0x4f9d76..0x4fa32d`; x offsets 0x26 / 0x70 / 0xBA / 0x42 / 0x9E.
  - The exact combination per mode is **NIEPEWNE**.

#### 3.2 Unit panel (`0x4f6be0` slots, `0x4f7860` draw)

Positions are window-relative, with flag `[panel+0x2C]` = 0 as used here. With the flag set, all y
values are +20 and the number row moves to y = 0/1.

| Element | Position | Content | Evidence |
|---|---|---|---|
| Strip background `crb\aiw_bgstripp` | (54,11) | unit strip (army `t-armystripp l-%d`, navy `t-navystripp`) | `0x4f796b` |
| Patrol checkbox `btn_kryssruta_small` | (190,21) | ANWPATROL | `0x4f79ac` |
| ANWMORALE "Morale" | centred (152,16), g14 white (`0xFFFF`, **NIEPEWNE** white) | | `0x4f7a85` |
| `morale_bar.bmp` | centred x=152, y=29 | morale fill | `0x4f7a41` |
| Morale value | centred (152,39), gold | `0x48b3b0()` string | `0x4f7ad6` |
| `nameframe.bmp` 96x24 | (44,58) | | `0x4f78f9` |
| Leader / unit name | centred (92,64) | `0x48c040()`[+4] | `0x4f7c87` |
| `rank.bmp` (7 frames 24x24) | (18,58) | rank frame | `0x4f7c9f..0x4f7cf6` |
| `profbig.bmp` + `rankframe.bmp` | (142,58) | leader portrait (hidden at (−100,−110) when there is no leader) | `0x4f7d12..0x4f7d92` |
| `btn_navyleader` / `emptyframe` | (170,58) | leader button / empty frame | `0x4f7f2e`, `0x4f7f53` |
| `attrition.bmp` | (170,72) | attrition level frame; value `"%d"` ·100, centred (190,72) | `0x4f82af..0x4f8305` |
| `maneuver.bmp` | (18,86) | value `"%d"` leader[+0x26], centred (38,86) | `0x4f7db8..0x4f7e05` |
| `fire.bmp` | (56,86) | value `"%d"`, centred (84,87) | `0x4f79db`, `0x4f7e45` |
| `shock.bmp` | (94,86) | value `"%d"`, centred (122,87) | `0x4f79fb`, `0x4f7e85` |
| `siege.bmp` | (130,86) | value `"%d"`, centred (150,87) | `0x4f7ea5..0x4f7ef0` |
| `crb\unitweight.bmp` (army) / `niw_shipload.bmp` (navy) | right edge at 206, y=86 | value right-aligned at (202,87), white: weight (`0x48fad0` etc., `"%d"` or `"%.3f"`); navy NIWNTP | `0x4f7ffb`, `0x4f8674` |
| `numberframe.bmp` ×3 | (18,102) / (84,102) / (150,102) | unit counts per type, centred at frame centre, y = 103 (int(unit[0xE8]) …) | `0x4f7f79..0x4f7fc5`, `0x4f86ae..0x4f8728` |

Tooltip keys:
- ranks RANKCAPTAIN…RANKKING (army) and RANKSEACAPTAIN…RANKLADMIRAL (navy);
- UNITLAND/NAVALSHOCK1/2, FIRE1/2, WEIGHT1/2, UNITSIEGE1/2, UNITMANUEVER1/2;
- UNITLEADERNAME, ANWVIERWLEADER(2), ANW_MORALE_IRO / DRO / DRO2 / DRO3, ANWPATROL, UIW_NOREORG,
  HASESHIP_D.

The numbers above use the colour `0xFFFF` passed straight in as a word. In 16-bit RGB that is white;
**NIEPEWNE**.

---

### 4.8 Diplomacy

(Merged from `layouts_iw2_dip.md`.)

#### Diplomacy: iw_drlw overview, iw_draw action panel, dip_confirmpopup

This covers item 4 of the second batch and will be merged into `layouts_iw2.md`. It records layout
facts only, with evidence addresses, read from the disassembly (`re/dip_a.txt`, symbolic tracker
`re/symx.py`) and the Ghidra export. English texts are from `Config/text.csv`.

Bitmap sizes (from the GOG data):

| Bitmap | Size |
|---|---|
| `iw_drlw.bmp` | 224x344 |
| `iw_draw.bmp` | 224x344 |
| `dip_confirmpopup.bmp` | 300x240 |
| `GFX\settings\btn_radio.bmp` | 64x14, 4 frames of 16x14 |
| `alliance_strip.bmp` | 192x16 |

**`iw_diplomatics.bmp` (224x344) is never used.** The exe contains no reference to it (a string
search returned 0 hits).

#### 0. Objects

Everything is **one object**, built by ctor `0x50a3b0` (created once in the main interface at
`0x524fc2`), vtable `0x5ebd9c`:
- +0x04 events `0x50aa60`;
- +0x0C draw `0x50af10`;
- +0x3C open `0x50b520`;
- +0x40 close `0x50b730`;
- +0x44 list hit test `0x50ea90`;
- +0x48 tooltip `0x50ec70`.

Members:

| Member | What it is |
|---|---|
| +0xAE | radio group with 2 `btn_radio` (`0x427700`): index 0 = "View Alliances", 1 = "View Relations"; `[+0xAE]+0x10` = selected index |
| +0xBA | text button BTNREL "Relation" (g14) |
| +0xBE | text button BTNNAT "Nation" (g14) |
| +0xC4 | text button "OK" (g14b, id 0xD) |
| +0xC8 | CANCEL (g14b) |
| +0xCC | BACKTOMAIN "Back to Main" (g14b) |
| +0xD0 | DWGOTO "Center Map..." (g14b, id 0x67) |
| +0xD4 | bitmap object, used for the confirm popup |
| +0xB2 | action list (iw_draw) |
| +0xB6 | overview list / alliance text box |
| +0xDA | target country (0 = none) |
| +0xDE | pending action id |
| +0xE2 | popup state (0 = closed, 1 = open) |
| +0xC2 | sort flag |

The ctor also registers itself as handler for buttons-table ids 0x1D–0x25 and 0x27 (`0x51f750`,
`0x50a83b..`).

The **action panel** is a second 224x344 surface, `[this+0x82]+0x22DE`. Its background is set to
`iw_draw.bmp` (`0x50c8f0`, `0x50ab2a`). It is placed at x = window x + window width, i.e. at
**screen (224,103), right of the sidebar over the map** (`0x50c980`: setpos(W + winx, winy),
Ghidra lines 85–87). What `[this+0x82]` is exactly is **NIEPEWNE** (it is used both as the player
country and as the owner of that panel).

#### Flow

1. The window opens (`0x50b520`):
   - sidebar title HEAD_DIP "Diplomatic Actions" + player name;
   - selected country (`[0x805f9c]`) = the player;
   - target `+0xDA` = 0;
   - popup closed.
   If the province last selected on the map (`[0x61f418]`) has an owner that is not the player and
   that the player has contact with (byte `[0x8077d4 + owner[0x1F7E]·0x4A0 + player[6] + 0x256]`),
   the action panel opens immediately for that owner (`0x50b603..0x50b6e9`). In every case the map
   switches to mode 5 (relations) or 6 (alliances), depending on the radio (`0x478950`).
2. Clicking a province on the map (`0x50ad5a` hit test `0x533140`, owner `[prov+0x3C4]`):
   - if `0x50c120(owner)` (not the player and in contact) → `0x50c8f0(owner)`: selected country =
     owner, iw_draw panel built with the action list (`0x50adbf`);
   - otherwise the action panel closes (`0x50b770`).
   In both cases the map mode and the overview list are refreshed (`0x50adf4`, `0x50adfb`).
3. **Radio** View Alliances / View Relations (`0x50aac1`): sets map mode 6/5, rebuilds the overview
   list (`0x50c1b0`) and, if a target is selected, the action list (`0x50ab30`).
4. **BTNREL / BTNNAT** are visible only in "View Relations". They are sort headers of the relations
   list: a click toggles the sort and rebuilds the list (`0x50ab8d`, `0x50abc7`).
   - Comparator `0x50c700` / `0x50c6a0` when BTNNAT is pressed, `0x50c580` / `0x50c610` otherwise.
   - The second choice of each pair depends on flag `+0xC2`.
   - That BTNNAT means sort by name and BTNREL sort by relation value is **NIEPEWNE**.
5. Clicking an action in the list dispatches on the id (`0x50e400`, jump table `0x50e7e0`):
   - **direct**:
     - Royal Marriage offer/break (0/1): `0x441140(1)` / `0x441460`;
     - Offer Vassalization (2): `0x441140(2)`;
     - Break/Cancel Vassalization (3, 4): `0x4414c0`;
     - Annex (5): `0x441040`;
     - gifts (0x12–0x14): `0x441760`;
     - Warning: `0x43e3a0`;
     - Insult: `0x43e2b0`;
     - Claim: `0x43e580`;
     - Guarantee: `0x43e490`;
     - Ask Access: `0x43ea30`;
     - Revoke Access: `0x43ed00`;
     - Cancel Access: `0x43edf0`;
     - Cancel Trade: `0x43e760`;
     - Offer Trade: `0x43e670`;
     - Allow Trade (0xF): `0x443cd0`.
   - **opens another window** (returned to the main interface): Offer Peace (7) → window id 0x10
     (peace); Offer Loan (0x11) → window id 0x13 (loans/gift).
   - **confirmation popup first** (`0x50b7a0(1)`): Declare War (6), Invite (8), Create Alliance (9),
     Join (10), Ban (11), Leave Alliance (12), Exchange Discoveries (13), Trade Embargo (0x10).
     Invite, Ban, Join and Leave first look up the relevant alliance in the alliance list
     `[0x8ff602]` (type 4 = military alliance) and abort if there is none (`0x50e67b..0x50e7af`).
6. **Popup**:
   - OK executes `0x50e360(target, id)` (`0x50accf`). For ids 9 and 10, OK is disabled when
     `player.table→0x4cc110()` is true (presumably "already in an alliance", **NIEPEWNE**,
     `0x50ac57..0x50ac9a`).
   - CANCEL closes the popup (`0x50ad0a`). When `player[0x2274]` is set, CANCEL closes the whole
     window instead (**NIEPEWNE** what that flag is).
7. **BACKTOMAIN** with the popup closed → `0x5271a0(0)` returns to the main sidebar (`0x50ac28`).
   **DWGOTO** (shown only with a target) centres the map on the target (tooltip DIWROLL_GOTO
   "Centers map over %s").

#### Overview window `iw_drlw` (sidebar, window-relative)

Drawn by `0x50af10` onto the screen surface. Colours: orange `#FFC000` (`0x50af31`), white.

| Element | Position | Align / font / colour | Content | Evidence |
|---|---|---|---|---|
| Radio buttons | (26,20) and (26,38) | `btn_radio` 16x14 | 0 = alliances, 1 = relations | `0x50b520` (x + 0x1A, y + 0x14 + 18·i) |
| DW_VIEW_ALL "View Alliances" | (44,22) | left, g14 orange | | `0x50affe` |
| DW_VIEW_REL "View Relations" | (44,40) | left, g14 orange | | `0x50b02c` |
| Header label | (112,73) | centred, g14 gold `#FFBD00` | DIP_CURR_ALL "Current Alliances of" / DIP_CURR_REL "Current Relations of" | `0x50b28e..0x50b2dc` |
| Country name | (112,87) | centred, g24b, colour 0xFFFF (white) | selected country name (`0x64e140[tag]+0x12`) | `0x50c760` |
| Two shields | (22,73) and (174,73) | `smallshield_<tag>` (4 frames) | selected country | `0x50c760` (x−90, x+62) |
| Reputation line (alliances view, no target) | (112,116) | centred, **arial10 white** | BADBOY0..9 by r = player[0xE42] / `0x45aef0()`: r<0.01 → 0, <0.1 → 1, <0.2 → 2, <0.4 → 3, <0.6 → 4, <0.8 → 5, <1.0 → 6, <1.2 → 7, <1.4 → 8, else 9. Tooltip `"%.1f/%d"` (badboy / limit) | `0x50b114..0x50b246`, `0x50ec43` |
| Relation line (alliances view, target set) | (112,116) | centred, g14 orange | DW_OURREL "Relation towards you: %c%d", sign `+`/`-` and abs(relation); relation = player table entry of the target (`0x4cdf70`). Tooltip `"%s: %.1f/%d"` (target name, target badboy / limit) | `0x50b090..0x50b10f`, `0x50ec05` |
| BTNREL "Relation" (relations view) | (24,115) | text button g14 | sort header | `0x50b5e7..0x50b5fd` |
| BTNNAT "Nation" (relations view) | x = 78 + w/2, y = 115 | text button g14 | sort header (the formula is as coded, **NIEPEWNE**) | `0x50b5bd..0x50b5e4` |
| List / text box | (24,135), 158x139 | | alliances view: text box (class `0x5ebc88`, white) filled by `0x509040` / `0x509420` / `0x509a20`; relations view: one row per country | `0x50c1b0` |
| "(No Alliances)" | (112,135) | centred, g14 white | NOALLIANCES, when the alliance box is empty | `0x50b25a..0x50b289` |
| DWCLICK1 / DWCLICK2 | (112,292) / (112,306) | centred, g14 orange | "To select a nation, left-click" / "on map." (popup closed only) | `0x50b4ae`, `0x50b4de` |
| BACKTOMAIN | centred x = 112 (no target) or x = 66 (target set), y = 319 | g14b | | `0x50b42b..0x50b482` |
| DWGOTO | centred x = 158, y = 319 | g14b | drawn only with a target | `0x50b2e1`, `0x50b3c4..0x50b3f2` |

Relations-list row (class `0x5ebd58`):
- Width 158 (`0x50a260`), height = font height.
- Value: `"%d"`, or `"+%d"` when > 0, right-aligned at row x + 52.
- Value colour by relation v:

  | Relation v | Colour |
  |---|---|
  | ≥ 175 | `#006000` |
  | ≥ 125 | `#00A400` |
  | ≥ 75 | `#80FF00` |
  | > −75 | `#E0E0E0` |
  | > −125 | `#E08000` |
  | > −175 | `#A04000` |
  | otherwise | `#800000` |

- Name: left at row x + 78. White, or gold `#FFBD00` when country byte [0x11E] is set (its meaning
  is **NIEPEWNE**). Evidence `0x50a270`.
- Rows include every country with [0x1386] > 0 that the selected country has contact with
  (`0x50c1b0`, Ghidra lines 193243–193254).

Alliance text box lines (alliances view), in the order of the three fillers. Keys with their
evidence addresses:

| Filler | Keys |
|---|---|
| `0x509040` | DIRM `0x508e09`, DILORD `0x508e5d`, DIVASSAL `0x508e64`, DIMIL `0x508e8b`, DIWAR `0x508eed`, DIWARALONE `0x508f3a`, DIAGAINST `0x508f6b`, DITRUCE `0x5090c4`, DIACCESS `0x50921a`, DIACCESS2 `0x509335`; members joined with `", "` / DIAND (`0x508d0a`, `0x508d13`) |
| `0x509420` | DIGUAR, DIGUAR2, DIWARNEDBY, DIWEPERMCB, DIWETEMPCB |
| `0x509a20` | DITRADEAGREE, DIWEREFTRADE, DITHEYREFTRADE |

Expiry tooltips ALL_EXPIRE / ALL_VASSAL and `alliance_strip.bmp` 192x16 belong to the row helper
`0x5083d0` (`0x50845e`, `0x50886c`, `0x5088b5`). The exact line layout is **NIEZNANE**.

#### Action panel `iw_draw` (panel-relative; the panel sits at screen (224,103))

Built by `0x50c980(target)`:

| Element | Position | Align / font / colour | Content | Evidence |
|---|---|---|---|---|
| DIP_SENDDIPLO "Send Diplomat to" | (112,17) | centred, g14 gold | | `0x50e209` → `0x50c760(tag, 0x70, 0x11)` |
| Target name | (112,31) | centred, g24b white | | `0x50c760` |
| Sub-label | (112,55) | centred, g14 gold | DIP_MODREL "to modify relations" (relations view) / DIP_MODALL "to modify alliances" (alliances view) | `0x50d0dc`, `0x50e1f0` |
| Shields | (22,17), (174,17) | smallshield of the target | | `0x50c760` |
| Action list | (32,72), 148 x (panel height − 112 = 232) | rows g14 | items below | `0x50c9e3..0x50ca56` |

Action item (class `0x5ebcc8`, `0x509eb0(id, text, enabled)`):
- width 148, height = font height;
- text left-aligned;
- colour `#FFC600` normal, white under the mouse, grey `#7F7F7F` when disabled (`0x509fc0`).

Separator (class `0x5ebdf4`): height 8, an orange `#FFC000` horizontal line at mid-height
(`0x41f320`, Ghidra lines 193189–193202).

#### 3a. Relations view (radio = 1)

Items, top to bottom:

| id | Key (text) | Shown | Enabled when | Evidence |
|---|---|---|---|---|
| 0x12 | DIP_STATE_GIFT "Send State Gift (%d$)" | always | `0x45b7e0(0x12, tag)`: not at war, cost ≤ treasury player[0xE70], and `0x45c060` | `0x50caa7` |
| 0x13 | DIP_PERSONAL_GIFT "Send Personal Gift (%d$)" | always | as above with the personal cost | `0x50cb79` |
| 0x14 | DIP_INTRODUCTION "Send Letter of Introduction (%d$)" | always | as above with the introduction cost | `0x50cc4b` |
| — | separator | | | |
| 0x15 | DIP_WARNING "Send a Warning (%d$)" | always | `0x45b320(tag)` | `0x50cdc8` |
| 0x16 | DIP_INSULT "Send an Insult (%d$)" | always | `0x45b2c0(tag)` | `0x50ce90` |
| 0x17 | DIP_CLAIM "Claim their Throne (%d$)" | always | `0x45b420(tag)` | `0x50cf58` |
| 0x18 | DIP_GUARANTEE "Proclaim Guarantee (%d$)" | always | `0x45b3b0(tag)` | `0x50d020` |

Costs (`%d$` = int(cost)):
- cost = k · B with B = `0x45afc0(target)`.
- k = 250 for state gift and claim, 100 for personal gift, warning and guarantee, 25 for
  introduction, 10 for insult (`0x45b120` / `0x45b140` / `0x45b160` / `0x45b100`; constants
  `0x5e5840` = 250, `0x5e5650` = 100, `0x5e564c` = 25, `0x5e50ac` = 10).
- B = (1 + inflation) × f, clamped as below:
  - s1 = clamp(player[0x1386] × 0.004, 0.2, 2);
  - s2 = clamp(target[0x1386] / player[0x1386], 0.2, 2), used only if player[0x1386] > 1,
    otherwise s2 = s1;
  - f = clamp(s1 · s2 · (1 + 0.1·(DIP_a − DIP_b)), 0.33, 3), where DIP is the monarch diplomacy
    skill `0x43bf40` of the two countries (which one is a and which b is **NIEPEWNE**).

#### 3b. Alliances view (radio = 0)

Items, top to bottom. "table" is the player relation table `player[0x38E] + tag·0x36` and the
offsets are absolute.

| id | Key (text) | Shown when | Enabled when | Evidence |
|---|---|---|---|---|
| 6 | DIPWAR "Declare War" | not at war (table + 0x36 == 0) | player vfunc +0x378(tag) | `0x50d1d6` |
| 7 | DIPPEACE "Offer Peace" | at war | `0x45d620(tag)` | `0x50d138` |
| — | separator | | | |
| 0 | DIPOFFDYN "Offer Royal Marriage" | no royal marriage (+0x3B == 0) | `0x45d1e0` | `0x50d3e9` |
| 1 | DIPBRKDYN "Break Royal Ties" | royal marriage | `0x45c060` | `0x50d34a` |
| 4 | DIPCANVAS "Cancel Vassalization" | the player is the target's vassal (`0x4cbe90` on the target table) | `0x45b180` | `0x50d4aa` |
| 2 | DIPOFFVAS "Offer Vassalization" | the target is not the player's vassal | `0x45bf60` | `0x50d604` |
| 3 | DIPBRKVAS "Cancel Vassalization" | the target is the player's vassal | `0x45c060` | `0x50d565` |
| 5 | DIPANNEX "Annex" | always | `0x45bdc0` | `0x50d6a1` |
| 0x19 | DIPAACCESS "Ask for Military Access" | the player has no access (+0x41 == 0) | `0x45b530` | `0x50d75c` |
| 0x1B | DIPCACCESS "Cancel Military Access" | the player has access | `0x45b5d0` | `0x50d7fb` |
| 0x1A | DIPRACCESS "Revoke Military Access" | additionally, the target has access through the player (target table, player tag) | `0x45b580` | `0x50d8c0` |
| — | separator | | | |
| 9 | DIPCNALL "Create New Alliance" | always | `0x45bd00` | `0x50da12` |
| 0xC | DIPBRKALL "Leave Alliance" | always | `0x45cdc0` | `0x50daaf` |
| 0xB | DIPBANALL "Ban from Alliance" | always | `0x45cf60(0x4cc4c0(tag), …)` | `0x50db5f` |
| 8 | DIPINVAL "Invite to Alliance" | always | `0x45f9b0(tag)` | `0x50dbfc` |
| 10 | DIPJOINALL "Join Alliance" | always | `0x45f9b0(player)` and `0x45c060(tag)` (sets player[0xE56] around the call) | `0x50dcd6` |
| — | separator | | | |
| 0x10 | DIPRTRADE "Trade Embargo" | no embargo (+0x39 == 0, `0x4cbf20`) | player vfunc +0x37C(tag) | `0x50dee7` |
| 0xF | DIPATRADE "Allow Trade" | embargo in force | player vfunc +0x380(tag) | `0x50de45` |
| 0x1D | DIPOFFTRADE "Offer Trade Agreement" | no trade agreement (+0x40 == 0) | `0x45b4a0` | `0x50e039` |
| 0x1C | DIPCANTRADE "Cancel Trade Agreement" | trade agreement | `0x45b500` | `0x50df9e` |
| 0xD | DIPEXCDIS "Exchange Discoveries" | always | `0x45c060` | `0x50e0d6` |
| 0x11 | DIPOLOAN "Offer Loan" | always | `0x45c060` | `0x50e173` |

**Diplomat check `0x45c060(tag)`.** This is the common base condition. It returns false when any of
these holds:
- player[0xE98] (diplomats) < 0.99, unless flag player[0xE56] is set;
- the target id is 0 or in 0x14F..0x153;
- the target slot is empty;
- there is no contact;
- the target is the player;
- or `0x417220(date)` is true, which is presumably the "diplomat en route" cooldown
  (**NIEPEWNE**).

Tooltips (`0x50ec70` → `0x50ee80(id)`):
- Every item has a DIPROL_* explanation. Rule texts come from the same keys: DIPROL_VASSAL1 /
  DIPROL_ANNEX1 (preconditions), DIPROL_WAR1–4, DIPROL_PEACE1/2, DIPROL_INV0–3, DIPROL_CNA0–2,
  DIPROL_JOIN0–3, DIPROL_BAN0/1, DIPROL_BRKMIL0–2, DIPROL_RELIGION0/1, DIPROL_DYN_VASSAL,
  DIPROL_REFT0/1 and others (`0x50eed3..0x50f7e9`).
- No diplomat: DIPROL_DIPS_INSTANT "(No diplomats available)" / DIPROL_DIPS_DELAYED.
- Cooldown: DIPROL_NO_INSTANT "(Diplomat en route)" / DIPROL_NO_DELAYED with the date
  (`0x50eb01`, `0x50eb72`, `0x50ecb4`, `0x50ed3b`).
- Radios: DIWROLL_ALL_I / DIWROLL_REL_I; elsewhere DIWROLL_ALL_D / DIWROLL_REL_D; BACKTOMAIN:
  DIWROLL_BACK.
- The exact id → key map lives in `0x50ee80` and was not decoded line by line (**NIEPEWNE**).

#### 4. Confirm popup `dip_confirmpopup.bmp` (300x240)

Opened by `0x50b7a0(1)`:
- **Position:** x = window x + window width − 150 = **74**, y = window y + (344 − 240)/2 = window
  y + **52**, i.e. screen (74,155). It straddles the edge between the sidebar and the action panel
  (`0x50b812..0x50b861`).
- **Contents:** `0x50b880`. Coordinates below are popup-relative; the content is drawn into the
  popup surface.

| Element | Position | Align / font / colour | Content | Evidence |
|---|---|---|---|---|
| Title | (150,54) | centred, g24b, 0xFFFF white | DWMLORD "Please Confirm" | `0x50bffe..0x50c01b` |
| Message text box | (50,80), 200 x (240 − 130 = 110) | g14 white, word-wrapped | message for the pending action (below) | `0x50b8b8..0x50b922`, `0x50c0b9` |
| Line 1 | (150,174) | centred, g14 white | DWAREYOUSURE1 "(Effects are immediate and" | `0x50c063` |
| Line 2 | (150,188) | centred, g14 white | DWAREYOUSURE2 "cannot be undone.)" | `0x50c0ad` |
| CANCEL | (25,211) | text button g14b (top-left) | closes the popup | `0x50b363..0x50b36a` (popup x + 0x19, y + 0xD3) |
| OK | (240,211) | text button g14b | executes the action | `0x50b33d..0x50b34b` (popup x + 0xF0, y + 0xD3) |

While the popup is open, BACKTOMAIN is moved off-screen to (−100,−100) (`0x50b354`).

Messages (jump table `0x50c0e8`, indexed by id − 6):

| id | Message |
|---|---|
| 6 War | AWDW1 "We are about to declare war on %s." (`0x50ba72`). If the stability loss S > 0: + AWDWLS "We will lose %d stability" (`0x50ba93`), followed by one clause per cause that applies, in this order: |
| | • AWDWCB "<esc>2 for lacking a Casus Belli" (+2): no permanent CB (table+0x33) and the CB-object test is true (`0x50bb12`); |
| | • AWDWRM "<esc>1 for breaking a Royal Marriage" (+1): +0x3B (`0x50bb6e`); |
| | • AWDWMA "<esc>2 for breaking a Military Alliance" (+2): +0x3C (`0x50bbca`); |
| | • AWDWVA "<esc>3 since they are our Vassals" (+3): `0x4cbe90` (`0x50bc22`); |
| | • AWDWREL "<esc>1 same true faith" (+1): `0x45fbb0` (`0x50bc71`); |
| | • AWDWQUAT "<esc>5 since we have a peace agreement" (+5): `0x45f5d0` (`0x50bcc3`); |
| | • AWDWFRIE2 (+2) if relation > 150, else AWDWFRIE1 (+1) if relation > 100 (`0x50ba1f`, `0x50ba55`, `0x50bd2d`, `0x50bd69`). |
| 8 Invite | AWINV "We will invite %s to the MILITARY ALLIANCE we have with " + member list (`0x50bed7`, `0x50bf0b`) |
| 9 Create | AWCNEW "We will create a MILITARY ALLIANCE with %s." (`0x50be8c`) |
| 10 Join | AWJOIN "We will ask %s if we can be allowed to join their" + DIMIL / members (`0x50bf6a`, `0x50bf87`) |
| 11 Ban | AWBANALL (`0x50be7b`) |
| 12 Leave | AWLVALL (`0x50be6a`) |
| 13 Exchange | AWEXCHANGE with two counts of provinces/sea zones unknown to the other side (`0x50be43`, `0x426ec0`) |
| 0x10 Embargo | AWRTRADE (`0x50bdb1`) |

Texts carry a colour escape: byte 0x96 (octal 226) followed by a letter or digit, e.g. 0x96 "Y" (yellow), 0x96 "W" (white), 0x96 "2" (`text.csv` AWDWCB = ", 2 for lacking..."). What the colour codes mean is **NIEPEWNE**.

**Acceptance chance:** **there is no acceptance-chance text** in this window, its list items or the
popup. No key or format string for a probability is referenced anywhere in `0x5083d0..0x50fa70`.
The only numbers shown are the gift/action costs (`%d$`), the stability loss (AWDWLS), the
relation (DW_OURREL, list rows) and badboy/limit (`%.1f/%d` tooltips). If the original shows a
chance, it is elsewhere (**NIEZNANE**).

## 5. Diplomacy

- Diplomacy list windows `iw_drlw` (`0x50a3b0`: buttons BTNREL "relations", BTNNAT "nations",
  BACKTOMAIN, CANCEL, DWGOTO) and `iw_draw` (`0x50b520`, `0x50c8f0`).
- Action buttons (text buttons drawn with `tb_*` pieces, 16+8n+16 x 38) are the `press`
  entries BUTTON_DECLARE_WAR*, OFFER_PEACE, BAN_FROM_ALLIANCE, VASSAL, MILITARY, DYNASTIC,
  CANCEL_VASSALIZATION, OFFER_LOAN, GIFT with tooltips BUTTON_0065..0082/0095/0096. Positions:
  centred on the window at x = 112 (and 0x26/0xBA, i.e. 38 and 186, for two-column rows) — from
  call sites `0x4f9da5`, `0x4f9dea`, `0x4fa02f`, `0x4fa3a3`; exact y NIEZNANE.
- Confirmation popup `dip_confirmpopup.bmp` 300x240 (`0x50b7a0`); incoming proposal popup
  `diplopup.bmp` 40x21 + `dipstrip.bmp` 208x16 (13 icons of 16x16) with INVACCEPT / INVDECLINE
  (`0x520650`); `alliance_strip.bmp` 192x16 (`0x5083d0`).

## 6. Army / fleet window

`iw_army` (`0x4f2de0`), `iw_navy` (`0x4f3330`); detail/reorganisation in `0x4f65d0`, `0x4f8740`.
Bitmaps: `reorganisation\nameframe` 96x24, `numberframe` 56x14, `rankframe` 24x24,
`morale_bar` 56x8, `moralestatusbar` 56x10, `infantry/cavalry/artillery1..3` 52x32,
`warships/galleys/transports` 52x32, `fire/shock/maneuver/siege` 36x14, `attrition` 144x12,
`btn_navyleader` 144x11, arrows `btn_pil_left/right` (16x20 x4), `btn_pil_upp/ner` (20x20 x4),
`profbig/profsmall`, `rank.bmp` 168x24 (7 ranks of 24x24), `crb\aiw_bgstripp` 624x44,
`crb\t-armystripp l-%d` / `t-navystripp` (unit strips), `btn_kryssruta_small` (checkbox 16x16 x4).
Unit-command buttons (generic text buttons) BUTTON_SPLIT_ARMY, REORGANISE_ARMY, CREATE_UNIT,
DISBAND_ARMY, ASSAULT, DETACH_SIEGE, CREATE_COVER, MERGE, UNLOAD_ARMY, KILL_NATIVES are centred at
window x = 112 (one per row) or at 66 / 158 (two per row: offsets 0x42 / 0x9E) — `0x4f3449`,
`0x4fa02f`; buttons that do not apply are moved to (−1000,−1000) (`0x4f2fea`). Rows' y: NIEZNANE.

## 7. Event / message window

`0x56bce0`. A `win8` popup the size of `bgbox_event.bmp` (360x328) at the cascaded default
position (§1). Window-relative:

| Element | Position |
|---|---|
| `bgbox_event.bmp` (and `scroll_open.bmp`, which is **missing** from the data — load fails silently) | (0,0) |
| Title | centred x=180, y=24, garamond24bold, `#FFBD00` |
| Short lines (date etc.) | centred x=180, y = 68 + n·line_height, garamond14 white (wrapped to 200 px) |
| `btn_scroll.bmp` (260x180 frames x4: cushion with scroll) | (42,123) — clicking opens the full text |
| Full text box | 161x189 at (80,68), 10 lines per page |
| Page left `btn_eventpage_left` (28x16 x4) | (142,229) |
| Page number box `box_pagenumber.bmp` 20x18 | (172,229) |
| Page right `btn_eventpage_right` | (204,229) |
| EVENTWINDOW_CLOSE ("Close") text button | centred x=180, y=294; or at (190,294) when GOTO exists |
| EVENTWINDOW_GOTO ("Goto") text button | right edge at x=170, y=294 (only if the event has a location) |

The multiple-choice historical event dialog (actions A–D): NIEZNANE — `iw_eventinfo.bmp`
(360x304) and `iw_event.bmp` are never loaded by v1.09, so the choices are rendered in this same
window type or by another class not yet located.

## 8. Frontend

Backgrounds (800x600, `0x58efd0`): `bg_start` (title), `bg_frontend` (menus), `bg_tutorial`,
`bg_valkyrienet`, `bg_winner`, `bg_loser`; `bg_multiplayer` 404x234 panel.
Buttons: `btn_frontend.bmp` 544x32 (4 frames 136x32), `btn_frontend_big.bmp` 704x32 (176x32)
with drop shadows `btn_frontend_shadow` / `_big_shadow` (P, drawn first, offset NIEPEWNE +4,+4);
boxes `box_doublesize` 176x74, `box_triplesize` 176x115 (+ shadows); `editbox.bmp` 88x23;
dropdown `bg_dropdown_1/2` + `btn_dropdown_1/2`; radio/tabs `btn_radio_flik(_2)` 368x24
(4x 92x24); `plate.bmp` 280x448 (scenario list plate, `0x58c090`); `shield_flare.bmp`
12000x120 (100 frames of 120x120 — animated glow behind the country shields), `shield_shadow`.
Scrollbars: `frontend\scrollbar_*`, `menu\scrollbar_*` (64x18 = 4 frames of 16x18; `_middle`
16x18 tiled).

Main menu buttons are centred at **x = 400** (`0x57e160`, `0x57d4f0`, `0x586c60`: position =
400 − width/2); y values NIEZNANE (`0x57e160` starts at 444 for one button).

Single-player setup (manual p.6 screenshot, coordinates NIEPEWNE except where marked):
scenario list on the left plate, saved games below it; country shields in a row along the top
(right-click a shield for other countries); right column INFO / VICTORY / OPTIONS / BACK / START.
Options box `box_options.bmp` 516x132 at **(238,146)** with ACCEPT at **(374,305)** and BACK at
**(520,305)** (`0x58aaa0`, confirmed); option rows: difficulty, AI aggressiveness, game speed, fog
of war, forced annex, dynamic missions, base victory points, autosave (manual p.5).

Credits: `frontend\credits\back.bmp` 640x440 + `bar.bmp` 640x20 (`0x57dbe0`).
Loading screen: `gfx\load_800.bmp` / `load_1024` / `load_1280` full screen (`0x41a890`);
`loadingbar*.bmp` are never loaded by name (NIEPEWNE: unused, progress drawn otherwise).

## 9. In-game options, save/load, message settings

- In-game menu (`0x55f560`): `gfx\settings\setupscreen_bg.bmp` 352x450 at a computed origin
  (globals `0x8c0aec/0x8c0af0`, NIEPEWNE: centred on screen); six `btn_frontend_big` buttons
  centred horizontally on the panel; opening it pauses the game.
- Save/load (`0x4226c0`, `0x423280`): `btn_saveload.bmp` 464x32 (4 frames 116x32), a 260x128
  list box with `menu\scrollbar_*`; buttons centred at panel offsets 240 and 114 (two columns).
- Message settings (`0x5760d0`, `0x576930`): `bg_settings_listbox.bmp` 272x276, per message type
  three toggles `msg_setting_box` (popup box), `msg_setting_log` (log line), `msg_setting_pause`
  (pause) 16x10, radio `gfx\settings\btn_radio.bmp` 64x14 (4 frames 16x14). The message log
  (`0x577080`, global `0x8c931c`) uses `Message_Ena_Disa.bmp` 64x16 (enable/disable toggle) and a
  text box with 5 lines; its on-screen position NIEZNANE (manual: right-click a message box or the
  log to choose how that message type is displayed).

## 10. Cursors

Loaded in `0x566840` (`0x4ca230` = load cursor). All 56x41 RGB, green transparent; hotspot
NIEZNANE (probably top-left of the arrow tip — check `0x4ca230`/`0x4ca5d0`).

| Bitmap | Mode |
|---|---|
| cursor_select | default / select |
| cursor_click | over a clickable control |
| cursor_move_army / cursor_move_navy | move order |
| cursor_attack_army / cursor_attack_navy | move into enemy |
| cursor_explore_army / cursor_explore_navy | move into terra incognita |
| cursor_enterport | fleet into port |
| cursor_load / cursor_unload | embark / disembark |
| cursor_send_colonist / cursor_send_merchant | dispatch modes |
| cursor_illegal | illegal target |
| cursor_small | small variant (NIEPEWNE: during drag) |
| cursor_wait (56x56) | not loaded by name — unused |

## 11. Other resolutions

The sidebar does **not** scale: it is 224 px wide and fixed at x=0 at every resolution; header
`sidebar_header2` and bottom `sidebar_bottom2` replace the 800x600 versions when width ≠ 800,
bottom still at y=447, minimap still at (32,504), sidebar buttons at the same absolute positions
(`0x52de10` uses constants). The top-bar contents are shifted by x0 = (W − 800)/2. Everything else
on the right is map. Ledger has resolution-specific frames `EYR_bg_top/bottom_1024/1280`; loading
screens `load_1024/1280`. For the Amiga AGA 640x480 target this means the UI must be re-laid out
(sidebar rescaled, top bar 560 → ~416 wide) — the original has no 640x480 mode.

## 12. Bitmaps never loaded

Not referenced anywhere in `eu2.exe` v1.09 (safe to ignore): `mainmap_bar`, `button area\btn_ok`,
`storm` (interface), `Pause\PauseBtn`, `pause_button` (root copy), `shouldsend`, `confirm`,
`tutorialarrow`, `StrippKryss`, `icon_shieldframe`, `shield_semitransparent`, `gul_linje`,
`borderlinje`, `line_hor_271`, `line_ver_77`, `bg_slider` (interface copy), `SZIW_good/bad`,
`iw_event`, `iw_eventinfo(_anim)`, `iw_military`, `iw_diplomat`, `iw_merchant`, `iw_eco_aid`,
`iw_seaice`, `iw_missioninfo`, `Eyr\eyr_background`, `mynt`, `treasury.bmp`, `coin.bmp`
(interface; the font one in `fonts\Additional` is used), `cursor_wait`, `loadingbar*`,
`sidebar_banner` (12 px), `sidebar_banner3`, PIW `city.txt` (no province uses it), `piwbg_forest2`
(NIEPEWNE). `scroll_open.bmp` is loaded but absent from the data.

## 13. Key addresses

| Address | What |
|---|---|
| `0x524840` | main interface ctor (all info windows, top-bar icons, statusbars, layers) |
| `0x526340` | main interface layer draw: id 2 sidebar contents, id 5 top bar + tooltip |
| `0x527490` | main interface event handling / button enable logic |
| `0x52de10` | sidebar button positions |
| `0x5665e0`, `0x566b00`, `0x567280` | sidebar frame ctor, bitmap choice per resolution, draw |
| `0x566840` | cursor set |
| `0x47b270`, `0x47b5f0` | minimap ctor / draw at (32,504) |
| `0x428170`, `0x428740`, `0x428810` | win8/win16 frame tiles, framed popup window (clamp ≥224/≥32) |
| `0x423810` | text-button pieces `tb_*` / `tbsmall_*` (`%s_plain/over/down/dis_l/c/r`) |
| `0x51f310`, `0x51f4d0`, `0x51fad0`, `0x62a618` | buttons.txt loader, button parser, id→index table |
| `0x40ad40` | draw-layer registration (object, priority, id) |
| `0x502830`, `0x503ff0`, `0x504ed0`, `0x484380` | PIW ctor, PIW draw, PIW background/buttons, PIW layout file reader |
| `0x5148a0` | info-window base class |
| `0x51c180`, `0x517ac0`, `0x427aa0`, `0x560f40` | budgeting, domestic policy, slider, slider with ± buttons |
| `0x56bce0` | event/message window |
| `0x55f560` | in-game menu |
| `0x58aaa0`, `0x58efd0`, `0x57e160` | frontend options box, frontend backgrounds, frontend menus |
| `0x4ca110` / `0x4ca120` | screen width / height |

## 14. Open questions

1. Choice dialog for historical/random events (actions A–D): which class, which bitmaps.
2. Exact y rows of unit-command and diplomacy action buttons (§5, §6) and slider rows in domestic
   policy (§4.2) — read the draw methods (vtable slot +0x0C of each window class).
3. Value at top-bar x=684 next to the soldier icon: manpower? (check field `+0xEB2` of the country).
4. Which PIW number (x=74 / x=153) is which; position of bailiff/mayor/sheriff buttons in the PIW.
5. Cursor hotspots; message-log position and size on screen.
6. Frontend main-menu button y coordinates and the scenario-selection layout (only the options box
   is confirmed).
7. Whether lower layer priority really means "drawn first" (consistent with every overlap seen).
8. Shadow-pixel darkening amount for palette index 1.
