# Revolt risk, religion, conversion, Defender of the Faith (EU2 v1.09, spec from eu2.exe)

Clean-room notes: rules described in our own words, addresses are evidence only. Raw disassembly
(capstone) was used wherever Ghidra hid x87 operands. NIEPEWNE = guess, NIEZNANE = not found.

Religion indices (religion.csv order): 0 Protestant, 1 Reformed, 2 Counter-Reform, 3 Catholic,
4 Orthodox, 5 Sunni, 6 Shiite, 7 Pagan, 8 Confucian, 9 Buddhist, 10 Hindu.
World flags `0x4d16a0(n)`: 1 Reformation, 2 Calvin/Reformed, 3 Counter-Reformation, 4 its end.
"Same religion" everywhere = `0x431270(a,b)` returns 0: equal index, or the pair {2,3}.
"Effective province religion" r' = province religion (prov+0xf6), except 0 → 3 while flag 1 is off
and 1 → 3 while flag 2 is off.

## 1. Revolt risk of a province, `0x481a30` (integer, result always >= 0)

P = province, C = **controller** (P+0x3c8), O = owner (P+0x3c4). Tooltip `0x481dc0` mirrors the
same terms line by line (RRISK_* strings), which confirms the meaning of each term.

```
if O == 0, or P+0x40c != 0 (colony / trading post), or city(P+0x408)->vfunc+0x10 true:  RR = 0
                                                                  (tooltip RRISK_TP_COL)
tol = C.tolerance[r']                 (float, country+0x228 + 4*r')
t   = trunc(1 - tol)                                                0x481b18 / 0x481b8c
if r' same as C.state religion (C+0x254):   if t > 0: t = 2*t       0x481ba8
else (heretic / heathen):                   if C is Defender of the Faith: t += 2   0x481b34
if t < 0: t = t / 2   (C division, toward zero)                     0x481bb4
                         -> RRISK_RELIGION "+%d" (t>=0) / RRISK_RELIGION2 "%d" (t<0)
if P culture (P+0xd8) not accepted by C (0x429fb0 on C+0x382):
    t += 1, +1 more if C is at war (0x43c000 = C+0x38e -> byte +0x47cc)   RRISK_CULTURE
t = t - (stability(C) + 2)          stability = int (0x43a230)      0x481c03  RRISK_STAB/STABP
city: -1 if manufactory (city+0x1c != 0)                            RRISK_MANU
      -2 if capital (city byte +0x2c)                               RRISK_CAP
+1 if O != C (occupied)                                             RRISK_OCCU
+2 if any neighbour (P+0x114 count, P+0x11c list, 12-byte entries) is controlled by tag 0x153
   (rebels)                                                         RRISK_REB
t = trunc(t + WE)    WE = 0x481920 (float, below)                   RRISK_EXHAU
+1 if C.war taxes active (C+0xe78 > 0)                              RRISK_WARTAXES
+1 if less than 1 year (360 days) since C's bankruptcy date C+0xe9c RRISK_BANKRUPTCY
   (0x417440 date difference, year field < 1)
+3 if city flag +0x6a (tax collector / "bailiff")                   RRISK_BAILIFF
-1 if city flag +0x6c (chief judge / courthouse)                    RRISK_COURTHOUSE
+ C+0x1000 (int, country event modifier, 0x460440) + P+0xc2 (int, province event modifier)
                                                                    RRISK_EVENT
N = 0 if P is a national (core) province of C (C+0x1350 list, vfunc +0x24(P id)) or r' == 7 (pagan);
    else by years since acquisition A = currentYear - P+0x3be:  A<10 -> 3, A<20 -> 2, A<30 -> 1, else 0
RR = t + N ;  RR = max(RR, N)                                       RRISK_NATIONALISM "(can not be lower)"
```

War-exhaustion term `0x481920` (float): w = trunc(C.WE) (C+0x122a, `0x43d2b0`) × m, floored at 0.
m = 1.0 for a core of C; otherwise: accepted culture & same religion 1.25; exactly one of the two
differs 1.5; both differ 2.0 (constants 0x5e563c / 0x5e44ac / 0x5e4ee4). Matches readme 1.05.

Notes
- religion.csv columns are **not** used in RR. Heretic vs heathen is not distinguished: only
  "same / not same" (with the 2~3 pairing) and the tolerance value of that faith.
