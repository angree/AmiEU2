# Missions and victory points

This file records rules and layout facts only, read from the `eu2.exe` v1.09 disassembly and the
Ghidra export. No code was copied.
- Window coordinates are window-relative (224x344 at screen (0,103)).
- `country` = the country object; `prov` = province record `0x8077d4 + id·0x4A0`.

## 1. Missions

### 1.1 Data: `DB/Missions.csv`

Columns: `Tag;Code;Start;End;X;Y;END`. There are 578 rows.

| Column | Meaning |
|---|---|
| Tag | country that can receive the mission |
| Code | mission type 0..10 (table below) |
| Start / End | year range in which the row may be offered (`0x57bd60`: start ≤ current year ≤ end) |
| X | target: a province id, or a country tag for the country types |
| Y | an area name; type 5 only |
| END | `X` row terminator |

- **Run-time copy:** a per-country candidate table at `0x8cb040` (340 lists, one per tag). Record
  fields: +0x04 type, +0x08 start, +0x0C end, +0x10 X, +0x14 Y (`0x57bd60`, `0x57b750`).
- **The file name `Missions.csv` does not occur anywhere in `eu2.exe`.** The code that fills this
  table was not located (**NIEZNANE**). The record layout matches the csv exactly, so it is almost
  certainly this file (**NIEPEWNE**).
- The scenario keys `missiontype = n`, `missioncountry`, `missiontactics` in `.eug` files belong to
  the **scenario mission** (victory mode 3, §2.2), not to this system.

### 1.2 Mission types

The type sets the class, the text keys and the base VP (table `0x638210`).

| Code | Class (vtable) | Text | Target | VP success | VP failure (= int(0.66·VP)) |
|---|---|---|---|---|---|
| 0 | province (`0x5f6b58`) | MISSION_TITLE/DESC_F "Conquer %s" | province X | 30 | 19 |
| 1 | province | _G "Keep %s" | province X | 20 | 13 |
| 2 | country (`0x5f6b9c`) | _B "Vassalize %s" | country X | 20 | 13 |
| 3 | country | _C "Annex %s" | country X | 30 | 19 |
| 4 | province | _H "Build shipyard in %s" | province X | 15 | 9 |
| 5 | area (`0x5f6be0`) | _A "No %s in %s" | country X in area Y | 50 | 33 |
| 6 | province | _I "Discover %s" | province X (its region name is shown) | 5 | 3 |
| 7 | province | _J "Colonize %s" | province X | 5 | 3 |
| 8 | province | _K "Establish trade in %s" | province X | 5 | 3 |
| 9 | country | _D "Alliance with %s" | country X | 10 | 6 |
| 10 | country | _E "Royal Marriage with %s" | country X | 2 | 1 |

Evidence:
- type → class: factory `0x579ae0`, `0x579cf0`;
- texts: `0x57a9e0` (province), `0x57b420` (country, jump table `0x57b560`), `0x57bb00` (area);
- VP: `0x57aff0`, `0x57b750`, table `0x638210`, factor 0.66 at `0x5eb8bc`.

Mission object fields:

| Offset | Meaning |
|---|---|
| +0x16 | owner country |
| +0x1A | type |
| +0x1E | date (day, month, year at +0x20) — 0 / 0 / 5, i.e. a duration of **5 years**, until accepted; then the absolute deadline |
| +0x26 | valid flag |
| +0x28 | VP on success |
| +0x22 | VP penalty on failure |
| +0x52C | target X |
| +0x530 | area Y |
| +0x42C | title text |
| +0x2C | description text |

### 1.3 Offering

