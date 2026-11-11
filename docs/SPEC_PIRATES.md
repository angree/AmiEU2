# Pirates (country PIR, index 0x151) — EU2 v1.09

Clean-room specification read from `eu2.exe` (objdump, VAs). Prose and tables only.
**NIEPEWNE** = believed, not proven; **NIEZNANE** = not found. Work files: `C:\temp\amiga_eu2\sr\pi*.sh`,
dumps `pi_spawn.txt`, `pi_mon.txt`, `pi_seaev2.txt`, `pi_w84.txt`, `pi_blk.txt`, `pi_cf.txt`, `pi_ldr*.txt`.

Conventions as in `docs/SPEC_SMALLRULES.md`. `rand()` = the CRT `0x5ba99c`. `ftol` = `0x5b98fc`
(truncation toward zero). "Host" = `0x4196e0()` (always true in single player).

**Correction to SPEC_BATTLE_MERC §4.7:** the spawner is **not monthly**. It runs on the PIR tick
at a random interval of 25..73 days (§1.1).

---

## 0. The PIR country object

- Class: constructor at `0x43b955..0x43b996`, vtable `0x5e6a0c`. Object size 0x2136: the base country
  (0x212e bytes) plus two PIR fields:
  - **+0x212e** next spawner date (date record, set to 0 by the constructor);
  - **+0x2132** province id of the last spawn (int).
- Overridden vtable slots (all others are the base country's, base vtable `0x5e5ac0`):

| slot | base | PIR | meaning |
|---|---|---|---|
| 0x00 | `0x435540` | `0x43bbe0` | destructor |
| 0x14 | `0x436060` | `0x55bf70` | post-load init (§1.4) |
| 0x260 | `0x450ad0` TICK_LOOP | `0x55c110` | tick: spawner + mover, then the base tick (§1.1) |
| 0x268 | `0x451300` monthly | `0x55bfa0` | tech update, then the base monthly (§1.5) |

- PIR has no AI controller and no provinces. If it is missing at start-up, it is created
  (`0x43b790(0x151)` at `0x419e16`). Map style "northam" (`0x49e3be`, unit sprites).
- **Always hostile:** `0x43b6e0` (hostility test, used by blockade, the military AI, etc.) returns
  true whenever either side is tag 0x151..0x153. No war declaration is involved. DIPWAR `0x45d520`
  refuses PIR as a target (`0x45d545`), so nobody can declare war on PIR (the hostility is already
  there).
- **Terra incognita:** PIR units may enter unknown provinces (`0x48c0e9` and `0x48c785` in
  `0x48c640`, SPEC_EXPLORERS §3.1 rule 1).
- **No attrition:** the unit upkeep loop `0x4de5a0` skips the attrition block for PIR units
  (`0x4de63c` jumps over the `vfunc 0x34` / `0x48e660` / `0x4d1fd0` part). The later part of the same loop
  (command 0x37, believed morale recovery, NIEPEWNE) applies to PIR as well.

---

## 1. When things run

### 1.1 The tick (`0x55c110`, slot 0x260, every simulation tick)

1. If host **and** today > the next spawner date (+0x212e) (`0x417220`, strict "after"):
   1. the **spawner** `0x55c300` (§2), which also schedules the next date;
   2. the **mover** `0x55c160` (§3).
2. Always: the base TICK_LOOP `0x450ad0` (bankruptcy check etc.).

So the spawner and the mover run together, once per spawner interval. Pirate fleets get a **new
move order only once every 25..73 days**. The movement itself then takes the usual travel days.

### 1.2 The spawner interval

At the start of every spawner run (before any other test):
`next = today + 25 + rand()%25 + rand()%25` days (`0x55c32e..0x55c351`, `0x417130` adds days).
That is 25..73 days, triangular, mean 49.

### 1.3 First run

The constructor sets the date to 0, so in a new game the first tick on the host runs the spawner and
the mover at once. Whether +0x212e is saved is NIEZNANE. The post-load init covers the saved-game case (§1.4).

### 1.4 Post-load init (`0x55bf70`, slot 0x14)

Host only: last spawn province (+0x2132) := 0. If the loaded scenario is a saved game
(`[0x8ff63c]+0x2cc4`, set by the scenario keyword `saved`, token 0x1d5, `0x4a716e`), the spawner
`0x55c300` runs once immediately (without the mover).

### 1.5 PIR monthly (`0x55bfa0`, slot 0x268): the pirate naval tech

For every real country X (`0x45bce0`), in index order:
- `allMax` = max over all real X of naval tech level (`0x4333d0` → tech block +0x24), starting at 0;
- `eligMax` = max over the "eligible" X, **starting at 21**. X is eligible if the fantasy flag
  `[0x8d0d7a]` is set, or X's state religion (+0x254) is 0..6 (Christian and Muslim; pagan 7 and the
  eastern 8..10 are not eligible).

