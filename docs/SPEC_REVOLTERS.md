# Rebel-held provinces and revolter countries (EU2 v1.09)

Clean-room specification read from `eu2.exe` (objdump disassembly, VAs). Prose and tables only.
**NIEPEWNE** = believed, not proven; **NIEZNANE** = not found. Work scripts and dumps:
`C:\temp\amiga_eu2\sr\rv*.sh`, `rv_*.txt` (`rv_main.txt` = `0x4609a0..0x461780`).
Conventions (country table `0x64e140`, REB = tag idx 0x153, dates, message codes) as in
`SPEC_SMALLRULES.md`. Date helpers proven here: `0x417220(a,b)` = a > b (strict),
`0x417270(a,b)` = a >= b, `0x417210(n)` adds n years, `0x4171a0(n)` adds n months,
`0x417440(a,b)` = a − b as {day, month, year} in 30/360-day units.

---

## 0. Data: revolt.txt records

`db\revolt.txt` is loaded by `0x5b1700..0x5b1856` into a global array of 340 records of
**0x146 bytes at `0x8ff640 + 0x146·tag`** (ctor `0x5b3bf0`, parser = record vfunc 0 `0x463680`).
268 tags have an entry. Record layout:

| offset | key | meaning / default | parser |
|---|---|---|---|
| +0x04 | `date` | start of window (date) | `0x46371d` (token 0x33) |
| +0x08 | `expirydate` | end of window (date) | `0x463728` (0x61) |
| +0x0c | `minimum` | province-id list object (head at +0x10) | `0x4638e1` (0x184) |
| +0x1e | `extra` | province-id list object (head at +0x22) | `0x4638f1` (0x185) |
| +0x30 | `capital` | province id (atoi) | `0x463733` (0x59) |
| +0x34 | `group` | tech group: latin 0, orthodox 1, muslim 2, china 3, exotic 4 | `0x46384e` (0x80) |
| +0x38 | `revolt` | byte "may form by revolt"; **default 1**; `revolt = no` → 0 (6 entries: JER, KNI, ATH, LAT, CAL, LIV) | `0x4636d1`, `0x4638cf` (0x17d) |
| +0x3a | `no` | tag index; this tag must not exist (16 entries, e.g. ROY↔PUR); default 0 | `0x463747` (0x4d) |
| +0x3e | `ai` | AI file name; **default "default.ai"** | `0x46392a` (0x16b) |
| +0x13e | `culture` | culture id; **default 0 = none** | `0x463901` (0x1d8) |
| +0x142 | `religion` | protestant 0, reformed 1, counterreform 2, catholic 3, orthodox 4, sunni 5, shiite 6, pagan 7, tokens 0xfa/0xfb/0xfc → 8/9/10; **default 11 = none** | `0x46375e` (0x1b) |

**Reverse index (province +0x64 list, head +0x68)**: after each record is parsed, the tag is
appended to the list of **every province in its `minimum`** (not `extra`) (`0x5b17e8..0x5b184b`).
So "P+0x68" = the revolter tags whose `minimum` contains P. (SPEC_REVOLT_RELIGION §2 calls the
lists at `0x8ff64c`/`0x8ff65e` "cores"/"claims": they are these `minimum`/`extra` lists.)

---

## 1. Monthly driver

`0x460aa0(C)` (country month step 25, host only; SPEC_SMALLRULES §4.2):
1. revolt roll `0x460450(C, 0)` (SPEC_REVOLT_RELIGION §2);
2. **`0x460ae0(C, force = 0)`** — at most one revolter formed per country per month (returns 1 at
   the first success), plus defection checks (§4) for the other rebel-held provinces;
3. if (owned provinces controlled by REB, `0x438c00`) **>** (controlled provinces with a full
   city, `0x438c40`): `0x4609a0` = COUNTRY_BROKEN: unless C+0x11e and game type 3, call
   `0x460ae0(C, 1)` **repeatedly while it returns 1**, then post 0x128 (handler `0x4550a0`,
   SPEC_SMALLRULES §4.2: remaining REB-held provinces go back to C).

## 2. `0x460ae0(C, force)` — forming a revolter country

Loop over C's **owned** province list (C+0x131e). Only provinces P whose controller (P+0x3c8)
is REB are considered. For each such P, loop over the tags X in P+0x68 (in list order); X
**qualifies** when all hold (`0x460b6a..0x460cd6`):

1. X is not 0 and **X does not exist** (`0x64e140[X] == 0`);
2. P+0xae (unit container pointer) is non-null (NIEPEWNE: always true);
3. record byte +0x38 set (`revolt` not `no`);
4. the `no` tag (+0x3a) does **not** exist (NIEPEWNE: idx 0 when absent, table[0] presumed null);
5. today **>** `date` (strict) and today **<** `expirydate`;
6. at least one `minimum` province is controlled by REB — always true here, since P itself is in
   X's `minimum` (that is why X is in P+0x68). Ownership of the minimum is **not** tested;
