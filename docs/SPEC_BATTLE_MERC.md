# Battle-window sprites, leaders, naval placement tables, mercenary market (eu2.exe v1.09)

These are rules, constants and layouts read from the exe. They are a specification only, and no
code was copied. Addresses are VAs. **NIEPEWNE** marks a guess and **NIEZNANE** marks an unknown.
Window positions are relative to the window origin `[win+0x12]` / `[win+0x16]`.

---

## 1. Land battle: unit figures

### 1.1 Correction to SPEC_UI_WINDOWS3 §1

The battle window does **not** use `crb\T-Armystripp L-n.bmp`. It draws the **map unit sprite** of
each unit: the same animated `gfx\map\units\T-ARMY C-<nat> A-<act> [D-<dir>] L-<n>.spr` that the
unit shows on the map. The evidence follows.

- The draw `0x4fdff0(list, x, y)` reads `unit+0x10`, the unit's map-sprite object (set up by
  `0x4108d0`, `0x410680`, `0x410750` and others). Its `+0x0C` is the bitmap that the sprite hash
  lookup `0x411140` currently holds.
- From that bitmap the draw copies the image (`0x40eec0`), the frame count `+0x4E` and the
  **current** frame `+0x52` into a temporary bitmap, then draws it.
- So the battle figure shows the same animation frame as the map, on every redraw. Nothing is
  mirrored: the left-facing and right-facing views are separate files.

Fields of the map-sprite key. The parser is `0x410b20`, which reads the letter codes from the
file name.

| Field | Letter | Name table | Values |
|---|---|---|---|
| +0x10 type | T | `0x6154a8` | army 0, navy 1, … (default 0x12) |
| +0x14 nationality | C | tag → country index (`0x4696d0`) | |
| +0x18 action | A | `0x6166f0` | stand 0, walk 1, fire 2, … sink 24 (default 0x1C) |
| +0x1C direction | D | `0x618360` | n 0, ne 1, e 2, se 3, s 4, sw 5, w 6, nw 7, l 8, r 9 (default 10 = none) |
| +0x20 level | L | number | default 1 |

### 1.2 Which picture a unit gets

**Nationality (C).** The army sprite comes from the country.csv column `ArmyNationalitySprite`
(static record `0x64f730 + idx·0x144`, field `+0x114`, getter `0x43a590`). The fleet sprite comes
from `ShipNationalitySprite` (`+0x118`, getter `0x43a5b0`). The value is an ordinary country tag,
used directly in the file name, so **no frame table is needed**.

- Tags used in v1.09:
  - army: MIN (214 countries), MUS, MOG, BAN, CHI, OHI, AZT, POR, HOL, NIP, plus one-off national
    sets (ENG FRA SPA SWE RUS TUR POL PRU HAB VEN USA);
  - navy: MIN / MUS / CHI.
- **Natives (country 0x152 NAT)** get their sprite from the province (`0x485b90`, switch on
  `0x4d9500(prov)`):

  | Province value | Sprite tag |
  |---|---|
  | 0, 1 | AZT (0x41) |
  | 2 | OHI (0x2c) |
  | 3 | BAN (0x4c) |
  | 4, 9 | tag 0x14f (**NIEPEWNE**: ALI) |
  | 5, 8 | MOG (0x40) |
  | 6, 7 | CHI (0x3e) |
  | anything else | 0x4c |

- **Fallback when no sprite matches the key.** If the country's `+0x254` (techgroup) is 0..4, the
  tag becomes MIN (0xc). If it is 5 or 6, the tag becomes 0x14f. Otherwise the level becomes 1.
- Tag indices, from the init table at `0x463a7d…`: 0xc MIN, 0x2c OHI, 0x3e CHI, 0x40 MOG, 0x41 AZT,
  0x4c BAN, **0x150 MER, 0x151 PIR, 0x152 NAT, 0x153 REB**.

**Level (L).** For an army, L = the land "class" `0x45b8a0(owner)`. That is the current land-tech
record's `+0x30` + 1, taken from the tech data. It runs 1..4, and L-4 map sprites exist. For a
fleet, L = the naval class `0x45b8f0` (`0x48a0d0`).

**Action and direction on joining a battle** (`0x48b4a0`):

| Situation | Action | Direction |
|---|---|---|
| Battle type 3 or 4 (siege) | stand | none (10) |
| Attacker, i.e. `unit+0x51 = 1` (side flag from `0x46cd70`: non-zero = "Attackers") | fire | **NE** |
| Battle type 5 (assault), any side | fire | **NE** |
| Defender | fire | **SW** |

This matches the file set: only `A-FIRE D-NE` and `A-FIRE D-SW` exist. When a unit leaves battle
(`0x48eaa4`), it goes back to stand with no direction.

### 1.3 Placement (draw `0x4ff240`, helper `0x4fdff0`)

The base point is (88,81) for the **defender** (side +0x34) and (84,98) for the **attacker**
(side +0x30). The defender is drawn first, so the attacker sits on top. If the battle type
`+0x24` is 5 (assault), the defender is not drawn.

