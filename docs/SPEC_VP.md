# Victory points (eu2.exe v1.09) - specification

Implementation: `world_vp` (src/world.c) and its callers; the monthly step in src/economy.c `score_month`.
Port decisions (NIEPEWNE): discovery VP for every country since 2026-11-10 (the AI's map knowledge is saved now);
KILL_LEADER (row 13) and COUNTRY_BROKEN (row 27) are not ported; the first-discovery specials (row 8)
are not ported; JOIN_ALLIANCE VP goes to both partners; the join-war truce penalty (row 22) is not ported.

Specification from `eu2.exe` (prose only). NIEPEWNE = guess, NIEZNANE = unknown.
Supersedes the table in `docs/SPEC_MISSIONS.md` §2.1 (several rows there were wrong, see §5).

## 1. Storage and the two add paths

- VP ledger = object at country+0x20E2 (own vtable `0x5e5fd4`): +0x04 + 4·cat = float per
  category (cat 0 Economy BVPECO, 1 Peace BVPPEACE, 2 Battle BVPBATTLE, 3 Diplomacy BVPDIP,
  4 Establishment BVPEST, 5 Discovery BVPDISC, 6 Mission BVPMISS; labels `0x55f070`);
  +0x20 = back-pointer to the country. So the categories sit at country+0x20E6 .. +0x20FE.
- Total = float country+0x20DE. `0x441320(x)`: total += x.
- `0x55f030(cat, x)`: if BASE VICTORY POINTS (`[[0x8c0bfc]+0x2DE]`) is off → nothing; else
  cat += x and total += x. Every source below goes through it **except** the monthly score step
  `0x55f130` (§3), which adds to the category floats and to the total itself, also only when the
  option is on.
- Nothing else writes +0x20DE (only zeroed at country init `0x435660`). No end-of-year or
  end-of-game VP table exists: the final score is simply int(total) (`0x52ce90`).
- Amounts may be negative; the total is never clamped.
- Note: the `vp` event command and the mission system also go through `0x55f030`, so with BASE VP
  off even mission/event VP are **not** counted (as found; NIEPEWNE whether intended).

## 2. Every VP source (45 call sites of `0x55f030` + `0x55f130`; no indirect uses found)

Who = the country whose ledger is credited. w = 0.05·|P+0x24| (war score of the peace offer, so 0..5).

| # | Cat | Amount | Trigger / who | Evidence |
|---|---|---|---|---|
| 1 | 0 Eco | +1/12 per goods, monthly | Goods market month (`0x41b5e0` → `0x41bcd0`). For each of the 21 trade goods g, the "leading nation" in production `country[0xF06+4g]` (float; shown ×100 as "Units", RWLEADING). Scan countries in index order 0..0x153 with an **integer** threshold t=0: if prod > t then leader = this country, t = trunc(prod). Quirk: a later country beats the current leader when its value exceeds the *truncated* leader value (3.5 beats 3.7); with all values < 1 the last country with prod > 0 wins. No leader if nobody > 0. = "The leading nation gets 1 VP each year" | `0x41bcd0` |
| 2 | 0 Eco | +0.16 per CoT, monthly | Trade manager month (`0x41a680` → `0x499e92`): for every CoT and every country whose merchant count (entry+4, float) > 5, i.e. 6 = monopoly | `0x499e92..0x499f81` |
| 3 | 0 Eco | +0.05 | EXEC_MERCHANT_SUCCESS 0xA9, **uncontested** CoT, merchant succeeded (any outcome: new entry, +1, or refund at ≥6). Per repetition of the arrival loop (NIEPEWNE). Contested arrivals give nothing | `0x446df9..0x446e06` |
| 4 | 4 Est | +1 | EXEC_BUILD_FACILITY 0x7F (a building/manufactory is finished), unconditional | `0x4475a3` |
| 5 | 4 Est | +0.8 (+5 bonus) | COLONISE 0x9E, only when the province had **no city** yet (a new colony; colonists into an existing colony give 0). Bonus +5 if `0x418e80` returns class 1..4 (else a +0 call) | `0x447a35..0x447aa5` |
| 6 | 4 Est | +0.4 (+5 bonus) | ESTABLISH_TRADEPOST 0xA4, only for a **new** trading post (province+0x40C was 0; raising an existing one gives 0). Bonus +5 if class 5..7 | `0x44770e..0x447761` |
| 7 | 5 Disc | +2 / +0.3 / 0 | Unit enters a province its owner did not know (`0x48cdd0`): land: +2 if nobody had discovered it (province byte +0x254 = 0), else +0.3; sea zone: +0.3 if nobody had, else 0 | `0x48ce18..0x48ce48` |
| 8 | 5 Disc | special | DISCOVERPROVINCE 0x93: first discovery in the world of a special place (`0x418420`, cases 1..17, once per game): SPEC_AMERICA 50, ASIA 25, QUEBEC 5, GREATLAKE 10, CAPEHORN 10, GOODHOPE 10, HUDSONBAY 10, ALASKA 5, PANAMA 5, INLANDCHINA 20, AMAZONEZ 10, NORTHAM 20, INDIA 25, KAMTCHATSKA 10, CALIFORNIA 10, PACIFIC 10, AUSTRALIA 5 (message 0xB0 only for the local player; VP for everyone) | `0x44f504..0x44f9e2` |
| 9 | 2 Bat | +4 or +0.5 | PROVBATTLEWON 0x8F, per receiving country: +4 if the battle was **decisive** (msg+0x10, below), else +0.5. Nothing if any of the losers is PIR 0x151, NAT 0x152 or REB 0x153 | `0x44d22b..0x44d267` |
| 10 | 2 Bat | −3 or −0.5 | PROVBATTLELOST 0x90: −3 if decisive, else −0.5 (no rebel/native exclusion) | `0x452b50` |
| 11 | 2 Bat | +(size+1) | PROVSIEGEWON 0x91: + (city size `city+0x6E` (0..6) + 1); unconditional | `0x44d53e..0x44d56c` |
| 12 | 2 Bat | −(fort+1) | PROVSIEGELOST 0x92: − (trunc(city+0x3A, fort level float, NIEPEWNE) + 1); 0+1 if no city | `0x452bd0` |
| 13 | 2 Bat | +(25 + rank) | KILL_LEADER 0xB5 handled by the **combat** object (`0x4699d0`, case 0x469b9e): the leader's **owner** (leader+0x32) gets 25 + leader+0x22 (rank, lower = senior, default 100). Sign as found: fild rank, ×(−1), 25 − that. NIEPEWNE whether the original intended a penalty; reproduce as is | `0x469b9e..0x469bc9` |
| 14 | 1 Peace | −2w / +2w | EXEC_ACCEPT_PEACE 0x10C. Giver side = recipient if the offer is a demand (P+0x3F58=1), else the sender. Every country of the giver side (or only the separate-peace country, side+0x8/+0xA) gets −2w, every country of the gaining side +2w (= ∓0.1·|war score|, max 10) | `0x44e6e1..0x44eabd` |
| 15 | 1 Peace | −v / +v per province | Same handler, for each province ceded: old owner −v, receiver +v, v = province income value `0x47d970` | `0x44e087..0x44e0c6` |
| 16 | 1 Peace | +15 | Same handler: each Defender of the Faith (`0x4602d0`) on the gaining side | `0x44d903..0x44d939` |
| 17 | 1 Peace | +100 or +20 | Same handler, religious conversion demand (P+0x3F5F): the converting country (this) gets +100 if target byte +0x11E is set, else +20 (+0x11E NIEZNANE); the target takes our religion | `0x44eca7..0x44ed5d` |
| 18 | 1 Peace | −25 | EXEC_DECLINE_PEACE 0x10D, only if the refused offer had "better offer" P+**0x3F5B** = 1 (PEACEBETTER; not 0x3F53). The refuser (msg+0x16): stability −1, VP −25, immediate revolt roll with bonus +2 (`0x460450(2)`) | `0x44eea9..0x44ef02` |
| 19 | 3 Dip | −25 | WE_DECLARE_WAR 0xE8, truce (peace < 61 months ago): with stability −5, relations −50 | `0x44b276` |
| 20 | 3 Dip | −5 | WE_DECLARE_WAR without CB (with stability −2, badboy +2/+4, ×1/3 vs human) | `0x44b5a5..0x44b5bb` |
| 21 | 3 Dip | −3 | WE_DECLARE_WAR with a CB | `0x44b613` |
| 22 | 3 Dip | −25 | JOIN_WAR 0xEB when joining breaks a truce (`0x45f5d0` with any enemy member): only if we join the attacker side, or the defender side once the war is ≥ 2 months old (NIEPEWNE reading of the month test). Also stability −5 (with revolt roll), relation −50 with every country flagged in our +0x138A list | `0x44bf6b..0x44c07d` |
| 23 | 3 Dip | +2 / +20 / +5 | EXEC_JOIN_ALLIANCE 0x105, by relation-object type (+0x22): 1 royal marriage +2, 2 vassalisation +20, 4 military alliance +5 (credited to the handling country; both partners if both receive it, NIEPEWNE) | `0x449bc4..0x449d7b` |
| 24 | 3 Dip | −5 | EXEC_LEAVE_ALLIANCE 0x106 (the leaver): type 4 alliance: stability −1, relation −100 both ways, VP −5; type 2 vassalage broken: VP −5, relation −150 both ways, stability −3, CB-type event 0xAD to the other party, relation −50 with every country for which `0x431270(religions)` is false (NIEPEWNE: other religion group); type 1 royal marriage: VP −5, stability −1, relation penalty | `0x44a1f2..0x44a512` |
| 25 | 3 Dip | +20 | EXEC_SET_ANNEXATION 0x102 (diplomatic annexation): the annexer (msg+0xC) | `0x449670..0x44968b` |
| 26 | 3 Dip | +50 | NEW_EMPEROR 0x150: the new Holy Roman Emperor (global `0x8d0e18` = its tag) | `0x45557f` |
| 27 | 3 Dip | −100 | COUNTRY_BROKEN 0x128: provinces it holds of others are released, rebel armies inside are resolved, Defender of the Faith lost, VP −100, stability set via `0x43a250(3)` (NIEPEWNE: set to 3 or −3). What posts 0x128 is NIEZNANE (only the message factory found) | `0x4554a7` |
| 28 | 6 Miss | + mission VP | MISSION_COMPLETED 0xCA: + mission+0x28 | `0x452cb3` |
| 29 | 6 Miss | − penalty | MISSION_FAILED 0xCB: − mission+0x22 | `0x452d23` |
| 30 | 6 Miss | random mission VP | AI only (AI override of TIMER_MONTH `0x557ab0`), only with DYNAMIC MISSIONS (+0x2DA) on: r = rand(); if r % 1000 < AIaggr(+0x2C6) + difficulty(+0x2CA)/2 + 2, then + trunc(table`0x638210`[r % 11]) = 30,20,20,30,15,50,5,5,5,10,2 (the base VP of mission type r%11). Same r for both tests | `0x557ace..0x557b35` |
| 31 | any (msg) | ±n | Country event 0x17B (dispatcher `0x461e90`, case `0x462a57`): category = msg+0x10, amount = msg+0x0C (float). Only poster found: event command `vp` (`0x5972a0`): amount = (float)value, category **6** | `0x462a57`, `0x5972d2..0x5972df` |
| 32 | 0 / 3 | monthly | Score step `0x55f130` (§3) | `0x451cb9` |

Province class `0x418e80` (rows 5/6): global flags (object `[0x8ff634]`+4, one byte per class, set
to 1 when claimed, so each bonus is paid **once per game, to the first country**). It returns the
first unclaimed class matching the province (0 = none) and claims it:
1 city (+0x408) on continent (+0x106) 1 America; 2 city in 5 Africa; 3 city in region (+0x10E) 13
REG_Siberia; 4 city in continent 4 Oceania; 5 trading post (+0x40C) in region 14 REG_India;
6 TP in 15 REG_China; 7 TP in 18 REG_Japan. (Region ids index the `REG_*` table after the `CON_*`
names at `0x61dc74`, NIEPEWNE that +0x10E uses that index directly.) Quirk: the colony handler can
claim a TP class (5..7) without paying it, and vice versa; reproduce.

Decisive battle (msg+0x10 = combat byte +0x18, set by combat vfunc `0x46aaa0(winner, loser)`):
both sides had ≥ 10000 men at the start (side+0x24), the loser lost ≥ trunc(0.666·its start
men), and the winner lost < trunc(0.5·loser's losses). (Sides' "current men" = `0x48ee00`.)
The same flag also raises the per-enemy war-score byte by 3 instead of 1 (`0x4cfb70`/`0x456180`;
the siege handler does +2/+1 capped at 95 using the same msg+0x10).

## 3. Monthly score step `0x55f130` (country month step 22, every country)

All only when BASE VP is on; credited to the category float **and** the total:

1. Economy += stability / 72 (stability `0x43a230`, −3..+3 → ±0.5 VP/year).
2. Economy −= 0.1 for every owned province (list +0x131E) whose controller is REB (0x153).
3. Diplomacy (cat 3) += 0.0916667 (= 1.1/12) if not at war (`0x43c000` false).
4. Seven "world leader" awards, +1/24 each to Economy (0.5 VP/year each). A value counts when it
   is ≥ the same value of **every** existing country (ties: all tied countries get it) and > 0:
   manufactories `country[0xEE6]`; land forces 100·artillery + cavalry + infantry (vfuncs +0x390,
   +0x38C, +0x388); navy warships + galleys + transports (vfuncs +0x39C, +0x398, +0x394); land tech,
   naval tech, infrastructure, trade levels (tech block country+0xE46: +0x0C/+0x24/+0x3C/+0x54,
   vfunc +0x34).

No other periodic VP: the yearly step `0x451250` has none.

## 4. Related score fields

- [0x20DE] total VP (above). Shown/ranked in modes 0 and 1.
- [0x1224] (int): mode 2 (Deathmatch) only — on every owner change (`0x481260`, when
  `0x419690` is true) the old owner −1 and the new owner +1, i.e. net provinces gained.
- [0x121A] (float): mode 3 scenario-mission progress, updated by `0x463100` (country month step 3,
  game type 3) and `0x4634e0`/`0x463560` (also called from the peace handler when a member's
  +0x1016 = 3). Not decoded (out of scope).

## 5. Corrections to SPEC_MISSIONS §2.1

- `0x41bcd0` ranks trade-goods production (country+0xF06 per goods), monthly, ties per §2 row 1.
- `0x446ae0` +0.05 = merchant arrival; `0x499e92` +0.16 = monopoly per CoT per month.
- `0x4699d0` is **KILL_LEADER** (leader killed in combat), not "won battle"; +0x22 is the leader
  rank. Battles are rows 9–12.
- `0x44a100` is EXEC_LEAVE_ALLIANCE (types 1 marriage / 2 vassal / 4 alliance), not insults.
- The refused-peace flag is P+0x3F5B (better offer), stability −1.
- Missing before: rows 4, 9–12, 14–17, 22, 23, 25, 27, 30, 31 and the whole §3.

## 6. Open points

- Who posts COUNTRY_BROKEN 0x128 (NIEZNANE) and whether `0x43a250(3)` sets +3 or −3.
- KILL_LEADER sign (25 + rank to the victim's owner) — as found, looks like a bug or a
  misread of the "who" field; verify in play.
- JOIN_ALLIANCE / ACCEPT_PEACE: which countries receive the EXEC message (both partners?).
- Country byte +0x11E (row 17) NIEZNANE; city+0x3A = fort level NIEPEWNE.
- +0.05 merchant VP per loop repetition vs once per message (NIEPEWNE).
- Mission-progress [0x121A] formulas (mode 3) not decoded.
