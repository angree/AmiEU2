# SPEC_AI_STRENGTH — country strength indices (+0x360 / +0x364 / +0x368)

Source: eu2.exe v1.09 (GOG 2.0.0.1), function **`0x43c010`..`0x43c855`** (`ret` at `0x43c855`;
the previous function ends with `ret` at `0x43c00c`). It is a country method (`this` = C) with no
arguments and no return value. Dump: `C:\temp\amiga_eu2\sr\str_main.txt`. Everything below is a
specification read from the disassembly; no code is copied.

## 1. Callers and frequency

| Call site | Enclosing function | When |
|---|---|---|
| `0x451595` | country monthly `0x451300` (vtable slot 0x268), step 6 of SPEC_TIME_ECONOMY §4.1, right after blockade share `0x43d200` and manpower `0x43ce50`, before `0x43c000` | **every month, every country** (no AI/human gate) |
| `0x45091a` | `0x4505d0` = country vtable slot 0xac (`GAME_STARTED` 0x50 handler, SPEC_NATIVES); runs right after the initial monthly income +0xe80 has been computed from the provinces | once at game start / load |

So the indices are a month old at most and are valid from day one.

## 2. Outputs

| Field | Meaning (our name) | Written at | Floor |
|---|---|---|---|
| C+0x364 | **FS** land strength index | `0x43c41e` | 1.0 (`0x43c81d..0x43c830`) |
| C+0x368 | **FN** naval strength index | `0x43c445` | 1.0 (`0x43c836..0x43c849`) |
| C+0x360 | **FE** economic strength index (SPEC_AI calls it F3) | `0x43c804` | 1.0 (`0x43c80a..0x43c817`) |

All three are set to 1.0 at entry (`0x43c018..0x43c02f`) and floored at 1.0 at the end. Nothing
else is written. Readers: diplomacy AI `0x536000..0x540e15` (FS+FN: alliance honour ratio, candidate
score, SPEC_AI §1-§4), trade AI `0x5521bd..0x5526a4` (FE ratio, SPEC_AI "Trade scores"), ledger /
UI `0x517236..0x51726c`, `0x4112ad`/`0x41127d` (NIEPEWNE: a sort).

## 3. Inputs (our terms)

| Symbol | Source | Meaning |
|---|---|---|
| inc | C+0xe80 (float) | last month's income |
| money | `0x45b940` + C+0xe70 | 12·inc (annual income) + treasury (`0x43c039..0x43c046`) |
| nInf, nArt | country vf 0x388 / 0x390 | infantry men / artillery pieces owned + queued (SPEC_MILITARY_AI) |
| nWar, nGal | country vf 0x394 / 0x398 | warships / galleys (transports vf 0x39c are **not** used) |
| costInf/Cav/Art | `0x429ab0` / `0x429b00` / `0x429b50` | unit cost incl. inflation, ≥ 1 (SPEC_UI_WINDOWS4 §1.4) |
| IF CF AF IS CS AS | `0x433400 / 0x433450 / 0x4334a0 / 0x4334f0 / 0x433540 / 0x433590` | LAND.CSV fire/shock of inf/cav/art at C's land level, /100 |
| WF GF WS GS | `0x4336d0 / 0x433720 / 0x433770 / 0x4337c0` | NAVAL.csv record +0x18/+0x1c/+0x20/+0x24 = warship fire, galley fire, warship shock, galley shock (/100) |
| landLvl | `0x4333c0` → vf 0x34 | land tech level (integer) |
| mL, mN | `0x438690`, `0x438850` | maximum land / naval morale of C (SPEC_WAR_DIPLOMACY §8.2) |
| MIL, ADM | monarch C+0x20bc looked up in C+0x1f8e (`0x42ea40`), getters `0x42f5c0` (+0x8) / `0x42f540` (+0x10), clamped ≤ 9; **3** when there is no monarch | ruler military / administrative skill |
| maintL | `0x459c40` + `0x459e20` + `0x45a000` | monthly land maintenance (inf + cav + art) |
| maintN | `0x45a1f0` + `0x45a3f0` + `0x45a5f0` | monthly naval maintenance (war + galley + transport) |
| ports | C+0x1f84 (int) | count from misc refresh `0x43adcb`: own provinces with field +0x41c (port, NIEPEWNE) on the capital's landmass/continent ("home ports"; SPEC_ENVOYS: 0 = no harbour) |
| yards | C+0xfd2 (int) | shipyards |
| loans | container C+0xecc (vtable `0x5e48dc`, vf 4 `0x411bf0` = count at +0xc); list head C+0xed0 | loan objects: +0x1a amount, +0x16 rate (percent, fixed per loan) |
| PE | §4.4 | production efficiency (same sum as `0x43b560`) |
| TE | §4.4 | trade efficiency (same sum as SPEC_SMALLRULES §3.3 / SPEC_TIME_ECONOMY §5.3) |
| k | C+0xec8 (float) | set to 1.0 in the country ctor (`0x435eea`), no other direct writer found (NIEPEWNE: savegame) |
| bankr | C+0xe9c (date) | last bankruptcy date |

