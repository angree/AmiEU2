# Envoys: merchants, colonists/traders, missionaries (rules + layout)

This file records rules and layout facts only, read from the `eu2.exe` v1.09 disassembly and the
Ghidra export. No code was copied. Conventions are as in `layouts_iw*.md`:
- Positions are window-relative (224x344 at screen (0,103)).
- `prov` is the province record `0x8077d4 + id·0x4A0`; `country` is the sending country (the
  player, `[this+0x82]`).
- Constants: K = 0.01 (`0x5e4eec`). `infl` = country[0xE84] clamped to ≥ 0. "×(1+infl)" means the
  price is multiplied by (1 + inflation).

Province fields used below (names chosen for our data model):

| Offset | Meaning | Evidence |
|---|---|---|
| prov[0xB6] | base tax (CWMAXTAX "Base Tax Value: %d") → `income_m` | `0x4fbe45` |
| prov[0x12C] | native size (shown ×100 as "Strength") → `natives` | `0x4fc2a7` |
| prov[0x13C] | native aggressiveness 0..9 | `0x4fc2f8` |
| prov[0x130] | native attitude to traders (**NIEPEWNE**) | `0x437260` |
| prov[0x124] | colonisation difficulty (climate/terrain hardship, **NIEPEWNE** name) | `0x436b90`, `0x436960` |
| prov[0xC6] | terrain class; 4 and 6 are the "hard" ones (**NIEPEWNE**: probably jungle/arctic or similar) | |
| prov[0x487] | coastal flag (byte) | `0x4fc81b` |
| prov[0x498] | continent id | `0x45c770`, `0x436b90` |
| prov[0x408] | settlement (colony/city): pop float +0x7E, colony level +0x72, vfunc +0x10 = "is a colony", +0x2C = "is a city" | |
| prov[0x40C] | trading post: level float +0x04 | |
| prov[0x3C4] | owner | |
| prov[0xD8] | culture | |
| `0x47ca10(prov)` | religion | |
| prov[0x114] / prov[0x11C] | neighbour count / list (12-byte entries) | |

Country fields used below:

