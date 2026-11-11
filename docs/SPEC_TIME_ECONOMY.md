# EU2 v1.09 - time step and economy (specification)

This spec comes from reading `eu2.exe` (GOG v1.09) in Ghidra and capstone. No code was copied.
Every claim names the function or data address it came from, so it can be checked again. Field
offsets are the original's and are given only as anchors for re-checking; the port uses its own
structures. Markers: **NIEPEWNE** = believed but not proven, **NIEZNANE** = not found.

Floating point: the original uses x87 `float` throughout. Values that **accumulate** (treasury,
inflation, research progress, manpower pool, population, war exhaustion, badboy, envoy stocks,
CoT counters) need a fixed-point format with enough fraction bits. The monthly steps add amounts
like 1/1200 to them (see the section on precision).

**Coverage warning.** The Ghidra export misses a number of functions. The month dispatcher of
provinces (`0x480730`), the yearly country handler (`0x451250`) and the date-advance event handler
(`0x4d1380`) are not in `eu2_all.c` and were read from the disassembly instead. Some
argument-passing in the decompiled text is wrong (`__ftol` with no visible argument), so constants
were checked in the disassembly wherever a formula depends on them.

## Contents

1. [Architecture: events, not a loop](#1-architecture-events-not-a-loop)
2. [Calendar](#2-calendar)
3. [Objects and fields used below](#3-objects-and-fields-used-below)
4. [Monthly processing](#4-monthly-processing)
5. [Income formulas](#5-income-formulas)
6. [Expenses, budget split, investments](#6-expenses-budget-split-investments)
7. [Inflation, bankruptcy, loans](#7-inflation-bankruptcy-loans)
8. [Population, manpower, envoys, war exhaustion, badboy](#8-population-manpower-envoys-war-exhaustion-badboy)
9. [Goods prices and centres of trade](#9-goods-prices-and-centres-of-trade)
10. [Yearly processing](#10-yearly-processing)
11. [Data files: which column goes where](#11-data-files-which-column-goes-where)
12. [Worked example: France, 1 Jan 1419](#12-worked-example-france-1-jan-1419)
13. [Precision notes for the fixed-point port](#13-precision-notes-for-the-fixed-point-port)
14. [Open questions](#14-open-questions)

---

## 1. Architecture: events, not a loop

The game is driven by an event bus. Every action is an event object with a 16-bit type id. The
names come from the id-to-name switch at `0x4b95a0`. The ids that matter here:

| id | name | sent by | meaning |
|---|---|---|---|
| `0x175` | `TIMER_DATE` | real-time clock `0x416600` | "advance the date by n days", n in 1..10 |
| `0x16` | `TIMER_MONTH` | date advance `0x416840` | the month number changed |
| `0x17` | `TIMER_YEAR` | date advance `0x416840` | the year number changed |
| `0xc0` | NIEZNANE | date advance `0x416840` | sent together with `TIMER_YEAR` |
| `0x18` | `TIMER_TICK_LOOP` | main loop `0x5b47c9` | once per main-loop pass (per frame), not per day |
| `0x19` | `TICK_ANIMATIONS` | clock `0x416600` | real milliseconds elapsed |
| `0x50` | `GAME_STARTED` | | several receivers treat it like a month tick (initial compute) |

Event posting (`0x4b8700`, `0x4b8850`, `0x4b87e0`, queue insert `0x4c0de0`): an event can be

- dispatched synchronously,
- queued for "now",
- queued with a **date**: the date is stored in the event header and the event goes into a
  date-sorted queue (`0x4c17b0`).

Things that happen "on a day" are dated events, not a daily scan: unit arrival dates, colonist
arrival (`0x4dc620` schedules one for tomorrow), sieges, and so on. There is **no per-day economy
pass**. The economy runs only on `TIMER_MONTH` and `TIMER_YEAR`, plus a per-frame check
(`TICK_LOOP`, see 7.2).

### 1.1 Real-time clock (`0x416600`)

- The ms-per-day value comes from the game-speed setting through the table at `0x619edc`:
  **266, 133, 100, 67, 34, 17, 11, 6** ms per game day (speeds 1..8, NIEPEWNE which UI speed is
  which index).
- Each call adds (now − last) / msPerDay to a float day accumulator. The integer part n is taken
  out of the accumulator and **capped at 10**, then `TIMER_DATE(n)` is queued. Days above the cap
  are lost.
- The accumulator advances only when the dated queue is empty or its head is not yet due
  (NIEPEWNE: the comparison's receiver is hidden in the decompile). In practice the clock waits
  until every event dated today or earlier has run.

### 1.2 Date advance (`0x4d1380` → `0x416840`)

The `TIMER_DATE` handler calls `0x416840(n)`, which:

1. remembers the old month and year,
2. adds n days (`0x417130`),
3. clamps the date: if after **1 Jan 1820** (`0x071c0000`) it is set to exactly that; if before
   **1 Jan 1419** (`0x058b0000`) it is set to 1 Jan 1419,
4. if (`0x4196e0`, believed "simulation running / host") the month changed, posts one
   `TIMER_MONTH`; if the year changed, posts `TIMER_YEAR` and then event `0xc0`.

Because n can be up to 10, **one month or year event fires per jump, never two.** A jump from
28 Jan to 8 Feb fires one `TIMER_MONTH`. A 30-day month cannot be skipped by a 10-day jump, so in
practice no month is lost.

### 1.3 Who receives the month and year events

| receiver | function | on `TIMER_MONTH` | on `TIMER_YEAR` |
|---|---|---|---|
| each country | dispatcher `0x461e90` → vtable slot `0x268` = `0x451300` | country monthly (§4.1) | slot `0x264` = `0x451250` (§10) |
| each province | dispatcher `0x480730` (jump table `0x480cbc`) | `0x480370` (trade, production, revolt roll), then `0x47c930` | nothing |
| goods market | `0x41b5e0` | prices `0x41b620`, then `0x41bcd0` (production leader) | |
| trade manager (`DAT_00647d40`) | `0x41a680` | `0x499e92` (CoT counters), `0x49a0dd` (CoT values) | `0x499ba2` (CoT yearly) |
| diplomacy matrix | `0x4cb760` | per-country refresh | |
| loan objects | `0x42d940` region (`0x42da11`) | monthly interest (§7.3) | |

Country `TICK_LOOP` (slot `0x260`) = `0x450ad0`, which checks bankruptcy (§7.2).

The order in which receivers get the same `TIMER_MONTH` is the registration order on the bus.
**NIEPEWNE** (not traced). Provinces post their trade and production income straight into the
country ledgers (§5), and the country monthly reads the ledger pool. The ordering between province
and country handlers therefore decides whether this month's trade income is split by this month's
or next month's budget.

---

## 2. Calendar

- **Date record**, 4 bytes, little-endian (`0x4171a0`, `0x417130`, `0x417220`):
  byte 0 = day **0..29**, byte 1 = month **0..11**, int16 at +2 = year. As a 32-bit value it is
  `year<<16 | month<<8 | day`, so 1 Jan 1419 = `0x058b0000`.
- **Every month has 30 days and every year has 360 days.** The constants are `DAT_00619f24` = 30
  and `DAT_00619f28` = 12. There are no leap years and no real month lengths in the simulation.
  - Day arithmetic converts to a day count `(year·12 + month)·30 + day`, adds, and converts back.
  - Add-months (`0x4171a0`) works on `(year·12 + month + k)·30 + day`.
  - Date differences (`0x417340`, `0x417440`) are in these 360-day units. For example, "fewer than
    35 days since war" in §8.4 is 35 internal days.
- **Display day** (`0x417100`): shown day = internalDay × realLength[month] / 30, integer division,
  with real lengths **31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31** (`0x619f2c`; February
  always 28). Internal day 0 shows as 0. Day 0 is the first day of the month, so the UI's "1st"
  probably adds 1 elsewhere (NIEPEWNE, check the formatting code).
- Month names: text keys `JANUARY`..`DECEMBER` through the pointer table `0x631060`, indexed by
  byte 1.
- The current date lives in the timer object `DAT_00647c2c` at +0x14.

---

## 3. Objects and fields used below

Arrays (all fixed):

- **Countries**: 340 pointers at `0x64e140..0x64e690` (index = tag slot; also the size of the
  per-CoT merchant arrays).
- **Provinces**: 2020 records of `0x4a0` bytes at `*DAT_008077d4`.

Only the fields this spec relies on are listed.

**Country** (vtable `0x5e5ac0`):

| offset | meaning |
|---|---|
| +0x254 | state religion index (religion.csv row order) |
| +0xe42 | badboy (float) |
| +0xe46 | technology block: +0x0c land, +0x24 naval, +0x3c infrastructure, +0x54 trade, +0x6c stability; tech group at +0x84 |
| +0xe70 | treasury (float) |
| +0xe74 | ledger object (§6.1) |
| +0xe78 | war-taxes months left (> 0 = active) |
| +0xe84 | inflation (float fraction, 0.01 = 1 %) |
| +0xe88 | **budget pool**: this month's regular income waiting to be split |
| +0xe94 | colonists (float stock); merchants, missionaries, diplomats have their own stocks via vtable slots 0x60/0x5c/0x68 (offsets NIEZNANE) |
| +0xea4 … +0xeb8 | budget sliders, fractions summing to 1: +0xea4 naval tech, +0xea8 land tech, +0xeac infra, +0xeb0 trade, +0xeb4 stability, +0xeb8 treasury (§6.3) |
| +0xec0 | manpower pool (float) |
| +0xec4 | yearly manpower rate (float) |
| +0xef2 | fine-arts academies |
| +0xef6 | weapon manufactories |
| +0xefa | naval-equipment manufactories |
| +0xefe | refineries (trade-efficiency bonus) |
| +0xf02 | goods manufactories (infra-efficiency bonus); NIEPEWNE, tooltip `TWINFRA_MAN` |
| +0xfd2 | shipyards |
| +0xfd6 | conscript centres |
| +0xfde | loan size (default 200, savegame key `loansize`) |
| +0x122a | war exhaustion |
| +0x12c4 | land maintenance slider |
| +0x12c8 | naval maintenance slider |
| +0x20bc | monarch; skill getters `0x43bf10` ADM, `0x43bf40` DIP, `0x43bf90` MIL, each 3 when there is no monarch |
| +0x37e | domestic-policy block: 8 int sliders 0..10 at +0x08..+0x24 (§3.1) |
| +0x38e | diplomacy object; byte +0x47cc there = **at war** (`0x43c000`) |

**Province:**

| offset | meaning |
|---|---|
| +0x54 | goods object |
| +0x84 | pirate level |
| +0xaa | centre of trade this province belongs to; the CoT's home province id is at CoT+4 |
| +0xb6 | base tax (province.csv `Income`) |
| +0xba | manpower (province.csv `Manpower`) |
| +0xd8 | culture |
| +0xe2 | mine value (gold) |
| +0xf6 | religion |
| +0x106 | continent |
| +0x3c4 | owner |
| +0x3c8 | controller |
| +0x3cc | looted flag |
| +0x408 | city object |
| +0x40c | colony / trading post |
| +0x489 | blockaded |
| +0x498 | land-connection group (compared with the capital's) |

**City** (vtable `0x5e8e60`):

| offset | meaning |
|---|---|
| +0x7e | population (float, max 999 999) |
| +0x6e | city size 0..6 |
| +0x72 | settlement size |
| +0x7a | production multiplier |
| +0x1c | manufactory type 0..5 |
| +0x68..+0x6c | five promotion / official flags (§3.2) |

City virtuals:

- `+0xc` isCity: population ≥ 5000
- `+0x10` isColony: population < 1000
- `+0x18` production multiplier
- `+0x14(x)`: x × goods base value × infra efficiency / 12

### 3.1 Domestic policy sliders

The policy block at country+0x37e holds 8 ints, read as s0..s7 at +0x08, +0x0c, … +0x24. The
mapping to names below is **NIEPEWNE**: it assumes the order of the country.csv columns. Which end
of each slider is "low" is also unproven. The worked example (§12) suggests the formulas treat the
csv values as given.

| idx | offset | country.csv column (assumed) |
|---|---|---|
| s0 | +0x08 | Aristocracy / Plutocracy |
| s1 | +0x0c | Centralisation |
| s2 | +0x10 | Innovative / Narrowminded |
| s3 | +0x14 | Mercantilism / Free trade |
| s4 | +0x18 | Offensive / Defensive |
| s5 | +0x1c | Land / Naval |
| s6 | +0x20 | Quality / Quantity |
| s7 | +0x24 | Serfdom / Free subjects |

Write d_i = s_i − 5 (−5..+5). The policy terms used below:

| term | formula | function |
|---|---|---|
| trade efficiency (percentage points) | −d0 − d5 | `0x42b6c0` |
| production efficiency (pp) | d5 + 2·d1 − 2·d7 − d0 | `0x42b6f0` |
| war-exhaustion cap | 10 + 0.4·d1 + 0.4·d2 | `0x42b7b0` |
| manpower factor | 1 + 0.05·d5 − 0.05·d6 | `0x42b840` |
| colonists per year | −0.4·d2 − 0.2·d3 + c5, with c5 = −0.4·d5, halved if negative | `0x42b870` |
| merchants per year | −0.4·d3 | `0x42b8c0` |
| missionaries per year | −0.4·d2 | `0x42b820` |
| tech cost factor | 1 − 0.02·d1 − 0.03·d2 | `0x42b7e0` |
| stability cost factor | 1 + 0.05·d2 − 0.1·d7 | `0x42b770` |

### 3.2 City promotion flags

The city constructor zeroes 5 bytes at +0x68. Their effects, as found:

| flag | tax | production | growth | other |
|---|---|---|---|---|
| +0x68 | | | | market demand counter only |
| +0x69 | | | | manpower ×2 + 1 |
| +0x6a | base +1 | +1/12 per month | | revolt +3; census +0.75 |
| +0x6b | | +1/12 per month | +1 (`POPGRWTH_CITYRIGHTS` "Governor") | counts as a mayor for deflation |
| +0x6c | base +1 | | | revolt −1 |

The names (bailiff/tax collector, judge, mayor, governor …) are **NIEPEWNE**. The infra-level texts
mention "Bailiffs promoted to Tax collectors" and "Legal counsels promoted to Chief judges".

---

## 4. Monthly processing

### 4.1 Country monthly, `0x451300`, in this exact order

AI subclasses at `0x557a20`, `0x55bca0`, `0x55bf70` and `0x55e8f0` call this as their base. Steps
marked "AI only" use the per-tag player table at `DAT_0091a738`: 0 = AI, 1 or 2 = human.
Difficulty D is the `int` at `*DAT_008c0bfc + 0x2ca`, believed to be 0..4.

1. **Badboy decay.** k = 0.25. If (`0x4196c0` false, and D > 2) then k = 0.125, or 1/6 when
   D < 4 (so D = 3 → 1/6, D = 4 → 1/8). Then badboy −= k × DIP × 0.2 / 12, floored at 0.
2. **War exhaustion** (§8.4).
3. AI special case `0x463100` (game-type 3 only; out of scope).
4. **Blockade share** (`0x43d200`). country+0x120 = (blockaded provinces among those that have a
   land connection or are on the capital's continent, and have field +0x41c) ÷ (count of such
   provinces). It feeds the "no land connection" tax penalty (§5.1).
5. **Yearly manpower rate** recomputed, `0x43ce50` (§8.2).
6. AI military bookkeeping (`0x43c010`).
7. If at war, war-declaration notifications (diplomacy; out of scope).
8. Leader / mission AI (`FUN_0045...`, out of scope).
9. **Tax income** T = Σ yearly province tax / 12 (§5.1).
   - If this country is a vassal: the overlord gets 0.5·T as income category 5 ("Vassals"), and
     the vassal keeps 0.5·T.
   - The war-taxes counter (country+0xe78) is decremented if > 0.
   - T is posted as income category 0.
10. **AI difficulty bonus** (AI only, at war, D ≥ 2): extra tax income
    ((country+0x1386) × 0.1 + B) × (D − 1) / 12 + 5, with B = 10 for Sunni or Shiite state religion
    (index 5 or 6) and 15 otherwise. If D ≥ 3, also extra manpower (D − 2) through slot 0x58.
    Country+0x1386 is NIEZNANE (believed: number of provinces).
11. **Trade tax (tolls)**, category 10: Σ over own provinces of the province trade tax (§5.4) / 12.
12. **Trade tariffs**, category 11: Σ over all CoTs of the CoT tariff for this country (§5.5) / 12.
13. Sub-object month hooks (country+0x3b3 vtable +0x30; NIEZNANE, likely the event/AI scheduler).
14. Fleet counts by ship class (`0x43b0d0`).
15. **Maintenance** `0x43a270`: six expense posts (§6.2).
16. **Ledger month rollover** `0x42c650` (§6.1).
17. Budget pool floored at 0. A copy of the pool goes to country+0xe80 ("last month's income";
    used for the support limit and the UI).
18. **Budget split** `0x458df0` (§6.3).
19. **Monthly inflation** (§7.1).
20. **Cities**, `0x457c00`: culture assimilation, manufactory income (category 3), population
    growth (§8.1), manpower accrual (§8.2).
21. **Diplomats** `0x458d70`, **merchants** `0x458dd0`, **colonists** `0x457eb0`, **missionaries**
    `0x457f90` (§8.3).
22. Score / victory points `0x55f130` (out of scope).
23. Relation drift `0x43c970` (diplomacy; out of scope).
24. Forward the month event to the country's armies, fleets and AI (`0x463000`).
25. If host: diplomatic AI `0x460aa0`.
26. Colonial dynamism bonus (country+0x37a) reset to 0 (`0x4385d0`).
27. Ranking: country+0x370 = 1 + (number of countries with a higher score). The score used
    depends on the game type (`DAT_00647d48`).
28. Misc refresh `0x43aa30`. It recounts country+0xe64 (CoTs in own provinces), +0xe68/+0xe6c
    (trade embargo counts) and +0xe60 (vassal count). **These counts are used next month.**

### 4.2 Province monthly, `0x480370` then `0x47c930`

Weather (`0x4825b0`) runs first. Then:

1. **Revolt roll.** Only if field +0x12c > 0 (NIEPEWNE: believed "revolt possible / rebels
   active") and the province has an owner. r = (field +0x13c) − 2 for a city, or − 4 for a colony.
   If r > 0, r = r/2 (integer). Then r += ftol(…), source NIEZNANE; if r > 0, r = r². For a
   colony, r −= 10, capped at 5. A revolt fires when rand() % 1000 < r.
2. **Trade income** (§5.3), posted to every country with merchants in this province's CoT.
3. **Production or gold** (§5.2), posted to the controller, unless the city is blockaded.
4. `0x47c930` sets province+0x140, an AI-only colonisation attractiveness value.

### 4.3 Market and trade manager monthly

Goods prices `0x41b620` (§9.1) and CoT counters `0x499e92` / values `0x49a0dd` (§9.2).

---

## 5. Income formulas

All values are in ducats. "Yearly" values are what the province window shows (`0x47ce00`). The
ledger receives /12 of them each month.

### 5.1 Province tax (`0x47e270`, modifier `0x47e0b0`)

There is tax only if the province has a city that is a real city: no colony / trading-post object,
and the city's isColony virtual false.

`yearlyTax = (baseTax + flag6a + flag6c) × M × (0.5 if blockaded)`

baseTax is floored at 0. The tax modifier M (`0x47e0b0`, shown as `PROVTAX_*`):

| term | value |
|---|---|
| start | 1.0, or **2.5** if war taxes are active (country+0xe78 > 0) |
| culture not accepted (`0x429fb0` on province culture) | −0.3 |
| religion differs from state religion | −0.3 |
| no land connection to the capital (province+0x498 ≠ capital's) | −0.1, or −(0.5 × blockadeShare + 0.01 × X + 0.1) |
| religion.csv `Tax Income` / 100 of the state religion | + |
| stability | + (stabMod − 1), see below |
| revolt risk | − 0.05 × RR (`0x481a30`) |
| result | floored at 0 |

Details:

- **Religion comparison** (`0x431270`): equal indices match, and Counter-reformed (2) and
  Catholicism (3) are treated as the same religion. A province of religion 0 (Protestantism)
  counts as Catholic (3) until world flag 1 is set (`0x4d16a0(1)`); religion 1 (Reformed) counts as
  Catholic until flag 2 is set. So before the Reformation, "protestant/reformed" provinces in
  province.csv cost nothing.
- **No-land-connection penalty**: the second form applies when province+0x41c ≠ 0, or
  (+0x498 ≠ 0 and `0x472fbf`). X = `0x42ba40` = 2 × (field +0x1c of an object NIEZNANE − 5).
  NIEPEWNE.
- **Stability**: stabMod[s] = (tax_stab.csv Modifier + 100) / 100 (`0x431840`: 7 rows, stability
  clamped to −3..3). With the shipped csv: +3 → 1.2, +2 → 1.1, other values → 1.0.
- **Revolt risk** RR (`0x481a30`) is a long integer formula over religion, culture, stability, war
  exhaustion, officials, occupation and years since acquisition. It is only partly decoded
  (open question 6).

Monthly country tax (`0x457a00`): Σ over provinces with owner = controller = this country, not
looted, with a city, of yearlyTax / 12 → category 0. War taxes give 2.5× (text `TWR_WT`: "150 %
extra").

**Census tax** (yearly, `0x457b60` → `0x457a70`, category 9). For each owned, controlled, unlooted
province:

`share = 0.25 (if a core: country+0x1350 list) + 0.75 (if flag6a) + s1 × 0.025`

share is capped at 1. census += yearlyTax × share. Posted once per year.

### 5.2 Production and gold (`0x480690`, monthly values)

For a province with a city:

- Gold (goods = the gold object `DAT_0066c37c`):
  `mineValue × cityMult / 12`. **No** infrastructure term in this branch. Posted as category 4.
  Gold income is withheld when the province's continent is 2 and the controller's byte +0xfe2 is
  set (NIEPEWNE: believed "Spanish/Portuguese treasure-fleet rule").
- Other goods (city virtual +0x14): `cityMult × goodsBaseValue × infraEff / 12`, where
  goodsBaseValue is the **integer base value from goods.csv** (goods+0x114), not the market price.
  Posted as category 2.
- Then +1/12 for each of flags 0x6a and 0x6b, and ×0.5 if blockaded.

**cityMult** (city+0x7a, set by `0x4911f0` whenever population changes):

- City (population ≥ 5000): cityMult = size/6 + 1, where size is the first k (0..6) with
  population ≤ {5 000, 10 000, 20 000, 40 000, 80 000, 200 000, ∞}[k] (`0x620254`).
- Settlement (population < 5000): cityMult = settlementSize/10, where settlementSize = k+1 for the
  first k with population < {200, 300 … 1200}[k] (`0x620218`; default 10). A quirk:
  population 1100..1199 gives 11.
- Colony or trading post without a city object: production NIEZNANE (the virtual lives on the
  colony object); trade uses colonySize × 0.1 + 0.4 (§5.3).

**infraEff** (`0x43b560`) = infra.csv Efficiency / 100 for the current infra level
+ religion `Prod. Eff.` / 100 + 0.01 × country+0xf02 + 0.01 × productionPolicy (§3.1),
floored at 0.01.

### 5.3 Trade income (`0x480370` → share `0x480ea0`)

The province trade value per month (`0x480e30`):

`V = max(0, F × marketPrice / 12 − 0.5 × pirates)`

- F = cityMult for a city, or colonySize × 0.1 + 0.4 for a colony.
- marketPrice = goods+0, the float price from §9.1.

For each country c with a merchant entry in this province's CoT (CoT+0x28 + 4c), when the province
has an owner and a city or colony:

1. n = ftol(merchant count, entry+4). If n = 6 (believed "monopoly"), n = 26 − (CoT total
   merchants at +0x20).
2. share = n × 0.05.
3. c's production statistics for this goods type += F × share.
4. income = tradeEff(c) × V × share. If the CoT home province's continent ≠ c's capital continent,
   multiply by the overseas factor `0x43d1c0`:
   0 if c+0x1f84 = 0, else 1 − c.blockadeShare − 0.01 × X.
5. If c is under embargo by the controller (`0x4cbf60` false), the plain V × share is booked into
   the merchant's annual statistic instead (`entry+0x10 += 12 × V × share`) and still posted.
   **NIEPEWNE** which branch applies the efficiency; the two branches look swapped in the decompile.
6. Posted to c as category 1 (category 4 if the goods is gold).

**tradeEff** (`0x43b610`) = trade.csv Efficiency / 100 for the trade level
+ religion `Trade. Eff.` / 100 + 0.01 × refineries (+0xefe) + 0.01 × tradePolicy
+ 0.02 × (CoTs in own provinces, +0xe64) − 0.03 × (embargo counts +0xe68 + +0xe6c),
floored at 0.01.

### 5.4 Trade tax ("Tolls", category 10) (`0x47cbf0`)

Yearly, for a province whose owner equals its controller:

- tradeEff × colonySize (colony), or
- tradeEff × (size + 10) (city ≥ 5000), or
- tradeEff × settlementSize (smaller city).

The province window calls it "Trade Taxes (Max 12$)".

### 5.5 Trade tariffs (category 11) (`0x47cb90`)

A CoT's home province pays its controller **3 × (total merchants in the CoT)** per year, unless the
city is blockaded (`0x4961f2`). Matches text `PIWINC_DELAYED4`.

### 5.6 Manufactories (category 3) (`0x4805d0`)

Yearly income per city with manufactory type t (1..5): **12** if the manufactory matches the
province's goods, else **6**.

| type | goods ids that count as matching |
|---|---|
| 1 | needs city flag +0x2c |
| 2 | 8, 9 |
| 3 | 4, 11 |
| 4 | 15, 17 |
| 5 | 1, 3, 16 |

The goods ids are the internal ids at goods+4. Mapping them to names is NIEPEWNE (the constructor
`0x41ad30` assigns them by name). Posted monthly as /12 from `0x457c00`.

### 5.7 Other income categories

| id | key | source |
|---|---|---|
| 0 | INCOMETAX | §5.1 |
| 1 | INCOMETRADE | §5.3 |
| 2 | INCOMEPRODUCTION | §5.2 |
| 3 | INCOMEPRODFAC | §5.6 |
| 4 | INCOMEGOLD | §5.2 |
| 5 | INCOMEVASSAL | vassal half-tax (§4.1); yearly 2 × `0x460390` in `0x451250` |
| 6 | INCOMELOAN | interest received from loans you gave (§7.3) |
| 7 | INCOMEGIFT | events and gifts (`0x453530`) |
| 8 | INCOMESOW | peace treaties |
| 9 | INCOMEDTAX | census tax (§5.1) |
| 10 | INCOMEEXPORTDUTIES ("Tolls") | §5.4 |
| 11 | INCOMEHARBORFEES ("Trade Tariffs") | §5.5 |

---

## 6. Expenses, budget split, investments

### 6.1 Ledger (`0x42c650` .. `0x42c780`, object at country+0xe74)

- 12 income and 16 expense float categories. Each has a running current month (+0x74 / +0xa4),
  last month (+0x58 / +0x88), year-to-date (+0x154 / +0x184) and last year (+0xe4 / +0x114).
- Monthly rollover copies current → last month, sums the total into +0x1cc / +0x1d0, and zeroes
  current.
- Yearly rollover (`0x42c6c0`, called from `0x4e5330`) does the same for the year arrays.

**AddIncome(cat, x)** (`0x438a30`):

- Categories 0..5, 10, 11 ("regular income") are added to the **budget pool** (+0xe88).
- All other categories (loan, gift, peace, census) go **straight to the treasury** (vtable slot
  0x3b0 = treasury += x).
- Both cases also book the ledger.

**AddExpense(cat, x)** (`0x438ab0`): books the ledger. For categories 0 and 1 (army or fleet
maintenance), if x > treasury, the part above the treasury is taken from the budget pool first, up
to the size of the pool. The remainder is subtracted from the treasury, which may go negative.

Expense categories (`0x42c970`):

| id | key | id | key |
|---|---|---|---|
| 0 | army maintenance | 8 | merchants |
| 1 | fleet maintenance | 9 | technology |
| 2 | raising armies | 10 | stability |
| 3 | building fleets | 11 | trade & infra |
| 4 | fortresses | 12 | manufactories |
| 5 | colonies | 13 | repaid loans |
| 6 | trading posts | 14 | interest |
| 7 | missionaries | 15 | gifts / events |

### 6.2 Maintenance (`0x43a270`, monthly)

There are six posts: infantry `0x459c40`, cavalry `0x459e20`, artillery `0x45a000` (category 0);
warships `0x45a1f0`, galleys `0x45a3f0`, transports `0x45a5f0` (category 1). Each is posted with the
AddExpense rule above.

**Unit cost** (`0x4298d0` .. `0x429a60`):

`unitCost = max(1, trunc( trunc(base + h) × (1 + inflation) ))`

- base: infantry 9, cavalry 21, artillery 30, warship 50, galley 10, transport 15.
- h = D − 2 for human players, 0 for AI.

Domestic-policy unit cost changes (text `DOMESTIC_*_INF`) are **not** in this function. They are
believed to apply to build costs only (NIEPEWNE).

**Monthly maintenance of one class:**

`base part = landSlider × unitCost × count × k`

- k = 1/120000 for infantry and cavalry (count in men), 1/1200 for artillery (count in pieces),
  1/480 for ships.
- landSlider = country+0x12c4 (land) or +0x12c8 (naval).

**Over-limit surcharge**, only for human players, or for everyone when D < 1:

- troops = infantry + cavalry + 100 × artillery
- excess = (troops − supportLimit) × 0.001
- If excess > 0: surcharge = unitCost × slider × (classCount / troops) × excess / 24
- For ships the same pattern applies with ships = warships + galleys + transports and the naval
  limit (constants not separately verified, NIEPEWNE).

**Support limit** (land, men; `0x459c40`, tooltip `0x45abe0`):

`5000 + 1000·ftol(e80 × 0.2 × (0.1·s5 + 0.5)) + 2000·ftol(ec4) + 10000·conscriptCentres + 5000·weaponManufactories + 2000·ftol(grainProduced, +0xf1e) + 1000·ftol(grainTraded, +0xf72)`

Here e80 = last month's income and ec4 = yearly manpower.

### 6.3 Budget split (`0x458df0`, monthly, after maintenance)

With pool P (≥ 0) and slider fractions f, in this order:

1. **Naval tech**: if research is possible (not at maximum level), progress +=
   P·f + 5·navalManufactories + navalNeighbourBonus + MIL (`0x434260`), and P·f is booked as
   expense 9. Otherwise P·f goes to the treasury and **the slider is set to 0**.
2. **Land tech**: the same, with 5·weaponManufactories + neighbour + MIL (`0x4341b0`).
3. **Trade**: the same, bonus `0x4343c0` (refineries and ADM, NIEPEWNE), expense 11.
4. **Infrastructure**: the same, bonus `0x434310` (goods manufactories, ADM, country bonus),
   expense 11.
5. **Stability**: if stability < +3, progress += P·f + (5·fineArts + 2·ADM − warExhaustion)
   (`0x431a40`), expense 10. Otherwise P·f goes to the treasury and the slider is zeroed (the UI
   slider is re-normalised for the player).
6. **Treasury**: treasury += P·f.
7. P = 0. Then an empty base-class hook (slot 0x24 is the release-build no-op `0x514d80`) and the
   tech-progress UI refresh `0x43b3c0`.

The "+bonus" amounts are added to research progress every month **even when the slider is 0**:
they are part of the `0x4339xx` add-progress call, and the call happens only when research is
possible. NIEPEWNE whether a 0 slider still makes that call; the code multiplies f × P but calls
regardless.

### 6.4 Research progress and costs

Add-progress (`0x432590`, research vtable `0x5e58c0`, stability vtable `0x5e5880`):

1. If the level can still rise: progress += x, floored at 0.
2. If progress ≥ cost: progress −= cost and level += 1. **At most one level per month**; the
   carry is kept.

**Level cost tables** (loader `0x432f30`):

- land.csv → table `0x64dad0`, used by the object at +0x0c (**land**)
- naval.csv → `0x64dae0` (+0x24, **naval**)
- infra.csv → `0x64df20` (+0x3c)
- trade.csv → `0x64dab0` (+0x54)

61 rows each for land and naval, 11 for infra and trade, plus a sentinel row of cost 99 999 999.

For row k with year Y_k (column 2: "Average Date for Next" or "Cost") and previous row year
Y_{k−1}, starting at 1418, with integer division:

`baseCost_k = ((Y_k − 1400) × 137 div 300 + 275) × (Y_k − Y_{k−1})`

Values (verified with a script on the shipped csv):

| table | row 0 (1419) | row 1 | row 2 | row 3 |
|---|---|---|---|---|
| land / naval | 283 | 2 880 (1429) | 6 237 (1450) | 4 560 (1465) |
| infra | 283 | 1 716 (1425) | 7 425 (1450) | 14 310 (1495) |
| trade | 283 | 1 716 (1425) | 15 450 (1475) | 8 000 (1500) |

The last real row is 9999, which gives about 34 million: effectively unreachable.

**Cost to go from level L to L+1** (`0x4323a0`):

`cost = max(1, baseCost_L × (1 + inflation)) × (2 − G) × S × P × R × A`, then ftol.

- G by tech group (tech block +0x84 = 0..4): 1.0, 0.9, 0.8, 0.6, 0.1, otherwise 0.01. So (2 − G)
  ranges 1.0..1.99. The group-to-csv-name mapping is NIEPEWNE, believed latin = 0.
- S = size/organisation factor `0x434a90`:
  - by number of provinces N (country+0xe5c): 0.5 (N ≤ 1), 0.6, 0.7, 0.75, 0.8, 0.9, 0.95
    (N = 7); for N ≥ 8, N/8, and above 1 compressed to 1 + (N/8 − 1) × 0.1;
  - forced to 1 if `0x45b940` < 100;
  - − religion `Techspeed` / 100;
  - − 0.01 × `0x460390` if `0x43ce10`;
  - − min(0.02, (10 − s1) × 0.2 × vassals × 0.02);
  - clamped to 0.5..2;
  - then, on a normal map with the capital on continent 1 and country+0xe4e < 5:
    + (5 − e4e); and if country+600 < 20: + (2 − 0.1 × country+600).
  - Several of these inputs are NIEZNANE.
- P = tech policy factor (§3.1).
- R = 1.15 if this country is its religion's leader (`0x4312a0`: table `0x64d4bc` per religion).
  NIEPEWNE.
- A = ahead-of-time factor: if L > 0 and Y_{L−1} > currentYear + 10, then
  A = 1.5 + 0.1 × (Y_{L−1} − currentYear). Otherwise A = 1. The code uses row L−1 here and
  row L for the base cost; reproduce it as written.

**Stability cost** (`0x4315a0`): sum over the country's cities:

| case | cost |
|---|---|
| colony (population < 1000) | +5 |
| city, same religion | 25 |
| city, different religion, Christian state (state religion 0..4), province religion 0..4 | 50 |
| city, different religion, Christian state, province religion > 4 | 100 |
| city, different religion, non-Christian state | 60 |

For cities, the religion's `Stab bonus` / 100 is subtracted from the per-city cost, with a minimum
of 5 per city. Then:

`cost = (Σ + 2 × |country+0x1308 list|) × (1 + inflation) × stabPolicy × (1 − 0.01 × min(50, fineArts)) × (1 + min(3, badboyFactor))`

- The +0x1308 list is NIEZNANE, believed vassals or cores.
- badboyFactor uses lim = `0x45aef0` = DIP + 30 + (year − 1419) div 20, plus (year − 1790) from
  1791 on:
  - badboy ≤ lim/2 → badboy × 0.02
  - else badboy ≤ lim → × 0.04
  - else × 0.08
- Then × (1 − 0.01 × `0x460390`) if `0x43ce10`.
- Then × max(0.8, 1 − (10 − s1) × 0.2 × vassals × 0.02).
- Then ftol.

Stability stays in −3..+3 (`0x4318d0`, `0x431900`).

---

## 7. Inflation, bankruptcy, loans

Inflation is a float fraction (0.01 = 1 %) at country+0xe84. It never goes below 0 (clamped in
`0x43c8e0` and in the yearly step). It multiplies unit costs, research costs and stability cost.
No direct effect on income was found.

### 7.1 Monthly change (`0x451300`, after the budget split)

`Δ = treasurySlider / 1200 − mayorDeflation / 1200`

- treasurySlider is the fraction 0..1.
- mayorDeflation = 0.25 × (cities with flag6b) / (number of provinces) (`0x457bb0`).
- So 100 % to treasury gives +1 % per year. The yearly mayor deflation is mayorDeflation % (text
  `TWRINFLATION`, `0x51d4a0` at `0x51db41`).

AI only (player table = 0): a year-based ceiling replaces Δ once it is exceeded:

- cap = min(1, max(0, year − 1419) × 0.0025); multiplied by (goldShare + 1.05) when goldShare > 0
  (NIEPEWNE: 1.05 is read as a double); × 1/3 at D = 4, × 1/2 at D = 3.
- If inflation > cap: Δ = −(mayor/1200 + 0.0001)
- If > 1.5·cap: Δ = −(mayor × 0.001667 + 0.0005)
- If > 2·cap: Δ = −(mayor × 0.01 + 0.001)

### 7.2 Yearly gold inflation and bankruptcy

**Gold inflation** (yearly, `0x451250`): inflation += goldShare × 0.0025 when goldShare > 0.4,
where goldShare = gold / (tax + trade + production + gold) from the year-to-date ledger
(`0x45b240`). Example: 60 % gold → +0.15 percentage points per year.

**Bankruptcy** (every frame, `0x450ad0`): when treasury < 0 and the country cannot borrow
(`0x45bce0`):

- the player gets a message (AI: `0x4405a0` / notifications);
- the bankruptcy date is stored at country+0xe9c;
- stability −3 (`0x436490(3)`);
- every research track −1000 progress (floored at 0);
- stability progress −(current value);
- inflation += 0.01 × (number of loans) + 0.05;
- all bank loans are dropped;
- treasury = 0 and country+0xea0 = 0;
- a host-side event `0x5f` is posted (value 1000, 7).

The revolt risk adds +1 for 10 years after the bankruptcy date (`0x481a30`).

### 7.3 Loans (partial)

- **Loan object** (month receiver `0x42da11`): debtor tag (+0x2a), lender (+0x22 / +0x24; the bank
  is a special id at `0x64d4b4`), amount (+0x1a, int), interest rate in percent (+0x16, int), due
  date (+0x1e).
- **Monthly interest** = amount × rate / 1200 → the debtor's expense 14. If the lender is not the
  bank, the lender gets income 6 of the same amount.
- **At the due date**: the loan-expired dialog (extend or pay; events `REQ_EXTENDLOAN` /
  `REQ_PAYLOAN`). Repaying books expense 13.
- **Bank loan size** = country+0xfde (default 200; scenario/save key `loansize`). Text:
  "Take Loan from the Bank (%d$ for 5 years)", and "Your credits does not give you any more loans"
  (limit believed 5).
- **Taking a loan** is country command 0x65 (vtable +0x168 = `0x448fb0`): a loan object (`0x42d880`)
  with the message's rate (+0x1c → loan +0x16) and amount; the rate is fixed per loan.
- **Bank rate** `0x45bb00` (percent a year): trunc((10 − stability) + 3 × country+0xebc − trunc(last
  month's income country+0xe80) × 0.01 + 0.2 × (bank loans) + country+0xea0); −3 for the country
  indices 1 and 4 of the exe's table (NIEZNANE which tags); +25 while today is before the last
  bankruptcy (+0xe9c) + 5 years; at least 5; halved when country+0xfdc is set (event command `bank`,
  vtable +0x354; `stock` sets +0xfdb, +0x350). country+0xea0 is only ever reset (bankruptcy, ctor).
- **Extending** a due loan (`0x449210`): country+0xebc += 1 and stability −1.
- **Credit limit** `0x45bc00`: a country in debt (treasury < 0) with fewer than 3 bank loans
  (`0x439cb0`) may always borrow; otherwise it may if Σ (amount × rate / 1200) over its loans plus
  the new loan's (loan size `0x4389e0` = country+0xfde × the current rate / 1200) is at most 0.75 × last
  month's income (country+0xe80; when the scenario is a saved game, e+0x1cc instead - NIEPEWNE).
  TWR_5LOANS is the refusal text.

---

## 8. Population, manpower, envoys, war exhaustion, badboy

### 8.1 Population growth (monthly, per city, `0x457c00`; rate `0x47e940`)

The yearly growth rate g (per mille) is the sum of:

| term | value |
|---|---|
| stability | stability (−3..3) |
| base | +4 |
| centre of trade in this province | +5 |
| per neighbouring CoT | +2 |
| manufactory | +2 |
| colony (not a city), unless continent 2 on the normal map | +5 |
| enemy troops present | −5 |
| occupied (controller ≠ owner) | −3 |
| flag6b (governor) | +1 |
| siege or blockade | −5 |
| looted | −5 |

Plus a **location** term, only when not fantasy, the province is not on the capital's continent,
and the city is < 5000:

- climate (+0xc6) 4 or 6: −8 if this or a neighbour has +0xca < 5, else −1
- continent 3: −4 or −1 by the same test
- continent 5: −12 or −2
- otherwise: −0.5 × (+0x124) when +0xca < 5 somewhere
- then scaled by population: × 0.25 (> 3500), × 1/3 (> 2500), × 0.5 (> 2000), × 2/3 (> 1000)
- floored at −14

(+0xca is believed to be the province's colonisation difficulty; NIEPEWNE.)

Monthly update:

1. newPop = pop × (1 + g / 12 × 0.001)
2. The monthly increase is capped at baseTax + 200 people.
3. A rounding guard compares the integer old and new values (NIEPEWNE; the decompile is garbled).
4. A city that is not a colony is never set below 1000.
5. If the result is < 10: in a CoT home province it is set to 10; otherwise the city is destroyed
   (`0x47d800`, message `0x8a`).
6. If the province has pending colonists (+0x12c) and population > 900, add 100 × that count
   (`0x482a20` resets it).
7. Population is capped at 999 999, and city size and multipliers are re-derived (`0x4911f0`).

Country+0xfae counts the colonies.

### 8.2 Manpower

Per province, yearly (`0x47cc80`): requires a city that is not a colony, and the province is
land-connected to the capital or on the capital's continent.

1. m = manpowerColumn × 0.25 × (size div 3 + 1)
2. If flag69: m = 2m + 1
3. If the culture is not accepted: −1
4. If not a core: −1 when the religion is a non-matching religion (with the same pre-Reformation
   remapping as tax) and the province is not religion 7, or when it was acquired less than 10 years
   ago (+0x3be = acquisition year)
5. Floored at 0

Country:

- **Yearly rate** ec4 (recomputed monthly, `0x43ce50`) = Σ m × (1 + 0.05·d5 − 0.05·d6), with a
  minimum of landLevel × 5 div 60 + 2. Plus, for each vassal on the capital's continent:
  vassal.ec4 × (10 − s1) × 0.05. A special tag `DAT_008d0e18` gets + `0x460390`. ×12 for the host
  in debug mode (`0x419700`).
- **Monthly accrual** (end of `0x457c00`): add max(1, Σ m / 12 × manpowerFactor) to the pool, only
  while pool < 2 × ec4 (slot 0x58 = `0x457b80`). There is no clamp, so the pool can overshoot by
  one month's worth.
- Manpower and the pool are in thousands of men (NIEPEWNE: inferred from the ×1000 / ×2000 in the
  support limit).

### 8.3 Envoys (monthly accrual = yearly rate / 12)

**Colonists** (`0x457eb0`, slot 0x64 = `0x43d590`):

- Fantasy map: 6 per year.
- Otherwise, if country+0x36c ≥ 0.001 (believed "has a coast / port share") or the
  frontier-country byte (+0x1f8c) is set:
  `rate = religion Ann.Col / 100 + frontier (1) + min(1, shipyards) + colonistPolicy + colonial dynamism (+0x37a)`,
  floored at 0. Otherwise 0 ("No coast").
- The stock (+0xe94) is capped at **6** (`0x458d10`) and floored at 0.

**Diplomats** (`0x458d70`): rate = religion Ann.Dip / 100 + max(1, DIP − 5) + (1 if at war).
Slot 0x68; the cap is believed 6 (tooltip).

**Missionaries** (`0x457f90`): rate = religion Ann.Miss / 100 + missionaryPolicy. Slot 0x5c; cap
believed 6.

**Merchants** (`0x458b50`), the sum of:

| term | value |
|---|---|
| fantasy map | 6 (replaces everything) |
| (+0xfba + +0xfb6) / 2, believed "protestant coastal provinces" | capped at 3 |
| trade level | div 5 |
| CoTs (+0xfc6) | + |
| coastal provinces (+0xfbe) | div 25 + 1, only if +0x36c ≥ 0.001 |
| merchantPolicy | + |
| stability + 1 | clamped to −3..3 |
| monopolies: CoTs where this country's merchant count > 5 | + |
| result | clamped to 0..**12** |

Slot 0x60; the stock cap is believed 6.

The scenario files store fractional stocks (France 1419: `merchants = 1.083333`), which confirms
monthly fractional accrual.

### 8.4 War exhaustion (country+0x122a, monthly, step 2 of §4.1)

- cap = war-exhaustion cap (§3.1). Humans: unscaled, f = 1. AI: D = 0 → ×1, f = 1;
  D = 1 or 2 → ×0.5, f = 1; D = 3 → ×0.5, f = 2; D = 4 → ×1/3, f = 3.
- Not at war: WE −= 0.25 × f per month.
- At war: if the list `DAT_008ff614` holds an entry involving this country dated less than 35 days
  ago (believed: recent combat), WE += (1/36, or 1/45 if this country is its religion's "leader"
  per `0x4312a0`) / f. Otherwise WE −= 0.0125 × f. NIEPEWNE what the list holds.
- WE is clamped to 0..cap.
- Effects found: −WE to monthly stability investment (§6.3) and +WE-related revolt risk.

### 8.5 Badboy

- Monthly decay is in §4.1 step 1: about 0.05 × DIP per year at the normal rate.
- The "limit" lim (`0x45aef0`) is used by stability cost and diplomacy.

---

## 9. Goods prices and centres of trade

### 9.1 Market price (`0x41b620`, monthly)

There are 21 goods objects (goods.csv rows). Each month:

1. **Supply** (+0x11c) is reset to 0.5. Then +0.01 for every province with a city or colony
   producing that goods, clamped to 0.5..1.5.
2. **Demand** (+0x120) is computed from world-wide counters:

   | counter | meaning |
   |---|---|
   | a | cities with flag69 |
   | b | cities with flag68 |
   | c | cities with flag6c |
   | e | cities with flag6b |
   | M | cities with any manufactory |
   | m2..m5 | manufactories of types 2..5 |
   | n1 | provinces producing goods id 1 |
   | w | provinces producing ids 3, 15 or 16, counting 1 under a Christian owner and 0.1 otherwise |

   By goods id:

   | goods id | demand |
   |---|---|
   | 1 | 1.0 |
   | 2 | 0.5 + 0.01 (n1 + m5) |
   | 3, 5, 11, 16, 17 | 0.25 + 0.01 (b + a + M) |
   | 4, 6, 9, 13, 15, 18, 19 | 0.75 + 0.01 (c + e + b + a + M) |
   | 7 | 0.5 + 0.01 (m2 + a) |
   | 8 | 0.5 + 0.01 (m2 + b) |
   | 10 | 0.5 + 0.01 (m3 + b) |
   | 12 | 0.1 w |
   | 14 | 1 + 0.01 m4 |

   Each is clamped to 0.01..2.0. Several groups also have an upper cap: 1 for the 0.25 group,
   2 for the 0.75 group, 3 for id 12. The id-0 case is lost in the decompile (NIEZNANE).
3. **Price**: price (+0) = baseValue × (2 − supply) × demand.

The goods-id-to-name mapping is NIEPEWNE (ids are assigned in `0x41ad30`; gold is the object
`DAT_0066c37c`).

### 9.2 Centres of trade

There are up to 0x114 CoTs (`DAT_00647d40`). Each has a per-country merchant array of 340 entries
at +0x28; each entry holds a merchant count at +4.

**Monthly counter** (`0x499e92`): with t = total merchants (+0x20) and k = number of countries
present:

`f = (1 if anyone has > 5 merchants) − 0.5·[t > 19] − 0.5·[t > 15] − 0.5·[t > 12] + 1·[t < 8] + (2 if k < 2, 1.15 if k < 6, −0.5 otherwise)`

CoT+0x14 += f / 12, clamped to 0..110. Every merchant entry with count > 5 also adds 0.16 to its
country's score.

**Monthly value** (`0x49a0dd`): CoT+0x24 = Σ over member provinces of 12 × V (§5.3).

**Yearly** (`0x499ba2`):

1. Every CoT with counter ≥ 100 is dissolved (`0x49a71e`). Its province loses the CoT link, and
   its field +0xb2 is reduced by 35 (believed "CoT historical modifier").
2. CoT membership is re-evaluated for every province (`0x49aa0f`; attractiveness `0x47f400`).
3. New CoTs are founded while the count is below the limit `0x49a1d7` (`0x49a7c2`).
4. The CoT indices are renumbered.

The full membership and placement rule is out of scope here (AI/geography; NIEPEWNE).

---

## 10. Yearly processing

Per country, on `TIMER_YEAR` (`0x451250`, slot 0x264):

1. Increment a global year counter (`0x4764c0`).
2. Gold inflation (§7.2).
3. If rand() % 100 < 4·ADM + 4: base-class hook (empty in release; AI subclasses may use it,
   NIEPEWNE).
4. Inflation floored at 0.
5. Census tax (§5.1).
6. If `0x43ce10` (believed "has vassals" or "is overlord"): income category 5 = 2 × `0x460390`.
7. Forward the event to armies, fleets and AI (`0x463000`).

World: CoT yearly (§9.2), event `0xc0`, and the ledger year rollover (`0x42c6c0` from the UI or
host object `0x4e5330`; NIEPEWNE that it fires exactly on the year event).

**Not yearly in this engine:** population growth, envoy accrual, badboy decay and stability
investment are all monthly. The "yearly" figures in tooltips are the monthly rate × 12.

---

## 11. Data files: which column goes where

- **tax_stab.csv** (`0x431840`): 7 rows of (stability, modifier %). Stability is clamped −3..3;
  stabMod = (mod + 100) / 100.
- **religion.csv** (`0x431340`): rows in file order form the religion index (0 Protestantism,
  1 Reformed, 2 Counterreformed, 3 Catholicism, 4 Orthodox, 5 Sunni, 6 Shiite, 7 Paganism,
  8 Confucianism, 9 Buddhism, 10 Hinduism). Every number is stored /100:

  | column | table |
  |---|---|
  | Techspeed | `0x64d540` |
  | Stab bonus | `0x64d648` |
  | Prod. Eff. | `0x64d5f0` |
  | Trade. Eff. | `0x64d61c` |
  | Tax Income | `0x64d598` |
  | Morale | `0x64d4e8` |
  | Ann.Col | `0x64d56c` |
  | Ann.Dip | `0x64d5c4` |
  | Ann.Miss | `0x64d514` |

- **land.csv / naval.csv** (`0x431e60`, `0x431d10`): the year column feeds the cost formula
  (§6.4); the next six ints are /100 combat modifiers, then two floats and two ints (combat, not
  covered here).
- **infra.csv / trade.csv**: year → cost (§6.4); Efficiency / 100 → infraEff / tradeEff.
- **goods.csv** (`0x41b490`): name, base value (int, also the initial price), mine flag,
  trade-post flag, slaves.
- **province.csv**: `Income` → base tax (+0xb6), `Manpower` → +0xba, `MineValue` → +0xe2,
  `Goods` → +0x54. The mapping of the other columns is not needed for the economy (and not
  verified).
- **traders.csv**: not referenced by the code paths read here (NIEZNANE; maybe AI).

---

## 12. Worked example: France, 1 Jan 1419

Inputs come from `Scenarios/1419/1419_fra.inc` and `DB/province.csv`:

- 9 owned and controlled provinces, all French culture, all cities.
- Stability +1, all techs at level 0..1, inflation 0, treasury 400.
- Army 30 000 infantry and 7 000 cavalry; fleet 10 warships and 1 transport.
- Policies taken from country.csv FRA: 8, 3, 2, 6, 5, 5, 4, 8.

| prov | name | base tax | manpower | goods | population | size | cityMult | manpower m |
|---|---|---|---|---|---|---|---|---|
| 385 | Ile de France (capital) | 15 | 5 | grain | 60 000 | 4 | 1.667 | 5·0.25·2 = 2.50 |
| 376 | Champagne | 13 | 5 | wine | 12 000 | 2 | 1.333 | 1.25 |
| 383 | Picardie | 6 | 5 | cloth | 13 000 | 2 | 1.333 | 1.25 |
| 386 | Nivernais | 9 | 5 | grain | 13 000 | 2 | 1.333 | 1.25 |
| 407 | Dauphiné | 9 | 5 | metal | 10 000 | 1 | 1.167 | 1.25 |
| 408 | Lyonnais | 13 | 5 | grain | 12 000 | 2 | 1.333 | 1.25 |
| 417 | Vendée | 7 | 6 | salt | 8 000 | 1 | 1.167 | 1.50 |
| 422 | Languedoc | 11 | 5 | wine | 16 000 | 2 | 1.333 | 1.25 |
| 423 | Guyenne | 11 | 5 | wine | 9 000 | 1 | 1.167 | 1.25 |

**Tax.** 407, 422 and 423 are "reformed" in province.csv, but Reformed counts as Catholic until the
Reformation flag is set, so there is no religion penalty. M = 1 + 0 (stabMod(+1) = 1.0) + 0
(Catholic tax 0) − 0.05·RR. Assuming RR = 0 everywhere (RR not decoded; open question 6), yearly
tax = Σ base = **94**. The ledger INCOMETAX gets **7.83 per month**. Census tax per year =
94 × (0.25 + 3 × 0.025) = **30.6** (no officials at start).

**Manpower.** Σm = 12.75. Factor = 1 + 0.05·0 − 0.05·(4 − 5) = 1.05.

- Yearly rate = 13.39. The minimum is 1·5 div 60 + 2 = 2, so 13.39 stands.
- Monthly accrual = max(1, 12.75 / 12 × 1.05) = **1.116** (thousand) until the pool reaches 26.8.

**Production** (yearly, ×1/12 per month). Σ cityMult × base value = 8.33 + 20 + 20 + 6.67 + 17.5
+ 6.67 + 17.5 + 20 + 17.5 = **134.2**. Then:

- infra level 0 → 0.20;
- Catholic production efficiency 0;
- productionPolicy = d5 + 2·d1 − 2·d7 − d0 = 0 − 4 − 6 − 3 = −13 → −0.13;
- infraEff = 0.07, which gives about **9.4 per year**.

If the policy polarity guess is wrong (open question 1), infraEff would be 0.20 + 0.13 = 0.33,
about 44 per year. That makes this a good first test against the original's province window.

**Army maintenance** (monthly, before the over-limit surcharge, slider s):

- infantry 9 × 30 000 / 120 000 × s = 2.25 s
- cavalry 21 × 7 000 / 120 000 × s = 1.225 s
- warships 50 × 10 / 480 × s = 1.04 s
- transport 15 / 480 × s = 0.03 s

Support limit = 5000 + 2000 × 13 + 1000·ftol(e80 × 0.2 × 1.0) + grain terms, about 31 000+ against
37 000 troops. So a human France pays a small surcharge (about 6 000 men over, infantry share 81 %:
9 × s × 0.81 × 6 / 24 ≈ 1.8 s).

**Research.** Land level 1 → 2 has base cost 6 237 (row 1 is 2 880, for level 0 → 1). Final cost
needs the size factor S (N = 9 → 1.0125) and inputs that are NIEZNANE, so it is not computed here.

---

## 13. Precision notes for the fixed-point port

- **Inflation** changes by slider/1200 per month (minimum step about 8e-4 percentage points). It
  needs at least 1e-6 resolution. Suggestion: 16.16 with inflation stored ×100 (percent), or
  2.30 as a fraction.
- **Population** grows by pop × g/12000. For pop = 1000 and g = 1 that is 0.083 people per month,
  and a 16.16 population (max 999 999 < 2^20) keeps it. The original rounds nothing here, so
  integer population would freeze small colonies. Use at least 8 fraction bits.
- **Treasury, budget pool, ledger** are sums of /12 values. Use 1/256 ducat or finer, with a
  signed range of at least ±2^23.
- **Research progress** reaches about 34 million on sentinel rows. Use 32-bit integer ducats plus
  a fraction (e.g. 24.8), or saturate the sentinel.
- **Envoy stocks** (0..6, monthly steps of about 0.03) and **war exhaustion** (steps of 1/36 / f):
  8.8 is fine.
- **Badboy** decay of 0.25 × DIP × 0.2 / 12, about 0.004 per month: 8.8 is too coarse; use 16.16.
- **Integer truncation in the original** that the port must reproduce exactly:
  - unit cost: trunc(base + h), then trunc(×(1 + inflation)), minimum 1;
  - tech base cost: integer division by 300;
  - city size div 3 in manpower;
  - revolt-roll integer maths;
  - ftol (truncation toward zero) in the support-limit terms and in the merchant count.
  - x87 `__ftol` truncates toward zero, not floor; negative values matter in stability + 1 and in
    the growth terms.
- **Calendar**: integer day counts (year·360 + month·30 + day) fit in 32 bits trivially. Display
  day = day × len / 30.

---

## 14. Open questions

1. Domestic-policy slider order and polarity (s0..s7 versus country.csv columns, and which end is
   0). Test against the original's tooltips `DOMESTIC_*`.
2. Receiver order for `TIMER_MONTH` (provinces versus countries versus market). It decides whether
   trade and production income enter this month's or next month's budget split.
3. Event `0xc0` posted with `TIMER_YEAR`: name and effect.
4. Exact real-time pacing: which speed index matches which UI speed; the `0x417220` comparison in
   the clock.
5. Loans: bank interest rate, credit limit ("5 loans"), extend/pay handlers. These are in
   functions Ghidra missed (search the `REQ_GETLOAN` / `EXEC_GETLOAN` ids in the dispatchers).
6. Revolt risk `0x481a30`: the religion and culture base terms are hidden behind `__ftol` with
   invisible arguments. Re-read in the disassembly. It enters tax (−5 % per point) and revolts.
7. Colony and trading-post production: the colony object's virtuals were not traced. Trade uses
   colonySize × 0.1 + 0.4.
8. Goods-id-to-name mapping (the constructor `0x41ad30`), needed for manufactory matching and for
   the demand formulas.
9. City flags 0x68..0x6c: names and how they are gained (infra levels / promotions).
10. Naval support limit and naval over-limit constants (`0x45a1f0` family): same shape as land,
    numbers not checked.
11. Unknown inputs of the tech size factor `0x434a90` (+0xe4e, +600, `0x45b940`) and of the
    research bonuses `0x4341b0`, `0x434260`, `0x4343c0`, `0x434310` (neighbour bonus `0x4344c0`).
12. Country+0x1386 (AI bonus), +0x1308 list (stability cost), +0x36c (coast share), +0xfc6/+0xfbe
    (merchant terms): confirm meanings.
13. Whether the budget-split call is made at 0 % slider (so monthly bonuses still accrue) when
    research is possible.
14. Trade-income embargo branch in `0x480370`: which branch applies trade efficiency.
15. traders.csv: where it is used (not on the economy paths read).
16. AI `TICK_RESOURCE` (`0xe0`, `0x545710`, parameter 2..8): resource ticks of the AI. Not part of
    the rules, but it may move money (not checked).
