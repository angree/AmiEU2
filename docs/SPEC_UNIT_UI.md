# EU2 v1.09 — main-map unit interaction (selection, orders, route display)

Clean-room spec from `eu2.exe` (read in objdump, `/home/angree/full.txt`). No code copied; addresses are
evidence. **NIEPEWNE** = inferred, **NIEZNANE** = not found. Coordinates: "screen" = 800x600 pixels,
"map" = map pixels at zoom 0. Zoom shift `z` = `[0x7fb8cc]` (0..2); one screen pixel = `2^z` map pixels.
Conversions: screen→map `0x4766d0`, map→screen `0x476760` / `0x476810`, map point→province `0x4762d0`.

The main interface object (`[0x8bfa50]`, ctor `0x524840`) **is** the human player's country object
(SPEC_UI_WINDOWS4 §0). Fields used below: `+0x63c8` selection list, `+0x63d8` picked unit, `+0x63dc`
picked province, `+0x63d0` current sidebar window id, `+0x6400/+0x6404/+0x6408` drag start x/y/active,
`+0x1248` (list head `+0x124c`) = the player's own unit list.

Unit virtuals that matter: `+0x14` isArmy, `+0x18` isNavy (army vtable `0x5e8910`, navy `0x5e8b14`),
`+0x3c` show route, `+0x40` hide route, `+0x8c` a size/strength value used to pick in a stack
(**NIEPEWNE** meaning). `unit+0x10` = map sprite, `unit+0x0c` = province, `unit+0xde` = owner,
`unit+0x56` = "route visible" byte (= selected), `unit+0x9a` = route object, `unit+0xc4/+0xc8` = path
(province list).

---

## 0. Mouse events (window proc `0x5b7a80`, poll `0x4c80a0`)

| Win32 | Event type | Notes |
|---|---|---|
| WM_LBUTTONDOWN | 0x11 | sets L-held flag `0x89ad34` |
| WM_LBUTTONUP | 0x0E | clears L-held |
| WM_LBUTTONDBLCLK | 0x13 | |
| WM_RBUTTONDOWN | — | only sets `0x89ad35`; **no event** |
| WM_RBUTTONUP | 0x0F | **right-click acts on release** |
| WM_MOUSEWHEEL | 0x0C | delta/120; consumer NIEZNANE (not the map handler) |
| poll: cursor moved | 0x0D | dx,dy at `+0xc/+0x10`, x,y at `+0x14/+0x18` |
| poll: L held | 0x12 | every poll while L is down |

Shift = `0x89ad36` (VK 0x10), Ctrl = `0x89ad37` (VK 0x11). There is **no right-drag** (no event while R is
held). `0x89ad38` is a "mouse-capture push-scroll" mode (cursor held in place, ±100 px of push scrolls the
map, `0x4c80c7`, `0x47598a`). Nothing ever sets it to 1 (the only writer at `0x5b815d` can only clear it),
so it is dead code in v1.09.

Dispatch order in the interface handler `0x527490`: open popups and buttons first. Then a switch on the
event type (`0x527b15`), then the current sidebar window (`+0x632A + 4·id`, vfunc +4) gets the event. Only
if nothing consumed it: 0x0E → map left-click (`0x52bb0b`), 0x0F → right-click menu (`0x52a190`),
0x13 → double-click (`0x52c5dc`). **Move orders are therefore issued by the army/navy/battle/siege windows**
(`0x4f8cd1`, `0x4fde9f`, `0x500740` all call `0x530f70`), which are open whenever units are selected.

---

## 1. Map scrolling

- **Edge scroll** (map handler `0x475910`, event 0x18 = tick). If the cursor's raw screen x ≤ 0 →
  dx = −(32<<z); x ≥ screenW−1 → +(32<<z); the same for y with screenH−1. Map pixels, so the step is
  **32 screen px per tick** at every zoom. The margin is **the single outermost pixel** of the screen,
  including the sidebar side. Diagonals work. It is blocked while `[0x8aa7c4]` (modal) is set or the
  tutorial lock `+0x66e0` is non-zero. Each scrolling tick increments the tutorial counter `+0x6718`.
  Tick rate NIEZNANE (event 0x18 frequency).