- The tolerance index for a Counter-Reform province is 2, whose value is always forced equal to
  Catholic (index 3) (`0x4310e0` / `0x431090`: every write copies tol[3] into tol[2]).
- Example, Catholic state with default tolerances (sec. 5), stability 0, at peace:
  Catholic province t = trunc(1-10) = -9 -> -4, then -2 => 0 after floor;
  Sunni province before 1st flag (tol 0): t = 1 (+2 if DoF) -2 => 0 (1 with DoF);
  Shiite (tol -10): t = 11 (+2) -2 => 9 (11).
- RRISK_DUTCH text exists but no code references it (NIEPEWNE: unused).
- The SPEC_TIME_ECONOMY claim "+1 for 10 years after bankruptcy" is wrong: the test is < 1 year.
  SPEC_WAR_DIPLOMACY §13.1 has the doubling/DoF branches swapped: doubling is for the **same**
  religion, +2 DoF for **other** religions (0x431270 returns 0 when equal; `je 0x481b4a` = same).

## 2. Where RR is used

| Use | Rule | Evidence |
|---|---|---|
| **Monthly revolt roll** `0x460450(C, bonus)` | run on the host, per country C, from `0x460aa0` (country monthly `0x451300` step 25). Skipped if global flag `[0x8ff63c]+0x2766` (NIEPEWNE: "no revolts" option) or C is a special tag (0, 0x14f..0x153, `0x45bce0`). Needs > 1 province in C's list +0x133e/+0x1342 (NIEPEWNE: controlled provinces). For each such province: **revolt if rand() % D < RR + bonus**, D = 1500 for a human country, 400 for an AI (`[0x91a738+4*id]` 0 = AI). Revolt = `0x461780(province)` (rebel army, SPEC_WAR_DIPLOMACY §13.3) | `0x460450` |
| immediate extra rolls (same function, bonus) | stability falls below -3 (`0x436490`: bonus 0); war taxes raised (`0x448eb0`, 0); tolerance changed (`0x453630`, 0); state religion converted (`0x4536c0`, **5**); bankruptcy (`0x450ad0`, **10**); peace-breaking case (`0x44ee50`, 2); others `0x44b170`, `0x44bcb0`, `0x4559e0` (0) | call sites |
| low-stability uprising (2nd half of `0x460450`) | if > 4 controlled provinces, not at war, stability < 1: p from stability s and owned count n (C+0x131a): s=0: n/10+2; s=-1: n/9+2; s=-2: n/9+3; s=-3: n/8+3; s=-4: n/7+4; then p += 3 - domestic slider #1 (`0x42bbf0(1)`), min 1; if badboy C+0xe42 > limit `0x45aef0`: p = trunc(10 × excess + p); p = 0 if n < 2. If rand()%20000 < p: each army 50 % defects to rebels (event 0xd5), each own province 25 % goes to rebel control (event 0x8d), event 0x14e posted | `0x46058a..0x460700` |
| Tax modifier | M -= 0.05 × RR (0x5e71f8) | `0x47e0b0` |
| Same M also feeds two recruitment/manpower quantities | `0x4851b0`, `0x485320` (NIEPEWNE which UI values) | |
| Province window | "-5 % × RR" line (HPWREVOLT, constant -5.0) | `0x47db70` |
| Religious revolt | after the Reformation, RR > 10: bigger rebel army (§13.3) | `0x461780` |
| Garrison defection | chance% = min(40, 2·RR − 5·fortLevel) + 1 | `0x461780` |
| Map mode 7 colour | shade index 16 − min(16, 2·RR) | `0x478950` |
| AI | target score −3·RR (`0x54de70`); garrison weight × (RR+1) (`0x554140`) | |

Production is **not** reduced by RR in code (readme says it is; `0x480690` never calls 0x481a30).

The province roll in `0x480370` (r = P+0x13c − 2 city / −4 colony, halved, + trunc(P+0x134), squared,
colonies −10 cap 5, rand()%1000 < r → `0x4d8f10`) is the **native uprising**, not the revolt
roll: P+0x12c/+0x13c/+0x134 match province.csv "Native Combat Strength / Ferocity / Efficiency"
(NIEPEWNE mapping; RRISK_TP_COL text says natives revolt in colonies). Our `rev_month` currently
uses this native formula for RR — it should use the 1500/400 roll above.

