# The AI's envoys: merchants, colonists, traders, missionaries (eu2.exe v1.09) - specification

Read from the `eu2.exe` v1.09 disassembly as a **specification only** (no code copied; addresses are
evidence). Markers: **NIEPEWNE** = believed, not proven; **NIEZNANE** = not found. Conventions as in
`docs/SPEC_AI.md` / `docs/SPEC_MILITARY_AI.md`: U = the acting AI country, K = its controller ("King",
U+0x212e), prefs = U's .ai object (U+0x25c), rand = 0x5ba99c, `a % b` = C remainder, trunc = toward
zero, dates {day byte, month byte, year word}, 30-day months. Player-side rules (costs, chances,
can-send tests, travel times) are in `SPEC_ENVOYS.md` and `SPEC_SMALLRULES.md` §3 and are only
referenced here.

## 0. Overview

| envoy | who decides | how often | at most | target choice |
|---|---|---|---|---|
| merchant | merchant agent 0x53dbe0 (one per King, created at 0x541dd1) | think every 30-59 d; plus a new roll after every merchant plan ends | 1 merchant plan at a time | weighted random over reachable CoTs (§1.3) |
| (trade agreement) | same agent | same think | list size < 2 | best CoT owner (§1.5) |
| colonist / trader | colonial agent 0x53b6c0 (King+0x4c, created at 0x541e23) | think every 20-39 d | 1 colonist-or-trader plan at a time | best-scored site from candidate lists (§2) |
| missionary | AI country monthly 0x557ab0 (slot 0x268 of vtable 0x5f55ec) | monthly roll | 1 per month | best own heretic city (§3) |

Fields used: U+0xe8c merchant stock, U+0xe94 colonist stock (shared by traders), U+0xe90 missionary
stock, U+0xe70 treasury, U+0xe52 date of the last colonist/trader sent, U+0xe68 embargo count
(SPEC_TIME_ECONOMY), prefs +0x0a `tradingpost`, +0x0e `neighbour`, +0x12 `enemies`, +0x16
`expansion`, +0x1a `traders` (all int), +0x8c `area` list, +0x9e `region` list, +0xb0 `continent`
list (lists of ids, searched linearly, 0x55dd10).

Resource pools of the King (SPEC_MILITARY_AI A.6; mapping proven by the request slots 0x545bc0..
0x545df5 and the seed slots 0x546150..0x546390):

| King request vfunc | pool | seeded from |
|---|---|---|
| 0x1c8 | money (+0x70) | treasury |
| 0x1cc | pool +0x82 | U+0xec0 |
| 0x1d0 | colonists (+0x94) | U+0xe94 |
| 0x1d4 | diplomats (+0xa6) | U+0xe98 |
| 0x1d8 | merchants (+0xb8) | U+0xe8c |

A plan's request is served on the King's TICK_RESOURCE (once per King think, 20-39 d at peace,
5-9 d at war); higher root score takes resources from lower (SPEC_MILITARY_AI A.6). Granting calls
the plan's "add resource" slot, which re-runs the plan's update immediately.

---

## 1. Merchants (agent 0x53dbe0, vtable 0x5f02a8, think = slot 0x1c = 0x53dcf0)

### 1.1 Agent layout

+0x16 plan list (count = list vfunc 4); +0x44/+0x48/+0x4c candidate list (head, tail, count) of
entries {CoT province id, integer weight}; +0x52 sum of the weights; +0x56 + 4*X = date of the last
trade-agreement plan toward country X (0x154 entries, zeroed by the ctor 0x53dbe0).

### 1.2 Start and think

- Start (slot 0xc0 = 0x53dcc0): roll for a merchant plan (§1.3), schedule the first think in
  0x55a4a0(30, 30) = 30 + rand%30 days (AI-speed adjusted, SPEC_MILITARY_AI 0x55a4a0).
- Think 0x53dcf0: schedule the next think 0x55a4a0(30, 30). If the agent is active (+0x0e):
  - plan list empty -> roll for a merchant plan (§1.3);
  - otherwise -> run the update (§1.4) of every plan of type 0x18 (merchant plans).
  Then, active or not, the trade-agreement step (§1.5).
