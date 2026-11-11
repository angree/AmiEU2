# Holy Roman Empire: emperor, electors, HRE provinces (EU2 v1.09)

Clean-room specification read from `eu2.exe` (objdump, VAs). Prose and tables only, no code copied.
**NIEPEWNE** = believed, not proven; **NIEZNANE** = not found. Conventions (country table
`0x64e140`, idx, message ids, dates) as in `docs/SPEC_SMALLRULES.md`. Work scripts: `C:\temp\amiga_eu2\sr\hr*.sh`.

Short version: there are exactly three pieces of state (emperor idx, a static per-tag elector weight,
a static per-province HRE byte). An election runs when the emperor's monarch changes or the emperor
country is gone; it is a weighted relation vote with a rand%10 tiebreak noise. Being emperor gives
+50 VP per election, +2 ducats/yr and +1 manpower per "loyal" elector, cheaper tech and stability,
military access through HRE provinces while at war, and AI goodwill from electors.

---

## 1. Storage

### 1.1 Emperor

| Item | Value | Evidence |
|---|---|---|
| Global | dword `[0x8d0e18]` = **country idx** (not a string) of the emperor. It is field +0x98 of the scenario/globals object `0x8d0d80` (the same object holds start date at +0x90 = `0x8d0e10`, `0x419660`, end date +0x94 = `0x8d0e14`, `0x419680`) | `0x5b22a0`, `0x5b1da8` |
| Default | 3 (**HAB**) when the globals object is reset, before the scenario is parsed | `0x5b186f` |
| Accessor "emperor country" | `0x5b22a0` (this = `0x8d0d80`) returns `countries[[+0x98]]` (a pointer, NULL when that country does not exist) | `0x5b22a0` |
| Predicate "C is emperor" | `0x43ce10(C)`: C+6 (its idx) == `[0x8d0e18]` | `0x43ce10` |

Scenario: inside `globaldata = { ... }`, keyword id **0x159 = `emperor`** (keyword table registration
`0x49c80a`, string `0x620b1c`); the value is a tag looked up with `0x4696d0` (scan of the static
country records `0x64f730` + 0x144·i, returns the record's idx at +0x100, or **0 if the tag is
unknown**), stored at +0x98 (`0x5b1d95..0x5b1da8`). Shipped scenarios: 1419 HUN, 1492/1617/1700 HAB,
1773 LOR, 1795 TOS (one line each, at the end of the globaldata block).

Save: the globals writer `0x5b2610` (this = `0x8d0d88`, called from `0x4a1e69`) emits keyword
0x159 with a country-typed value (type word 3) taken from this+0x90 = `0x8d0e18`
(`0x5b2aef..0x5b2b9a`), i.e. `emperor = TAG` inside globaldata, the same form as the scenario.
Nothing else ever writes `[0x8d0e18]` except the NEW_EMPEROR handler (§2.4); there are only 3
direct references to `0x8d0e18` in the exe (`0x43ce13`, `0x43cf11` read; `0x45553d` write).

### 1.2 Elector

- **Static and per tag.** It is the integer column 17 `Electors` of `DB/country.csv` (the column after
  `Serfdom`), stored at +0x140 of the static country record (`0x64f730 + 0x144·idx`, i.e.
  `0x64f870 + 0x144·idx`), loaded by `0x46968c`. It is a **vote weight**, not a flag: the shipped
  file has 35 tags with value > 0, two of them (KOL, PFA) with 2, the rest 1.
- Predicate "C is an elector" `0x43ce30(C)`: weight(C+6) > 0.
- There is **no writer** apart from the csv loader: no scenario, save, event or code path changes
  electorates. Being an elector does not depend on owning any province, religion or the emperor.
- Readers of `0x64f870` (all of them): `0x43ce30`, `0x59ba80` (event trigger), `0x5b216d` (vote
  weight), `0x46968c` (loader).

### 1.3 HRE province flag

- `province.csv` column 17 `HRE`; the loader sets province byte **+0x78** = (value == 1)
  (`0x484853`; +0x76 / +0x77 are ToT-SPA / ToT-POR from the two columns before). Reset to 0 at
  `0x47c232`. Static: no other writer.
- Readers (all byte reads of +0x78 that are this field): `0x45f7f9` (military access, §3.4),
  `0x476f1f` (map borders, §3.7), `0x532c53` (tooltip, §3.7). The election does **not** use it.

---

## 2. Election

