# Natives in provinces (EU2 v1.09, eu2.exe)

Clean-room specification read from the objdump disassembly of `eu2.exe` (addresses are VAs).
Prose and tables only, no code copied. **NIEPEWNE** = believed but not proven, **NIEZNANE** = not
found. Conventions as in `docs/SPEC_SMALLRULES.md` (tag indices ENG = 1, FRA = 2, SPA = 8,
NAT = 0x152; province table `[0x8077d4]`, stride 0x4a0; `0x4196e0()` = host, always true in SP).
Work files: `C:\temp\amiga_eu2\sr\nt*.sh`, dumps `C:\temp\amiga_eu2\sr\nt_*.txt`.

Event ids below are **event** ids (`_CEUEVT_*` names from the name getter `0x4b95a0`, jump table
`0x4b9ca8`). News ids are the **code** ids of the type table `0x63592e` (code k = entry k+1).

| Event | Name | Handler |
|---|---|---|
| 0x16 | TIMER_MONTH | native manager: growth (§2) |
| 0x88 | CREATE_ARMY | (country) |
| 0x8d | PROVCHANGECONTROL | NAT override `0x55bce0` = settlement damage (§4.3) |
| 0xcf | REQUEST_CONQUISTADOR_TEST | `0x4d8de0` (§5.2) |
| 0xd0 | REQUEST_NATIVE_ATTACK | `0x4d8f10` (§4.1) |
| 0xd1 | REQUEST_PERHAPS_NATIVE_ATTACK | `0x4d92a0`, then `0x4d8f10` (§5.1) |
| 0xd2 | SET_NATIVES | province `0x482a20` (§1.3) |
| 0xd3 | PACIFY_NATIVE | `0x4d8d37` (§4.2) |
| 0x126 / 0x127 | REQ_ / EXEC_DESTROY_TPOST | manager forwards 0x126 → 0x127 to the TP owner when the province is not its CoT seat (not native-specific) |

The **native manager** is the object `[0x8ff5fa]`; its event handler is `0x4d89c0` (host only).
All native events are sent to it, with the province index in payload +0xc.

---

## 1. Province fields

### 1.1 Mapping (province.csv loader `0x4848e5..0x484973`, sequential column reads)

| CSV column | Field | Type | Range in v1.09 data | Used for |
|---|---|---|---|---|
| 23 Difficulty for Colonization | +0x124 | int | | colonist/trader chance (SPEC_ENVOYS) |
| 24 Native Combat Strength | **+0x12c** natives (current) | int, hundreds of men | 0..400 (most 5..100) | everything below; shown ×100 as "Strength" |
| (copy of 24) | **+0x128** maxnatives | int | | growth cap |
| 25 Ferocity | **+0x134** | float | 0..5 | battle multiplier, uprising, perhaps-attack |
| 26 Efficiency of Natives in combat | **+0x138** | float | 0.1..2 | **no reader found** (loaded + saved only, NIEPEWNE) |
| 27 Negotiation Value for Trading Posts | **+0x130** | int | 0..9 | trader cost/chance (`0x43712c`, `0x43729a`) |
| 28 Natives Tolerance value | **+0x13c** | int | 0..9 | shown as **"Aggressiveness"** (CWNATIVETOL, CWN* words) |

**Corrections to the brief / older docs:** +0x13c is *Tolerance* (the value the UI calls
aggressiveness), **not** Ferocity; +0x134 is **Ferocity**, not Efficiency; Efficiency is +0x138
and appears unused. `SPEC_WAR_DIPLOMACY §7` ("n = province+0x134 for NAT") is therefore the
Ferocity multiplier. Savegame keys: `maxnatives` (`0x49b6d3`) and `natives` (`0x49e7ff`).

### 1.2 Other natives state

- prov **+0x88** (byte): "conquistador test done" — set to 1 by the test (§5.2) and also by
  `0x481260` (NIEZNANE purpose) and the savegame loader; cleared only by the province reset
  `0x47c2cd`. The test therefore runs **once per province per game**.
- The native village sprite (`prov+0x422` object) is shown while natives > 0; its tag follows the
  native style (§2.1 / SPEC_BATTLE_MERC).
- Event effect EE_NATIVE ("Native Aggressiveness in %s set to level %d"): province handler
  `0x480c58` **sets** +0x13c := trunc(value) (other effects nearby add; this one assigns).

### 1.3 SET_NATIVES 0xd2 (province `0x482a20`, value n)

- n ≤ 0: natives := 0, hide the village sprite.
- else: natives := min(n, maxnatives); place/refresh the village sprite.

