# The AI's exploration: explorer, conquistador and "genocider" plans (eu2.exe v1.09)

Specification only, no decompiled code. Evidence = addresses in `eu2.exe`. **NIEPEWNE** = believed,
not proven; **NIEZNANE** = not found. U = the AI country, K = its King (plan vfunc 0xdc returns it;
K+0xa = U, K+0x40 = strategic mode, 2 = COLONISE). A = the colonial agent 0x53b6c0 (vtable
0x5ef518, SPEC_AI_ENVOYS §2). Dumps: `C:\temp\amiga_eu2\sr\ax_*.txt`, scripts in
`C:\temp\amiga_eu2\re\agent_explore\`.

Helpers used below: prov+0xca terrain (>= 5 sea, 6 river), +0x10a area, +0x10e region (REG_* order
of text.csv, North America = 1), +0x106 continent (2 = Europe), +0xb6 base tax, +0x124 colonisation
difficulty, +0x12c natives, +0x3c4 owner, +0x114/+0x11c neighbour count/array, prov[0x256+tag]
"known to tag". `0x43a820(capital, P)` = Euclidean map distance (SPEC_SMALLRULES). `0x55dd10(b, P)`
= b + 50 if P's area is in the .ai `area` list + 25 if its region is in `region` + 10 if its
continent is in `continent`. Country U+0x122e / +0x1236 = warships / transports.

## 1. The three plan types

All three are children of A. Plan base: ctor 0x54b4b0 (+0x3c controller, +0x40 root = itself,
+0x44 score), plus a unit list at +0x76 (vfunc 0x194 for armies / 0x1a4 for fleets returns it,
0x53d7b0). Score after creation is a fixed **priority** (it decides who wins units in the King's
queues, SPEC_MILITARY_AI §4).

| type (vf 0x1c8) | vtable | name in logs | target list | unit | priority (+0x44) | creator |
|---|---|---|---|---|---|---|
| 0x32 (0x53d710) | 0x5ef8fc | explorer | L68 = unknown sea zones | one fleet | 25.0 (0x53c495) | 0x53c260 |
| 0x33 (0x53d7a0) | 0x5efae8 | conquistador | L56 = unknown land next to known land | army(ies) | 52.0 (0x53c738) | 0x53c520 |
| 0x34 (0x53d7e0) | 0x5efcd8 | "genocider" (KillNatives) | L8c = native provinces, aggressiveness > 4 | army(ies) | 51.0 (0x53c9a2) | 0x53c800 |

Type 0x34 is **not exploration**: it sends an ordinary army to exterminate natives (SPEC_NATIVES
§5.3). It lives in the same agent and shares the conquistador's movement code (vf 0x198 = 0x553980),
so it is specified here too.

Vtable slots that differ (from a slot-by-slot compare of the three vtables):

| slot | 0x32 sea | 0x33 land | 0x34 natives | meaning |
|---|---|---|---|---|
| 0xc0 | 0x553300 | 0x553830 | 0x553e00 | start |
| 0x1c | none (0x514d70 = `ret 4`) | 0x553bc0 | 0x553ea0 | think (timer) |
| 0x1a8 / 0x198 | 0x5533a0 (fleet) | 0x553980 (army) | 0x553980 (army) | unit offered |
| 0x24 | 0x5534c0 | – | – | unit event, fleet (NIEPEWNE: "unit moved/arrived") |
| 0x2c | 0x53d720 = cancel | – | – | unit event (NIEZNANE which; plan cancels) |
| 0x20 | – | 0x553b10 | 0x553b10 | unit event, army (NIEPEWNE: "unit moved/arrived") |
| 0x28 | – | 0x553b80 | 0x553b80 | unit event "is stopped" |
| 0x1ec | – | 0x553c90 | 0x553f30 | "arrived at the target" action |

## 2. When plans are created

### 2.1 Gates (A's think 0x53bbc0, every 20 + rand%20 days, not AI-speed adjusted)

1. Nothing if .ai `expansion` (prefs+0x16) == 0 (0x53bbec; a negative value passes).
2. Count own child plans by type through the jump table 0x53bd20/0x53bd38 (type − 0x17):
   0x17/0x19 colonist/trader (also updated on the way), 0x32 -> n32, 0x33 -> n33, 0x34 -> n34.
3. c = 1 if U is a colonial power (0x43a970: `fantasy` byte 0x8d0d7a, or colonists/yr 0x4582d0
   >= 0.99, or explorers U+0xfe8 > 0, or conquistadors U+0xfe4 > 0); else c = 1 if colonist
   stock U+0xe94 + colonists/yr > 1.0; else **stop** (0x53bc75..0x53bcaf).
4. m33 = min(5, trunc(|L56| / 5) + 1) (0x53bcb5..0x53bcd7).
5. In this order, at most one creation of each kind per think:
   colonist/trader if fewer than 1 (0x53c9f0, SPEC_AI_ENVOYS); **0x32 if n32 < 1**; **0x33 if
   n33 < m33**; **0x34 if n34 < 2** (0x53bcdc..0x53bd0e). So at most 1 sea, 5 land, 2 native plans,
   and the land count grows by at most one per think.

At agent start (0x53b960, after filling the lists, 0x53bb46..0x53bb7a): if 0x43a970 is true **and**
`expansion` > 0, it calls 0x53c260, 0x53c520, 0x53c9f0, 0x53c800 once each, unconditionally.

Extra trigger: A's slot 0xac (0x53c210, args country X, k): if X is U, k == 3 -> 0x53c260 (sea
plan), k == 2 -> 0x53c520 (land plan), with no count limit. NIEPEWNE: k is a leader type, i.e. this
is "U got a new explorer (3) / conquistador (2)"; the sender was not found (NIEZNANE).

No other gate: no war test, no money test, no tech test, no test that U owns a unit able to explore
(that is left to the King's request, §3). Every creator also returns at once if A is not started
(A+0xe byte).

### 2.2 The lists (kept by A, SPEC_AI_ENVOYS §2.1)

- Start (0x53ba46..0x53bb40): for every province P (0..2019): P unknown and area (+0x10a) == 0x94
  -> L68; P known and not a river -> each unknown land (+0xca < 5) neighbour -> L56.
- Discovery by U (slot 0x74 = 0x53c120): rivers ignored; land: classify if unowned, remove from L56;
  sea: removed from L68 **only if naval tech >= 18** (0x432e30); then every unknown land neighbour
  joins L56. So below naval tech 18 an explored sea zone stays in L68 (literal; it then scores low,
  §2.3).

### 2.3 Sea plan 0x32 (creator 0x53c260, value 0x553570)

Gate: Russia (country index 7) without the byte U+0xfec gets **no sea plan** (0x53c28e).
For each P in L68, skipping P if an active 0x32 plan already targets it (0x53c349..0x53c3af),
V(P) is computed into a template plan; the strictly highest V wins (start −1e7, first wins ties).
The winner gets a new plan (0x8a bytes), target P, priority 25, A.addChild (vf 0xf8), start (vf 0xc0).

**V(P), 0x553570** (float):
1. n = number of neighbours of P (land or sea) unknown to U.
2. If n == 0: V = 0 + bonus B (step 7), nothing else.
3. s = 20 if P itself is unknown (it normally is), else 0.
4. For each unknown neighbour N: w = N.base tax (+0xb6); w = 2w if N is owned by a country other
   than U; w = w − trunc(N+0x124 / 5); if w < 1 then w = 1; s += 0x55dd10(w, N) (= w + the .ai
   area/region/continent bonus 50/25/10 of N).
5. v = n + s; d = distance(U's capital, P); if d >= 1: v = v / d. v = 40·v.
6. Region bonus, only if `fantasy` is off and U's capital is on continent 2 (Europe), by P's region
   (+0x10e; jump table 0x553800 / index bytes 0x55380c, region − 26):

   | region of P | bonus |
   |---|---|
   | 26-29 Alaska Sea, East Pacific, SW Pacific, SE Pacific | 0 |
   | 30-47 Hudson Bay, NW Atlantic, Caribbean Sea, W/SW/N/SE/E Atlantic, Tago Sea, Greenland Sea, Arctic, Baltic, Biscay, North Sea, W/E Mediterranean, Red Sea, Persian Gulf | **+100** |
   | 48-52 SW/W/SE/NE Indian Ocean, Thai Sea | +10 for Portugal (tag index 5) only, else 0 |
   | 53-59 Southern Pacific, Barrier Reef, W Pacific, Chinese Sea, Sea of Japan, N Pacific, NW Pacific | 0 |
   | anything else (< 26 = land regions, 60 Lake, ...) | **+100** |

7. V = v + 25 if K's mode is COLONISE (K+0x40 == 2), else v + 1 (0x5537c0..0x5537f1).

Because d is in map pixels (hundreds to thousands), 40·(n+s)/d is small; for a European country the
+100 decides: the AI explores the Atlantic, Mediterranean and northern seas first, among them the
zone with the most unknown, rich, foreign-owned or .ai-preferred neighbours per distance. Non-European
AIs rank purely by that ratio.

### 2.4 Land plan 0x33 (creator 0x53c520, value 0x553ce0)

Loop over L56 like §2.3 (same −1e7 start, strict >). **Bug, literal:** the "already targeted" test
compares with type **0x32** (0x53c62d), so it never matches a land plan; since V is deterministic,
consecutive land plans pick the **same** province until it becomes known (port: keep, or skip
provinces already targeted by a 0x33 — decision for the port, mark it).

**V(P), 0x553ce0:**
1. If U has no warships and no transports (U+0x122e + U+0x1236 < 1) and P's continent differs from
   the capital's: V = −1 (0x553d15). Note −1 still beats the start value, so such a P is chosen when
   nothing else exists (literal).
2. `fantasy` on: V = 100 / max(1, d) + c, d = distance(capital, P).
3. Normal game: V = max(1, 0x55dd10(1.0, P)) + c = 1 + .ai preference of P + c.
4. c = 25 if K+0x40 == 2 (COLONISE) else 1.

So in a normal game **distance does not count**; the land target is the first L56 province (list
order, NIEPEWNE: insertion order) with the highest .ai area/region/continent preference.

### 2.5 Natives plan 0x34 (creator 0x53c800, value 0x554000)

No duplicate test. Over L8c (A+0x8c, head +0x90), every P with natives (+0x12c) > 0 gets the weight
w = max(1, 100 − natives); total W; r = rand % W; walk the list subtracting weights, the first entry
where r goes negative is the target (0x53c8ee..0x53c916). Plan created with target P, 0x554000
evaluated (−1 if no ships and P off the home continent, else natives; unused), priority 51, add, start.
Fewer natives -> more likely target.

## 3. How a plan gets its unit

No plan creates a leader, recruits an explorer or attaches a free leader to a unit (no such call in
0x553300..0x554050). They **ask the King** for a unit:

| plan | start posts | args (power, w1, w2, province, kind) |
|---|---|---|
| 0x32 | fleet request K vf 0x1e0 = 0x545ee0 (queue 6) (0x553340) | 1.0, 0, target sea zone, kind **3** |
| 0x33 | army request K vf 0x1dc = 0x545e00 (queue 5) (0x553903) | 4000.0, 1.0, 1.0, S, kind **2** |
| 0x34 | army request (0x553e74) | 150 × natives(P), 1.0, 1.0, P, kind **0** |

S (0x553830..0x5538d5) = the last neighbour of the target that U knows and that is land; if none,
U's capital. It is the point the candidate armies are sorted by distance to.

**Serving** (armies 0x546c40, fleets the twin at ~0x5471f0; SPEC_MILITARY_AI §2.3): U's free units
are listed, those already held by a plan whose root priority >= the requester's − 1 are skipped,
then sorted by squared map distance to the request province (+250000 other connected continent,
+1e6 unconnected, +10000 if it already serves a plan) and offered in that order until the remaining
power < 0.5. The **kind** filter decides who explores (0x546d30..0x546d62, 0x547308..0x54733a):

- kind 2 or 3: only units that **can explore** (0x48c0e0): pirates; an army with land tech >= 31
  (0x432d90), or `fantasy`, or unit+0x8c in {2,3}, or any leader of type 2 or 3 aboard; a fleet with
  naval tech >= 27 (0x432e40), or `fantasy`, or +0x8c in {2,3}, or any type 2/3 leader.
- any other kind: units with a type 2/3 leader (or +0x8c 2/3) are **excluded** (0x48c1f0).
- fleets additionally need strength (vf 0x84) >= 0.1 when the request's +0x24 is 0.
- kinds 2 and 3 never trigger recruitment (0x547040 builds only for other kinds); kind 0 may create
  a BuildArmy for the 0x34 plan.

So the leader comes **with the unit**: the AI uses whatever army/fleet already carries a
conquistador/explorer (created by events, SPEC_EXPLORERS §2.3: conquistador = new 500-man army,
explorer = new 1-ship fleet), nearest first. Such units are never used for war or garrisons, never
merged (SPEC_EXPLORERS §5), so they wait for an explore plan. Which of several leaders is used is
not chosen at all: it is whichever unit is nearest. A conquistador in a fleet or an explorer in an
army also qualifies (0x48c0e0 tests "type 2 or 3" for both).

**Accepting** (offer = vf 0x198 / 0x1a8):
- Sea, 0x5533a0: refused (vf 0x17c, the unit goes into the plan's rejected list +0x28, NIEPEWNE
  from 0x558c7f) if the plan already has a fleet or a child plan; else 0x559f60 orders the fleet to
  the target (direct path, 0x55a010). Route found: "%s sends an explorer towards %s(%d)" (0x553481),
  or, if the fleet already is in P, "%s has now sea explored" + finish. No route: rejected.
- Land, 0x553980 (also 0x34): sets +0x74 = 1 ("has had an army"). Direct land route 0x55a010:
  "%s sends an conquistador towards %s(%d) with a %s plan" (0x553ac7). Else **by sea**: 0x55a320
  creates a TRANSPORT plan (SPEC_MILITARY_AI §7: needs enough ships, the King is asked for a fleet
  K vf 0x1e0, the army walks to the embarkation zone, is shipped and unloaded at the target):
  "...and we do it with TRANSPORTS towards..." (0x553a86). Neither: rejected. Land plans do **not**
  refuse a second army: with power 4000 asked, the King keeps offering explore-capable armies until
  their summed strength (0x486820) covers it, and each accepted army is sent separately
  (NIEPEWNE how many that is in practice; the strength scale was not measured).

## 4. Movement, timers and end conditions

Paths use the normal search with the terra incognita rule (SPEC_EXPLORERS §3): an unknown province
can be entered only by units that can explore; the fleet search never routes **through** an unknown
sea zone, so a sea target must touch known water to be reachable. Each province entered becomes
known (SPEC_EXPLORERS §4); fleets in a sea zone may also reveal adjacent land.

| | sea 0x32 | land 0x33 | natives 0x34 |
|---|---|---|---|
| start | log "%s starts an %s plan for %s(%d)" | same log; first think in **30 + rand%30** days | first think in **10 + rand%15** days |
| periodic think | **none** | every 30 + rand%30 days (0x553bc0) | every 20 + rand%10 days (0x553ea0) |
| unit arrives at target | finish, "has now sea explored" (0x5534c0) | vf 0x1ec: "has now land explored", finish (0x553c90) | vf 0x1ec: KillNatives (below) |
| unit elsewhere and not moving | cancel, no log (0x553537) | "plan #%d unit #%d isnt moving, we cancel", finish (0x553b5b) | think: "genocider #%d isnt moving, we cancel" (0x553f0f) |
| "stopped" event (slot 0x28) | – | "is stopped, we cancel", finish (0x553b96) | same |
| slot 0x2c event | cancel (0x53d720) | – | – |
| think-time cancels | – | (a) rejected list +0x28 not empty; (b) target now known; (c) +0x74 set and no army and no child left (0x553bdc..0x553c59) | none |

Finish = 0x5584b0 (+0x10, release vf 0xb4, parent slot 0x10c), cancel = 0x5584e0 (+0x0f, release,
parent slot 0x110). A's handler for both (0x53c7c0) reacts only to colonist/trader plans, so a new
explorer plan comes only at A's next think (20-39 days). The released unit goes back to the King's
free stock (vf 0xb4 = 0x558030, NIEPEWNE exact path) and **stays where it is**; it is offered again
to the next explore request, nearest first. Leaders are not touched (they die of age, SPEC_EXPLORERS
§5).

Corrections to SPEC_EXPLORERS §6: "an explore plan lasts 30-59 days" is the land plan's **think
interval**, not a lifetime; a land plan with a moving army and an unknown target survives every
think. The sea plan has no timer at all.

NIEPEWNE consequence (literal reading): a sea plan whose target cannot be reached rejects every
fleet and, having no think, never ends unless the slot 0x2c event fires; n32 then stays 1 and **no
other sea plan is created**. Same for a 0x34 plan that never gets an army (its think does nothing
with an empty unit list).

**KillNatives** (0x553f30, unit u at the target): if 0x45fa20(u) (u not moving, u+0xf4 == 0 i.e. not
in battle, natives > 0): log "%s will now KillNatives in %s(%d)", 0x443450 = REQUEST_NATIVE_ATTACK
(SPEC_NATIVES §5.3); the plan stays. Otherwise: natives < 1 -> "genocider #%d is done", finish;
else "cancels #%d, can't kill natives (not anyone around)", finish.

## 5. Units without a conquistador/explorer

Yes, for the AI they explore once the tech allows: kind 2/3 requests accept **any** army once land
tech >= 31 and **any** fleet once naval tech >= 27 (0x48c0e0); the King picks the nearest free one.
Below those levels only units carrying a type 2/3 leader (or flagged unit+0x8c 2/3, where that flag
is set is NIEZNANE) are taken, and without such a unit the request simply stays open: the sea plan
waits forever (no think); the land plan waits too (cancel (c) needs +0x74, which is set only when an
army was offered) — NIEPEWNE that it is ever cancelled before an army appears. Naval tech >= 18
also lets any fleet in a sea zone reveal all adjacent land (SPEC_EXPLORERS §4.2), independent of
plans.

## 6. Port summary

### 6.1 Per AI country, every 20 + rand(20) days (colonial agent think)

1. If expansion == 0: stop.
2. Count own plans: n32 (sea), n33 (land), n34 (natives); update colonist/trader plans.
3. If not colonial (fantasy / colonists per year >= 0.99 / any explorer / any conquistador) and
   colonist stock + colonists per year <= 1: stop.
4. Colonist/trader plan if none (SPEC_AI_ENVOYS §2.3).
5. If n32 == 0 and not (RUS without its flag): target = argmax over L68 not already targeted of
   Vsea (§2.3); create sea plan (priority 25), request one explore-capable fleet (kind 3) for it.
6. If n33 < min(5, |L56|/5 + 1): target = argmax over L56 of Vland (§2.4); create land plan
   (priority 52), request explore-capable armies, power 4000, sorted by distance to a known land
   neighbour of the target (else the capital).
7. If n34 < 2 and L8c has native provinces: weighted random target (weight max(1, 100 − natives));
   create natives plan (priority 51), request a normal army (kind 0, power 150 × natives; may recruit).

At agent start (expansion > 0 and colonial): one of each, without the counts. On the (NIEPEWNE)
"new explorer/conquistador" event: one sea / land plan.

### 6.2 Plan life

- Unit offered: sea plan takes one fleet; land/natives plans take every army offered. Order a direct
  path; land plans fall back to a transport; if no route, reject the unit (remember it, never
  re-offered).
- Sea: on a fleet event, at target -> done; not moving -> cancel. No timer.
- Land: event at target -> done ("land explored"); not moving or stopped -> end. Think every
  30-59 days: end if any unit was rejected, if the target is known, or if it had an army and has
  none left.
- Natives: think every 20-29 days and on arrival: at target and idle -> native attack; natives
  gone -> done; unit busy elsewhere -> end.
- Units go back to the free pool where they stand; the next plan takes the nearest.

### 6.3 Fixed-point notes

Vsea needs a division by distance and ×40; keep it as (n+s)·40·256/d in 32-bit (s < ~10^4,
d >= 1). Vland and the natives weights are integers already. Priorities 25/51/52 can be integers.

### 6.4 Open points

1. Sender and meaning of A's slot 0xac (0x53c210, k = 2/3) — NIEZNANE / NIEPEWNE "new leader".
2. Which events drive plan slots 0x20/0x24/0x28/0x2c (NIEPEWNE: unit arrived / stopped / removed).
3. Whether an unreachable sea target really blocks sea exploration forever (no think, §4).
4. Strength scale of 0x486820 vs. the 4000 asked: how many conquistador armies one plan grabs.
5. +0x28 = "rejected units" list: inferred from the reject path 0x558c7f and the unit back-list
   unit+0xa2 (0x48b20f); NIEPEWNE.
6. The land creator's duplicate test against type 0x32 (bug) — reproduce or fix in the port.
7. Where unit+0x8c is set to 2/3 (NIEZNANE, as in SPEC_EXPLORERS).
8. Order of L56/L68 lists (tie-break) — believed insertion order (0x40f360), NIEPEWNE.