| Field | Meaning |
|---|---|
| [0xE70] | treasury |
| [0xE94] | colonist stock (shared by colonists **and** traders) |
| [0x254] | state religion |
| [0x382] | country culture list (`0x429fb0` = "culture is ours") |
| [0x1F7E] | capital province |
| `0x45b7d0` | capital province record |
| `0x43bf10` | monarch ADM (the country window's ADM pips) |
| `0x436670(prov)` | best skill (+0x26) of our conquistador/explorer leaders present in `prov` (leader types 2/3) — HINT_77 |

---

## 1. Merchants

See the merchant section at the end of this file (merged from `envoys_merchant.md`).

---

## 2. Colonists and traders

### 2.1 Flow

1. BUTTON_COLONIST (sidebar, id 43) → colonisation map mode → click a province.
2. The window `iw_Colonist.bmp` opens (ctor `0x4fb550`, vtable `0x5eb768`, draw `0x4fbc60`,
   button tooltips `0x4fc750`, info tooltips `0x4fcb40`).
3. It has two columns: **Trader** and **Colonist**.
4. **Send Colonist** calls `0x43efc0(prov)`; **Send Trader** calls `0x43f230(prov)` (event handler
   `0x4fb880`: `0x4fb99a`, `0x4fb9f1`).

### 2.2 Window layout

Sidebar title: HEAD_COLP "Settler Placement" + province name (`0x4fbd08`).

| Element | Position | Font / colour | Content | Evidence |
|---|---|---|---|---|
| CWSENDC "Expand Establishment in" | centred (112,11) | g14 `#FFC000` | | `0x4fbdf1` |
| Province name | centred (112,25) | white (font **NIEPEWNE**, probably g24b) | prov[0x04] | `0x4fbe40` |
| CWMAXTAX "Base Tax Value: %d" | left (76,55) | g14 `#FFC000` | prov[0xB6] | `0x4fbe8e` |
| Settlement line | left (76,67) | g14 `#FFC000` | CWCITY "Colony, %d settlers" (int pop) / CWTP "Level %d Trading Post" / CWNATIVEVILL "Native Settlement" (if natives) / CWNOSET "(No Settlement)" | `0x4fbe93..0x4fbf63` |
| Settlement picture | centred at (44,66) | settlement-type bitmap (frame from its +0x4E/+0x52) or `gfx\empty.bmp` | | `0x4fbf6c..0x4fc051` |
| CWTRADER / CWCOLONIST column headers | centred (136,92) / (178,92) | g14 `#FFC000` | | `0x4fc112`, `0x4fc144` |
| CWTIME "Time to establish:" | left (28,110) | g14 `#FFC000` | | `0x4fc080` |
| CWCHANCE "Suitability:" | left (28,125) | g14 `#FFC000` | | `0x4fc0af` |
| CWCOST "Cost:" | left (28,140) | g14 `#FFC000` | | `0x4fc0e0` |
| Column values (each column) | centred at x = 136 / 178; y = 110 time, 125 chance, 140 cost | g14 | time `"%d"` months (years·12 + months); chance `"%d%%"`; cost `"%d $"` | `0x4fbab0` |
| Value colours | | | time/cost `#FFC000`, grey `#808080` when the column is disabled. Chance: red `#E00000` if ≤20, `#FFC000` if ≤60, green `#00E000` if >60, grey when disabled | `0x4fbb37..0x4fbc0e` |
| CWNATIVE "NATIVES" | centred (130,174) | g14 `#FFC000` | | `0x4fc23e` |
| `piw\buildings\native.bmp` (6 frames) | (24,161) | frame by native state | | `0x4fc3eb..0x4fc473` |
| Province religion icon | (74,163) | `religions.bmp` (11 frames) | | `0x4fc3b7..0x4fc3dd` |
| CWNATIVESTR "Strength:" / value | (24,198) / right @192 | g14 `#FFC000` | `"%d"` prov[0x12C]·100 | `0x4fc270`, `0x4fc2f3` |
| CWNATIVETOL "Aggressiveness:" / value | (24,212) / right @192 | g14 `#FFC000` | aggression 0 CWNNONE, 1–2 CWNVLOW, 3–4 CWNLOW, 5–6 CWNMEDIUM, 7 CWNHIGH, 8 CWNVHIGH, ≥9 CWNEHIGH | `0x4fc2f8..0x4fc399` |
| Trade-good icon | (24,238) | `piw\buildings\resource.bmp` (21 frames) | frame = goods | `0x4fc48f..0x4fc4b2` |
| Trade-good name | centred (136,238) | g14 `#FFC000` | | `0x4fc5da` |
| CWPRO "Current Market Price" | centred (136,259) | g14 `#FFC000` | | `0x4fc4e6` |
| Price tier | centred (136,276) | g14 `#FFC000` | CWNONE..CWEHIGH | `0x4fc586` |
| COWSENDTRA "Send Trader" | centred x=66, y=298 | text button g14b | | `0x4fc652` |
| COWSENDOL "Send Colonist" | centred x=158, y=298 | text button g14b | | `0x4fc618` |
| COWBACK "Back to Main" | centred x=112, y=319 | text button g14b | | `0x4fc6a0` |

Tooltips (`0x4fcb40`):
- CCWEMPTY2 / CCWEXPAND2 (colony);
- CCWEMPTYTP2 / CCWEXPANDTP2 (trading post);
- CCWROLLGOOD (price; "Basic Price / Supply / Demand / Current Market Price");
- CCWNATIVEROLLER (aggressiveness).

### 2.3 Button conditions (tooltip text when the button is disabled, `0x4fc750`)

**Send Colonist**, checked in this order:
1. Colonist stock country[0xE94] < 0 → CCWCOLONISTS "We do not have any colonists to send". The
   comparison is against 0.0, not 1 — **NIEPEWNE** whether the stock is stored as count−1 or the
   check really allows fractions.
2. Cost `0x436960` > treasury → CCWCOLONISTSMONEY.
3. Not coastal (prov[0x487] = 0) and no neighbour owned by us → CCWCOASTAL "You can only establish
   colonies in coastal provinces and provinces adjacent to an owned province".

When enabled, the tooltip is:
- CCWEMPTY "Establishes new colony in %s" — no settlement and no TP;
- CCWTP "This will expand our Trading Post to a Colony" — a TP only;
- CCWEXPAND "Expands our colony in %s" — a colony.

**Send Trader**, checked in this order:
1. `0x432ef0(country tech)` false → CCWTPLEVEL "Requires Trade Level 2".
2. A colony/city exists → CCWCITY.
3. TP level > 5 → CCWTPMAXXX (a TP stops at level 6).
4. Colonist stock < 0 → CCWCOLONISTS. **Traders use the colonist stock.**
5. Cost `0x437110` > treasury → CCWTPMONEY.

When enabled, the tooltip is CCWEMPTYTP or CCWEXPANDTP.

The other refusal texts (CANTSENDCOLONIST1..3, CANTSENDTRADER1, CBNOCASH) exist in `text.csv`.

### 2.4 Colonist cost (`0x436960`, float, also the price charged at sending)

| Step | Value |
|---|---|
| base B | 1 if a TP exists; otherwise colony level (+0x72) for a colony, 2·(+0x76) for another settlement, 0 for none |
| v | max(1, 4·prov[0x124] − conq + (travel_months − B)), where conq = `0x436670`; travel = `0x4366f0` |
| p | 10·v + 100; +10 if terrain class 4 or 6; +10 if **not** coastal |
| n | 15 by default; the minimum over our neighbouring provinces with a settlement: 5, or 0 if that settlement is a city (vfunc +0x0C) |
| d | (difficulty − 2) if `0x91a738[tag]` is 1 or 2, else 0 (difficulty = `[*0x8c0bfc + 0x2CA]`; table meaning **NIEPEWNE**: probably the human/AI flag) |
| cost | max(20, (n + p + 5·d) / 6) × (1 + infl) |

Evidence: `0x436960`, constants 4, 10, 100, 1/6, 20 at `0x5e6104`, `0x5e50ac`, `0x5e5650`,
`0x5e6100`, `0x5e60fc`.

**Travel time** (`0x4366f0` colonist, `0x436eb0` trader): distance² from the capital to the province
(the x distance wraps at 0x4A00), scaled by a speed from the tech table (+0x0C / +0x08) and the
religion table `0x64d5f0` / `0x64d61c`. The result is (years, months).
- The exact speed formula is **NIEPEWNE**: Ghidra lost the float expression, and it was not
  re-derived from the disassembly.

### 2.5 Colonist success chance (`country vfunc +0x44` = `0x436b90`, int clamped 1..95)

| Step | Value |
|---|---|
| start | 90 − 4·prov[0x124] |
| natives | if prov[0x12C] ≠ 0 or `0x47e330(prov)`: −4·aggressiveness (prov[0x13C]) |
| hard terrain | −10 if the target terrain is class 4/6 and the capital's terrain is not |
| conquistador | +5·conq (best conquistador/explorer skill present) |
| existing settlement | +4·colony level if a settlement exists; +5 if a TP exists |
| religion | +5 if province religion = state religion |
| culture | +8 if province culture is a country culture |

Only when `[0x8d0d7a]` = 0 (**NIEPEWNE**: probably "not a special/editor mode") does the next block
apply:

| Step | Value |
|---|---|
| early years | unowned province: + min(0, ⌊max(0, year − 1419) / 8⌋ − 25), i.e. −25 in 1419, 0 from 1619 (`0x436c95..0x436cbc`) |
| efficiency | c = int(c + 10·E − 3), where E = max(0.01, infraTable[level]+0x10 + relTable`0x64d5f0`[religion] + 0.01·country[0xF02] + 0.01·`0x42b6f0()`); this is the same efficiency as production efficiency `0x43b560` (`0x436cc0..0x436d6a`) |
| isolation | if no settlement and none of our neighbours of this province lies on the capital's continent: if ADM < country[0xFAE], + 5·(ADM − country[0xFAE]) (meaning of [0xFAE] **NIEPEWNE**) |
| tech | − max(0, 20 − country[0xE4A]) (**NIEPEWNE**: [0xE4A] probably a tech level) |
| bonus | +5 if `0x4842c0(country)` |

Then:
- If `0x91a738[tag]` == 0: +10 / +20 / +30 at difficulty 2 / 3 / 4.
- Clamp to [1, 95].

### 2.6 Trader cost (`0x437110`) and chance (`0x437260`)

**Cost:**
- f = 3·prov[0x130] − conq + 5 (if `0x431270(state religion)`) + 20 + 5·d.
- No TP: f·1/3. With a TP: f·0.2 − 2·TP level.
- Then max(5, …) × (1 + infl).

**Chance** (clamped 1..95):

| Step | Value |
|---|---|
| start | 60 if there are no natives, else 5·(16 − prov[0x130]) |
| hard terrain | −10 for terrain class 4/6 |
| trade efficiency | + 50·int(trade efficiency) (trade-tech table `0x64dab0`, `0x42b6c0`) |
| existing TP | + 5·TP level if a TP exists, else − prov[0x124] |
| early years | unowned: + min(0, ⌊max(0, year − 1419) / 8⌋ − 10) |
| list sizes | −5 if count(country[0x12F6]) < count(country[0x1308]) (**NIEPEWNE**: two country lists, probably merchants vs. trade posts) |
| difficulty | +10 / +15 / +20 at difficulty 2/3/4 when `0x91a738[tag]` == 0 |
| overextension | if the list at [0x1308] is non-empty and there is no TP: k = 3·count − country[0xFB2] − ADM; if k > 0, −k |

### 2.7 Sending and outcome

**Colonist** (`0x43efc0`); **trader** (`0x43f230`) is the same with message type 0xA2:
1. Requires `country vfunc +0x370` (+0x374 for a trader), i.e. a colonist is available; stores the
   send date at country[0xE52].
2. Creates a travel record of message type 0x9D, with cost = the formula above and arrival = travel
   time.
3. **The outcome is rolled at sending time.** Success unless `0x419700()`; failure if
   chance < rand%100.
4. **On failure**, with probability `0x47f370(prov)` (native aggressiveness based) a native-attack
   event (type 0xD0) is posted. CCWNATIVEROLLER: "risk of a native attack if you fail".

**Arrival:** the country handlers are vtable +0x128 (type 0x9D → `0x448a90`) and +0x13C (type 0xA2
→ `0x448ca0`):
- For the player, a message is posted: colonist 0x9F success / 0xA0 failure; trader 0xA3 success /
  0xA5 failure. These are the COLNEWSUCCESS_* texts and their counterparts.
- An expense entry is booked: type 5 for colonists, type 6 for traders, with the cost.
- A province command is queued: type 9 colonist, type 8 trader (`0x443950`).

**Effect on the province** (from the game's own help texts plus `0x47f630`):

| Case | Effect |
|---|---|
| Colonist into an empty province | a new colony with 100 settlers |
| Colonist into a colony | +100 population (CCWEXPAND2) |
| Colony reaching 1000 population | becomes a City; the remaining natives join the population |
| Trader into an empty province | a level-1 trading post |
| Trader into a TP | +1 level = +10 population (CCWEXPANDTP2), maximum level 6 |
| Colonist into a TP | converts it to a colony; the population becomes TP level·10 + colony population (`0x47f630`) |

- **NIEPEWNE:** the type-9/8 command handler that applies the +100 / +10 was not located; the
  numbers come from the game's help texts.
- **Colony level** (HEAD_COL "Level %d Colony") is settlement+0x72. Its formula is **NIEZNANE**,
  probably ⌊pop/100⌋.

---

## 3. Missionaries

### 3.1 Flow

1. BUTTON_MISSIONARY (id 44) → religion map mode → click a province.
2. The window `iw_Missionary.bmp` opens (ctor `0x4fd0a0`, vtable `0x5eb810`, draw `0x4fd4a0`,
   events `0x4fd330`).
3. **Send** calls `0x43f870(prov)` (`0x4fd3ff`). The resolution handler is `0x448700`.

### 3.2 Layout

Sidebar title: HEAD_MSW "Send Missionary to" + province name.

| Element | Position | Font / colour (**NIEPEWNE** where noted) | Content | Evidence |
|---|---|---|---|---|
| `missionaries_strip_left.bmp` (4 frames) | (10,16) | | | `0x4fd604` |
| `missionaries_strip_right.bmp` (4 frames) | (154,16) | | | `0x4fd62b` |
| MSW_SEND "Send Missionary to" | centred (112,40) | g14b gold (**NIEPEWNE**) | | `0x4fd66c` |
| Province name | centred (112,67) | g24b white (**NIEPEWNE**) | | `0x4fd695` |
| MSW_REL "CURRENT RELIGION" | centred (112,112) | | | `0x4fd6c8` |
| Religion icon | centred x=112, y=128 | `religions.bmp`, frame = province religion | | `0x4fd6f8..0x4fd724` |
| Religion name | centred (112,160) | | `0x431180` | `0x4fd75b` |
| MSW_POP "POPULATION" / value | left (24,195) / right @200 | g14 | int(settlement pop) | `0x4fd791`, `0x4fd813` |
| MSW_CUL "CULTURE" / value | (24,213) / right @200 | g14 | culture name | `0x4fd844`, `0x4fd87d` |
| MSW_SUC "SUCCESS CHANCE" / value | (24,232) / right @200 | g14 | `"%d%%"` `0x45c8b0` | `0x4fd8ae`, `0x4fd8fd` |
| MSW_TIME "ESTIMATED TIME" + MSW_TIME2 "OF CONVERSION" / value | (24,250) and (24,264) / right @200, y=264 | g14 | MSW_TIME3 "%d months", `0x45c710` | `0x4fd92e..0x4fd9d1` |
| MSW_COST "COST" / value | (24,284) / right @200 | g14 | `"%.f$"` `0x45c770` | `0x4fda03..0x4fda57` |
| MSWSEN "Send" | centred x=158, y=319 | text button g14b | | `0x4fda95` |
| MSWBAC "Cancel" | centred, y=319 | text button g14b | x = 112 at `0x4fdb15`; it probably moves to 66 when Send is shown (**NIEPEWNE**) | |

### 3.3 Chance (`0x45c8b0`, int)

Only if the province has a settlement (prov[0x408]):

| Step | Value |
|---|---|
| base by **our state religion** country[0x254] | 0, 2, 3, 4, 5, 9 → 30; 1, 6 → 27; 7 → 50; 8 → 20; 10 → 35; others 10 |
| + 2·X | X = `0x47f3e0(prov)`, an int of the settlement (**NIEZNANE** which field) |
| − base tax/3 | − prov[0xB6]/3 |
| + 4·ADM | |
| − population term | − min(20, int(pop·0.0002)) |
| culture | if the province culture is not a country culture **and** the province religion ≠ 7: halve |

Then:
- The result is at least ADM. With no settlement the chance is just ADM.
- At most 99.
- Evidence: `0x45c8b0..0x45c9ac`; jump table `0x45c9b4`; 0.0002 at `0x5e7cb8`.

Notes:
- The base keys on the **sender's** religion, not the province's. HINT_81 says "some religions
  (e.g. pagan) are easier to convert from", which does not match; recorded as found.
- The religion numbering (which id is pagan) is **NIEZNANE**. Religion 7 is exempt from the culture
  halving, and that suggests 7 is pagan (**NIEPEWNE**).

### 3.4 Cost (`0x45c770`)

| Step | Value |
|---|---|
| no settlement | cost 0 |
| base | pop·0.004 + 100 + travel_days/3 + 4·base tax, where travel = `0x439bf0(capital, prov, 30)` and days = (years·12 + months)·30 + days |
| other continent | ×1.75 if the province is on another continent than the capital (prov[0x498]) |
| discount | ×0.2 if the settlement is a colony; also ×0.2 if it is not a colony but vfunc +0x20 is true and prov[0x106] ≠ capital[0x106] (meaning of [0x106] **NIEPEWNE**, a region id) |
| final | × (1 + infl) |

### 3.5 Time (`0x45c710`)

travel(capital → province) + (10 − ADM) + 24, in the units of `0x417210` / `0x4171a0`. It is shown
as "%d months". The units are **NIEPEWNE** (probably months).

### 3.6 Sending and outcome (`0x448700`)

1. **The outcome is rolled at sending time** (HINT_81 agrees): success if rand%100 < chance.
2. For the player, a message is posted: 0x95 success / 0x96 failure, i.e. MISSOSUCC "Inquisition
   Successful!" / MISSOFAIL.
3. An expense of type 7 is booked with the cost (EXPENSEMIS "Missionaries").
4. A province command of type 0x1B is queued, to complete at the conversion time.
5. On success the province takes the state religion. HINT_76 says a missionary or colonist can also
   change the culture of a pagan province or one without a city — **NIEPEWNE**: the type-0x1B
   handler was not decoded.
6. A switch on the state religion in `0x448700` has empty cases and no effect.

---

## Mapping to the coordinator's data model

| Ours | EU2 |
|---|---|
| Province `income_m` | prov[0xB6] base tax |
| Province `natives` | prov[0x12C]·100 (strength); aggressiveness prov[0x13C] is a separate 0..9 value that must be added |
| Province `population` | settlement+0x7E (colony/city), or TP level·10 |
| Province `religion` / `culture` | `0x47ca10` / prov[0xD8] |
| Country `colonists_m` | country[0xE94]; spent by **both** colonists and traders |
| Country `missionaries_m` | its stock field was not located (**NIEZNANE**) |
| Country `treasury_m` | country[0xE70]; all costs ×(1 + inflation) |
| Country `infra_tech` | colonist chance via production efficiency (§2.5) |
| Country `trade_tech` | trader chance/cost (§2.6); "Trade Level 2" is needed for traders |

Fields our model lacks:
- native aggressiveness;
- the "difficulty" value prov[0x124];
- the coastal flag;
- the continent id;
- colony level;
- TP level;
- the monarch's ADM.

---

# Appendix: Merchants (merged from envoys_merchant.md)

# Merchants (envoys, item 1)

This file records rules and layout facts only, read from the `eu2.exe` v1.09 disassembly (tools in
`re/`) and the Ghidra export. No code was copied.

Notation:
- `prov[x]` is the province record `0x8077d4 + idx·0x4A0`.
- `C` is the acting country (its tag index is `C[6]`).
- `cot` is the CoT object `*prov[0xAA]`.
- Coordinates are window-relative, as in `layouts_iw*.md`.

## 0. Data that matters (offsets → our model)

| Engine field | Meaning | Our model |
|---|---|---|
| `C[0xE8C]` float | merchants in stock; capped at `0x458d10()` (6: YEARLYMER_MAX), floored at 0 | `merchants_m` / 1000 |
| `C[0xE70]` float | treasury | `treasury_m` |
| `C[0xE84]` float | inflation (clamped ≥ 0) | `inflation` |
| trade-tech object `*(C+0xE46)+0x54`, vfunc +0x34 = level | trade level; ≥1 needed for merchants (`0x432ee0`), ≥3 for a monopoly (`0x432f00`) | `trade_tech` |
| `0x43b610(C)` | trade efficiency (float, ≥ minimum) | derived from `infra_tech` / trade tech |
| `0x43a230(C)` | stability (int) | – |
| `0x43bf10(C)` → monarch `0x42f540` | monarch ADM (0..9, 3 without a monarch). It is the same getter as the ADM pips in iw_country, so the ADM label is **NIEPEWNE** | – |
| `0x42bbf0(C,3)` = `C[0x37E]`+8+3·4 | domestic policy slider #3, mercantilism ↔ free trade, 0..10 | – |
| `C[0x1F7E]` | capital province | – |
| `C[0x1F84]` | non-zero = can send overseas (capital has a harbour; ERR9) | – |
| `C[0x225A]` | province last selected for the CoT window | – |
| `cot[0x04]` | CoT home province | Cot.home |
| `cot[0x20]` int | total merchants present (recounted by `0x499700`); at most **20** slots | Σ counts |
| `cot[0x24]` float | total yearly trade value of the CoT | – |
| `cot[0x28 + 4·tag]` → entry | per-country merchant entry (340 slots) | list (country, count) |
| entry[0x04] float | merchants present, 0..6 | count 1..6 |
| entry[0x10] float | this country's income from the CoT | – |
| entry[0x14] list | merchants en route (pending arrivals, at most 6) | – |
| `C[0x38E]` + 0x36·tag | relation record with country tag: +0x32 relation value, +0x39 trade refused, +0x3B royal marriage, +0x40 trade agreement | – |
| `prov[0x256 + tag]` byte | province known to that country | – |
| `prov[0x106]` / `[0x10E]` | continent / region id (used for distances) | – |

## 1. UI flow

### Ways to open or send

1. **CoT window (iw_cot)** opens for the province in `C[0x225A]` (`0x5155d1`):
   - if that province has no CoT, the capital's CoT is used;
   - if neither has one, the window closes (id 0).
   - It is reached from:
     - the foreign-province window's CoT icon (that click centres the map);
     - BUTTON_VIEW_COT (id 86);
     - the right-click map menu: RCDM_SENDMERCHANT "Send Merchant" and RCDM_VIEWCOT (`0x52ba4f`, `0x52ba9e`).
2. **Map or right-click send:**
   - SNDMERCHANT / CONFIRMMERCHANT "Should we send a merchant to %s for a cost of %d$?" (`0x5329f7`, `0x53353d`), with the cost from `0x4378d0`.
   - CANTSENDMER when the send is not possible.
   - BUTTON_MERCHANT (id 42) is the sidebar trade-mode button: trade map mode and the cursor `cursor_send_merchant.bmp`.
   - Which exact click sequence opens the confirm popup and which opens the window is **NIEPEWNE**.
3. **In the CoT window**, the button COTIWSEND "Place Merchant":
   - It is enabled when `0x45c600(C, cotprov)` (can-send) is true (`0x51526a..0x51529e`).
   - A click calls `0x43f4b0` (send), with no extra confirmation (`0x5152d3`).
   - BACK → window 0.
4. **Autosend:**
   - The checkbox COT_AUTO "Autosend merchants." toggles automatic sending (`0x515237`).
   - The automatic routine (`0x528131..0x5282a7`):
     - it scores every CoT, using `cot[0x24]` and the rest of the score at `0x5281c5`;
     - a CoT with 20 merchants is skipped unless every country present has a trade agreement with C;
     - it sends to the best CoT for which can-send is true;
     - then it posts the message MERCSEND "Merchant sent" (message 0x13).
   - When it runs is **NIEPEWNE**.

### Can-send (`0x45c600`, button enable)

All of these must hold:
- trade level ≥ 1;
- `C.vfunc+0x4C(cotprov)` is true (`0x45c490`, see below);
- en route to this CoT ≤ 5;
- cost ≤ treasury;
- stock ≥ 1.0;
- not (no contest per `0x45caf0`, **and** some country already has 6 there, **and** (total = 20 or our int(count) > 4)).

`0x45c490` (reachability) requires all of:
- the CoT province is known to C;
- it has a CoT;
- (C can send overseas, or C owns the province, or the province is on the capital's continent);
- the province has an owner;
- the CoT's owner does not refuse trade with C (`0x4cbf60`);
- if C's trade level is < 3: our count < 6 and not > 4.

When the button is disabled, its tooltip gives the first failing reason, checked in this order
(`0x515c25..0x515e5a`):

| Order | Condition | Key |
|---|---|---|
| 1 | CoT province not known | COTIWSENDERR8_I "You must first discover…" |
| 2 | trade level < 1 | ERR4 "Requires Trade Level 1" |
| 3 | no harbour (`C[0x1F84]`=0), province not ours, other continent | ERR9 |
| 4 | our count > 4 and trade level < 3 | ERR5 "cannot place six … until Trade Level 3" |
| 5 | our count > 5 and fewer than 7 merchants in the CoT | ERR6 "already have a monopoly … no competition" |
| 6 | stock < 1 | ERR1 "You have no merchants to send!" |
| 7 | CoT owner refuses trade with us | ERR2 "%s refuses to trade with us!" |
| 8 | cost > treasury | ERR3 "cannot afford … (Cost: %d$)" |
| 9 | more than 5 en route | ERR7 "already have six merchants en route" |

When the button is enabled, the tooltip is COTIWSENDOK_I "Send merchant to %s at a cost of %d$?".

## 2. Placement cost (`0x4378d0(C, cotprov)`)

**`merchantplacementcost` is not static data.** It is an event-script keyword (parser token 0x192,
`0x49ed54`, in the same keyword table as `colonycost` and `tpcost`). It is also the effect behind
DOMESTIC_M_COS "Merchant Cost: %s%.2f percent". The cost itself is a formula. With K = capital
province and P = CoT province:

```
a = 10 − ADM(monarch)                        (ADM 3 without a monarch)
b = (P[0xC6] ∈ {4,6} ? 25 : 0)               (terrain/type class of P, NIEPEWNE meaning)
  + (K continent ≠ P continent ? 30 : 5)      (prov[0x106])
  + (K region   ≠ P region    ? 5 : 2)        (prov[0x10E])
if P not owned by C:
    if the capital's own CoT home province == P:  b = b/3 + 1   (integer)
    else                                          b = b + 10
d = 0, or (difficulty-like global [[0x8c0bfc]]+0x2CA) − 2 for countries flagged 1/2 in [0x91a738] (NIEPEWNE)
base = max(2.0, (a + b + d) / 3)
cost = int( base × (1 − (policy3 − 5)·0.1) × (1 + inflation) )
```

- `policy3` is domestic slider #3, 0..10, so the policy factor runs from ×1.5 to ×0.5.
- Evidence: `0x4378d0..0x437a9b`; the policy factor is `0x42b900`.
- A naval-tech call at `0x4379c6` has no effect.
- The cost is paid when sending (an event command carries it); the exact deduction site is **NIEZNANE**.

## 3. Sending and success chance

### Sending (`0x43f4b0`)

1. Queue event **0x59 USE_MERCHANT**: stock −1 (`0x448350`).
2. Queue event **0xA7 SEND_MERCHANT** carrying C, the CoT province, the arrival date (now +
   `0x437800` travel time from the capital), the cost, count byte +0x21 = 1, and a success flag.
3. **Pre-rolled success flag:**
   ```
   chance = clamp( int(trade_eff·50 + stability·5 + ADM·3 + 35), 1, 99 )
   success = (rand()%100 ≤ chance)          (0x4170b0 result ≥ 2)
   ```
   - Evidence: `0x43f60e..0x43f68a`.
   - The flag is forced true when the CoT is not contested (`0x45caf0` = 0) and some country already
     has 6 merchants there (`0x499730`) (`0x43f68d..0x43f6b2`).
   - It is also forced true by a debug/cheat flag (`[0x8ff63c]+0x2766`).

### Event handling (country vtable `0x5e5ac0`, dispatch `0x461e90`)

| Event | Handler | Effect |
|---|---|---|
| 0xA7 SEND_MERCHANT | +0x150 `0x448470` | creates the CoT entry for C if missing (count 0) and adds the merchant to the en-route list |
| 0xA8 REQ_MERCHANT_SUCCESS | +0x154 `0x4469a0` | on arrival, re-posts 0xA9 with the same flag and count |
| 0xA9 EXEC_MERCHANT_SUCCESS | +0x158 `0x446ae0` | resolves the arrival, see below |
| 0x5B GET_MERCHANT | +0xB4 `0x449460` | stock +1 (refund) |
| 0x9C (competition result) | +0xE4 `0x4494a0` | the loser loses one merchant, see §4 |

### Arrival (`0x446ae0`)

The handler repeats n times, n = count byte × (2 if `C[0x1246]`==1 else 1); invalid values become 1.
Each repetition first removes one en-route item.

**Not contested** (`0x45caf0` = 0):
- Failure when:
  - the success flag is 0; or
  - (someone has 6 here and our count ≥ 5); or
  - the CoT is full (20).
  - Failure posts **MERCFAIL** "Merchants Failed!" and the merchant is lost.
- Success posts **MERCSUCC**, then:
  - no entry: create it with count 1;
  - count ≥ 6: queue GET_MERCHANT (the merchant is refunded);
  - otherwise count += 1;
  - if count > 5, a monopoly message goes to us or to observers (message ids 0x23 / 0x24).
- Afterwards:
  - the total is recounted;
  - `cot[0x14]` −= 0.005 (meaning **NIEPEWNE**);
  - if C's capital does not know the CoT owner, a contact/discovery event 0x93 is sent.
- Evidence: `0x446bb6..0x446e91`.

**Contested** (`0x45caf0` = 1), `0x446ebb..0x4472d7`:

1. **Pick a competitor j** among the other countries with merchants there.
   - If the total is < 20, each j gets a score:

     | Term | Value |
     |---|---|
     | random | rand%25 + 50 |
     | j's relation-record byte +0x36 set (NIEPEWNE: alliance) | +20 |
     | j's merchant count there | +3 per merchant |
     | j refuses trade with C | +20 |
     | C refuses trade with j | +30 |
     | no marriage/alliance tie (`0x4cb900` false) | +5 |
     | different religion group (2 and 3 count as one) | +20 |
     | (10 − ADM_j) | added |
     | j's merchants per year (`0x458b50`) | −3 per merchant/year |
     | trade-efficiency difference | +20·(trade_eff_C − trade_eff_j) |
     | (10 − policy3_C) − policy3_j | added |

     The score is at least 1, and it is **0 if C and j have a trade agreement**. The highest score
     is chosen.
   - If the total is 20, j is the other country holding exactly 6 (none if it has a trade
     agreement with C).
2. **No competitor, but the flag is set:** an 0xA9 is re-posted with count 0xFF and flag 0, so the
   merchant fails.
3. **Competitor found and flag set:** a second roll, rand%100 ≤ chance2:
   ```
   chance2 = 60 + ADM_C − ADM_j
           + 50·int(trade_eff_C − trade_eff_j) + int(stab_C − stab_j)
           − policy3_C + policy3_j
   ```
   - If the CoT province's controller (`prov[0x3C8]`) is not C:
     chance2 = int(chance2 − 0.2·`C[0xE42]`) (reputation / badboy).
   - If C refuses trade with j: +15.
   - The int() is applied to the trade-efficiency difference, so it usually contributes 0.
   - **Win:** event 0x9C against j (§4).
   - **Loss:** 0xA9 re-posted with flag 0 → MERCFAIL; the merchant is lost.

The message names are inferred by an offset of one in the type table `0x635b1c..`:
- 0x13 MERCSEND, 0x14 MERCSUCC, 0x15 MERCFAIL, 0x16 MERCLOSS, 0x17 MERCOLOSS;
- the table itself lists MERCSEND under id 0x14 (**NIEPEWNE**).

## 4. Pushing out (competition) and other rules

- **Event 0x9C** (`0x4494a0`): the loser's count −1. Its entry is deleted when the count drops below 1.
  - The loser gets MERCLOSS (with the remaining count, `0x44955b`) and the winner gets MERCOLOSS (`0x449590`).
  - Relation loser→winner −5 (`0x4495de`).
  - The winner's own merchant is **not** added in this step (NIEPEWNE: it may only free the slot).
- Merchants are displaced **only when someone sends a merchant into a contested CoT**. No monthly or
  yearly erosion routine was found (**NIEPEWNE**: absence of evidence).
- **Refused trade:** when the CoT owner refuses trade with a country (relation byte +0x39), that
  country cannot place merchants and gets no income from those present (COTIWSENDERR2_D,
  COTIW_REFUSED). The CoT window greys out that country's shield (frame 3, `0x515a3d`).
- **Trade agreement** (relation byte +0x40): the two countries never compete each other out
  (DIPROL_OFFTRA; score 0 at `0x4470ad`).
- **Monopoly:**
  - 6 merchants, which requires trade level 3 (ERR5, TRADELEVEL_TRADEIW).
  - Per COTIWMONOPOLYD, a monopolist also earns the income of all empty slots out of 20.
  - That income rule is in the income code (**NIEZNANE** here).
  - MONOPOLY_YOUR: 10 monopolies win the game.
- **Income** (COTIW_DRO tooltip, `0x5164b9`): yearly income = total trade × (count / 20) × trade
  efficiency (`0x43b610`).
- **Merchants per year** (`0x458b50`), clamped 0..12, and 6 in the fantasy setting. The sum is:
  - the number of CoTs where C holds more than 5 merchants (monopolies);
  - clamp(stability + 1, −3, 3);
  - the policy term `0x42b8c0`;
  - (`C[0xFBE]`/25 + 1), or 0 when `C[0x36C]` is below a threshold (coastal provinces, **NIEPEWNE**);
  - `C[0xFC6]` (CoTs owned, **NIEPEWNE**);
  - trade level / 5;
  - min(3, (`C[0xFB6]`+`C[0xFBA]`)/2) (Protestant coastal provinces, **NIEPEWNE**).
  - The terms follow the YEARLYMER_* tooltip lines (`0x4584e0`).
  - The stock is capped at 6.

## 5. CoT window (`iw_cot.bmp`)

Ctor `0x514dc0`, vtable `0x5ec088`:
- +0x04 events `0x5151e0`;
- +0x0C draw `0x5153a0`;
- +0x3C open/positions `0x515480`;
- +0x44 tooltip `0x515b80`;
- +0x48 tooltip `0x5161e0`;
- the contents are built by `0x515640(cotprov)` into the window surface.

Sidebar header: (CoT province name, HEAD_COT "Center of Trade") (`0x5157a8`).

| Element | Position | Font / colour | Content | Evidence |
|---|---|---|---|---|
| CoT province name | centred (80,15) | garamond20bold white | `prov[cot[4]][0x04]` | `0x515860` |
| CoT owner name | centred (80,31) | g14b gold | owner[0x12] | `0x515894` |
| Total trade value | right-aligned, right edge x=176, y=15 | garamond20bold white | `"%d"` int(`cot[0x24]`) | `0x5158c4..0x5158f6` |
| Our income | centred (176,30) | g14b gold | `"(%d$)"` int(our entry[0x10]). The share % = income·100/total is stored at `+0xF0` | `0x51590d..0x51592f` |
| En-route merchants | x=198, y = 273/231/189/147/106/56 (k = 1..6) | `merchant.bmp` | one icon per merchant en route, at most 6 | `0x515737..0x515773`, table `0x6299d8` |
| Level bars `cotlevel1..6.bmp` | x=14; y = 266 (1), 225 (2), 179 (3), 137 (4), 95 (5), 47 (6) | | static background rows, one per merchant count | `0x51554b..0x5155ce` |
| Country shields in row k (countries with exactly k merchants) | from x=82, step 20 px (or 92/n when more than 5 countries); y per k = 270 / 228 / 186 / 145 / 102 for k = 1..5 | `smallshield_<tag>` (4 frames): frame 0, frame 3 (grey) when the CoT owner refuses that country's trade | | `0x51593d..0x515b54`, table `0x6299bc` |
| Monopoly shield (k = 6) | (160,53) | | | `0x5159e3` |
| COT_AUTO checkbox | `btn_radio` at (20,305); label (40,305) | label g14 gold | "Autosend merchants." | `0x515409`, `0x51546e` |
| BACK (COTIWBACK) / PLACE (COTIWSEND) | centred x = 66 / 158, y = 319 | g14b text buttons | | `0x5154ff`, `0x515530` |

Tooltips (`0x515b80`, `0x5161e0`):
- send button: OK / ERR keys as in §1;
- level rows: COTIWROLLOVER "The countries with %d merchants in this center";
- monopoly row: COTIWROLLOVER2;
- each shield: COTIWSHIELD_D "%s's %d merchant(s) generate %.2f$ yearly", plus COTIW_REFUSED when refused;
- totals: COTIWNUMBERS_I/_D "The total value … your current share is %.2f$ monthly";
- owner: COTIWCENTER_I/_D;
- income: COTIWINC_I/_D and the COTIW_DRO income breakdown;
- COTIWEFF_I/_D (trade level), COTIWMONOPOLYD.
