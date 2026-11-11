# SPEC_MARKET — the world goods market (eu2.exe v1.09)

Specification only (prose and tables). Every claim cites the eu2.exe addresses it was read from.
Dumps: `C:\temp\amiga_eu2\sr\mkt_*.txt` (main `mkt_main`, constructor and reader `mkt_ctor`,
helpers `mkt_bld`, `mkt_set`, `mkt_pg`, `mkt_c43b`, `mkt_lead`). Marks: **NIEPEWNE** = believed,
not proven; **NIEZNANE** = not read.

**This file replaces SPEC_TIME_ECONOMY §9.1.** That section numbers the demand cases one too
low: it shows the case index of the switch (`id − 1`, jump table `0x41bc80`), not the goods id.
It also names no goods. Both are fixed below.

## 1. Objects, ids and names

The market is the global object `0x66c370`: vtable at +0, then `goods[21]` (pointers) at
`0x66c374`, indexed by the **goods id**. GOLD is `[0x66c37c]`, SLAV is `[0x66c3a8]`.

- **Reader** `0x41b490` runs on `db\goods.csv`, called from `0x47a6e8` before `db\province.csv`.
  Each row is: name, Base Resource Value, Mine, TradePost, NumOfSlaves.
- Per row it calls `0x41ad30(name, base)`, then sets +0x128 = (Mine == 1),
  +0x129 = (TradePost == 1) and +0x12a = NumOfSlaves.
- It stores the object at `goods[obj+4]` (`0x41b597`). The file's row order is therefore
  irrelevant; the array order is the id order.
- **Name → id** is `0x41b210`, and it is the only binding.
  - Province goods are bound through the same function: province.csv at `0x484897`,
    and saves/scenarios at `0x4b7a11` (a number, the id itself, NIEPEWNE).
  - Any name not in the list gives id 0. Comparison is `0x5bc060` (strcmp, case-sensitive,
    NIEPEWNE).

| id | name | base | id | name | base | id | name | base |
|---|---|---|---|---|---|---|---|---|
| 0 | NOTHING | 0 | 7 | IVOR | 10 | 14 | SPIC | 15 |
| 1 | CLO | 15 | 8 | METAL | 15 | 15 | SUG | 15 |
| 2 | GOLD | 0 | 9 | MINERAL | 15 | 16 | TOB | 10 |
| 3 | COT | 10 | 10 | ORIENT | 15 | 17 | WINE | 15 |
| 4 | FISH | 10 | 11 | NAVS | 5 | 18 | WOOL | 5 |
| 5 | FURS | 10 | 12 | SALT | 15 | 19 | TEA | 10 |
| 6 | GRAI | 5 | 13 | SLAV | 5 | 20 | COFFEE | 10 |

The base values come from goods.csv v1.09; they are data, not code.

- **WOOL, TEA and COFFEE are three separate goods** (ids 18, 19, 20), each with its own
  supply, demand and price.
- Note the order: COFFEE → 20 is tested before TEA → 19 (`0x41b3d7`, `0x41b3f0`).
- There is no WEAP, SPIR, ARTS or INST goods. Those names occur only in the constructor's
  second if-chain (`0x41af1a..0x41b1f0`), which sets field **+0xc**: CLO 1, COT 2, ARTS 3, GOLD 4,
  FISH 5, FURS 6, GRAI 7, INST 8, IVOR 9, METAL 10, MINERAL 11, NAVS 12, ORIENT 13, SALT 14, SLAV 15,
  SPIC 16, SPIR 17, SUG 18, TOB 19, WEAP 20, WINE 21, WOOL / TEA / COFFEE 22, NOTHING 0.

- **+0xc is not the market id.** Its use is NIEPEWNE: a second icon or category index.
- The goods sprite `resource.bmp` has 21 frames (`0x41ade3`). Its frame is set from
  **+4** (`0x41af17`).

Object fields: +0 price (float), +4 id, +0xc the category above, +0x10 text `<name>DEMAND`,
+0x14 name, +0x114 base value (int), +0x118 = 0, +0x11c supply (float), +0x120 demand (float),
+0x124 settled producer count, +0x128..+0x12a csv flags, +0x12e description, +0x136 sprite.

The constructor sets price = base and does not initialise supply or demand (`0x41ad59..0x41ad8c`).

## 2. When it runs

- Receiver `0x41b5e0` is called by the province manager `0x475910`, **before** the
  per-province handlers in the same message. It handles two messages:
  - **0x16 TIMER_MONTH:** `0x41b620` (prices), then `0x41bcd0` (production leaders, §7).
  - **0x50 GAME_STARTED** (start or load): `0x41b620` only.