- The merchant plan reports its end to the agent: execute -> slot 0x12c, cancel -> slot 0x130, both
  = 0x53de70 = "if active, roll again" (§1.3). So after every successful send or failed attempt
  the agent immediately tries the next merchant; between attempts there is at most one merchant
  plan.

### 1.3 Merchant roll (0x53de80) and candidate list (0x53df00)

1. Proceed only if rand%100 <= prefs `traders` (default.ai 100 = always; spain.ai 50).
2. Rebuild the candidate list (0x53df00): for every CoT in the global list 0x647d40 (count +0x114)
   compute w = weight(U, CoT province) (§1.3.1); if w >= 1, append {CoT province, trunc(w)} and add
   trunc(w) to the sum. Then, if the capital's province has a CoT link (capital prov+0xaa, the CoT
   that province trades in), append that CoT once more with weight 1 (it may now appear twice).
3. Log "Considering a new merchant plan for %s!" (0x631d88).
4. If sum > 0: r = rand % sum; walk the list subtracting weights; the first entry where r drops
   below 0 is chosen (ordinary roulette wheel). Create a merchant plan on that CoT (0x53ddc0, §1.4).

#### 1.3.1 CoT weight (0x53e2d0), C = the CoT object of province P

1. If U.vfunc 0x4c(P) (= 0x45c490, reachability: known, has CoT, overseas/harbour or own or same
   continent, owned, owner does not refuse trade, monopoly-before-trade-level-3 test; SPEC_ENVOYS
   §1 "Can-send") is false -> weight -1 (excluded).
2. w = C+0x24 (the CoT's yearly value). If w == 0 (not yet computed), w = sum of 0x480e30(p) (monthly
   trade value) over all provinces p whose CoT link is this CoT.
3. c = merchant placement cost 0x4378d0(U, P) (SPEC_ENVOYS §2); if c > 1: w = w / c.
4. n = C+0x20 (all merchants in the CoT) - 2 x trunc(U's merchant count there, C+0x28[U]+0x04).
   If n < 6: w x= 1.5; if also n < 2: w x= 1.5 again (x2.25 total). (Little competition, or our own
   share already dominant, makes a CoT more attractive.)
5. If P's owner is U: return 5w. Else if U has the flag +0x40 toward the owner (0x4cb860, the
   trade-agreement flag, DIPOFFTRADE rule SPEC_UI): return 1.5w. Else return
   w / max(1, distance(P, capital)) (0x499760 -> 0x43a820, map-distance units of SPEC_AI §4).

No explicit monopoly target: the competition term (step 4) rewards CoTs with few rival merchants,
and the hard limits (5 merchants before trade level 3, the 6th = monopoly, no 7th unless the CoT
holds >= 7 merchants) come only from can-send at execution time (§1.4 step 4).

### 1.4 Merchant plan (create 0x53ddc0, size 0x84, vtable 0x5f04b0, type 0x18)

- Target: +0x80 = CoT province (set-target 0x5503d0). Added to the agent's list.
- Start 0x53d980: score +0x44 = **85**; money needed +0x74 = trunc(cost + 1); merchants needed
  +0x7c = 1. Requests: money pool (K vfunc 0x1c8) and merchant pool (K vfunc 0x1d8).
- Update 0x53da70 (agent think, and every resource grant via slots 0x144 / 0x174):
  1. money still short (+0x74 - +0x70 > 0.01): keep waiting if +0x4c != 0, else **cancel**
     (+0x4c is never set by this family, so in practice: cancel - NIEPEWNE);
  2. merchant still short (+0x7c - +0x78 > 0.01): same rule with +0x5c;
  3. recompute trunc(cost + 1); if it rose above +0x74, request the difference from the money pool,
     store the new figure, wait;
  4. if can-send 0x45c600(U, P) (SPEC_ENVOYS: trade level >= 1, reachable, <= 5 en route, cost <=
     treasury, stock >= 1, monopoly rule) and cost <= +0x74: send 0x43f4b0(P), log "%s sends a
     merchant to %s" (0x631d6c), execute; else log "%s cant send a merchant to %s as he wanted"
     (0x631d40) and cancel.
- Execute 0x53da00: release the reserved merchant unit (slot 0x178 of +0x78), notify the agent
  (re-roll), finish. Cancel 0x53da40: notify the agent (re-roll), cancel.
- No war check: merchants are sent at war too.

### 1.5 Trade-agreement offers (0x53e0e0; plan vtable 0x5f0698, size 0x78, type 0x3e)