---

## 2. Growth (TIMER_MONTH in the manager, `0x4d8a50..0x4d8bff`)

Every month, for every province 1..0x7df: if natives ≠ 0 **and** natives < maxnatives, post
SET_NATIVES(natives + step). The SET_NATIVES clamp keeps it ≤ maxnatives. Natives at 0 never
regrow (killed natives are gone for good).

### 2.1 Native style `s` (`0x4d9500(prov)`, switch on region prov+0x10e)

Region numbers follow the REG_* order of text.csv, North America = 1 (NIEPEWNE that +0x10e is
that index directly, as in SPEC_SMALLRULES).

| Region (+0x10e) | s |
|---|---|
| 1 North America | 2 |
| 2 Central America, 3 Caribbean | 1 |
| 4 South America | 0 |
| 13 Siberia | 9 |
| 14 India | 5 |
| 15 China | 6 |
| 16 Indochina, 17 Indonesia, 19 Australia, 24 Pacific | 8 |
| 18 Japan | 7 |
| 21 East Africa, 22 West Africa | 4 if province religion (`0x47ca10`) = 5 (Sunni), else 3 |
| everything else (5..12 Europe/Middle East, 20 North Africa, 23 Central Asia, ≥ 25) | 4 |

| s | Growth step / month (hundreds) | Native army split (inf / cav) |
|---|---|---|
| 0, 1, 2 (Americas) | +1 | 100 % / 0 |
| 3 | +3 | 100 % / 0 |
| 4 | +3 | 50 % / 50 % |
| 5, 6, 7, 8 | +5 | 80 % / 20 % |
| 9 | +3 | 75 % / 25 % |

### 2.2 Natives joining a settlement (monthly city step, `0x457e13..0x457e50`)

In the country's monthly settlement loop (`0x457c00`, SPEC_TIME_ECONOMY §4.1 item 20), after
population growth: if natives ≠ 0 **and population > 900**: population += 100·natives, then
SET_NATIVES(0) is applied directly (`0x482a20(0)`). **Correction to SPEC_ENVOYS §2.7**: the
natives join at > 900, not at the 1000 city threshold.

(Same loop, not native-specific: population < 10 → the colony is destroyed by `0x47d800` and
news **0x8a COLONYDIE** goes to the player — unless the province is its own CoT seat, then the
population is set to 10.)

---

## 3. Monthly native uprising (`0x480370`, province monthly, SPEC_TIME_ECONOMY §4.2 item 1)

Called once a month for every province (province TIMER_MONTH branch `0x48082d`), after the
weather step. It is **the native uprising**, not a rebel revolt.

Qualifies when all hold (host only): natives > 0; the province has a settlement (+0x408,
colony or city) **or** a trading post (+0x40c); the province has an owner (+0x3c4). The owner may
be anyone (no player/AI distinction).

```
r = Tolerance(+0x13c) − 4, or − 2 if a settlement (+0x408) exists
if r > 0: r = r / 2            (integer)
r += trunc(Ferocity +0x134)
if r > 0: r = r * r
if a trading post (+0x40c) exists: r −= 10; if r > 5: r = 5
uprising iff rand() % 1000 < r
```

**Correction to SPEC_REVOLT_RELIGION / the brief:** the −2 case is "has a settlement" (colony or
city), the −4 and the "−10, cap 5" case is the **trading post**, not "city / colony".
Examples: settlement, Tol 9, Fer 2 → (7/2 = 3) + 2 = 5 → 25 → 2.5 %/month; maximum (Tol 9,
Fer 5) 64 → 6.4 %/month; a trading post never exceeds 0.5 %/month.

An uprising calls `0x4d8f10(prov)` = the full native attack of §4.1 (no message of its own).

---

## 4. Native attack (`0x4d8f10`, REQUEST_NATIVE_ATTACK 0xd0)

### 4.1 Spawning the native army

1. S = natives × 100 (men). If S ≤ 0: nothing.
2. Split by the style of §2.1: inf = S·p_inf, cav = S·p_cav (each trunc).
3. Host: post SET_NATIVES(0) at once — **all natives leave the province and become the army**.
4. A new unit id is allocated (`0x414530`); CREATE_ARMY 0x88 for NAT (0x152) in this province,
   infantry = inf, cavalry = cav (payload +0x22 / +0x26, troop order NIEPEWNE as in
   SPEC_EXPLORERS), executed at once (`0x4b8850`).
5. PACIFY_NATIVE 0xd3 for that unit id, dated **today + 5 days**.