The helper takes the side's unit list (side vfunc +0xC; its vfunc +4 is the count) and draws
**at most 3 units**, one figure per army stack (not one per regiment), in list order:

- count = 1: one figure at base + (20, 8);
- count ≥ 2: figures at base, base + (20,8), base + (40,16).

The sprite's own `Origin = {x y}` from the .spr file is presumably the hotspot (**NIEPEWNE**).

### 1.4 Where the `T-Armystripp` strips are actually used

They are used only in the army/navy **unit panel** (`0x4f6520`, refreshed by `0x4f7600` when the
tech class changes):

| Panel | File | Frames | Current frame |
|---|---|---|---|
| Army | `crb\t-armystripp l-%d.bmp`, %d = `0x45b8a0(owner)` with 4 clamped to 3, so **L-4 is never loaded** | 19 | **always 11**, the plain grey soldier; never culture-dependent in v1.09 (`push 0xb` at `0x4f657b`) |
| Navy | `crb\T-Navystripp.bmp` | 3 | naval class − 1 |

Both are recoloured through `aiw_armystripp palette.bmp` (`0x40eed0`). No code was found that
picks one of the 19 culture frames (**NIEPEWNE**, but it was looked for).

---

## 2. Leaders in the battle window

**They are not shown.** The strings CRBLEADERNAME, CRBLEADEFIRE, CRBLEADERSHOCK and CRBLEADERMOVE
appear in text.csv (ids 2045, 2048, 2049, 2050) but are **not in the exe at all**: a full scan
finds no "CRBLEADE" bytes. The CRB keys that the exe does contain are:

- CRB_ROLL_AFIRE / ASHOCK / DFIRE / DSHOCK / DADA;
- CRBINF / CAV / ART / WAR / GAL / TRA;
- CRBSIEGE1..6;
- CRBBREACH.

The per-side draw `0x4fe7c0` and the common draw `0x4fe110` draw no leader name, portrait or
value. The leader keys are therefore dead text left over from an earlier design.

(Side note for SPEC_UI_WINDOWS3: "CRBFIRE"/"CRBSHOCK" are not exe strings either. The phase-icon
tooltips must use other keys, **NIEZNANE**.)

---

## 3. Naval battle placement tables

### 3.1 Where they come from

The tables are filled by **static initialisers** (CRT start-up functions, constants only):
`0x4ff840`, `0x4ff890`, `0x4ff8e0`, `0x4ffa00`. They are indexed by the **sprite level (L) − 1**
of each side's **lead ship** (the first unit in the side list, `[[unit+0x10]+0x20]`). They are
**not** indexed by ship count.

### 3.2 How the setup uses them (`0x4ffb20`)

- **Ships.** The attacker ship bitmap `+0xDE` and the defender ship bitmap `+0xE2` take the lead
  unit's map-sprite image and frame count (the FIRE NE / SW animation). Their own animation is
  restarted: frame 0, `+0x26` = 10 (frame delay, **NIEPEWNE**), `+0x67` = 0.
  - Attacker: placed at `T_A[a]`, with a = attacker L − 1.
  - Defender: placed at `T_D[d]`, with d = defender L − 1.
- **Smoke.**
  - Attacker: min(a + 2, 4) clouds of `gfx\interface\T-NAVY A-SMOKE D-SE.bmp` (45 frames of
    72x60). Start frames are 0, 5, 10, 15. Positions are `S_A[a][i]`.
  - Defender: min(d + 2, 4) clouds of `T-NAVY A-SMOKE D-NW.bmp` (51 frames of 72x60). Start frames
    are 20, 25, 30, 35. Positions are `S_D[d][i]`.
  - Both use the palette `navysmoke palette.bmp` and `+0x26` = 2.
- **Draw order** (`0x4ff7f0`): attacker ship, then the smoke list `+0xDA`, then the defender ship.

### 3.3 Table contents

All entries are (x, y), window-relative. −100 marks an unused slot, which is never reached because
of the min() above.

| Level index | `T_A` 0x8abfc8 | `T_D` 0x8ac068 | `S_A` 0x8ac088 (row = 32 bytes) | `S_D` 0x8abfe8 (row = 32 bytes) |
|---|---|---|---|---|
| 0 (L1) | (40,79) | (120,101) | (60,94) (50,97) | (94,96) (80,99) |
| 1 (L2) | (52,74) | (104,96) | (64,102) (58,104) (54,105) | (74,98) (70,102) (64,105) |
| 2 (L3) | (24,74) | (102,103) | (30,99) (42,97) (52,95) (62,92) | (88,100) (80,103) (70,105) (98,97) |
| 3 (L4) | (30,72) | (102,98) | same as L3 | same as L3 |

---

## 4. Mercenary market

### 4.1 The market is a country

`[0x64e680]` is the pointer to country **index 0x150, tag MER**: `0x64e140 + 4·0x150`. It is
created at start-up if missing (`0x43b790(0x150)`, at `0x41a0..`). Its player type
`0x91a738[0x150]` is 7 (set in `0x5b25d0`), which selects the class with vtable `0x5e6264`.