Gates, in order: U not at war (0x43c000); merchants/yr(U) (0x458b50) >= 5; U+0xe68 (embargo count)
<= 0; agent plan list count < 2.

Candidates = the entries of the merchant candidate list (§1.3 step 2, as last rebuilt). For each
CoT province P with owner O != U and more than 5 years since +0x56[O] (year part of the date
difference > 5): value = S(O) + C+0x24, where S = 0x552ec0 (below). Best strictly highest value
(> 0, start 0) wins; ties keep the earlier. If a winner exists: +0x56[O] = today; create the plan
on O, add it, start it.

S(O) (0x552ec0): -1 if U already has the trade-agreement flag toward O (0x4cb860), or O refuses
trade with U (0x4cbf60), or U embargoes O (0x4cbf20). Otherwise s = -5 per country with which O
already has the trade-agreement flag; +1 if royal marriage (+0x3b); +5 if allied (+0x3c);
+0.1 x rel(U->O); s = s x merchants/yr(U); if s <= 0.01 the score stays -1. Note the -1 does not
exclude: it is still added to the CoT value (literal).

Plan: start 0x551c20 (request one unit from pool +0x82, K vfunc 0x1cc, as the diplomatic plans of
SPEC_AI §0); standard update 0x551d90; can-execute 0x552fe0 = 0x45b4a0 (DIPOFFTRADE menu rule);
execute 0x552e80 = 0x43e670(O) (the human "Offer Trade Agreement"), then the SPEC_AI epilogue
(R(O)+0x18 = today) and finish. The update runs when the pool grants the unit (slot 0x168 =
0x53aab0); the merchant agent's think never updates type-0x3e plans. NIEPEWNE: while such a plan
waits it counts in the list and blocks the think's merchant roll (§1.2).

---

## 2. Colonists and traders (agent 0x53b6c0, vtable 0x5ef518, think = slot 0x1c = 0x53bbc0)

The same agent also runs the explorer/conquistador plans (types 0x32/0x33/0x34, creators 0x53c260,
0x53c520, 0x53c800; SPEC_EXPLORERS §6). Only the colonist (type 0x17) and trader (type 0x19)
parts are specified here.

### 2.1 Province lists

Seven province-id sets (each with find / add 0x40f360 / remove / count):

| field | content |
|---|---|
| L56 (+0x56) | unknown land provinces next to a known non-river province (exploration frontier) |
| L68 (+0x68) | unknown provinces of area 0x94 (Sea) |
| L7a (+0x7a) | **new sites, preferred**: no settlement, no TP, .ai preference > 0 |
| L8c (+0x8c) | provinces with native aggressiveness > 4 where a trader may go (explorer side) |
| L9e (+0x9e) | **trader sites**: a TP of level < 5 |
| Lb0 (+0xb0) | **colonist sites**: a settlement (own colony), or a TP of level >= 5 |
| Lc2 (+0xc2) | **new sites, not preferred**: no settlement, no TP, preference <= 0 |

.ai preference (0x55dd10): 50 if P's area (+0x10a) is in the `area` list, + 25 if P's region
(+0x10e) is in the `region` list, + 10 if P's continent (+0x106) is in the `continent` list.

Classify(P) (0x53bf90), only if P is in none of L7a, L9e, Lb0, Lc2:
1. if native aggressiveness P+0x13c > 4 and U.vfunc 0x54(P) (may send a trader, §2.6): add to L8c;
2. P has a settlement -> Lb0; P has a TP: level (0x482a00 = TP+0x04) < 5 -> L9e, else Lb0;
3. neither: preference > 0 -> L7a, else Lc2.

Filling:
- At agent start (0x53b960): subscribe to listener events 0x17, 0x23, 0x10 (NIEZNANE which), first
  think in 20 + rand%20 days; classify every province of the list U+0x1330 (NIEPEWNE: known
  provinces) that is
  unowned land (+0xca < 5), or own with a colony and no TP (0x485490), or own with a TP; then fill
  L56/L68 by a scan of all 2020 provinces; then, if U is a colonial power (0x43a970: fantasy, or
  colonists/yr >= 0.99, or explorers/conquistadors) and `expansion` > 0, create the first explorer
  plans and one colonist/trader plan (§2.3).