- **Nothing of the market is saved.** The only references to `0x66c370..0x66c3c4` are:
  - the constructor, reader and destructor (`0x475030`, `0x47ace0`);
  - the dispatcher;
  - the province goods binding;
  - GOLD and SLAV lookups.

  There is no save keyword for price, supply or demand. All three are recomputed on
  GAME_STARTED. The only market-related save datum is the province's `goods`
  (keyword table entry `0x18d` → "goods", `0x49ec91`).

## 3. Supply (`0x41b68d..0x41b70d`)

1. For all 21 goods: supply = **0.5** and producers (+0x124) = 0.
2. For every province slot 0..2019 (stride 0x4a0; slot 0 included), if the province has a
   settlement:
   - supply(goods of the province) += **0.01** (`0x5e4eec`);
   - then supply > 1.5 → 1.5 (`0x5e44ac`), and supply < 0.5 → 0.5.

   "Has a settlement" is `0x47f6c0`: the settlement object +0x408 (colony or city), **or** a
   trading post +0x40c. Owner, controller and blockade are not tested.

Supply therefore saturates at 100 settled provinces. A trading post counts for supply but for
nothing in §4.

## 4. World-wide counters (`0x41b713..0x41b887`)

These are counted only for provinces with prov+0x408 ≠ 0. That means colonies and cities,
**not** trading posts. Colony versus city is not tested.

| sym | counts | test |
|---|---|---|
| S | shipyards | `0x47caf0`, city+0x68 |
| B | conscription centres | `0x47cb10`, +0x69 |
| G | governors | `0x47cb50`, +0x6b |
| J | chief judges | `0x47cb70`, +0x6c |
| M | settlements with a manufactory | city+0x1c ≠ 0 |
| m2..m5 | manufactories of type t = city+0x1c (jump table `0x41bc50`) | |
| n1 | settlements whose goods id = 1 (CLO) | byte map `0x41bc70`, table `0x41bc64` |
| w | settlements whose goods id is 3, 15 or 16 (COT, SUG, TOB) | +**1** if owner (prov+0x3c4)+0x254 ∈ {0,1,2,3,4} (Prot, Ref, Counter-Ref, Cath, Orth), else +**0.1** (`0x5e43a0`) |

The tax collector (+0x6a) is not counted.

- Manufactory types: 1 Fine Arts Academy, 2 weapons (NIEPEWNE), 3 naval equipment
  (NIEPEWNE), 4 refinery, 5 goods manufactory (SPEC_BUILDINGS).
- Type 1 is counted (stack slot +0x38) but **never read**.
- Producers +0x124 of the province's goods += 1.
- The set {3, 15, 16} is hard-coded. It coincides with goods.csv NumOfSlaves = 1, but +0x12a
  is not read here.

## 5. Demand (`0x41b8b0..0x41bbb6`, jump table `0x41bc80`, index id − 1)

Each case **overwrites** demand: **d = clamp(formula, 0.01, 2.0)**. The clamp is "> 2 → 2,
< 0.01 → 0.01", applied in that order. The formula is a pure function of this month's
counters: there is no smoothing and no use of last month's value.

Then the common tail `0x41bbb6` applies **d = min(d, cap)**, where the cap is the value left in
FPU st(0). The tail compares the stored demand with the cap (not old against new). Only if
d > cap does it store the cap and re-clamp to 0.01..2.0.

| ids (goods) | formula | cap | evidence |
|---|---|---|---|
| 1 CLO | 0.5 + 0.01(m5 + B) | 1.5 | `0x41b981` |
| 2 GOLD | 1.0 | 1.5 | `0x41b8c6` |
| 3 COT | 0.5 + 0.01(n1 + m5) | 1.5 | `0x41b930` |
| 8 METAL | 0.5 + 0.01(m2 + B) | 1.5 | `0x41bac1` |
| 9 MINERAL | 0.5 + 0.01(m2 + S) | 1.5 | `0x41b9d2` |
| 11 NAVS | 0.5 + 0.01(m3 + S) | 1.5 | `0x41ba70` |
| 15 SUG | 1.0 + 0.01·m4 | 1.5 | `0x41ba23` |
| 4 FISH, 6 GRAI, 12 SALT, 17 WINE, 18 WOOL | 0.25 + 0.01(S + B + M) | **1.0** | `0x41b8d7` |
| 5 FURS, 7 IVOR, 10 ORIENT, 14 SPIC, 16 TOB, 19 TEA, 20 COFFEE | 0.75 + 0.01(J + G + S + B + M) | 2 (no effect) | `0x41bb5a` |
| 13 SLAV | 0.1·w | 3 (no effect: already ≤ 2) | `0x41bb12` |
| 0 NOTHING (default) | not computed: the stored value is kept | 1.5 | `0x41b8b0` |

- NOTHING's demand is never initialised (NIEZNANE start value). Its base value is 0, so its
  price is always 0.
- The constant 1.5 at `0x5e44ac` is loaded before every switch dispatch. It is the cap of
  every case that does not replace st(0).