The related special countries are 0x151 PIR (type 6), 0x152 NAT (type 5) and 0x153 REB (type 4).

**There is one global market**, not one per country. The offered companies are simply MER's own
land units (unit list `country+0x1248` / `+0x124C`).

### 4.2 Spawning (MER vfunc +0x268 = `0x55b930`)

This runs on message 0x16 = **TIMER_MONTH**, on the host only (`0x4196e0`). Each month:

1. Compute the cap:
   - base cap: **year < 1500 → 6**, **1500..1699 → 5**, **≥ 1700 → 1**;
   - cap = base + number of **active wars** (`[0x8ff61c]`, the length of the war list
     `0x8ff614`).
2. If MER currently has fewer units than the cap, create **one** company with event 0x88
   CREATE_ARMY (`0x4437c0`) for MER at province 0. Province 0 is presumably off-map
   (**NIEPEWNE**). The troop order is inf / cav / art.

| Troop | Rule |
|---|---|
| Infantry | (rand % 6 + 3) × 1000 = **3000..8000**, step 1000 |
| Cavalry | (rand % 4 + 1) × 500 = **500..2000**, step 500 |
| Artillery | if year > 1500 and rand % 10 > 5 (40 %): (rand % 3 + 1) × 5 = **5 / 10 / 15**, else 0 |

The draw order in the code is: art test, then inf, then cav.

- There is no expiry: no code that removes unsold companies was found (**NIEPEWNE**).
- The offer refreshes only by the monthly top-up.
- In 1500..1699 there are up to 5 + wars companies waiting. They are sold first-found first: the
  first land unit in MER's list.

### 4.3 The offer and the price

- **The offered company** is the first unit in MER's list whose vfunc +0x14 is true (= land unit)
  (`0x55ba30`). Its troop floats are read **rounded up** (`0x5bc2a6` = ceil, x87 control word
  0x1b3f): inf `+0xF0`, cav `+0xEC`, art `+0xE8`.
- **The price** (`0x55ba60(company, buyer)`, constants `0x5e6110` / `0x5ea450` / `0x5f5f14`):

  ```
  price = (0.02·inf + 0.04·cav + 6.5·art) × (1 + inflation(buyer))
  ```

  Inflation is `buyer[0xE84]` clamped to ≥ 0 (`0x43c8e0`).

  Example: 5000 / 1000 / 10 → 100 + 40 + 65 = 205 ducats before inflation.

### 4.4 The UI gate (army window, `0x4f4632`, tooltips `0x4f5240`)

The RAISEMERC button is enabled only when all of these hold:

- MER exists;
- price ≤ treasury `[0xE70]`;
- a company is on offer;
- the capital province's continent `+0x106` = **2 (Europe)**, from the `CON_*` table `0x61dc74`:
  1 America, 2 Europe, 3 Asia, 4 Oceania, 5 Africa;
- the army's province has the same continent as the capital.

The **Europe restriction** is therefore: European capital, and recruiting in Europe.

### 4.5 Hiring (`0x55bb30(tag, prov)`)

The UI sends event **0x145** carrying the company's unit id (`+0x18` / `+0x1C`), the buyer tag and
the province. The executor (`0x4dd000`, branch at line 162345) runs only if the unit is still
owned by MER (`unit+0xDE == [0x64e680]`). It then:

1. **0x60 LOSEMONEY** to the buyer, for price(company, buyer) recomputed at execution time,
   booked as **expense category 2, "Raising Armies"** (`0x438ab0` → ledger `0x42c780`). The
   treasury is not re-checked here (**NIEPEWNE**: it can go negative in a race).
2. **0xD5 SWITCH_ALLEGIANCE**: the unit becomes the buyer's.
3. **0x39 SET_IN_PROVINCE**: the unit appears in the recruiting province at once, with no build
   time.

The result is an ordinary army with the same troop counts. **No separate mercenary maintenance**
or merc flag was found (**NIEPEWNE**). The army maintenance code was not re-read for this.

### 4.6 AI

There is **no AI use**:

- the only callers of `0x55ba30` / `0x55ba60` / `0x55bb30` are the army-window UI
  (`0x4f465e…0x4f532f`) and the 0x145 executor;
- event 0x145 is built only in `0x55bb30`.

The AI never hires mercenaries (**NIEPEWNE** only in that a raw-byte call scan can miss an
indirect route).

### 4.7 Not the mercenary market: the pirate spawner

`0x55c110` / `0x55c300` / `0x55c4b0` / `0x55c570` belong to **PIR (0x151)**, not MER. They form
the monthly pirate-fleet spawner:

- it picks a sea province (region 0x94 "Sea") with the best score;
- the fleet has 2..4 ships;
- the spawn chance is 100 % before 1680, (1730 − year)·2 % until 1730, then 1 %.

Do not confuse it with the mercenary market.