Rebel-held provinces (bonus, answers §13.3 "NIEZNANE"): `0x461220` (from `0x460ae0`), province
controlled by 0x153 for more than 3 years: each neighbouring owner X that knows the province scores
10 (+25 → 35 if X accepts the culture) then +30 if X's religion is the province's, else
score = trunc(X.tol[provRel] + score); +30 if province is in X's tag list 0x8ff64c (cores), +20 if in
list 0x8ff65e (NIEPEWNE: claims); −1 if that neighbour is rebel-held or X is pagan and the province
is not. Highest positive score gets the province (`0x4614b0`); otherwise the first country in
P+0x68 that knows it (non-pagan rule as above).

## 3. Converting the state religion

UI: Convert disabled while at war (`0x43c000`) or target = current; targets `0x45fc40`. Confirm
CONFIRMRELIGION → `0x444a00(target, 0)` posts command 0x119 REQ_CHANGE_RELIGION (→ 0x11a EXEC).
Events use the same command with flag = 1 (`0x594eb0` effect type 10). Executor `0x4536c0`:

1. new == current → nothing.
2. Tech-block stability track field (C+0xe46)+0x70 := today + 10 years. Reader NIEZNANE
   (NIEPEWNE: stability-cost period).
3. Only if neither old nor new religion is 2 (Counter-Reform):
   - flag 0 (player/AI choice): **stability −5** (`0x436490(5)`; drop below −3 also rolls revolts);
   - badboy C+0xe42 += 1 (floored 0);
   - for every other country X: if X's religion is the same as C's **old** religion: relation
     C↔X = rel − 100 (both directions get the same value, clamped ±200); else if X's religion ==
     new exactly: rel + 50;
   - if C was Defender of its old faith: title cleared (`0x431330(old, 0)`);
   - every alliance (not vassalage, type ≠ 2) where C is a member and another member has the old
     religion: C leaves it (`0x441520`).
4. Catholic → Protestant only, and C+0x11c (year of C's last conversion) < 1419 (i.e. never
   converted before, NIEPEWNE initial value 0): income category 7 = 25 × owned provinces.
5. Messages: own player 0x9e WERELIGION (tax word "decrease" if new == 0 Protestant, else
   "increase" — literal, cosmetic); others who know C: 0x9d OTHERRELIGION with OTHERFRIEND (viewer's
   religion == new), OTHERHERETIC (== old), else OTHERNEUTRAL. No CB is actually created
   (WERELIGION_4 text only, NIEPEWNE).
6. state religion := new; **all 11 tolerances reset** to the defaults of the new religion (sec. 5).
7. Event triggers of class 0x19 re-evaluated (`0x55aa60`).
8. Host, neither religion 2: immediate revolt roll with bonus +5 (`0x460450(5)`).
9. C+0x11a := today (last-conversion date). No ducat cost. Province religions do not change.

## 4. Defender of the Faith

Table `0x64d4bc[religion]` = country id (0 none); `0x4312a0` reads it with 1 and 2 aliased to 0
and 3 (Reformed shares Protestant's title, Counter-Reform shares Catholic's). C is DoF if
table[C.religion] == C.id (`0x4602d0`).

| Item | Rule | Evidence |
|---|---|---|
| Can claim | religion not 7..10, no defender yet, treasury C+0xe70 >= 1000 | `0x460340` |
| Claim (cmd 0x129 → exec `0x455660`) | for every country X of the same religion: relation C↔X += 50 if currently > 0, else −50 (mirrored); table := C; message 0x9f WEDEFENDER / 0xa0 OTHERDEFENDER; expense category 15 = 1000 | `0x455660..0x4557d1` |
| Revolt risk | +2 to the religion term of every non-same-religion province | `0x481b34` |
| Land morale | +0.5 | `0x438690` |
| Naval morale | +0.15 | `0x438850` |
| Tech cost | ×1.15 | `0x432300` (R in SPEC_TIME_ECONOMY) |
| War exhaustion | gains 1/45 instead of 1/36 per month of combat | `0x451300` |
| Casus belli | when a country of the DoF's faith is attacked (`0x44b7a0`, at `0x44b931`): the DoF of the victim's religion gets GIVECB (0xad) against the other party for 3 months (date offset 0 y 3 m), unless it is one of the two, or its diplomacy flag +0x36 toward the victim is set. NIEPEWNE which side is "victim" in that handler | `0x44b931..0x44ba14` |
| Victory points | each DoF on the winning side of a peace: +15 (`0x55f030(1, 15)`) | `0x44d923` |
| Lost | (a) the signing leader of the losing side of a non-white peace (`0x4cce80` false) | `0x44d8ce` |
|  | (b) converting state religion (sec. 3) | `0x453862` |
|  | (c) becoming a vassal (alliance type 2 accepted) | `0x449e27` |
|  | (d) country destroyed | `0x438c80` |
|  | (e) handler `0x455xxx` that ends all wars, −100 VP, stability set to 3 (NIEPEWNE: country reset / restart) | `0x45548b` |

