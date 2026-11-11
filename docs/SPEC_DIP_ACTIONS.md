# SPEC_DIP_ACTIONS — effects of diplomatic actions, EU2 v1.09

Clean-room specification. Prose and tables only; every claim names the address it was read from
(disassembly of `eu2.exe`, image base 0x400000; dumps `C:\temp\amiga_eu2\sr\dipa_*.txt`, scripts in
`re\agent_dipa\`). No code was copied. Guesses are **NIEPEWNE**; things not found are **NIEZNANE**.
Relation values are the float −200..+200 of SPEC_WAR_DIPLOMACY §1.2. "Record X→Y" means the 0x36-byte
record for Y inside X's diplomacy object (country+0x38e). B = the cost base `0x45afc0(target)` of
SPEC_UI §3a. Dates use 30-day months and 12-month years (`0x4173c0` add, `0x417440` difference).

## 1. Command pipeline (all actions, player and AI alike)

1. A **sender** builds a request event and posts it to the host (`0x4b87a0`). The UI (`0x50e400`)
   and the AI plan executors (SPEC_AI §14: `0x552c50`, `0x552da0`, `0x5531f0` …) call the same
   senders, so the AI's actions have exactly the player's effects.
2. Destination 9 (static `0x6459bc`, set at `0x413780`) is routed by `0x4db2b0` (table `0x4db3a8`) to
   the **diplomatic host handler `0x4d2b60`** (switch base 0xe6, tables `0x4d5af0` / `0x4d5a60`). It
   runs only on the host (`0x4196e0`). It does almost no validation (see §4).
3. The host posts, in this order: USE_DIPLOMAT 0x58 to the sender (country handler `0x4482f0`); the
   **exec event** to the receiving country (immediate, `0x4b8770`); SET_SEND_DATE 0xd4 to the sender
   with (target, today + 1 month) (`0x4b8850`; stored at sender+0x39e+4·target by `0x453430`; this is
   the "diplomat en route" cooldown tested by `0x45c060`). The common tail is `0x4d3d49`.
4. The exec event is dispatched by the country dispatcher `0x461e90` to a country vfunc (table below).
   Unless stated, the exec event goes to the **target** T with payload +0xc = T, +0x10 = sender S.
5. Every relation change in this file is written to one record and then **copied** to the opposite
   record (`0x4cdfa0` clamp-set, `0x4cdf70` read), so relations stay symmetric.
6. A "CB of N" is GIVECB 0xad (handler `0x447f20`) posted with `0x4b87e0`: a temporary casus belli
   for the holder against the other country, expiry = today + N, flag byte +0x1a = 1, +0x1b = 0.

| UI id | Action | Sender | Request | Host case | Exec event → handler (runs on) |
|---|---|---|---|---|---|
| 0x12/0x13/0x14 | state / personal gift / letter | `0x441760` | 0x16a REQ_RELATION | `0x4d3d81` | 0x16b → `0x4548b0` (sender) |
| 0x16 | insult | `0x43e2b0` | 0x12b | `0x4d2f52` | 0x138 → `0x455830` (T) |
| 0x17 | claim throne | `0x43e580` | 0x12c | `0x4d30a8` | 0x139 → `0x4559e0` (T) |
| 0x15 | warning | `0x43e3a0` | 0x136 | `0x4d3a8d` | 0x143 → `0x456310` (T) |
| 0x18 | guarantee | `0x43e490` | 0x137 | `0x4d3bfc` | 0x144 → `0x4563b0` (T) |
| 0x1d | offer trade agreement | `0x43e670` | 0x12d | `0x4d31f5` | 0x13a → AI answer `0x557100`; yes 0x12f→0x13c `0x455e20`, no 0x130→0x13d `0x455ee0` |
| 0x1c | cancel trade agreement | `0x43e760` | 0x12e | `0x4d3457` | 0x13b → `0x455c50` (T) |
| 0x19 | ask military access | `0x43ea30` | 0x131 | `0x4d35f3` | 0x13e → AI `0x557170`; yes 0x132→0x13f `0x455f20`, no 0x133→0x140 `0x455fd0` |
| 0x1b | cancel military access | `0x43edf0` | 0x134 | `0x4d37e2` | 0x141 → `0x456010` (T) |
| 0x1a | revoke military access | `0x43ed00` | 0x135 | `0x4d3937` | 0x142 → `0x4561a0` (T) |
| 0x10 / 0xF | trade embargo / allow trade | `0x443be0` / `0x443cd0` | 0xff / 0x100 | `0x4d4f1f` / `0x4d4fff` | 0x113 → `0x447c70` / 0x114 → `0x447e40` (sender) |
| 1 / 3, 4 / 0xC | break royal ties / cancel vassalization / leave alliance | `0x441460` / `0x4414c0` / UI via `0x441520` | 0xf2 REQ_LEAVE_ALLIANCE | `0x4d4245` | 0x106 → `0x44a100` (the leaver) |
| 0xB | ban from alliance | — | 0xf5 | `0x4d43b3` | 0x109 → `0x44a680` (banned) |
| 6 | declare war | — | 0xfb | `0x4d57d7` | 0x10f → `0x44ada0` → 0xe8 `0x44b170` / 0xe9 `0x44b7a0` |

The 0x12b..0x137 requests never reach the country dispatcher (`0x461e90` sends them to its default).

## 2. Effects, action by action

### 2.1 Gifts (state 0x12, personal 0x13, letter 0x14)

Sender `0x441760`: cost = 250·B / 100·B / 25·B (`0x45b120` / `0x45b140` / `0x45b160`) computed at
click time, put in the request (+0x10 cost, +0x14 action id, +0x16 sender, +0x18 target).
Host `0x4d3d81`:
- If T does not know S's capital (contact byte of SPEC_UI §4.8), T discovers S: DISCOVERCOUNTRY 0x124
  plus discover-province 0x93 for S's capital (`0x4d2970`).
- **Relation value** = R100 · (DIP + R3 − 1) · k · 0.0033, with DIP = S's monarch diplomacy
  (`0x43bf40`), R3 = rand() mod 3 drawn first, R100 = rand() mod 100 drawn second (`0x5ba99c`),
  k = 100 (state), 50 (personal), 10 (letter) (`0x4d3db7..0x4d3e47`, constant `0x5ea4c8` = 0.0033).
  **The value does not depend on the cost, the target's size or its income.** Mean ≈ 0.163·DIP·k
  (DIP 5: state +82, personal +41, letter +8); range 0 .. 0.327·(DIP+1)·k, clamped by the ±200 limit.
- Posts 0x58, 0xd4, then 0x16b back to S with +0xc = value, +0x10 = cost.
Exec `0x4548b0` on S: if cost > 0, S books expense 15 (gifts) = cost and **T books income 7
("Events", straight to the treasury) = cost** (`0x438ab0`, `0x438a30`). Relation S↔T += value.
If S is the local human and cost > −1: message 0x5c RELCHANGE "%t, `<action>` has `<change>` our
relations with %s. They should be regarded as `<level>`." (`0x5782c0`). `<action>` = RELCH_A_SGIFT /
PGIFT / INT. `<change>` from the value v: v < −101 DEC_5, < −51 DEC_4, < −26 DEC_3, < −11 DEC_2,
< 0 DEC_1, < 11 INC_1, < 25 INC_2, < 51 INC_3, < 101 INC_4, else INC_5. `<level>` from the new
relation r: r > 175 R_0, > 124 R_1, > 76 R_2, > −74 R_3, > −124 R_4, > −174 R_5, else R_6.
**Not the gift path:** SENDGIFT 0x125 (`0x453530`: income 7 + relation += amount / max(50, ledger
+0x1cc)), quoted in SPEC_WAR_DIPLOMACY §2.4, has **no poster** in the exe (searched every immediate
0x125). It is dead code for the diplomacy window (NIEPEWNE: maybe a removed feature).

### 2.2 Insult (0x16) — exec `0x455830` on T
- Relation T↔S −50.
- S pays 10·B (expense 15, `0x45b100`) — paid at exec time, not at the click.
- **T gets a CB of 1 year** on S.
- T local human → message 0x69 INSUTOUS "%s has sent us an insult." (`0x577aa0`).
- If T's capital is on continent 1 and S's is not: T+0xe4e += 1 (the same counter as the war
  declaration, SPEC_WAR_DIPLOMACY §1.1; meaning NIEPEWNE).

### 2.3 Claim throne (0x17) — exec `0x4559e0` on T
- S pays 250·B (expense 15, `0x45b120`); **S stability −1** (`0x436490(1)`, which also triggers the
  host revolt roll `0x460450` when the drop goes below −3 and posts STABDROP for the local player).
- For **every** country X whose record X→S has the royal-marriage flag (+0x3b), T included:
  relation X↔S −100 (`0x455a3b..0x455aba`).
- **S gets a CB of 3 years** on T (`0x455aef`: year field 3).
- On the host only: T gets one revolt roll `0x460450(0)` (the routine of "stability below −3").
- Messages: T local human → 0x6a CLAIMTOUS; else, if S is not human and the local player knows T's
  capital → 0x6b CLAIMTOOTH (`0x577b80`). The royal marriage itself is **not** broken.

### 2.4 Warning (0x15) — exec `0x456310` on T
- S pays 100·B (expense 15, `0x45b140`).
- Record T→S +0x26 = today (the warning date). No relation change. T human → 0x6c WARNTOUS.
- Continent counter as in §2.2.
- **Active** while today − date < 240 months (`0x4cded0`: 20 years). The text DIPROL_WARNING says
  60 months: **NIEPEWNE**, the code says 240.
- **Effect** (WE_DECLARE_WAR `0x44b170`, `0x44b2b6..0x44b3be`): when T declares war on anyone, every
  country W ≠ T, ≠ the victim, not at war with T (record W→T +0x36 clear), with an active warning in
  record T→W, gets a **CB of 2 years** on T. There is no "neighbour of W" test, despite the text.

### 2.5 Guarantee (0x18) — exec `0x4563b0` on T
- Record S→T +0x2a = today (the guarantee date); S pays 100·B (`0x45b140`). T human → 0x6d GUARATOUS.
- Active while today − date < 240 months (`0x4cdf10`). Text DIPROL_GUARANTEE: five years
  (**NIEPEWNE**, code 240 months). While active, S→T drifts +0.08 a month (SPEC_WAR_DIPLOMACY §2.3).
- **Effect** (THEY_DECLARE_WAR `0x44b7a0`, `0x44ba17..0x44bb15`): when T is attacked, every G ≠ T,
  ≠ attacker, not at war with T, with an active guarantee in record G→T, gets a **CB of 6 months**
  on the attacker (`0x44ba9b`: month field 6).

### 2.6 Cancel trade agreement (0x1c) — exec `0x455c50` on T
- Clears +0x40 (trade agreement) in both records. **T gets a CB of 1 year** on S. Relation T↔S −20.
- **No stability change** (the manual's stability cost is not in the code).
- Messages: T human → 0x71 BRKTRADEUS; else (S not human, player knows T's capital) 0x72 BRKTRADEOTH.
- Accepting an offer (0x13c `0x455e20`) sets +0x40 both ways, messages 0x6e / 0x70; declining
  (0x13d) only messages 0x6f.

### 2.7 Military access
+0x41 in record X→Y = "X has military access in Y" (grant 0x13f `0x455f20` sets it in the asker's
record; messages 0x73 / 0x75; refusal 0x140 → 0x74).
- **Cancel** (0x1b, exec `0x456010`): record S→T +0x41 = 0; **S stability −1**; no CB.
  Messages 0x76 CANCMILUS to T / 0x77 to others.
- **Revoke** (0x1a, exec `0x4561a0`): record T→S +0x41 = 0; **S stability −1**; **T gets a CB of
  1 year** on S. Messages 0x78 REVMILUS to T / 0x79 to others.

### 2.8 Trade embargo (0x10) and allow trade (0xF) — exec on the sender E, T = +0x10
- Embargo `0x447c70`: record E→T +0x39 = 1 (so SPEC_WAR_DIPLOMACY §1.2 "+0x39 = embargo against us"
  is wrong: it is **E embargoes T**; `0x4cbf20` also counts E's global flag country+0xfda). Relation
  E↔T −25; relation E↔W −10 for every W ≠ E, T with the **same** religion as T (`0x431270` returns
  true when religions differ; Catholic and Counter-Reformed count as the same). AI hook `0x55aa00(0x11)`.
  Messages (`0x577ec0`): E human → 0x1e, else T is the player → 0x1c (NIEPEWNE keys
  OWNTRADEREFUSAL / TRADEREFUSAL).
- **No stability loss and no CB** are in the exec or host path. AWRTRADE promises "−1 stability
  unless at war" and "a CB until we allow trade": **NIEZNANE** where (if anywhere) they happen; the
  CB may be the CB test reading `0x4cbf60` (T embargoes us) — not traced.
- Allow trade `0x447e40`: relation E↔T **+15**; +0x39 cleared; messages 0x1f / 0x1d.

### 2.9 Leave alliance / break royal ties / cancel vassalization — EXEC_LEAVE_ALLIANCE `0x44a100`
Request 0xf2 carries the alliance-list object id, +0x16 = the leaver L and +0x1a = 1 when sent from the
UI (`0x441460` uses `0x4cba60` to find the marriage, `0x4414c0` uses `0x4cbae0` for the vassalage).
Host `0x4d4245`: object must exist with ≥ 2 members and pass `0x45ced0`; posts 0x106 to L through the
**first member's** vfunc +0x3a0 (`0x440e60`: USE_DIPLOMAT + send date for that member, then post),
and, if +0x1a = 1, a further 0x58 for L (NIEPEWNE: why the first member spends a diplomat).
Exec on L: L is removed from the member list; then by object type (+0x22):

| type | effects on L | others | message (NIEPEWNE key) |
|---|---|---|---|
| 1 royal marriage | VP −5 (`0x55f030(3, −5)`), stability −1 | — | 0x58 ROYMARBREAK |
| 2 vassalage | VP −5, stability −3; relation L↔W −50 for every W ≠ L, ≠ P of the same religion as P | P = the remaining first member (the vassal if the lord left, the lord if the vassal left) gets a **CB of 1 year** on L | 0x59 VASALLBREAK |
| 4 military alliance | stability −1, VP −5 | every remaining member gets a **CB of 1 year** on L (`0x443a70`) | 0x57 MILALLBREAK |

Each type also subtracts 100 / 150 / 45 (types 4 / 2 / 1) from the relation record selected by
payload +0x16 — which is L itself in every path traced, so **no relation change between the
partners happens** (NIEPEWNE; looks like an original bug — do not "fix" it silently).
Messages go to everyone who knows L's capital. Leader leaving: `0x4caa20` (not traced).

### 2.10 Ban from alliance (0xB) — exec `0x44a680` on the banned B, banner L = +0x16
Host: object exists, ≥ 2 members, `0x45cf60(object, B)`; posted through L's vfunc +0x3a0 (L spends the
diplomat). Exec: relation B↔L −50; message 0x4d MILALLBANOTHER; B removed; relation L↔M −10 for every
remaining member M ≠ L; then **every temporary CB of L on B with flag 1 is consumed** (`0x4cec40`).
**B gets no CB** (`0x441da0` is an empty stub), although AWBANALL / DIPROL_BAN1 say it does
(NIEPEWNE which to follow; the code is unambiguous).

### 2.11 Declare war (6) — additions to SPEC_WAR_DIPLOMACY §4
- Host `0x4d57d7` re-runs the enable test (vfunc +0x378) and posts 0x10f through the attacker's
  vfunc +0x3a0: **a war declaration costs a diplomat** and sets the one-month cooldown.
- Attacker `0x44b6f4` and defender `0x44b82e` both call `0x4cbfb0`: every alliance-list object that
  holds both of them gets a REQ_LEAVE (flag 0) from that side. The attacker's arrives first, so a
  shared **royal marriage breaks with VP −5 and stability −1 for the attacker** (§2.9 type 1); this is
  the "1 for breaking a Royal Marriage" of the AWDW popup that §4.2 could not find. A shared vassalage
  adds §2.9 type 2 (−3 stability, VP −5, the other side's 1-year CB) on top of §4.2's −3. NIEPEWNE:
  order of the two requests.
- §2.5 of SPEC_WAR_DIPLOMACY "−100 with every member of the target's alliance" is really: −100 with
  the target's **overlord**, if the target is a vassal (`0x44b4f1..0x44b55a`).
- Defender: stability becomes trunc(stability + 0.2) if the attacker's byte +0x11e is set, else
  trunc(stability + 0.1) (`0x44b7ff..0x44b81f`): a negative stability rises by 1, a positive one is
  unchanged (NIEPEWNE: depends on `0x5b98fc` truncating toward zero).
- Defender's overlord (if the defender is a vassal, `0x4cc150`/`0x4cc1b0`) gets a **CB of 1 year** on
  the attacker; guarantors §2.5; Defender of the Faith 3 months (SPEC_REVOLT_RELIGION); continent
  counter on the defender (`0x44b906`).

## 3. Enabled-when rules not in SPEC_UI §3a/3b

"Diplomat" = `0x45c060` (SPEC_UI). "Money" = treasury (+0xe70) ≥ the action's cost. "Not at war" =
record S→T +0x36 clear.

| Action | Rule (address) |
|---|---|
| Insult | money (10·B), not at war, diplomat (`0x45b2c0`) |
| Warning | money (100·B), not at war, T exists, T→S warning not active, diplomat (`0x45b320`) |
| Guarantee | money (100·B), not at war, S→T guarantee not active, diplomat (`0x45b3b0`) |
| Claim | money (250·B), not at war, royal marriage S→T, diplomat (`0x45b420`) |
| Offer trade | no agreement, T does not embargo S (`0x4cbf60`), S does not embargo T (`0x4cbf20`), diplomat (`0x45b4a0`) |
| Cancel trade | agreement, diplomat (`0x45b500`) |
| Ask access | no access yet, not at war, diplomat (`0x45b530`) |
| Revoke access | T has access in S, diplomat (`0x45b580`) |
| Cancel access | S has access in T and **no unit of S stands in a province owned by T** (unit list +0x124c, `0x48bd30`), diplomat (`0x45b5d0`) |
| Embargo | T ≠ S, not already, **S trade tech ≥ 4** (`0x432f10`), S+0xfda clear, last peace with T ≥ 61 months ago (truce, S+0x8ee), no trade agreement, diplomat (`0x45f620`) |
| Allow trade | embargo in force, S+0xfda clear, diplomat (`0x45f6e0`) |
| Leave alliance | S in a military alliance whose **leader is T**, alliance has no war (+0x1e list empty), S not its leader while +0x16 > 0, `0x43c000` rule (types 1/2 exempt), diplomat (`0x45cdc0`) |
| Ban | S is the leader, T a member, S has a CB on T (+0x3f or a temporary one), record +0x3d clear, diplomat (`0x45cf60`) |
| Declare war | T ∉ {0, 0x151, 0x152, 0x153}, not same alliance (+0x3c), **stability ≥ −2**, no access of S in T (+0x41), T ≠ S, not at war, record +0x38 clear, record +0x3d clear, diplomat (`0x45d520`) |

Host re-validation: only declare war, embargo/leave/ban (object checks) and the trade/access cases
that test +0x40 are re-checked; insult, claim, warning, guarantee and gifts only check that T exists.
The meanings of +0x38, +0x3d, +0xfda, +0x11e and `0x43c000` are NIEPEWNE.

## 4. Port checklist (`src/diplo.c`, `diplo_action_state` ~895, `diplo_action` ~939–1090)

1. **Gifts** (~1046): replace `cost·10/max(50, income)` with §2.1 (R100·(DIP+R3−1)·k·0.0033,
   k 100/50/10, independent of cost); credit the cost to T's treasury (income 7); reveal S's capital to
   T; RELCHANGE message. Use the same RNG order (R3 then R100).
2. **Insult** (~1084): add the money test to the enable rule (missing); otherwise matches (−50, 1-year CB).
3. **Claim** (~1074): the CB is **timed, 3 years** (`world_give_cb`), not `DF_CB_PERM`; −100 with every
   RM partner of S *including T*, symmetric; add T's revolt roll; −1 stability is right.
4. **Warning** (~1065): drop the invented −10 relation; store the date on T's side; duration 240 months
   (or 60 per the text — decide and log); implement the 2-year CB for the warner in the war
   declaration; the enable test looks at T's record of S.
5. **Guarantee** (~1071): duration 240 months (text: 5 years); add the 6-month CB on the attacker in
   the defender's war handler and the +0.08/month drift.
6. **Cancel trade** (~1018): remove the −1 stability; CB 1 year timed, not permanent; add relation −20;
   message BRKTRADEUS to T.
7. **Revoke access** (~1030): CB 1 year timed. **Cancel access**: add the "no own units in T" test.
8. **Embargo** (~1050): remove the stability loss and the permanent CB (not in the exec path, §2.8 —
   keep a NIEPEWNE note); add relation −25 and −10 with T's co-religionists; enable needs trade
   tech ≥ 4 and no trade agreement and a diplomat. **Allow trade**: +15 relation.
9. **Break royal ties / vassalage / leave** (~985, ~1000, ~1036): royal ties −1 ✓, VP −5 ✓.
   Cancel vassalization costs **−3 for whoever leaves** (the port gives the vassal −1) and the other
   side gets a **1-year** CB (not permanent); add the −50 co-religionist loop. Leave enable: target =
   the alliance leader, alliance not at war.
10. **Ban** (~1043): no CB for the banned; −50 B↔L, −10 L↔members; consume L's temporary CBs on B.
11. **Declare war** (~950): spend a diplomat (the port skips it, ~1089); shared royal marriage → leave
    effects; −100 with the target's overlord only; defender's overlord 1-year CB; stability rule
    "≥ −2" matches `> -3` ✓; add the +0x38 / +0x3d blocks once their meaning is known.
12. Costs of insult/claim/warning/guarantee are booked as expense 15 at exec time; the cooldown is
    one month per (sender, target) pair (`d->pcd[b] = today` is per target only — check it is per pair).
