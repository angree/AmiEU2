# EU2 v1.09 — Sieges and the fortress assault (BUTTON_ASSAULT)

Clean-room specification read out of `eu2.exe` (v1.09). Prose and tables only; addresses are for
cross-checking. **NIEPEWNE** = inferred, **NIEZNANE** = not found. Builds on
`docs/SPEC_WAR_DIPLOMACY.md` §7 (land combat engine) and §9 (sieges); everything said there about
the CRT, phases, die rolls and loss application still holds and is only referenced here.

Headline findings (new compared with our docs / `src/military.c`):

1. **The garrison is a real unit**, created once when the siege combat is created:
   `infantry = 5000 × L`, `artillery = 10 × L` (only if a controller predicate holds, NIEPEWNE),
   no cavalry, both **halved if the city population < 5000**; morale = the controller's max morale.
   `L` = fortress level (city+0x3a, float, truncated).
2. **An assault is not a dice roll**: it switches the siege combat to mode 5 and runs the normal daily
   land-battle engine (5-day fire/shock phases, CRT, morale) with assault tables until one side breaks.
3. A repulsed assault does not end the siege: the attacker falls back to **siege (mode 4)** if it still
   has ≥ 5000 × L men, to **cover (mode 3)** with ≥ 1000 × L, otherwise it really retreats.
4. A won assault and a surrender go through the same exit ("The fortress troop retreats!"): **20 %**
   chance the fort loses one level (never below 1), religious-enemy **plunder** of the population,
   then the province changes controller and PROVSIEGEWON / PROVSIEGELOST are sent.
5. Siege test interval is 30 days, but in land era 6 (land tech 51+) it shrinks:
   `max(7, 21 − 2 × (landTech − 50))` days.

---

## 1. Objects and fields

Siege combat = combat class vtable `0x5e8324` (secondary `0x5e8310`), constructor `0x46f820`,
log name "Siegecombat". It has the usual two sides: combat+0x30 attacker (vtable `0x5e83b0`,
"besieger side") and combat+0x34 defender (vtable `0x5e8434`, "garrison side"). combat+0x24 is the
**mode**: 3 cover, 4 siege, 5 assault. combat+0x28 = province; the city object is province+0x408
and the province's current siege combat is city+0x2e.

City fields used (city = province+0x408):