### 2.1 When (callers of the election `0x5b20a0`, this = `0x8d0d80`)

1. **Emperor's monarch changes.** Handler of command **0xbd ACTIVATE_MONARCH** `0x44fda0` (country
   vtable slot 0x23c): after the new monarch is installed in C+0x20bc and the NEWMONOTHER news, on the
   **host only** (`0x4196e0`), if C is the emperor (`0x43ce10`) and **not** (start date + 1 day > today)
   (`0x4173c0`, `0x417220` against `[0x647c2c]+0x14`), i.e. not on the scenario start day →
   election (`0x44fef9..0x44fefe`). So every historical monarch change (and any event that installs a
   monarch through 0xbd) of the emperor country triggers a new vote.
2. **Emperor country does not exist.** Globals month step `0x5b22f0` (from the globals message handler
   `0x5b22b0` on **0x16 TIMER_MONTH** and **0x50 GAME_STARTED**), host only, skipped when
   `0x419700` is true ([`[0x8ff63c]`+0x2766], NIEPEWNE debug/editor flag): at its end, if
   `countries[[+0x98]]` is NULL **and** fantasy mode `[0x8d0d7a]` is off → election
   (`0x5b2503..0x5b251f`). This covers annexation of the emperor and an unknown/0 tag from the
   scenario. It repeats monthly as long as no emperor exists (see 2.3, HAB missing).

Not a trigger: the emperor's monarch dying without a successor installed, wars, religion change,
losing provinces, a timer. **There is no periodic election.**

### 2.2 Candidates and score (`0x5b20a0`)

Loop over all 0x154 country slots in index order. Candidate K qualifies when all hold:

1. K exists;
2. K's state religion (+0x254) is **3 Catholic or 2 Counter-Reformed**, **or** K is an elector
   (any religion; a Protestant elector may run);
3. K has a current monarch (+0x20bc != 0);
4. the continent of K's capital (`0x45b7d0` → province +0x106) is **2 (Europe)**.

No condition on HRE provinces, size, war, being the current emperor (the sitting emperor's country
competes too), or the player.

Score of K (integers):

```
score = rand() % 10                                       (0x5ba99c)
for every existing elector E (index order):
    r = trunc( clamp(rel, -200, +200) )                   (0x4cdf70, ftol 0x5b98fc)
        rel = float +0x26 of K's diplomacy record for E (K+0x38e, +0xc + 0x36*E)
    score += (r + 100) * weight(E)
    if E == K: score += 200                               (flat, not weighted)
if K is HAB (idx 3) or SPA (idx 8): score += 25
```

- Each elector's "vote" is a continuous value (r+100)·w from 0 to 300·w; the relation is the one
  stored on K's side (NIEPEWNE whether relations are symmetric; reproduce as K's record).
  Electors' religion, royal marriages, alliances, bribes and the emperor's own relations do not count
  except through that relation number.
- K = an elector itself: its own term (r_self + 100)·w is also added (relation with oneself is what
  the record holds, NIEPEWNE 0), plus 200.

### 2.3 Winner and tie-break

- Running best starts at **0 with winner = HAB (idx 3)**. A candidate replaces it only if
  score **>** best (strict). Ties therefore go to the lower index; HAB wins by default if nobody scores
  above 0 (possible only with very bad relations), even when HAB itself did not qualify.
- If `countries[winner]` is NULL (only possible for the default HAB) → nothing is posted, the
  emperor stays as it was; with a dead emperor the monthly check retries every month.
- Otherwise a message object (vtable `0x5e4c0c`, flags +8=0, +9=1, +0xa=0) with command
  **0x150 NEW_EMPEROR** (internal name `_CEUEVT_NEW_EMPEROR_`, `0x4b9a7e`), target = {type 3
  country, idx winner}, sender 0, is posted through `0x4b8770` (`0x5b21da..0x5b2277`).

### 2.4 NEW_EMPEROR 0x150 handler `0x455530` (country vtable slot 0xf0; case 170 of `0x461e90`)

On the elected country C (also if C already was the emperor):

1. `[0x8d0e18]` := C's idx.
2. If the local player knows C's capital (byte capital-province +0x256 + viewer idx != 0): news
   **EMPEROR** (news code **0x60**, text keys `EMPEROR_SETUP`, `EMPEROR_HEADER`, `EMPEROR_1..6`,
   `EMPEROR_LOG`, `EMPEROR_BTN1/2`), posted through `0x577fa0(0x8c90d8, 0x60, C idx, 0x6452c8,
   monarch name 0x43bee0(C), C+0x12, capital)`; it goes to the viewer only (log/popup per the
   message settings). "Goto" = capital.
