# The military AI (eu2.exe v1.09) - specification

Read from the `eu2.exe` v1.09 disassembly as a **specification only** (no code copied; addresses are
evidence). Markers: **NIEPEWNE** = believed, not proven; **NIEZNANE** = not found. Conventions as in
`docs/SPEC_AI.md` (U = acting country, K = its AI controller "King" = country+0x212e, prefs = the .ai
file object at country+0x25c, rand = 0x5ba99c, dates 30-day months).

Parts (decoded separately, cross-checked at the joins; each part keeps its own numbering):
- **A** the King: objects, strategic mode, think order and interval, budget sliders, resource pools,
  peace-time demobilisation.
- **B** recruitment: wanted force levels, BuildArmy/BuildFleet plans, unit mix, where and how paid,
  military buildings.
- **C** the land war: Generals, CONQUER and LANDATTACK plans, target scoring with the .ai weights,
  force needed, gathering, attack/retreat thresholds, sieges and assaults, army split/merge, rebels.
- **D** Admirals and naval plans (naval attack, blockade, break blockade), DEFEND/GARRISON, INVASION,
  TRANSPORT.

## 0. Overview for the implementer

```
King (per AI country)            think every 20-39 d (peace) / 5-9 d (war)          A
 |- strategic mode (BUILDUP / COLONISE / DOMINANCE), re-rolled after war or >7 y     A.3
 |- budget sliders + tech/stability plans + AI money/inflation cheats                A.5
 |- wanted forces -> BuildArmy (war only) / BuildFleet plans; reinforcement builds   B
 |- 9 resource pools: money, armies, fleets, ... given to plans by score             A.6, B.4
 |- General per own land-connection group (prov+0x498)  think 3-12 d / 7-20 d        C, D.1
 |    |- up to clamp(4 + prov/3 + links, 4..16) plans, best 16 candidates kept
 |    |- CONQUER (enemy-held provinces), LANDATTACK (enemies/rebels in our land)     C.3, C.4
 |    |- DEFEND -> GARRISON (peace only)                                             D.5
 |    '- expedition General per overseas enemy group -> INVASION + TRANSPORT         D.6, D.7
 '- Admiral per sea area of a group: home port; at war NAVALATTACK, BLOCKADE,
      BREAKBLOCKADE                                                                  D.2-D.4
```

### 0.1 Timing (all via _CEUEVT_AI_TICK_ 0xd8 posted to self, 0x55af00)

| agent / plan | interval | where |
|---|---|---|
| King | 20 + rand%20 days; /2 if country byte +0x11e; /4 at war; min 2 | A.4 |
| strategic mode | at init, when a war ends and we are at peace, else 4 % per think once older than 7 years | A.3 |
| General / Admiral | 0x55a4a0(3,10) in the capital's group, 0x55a4a0(7,14) elsewhere; first think 0x55a4a0(5,10); scaled by the AI option [[0x8c0bfc]+0x2c6] | C.2.2, D.1 |
| candidate commit | 1 day after the first new candidate | C.2.4 |
| CONQUER | 0x55a4a0(3,5) not engaged; 2 + rand%5 engaged | C.6 |
| LANDATTACK | 3 + rand%7 not engaged; 2 + rand%5 engaged | C.6 |
| naval attack family | 0x55a4a0(2,3) | D.3 |
| buildings | retried every 100-199 days, peace only | B.6 |
| resource pools | a TICK_RESOURCE per non-empty pool each King think | A.4, A.6 |

0x55a4a0(a, b) = a + rand%b days, adjusted by the AI-speed option (formula in D.1).

### 0.2 What the .ai file and the strategic mode change

| input | read by | effect |
|---|---|---|
| `war` (+0x22) | mode choice A.3, war predicate (SPEC_AI) | DOMINANCE weight; willingness to declare war |
| `expansion` (+0x16) | A.3 | COLONISE weight (0 = never COLONISE unless colonies exist) |
| `combat` list (+0xc2) | A.3, diplomacy | +100 DOMINANCE weight; war targets (SPEC_AI) |
| `ferocity` (+0x04) | peace logic (SPEC_AI) | not read by the military code |
| `base` / `front` (+0x28/+0x2c) | gathering point C.4.2 | rally near home vs near the target |
| `conquer` block (+0x5c..+0x70) | CONQUER score C.3.1 | which provinces to attack; `base` adds 15 x base to every conquer plan (main "aggressiveness" knob) |
| `garrison` block (+0x78..+0x88) | GARRISON score D.5 | where peace-time troops stand |
| mode BUILDUP | buildings B.6 | military buildings score 35 instead of 11 |
| mode COLONISE | INVASION D.6, peace acceptance (SPEC_AI) | no distance penalty on overseas targets |
| mode DOMINANCE | (none in the military code) | acts through diplomacy: war declarations |
| at war | everywhere | King thinks 4x as often; BuildArmy only at war; garrisons dissolve; naval scans only at war |

NIEPEWNE: no military function found reads the mode or `war`/`ferocity` directly; aggressiveness is
expressed by war declarations (diplomacy) and the conquer weights.

### 0.3 Rebels and enemies in our territory

There is no separate rebel AI: rebels (tag index 0x153) are hostile to everyone and are handled by
LANDATTACK (C.4.1): an own province holding enemy or rebel troops, or occupied, is a candidate with
score 20 + 2V; own occupied x4 + 100, rebel-held x2, rebel siege x3 - so recovering occupied home
provinces outranks most conquests. Peace-time garrisons (D.5) weight provinces by unrest
(NIEPEWNE: that is the rebel prevention term). Enemy fleets in adjacent sea zones produce
NAVALATTACK plans; a blockaded own port produces BREAKBLOCKADE.

### 0.4 Compared with our current AI (`src/military.c`)

| topic | ours (v1, NIEPEWNE rules) | the exe |
|---|---|---|
| recruitment | monthly, capital, 1000 inf (cav 1 in 4) while army < 4000 + 2000/prov (x2 at war), treasury >= 60 | B.1-B.3: wanted inf 1000s + 10000, cav, art by tech; BuildArmy only at war (+ reinforcement requests); mix from costs/shock; nearest recruitable province to the target |
| target choice | nearest enemy-controlled province within 120 days, skip if enemy > 1.2x | scored candidates (C.3/C.4) with the .ai weights, max 4..16 plans per General |
| forces | each army alone, weekly | plans gather armies to 75 % of the need, merge/split, attack at >= 100 % and full morale |
| retreat | none | C.4.4: < 66 % strength and morale |
| sieges | not interrupted | C.4.5 assault rule |
| fleets | sail home / carry invasions breadth-first | D.2-D.4 naval attack, blockade, break blockade; TRANSPORT plans |
| peace | nothing | DEFEND/GARRISON by the garrison weights; demobilisation (A.7); fleets home |
| rebels | armies of countries with rebels go out | LANDATTACK with x2/x3 rebel bonuses |

---

## A. The King: structure, strategic mode, think order, budget (eu2.exe v1.09)

### A.1 Objects

The AI controller of a country (country+0x212e, "the King", log name "King" 0x547c50) is class vtable
0x5f17f8 (ctor 0x5417b0, dtor 0x5419e0, think = slot 0x1c = 0x543f20). Its agents:

| field | object | ctor / vtable | role |
|---|---|---|---|
| +0x48 | 0x55a5c0 object, vt 0x5f1a78 (0x40 bytes) | 0x541e76 | gets vfunc 0x80(a, b) when a war involving us ends (0x544371) - NIEPEWNE: peace bookkeeping |
| +0x4c | 0x53b6c0 (0xd4 bytes) | 0x541e23 | economic/building agent (NIEPEWNE) |
| +0x50 | diplomat 0x53e5a0, vt 0x5f088c | 0x541ecd | SPEC_AI |
| (list) | 0x53dbe0 (0x5a6 bytes) | 0x541dd1 | merchant/colonist agent (NIEPEWNE) |
| +0x754/+0x758 list | Generals, one per land-connection group (prov+0x498) | 0x542f90 / 0x5482a0, vt 0x5f2260 | §C |
| (list via 0x543110) | Admirals, one per sea region of a land group | vt 0x5f2470 | §D |
| +0x778 / +0x77c | the BuildFleet / BuildArmy plan (at most one each) | §B | |

Controller fields used by the military AI: +0x0a country, +0x0e "is AI", +0x3c current war target
(set by the war plan, cleared when the country stops existing), +0x40 strategic mode (0 DOMINANCE,
1 BUILDUP, 2 COLONISE), +0x44 date the mode was chosen, +0x54/+0x55/+0x56 "plan pending" bytes
(manufactory / stability / tech plans), +0x58..+0x6c wanted unit counts (§B), +0x70 nine resource
pools of 0x12 bytes (+0x780[i] = "tick pending" byte per pool), +0x1b6 King's free money,
+0x1f6 list of owned provinces (each province+0x49c = this King), +0x204 per-country records (SPEC_AI).

### A.2 Initialisation (0x541c50, at game start / load)

1. Choose the strategic mode (A.3).
2. Every existing unit of the country (country+0x124c) is adopted: armies through vfunc 0x198,
   fleets through vfunc 0x1a8 (they go into resource pools 5 / 6, A.6).
3. Owned provinces are listed (+0x1f6); resource pools are seeded: vf 0x144(treasury +0xe70),
   vf 0x174(+0xe8c), vf 0x150(manpower +0xec0), vf 0x15c(+0xe94), vf 0x168(+0xe98) (pool meaning
   beyond money and manpower NIEPEWNE: envoy counts).
4. The agents of A.1 are created; then the per-country records (0x154 countries).
5. 0x5445c0 (manufactory plan, economic, not described here).
6. For every land-connection group in country+0x12e8 (the groups where the country owns land):
   a General is created if none has that key (0x542f30 searches +0x758 by General+0x48); it is put
   at the head of the list if 0x472ebb(group) is true, else at the tail (NIEPEWNE: "home group");
   then for every entry in group+0x1a of type +0x28 == 2 an Admiral is made (0x543110(id)).
7. Subscribes to listener lists 0x13, 0x12, 0x16 (0x55ae60; 0x16 is the monthly timer, the other
   two NIEPEWNE) and schedules the first think (A.4 interval).

### A.3 Strategic mode (0x5421a0)