## 4. Formulas

### 4.1 Leader sums (loop `0x43c118..0x43c1a4`)

Walk the unit list C+0x124c (node: +0 unit, +8 next). For each unit take its commander
L = `0x48c040`(unit) (the attached leader with the lowest rank +0x22; a unit without one gets its
built-in record, rank 100, or nothing). Skip if no L or **L.rank ≥ 5**. Leader getters (vtable
`0x5e8a6c`): fire = vf 0xc `0x488160`, shock = vf 0x10 `0x4881b0`, siege = vf 0x14 `0x488200`,
each = record byte (+0x27/+0x28/+0x29) + rounded policy modifier (`0x42b940/0x42b920/0x42b960` on the
owner's C+0x37e), floored at 0; fire/shock are 0 for a conquistador (category 2) whose unit is on a
province with continent ≠ 2 (NIEPEWNE which continent 2 is). maneuver = signed byte L+0x26.

- unit vf 0x14 true (army): **GL1 += fire + shock − 5**, **GL2 += siege**.
- otherwise (fleet): **GN += fire + shock + 2·maneuver − 10**.

### 4.2 Maintenance ratios (`0x43c1b0..0x43c240`)

ratioL = maintL / inc and ratioN = maintN / inc, but **only if inc ≥ 1**; otherwise the raw sum is
kept (no division).

### 4.3 FS and FN

```
inf'  = nInf + 500·money / costInf
cav'  = nInf + 400·money / costCav        <- vf 0x388 (infantry) is called twice; the cavalry
                                             count is never read. Literal original quirk.
art'  = nArt +  10·money / costArt
troop = 0.001 · ( (IF+IS)·inf' + (CF+CS)·cav' + (AF + 10·AS)·art' )          (0x43c244..0x43c394)
ships = nWar·(WF + WS) + 0.33·nGal·(GF + GS)                                    (0x43c042..0x43c10e)

FS = landLvl + 20·mL + troop + GL1 − 2.5·ratioL + 2·MIL + GL2                   (0x43c3bb..0x43c41e)
FN = 5·mN + ports + yards + ratioN + GN + ships                                 (0x43c424..0x43c445)
```
Note the asymmetry, both literal: the naval tech level itself is not added (only through
ship stats and morale), and naval maintenance is **added** to FN while land maintenance is
subtracted from FS. The money terms use the same `money` (negative treasury lowers them).
Constants: 0.33 `0x5e71a8`, 500 `0x5e71a4`, 400 `0x5e6114`, 10 `0x5e50ac`, 0.001 `0x5e71a0`,
20 `0x5e60fc`, 2.5 `0x5e719c`, 5 `0x5e4644`.

### 4.4 Efficiencies (`0x43c4dc..0x43c71a`)

- PE = max(0.01, infra.csv[infraLvl]+0x10 (`0x4333e0` vf 0x34 → table `0x64df20`, vf 0xc)
  + religion Prod.Eff `0x64d5f0`[C+0x254] + 0.01·int C+0xf02 + 0.01·`0x42b6f0`(C+0x37e)).
- TE = max(0.01, trade.csv[tradeLvl]+0x10 (`0x4333f0` → `0x64dab0`, vf 0x8) + religion Trade.Eff
  `0x64d61c`[C+0x254] + 0.01·int C+0xefe + 0.01·`0x42b6c0`(C+0x37e) − 0.03·C+0xe68 − 0.03·C+0xe6c
  + 0.02·C+0xe64). TE is computed **twice** with identical code (`0x43c57e` and `0x43c64c`);
  the result is the same value TE both times. Bounds failures only log "Array bounds violation."
  and use element 0.

### 4.5 FE (`0x43c44b..0x43c804`)

```
net   = 0.12·inc − 0.12·(maintL + maintN) − Σloans amount·rate·0.0001
        (0.12 = 12 months × 0.01: yearly money in hundreds; interest = amount·rate% /100)
m     = months since bankr (0x417440(today, bankr), y·12 + m):  < 12 → pen = −10,
        < 60 → pen = −5, else pen = 0
FE    = (net + 0.01·money + 50·(TE·k + TE) + 100·PE) · (ADM + 50) · 0.01 + pen + nLoans
```
With k = 1.0 the TE term is 100·TE. **nLoans is added** (positive), literal (NIEPEWNE intent).
Constants: 0.12 `0x5e7198`, 0.0001 `0x5e50a4`, 0.01 `0x5e4eec`, 50 `0x5e5620`, 100 `0x5e5650`,
−10/−5 immediates `0xc1200000`/`0xc0a00000` (`0x43c785`, `0x43c794`).

## 5. Magnitudes (orientation only)

LAND.CSV /100 gives IF/IS ≈ 1..2.5 but AF up to ~300 and AS up to ~90, so artillery dominates
`troop` once it exists; 20·mL is ≈ 30..110; landLvl 0..60; 2·MIL ≤ 18. A 20 000-man infantry
army at mid tech adds ≈ 60; a large treasury adds far more via inf' (500·money/9 men).
FN for a 30-warship navy is ≈ 100..150; for a country without a navy it is ≈ 5·mN + ports.

## 6. Port summary (integer friendly)

Keep the three indices as `long` in **centi-units** (×100) and do the whole function monthly per
country (`0x451300` step 6) and once after load/new game:

```
money   = 12*inc + treasury                         (whole ducats)
inf'    = nInf + 500*money/costInf                  (long; costs >= 1)
cav'    = nInf + 400*money/costCav                  (keep the quirk: infantry, not cavalry)
art'    = nArt +  10*money/costArt
troop100= ((IF+IS)*inf' + (CF+CS)*cav' + (AF+10*AS)*art') / 1000     (stats kept ×100 = csv %)
ships100= nWar*(WF+WS) + 33*nGal*(GF+GS)/100        (stats ×100)
FS100   = 100*landLvl + 20*mL100 + troop100 + 100*(GL1+GL2+2*MIL) - 250*maintL/max(inc,1)
FN100   = 5*mN100 + 100*(ports+yards+GN) + 100*maintN/max(inc,1) + ships100
net100  = 12*inc - 12*(maintL+maintN) - SUM(amount*rate)/100
FE100   = (net100 + money + 100*TE100 + 100*PE100) * (ADM+50) / 100 + 100*pen + 100*nLoans
each: if < 100 then 100
```
(`TE100`/`PE100` = efficiencies ×100, `mL100`/`mN100` = morale ×100, ratio division only when
inc ≥ 1, otherwise use maint itself; with k = 1 the TE term is 100·TE100.) Overflow: in csv
percent AF+10·AS reaches ~122 500, and art' grows with money (10·money/costArt), so the art product
can pass 2^31. Compute every stat·n'/1000 as stat·(n'/1000) + stat·(n'%1000)/1000 (exact, safe
while stat ≤ 2·10^6), or clamp n' to 10^6 first.

Replacing src/ai.c (~line 172): our `1000·tech + 2000·MIL + men` is roughly 1000 × the original
scale. Readers that only use ratios (alliance honour S_friend/S_enemy, FE ratio in trade) do not
care, but SPEC_AI candidate score A clamps rel·(FS+FN)·0.1 at ±300 in **original** units, so the
port must use original units (or divide centi-units by 100 at those readers).

## 7. Open points

- NIEPEWNE: C+0xec8 is always 1.0 (only the ctor writes it directly); a savegame key may set it.
- NIEPEWNE: whether unit list C+0x124c contains fleets as well (the fleet branch implies it;
  SPEC_BOOST names +0x1260 as the fleet list). If fleets live only in +0x1260, GN is always 0.
- NIEPEWNE: L.rank < 5 is the "active commander" cut; meaning of rank values in the leader DB.
- NIEPEWNE: continent 2 in the conquistador fire/shock rule (§4.1).
- NIEZNANE: why naval maintenance and loan count are added with a positive sign (kept literally).
- The cav' quirk (infantry count used twice) is literal; keep it, it is cheap and matches AI behaviour.