- Province discovered by U (slot 0x74 = 0x53c120, NIEPEWNE event): unowned land -> classify; the
  province leaves L56 (land) or L68 (sea, with naval tech 0x432e30); its unknown land neighbours join
  L56.
- Owner change (slot 0xa4 = 0x53bd60, NIEPEWNE event): unowned and known -> classify. Owned by
  another country -> removed from L7a, L9e, Lb0, Lc2, L8c. Owned by U: a full city (settlement that
  is not a colony) -> removed from all; a province that was in L7a or Lc2 (we just gained it) ->
  removed from both, then Lb0 if it has a settlement, else L9e with probability `tradingpost`%
  (rand%100 < prefs+0x0a), else Lb0; a province with TP level > 5 (or 0 < level <= 5 when the
  world byte [0x8d0d7a] is set, NIEZNANE) -> moved from L9e to Lb0.

### 2.2 Think (0x53bbc0)

1. Next think in 20 + rand%20 days (not AI-speed adjusted).
2. Nothing if prefs `expansion` == 0 (default.ai: 0 = "does not colonize").
3. Count own plans by type; colonist/trader plans are updated on the way (0x550090 / 0x5504d0).
4. Gate: U is a colonial power (0x43a970), or colonist stock + colonists/yr (0x4582d0) > 1;
   otherwise stop.
5. If there is no colonist or trader plan: make one (§2.3). (Explorer plans: type 0x32 if none,
   type 0x33 while fewer than min(5, |L56|/5 + 1), type 0x34 while fewer than 2.)

So the AI has **at most one colonist-or-trader plan at a time**, whatever `expansion` says (the
.ai comment "amount of provinces we try to colonize at the same time" is not what the code does).

### 2.3 Choosing the site and the envoy (0x53c9f0)

k = max(1, trunc(expansion / 2)). T = |Lb0| + |L9e| + |L7a| + |Lc2|; nothing if T <= 0.
E = |Lb0| + |L9e| (existing colonies and TPs).

1. If E > k + rand%k: go to "expand" (step 3).
2. "New site": if |L7a| + |Lc2| >= 1: P = best of L7a (0x53cbb0) if L7a is not empty, else best of
   Lc2 (0x53cc70). If P exists: colonist plan if prefer-colonist(P) (§2.5) else trader plan; done.
   If no P: go to step 3.
3. "Expand": if E > 0: r = rand % E; r <= |Lb0| -> P = best of Lb0 (0x53cd40), colonist plan;
   else P = best of L9e (0x53ce40), trader plan. (Off by one: r = |Lb0| also picks the colonist.)
   No plan if the best is 0.

Best-of rules (all keep the strictly highest score > 0, start 0, first wins ties):

| list | eligible if | score |
|---|---|---|
| L7a, Lc2 | (trade level >= 2 (0x432ef0) and may-trader(P)) or may-colonist(P) | trunc(V(P) - trader cost 0x437110(P)) |
| Lb0 | may-colonist(P) | trunc(V(P) - colonist cost 0x436960(P)), + colony population (settlement+0x7e) if the settlement is a colony, truncated |
| L9e | may-trader(P) | trunc(V(P) - trader cost) |

(The new-site score subtracts the trader cost even when a colonist will be sent - literal.)

### 2.4 Site value V(P) (0x53cee0, int)

1. v = trunc(2 x goods price (P+0x54 goods object +0x00) + 2 x base tax (+0xb6) + 250).
2. Scan P's neighbours: "own" if any is owned by U, "other" if any is owned by another country.
   If the owner of any owned neighbour is at war with U (its flag +0x36 toward U) -> **V = 0**.
3. own: v += `neighbour` (prefs+0x0e); not own and `neighbour` > 90: v -= `neighbour`.
4. other: v += `enemies` (prefs+0x12; default -50).
5. v -= 3 x colonisation difficulty (P+0x124).
6. natives (P+0x12c > 0): v = trunc(v / (trunc(P+0x13c x P+0x12c / 80) + 1)).
7. V = 2v. (A distance to the capital is computed when v > 0 and not own, then discarded:
   **distance does not count**.)

Examples: spain.ai (`neighbour` 100) gets +100 next to its land and -100 elsewhere; portugal.ai
(40) gets +40 / 0.

### 2.5 Colonist or trader for a new site (0x53d060, true = colonist)