- **Keyboard** (map message 0x15, `0x4757f0`): key ids 1/2/3/4 set a fake offset of ±10000 on x or y, so
  the next tick scrolls **one 32-screen-px step per key event** (OS auto-repeat gives continuous scroll).
  Ids 1=left, 2=right, 3=up, 4=down. The VK→id mapping is NIEPEWNE (arrow keys). Key id 0x52 reloads the
  palette.
- **No drag-scroll**: left-drag is the rubber band, and right-drag does not exist.
- Minimap: a right-click on the minimap (`0x47b4c0` hit, `0x476520` → province) issues the move order to
  that province (`0x530fee`). Minimap left-click is outside this report (SPEC_UI minimap `0x47b5f0`).
- Selection groups (Ctrl+0..9 / 0..9, `0x534ff0..0x5351bb`): Ctrl+N stores the current selection in
  slot N (10 slots of 0x12 bytes at `+0x6410`, sound 4). N recalls it: the selection is cleared, the stored
  units are selected and the sidebar is updated. Pressing the same N twice in a row (`+0x64c4` == N) centres
  the map (`0x476960`) on the average sprite position of the group.

---

## 2. Left button on the map

### 2.1 Drag start, rubber band
- **L-down** (type 0x11, `0x527ec0`): if the point is not over the interface (`0x533140`) or the overlay
  checked by `0x567210`, **and** map-mode flag bit 0 (`[0x7fb8ac] & 1`, units shown) is set: store the
  cursor in `+0x6400/+0x6404` and set `+0x6408 = 1`.
- Moving over the interface (type 0x0D at `0x527e81`) cancels it (`+0x6408 = 0`). L-up always clears it
  (`0x52baeb`).
- It counts as a **drag** when `+0x6408` is set and `|mx−x0| + |my−y0| > 16`, a Manhattan distance
  (`0x52fb40`). Anything less is a click.
- **Drawing** (`0x526fe4`, in the interface draw, over the map): while L is held and the drag is real,
  4 one-pixel lines through the clipped line routine `0x4010d0`, colour word `0xFFFF` in the 16-bit
  surface = **white**, solid. Lines: (x0,y0)-(mx,y0), (x0,y0)-(x0,my), (mx,y0)-(mx,my), (x0,my)-(mx,my),
  in screen coordinates. No fill, no stipple.

### 2.2 Rubber-band selection (L-up with a real drag, `0x52bd42..0x52bf76`)
1. Without Shift, the selection is cleared (`0x4905c0`, which also hides every route). With Shift, the
   new units are added.
2. Normalise the rectangle (x0 ≤ x1, y0 ≤ y1).
3. **Pass 1 – armies**: for each unit in the player's own list `+0x1248` with isArmy, test its map sprite
   against the rectangle (sprite vfunc +0x3c `0x40fce0`: overlap of the sprite's **bounding box** in screen
   space = [pos−origin, +frameW>>z) × [.., +frameH>>z); no pixel test). A unit that is not yet selected
   is a candidate. If the selection is empty, its type becomes the selection type (`sel+0x14` := isNavy).
   The unit is added (interface vfunc +0x3bc) **only if isNavy == the selection type**.
4. **Pass 2 – fleets**: the same with isNavy units.
5. Result: **own units only. Armies win**: a box that holds both selects only the armies. With Shift, the
   existing selection's type decides. There is no count limit (none found).
6. `+0x6408 = 0`, then the sidebar update `0x530d60`. No sound on this path.

### 2.3 Sidebar for a selection (`0x530d60`)
The window is chosen **from the first unit in the selection list**:
- empty → window 0 (main sidebar);
- army → 6 (siege) if the unit is besieging (`0x48df30` / `0x48df10`), else 5 (land battle) if it is in
  a battle with both sides present (`unit+0x48|+0x4c`), else **3 (army window)**;
- navy → 7 (naval battle) when in battle, else **8 (navy window)**.

For several units: the army/navy window has a strip list (`0x4fb170`, `crb\aiw_bgstripp` + `aiw_bg_select`,
rows `"%s (%d/%d/%d)"`) and prev/next arrows (SPEC_UI §4.10). That this list shows the multi-selection is
**NIEPEWNE**.

### 2.4 Plain left-click (L-up, no drag): pick (`0x530410`) → action (table `0x52c7a0`, code−1)
Pick order (it stops at the first hit; `+0x63d8` = unit, `+0x63dc` = province). Unit tests only run
when `[0x7fb8ac] & 1`:

| # | Test | Code |
|---|---|---|
| 1 | selection non-empty: the province unit stacks (`prov+0xae`, fleets also the port stack `prov+0x41c`+0x30) of the selected units, hit test +0x48 | 0x10 |
| 2 | same, hit test +0x44 | 1 |
| 3 | every country's units, object `unit+0x44` (vfunc +0x18 hit): own → 3; foreign in a visible province (`prov+0x256+tag` and `prov+0x420`) → 4 | 3 / 4 |
| 4 | province `+0x41c` stacks (flag `+0x420`), same rule | 3 / 4 |
| 5 | **own units**, map sprite `unit+0x10` (fleet in a land province: the port sprite `prov+0x41c`) | 1 |
| 6 | other countries' units, map sprite, if the province is visible | 2 |
| 7 | map objects list `[0x647d40]` (map-mode bits 0x4 and 0x2000; COT and trade icons, **NIEPEWNE**) | 0x0C / 0x0D |
| 8 | province (`0x5309c0`) | see below |

The sprite hit test (`0x40fbf0`) uses the bounding box [screen(pos−origin), +w>>z) × [.., +h>>z), then a
**per-pixel test** on the current animation frame. A transparent pixel is a miss (`0x403a10`, skipped
when sprite `+0x68` is set). Army A-STAND L-1 frames are 48x59 with origin (30,54), so the box runs from
x−30..x+18 and y−54..y+5 around the province unit position at z=0. Navy frames are 72x26, origin (29,14).

**Stack priority (code 1):** when an own sprite is hit, the unit picked is **the player's unit in that
province with the largest vfunc +0x8c**, not the sprite that was hit (`0x5307a1`, `0x53081c`).

Province codes from `0x5309c0`: 0 none or invalid; 0x11 unknown (terra incognita); land with no
controller → 8; own → 5 (has `+0x40c`), 6/7 (city state via `+0x408` vfunc +0xc); foreign → 9.
Sea zone: a hit on a neighbour's port sprite → 0x0A (`+0x63dc` = that port province); with map bit 0x10,
building icons → 0x0F and the own city sprite → 0x0E; otherwise → 0x0B.

Actions:
- **1 / 3 (own unit)** `0x52c044`: ignored in diplomacy (window 2).
  - Shift + already selected → remove it (vfunc +0x3a4).
  - Shift + not selected → add it if it has the same type.
  - No Shift: if exactly one unit is selected and it stands in the clicked unit's province, **cycle**:
    another own unit of that stack becomes `+0x63d8`. The loop keeps the last match; the exact order is
    **NIEPEWNE**.
  - Then clear the selection, play sound 5 (army) / 6 (navy) unless the unit is in battle or siege, select
    the unit, and update the sidebar.
- **2 / 4 (foreign unit)**: nothing.
- **5–8 (land province)** `0x52c3f2`: sound 0, select the province at the cursor (`0x476310`), then the
  sidebar by map mode `[0x7fb8a8]` (table `0x52c7e0`): 2 → window 0x11 (resource); 3 → 0xA; 4 → 4 (COT
  of the province); 7 → nothing; other modes → 6 (siege) if the province has a siege of ours, else 9 if
  we control it, else 0xA.
- **9 (foreign province)**: sound 0, select it, window 0xA unless map mode 7.
- **0x0A (port)**: if the player owns the port province and its port stack holds our fleet → clear the
  selection, select that fleet (type navy), update the sidebar.
- **0x0B (sea zone)**: sound 0, select it, window 1 (sea).
- **0x0C (COT)**: window 4.
- **0x0E**: window 0x15 (country) for that province's owner (`0x517180`), **NIEPEWNE**.
- **0x10**: clear the selection, select `0x4df550(+0x63d8)`, update the sidebar, poke button 0x15
  (**NIEPEWNE**).
- **0x11, 0x0D, 0x0F**: nothing.

Double-click (`0x52c5dc`): with map bit 0x10, on an own port (code 0x0A) → `0x45ea10` and window 0xC
(purchase navy), **NIEPEWNE**.

---

## 3. Right button

