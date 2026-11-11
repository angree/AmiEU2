# Small rules: war taxes, world flags, envoy travel, extra VP, "OTHER" news (EU2 v1.09)

Specification read from `eu2.exe` (objdump disassembly, addresses are VAs) as a clean-room
specification: prose and tables only, no code copied. **NIEPEWNE** = believed but not proven,
**NIEZNANE** = not found. Work files (scripts and dumps): `C:\temp\amiga_eu2\sr\`.

Conventions used below:
- C = the country running the handler; "viewer" = the local player's country (`[0x8bfa50]`).
- `0x434d90(C)` = "C is the local player". `0x4196e0()` = "this machine is the host" (always true in
  single player).
- Country table `0x64e140[idx]` (idx = row of `country.csv`, ENG = 1, FRA = 2, HAB = 3, HOL = 4,
  POR = 5, ..., SPA = 8, PAP = 0x2e, ROY = 0x49, FPR = 0x4a, SPR = 0x4b; 0x14f..0x153 special tags,
  0x153 = REB). `0x45bce0(idx)` = "real country" (idx != 0 and not 0x14f..0x153).
- Country fields: +0x254 state religion (0 Prot, 1 Ref, 2 Counter-Ref, 3 Cath, 4 Orth, 5 Sunni,
  6 Shiite, 7 Pagan, 8..10 eastern), +0xe46 tech block, +0xe70 treasury, +0xe84 inflation,
  +0x122a war exhaustion, +0x1f7e capital province, +0x20e2 VP ledger, +0x38e diplomacy object
  (record of X at `+0xc + 0x36*X`; byte +0x47cc of the object = "at war with anyone").
- Dates: struct {day byte, month byte, year word}; 30-day months, 12-month years
  (`0x619f24` = 30, `0x619f28` = 12). `0x417510(n)` = duration of n days; `0x4171a0(n)` adds
  n months; `0x417210(n)` adds n **years** (it adds to the year word); `0x417130(n)` adds n days.
- Message (news) ids: the code posts id k, whose text key is the entry k+1 of the type table at
  `0x63592e` (records of 0x1a bytes: name pointer, id, 14 flag bytes, prefix pointer). Proven by
  several pairs (EXPLOREOTHER = code 0xad, JOINWAROTHER = code 0x53, BROKENOTHER = code 0x62,
  OTHERRELIGION = code 0x9d). The ids below are always the **code** ids.

---

## 1. War taxes

### 1.1 How they are raised

- Only from the Budget window (`iw_treasury`, window code around `0x51c180`): button id **0x38**
  (`BUTTON_0209` "Raise War Taxes"). Click → `0x440120` posts command **0x56
  EXCEPTIONAL_TAXES** to the player's own country (`0x51d436`).
- **The AI never raises war taxes.** `0x440120` has exactly one caller (the button), and 0x56 is
  written nowhere else (all `WORD 0x56` stores in the exe were checked).
- Button enabled (`0x45d4e0`, called at `0x51c875`) when all hold:
  1. C is at war with anyone (diplomacy-object byte +0x47cc);
  2. war taxes are not already running (C+0xe78 <= 0);
  3. stability > −2.99 (i.e. not −3; `0x43a230`).
- Tooltip (`0x51c982..0x51c9f2`): enabled → `TWR_WT`; disabled → first match of
  stability < −2.99 → `TWR_WARTAX3`; C+0xe78 > 0 → `TWR_WARTAX1`; else → `TWR_WARTAX2`.
  (`BUTTON_0092` is an older text with different numbers; it is not used by this code path —
  NIEPEWNE, the button text table was not traced.)

### 1.2 Effect of command 0x56 (country handler `0x448eb0`, vtable slot 0xd0)

Applied at once, no money cost:
1. C+0xe78 (war-tax months) := **6**.
2. War exhaustion C+0x122a += **1.0**.
3. Inflation C+0xe84 += **0.01** (1 %).
4. Host only: immediate revolt roll `0x460450(C, bonus 0)` (SPEC_REVOLT_RELIGION §2).
5. `0x443dc0` (refresh; NIEPEWNE: recalculates cached country values / UI).

### 1.3 While active (C+0xe78 > 0)

Every reader of C+0xe78 in the exe (`0x435d73` init 0, `0x448ebf` set, `0x451943` decrement,
`0x45d4f0` button, `0x47db9a`/`0x47e0c4` tax, `0x481cb8`/`0x482367` revolt risk, `0x51c9d0` UI):

| Effect | Rule | Evidence |
|---|---|---|
| Province tax multiplier M | the start value of M is **2.5** instead of 1.0 (other terms of M are added to it unchanged; SPEC_TIME_ECONOMY §5.1). Tooltip line `PROVTAX_EXC` with 150 | `0x47e0c4`, `0x47db9a` |
| Monthly tax and census tax | both use the province yearly tax, so both get the 2.5 start | SPEC_TIME_ECONOMY §5.1 |
| Troop build capacity | cap(p) = int(province tax `0x47e270(p)` × 1000) and `0x47e270` multiplies by M, so build capacity rises the same way (this is the "150 % extra troop build capacity" of `TWR_WT`; no separate rule exists) | `0x47e307`, SPEC_UI_WINDOWS4 purchase |
| Revolt risk | +1 in every province of C (`RRISK_WARTAXES`) | `0x481cb8` |

### 1.4 Duration

- Country month step 9 (`0x451943`): after the monthly tax has been computed with the current
  value, C+0xe78 is decremented if > 0. So the war taxes pay **6 monthly tax posts** at 2.5
  (the month of raising included only if the raise happened before that month step; the counter
  is set immediately), and the build-capacity / revolt-risk effects end at the month step that
  takes the counter from 1 to 0.
- Peace does not end them; the counter just runs out. They can be raised again only after it
  reaches 0 and only while at war.

---

## 2. World flags (Tordesillas, Reformation, Calvin, Trent, Tolerance, flags)

### 2.1 Storage, setting, reading

- Object `[0x8a230c]` (vtable `0x5ea464`, ctor `0x4d12e0`): 7 flag bytes at +0xa..+0x10
  (flags 0..6), all 0 at creation.
- Set by message **0x14f SETEVENTFLAG** (poster `0x4d15b0(flag, value)`; handler `0x4d13bf`):
  flags[n] := (value > 0); then the flag graphics of ENG and FRA are refreshed (`0x456a00`; for
  flags 5/6). The event command `flag` (`0x596e78`) always posts value 1: **flags are only ever
  set, never cleared** (the console cheats `luther`/`calvin`/`trent`/`tordesillas` toggle them).
- Scenario / save: block `flags = { n n ... }` inside the global data (`0x5b1d3a`, error
  "Global flags - No left curly brace") sets those bytes directly. Shipped values: 1617/1683/1755
  `{0 1 2 3}`, 1700 `{0 1 2 3 4}`, 1773 `{0 1 2 3 4 6}`, 1795 `{0..6}`; 1419/1492 none.
  The save writer (`0x5b2f05`) writes the flags through the reader below, so flag 0 is only saved
  while it is *effective* (quirk; reproduce or not, harmless).
- Reader `0x4d16a0(n)`:
  - n ≥ 1: the raw byte.
  - **n = 0 (Tordesillas) is effective only if**: flag 4 is off, SPA (idx 8) exists and POR
    (idx 5) exists, and both have state religion Catholic (3) or Counter-Reform (2).
- Port note: `src/events.c` already keeps the bits; it must also read the scenario `flags` block
  and expose `world_flag(n)` with the rule above.

### 2.2 When they appear (data, not code)

All by **events**, never by date in the code (`DB/events/ReligiousEvents.txt`,
`hugeevents.txt`), each with `date = { year }` and a random `offset` (days, SPEC_EVENTS):

| Flag | Event | Year + offset (days) | Meaning (`EE_FLAG_n`) |
|---|---|---|---|
| 0 | 110 Tordesillas | 1505 + 0..5000 | Treaty of Tordesillas rules |
| 1 | 100 Reformation (Luther) | 1515 + 0..4000 | Protestant religion enabled |
| 2 | 101 John Calvin | 1540 + 0..4000 | Reformed religion enabled |
| 3 | 102 Council of Trent | 1570 + 0..5000 | Counter-Reformation enabled |
| 4 | 103 Edict of Tolerance | 1650 + 0..3000 | ends Tordesillas, "reduces the effect of religion" |
| 5 | (France events, flagname REV) | – | French tricolour |
| 6 | (England events, flagname UK) | – | Union Jack |

### 2.3 Everything the flags change (all 70 call sites of `0x4d16a0` enumerated)

**A. Religion availability `0x4310f0(r)`** (fantasy mode `[0x8d0d7a]` → always true):
r = 0 needs flag 1; r = 1 needs flag 2; r = 2 needs flag 3 **and not** flag 4; all other religions
always available. Used by the AI conversion (2.3 F). The player's targets come from
`0x45fc40` (SPEC_UI_WINDOWS4 table: Cath → Prot if flag 1, → Counter-Ref if flag 3 and not flag 4;
Prot → Ref if flag 2). Replace `rel_exists()` in `src/religion.c` by these flag tests.

**B. Effective province religion** (`0x47ca10` and inline copies at `0x47cd76`, `0x47dc71`,
`0x47e104`, `0x47f7f8`, `0x481962..`, `0x4824df`): a Protestant (0) province counts as
Catholic (3) while flag 1 is off; a Reformed (1) province counts as Catholic while flag 2 is off.
It feeds: tax "religion differs" −0.3 (`0x47dcc1`, `0x47e154`), production, revolt risk and its
tooltip, nationalism (the 10/20/30-year RR floor is skipped for pagan provinces only after the
effective test), map colouring (`0x47f840`), missionaries and the AI. `src/economy.c rel_norm()`
must apply this per flag instead of "always pre-Reformation".

**C. Tolerance defaults** (`0x430c70`): flags 1 / 2 select rows (table in SPEC_REVOLT_RELIGION §5).
Applied at scenario load and at every state-religion change only (not when a flag is set later).

**D. "Religious enemies" predicate `0x45d050(C, X)`** (true = hostile; used by relation drift,
alliance and war scoring, SPEC_WAR_DIPLOMACY §… −0.10): X missing → true. While flag 4 is off:
Prot/Ref vs Catholic/Counter-Ref → true. Muslim vs Christian (0..4) → true. Pagan vs non-pagan →
true. Sunni vs Shiite → true. Christian vs non-Christian → true. Otherwise false. So **flag 4
removes only the Protestant–Catholic hostility.**

**E. Casus belli predicate of a war `0x4ce3b0` (sets war+0x33; +0x2b = "core" CB)**, attacker A =
war+0x22 object, target T = `countries[war+0x16]`. True when, in order:
1. difficulty (`[[0x8c0bfc]+0x2ca]`) ≥ 3 or multiplayer, T is human (`0x91a738[T]` 1/2) and
   `0x45af40(T)` (NIEPEWNE: T over the badboy limit);
2. (T == A → false);
3. A owns-list... a province of A's core list (+0x1354) is owned by T (also sets +0x2b);
4. `0x4cbf60` relation flag of A toward T and T+0xfda == 0 (NIEPEWNE: temporary CB);
5. **A is Counter-Reform (2), flag 4 off, and T is Protestant (0) or Reformed (1)**;
6. A Catholic/Counter-Ref, PAP (idx 0x2e) does not exist and T owns province 399 (Roma);
   or A's diplomacy record of PAP has byte +0x0 set (NIEPEWNE: excommunication-like flag);
7. A is SPA (idx 8) and T is Sunni, Shiite or Pagan;
8. Sunni vs Shiite either way.

**F. AI state-religion conversion `0x556480`** (AI country; trigger frequency NIEZNANE):
1. at least 6 years since the last conversion (C+0x11a date); not at war;
   rand%10 ≤ ruler ADM (`0x43bf10`, 3 without a ruler) else stop;
2. only for state religion 0..3 (not Orthodox);
3. weight w[r] over effective religions r (B): each owned province adds its income value
   (`0x47d970`), +10 if its culture is accepted; each **owned core** province adds 2× value + 50;
4. best = 3 (Catholic) with best value 0; for r = 0..10: if trunc(best value) < w[r] then
   best = r, best value = trunc(w[r]) (integer comparison quirk);
5. if best ≠ 3: n = number of existing countries with state religion best; if n < 4 and
   rand%10 > n → stop. If best = 3 and the country's AI-setup byte (C+0x25c)+0x26 is set →
   best = 2 (Counter-Reform) (NIEPEWNE meaning of that byte);
6. **flag 4 on and C is Counter-Reform → best = 3** (back to Catholic after the Edict); else
   best 1 while C is Catholic → 0; C Reformed and best 3 → 0;
7. stop if best == current, `0x4310f0(best)` false (A) or not an allowed target (`0x45fc40`);
8. rand%100 < 5 → convert (`0x444a00(best, 0)`, the normal conversion with stability −5 etc.).

**G. AI Defender of the Faith** (AI month `0x557b3a`): only while **flag 4 is off**: treasury
> 1000, the faith has no defender (`0x4312d0`), rand%100 < (owned provinces − 10) → become DoF
(`0x43f6f0`).

**H. Military-AI war scoring** (`0x535754`): a Christian AI scoring a non-Christian target gets
−200 unless flag 4 (military AI agents' area; listed for completeness).

**I. Revolter countries (`0x460ae0`, monthly after the revolt roll)**: a revolter tag from
`revolt.txt` may only form if: tag 0x49 ROY → ENG does not exist or is Reformed; tag 0x4a FPR →
**flag 2**; tag 0x4b SPR → **flag 1**. (The rest of revolter formation is outside this file.)

**J. Dutch revolt** (`0x461a00` in the rebel-army builder `0x461780`, and `0x55ebdc` in the AI):
for the eight Low-Countries provinces `0x482970` = {337 Friesen, 338 Geldre, 339 Holland,
340 Zeeland, 377 Luxembourg, 378 Brabant, 379 Artois, 380 Flandern}: when **flag 2** is on, HOL
(idx 4) does not exist and the province revolt risk > 10, the rebel army is the big "religious"
one (infantry ≥ 5000·fort + 1000, cavalry = infantry/3, artillery = (fort+2)·5,
SPEC_WAR_DIPLOMACY §13.3; the "after the Reformation" wording there should read "after Calvin,
Low Countries, no Holland").

**K. Tordesillas (flag 0 effective)** — not religion, listed because it is a world flag:
- `0x45f730(C, P)` "C may enter P" and `0x45f840(C, P)` "C may besiege / take P without war":
  if flag 0 is effective and C is SPA with P's byte +0x76 (`province.csv` ToT-SPA) set, or C is POR
  with +0x77 (ToT-POR) set: P counts as enterable/takeable when it is unowned, or its owner is
  Catholic / Counter-Reform and not in a military alliance with C (record byte +0x3c). Outside
  their half, or for anyone else, the normal access rules apply.
- AI colonisation candidates (`0x5566e0`, unconditional, no flag test): SPA only targets provinces
  with +0x76, POR only +0x77.

---

## 3. Envoy travel times (colonist, trader, merchant, missionary)

### 3.1 Distance

`dist(A, B)` (`0x436940`, inline in all four): position of a province (`0x475790`) = centre of its
bounding box at prov+0x3ac..+0x3b8 (x1, y1, x2, y2): x = x1 + trunc((x2−x1)/2), same for y
(full-resolution map pixels). dx is wrapped: if |dx| > 0x2500 then dx := 0x4A00 − |dx| (map width
18944). dist = sqrt(dx² + dy²). Origin is always the capital (C+0x1f7e); target = capital → 0.

### 3.2 Colonist `0x4366f0(P)` (returns a duration)

```
E      = max(0.01, infraEff(C))          infra.csv Efficiency of the infra level (tech record +0x10)
                                          + religion.csv Prod.Eff/100 (0x64d5f0) + 0.01*C+0xf02
                                          + 0.01*production policy pp (0x42b6f0) = 0x43b560