PIR naval tech := max(0, min(eligMax, allMax) − 2). If the value changed, `0x433b60` refreshes it.
PIR land, infrastructure and trade tech := 0. Then the base monthly `0x451300` runs.

Effect: pirates sail at 2 levels below the best Christian/Muslim navy, but never above (best
navy of all − 2), and at least 19 when somebody has 21+.

---

## 2. The spawner (`0x55c300`)

### 2.1 Order of operations and random draws

1. Schedule the next date (2 draws, §1.2).
2. Chance c (percent), from the current year y:
   - y < 1680: **100**;
   - 1680 ≤ y < 1730: **(1730 − y) × 2**;
   - y ≥ 1730: **1**.
3. n = PIR's number of units (the count of the unit list at PIR+0x1248, all PIR units). If
   n ≥ maxFleets (§2.2): stop (no further draws).
4. If rand()%100 ≥ c: stop.
5. Choose the sea zone (§2.3; one draw per scored zone with a positive raw score).
6. Post CREATE_FLEET 0x8c for PIR in that zone: **warships = 2 + rand()%3** (payload +0x22 → unit
   +0xe8 warships; galleys and transports 0). No leader (empty strings). Executed at once (`0x4b8850`).
   Province +0x2132 := the chosen zone.

### 2.2 Maximum number of pirate units (`0x55c4b0`)

Over every slot of the country table `0x64e140..0x64e68c` (special countries included, PIR too):
- W = Σ country vfunc 0x394 (warship count, per SPEC_MILITARY_AI; NIEPEWNE which ship type);
- S = Σ country+0xfd2 (number of shipyards).

base = S + W/100 (integer division, truncated).

| current year | maxFleets |
|---|---|
| < 1550 | base / 2 |
| 1550..1599 | base + 1 |
| 1600..1699 | 2·base + 2 |
| 1700..1769 | max(base / 2, 2) |
| ≥ 1770 | max(base / 4, 1) |

(All divisions are signed and truncated.) The cap counts **all** PIR units (§6.2).

### 2.3 Choosing the zone

Loop over province ids 0..0x7e3 in order. A candidate is a province with region (+0x10a) **0x94
("Sea")** and id ≠ the last spawn province (+0x2132). Its score is `score(zone)` (§2.4, includes a
random draw). Keep the first zone with the strictly highest score. The best score starts at 0, so
only score > 0 counts. If no zone scores > 0, the zone is **969 (Tortuga Island)** (the default, even if
969 was the last spawn province or contains fleets).

### 2.4 Zone score (`0x55c570`, ecx = PIR, arg = sea zone Z)

A running integer s = 0. For every neighbour record i of Z (Z+0x114 count, Z+0x11c list, 12-byte
records), in order:

1. If Z's unit list (Z+0xae) is **not empty** (any unit of any country, pirates included): this
   neighbour adds nothing (go to step 9). Since this test does not depend on i, **any unit in Z makes
   the score 0** (text HINT_42 "Pirates will not appear in sea zones containing regular fleets").
2. N = the neighbour province. If N has no owner (+0x3c4 = 0): skip (step 9).
3. s −= 8 × Z+0x84. (Z+0x84 is the province "pirate level" field of §4.1. For a sea zone it is
   never filled because a sea zone has no controller, so this term is believed to be always 0. NIEPEWNE.)
4. inc = ftol(`0x47d970`(N)) (province income value, §4.3); f = fortress level `0x47f3e0`(N)
   (trunc city+0x3a, 0 without a city).
   s += inc / 2 if f = 0, else s += inc / (f + 4) (integer divisions).