- **Move order** (`0x530f70`, R-up, only via an open army/navy/battle/siege window). It aborts if
  `0x48ffc0(selection)` is true (NIEPEWNE: nothing can move) or a modal is open.
  1. Target: minimap → `0x476520`; otherwise province `0x5309c0` at the cursor.
  2. Validity: see SPEC_EXPLORERS §3.2 for RIKTIGTERRA / LANDMORON / NAVALMORON. Also allowed: an army
     onto a fleet in a sea zone of region 0x94 (`0x490930`).
  3. **Success**: sound 0x3F (army, `0x48fad0 ≥ 0x48fb20`) / 0x40 (army, otherwise) / 8 (fleet). Then a
     **one-shot `moveorderanim.bmp`** (840x40 = **21 frames of 40x40**, palette `armypalette 3.bmp`, frame
     0, speed 4, auto-delete `+0x66`) **at the click point** in map coordinates, centred (the frame count
     setter `0x40fdd0` makes origin = frame centre). Draw layer priority 0x191.
  4. Order: Shift → `0x4907a0(prov, 0)` = **append as a waypoint**; otherwise `0x490790(prov)` =
     `0x4907a0(prov, 1)` = **replace the path**. Every selected unit gets it.
  5. **Failure**: sound 2, one-shot `illegalmoveorder.bmp` (240x25 = **10 frames of 24x25**) at the
     click point; the message dialog where one applies.
- **Right-click menu** (`0x52a190`) when no window consumed R-up: a pick, then a context menu built from
  `RCDM_*` text keys (View/Enter province, Build Army/Navy, Declare War, Locate Armies/Navies/…, Select
  All, Split, Merge, Unload, Assault, Siege/Cover force, Disband, …). It is out of scope here; its layout
  is NIEZNANE.

---

## 4. Selection marker

- **`statusbarselect.bmp`** (396x7 = **9 frames of 44x7**, speed 0x0F, interface `+0x63c4`) is drawn
  over the **strength bar** of every unit whose `+0x56` (route visible = selected) is set (`0x490467`).
- Strength bars (`unit+0x40`, `statusbar.bmp` 44x7) stack **below the unit's map position**: the first
  at y+8, each next one `+barH+1` = +8 (`0x4903d2`, `0x4904a5`). Fleets: y+10, −13 more when the
  province has a port stack. Both are centred on x (origin = frame centre).
- **`selectanim_army.bmp` / `selectanim_navy.bmp`** (988x45 = 13 frames of 76x45, loaded at `+0x64d0`,
  `+0x64d4` (army twice) and `+0x64cc`): they are loaded, animated (`0x527df9`), reset to frame 0 /
  speed 4 when the selection changes (`0x530375`) and freed. **Nothing ever positions or draws them** —
  no reference sets their position and none registers them in a draw layer. So **v1.09 shows no selection
  ring** (strong evidence, NIEPEWNE only because a draw through an unseen pointer cannot be ruled out).
  SPEC_UI §2.4 "selection ring" should be corrected.

---

## 5. Route display (route object `0x408f50`, vtable `0x5e433c`, one per unit at `unit+0x9a`)

Shown **only for selected units**: adding to the selection (`0x490e90`) calls unit vfunc +0x3c (show,
`0x48db90`); removing (`0x490eb0`) or clearing (`0x4905c0`) calls +0x40 (hide, `0x48dbc0`). All selected
units show their own routes. The colour does not depend on the owner (only own units can be selected).

### 5.1 Points (`0x48da00`, `0x48daf0`; add `0x409210`)
- P0 = the unit's current sprite position (always flagged).
- Then each path province: an army uses the province unit position `prov+0x3ec/+0x3f0`. A fleet uses the
  port position `prov+0x3da/+0x3de` in a land province (terrain < 5), else `+0x3ec`.
- Point flag = the province is known to the owner (`prov+0x256+tag`). Unknown provinces get no arrow, and
  ribbon segments with both ends unknown are skipped.
- Tangent per point (Catmull-Rom): Tᵢ = (Pᵢ₊₁ − Pᵢ₋₁)/2. The first point gets P₁−P₀ and the last its
  incoming segment vector. x differences wrap at ±9472, map width 18944.