No news is posted here. The army then fights like any army: NAT's land fire and shock are
multiplied by the province **Ferocity** (+0x134) (`0x48b4a0`, assault `0x486xxx`; SPEC_WAR_DIPLOMACY
§7). NAT army sprites come from the style (SPEC_BATTLE_MERC). Battles against NAT give no battle
VP (SPEC_VP row 9). No badboy or native-specific attrition was found (NIEZNANE).

### 4.2 PACIFY_NATIVE 0xd3 (`0x4d8d37`)

Find the unit by id; if it still exists and is idle (unit +0x48 = 0 and +0x4c = 0, i.e. not in a
battle / siege, NIEPEWNE), its owner disbands it (`0x43fa00`). The men do **not** return to the
province's natives. It is also posted:
- **2 days** after a NAT unit becomes idle again (`0x48ba3d..0x48bb3d`, unit-idle path, host only);
- NAT's handler for GAME_STARTED 0x50 (and other ids of slot 0xac, `0x55bca0`) disbands every idle
  NAT unit at once (`0x4d95d0`).

So a native army lives until it has been idle for ≈ 2–5 days, or is destroyed.

### 4.3 Damage to the settlement (NAT override of PROVCHANGECONTROL 0x8d, `0x55bce0`)

NAT is country class type 5 (`0x91a738`, ctor case `0x43b99b`, vtable `0x5e6638`). When control of
a province passes to NAT (the native army wins it; how a settlement without a fort is taken is the
normal control/siege code — NIEPEWNE), NAT first runs the normal handler (`0x44cbb0`), then,
unless `0x419700()` (debug/cheat flag `[0x8ff63c]+0x2766`):

```
D = Tolerance(+0x13c) × Σ over NAT units in the province of (men × 0.025)    (men = unit vfunc +0x8c, NIEPEWNE)
```

**Settlement (+0x408), population pop (+0x7e):**
1. p = pop − D.
2. If the settlement's fort level (+0x3a) > 0 and p < 25: p = pop − D/3; if that is still < 25,
   **nothing happens** (the fort saves it).