5. s += ftol(`0x480e30`(N)) (N's monthly trade value, already reduced by pirates, §4.2).
6. If N has a trading post (+0x40c): **s ×= 3**.
7. If N's continent (+0x106) is 2 (Europe): **s /= 2**.
8. If N has a port object (+0x41c) whose unit list (+0x30) is not empty (ships in port): **s /= 5**.
9. If s < 0: s = 0.

Steps 6..8 apply to the **running total**, not to this neighbour's part only (quirk; reproduce it).

After the loop: if Z's id is one of **965, 966, 967, 969, 1009, 1043..1048, 1185, 1188, 1325, 1378**
(Florida Straits, Jamaican Sea, The Bahamas, Tortuga Island, The Dodecanese, Gulf of Darien,
Caribbean Sea, Venezuelan Sea, Straits of Puerto Rico, Leeward Islands, Winward Islands, Rufiji
Delta, The Comores, Diego Suarez Bay, Lesser Antillas): **s ×= 2** (jump table `0x55c6e8/0x55c6f0`
plus the compares at `0x55c692..0x55c6c5`).

Result: if s > 0, **rand() % s** (one draw), else s (= 0).

So pirates prefer rich, unfortified, non-European, colonial (trading-post) coasts with empty
harbours, in the Caribbean and East African hot spots. Every candidate zone gets its own random draw.

---

## 3. Pirate behaviour: the mover (`0x55c160`)

For each unit in PIR's unit list (PIR+0x124c, in list order):

1. Skip it if its busy byte +0x53 ≠ 0 (moving / on a path, NIEPEWNE).
2. Skip it if it is in a combat (+0x48 or +0x4c set), **unless** the combat is a siege
   (`0x48df10` type 3 or `0x48df30` type 4).
3. P = its province (+0xc). k = rand() % P.neighbourCount. T = neighbour k.
4. a = score(T) + 1, b = score(P) + 1 (§2.4; each score draws once if positive).
5. If T is in region 0x94 and a > b: order a move to T (path object vtable `0x5e5a28`, `0x490790`
   adds T, vfunc +0x10 executes).

P always contains the pirate fleet itself, so score(P) = 0 and b = 1. **The rule therefore is: pick
one random neighbour. Move there if it is an empty open-sea zone (no units of anybody) whose score
draw is ≥ 1. Otherwise stay.** Pirates never enter land or ports, never move into a zone with any fleet
(they do not hunt), and drift towards rich coasts only through the weight of the score draw.
Port note: rand() % 0 would fault if P had no neighbours; guard it.

**Combat.** Pirates have no AI. Fights happen only because every fleet is hostile to them
(§0). Another country's fleet entering a pirate zone, or a fleet already there, starts a naval
battle with the generic rules (SPEC_WAR_DIPLOMACY §12; the start code is generic, NIEPEWNE). No
pirate-specific code was found in combat, retreat or pursuit. The military AI counts PIR fleets as
enemies (SPEC_MILITARY_AI, the `0x43b6e0` notes), so AI navies may attack them.

**Morale.** A new PIR fleet starts at full morale: for a non-real owner the creator `0x4381d0` sets
+0x58 = `0x438850`(PIR) = PIR's maximum naval morale (SPEC_WAR_DIPLOMACY §8.2, with PIR's naval tech
of §1.5 and PIR's default sliders; the value in numbers is NIEZNANE). Real countries start at 0.5
instead.

**Despawn.** No timer, no disbanding, no attrition (§0). A pirate fleet disappears only when it
loses all ships in combat (`0x4d1fd0` removes units at 0 ships). The only limit is the spawn cap
(§2.2), which counts living fleets. The search covered all `0x151` and `[0x64e684]` references in the exe.

---

## 4. Effects on the economy

### 4.1 The pirate level of a land province (+0x84) (`0x47e710`)

For a province L, refreshed:
- when a fleet enters or leaves a sea zone that has the flag +0x488, and the fleet belongs to PIR:
  for every neighbour of that zone (`0x47c7cf..0x47c817` add, `0x47c8de..0x47c926` remove);
- when a fleet's state or ship count changes (`0x47e680` from `0x48b71d`, `0x48ba00`, `0x48d83f`,
  `0x48d9cb`; NIEPEWNE exact triggers: combat start/end, losses).

Rule: L+0x84 := 0. If L has a coastal sea zone (+0x3d6 ≠ 0) and a controller (+0x3c8 ≠ 0): for every
unit in that zone's unit list that is PIR-owned and **idle** (+0x48 = 0, +0x4c = 0, +0x53 = 0):
L+0x84 += ceil(warships +0xe8) (`0x5bc2a6` = ceil, NIEPEWNE, then ftol).
Only the one zone at +0x3d6 counts, even if L touches several sea zones. Fighting or moving pirates do
not count.