### 5.2 Ribbon (`0x409440` build, `0x409700` draw, draw layer priority 0x15E)
- A cubic Hermite spline through the points, **16 samples per segment** (sample count `+0x22` = 16·(n−1)).
  At each sample the ribbon extends ±6 map px perpendicular to the curve, 12 px wide in total, in map
  coordinates so it shrinks with zoom. The t start and step details are NIEPEWNE.
- Drawn as triangle pairs by `0x402880`, with an intensity interpolated from 0 at both edges to 255 on the
  centreline. The 0x80 end cap is NIEPEWNE.
- **Pass 1 shadow**: offset (4>>z, 6>>z) screen px, each covered pixel halved (`(p>>1) & 0x7BEF`, 565).
- **Pass 2 colour**: the pixel is replaced by `ramp[intensity]`, ramp(i) = sin(π·i/256):
  - **red** ramp (`0x644e30`, R = ramp·31) for samples ≥ `+0x2a`;
  - **green** ramp (`0x645048`, G = ramp·63) for samples < `+0x2a`.

  `+0x2a` = progress along the route from the date (`0x409b80`): (now − leg start)/(arrival − leg start)
  · 16 · legs. So the **travelled part is green and the remaining part red**. The exact formula is
  NIEPEWNE.
- **Grow-in**: a new order (`0x48daf0` with flag) starts with `+0x26` = 1 drawn samples, and every tick
  (event 0x19) adds its parameter until the ribbon is complete. Re-showing an existing route draws it
  whole at once. On arrival at each province: the first point is removed and `+0x26 −= 16` (`0x4093c0`).

### 5.3 Arrows (`0x409ab0`)
- Bitmap: **`gfx\map\T-MAP_ARROWS.bmp`** (loads the file `GFX/Map/t-map_arrows.bmp`, 128x16 8-bit,
  palette `gfx\palette\arrow.bmp`), 8 frames of 16x16, global `[0x645254]` (`0x408df0`). The
  `Units/bmp/map_arrows.bmp` + `map_arrows.spr` pair is a different file (another MD5) and is **not**
  what is drawn (NIEPEWNE: the sprite parser's type list has no MAP_ARROWS).
- One arrow **at every flagged point except the last** (the destination itself gets no arrow; P0, the
  unit's own position, does). It is drawn **centred** on the point converted to screen ((x−8, y−8) at
  frame 16x16) through the generic sprite draw, so it scales with zoom (NIEPEWNE).
- Frame = direction of that point's tangent: a = atan2(dy, dx) with map y downwards, in π/4 sectors
  centred on the axes (`0x408d00`): **0 N, 1 NE, 2 E, 3 SE, 4 S, 5 SW, 6 W, 7 NW**. This is the
  clockwise frame order of the bitmap.
- The arrows are drawn after both ribbon passes, so they sit on top. `t-map_arrows`' red = shadow-key
  handling is done by the sprite blitter (not re-checked).

---

## 6. Unit walking animation and movement

- Animation state (`0x48e9b0`, unit vfunc +0x70): with a non-empty path, action **A-WALK** (sprite action
  1) and direction = `0x4109c0`(own province unit pos → next path province unit pos). The x difference
  wraps when |dx| > 15000 (±18944). For a loaded army the fleet's province is used. Without a path and not
  in battle or siege: **A-STAND**, direction 10 = none.
- Direction (`0x4109c0`): a = atan2(y0−y1, x1−x0) (screen-up positive), in 16ths of a circle:
  [1,3) NE=1, [3,5) N=0, [5,7) NW=7, [7,9) W=6, [9,11) SW=5, [11,13) S=4, [13,15) SE=3, else E=2.
  Codes as above; the D-N..D-NW sprite sets map 0..7 clockwise from N.
- **Position**: the army sprite stands on its province's unit position (placement `0x48e2f0`, mode at
  `unit+0x3c`): mode 2 = `prov+0x3ec/+0x3f0` (fleets in a land province `+0x3da/+0x3de`); mode 0 =
  (x−16, y+16 army / y−16 navy); mode 1 = (x+16, y−16 army / y+16 navy), two battle sides (NIEPEWNE);
  mode 3 = (x−12, y+12) (NIEPEWNE: siege). **No interpolation between provinces was found for armies or
  fleets**: the unit walks in place facing the next province and jumps when it arrives (NIEPEWNE).
  Merchants, colonists and diplomats (T-MEC sprites, `0x41ca20`) do move, linearly by date between
  waypoints, with the same direction function.
