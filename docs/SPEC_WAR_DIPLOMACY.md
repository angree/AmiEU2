# EU2 v1.09 - diplomacy and war (specification)

This spec comes from reading `eu2.exe` (GOG v1.09) in Ghidra and capstone, plus the shipped data
files, `Readme.txt` (patch notes), `EU2_manual.pdf` and the rollover texts in `Config/text.csv`.
No code was copied. Every claim names the function or data address it came from, so it can be
checked again. Field offsets are the original's and are given only as anchors for re-checking; the
port uses its own structures. Markers: **NIEPEWNE** = believed but not proven, **NIEZNANE** = not
found. Companion documents: `SPEC_TIME_ECONOMY.md` (calendar, monthly order, war exhaustion,
badboy decay, envoys) and `SPEC_EVENTS.md` (event commands that touch diplomacy).

**Coverage warning.** Many functions in this area are missing from `eu2_all.c` (examples:
`0x44d5b0` accept peace, `0x44d0b0` battle won, `0x4865e0`/`0x486700` unit fire/shock strength,
`0x485d30`/`0x485ff0` attrition, `0x46a730` apply losses, `0x46fd50` siege scheduler). Those were
read from the raw disassembly. The decompiler also hides most `__ftol` arguments and float
constants; every constant quoted here was read from the binary (`const.py`).

## Contents