## 6. Price (`0x41bbfd..0x41bc1d`)

**price(+0) = base(+0x114) × (2 − supply) × demand**, a float, for all 21 goods including
NOTHING.

The range is 0.5 × 0.01 × base to 1.5 × 2 × base.

After the loop, for each of the 340 country slots `0x64e140..0x64e690`, `0x43b140` runs:

1. It zeroes country+0xf06[21] and +0xf5a[21].
2. For every province slot ≥ 1 owned by the country (+0x3c4; the controller is not tested),
   country+0xf06[goods id] += the settlement's vfunc+0x18 (production multiplier).
   - A province with no settlement adds 0.
   - Trading posts count through their own vfunc.

This runs on both messages.

## 7. Production leaders (`0x41bcd0`, TIMER_MONTH only)

For each goods id 0..20, including NOTHING and GOLD:

- best = 0. Walk the countries in slot order. If best < country+0xf06[id], that country
  becomes the leader and best = int(that value) (`0x5b98fc`, ftol).
- A later country therefore wins by beating the **truncated** integer of the current best.
  Ties at equal integers go to the later country if its float is larger.
- The leader gets `0x55f030(country+0x20e2, 0, 1/12)`, which is VP category 0, Economy
  (`0x3daaaa7e`).

## 8. Differences from our code (`src/economy.c`)

1. **Wrong id table.** `cid[]` (line 154) reproduces field **+0xc** (CLO 1, COT 2, ARTS 3,
   GOLD 4, …, WOOL/TEA/COFFEE 22), not the market id **+4** (`0x41b210`). Combined with the
   old shifted cases, 14 of the 20 real goods get the wrong demand:

   real → ours: CLO 0.5+m5+B → 1.0; GOLD 1.0 → 0.75-group; GRAI 0.25 → METAL rule; METAL m2+B →
   NAVS rule; MINERAL m2+S → 0.25; NAVS m3+S → 0.1w; SALT 0.25 → 1+m4; SLAV 0.1w → 0.75; SPIC 0.75 →
   0.25; SUG 1+m4 → 0.75; WINE 0.25, WOOL 0.25, TEA 0.75, COFFEE 0.75 → all 1.0 (default).

   Correct by coincidence: COT, FISH, FURS, IVOR, ORIENT, TOB, NOTHING.
2. **Plantation counter.** `g == 3 || 15 || 16` under cid selects ARTS, SLAV and SPIC.
   It must be COT, SUG and TOB. n1 (`g == 1` = CLO) is right by coincidence.
3. **Missing 1.5 cap** on the 0.5 groups, GOLD and SUG. Ours lets them reach 2.0.
4. **Slaves.** Ours exempts its case 12 (the slave formula, which cid gives to NAVS) from the 2.0 clamp and caps at 3.0; the real maximum is 2.0.
5. **Supply ignores trading posts.** `has_city` = population > 0. The real rule also counts
   provinces with only a trading post (prov+0x40c, our `p->tp`). Province 0 is included in the
   real loop (harmless).
6. **Colonies.** Ours skips them for the building and manufactory counters (`is_colony →
   continue`). The real code counts every +0x408 settlement. This matters only if a colony can
   hold a manufactory or official (NIEPEWNE: normally not).
7. **Production leader** (score_month).
   - Real: summed per **owner**, no controller test; trading posts contribute their own
     multiplier; NOTHING and GOLD are included.
   - Ours: requires owner == controller, uses 0.5 for colonies (NIEPEWNE versus vfunc+0x18),
     skips goods with value 0 and gid ≤ 0 (that is, NOTHING).
8. **Same as the original:**
   - the price formula (×1000 fixed point);
   - the supply step and clamp;
   - the formulas themselves once the id is right;
   - monthly order (market before trade and provinces);
   - lazy first compute in `econ_price_m`, which equals GAME_STARTED;
   - not saving the market.

## 9. Port checklist

- [ ] Replace `code[]` / `cid[]` with the §1 id table (+4). Keep +0xc only if the UI needs it.
- [ ] Re-key the `market_month` switch to real ids (§5), with caps 1.5 / 1.0 and the 0.01..2.0
      clamp for all, slaves included.
- [ ] w over COT, SUG and TOB; n1 over CLO; Christian = state religion index 0..4.
- [ ] Supply counts settlement **or** trading post; the §4 counters count settlements only.
- [ ] Production leader: owner-only, include NOTHING and GOLD, settlement production
      multiplier (§6).
- [ ] Optional: drop the colony skip in the counters (§8.6) after checking the building rules.

NIEZNANE / NIEPEWNE:

- the names of manufactory types 2 and 3;
- the purpose of +0xc;
- whether province goods in a save are written as the numeric id;
- the colony value of vfunc+0x18;
- whether the market runs on clients too (no host test is seen in `0x475910`).
