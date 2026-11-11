# Explorers, conquistadors and terra incognita

This file records rules and layout facts only, read from the `eu2.exe` v1.09 disassembly and the
Ghidra export. No code was copied.
- `prov` = province record `0x8077d4 + id·0x4A0`; `country` = country object; `tag` = country
  index (`country+6`); `unit` = army/fleet object; `L` = leader object.
- Known flag: byte `prov[0x256 + tag]` (1 = province known to that country).
- `prov+0x3c4` owner, `+0x3c8` controller, `+0xca` terrain (0-4 land, 5 ocean, 6 river
  pseudo-province), `+0x10a` region (0x94 = the "Sea" region, 0x95 = "Lake"), `+0x106` continent
  (0 "-", 1 America, 2 Europe, 3 Asia, 4 Oceania, 5 Africa; name table `0x61dc74` `CON_*`),
  `+0x114/+0x11c` adjacency count/list (12-byte records), `+0x41c` port.
- Country 0x151 = pirates (as already noted in SPEC_UI). Player type per tag `0x91a738[tag]`:
  0 AI, 1 local human, 2 remote human (switch in `0x43b790`, player lookup in `0x4198e0`).

## 1. Leader records

### 1.1 Object layout (ctor `0x487fc0`, copy `0x487f60`, file reader `0x4b2630`)

| Offset | Type | Meaning | DB keyword (token id) |
|---|---|---|---|
| +0x10/+0x12 | short/int | id `{ type id }`; +0x12 indexes the leader table `0x8ff5ee+10+id·4` (< 200000) | `id` (0x238) |
| +0x16 | date | death date (random leaders: today + 15 years) | `deathdate` |
| +0x1a | date | start date (random leaders: today) | `startdate` |
| +0x1e | int | owner tag (set by `0x488120` together with +0x32) | – |
| +0x22 | int | **rank**, lower = more senior; ctor default 100 | `rank` (0x5b) |
| +0x26 | char | **movement / maneuver** | `movement` (0x5c); token 0xea also lands here (NIEPEWNE which keyword) |
| +0x27 | char | **fire** | `fire` (0x5e) |
| +0x28 | char | **shock** | `shock` (0x5f) |
| +0x29 | char | **siege** | `siege` (0x60) |
| +0x2a | ptr | unit the leader is attached to (0 = none; set `0x487e20`) | – |
| +0x2e | int | **category** (below) | `category` (0xe9) |
| +0x32 | ptr | owner country | – |
| +0x36 | byte | arrival already scheduled | – |
| +0x38 | short | start **location** province (0 = capital) | `location` (0x26) |
| +0x3a | byte | dormant | `dormant = yes` (0x221) |

Fields `name` (0x19) and the dates are parsed too; tokens 0xe6/0xe7 are accepted and ignored
(NIEPEWNE: `remark` / `special`). Anything else gives `Leader : unexpected field` and aborts.

Note: SPEC_LEDGER says "type +0x29"; in the object +0x29 is siege, the type is the int at **+0x2e**
(proof: `0x488140` uses +0x2e to index the name table `0x61fdf4`).

### 1.2 Category mapping (`0x4b2630` at `0x4b2788`; save writer `0x4b2a60`)

| `category =` | +0x2e | name key (`0x61fdf4`) |
|---|---|---|
| general | 0 | GENERAL |
| admiral | 1 | ADMIRAL |
| conquistador | 2 | CONQUISTADOR |
| explorer | 3 | EXPLORER |
| monarch | 4 | MONARCH |
| privateer | **3** when read (the writer saves type 5 as `privateer`) | PRIVATEER (5) |

Unknown value: `unknown leader type`. **The reader turns `privateer` into an explorer (type 3).**
The 10 pirate privateers in `leaders.pir` / `leaders.eng` therefore get all explorer rules. Type 5
can only come from code (`0x4d8240` handles it like 3).

### 1.3 Files

- `db\leaders\leaders.<tag>` (`0x4b0f10`, format string `0x622390`, tag = country+0x124), blocks
  `historicalleader = { ... }`. One leader per id; a reused id logs `Attempting to create a leader
  with a leader-id(%d) that is already used.` (`0x4d7bb0`).