## 5. Tolerance values

Storage: country+0x224 = small object (vtable 0x5e5844), floats tol[0..10] at +0x228..+0x250,
state religion copy at +0x254. Tol[2] is always kept equal to tol[3].

**Initial values**: scenarios only say `religion = { type = X }`. The parser (`0x4308e0`, token 0x73)
loads the defaults `0x430c70(X)`; explicit keys `protestant/reformed/.../hinduism = value` (tokens
0xf0..0xfc, used by saves) override them; after parsing, tol[7] (pagan) := 0 unless the state is
pagan. Country creation (`0x452de0`) and conversion use the same defaults. Not from religion.csv.

Defaults (indices not listed = 0; 7..10 always 0; pagan/eastern states all 0):

| state | condition | P0 | R1 | C3 (=CR2) | O4 | Su5 | Sh6 |
|---|---|---|---|---|---|---|---|
| 0 Prot | flag2 off | 6 | 0 | 0 | 0 | −6 | −6 |
| 0 Prot | flag2 on | 6 | 2 | −2 | 0 | −6 | −6 |
| 1 Ref | | 3 | 5 | −2 | 0 | −6 | −6 |
| 2 CR | | −5 | −5 | 10 | 3 | −3 | −3 |
| 3 Cath | no flags | 0 | 0 | 10 | 0 | 0 | −10 |
| 3 Cath | flag1 only | −4 | 0 | 10 | 2 | −7 | −7 |
| 3 Cath | flag2 | −4 | −2 | 10 | 2 | −6 | −6 |
| 4 Orth | no flags | 0 | 0 | −4 | 10 | −6 | −6 |
| 4 Orth | flag1 only | −2 | 0 | −2 | 10 | −4 | −4 |
| 4 Orth | flag2 | −3 | −3 | 0 | 10 | −4 | −4 |
| 5 Sunni | flag1 off | 0 | 0 | −3 | −1 | 10 | −6 |
| 5 Sunni | flag1 on | −2 | −2 | −2 | 0 | 10 | −6 |
| 6 Shiite | flag1 off | 0 | 0 | −3 | −1 | −6 | 10 |
| 6 Shiite | flag1 on | −2 | −2 | −2 | 0 | −6 | 10 |

(The Catholic no-flag row has Sunni 0 / Shiite −10 exactly as coded.)

**Changing**: UI sliders (clamped −10..10, linked group, SPEC_UI_WINDOWS4 §2) → command 0x115/0x116
CHANGE_TOLERANCE carries 11 signed bytes; executor `0x453630` stores them as floats (integers);
if any changed and host: immediate revolt roll `0x460450(0)`. No stability or money cost found.

**Other uses** (all direct reads of +0x228 in the exe were enumerated):
- monthly relation drift `0x43c970`: relation of C toward X += 0.025 × C.tol[X.state religion],
  unless X is pagan (7) (`0x43cc77`);
- rebel-province defection score (sec. 2, `0x461365`);
- religion window UI (`0x51a89b`), revolt risk (`0x481b18`, tooltip `0x481ed3`).
- Missionary chance does **not** read tolerance (HINT_81: ADM, culture, population).

## 6. Differences from src/revolts.c (to fix)

- religion term: use t = trunc(1 − tol[r']) (same: double if > 0; other: +2 DoF), halve if < 0 —
  not 2/1 − tol/50; tolerance is a −10..10 integer, not ±100.
- culture +1 (+1 more at war) correct; stability −(s+2) correct; add manufactory −1, capital −2,
  WE × m term, war taxes +1, bankruptcy (< 1 year) +1, events, nationalism floor 3/2/1 by years
  (non-core, non-pagan), not "non-core min 1".
- uses controller tolerance/culture (correct per readme "based on the tolerance of the controller").
- revolt roll: rand()%1500 (human) / %400 (AI) < RR + bonus, monthly, all controlled provinces
  of countries with > 1 province — replace the r²/1000 native formula.