speed  = 5*(E + 1)
       + 1  if P has a trading post (prov+0x40c)
       + 2  else if P has a colony (city object, isColony vfunc +0x10 true)
       + 3  else if P has a real city
       - d       d = difficulty-2 if C is human (0x91a738[C] 1 or 2), else 0
speed  = max(1, speed)
days   = trunc( dist*0.01*30 / trunc(speed) )        (0 if P is the capital)
result = duration(days); if years*12 + months < 2 then result = exactly 2 months
```
Evidence `0x436710..0x43692c`; constants 0.01 `0x5e4eec`, 5, 0.01 `0x5e5860`, 30 `0x5e50a0`.
The cost formula of SPEC_ENVOYS §2.4 uses travel_months = years·12 + months of this result.

### 3.3 Trader `0x436eb0(P)`

```
Et     = max(0.01, trade efficiency)     trade.csv Efficiency of the trade level (record +0x10)
                                          + religion Trade.Eff/100 (0x64d61c) + 0.01*C+0xefe
                                          + 0.01*trade policy (0x42b6c0) + 0.02*C+0xe64
                                          - 0.03*C+0xe68 - 0.03*C+0xe6c   (= SPEC_TIME_ECONOMY §5.3)
speed  = 10*Et + 4 (+ 0.5 * TP+0x4 if P already has a trading post; TP+0x4 = level, NIEPEWNE)
       - d  (as above),  max 1