- `leaders.txt` (`leadertable = { tag = .. leader = {.. negotiation fidelity ..} }`) has a parser
  (`0x488420`, `LeaderTable : unexpected field`), but the exe has no `leaders.txt` string; whether it
  is loaded is NIEZNANE. `negotiation`/`fidelity` are not fields of the leader object.
- `db\leaders\randomleaders.csv` (`0x625334`): names only.

### 1.4 Stats per category

Every category has all five stats in the file. Ranges in the v1.09 data (all `leaders.*` except
`leaders.txt`):

| category | n | movement | fire | shock | siege | rank |
|---|---|---|---|---|---|---|
| general | 611 | 1-6 (avg 3.2) | 1-6 | 0-6 | 0-4 | 0-18 |
| admiral | 197 | 1-6 (3.5) | 0-6 | 1-6 | 0-1 | 0-15 |
| conquistador | 90 | 2-6 (4.1) | 1-6 | 1-6 | 0-2 | 0-19 |
| explorer | 78 | 1-6 (4.3) | 0-5 | 0-5 | always 0 | 0-14 |
| privateer | 10 | 5-6 | 1-6 | 2-6 | 0-3 | 25 |

What the code uses per type:
- **Conquistador fire and shock are 0 in Europe.** The fire getter (`0x488160`, vtable slot 3)
  and shock getter (`0x4881b0`, slot 4) return the stat (+ monarch term when +0x32 set, clamped
  ≥ 0), but for type 2 attached to a unit whose province has continent 2 (Europe) they return 0.
  Same rule in the second getter pair `0x4886a0`/`0x488700` (vtable `0x5e8aa8`).
- Movement: attrition (×4 for type 2, SPEC_WAR §10.3), terra-incognita move cost (§3.3), coastal
  discovery chance for fleets (§4.2), pursuit, retreat choice (`0x487960`: `rand()%10 < maneuver`).
- Siege: generals/monarchs; admirals and explorers get siege 0 only when randomly generated.

### 1.5 Randomly generated leaders (`0x4d86f0` + `0x488230`)

Used by event commands `conquistador`/`explorer` (§2.3), `REQUEST_NEW_LEADER` 0xb4 and rebels.
- Name: random from the culture list `0x8a2318 + culture·16` (culture `0x64f83c[tag·0x51]`),
  else the country name.
- Category: given; −1 means general, or admiral when the target unit is a fleet.
- rank 25; movement 2, but **3 for conquistador/explorer**.
- shock = trunc((s4 − 5)·0.25 + 2), fire = trunc((s6 − 5)·0.25 + 2), siege = trunc(−(s4 − 5)·0.25),
  each clamped ≥ 0. s4 = policy +0x18, s6 = policy +0x20 (country+0x37e block; the names
  offensive/defensive and quality/quantity are the SPEC_TIME_ECONOMY §3.1 guess, NIEPEWNE).
- siege forced to 0 for admiral and explorer.
- start = today, death = today + 15 years (date add `0x4173c0`, +0x000f0000).

## 2. How a leader enters the game

### 2.1 Scheduling (`0x4d7bb0`, called per leader by the loaders)

The leader is registered in the id table. When it is not dormant (+0x3a), not yet scheduled
(+0x36) and the date check passes (NIEPEWNE: not already dead), a dated command
**`ARRIVE_LEADER` (0xb6)** is queued (vtable `0x5e7aa4`) and +0x36 = 1. If the date has day and
month both 0 (year only in the file), day = rand()%30 and month = rand()%12 are set first. Dormant
leaders arrive only through the event commands `leader`/`wakeleader` (`0x594eb0` case 0x20).

### 2.2 Arrival (`0x4d7ed0`, handler of 0xb6): one code path for every category

Needs: leader exists, not dormant, owner country exists. It sets the owner, picks or creates a
unit (`0x4d8240`) and queues **`ACTIVATE_LEADER` (0xb2)** (`0x4d7f70`, handler `0x450010`) to
attach the leader. A message of type 0x23 follows (`0x55aa00`), believed to be `LEADERNEW_*`
("New Leader!", "%s awaits your command in %s", buttons OK / Goto) (NIEPEWNE: the type→key
table was not decoded). **The player is not asked; the leader is always placed automatically.**