7. tag-specific: ROY (0x49): ENG (idx 1) does not exist or ENG's state religion is Reformed (1);
   FPR (0x4a): world flag 2 (`0x4d16a0(2)`, Calvin); SPR (0x4b): world flag 1 (Luther);
8. **timing** (skipped entirely when force = 1):
   - T = P+0x3c0 (date of the last controller change, §4.1) + **3 years** + m months, where
     m = trunc((s1 − 5) × 6 × 0.2) and s1 = C's domestic slider at policy+0x0c
     (`0x42b750`; NIEPEWNE that s1 is centralisation). m ∈ {−6, −4, −3, −2, −1, 0, 1, 2, 3, 4, 6}
     for s1 = 0..10 (truncation toward zero). NIEPEWNE: `0x4171a0` with negative n.
   - need today **>** T (skip if T >= today);
   - then **rand() % 100 < 50** (50 %).

The first X that qualifies is formed (§3) and the function returns 1. If no X in P+0x68
qualifies (or P+0x68 is empty), **the defection check `0x461220(C, P)` runs for P** (§4) and the
loop continues with the next province. At the end the function returns 0.

Consequences: only `minimum` provinces can trigger a revolter; `extra` provinces are only carried
along. Several eligible X for one P: list order of P+0x68 = revolt.txt file order. Random factors:
the 50 % roll only (none when forced).

## 3. What the new country gets

### 3.1 Command sequence posted by `0x460ae0` (`0x460d12..0x4611e4`)

L = every province of X's `minimum`, then every province of X's `extra`, that is currently
**controlled by REB** (whoever owns it).

1. **CREATE_COUNTRY (0xcd)** to C (`0x461cd0`): payload = X, C's tag (P+0x3c4 = C), the list L.
   Executed at once (`0x4b8770`; X exists right after, step 4 uses it).