- **Who gets offers:** only countries with `country[0x11E]` set (the tracked countries), when the
  dynamic-missions option is on. The option is `[*0x8c0bfc + 0x2DA]` (FEOPT_MISSIONS "DYNAMIC
  MISSIONS", **NIEPEWNE** mapping).
- **How many:** for each of the **11 types** that the country has no current offer for
  (`0x4362b0(type)` false), one new offer is requested: message 200 → `0x446540` (`0x4505d0`,
  `0x451300`).
- **Choice:** `0x5799d0` builds the candidate list for (tag, type, current year), picks one **at
  random** (`rand() % n`) and initialises it.
- **Rejection:** a candidate whose test (vfunc +0x24) is already non-zero — already done or already
  impossible — is dropped.
- **Delivery:** the offer is sent as message 0xCC (`0x446710`). It replaces any older offer of the
  same type and is added to the country's available-missions list.
- **Invalid offers:** an available offer whose test becomes non-zero is replaced by a fresh one of
  the same type (`0x451300` loop over country+0x2122).
- **Frequency:** `0x4505d0` / `0x451300` are periodic country updates; their exact frequency
  (monthly or yearly) is **NIEPEWNE**.

### 1.4 Accepting

- **Player:** MWACCEPT in the mission window → `0x436350` posts message 0xC6. Handler `0x452c30`:
  deadline = today + 5 years (`0x4173c0`); the mission moves from "available" to "current"
  (`0x4360d0`).
- **AI:** `0x451300` scores the offers and accepts the best one itself (`0x436350`). Types 7 and 8
  are scored by colonisation value, with −10 if it is low (`0x451710..0x451847`). It shows message 4
  "MISSNEW" to the player when it is the player's country.

### 1.5 Completion test

Each class has a test at vfunc +0x24; it returns 0 = in progress, 1 = failed, 2 = succeeded.
"Before the deadline" = `0x417270(today)`. The tests are `0x57a770` (province), `0x57b2c0`
(country) and `0x57ba70` (area).

| Type | Succeeds | Fails |
|---|---|---|
| 0 Conquer | at the deadline, if we own (`prov[0x3C4]`) **and** control (`prov[0x3C8]`) it | province unowned or unknown to us; at the deadline, if we do not own and control it |
| 1 Keep | same as Conquer | same; before acceptance, fails at once if we do not own it |
| 2 Vassalize | as soon as the target is our vassal (`0x4cbe90`) | the deadline passes |
| 3 Annex | when the target no longer exists (`0x64e140[X]` = 0 → 2) | the deadline passes |
| 4 Shipyard | the province has a shipyard (`prov[0x41C]` and `0x47caf0`) | no settlement (`prov[0x41C]` = 0); the deadline passes |
| 5 No X in area | at the deadline, if no province of area Y is owned by X; or if X no longer exists | at the deadline, if X still owns a province there |
| 6 Discover | the province is known to us (`prov[0x256 + tag]`) | the deadline passes |
| 7 Colonize | the province has a settlement (`prov[0x408]`) owned by us | it becomes someone else's; the deadline passes |
| 8 Trading post | the province has our trading post (`prov[0x40C]`) | the province gets an owner; the deadline passes |
| 9 Alliance | relation-table byte +0x3C with X is set | refused (`0x4cc110`) or at war (`0x4cc150`) and not our vassal; the deadline passes |
| 10 Royal marriage | at the deadline, if byte +0x3B with X is set | at the deadline otherwise |

### 1.6 Rewards

| Outcome | Message / handler | VP | Then |
|---|---|---|---|
| Success | 0xCA → `0x452c90` | + VP success (category 6 "Mission", §2.1) | the mission is removed; a new offer of the **same type** is requested |
| Failure | 0xCB → `0x452d00` | − VP penalty | same as success |

- No stability or money reward was found (**NIEPEWNE**).
- Message texts: MISSUCC_* (`%s` VP), MISSFAIL_*, and MISSUCCOTHER_* for other countries.

### 1.7 Mission window (`iw_missionlist.bmp`)

Ctor `0x519240`, vtable `0x5ec20c`. Title HEAD_MIS "Missions".

| Element | Position | Notes | Evidence |
|---|---|---|---|
| MWCURRENT "Current Missions" | centred (96,15) | g14 | `0x519a88` |
| Current-missions list | (32,42), 128 px wide, height 4·rows | lists the titles (+0x42C) | `0x519bb1`, `0x519bd9` |
| MWAVAILABLE "Available Missions" | centred (96,106) | g14 | `0x519aba` |
| Available-missions list | (32,136), 128x157 | lists the titles | `0x519c06`, `0x519c2e` |
| BACK | centred x=112, y=319 | text button | `0x519b6c` |
| Info box for the selected mission | rectangle from `[main+0x22DE]` vfunc +0x1C — position **NIEZNANE** (probably the separate MISS_INFO panel `0x570a00` over the map) | description; then, centred, MWFINISHED "Time Limit: : date", MWVPSUCCESS "Victory Points: %d", MWVPFAILURE "Penalty Points: %d" | `0x5198d0..0x519a2e` |
| MWCLOSE "Cancel" / MWACCEPT "Accept" | bottom-left / bottom-right of that info box | ACCEPT is enabled only for an available mission | `0x519805..0x5198a4` |

- Clicking a list entry selects the mission and shows its description (`0x519db0`).
- ACCEPT → `0x436350`, then back to window 0x12 (this window).

---

## 2. Victory points

### 2.1 VP sources

**Superseded by docs/SPEC_VP.md** (all 45 award sites decoded; several rows below were wrong).

- **Storage:** VP is added only through `0x55f030(category, amount)`. It adds to the float at
  vp+4 + 4·category, where vp = the struct at country+0x20E2, so category 0 is at country+0x20E6
  (the Ledger's Basic VP table). It also adds to the running total via `0x441320`.
- **Condition:** nothing is added when the global option `[*0x8c0bfc + 0x2DE]` is 0.
- **Labels** (`0x55f070`): 0 BVPECO, 1 BVPPEACE, 2 BVPBATTLE, 3 BVPDIP, 4 BVPEST, 5 BVPDISC,
  6 BVPMISS.

| Category | Award | Evidence |
|---|---|---|
| 0 Economy | +1/12 per call to the country that is best in a per-country ranking (field [0xF06 + 4·i], probably monthly, **NIEPEWNE**) | `0x41bcd0` |
| 0 Economy | +0.05 (after a building/expansion action, **NIEPEWNE**) | `0x446ae0` |
| 0 Economy | +0.16 | `0x499e92` |
| 1 Peace | −25 (peace-related case with flag [0x3F53] = 1, **NIEPEWNE**: unconditional surrender or breaking a peace) | `0x44ee50` |
| 2 Battle | + (25 + value at battle-result +0x22) per won battle (message 0xB5) | `0x4699d0` |
| 3 Diplomacy | −5 for each of three diplomatic acts (types 1, 2, 4 in `0x44a100`, which also cost stability through `0x436490`) | `0x44a100` |
| 3 Diplomacy | **+50 on becoming Holy Roman Emperor** (sets the emperor global `0x8d0e18`) | `0x455530` |
| 4 Establishment | new colony: +0.8, plus +5 when the province class (`0x418e80`) is 1..4 | `0x447900` |
| 4 Establishment | new trading post: +0.4, plus +5 when the class is 5..7 | `0x4476b0` |
| 5 Discovery | per discovery, amount from the caller (special discoveries e.g. SPEC_AUSTRALIA have their own) | `0x44f470`, `0x48cdd0` |
| 6 Mission | + VP success / − penalty (§1.6) | `0x452c90`, `0x452d00` |
| any | message-driven award: category and amount carried in the message (events) | `0x461e90` |

Each monarch's "VP:s" in the Ledger is the growth of the total during the reign (see the Ledger
spec).

### 2.2 Score / victory modes (`[0x647d48]`)

The mode is set from the frontend VICTORY choice (`0x57fe70`, `0x583630`, `0x58e340`); 0 is the
default (`0x5848f0`). The init text is chosen in `0x4198e0`.

| Value | Mode | Init text | Score shown in the country window / ranking | Win condition (frontend text) |
|---|---|---|---|---|
| 0 | Standard | STANDARDGAME | int(country[0x20DE]) = VP total | most VP at the end of the scenario (FEVIC_STANDARD_DESC; end year from FEVIC_STANDARD_LOG) |
| 1 | Supremacy | VICTORYGAME | country[0x20DE] | first to reach N VP (FEVIC_SUPREMACY_LOG "%d Victory Points"; VICTORYGAME_* panel) |
| 2 | Deathmatch | DEATHMATCH | country[0x1224] = provinces conquered (incremented in `0x481260` only in this mode) | first to conquer N new provinces |
| 3 | Scenario mission | SCENARIOMISS | `"%d%%"` country[0x121A] = progress towards the scenario mission | first to complete its scenario mission (FEVIC_MISSION_DESC; the per-country objective comes from `missiontype` in the `.eug` and the *_YOUR / *_OTHER texts) |

Notes:
- Ranking in the country window and in `0x451300`: mode 2 compares [0x1224], mode 3 compares
  [0x121A], the others compare [0x20DE].
- End-of-game text (`0x52ce90`): GAME_MISSION in mode 3, else GAME_NORMAL "You got %d Victory
  Points and %s"; then GAME_WON or GAME_LOST.
- The thresholds N (VP or provinces) and the end year are frontend settings. Where they are stored
  is **NIEZNANE** (not traced).