| offset | meaning |
|---|---|
| +0x2c (byte) | copied to combat+0x18 (`0x470cc0`); NIEPEWNE (shown in the result message) |
| +0x3a (float) | fortress level; garrison and thresholds use `trunc()` of it (`0x47f3e0` reads it) |
| +0x7e (float) | population |
| +0x82 | breaches (int) |
| +0x86 | artillery rating 0..3 |
| +0x8a | besieging leader's siege value (max over the besieger's units' leaders, `0x48f940`) |
| +0x8e | distress |
| +0x92 | defending leader's siege value (max over garrison-side leaders; normally 0) |
| +0x96 | fort level as int (copy of the garrison side's fort level, refreshed by `0x4703b0`) |
| +0x9a | defender base: **2 if province terrain (province+0xca) ≠ 0, else 0** (`0x4918fa`) |
| +0x9e (float) | starvation |

City virtual helpers seen: slot 0xc = "population ≥ 5000" (`0x4960e7`), slot 0x10 = "population
< 1000" (`0x496113`), slot 0x1c = province controller (province+0x3c8, `0x498170`).

Unit fields: +0xf0 infantry, +0xec cavalry, +0xe8 artillery (floats), +0x58 morale,
+0x48 / +0x4c the (up to two) combats the unit is in.

---

## 2. Siege creation and the garrison

### 2.1 When a siege combat starts (`0x4d1e20` → combat type 3)

A land unit standing in a province creates a siege combat (initial mode **3 = cover**) when, among
other checks: the province has a city with fort level `L ≥ 1`, the controller is not the unit's
owner, and the owner is at war with the controller (or the rebel/occupation predicates
`0x43b6e0` / `0x45f840` allow it). A province with fort level 0 has no siege: the controller changes
as soon as an enemy wins / enters (§6.3).

### 2.2 Garrison creation (constructor `0x46f820`, only on the host)

```
L      = trunc(city.fortLevel)                         (city+0x3a)
inf    = 5000 * L
art    = (controller predicate, country vslot 0x34) ? 10 * L : 0     (NIEPEWNE what it tests;
                                                                       treat as "true")
if city.population < 5000:  inf = inf / 2 ; art = art / 2   (integer, truncating)
cav    = 0
```

An event 0x28 carries (owner, unit type, inf, cav, art) to the garrison side, which builds one land
unit (`0x471020`): owner = the province **controller**, men as above, morale = controller's maximum
morale (`0x438690`). The unit type is looked up by a key held in a global (`0x6459a4`, NIEPEWNE:
the controller's standard infantry unit). The garrison is **not** reinforced afterwards; it only
loses men (siege attrition §3.3, assault losses §5).

The garrison side's fort level (slot 0x3c, `0x471180`) is always read live from the city.

The UI "GARRISON" line (`0x500d85`) shows the garrison side's infantry / cavalry / artillery sums
(`0x48f880`, `0x48f8c0`, `0x48f900`).

### 2.3 Mode thresholds (men = inf + cav + 100 × art of the besieger side, `0x48eec0`)

| check | where | result |
|---|---|---|
| combat start (`0x470b10`): men < 1000 × L | "Can't cover!" | besieger retreats, combat ends |
| unit joins in mode 3 (`0x46fef0`): men ≥ 5000 × L | | → mode 4 (siege) |
| every siege test (`0x470870`): men < 1000 × L | "Can't cover!" | besieger retreats |
| every siege test: 1000 × L ≤ men < 5000 × L | | → mode 3 (cover) |
| every siege test: men ≥ 5000 × L | "The siege continues..." | next test scheduled |

Mode 3 schedules no events at all (`0x46fd50` returns at once for mode 3), so a covered city does
not progress; it moves to mode 4 when reinforcements push the besieger to 5000 × L.
After any other combat in the province ends (`0x4d25e1`), a siege whose besieger can no longer
cover is lifted ("Attacker ... suffered too much casualties", besieger retreats).

---

## 3. Siege progress (mode 4)

### 3.1 Timing (`0x46fd50`, `0x47019f`)

- Entering mode 4 (from anything but 5) schedules the first test **30 days** later.
- After each test the next one is scheduled after `D` days:

```
country = besieger side's leading country (side+0x52)
if country exists and its land era (LAND.CSV Era column) == 6:     (land tech 51..60)
    D = max(7, 21 - 2 * (landTech - 50))       51→19, 52→17, 53→15, 54→13, 55→11, 56→9, ≥57→7
else
    D = 30
```

- Each due day (`0x470800`): if any besieger unit is in two combats at once (unit+0x48 and +0x4c
  both set, i.e. also fighting a field battle), nothing happens except rescheduling. Otherwise the
  leaders are refreshed and `0x470870` runs: the die `rand()%10 + 1` is rolled and sent as event 0x27
  (siege result, applied by `0x4704a0`), then the §2.3 strength checks run.

### 3.2 The test (`0x4704a0`)

Before the roll: garrison side+0x28 (cumulative attrition fraction) −= garrison side slot 0x38
(`0x46e1f0`, NIEPEWNE recovery); the city fields of §1 are refreshed (`0x4703b0`):

- artillery rating: besieger artillery pieces ≥ 40 L → 3, ≥ 25 L → 2, ≥ 10 L → 1, else 0;
- besieger leader siege → +0x8a, defender leader siege → +0x92, L → +0x96;
- **if the garrison has fewer than 100 men: distress = 15** (forces a surrender-level result).

```
mod   = (besiegerLeaderSiege + artilleryRating)                                   (0x471880)
      - (base + L + defenderLeaderSiege - trunc(starvation) - distress)          (0x4718d0)
total = roll(1..10) + mod
```

| total | log text | effect |
|---|---|---|
| ≤ 2 | – | nothing |
| 3–4 | "Status quo!" | nothing |
| 5–7 | "Food shortage!" | distress +1; garrison fraction +0.01; garrison loses men (`0x48f620`) |
| 8–10 | "Water shortage!" | distress +3; fraction +0.03; garrison loses men |
| 11–12 | "Breach!" | **breaches +1**, distress +2 |
| 13 | "Defender suffer from dessertation!" | fraction +0.1; garrison loses men |
| ≥ 14 | "Defender surrenders!" | garrison side retreats → §6 (no starvation update this test) |

The men lost in the shortage/desertion rows are applied by `0x48f620(fraction)` with the new
cumulative fraction (NIEPEWNE whether of current or original men; implement as
`men -= men × fraction`).

After every result except surrender:

```
if province has a port (province+0x41c) and is not blockaded (province+0x489 == 0):
    starvation -= 0.3 ; if starvation < 0.01: starvation = 0
else:
    starvation += 0.3
distress = min(distress, 8)
```

### 3.3 Breaches

Only the 11–12 result creates a breach; nothing in the siege code removes one (reset when the siege
ends: NIEZNANE, assume the city fields are cleared with the siege). Effects: distress +2 at once,
and in an assault with **breaches ≥ 1 the fort level is no longer subtracted** from the attacker's
modifiers (§5.3). UI: `CRBBREACH`, `gfx\interface\crb\breach.bmp`.

---

## 4. Ordering an assault

### 4.1 Button availability (`0x45dec0`, tooltip code `0x500600..0x50070a`)

The button is enabled when **all** hold for the selected units of country C:

1. at least one unit is selected;
2. C's land tech ≥ 5 (`0x432d80`, LAND.CSV "Allows Assault");
3. the selection's total infantry ≥ 1 (`0x48fad0`); otherwise the tooltip is `ASSAULT_NOINF`;
4. one selected unit is in a siege combat in **mode 4** (`0x48df30`; not in cover mode 3, not
   already mode 5);
5. C is that siege's attacker leading country (attacker side+0x52 == C);
6. the attacker side's morale ≥ **0.5 × C's maximum morale** (`0x438690`, constant 0.5 at
   `0x5e4380`).

Otherwise the normal tooltip is `BUTTON_0088`.

### 4.2 What the click does

`0x4432e0`: for each selected unit that is in a mode-4 siege, event 0x22 (`SIEGE_ASSAULT`) is sent to
its combat. On the host the combat manager (`0x4d1907`) calls `0x4707f0`: if the mode is not
already 5, change mode to 5 (event 0x23 `SIEGE_CHANGEMODE` → `0x470110`). The AI does the same
through `0x54ce29` / `0x54d360` ("AICombat #%d (%s) assaults the fortress!"; AI decision not covered
here).

### 4.3 Entering mode 5 (`0x470149`)

- log "The assault begins!"; both sides' leaders recomputed (`0x46cbb0`);
- combat start initialisation (`0x46a240`): both sides start (slot 0x54), morale logged, first phase
  = **fire** if either side has fire strength > 0.1, else **shock** (`0x46eff0`);
- pending siege-test events are cancelled; the next combat day is scheduled **tomorrow**; from then
  on the scheduler (`0x46fd50`) reschedules mode 5 every **1 day**.

The assault is therefore **not instant**: it takes days (normally ≥ 5, one phase), exactly like a
field battle.

---

## 5. The assault battle (mode 5)

Daily processing is the generic land-combat day (`0x46a460`, spec §7.2) with these differences.

### 5.1 Timeline

- Phases of 5 days, fire then shock; shock → fire again only if anyone still has fire strength.
- Each phase start: both sides roll `rand()%10` (0..9) and the per-day losses for the whole phase are
  computed once (`0x46dda0`); each day they are applied to the attacker, then the defender.
- 1 % leader-death roll per side at phase changes (as in field battles).
- **No overrun** (the 10 % instant destruction is only for combat types 1 and 2).
- If on a day the besieger also sits in another combat (two combats), the assault day is skipped.

### 5.2 Strength

| side | fire | shock |
|---|---|---|
| attacker (`0x48f800` → `0x4869d0`; `0x48f840` → `0x486ac0`) | Σ inf × vIF + art × vAF (art × 0.5 in marsh) | Σ inf × vIS + art × vAS |
| garrison (normal land strength, slots `0x46f2e0`/`0x46f2f0`) | inf × vIF + art × vAF (+ cav term, cav = 0) | inf × vIS + art × vAS |

Cavalry contributes nothing to the attacker. v** = the unit owner's LAND.CSV row /100; natives and
rebels as in §7.3 of the war spec.

### 5.3 CRT columns and modifiers

Columns by era (`index = 7 × ownEra + enemyEra`, LAND.CSV Era column 0..6 =
MEDIEVAL … DENTELLES; table rows = own era, columns = enemy era):

- attacker: fire column from `Assault Fire.csv`, shock column from `Assault Shock.csv` (`0x470d70`);
- garrison: **both** fire and shock columns from `Assault Defender.csv` (`0x470f90`).

DB/combat data (v1.09, for testing only — the game reads them from the player's files):

```
Assault Fire (attacker era rows x defender era cols)   Assault Shock
MED  C C D E E E E                                      B B C C D D D
REN  B C D E E E E                                      B B C C C D D
ARQ  B B C D D E E                                      A B B C C C D
MUS  B B B D D D E                                      A B B C C C D
BAR  B B B C C D E                                      A B B C C C C
MOV  B B B C C D D                                      A B B B C C C
DEN  A A B B C C D                                      A A B B B C C
Assault Defender (defender era rows x attacker era cols), used for fire and shock
MED  A B B B B B B
REN  A A A A A B B
ARQ  A A A A A A B   (MUS, BAR, MOV, DEN rows: all A)
```

Die-row modifier:

- attacker fire mod = leader fire bonus + terrain mod − (breaches == 0 ? L : 0) (`0x470dd0`);
  attacker shock mod = leader shock bonus + terrain mod + cav superiority − (breaches == 0 ? L : 0)
  (`0x470e20`). In practice leader bonuses are 0 in assaults (§7.5 of the war spec) and the siege
  sides never compute terrain (their column routine sets only columns), so effectively
  **attacker mod = −L without a breach, 0 with one**.
- garrison mod = leader bonus + terrain mod = 0 in practice (no fort bonus for the defender: the
  fort works only as a penalty on the attacker's row).

### 5.4 Losses and morale (unchanged engine, `0x46dda0`, `0x46d9d0`)

For the side X taking losses from enemy E in the current phase:

```
row = clamp(E.die + E.mod, 0, 15)
if E.strength > 0 and E.column != NA and E.morale > 0.4:
    X.menPerDay    = CRT_cas[col][row] * E.strength * 0.2
    X.moralePerDay = CRT_mor[col][row] * 0.2 * sqrt(max(1, X.maxMorale / 3))     (k = 1/3, 0x46dd30)
else 0
```

The 1/3 factor applies to **both** sides in mode 5 (field battles use 1/2). Men losses are split
over the side's units proportionally; a unit at 0 men dies. Morale reaching 0 breaks the side
(morale set to 0.4, retreat).

### 5.5 How the assault ends

| event | handler | result |
|---|---|---|
| garrison morale breaks, or garrison unit killed (`0x471420`) | defender retreat → `0x471470` | **assault won**, §6 |
| attacker has no fire and no shock strength and morale ≤ 0.4, or attacker morale breaks | attacker retreat override `0x470e70` | see below |

Attacker retreat in mode 5 (`0x470e70`, host only):

```
if attackerMen >= 5000 * L:  mode := 4   (back to the siege; next test in D days)
elif attackerMen >= 1000 * L: mode := 3  (cover)
else: real retreat (0x46d410); if the side is empty the combat ends
```

Leaving mode 5 does not reset breaches, distress or starvation; the garrison keeps its losses.
Attacker losses are permanent. A repulsed assault sends no won/lost messages and gives no VP
(it is not a combat end). The attacker may assault again once the button conditions hold (notably
morale ≥ 50 % of max, which usually needs monthly recovery).

---

## 6. Fortress taken (assault won or surrender) — `0x471470`

Both a won assault and a "Defender surrenders!" result end here (surrender: garrison retreat
→ event 0x24 → `0x469e40` → garrison slot 0x6c).

1. log "The fortress troop retreats!".
2. **Fort damage** (`0x471190(1.0)`): with probability 20 % (`rand()%100 >= 80`):
   `cut = min(1, L − 1)`; if `cut > 0` the fort level drops by `cut` (event 0x29 to the province).
   So: 20 % chance of −1 level, never below level 1.
3. **Plunder** (only if both sides have countries and the religion-enemy predicate `0x45faa0`
   holds for (garrison country, attacker country)): with `amount = attackerMen × 0.01`
   (`0x4712e0`):

```
if population > 5000:
    amount = min(amount, population - 5000)
    if amount < 1000: amount = 1000
    population -= amount            (event 0x2a to the province)
```

   Religion-enemy predicate (`0x45faa0`; ids: 0 protestant, 1 reformed, 2 counter-reform,
   3 catholic, 4 orthodox, 5 sunni, 6 shiite, 7 pagan):
   - true if either religion is pagan (7);
   - while world flag 4 (end of the Counter-Reformation, `0x4d16a0(4)`) is **not** set:
     true for protestant (0) vs catholic (3) or counter-reform (2), either way round;
   - before 1700: true for Christian (0..4) vs Muslim (5, 6), either way round;
   - true for sunni vs shiite;
   - otherwise false.
4. The garrison side is marked forced out (side+0x43) and its unit deleted (`0x471110`); the combat
   ends.

### 6.1 Combat end (`0x46a7d0`, `0x46aec0`, `0x4d24a0`)

- Winner = the side not forced out while the other is. For siege combats the messages are
  **0x91 PROVSIEGEWON** to the winner's countries and **0x92 PROVSIEGELOST** to the loser's
  (field battles use 0x8f/0x90). VP for these messages: see `docs/SPEC_VP` / our `siege_vp`.
- **Controller change** (`0x46affc` → `0x482b90`): if the province is land (province+0xca < 5) and
  the win is clean and the winner side has a leading country, the province controller becomes that
  country. (For a field battle, type 1, this only happens when the fort level ≤ 0.)
- The combat is deleted (event 0x1b); units left in the province without a combat are re-checked
  (`0x4d1c40`), which is how a new siege would start in another province.

If the besieger is the one forced out (it could not even cover), the garrison side "wins" and the
messages go the other way; the controller stays (NIEPEWNE whether that message is shown as a lost
siege to the besieger).

---

## 7. Implementation notes for `src/military.c`

Current code (`mil_assault`): instant `roll + rating + siege − fort − (men < 2 × garrison ? 2 : 0)`
against 6, garrison `1000 × L`, loss formula invented. Replace with:

1. Give each siege a garrison record `{inf, art, morale}` created when the siege starts
   (`5000 L`, `10 L`, halved below 5000 population; morale = controller max morale). Use it in the
   siege thresholds' messages, the GARRISON UI line and the `< 100 men → distress 15` rule; apply the
   shortage/desertion fractions to it.
2. `mil_assault` = validate §4.1 (tech 5, inf ≥ 1, mode 4, own siege, morale ≥ 0.5 × max), then set
   mode 5 and run the existing battle engine day by day with: attacker strength without cavalry,
   garrison as a normal land side (no cav), Assault Fire/Shock/Defender columns, attacker row
   modifier −L while breaches == 0, morale factor k = 1/3, no overrun, no terrain, no leader bonus.
3. Attacker breaks / runs dry → mode 4 / 3 / retreat by the 5000 L / 1000 L thresholds.
   Garrison breaks or dies → §6 (20 % fort −1 min 1, religious plunder, controller change, siege
   won/lost notes and VP) — the same path as a surrender.
4. Siege test interval: 30 days, era-6 formula above; skip a due day while the besieger is also in a
   field battle in that province (we already skip sieges while `inbat`).
5. Fixed point: all the constants are exact in integers (0.01 / 0.03 / 0.1 fractions in 1/1000,
   starvation in tenths: ±3, 1/3 morale factor: compute `sqrt(max(1, M/3))` from M in 1/1000).

## 8. Open points

- Country predicate that gives the garrison artillery (country vslot 0x34 in `0x46fa16`): NIEZNANE
  (assume true).
- Unit type of the garrison (`0x6459a4` key): NIEPEWNE (only matters for the sprite).
- Breach / distress / starvation reset when a siege ends: NIEZNANE.
- `0x48f620` fraction applied to current or original men: NIEPEWNE.
- Garrison side slot 0x38 (`0x46e1f0`) "recovery" subtracted from the attrition fraction: value
  NIEZNANE.