3. If p < 10: if the province is not its CoT's seat (CoT `+0xaa` exists and its province ≠ this
   one) → **destroy** (`0x47d800`) and news **0x89 NATIVESKILL** ("have massacred the entire
   population…"); otherwise p = 10.
4. Else population := p and news **0x88 NATIVES** ("has damaged our settlement there!").

**Trading post (+0x40c), level L (+0x4 float):**
- L' = L − D/60. If L' < 1 and the province is not its CoT seat → destroy + **0x89 NATIVESKILL**.
- Else level := trunc(L') (can become 0 for a CoT seat, NIEPEWNE) + **0x88 NATIVES**.

News go only to the local player, and only if the province's **controller before the change**
(read at entry) is the player. Payload: province name, province id. (0x8b TPOSTBURN exists in
the text table but no poster was found.)

`0x47d800` (destroy settlement): removes the province from the owner's lists (+0x131a owned,
+0x12f6 cities, +0x1308 TPs) and the controller's list (+0x133e), clears port users, and deletes
the settlement/TP object (detail out of scope).

---

## 5. Natives versus units

### 5.1 Army entering a province: REQUEST_PERHAPS_NATIVE_ATTACK 0xd1 (`0x4d92a0`)

Every time a **land army** (unit vfunc +0x14) finishes a move into a province, the arrival code
(`0x4dd78f..0x4dd907`) posts REQUEST_CONQUISTADOR_TEST 0xcf and then 0xd1 for that province, at
once. 0xd1 → if the test below is true, the full native attack `0x4d8f10` (§4).

The test (false at once if the **"cortez" cheat** flag `[0x8d0d77]` is on — "The natives went to
sleep!", toggled at `0x565560`; this is its only reader):

1. natives = 0 → false.
2. Walk the province's unit list in order, keeping min = 9999. For a unit whose leader record
   (`0x48c040`, units without a leader use their built-in record) has rank (+0x22) < min **and**
   which is an army: min = rank, best = that leader, total += unit men (vfunc +0x8c). **Quirk:**
   only the units that set a new best rank are summed — reproduce as is.
3. b = 10·Tolerance. If best exists: b −= 10·maneuver(best+0x26); b += 20 if best's country
   (+0x32 → tag) is SPA (8); b −= 20 if it is FRA (2); b −= 30 if best is a conquistador
   (type +0x2e = 2).
4. c = total/1000 − natives/10 + b + 25 (C integer division).
5. Attack iff rand() % 100 < trunc(c × Ferocity).

### 5.2 REQUEST_CONQUISTADOR_TEST 0xcf (`0x4d8de0`)

Only if prov+0x88 = 0 (first army ever, §1.2):
1. base = 40 − rand() % 20 (21..40).
2. Among units in the province, the **conquistador** leader (type 2) with the lowest rank; if one
   exists: base += leader vfunc +0x10 + vfunc +0xc (NIEPEWNE: fire and shock/siege) + maneuver
   (+0x26) + 2 if its country is SPA or FRA.
3. +5 if region (+0x10e) is 2..4 (Central America, Caribbean, South America).
4. prov+0x88 := 1; SET_NATIVES(trunc(base × 0.01 × natives)).

So the first army that enters a native province cuts the natives to about 21–40 % (more with a
conquistador). As found, a better conquistador leaves **more** natives (NIEPEWNE intent; reproduce
as is). Note the order: 0xcf is posted before 0xd1, so the perhaps-attack sees the reduced count.

### 5.3 "Attack Natives" (BUTTON_KILL_NATIVES, bitmap BUTTON_0210, tooltip BUTTON_0094, key k)

- Unit window command 0x2d (`0x4f3060`): posts REQUEST_NATIVE_ATTACK 0xd0 for the selected
  unit's province (unit +0xc) at once (`0x443450` → `0x4b8870`).
- Enabled (`0x45fa20`, called at `0x4f2e7f`) when the unit is stationary (its path +0xc4 is
  empty), not in a battle (+0xf4 = 0), and its province has natives > 0.
- Effect: §4 — all natives become a NAT army that fights the unit(s) there; natives := 0 at once
  and never regrow. No VP for the battle; no badboy found.
- **AI** ("genocider" plan, `0x553ea0` / `0x553f30`): same check `0x45fa20`, then the same
  `0x443450`; the plan ends when natives < 1 ("is done") or when the unit cannot act. How the AI
  picks targets: NIEZNANE (out of scope).

---

## 6. Colonists / traders (SPEC_ENVOYS §2.7 step 4 — confirmed)

- Failure roll (`0x43f0b7..0x43f0d0`): fail iff rand()%100 > chance; never fails if `0x419700()`.
- On failure: `0x47f370(prov)` = trunc(10 × Tolerance) → native attack iff rand()%100 < that
  (Tolerance 0..9 → 0..90 %).
- The 0xd0 is dated today + T + 2 + rand()%4 days (T = travel days), i.e. it lands 2..5 days after
  the failed colonist/trader arrives, and runs the full §4 attack. The COLFAIL_4 / TPFAIL_4
  texts warn about it. Trader version at `0x43f368`.
- Colonist chance natives term (SPEC_ENVOYS §2.5) uses **Tolerance** +0x13c; trader terms use
  **Negotiation** +0x130. Field names in SPEC_ENVOYS lines 16-18 should read: +0x12c strength,
  +0x13c Tolerance ("aggressiveness"), +0x130 Negotiation (confirmed, no longer NIEPEWNE).

---

## 7. Port notes

**State per province:** natives (int, hundreds), maxnatives, ferocity (fixed point, e.g. ×10
— values are multiples of 0.5), tolerance 0..9, negotiation 0..9, conq-test-done flag; efficiency
can be dropped (unused) but should be kept in the save format. Per NAT army: its id, so PACIFY can
find it.

**Monthly (TIMER_MONTH):**
1. Manager: growth step for every province with 0 < natives < max (§2).
2. Province monthly: uprising roll (§3) for provinces with natives and a settlement/TP.
3. Settlement monthly: natives join when population > 900 (§2.2).

**Event-driven (dated or immediate):**
- army arrives in a land province → conquistador test (once) then perhaps-attack (§5);
- failed colonist/trader → maybe dated native attack (§6);
- Attack Natives button / AI → immediate native attack (§5.3);
- native attack → natives := 0, NAT army, PACIFY at +5 days (§4.1); NAT idle → PACIFY at +2 days;
- NAT takes control → settlement damage / destruction + NATIVES / NATIVESKILL news (§4.3).

**Fixed point:** all products here are small (natives ≤ 400, ferocity ≤ 5, men × 0.025 = men/40);
use integers with ferocity in tenths and truncate where the original uses ftol (toward zero).
rand() is the C library rand (0..32767), always taken modulo as above.

**Open:** how NAT gains control (siege vs instant occupation of an unfortified colony) and whether
control returns to the owner when the native army is disbanded — follow the generic control code
(NIEZNANE here).