1. trade level < 2 -> colonist; 2. P already has a settlement -> colonist;
3. rand%100 < `tradingpost` -> trader;
4. colonist chance (U.vfunc 0x44 = 0x436b90) < trader chance (0x437260) -> trader;
5. 5 x trader cost < colonist cost -> trader;
6. at war -> trader, else colonist.

### 2.6 Where the AI may colonise / trade (AI country vtable 0x5f55ec)

The AI class overrides "may send a colonist" (slot 0x50 = 0x5566e0) and "may send a trader"
(slot 0x54 = 0x5568b0); the base class has 0x45c110 / 0x45c2e0. Tests in order (tag = U+0x06,
country.csv row; religion and continent ids from SPEC_EVENTS / the CON_ table 0x61dc74):

1. colonist only: P owned by U -> **allowed** (no further test, not even the base rule);
2. SPA (8): P must have ToT-SPA (+0x76); POR (5): P must have ToT-POR (+0x77);
3. tag 0x52 (NIEZNANE which: csv row 82 is NAT, but NAT is 0x152 in the exe): P must border an
   own province, and if province 356's owner exists and is not U, rel(U -> it) >= 100;
4. RUS (7): without the byte U+0xfec (NIEZNANE) only Siberia (region 0xd), and there only next
   to an own province; with it, no restriction;
5. state religion Sunni (5) or Shiite (6): only continents Asia (3) and Africa (5);
6. then the base rule.

Base colonist rule 0x45c110: valid land province (+0xca < 5), known to U, not under siege, owner
none or U (and then controlled by U), settlement absent or a colony (a full city is excluded),
0x4977c0(P+0x22) false (NIEZNANE), and coastal (+0x487), or a neighbour owned by U that is a full
city, or (P in Siberia and any own neighbour).
Base trader rule 0x45c2e0: valid, no settlement, not besieged, land, known; an existing TP must be
U's and not level 6; 0x4977c0 false; own-and-controlled -> yes; else unowned and (coastal or an own
neighbour).
Send-time checks (vfunc 0x370 = 0x45c270, 0x374 = 0x45c420): the rule above, trunc(treasury) >=
trunc(cost), stock U+0xe94 >= 0.99 (trader also trade level >= 2).

### 2.7 Colonist plan (create 0x53d140, vtable 0x5efed0, type 0x17) and trader plan (0x53d300, vtable 0x5f00c0, type 0x19)

The trader creator falls back to a colonist plan when trade level < 2.

Score +0x44 = colonists/yr(U) x (B + c) x F x W, with c = 30 (colonist) / 25 (trader), B = 100 if
K's mode is COLONISE (K+0x40 == 2) else 0, F = clamp(0.004 x days since U+0xe52, 1, 3) (days =
(years x 12 + months) x 30 + days; x1 up to 250 days, x3 from 750 days), W = 0.2 at war (colonist
only). The score only ranks the plan in the King's resource pools.

Start (0x54ffb0 / 0x550430): money needed = trunc(cost + 1), envoy needed = 1; request money (K
0x1c8) and a colonist (K 0x1d0 - traders also draw from the colonist pool); subscribe to event 0x13
(NIEZNANE); timer in 32 + rand%10 days.

Update (0x550090 / 0x5504d0): wait while money or the colonist is short (no cancel); if the cost
rose, request the difference and wait; else if the send-time check (§2.6) holds: send 0x43efc0 /
0x43f230, log "%s sends a colonist/trader to %s" (0x632c28 / 0x632c44), execute (release the
envoy unit, finish); else cancel.

Timer (colonist 0x5501a0 / trader 0x5505e0), every 30 + rand%20 days:
1. P owned by somebody and not controlled by U -> cancel;
2. colonist: cancel if month(today) - month(start) > 15 - **never true** (months only, 0..11);
   trader: cancel if year(today) - year(start) > 1;
3. money cheat: if month(today) - month(start) > 11 (colonist: never true) / > 6 (trader), and
   colonists/yr > 1.5, U owns more than 5 provinces, money granted < needed and the colonist unit
   is granted (> 0.99): K vfunc 0x1ec(plan) (NIEZNANE), score = 999999, post GAINMONEY (event 0x5f,
   category 7) of needed + 1.01 - granted to U, and request that amount from the money pool.