- Sprite timing from the `.spr` files: `Speed = 5`. Army walk L-1 frames are 76x52 (14 frames), origin
  (38,44); navy walk frames are 72x27 (12 frames), origin (28,16).

---

## 7. Cursor choice (`0x52fc00`, each tick; ids → bitmaps from `0x566840`)

Ids: 0 attack_army, 1 attack_navy, 2 click, 3 enterport, 4 explore_army, 5 explore_navy, 6 illegal,
7 load, 8 move_army, 9 move_navy, 0xA select, 0xB send_colonist, 0xC send_merchant, 0xD unload, 0xE small.

| Situation | Cursor |
|---|---|
| over the `+0x2294` overlay (`0x41ffe0` hit) | small |
| L button held | click |
| modal open, over the interface, or over the `0x567210` area | select |
| colonist window (0xE) | send_colonist if valid (vfunc +0x50), else illegal |
| merchant window (0xF) | send_merchant if valid (vfunc +0x4C), else illegal |
| army or siege window (3/6) with a selection, by pick code: | |
| · own unit (1/3/0x10) or own fleet in a sea zone of region 0x94 that the army fits on | load |
| · other own-unit codes (0xE/1/3/0x10) | select |
| · reachable (`0x48dff0`) and target unit or controller at war | attack_army |
| · reachable, otherwise | move_army |
| · unreachable, unknown land, not terrain 6 | explore_army |
| · on a fleet, land target, `0x45f730` true | unload |
| · otherwise | illegal |
| navy window (8): own unit | select |
| · foreign unit, owner at war | attack_navy |
| · sea zone | move_navy |
| · land province with port `+0x487`: allowed by `0x48c640` | enterport, else illegal |
| · unknown sea, navy selection | explore_navy |
| other windows | select |

---

## 8. Differences from our code (for the implementer)

- `src/units.c` draws `selectanim_*` rings under the selection. v1.09 does **not** (§4). Use the animated
  `statusbarselect` over the strength bar instead.
- `g_dest` (moveorderanim) is drawn at the destination province continuously. v1.09 plays it **once, at
  the click point** (21 frames). Add `illegalmoveorder` for refused orders.
- `src/main_amiga.c` treats left-drag as map scrolling. v1.09 has **no drag-scroll**: left-drag is the
  white rubber band (own units, armies before fleets, one type), and scrolling is the screen edge (outermost
  pixel, 32 screen px/tick) plus keys.
- Missing: Shift-click add/remove, Shift-right-click waypoints, Ctrl+0..9 groups, click-again stack cycling,
  the route ribbon (shadow + red/green) with direction arrows for selected units only.

## 9. Key addresses

| Address | What |
|---|---|
| `0x5b7a80`, `0x4c80a0` | window proc mouse → events; cursor poll (move / L-held events) |
| `0x527490` | interface event handler (switch `0x527b15`, map L-up `0x52bb0b`, R-menu `0x52a190`, dbl `0x52c5dc`) |
| `0x527ec0`, `0x52fb40`, `0x526fe4` | drag start; drag test (>16 Manhattan); rubber-band draw |
| `0x52bd42` | rubber-band selection |
| `0x530410`, `0x5309c0` | pick unit / pick province (codes §2.4) |
| `0x530d60` | sidebar window from selection |
| `0x530f70` | move order (R-up), moveorderanim / illegalmoveorder |
| `0x4907a0` | give the path to the selection (arg 1 = replace, 0 = append) |
| `0x490e90` / `0x490eb0` / `0x4905c0` | selection add / remove / clear (show / hide routes) |
| `0x408f50`, `0x409210`, `0x409440`, `0x409700`, `0x409ab0`, `0x408d00` | route: ctor, add point, build spline, draw ribbon, draw arrows, arrow frame |
| `0x48e9b0`, `0x4109c0`, `0x48e2f0` | unit walk/stand state, 8-way direction, placement in province |
| `0x475910`, `0x4757f0` | edge scroll, key scroll |
| `0x52fc00`, `0x567460`, `0x566840` | cursor choice, set cursor, cursor table |
| `0x40fbf0`, `0x40fce0` | sprite pixel hit test, sprite rect overlap |
| `0x534ff0` | selection groups 0..9 |