**Place** (`0x4d80a0`): `location` 0 → capital (country+0x1f7e). Otherwise the location, unless it
is land that the country does not control-with-a-city. Then, in order: an adjacent province the
country controls with a city; any such province in the same region (+0x10a list `0x66ae4c`); the
nearest province of the country with a city (straight distance); the capital.

**Unit** (`0x4d8240`, by category):

| category | result |
|---|---|
| 2 conquistador | **always a new army of 1000 men** (CREATE_ARMY 0x88, first troop field = infantry, NIEPEWNE) at the place |
| 3 explorer (and 5) | place = capital, or no port → another port province (`0x43c910`). If that is a port or a sea zone: **new fleet of 3 ships** (CREATE_FLEET 0x8c, first ship field, NIEPEWNE warships). Otherwise falls through to the general rule |
| 0 general, 1 admiral, 4 monarch | **joins an existing unit**: fleets for an admiral, armies for every other type; the one with the best score = strength / (leaders² + 1), i.e. the biggest unit with the fewest leaders |
| none found | admiral: port as above → new fleet of 1 ship (no port: no unit). Others: new army of 1000 at the place |

Note the fall-through: an explorer with no reachable port is put on an **army**.
Troop field order of 0x88 is inferred from the build-time helpers in `0x437d60` (`0x429800` 2
months, `0x429820` 3 months, `0x429840` third) = infantry, cavalry, artillery (NIEPEWNE).

### 2.3 Event commands (executor `0x594eb0`, cases 0x1e/0x1f)

| command | unit created at P | leader |
|---|---|---|
| `conquistador` | new army of **500**: in field 1 (infantry) when the country's capital is in America (continent 1), else in field 2 (cavalry) (`0x5960e6`) | random, category 2 (§1.5), attached with 0xb2 |
| `explorer` | new fleet of **1** ship (field 1) | random, category 3 |

