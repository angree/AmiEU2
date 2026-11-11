# EU2 v1.09 - centre-of-trade lifecycle (specification)

Read out of `eu2.exe` (GOG v1.09) with objdump; no code was copied. Every rule names the address
it came from. Offsets are the original's and serve only as anchors for re-checking. Markers:
**NIEPEWNE** = believed, not proven; **NIEZNANE** = not found. Dumps: `C:\temp\amiga_eu2\sr\cot_*.txt`,
scripts `C:\temp\amiga_eu2\re\agent_cot\`. This extends SPEC_TIME_ECONOMY §9.2 and **corrects** it
in three places (marked "correction").

## 1. Objects

**Trade manager** `[0x647d40]` (0x5fd0 bytes, constructor `0x499af0`, created in world init
`0x4199e9`):

| offset | meaning |
|---|---|
| +0x000 | CoT pointers, **69 slots** (0x45; `0x499af0` clears 69, `0x49a7e7` refuses a 70th) |
| +0x114 | CoT count. *Correction*: SPEC_TIME_ECONOMY says "up to 0x114 CoTs"; 0x114 is the count's offset |
| +0x118 + 8·p | founding candidate p: (+0 province id, +4 site value float) (`0x49a399`) |
| +0x4038 | candidate province ids, sorted best first (2020 ints) |
| +0x5fc8 | candidate count; +0x5fcc = next-candidate cursor (`0x49a2e3`) |

**CoT** (0x578 bytes, vtable `0x5e8f50`, constructor `0x499300`; vfuncs: 0 `0x41e6b0` set home,
1 `0x4994b0` load, 2 `0x499780` save):

| offset | meaning |
|---|---|
| +0x04 | home province id |
| +0x08 | list object built by `0x4108d0` (NIEZNANE use) |
| +0x14 | **failure counter**, float 0..110 (save keyword `failure`) |
| +0x1c | position in the manager list (rewritten after each add or remove) |
| +0x20 | total merchants, int = Σ trunc(entry+4) (`0x499700`) |
| +0x24 | yearly trade value, float, rebuilt monthly (`0x49a0dd`) |
| +0x28 + 4·c | merchant entry of country index c, 340 slots, null = not present |

**Merchant entry** (0x26 bytes, vtable `0x5e7c14`, parse `0x498ce0`, save via sub-object +8,
vtable `0x5e7c0c`, `0x498f60`): +0x04 merchant count (float, keyword `level`), +0x0c country
index (`tag`), +0x10 income figure (`value`; set to 0 at the start of each month by `0x499a70`,
then increased by trade income, SPEC_TIME_ECONOMY §5.3 step 5), +0x14.. a list read from keyword
`extra` (0x185) (NIEZNANE; not written by the save).

**Province fields used:** +0x58 own id, +0xaa CoT pointer the province trades in (for a home province it
points to its own CoT; `0x49a231` "is home" = +0xaa ≠ 0 and CoT+4 = own id), +0xb2 **CoT
historical modifier** (province.csv column 22 "CoT Historical Modifier", read in the row parser
`0x4848ee` right after Income +0xb6 and MineValue +0xe2; values in v1.09 are −3..5), +0xca terrain
(≥ 5 = not land), +0x106 continent (2 = Europe), +0x256 + c "known to country c", +0x3c4 owner,
+0x408 city, +0x40c colony/trading post, +0x487 coastal, +0x54 goods, +0x84 pirates.

"Land and owned" = `0x49a290`: +0xca < 5 and +0x3c4 ≠ 0.

## 2. Site value S(p) - `0x47f400`

Used for a CoT home province (membership, §4) and for founding candidates (§5.3). Steps, in order:

| # | term | address |
|---|---|---|
| 0 | p is not land (+0xca ≥ 5), or has no city and no colony → **S = −1**, stop | `0x47f406`, `0x47f439` |
| 1 | base A: colony object present (+0x40c) → **1**; else city with population < 5000 (city vfunc +0xc false) → **25**; else **50 + 20 × city size** (city+0x6e, 0..6) | `0x47f444..0x47f476` |
| 2 | A += 1 if trunc(city fortress level, city+0x3a) ≥ 3 (0 without a city) | `0x47f48e..0x47f4ab` |
| 3 | A += 1 if p is coastal (+0x487) | `0x47f4b1` |
| 4 | S = A + H, H = **4 × historical modifier** (+0xb2) in Europe (+0x106 = 2), **1 ×** elsewhere | `0x47f4c1..0x47f4e3` |
| 5 | S += **30 × tradeEff(owner)** (`0x43b610`, SPEC_TIME_ECONOMY §5.3) | `0x47f4f1` |
| 6 | S += **owner's naval tech level / 2** (int division: `0x4333d0` = tech block +0x24 = naval track, vfunc +0x34 `0x52dad0` = level at track+8) | `0x47f50a..0x47f52b` |
| 7 | S += **0.2 × V**, V = max(0, F × goods price / 12 − 0.5 × pirates), F = city production multiplier (city vfunc +0x18), or colony size × 0.1 + 0.4 (colony+4); this is the province trade value of §5.3 | `0x47f533..0x47f59b` |
| 8 | S += 10 if the city has a manufactory (city+0x1c ≠ 0) | `0x47f5a5` |
| 9 | if p already trades in a CoT (+0xaa): S += **10 × distance(that CoT's home, p)** (`0x499760` → `0x43a820`; 0 for a home province) | `0x47f5b6..0x47f5d1` |
| 10 | owner's state religion (owner+0x254) 0..4 (Protestant, Reformed, Counter-Reform, Catholic, Orthodox): S += **50**; religion 7 (pagan) and fantasy byte `[0x8d0d7a]` = 0: **S = 0**; Muslim 5/6: nothing | `0x47f5df..0x47f615` |
| 11 | S += **100** if p is not in Europe (applied after step 10, so a pagan non-European site ends at 100) | `0x47f61b` |

Step 5 and 6 read the owner unconditionally; every caller passes an owned province (NIEPEWNE for a
CoT home that loses its owner - the original would crash or read garbage).

## 3. Monthly counter - `0x499e92` (trade manager event 0x16, `0x41a6b8`)

For every CoT: clear entry+0x10 (`0x499a70`), recount t = CoT+0x20 (`0x499700`), count k = number of
non-null merchant entries (any count). For every entry with count > 5 (a monopoly): VP **+0.16**
in category 0 Economy (`0x55f030(country+0x20e2, 0, 0.16)`, `0x499f64`). Then

| term of f | condition |
|---|---|
| +1 | some entry has count > 5 (`0x499730`) |
| −0.5 each | t > 19; t > 15; t > 12 (cumulative, max −1.5) |
| +1 | t < 8 |
| −0.5 / +2 / +1.15 | k > 5 / k < 2 / otherwise (2..5) |

counter += **f / 12**, then clamp to 0..110 (`0x499370`: > 110 → 110, < 0 → 0). So f is a yearly
rate: a lonely CoT (k < 2, t < 8) gains 3/year (4 with a monopoly) and reaches 100 after 25..34
years; a busy one (k > 5, t > 19) loses 2/year. Right after it, `0x49a0dd` sets CoT+0x24 = Σ over
all provinces with +0xaa = this CoT of 12 × V(p).

## 4. Membership - `0x49aa0f(p)`

Callers: world init for all 2020 provinces (`0x419d80`), yearly pass (§5, twice), event `removecot`
(`0x4547b4`, all provinces), and for one province after a settlement is created
(`0x437ba5` in `0x437aa0`, NIEPEWNE trading post; `0x437ceb` in `0x437be0`, colony with 100
population). **Not** on conquest, peace or owner change: a conquered province keeps its CoT until
the next yearly pass.

1. Leave p unchanged if it is not land-and-owned, or if it is a CoT home. (A check against province
   0x102 sets an unused local: debug leftover, `0x49aa1e`.)
2. best = −1 000 000, choice = none. For every CoT k (list order), C = its home, Oc = C's owner,
   Op = p's owner, R = Op's relation record for Oc (`Op+0x38e` + 0x36·Oc.index):

| term | value | address |
|---|---|---|
| start | 2 × S(C) | `0x49aaee` |
| Op = Oc and Op's capital in Europe (`0x436570` false = capital continent 2) | +25 if p in Europe, **+1000** if not | `0x49ab10..0x49ab64` |
| always | + (10 − Oc's domestic slider 3) × 0.05 (`0x42ba50` on Oc+0x37e, slider at +0x14) | `0x49ab88` |
| Op = Oc | + 3 × Op's slider 3 (`0x42bbf0(Op,3)`, mercantilism ↔ free trade) | `0x49abc4` |
| R+0x36 = 0 | +5 (NIEPEWNE: "not at war"; SPEC_WAR_DIPLOMACY calls this byte an unknown −0.1/month flag) | `0x49ac19` |
| R+0x3f = 0 and temporary-CB list R+0x10 empty (list vfunc +8) | +2 ("no casus belli", same test as SPEC_WAR_DIPLOMACY war declaration) | `0x49ac7b..0x49acbf` |
| Op is Oc's vassal / vassal tie (`0x4cbe90(Op.dip, Oc)`) | +3 | `0x49ad0b` |
| royal marriage R+0x3b | +2 | `0x49ad76` |
| same religion (`0x431270(Op.rel, Oc.rel)` false; Counter-Reform 2 = Catholic 3) | +2 | `0x49ade7` |
| both state religions Christian (0..4) | +25 | `0x49ae1f..0x49af3d` |
| p's culture (+0xd8) accepted by Oc (`0x429fb0` on Oc+0x382) | +25 | `0x49af83` |
| **Oc refuses trade with Op** (`0x4cbf60(Op.dip, Oc)` = Oc has global flag +0xfda and Oc ≠ Op, or Oc's record for Op byte +0x39) | −50 | `0x49afe9` |
| Op does not know C and Oc does not know p (+0x256 bytes) | −9999.9 | `0x49b02d..0x49b085` |
| failure | − 3 × CoT+0x14 | `0x49b0d3` |
| distance d = `0x43a820(C, p)` (map pixels / 100) | if d > 1: divide the whole sum by d | `0x49b0aa..0x49b0fe` |

   *Correction to the brief*: `0x4cbf60` tests the **CoT owner's** embargo of p's owner (it reads
   Oc's record, Op's index), and `0x431270` returns "religions differ", so +2 rewards the **same**
   religion. Op = Oc does not short-cut the relation terms (they read the self record).
3. Keep k if its score is **strictly** greater than best (ties keep the earlier CoT).
4. p+0xaa = choice. Because best starts at −1e6, p always gets some CoT while any exists, even an
   unknown one.

## 5. Yearly - `0x499ba2` (trade manager event 0x17, `0x41a69a`)

1. **Collect** the home ids of all CoTs with counter ≥ 100 (before any change).
2. For each: **dissolve** `0x49a71e(home)`: message 0x22 to the player's log if the player knows the
   province (`0x577ec0`); then `0x49a86e` removes the CoT from the list (shift down, count − 1,
   rewrite +0x1c) and **destroys it with all its merchant entries** (`0x4993b0`): merchants are lost,
   nobody is refunded or notified. Back in the yearly code: home +0xaa = 0 and home
   **+0xb2 −= 35** (permanent penalty on the historical modifier, `0x499d57`).
3. If anything was dissolved: membership for all provinces 0..2019.
4. **Candidates** `0x49a399`: every province with a city (+0x408; colonies and trading posts are not
   candidates), land-and-owned, not a home: value S(p) (§2); sort descending with `qsort`
   (`0x5ba72b`) and comparator `0x49a33c` = trunc(S(b) − S(a)) (differences below 1 count as equal;
   MSVC qsort is unstable, so their order is not reproducible).
5. **Limit** `0x49a1d7` = (number of land-and-owned provinces) / 29, integer division (colonies and
   trading posts count). If limit > count: **found exactly one** CoT (`0x49a7c2`) at the top
   candidate. So at most one new CoT per year; scenarios with fewer CoTs fill up one per year.
6. Membership for all provinces again.
7. Renumber: CoT+0x1c = list index.

**Founding** `0x49a4b2(p)`: new CoT (`0x49a98c`), home = p; `0x49a7e7` appends it if count < 69
(sets +0x1c, p+0xaa = it, recounts merchants; at 69 the object is silently dropped but the
rest still runs); message 0x21 if the player knows p; then if p is owned: `0x444870(owner, 1)` and a
command 0xa7 (owner index at +0x22, province at +0xc, +0x20 = +0x21 = 1) posted now via `0x4b8770`.
With SPEC_TUTORIAL (AddMerchants = `0x444870(n)` + command 0xa7) this reads as **the owner gets
one free merchant placed in the new CoT** (NIEPEWNE).

The new CoT starts with failure 0 and no merchants besides that one.

**Event effects** (SPEC_EVENTS): `cot = P` (`0x454720`) = found at P (`0x49a4b2`) and then run the
**whole yearly routine** (it can dissolve others and found one more). It does **not** move or remove
the region's old CoT (correction to SPEC_EVENTS "NIEPEWNE: moves"). `removecot = P` (`0x454780`) =
dissolve the CoT that P's +0xaa points to (with message, penalty not applied: the −35 is only in
the yearly code), clear P+0xaa, membership for all.

**Dangling pointers.** Dissolution clears +0xaa only on the home; members are fixed by the
membership pass, which skips unowned/sea provinces. A province that lost its owner after being
assigned keeps a pointer to the freed CoT, and the monthly value `0x49a0dd` would write through it
(NIEPEWNE whether this happens in practice). The port must clear every pointer to a dissolved CoT.

## 6. Save file and scenarios

Writer `0x4a2080`: for each CoT in list order, `cot = { ... }` (token 0xdb) from `0x499780`:

| keyword | token | content | loader `0x4994b0` |
|---|---|---|---|
| `location` | 0x26 | home province id | sets +4 and province +0xaa |
| `failure` | 0xaa | counter CoT+0x14 (float) | read as float |
| `merchant = { tag level value }` | 0xdd / 0x40, 0x22, 0x7f | per present country: tag, count (+4), value (+0x10) | entry needs a tag ("COT Merchant needs to have a countrytag"); a second entry with the same tag **replaces** the first; `extra` (0x185) is accepted, never written |

The exact nesting of `tag` (a 0x238 `id` token is built next to it in `0x49901b`) is NIEPEWNE.
Load path `0x4a1649`: keyword `cot` followed by `{` → new CoT, parse, append with `0x49a7e7` (same
69 cap). **Not saved:** members' +0xaa (rebuilt by the init membership pass `0x419d80`; NIEPEWNE that
loading a save runs through the same init), +0x20 and +0x24 (recomputed), the candidate list.
Province +0xb2 is read from province.csv; no save writer for it was found (NIEZNANE), so the
−35 penalty is probably lost on reload.

Scenarios (`Scenarios/*.inc`, e.g. 1492.inc) use the same block with `location` and
`merchant = { TAG = XXX level = n }`, never `failure` (so 0). Counts in v1.09: 1419 21, 1492 23,
1617 26, 1683 17, 1700 27, 1755 29, 1773 27 CoTs. The file comment ("30 possible, at least 22,
missing ones are created") does **not** match the code: the cap is 69 and the target is
owned-land/29, growing by one per year.

## 7. Port summary (integer-friendly)

Units: map distance d in 1/100 map pixels as an integer (integer sqrt of the squared pixel
distance, x wrapping at 0x4a00 as in `0x43a820`, keep d ≥ 1 by the rule "divide only if d > 1").
Scores are fixed point ×100 in 32-bit ints (2 × S ≤ ~2 × 900, the +1000 and −9999.9 terms fit easily).

1. **SiteValue(p)** ×100: return −100 if sea or no settlement; A = 1 / 25 / 50+20·size, +1 fort ≥ 3,
   +1 coastal; + (Europe ? 4 : 1)·hist; + 30·tradeEff (tradeEff already ×100 → ×30); + navalTech/2;
   + V/5 (V in the economy's fixed point); +10 manufactory; +10·d(current CoT home, p);
   religion 0..4 +50, pagan without fantasy → 0; +100 outside Europe.
2. **Monthly**: compute t, k, monopoly; f in hundredths (+100, −50×3, +100, −50/+200/+115);
   store the counter in units of 1/1200 (100.0 = 120000, 110.0 = 132000): counter += f, clamp
   0..132000. VP +0.16 per monopoly entry. Rebuild CoT value = Σ 12·V.
3. **Membership(p)**: skip non-land/unowned/homes; score per CoT exactly as §4 (×100), minus
   3·counter, then score·1 / d if d > 1 (do the division last, on the ×100 value; truncation is
   acceptable); keep strictly best; assign.
4. **Yearly**: collect counter ≥ 100; dissolve each (log, drop merchants, clear home pointer and
   *all* other pointers to it, hist −= 35); if any, membership for all; candidates = cities
   land-owned non-home, sort by SiteValue descending, ties by province id ascending (deviation: the
   original order is unstable); if (owned land / 29) > count and count < 69: found one at the top
   candidate (+1 merchant for its owner, placed there); membership for all; renumber.
5. **Init/load**: read `cot` blocks (cap 69, last duplicate tag wins), then membership for all
   provinces. Also run Membership(p) when a colony or trading post is founded in p.
6. **Save**: location, failure, merchants (tag, level, value). Recommended deviation: also save
   province +0xb2 so the dissolution penalty survives a reload.

## 8. Open points

- R+0x36 meaning ("at war" per the brief vs unknown flag in SPEC_WAR_DIPLOMACY) - NIEPEWNE.
- Global flag Oc+0xfda in `0x4cbf60` ("refuses trade with everyone"?; toggled by command handler
  `0x454700`) - NIEZNANE.
- `0x444870(owner,1)` + command 0xa7 on founding = free merchant - NIEPEWNE (from SPEC_TUTORIAL).
- CoT+0x08 list and merchant `extra` list - NIEZNANE.
- Whether loading a save goes through the init membership pass `0x419d80` - NIEPEWNE.
- Behaviour when a CoT home loses its owner (S(C) and the Oc terms read a null owner) - NIEZNANE.
- Whether +0xb2 is saved anywhere - NIEZNANE (no writer found).
- Exact date of trade-manager events 0x16/0x17 (month start / year start) - see SPEC_TIME_ECONOMY.
