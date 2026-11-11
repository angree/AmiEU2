# Province buildings built from the PIW

This file records rules and layout facts only, read from the `eu2.exe` v1.09 disassembly, the
Ghidra export and the game texts. No code was copied.
- `city` = the settlement object `prov[0x408]`.
- `country` = the player.
- `infl` = inflation `country[0xE84]` clamped to ≥ 0.
- Tech levels: country+0xE46 objects — +0x0C land, +0x24 naval, +0x3C infrastructure, +0x54 trade.
- The flag offsets below are city offsets, as in SPEC_TIME_ECONOMY §3.2.

## 0. Rules common to all PIW buildings

Checked in `0x45fe90` (officials, shipyard, conscription centre) and `0x45b950` / `0x45b9f0`
(manufactory). A building can be ordered only if **all** of these hold:
- we **own and control** the province (`prov[0x3C4]` = `prov[0x3C8]` = country);
- it has a settlement, and that settlement is **not a colony** (city vfunc +0x10 ≠ 1) and **not
  under siege** (`0x482dc0`);
- **one improvement at a time per province:** the province's order list `prov[0x26]` has no entry
  of a type outside 6..9 and 13 (`0x4602f0`). Otherwise the tooltip is PIW_ISIMPROVING;
- cost ≤ treasury (`country[0xE70]`).

The confirm popup texts (CONFIRM*) are built in `0x5333d0`. Its kind numbers are:

| Kind | Building |
|---|---|
| 0 | manufactory |
| 1 | fortress |
| 2 | merchant |
| 5 | bailiff |
| 6 | courthouse |
| 7 | city rights |
| 8 | conscription centre |
| 9 | shipyard |

## 1. Summary table

| Item | Sidebar button (id) | Cost | Time | Requirements beyond §0 | Limit | City flag | Evidence |
|---|---|---|---|---|---|---|---|
| Tax collector ("promote Bailiff") | BUTTON_BUILD_BAILIFF (90), tooltip BUTTON_0051 | **50 × (1+infl)**, ×0.25 for AI countries (`0x91a738[tag]` = 0) | 12 months (CONFIRMBAILIFF text) | infrastructure ≥ 1 (`0x432e90`) | once per province | **+0x6A** | `0x45b700`, `0x45fe90` case 2 |
| Governor ("promote Mayor", city rights) | BUTTON_BUILD_CITYRIGHTS (91), BUTTON_0052 | **100 × (1+infl)** | 24 months | infrastructure ≥ 5 (`0x432ed0`) | once per province | **+0x6B** | `0x45b730`, case 3 |
| Chief judge ("promote Legal counsel", courthouse) | BUTTON_BUILD_COURTHOUSE (92), BUTTON_0053 | **100 × (1+infl)** | 24 months | infrastructure ≥ 4 (`0x432eb0`) **and** a Tax collector already present (+0x6A) — PIWCOURTHOUSE_I4 | once per province | **+0x6C** | `0x45b730`, case 4 |
| Shipyard | BUTTON_BUILD_NAVY (26) / BUTTON_BUILD_SHIPYARD (88), RCDM_BUILDSHIPYARD | **(1000 + 100·n) × (1+infl)**, n = country[0xFCA] + [0xFD2] (shipyards owned + in construction, **NIEPEWNE** which is which) | 36 months | naval tech ≥ 11 (`0x432e60`; NTLEVEL "Level 11: Great Royal Wharf"); a port (`prov[0x41C]` ≠ 0); base tax `prov[0xB6]` > 5 | once per province | **+0x68** | `0x45b750`, case 0 |
| Conscription centre | BUTTON_BUILD_ARMY (25) / BUTTON_BUILD_BARRACK (89), RCDM_BUILDBARRACK | **(1000 + 250·n) × (1+infl)**, n = country[0xFCE] + [0xFD6] | 24 months | land tech ≥ 41 (`0x432df0`; LTLEVEL "Level 41: … Allows Conscription Centers"); province on the capital's continent (`prov[0x498]`) **or** in the capital's region (`prov[0x106]`) | once per province | **+0x69** | `0x45b790`, case 1 |
| Manufactory (5 types) | BUTTON_BUILD_MANUFACTORY (24) + sub-panel (SPEC_UI §3.3) | formula `0x4374e0` (§3) | 720 days = 24 months (`0x4374b0`) | per type (§3); none present and none in construction (city+0x1C = 0 and +0x20 = 0, `0x496352`) | one per province | **+0x1C** = type 1..5; +0x20 = under construction | `0x45b9f0`, `0x45b950` |