2. For each province of L still controlled by REB: **PROVCHANGECONTROL (0x8d)** → controller X.
   For each unit in that province owned by REB: if it is a land army (unit vfunc +0x14),
   it **loses 75 % of its infantry** (amount = 0.75 × round(unit+0xf0 infantry), rounding by
   `0x5bc2a6`, NIEPEWNE ceil/floor; command 0x36 → unit vfunc +0x78 = `0x48d880` take-losses with
   mode 2; halved again by the owner's +0x1246 flag as for any loss); cavalry/artillery and fleets
   untouched. Then **0xd5** (`0x453450`): the unit changes owner to X (moved from REB's unit list
   +0x1248 to X's; AI version `0x556fb0` also registers it with X's AI).
3. **If force = 0 only** (`0x46108b`):
   - `0x444830(X, +1)`: command 0x5c once → X's +0xe98 += 1.0 (capped by `0x458d10`;
     NIEPEWNE: diplomats pool);
   - **GIVECB (0xad)** to X against C, expiry = today + **5 years**, CB bytes +0x1a = 1, +0x1b = 0
     (handler `0x447f20` appends to X's temporary-CB list for C, record +0x10);
   - **X declares war on C** (`0x4418b0(X, C)`; X is the attacker; normal declaration rules
     and messages of SPEC_WAR_DIPLOMACY apply).
   With force = 1 (COUNTRY_BROKEN) there is **no war, no CB, no extra diplomat**.

### 3.2 CREATE_COUNTRY handler `0x452de0` (runs on C, the country losing provinces)

N = the new country object of tag X, R = X's revolt.txt record.

1. `0x43b790(X)` creates the country object (ctor `0x435660`: treasury +0xe70 = 0, badboy
   +0xe42 = 0, war exhaustion +0x122a = 0, +0xec0 = 10.0 (manpower), +0xe98/+0xe94 = 0;
   NIEPEWNE the register holding the zeros; stability, sliders and monarch: ctor defaults,
   NIEZNANE values).
2. **Ownership**: each province of L is ceded to N (`0x481010`): removed from the old owner's city
   and colony lists; if old owner ≠ N: P+0xc2 = 0, city flags +0x6a/+0x6c cleared; if the
   province was controlled by its old owner the controller follows and P+0x3c0 = today.
   Then P+0x3bc := {0,0,0} (acquisition date cleared) and P+0xc2 := 0. Provinces of L owned by a
   third country are taken from it as well (no message to that owner).
3. X added to the global tag list `[0x8ff63c]+0x554` if not yet present (NIEPEWNE: "tags that
   have existed").
4. Tech group N.tech+0x84 := R.group. AI file N+0x260 := R.ai (default "default.ai"), loaded
   (`0x4aaf20`).
5. **Cores**: every province of R.minimum and R.extra is added to N's core set (N+0x1350).
6. **Map knowledge**: N copies C's known-province list (C+0x1330) and every such province is
   marked known to N (`0x476070`).
7. `0x4d79d0(N)` on `[0x8ff5ee]` (NIEPEWNE: registration in a global manager).
8. **Capital**: R.capital if N owns it (N+0x131a) and it has a city: N+0x1f7e := capital and the
   city is marked capital (`0x492cec`); otherwise automatic choice `0x439a70`.
9. **Culture**: R.culture, or if 0 the culture of N's capital province (+0xd8); added to N's
   accepted cultures (N+0x382) unless already accepted.
10. **Religion**: R.religion, or if 11 the religion of N's capital province (`0x47ca10`);
    N+0x254 := it; tolerances N+0x224.. := defaults for that religion (`0x430c70`, the same
    defaults as SPEC_REVOLT_RELIGION §1).
11. N vfunc +0x14 = `0x436060`: **if N is HOL, AI file := "holland.ai"**.
12. Diplomacy object reset (`0x4cb830`, `0x4cb810` at the end); in every existing country's
    diplomacy object, N's record is (re)initialised (+0x22 = N idx, +0x2e = that country);
    `0x4ce030`.
13. **Techs copied from C**: land (+0x0c), infrastructure (+0x3c), trade (+0x54), naval (+0x24)
    levels (`0x4333c0..0x4333f0`). Stability tech not copied.
14. **Message NEWNATION (code 0x5f)** if the local player knows N's capital province
    (P+0x256+viewer): "The people of %s declared independence from %s." (args N, C).
15. If N is HOL: global byte `0x8d0e1c` := 0 (it is set to 1 by EXEC_SET_ANNEXATION 0x102
    `0x4496ae` when HOL is diplomatically annexed; readers NIEZNANE).
16. `0x4b0f10`/`0x4b0e00` (serialise N into a buffer and read it back; NIEPEWNE: network sync),
    AI reloaded (`0x4aaf20`).
17. **Badboy**: if N is not pagan, **C's badboy −= |L|** (number of provinces transferred),
    floored at 0.
18. N+0xe4e := C+0xe4e if C is pagan, else 100 (meaning NIEPEWNE).

No VP entry is made anywhere in this path. Relations start from the reset values (no explicit
relation change).

## 4. Defection of rebel-held provinces: `0x461220(C, P)` and `0x4614b0`

### 4.1 The "rebels since" date

**P+0x3c0** (date) = date of the last change of controller. Set to today by
`0x4814c0` (set controller, only when the controller actually changes) and by `0x481010`
(owner change of a province its old owner controlled). Saved/loaded with the province
(`0x4b768b`). There is no separate rebel timer: for a REB-held province it is the day the rebels
took it; it resets whenever control changes again.

### 4.2 Conditions (`0x461220`, called from §2 for each REB-held province of C that formed nothing)

P controlled by REB, **(today − P+0x3c0).years ≥ 4** (whole 360-day years, `0x417440`),
P is **not a capital** (city flag +0x2c, `0x482920`), P **not under siege** (`0x482dc0`).

### 4.3 Choosing the receiver

For each neighbour Q of P (array P+0x11c, 12-byte entries, count P+0x114), X = owner of Q;
skip if none, X = C, **X at war with C** (C's diplomacy flag +0x36 for X), or X does not know P
(P+0x256+X). Score s:

| step | rule |
|---|---|
| base | 10; 35 if X accepts **Q's** culture (Q+0xd8, `0x429fb0` on X+0x382) |
| religion | if X's state religion is compatible with **Q's** religion (`0x431270` = 0): s += 30; else s = trunc(X.tolerance[Q.religion] + s) |
| revolt.txt | +30 if P is in X's `minimum`; +20 if P is in X's `extra` |
| overrides | s := −1 if Q is controlled by REB; s := −1 if P is not pagan and **Q's controller** is pagan |

The best score > 0 wins, ties to the first neighbour found (strict >). The same X may be scored
once per shared border; the maximum counts. As found, culture/religion are taken from the
neighbour Q, not from P (SPEC_REVOLT_RELIGION §2 says "province"; reproduce literally).

**Fallback** (no positive score, or P has no neighbours): walk P+0x68 (tags whose `minimum`
holds P) in order; skip X that does not exist, = C, is at war with C or does not know P. The first
remaining X gets P **only if** P is pagan or X is not pagan; otherwise **nothing happens** (the
walk stops there).

### 4.4 `0x4614b0(C, X, P)` — the transfer

1. **0x8e** (owner change, handler `0x44cfc0` on X): `0x481010`, P+0x3bc := 0, P+0xc2 := 0.
   Because the controller is still REB at that moment, a message is posted:
   C local → **DEFECTU (0x83)** "The revolters in %s have defected to the crown of %s." (with
   "We now have a casus belli upon them."); else X local → **DEFECTTU (0x82)**; else if the viewer
   knows P → **DEFECTO (0x84)**.
2. **0x8d** controller := X.
3. **GIVECB (0xad)** to **C** against X for **5 years** (+0x1a = 1).
4. REB units in P: land armies lose 75 % of infantry, then all REB units change owner to X
   (0xd5), exactly as §3.1 step 2.

No war, no badboy, no VP.

## 5. Other ways rebel control ends (as far as these functions go)

- Revolter formation (§3): REB-held provinces of L → owner and controller X.
- Defection (§4).
- COUNTRY_BROKEN 0x128 (`0x4550a0`): every province of C still controlled by REB goes back to
  C; rebel armies in C's provinces removed/disbanded.
- Military reconquest by sieges and rebel armies dissolving after broken morale are outside these
  functions (NIEZNANE here; SPEC_WAR_DIPLOMACY §13.3).

## 6. Existing or dead revolters

- A revolter that **exists** is never "re-formed": condition 1 fails, nothing is added by §2. It
  gains rebel provinces only by defection (§4): it scores +30/+20 for its `minimum`/`extra`, and it
  is the fallback receiver for its `minimum` provinces.
- A country whose object is destroyed has its table slot cleared (dtor `0x434f13`), so the tag can
  form again from revolt.txt (date window, `no`, etc. permitting). When a country is destroyed is
  outside this file (NIEZNANE here).
- `0x43d0d0(C, list)` (callers `0x43d05d`, UI `0x51ed3b`) lists the tags that do not exist, are in
  their date window and have a `minimum` province with a city owned by C and controlled by C or REB
  (NIEPEWNE: a "possible revolts" display; it forms nothing).

## 7. The Dutch revolt

- revolt.txt HOL: window 1559-01-01..1820-12-31, minimum {337, 338, 339, 340}, capital 339, no
  `extra`.
- The rebel-army builder (`0x461780`, `0x4619e0..0x461a76`) makes the large "religious" army in
  the Low-Countries provinces (`0x482970`) when world flag 2 is on, HOL does not exist and the
  province revolt risk > 10 (SPEC_SMALLRULES §2.3 J). This feeds REB control of 337–340.
- Once one of 337–340 has been REB-held for 3 years ± m months, HOL forms with 50 %/month (§2),
  declares war on the owner (normally the Habsburg/Spanish owner) and takes all REB-held
  provinces of 337–340; it gets cores on 337–340 and "holland.ai" (`0x436060`). Under
  COUNTRY_BROKEN it forms at once without the war.
- AI side (`0x55ebc5..0x55ec16`): with flag 2 on and no HOL, an AI army handling path for
  Low-Countries provinces (`0x45dec0` / `0x4432e0`) — unit logistics, not revolter formation
  (NIEPEWNE purpose).
- Byte `0x8d0e1c` (§3.2 step 15) is the only other HOL-specific state touched.

---

## Port notes

State a port needs:

- **Per province**: `control_since` date (P+0x3c0), set whenever the controller changes (and when
  an owner-controlled province changes owner); `acquired` date (P+0x3bc) cleared on revolt and
  defection transfers; the list of revolter tags whose `minimum` contains the province (build at
  load); `revolt_mod` (P+0xc2) zeroed on transfer.
- **Per revolt.txt tag** (340 entries): date, expirydate, minimum list, extra list, capital, group
  (0..4), may-revolt flag (default yes), `no` tag, AI name (default "default.ai"), culture
  (default none → capital's), religion (default none → capital's).
- **Per country**: temporary-CB list with expiry per other country; "at war with" flag per other
  country; core set; known-province list; slider s1 (for the ±6-month delay).
- Monthly per country on the host: revolt roll → `form_revolter(force=0)` (≤ 1 per month) with
  defection checks → COUNTRY_BROKEN test (then `form_revolter(force=1)` until none).
- Literal quirks to reproduce: defection culture/religion from the neighbour Q; fallback stops at
  the first eligible-but-pagan-blocked tag; badboy reduction for the losing country; forced
  formation skips date-delay/random/war/CB; ROY/FPR/SPR gates.
- Fixed-point: the only floats are m = trunc(1.2·(s1−5)), the 0.75 infantry loss and the
  tolerance in the score (tolerances are integers in data → store ×1 or ×100).