Let **P = L+0x84** (pirate warships off L's coast).

### 4.2 Trade (real money)

Province monthly trade value (`0x480e30`, copy in the CoT share `0x480ea0` at `0x480f21`):
`V = max(0, F × marketPrice / 12 − 0.5 × P)` (SPEC_TIME_ECONOMY §5.3). **Every pirate warship off
the coast removes 0.5 ducat per month** from the province's trade value, down to 0. The same
deduction appears in the CoT attractiveness `0x47f400` (at `0x47f56a`), so pirates also make the CoT
less attractive.

### 4.3 Income value and tooltip

- `0x47d970` (province "income value": base tax + tolls + manufactory, used by the AI, map mode,
  peace VP and the pirate score): **− 0.5 × P, floored at 0, only if the province has no fortress**
  (no city, or fortress level trunc(city+0x3a) = 0) (`0x47db28..0x47db5d`).
- The province income tooltip `0x47ce40` adds the line **PIWINC_PIRATES** "-%d$ Pirates\n" with
  %d = P / 2 (integer). It is shown only if P ≠ 0 and the province has no fortress (`0x47d226..0x47d295`).
  The second tooltip `0x47d5d0` shows the same line whenever P ≠ 0, without the fortress test
  (`0x47d61b..0x47d6a9`).
- Province **tax** (`0x47e270`) does not read P. Pirates reduce tax only through a blockade (§4.4).

### 4.4 Blockade (`0x47e7c0`)

A port province (+0x41c, with a controller) is blockaded (+0x489 = 1) when, over the idle fleets
in its sea zone (+0x3d6) that are hostile to the controller (`0x43b6e0`, so **pirates always
count**): Σ ceil(warships) + 0.5 × Σ ceil(galleys) ≥ 5. (The tech test `0x522960` is a stub that
always returns true.) One spawned pirate fleet (2..4 ships) cannot blockade on its own. Two fleets
in one zone, or a 5-ship privateer fleet (§5), can. The blockade effects are the generic ones
(tax × 0.5, siege starvation, ...; SPEC_TIME_ECONOMY §5.1, SPEC_WAR_DIPLOMACY §12). Change messages:
`0x55aac0` and the log `0x61f970`.

### 4.5 Sea-zone window (`iw_sea`, SPEC_UI §4.7)

- Pirates present (any PIR unit in the zone's unit list): **SEA_PIRATES** "%d pirate vessels in
  this area." with %d = Σ ships of the PIR units in the zone, plus up to 8 PIR small shields.
  Otherwise **SEA_NOPIRATES** "No pirates in the area".
- The coast-owner shields above them are from SEA_NATIONAL (unrelated to pirates).

---

## 5. Commissioning privateers (sea-zone window button SEA_PIRB)

Button "Commission Privateers" (`0x506fc5..0x507482`). It is enabled unless one of these holds:
- cost > treasury;
- a PIR unit is in the zone (tooltip SEA_ALREADY "There are already pirates in this seazone.");
- player+0x1f84 = 0 (SEA_NOTNAVAL "We are not a naval power so we can't commission privateers.").

Otherwise the tooltip is SEA_PRIV "It will cost %d$ to commission ... may trigger a Casus belli
from affected nat...". No war or tech requirement.

**Cost** `0x45b2a0` = ftol(25 × (1 + inflation)), inflation = country+0xe84 floored at 0.

On click, in this order:
1. **CREATE_FLEET 0x8c for PIR** in the selected zone, **warships = 1 + rand()%3 + rand()%3**
   (1..5, triangular). Executed at once. This also posts the PIRATES news (§6.1).
2. **LOSEMONEY 0x60** to the player: amount = cost (+0xc), +0x10 = 15 (ledger category,
   NIEPEWNE).
3. **10 %** (rand()%100 < 10): **message 0x148 = badboy +1** for the player (+0xc = 1; same message as
   the event command `badboy`).
4. For every neighbour province of the zone whose owner exists and is not the player, once per
   distinct owner O (in neighbour order):
   - **EXEC_RELATION 0x16b**: player tag (+0x16) and O (+0x18), amount **−50** if that province is on
     the same continent (+0x106) as O's capital, **−25** otherwise (+0xc, float), +0x10 = −10.0
     (meaning NIEPEWNE, believed a floor), +0x14 = 0x13;
   - **GIVECB 0xad** to O against the player, expiring **today + 3 months**, CB bytes +0x1a = 1,
     +0x1b = 0.

The privateer fleet is an ordinary PIR fleet. It attacks the commissioner's ships too (TUTORIAL_2_G6
text). It counts toward the spawn cap and is moved by the mover.

---

## 6. Interactions

### 6.1 News

**0x87 PIRATES** is posted by the fleet creator `0x4381d0` (CREATE_FLEET core, called from
`0x452221` / `0x45227c`) whenever the owner is PIR **and the local player knows the province** (byte
province+0x256+viewer index), at `0x4381f5..0x438236`. Arguments: viewer, zone name, zone id. That covers
spawner and privateer fleets alike. Texts: PIRATES_SETUP "When a fleet of pirates appears in a
seazone", PIRATES_HEADER "Our Marshal Reports%s", PIRATES_1 "Pirates sighted", PIRATES_2 "%t, a fleet
of pirates have been sighted in", PIRATES_3 "%s", PIRATES_4 "If they are not destroyed, they might
impact our revenue in that area.", PIRATES_LOG "Pirate Fleet sighted in %s!", buttons OK / Goto.
(`re/mt_missing.txt` lists PIRATES as missing. That list is wrong for this id.)

### 6.2 Leaders / "privateer"

- Spawned and commissioned fleets get **no leader**.
- `leaders.pir` holds 10 PIR "privateer" leaders (e.g. Morgan 1645-1685, location 966). The reader
  turns `privateer` into explorer type 3 (SPEC_EXPLORERS §1.2). Static reading of the arrival path
  `0x4d8240`, type 3: the location is a sea zone without a port object → `0x43c910` (best port of the country)
  returns 0 for PIR → falls through to the general rule → no PIR army to join → **CREATE_ARMY 1000
  men at province 0** with the leader. **NIEPEWNE**: it is not proven that PIR leaders are scheduled at all.
  If they were, the mover would do rand()%0 on such an army (division fault), unless province 0 has
  neighbours. **Port recommendation:** do not give PIR leaders units, or skip them.
- Such units would also count toward the spawn cap (§2.2).

### 6.3 VP, badboy, events

- **VP:** winning a battle against PIR gives **no** battle VP (PROVBATTLEWON 0x8F excludes losers PIR/NAT/REB,
  SPEC_VP row 9). **Losing** to pirates still costs −3 / −0.5 (row 10 has no exclusion).
- **Badboy:** only the 10 % roll when commissioning privateers (+1).
- **Events:** `randomevents.txt` "A Pirate Offers His Services" (EVENT_PIRATE_*) only changes
  warships, SPA relation, cash and VP. It does not touch PIR. The Chinese/Japanese pirate events (10005,
  10006, 10021, 10022) are flavour text plus ordinary commands. No event command creates PIR units
  (NIEPEWNE: event command list not re-scanned for this).

---

## 7. Port notes

**State**
- PIR country: next spawner date (4 bytes), last spawn province (int16 enough). Tech levels as for
  any country.
- Per land province: `pirates` (int, = P of §4.1), recomputed incrementally, not every tick.
- Pirate fleets are ordinary fleet units owned by PIR (warships only).

**Per tick (host)**
- If today > nextDate: run the spawner (§2), then the mover (§3).

**Monthly (PIR)**
- Pirate naval tech (§1.5), then the base monthly.

**Event-driven**
- On fleet enter/leave of a sea zone with PIR involved, and on PIR fleet state changes: refresh P
  for the neighbouring land provinces (§4.1). Refresh the blockade of those ports (§4.4).

**Read sites of P:** monthly trade value and CoT share (−0.5·P), CoT attractiveness, income value
and the income tooltip (only without a fortress).

**Fixed-point:** all pirate arithmetic is integer except the −0.5·P trade terms (half-ducats; keep trade
values in a ×2 or ×100 fixed point). ceil(warships) is an integer count for spawned fleets anyway.

**Random-draw order** (for replay parity with the original, if wanted): spawner = 2 (interval) [+1
chance roll if under the cap] [+1 per candidate zone with a raw score > 0] [+1 ship count];
mover = per fleet 1 (neighbour) [+1 if the target scores > 0].

**Open points**
- Whether +0x212e is saved (NIEZNANE). The behaviour is the same either way (immediate run after load).
- Whether naval battles and retreats treat PIR specially beyond hostility and VP (none found).
- Initial maximum morale value of PIR fleets in numbers (depends on PIR sliders/tech; NIEZNANE).