- **Costs grow with the number built** (shipyard, conscription centre) and are always ×(1 +
  inflation).
- **Build times:** only the manufactory time was read from code. The other times come from the
  confirm texts and agree with them; the code values were not traced (**NIEPEWNE**).

## 2. Officials and the infrastructure texts

The infrastructure tech list (INFLEVEL_INFIW) is exactly the unlock list of the officials and
manufactories:

| Infra level | Unlocks | Button |
|---|---|---|
| 1 | "Bailiffs can be promoted to Tax collectors" | BUILD_BAILIFF |
| 2 | Fine Arts Academy | manufactory type 1 |
| 3 | "Breweries" | manufactory type 4, shown in game as "Refinery" |
| 4 | "Legal counsels can be promoted to Chief judges" | BUILD_COURTHOUSE |
| 5 | "Mayors can be promoted to Governors" | BUILD_CITYRIGHTS |
| 6 | Goods Manufactory | type 5 |

- **Every city starts with the three junior officials:** Bailiff, Mayor and Legal counsel, i.e.
  frame 0 of each official sprite.
- **Building one is a promotion:** it sets the flag and switches the sprite to frame 1.
- **The promoted states are permanent:** PIW*_I0 / D0 "Cannot be further upgraded".

PIW tooltips (`0x5061d5..0x5064e3`) for each official:

| Situation | Text |
|---|---|
| Already promoted | `_I0` (office name) |
| Can build now | `_I2` "Promote … (%d$, n months)" |
| Tech ok but too expensive | `_I3` |
| Not yet available | `_I1` (junior office name) |
| Courthouse without a Tax collector | PIWCOURTHOUSE_I4 |

The delayed tooltips `_D0/_D1/_D2` explain the effect.

**Effects** (as stated by the game texts, applied through the flags; the numeric formulas are in
SPEC_TIME_ECONOMY):

| Official | Effect |
|---|---|
| Tax collector | + tax and production income, **+3 revolt risk** (RRISK_BAILIFF), required for census tax |
| Chief judge | + tax income, **−1 revolt risk** (RRISK_COURTHOUSE) |
| Governor | + production income, **+1 city growth** (POPGRWTH_CITYRIGHTS), reduces inflation |

## 3. Manufactories

| Type (city+0x1C) | Name (FACILITY_* / PIWFACILITY_*) | Sub-panel button | Requirement (`0x45b9f0`) |
|---|---|---|---|
| 1 | Fine Arts Academy (LUXURYGOODS) | `btn_arts`, y=83 | infrastructure ≥ 2 |
| 2 | Weapons Manufactory | `btn_weapons`, y=125 | land tech ≥ 18 |
| 3 | Naval Equipment Manufactory | `btn_naval`, y=161 | naval tech ≥ 16 |
| 4 | Refinery ("Breweries") | `btn_refinery`, y=198 | infrastructure ≥ 3 |
| 5 | Goods Manufactory | `btn_manufgoods`, y=238 | infrastructure ≥ 6 |

The type ↔ button order is from SPEC_UI §3.3 and the text order (**NIEPEWNE** mapping).

Other conditions:
- §0 rules, cost ≤ treasury.
- PIWBUILDMANUFACTORYERR1 "can't build a manufactory on these resources": a goods restriction
  exists, but its test was not located (**NIEZNANE**).
- ERR3 = already under construction; ERR4 = cannot afford.

**Cost** (`0x4374e0`; branchy — structure only, **NIEPEWNE** in detail):
- A per-type constant (default 15; type 1 → 25; types 2–3 add 10 to a second term; type 5 → 10).
- An "over-quota" surcharge of 100 per manufactory beyond an allowance. The allowance is
  max(2, (year − 1520)/20 + a culture term), checked against country[0xEE6] + [0xEEA]
  (manufactories owned + building).