`which = -1` is the usual data form (`DB/events/randomevents.txt`: Colonial Dynamism and the
domestic-policy triggered random events). HINT_8 ("go Naval for explorers, Narrow-Minded for
conquistadors") is implemented by these event triggers, not by exe code.

## 3. Terra incognita and movement

### 3.1 The rule (`0x48c640`, "may enter province")

For a province **not known** to the unit's country the move is refused unless one of these holds:

1. the unit belongs to the pirates (tag 0x151);
2. **land unit** (unit vtable +0x14) and (land tech ≥ **31** (`0x432d90`), or the `fantasy` flag,
   or unit+0x8c is 2 or 3, or the unit carries **any leader of type 2 or 3** (`0x48c230`));
3. **fleet** (vtable +0x18) and (naval tech ≥ **27** (`0x432e40`), or the `fantasy` flag, or
   unit+0x8c is 2 or 3, or any leader of type 2 or 3);
4. **the province has an owner** (`prov+0x3c4 ≠ 0`): anyone may enter an owned unknown province.

So in the code a conquistador also opens unknown sea for a fleet and an explorer opens unknown land
for an army. The refusal messages are stricter (they only test the "proper" type).

- `fantasy` = global byte `0x8d0d7a`, set by the settings keyword `fantasy` (`0x5b1930`); a
  debug/scenario switch that lifts all exploration limits; treat as 0.
- unit+0x8c values 2/3: NIEPEWNE (believed: the unit itself is flagged as conquistador/explorer
  unit); where it is set was not found.
- Known provinces then get the normal access/war checks (owner/controller relations, enemy
  fortress strength vs. unit size `0x47f3e0`·1000).

### 3.2 Order UI (`0x530f70`, after the path search failed)

| case | text key | text |
|---|---|---|
| target is province 0 | RIKTIGTERRA | "permanent Terra Incognita and cannot be accessed" |
| land unit, unknown land, no land tech 31, no conquistador (`0x48eda0`, type 2 only), unowned | LANDMORON | "...Send an conquistador here or wait until we reach land technology level 31." |
| fleet, unknown sea zone, no naval tech 27, no explorer (`0x48edd0`, type 3 only) | NAVALMORON | "...Send an explorer here or wait until we reach naval technology level 27." |

Then `gfx\interface\illegalmoveorder.bmp`. Cursors `cursor_explore_army/navy` (SPEC_UI) use
`0x48c0e0` = "this unit can explore at all" (rules 1-3 without the province).

### 3.3 Path search

- Edge filters `0x41dbf0` and `0x41e240` call `0x48c640` for every step. `0x41e240` also never
  routes through an unknown sea zone, refuses Lake (region 0x95), and refuses unknown unowned land
  unless `0x48c0e0` is true.
- Cost: unknown destination × U (`0x48dfa0`), U = 1.5 − 0.15 × maneuver + K, K = **5 general,
  4 admiral, 2.2 any other type** (conquistador, explorer, monarch). The commander is
  `0x48c040` (lowest rank value); a unit with no leader uses its built-in empty record (unit+0x5e,
  ctor defaults: type 0, maneuver 0) → U = 6.5.
- **Correction to SPEC_WAR_DIPLOMACY §11.1**: there "4 for a conquistador (type 1)" is wrong;
  type 1 is the admiral, conquistador/explorer get 2.2.

## 4. Discovery

### 4.1 Core: discover(P, tag) `0x481660` (wrappers `0x476070`, `0x4760e0` redraw for the player)

- First discovery by anyone: `prov+0x254` = 1, `prov+0x250` = date.
- `prov[0x256+tag]` = 1; the AI is notified.
- Up to 3 linked provinces at `prov+0xfa..+0x102` that are **rivers (terrain 6)** and not yet known
  are discovered with it (recursive).

### 4.2 Units (arrival handler `0x48cf10`)

On arrival in P (both armies and fleets): if P is unknown, `0x48cdd0` queues
**`DISCOVERPROVINCE` (0x93)**. Nothing else for armies: **a land unit reveals only the province it
enters** (conquistador or not).

A **fleet in a sea zone** (region 0x94) also checks every adjacent **land** province N that is
unknown:
- naval tech ≥ **18** (`0x432e30`): N is discovered;
- else, if the fleet has unit+0x8c 2/3 or a type 2/3 leader: discovered when
  (low byte of the current date + unit id + P + N) mod 100 < that leader's maneuver, **or** a
  global miss counter `0x80b4fc` exceeds 3 (AI country) / 8 (human); hit → counter = 0, miss →
  counter + 1. The counter is one global for all fleets of all countries;
- else: nothing.
Fleets never reveal adjacent sea zones; a sea zone is known only by entering it.

### 4.3 DISCOVERPROVINCE handler `0x44f470` (country slot 0x228)

- Log message 0xad, discover(P). Discovery victory points (`0x55f030`, category 5 BVPDISC, only in
  VP games): queued by `0x48cdd0` with 2.0 for land nobody knew before, 0.3 for land known to
  others or for a sea zone nobody knew, 0 for a sea zone known before.
- First-discovery specials `0x418420` (cases 1..0x11 = SPEC_AMERICA 50, SPEC_ASIA 25,
  SPEC_QUEBEC 5, SPEC_GREATLAKE 10, SPEC_CAPEHORN 10, SPEC_GOODHOPE 10, SPEC_HUDSONBAY 10,
  SPEC_ALASKA 5, SPEC_PANAMA 5, SPEC_INLANDCHINA 20, SPEC_AMAZONEZ 10, SPEC_NORTHAM 20,
  SPEC_INDIA 25, SPEC_KAMTCHATSKA 10, SPEC_CALIFORNIA 10, SPEC_PACIFIC 10, SPEC_AUSTRALIA 5):
  points + message 0xb0. Which provinces trigger each case is in `0x418420` (region/area/province
  id tests), not decoded here.
- If P has an owner whose capital is still unknown to us: **`DISCOVERCOUNTRY` (0x124)** is queued
  (`0x453e20`: learn that country's capital).

### 4.4 Other sources of knowledge

| source | rule | evidence |
|---|---|---|
| Scenario | `knownprovinces`, owned and controlled lists are known at load | country loader `0x4aee70`, keyword cases 0x54/0x55/0x56 |
| Diplomacy "exchange discoveries" (`AWEXCHANGE`) | `ACCEPT_SHARE_EXPLORATION` 0x121 → `0x444440`: **both** countries learn each other's known **sea zones and owned provinces** only (unowned land is not shared, although the offer text counts all) | `0x453b40`, `0x444440`, offer text `0x50b880` case 0xd |
| `STEAL_EXPLORATION` 0x123 | from the target's known list, at most **monarch skill (cap 9, 3 without monarch) + 5** provinces that are coastal (`prov+0x487`) or sea and touch a province we know; message 0xb1 if any gained | `0x453be0`, `0x43bf10`, `0x52f9f0`; who sends it: NIEZNANE |
| Taking control of a province whose city has flag +0x2c (NIEPEWNE: capital) | the new controller learns **all provinces the old owner knew** | `0x44cbb0` (PROVCHANGECONTROL 0x8d) |
| Annexation/inheritance | the annexer learns everything the annexed country knew | `0x456ab0` |
| New country (`CREATE_COUNTRY` 0xcd) | knows its own provinces | `0x452de0` |
| Colony / trading post founded | its coastal sea zone (`prov+0x3d6`) becomes known | `0x437be0`, `0x4381d0` |
| Event `discovered` | trigger only (SPEC_EVENTS) | – |

No sharing through alliance, vassals or trade was found. Allies' provinces are **not** known
automatically. (Possible but not seen: an event command that discovers.)

## 5. Other special rules

| rule | evidence |
|---|---|
| Conquistador fire and shock count 0 in Europe (continent 2) | `0x488160`, `0x4881b0` |
| Attrition: ×4 maneuver and 0 % minimum for conquistadors (known) | SPEC_WAR §10.3 |
| AI never merges a unit that has a conquistador/explorer (or unit+0x8c 2/3) into another | `0x558e90` via `0x48c1f0` |
| When an AI agent with an explore plan (agent type 2/3) splits a unit, the type 2/3 leader moves to the split-off part (command 0x54) | `0x54b560` (army), `0x54b8f0` (fleet) |
| After arrival, other own units in the province without a conquistador/explorer are counted and handed to vtable slot 0xb0 (NIEPEWNE: auto-merge/stack bookkeeping) | `0x48c260` |
| Units led by a conquistador/explorer do not get a generic army/fleet name (NIEPEWNE: named after the leader) | `0x4deb70` |
| Per-country counters: conquistadors +0xfe4, explorers +0xfe8 | `0x43b0d0` |
| Colonist success: + best conquistador/explorer skill present (known) | SPEC_ENVOYS, `0x436670` |
| `REQUEST_CONQUISTADOR_TEST` 0xcf: once per province (`prov+0x88`), the best-ranked conquistador there is tested with its fire/shock; outcome NIEZNANE (sits next to `REQUEST_NATIVE_ATTACK` 0xd0) | `0x4d8de0` |
| Explorers and privateers have no special naval-attrition code found (naval attrition only has a generic leader term) | NIEZNANE |
| Leader lifetime: active between start and death date and not dormant (`0x488310`); random leaders live 15 years; combat death 1 % per day change (known, `0x46d2c0`) | `0x488310`, `0x488230` |
| Leader when its unit is destroyed: not traced (merging moves leaders, `0x48dd60`) | NIEZNANE |

## 6. AI

The AI does use them (`0x5533a0`..`0x553ce0`, log strings "sends an explorer towards", "sends an
conquistador [and we do it with TRANSPORTS] towards", "has now sea/land explored").
- Plan value per candidate province (`0x553570`): +1 per unknown neighbour; if any: +20 when the
  province itself is unknown, + a value per unknown neighbour (province +0xb6, doubled when owned by
  another country, − prov+0x124/5, minimum 1); divided by the distance from the capital; extra
  weight for some areas when the capital is in Europe (and the `fantasy` flag is off).
- An explore plan lasts 30-59 days (`rand()%30 + 30`, `0x553830`, `0x553bc0`) and ends when the
  target becomes known or the unit stops moving ("isnt moving, we cancel").
- How the AI picks the leader/unit and lands conquistadors by transport: NIEZNANE (high level only).
