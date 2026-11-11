# SPEC_EVENTS — the EU2 v1.09 event system

Clean-room specification for AmiEU2. It describes behaviour in our own words. No decompiled code is
reproduced here. Every claim cites the `eu2.exe` function address (Ghidra names `FUN_xxxxxxxx` in
`C:\temp\amiga_eu2\ghidra\eu2_all.c`) so it can be re-checked. Some functions are missing from the
Ghidra export (mostly the small trigger evaluators). For those, the address is the vtable slot,
checked by disassembly. Counts come from scripts over the shipped data (see §11).

Markers: **NIEPEWNE** = inferred, not proven; **NIEZNANE** = unknown.

## Contents

1. [Sources and terms](#1-sources-and-terms)
2. [Files and loading](#2-files-and-loading)
3. [Event block keys](#3-event-block-keys)
4. [Scheduling and firing](#4-scheduling-and-firing)
5. [Random events](#5-random-events)
6. [Triggers](#6-triggers)
7. [Command parameters: which / value](#7-command-parameters-which--value)
8. [Command validity (dropping commands)](#8-command-validity-dropping-commands)
9. [Command types](#9-command-types)
10. [Choosing an action: AI and human](#10-choosing-an-action-ai-and-human)
11. [Data statistics](#11-data-statistics)
12. [Implementation notes for AmiEU2](#12-implementation-notes-for-amieu2)
13. [Open questions](#13-open-questions)

---

## 1. Sources and terms

- `DB\event_manual.txt` (old Paradox notes), `eu 2 event scripting.doc` (Paradox, 2001-11-06,
  H. Fåhraeus). Both are partly wrong about v1.09. Where they disagree with the exe, the exe wins
  and the difference is flagged.
- **Keyword ids.** The tokenizer turns every known word into a numeric token id. The table is built
  in `0x0049b18a`: 558 keywords, each a (string, id) pair. All parsers compare ids, not strings.
  Keywords are matched case-insensitively. The generic compare `0x005bc060` is `stricmp`, and the
  data mixes `INF`/`inf` and `europe`/`Europe`.
- **Country index** is a slot in the 340-entry country table. A null slot means the tag does not
  exist now. **Province id** is 0..2019 (the exe checks `< 0x7e4`).
- **Date** = day byte, month byte (0 = January), year int16. Comparisons: `0x00417220` (A > B) and
  `0x00417270` (A ≥ B). `0x00417130` adds N days. **NIEPEWNE**: "days" is inferred from the
  magnitudes involved and the scripting doc's "offset in days".
- **Message queue.** Everything time-related goes through the game's dated message queue
  (`0x004b86d0` = post at a date, `0x004b8770`/`0x004b8850` = post now). Events are therefore
  processed when the daily clock reaches the queued date. Triggers are *not* polled every day (§4).

## 2. Files and loading

- A scenario `.eug` does `include = "db\events.txt"`. `events.txt` lists `event = "db\events\X.txt"`
  lines. The scenario-level parser passes each listed file to the event-file loader `0x0058f8a0`.
- Inside an event file the loader accepts two top-level keys:
  - `event = { … }` creates an event object (constructor `0x00592e60`) and parses it (vtable slot 2,
    `0x00594320`). For non-random events it then applies the offset (§4.1) and registers the event
    (`0x00590530`).
  - `include = "file"` recurses into that file.
  - Any other key logs `unknown data defined in event database` and stops that file.
- Parse errors go through `0x004a12d0`, which logs `ERROR : (%s) '%s %s %s' Line = %d file = %s` and
  carries on. Nothing is fatal.
- Shipped data: 125 files are listed. Of the 128 files present, `extragc.txt`, `special_por.txt` and
  `special_pru.txt` are **not** listed, so they are dead content. `major_col.txt` is commented out;
  its events also appear in `major_kol.txt`. `eventlist.txt` is an id-range note, not an event file.

## 3. Event block keys

Parser `0x00594320`. Field offsets are in the event object (base pointer).

| Key | Stored as | Meaning / notes | Events using it |
|---|---|---|---|
| `id` | int | Unique id. Together with the class tag 7 it forms the lookup key used everywhere (`0x00413c00`). Ids 1–15000 are reserved by Paradox. Shipped range is 100..99137. | 1528 |
| `trigger = {…}` | AND-node at +0x9c | Root of the condition tree (§6). **A second `trigger` block appends to the same root, so it is ANDed.** | 883 (16 events have two blocks) |
| `random` | bool +0x32 | True only for the token `yes`; anything else, or a missing key, means false. | 1523 (481 `yes`) |
| `country` | country index +0x40 | Tag → index. Missing → 0 (= "no country"). | 1056 |
| `province` | int +0x26 | Default −1. Target = the province's owner at fire time. **Unused in the data.** | 0 |
| `date = {day month year}` | date +0x34 | Missing fields stay 0 (8 events give only `year`, so day 0 / January). Unknown month word → error. | 827 |
| `offset` | int days +0x3c | Random spread, see §4.1. | 773 |
| `deathdate = {…}` | date +0x38 | Last date for retries (§4.3). | 585 |
| `name`, `desc` | string | Looked up in `Config\text.csv`. If the lookup fails, the literal text is kept. If empty, defaults to `EVENTNAME<id>` / `EVENTDESC<id>` (`0x005940f0`). All 1528 names and 1527 descs resolve. | 1528 |
| `style` | int +0x2c | Dialog picture `gfx\interface\infopics\infopic<style>.bmp` (`0x00590c50`). Values 0–5. **NIEPEWNE**: no `infopic*.bmp` ships in GFX. | 1528 |
| `action_a`…`action_d` | command lists +0x44/+0x5a/+0x70/+0x86 | Parsed by `0x00594640`: `name = "KEY"` plus any number of `command = {…}`. Action name defaults to `EVENTACT<id>A..D`. | a 1528, b 826, c 160, d **0** |
| anything else | — | Logs `unknown lhs in game-event`. | — |

There is **no** AI-chance key, no `when` command parameter, and no per-event repeat flag. The command
parser `0x00592da0` accepts only `type`, `which` and `value`; any other key logs
`unknown lhs in command`.

## 4. Scheduling and firing

### 4.1 Registration (load time)

Done once per non-random event, at load (`0x0058f8a0` → `0x005933b0` → `0x00590530`):

1. **Offset**: if `offset` > 0, then `date += rand() % offset` days (`0x005933b0`). The fire day is
   fixed here, once.
2. **Expiry** (`0x00598060`): if `deathdate` > `date`, expiry = `deathdate`. Otherwise expiry =
   `date + offset`. Note that the offset is counted again from the already-shifted date.
3. If today ≥ expiry, or the event is already in the *happened* set, it is not scheduled. When the
   date has passed but the event never happened, the exe only writes
   `Event %d should have happened.` to its debug log. **Consequence: an event with no `date` (year 0)
   never schedules itself.** It can only fire through `type = trigger` (§9). In the data that is
   220 events; 211 of them are trigger targets, and 9 are unreachable (3364, 3365, 3388–3390, 3749,
   3825, 3847, 3891).
4. Otherwise a *fire* message (type 0xc5) is posted for the shifted `date`:
   - `country` set → one message for that country;
   - `province` set → one message with no country (resolved to the owner when it fires);
   - neither set → **one message for every country that exists at load time**.

A `date` that is already in the past but not yet expired is still queued. **NIEPEWNE**: the queue
probably delivers it on the first processed day.

### 4.2 Fire handler (manager side, `0x0058fe50` → `0x0058fea0`)

The handler runs only on the host (`0x004196e0`, always true in single player). When the 0xc5 message
arrives:

1. Look up the event. Stop if it is missing or carries the per-event flag at +0x31 (**NIEPEWNE**
   "dormant"; nothing in the data sets it).
2. Target = the message's country. If that slot is null, use the owner of `province`.
3. **No target** (the country does not exist, or the province is unowned): a random event is dropped.
   A dated event is retried (§4.3).
4. **Non-random event already in the happened set** → stop. This makes dated events **one-shot
   globally**. For a no-country event queued for every country, only the *first* country whose
   trigger passes gets it.
5. Evaluate the trigger with the target country.
   - **True**: post "event happens" (0xc5) to the target immediately (§10). Then:
     - a random event schedules the country's next random event (§5); it is **not** marked happened;
     - a non-random event is added to the happened set (and to the second "slept" set if +0x31).
   - **False**: a random event schedules the next random event. A non-random event is retried
     (§4.3).

### 4.3 Retry rule (`0x00598040`)

A dated event whose trigger fails, or whose target is missing, is re-queued only if today <
`deathdate`. The delay is **16 + (rand() % 44) + (rand() % 16) days**, i.e. 16–74 days, mean about
44.5. With no `deathdate`, the event gets exactly **one** attempt on its shifted date. This matches
the scripting doc's remark that deathdate "is only useful if the event has a trigger". The retry
ignores the `date + offset` expiry and uses `deathdate` alone.

### 4.4 Happened / slept sets

- The manager keeps two id sets, at manager +0xea86 and +0xea98. `0x005904c0` (used by the
  `event = N` trigger) answers *true* only if the id is in +0xea86 and not in +0xea98.
- Firing a non-random event adds it to +0xea86. `sleepevent` (`0x00590450`) does the same.
  **Consequence (NIEPEWNE but direct from the code): a slept event also satisfies `event = N`.**
  Both functions add to +0xea98 only when flag +0x31 is set.
- Random events are never added, so `event = <random id>` is never true.
- These sets are saved (`archivedgameevents` keyword, **NIEPEWNE**: not traced).

## 5. Random events

- Load: random events are not scheduled. Their ids go into a pool (`0x00590530`, manager +0xea70
  count, 6-byte entries).
- Each country runs **one random-event chain**. It is started by the country initialisation
  `0x004505d0` (host only) and continued by every firing or failure (§4.2). All of it is in
  `0x0058fc00`:
  1. **Pick**: repeatedly draw a uniform random pool entry until one passes all of these:
     - the event exists and is not currently pending (flag +0x30, set while a dialog or decision is
       open, cleared on execution);
     - its `country` is 0 or this country;
     - its `province`, if set, is owned by this country;
     - its **trigger is true for this country now**.

     There is **no attempt limit**. The exe can spin forever if nothing qualifies (§12).
  2. **Delay**: N = number of owned provinces (country +0x131a list count).
     `base = max(100, (25 − N) × 20)`.
     `delay = rand()%100 + 100 + rand()%100 + base + rand()%base` days.
     The mean is about 199 + 1.5·base: about 919 days for a 1-province minor, about 349 days for
     ≥ 20 provinces.
  3. Post the fire message for (event, country) at today + delay. At fire time the trigger is
     checked **again** (§4.2). If it now fails, the event is skipped and a new pick is scheduled.
- Random events are repeatable without limit; the same event can come up again.
- Random events are stricter about invalid commands (§8): one invalid command cancels the whole
  event.
- `set_rand_events` / `set_hist_events` exist as scenario/option keywords. **NIEZNANE**: how they
  gate the pool and the dated events (not traced).

## 6. Triggers

Parser `0x00598bd0`. Each keyword creates a node. Evaluation is vtable slot 12 (+0x30) and takes the
**event's target country** as its argument. The root is an implicit AND. Unknown keywords log
`Unknown Trigger Type` and are skipped (they behave as if absent). Every failing leaf writes a reason
to the debug log (e.g. `Province %s is not owned by %s`).

**Booleans** (disassembly at `0x0059b240`, `0x0059b270`, `0x0059b2a0`):
- `AND = {…}`: true if every child is true; empty → true; stops at the first false child.
- `OR = {…}`: true if any child is true; empty → false.
- `NOT = {…}`: true if **no** child is true (NOR). `NOT = { a b }` means "neither a nor b". It does
  **not** mean "not (a and b)".
- Nesting is unlimited. The deepest in the data is 3.

"≥" means "at least". "Country" is the event's target country unless the trigger names another.

| Trigger | Syntax | True when | Eval addr | Uses |
|---|---|---|---|---|
| `owned` | `{ province = P data = T }` | P's owner is country T. `data` ≤ 0 or missing means the target country. False if P is unowned. | 0x0059b700 | 531 |
| `control` | `{ province = P data = T }` | P's controller is T (same `data` rule). | 0x0059b780 | 11 |
| `core` | `{ province = P data = T }` | P is a core of T. False if P is unowned. | 0x0059be40 | 0 |
| `exists` | `= TAG` | TAG's country slot is non-null. **The doc's `exist` is not a keyword.** | 0x0059b870 | 175 |
| `religion` | `= name` | Country's state religion equals it (ids in §7.3). | 0x0059b3f0 | 180 |
| `continent` | `= name` | Continent of the country's capital equals it. Ids: 1 America, 2 Europe, 3 Asia, 4 Oceania, 5 Africa; an unknown name gives 0 (`0x00471bb3`). | 0x0059b800 | 94 |
| `event` | `= id` | Event id is in the happened set and not in the slept set (§4.4). | 0x0059b610 | 189 |
| `year` | `= Y` | **Current year ≥ Y.** The doc says "equals"; it is wrong. | 0x0059b650 | 183 |
| `stability` | `= S` | Stability ≥ S. | 0x0059bc20 | 20 |
| `land` / `naval` / `infra` / `trade` | `= L` | Tech level ≥ L. | 0x0059bc70 / bcb0 / bcf0 / bd30 | 8 / 42 / 14 / 40 |
| `domestic` | `{ type = slider value = V }` | Slider ≥ V (slider ids in §7.3). Use `NOT` for "below". | 0x0059bed0 | 307 |
| `countrysize` | `= N` | Country counter +0xe5c ≥ N. That counter is the number of owned provinces that have a city which is **not a colony** (the country statistics pass `0x0043aa30` builds it). Undocumented. | slot of 0x005f7a10 → 0x0059c040 | 683 |
| `atwar` | `= yes/no` | (Country is at war) equals the flag. | 0x0059ba20 | 20 |
| `war` | `{ country = A country = B }` | A and B are at war. False if either is missing. | 0x0059b970 | 23 |
| `alliance` | `{ country = A country = B }` | A and B are in a military alliance. | 0x0059b8b0 | 0 |
| `dynastic` | `{ country = A country = B }` | A and B have a royal marriage. | 0x0059bab0 | 0 |
| `vassal` | `{ country = A country = B }` | B is a vassal of A. | 0x0059bb70 | 40 |
| `relation` | `{ country = T data = V }` | Relation with T ≥ V. **Note `data`, not `value`.** It is only evaluated if T's capital is known to the target country; otherwise false. | 0x0059c2d0 | 0 |
| `neighbour` | `= TAG` | TAG is flagged as a neighbour (country +0x138a byte array). | 0x0059bff0 | 0 |
| `leader` | `= id` | Leader id exists and is alive. | 0x0059b5a0 | 2 |
| `monarch` | `= id` | **Monarch id is the target country's current ruler.** The doc says "alive"; it is narrower. | 0x0059b670 | 14 |
| `discovered` | `= P` | **Province** P is known to the target country. The doc says "region". | 0x0059bd70 | 0 |
| `cot` | `= P` | P is the centre of trade of its trade region. | 0x0059bdc0 | 0 |
| `provincereligion` | `{ province = P data = R }` | P's religion is R. | 0x0059c080 | 0 |
| `provinceculture` | `{ province = P data = C }` | P's culture is C. | 0x0059c210 | 0 |
| `badboy` | `= X` | Badboy (country +0xe42) ≥ X. | 0x0059bf90 | 2 |
| `ai` | `= yes/no` | (Country has no human player) equals the flag. Slot table at 0x91a738. | 0x0059b500 | 1 |
| `flag` | `= name` | A named game flag is set (string flags, see `setflag`). | 0x0059b550 | 0 |
| `emperor` | `= yes/no` | (Country is the HRE emperor) equals the flag. | 0x0059ba40 | 0 |
| `elector` | `= yes/no` | (Country is an elector) equals the flag. | 0x0059ba70 | 0 |

`owned`, `control`, `core`, `provincereligion` and `provinceculture` share one sub-parser
(`0x0059b350`). Its `data` accepts a country tag, a culture/religion name, or a number.

## 7. Command parameters: which / value

### 7.1 Storage and symbols

- Each command has `type` (an internal code, §9), `which` and `value`. The original `which`/`value`
  are kept and copied into working fields on every firing, so random choices are made fresh each
  time (`0x005933d0`).
- The symbol parser `0x00592b00` handles `which` and `value`. It maps the symbolic names (§7.3) to
  small integers, then tries a country tag, then a culture name, and otherwise reads a number with
  `atoi`. **Decimals are truncated.** Unrecognised text logs `Unrecognisable which or value` and
  gives 0.
- Exception: for `flagname`, `ai`, `setflag` and `clrflag` the text is stored as a string.

### 7.2 Special negative `which` (resolved per firing, `0x005933d0`)

Before any resolution, `srand(seed)` is called with the seed carried by the message, so the resolution
is reproducible in multiplayer. Codes 6/7/8 (ADM/DIP/MIL) are skipped, because their `which` is an
amount.

| Code | Meaning |
|---|---|
| −1 | A **random eligible** province of the event country, drawn uniformly from its owned-province list. Eligibility depends on the command (§8 table, "eligible for −1"). If no province qualifies and the command takes a country, a **random eligible country** is drawn instead (below). |
| −2 | The country's **capital** province. |
| −3 | **The previous random pick**, held in one global variable shared by all events and by country picks. In practice −3 means "the same province (or country) as the last −1 in this event". It can be stale if no −1 came before it in this event. |
| −4 | Like −1, but a province also qualifies if it has no city or its city is a colony, or if it lies in America (continent 1). For `fortress` it also skips the "not a colony" test. Used 10× with `fortress` and once with DIP (where it is just −4 points). |
| −6 | The current HRE emperor (a country). Unused. |

**Random country** (`which = -1` on country commands): the candidates are every existing country
other than self, known to the event country, and passing a "living" check (`0x0045bce0`). The extra
rules per type are:

| Command | Extra candidate rule |
|---|---|
| `dynastic`, `vassal`, `relation`, `casusbelli` | Neighbour flag set, **or** the float at country +0x1a2e[other] < 6.0. **NIEPEWNE**: that float is probably a distance measure. |
| `breakdynastic` | Has a royal marriage with self. |
| `breakvassal` | `0x004cbe90(other)` is true. **NIEPEWNE**: "other is our vassal". |
| `alliance` | Not already allied. |
| `inherit` | Owns fewer provinces than self **and** has a royal marriage with self. |
| all others | No candidates, so the command is dropped. |

`gainmanufactory` stores `value` unchanged: case 0x24 posts message 0x15d CREATE_PRODUCTION_FACILITY
(`0x59664b`), the province receiver (jump table `0x480dec`, entry 10 → `0x480c94`) calls `0x4962f2`
(city+0x20 = value) and `0x496308` (city+0x1c = city+0x20, icon by `0x496248`, which knows only 1..5).
There is no goods mapping. `value = -1` (3 events in the data) therefore leaves a manufactory of type
−1: non-zero (it counts as a manufactory and blocks building one), no icon, no type effect.

### 7.3 Symbolic values (`0x00592b00`)

| Group | Names → number |
|---|---|
| Religion | protestant 0, reformed 1, counterreform 2, catholic 3, orthodox 4, sunni 5, shiite 6, pagan 7, confucianism 8, buddhism 9, hinduism 10 |
| Tech group (`technology`) | latin 0, torthodox 1, muslim 2, china 3, exotic 4 |
| Domestic slider | aristocracy 0, centralization 1, innovative 2, mercantilism 3, offensive 4, land 5, quality 6, serfdom 7 |
| Building | shipyard 0, barrack 1, bailiff 2, cityrights 3, courthouse 4 |
| Manufactory | luxury 1, weapons 2, navalequipment 3, refinery 4, goods 5 |

The numbers overlap between groups (`china` = `catholic` = `cityrights` = 3, `land` = `sunni` = 5).
That is harmless, because each command reads its value in its own sense.

## 8. Command validity (dropping commands)

`0x005947a0` decides whether a resolved command can apply. `0x00594d30` walks a list:

- **Non-random event**: invalid commands are **removed silently**, and the rest of the action still
  runs.
- **Random event**: the first invalid command makes the list fail. The event is then **cancelled**
  with `CommandList X is currently Invalid` (`0x005980b0`, before the decision is made). Its random
  chain has already been re-armed (§4.2).
- Validation runs on arrival (before the AI/human decision) and **again at execution**
  (`0x00594eb0`), because the state may change while a human dialog is open.
- A command whose `which` is still negative after resolution is invalid, except ADM/DIP/MIL.
- `command = { }` (type −1) counts as valid and does nothing. That is why Paradox says "even an action
  that does nothing must contain a command".

Per-type rules (P = `which` as a province; C = `which` as a country; "owned" = owned by the event
country; "city" = the province has a settlement object, +0x408):

| Commands | Valid if | Eligible for −1 |
|---|---|---|
| revolt, religiousrevolt, colonialrevolt, heretic, conversion, provincereligion, gainmanufactory, losemanufactory, population, provincetax, provincemanpower, mine, provinceculture | P owned and has a city | revolt: owned. religiousrevolt / conversion: religion ≠ state religion. colonialrevolt: colony or no city. heretic: the "heretic" religion of the province (map `0x0042f900`) is available (`0x004310f0`) and differs from the current one. provincereligion: ≠ value. gainmanufactory: city, no manufactory, not a colony. losemanufactory: has a manufactory. population: pop + value > 1000. provincetax: tax + value > 1. provincemanpower: mp + value > 1. mine: mine value ≠ 0 and stays > 1. provinceculture: culture ≠ value (value −1 = state culture). |
| ADM / DIP / MIL | ruler stat + `which` ≥ 0 | — |
| dynastic | C exists and has no royal marriage with us (**NIEPEWNE**: a date check) | see §7.2 |
| vassal | C exists, not already our vassal | §7.2 |
| alliance | C exists, not already allied | §7.2 |
| inherit | C exists, is alive, host | §7.2 |
| breakdynastic, breakvassal, relation, casusbelli | C exists | §7.2 |
| conquistador | P has some owner | any province in our list |
| explorer, warships, galleys, transports | P owned and has a city; the ship/unit types need `value` ≥ 1 | owned with a port |
| INF / CAV / ART (`infantry`/`cavalry`/`artillery`) | `value` ≥ 1, P owned and has a city | owned |
| fortress, province_revoltrisk, capital | P owned and has a city (capital also needs a city) | fortress: below the date-dependent maximum level (1 + count of the land-tech thresholds 1, 10, 20, 25, 40 passed, from `0x00432db0…df0`; **NIEPEWNE**: that it is land tech) and not a colony (except −4) |
| leader / wakeleader / sleepleader | Leader id is in the leader table and its state ≠ 0x0c | — |
| monarch / wakemonarch / sleepmonarch | Monarch id is in the table and its state ≠ 0x30 | — |
| country (tag change), independence | Target tag does **not** currently exist | — |
| gainbuilding | Building can be built there (`0x00460210`: owned, city; shipyard needs a port) | buildable |
| losebuilding | P has that building | has it |
| desertion | One of our armies is in P | has one of our armies |
| secedeprovince | Province (`value`) is owned by us and target C exists | — |
| cot | P is in a trade region whose CoT is elsewhere, has a city, and is owned | — |
| removecot | P is its region's CoT, has a city, and is owned | is CoT |
| war | C exists and the country's own can-declare-war check passes | — |
| mission | **always invalid** (dropped) | — |
| natives | — | natives ≠ 0 and natives + value > 1 |
| everything else | always valid | — |

The doc's rule "province commands are ignored if the province belongs to another country" is exactly
the "P owned" column above.

## 9. Command types

The keyword → internal code map is `0x00592480`. An unknown keyword logs `Illegal Command Type` and
becomes **treasury** (code 0x13), which is harmless with value 0. The executor is `0x00594eb0`, a
switch on the code. Most effects are posted as game messages and applied by handlers outside the event
module; those effects are described from the docs (marked "doc").

Table columns: code (hex), keyword(s), uses in data, which/value, effect.

| Code | Keyword | Uses | which / value | Effect (exe detail where seen) |
|---|---|---|---|---|
| 00 | revolt | 729 | P / – | Rebels rise in P (−1 random, −2 capital, −3 previous). Fort may stay loyal (doc). |
| 01 | religiousrevolt | 148 | P / – | Religious rebels in P. |
| 02 | colonialrevolt | 55 | P / – | Colonial rebels (colonies only). |
| 03 | heretic | 4 | P / – | P's religion → its "heretic" counterpart (`0x0042f900`: protestant↔reformed, catholic/counterreform→protestant/reformed, orthodox→catholic, sunni↔shiite, confucian→buddhism→hinduism, pagan stays). |
| 04 | conversion | 55 | P / – | P → state religion. |
| 05 | provincereligion | 93 | P / religion | Set P's religion. |
| 06/07/08 | ADM / DIP / MIL | 67/77/58 | **which = change, value = months** | Temporary ruler stat change. Ignored if there is no ruler (+0x20bc). Reverted after `value` months (**NIEPEWNE**: the revert mechanism was not traced). |
| 09 | domestic | 1834 | slider / ±n | Move a domestic slider by n (§7.3 ids). |
| 0a | religion | 39 | religion / – | Change state religion only. |
| 0b | revoltrisk | 254 | months / % | Country-wide revolt-risk modifier for `which` months. 25 uses lack `which`, so 0 months. **NIEPEWNE**: what 0 months does. |
| 0c | inflation | 85 | – / ±% | Inflation += value × 0.01 (constant 0.01 at 0x005e5860). |
| 0d | dynastic | 72 | C / – | Royal marriage. |
| 0e | vassal | 38 | C / – | C becomes our vassal. |
| 0f | breakdynastic | 8 | C / – | End the royal marriage. Undocumented. |
| 10 | breakvassal | 21 | C / – | End the vassalage. Undocumented. |
| 11 | alliance | 47 | C / – | Add C to our alliance; creates one if none. |
| 12 | inherit | 45 | C / – | Annex C diplomatically (sent with a "forced" flag). |
| 13 | treasury, **cash** | 384 + 119 | – / ±ducats | Treasury += value. Booked under ledger category 7 (income) or 15 (expense). |
| 14 | manpower | 38 | – / ±n | Manpower pool. |
| 15/16/17/18 | land / naval / infra / trade | 115/55/135/167 | – / ±ducats | Investment into that research. |
| 19 | stability | 1393 | – / ±n | Applied as \|n\| single ±1 steps, so normal stability clamping applies per step. |
| 1a–1d | diplomats / colonists / merchants / missionaries | 18/42/54/23 | – / ±n | Add envoys. |
| 1e | conquistador | 16 | P / – | Spawn a conquistador. The unit size is 500 when the country's capital is in America (continent 1), otherwise 0. **NIEPEWNE**. |
| 1f | explorer | 22 | P / – | Spawn an explorer fleet. |
| 20 | leader, wakeleader | 46 + 0 | id / – | Activate the leader and mark it woken. |
| 21 | monarch, wakemonarch | 16 + 121 | id / – | That monarch becomes ruler. |
| 22 | country | 24 | TAG / – | Our tag changes to TAG (e.g. CAS → SPA). |
| 23 | relation | 3339 | C / ±n | Relation with C += n (sent as float, with a second parameter −10.0; **NIEZNANE** meaning). |
| 24 | gainmanufactory | 81 | P / type | Build a manufactory; the value is stored as is (−1 → a typeless manufactory, §7.2). |
| 25 | losemanufactory | 12 | P / – | Remove the manufactory. |
| 26 | gainbuilding | 24 | P / building | |
| 27 | losebuilding | 1 | P / building | |
| 28 | desertion | 97 | P / men | Remove up to `value` men from our armies in P, army by army. |
| 29 | fortress | 121 | P / ±n | > 0: raise the fort; < 0: lower the fort by \|n\|. |
| 2a | population | 285 | P / ±n | New pop = max(pop + n, min(pop, 1000)). It never drops below 1000 by event, unless already below. |
| 2b/2c/2d | INF, infantry / CAV, cavalry / ART, artillery | 68+40 / 67+14 / 32+14 | P / n | Raise land units in P. **NIEPEWNE**: CAV/ART pass a 0 where INF passes the amount; whether the amount is read elsewhere was not verified. |
| 2e/2f/30 | warships / galleys / transports | 34/10/12 | P / n | Ships in P's port (same NIEPEWNE as above for galleys/transports). |
| 31 | provincetax | 785 | P / ±n | Province tax. |
| 32 | provincemanpower | 127 | P / ±n | Province manpower. |
| 33 | mine | 1 | P / ±n | Mine value (gold/iron/copper only). |
| 34 | flag | 5 | n / – | Set numbered global flag n (0 Tordesillas, 1 Reformation, 2 Calvin, 3 Trent, 4 Edict of Tolerance, 5 French tricolour, 6 Union Jack). |
| 35 | trigger | 227 | event id / – | Fire event id **now** for: its province's owner if it has one, else its own `country`, else the current event's country. It goes through the full fire handler (§4.2), so the target's **trigger must pass** and a non-random target must not have happened yet. |
| 36 | vp | 109 | – / ±n | Victory points (category 6). |
| 37 | casusbelli | 251 | C / months | Casus belli against C for `value` months. |
| 38 | capital | 25 | P / – | Move the capital. |
| 39 | natives | 0 | P / ±n | Native count/aggression change. |
| 3a/3b | bank / stock | 0 | – | Unused; posts messages 0x166/0x167. **NIEZNANE**. |
| 3c | provinceculture, culture | 45 + 0 | P / culture (−1 = state) | |
| 3d/3e | addcore / removecore | 565 / 33 | P / – | |
| 3f | cot | 14 | P / – | Make P its region's centre of trade (**NIEPEWNE**: moves an existing CoT). |
| 40 | secedeprovince | 329 | **TAG / P** | Give province `value` to TAG. The doc's `secede` is not a keyword. |
| 41 | province_revoltrisk | 978 | P / ±n | Permanent province revolt-risk modifier; it ends when the owner changes (doc). |
| 42/43 | sleepmonarch / sleepleader | 99 / 37 | id / – | Make dormant (a living leader does not die). |
| 44/45 | add_ / remove_countryculture | 17 / 4 | culture / – | |
| 46 | loansize | 21 | **which** = n / – | All 21 uses put the size in `which`. **NIEPEWNE**: which field the exe reads. |
| 47 | sleepevent | 157 | event id / – | Mark the event happened (§4.4). |
| 48 | mission | 0 | – | Always dropped. |
| 49 | war | 6 | C / – | Declare war on C. The doc denies this exists; the exe has it. |
| 4a | technology | 9 | group / – | Change tech group (§7.3). The doc's `tech` is not a keyword. |
| 4b | independence | 30 | TAG / – | Release TAG as a new country (only if it does not exist). |
| 4c | flagname | 3 | string | Change the flag graphic (REV, UK). Undocumented. |
| 4d | badboy | 7 | – / ±n | Badboy change (message 0x148). Undocumented. |
| 4e | ai | 0 | string | Switch AI file/personality. **NIEPEWNE**. |
| 4f/50 | setflag / clrflag | 0 | name | Named global flags (read by the `flag` trigger). |
| 51 | removecot | 8 | P / – | Remove the CoT. Undocumented. |
| −1 | `command = { }` | 13 | – | No-op. |

Every command keyword in the data maps to a code. No data command hits `Illegal Command Type`.

## 10. Choosing an action: AI and human

On arrival at the target, the "event happens" message is handled by `0x005980b0` (event vtable
slot 1):

1. Set the target country. `srand(seed)`. Resolve parameters for A–D (§7.2) and validate them (§8).
   A random event with any invalid command stops here.
2. Mark the event pending (+0x30).
3. **Human dialog** if the target is the local player's country. The dialog is also used for an event
   with no `country` and no owned `province` (a world event such as the Reformation or Tordesillas);
   **NIEPEWNE**: this means the local player sees and decides world events even when the target
   country is someone else. In this path `0x005940f0` cleans the lists and fills the default texts,
   and the dialog window opens (`0x00590b80`, layout `0x00590c50`). The window sets a "dialog open"
   flag on the player country, which **NIEPEWNE** pauses the game. Buttons show the action names.
   When there is a single action with 1–4 commands, an effect summary is shown; otherwise a tooltip
   hint (`EVENTBUTTON_INFO`). The click posts "execute action k" (message 0x9a).
4. **AI** otherwise (host only, target alive). Let M = 85, raised to 90 if list B is non-empty, to 95
   if C is non-empty, and to 100 if D is non-empty (each check overrides the last). Draw
   r = rand() % M. Then: r < 85 → A, r < 90 → B, r < 95 → C, else D.
   - That gives A 85/90 ≈ 94.4 % and B 5.6 % with two actions; 85/5/5 % of 95 with three; 85/5/5/5 %
     with four.
   - Counts are taken **after** invalid commands have been removed. An action that lost all its
     commands is skipped as "empty" for the M calculation. If B is empty but C is not, B can still be
     chosen (and does nothing).
   - There are **no per-event AI weights** in v1.09.
5. "Execute action k" (`0x00594eb0`): re-validate, write the history/log line, then run the
   commands of list k in file order (host only).
   - Log line for the player's own events: `EVENT_SIMPLE` "%s : %s happened to us." with one action,
     or `EVENT_SELECTION` "%s : We went with %s in %s." with several.
   - Foreign events of known countries post a notice to the player (**NIEPEWNE**: wording and filter
     flag +0x2a).

## 11. Data statistics

Computed over the 125 listed files: 1528 events, all ids unique. Scripts are in
`C:\temp\amiga_eu2\agent_ev\stats*.py`, output in `stats.txt`.

| By file group | Events |
|---|---|
| major_* | 979 |
| randomevents | 473 |
| flavor_* | 55 |
| randomreligiousevents | 8 |
| special_* (listed) | 8 |
| ReligiousEvents | 4 |
| hugeevents | 1 |

Key combinations (C = country, D = date, O = offset, X = deathdate, T = non-empty trigger,
R = random):

| Combination | Events | Combination | Events |
|---|---|---|---|
| T R | 446 | C T R | 14 |
| C D O X T | 357 | C D T | 10 |
| C D O X | 227 | C T | 9 |
| C (trigger target) | 211 | D O (world) | 5 |
| C D O | 139 | C D X T | 1 |
| C D O T | 45 | | |
| C D | 43 | | |
| R | 21 | | |

- Actions per event: 1 → 702, 2 → 666, 3 → 160, 4 → 0. The most commands in one action is 61.
- Style values: 0 → 467, 1 → 227, 2 → 250, 3 → 270, 4 → 256, 5 → 58.
- Random events: 481 in total, 460 with a trigger, 14 with a country, none with a date.
- Triggers (all nesting levels): not 698 (613 at top level), countrysize 683, owned 531, domestic 307,
  event 189, year 183, religion 180, exists 175, or 165, and 99, continent 94, naval 42, vassal 40,
  trade 40, war 23, stability 20, atwar 20, monarch 14, infra 14, control 11, land 8, leader 2,
  badboy 2, ai 1.
- `which` specials in the data: −1 (many), −2, −3 (revolt 78, religiousrevolt 62, provincetax 37,
  CAV 22, provincemanpower 20, …), −4 (fortress 10).
- Text keys: all `name`, `desc` and action names resolve in `text.csv` except one `desc`.

## 12. Implementation notes for AmiEU2

- Do the scheduling with a sorted per-day queue: (date, event, country, seed). No daily trigger
  polling is needed. The work per day is proportional to the events due that day plus the
  random-event checks.
- Store the event's shifted `date`, expiry and `deathdate` as day numbers (int32). All arithmetic is
  integer. The only floats in the original are relation, inflation, badboy and population; these
  belong to SPEC_TIME_ECONOMY's fixed-point formats. Inflation: value is in percent, so store it as
  percent × fixed scale and do not multiply by 0.01 in floating point.
- `rand()`: the original uses MSVC `rand()` (15-bit, `0x005ba99c`) with `srand(seed)` per message.
  Bit-exactness with the PC is not a goal, but keep the **draw order** (offset at load, one draw per
  −1 resolution, AI choice last). Then a fixed seed gives reproducible runs.
- The random-event picker must get an **attempt cap**, e.g. pool size × 4, after which the chain is
  re-armed with the normal delay. The original loops unbounded (§5).
- Precompile triggers to a flat node array (op, args, child count) at load. Do the same for commands
  (code, which, value, string index). Then nothing is parsed at run time.
- Error tolerance: unknown keys log and continue, as the original does. Data authors relied on it:
  `exist`, `tech` and `secede` in the docs are silently ignored by the exe.

## 13. Open questions

1. What is the per-event flag +0x31? It blocks firing and routes the event to the "slept" set, but no
   file key sets it. Savegame? (NIEZNANE)
2. Does `sleepevent` really make `event = N` true? The code says yes unless +0x31 is set. Verify in
   game with a sleep + trigger pair. (NIEPEWNE)
3. Country +0x1a2e float (< 6.0) in random-country selection: distance? (NIEPEWNE)
4. Are dated events whose date is already past at scenario start (but before expiry) delivered on
   day 1? (NIEPEWNE)
5. Does the world-event dialog appear for the local player even when the target is another country,
   and whose state do the commands then change? (NIEPEWNE, §10)
6. Message handlers behind each command (stability clamp, desertion arithmetic, revolt unit size,
   ADM/DIP/MIL revert, relation's −10.0 parameter, CAV/ART amount) are outside the event module and
   were not traced. Several belong to the economy/diplomacy specs.
7. `loansize` and 25 `revoltrisk` commands put their number in an unexpected field. What does the exe
   really read? (NIEPEWNE)
8. (Resolved: `gainmanufactory` has no goods mapping, see §7.2.)
9. Exact gating by `set_rand_events` / `set_hist_events` options. (NIEZNANE)
10. `infopic<style>.bmp` is referenced but not shipped. Confirm what the PC game shows.