days, minimum 2 months: as for the colonist
```

### 3.4 Merchant `0x437800` — always **exactly 1 month**

The function computes a distance and converts it, then overwrites the result with (0 y, 1 m, 0 d)
(`0x4378a5..0x4378b3`). The merchant arrives one month after sending (SEND_MERCHANT 0xA7 arrival
date = today + 1 month, `0x43f5d4`).

### 3.5 Missionary `0x45c710(P)`

```
t = generic travel 0x439bf0(capital, P, speed 1) = duration(trunc(dist*0.3 / 1)) days (no minimum)
t += (10 - ADM) YEARS            0x417210 adds to the year field; ADM = 0x43bf10, 3 without ruler
t += 24 months
```
So the conversion takes travel + (12 − ADM) years. The window shows years·12 + months as
"%d months" (`MSW_TIME3`, `0x4fd978`). (SPEC_ENVOYS §3.5 "units NIEPEWNE" is resolved: years.)

### 3.6 Diplomats

No travel-time function is called for diplomats (the only callers of the four functions are the
send paths, the colonist/missionary windows and the merchant tooltip `0x5329be`). Diplomatic
proposals are posted immediately. NIEPEWNE only in the sense that the AI diplomat plans have their
own "one envoy per target per month" gate (SPEC_AI).

### 3.7 When the result happens

| Envoy | At sending | At arrival |
|---|---|---|
| Colonist (`0x43efc0` → 0x9D, handler `0x448a90`) | stock −1 (0x57), cost booked (0x60 expense type 5), success rolled (fail iff rand%100 > chance; always success if `0x419700`), colonisation symbol on the map (`0x41be40`) | arrival date = today + T: message 0x9F COLNEW/EXPAND or 0xA0 failure, province command type 9 (+100 settlers) |
| Colonist failure | if rand%100 < `0x47f370(P)` (native aggressiveness), native attack 0xD0 posted for today + T + 2 + rand%4 **days** | |
| Trader (`0x43f230` → 0xA2) | same as colonist (cost type 6) | 0xA3 / 0xA5, province command type 8 (+1 TP level) |
| Merchant (`0x43f4b0`) | stock −1, cost, pre-rolled flag | today + 1 month: REQ_MERCHANT_SUCCESS → resolution (SPEC_ENVOYS §1 appendix) |
| Missionary (`0x43f870` → 0x97, handler `0x448700`) | cost (expense type 7), success rolled (rand%100 < chance) | completion date = today + 3.5 time: province command type 0x1B converts P; result message 0x95 / 0x96 (MISSOSUCC / MISSOFAIL) posted for that date (`0x448a2e`, `0x4b8700`) |

So all outcomes are rolled at sending and applied (and announced) at arrival / completion.

---

## 4. Victory points not covered (or wrong) in SPEC_VP

### 4.1 KILL_LEADER (0xB5) — corrects SPEC_VP row 13

Two different handlers exist:
- **Country** handler (vtable slot 0x224, `0x4467f0`): detach the leader from its unit, remove it
  from C's leader list (+0x20d6), delete it. **No VP.** Posted at the leader's death date
  (`0x4501b2`, set to today + 12 years when a leader is activated; loader `0x4b1c6c` re-posts it
  from saves; arrival `0x4d78a6`). This is the normal "leader dies of age".
- **Combat** handler (combat vtable `0x5e7d20`, `0x469b9e`): only for leaders killed in battle.
  The roll `0x46d2c0` runs per combat side (`0x46a790` calls it for both sides; the vtable slot
  that triggers it per day/round is NIEZNANE): host only, **rand()%100 == 17** (1 %), the side
  has a leader, leader vfunc +0x18 true (NIEPEWNE: "is a real leader, not a monarch"), and the
  province of the combat has no city with `0x4961f2` true (NIEPEWNE). Then 0xB5 is posted to the
  combat; its handler adds VP category 2 (battle) **to the dead leader's own country**:
  `25 + rank` (rank = leader+0x22, 0 = most senior; shipped leaders 0..25, so 25..50 VP), and
  removes the leader from both sides. As found (sign and recipient look like a bug, reproduce
  literally).

### 4.2 COUNTRY_BROKEN (0x128) — who posts it, what it does (row 27)

- **Trigger** (monthly, host, country month step 25 → `0x460aa0`): after the revolt roll and the
  revolter step, if `0x438c00(C)` (owned provinces whose controller is REB) **>**
  `0x438c40(C)` (controlled provinces that have a real, non-colony city) → `0x4609a0`:
  skipped when C+0x11e is set and the game type `[0x647d48]` == 3 (mission scenario; NIEPEWNE
  meaning of +0x11e, also used by the declaration stability step); otherwise revolter countries
  are formed with force (`0x460ae0(1)` repeated while it succeeds), then 0x128 is posted to C.
- **Handler** `0x4550a0` (slot 0x80):
  1. Messages: C local → 0x61 BROKENUS; else if the viewer knows C's capital → 0x62 BROKENOTHER.
  2. Ruler ADM/DIP/MIL all := 3.
  3. C leaves **every war** it is in: removed from its side, `0x4445a0` (end hostilities) with each
     member of the other side both ways; wars left with an empty side are deleted (white peace for
     C, no peace VP).
  4. Owned provinces: controlled by REB → control back to C; controlled by another **real**
     country → **ceded to that occupier** (`0x481010`), and the occupier's badboy +2 (floored 0)
     unless `0x482940(occupier)` (NIEPEWNE: province is the occupier's core); the capital is
     always given back to C. Rebel armies standing in C's provinces are removed from their
     combats or disbanded (`0x46cf50`/`0x43fa00`).
  5. Provinces C controls but does not own → control back to their owners.
  6. Defender of the Faith title lost; **VP −100** (category 3 diplomacy); **stability set to +3**
     (`0x43a250(3)` sets the value directly; the `oranje` cheat uses it for +3).

### 4.3 First-discovery specials (row 8, SPEC_EXPLORERS §4.3) — exact triggers

DISCOVERPROVINCE (0x93, handler `0x44f470`) runs when a unit of C enters a province C did not
know (also fleets' adjacent land). After discover(P) it calls `0x418420(C, P)`: cases 1..17 in
order, **skipping cases already claimed by anyone**, first match wins and is claimed
(global bytes `[0x8ff638]+4+k`). The claim happens even when BASE VP is off (then no VP, the case
is still used up). Land = prov+0xca < 5, sea = ≥ 5. Area = prov+0x10a (AREA_* order in the exe,
= `province.csv` "Area" first-appearance order), region = prov+0x10e (REG_*, North America = 1),
continent = prov+0x106.

| k | Text key | VP | Trigger |
|---|---|---|---|
| 1 | SPEC_AMERICA | 50 | land, continent 1 America |
| 2 | SPEC_ASIA | 25 | land, continent 3 Asia |
| 3 | SPEC_QUEBEC | 5 | land, area 20 Quebec |
| 4 | SPEC_GREATLAKE | 10 | area 17 Grand Lacs (land or lake) |
| 5 | SPEC_CAPEHORN | 10 | province 1240 Cape Horn |
| 6 | SPEC_GOODHOPE | 10 | province 1252 Cape of Good Hope |
| 7 | SPEC_HUDSONBAY | 10 | province 829 Hudson Bay |
| 8 | SPEC_ALASKA | 5 | area 1 Alaska |
| 9 | SPEC_PANAMA | 5 | province 39 Isthmus |
| 10 | SPEC_INLANDCHINA | 20 | area 90 Manchuria **and** C already knows every province of the region list at `0x66aa5e` = REG 13 Siberia (list objects at `0x66a970 + 0x12·region`, head at +4) — as found |
| 11 | SPEC_AMAZONEZ | 10 | land, area 33 Amazonia |
| 12 | SPEC_NORTHAM | 20 | **never** (its case jumps straight to "no match") |
| 13 | SPEC_INDIA | 25 | land, region 14 India |
| 14 | SPEC_KAMTCHATSKA | 10 | land, area 89 Kamchatka |
| 15 | SPEC_CALIFORNIA | 10 | land, area 3 California |
| 16 | SPEC_PACIFIC | 10 | **sea**, region 24 Pacific |
| 17 | SPEC_AUSTRALIA | 5 | province 720..726 |

Award: VP category 5 to C if BASE VP is on (`0x44f9e2`); message 0xb0 (EXPLORESPEC, "%d" VP) only
if C is the local player. Note the order matters: the first American land province gives
AMERICA (50) and cannot also give e.g. QUEBEC; a later Quebec discovery gives QUEBEC.

### 4.4 Joining a war while in truce (row 22, corrected reading)

JOIN_WAR handler (`0x44bcb0..0x44c082`): ourSide = the side C joins, enemySide = the other one.
For each enemySide member X: truce(C, X) = `0x45f5d0`: X ≠ C and fewer than **61 months** since
C's last peace with X (C+0x8ee+4·X). Then the war's relation bookkeeping is done for every X.
Penalty applies when any truce was found **and** (C joined the **defender** side, or the war is at
least **2 months** old (war+0x16 start date)). I.e. allies joining the attacker within the first two
months are exempt; joining the defender is never exempt (SPEC_VP had it the other way round).
Penalty: s = trunc(stability − 2) taken before the drop; stability −5 (`0x436490(5)`); host:
revolt roll with bonus max(0, −s); relation −50 with every country flagged in C+0x138a
(neighbours), set symmetrically; **VP −25** (category 3).
Message: everyone who knows C's capital gets 0x53 JOINWAROTHER (sides' names; the local player too).

### 4.5 Completeness

The 45 call sites of `0x55f030` and the one of `0x55f130` were re-listed; every one is a row of
SPEC_VP §2. No VP source outside SPEC_VP exists; the corrections are 4.1, 4.2, 4.3 and 4.4.

---

## 5. "OTHER" news messages

### 5.1 Common rule

Every OTHER message is posted from the handler that performs the action, on the acting country C,
with the same filter: if C is the local player → the "US/WE" variant; **else, if the viewer
knows C's capital** (`prov+0x256+viewer` of `0x45b7d0(C)` = C's capital province) → the OTHER
variant; else nothing. Posting functions `0x577aa0/0x577b80/0x577c70/0x577ec0/0x577fa0/
0x578120` differ only in the number of `%s` arguments; display/log is then filtered by the
player's message settings (`0x578630`, SPEC_UI message settings). Arguments below are given in
the order of the `_LOG` text.

### 5.2 Posted OTHER types (code id, text key, trigger)

| Code | Key | Trigger (handler) | Text args |
|---|---|---|---|
| 0x05 | NEWMONOTHER | new monarch activated (ACTIVATE_MONARCH `0x44fda0`), only if not at the scenario start date | monarch name, country |
| 0x07 / 0x08 | EVENTOTHER / EVENTOTHERS | an event executed for another country (`0x5950b2`): event has a country or province, is not `random = yes` (+0x32), flag +0x2a clear, viewer knows its capital. EVENTOTHERS if the event has ≥ 2 actions (with the chosen action name) | country, event name (, action) |
| 0x24 | MONOPOLYOTHER | merchant arrival makes a monopoly (count > 5), `0x446da7` | country, CoT |
| 0x25 | BANKRUPTOTHER | bankruptcy (`0x450ad0`, timer tick) | country |
| 0x47 | ANNEXACCEPTOTHER | diplomatic annexation executed (0x102, `0x449660`) | annexer, annexed |
| 0x4d | MILALLBANOTHER | banned from alliance (0x109, `0x44a680`) | banned, alliance leader |
| 0x4f | MILALLDISHONOROTHER | alliance dishonoured (0x110, `0x44c980`; own: 0x56 WEMILALLDISHONOR, partner: 0x55) | country, former allies |
| 0x50 | ROYMARACCEPTOTHER | royal marriage made (EXEC_JOIN_ALLIANCE `0x449b50`, type 1) | country, partner |
| 0x51 | DECLWAROTHER | war declared (THEY_DECLARE_WAR `0x44b7a0`; own side gets 0x27 DECLWAR) | attacker, defender |
| 0x52 | PEACEACCEPTOTHER | peace accepted (EXEC_ACCEPT_PEACE `0x44d5b0`) | country, enemies, terms |
| 0x53 | JOINWAROTHER | country joins a war (JOIN_WAR, §4.4) | joiner, ally, enemy side |
| 0x62 | BROKENOTHER | COUNTRY_BROKEN (§4.2) | country |
| 0x64 | CWAROTHER | CIVIL_WAR_MESSAGE 0x14e (`0x452d70`; own: 0x63 CWARUS) | country |
| 0x6b | CLAIMTOOTH | claim throne executed (0x139, `0x4559e0`; target gets 0x6a CLAIMTOUS; host revolt roll) | claimant, target |
| 0x70 | OTHERTRADEAG | trade agreement made (0x13c, `0x455e20`; partner: 0x6e ACTRAWITHUS) | country, partner |
| 0x72 | BRKTRADEOTH | trade agreement broken (0x13b, `0x455c50`; partner: 0x71 BRKTRADEUS) | country, partner |
| 0x75 | ACCMILOTHER | military access granted (0x13f, `0x455f20`; partner: 0x73 ACCMILUS) | grantor, grantee |
| 0x77 | CANMILOTHE | access cancelled by the user (0x141, `0x456010`; partner: 0x76 CANCMILUS) | country, grantor |
| 0x79 | REVMILOTHER | access revoked (0x142, `0x4561a0`; partner: 0x78 REVMILUS) | grantor, grantee |
| 0x8d | REVOLTOTHER | rebel army appears (`0x437d60`, called from the REB army creation `0x4520b8..`): own (controller or owner of a REB-held province) → 0x86 REVOLT; else if the viewer knows P → REVOLTOTHER | province, owner |
| 0x8e | SBWINOTHER | siege won (PROVSIEGEWON `0x44d3c0`; own 0x81 SBWIN) | winner, province, former owner |
| 0x8f | LBWINOTHER | land battle won (PROVBATTLEWON `0x44d0b0`) | winner, loser, province |
| 0x9d | OTHERRELIGION | state religion changed (`0x4536c0`, SPEC_REVOLT_RELIGION §3) | country, religion (+ friend/heretic/neutral word) |
| 0xa0 | OTHERDEFENDER | became Defender of the Faith (0x12a, `0x455660`; own 0x9f WEDEFENDER) | country, faith |
| 0xad | EXPLOREOTHER | DISCOVERPROVINCE for a non-local country (`0x44f4c5`; no capital-known test, every discovery of every AI) | country, province |

### 5.3 OTHER keys that exist but are never posted in v1.09

No code posts these ids (searched every message-post call; none uses a computed id):
MISSUCCOTHER (0x06), STABINCREASEOTHER (0x26), GIFTTOOTHER (0x2f), DECLWAROTHERALLY (0x45),
PEACEACCEPTOTHERALLY (0x46), VASALLACCEPTOTHER (0x48), MILLALLJOINREJECTOTHER (0x49),
MILLALLINVACCEPTOTHER (0x4b), MILALLJOINACCEPTOTHER (0x4c), MILLALLINVDECLINEOTHER (0x4e),
WHITEPEACEOTHER (0x5e), LIBPROVOTHERALLY (0x8c). The text.csv keys NBWINOTHER, WPINV*,
EXPLOREAUTO, RANEVENT, RANEVENTHIST, HISTEVENT, LEADERNEW_MONARCH are not even in the exe's type
table (settings-screen text only). Do not implement them.

### 5.4 Port notes

- `src/` currently posts only DECLWAROTHER of these. Add the 5.2 rows at the matching handlers with
  the capital-known filter; EXPLOREOTHER should be "log only" by default (it fires for every AI
  discovery).
- The code-id → key offset (+1) matters only if ids are stored; the port uses keys.

---

## 6. Corrections to existing docs (summary)

- SPEC_ENVOYS §2.4 travel: formula of 3.2/3.3 (not "distance²"); merchant fixed 1 month; §3.5
  missionary units are years (+24 months).
- SPEC_VP row 13 (KILL_LEADER): only battle deaths (1 % roll) give VP; natural deaths go to the
  country handler without VP. Row 22: penalty exemption is for joining the **attacker** within
  2 months. Row 27: trigger = rebel-held owned provinces > controlled real cities; stability set to
  +3; occupied provinces ceded to occupiers; C leaves all wars. Row 8: exact triggers 4.3,
  NORTHAM never fires.
- SPEC_WAR_DIPLOMACY §13.3: the "religious revolt" army is the Dutch revolt (Low Countries, flag 2,
  no HOL).
- SPEC_REVOLT_RELIGION: add flag-4 effects (D, E, F, G).