- 100 × country[0xEEE + 4·type] (same-type count).
- (20 − ADM) times the type constant, + 400.
- That sum ×0.02, + the count + the difficulty term (difficulty − 2 for table value 1/2), minimum 4.
- Then ×50 × (1+infl) (`0x437786..0x43779c`).

**Effects** (texts): extra income and tech investment, −revolt risk, **+2 city growth**
(POPGRWTH_MANUFACTURY), more demand for certain goods (PIWBUILDMANU_DELAYED). Messages:
MANUFINISH_* when done, MANUFAILED_* on failure.

## 4. Shipyard and conscription centre — answers

- **Conscription centre = the "barracks" building.**
  - The `barracks` key in the PIW layout files (inland (0,224), coastal (85,58), city (112,−6);
    `barracks.bmp` 2 frames) belongs to button BUTTON_BUILD_ARMY (id 25).
  - Its frame is 1 when city+0x69 is set (`0x50426f..0x504291`), else 0.
  - The tooltip PIWBUILDTROOPS reads "Build Troops / Conscription Center".
  - It **can be built** (kind 8, §1).
  - Its manpower effect is the city flag +0x69 described in SPEC_TIME_ECONOMY.
- **Shipyard = the "port" building.**
  - Layout key `port`: coastal (0,180), city (24,245); `port.bmp` 3 frames. It belongs to
    BUTTON_BUILD_NAVY (id 26).
  - Frame 2 when the shipyard flag +0x68 is set (`0x504294..0x5042b8`); otherwise frame 0 (port
    present but no shipyard, **NIEPEWNE** for frame 1).
  - Effects (texts): +colonists per year (YEARLYCOL_SHIPYARD "+%.2f Shipyards", HINT_15: "+1
    colonist per year, not more for more than one shipyard") and a higher naval support limit
    (MAXSHIP_SHIPYARDS "+%d (Shipyards)").

## 5. How the PIW shows them

- **Manufactory:** button 24 uses `manufacturies.bmp` (6 frames). The frame is set from city+0x1C:
  type 1→3, 2→5, 3→1, 4→4, 5→2, none→0 (jump table `0x503b90`, `0x503767..0x5037be`).
  - Clicking it with no manufactory opens the build sub-panel `bg_buildmanufact.bmp` (SPEC_UI §3.3).
- **Officials:**
  - Buttons 90/91/92 carry the culture-specific sprites `piw\buildings\<culture>\bailiff.bmp` /
    `mayor.bmp` / `sheriff.bmp`, 2 frames each, with the culture palette (`0x50537a..0x5054f8`).
  - Frame 1 = promoted (flags +0x6A / +0x6B / +0x6C, `0x50430f..0x504381`).
  - **Their screen positions are still NIEZNANE:** none of `inland.txt`, `coastal.txt` or
    `city.txt` has a key for them, the layout parser's key list has none either (`0x50802e..0x508284`
    accepts fort, church, port, barracks, bank, embassy, merchant, resource, blockade, house1–4,
    siege, trading, FortUtbyggd, native, tree%d), and no direct setpos call for buttons 90–92 was
    found in the executable.
- **Right-click menu entries:** RCDM_BUILDBAILIFF / CITYRIGHTS / COURTHOUSE / SHIPYARD / BARRACK /
  MANUFAC.

## 6. AI

The AI builds all of them, each with its own decision routine and debug line ("%s wants to build
…"):

| Building | AI routine |
|---|---|
| Bailiff | `0x54dd40` (decision `0x54de70`, which ranks provinces by income and adds 5 for some) |
| Courthouse | `0x54e010` |
| City rights | `0x54e290` |
| Conscription centre | `0x54e7b0` |
| Shipyard | `0x54ea10` |
| Manufactories | `0x554c20` / `0x554d10` (call `0x45b9f0` / `0x4374e0`) |

AI countries pay only 25 % for the Tax collector (§1).