Called at init, after every war involving us ends while we are no longer at war (0x544350, together
with the diplomat's vfunc 0x80 and 0x5443a0), and from the think when the mode is older than 7 years
with a 4 % roll per think (year part of today - +0x44 > 7 and rand%100 > 95).

First our own tag is removed from our .ai `combat` list if present. +0x44 = today. Three integer weights:

- B (BUILDUP) = 100; 300 if the last war ended less than 3 years ago (today.year - country+0xe3e.year < 3).
- W (DOMINANCE) = prefs `war` (+0x22, int); +100 if the `combat` list is not empty **or** there is a
  current war target (+0x3c); + 10 x (cores - owned provinces) when we have more cores (+0x1350
  count) than owned provinces (+0x131a count).
- C (COLONISE): E = prefs `expansion` (+0x16). If colonists per year (0x4582d0) > 0.99: E += 25,
  else E = 0. If `expansion` == 0: E = 0.
- After 1480 (year > 0x5c8): if colonists/yr > 0.99: B = B/5, W = W/5 (integer); if colonists/yr > 1.2:
  E += 200 and B = 0; (still after 1480) E += country+0x386 = the unowned land provinces the country knows
  (the recount `0x43ac12`: owner none, terrain < 5, known byte +0x256).
- C = 0 if country+0x386, +0xfe8 (explorers) and +0xfe4 (conquistadors) are all 0; else C = E + 1.
- For every owned province that is **not** a core (0x482940 false), with prov+0x106 == 2
  (NIEPEWNE: region/continent class 2) and acquired less than 30 years ago (d = (year -
  prov+0x3be)/10 < 3): W -= 10 x (3 - d) and B += 10.
- W = max(W, 0). T = B + C + W; r = rand % T (r = 0 if T == 0).
  r <= B -> BUILDUP (+0x40 = 1); else r <= B + C -> COLONISE (2); else DOMINANCE (0).
  Logs "****** %s is now in BUILDUP/COLONISE/DOMINANCE MODE!" (0x631e38 / 0x631e14 / 0x631df0).

What the mode changes is described where it is read (SPEC_AI: war predicate mode 1/2, H score,
peace acceptance; §B-§D for the military).

### A.4 The King's think (0x543f20) and its interval

Runs only for AI countries (+0x0e). If the war-target country no longer exists, +0x3c = 0. Then in order:

1. 0x544b60 budget sliders, tech/stability plans and money cheats (A.5);
2. 0x5445c0 manufactory plan (economic);
3. 0x5424e0 wanted army/navy composition (§B);
4. 0x542d30 BuildFleet trigger, 5. 0x542b80 BuildArmy trigger (§B);
6. 0x543990 religious tolerance sliders (11 religions, country+0x228; not military);
7. for every province we **control** (country+0x1342 list) that another country owns and where
   0x45b690(us, prov) holds (a trading post that can be burnt, SPEC_UI): burn it (0x43eee0);
8. 0x543c80 peace-time demobilisation (A.7);
9. for each of the 9 resource pools at +0x70 that is not empty (pool vfunc 8 false): 0x545790(i)
   posts a _CEUEVT_TICK_RESOURCE_ (0xe0) to the King for pool i, once (+0x780[i] guards);
10. mode re-roll (A.3).

Next think: n = 20 + rand%20 days; n = n/2 if country byte +0x11e is set (the scenario's featured countries; also used
in SPEC_AI §6); n = n/4 if at war (relation byte +0x47cc); n >= 2. Scheduling is a
_CEUEVT_AI_TICK_ (0xd8) posted to itself (0x55af00(days, 0)); the 0xd8 handler 0x55aec0 calls
vfunc 0x1c (think) if the agent is active (vfunc 0x18). **So the King thinks every 20-39 days at
peace and every 5-9 days at war (10-19 / 2-4 with flag +0x11e).** The same 0x55af00 mechanism drives
every General, Admiral and plan (their intervals are in §C/§D).

Event handler 0x545710: 0xe0 TICK_RESOURCE (pool index i at event+0xc): i == 0 (money) first
re-computes the King's free money (0x546090: +0x1b6 = treasury - money held by agents); i in 1..8
-> 0x5458a0(i); 0x169 _CEUEVT_ADD_OWNERAREA_ -> 0x544520 (A.6).

### A.5 Budget sliders (0x544b60), log "%24s budget l n s pe te $" (0x631e5c)

I = last month's income (+0xe80); D = max(0, ledger costs (+0x34 + +0x38 + +0x6c of +0xe74) - monthly
tax 0x457a00) = the monthly deficit not covered by tax.

1. Stability share s, remainder R:
   - I <= 0.00001: s = 0.2, R = 0.8 if stability < 0, else s = 0, R = 1.
   - else s = min(max(0, -0x431a40())/I, 1) (0x431a40 = monthly stability bonus; negative = needs
     money). If stability < 2: s = max(s, 0.05); stability >= 0: s *= 1.25; stability < 0: s = 0.75
     (1.0 if stability < -2) and, once (+0x55), a BOOST_STABILITY plan (vt 0x5f3218, score
     6 x (7 - stability)). R = 1 - s.
2. Treasury share $ = R, except: if I - D > 0, I > 0 and treasury < 1000: $ = D / I. At war:
   $ += 0.5 if treasury < 100, else $ += 0.25 if treasury < 300.
3. Investment rest X = max(0, 1 - $); if treasury > 2000: X = 1, $ = 0.
4. tech = 0.6X, trade = 0.2X, production = 0.2X; "trade minded" (0x43a9e0): trade = 0.3X,
   production = 0.1X; if tech object 0x4333f0 level > 3: tech = 0.8X, trade = 0.01 (NIEPEWNE which
   tech that is - SPEC_TUTORIAL says trade).
5. Naval part n = coast share (+0x36c) x 0.25, x2 if the country has a shipyard (+0xfd2 > 0), x1.5 for
   tag indices 1, 6, 12, 26 (ENG, PRU, MIN, GEN by DB/country.csv row - as found, byte table
   0x5456e8); n = 0 if the country has no ships (+0x122e + +0x1232 < 1).
   naval tech = n x tech, land tech = (1 - n) x tech.
6. Once (+0x56), if annual income > 100 and not at war: a boost plan (score 100) for land tech if
   land > 0 and rand%100 < 40, else naval tech (< 40), else BOOST_TRADE on the 0.2/0.3 share (< 50),
   else BOOST_PRODUCTION on the other share.