1. [Objects and fields](#1-objects-and-fields)
2. [Relations](#2-relations)
3. [Diplomatic actions](#3-diplomatic-actions)
4. [Declaring war](#4-declaring-war)
5. [War score and peace](#5-war-score-and-peace)
6. [Badboy (reputation)](#6-badboy-reputation)
7. [Land combat](#7-land-combat)
8. [Morale](#8-morale)
9. [Sieges, assaults, fortresses](#9-sieges-assaults-fortresses)
10. [Supply and attrition](#10-supply-and-attrition)
11. [Movement](#11-movement)
12. [Naval combat, naval attrition, blockade](#12-naval-combat-naval-attrition-blockade)
13. [Revolt risk and rebels](#13-revolt-risk-and-rebels)
14. [War exhaustion inputs](#14-war-exhaustion-inputs)
15. [Data files: which column goes where](#15-data-files-which-column-goes-where)
16. [Worked example: France against England, 1419](#16-worked-example-france-against-england-1419)
17. [Precision notes for the fixed-point port](#17-precision-notes-for-the-fixed-point-port)
18. [Open questions](#18-open-questions)

---

## 1. Objects and fields

### 1.1 Country (additions to `SPEC_TIME_ECONOMY.md` §3)

| offset | meaning | source |
|---|---|---|
| +0x6 | tag index (0..339) | everywhere |
| +0x228 | religious tolerance, one float per religion (religion.csv order) | `0x43c970`, `0x481a30` |
| +0x254 | state religion | |
| +0x37e | pointer to the domestic-policy block (8 ints s0..s7 at +0x08..+0x24) | `0x438690` |
| +0x382 | culture object (accepted-culture test `0x429fb0`) | `0x481a30` |
| +0x38e | diplomacy object (§1.2) | |
| +0x8ee | per-country date of the last peace with that country (truce clock), 340 dates | `0x45f5d0` |
| +0xe42 | badboy (float) | |
| +0xe4e | counter incremented when a country whose capital is on continent 1 declares war on one whose capital is not (NIEPEWNE: colonial wars) | `0x44b170` |
| +0xe9c | bankruptcy date | `0x438690`, `0x481a30` |
| +0x1000 | an integer added to every province's revolt risk (NIEPEWNE: event modifier) | `0x460440` |
| +0x122a | war exhaustion | |
| +0x1246 | flag: halves losses taken (`0x48d450`, `0x48d880`; NIEPEWNE: "sturdier hulls / supply" tech flag) | |
| +0x12c4 / +0x12c8 | land / naval maintenance slider (0.5..1.0) | `0x438690`, `0x438850` |
| +0x1350 | core-province set (vtable slot 0x24 = "is province a core") | `0x481a30` |
| +0x1386 | number of provinces (NIEPEWNE, as in the economy spec) | `0x45afc0`, `0x43c970` |
| +0x138a | neighbour flags, one byte per country | `0x43c970`, `0x44b170` |
| +0x14de | per-country float: the share of this country's border contacts with each other country (the recount `0x43aa90..0x43b0ac`: every neighbour of an owned province owned by X adds 1 to X, then divided by the total); −250 × it in the relation ceiling | `0x42f740` |
| +0x1f8e | monarch block (MIL getter `0x42f5c0`) | `0x438690` |
| +0x20bc | monarch pointer; 0 means "no monarch", then MIL = 3 | `0x438690` |
| +0x20e2 | victory-point ledger (`0x55f030(type, value)`) | `0x44b170` |

### 1.2 Diplomacy object (country+0x38e)

An array of 340 per-country records of 0x36 bytes starting at +0xc. Offsets below are relative to
`obj + other*0x36` (so the relation float of record `other` is at `obj + other*0x36 + 0x32`).

| offset | meaning | evidence |
|---|---|---|
| +0x10 | list of **temporary** casus belli against `other` (empty-test = vtable slot 8) | `0x447f20` (GIVECB), `0x44b170` |
| +0x18 | tail pointer of that list | `0x447f20` |
| +0x26 (= +0xc+0x26) | NIEPEWNE date: "warning" date, true while within N months | `0x4cded0` |
| +0x2a (= +0xc+0x1e) | date tested "< 240 months ago" (`0x4cdf10`); gives +0.08 relation/month. NIEPEWNE: trade agreement or recent marriage | `0x43c970` |
| +0x32 | **relation** (float, clamped −200..+200 by `0x4cdfa0`) | `0x4cdf70`, `0x4cdfa0` |
| +0x36 | flag, −0.1 relation per month (NIEPEWNE: embargo by us) | `0x43c970` |
| +0x37 | flag, −0.5 relation per month (NIEPEWNE: insulted / claimed throne) | `0x43c970` |
| +0x38 | flag cleared when we declare war on `other` (NIEPEWNE: military access granted) | `0x44b170` |
| +0x39 | flag, −0.1 per month; also true if our overlord… (`0x4cbf20`) (NIEPEWNE: embargo against us) | `0x4cbf20` |
| +0x3b | **royal marriage** (trigger `dynastic` `0x59bab0`) | |
| +0x3c | **same military alliance** (trigger `alliance` `0x59b8b0`) | |
| +0x3f | **permanent casus belli** against `other` | `0x4cc520`, `0x44b170` |
| +0x40 | flag, +0.05 per month (NIEPEWNE: guarantee / access) | `0x4cb860` |
| +0x41 | flag, +0.025 per month (NIEPEWNE) | `0x4cb880` |

Global lists: wars `DAT_008ff614` (war objects, §5.1), alliances/vassalages `DAT_008ff602`
(objects with a member list at +0x1a and a type at +0x22: 2 = vassalage (overlord is the first
member, `0x4cbe90`), 4 = a type that triggers an extra callback on war declaration (NIEPEWNE), 6 =
war side). Vassal test `0x4cbe90`: some type-2 object whose first member is us and contains the other.

---

## 2. Relations

### 2.1 Range and storage

- Relation is a float, −200..+200 (manual p.51; clamp `0x4cdfa0`, constants `0x5e583c`/`0x5e5838`).
- It is stored per direction, but every change the engine makes (war declaration, drift, gifts) is
  applied to both records, and readme 1.09 states that relations "should now match" (scenario files
  may list different values; set both to the same value at load. NIEPEWNE which one wins).

### 2.2 Ceiling ("where relations drift to"), `0x42f740(a, b)`

```
ceiling = 125, or 25 if the two state religions differ (Catholic and Counter-Reformed count as the
          same, `0x431270`)
        − 25 if exactly one of the two is Christian (religions 0..4)
        − 100 if b has a permanent CB on a (flag +0x3f), − 100 more if a has one on b
        − 250 × a.grievance[b] (country+0x14de)
clamped to −200..+200
```

### 2.3 Monthly drift, `0x43c970` (called from the country monthly, economy spec §4.1 step 23)

For every other existing country whose capital province is known to us, a delta `d` is summed:

| term | delta |
|---|---|
| ceiling < 0 and (our badboy > 1 or theirs > 1) | d = −0.05 (replaces the start value 0) |
| ceiling > 0 | +0.05 |
| same military alliance (+0x3c) | +0.25 |
| royal marriage (+0x3b) | +0.25 |
| flag +0x39 / overlord rule (`0x4cbf20`) | −0.10 |
| flag +0x36 | −0.10 |
| their badboy > 10 / > 15 / > 20 | −0.5 each (cumulative, −1.5 at > 20) |
| flag +0x37 | −0.5 |
| neighbour, or they have more than 200 provinces: count of {land, naval, trade} where their level − ours > 2, plus 1 if our infra level > theirs; then d += count / 12 | |
| their religion is not pagan | + 0.025 × our tolerance toward their religion |
| religions hostile (`0x45d050`: Protestant/Reformed vs Catholic/Counter-Ref before the Reformation flag 4; Muslim vs Christian; pagan vs non-pagan; Sunni vs Shiite; Christian vs non-Christian) | −0.10 |
| neighbour and neither of us at war | +0.05 |
| flag +0x40 | +0.05 |
| date test `0x4cdf10` (+0x2a within 240 months) | +0.08 |
| flag +0x41 | +0.025 |
| current relation > ceiling | −0.02 |

Then: clamp d to ±5/12 (±0.41667, i.e. ±5 per year). If the current relation is already above the
ceiling and d > 0, d = 0. Apply with `0x454840` (clamped add).

So relations converge slowly toward the ceiling; marriages and alliances push up to +0.5 a month
and badboy pushes down. The manual's "relations change more slowly near the extremes" is produced by
the ceiling rule, not by a separate curve.

### 2.4 Gifts (`0x453530` on the receiver, cost `0x45afc0`)

- Cost (UI `0x50c980`): base × f × (1 + inflation), base = 250 (state gift, `0x45b120`), 100
  (personal, `0x45b140`) or 25 (letter, `0x45b160`). f = clamp(0.2..2, ourProvinces × 0.004) ×
  clamp(0.2..2, theirProvinces / ourProvinces) × (1 + 0.1 × (DIP_a − DIP_b)), the product clamped to
  0.33..3. Which DIP is subtracted from which is NIEPEWNE (first call's receiver is the target).
- Effect on the receiver: relation += value / max(50, receiver's yearly income (ledger +0x1cc)).
  How the sender turns the cost into `value` (the texts promise DIP and luck, possibly negative) is
  **NIEZNANE** (the sender lives in a function Ghidra missed).

### 2.5 Relation effects of war (from `0x44b170`, §4)

On declaration: our relation with the target −100; with every country that has a royal marriage
with the target −25; with every member of the target's alliance −100. Breaking a truce also costs −50
with every neighbour.

---

## 3. Diplomatic actions

### 3.1 Diplomats

- Every action that is *sent* costs one diplomat; replying is free (manual p.52). Stock accrual is in
  the economy spec §8.3.
- One diplomat per target per month (`DIPROL_NO_DELAYED`). A rejected peace offer blocks a new one
  for a month (`DIPROL_PEACE0/1`). Offers auto-cancel after 2 months (readme).
- Offering a loan costs a diplomat (readme).

### 3.2 Action list and preconditions

Preconditions are taken from the rollover texts (`text.csv`, keys in brackets) and the readme; the
engine-side checks were not all traced (NIEPEWNE that the text and the code agree in every detail).

| action | precondition | effect |
|---|---|---|
| Royal marriage (`DIPOFFDYN`) | same religion, except Orthodox may marry any Christian and Counter-Ref ↔ Catholic (manual p.56); not a vassal (`DIPROL_DYN_VASSAL`) | flag +0x3b both ways; expires at a date (`DIPROL_RELIGION1`) |
| Break royal ties | have one | −1 stability |
| Offer vassalisation | royal marriage, same alliance, relation ≥ 190, same religion, target at peace, target not larger (manual: fewer provinces) (`DIPROL_VASSAL1`) | overlord gets 50 % of the vassal's tax (economy spec §4.1 step 9), military access |
| Cancel vassalisation | | −3 stability; the ex-vassal gets a CB (`DIPROL_VASBRK0`); the lord gets a CB if the vassal cancels (readme) |
| Annex | vassal ≥ 10 years, relation ≥ 190, same alliance, at peace, land border, same religion (`DIPROL_ANNEX1`, manual) | provinces, units and maps transfer; a 2-year CB for "our worst enemies"; centralisation −1 (readme); stability set to the higher of the two (manual) |
| Declare war | §4 | |
| Offer peace | §5 | |
| Create / invite / join alliance | one alliance per country; only the leader invites/bans; no alliance with other countries' vassals (`DIPROL_CNA2`) | flag +0x3c among members; alliance expires 10 years after its last war |
| Leave alliance | not the leader, not at war | −1 stability (manual); all members get a CB (`DIPROL_BRKMIL1`) |
| Ban from alliance | leader only, needs a CB | target gets a CB; other members may leave |
| Exchange discoveries | | maps |
| Trade embargo / allow trade | not during a truce (`DIPROL_REFT1`) | −1 stability (manual); target gets a CB while it lasts; −trade research per embargo (manual) |
| Offer / cancel trade agreement | | cancelling gives a CB, costs stability (manual) |
| Ask / cancel / revoke military access | not at war (readme); no troops inside when cancelling | cancel or revoke: −1 stability; revoke gives the other a CB |
| Warning | not at war, no warning in effect | we get a CB if they attack our neighbours within 60 months |
| Insult | not at war | big relation loss, target gets a 1-year CB |
| Claim throne | royal marriage with target | CB on them; relation loss with all our royal-marriage partners; stability loss (manual) |
| Guarantee | | CB on anyone who attacks them within 5 years |
| Gifts | §2.4 | |

### 3.3 AI acceptance

**NIEZNANE.** The AI answer functions were not traced (they are in the AI code at `0x53xxxx..0x56xxxx`).
Known facts from the readme: vassals are "extremely likely" to join their lord's alliance; annexation
offers are weighed by the years of vassalage; the AI looks at war exhaustion before declaring war or
honouring an alliance; the AI never accepts a peace with value above 100; alliances may be accepted
without a common border; "transcontinental" alliances are very unlikely. The manual says acceptance
depends mainly on relations.

### 3.4 Call to arms

When a war starts, every member of each side's alliance is asked (`REQUEST_HONOR_ALLIANCE` 0xea →
country slot 0x1e0 `0x44c820`; honour 0x111 `0x44c860`, dishonour 0x110 `0x44c980`). Refusing costs
−1 stability and gives the attacked ally a 6-month CB (manual p.59, p.61). Honouring is
stability-free and does not count as breaking a truce in the first two months of the war (readme).
Exact handler logic NIEPEWNE (functions not read in detail).

---

## 4. Declaring war

Event chain: `REQ_SETUP_WAR` (0xfb) → `EXEC_SETUP_WAR` (0x10f, `0x44ada0`) creates the war object
and posts `WE_DECLARE_WAR` (0xe8, `0x44b170`) to the attacker and `THEY_DECLARE_WAR` (0xe9,
`0x44b7a0`) to the defender. Country dispatcher: `0x456xxx` region, slots 0x1c8/0x1cc/0x1d0.

### 4.1 Who can declare

- Not while stability is −3 (`DIPROL_WAR3`), not on a member of our own alliance (`DIPROL_WAR2`),
  not on a country that granted us military access (`DIPROL_WAR4`), and not again before a given date
  after a previous war (`DIPROL_WAR1`). Needs a diplomat and the one-month rule.

### 4.2 Effects on the attacker (`0x44b170`, in order)

| condition | effect |
|---|---|
| truce: last peace with the target less than 61 internal months ago (`0x45f5d0`, country+0x8ee) | stability −5; if that pushes stability below −3, each missing step triggers automatic revolts (`0x460450`); relation −50 with every neighbour; VP −25 |
| attacker's capital on continent 1 and the target's not | country+0xe4e += 1 |
| every third country that holds an active **warning** against us (`0x4cb8a0`) and is not … (flag +0x36 toward us clear) | receives a GIVECB (0xad) lasting 2 years |
| our relation with the target > 150 | stability −1 |
| our relation with the target > 100 | stability −1 (so > 150 costs 2) |
| always | relation with the target −100; −25 with the target's royal-marriage partners; −100 with members of the target's alliance |
| **no CB** (no permanent flag +0x3f and the temporary CB list empty) | stability −2; VP −5; badboy +2, or +4 if our relation with the target is negative; ×1/3 if the target is a human player |
| **CB** | VP −3; badboy +1, or 0 if the target is a human player |
| badboy is **not** added if our capital is on continent 2 (Europe) and the target is pagan | |
| same religion (`0x45fbb0`; from 1615 on, two Christian religions no longer count) | stability −1 |
| same military alliance (+0x3c) | stability −2 |
| target is our vassal (`0x4cbe90`) | stability −3 |
| then | flag +0x38 cleared; alliance callbacks; `0x439cf0`, `0x443dc0` (refresh) |

Notes:
- The manual says a royal marriage costs −1. The code above has no royal-marriage term (the marriage
  is broken elsewhere, `0x4cbfb0` NIEPEWNE). Keep the code's table and verify in game.
- The badboy figures differ from the manual (which talks about stability only). They match readme
  entries 1230-1232.
- "Continent 1" and "continent 2" are the province field +0x106. America = 1 and Europe = 2 are
  inferred from the readme (no badboy for Europeans on pagans; natives have no cavalry, §13.3).
  NIEPEWNE.

### 4.3 Automatic badboy wars

Badboy limit `0x45aef0`: `limit = DIP + 30 + (year − 1419) / 20`, plus `(year − 1790)` after 1790.
The readme says that on Very Hard every country gets a permanent CB on a human with badboy ≥ 35 and
that "badboy wars" use this limit. The AI code that starts them is NIEZNANE.

---

## 5. War score and peace

### 5.1 War object (`0x456430` allocates 0x391f6 bytes; `0x44ada0`)

| offset | meaning |
|---|---|
| +0x16 | start date |
| +0x1a | **date of the last combat** in this war (set by `0x469960` whenever a battle starts between members) |
| +0x1e / +0x22 | attacker side / defender side objects (type 6), member list at +0x1a |
| +0x2e | battle-score matrix A: 340 rows of 0x158 bytes; byte [i][j] = battle points of country i against j |
| +0x1c916 | battle-score matrix B (the other direction) |

Each byte is capped at 95 (`0x4cf3d0` copies with that cap). How many points one battle adds is
**NIEZNANE** (the increment site was not found; the manual says it depends on the forces and losses).

### 5.2 War score (`0x4d0800`, tooltip `0x4d06a0`/`0x4d0a70`, texts `WARDESC_*`)

For a pair (us, them), or us against a whole alliance when one side is the leader:
1. Battle term = our battle points − theirs (`0x4cfbc0` − `0x4cfc80`), clamped to −95..+95.
2. Minus the war value of every province we own that they control, plus the value of every province
   they own that we control (`0x4d0620` sums `0x47ca60`).
3. Clamp to −99..+99.
4. If one side controls *every* province of the other (`0x4cfd50`), the result is ±100 directly
   ("−100: We control nothing").

### 5.3 Province war value, `0x47ca60`

| province | value |
|---|---|
| no city object | 1 |
| colony (city slot 0x10 true) | 2 |
| city | trunc(baseTax / 2 + 2 + 0.25 × V) + 10 if it is the home of a CoT + trunc(city+0x3a) / 2 |

V = `0x47cc80`, a value that depends on the province's culture and religion versus the owner and on
being connected to the capital (NIEPEWNE: it looks like the "national/core worth" used elsewhere).
city+0x3a is a float; NIEPEWNE: fortress level.

### 5.4 Peace offers

From the manual (p.63-64), the readme and the peace texts:
- Demands and offers are priced in war-score percent. Cash: 25 ducats per 1 %. Provinces: their war
  value. A total above 100 % is not allowed (readme).
- You may cede only your own provinces that the enemy controls, and demand only enemy provinces that
  you or your allies control, plus your own **core** provinces held by the enemy even if not
  controlled. Never the enemy's capital; annexation only when you control all provinces of a
  one-province country (or every province, readme: pagans can always be annexed in one war).
- Extra terms: vassalisation, military access, religion conversion (`PEACE_VASSAL`, `PEACE_ACCESS`,
  `PEACE_DEMAND`).
- Acceptance: the closer the offer is to the war score, the likelier. Controlling the enemy capital
  helps. A human is forced to accept when all cores, all land-connected provinces and the capital are
  occupied (readme). An offer "better than what we can expect" causes turmoil if refused
  (`PEACEBETTER`). The AI acceptance formula is **NIEZNANE**.
- White peace (0x171): status quo. Automatic peace after 36 months without hostile action when neither
  side holds an enemy province (manual).
- Separate peace counts only the pair's points; peace with the alliance leader counts the whole war.
- Truce: 5 years (the 61-month test of §4.2). Breaking it: stability −5, VP −25, −50 relations.

The accept handler `0x44d5b0` (`EXEC_ACCEPT_PEACE`, 0x10c) is missing from the export, so the badboy
from ceded provinces is **NIEZNANE** in the code. The readme gives: no badboy for national (core)
provinces; none for Europeans taking pagan provinces; inheritance 0.25 per province; annexing
religious enemies +2; "turbo-annexing" on par with normal province gains.

---

## 6. Badboy (reputation)

- Stored at country+0xe42, float, floored at 0.
- Increases found in code: war declaration (§4.2). Event command `badboy` (`SPEC_EVENTS.md`).
  Privateers: 10 % chance of +badboy (readme). Peace gains (decoded, 0x44e319..0x44e457), per province P
  moving from O to N: if P is not a core of N, N gets +1 (+1/3 when O is human), twice if N is on the
  war's attacking side (+0x1e), none when N's capital is on continent 2 (Europe) and O is pagan;
  if P was not a core of O, O loses 1 (floored at 0).
- Decay: monthly, economy spec §4.1 step 1.
- Effects found: relation drift (§2.3), stability cost (economy spec), limit (§4.3), CB in Very Hard.

---

## 7. Land combat

### 7.1 Objects

A combat is created per province (`0x469750`/`0x4697a0`, debug name "Combat %d in Prov%d"). It owns
two **sides**: +0x30 = attacker, +0x34 = defender. Combat types (combat+0x24): 1 = land battle,
2 = naval battle, 3 = covered (watch), 4 = siege, 5 = assault. Classes: land `0x5e8010`
(constructor `0x46ec50`/`0x46ee00`), naval `0x5e8178` (`0x46f370`), siege/assault `0x5e8324`. Base
class vtable `0x5e7d2c`; side vtable `0x5e8094` (land) and `0x5e8200` (naval).

Side fields: +0x4 die roll (0..9), +0x8 fire CRT column, +0xc shock CRT column, +0x10 leader fire
bonus, +0x14 leader shock bonus, +0x18 men lost per day, +0x1c morale lost per day, +0x20 morale,
+0x42 retreating, +0x43 destroyed/forced out, +0x4e leader, +0x52 leader's country, +0x56 is
attacker, +0x58 unit list, +0x6c terrain/crossing modifier, +0x70 cavalry superiority (0/1).

Unit (land vtable `0x5e8910`): +0xe8 artillery (pieces), +0xec cavalry (men), +0xf0 infantry (men),
+0x58 morale, +0xde owner country. Men = inf + cav + 100 × art (`0x487cc0`). For ships: +0xe8
warships, +0xec galleys, +0xf0 transports (`0x489740`).

### 7.2 Timeline

- Day 0: `COMBAT_STARTED` → `0x46a240`: both sides initialise morale (slot 0x54), leader bonuses
  and terrain (slot 0x6c → `0x46f0e0` per side). The first phase is **fire** if either side has fire
  strength > 0.1 (`0x46eff0`), else **shock** (`0x46a2b0` with flag combat+0x13).
- Each phase start (`0x46a2b0`): both sides roll a new die `rand() % 10` (`0x46dd10`) and the daily
  losses for the whole phase are computed once (`0x46dda0`, §7.4).
- Every day (event 0x1f queued for tomorrow, `0x46a310` → `0x46a460`): the phase day counter
  increments; when it passes 4 (i.e. after 5 days) the phase changes. Fire is followed by shock;
  shock is followed by fire if anyone still has fire strength, otherwise by shock again. At each
  change both sides get a 1 % leader-death roll (`0x46d2c0`: `rand() % 100 == 17`, leader slot 0x18,
  `0x482dc0`).
- Each day the stored losses are applied to the attacker, then (if the battle is not over) to the
  defender (`0x46a730` → event `COMBAT_RESULT` 0x26 → `0x46a190` → side slot 0x70 `0x46d9d0`).
- After applying: if the attacker has no fire and no shock strength and morale ≤ 0.4, it retreats.
- **Overrun** (types 1 and 2 only, and only when province+0x106 == 2, NIEPEWNE "Europe"): if one
  side's inf+cav (ships: warships+galleys) is below 10 % of the other's, it is destroyed outright
  (`0x46a460`, constant `0x5e50b4` = 0.1).
- The combat ends when a side is destroyed, retreats or breaks (`0x46a7b0`).

### 7.3 Strength (power value)

Land fire `0x4865e0` and shock `0x486700`, per unit, summed over the side (`0x48f1b0`, `0x48f1e0`):

```
FIRE  = (inf × vIF + cav × vCF + art × vAF × a) × n
SHOCK = (inf × vIS + cav × vCS × c + art × vAS) × n
```

- v** = the owner's current LAND.CSV row, columns /100 (§15).
- a = 0.5 if the province terrain is marsh (4), else 1 (artillery fire halved).
- c = 0.5 if the terrain is forest (1), mountain (2) or marsh (4) (cavalry shock halved).
- n = province+0x134 (float) if the owner is tag index 0x152 (NIEPEWNE: natives), else 1.
- Rebels (tag 0x153) use the tech of the province's controller/owner (`0x43d2f0`).

Assault strength (`0x4869d0` fire, `0x486ac0` shock) ignores cavalry: fire = inf × vIF + art × vAF
(× 0.5 in marsh), shock = inf × vIS + art × vAS.

### 7.4 Losses (`0x46dda0`)

For the side X that takes the losses and its enemy E:

```
row     = E.die + E.modifier            clamped to 0..15
column  = E's CRT letter for this phase  (A=0 … E=4, "NA" = −1)
if E.strength > 0 and column ≠ −1 and E.morale > 0.4:
    X.menPerDay    = CRT_cas[column][row] × E.strength × 0.2     (CRT_cas stored ×0.01)
    X.moralePerDay = CRT_mor[column][row] × 0.2 × F(X)
else:
    X.menPerDay = 0, X.moralePerDay = 0 × F(X) = 0
```

- Strength, column and modifier are the fire ones in a fire phase and the shock ones in a shock
  phase.
- Modifier: fire = leader fire bonus + terrain modifier (`0x46f290`); shock = leader shock bonus +
  terrain modifier + cavalry superiority (`0x46f2b0`). In an assault, without a breach, the fort level
  is subtracted from the attacker's modifiers (`0x470dd0`, `0x470e20`).
- F(X) = sqrt(max(1, M × k)) with M = X's country's maximum morale (§8.1), k = 0.5 for land and
  naval battles, 1/3 for assaults (`0x46dd30`). A side with no leader country (+0x52 = 0) uses 1.
- CRT columns by era: LANDFIRE / LANDSHOCK [our era][their era] (`0x46f0e0`: index = 7×ourEra +
  theirEra), assaults Assault Fire / Assault Shock (`0x470d70`), naval NAVAL FIRE / NAVAL SHOCK.
  "Era" = the LAND.CSV/NAVAL.csv `Era` column of the owner's level (`0x48f7b0` → `0x4335e0`).

Applying (`0x46d9d0`): the men loss is shared over the units in proportion to their men (event 0x36
per unit, `0x5b3780` splits it over inf/cav/art; the split rule is NIEPEWNE, assume proportional).
A unit at 0 men is killed. Morale: new = morale − moralePerDay; if any unit of the side is in a
special state (`0x48b490`, NIEPEWNE: fortress garrison) and the side still has strength, the morale
loss is halved and may not take morale below 0.9. When morale reaches 0 the side's morale is set to
0.4 and it **breaks** (retreat, slot 0x60).

### 7.5 Leader bonuses (`0x46cbd0`)

Only the side's best-ranked leader counts (`0x48ef30`, lowest rank value at leader+0x22).
- Fire bonus = max(0, our leader fire − their leader fire); shock bonus likewise (leader slots 0xc and
  0x10). Zero in assaults (type 5), matching the readme.
- Maneuver (leader+0x26) is used for pursuit (§7.7), attrition (§10.3) and unknown-province movement
  (§11). Siege skill for sieges (§9).
- Leader stats in `DB/leaders/*`: `movement`, `fire`, `shock`, `siege`, `rank`; generic leaders never
  exceed 4 (2 for siege) (manual p.23). Domestic policies modify historical leaders' stats (readme;
  not traced).

### 7.6 Terrain and crossing modifiers (`0x46f0e0`)

Province terrain = province+0xca (Terrain types.csv row; ≥ 5 means sea).

| situation | modifier |
|---|---|
| attacker in mountain (2) | −1 (die) |
| attacker in forest/marsh/mountain with more cavalry than infantry | −1 more |
| plains/desert (not 1, 2, 4): this side's cavalry > 2 × enemy cavalry | +1 shock (cavalry superiority) |
| attacker unit that came across a river (adjacency type 1) | −1 |
| attacker unit that came across a strait (adjacency type 2) | −2 |
| attacker unit landing from a ship (no origin province, not rebels) | −1 |

The crossing modifier is the worst over the attacker's units (it is assigned, not summed). Adjacency
types come from province+0x114/+0x11c (12-byte records: neighbour, type, through) built from the map
and `adj-defs.csv` (`0x47c370`; types: normal, river, sea).

### 7.7 Retreat and pursuit (`0x46d7d0`)

- A side retreats when its morale breaks, it is ordered away, or it is destroyed. Retreat target:
  preferably an own or allied province without enemies (manual); NIEZNANE in code (slot 0x60 →
  event 0x24).
- Pursuit (types 1, 2, 5): if the winner's leader maneuver exceeds the loser's, the loser loses an
  extra `menPerDay × (maneuver difference)` once.
- Rebels never retreat (manual).

---

## 8. Morale

### 8.1 Maximum land morale, `0x438690`

```
M = (landMaintenance + 1) × 0.5 × techMorale        (LAND.CSV Morale of the current level)
  + 0.5
  + religion Morale (religion.csv /100)
  + policy term (below)
  + (MIL − 5) × 0.0625                              (no monarch: MIL = 3)
  + 0.5 if this country is its religion's Defender of the Faith (`0x4312a0`)
× 0.5 while within 5 years after the bankruptcy date (country+0xe9c)
clamped to 0.75 .. 7
```

Rebels (tag 0x153): M = (year − 1400) × 0.01 + 0.6.

Policy term (`0x42b990`, sliders s_i with d_i = s_i − 5, order as in the economy spec §3.1):

```
0.04 × d4 + (0.1 × d5 if d5 > 0) + 0.05 × d6 − 0.05 × d7
```

(The readme says the offensive slider ranges ±20 %, the land slider gives up to +50 % and no penalty;
this matches d4 × 0.2 × 0.2 and d5 × 0.5 × 0.2.)

### 8.2 Maximum naval morale, `0x438850`

Same shape with the naval slider, NAVAL.csv Morale, policy term `0x42ba00` = 0.1 × |d5| when d5 < 0
(naval end of the slider, readme "only beneficiary"), Defender of the Faith +0.15, clamp 0.5 .. 7.

### 8.3 Current morale

- Unit morale +0x58; a side's morale is the men-weighted average (`0x48f220`); merging units averages
  the same way (`0x46cd70`, readme 1.06).
- New units start at the lowest morale and recover monthly to the maximum (manual). The monthly
  recovery rate is **NIEZNANE** (unit month handler not read).
- A side that has broken or lost still keeps its reduced morale; pursuers attack at an advantage.

---

## 9. Sieges, assaults, fortresses

### 9.1 Modes (`0x470110`, siege combat class `0x5e8324`)

| mode | meaning |
|---|---|
| 3 | covered: the city is watched (no siege progress) |
| 4 | siege: a test every 30 days (`0x417130(0x1e)`) |
| 5 | assault: runs as a combat with assault tables, no cavalry, no terrain |

Strength checks every test (`0x470870`): besieger men < 1000 × fort level → cannot even cover
(the besieger retreats). Men < 5000 × fort level → mode 3 (cover only). Otherwise the siege
continues. "Fort level" = the garrison side's slot 0x3c, stored as city+0x96.

### 9.2 City siege fields (city object, province+0x408)

| offset | meaning |
|---|---|
| +0x82 | breaches |
| +0x86 | artillery rating 0..3: art pieces ≥ 40 × L → 3, ≥ 25 × L → 2, ≥ 10 × L → 1, else 0 (`0x4703b0`) |
| +0x8a | besieging leader's siege value |
| +0x8e | garrison distress (+1 food, +3 water, +2 breach; capped at 8; forced to 15 if the garrison has < 100 men) |
| +0x92 | defending leader's siege value |
| +0x96 | fort level L |
| +0x9a | defender base (NIEPEWNE: terrain/garrison quality) |
| +0x9e | starvation (float): +0.3 per test when the province has no port (+0x41c = 0) or is blockaded (+0x489), else −0.3, floored at 0 |

### 9.3 Siege test (`0x4704a0`)

```
roll = rand() % 10 + 1                                         (1..10)
mod  = (besieger siege + artillery rating)                    (`0x471880`)
     − (base + fort level + defender siege − trunc(starvation) − distress)   (`0x4718d0`)
total = roll + mod
```

| total | result |
|---|---|
| ≤ 2 | nothing |
| 3–4 | status quo |
| 5–7 | food shortage: distress +1, garrison side +0x28 += 0.01, garrison loses that fraction (`0x48f620`) |
| 8–10 | water shortage: distress +3, +0x28 += 0.03, losses |
| 11–12 | **breach**: breaches +1, distress +2 |
| 13 | desertion: +0x28 += 0.1, losses |
| ≥ 14 | **surrender**: the garrison is removed and the province changes control |

Garrison side +0x28 is a cumulative attrition fraction; at each test it is first reduced by slot 0x38
(NIEPEWNE: recovery). The UI estimate ("can hold out for years" … "any day now", `CRBSIEGE1..6`)
is derived from these values.

### 9.4 Assault

- Allowed from land tech 5 ("Allows Assault"), needs infantry (`ASSAULT_NOINF`).
- Uses the Assault Fire / Assault Shock tables for the attacker and Assault Defender for the
  defender (`0x46e820` loads them; side `0x470d70` picks columns by era).
- Without a breach the fort level is subtracted from the attacker's fire and shock modifiers
  (`0x470dd0`, `0x470e20`). With at least one breach it is not ("the fortress will be useless").
- Morale damage factor uses k = 1/3 (§7.4). Leader fire/shock bonuses are 0.
- Success lowers the fort one level, never below 1, and may plunder the town (manual). The exact
  plunder rule is NIEZNANE.

### 9.5 Fortresses

- Build cost 100 ducats × (1 + inflation) (manual). Maximum level by land tech: 1 at level 0, 2 at 2,
  3 at 11, 4 at 21, 5 at 26, 6 at 41 (LAND.CSV `Effect`).
- A province without a fortress is controlled as soon as an enemy army enters it (manual).

---

## 10. Supply and attrition

### 10.1 Supply value of a land province, `0x483570(prov, country)`

```
S = baseTax × m
m = 5  owner is us / our vassal / gives us access, and the controller is us or friendly
    4  we own it but it is controlled by someone else, or the owner is our ally
    3 or 2 otherwise (enemy or neutral territory; NIEPEWNE exact split)
×2 if the province has a port, is blockaded, and the controller is not us (blockade of a port under siege)
if the province is ours or friendly: + 10 × fortress level (city), + 10 with a conscription centre (city+0x69)
```

Tooltip texts `PROV_SUPPLY_1..3`. (Readme 1.06: access provinces got the friendlier rate.)

Decoded (2026-11-10). "Side" = C itself or on C's side in a war (`0x45fa70`: dip record +0x3d),
for the owner O and the controller K:
- no owner: m = 1;
- (O is side, or O is C's vassal: `0x4cbe90`) and K is side: m = 5;
- O is side (K is not): m = 4;
- `0x4cb880` for O (dip byte +0x41; NIEPEWNE, ours: a military alliance): m = 4;
- K is side: m = 3; otherwise m = 2.
Then ×2 for a blockaded port (city port, +0x489) whose controller is not C; and when O = C or m came
from the 5 / 4 cases: + 10 × fort level, + 10 with a conscription centre (city+0x69).

### 10.1b The month's weather of a province, `0x4825b0`

Run first in the province monthly (`0x480377`) and once at the start (`0x480a66`). With the date
(day d, 0-based month m, year y), v = d + m + (12·y + m)·30 + prov+0x58 + last winter (+0x3e2) +
owner+0x6 (if owned) + trunc(last wind / 2):
- storm (+0x248) = Storm? column (+0xa6) set and v mod 100 < 30;
- wind (+0xa2) = (v/2) mod 7 + 1, +5 in a storm, −5 if the province's +0x10e is 0x2c or 0x2d
  (NIEZNANE which regions), clamped 0..9; the sea window shows wind × 5 m/s;
- winter (+0x3e2) by climate (+0xc6) with e = v/2: climate 0: Nov–Mar yes, Apr/May if e even, Oct if
  e mod 4 ≠ 0; climate 7: Dec–Feb yes, Mar if e even, Nov if e mod 10 < 4; climate 5: Jan e mod 5 = 0,
  Feb e mod 20 = 0, Dec e mod 10 = 0; climate 8 (south): Jun e mod 5 = 0, Jul e mod 4 = 0, Aug
  e mod 10 = 0; other climates never.
Users: sea window and map tooltip (icy = Ice? and winter; stormy = storm), sea supply (§12),
maximum attrition +10 and unit attrition +10 (+15 more outside the unit's own province) in winter.
Ours: prov+0x58 = the province index, owner+0x6 = our country index (NIEPEWNE), no −5 regions.

### 10.2 Maximum attrition of a province, `0x4837f0` (tooltip `0x4831f0`, `PROV_ATTRITION_*`)

```
at sea: 15
else: max(1, 10 − landTechLevel / 6)
    + 10 if out of supply (no supply line, `0x483b60` and neighbours)
    + 10 winter (province+0x3e2)
    + 5 tropical (climate +0xc6 = 4 or 6)
    + 5 looted (province+0x3cc)
    + 2 marsh, + 5 mountain, + 8 desert (climate 1)
```

Decoded in full (2026-11-10), arguments (province, country C, besieging):
- terrain +0xca = 5 (sea): 15. Base = max(1, 10 − land tech / 6).
- **besieging** (the unit is in a siege combat, `0x48df30`, and unit+0x52 is clear): only the base,
  +10 when the province has no port (+0x41c) or is blockaded (+0x489). Nothing else.
- otherwise +10 out of supply unless the province is a supply base for C or one of its neighbours is
  (each with +0x80 > 0; +0x10a = 0x94 and +0x12 = 0 are special cases). **Supply base** `0x483b60`:
  a controller exists; the owner is C's friend (`0x43a530`: C itself, on C's side in a war, or
  C's overlord, `0x4cbe90`) or hostile to C (`0x43b6e0`), i.e. not a neutral; and the controller is C's friend,
  or, while the city is besieged, the besieger is.
- then winter +10, climate 4/6 +5, looted +5, terrain 4 (marsh) +2, terrain 2 (mountain) +5,
  climate 1 (desert) +8. Terrain codes: Map/Terrain types.csv.
Ours: +0x80 > 0 read as "has people" (NIEPEWNE). `0x4cbe90(dip of X, Y)` = Y is X's vassal (a type-2
  vassalage object led by X); `0x4cb900` = friendly: royal marriage (+0x3b), the same military alliance
  (+0x3c), on the same side (+0x3d), vassalage.

### 10.3 Monthly attrition rate of a land unit, `0x485ff0` (tooltip `0x485d30`, `LU_ATTRITION_*`)

```
a = size of ALL land units in the province: ceil(inf/1000) + ceil(cav/1000) + ceil(art/10)   (`0x48f9a0`)
  + 10 in winter, + 15 more if the province is not owned by this unit's country
  + 5 tropical
  − 2 × leader maneuver   (× 4 for a conquistador, leader type 2)
  − supply value S (§10.1)
a = min(a, maximum attrition of the province)
if 0 < a < 1000 / men: a = 1000 / men
minimum: 1 %, or 0 for a conquistador or a unit on the move/aboard (slot +0xc4)
```

The result is a percentage; the monthly loss is a % of the unit's men (the application site was not
read; NIEPEWNE whether it is randomised). Owner tag index 0x150 has no attrition. After losses
`0x4d1fd0` removes units at 0 men and lifts a siege whose besieger can no longer cover.

### 10.4 Supply lines and pillage

From the manual (p.28-29): a line to a supply base through own/allied-controlled provinces without
enemy troops or uncovered enemy forts; bases are own controlled towns/colonies and own-controlled
unblockaded coastal provinces; trading posts are not bases. Out-of-supply adds 10 to the maximum
attrition. Provinces where units suffer high attrition may be looted (12 months, no income, one-time
income to the looter). Code for the line search: **NIEZNANE**.

**Looting, decoded** (2026-11-10). In the army's monthly attrition amount (vfunc +0x34 = `0x4861a0`),
after the rate (vfunc +0x2c) is taken: if the unit is **not supplied** (`0x48e930` false) and may loot
(`0x486210`), it loots (`0x4862a0`).
- Supplied (`0x48e930`): false if unit+0x54 is set or the province has no owner; true only when the
  owner is the unit's country, its vassal (`0x4cbe90`) or a friend (`0x43b740` → `0x4cb900`: royal
  marriage, the same military alliance, the same side, vassalage), **and** `0x483940` (in supply: the
  province or a land neighbour controlled by us or a friend) holds.
- May loot (`0x486210`): not carried (unit+0xf4), a land province (+0xca < 5), not looted already
  (+0x3cc), a city (+0x408) that is not a colony (city vfunc +0x10), owned by the unit's country or by
  a country hostile to it (`0x43b6e0`).
- Loot: the unit's country gets command 0x5f with **2 × base tax** (+0xb6); the province gets
  command 0x5d (`0x480a8d`): +0x3cc = 1 and a timed command 0x5e one year later that clears it
  (`0x480b74`); if the owner is not the looter and the city has a manufactory (+0x1c),
  `0x4d97c0(province, 5)`: with rand() % 100 < 5 it posts command 0x7d to the owner, whose handler
  (`0x4534c0` → `0x496229`) clears city+0x1c / +0x20 - the manufactory is destroyed.
- While looted: no tax (`0x457a2a`) and no production (`0x457aa5`) in the country sums,
  population growth −5 (POPGRWTH_LOOTED), maximum attrition +5 (PROV_ATTRITION_6).

---

## 11. Movement

### 11.1 Time to move one edge, `0x41d160`

```
if a ship moves between land and sea (port in/out): days = trunc(15 / speed)
else:
    cost = destination+0xd4 (int, NIEPEWNE per-province extra) + terrain factor (Terrain types.csv
           "Movment"; values ≤ 0.0001 count as 1)
    + 0.5 if the edge is a river, + 1.0 if it is a strait
    × 2 if the destination field +0x106 == 5 or its climate +0xc6 == 0 (NIEPEWNE: arctic/ice)
    × U if the destination is not known to the owner (terra incognita)
    × 0.5 for a land unit (dis)embarking where the land side has a port, is friendly-controlled and not blockaded
    days = trunc(cost × 30 / speed)
minimum 1 day (`0x4de020`); a missing adjacency costs 30 days (`0x41d110`)
```

U (`0x48dfa0`) = 1.5 − 0.15 × leader maneuver + (5 for a general, 4 for an admiral (type 1),
2.2 for a conquistador, explorer or monarch) - corrected in SPEC_EXPLORERS.md 3.3 (type 1 is the
admiral, not the conquistador). Winter makes the pathfinding cost +0.5 per end (`0x41d6c0`); NIEPEWNE
whether the real move time also includes it (the travel formula above has no winter term).

Land speed `0x485cd0` (the slowest branch moves the stack, manual): 1.5 if any artillery, else 2 if
any infantry, else 3 (cavalry only). Plains, infantry: (0 + 1.0) × 30 / 2 = **15 days** per province
if +0xd4 is 0. Naval speed: **NIEZNANE** (naval vtable slot 0x50 not read; the manual says warships
and transports get faster with tech, galleys do not).

### 11.2 Path search

A* style search (`0x41d2f0`, heap `DAT_00648250`) with edge cost = terrain cost as above and a
distance heuristic `0x41d780` = Euclidean distance between province centres (+0x3ec/+0x3f0, map
wraps at x = 0x4a00) × 0.012 on land, × 0.002 at sea. An AI variant (`0x41dab0`) adds 3 for
provinces with revolt activity (+0x12c > 0).

### 11.3 Movement rights

Peace: own and vassal provinces (and access). War: also allies' provinces and enemy provinces
(manual p.18, `DIPROL_ASKACC`). An enemy entering a province stops your move there and starts a battle.

---

## 12. Naval combat, naval attrition, blockade

Summary level.

- Same combat engine as land (type 2). Fire = warships × WarshipFire × w + galleys × GalleyFire; shock
  = warships × WarshipShock × w + galleys × GalleyShock (`0x489740`, `0x4897e0`). w = 1 − LowWind/12
  (NAVAL.csv "Low Wind", stored by `0x431d10`) when the sea zone has no wind (zone+0xa2 = 0), else 1.
  Transports do not fight.
- CRT columns from NAVAL FIRE / NAVAL SHOCK by era; morale factor uses naval morale (§8.2).
- Wind Gauge.csv is not loaded by the combat loader (`0x46e820`); how the wind bonus is applied is
  **NIEZNANE**. The manual says only one side gets it; a blockading fleet always gets it when the
  blockaded fleet sorties.
- Interception: always succeeds in some straits (Scania, Zealand, Bosphorus, Gibraltar, Tangiers);
  fleets outside their own coast get a bonus (manual). Formula **NIEZNANE**.
- Naval attrition (`NU_ATTRITION_*` texts, `0x488fa0`): + months at sea, + blockading, − near coast,
  − leader, cap; zero in a friendly port, which also resets the months-at-sea count. NAVAL.csv
  "Attrition" is the tech factor (`0x433810`). No time-at-sea attrition from naval tech 41 (readme).
  Sea "supply" (`0x483570` for sea zones): +10 storm, +10 ice, −3 coastal, −2 more next to an own port.
- **Naval attrition, decoded** (2026-11-10). The monthly unit upkeep (`0x4de5a0`, not for PIR) posts
  command 0x36 (`0x48e660`) with the loss **ships × rate** (navy vfunc +0x34 = `0x4896f0`; ships =
  vfunc +0x8c = ceil(warships) + ceil(galleys) + ceil(transports)). The rate (`0x489470`, percent × 0.01)
  for fleet F of country C in zone Z:
  1. 0 if Z is a port province (+0x41c), or any neighbour of Z is owned by C ("friendly waters"),
     or F has no ships.
  2. a = sea supply S(Z, C) (`0x483570`, sea branch `0x48371f`): s = 10 if the zone's storm byte
     (+0x248) is set and C's naval tech < 38 (`0x432e70`); +10 if ice (+0x9e) and the winter byte
     (+0x3e2) and naval tech < 41 (`0x432e80`); if Z touches ports (+0x488): −3, and −2 more when a
     neighbouring port's controller is C, on C's side in a war (dip +0x3d) or C's overlord (`0x4cbe90`).
  3. Unless naval tech ≥ 41: a = trunc(Attrition(C's naval tech) × 2 × months + a), months =
     (year − F+0xfa) × 12 − F+0xf9 + month while Z is a sea zone (F+0xf9/+0xfa: when it last lay in port).
  4. Leader (`0x48c040`): type 3 (explorer) a −= 2 × byte +0x26 and the floor below becomes 0;
     otherwise a −= byte +0x26.
  5. If Z touches ports and a neighbour is blockaded (+0x489) with a controller hostile to C
     (`0x43b6e0`): a −= 3 (shown as "Blockading").
  6. F+0xc4 vfunc +8 true: floor 0 (NIEZNANE what it is). Default floor 1.
  7. cap = 100 / ships (integer); a = min(a, cap); if 0 < a < cap/10: a = cap/10; a = max(a, floor).
  So a fleet loses at most about one ship a month. The tooltip `0x488ff0` lists the same terms
  (NU_ATTRITION_I0..I8).
  Ours: ship counts are integers, so the loss accumulates in thousandths and a whole ship is taken
  from the most numerous kind (NIEPEWNE). Storm and winter are the month's weather (below).
- Blockade: a fleet in a sea zone next to an enemy port blockades it (any tech since 1.0x per readme;
  NAVAL.csv still says level 26). Effects found: province+0x489, siege starvation (§9.2), supply ×2
  for besiegers (§10.1), tax penalty (economy spec §4.1 step 4).

---

## 13. Revolt risk and rebels

### 13.1 Revolt risk of a province, `0x481a30` (answers economy spec open question 6)

Integer. 0 if the province has no owner, is a colony/trading post (+0x40c), or its city is not a
full city. C = the controller (province+0x3c8). r = province religion, with Protestant and Reformed
counted as Catholic until the Reformation flags (`0x4d16a0(1)`, `(2)`) are set.

```
t = trunc(1 − C.tolerance[r])
if r is compatible with C's state religion (`0x431270` = 0):  rr = t, doubled if t > 0
else:                                                      rr = t, +2 if C is its religion's Defender of the Faith
if rr < 0: rr = rr / 2 (toward zero)
+1 if the province culture is not accepted by C, +1 more if C is at war
rr = trunc(rr − (C.stability + 2))
−1 if city+0x1c ≠ 0 (NIEPEWNE: manufactory), −2 if city+0x2c (NIEPEWNE: garrison/fort flag)
+1 if owner ≠ controller
+2 if any neighbouring province is controlled by rebels (0x153)
+ trunc(event modifiers + war-exhaustion term):
      war-exhaustion term = trunc(C.warExhaustion) × m, m = 1 for a core, otherwise
      1.25 (accepted culture, same religion), 1.5 (one of the two differs), 2.0 (both differ)
+1 while C has war taxes active
+1 within the first year after C's bankruptcy
+3 if city flag +0x6a, −1 if city flag +0x6c          (economy spec §3.2)
+ C+0x1000 + province+0xc2 (NIEPEWNE: event/scenario revolt risk)
non-core, non-pagan province: +3 / +2 / +1 if acquired less than 10 / 20 / 30 years ago (+0x3be year),
      and that bonus is also the minimum result
```

The ordering of the tolerance sign is as decoded; that "same religion" doubles a positive term looks
odd and is NIEPEWNE until the tolerance storage convention is checked against the tooltip.

### 13.2 Revolt roll

Monthly per province (economy spec §4.2 step 1): r from field +0x13c (believed to be the stored revolt
risk), r²-shaped chance per 1000. Readme 1.05 says revolt risk became "yearly"; the monthly roll with
small per-mille chances is consistent with that. Rebellions also come from events and from stability
below −3 (each missing step, `0x460450`).

### 13.3 Rebel army size, `0x461780(country, province)`

```
S = 2 × stability(country) + rand() % 4 + baseTax / 2 + 4
    + city size (city+0x6e) + trunc(city+0x3a) / 2              (if a city exists)
A = 0.5 if the country has fewer than 3 provinces, else 1.5 − 0.1 × s1 (centralisation slider)
B = 1.0 if province+0x106 == 2 (Europe), else 0.75
C = 0.5 if province+0x10e == 6, 1.1 if it is 12, 20 or 25, else 1   (NIEPEWNE meaning of +0x10e)
men = S × A × B × C × 500
if s0 (aristocracy slider) > 7 and province+0x106 ≠ 1: infantry = 0.75 men, cavalry = 0.25 men
else infantry = men
artillery pieces = (year − 1550) / 25, floored at 0
non-city (population < 5000): infantry capped at the population
city: infantry raised to at least 5000 × fortLevel + 1000
religious revolt (`0x482970`, after the Reformation, revolt risk > 10): infantry ≥ 5000 × L + 1000,
      cavalry = infantry / 3, artillery = (L + 2) × 5
minimum 100 infantry
```

Garrison defection: if the city's garrison belongs to the owner, chance = min(40, 2 × RR − 5 × L) + 1
percent; on success the province switches control to the rebels at once (event 0x8d to 0x153) and the
rebel army is halved. The army is created with event 0x88 for tag 0x153.

Rebels: tech of the country they revolt from (§7.3), morale (§8.1), never retreat, dissolve when
their morale breaks (manual). Large forts need a much larger revolt risk to be lost (readme).
Rebel-held provinces defect to existing countries after a while or form revolters from `revolt.txt`
(`0x5b15f0`); that logic is **NIEZNANE**.

---

## 14. War exhaustion inputs

Monthly rule in the economy spec §8.4. Decoded here: the list `DAT_008ff614` is the **war list**, and
the date it tests is war+0x1a, which every battle start between members of that war refreshes
(`0x469960`). So "recent military action" = a battle within the last 35 days. Other inputs from the
readme: recruiting the whole yearly manpower adds +1; war taxes +1 (`TWR_WT`); Defenders of the Faith
gain at 80 % (the 1/45 versus 1/36 of the economy spec).

---

## 15. Data files: which column goes where

- **LAND.CSV** (`0x431e60`, row object): Level; date → cost; vIF, vCF, vAF, vIS, vCS, vAS (/100 →
  +0x10..+0x24, getters `0x433400`..`0x433590`); vSize (+0x28, NIEPEWNE use); Morale (+0x2c); Sprite
  (+0x30); Era (+0x34, CRT index, `0x4335e0`).
- **NAVAL.csv** (`0x431d10`): WarshipFire +0x18, WarshipShock +0x20, GalleyFire +0x1c, GalleyShock
  +0x24 (/100); Attrition +0x28; Morale +0x2c; Wind +0x10; Low Wind → +0x14 = 1 − LowWind/12.
- **combat/CRT.csv** (`0x46e820`): 16 rows = die total 0..15; 5 column pairs (casualty %, morale) for
  letters A..E. Casualty stored ×0.01.
- **combat/LANDFIRE, LANDSHOCK, NAVAL FIRE, NAVAL SHOCK, Assault Fire, Assault Shock, Assault
  Defender**: 7×7 letters, row = our era, column = their era.
- **combat/Wind Gauge.csv**: not loaded by `0x46e820` (NIEZNANE whether used).
- **Map/Terrain types.csv** (`0x485920`): movement factor per terrain id (doubles at `0x80a670`).
- **Map/adj-defs.csv**, **DB/straits.csv** (`0x47c370`): extra adjacencies; type normal / river /
  sea (strait), "through" sea zone.
- **DB/DiplomaticMatrix.csv**: a tag × tag table of small integers (−10..+10). Its loader and meaning
  are **NIEZNANE** (candidate: the scenario-start grievance table at country+0x14de or the AI's
  attitudes).
- **leaders**: movement, fire, shock, siege, rank (§7.5). **randomleaders.csv**: names only.
- **revolt.txt**: revolter definitions (`0x5b15f0`).

---

## 16. Worked example: France against England, 1419

Inputs (`Scenarios/1419/1419_fra.inc`, `1419_eng.inc`, `DB/country.csv`, `DB/LAND.CSV`):
- France "Armée du Roi": 15 000 infantry, 4 000 cavalry. England: 12 000 infantry, 2 000 cavalry.
- Both land tech 1: vIF = vCF = vAF = 0, vIS = 0.5, vCS = 4.0, vAS = 4.5, Morale 1.60, Era 0.
- Plains, no river, no leaders, both Catholic. Assumptions: monarch MIL 5 for both, full maintenance,
  units start at maximum morale, loss split proportional (§7.4, NIEPEWNE).

**Maximum morale** (§8.1). FRA policies (8,3,2,6,5,5,4,8): term = 0.04·0 + 0 + 0.05·(−1) −
0.05·3 = −0.20 → M = 1.6 + 0.5 − 0.2 = **1.90**. ENG (8,3,2,6,5,5,7,7): 0 + 0 + 0.1 − 0.1 = 0 → **2.10**.
Morale factors F: FRA sqrt(max(1, 0.95)) = 1.000; ENG sqrt(1.05) = 1.025.

**Strength.** Fire is 0 for both (all v*F are 0), so every phase is shock.
Shock FRA = 15 000·0.5 + 4 000·4.0 = 23 500; ENG = 12 000·0.5 + 2 000·4.0 = 14 000.
Cavalry superiority: FRA 4 000 is not > 2·2 000, so no bonus. Modifiers 0. Columns:
LANDSHOCK[Medieval][Medieval] = A for both.

**Phase 1** (days 1-5), dice FRA 6, ENG 3.
- England takes A row 6 (15 %, 1): 0.15·23 500·0.2 = **705 men/day**, morale 1·0.2·1.025 = 0.205/day.
- France takes A row 3 (5 %, 0.25): 0.05·14 000·0.2 = **140 men/day**, morale 0.25·0.2·1 = 0.05/day.
- After 5 days: ENG 10 475 men, morale 2.10 − 1.025 = 1.075. FRA 18 300 men, morale 1.65.

**Phase 2**, dice FRA 4, ENG 5. New strengths (proportional losses): FRA 14 447 inf + 3 853 cav →
22 635; ENG 8 979 + 1 496 → 10 475.
- ENG takes A row 4 (5 %, 0.5): 226 men/day, 0.1025 morale/day → morale 0.56 after the phase.
- FRA takes A row 5 (10 %, 0.5): 210 men/day, 0.10 morale/day → morale 1.15.

**Phase 3**, dice FRA 5, ENG 2: England takes A row 5 (10 %, 0.5) → morale 0.56 − 5·0.1025 ≈ 0.05.
England's own morale was still above 0.4 at the phase start, so France keeps taking losses this phase
(A row 2: 2 %, 0.25). Phase 3 strengths: FRA ≈ 21 340 → England loses ≈ 427 men/day; ENG ≈ 9 340 →
France loses ≈ 37 men/day. On day 1 of phase 4 England's morale reaches 0: it breaks, its morale is
set to 0.4 and it retreats. No pursuit losses (no leaders). Total, roughly: England −6 800 men,
France −1 900.

Note: morale damage does not depend on the size of the enemy; size only drives men lost. The battle is
decided by dice, CRT letters and maximum morale.

**Movement check:** the same French infantry moving through one plains province: trunc((0 + 1.0) ×
30 / 2) = 15 days (if province+0xd4 is 0).

**Siege check:** after the battle France besieges an English fort level 1 with 18 000 men: ≥ 5 000 ×
1, so the siege runs. With no artillery (rating 0) and no leaders, mod = −(base + 1 − starvation −
distress). With base 0 the first test needs roll ≥ 6 for food shortage and 15 (impossible at once) to
surrender, so the fort falls only after distress and starvation have built up (months).

---

## 17. Precision notes for the fixed-point port

- **Relations** move by 0.02..0.42 per month: store ×100 or as 16.16; clamp ±200.
- **Losses per day** = table × strength × 0.2; strength reaches ~10⁶ late game (100 000 inf × 2.5 +
  art 9250 × 100 pieces). Use 32-bit integer men with a 16.16 product, or 24.8.
- **Morale** 0..7 with daily steps of ~0.05..0.4: 8.8 is too coarse; use 16.16 (or ×1000 integers).
- **sqrt** in the morale factor: only of 1..3.5 (M×0.5); a 64-entry table or one Newton step is enough.
- `__ftol` truncation toward zero matters in: the revolt-risk halving of negatives, the attrition size
  ceilings, siege starvation, rebel size and movement days. Reproduce exactly.
- `rand()`: the original uses the MSVC LCG (`rand() % 10` dice, `% 100` leader death and defection,
  `% 4` rebel size). Bit-exact replay would need that LCG; gameplay only needs the ranges.

---

## 18. Open questions

1. Battle points added to the war matrix per battle (§5.1): increment site not found.
2. Peace acceptance (AI) and the accept handler `0x44d5b0` (badboy for provinces, relation effects).
3. AI acceptance of marriages, alliances, vassalisation, annexation, access (§3.3).
4. Diplomacy flags +0x36, +0x37, +0x38, +0x39, +0x40, +0x41 and the dates at +0x26/+0x2a: confirm the
   names (embargo, insult, access, guarantee, trade agreement, warning) against the savegame writer.
5. Gift value on the sender side (DIP and luck) (§2.4).
6. Monthly morale recovery rate; naval speed; naval interception and wind gauge.
7. Supply multiplier split (×2 versus ×3) for enemy/neutral provinces (§10.1); supply-line search.
8. Province fields: +0xd4 (movement extra), +0x106 (continent ids 1 = America?, 2 = Europe?, 5 = ?),
   +0x10e (rebel size factor), +0xc2, +0x9a city base, city+0x3a, +0x134 (natives).
9. `0x48b490` unit state that halves morale loss with a 0.9 floor.
10. Loss split over infantry/cavalry/artillery (`0x5b3780`).
11. DiplomaticMatrix.csv: loader and use.
12. The sign convention of religious tolerance in revolt risk (§13.1).
13. Royal-marriage stability penalty on declaration: the manual says −1, the declaration code has no
    such term.