---

## 3. Missionaries (AI country monthly 0x557ab0, then the base monthly 0x451300)

Two unrelated preludes in the same function: an AI VP gift when option [[0x8c0bfc]+0x2da] is set
(rand%1000 < difficulty/2 + option +0x2c6 + 2 -> VP ledger type 6 += table 0x638210[rand%11]),
and the Defender-of-the-Faith claim (world flag 4 not set, treasury > 1000, the religion has a
defender title 0x4312d0 and nobody holds it 0x4312a0, rand%100 < owned provinces - 10 -> 0x43f6f0,
command 0x129, SPEC_REVOLT_RELIGION). Then:

1. Only if the state religion is 0..6 (Protestant, Reformed, Counter-Reformed, Catholic, Orthodox,
   Sunni, Shiite) and missionary stock U+0xe90 > 0.999.
2. For every owned province P (list U+0x131a) whose religion differs from the state religion
   (0x431270; Catholic and Counter-Reformed count as equal):
   s = 1; +1 if the missionary chance 0x45c8b0(P) > 30, +1 more if > 50; +4 if P is not in
   Europe (continent != 2); +2 if P is pagan (7); s = -1 if P has a TP or 0x485490(P) (no city,
   or only a colony) - so only full non-colony cities count.
   best = the P with the strictly highest s (> 0); sum = sum of all s (the -1s included).
3. sum = 0 at war.
4. If rand%2500 < sum and best exists: log "%s sends a missionary to #%d (%d : %d )" (0x633634);
   post GAINMONEY (0x5f, category 7) of cost 0x45c770(best) + 1 to U; send 0x43f870(best).

So at most one missionary per month, with probability sum/2500 (e.g. 10 heretic cities outside
Europe with chance > 50: s = 7 each, 70/2500 = 2.8 % per month). The AI is given the cost before
sending, so missionaries are free for it; no treasury test.

---

## 4. Defects and quirks to reproduce (or knowingly fix)

- Colonist plan timers compare months without years: the 15-month cancel and the 11-month money
  cheat can never fire (§2.7). The trader plan's > 6 month test is reachable but also year-blind.
- Merchant plan: money not granted by the next agent think -> cancel (+0x4c unset) and immediate
  re-roll; a merchant also cancels when can-send fails at the end (e.g. monopoly limits).
- The capital's CoT can be in the roulette twice (§1.3 step 2).
- New-site scores subtract the trader cost for colonist plans; distance to the capital is computed
  and dropped (§2.4).
- Trade-agreement score -1 is added to the CoT value instead of excluding the candidate (§1.5).
- `expansion` only sets the expand-vs-new threshold k; one colonist/trader plan at a time.

## 5. Port summary (decision procedure per AI country)

1. Every 30-59 days (merchants): if no merchant plan and rand%100 <= traders: roulette over CoTs by
   §1.3.1; reserve money cost+1 and one merchant; send when both are granted and can-send holds;
   on any end, roll again. Not at war, >= 5 merchants/yr, no embargo: offer a trade agreement to
   the best CoT owner, at most once per owner per 6 years.
2. Every 20-39 days (colonists/traders): if expansion > 0 and a colonial power, and no colonist/
   trader plan: pick expand-vs-new (§2.3), the site (§2.3-2.4) under the AI filters (§2.6), the envoy
   (§2.5); reserve money and a colonist; send when granted.
3. Monthly (missionaries): §3.

## 6. Open points

- Tag 0x52 in §2.6 step 3 (row 82 = NAT in country.csv, but NAT is 0x152 in the exe; the tag
  numbering of the special rows is NIEZNANE) and province 356's role.
- U+0xfec (Russia's unrestricted colonisation), 0x4977c0(P+0x22), world byte [0x8d0d7a].
- Listener events 0x10/0x13/0x17/0x23 and the exact events behind slots 0x74/0xa4/0xac.
- When the King actually grants pool +0x82 to the trade-agreement plan (and whether a waiting
  plan blocks merchants for long) - NIEPEWNE.
- Whether the AI class 0x5f55ec is used for human countries too (then the Tordesillas/religion
  filters of §2.6 would also limit the player's colonist map mode) - NIEPEWNE; SPEC_AI calls it
  the country vtable.
- K vfunc 0x1ec (called by the money cheat) not decoded.