7. Inflation cheat: limit = max(human player's inflation (0x43c8e0 of [0x8bfa50]), L) with
   L = max(0, year - 1419) x 0.0025, x (10 x 0x45b240(us) + 1.05) when 0x45b240 > 0; difficulty
   option [[0x8c0bfc]+0x2ca] == 4: limit/3, == 3: limit/2. Unless the tag is excluded (0x43a9c0), if
   our inflation > limit and rand%100 < 10: post _CEUEVT_INFLATION_ (0xce) -0.005 to ourselves.
8. Money cheat (not for 0x43a9c0 tags), with treasury < 50, treasury < D and no 0x45bc00 (NIEPEWNE:
   already has a loan / bankrupt): at war post GAINMONEY (0x5f) 12D + 10 (category 7); at peace
   20D + 10. Else, within 50 months of the last bankruptcy (+0xe9c): GAINMONEY D + 10.
9. All six shares are floored at 0, logged, normalised by their sum and, if the summed absolute
   difference to the current sliders (+0xea4..+0xeb4) exceeds 0.02, sent as
   _CEUEVT_REQUEST_INVESTMENT_CHANGE_ (0xbe, 0x443e90). NIEPEWNE: the log labels "pe"/"te" are
   swapped relative to the boost plans.

So the AI never budgets "for the army" through sliders; recruitment money comes from the treasury
through the resource pools (A.6, §B).

### A.6 Resource pools (the King's "AI Money Requests")

Nine pools at +0x70 (0x12 bytes each; a list of holders +0x74 and of requests +0x116). Pool 0 =
money, pool 5 = armies, pool 6 = fleets (proven by 0x545a33/0x545ab3; the others NIEPEWNE: manpower
and envoys). On TICK_RESOURCE(i) (0x5458a0): every holder that is not done gives up / keeps its
resource; a request whose plan's root score exceeds the holder's root score by more than 0.1 takes
the resource from it (holder vfunc 0x10); unserved requests are then served from the free stock.
Leftover (unassigned) armies go to a General: 0x5432a0 gives each free army (not empty, not in
battle or siege unless retreating, not flagged +0x53/+0x55) to the General of the army's
land-connection group, else to the General whose group is adjacent (cost 0, or 1 when 0x472ebb),
else to the nearest by squared map distance (+0x3e4/+0x3e8). Fleets likewise via 0x543450.
0x544520 (on ADD_OWNERAREA, peace only): each General's current plan gets +0x80 = 0.25 x its score /
(sum of all Generals' plan scores, at least 1) - its money share.

### A.7 Peace-time demobilisation (0x543c80; only when not at war)

For each unit of the country that is idle (path object +0xc4 empty, not in battle/siege +0x48/+0x4c,
not flagged +0x55):
- standing in a province owned by another country that it may not enter (0x48c640 false): disbanded
  (0x43fa00);
- a fleet (+0x54): ordered home to the capital (event 0x34 UNITMOVE_ADD_DEST); disbanded if no path;
- an army: disbanded if the country owns no port (no province with +0x41c) and the unit's vfunc 0x18
  holds, or if annual income < its cost (vfunc 0x38) and vfunc 0x18 holds (NIEPEWNE what vfunc 0x18
  tests - probably "may be disbanded / not a garrison").

---

## Military AI, part B: recruitment (eu2.exe v1.09)

Read from the disassembly as a specification only. NIEPEWNE = believed but not proven, NIEZNANE = not
found. C = the AI country (agent+0xa), K = its controller ("King", class vtable 0x5f17f8, think
0x543f20). Unit fields: an army has inf/cav/art at unit+0xf0/+0xec/+0xe8, a fleet has warships/galleys/
transports at +0xe8/+0xec/+0xf0 (floats, rounded up when counted).

#### 0. Helpers used below

| Address | Meaning |
|---|---|
| `0x45ab50(C)` | **land support limit** in men = 1000 × (5·(C+0xef6 + 2·C+0xfd6 + 1) + 2·(round(C+0xf1e) + round(C+0xec4)) + round(0.2·income·(0.1·slider5+0.5)) + round(C+0xf72)) (same formula as the ledger's "supportable" value, layouts_iw.md) |
| country vf 0x388/0x38c/0x390 (`0x45f570..0x45f590`) | infantry / cavalry / artillery owned: C+0x123a/+0x123e/+0x1242 **plus** what is in the build queue (C+0x125c list, `0x48fad0..`) |
| country vf 0x394/0x398/0x39c (`0x45f5a0..0x45f5c0`) | warships / galleys / transports: C+0x122e/+0x1232/+0x1236 plus queued |
| country vf 0x30 (`0x429590`) | **cavalry allowed**: false only if the capital's continent (prov+0x106) is 1 and C+0xe4e < 5 (NIEPEWNE: land tech < 5 for New-World natives) |
| country vf 0x34 (`0x4295c0` -> `0x432d70`) | **artillery allowed**: land tech (tech object +0xc, vf 0x34) >= 7 |
| `0x429ab0` / `0x429b00` / `0x429b50` | cost of infantry / cavalry / artillery (recruit window, SPEC_UI_WINDOWS4 §1.4) |
| `0x4295d0(inf,cav,art)` | army cost in ducats (each count × its unit cost × 0.001 per man); fleets `0x4296f0` |
| `0x45a1f0` / `0x45a3f0` / `0x45a5f0` | monthly maintenance of warships / galleys / transports |
| `0x43a970(C)` | true if fantasy mode, or `0x4582d0(C)` >= 0.99, or C+0xfe8 > 0, or C+0xfe4 > 0 (NIEPEWNE: "overseas/colonial power") |
| `0x436540(C)` | number of armies (units in C+0x124c whose vf 0x14 says "land") |
| `0x45b7d0(C)` | capital province (C+0x1f7e) |
| `0x4851b0(p)` / `0x485320(p)` | recruit capacity of a province, in **regiments (1000 men)** = min(province manpower, country pool C+0xec0); ships `0x485320` |
| `0x45e540(C,p,i,c,a)` / `0x45ea10` | "may recruit this army / fleet in p" (the human UI rule) |
| `0x481820(p,tag)` | an army hostile to tag stands in p |
| `0x481870(p,tag)` | p is owned by a country at war with tag and has a fortress (city+0x3a > 0) |
| `0x47f3e0(p)` | fortress level; `0x47caf0(p)` has a shipyard; prov+0x498 continent / land-connection group; prov+0x3e4/+0x3e8 map x,y |
| LAND.CSV getters `0x433400..0x433590` | IF, CF, AF, IS, CS, AS (fire/shock of inf/cav/art, /100) for C's current land tech; NAVAL getters `0x4336d0..0x4337c0` |

#### 1. Wanted force levels (`0x5424e0`, every King think)

Called from the King think `0x543f20` (after the budget `0x544b60` and `0x5445c0`), then the fleet
trigger `0x542d30` and the army trigger `0x542b80` run in that order. The King think re-schedules itself
(`0x55af00`) in d = 20 + rand%20 days; d halved if C+0x11e (NIEPEWNE flag); quartered if C is at war
(relation table +0x47cc); minimum 2 days.

Writes K+0x58..+0x6c (ints):

1. s = C's province count (C+0x1386). If 200·s > supportLimit: s = supportLimit / 200.
2. **wantInf** K+0x58 = 200·(5s + 50) men = 1000·s + 10000.
3. **wantCav** K+0x5c = 100·(5s + 50) if cavalry allowed, else 0.
4. **wantArt** K+0x60 = s/2 + 5 pieces if artillery allowed, else 0.
5. If cavCost/2 < infCost (cavalry costs less than twice infantry): wantCav += wantCav/2,
   wantInf = wantInf/2.
6. **wantTransports** K+0x6c = 2 × (number of admirals, K's list +0x766) × (2 if `0x43a970`, else 1).
   K+0x64, K+0x68 = 0.
7. Naval base value V = wantTransports + round(ownedCount(C+0x131a) × C+0x36c (coast share)) +
   2 × shipyards (C+0xfd2).
8. Rival list = every country i with C+0x138a[i] (neighbour). Extra rivals by C's tag index
   (switch on C+0x6): index 1 adds 8, 2, 4; index 2 adds 8, 1, 4; index 4 adds 8, 2, 1; index 8 adds 1,
   2, 4 (four mutually watching naval powers; NIEPEWNE which tags).
9. M = max over the rival list of (galleys/4 + warships). If V < M: V = round(M). V = min(V, ownedCount).
10. Galley navies: if C+0x6 - 10 is 0, 1, 12 or 16 (tag indices 10, 11, 22, 26; NIEPEWNE which tags):
    **wantGalleys** K+0x68 = 2V and **wantWarships** K+0x64 = V/2; every other country: wantWarships = V.
11. If 0.2 × last month's income (C+0xe80) < monthly cost of the current transports + galleys + warships:
    all three naval wants = 0 (the navy is not affordable).

#### 2. King-level triggers

#### 2.1 Army (`0x542b80`)
- Only **while C is at war** (`0x43c000`) and no King BuildArmy plan is alive (K+0x77c == 0).
- d = max(0, wantInf - inf) + max(0, wantCav - cav) + max(0, wantArt - art) (raw sum of men and
  pieces); d = 0 -> nothing.
- Refuse if inf + cav + 100·art > 2 × supportLimit.
- Create BuildArmy (§3, ctor `0x547ff0(K, 1000, 1000, 10)`, vtable 0x5f1c50) into K+0x77c, parent K,
  setup vf 0x20c(P = d, 0, 0, 0, w1 = 0, w2 = 0, capital province). Priority +0x44 = **65**, or
  **350** if C has no army at all. K.addPlan (vf 0xf8), start (vf 0xc0).

#### 2.2 Fleet (`0x542d30`)
- Not war-gated. dW = clamp(wantWarships - warships, 0, 10), dG = clamp(wantGalleys - galleys, 0, 10),
  dT = clamp(wantTransports - transports, 0, 10).
- If the King BuildFleet K+0x778 exists: re-target it (`0x54fd10(dW, dG, dT)`).
- Else if any > 0: create BuildFleet (ctor `0x547f80`, vtable 0x5f1e60, unit sizes all 1) with
  vf 0x20c(dW, dG, dT, capital province); priority **10**.

#### 2.3 Reinforcement build for a requesting plan (`0x547040`, end of `0x546c40`)
The King's army request (queue 5, created by `0x545e00(plan, power P, w1, w2, province, kind)`, request
object vtable 0x5f3400: +0xc power still needed, +0x10 kind, +0x14/+0x18/+0x1c inf/cav/art already
assigned, +0x20 target province, +0x24 "build already asked", +0x26/+0x2a floats w1/w2) is first served
from existing armies (`0x546c40`: free armies sorted by squared map distance to the target, +250000 if
the army's continent differs but is connected, +1e6 if not connected, +10000 if it already serves a
plan; armies are taken until the need < 0.5). If the need is still open and:
- inf + cav + 100·art <= 2 × supportLimit,
- the request is not yet satisfied (vf 0), +0x24 is clear, and kind is not 2 or 3 (transport kinds),

then +0x24 = 1 and a BuildArmy is created whose parent is the requesting plan (so its priority in the
King's queues is the requester's root priority, NIEPEWNE), setup vf 0x20c(remaining power,
assigned inf, cav, art, w1, w2, target province). The flag is cleared again when the plan starts a
unit (`0x54fbd0`).

#### 3. BuildArmy / BuildFleet plan (types 7 / 8)

Fields: +0x70 target province; +0x74 ducats held; +0x78 ducats requested; +0x7c/+0x80/+0x84 still
to build (army: inf men, cav men, art pieces; fleet: warships, galleys, transports);
+0x88/+0x8c/+0x90 this step's order; +0x94/+0x98/+0x9c unit size (army 1000/1000/10, fleet 1/1/1);
+0xa0/+0xa4/+0xa8 power per unit; +0xac provinces in use; +0xb0 provinces held; +0xbe units under
construction (ids); +0xd0 offered provinces queue.

#### 3.1 Setup, army (`0x54f850`, vf 0x20c; args P, aInf, aCav, aArt, w1, w2, province)
Base score 1.0 (`0x54b4f0`), +0x70 = province. Power per man/piece:
pInf = IF·w1 + IS; pCav = CS·w2 + CF·w1; pArt = AF·w1·w2 + AS (with w1 = w2 = 0: IS, CS, AS).
- **Cavalry** (if allowed): r = infCost/(infCost+cavCost) (1 if the sum < 1); f = CS/(CS+IS)·r
  (r if CS+IS < 1); cav = round(f·P) - aCav, rounded **to the nearest 1000** (<= 0 -> 0).
- **Artillery** (if allowed): art = round(0.1·P / pArt) (if pArt > 0); if the target province is an
  enemy fortress (`0x481870`): art += 10 × fortress level; art -= aArt; rounded to the nearest 10.
- No cavalry if cavCost > 2·infCost **and** C's cavalry > infantry/4.
- No artillery if C's artillery pieces > infantry/1000 (one piece per 1000 foot).
- **Infantry** = round((P - cav·pCav - art·pArt) / pInf), rounded to the nearest 1000.
- All three 0 -> score -1 (the plan dies).

#### 3.2 Setup, fleet (`0x54fd10`)
pWar = warship fire + shock, pGal = galley fire + shock, pTr = 0. Still-to-build = the wanted counts
minus the ships already under construction (list +0xc2), floored at 0. All 0 -> score -1 and the
plan executes (finishes) at once.

#### 3.3 Getting provinces (the King's queues 7 and 8)
vf 0x200 (`0x5480d0`) asks K (vf 0x1e4 = `0x545fd0`, queue 7; fleets queue 8) for a province near
+0x70; vf 0x204 (`0x5480f0` -> `0x558dd0`) hands one back. The allocator picks, among C's provinces
not reserved by another agent (prov+0x49c) and buildable:
- **army** (`0x547660`): may recruit here (`0x45e540(p,0,0,0)`); score = squared map distance to the
  target, +250000 if an enemy army stands there, +40000 if on another but land-connected continent,
  +4e6 if not connected; **lowest** wins.
- **fleet** (`0x547840`): may build ships (`0x45ea10`), no enemy army there; score = squared distance,
  -9e6 with a shipyard (`0x47caf0`), +9e6 if prov+0x489 (NIEPEWNE flag, also halves manpower),
  +1e6 if on another continent; lowest wins.

#### 3.4 A province is offered (`0x54ed40`)
Skip if already used. n = round(still/size) per type. cap = province capacity (`0x4851b0` army,
`0x485320` fleet); cut the order to cap in the order infantry, cavalry, artillery; for an **army**
leftover capacity is filled with extra **infantry**. Army only: if inf + cav + 100·art (men) + the
new *regiment counts* (nInf + nCav + 100·nArt; literal unit mix-up) > 3 × supportLimit, the order is
zeroed and the plan cancels.
- Order empty: log "BuildPlan #%d of %s can't build anything  in %s, and gives it back to the King."
  (0x632ad8), give the province back; if nothing is left to build and no unit is under construction
  -> execute (finish), else ask for another province (vf 0x200).
- Else: order in men/ships, cost = vf 0x1f0, +0x78 += cost, **money request** K vf 0x1c8(plan, cost);
  log "BuildPlan ... wants to build a Unit (%d, %d, %d) in %s for Plan #%d (%s), and requests %0.2f
  ducats." (0x632a6c).

#### 3.5 Spending (`0x54f0a0`, vf 0x1ec, rerun whenever money arrives)
1. Nothing left (each still-to-build < one unit) -> execute.
2. Take the first offered province. If held + 0.01 < requested and one infantry regiment (one ship)
   costs more than held: wait.
3. Cut the order to the remaining capacity (drop infantry, then cavalry, then artillery, one unit at a
   time). If the order costs more than requested + 0.01: request the difference (K vf 0x1c8) and wait.
4. While the order costs more than held: drop artillery, then cavalry, then infantry, one unit at a
   time.
5. Leftover = held - cost; if > 1 and capacity remains: add min(capacity, round(leftover / cost of one
   infantry unit)) infantry units.
6. If can-build (vf 0x1f8 = `0x45e540` / `0x45ea10`): return held - cost to K (`0x558cf0`), post the
   ordinary recruit command (`0x42e230` + `0x4435c0` army / `0x4436c0` fleet, i.e. same path as the
   human), log "BuildPlan #%d of %s starts building Unit #%d (%d, %d, %d) in %s." (0x632b68), remember
   the unit id. Subtract from still-to-build; an artillery surplus is converted into less cavalry by
   pArt/pCav, a cavalry surplus into less infantry by pCav/pInf.
   Else: log "avoids building" (0x632b28), give the province and all money back.
7. Nothing left and no unit under construction -> execute; otherwise ask for the next province.

#### 3.6 Completion / cancellation
- Unit finished (`0x54f620`): log "BuildPlan #%d of %s builds %s #%d in %s." (Army/Fleet), the province
  goes back to K, the unit is handed to the parent plan (`0x5586a0`), or to K if the parent refuses.
  Finish when all done.
- Construction cancelled (`0x54f740`): log "cancelled construction of Unit #%d in %s." (0x632be8),
  the unit's amounts are added back to still-to-build.
- Plan deleted (`0x54ec80`): all held provinces returned, every unit still under construction is
  cancelled (`0x48eb20`). Start (`0x54ec30`): log "BuildPlan #%d of %s plans to build units (%d, %d,
  %d) for %s." (0x632a2c).

#### 4. How requests are paid (the King's queues)

K keeps 9 request queues at K+0x70 + i·0x12 (i = 0 money, 1..4 other resources, 5 armies, 6 fleets,
7 land-build provinces, 8 sea-build provinces). Each queue is sorted by the requester's **root plan
priority** (+0x40 -> +0x44), highest first (`0x545b30`); adding an entry posts event 0xe0 with the
queue index (`0x545790`, at most one pending per queue, K+0x780[i]), and the event runs `0x5458a0(i)`.

- Money request `0x545bc0(plan, amount)`: one request object per plan (plan+0x4c, `0x5465a0`), its
  amount grows by each call. Served in priority order. A request may also take the resource from a
  current holder whose root priority is lower by more than 0.1 (holder list +0x116; NIEPEWNE exact
  partial-payment rules).
- K's free money K+0x1b6 is kept so that K+0x1b6 + money held by all agents (list +0x11a) <= treasury
  C+0xe70 (`0x546090`); giving money back (vf 0x144 = `0x546150`) adds to K+0x1b6 and re-runs queue 0.
  So recruitment is paid straight from the treasury, competing by priority with every other plan
  (BuildArmy 65/350 at war > buildings 11-35 > BuildFleet 10; diplomatic plans 250).
- Manpower is not reserved by the King for recruitment (K+0x1be is capped by C+0xec0 but its users
  are NIEZNANE); province capacity (§3.4) is the only manpower brake.
- The budget log "%24s budget l %5.2f n %5.2f s%5.2f pe %5.2f te %5.2f $ %5.2f" (`0x545605`, end of
  `0x544b60`) is the **investment-slider** split (land tech, naval tech, stability, production, trade,
  treasury), applied through `0x443e90` when it moves by > 0.02. It does not earmark money for units.

#### 5. Strategic mode, war, .ai prefs

- No function in this part reads the .ai prefs (country+0x25c): recruitment ignores war/ferocity/
  conquer weights.
- The strategic mode (K+0x40) does not change unit wants. It only changes building priorities (§6).
- War: the King's own BuildArmy exists only while at war; in peace armies are built only through
  reinforcement requests (§2.3). Fleets are topped up in peace and war.

#### 6. Military buildings (agent `0x53ae70`, plan scores `0x54e4b0` / `0x54e740` / `0x54e9a0`)

The building agent acts only when it has no building plan, and **never while at war**. Order: bailiff
if possible; else with 50 %: (city rights if inflation > 0.2), else fortress; then city rights if
inflation > 0.4; then r = rand%100: < 50 bailiff, < 70 courthouse, < 80 city rights, < 90 fortress,
< 95 shipyard, else conscription centre; then fallbacks fortress, courthouse, city rights, bailiff.
Nothing created -> next try in 100 + rand%100 days. A plan is kept only if its score >= -0.1.

| Building | Province choice | Score |
|---|---|---|
| Fortress (vt 0x5ee978, start `0x54e510` "%s wants to build fortress in %s.", cost `0x43b290`) | over every general's region province list: buildable (`0x45e0b0`); value `0x47d970(p)` + 5 if `0x482960(p)` + 5 if `0x4818b0(p)` (NIEPEWNE: coastal / border) + 10 with a shipyard or `0x47cb10` + 2·(6 - fortress level); max | 10 + (25 in BUILDUP mode, else 1) |
| Conscription centre (vt 0x5eed58, "%s wants to build conscription center in %s.") | needs tech (`0x432df0`); over generals: their base province g+0x44 if buildable (`0x45fe90(p,1)`); value = sizes of the two region lists (+0x16, +0x4) + 10 if `0x472ebb(region)` (NIEPEWNE: region at war) - 5 if `0x45f260(p)` < 10; max | same |
| Shipyard (vt 0x5eeb68, "%s wants to build shipyard in %s.") | needs tech (`0x432e60`); over admirals (K+0x76a): base province if `0x45fe90(p,0)`; value = region list sizes + 10 if the sea region touches the capital's continent - 5 if `0x45f260(p)` < 10; max | same |

#### 7. Port summary

1. Each King tick (20-39 days, /4 at war, min 2; §1): compute wantInf = 1000s + 10000, wantCav =
   500s + 5000 (if allowed), wantArt = s/2 + 5 (if tech >= 7), s capped at support/200; cheap
   cavalry swaps half the infantry into +50 % cavalry.
2. Naval wants: transports 2 per admiral (x2 for colonial powers); warships = max(coast-scaled
   provinces + 2·shipyards + transports, strongest neighbour/rival navy), capped at owned provinces;
   4 galley nations want 2V galleys + V/2 warships; zero all if navy upkeep > 20 % of income.
3. At war and no King army plan: BuildArmy for the summed deficit (priority 65, 350 with no army),
   unless troops > 2 × support. Always: BuildFleet for min(10, deficit) per ship type (priority 10).
4. Plans fighting short of power ask for a BuildArmy at the target (once per request).
5. Army mix from power P: cavalry share CS/(CS+IS) × infCost/(infCost+cavCost); artillery 0.1·P/pArt
   + 10 per enemy fort level; caps 1 cavalry per 4 infantry when cavalry is expensive, 1 gun per 1000
   foot; rest infantry; round to 1000 / 10.
6. Build in the own recruitable province nearest the target (fleet: prefer shipyards), regiment
   capacity = province manpower; pay from the treasury in priority order; one province at a time.
7. Buildings only in peace; fortress/barracks/shipyard scores 35 in BUILDUP mode, 11 otherwise.

---

## Military AI, part C: the land war (generals, conquer / land-attack plans, gathering, sieges)

eu2.exe v1.09, read as a specification only. Markers: **NIEPEWNE** = believed but not proven,
**NIEZNANE** = not found. U = the AI country, K = its King (controller, vtable 0x5f17f8),
G = a General agent, P = a province, prefs = U+0x25c (the .ai file).
rand = 0x5ba99c, "a%b" = C remainder. Province pointer = 0x482da0(id) (stride 0x4a0).

#### 0. Helpers used everywhere (meaning established from code)

| Function | Meaning |
|---|---|
| prov+0x3c4 / +0x3c8 | owner / controller country pointer |
| prov+0x498 | the province's **area** (see §1) |
| prov+0x114 / +0x11c | neighbour count / neighbour array (12-byte entries, first dword = province id) |
| prov+0xca | terrain class; **5 = sea** (land tests are "< 5"); 1, 2, 4 are land terrains that raise the force needed (§4.4) |
| prov+0xae | list of units in the province; prov+0x408 = settlement, its +0x2e = siege object |
| 0x43b6e0(C, idx) | "hostile": idx or C is a rebel/pirate tag (0x151..0x153) or at war |
| 0x43a530(C, idx) | "friendly": same country, military access (rel. byte +0x3d) or allied (0x4cbe90) |
| 0x45f730(U, P) | **U may enter P**: no owner, own, controlled by U, owner hostile, access, allied (game options 0x4d16a0(0)/(4) and tags 5/8 add special cases) |
| 0x483940(P, U) | **"in supply"**: P (or any land neighbour of P) is controlled by U or a friend of U (an ongoing siege counts for the besieger); true for prov+0x10a == 0x94 |
| 0x484260(P, U) | P has a neighbour that U both owns and controls (P borders our land) |
| 0x481630(P, id) | province id is a neighbour of P |
| 0x481820(P, idx) | an army hostile to idx stands in P (and is not marked +0x53) |
| 0x482dc0(P) | P is under siege; 0x482de0(P) = besieging country |
| 0x482920(P) | P has a city; 0x482940(P, U) = core of U; 0x47f3e0(P) = fortress level; 0x47d970(P) = income value; 0x483570(P, U, 0) = supply limit |
| 0x43a820(a, b) | distance between provinces a and b (ids) |
| 0x438690(C) | land morale of C (rebels: 0.6 + 0.01*(year-1400)) - also the strength multiplier vfunc 0x204 of the attack plans (0x54ce90) |
| 0x48f070(army) | army strength (float); unit vfunc 0x84 = unit strength; 0x48eec0 = men |
| 0x48efe0(list, idx, &f, &s) | sums vfunc 0x7c (f) and 0x80 (s) of every unit in the list hostile to idx; returns the hostile unit with the best leader (+0x22 rank). **NIEPEWNE**: f, s = fire / shock power |
| 0x559c80(P) / 0x559ca0(P) | 0x48efe0 over P / over every neighbour of P of the same land-sea class |
| 0x55a4a0(a, b) | random delay: with k = option [[0x8c0bfc]+0x2c6] (**NIEZNANE** which option; k = 0 changes nothing): a' = max(1, a - a*k/10), b' = max(1, b - b*k/10); returns a' + rand % b' |
| 0x55af00(days, p) | post wake-up event 0xd8 to the agent after `days` days with parameter p (p = 1 normal think, 0 = "commit candidates") |
| 0x55ae60(id, on) | subscribe / unsubscribe the agent to global event id (table 0x8c07d0 + id*18) |

#### 1. Areas and who owns a General

Every province has an area pointer (prov+0x498); a country keeps its areas in U+0x12e8 (built in
0x4724xx..0x473xxx). **NIEPEWNE** reading (consistent with every use below): an area is a
connected set of provinces of **one owner**; area+0x04 and area+0x08 are its province lists
(+0x04 is a counted list object: vfunc 4 = count, 8 = empty); area+0x16/+0x1a is the list of links
to adjacent areas (link->+0x28 = 0 land border, 2 sea connection; +0x2c = visited flag used by the
breadth-first walks, cleared by 0x472e41). 0x472b00(area) = owner of its first province.
0x472ebb(area) = the area contains its owner's capital (capital prov = 0x45b7d0(C) = C+0x1f7e).

**King init (0x541c50):** for each area in U+0x12e8 without a General (lookup 0x542f30 by
G+0x48 == area in K+0x758 list), create a General (0x5482a0, vtable 0x5f2260, 0xc6 bytes),
insert it at the head of K+0x754 if the area holds our capital, else at the tail, and run
G.init(area) (vfunc 0x208 = 0x549500). Every sea link of the area also gets an Admiral
(0x543110; part D). K then re-schedules itself 20+rand%20 days (halved if U+0x11e, quartered
at war, min 2) - see part A for the King.

**New area / war (King vfunc 0x84 = 0x544160, args C1, C2, one of them U):** if U has a coast
(U+0x36c > 0), for each area of the *enemy* (U+0x12e8 of the other one) that has a sea link and
in which no linked area is ours (0x472b00 != U), and that has no General yet: create an
**overseas General** (vtable 0x5f2680, same layout) for that enemy area and init it (0x54a2c0 =
0x549500 + an immediate invasion check 0x54a220). It deletes itself (vfunc 0xc8/0xc4 and removal
from K+0x754) when the war with that area's owner ends (vfunc 0x80 = 0x54a2e0) or the area's
province list becomes empty (vfunc 0x64 = 0x54a380). Areas reached by land are handled by the
home Generals' conquer candidates instead (§3).

#### 2. The General (0x5f2260; base ctor 0x548700, layout)

| Field | Meaning |
|---|---|
| +0x0a | country U; +0x0e "active" byte |
| +0x16/+0x1a | list of active child plans (each entry: vfunc 0xe4 active, 0xe8 plan) |
| +0x40 | **maximum number of active plans** |
| +0x44 | "home fortress" province of the area (0x54a140) |
| +0x48 | the area |
| +0x4c..+0x64 | 7 **template plans** indexed by slot: 0 LANDATTACK (+0x4c), 1 CONQUER (+0x50), 2 DEFEND (+0x54), 3 INVASION (+0x58) |
| +0x68..+0xa8 | candidate array (17 slots, at most 16 used), sorted by score, count +0xac |
| +0xb0 | "commit already scheduled" byte |

#### 2.1 Init (0x549500, also logs `"%s starts a CAIMilitaryMinister for %s with %d plans."`)
Builds the 4 templates (LANDATTACK 0x5f422c with +0x86 = +0x8a = 1.0; CONQUER 0x5f4004;
DEFEND 0x5511e0; INVASION 0x5f3ddc), sets +0x48, computes the plan limit (0x548800):

  limit = 4 + count(area+0x04)/3 + count(area links +0x16); if limit > 0.25 * U+0xec4
  (yearly manpower) then limit = trunc(0.25 * manpower); clamp 4..16.

Subscribes to global events 0x12, 0x00, 0x15, 0x1b, 0x1a and (0x54ad60) 0x13, 0x16 (**NIEZNANE**
which game events these ids are). First think after 0x55a4a0(5, 10) days. Creates one standing
DEFEND plan on the area (vfunc 0x1e8(first province, area), started at once; part D), then
collects LANDATTACK candidates over the area's provinces (0x549050(slot 0, area+4)).

#### 2.2 Think (0x549850, slot 0x1c), timing
- p = 1 (normal): invasion check 0x54a220 (below); if no active plan other than DEFEND exists,
  collect LANDATTACK candidates over the area (0x549050(0, area+4)); vfunc 0x180; re-schedule:
  **home area (contains the capital): 0x55a4a0(3, 10) days; other areas 0x55a4a0(7, 14)**.
- p = 0 (posted one day after the first candidate is inserted, 0x548d60): commit (0x548df0).
- Invasion check 0x54a220: n = count(area+0x04); c = provinces of the area that are not
  controlled by U, or are controlled by U and under siege; if c / n > 0.25 collect INVASION
  candidates (slot 3) over the area (part D decodes the invasion plan). For an overseas General
  c = n, so it always collects them.

#### 2.3 Candidate collection
0x549050(slot, provinceList, exclude): for every province P of the list (prov+0x7e = visited
flag, cleared afterwards):
1. skip P if an active non-DEFEND plan or a candidate already targets it (0x549280);
   for slot 3 skip it if an active INVASION (type 0x10) targets it;
2. template[slot].score(P, area) (vfunc 0x1e8) -> +0x44; skip if score <= 0;
3. if `exclude` is given and P is a neighbour of `exclude`: score += 5;
4. insert into the sorted candidate array (0x548d60; full array of 16 -> the new one is only
   inserted if it beats the last; the dropped 17th is deleted); on insert the template is cloned
   (vfunc 0x1e4) so the next province gets a fresh template, and a commit is scheduled (1 day).

0x5491f0(slot, P) does the same for one province. 0x548f50(unitList, slot) does it for the
locations (unit+0xc) of a list of units, restricted to provinces of G's area.

#### 2.4 Commit (0x548df0)
- worst = the active non-DEFEND plan with the **lowest** score (its root's +0x44).
- For each candidate in score order: if active count < limit: add it as child (vfunc 0xf8) and
  start it (vfunc 0xc0). Else, if candidate score > worst's score: add and start it too.
- Delete the remaining candidates, clear the array. If active count > limit now, cancel `worst`
  (only that one; the list can stay one over the limit at most once per commit).

#### 2.5 Event handlers of the General (what triggers new plans)
| vfunc | args | action |
|---|---|---|
| 0x84 (0x548d20) | war C1, C2 involving U (and limit set) | DEFEND children released (0x5514d0, part D); **0x549bc0**: for every country hostile to U, LANDATTACK candidates on the provinces of our area where its units stand (0x548f50(country+0x1248, 0)); **0x549b80**: CONQUER candidates (§3.2) |
| 0x80 (0x548cf0) | peace C1, C2 involving U | if U is no longer at war: DEFEND re-plans its armies (0x551500, part D) |
| 0x60 (0x54a0c0) | P | P in our area: CONQUER candidate (slot 1), else INVASION (slot 3); commit |
| 0x68 / 0x6c (0x54a030 / 0x54a070) | P in our area | CONQUER candidate; if not created, LANDATTACK and INVASION candidates (0x6c also commits) |
| 0x64 (0x5494d0) | P | P == home fortress or owned by U: recompute the home fortress (0x54a140) |
| 0x1ec / 0x1e4 (0x549b40 / 0x549b60) | land P | CONQUER / LANDATTACK candidate on P |
| 0x1fc, 0x200 (0x549fa0) | (type, P), P owned | type 2: CONQUER candidate on P; type 0xb: if P's controller is hostile, CONQUER candidates over P's area with P as `exclude` (neighbours of P +5); type 0x10: invasion check; otherwise (or nothing created) redo 0x549bc0 + 0x549b80 |
| 0x198 (0x54a110) | a unit given to G | §5.1; if no plan takes it, vfunc 0x17c (hand back) |

Home fortress (0x54a140, stored at G+0x44): among the area's provinces controlled by U, maximise
income + 2*fort - 4 if coastal (prov+0x487) - 10 if 0x4818b0(P) + 10 if 0x47cb10(P)
(**NIEPEWNE**: 0x47cb10 = conscription centre, SPEC_UI_WINDOWS4).

General evaluation of a province (vfunc 0x1dc = 0x5499f0, used by the King to pick a General,
part A, **NIEPEWNE**): 0.6*min(1, 0.0001*a + 0.00005*b) (a, b from 0x481790(P, U)) +
0.4*d, where d = 1 if U owns P, 0.9 if P is in supply, else max(0, 1 - 0.0025 * min distance
(map coordinates prov+0x3e4/+0x3e8, start 400) from P to the area's provinces).

#### 3. CONQUER plan (vtable 0x5f4004, type 0x0b, 0xa2 bytes)

#### 3.1 Target score (vfunc 0x1e8 = 0x550830), logs
`"%s considers conquering %s, owned by %s and controlled by %s"` and
`"%s considered conquering %s, owned by %s and controlled by %s. Priority %.3f."`

Target P is rejected (score -1) unless: P has an owner and a controller, the controller is not U
and is **hostile** to U (at war, or rebels), and (U owns P **or** P is in supply for U, 0x483940).
(The leading "+0x44 < -0.1" test is dead: 0x54b4f0 has just set +0x44 = 1.)

With w = prefs conquer block (enemy +0x5c, supply +0x60, distance +0x64, owner +0x68,
notsupply +0x6c, base +0x70):

1. s = w.enemy * V(P), V = 0x559990 (§3.3).
2. If P is in supply for U:
   - s += 30 * w.supply;
   - if P's area exists, P's owner is hostile to U and that area holds the owner's capital:
     d = 10 if P **is** the capital, else (dist to the capital, map coordinates 0x475790) < 10 ?
     9 : 9 / dist; s += w.distance * d;
   - else if U owns P (our occupied province): s += 3 * w.owner.
3. Not in supply (only possible when U owns P): s = s * w.notsupply * 0.25.
4. Defenders: (f, s2) = hostile units in P (0x48efe0); if any: s -= (f + s2) *
   morale(their country) / (morale(U) * 10000).
5. If s <= 0.1: s = 1.1. **Priority = s + 15 * w.base** (stored in +0x44).

So `enemy` scales the province value, `supply` is a flat bonus for adjacent targets,
`distance` pulls towards the enemy capital, `owner` favours liberating own provinces, `notsupply`
discounts own provinces deep behind enemy lines, `base` is a flat bonus that lets conquer plans
outrank defensive ones in the commit (§2.4, LANDATTACK scores are about 20 + 2V).

#### 3.2 Where conquer candidates come from (0x549b80 / 0x549c20)
1. Our area's provinces (0x549050(1, area+4)).
2. Breadth-first over the area links (land links only, +0x28 == 0): each adjacent area whose
   owner is hostile to U gets CONQUER candidates over its provinces; if its owner is hostile, or
   we have military access (0x45fa70), or it is our own, the walk continues through it (a hostile
   area with its owner's capital is searched immediately); the walk stops as soon as one area
   produced candidates. So armies march to the nearest hostile land area, preferring the one with
   the enemy capital.

#### 3.3 Province value V (0x559990, conquer)
v = income/12 + (6 - fort) + 5 if city + 10 if core of U;
if P is a neighbour of our capital: v = 2*(v + 25); if P borders land we own and control:
v = 2*(v + 25) (both can apply); then v *= 0.85 for each of supply < 5, < 10, < 20, < 30
(cumulative); finally v += 10 * prov+0x140 (the colonisation attractiveness value of
SPEC_TIME_ECONOMY; literal, **NIEPEWNE** intent).

#### 3.4 Force needed (vfunc 0x1f8 = 0x550b20 -> +0x76)
- (a, b) = hostile f/s in P + 0.5 * hostile f/s in P's neighbours.
- If no hostile army: (lf, ls) = fire/shock of the defending leader set built from the controller
  (0x487fc0 object, 0x488160 fire / 0x4881b0 shock); else from the best enemy leader.
- Fort floor with F = fort level: A = (7500F + 5000)*g1(U) + 30F*g3(U) + 1000(F+2)*g2(U),
  B = (7500F + 5000)*g4(U) + 30F*g6(U) + 1000(F+2)*g5(U), both times morale(U)^2 (vfunc 0x204
  twice). g1..g6 = 0x433400, 0x433450, 0x4334a0, 0x4334f0, 0x433540, 0x433590 (**NIEPEWNE**:
  country unit-type values, three for fire, three for shock). a = max(a, A), b = max(b, B).
- Our leader (lf', ls') from U; mountain bonus m = 1 if prov+0xca == 2.
  need = ((ls + m - lf' + 10)*a*0.1 + (ls'' + m - ls' + 10)*b*0.1) * morale(controller) /
  morale(U) (the exact pairing of the four leader values is **NIEPEWNE**; the structure
  "(defender leader - our leader + 10) * 0.1 per component" is certain).
- need = max(need, 0x43d340(U, P)) (**NIEZNANE** meaning; a minimum force for P).

#### 3.5 Conquer plan think (vfunc 0x1c = 0x550f20)
1. Own strength (vfunc 0x1f0 = 0x54bcd0): +0x7a = strength of the main army + every army of
   the sub-agents; phase update 0x54c450 (§4.2).
2. Done (vfunc 0x208 = 0x551090: P has no controller, or the controller is no longer hostile, or
   U may no longer enter P) -> finish (vfunc 0xc4), release armies (vfunc 0x108).
3. Not engaged (+0x74 == 0): recompute need, own strength, reinforcement request (0x54d3a0,
   §4.5); if ready (vfunc 0x200 = 0x551040) engage (0x54c140); if assault test (0x54cfc0) -> assault
   (0x54d2c0); next think **0x55a4a0(3, 5) days**.
4. Engaged: same updates; assault if 0x54cfc0, else engage again if ready; next think
   **2 + rand%5 days** (not scaled).

Ready (0x551040): false if our men (0x48eec0) < 5000 * fort level **and** own < need; otherwise
the common test 0x54c0a0 (§4.3).

Accepting an army offered to the plan (vfunc 0x198 = 0x5510e0): refuse (vfunc 0x17c and a new
reinforcement request) when own > 0.01, own + unit > 1.5 * need and own > need (enough already);
accept when the army can reach the destination (path test 0x559eb0 to the gather point or the
target) and 0x48ead0(army) <= 10000 (**NIEPEWNE**: travel cost); else refuse.

#### 4. LANDATTACK plan (vtable 0x5f422c, type 2) and the shared attack machinery

#### 4.1 Target score (0x54cbf0): attack enemies inside our own area
Reject (score -1) unless P has owner and controller, is land, belongs to G's area, and the plan
is not already "done" for P (0x54c200: done unless P is ours and occupied, or a hostile army
stands in P and U may enter P). v = V2(P) (0x559ae0, below);
if P is owned by U and (occupied, or controlled by U but under siege): v = 4v + 100;
v *= 2; if the controller is the **rebel tag 0x153**: v *= 2; else if P is besieged by rebels:
v *= 3; score = max(v, 0) + 20.

V2 (0x559ae0): v = income/12 + (6 - fort); x2 if city; x2 if U owns P, else x1.5 if U controls
it, else x2.5 if it borders our land; x5 if core; supply < 5: x0.25, < 10: x1/3, < 20: x0.5,
< 30: x0.8 (cumulative); x1.1 if P is in our capital's area; finally x0.5 (x1.25 instead when P
is controlled by U and under siege).

**This is how the AI reacts to enemy armies and rebels in its territory**: the war event
(0x549bc0) and the province events make LANDATTACK candidates on every province of the area
holding a hostile army or occupied by an enemy; rebel-held or rebel-besieged provinces get x2/x3.
Rebels need no war: 0x43b6e0 treats tags 0x151..0x153 as always hostile.

Force needed (0x54bdb0): (a, b) = hostile f/s in P, (c, d) in the neighbours. If P has a fort
and a foreign controller: A = (15000F + 2000)*g1 + 1000(F+1)*g2 + 5F*g3, B likewise with
g4..g6, both x morale(U)^2; a = max(a, A), b = max(b, B); with no fort, b = max(b, 500).
need = a + b + 0.25(c + d); x morale(enemy)/morale(U) if an enemy army is there; terrain
prov+0xca 1: x1.1, 2: x2, 4: x1.5; then x1.5 always; on land need = max(need, 0x43d340(U, P)).
Enemy strength for the ready test (0x54bd50): (f + s) in P x morale(enemy)/morale(U).

Think (0x54cd30): cancel when U may no longer enter P (0x45f730 false; event 0x60 does the same,
0x54d370). Otherwise strength + phase update; if engaged: assault test -> `"AICombat #%d (%s)
assaults the fortress!"` (0x4707f0 on the siege object); next think **2 + rand%5** days;
not engaged: **3 + rand%7** days; if ready -> engage; then the common tail 0x54c920 (retreat
check vfunc 0x108, done -> finish).
Re-target (vfunc 0x20c = 0x54d470): a land province U may enter, with 40 % chance.

#### 4.2 Phases: gathering and marching (0x54c450; not for types 3 and 14..16, which go to phase 1)
Fields: +0x70 target, +0x7e gather province, +0x82 phase (0 = gather, 1 = attack), +0x74
engaged, +0x76 need, +0x7a own strength.
- No gather province -> phase 1.
- Phase 1 and own < 0.25 * need -> **fall back**: choose a new gather point, phase 0.
- Phase 0 and own >= 0.75 * need: phase 1 if every army of the plan stands in the target or the
  gather province, or if the strength of those that do is > 0.75 * need.
- Gather province controlled by U and under siege -> choose a new gather point.
- Phase 1, when the target holds hostile troops (or is our besieged province): sum our strength
  already in the target; if any of our armies on the way would arrive with (already there + its
  own) < enemy strength, go back to phase 0 and clear "engaged" (**retreat to the gather point**).

Gather point (vfunc 0x1fc = 0x54c6a0, log `"%s will attack %s (gathering in %s)"`):
- start point S = our capital; if an army is passed or the plan has armies: S = location of the
  army minimising (10000 - 0.1*strength) + dist(target, location) (9000 for the passed one).
- candidates: every province Q controlled by U; cost c = 10000, 5000 if Q's area links to the
  target's area, 4500 if the same area, 4000 if Q neighbours the target; +2500 if Q is under siege
  else -250; +10000 if controlled by U and besieged; c += 10 * prefs.base(+0x28) * dist(S, Q)
  + 5 * prefs.front(+0x2c) * dist(target, Q) - supply(Q); the lowest c wins (start bound 10000).
  So `base` pulls the rally point towards the main army / capital, `front` towards the target.

#### 4.3 Ready to attack (0x54c0a0)
Phase 1: false if any land unit of the main army has morale (+0x58) < 0.99 * max morale; then
ready iff own main-army strength (0x54bd40) > enemy strength in the target (vfunc 0x1ec). Phase 0:
always ready (marches to the gather point).

Engage (0x54c140, log `"%s #%d (%s) engages in battle!"`): +0x74 = 1; destination = the
target, or the gather point while in phase 0; every idle army of the plan (no path, not moving,
not already there) that has a path (vfunc 0xcc result 2 or 3) is ordered there (0x5586a0).

#### 4.4 Retreat from a battle (vfunc 0x108 = 0x54c350)
For each army of the plan that is in a battle (+0x48/+0x4c): compare our side with the other
(side objects +0x30/+0x34 chosen by 0x48bd10): if our vfunc 0x18 (**NIEPEWNE** strength) <
0.66 x theirs **and** our vfunc 0x1c (**NIEPEWNE** morale) < 0.66 x theirs **and** our +0x20 <
their +0x20: order the retreat (side vfunc 0x60 to the army's province). Returns true if one
retreated (the caller then skips the rest of the tick).

#### 4.5 Sieges and assaults (0x54cfc0 test, 0x54d2c0 / 0x54cd5c assault)
Assault only when engaged, the target is under siege by U, the siege is not already in state 5
(+0x24) for U, 0x45dec0 holds (**NIEZNANE**, a siege-side validity test), and:
1. our men / 10000 >= fort level, **or** the siege has progressed (0x471590 != 0 and vfunc 0x84
   >= 7);
2. our assault value (leader fire x f + shock x s) * 0.1 * morale(U) > the same value of the
   defenders in the province;
3. and either the garrison morale (+0x34 side, +0x20) <= 0.25, or the owner has only one
   province, or no neighbouring province holds a hostile army stronger than 0.1 x our value.
Otherwise the AI just keeps sieging.

Reinforcement / recruitment request (vfunc 0x21c = 0x54d3a0): deficit = need - own; if a
request object exists (+0x60): update it with the deficit if > 1, else drop it; if none and
deficit > 1: ask the King (root agent vfunc 0x1dc) for `deficit` strength for the target
(mix weights +0x86/+0x8a = 1.0, 0x54cf40; the build side is part A/B). If deficit > 1 and U has
no army outside this plan (units U+0x1248 minus the plan's armies < 1): vfunc 0x180 (escalate).

Split (vfunc 0xd0 = 0x54b560, log `"Agent #%d (%s) of %s splits Army #%d (%d,%d,%d) and leaves
(%d,%d,%d)."`): when the plan's request reports a surplus, the surplus is taken from the army,
artillery first (army+0xf0), then cavalry (+0xec), then infantry (+0xe8), using the per-regiment
strength 0x486820(U, type); the split happens only if both the part kept and the part left are
>= 2000 men; the detached army returns to the pool (0x8a7828). Fleet twin 0x54b8f0.

#### 4.6 Armies: assignment and merging
- A new or freed army handed to a General (0x54a110 -> 0x549350): if not in battle, build the
  list of active non-DEFEND plans that do not refuse it (vfunc 0x184), sort by squared map
  distance from the army to each plan's target, offer it in that order (0x5586a0) until one
  accepts; otherwise vfunc 0x17c (returned to the King / DEFEND).
- Accept (0x5586e0, logs `"Agent #%d ... accepts Army ..."`, bounce 0x558a35, reject 0x558bd1)
  moves ownership (unit+0x9e = agent).
- Merge (0x558e90, log `"%s (#%d, %s) merges unit #%d into unit #%d!"`): an agent merges every
  other idle army it owns in the same province (same state 0x48c010, not moving, not in battle)
  into the given army.
- `"Unit #%d (%s) is halted in %s!"` (0x5597d0..0x55989b): an army without a path and not moving
  is stopped and re-offered (housekeeping).
- Plan start (0x54cea0): subscribe events 0x12, 0x0e; compute need/own; phase 0, choose the
  gather point; first think after 1 day. Stop (0x54cf20) unsubscribes.

#### 5. Aggressiveness, ferocity, war, strategic mode
In the land-war code decoded here the only .ai values read are the conquer block (§3.1), base
+0x28 and front +0x2c (§4.2) (the garrison block is read only by the garrison plan 0x554140,
part D). `war` (+0x22) is read only by the war predicate 0x540fc0 (diplomacy) and `ferocity`
(+0x04) by the peace logic; the strategic mode (K+0x40) and the combat list (+0xc2, read by the
mode selection 0x5421a0) are **not** read by the General or the conquer / land-attack plans
(**NIEPEWNE**: they act through war declaration and the budget, parts A/diplomacy).
"Aggressive" behaviour therefore = high `conquer.base` (every conquer plan +15*base), high
`conquer.distance`/`enemy`, plus the war predicate's `war`.

#### 6. Timing summary
| Agent / plan | interval |
|---|---|
| King | 20+rand%20 days, /2 if U+0x11e, /4 at war, min 2 (part A) |
| General, first think | 0x55a4a0(5, 10) |
| General, home area | 0x55a4a0(3, 10); other areas 0x55a4a0(7, 14) |
| candidate commit | 1 day after the first new candidate |
| plan first think | 1 day after start |
| CONQUER | not engaged 0x55a4a0(3, 5); engaged 2 + rand%5 |
| LANDATTACK | not engaged 3 + rand%7; engaged 2 + rand%5 |
| naval-type attack tail 0x54c920 | 0x55a4a0(2, 3) (part D) |

#### 7. Log strings of this part
0x631ef4 "starts a CAIMilitaryMinister ... with %d plans" (0x548947 General re-init, 0x5496ab
General init); 0x632c60 / 0x632cb0 conquer scoring (0x550ae6 / 0x5508ad); 0x6327fc gather
(0x54c8fc); 0x6327dc engage (0x54c15f); 0x632820 assault (0x54ce10, 0x54d347); 0x632748 /
0x632790 split army / fleet (0x54b815 / 0x54ba36); 0x633744.. accept / bounce / reject
(0x5587cc, 0x558a35, 0x558bd1); 0x63385c merge (0x558fd4); 0x633898 halted (0x55989b);
0x6338b8 creates an Army (0x55a778); 0x631ee0 "General" (0x5482e0 name).

#### 8. Port summary (pseudo-spec)
1. Per AI country: one General per own connected area (home area first), plus an overseas
   General per coastal enemy area without a land border, created on war and deleted on peace.
2. General limit = clamp(4 + provinces/3 + links, 4, 16), and <= manpower/4.
3. On war, on enemy armies in the area and on province events: score candidates (LANDATTACK on
   our provinces holding enemies/rebels or occupied: 20 + 2*V2 with x4+100 for own occupied,
   x2 rebels, x3 rebel siege; CONQUER on hostile-controlled provinces of our area and the nearest
   hostile land area: per §3.1, + 15*conquer.base); keep the best 16, commit the best up to the
   limit, a better candidate may replace the weakest plan.
4. Each plan: need = enemy (+fort floor, terrain, leaders, morale ratio); own = assigned armies;
   ask the King for the deficit; receive free armies nearest-first; refuse extras above 1.5*need.
5. Gather at the cheapest controlled province (base/front weights), march when >= 75 % of the
   need is assembled, fall back when below 25 %, pull back when arriving outnumbered.
6. Attack only with full morale (>= 99 %) and own > enemy in the target; retreat from a battle
   when below 66 % in both strength and morale.
7. Siege; assault when men/10000 >= fort level (or the siege is advanced), our assault value
   beats the garrison, and no strong relief army is adjacent (or the garrison is broken / it is
   the owner's last province).
8. Finish a conquer plan when the target is no longer hostile-held or no longer enterable;
   finish a land attack when no enemy remains and the province is not occupied.
9. Think intervals as in §6; port may use the same day counts with k = 0.

---

## Military AI, part D: admirals, naval plans, defence/garrisons, invasion, transport (eu2.exe v1.09)

Specification only (no code copied). **NIEPEWNE** = believed, not proven; **NIEZNANE** = not found.
U = the AI country, K = its King/controller (country+0x212e, class vtable 0x5f17f8), E = "hostile to U"
(0x43b6e0(C, x): true if C is at war with x — relation byte +0x36 — and **always true for the rebel tags
0x151..0x153**, so rebel armies/fleets count as enemies everywhere below). Dumps: re/mil/k1..k5.txt.

#### 0. Helpers used below (identified this session)

| addr | meaning |
|---|---|
| 0x558510 | agent/plan -> U's country index (+6) |
| 0x482da0(i) | province i (0x8077d4 + i*0x4a0) |
| prov+0x3c4 / +0x3c8 | owner / controller |
| prov+0xca | terrain class; **>= 5 = sea zone** (exactly 5 = the sea step tested by transport) |
| prov+0x114 / +0x11c | neighbour count / neighbour array (12-byte entries, first dword = index) |
| prov+0xae | unit list in the province; unit+0xde = owning country, unit+0x53 = busy byte, unit+0xc4 = path |
| prov+0x41c | city/port object (non-zero = has a port/city); city+0x30 = units in port; city+0x44 = adjacent sea zone |
| prov+0x408 | buildings: 0x47f3e0 = fortress level (+0x3a), 0x47caf0 shipyard (+0x68), 0x482920 flag +0x2c (NIEPEWNE: "has fort/port") |
| prov+0x498 | continent object ("theatre"; capital's = 0x45b7d0(U)+0x498) |
| prov+0x487 | coastal flag (invasion requires it); prov+0x488 = sea zone touches ports (blockade/breakblockade require it) (NIEPEWNE) |
| prov+0x140 | int, used as "strategic"/value weight (NIEZNANE which game field) |
| prov+0x3e4/+0x3e8 | map x/y (squared distance in 0x549350) |
| 0x47d970(P) | province value (base +0xb6 plus building bonuses) |
| 0x483570(P, U, 0) | supply limit of P for U (int) (NIEPEWNE name, use is clear) |
| 0x481a30(P) | religion/unrest term 0..3 from prov+0xf6 vs controller religion (NIEPEWNE: revolt-risk part) |
| 0x481820(P, U) | P holds a stationary unit (+0x53 == 0) hostile to U |
| 0x4818b0(P) | P borders a land province of another owner (border province) |
| 0x484260(P, C) | P has a neighbour **owned and** controlled by C; 0x484320 = owned **or** controlled |
| 0x481630(P, i) | province i is a neighbour of P |
| 0x482dc0(P) | P is under siege (0x4961f2) |
| 0x48f070(list) | sum of unit strength (vfunc 0x84) |
| 0x48efe0(list, U, &a, &b) | sum of two strength measures (+0x7c/+0x80 vfuncs) of units hostile to U |
| 0x48ee80(list, U) / 0x48ee30(list, U) | sum of unit size (vfunc 0x88) of U's own / of hostile units |
| 0x48fd70(list, U, n[3], &best) | hostile fleets: n[k] = sum of ship counts unit+0xe8/+0xec/+0xf0 (the 3 ship types) |
| 0x43a820(capital, P) | distance from U's capital |
| 0x4cbe90(C, x) | x is in a vassal relation with C; 0x4cc520(x) = relation byte +0x3f (casus belli) |
| 0x438690(C) | per-man unit strength factor of C (rebels: 0.6 + 0.01*(year-1400)) |
| 0x45b940(C) | annual income = 12 x last month's income |

#### 1. Agent framework (shared by General and Admiral) — needed to implement anything below

**Theatre minister** (base class vtable 0x5f3bd4, ctor 0x548700; log "%s starts a CAIMilitaryMinister
for %s with %d plans." at 0x548947 / 0x5496ab / 0x54a562). One **General** per continent where U owns
land (K+0x754 list, created by K's init 0x541c50 via 0x5482a0; also 0x542f90 at runtime, list K+0x758),
one **Admiral** per *sea area* (an area of such a continent with area+0x28 == 2) (0x543110, list K+0x76a;
created at K init 0x54204c and when a sea area gains an owner, event path 0x55ac00 -> 0x5444d0).
The capital's theatre is put at the head of the list. An extra "expedition General" (vtable 0x5f2680)
is created by K's war-declared handler 0x544160 (see §6).

Minister fields: +0x48 theatre object, +0x40 **max active plans**, +0x4c..+0x64 seven *prototype* plans
(one per category), +0x68[16] scored candidates (count +0xac), +0x16/+0x1a list of active child plans,
+0xb0 "commit already scheduled", Admiral +0x44 = home port, Admiral +0xb6 = fleet list.

- **Max plans** (0x548800): 4 + (provinces of theatre)/3 + (areas in theatre +0x16 list), then
  min(that, 0.25 x country+0xec4) (NIEZNANE field, a force-size limit), clamped to **4..16**.
- **Scan** (0x549050(cat, provinceList, nearProv) / single province 0x5491f0(cat, P) / units 0x548f50):
  for every province not yet visited in this pass, in this theatre, with no active plan already on it
  (0x549280; for cat 3 also no INVASION plan on it): prototype[cat].evaluate(P, theatre) (vfunc 0x1e8)
  writes score +0x44 (< 0 = invalid). If nearProv is given and P neighbours it: score +5. Valid
  candidates go into the sorted candidate list (0x548d60, max 16, highest first) and the prototype is
  replaced by a fresh clone (vfunc 0x1e4). The first insertion schedules a **commit** next day.
- **Commit** (0x548df0): find the weakest active plan (lowest score, DEFEND type 0xc excluded). Start
  candidates in score order while active < max. The first candidate that does not fit replaces the
  weakest active plan only if it scores higher (the weakest is then cancelled). Other candidates are
  dropped. Starting = add child + vfunc 0xc0.
- **Unit distribution** (0x549350): a free unit is offered to the active plans (DEFEND excluded) that
  are not full (vfunc 0x184), nearest first by squared map distance prov+0x3e4; offer = 0x5586a0
  (army -> plan vfunc 0x198, fleet -> plan vfunc 0x1a8) until one accepts.
- **Events** (minister vtable): slot 0x84 (0x548d20) = war declared involving U -> vfunc 0x1f4, 0x1e0,
  0x1e8 (stop peacetime work, then the two war scans); slot 0x80 (0x548cf0) = peace -> vfunc 0x1f0;
  slots 0x10c/0x110 (0x548a90 / 0x548b00) = a child plan finished / was cancelled -> vfunc 0x1fc /
  0x200 (type, target); slot 0x24 (Admiral 0x54ad10 -> 0x548b70) = a unit moved: if it is hostile and
  in this theatre and the local presence score (vfunc 0x1dc) >= 0.1 -> single-province scan
  (vfunc 0x1e4) on that province, then commit. AI event ids subscribed with 0x55ae60:
  Admiral 0x01, 0x0f, 0x12, 0x13, 0x16; General 0x00, 0x12, 0x13, 0x15, 0x16, 0x1a, 0x1b (meaning NIEZNANE).

**Timing.** Wake-up = 0x55af00(days, arg) posts event 0xd8 at today + days; its handler 0x55aec0
calls think(arg). All day counts are drawn by **0x55a4a0(lo, range)** = lo' + rand % range' with
lo' = lo - lo*o/10, range' = range - range*o/10 (each >= 1), o = option [[0x8c0bfc]+0x2c6]
(the AI speed/difficulty option already seen in SPEC_AI's join limit). Below "a+r" means 0x55a4a0(a, r).

| who | interval |
|---|---|
| minister first think (after creation) | 5+10 days |
| minister periodic think (0x548a20 / Admiral 0x54a640) | **3+10** days if the theatre is the capital's continent, else **7+14** |
| commit after a new candidate | next day |
| attack-family plans (NAVALATTACK, BLOCKADE, BREAKBLOCKADE, shared with land attacks) | start: 1 day; think **2+3** days (0x54c920) |
| DEFEND | start 1+2; think **250+500** days (0x5519b0) |
| GARRISON | start 10+30 (0x554430); think **60+120** (0x5544b0) |
| TRANSPORT | **15+25** days (0x555390 / 0x555f70) |

#### 2. Admiral (vtable 0x5f2470)

**Init 0x54a3d0.** Prototypes: slot 4 (+0x5c) BREAKBLOCKADE (vt 0x5f4af8), slot 5 (+0x60) BLOCKADE
(vt 0x5f48d4), slot 6 (+0x64) NAVALATTACK (vt 0x5f448c). Max plans, home port (vfunc 0x1f8), log,
first think, subscriptions.

**Home port (0x54ab60, vfunc 0x1f8; result +0x44).** Over the theatre's areas owned by U, every
province with a port, controlled by U, whose sea zone (land province: its +0x3d6 zone) belongs to this
sea theatre: score = value(P) + 2 x fortress level - 10 if border province + 10 if shipyard - 10 if a
hostile unit sits in it. Highest wins. Recomputed when a province event touches the home port or a
U-owned province (0x5494d0, slot 0x64).

**Periodic think 0x54a640.** (a) For each own fleet in a province with prov+0x10a == 0x94 that is
moving and not busy: if U has **no** own-controlled port at all, an UNITMOVE_ADD_DEST (event 0x34) is
posted for it (NIEPEWNE: stranded-fleet rule, 0x94 NIEZNANE). (b) run queued events (0x558440),
reschedule (§1). think(0) = commit.

**Fleet offered (0x54aa10, vfunc 0x1a8).** No home port -> refuse. Fleet in the home port with
strength > 0.1 -> offered to the nearest needing plan (0x549350). Otherwise sent to the home port
(0x559f60). Idle fleets therefore **collect in the home port** and are handed out from there.

**War scans.** War declared: NAVALATTACK scan (0x54a950, cat 6) and BLOCKADE scan (0x54a920, cat 5)
over every province of the theatre — only while U is at war. A port of ours blockaded (slot 0x70,
0x54aaa0 with flag): BREAKBLOCKADE candidate on that port's sea zone (city+0x44); blockade lifted
(no flag): cancel BREAKBLOCKADE plans on that zone. Plan ended (0x54a990 finish / 0x54a9d0 cancel):
BREAKBLOCKADE (0xe) ended -> full rescan; BLOCKADE (0xf) finished -> retry a blockade of the same zone,
if impossible full rescan (cancel: retry then rescan).

#### 3. Attack-family plan (base used by NAVALATTACK, BLOCKADE, BREAKBLOCKADE and the land attacks)

Fields: +0x70 target, +0x74 engaged, +0x76 required strength, +0x7a assigned strength (0x54bcd0:
strength of the plan's own units + units of sub-plans), +0x7e gathering point, +0x82 phase
(0 gathering, 1 attacking).

Think 0x54c920, every 2+3 days:
1. **Phase update 0x54c450** (NAVALATTACK and types 0xe..0x10 skip it and are always phase 1):
   phase 1 with assigned < 0.25 x required -> back to gathering (new gather point). Phase 0 with
   assigned > 0.75 x required -> phase 1 if every unit is at the gather point or the target, or the
   units already there exceed 0.75 x required. A gather point that is ours and besieged is replaced.
   In phase 1: if our strength already in the target plus that of the arriving units stays below the
   hostile strength in the target -> back to phase 0, not engaged (**regroup instead of attacking
   piecemeal**).
2. **Battle retreat 0x54c350** (vfunc 0x108): for each of our units in combat, if our two strength
   measures are both < 0.66 x the enemy's and our morale-like field +0x20 is lower -> order retreat
   (battle vfunc 0x60); the plan stops for this tick.
3. vfunc 0x208 "objective reached" -> finish (0xc4).
4. Not engaged: recompute required (0x1f8) and assigned, report shortfall (0x21c), and if ready
   (0x54c0a0: phase 1 and assigned >= required) **engage** (0x54c140, log "%s #%d (%s) engages in
   battle!"): every idle unit not at target/gather is ordered to the target (phase 1) or gather
   point; a unit whose order fails is given back to the minister.

**Required strength 0x54bdb0** (land and naval targets): 0.25 x hostile strength in the target and
around it (0x559c80 target, 0x559ca0 same-type neighbours) + a siege term when the target is held by
another country with a fortress (built from U's unit-type stats 0x433400..0x433590 times
(3000 x fort + 2000), (fort+1) x 1000 and 5 x fort; else at least 500 when ...); x1.1 / x2 / x1.5 for
terrain classes 1 / 2 / 4; x1.5; for land targets capped at 0x43d340 (supply). NIEPEWNE in detail —
naval targets have no fortress, so in practice **required = 1.5 x (0.25 x local hostile strength +
largest hostile stack term)**.

**Shortfall (0x54d710, naval):** if engaged and required - assigned > 1, the plan's request object
(+0x64) is updated with the deficit, or the King is asked (K vfunc 0x1e0) — this is the link to the
King's fleet-building budget (other fork).

#### 4. Naval plans

| plan | evaluate (score, -1 = invalid) | objective reached (0x208) |
|---|---|---|
| **NAVALATTACK** 0x54d550 (create 0x54af40) | target must be a sea zone with a stationary hostile fleet: **40 + 5 n0 + 4 n1 + n2** (n = hostile ship counts by type, 0x48fd70) | 0x54c200: not done while the target is ours but not controlled by us, or still has a hostile fleet that U may attack (0x45f730) |
| **BLOCKADE** 0x54d9f0 (create 0x54b0f0) | sea zone with +0x488: v = sum over neighbouring port provinces controlled by a country hostile to U of (value(N) + hostile ships docked at N + 30 if N is besieged); valid if v >= 0.1; **score 15 + v** | 0x54dc00: done when any of the plan's fleets drops below 0.8 (vfunc 0x34, NIEPEWNE morale/strength ratio) or v < 0.1 |
| **BREAKBLOCKADE** 0x54d8d0 (create 0x54b020) | sea zone with +0x488: v = sum over neighbouring ports controlled by U of (value(N) + U's ships docked at N); v >= 0.1; **score 25 + v** | shared 0x54c200 |

- BLOCKADE required strength (0x54db50) = 10 x (U naval tech values 0x433770 + 0x4336d0) +
  0.8 x strength of hostile fleets in the adjacent hostile ports.
- Gathering point of a naval plan (0x54d840) = the home port of the Admiral that owns the target's
  sea area (first Admiral if none); recomputed if that port is no longer ours.
- Fleet offered to a naval plan (0x54d780): go to the gathering point if one exists and the plan is
  not engaged, else join directly (0x54c250); failure = bounce + shortfall update.
- Naval "superiority": there is no global superiority measure; each target is attacked only when the
  assigned strength reaches the required strength (§3), and the 0.66 retreat rule applies in battle.
- 0x54d810 (vfunc 0x20c): target with prov+0x10a == 0x94 -> true with 40 % (use NIEZNANE).

#### 5. Defence: DEFEND + GARRISON

**DEFEND** (vt 0x5f4f20, ctor 0x5511e0, size 0x84): one per General, created with the General
(0x549500, evaluate on (first province of the theatre, theatre)). Score 0x551240: 5 if the theatre's
+0x2e equals its province count (NIEPEWNE: theatre fully ours), else 20.

**Threat 0x5512f0 (+0x78).** For every area of the theatre with an owner O (O not in a vassal relation
with U either way): t = strength of all O's armies (O+0x1248); remembered a = t if relation byte +0x3c
(NIEPEWNE: alliance) then t *= 0.1; royal marriage (+0x3b) t *= 0.5; 0x431270(religions) false ->
t *= 0.75; O holds a casus belli (+0x3f) t *= 1.5, else if relation +0x33 clear and its list empty
t *= 1.25; threat += t - 0.5 a. Finally threat *= 0.1, and *= 0.1 again when the theatre is fully ours.
(NIEPEWNE: U's own areas are included with O = U.)

**Defence power (+0x7c)** = min(threat, 5000) / 0x438690(U), capped at annualIncome(U) x +0x80 x 10000
(+0x80 is a budget share written by the King, NIEPEWNE), at least 1.

**Garrison layout 0x551500** (run at peace by DEFEND think and by the General's peace handler
0x549ee0): cancel all garrisons; score every province of the theatre (garrison score below); those with
score > (theatre provinces)/2 become garrisons; if none, the single best (a province with the
0x482920 flag wins ties); if still none, the DEFEND province itself with weight 1. Each garrison gets
**power = power(+0x7c) x score / sum of scores, capped at the province's supply limit 0x483570**
(log "%s starts a garrison in %s with %.2f power", 0x554485).

**Garrison score 0x554140** (log "%s considers a garrison in %s at %.2f power", 0x554375), weights
from the .ai garrison block (prefs = country+0x25c; fortress +0x78, strategic +0x7c, size +0x80,
supply +0x84, war +0x88):

1. s = 0.5 x (fortressLevel(P) x **fortress** + P.+0x140 x **strategic**)
2. s += (provinces in P's continent / 2) x **size**; if that continent has exactly one province
   (island): s /= (2 x number of Generals + 1)
3. s *= **supply** x max(0.05, 0.1 x supplyLimit(P))
4. if K has a current war target T (K+0x3c): P neighbours land owned or controlled by T ->
   s += **war** x 200; else if T is in an alliance: s += **war** x 50 when P neighbours any member
5. s *= (1 + unrest term 0x481a30(P)) — so garrisons go where revolt risk is high (the rebel answer;
   NIEPEWNE that this term is revolt risk).

**GARRISON plan** (vt 0x5f513c, type 0x11, size 0x90): +0x70 province, +0x88 power. Think 0x5544b0:
**if U is at war the garrison cancels itself** (its armies return to the pool for attack plans);
otherwise re-arm every 60+120 days. Army offered (0x5543a0): accept if it comes from a TRANSPORT
plan, else route it to the province (0x559eb0 / 0x55a320), else bounce. At war the General first
cancels the DEFEND children (0x549f50 = 0x5514d0), and at peace rebuilds them (0x549ee0).
DEFEND think 0x5519b0 at peace: recompute threat and power, rebuild; at war: only reschedule.

Reaction to enemy armies in own territory is done by the General's attack scans (other fork); this
part contributes only the war-time dissolution of garrisons and the 0.66 retreat rule.

#### 6. Invasion (overseas landings)

**Expedition General** (vt 0x5f2680; K's war handler 0x544160): when a war starts involving U and U
has a coast (+0x36c > 0), for each continent of the enemy (enemy+0x12e8) that has a sea area and where
U owns nothing and has no General, a General is created for it. Its init (0x54a2c0 -> 0x54a220) runs
an INVASION scan (cat 3) over the continent when > 25 % of its provinces are not controlled by U
(or are besieged). Normal Generals also try INVASION on a province event (0x54a030/0x54a070/0x54a0c0:
CONQUER first, else LANDATTACK and INVASION).

**INVASION** (vt 0x5f3ddc, prototype +0x58, type 0x10) evaluate 0x554560, target P:
-1 unless P is coastal (+0x487) and not controlled by U. Then
1. s = value(P)/12 + 5 x (6 - fortressLevel) + 5 if flag 0x482920 + 10 if P is in U's list
   country+0x1350 (cores/claims, NIEPEWNE)
2. P borders land owned and controlled by U: s = 2 x (s + 25)
3. any neighbour of P borders such land: s += 100
4. x0.95 for each of supplyLimit < 5, < 10, < 20, < 30
5. s += 2.5 x P.+0x140 (the later "x0.2 if not coastal" is dead code)
6. unless K's mode (K+0x40) is COLONISE (2): s -= distance(capital, P)
7. **score = max(s, -30) + 35**

Army offered (0x5547a0): already in P -> keep; else route it with a TRANSPORT (0x55a320) — log
"%s sends an invasion to %s(%d)" (0x55482a); if no route/transport -> "%s cancelled an invasion to
%s(%d)" (0x5547ef), bounce, shortfall. Done (0x554840) when P is uncontrolled, not attackable, ours
again, or held by a non-hostile country; not done while P is under siege. The landing point is the
target province itself (the transport's first land step after the sea leg, §7).

#### 7. Transport (every unit move with a sea leg)

0x55a320(unit, P) = "send this unit to P": unit already there -> keep; unit busy -> fail; else create
TRANSPORT (vt 0x5f538c, type 6, ctor 0x555030), evaluate, start; score < -0.1 -> fail.

**Evaluate 0x5551f0:** need = ceil(+0xf0/1000) + ceil(+0xec/1000) + ceil(+0xe8/5) (army's three troop
counts; which is infantry/cavalry/artillery NIEPEWNE); capacity = 2 x ships(0x39c) + ships(0x398) +
ships(0x394) (country ship-type counts; NIEPEWNE which is "transport"); need > capacity -> -1. Path
(0x41d2f0) from the army's province to P; empty path -> -1.

**Start 0x555390** ("AITransport #%d: Army #%d (%s) starts transporting to %s."): take the army (and its
fleet if it is already aboard); give it the land steps of the path up to the first sea step (+0xa0 =
embarkation sea zone). If there is a sea leg: +0xa4 = waiting for a fleet, and the King is asked for
ships (K vfunc 0x1e0). Rescheduled 15+25 days.

**Flow:** army arrives (0x555a70, "...has arrived at %s!" / "moved to %s!") -> waiting: load if on land
(0x555bf0: fleet in the embarkation zone with room -> "AITransport #%d of %s: Loads Army #%d.", army
ordered into the zone) or sail if already aboard. Fleet arrives (0x555b30) -> waiting: take the fleet,
load; else unload. Sail 0x555e20: "Starts moving to the Unload Point (%s)." (or "(outside %s)" when
there is no land step, i.e. the target is coastal and the fleet waits next to it); the fleet gets the
remaining sea steps, the first land step becomes the landing point. Unload 0x555d70: if the fleet can
unload there (0x490e20) "Starts unloading to %s." and the army gets the rest of the path, else cancel.
Think 0x555f70: cancel if the armies need more capacity than U has, or if no fleet was found
one year (+1 month +1 day, NIEPEWNE) after the start. A pure land path just moves and finishes.

King side 0x5463e0 (K vfunc 0x1a8, fleet returned to the King): log "The King of %s received a fleet
(#%d) with troop onboard!" if it carries troops; the King takes it (0x559150) and redistributes
category 6 (0x545790(6), other fork).

#### 8. .ai weights, mode and war state in this part

- garrison block: only the garrison score (§5). conquer block and war/ferocity: not read here.
- base/front (prefs +0x28/+0x2c): not read in this part (gathering point choice is in the land part).
- mode: only INVASION (no distance penalty in COLONISE).
- at war: garrisons dissolve, DEFEND idles, naval scans run; at peace: no naval attack/blockade scans
  (0x54a920/0x54a950 return false), garrisons rebuilt.

#### 9. Log strings of this part

0x631e9c King received fleet (0x5463fe); 0x631ef4 minister start (0x548947, 0x5496ab, 0x54a562);
0x6327dc engages in battle (0x54c15f); 0x6330bc considers a garrison (0x554375); 0x6330e8 starts a
garrison (0x554485); 0x633114 sends an invasion (0x55482a); 0x633134 cancelled an invasion (0x5547ef);
0x6332bc..0x63345c AITransport (0x5553c7 start, 0x555aa5/0x555acf army moved/arrived, 0x555b82/0x555bac
fleet moved/arrived, 0x555c51 loads, 0x555da0 unloading, 0x555e49 / 0x555e88 unload point).
Plan type ids (0x53a950 table 0x631f2c): 2 LANDATTACK, 3 NAVALATTACK, 6 TRANSPORT, 0xc DEFEND,
0xe BREAKBLOCKADE, 0xf BLOCKADE, 0x10 INVASION, 0x11 GARRISON.

#### 10. Port summary

1. One theatre agent per own continent (General) and per adjacent sea area (Admiral); max plans
   clamp(4 + prov/3 + areas, 4, 16). Think 3-12 days (home continent) or 7-20 days, scaled by the AI
   speed option; commit next day after new candidates.
2. Candidate = (plan type, target province, score); keep a sorted list of 16; fill free slots, replace
   the weakest only if beaten.
3. Admiral: home port = best own port (value + 2 fort, -10 border, +10 shipyard, -10 enemy present);
   idle fleets go there. At war: NAVALATTACK every sea zone with a hostile fleet (40 + 5/4/1 per ship
   type), BLOCKADE sea zones next to hostile ports (15 + value + docked + 30 if besieged), and when an
   own port is blockaded BREAKBLOCKADE (25 + value + own ships in port).
4. Attack plan: required = 1.5 x local hostile strength term; gather to 75 %, attack when assigned >=
   required, regroup below 25 % or when weaker than the defender, retreat from battle at < 0.66.
   Think 2-4 days.
5. Peace: DEFEND per continent: power = min(threat, 5000)/strength factor (cap income x share x 10000);
   split over the best garrison provinces by the garrison score (§5), capped at supply limit.
   War: garrisons cancel, armies go to attack plans.
6. War with an overseas enemy: expedition General per enemy continent; INVASION score §6; armies moved
   by TRANSPORT (capacity check, King supplies ships, load / sail / unload at the target).

---