3. **VP +50 in category 3 (Diplomacy)** to C (`0x55f030(C+0x20e2, 3, 50.0)`). A re-elected emperor
   gets the 50 again.

No relation, prestige, badboy or stability change, and nothing is sent to the electors or the old
emperor.

---

## 3. Effects (every reader)

Let **N = `0x460390(C)`** = number of existing electors E ≠ C whose relation toward the emperor
(E's record for the emperor, clamped ±200) is **≥ 100**. It is computed against the current emperor
country (`0x5b22a0`) and returns 0 when none exists. All callers call it only when C is the emperor.

### 3.1 Economy and military (C = emperor)

| Effect | Rule | Where |
|---|---|---|
| Yearly income | country year step `0x451250`: AddIncome(category **5**, **2·N** ducats) (`0x438a30`; the category is shared with vassal income in the ledger, SPEC_TIME_ECONOMY §AddIncome) | `0x4512b9..0x4512db` |
| Manpower | yearly manpower rate C+0xec4 (recomputed monthly, `0x43ce50`): **+N** after the minimum clamp | `0x43cf0e..0x43cf2f` |
| Tech cost | size factor S (`0x434a90`) **− 0.01·N**, before the 0.5..2 clamp | `0x434b7e..0x434b9c` |
| Stability cost | cost × **(1 − 0.01·N)** (`0x4315a0`) | `0x4317b9..0x4317de` |

SPEC_TIME_ECONOMY.md calls `0x43ce10` "has vassals / overlord" and the global "a special tag" in these
places: both are the emperor.

### 3.2 Not affected

Revolt risk, badboy, war exhaustion, CBs, declaring war on HRE members, peace, province tax:
**no reader**. There is no rule for attacking HRE members (the HRE byte has only the three readers
of §1.3), no imperial-ban or "reconquest" CB, no automatic call to arms.

### 3.3 Events

| Item | Rule | Where |
|---|---|---|
| trigger `emperor = yes/no` | (country == `0x5b22a0()` (emperor country)) == flag | `0x59ba40` |
| trigger `elector = yes/no` | (weight > 0) == flag | `0x59ba70` |
| scope/target −6 | replaced by the current emperor's idx, or −1 if no emperor exists | `0x593c39..0x593c5f` |

No event command sets the emperor or an electorate.

### 3.4 Military access (HINT_27)

`0x45f730(U, P)` "U may enter province P": after the own/controlled/war/access checks, U may enter
P when **U is at war with anyone** (`0x43c000`: dip-object byte +0x47cc), **U is the emperor** and
**P has the HRE flag** (+0x78), regardless of P's owner (`0x45f7e3..0x45f7fe`). This is the
"right to march your troops throughout the Holy Roman Empire land when at war, regardless of your
alliances" of `HINT_27`. Not at war → normal rules.

### 3.5 AI diplomacy (C = AI answering, O = the asking country)

In every case `0x43ce10` is tested on O and `0x43ce30` on C. SPEC_AI.md reads these as
"human(O) and flag(C)": they are **"O is the emperor and C an elector"**.

| AI routine | Rule | Where |
|---|---|---|
| Honour alliance `0x535f40` | p ×2 if O emperor and C elector | `0x5361d2` |
| Allow join `0x536300` | p ×2 (same) | `0x5363e1` |
| Annexation offer `0x536430` (O overlord) | p ×2 (same) | `0x5365c1` |
| Military alliance `0x5372b0` (step 11) | p ×2 (same) | `0x537679` |
| Royal marriage `0x5377e0` (step 4) | p ×2 (same) | `0x5379cf` |
| Vassalisation `0x537ae0` (step 7) | p −30 if C is an elector and O is not | `0x537c13` |
| Alliance score A `0x535600` | +100 if both candidate and partner are electors | `0x5356ce` |
| Friendliness H `0x535810` | +15 if both are electors | `0x535aaf` |
| Annex decision `0x539d10` | base p = 50 instead of 100 if T is an elector and Christian (0..4) (SPEC_AI §annex) | `0x539d39` |

The AI has no plan to seek the crown, vote, or protect the empire.

### 3.6 Other readers

None: the full reader list of `0x43ce10` is `0x4317b9, 0x434b7e, 0x44feb1, 0x4512b9, 0x45f7f0,
0x47f738, 0x5174e0, 0x532bea, 0x5361d2, 0x5363e1, 0x5365c1, 0x537679, 0x5379cf`; of `0x43ce30`:
`0x4603bc, 0x532c12, 0x5356ce/e1, 0x535aaf/c2, 0x5361e1, 0x5363f0, 0x5365d0, 0x537688, 0x5379df,
0x537c13/26, 0x539d39, 0x5b20ef, 0x5b213c`; of `0x5b22a0`: `0x4603af, 0x593c44/52, 0x59ba4b`;
of globals +0x98: `0x5b2503` (and the writer/parser).

### 3.7 UI

| Item | Rule | Where |
|---|---|---|
| Country window monarch label | `COWEMP` "MONARCH(Emperor):" if the country is the emperor, else `COWMONARCH` | `0x5174e0` |
| Province tooltip | after "%s: %s \n%s: %s": owner is emperor → `ROL_EMP`; owner is elector → `ROL_ELEC`; province HRE flag → `ROL_HRE` (each a line with its own `\n`) | `0x532be4..0x532c90` |
| Map borders | with map flag `[0x7fb8ad]&0x40` and `[0x80a528]` == 1 the border-key of a province is its HRE flag instead of its owner, so the HRE outline is drawn (the religious map per HINT_27; mode id NIEPEWNE) | `0x476f05..0x476f2d` |
| Emperor's capital | when map option `[0x7fb8ac]&0x10000` and the province's city has +0x2c == 1 (capital, NIEPEWNE) and the owner is the emperor, an extra sprite of the owner's +0x392 object (+0x18) is drawn before the normal one (NIEPEWNE: an imperial crown/marker) | `0x47f732..0x47f775` |

---

## 4. Messages

| Code | Text keys | Who receives | When |
|---|---|---|---|
| command 0x150 NEW_EMPEROR | (none; internal) | the elected country | §2.3 |
| news 0x60 EMPEROR | `EMPEROR_SETUP`, `EMPEROR_HEADER` "Our Governor reports%s", `EMPEROR_1` "New Emperor elected", `EMPEROR_2..6` "%t, the electorates of the Holy Roman Empire have elected / %s / of / %s / as emperor.", `EMPEROR_LOG` "The Holy Roman Empire has elected %s of %s as emperor..", `EMPEROR_BTN1` OK, `EMPEROR_BTN2` Goto | the local player, only if the new emperor's capital is known to it | in the 0x150 handler |
| UI | `COWEMP`, `ROL_EMP`, `ROL_ELEC`, `ROL_HRE`, `HINT_27` | – | §3.7 |

No message to the old emperor or the electors; nothing in the log when the vote is skipped.

---

## 5. Port notes

State:
- `emperor_idx` (int, in globals; default 3 before the scenario; scenario/save key `emperor = TAG`,
  unknown tag → 0 = none);
- `elector_weight[tag]` (u8, from country.csv `Electors`; read-only);
- `prov.hre` (bool, from province.csv `HRE`; read-only).

Steps:
1. **ACTIVATE_MONARCH (0xbd)** of country C (host): if C == emperor and today ≥ start + 1 day →
   `elect()`.
2. **Month tick / game start (globals, host)**: if `countries[emperor_idx]` does not exist and not
   fantasy → `elect()`.
3. `elect()`: best = 0, winner = HAB; for every K in index order meeting §2.2 conditions compute
   the score of §2.2 and take it if > best. If the winner exists, post 0x150 to it.
4. **0x150 handler**: emperor_idx = C; EMPEROR news to the player if the capital is known; VP[Dip]
   += 50.
5. Helper `loyal_electors()` = count of electors E ≠ emperor with rel(E→emperor) ≥ 100; use it
   yearly (+2·N income cat 5), monthly (manpower rate +N), and in the tech-cost and stability-cost
   formulas (−0.01·N), for the emperor only.
6. Movement: emperor at war may enter any HRE province.
7. AI modifiers of §3.5; event triggers and scope −6 of §3.3; UI of §3.7.

Fixed point: relations are already integers after trunc; the 0.01·N terms are N percent. `rand()%10`
uses the shared game RNG (`0x5ba99c`), so it must be the same generator as everywhere else for
multiplayer/repeatability.

Open points: whether relation records are symmetric (affects which side's relation is the "vote");
what `0x419700` is; the map mode ids in §3.7; the exact sprite drawn on the emperor's capital.
