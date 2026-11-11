# The diplomatic AI (eu2.exe v1.09) - specification

Read from the `eu2.exe` v1.09 disassembly as a specification only (no code copied). Markers:
**NIEPEWNE** = believed but not proven, **NIEZNANE** = not found. Implementation: `src/ai.c` (the
diplomat, scores, answers), `src/peace.c` (the AI and peace), `src/diplo.c` (wiring, player actions).

Contents:
1. Structure, helpers and the diplomat's month (working notes)
2. Answers to proposals
3. Peace: acceptance
4. Peace: when the AI offers and what it puts in the offer
5. Plans: gates, scheduling, pressure on the worst enemy

Port decisions (all NIEPEWNE unless stated): the AI's map knowledge is not saved, so "capital known"
means a neighbour or the same continent; grievances (country+0x14de) are not kept (taken as 0);
strength indices +0x364/+0x368 are tech + 2 MIL + troops / ships; the rank is by VP; ticks are taken
on days but at war at most once in 30 days (the exe: a fifth of the peace interval, 4-17 days);
proposals to the human player (marriage, alliance, vassal, annexation, access) are not sent until
there is a dialog for them; peace offers to the human use the existing peace dialog.

---

## 1. Structure, helpers and the diplomat's month

## AI decode notes (eu2.exe v1.09) - working notes, become docs/SPEC_AI.md

Evidence addresses refer to eu2.exe v1.09 (GOG 2.0.0.1).

### Structure
- country+0x25c = ai-prefs object (the .ai file), loaded by 0x55d7e0 (path "ai\%s", country+0x260 file name).
  prefs: +0x04 ferocity (byte), +0x0a tradingpost, +0x16 expansion, +0x1a traders, +0x1e monopoly, +0x22 war,
  +0x26 counterreform, +0x28 base (f), +0x2c front (f), +0x58 conquer block (0x55e7f0), +0x74 garrison (0x55e210),
  +0x9e region list, +0xb0 continent list, +0xc2 combat list (tags), area list, neighbour/enemies (int), flags.
- country+0x212e = AI controller; +0x0e = "is AI" flag; +0x204 + 4*idx = per-country record ("attitude") R:
  R+0x08 = other country index, R+0x14 = controller (R+0x14 -> +0xa = our country), R+0x10 state.
- Response handlers (country methods, check vfunc 0x3bc and ctrl+0xe first): 0x557030 offerloan (R.0x535da0),
  0x5571e0 annexation (0x536430), 0x557290 peace (forced 0x4ccda0, else 0x5366c0), 0x5573f0.. alliance
  (0x5372b0 / 0x537ae0 / 0x5377e0 by type), allow-join 0x536300, honour 0x535f40, 0x557100 (0x5380c0),
  0x557170 (0x537ed0).
- helpers: 0x43a230 stability, 0x43d2b0 trunc(war exhaustion +0x122a), 0x43c000 at war, 0x4cdf70 relation
  (table [C+0x38e] + 0xc + idx*0x36: C's relation toward idx), 0x43ce10(C) = C is the human player,
  0x43ce30(C) = table 0x64f870[C*0x144] > 0 (NIEZNANE), 0x43bf40(C) = int(ruler DIP + 0x42b6a0(C+0x37e)),
  0x438c00(C) = provinces of C held by tag index 0x153 (rebels), 0x4cabe0(side, C) membership,
  C+0x36c > 0.001 = has a coast, C+0x138a[idx] neighbour flag, C+0xe80 last month's income,
  C+0xed0 loan list (loan+0x1a amount), C+0xe74 ledger (+0x16c income cat 6 YTD = loan interest).
- rand: 0x5ba99c.

### Loan offer (0x535da0; A = amount, r = rate % / 100, m = months to due date; L = lender)
accept if r < 0.01; refuse if r > 0.25; refuse if A + sum(our loans) > 250*inc + treasury;
refuse if r*A > 12*inc; refuse if treasury > 24*inc; refuse if m*r*A > A; refuse if L ledger
(+0x16c + +0x1c) > 8 (NIEPEWNE: interest already earned by L); else accept.

### Honour alliance (0x535f40; A = ally asking, E = enemy)
gate byte [0x45b7d0()+0x256+us] (NIEPEWNE); rel(A->us) > 180 -> honour.
if E is neighbour of neither us nor A: refuse if E or we have no coast (+0x36c <= 0.001).
p = 80 - 10*stab(us) - 15*WE(us); p *= S_friend / S_enemy (sum of members' +0x368 + +0x364, the war's
sides; second war object adds the rest) ; p *= (1 + 0.25*rel(A->us)); x2 if A is human and flag(us);
+25 if rel > 170, +25 if > 160, +25 if > 150; honour if rand%100 < (int)p.

### Allow join (0x536300; R wants to join our alliance)
p = 50 + 0.25*rel(R->us); limit = 4 (+ [[0x8c0bfc]+0x2c6] when the human is not in it, single player);
members > limit: p -= 33*(members - limit); x2 human/flag; accept if rand%100 < p.

### Annexation offer to the vassal (0x536430; O = overlord)
refuse if rel(us->O) < -150. p = 5; stab<0 -> 10; stab<-1 -> +5; stab<-2 -> +10.
(dead store: clamp((100 - treasury + O.ledger+0x1c4)/100, +-10) + p - O.badboy, overwritten)
p += years of vassalage (vassal relation date +0x2a to today), then p -= 10; x2 human/flag.
refuse if O owns fewer provinces (+0x131a count) than us. p = p - ourProv/4 + OProv/8 (+0x1386).
if DIP(O) > p: p = DIP(us). accept if rand%100 < p.

### War predicate (0x540fc0, U = us, T = target)  (callers 0x53ea68.. = reaction to others' wars)
0x540ea0(T) must hold; no if WE(U) > 0; no if U at war; no if rel(U->T) > 140 + war/30;
no if U bankrupt within 5 years (+0xe9c); no if rebels(U) > min(4, provinces/5);
mode 1: no if T.badboy < 10; mode 2: no unless T religion == 7 ... (to finish)

### Diplomat think 0x53fd00 (this = AI minister, +0xa = U, +0x40 = worst enemy; monthly?)
1. If U not at war and option [[0x8c0bfc]+0x2ca] >= 4: for each valid country P (0x45bce0: idx != 0 and not
   0x14f..0x153) that is human (vfunc +0x20 false), P over the badboy limit (0x45af40), no truce U-P (0x45f5d0),
   P's capital known to U (prov+0x256+U), and (P is U's neighbour or (U+0x1f84 > 0 ships and P has coast and
   same continent of capitals prov+0x106)): war plan type 3 on P (stop).
2. else: worst enemy = 0x540310 (below); 0x53f4f0 (?); for each country i with embargo-ish 0x4cbf20 and no
   flag[i]+0 -> allow-trade plan 0x5391b0.
3. If at war: if 0x53f240 (peace logic) true -> done; else refuse-trade plan on every enemy (0x539360); 0x5409a0;
   0x53fb70.
4. Not at war, we are a vassal (0x4cc150): cancel-vassalization plan on overlord (0x538f10); 0x53f5f0;
   if war predicate 0x540fc0(enemy): war plan (type 3 if enemy in .ai combat list else 4).
5. Not at war, not a vassal: unless U index is 0x36 or 0x88 (0x43a9c0; NIEPEWNE tags): if royal marriages
   <= DIP/2: candidate 0x540460 -> royal marriage plan 0x5388b0; vassal candidate 0x540570 -> vassalization
   plan 0x538d90; for every own vassal that is a neighbour: annexation plan 0x538e40. Then 0x5409a0, 0x53f5f0,
   war predicate on enemy -> war plan (3/4).
6. Plans older than 300 days are cancelled (vfunc 0xc8), others updated (vfunc 0x1e8).
- worst enemy 0x540310: min over valid, capital-known countries of hostility score H (0x535810); mode 2:
  neighbour pagan (religion 7) -99; in .ai combat list: x5; only negative scores; logged "worst enemy".
- marriage candidate 0x540460: max of rel(U->i) (x2 if neighbour), -250 if Christian (rel 0..4) vs Muslim (5,6)
  either way; no existing royal marriage; > 0.
- vassal candidate 0x540570: only if 0x45bf60(U) (NIEPEWNE); max rel (x2 neighbour) > 0.
- alliance candidate 0x540600: valid, not in an alliance (0x4cc110), capital known; score A (0x535600, enemy);
  +100000 if it is our vassal; max > 0.
### Scores
- H (0x535810) friendliness of U toward X (low = enemy): s = 0.1*rel; -5 neighbour; +300 truce (0x45f5d0:
  U+0x8ee[X] date within 61 months); -4 if X+0x370 < U+0x370 (rank: X ranks higher); if U HAS a CB on X (permanent +0x3f, or the CB object's vfunc 8 false):
  s -= 5, s -= m per province in U's +0x1354 list (cores? NIEPEWNE) owned by X (m = 10 neighbour else 1),
  neighbour: s += prov(X) - prov(U). s -= 0.05*min(80, year - year(U+0xe3e)) + 20 (NIEPEWNE date);
  b = X.badboy, not neighbour: b *= 0.5 (dist U+0x1a2e[X] < 10), 1/3 (< 20), 0.25; s -= b;
  X human and b > 20: s -= 1000*b (neighbour) or 25*b (dist < 30); else s -= b again.
  +15 both flag 0x43ce30; mode 2: (different continents: Christian vs pagan -20 else -5, x2 if neighbour;
  same continent: s = 20 - b) / other modes: different continent and not neighbour +30; not neighbour:
  s += 4*dist; +100 allied, +30 royal marriage, +50 our vassal; + rand%5 - rand%5;
  H = s * (X.grievance[U] (+0x14de) + 1) * 0.5.
- A (0x535600, X candidate, E enemy): clamp(rel*(X.+0x364 + X.+0x368)*0.1, +-300); +30 X neighbour of U;
  +40 X neighbour of E; +100 both flag; U Christian and X not -> -200 (unless world flag 4, 0x4d16a0);
  E: *= (X.grievance[E] + 0.1); E neighbour of U and (U or X no coast) -> -1; X not neighbour of U nor E -> -300.
- badboy limit 0x45af40: limit = DIP + 30 + (year-1419)/20 (+ year-1790 after 1790); over when badboy > limit.
- 0x45bce0 valid tag index; relation objects list 0x8ff602 (+0x22 type 2 vassalage, 4 alliance; members +0x1a,
  first = overlord); wars list 0x8ff614 (sides +0x1e / +0x22); 0x4cbc00(U,X) = war between them.


---

## 2. Answers to proposals

## AI answers to diplomatic proposals (eu2.exe v1.09) - specification

Conventions as in `ai_notes.md`. U = the answering (AI) country = [R+0x14]+0xa; O = the proposing country
= countries[R+0x08]. rel(A->B) = 0x4cdf70 on A's record for B (A.+0x38e + 0xc + B*0x36). Flag offsets
(+0x36, +0x3b ...) are the SPEC_WAR_DIPLOMACY §1.2 convention (obj + other*0x36 + k).
rand = 0x5ba99c; "accept if rand%100 < X" with X truncated toward zero (0x5b98fc = ftol).
stab = 0x43a230 (float), DIP = 0x43bf40, flag(C) = 0x43ce30 (NIEZNANE: table 0x64f870[C] > 0),
human(C) = 0x43ce10, AI(C) = country vfunc +0x20 (true = AI controlled; per ai_notes, NIEPEWNE),
prov(C) = C.+0x1386, owned(C) = count of list C.+0x131a, cores(C) = count of set C.+0x1350,
Fs(C) = C.+0x364, Fn(C) = C.+0x368, F3(C) = C.+0x360 (three strength fields, NIEPEWNE land/naval/...),
cont(C) = continent (+0x106) of C's capital province (C.+0x1f7e via 0x45b7d0),
year = WORD [[0x647c2c]+0x16], dist(U,O) = U.+0x1a2e[O] (float, NIEPEWNE distance),
W = U's "worst enemy" = [[R+0x14]+0x50]+0x40 (the AI diplomat object, set by 0x540310).

### 0. How proposals reach the AI (evidence)

Country vtable = 0x5f55ec (ctor 0x556161). Event dispatcher = vfunc 0x5f5618 -> 0x461e90:
id = event vfunc +8; byte table 0x462e48[id-0x16] -> jump table 0x462b48 -> `call [vtbl+slot]`.
Decoded (id, slot, override):

| event | slot | handler | AI decision |
|---|---|---|---|
| 0x69 ASKEXTENDLOAN | 0x178 | 0x556d10 | inline (§6) |
| 0x6a OFFERLOAN | 0x17c | 0x557030 | 0x535da0 |
| 0x101 EXEC_OFFER_ANNEXATION | 0x180 | 0x5571e0 | 0x536430 |
| 0xec OFFER_PEACE | 0x18c | 0x557290 | 0x4ccda0 / 0x5366c0 |
| 0x107 EXEC_INVITE_ALLIANCE | 0x1a8 | **0x557400** | 0x5377e0 / 0x537ae0 / 0x5372b0 by type |
| 0x108 EXEC_ASKJOIN_ALLIANCE | 0x1ac | 0x557570 | 0x536300 |
| 0x13a ("newdiplo") | 0xfc | 0x557100 | **0x5380c0 = trade agreement** |
| 0x13e ("newdiplo") | 0x10c | 0x557170 | **0x537ed0 = military access** |

(The task's "0x5573f0" is the tail of the peace handler; the alliance handler starts at 0x557400.)

**Relation objects.** All three "alliance" kinds are one object class (list 0x8ff602, +0x22 = type,
+0x1a = member list, +0x16 = pending-offer count): created by 0x441140(type, target) -> CREATE_ALLIANCE
0xe3 (handler 0x44abf0 stores the type and an expiry date: type 1 -> 25, type 2 -> 350, other -> 10;
units NIEPEWNE years). Callers prove the types: 0x552030 posts type 1 then logs "sends a 'royalmarriage'",
0x552290 type 2 "sends a 'vassalization'", 0x552b60 type 4 "sends a 'offer-new-alliance'". The offer
then arrives as EXEC_INVITE_ALLIANCE 0x107; 0x557400 resolves the object (event+0x10 id) and switches on
+0x22: **1 -> 0x5377e0 royal marriage, 2 -> 0x537ae0 vassalisation, 4 -> 0x5372b0 military alliance**,
anything else -> decline. Accept -> 0x441960, decline -> 0x441a80, both logged with the same strings
"%s accepts/declines alliance with %s". (ALLIANCE_ADD/SUB_OFFER 0xe4/0xe5 only +-1 the pending count.)

**0x13a / 0x13e.** Answers: 0x13a accept -> 0x43e850 posts event 0x12f, decline -> 0x43e940 posts 0x130;
0x13e accept -> 0x43ec10 posts 0x132, decline -> 0x43eb20 posts 0x133. The base-class handlers of the
neighbouring ids show the relation: 0x13c (0x455e20) sets flag +0x40 both ways, message 0x6e; 0x13b
(0x455c50) clears +0x40 both ways, relation -20, messages 0x71/0x72; 0x13d message 0x6f; 0x13f (0x455f20)
sets flag +0x41, messages 0x73/0x75; 0x140 message 0x74; 0x141 clears +0x41, 0x76/0x77; 0x142 0x78/0x79.
Message-type table ("dip" records, 26 bytes, name under id+1 as in SPEC_ENVOYS): 0x6f ACTRAWITHUS,
0x70 DETRAWITHUS, 0x71 OTHERTRADEAG, 0x72 BRKTRADEUS, 0x73 BRKTRADEOTH, 0x74 ACCMILUS, 0x75 DECMILUS,
0x76 ACCMILOTHER, 0x77 CANCMILUS, 0x78 CANMILOTHE, 0x79 REVMILUS. So **+0x40 = trade agreement**
(matches SPEC_ENVOYS "DIPROL_OFFTRA"), **+0x41 = military access**; 0x13a = trade-agreement offer,
0x13e = military-access request. Both handlers check only ctrl+0xe (is AI), not vfunc 0x3bc.

### 1. Military alliance offer (0x5372b0, type 4; arg = id of relation object A)

p is a fraction; final test is rand%100 < trunc(100*p).
1. No if O does not exist; no if rel(U->O) < -100; no if A cannot be resolved.
2. p = 0.525 if same religion (0x431270 false: equal, or Catholic/Counter-Reformed pair), else 0.425.
3. If O is U's neighbour (U.+0x138a[O]): go to 6.
4. If AI(O): k = trunc((year - 1420) / 25) + 5.
   - If cont(U) != cont(O) and prov(O) < 200 and O.+0x1f84 < 5: **no**.
   - **No** if 0.02 * dist(U,O) > k (i.e. distance > 50k). Then go to 6.
5. Else (O human): if cont(U) != cont(O) and O.+0x1f84 < 5: p *= 0.5. If no country is a neighbour of
   both U and O: p *= 0.05.
6. If U has any tie with O (0x4cb900: royal marriage +0x3b, alliance +0x3c, flag +0x3d, or vassal
   either way): p *= 2.
7. If O is at war (0x43c000): p *= 0.5.
8. p *= 0.8 + 0.1 * (6 - stab(U)).
9. If W != 0 and W is a member of A (0x4cabe0): S_A = sum over A's members of Fn + Fs; S_W = Fn(W)+Fs(W),
   or, if W belongs to a military alliance (type-4 object, 0x4cc110/0x4cc0a0), the same sum over that
   alliance; p *= S_A / S_W.
10. limit = 4; if the human player country (0x8bfa50) is not in A and not multiplayer (0x4196c0 false):
    limit = option [[0x8c0bfc]+0x2c6] + 4. If members(A) > limit: p -= 50 * (members - limit).
11. If human(O) and flag(U): p *= 2.
12. If one of O/U is Christian (religion 0..4) and the other Muslim (5, 6): p *= 0.25; then if
    year < 1520: p = 0.05*p - 150; then if year < 1600: p *= 0.2.
13. If U is O's vassal (0x4cbe90 on O, U): p += 0.75.
14. p *= 1 + 0.0025 * rel(O->U).
15. Accept if rand%100 < trunc(100 p).

Open: step 9 only fires when the worst enemy is already in the offered alliance, then the ratio is
usually 1 (NIEPEWNE intent; reproduce literally). Meaning of +0x1f84 (compared with 5; SPEC_ENVOYS says
"non-zero = capital has a harbour") and of Fs/Fn: NIEPEWNE.

### 2. Royal marriage offer (0x5377e0, type 1)

1. No if O does not exist; no if U's tag index is 0x2e or 0x16 (NIEPEWNE: PAP and KNI if the index is
   the 1-based row of DB/country.csv); no if rel(U->O) < -150.
2. Base score: a temporary royal-marriage plan (0x78 bytes, vtable 0x5ecff0, owner U) is pointed at O
   (vfunc 0x1e4 = 0x551bc0) and scored (vfunc 0x1dc = 0x552090); p = score at plan(+0x40)+0x44
   (+0x40 is the plan itself unless the diplomat has a parent plan, NIEPEWNE). Score 0x552090:
   - s = 10 if U has no permanent CB on O (+0x3f) and the object at +0x10 of U's record for O answers
     true to its vfunc +8 (NIEZNANE; probably "no temporary CB"); else s = 0.
   - s += 10 if byte O.+0x11e == 0 (NIEZNANE); s += 20 if same religion (0x431270 false);
     s += 2 if O is not at war; s += 5 if U is O's neighbour.
   - for (f, w) in (Fs, 5), (Fn, 5), (F3, 10): r = f(U)/f(O); if r > 1: s += w*(r - 1).
   - s *= 0.25 * rel(U->O) + 0.5.
   - t = 5 * (number of O's royal marriages, flag +0x3b over its 340 records); t += 20 if religions differ.
   - score = s - t.
3. For every relation object: type 1 (royal marriage): -15 if O is a member, -5 if U is a member;
   type 2 (vassalage): -25 if O is a member, -25 if U is a member.
4. If human(O) and flag(U): p *= 2.
5. p *= 1 + 0.25 * rel(O->U).
6. Accept if rand%100 < trunc(p)  (no x100 here: p is in percent).

### 3. Vassalisation offer (0x537ae0, type 2; O offers to take U as its vassal, NIEPEWNE direction)

Integer p:
1. No if O does not exist.
2. p = 5*DIP(O) + trunc((owned(O) - owned(U)) / 4)   (C division, toward zero).
3. p += 20 if U is O's neighbour, else p -= 10.
4. p -= 5 * owned(U).
5. If owned(U) > cores(U): p -= 10.
6. If owned(O) < owned(U): p -= 85; else if AI(O) and AI(U): p += 50.
7. If flag(U) and not flag(O): p -= 30.
8. p = trunc(p - 5*stab(U)); p = trunc(p + 5*stab(O)).
9. No if U already has any vassal (0x4cbe90(U, X) true for any X).
10. Accept if rand%100 < p.

### 4. Military access request (0x537ed0, event 0x13e; O asks U)

1. No if O does not exist; **no** if U's flag +0x36 for O is set (NIEPEWNE: U embargoes O).
2. r = rel(U->O).
   - r > 150: p = r - 150.
   - else if trunc(prov(O) / 2) > prov(U) or U is O's neighbour: base = 1; if 0.5*Fs(O) > Fs(U):
     base = 26, and if r < -150: base = 36; p = base + (r - 50) / 3.
   - else p = 0.
3. If O is at war: p -= 15.
4. If U is in a military alliance and O has flag +0x36 toward any member of it (0x45d000): p -= 100.
5. Accept if rand%100 < trunc(100 p)  (so p >= 1 always accepts).

Accept posts 0x132 -> 0x13f (flag +0x41, ACCMILUS), decline 0x133 -> 0x140 (DECMILUS).

### 5. Trade agreement offer (0x5380c0, event 0x13a; O asks U)

Integer p:
1. p = 100 - U.aiprefs(+0x25c).+0x1e (monopoly preference).
2. If rel(U->O) >= -150: x = trunc((rel(U->O) + 100) * 0.25); if rand%100 < x: p += 25.
3. If 0x43a9e0(U): p += 25. (True if fantasy mode [0x8d0d7a], or U owns a CoT (+0xfc6 > 0), or
   domestic slider #3 (0x42bbf0(U,3), mercantilism<->free trade) < 4, or the merchants-per-year policy
   term 0x42b8c0 > 0.)
4. **No** if U.+0xe68 > 0 (trade-embargo count, SPEC_TIME_ECONOMY); **no** if U's flag +0x36 for O.
5. p -= 10 for every country that has a trade agreement (+0x40) with U.
6. Accept if rand%100 < p.

Accept posts 0x12f -> 0x13c (flag +0x40 both ways, ACTRAWITHUS); decline 0x130 -> 0x13d (DETRAWITHUS).
Note the handler tail 0x455c00 (shared, likely linker-folded) increments U.+0xe4e when cont(U) == 1 and
cont(O) != 1 (NIEPEWNE side effect, reproduce).

### 6. Loan due (0x556d10, event 0x69 ASKEXTENDLOAN, received by the **borrower** U)

Evidence: the loan L = 0x439a50(U, id) is in U's own loan list; L+0x16 interest (int), L+0x1a amount
(int), L+0x22 lender reference (dword +0x24 = lender index). Outcomes: 0x440cf0 posts REQ_CANCELLOAN
0x62, 0x4409f0 posts REQ_PAYLOAN 0x63 (0x440b70 = REQ_EXTENDLOAN 0x64 is not used here). The human dialog
(0x522de0) shows the same second button as EXTEND for a bank loan (null lender) and CANCEL for a loan
from a country (BEXTENDLOAN / BCANCELLOAN), so CANCEL = renounce the debt to a country.
No AI check in this handler (NIEPEWNE: human path elsewhere).
1. If the lender reference is null (bank loan, 0x457180 vs 0x64d4b4): p = -1 (never cancel).
2. Else p = 95; if the lender country does not exist: keep 95. Otherwise:
   - if trade agreement with the lender (U flag +0x40): p = 90;
   - p -= 80 if the lender is U's vassal; p -= 80 if U is the lender's vassal;
   - rel(lender->U) > 170: p -= 50; else > 120: p -= 30; else > 70: p -= 10;
   - p -= 25 if allied (+0x3c); p -= 10 if royal marriage (+0x3b);
   - p += 5 * interest.
3. If rand%100 < p: REQ_CANCELLOAN.
4. Else: if amount > U.treasury (+0xe70): post GAINMONEY (0x5f) to U for amount + 2 (category 7) -
   the AI is given the money; then REQ_PAYLOAN.

Open: what EXEC_CANCELLOAN does to the lender/relations (not traced); units of "interest" (L+0x16,
probably percent).


---

## 3. Peace: acceptance

## AI: accept a peace offer (eu2.exe v1.09, `0x5366c0`)

Specification in prose only (no code copied). NIEPEWNE = guess, NIEZNANE = unknown.

Call path: country handler `0x557290` (AI countries only: vfunc 0x3bc and controller+0x0e).
1. It looks up the offer (6-byte id). It writes into P+0x16 the id of the war between us and the
   sender (`0x4cbb60` on our relation table).
2. **Forced acceptance** `0x4ccda0` comes first (log string 0x633560). It applies only when the
   offer is neither an annexation nor a conversion (+0x3f5e = 0 and +0x3f5f = 0):
   - demand: accept if `P+0x24 − P+0x28 ≥ 50`;
   - offer: accept if `P+0x28 + P+0x24 ≥ 50`.
3. Otherwise R = our AI record for the sender, and the result is `R.0x5366c0(id)` (log "%s accepts
   peace with %s" 0x633544, refusal 0x633528).

"us" = the AI country deciding (the recipient, R+0x14→+0x0a). "sender" = R+0x08.

### 1. Peace-offer object P (0x3F6C bytes)

| field | meaning | evidence |
|---|---|---|
| +0x16 | war id (6 bytes); the receiver overwrites it with the us–sender war | `0x5572e8..0x557308`, used `0x5367ad` |
| +0x1c | sender country index; 0 in the window = "alliance leader", replaced by the side's first member | `0x511a76..0x511ae5` |
| +0x20 | recipient country index (same rule) | `0x511a03..0x511a6f` |
| +0x24 | **war score from the sender's view** (−100..100, the window's meter value) | `0x511b04` |
| +0x28 | **offer value** (int) = Σ over listed provinces of 2·warvalue(`0x47ca60`), doubled again (4·wv) if the province is not occupied (owner = controller); + 0.04·money; +50 religion; +20 access; +70 vassal (all rounded) | `0x4ccde0` |
| +0x2c | number of listed provinces (max 0x7E4) | |
| +0x34[i] | province ids | |
| +0x1fc4[i] | receiver of province i; 0 → filled at send: demand → the sender, offer → +0x20 | `0x511b1e..0x511b51` |
| +0x3f54 | money (float, steps of 25) | |
| +0x3f58 | **mode: 1 = demand** (the sender takes from the recipient), 0 = offer / white peace | `0x511af8` |
| +0x3f59 / +0x3f5a | separate peace for the sender / the recipient. 1 = only this country, 0 = with the whole alliance via its leader. Cleared when that side has ≤ 1 member; +0x3f5e forces +0x3f5a = 1 | `0x511a2b`, `0x442775..0x442824` |
| +0x3f5b | "**better offer**" (PEACEBETTER). Set at posting when sender score > 40 and the offer is either an offer, or a demand with value < score. If it is refused: stability hit, `0x55f030(−25)`, revolt roll +2 for the refuser | `0x442837..0x44285a`, `0x44eeaf..0x44ef02` |
| +0x3f5c / +0x3f5d / +0x3f5f | demand vassalisation / military access / religious conversion. +0x3f5c is cleared at posting when the recipient is already in a type-2 group (`0x4cc150`) | `0x511d39..0x511d7f`, `0x442787..0x4427ad` |
| +0x3f5e | **annex / convert demand** (page-2 button of the window; it also sets demand = 1) | `0x511d01..0x511d08` |
| +0x3f60 / +0x3f64 / +0x3f68 | retry limit / ... / refusal counter (+0x3f68 is incremented on a refusal) | `0x44eea9..0x44ef1d` |

AI record R:
- +0x00 = best value seen so far in this pair's offers. The value is signed: + for an offer, − for a
  demand.
- +0x04 = highest sender war score seen so far.
- +0x0c = a state enum; values 1 and 3 matter here (meaning NIEZNANE).

Controller +0x40 = AI mode: 0 DOMINANCE, 1 BUILDUP, 2 COLONISE (`0x542466..0x5424b2`).

### 2. Acceptance rule (in order)

Values used throughout:

| name | meaning |
|---|---|
| S | −P+0x24 = **our** war score |
| V | P+0x28 |
| n | P+0x2c |
| T | months since the war started (W+0x16; day ignored) × (trunc(WE(us)/5) + 1) |
| M | months since the last combat in the war (W+0x1a) |
| occ | count of our owned provinces (us+0x131e) with controller ≠ owner |
| A | **occupation share of our income** |
| C | count of listed provinces that are our cores (us+0x1350 lookup) |
| OS | our side of the war: W+0x1e if we are a member (`0x4cabe0`), else W+0x22 |
| SAS(sender) | "sender at least as strong": sender+0x364 ≥ us+0x364, and, if us+0x36c + sender+0x36c > 1, also sender+0x368 ≥ us+0x368 |

How A is computed:
- Every owned province adds inc(p) = `0x47d970` (the province "income value") to Total.
- An occupied province adds inc(p) to A.
- An unoccupied province under siege (`0x482dc0`) adds 0.5·inc(p) to A.
- A = A / Total when Total > 0.

Hard gates (every failure = refuse):

| step | rule |
|---|---|
| 1 | The sender exists, the offer exists, the war exists. |
| 2 | `0x539580(P)` is false (the AI-goal veto, §3). |
| 3 | If prefs+0x04 = 1: refuse when both we and the sender still hold at least one full city (`0x438c40` > 0). |
| 4 | V ≤ 100. |
| 5 | Vassal check. If we are a non-leader member of a type-2 group (`0x4cc150`), and our relation entry toward its leader L has the vassal byte (+0x2a) set, then L's vfunc +0x20 must be true (NIEPEWNE: "L is AI/local"). |
| 6 | If A = 0, n = 0 and +0x3f5a: refuse when T < 120. |
| 7 | Alliance check. If OS's leader is not us, and we alone do not occupy every full city of the sender (`0x4cfd50(us,OS,sender)` false), then `0x4cfec0(W,us)` must be false. That function is true when some enemy member is fully occupied by our side while we hold part of it but not all. |
| 8 | COLONISE mode (+0x40 = 2) with T > 40 and n > 0: an offer is **accepted**; a demand goes to step 10. |
| 9 | Any other demand goes to step 10. For an offer, every listed province must pass `0x53a7f0(receiver, prov)`; then go to step 11. |

**Step 10. They demand from us (+0x3f58 = 1).** Refuse if S ≥ −1. Then:

Annexation (+0x3f5e):
- If we have no controlled provinces, no units (+0x1248) and fewer than 2 owned: p = 0.99.
- Otherwise:
  - x = min(0.66, 0.05·owned + 0.1·controlled + 0.005·treasury);
  - p = 1 − x + 0.01·sender.owned + 0.05·M;
  - if a us–sender war exists (`0x4cbc00`) and OS has more than 2 members: p = p / members.
- Go to FINAL.

Other demands:
1. Accept outright if A ≥ 1 and we have no units.
2. t = 1 − V/|S| − 0.2·C.
3. If V ≤ |S|: t += 0.05·M; else t ×= 0.5.
4. t ×= (0.05·occ + A + 0.5).
5. ×0.5 if V > 90; ×0.05 if V ≥ 100; ×0.25 if R+0x0c = 3.
6. p = t + 0.01·(|S| − V).
7. If S/3 > V: p ×= 2. This never happens because S < −1 and V ≥ 0.
8. If V < 1, T < 60 and S > −10: p ×= 0.05.
9. Go to FINAL.

**Step 11. Offer or white peace (+0x3f58 = 0).**

White peace (V < 1):
1. Accept if M > 12 and none of our full cities is held by a non-rebel occupier. "Full city" means
   `0x485490` false; tag 0x153 is the rebels.
2. Refuse if T < 24 and A < 0.01.
3. Accept iff SAS(sender).

V ≥ 1 and S < 0 (we are losing):
1. Accept if SAS(sender), or if M ≥ 2.
2. Otherwise accept iff |S| > rand()%10.

V ≥ 1 and S ≥ 0 (we are winning):
1. t = 0.01·V if V > S. Otherwise t = V/150 + 0.01·T − 0.25, minus a further 0.25 if not SAS.
2. If SAS: t += 0.5.
3. p = t + 0.05·M.
4. If R+0x0c = 3: p ×= 0.25; p = 0 if n < 1; if T < 24, cap p at 0.02.
5. p = 0 if S > 99 and n < 1.
6. Go to FINAL.

**FINAL** (p from step 10 or 11):
1. If R+0x0c = 1:
   - if T < 12: p −= (12 − T)/15;
   - if controlled(us+0x133e) ≥ owned(us+0x131a): p −= 0.5.
2. Refuse if A < 0.001, OS's leader ≠ us and n = 0.
3. If `0x539880(W)` (we are doing well overall) and this is a demand: p ×= 0.1.
4. v = V, negated for a demand.
5. Unless this is an annexation:
   - refuse if not +0x3f5b, v < R+0x00, P+0x24 ≥ R+0x04, M < 4 and A < 0.66;
   - then R+0x04 = max(R+0x04, P+0x24) and R+0x00 = max(R+0x00, v).
6. If +0x3f5b: p ×= 3.
7. p += 0.1 · rebels(us)/owned(us), using `0x438c00` / us+0x131a count.
8. **Accept iff rand()%100 < trunc(100·p).**

### 3. Sub-calls

| addr | meaning |
|---|---|
| `0x414560` | id → object (+8). Used for the offer, the war and other ids |
| `0x417440` | months between today and a date. Result {d, m, y}; the code uses y·12 + m |
| `0x539580(P)` | AI-goal veto, true = refuse (details below) |
| `0x438c40(C)` | count of full cities (city present and not a colony) in C's controlled list (+0x133e/+0x1342) |
| `0x4cc150` / `0x4cc1b0` (rel table) | our country is a non-leader member of a type-2 group (global list `0x8ff602`, group+0x22 = 2); `0x4cc1b0` returns that group's leader (NIEPEWNE: overlord–vassal group) |
| `0x47d970(p)` | province income value, the number shown as "Income" in the province window |
| `0x482dc0(p)` | province under siege; `0x482da0(id)` = province pointer |
| `0x485490(p)` | true if the province has no trading post and either no city or a colony |
| `0x4cabe0(side,c)` | c is a member of that war side / alliance |
| `0x4cfd50(c,side,X)` | every full city owned by X is controlled by c (or by any side member when c = 0), and X controls no full city (total occupation) |
| `0x4cfec0(W,c)` | some member X of the opposing side has its land partly held by c (not all of it), and is totally occupied by c's side (`0x4cff20`) |
| `0x53a7f0(C,p)` | p may go to C (details below) |
| `0x4cbc00(rel,X)` | the war object containing us and X on opposite sides, or 0 |
| `0x539880(W)` | "we are winning" (details below) |
| `0x5396f0(a,b)` | ratio bucket of a/b: 1 if b = 0; >4 → 7, >3 → 6, >2 → 5, >1.7 → 4, >1.5 → 3, >1.25 → 2, >1 → 1, >0.8 → 0, >0.66 → −1, >0.58 → −2, >0.5 → −3, >0.33 → −4, >0.25 → −5, >0.2 → −6, else −7 |
| `0x438c00(C)` | C's provinces held by the rebels (tag 0x153) |
| `0x5ba99c` / `0x5b98fc` | rand() / ftol (truncate) |

Details:
- **`0x539580(P)`, AI-goal veto.** Applies when our country +0x1016 (AI goal type) = 1, with goal
  province list +0x1008. "We gain" means demand mode with P+0x1c = us, or offer mode with
  P+0x20 = us.
  - Refuse if we are not gaining and a listed province is one of our own goal provinces.
  - Refuse if we are gaining and the list omits a goal province that the sender owns and we control.
  - With goal type 8 and any goal province owned by the sender: no veto.
- **`0x53a7f0(C,p)`, p may go to C.** True when any of these holds:
  - C owns a neighbour of p (list p+0x11c, count p+0x114, stride 12);
  - p's culture is accepted by C (`0x429fb0` on C+0x382) and p's religion (`0x47ca10`) = C's state
    religion (+0x254);
  - C+0x1f84 > 0 (NIEPEWNE: has ports or a navy), and either `0x485490(p)`, or p is coastal (+0x487)
    and C's navy (`0x48f2d0`: 2·a + b + c ship counts, NIEPEWNE) > 0.1 × distance(C capital
    +0x1f7e, p). The distance comes from `0x43a820`: Euclidean distance with x wrapping at 0x4a00,
    divided by 100.
- **`0x539880(W)`, "we are winning".** The sum of these must be > 0:
  - war score `0x4d0800(us, us|0, sender|0)`, where a side leader passes 0 (= whole alliance);
  - bucket(our unit strength / sender's), where strength = Σ of unit vfuncs +0x7c and +0x80
    (`0x48f1b0` + `0x48f1e0`);
  - bucket(our yearly income `0x45b940` = 12·+0xe80 / sender's);
  - bucket(controlled/owned for us) − bucket(controlled/owned for the sender);
  - bucket(our manpower +0xec0 / sender's).

Country fields used: +0x364 / +0x368 = monthly power indices (computed at `0x43c41e` / `0x43c445`;
NIEPEWNE: land / naval-colonial power); +0x36c coast share; +0x1386; +0xe70 treasury; +0x131a owned,
+0x133e controlled, +0x1248 units, +0x1350 cores.

### 4. Open points

- R+0x0c (values 1 and 3) is NIEZNANE. It is reset by `0x535510`; the writers were not found.
- How R+0x00 / R+0x04 are reset between wars is NIEZNANE.
- Country vfunc +0x20 (step 5) is NIEPEWNE. It is called with no arguments and tested as a boolean;
  elsewhere it comes right before the controller's AI flag.
- prefs+0x04 = 1 (step 3): is it really "ferocity", or a "never make peace while holding land" flag?
- +0x364 / +0x368 / +0x1f84 / unit vfuncs +0x7c/+0x80 and the navy formula `0x48f2d0`: exact
  meanings are NIEPEWNE.
- Division by zero: owned(us) = 0 in FINAL step 7, and S = 0 cannot happen in step 10 (S < −1 is
  required there). x87 returns inf/NaN; a reimplementation should guard owned = 0 (NIEPEWNE: the
  original then gives inf and accepts).
- Step 10 item 7 (S/3 > V → ×2) is unreachable as written; it is kept for fidelity.


---

## 4. Peace: offering

## AI: decide to offer peace and build the offer (eu2.exe v1.09)

Specification in prose only (no code copied). NIEPEWNE = guess, NIEZNANE = unknown.
Companion of `ai_peace.md` (acceptance `0x5366c0`, offer object P, sub-calls `0x539580`, `0x539880`,
`0x53a7f0`, `0x5396f0`, `0x4cfd50`, `0x4cfec0`). Those are only summarised here; my independent reading
of each of them agrees with that file.

Names: R = our AI record about the other country (controller+0x204+4*idx); U = us (R+0x14 -> +0x0a);
T = the other country (R+0x08); W = the war between U and T (`0x4cbc00` on U's relation table).
"ws" = war score `0x4d0800(W, U, U|0, T|0)` (a side leader passes 0 = whole alliance), U's view.
mAW = months U has been continuously at war (see U+0x38a below). prov(C) = C's owned list +0x131a,
ctl(C) = C's controlled list +0x133e (all provinces whose controller +0x3c8 is C, foreign ones included;
proven by the set-controller functions `0x4814c0` / `0x481590`), counts via list vfunc +4.

### 0. New facts found on the way

| item | meaning | evidence |
|---|---|---|
| country+0x38a | date: **last day U was at peace**. Every update (`0x450ad0`, called from `0x55c14a`) writes today into +0x38a if U's relation-table byte +0x47cc ("at war with anyone") is 0, else into +0xe3e. So "months since +0x38a" = length of the current state of war, and +0xe3e = last day at war (H score's "years since last war") | `0x450b2b..0x450b55` |
| relation table +0x47c4 / +0x47cc | owner country pointer / "at war" byte | `0x4cc178`, `0x450b31` |
| relation record byte at table + 0x36 + X*0x36 (= record X +0x2a) | tested before every war-only action toward X (`0x5382f0`, `0x45d620`, `0x45cd00`, `0x45fda0`, `0x53f2c5`, the plan's execute `0x551f41`) and switches the `0x535520` interval from 12 to 1 month. NIEPEWNE: **"at war with X"** (`ai_peace.md` step 5 calls it the vassal byte) | |
| war+0x1a | date of the last combat (SPEC §5.1). The at-war caller uses *this*, not the war start | `0x53f30a` |
| war+0x16 | war start date; war+0x0c (6 bytes) = war id copied into P+0x16 | `0x539f1a`, `0x53a566` |
| country+0xe5c | number of owned non-colony city provinces (SPEC_EVENTS `countrysize`) | |
| country+0xec0 / +0xe70 | manpower / treasury (SPEC_TIME_ECONOMY) | |
| prov+0x41c / +0x487 / +0x114,+0x11c / +0xd8 / +0xf6 | port (NIEPEWNE) / coastal / neighbour count, list (12-byte stride) / culture / religion | |
| `0x47ca10(p)` | province religion, but religions 0 and 1 count as 3 until world flags 1 / 2 are set (`0x4d16a0`). NIEPEWNE ids: 3 catholic, 2 counter-reformed, 0 protestant, 1 reformed, 4 orthodox, 5/6 muslim, 7 pagan | |
| `0x431270(a,b)` | religions differ; 2 and 3 count as equal | |
| `0x482940(p,C)` | p is a core of C (C+0x1350 set) | |
| `0x47f3e0(p)` | trunc(city+0x3a) (fortress level, NIEPEWNE); 0 without city | |
| `0x43a820(a,b)` | distance between provinces /100 (x wraps at 0x4a00) | |
| `0x45b7d0(C)` | C's capital province (C+0x1f7e) | |
| `0x45c060(X)` (on U) | diplomat may go to X: U+0xe98 (diplomats) >= 0.99 **unless U+0xe56 is set**; X valid (not 0, not 0x14f..0x153), exists, X's capital known to U (prov+0x256+U), X != U, per-target date U+0x39e+4X not "today" (`0x417220`, NIEPEWNE: one envoy per target per month) | |
| `0x419700()` | byte [[0x8ff63c]+0x2766]; true blocks AI peace offers. NIEZNANE (game-state flag) | |
| `0x4196c0()` | [0x89ae44] != 0 and [[0x89ae48]+0x12f0] > 1. NIEPEWNE: multiplayer with >1 player | |

### 1. Gate `0x535520` (R): offer timing

- false if T does not exist.
- n = 1 month if the "at war" byte toward T is set, else 12 months. t0 = R+0x18 (date; NIEPEWNE: last
  diplomatic action toward T, probably written by controller+0x12 vfunc 0x114 after a send, `0x551fd0`).
- today < t0 + n -> false; today >= t0 + 2n -> true; in between -> rand()%100 < 50.

### 2. `0x539a40` (R): "T is almost fully occupied"

True iff count(prov(T)) > 0 and count(ctl(T)) / count(prov(T)) < 0.26. (ctl counts every province T
controls, foreign ones included.) The at-war caller then goes straight to `0x5382f0`, bypassing its
random roll.

### 3. `0x5382f0` (R): start an OfferPeace plan. Returns true = plan created.

Gates, in order (any failure -> false):
1. With U+0xe56 = 1 (skip diplomat stock), `U.0x45d620(T)` = "U may send a peace offer to T now":
   at-war byte toward T, war W exists, `0x45dd30` -> `0x45d6b0` finds no blocking record for this war
   and these sides (NIEPEWNE: pending / recently refused peace offer, DIPROL_PEACE*) and `0x45c060`
   accepts the target (T, or the leader of T's side). U+0xe56 is cleared again.
2. U's "at war" byte toward T is set; and gate 1 was true.
3. `R.0x535520()` true (§1).
4. `0x419700()` false.
5. If U is a vassal (`0x4cc150`; overlord O = `0x4cc1b0`) and T's "at war" byte toward O is set:
   O's vfunc +0x20 must be true (NIEPEWNE: O is not human; a human overlord makes the peace).
6. W = `0x4cbc00(T)` exists.

Score `base` (integer):
- ws as above. base = 25 * |ws|.
- If ws < 0 and we are the stronger one (U+0x364 > T+0x364, or (U+0x36c + T+0x36c > 1 and
  T+0x368 < U+0x368)) and mAW < 6: base = 0 (a stronger loser waits six months).
- OurSide = W+0x1e if U is a member of it (attacker side), else W+0x22. If U is on W+0x1e:
  base -= 5 * max(0, 32 - mAW).
- a = sum of +0x364 over OurSide members, b = same for the other side (`0x411290`); each min 1.
  base = trunc(base + 10 * max(a,b) / min(a,b)) (so always at least +10; imbalance either way
  raises the urge to settle).
- If ws > 0 and count(prov(T)) > 0: base += trunc(50 * (prov(T) - ctl(T)) / prov(T)).
- If OurSide's leader (first member) is not U:
  base -= trunc(0.5 * rel(U->leader) + 50); then if not `0x4cfd50(U, OurSide, T)` (U alone has not
  totally occupied T) and `0x4cfec0(W, U)` -> false (a member does not run off while it holds part of
  an enemy that its alliance has fully occupied, NIEPEWNE wording).
- If the other side's leader is not T: base -= 20.
- r = rebels(U) / count(prov(U)) (`0x438c00`); base += trunc(10 * r). (prov(U) = 0 -> x87 inf;
  guard it.)
- If `R.0x539880(W)` ("we are winning", see ai_peace.md) and ws <= 0 and r < 0.25 -> false.
- If prefs+0x04 = 1 and both U and T still control >= 1 full city (`0x438c40` > 0) -> false
  (that AI fights until one side holds nothing).
- rand()%100 < base, else false.

Then: P = `R.0x539e10(W)` (§4); null -> false. `R.0x539580(P)` (AI-goal veto) true -> delete P, false.
Otherwise allocate the plan (0x84 bytes, ctor `0x54b4b0(controller)`, vtable `0x5ed3d4`, +0x74/78/7c
= 0), vfunc 0x1f0(controller+0x50, T, P) (stores T at +0x70, P at +0x80), vfunc 0x1dc, add it to
controller+0x50 (vfunc 0x100). Return true.

**Execution** (`0x551f10`, plan vtable `0x5ed3d4`): if the plan is active (+0x0e), T exists and the
at-war byte is still set: delete the stored P, build a **fresh** P = `R.0x539e10(W)` (no veto this
time), null -> cancel the plan (vfunc 0xc8); else log "%s sends a 'OfferPeace' to %s", post it
`U.0x442560(P)`, +0x74 = 0, controller+0x12 vfunc 0x114(T), finish `0x5584b0`. If T is gone or
not at war: cancel. (Plans older than 300 days are cancelled by the diplomat think.)

### 4. `0x539e10` (R, W): build the peace offer P (0x3F6C bytes) or return null

Field map (agrees with ai_peace.md §1): +0x16 war id (6 bytes, W+0x0c), +0x1c sender = U, +0x20
receiver = T, +0x24 ws, +0x28 value, +0x2c province count, +0x30 = 0, +0x34[i] province ids,
+0x1fc4[i] who receives province i, +0x3f54 money (float), +0x3f58 demand, +0x3f59 U separate,
+0x3f5a T separate, +0x3f5b better-offer (set at posting, not here), +0x3f5c vassalise, +0x3f5d
military access, +0x3f5e annex, +0x3f5f convert religion, +0x3f60..68 = 0. vtables 0x5e7a98 /
0x5e7a84 (+8) / 0x5e7a7c (+0x12).

Steps:
1. P+0x24 = ws. **Demand** P+0x3f58 = 1 if `R.0x539880(W)` or ws > -10; otherwise 0 (we concede).
2. +0x3f59 = 1 if U is not the first member of either side; +0x3f5a = 1 if T is not. If prefs+0x04
   != 0: +0x3f5a = 1 (deal with T alone).
3. **Annex**: if `U.0x45cd00(T)` and `R.0x539d10(T)`: +0x3f5e = 1, return P at once (no provinces,
   value not computed, i.e. +0x28 = 0).
   - `0x45cd00(T)`: world option [[0x8c0bfc]+0x2d6] != 0, T exists, at-war byte, (T+0xe5c <= 1 or T
     is pagan (7)), U controls T's capital, ws(U vs T only) >= 100.
   - `0x539d10(T)`: true if R+0x0c = 3. p = 100 (50 if T has flag `0x43ce30` and T is Christian
     0..4). True if T is a vassal (`0x4cc150` on T). For each province of T: true at once if it is a
     core of U; p -= 10 if its religion differs (`0x431270`), p -= 10 if its culture is not in U's
     set (U+0x382). Annex iff rand()%100 < p.
4. Parties. Demand: receiver Pr = U, giver Pg = T; offer: Pr = T, Pg = U. Each party = {its side in
   W, battle matrix, separate flag (+0x3f59 for U, +0x3f5a for T), country}.
5. **Candidates** `0x4cca90(Pr, Pg)`: every province p owned by the giver country itself (not its
   allies), skipping p whose city has +0x2c set (NIEPEWNE: capital), that is
   (a) controlled by the receiver (separate) or by any member of the receiver's side, or
   (b) a core of the receiver that the giver still controls itself, or rebels (0x153), or the receiver;
   and that is in neither P's list nor any other pending offer (global list 0x8ff626).
6. **Score** each candidate: holder H = `0x4ccd30(Pr, p)` (the controlling receiver-side country, or
   the receiver when p is still held by its owner / rebels), s = `R.0x539ab0(W, p, H, !demand)`.
   Keep p if s > 0.01; list sorted by s, highest first.
   `0x539ab0` (F = 1 when conceding):
   - -1 if not `0x53a7f0(H, p)` (H could not hold p).
   - v = econ(p) (`0x47d970`) + 5*fort(p) + 5 if p+0x41c; F: v = 100 - v; v = min(v, 100).
   - a = 25 if p is owned by U, is U's core and we demand (taking a core back); else 15 if p is owned
     by U, is H's core and F; else -150 if p has a non-colony city and H does not control it; else 0.
   - a += 1 if p is coastal; otherwise +10 if a neighbour of p is owned by H, else -25.
   - d = max(10, distance(p, H's capital)).
   - s = v + a - d + (whole years since W+0x16).
   - F and p's owner is not U: s -= 0.5 * (rel(U->owner) + 50).
   - s = max(s, 1).
7. If demand, no candidate scored and ws > 10: every candidate is added with score 5.
8. **Budget** b = |ws|; if b > 10: b = rand() % (b/4) + trunc(90*b/100) (0.9|ws| .. 1.15|ws|).
   Conceding: b = b - trunc(b/5). Then b = max(b, R+0x00) (R+0x00 = best value T has offered us,
   see ai_peace.md). Spend B = b; but if ws < 0 and demand: B = 1 + rand()%3 (token demand).
9. Extras (demand only):
   - 10 %: if `U.0x45fda0(T)` and R+0x0c != 3: +0x3f5f = 1 (P+0x28 = 50 provisionally); B -= 50
     when set. `0x45fda0`: at-war byte, ws >= 50, religions differ (`0x431270`), then T pagan -> yes;
     either side religion 4 -> no; both 0..3 -> yes; both 5/6 -> yes; else no.
   - if (number of scored candidates) < B/10 and rand()%100 < 20 and T is U's neighbour
     (U+0x138a[T]) and R+0x0c != 3 and B > 20: B -= 20, +0x3f5d = 1 (military access).
   - if T is not a vassal, R+0x0c != 3, U controls T's capital and B > 70: B -= 70, +0x3f5c = 1.
10. **Provinces**: skipped if B <= 3, or if conceding and B <= 20. Otherwise walk the sorted list while
    B > 0: append p (holder in +0x1fc4), B -= 2*wv(p), 4*wv(p) if owner = controller (wv =
    `0x47ca60`). The last province may overshoot.
11. **Money**: only if the walk ended with B > 0 (or provinces were skipped): money = min(25*B, cap)
    in +0x3f54. cap `0x4cc9f0(Pg)`: separate giver: max(0, floor(treasury)); whole side: member
    count * min over members of min(5000, floor(treasury)), floored at 0.
12. **Value** `0x4ccde0`: +0x28 = sum(2*wv, x2 if unoccupied) + trunc(0.04*money) + 50 (+3f5f)
    + 20 (+3f5d) + 70 (+3f5c). **No clamp to 100** here, and none seen in `0x442560` (it only copies
    fields); values above 100 can occur (budget up to 1.15|ws| or R+0x00, last-province overshoot)
    and are then refused by acceptance step 4 (V <= 100). NIEPEWNE whether the posting UI path clamps.
13. **Drops** (delete P, return null), with m = months since W+0x16:
    - demand with no province and ctl(U) > trunc(75*prov(U)/100);
    - m' = m * (trunc(WE(U)/5) + 1); not `0x4196c0()`, T is the human player ([0x8bfa50]), m' < 60,
      option [[0x8c0bfc]+0x2ca] >= 4 (NIEPEWNE: difficulty) and the human is over the badboy limit
      (`0x45af40`), and (b = 0 or no province) -> no cheap peace for a badboy human;
    - R+0x0c = 3 and `R.0x539880(W)`: T+0xe5c > 1 and no province -> drop; T+0xe5c = 1 and m < 48
      and not annex -> drop.
14. Return P.

### 5. At-war caller `0x53f240` (confirmation)

For every country X with the at-war byte set and a war W: base = mAW; -18 if U is on W+0x1e
(attacker); m = months since **W+0x1a (last combat, not the war start)**, if m > 3: base += 5m;
base += |prov(U) - ctl(U)| + |ctl(X) - prov(X)|; if ctl(U) = 0: own > 1 province -> call `0x5382f0`
at once and base = 0, else base += 50; base += 4*WE(U); +10 if stability < -2, +10 if < -1. Then if
`0x539a40` (X controls < 26 % of its count) -> `0x5382f0`, else rand()%100 < base -> `0x5382f0`;
the first plan created returns true, else the loop goes on (base restarts at mAW for each X). This
matches the given summary except the 5*m term (months since last combat).

### 6. Open points

- The relation byte record+0x2a: "at war with X" (my reading, from six war-only call sites) vs
  "vassal" (ai_peace.md). One writer search would settle it.
- `0x45d6b0` (blocking peace record via `0x460410` / `0x4cbc80` / `0x4cbd10` / `0x4cbdf0`) not decoded.
- R+0x18 writer, R+0x0c values (1, 3), `0x419700` flag, `0x43ce30` flag, world option +0x2d6
  (annexation allowed?) and +0x2ca: NIEZNANE.
- city+0x2c (candidate excluded) assumed capital: NIEPEWNE.
- prov+0x41c meaning (port vs not land-connected) NIEPEWNE; it only adds +5 to the score.
- Units of `0x43a820` distance (map pixels / 100) and of econ(p) decide how much distance matters
  in §4 step 6; take them from the map/economy specs.


---

## 5. Plans

## AI diplomatic plans (eu2.exe v1.09) - specification

Conventions as in `ai_notes.md` / `ai_answers.md`. U = the acting AI country, R = U's record for the
other country (R+0x08 = its index), O = countries[R+0x08]. The diplomat object D = [R+0x14]+0x50
(the task's "King"; class vtable 0x5f088c, think = slot 0x1c = 0x53fd00, add-plan = slot 0x100 =
0x53e870). rel(A->B) = 0x4cdf70. "flag +k" = byte A.dip + B*0x36 + k. rand = 0x5ba99c, "%" = C
remainder, trunc = toward zero. annualIncome(C) = 0x45b940 = 12 x C.+0xe80 (last month's income).
merchants/yr = 0x458b50 (SPEC_ENVOYS). badboy = C.+0xe42. prov(C) = C.+0x1386.
Dates are {day byte, month byte, year word}, 30-day months, 12-month years (0x619f24/0x619f28);
0x417440 = difference, 0x417270 = ">=", 0x4171a0 = add n months.

**Flag +0x36 = "at war with".** It is the DIPWAR menu condition (SPEC_UI: "not at war (table+0x36
== 0)"), and every use below fits: no war plan if set, 1-month instead of 12-month cooldown, +999
to embargo an enemy, invite blocked if any member has it toward the invitee. (SPEC_WAR_DIPLOMACY
§1.2 still lists it as "NIEPEWNE: embargo"; this is the better reading, but still NIEPEWNE.)

### 0. Plan object and scheduling

Layout (base ctor 0x54b4b0; trade plans add 0x53aa80 on top, same fields):
+0x0a owner country U; +0x0f "cancelled" byte, +0x10 "finished" byte; +0x12 parent agent (D);
+0x3c AI controller; +0x40 root plan (itself; a sub-plan points at its parent's root);
+0x44 score (float, 0 at construction); +0x48 start date; +0x4c..+0x6c zero (+0x4c, +0x58 are
"keep waiting" pointers, nothing in this family sets them, NIEPEWNE); +0x70 target index;
+0x74 float (0); +0x78/+0x7a type-specific (war type; alliance id (word+dword); money needed);
+0x7c money granted (0x80-byte plans only).

Virtual methods used: 0x1e4 = set target (0x551bc0: root, parent D, +0x70 = target),
0x1dc = compute score into +0x44, 0x1f0 = extra parameter, 0xc0 = start (records +0x48 = today and
posts a resource request to the controller: vfunc 0x1cc(plan, 1.0); the money plans also post
vfunc 0x1c8(plan, cost+1)), 0x1e8 = update, 0x1ec = can-execute, 0xc4 = execute, 0xc8 = cancel.

Creation (every creation function): allocate, construct, set target, score, D.addPlan.

**D.addPlan (0x53e870).** Walk D's plan list (+0x16 list object, head +0x1a, tail +0x1e) in order:
- if the entry has the same plan type (vfunc 0x1c8 type id) and the new plan's score < 50 ->
  delete the new plan, stop;
- else if the entry's score < the new score -> insert the new plan before it, stop.
At the end, append. (A score > 0.001 with an empty list appends directly - same result.) Then the
plan is started (vfunc 0xc0). So the list is sorted by score, highest first.

**Per think (end of 0x53fd00, 0x5400f1..0x5401c2).** For each plan in D's list:
age = (today - start) as years*360 + months*30 + days; age > 300 -> cancel; else update.
Then entries marked done are unlinked, and while the list holds more than 3 plans the tail (lowest
score) is removed, cancelled and deleted. Plan creation (steps 1-5 of the think) happens earlier in
the same call, so a new plan is normally updated - and executed - in the same think.

**Standard update (0x551d90; all plans in this file except the money plans of §14).**
1. Target country gone -> cancel.
2. If +0x74 <= 1.05 (literally (+0x74 - 1) <= 0.05; +0x74 starts at 0, so this is always true for
   these plans): can-execute ? execute : cancel.
3. Else: +0x58 != 0 -> wait, else cancel.
NIEPEWNE: the intent was probably "|+0x74 - 1| < 0.05" (diplomat granted, see 0x53aab0 which adds
to +0x74 and re-updates); the shipped code executes at once. The real resource gate is in
can-execute: the menu-rule functions include the diplomat check 0x45c060 (>= 1 diplomat, contact,
"diplomat en route" cooldown; SPEC_UI).

**Execute epilogue (all plans).** +0x74 = 0; D.vfunc 0x114(target) = 0x53e960 sets R(target).+0x18
= today ("last diplomatic action", feeds §1); finish 0x5584b0 (+0x10 = 1, D removes and deletes it).
Cancel 0x551c80 = 0x5584e0 (+0x0f = 1, D removes and deletes it).

**Score.** Only matters for (a) the war plan's can-execute (> 0.001), (b) addPlan's same-type
"< 50" discard, (c) the 3-plan trim. Base score 0x54b4f0 = 1.0.

### 1. Cooldown 0x535520 (R method; "may we act toward O now?")

- false if O does not exist;
- I = 1 month if U is at war with O (flag +0x36), else 12 months;
- L = R+0x18 (date of the last executed plan toward O, §0);
- today < L + I -> false; today >= L + 2I -> true; otherwise true with 50 % (rand%100 < 50).

### 2. War plan (create 0x538200(R, type), vt 0x5ed1e0, size 0x7c)

Gates: not at war with O (flag +0x36 clear); global byte [0x8d0d79] == 0 (NIEZNANE game/option
flag). No cooldown. Score: vfunc 0x1f0 = 0x551e80 sets +0x44 = 250 and +0x78 = type (3 when O is
in the .ai combat list or a badboy human, 4 otherwise - see ai_notes). Start (0x551d20) also sets
controller.+0x3c = target ("current war target"); cancel (0x551d60) clears it.
Can-execute 0x551ea0: U vfunc 0x378(O) (= 0x45d520, the DIPWAR menu rule) AND root score > 0.001.
Execute 0x551e00: R(O).+0x0c = +0x78 (war type remembered on the record); 0x4418b0(U, O) posts the
declare-war request (the human DIPWAR path, event 0xfb); log "%s sends a 'declarewar' to %s";
controller.+0x3c = 0. Port: own "declare war" action.

### 3. Royal marriage (create 0x5388b0, vt 0x5ecff0, size 0x78)

Gates, in order: no royal marriage with O (flag +0x3b clear); U's tag index != 0x2e and != 0x16
(NIEPEWNE PAP/KNI, same test as the answer side); cooldown §1; t = 50, then for every relation
object of type 1 (royal marriage, list 0x8ff602): t -= 15 if O is a member, t -= 5 if U is a
member; create only if rand%100 < t. Score = 0x552090 (decoded in ai_answers §2 step 2).
Can-execute 0x552280 = 0x45d1e0(U, O) (DIPOFFDYN rule). Execute 0x552030: 0x441140(1, O)
(CREATE_ALLIANCE 0xe3 type 1 + invite; answered by 0x5377e0); log 'royalmarriage'.
Caller: diplomat think step 5 (candidate 0x540460, only while U's marriages <= DIP/2) and 0x53eedf.

### 4. Invite to existing alliance (create 0x538a10, vt 0x5ed5c8, size 0x7e)

Gate: cooldown only. Score 0x5529b0: +0x78 = id of U's military alliance (0x4cc0a0), score 1.0.
Can-execute 0x5529f0: the alliance resolves, O exists, alliance has >= 1 member, O may join it
(0x45d390: not already in a type-4 alliance, not a member, 0x471460 NIEZNANE, leader valid and
existing, if O is a vassal the leader must be its overlord, O knows the leader's capital, O is not
the leader, ...rest not traced) and 0x45f8d0(U, O): not at war with O, diplomat check, U is the
alliance leader, no member at war with O, diplomat check again.
Execute 0x552940: log 'invite-to-existing-alliance'; 0x441340(U, alliance id, O) (the human
Invite path; answered by 0x557400/0x5372b0).
Caller 0x5409a0 (alliance manager, below): only when U leads its alliance and members <
option[[0x8c0bfc]+0x2c6] + 4; O = alliance candidate 0x540600; created if rand%100 <
trunc(A-score 0x535600(O, worst enemy)).

### 5. Leave alliance (create 0x538ac0, vt 0x5ed7bc, size 0x7e)

Gates: if O is AI controlled (vfunc +0x20 true): rel(U->O) <= -150 required (human O: no relation
test here); then create only if rand%10 <= DIP(O) - literally O's (the leader's) DIP, NIEPEWNE
intent (U's). No cooldown. Score 1.0 (0x552390).
Can-execute 0x5528f0: alliance id from +0x78 must resolve and 0x45ced0(U, alliance, 0): U a member;
not at war unless the object type is 1/2; the object's +0x1e list empty; not (U is leader and the
pending-offer count +0x16 > 0).
**Defect: nothing writes +0x78 for this plan** (the creator never calls vfunc 0x1f0 = 0x53ab10,
and its score function does not fill it), so the id is null, can-execute is false and the plan is
cancelled on its first update: the AI never leaves an alliance through this path (NIEPEWNE but the
code path is unambiguous). Execute 0x552880 would be: 0x441520(U, alliance) (leave) and log
'leave-alliance'.
Caller 0x5409a0: U is in an alliance, not its leader, rel(U->leader) < -100, O = leader.

### 6. Offer new alliance (create 0x538be0, vt 0x5ed9b0, size 0x78)

Gate: cooldown only. Score 1.0 (0x552bc0). Can-execute 0x552be0 = 0x45bd00(U, O) (DIPCNALL rule).
Execute 0x552b60: log 'offer-new-alliance'; 0x441140(4, O) (create type-4 object + invite;
answered by 0x5372b0).
Caller 0x5409a0: U in no alliance; O = candidate 0x540600 (must exist); t = owned(U) (+0x131a
count) + 15, then +40 if byte U.+0x11e set else -10 (NIEZNANE flag); created if rand%100 < t.

### 7. Ask to join their alliance (create 0x538c90, vt 0x5edba0, size 0x7e)

Gates: cooldown; O is in a military alliance (0x4cc0a0 on O resolves); that alliance has <= 5
members. Score 0x552ae0: +0x78 = O's alliance id, score 1.0. Can-execute 0x552b30: alliance
resolves and 0x45d390(U, alliance) (as in §4). Execute 0x552a70: log 'ask-to-join-their-alliance';
0x441210(U, alliance id) (the human Join path; answered by 0x557570/0x536300).
Caller 0x5409a0, when U is in no alliance and the new-alliance roll (§6) failed: only if rand%100
<= [0x8ff60a] + 10 (NIEPEWNE: relation-object count); then over all type-4 alliances accepted by
0x4cab70 (NIEZNANE) with 1 <= members < option+4, best = max A-score 0x535600(leader, worst enemy)
with start value 20 (so > 20 needed); O = that leader; created if rand%100 < trunc(best).
(The same function, when U is in an alliance, calls 0x540720(member) with 4 % per other member -
NIEZNANE, not a plan of this list.)

### 8. Vassalization (create 0x538d90, vt 0x5edd94, size 0x78)

Gate: cooldown only (candidate choice 0x540570 is in the think). Score 0x5522f0 = 0.25 x
O.ledger(+0xe74).+0x1cc (NIEZNANE ledger figure). Can-execute 0x552320 = 0x45bf60(U, O)
(DIPOFFVAS rule). Execute 0x552290: 0x441140(2, O) (answered by 0x537ae0); log 'vassalization'.

### 9. Annexation (create 0x538e40, vt 0x5edf84, size 0x78)

Gates: cooldown; prov(O) <= 250. Score 1.0. Can-execute 0x552410 = 0x45bdc0(U, O) (DIPANNEX rule).
Execute 0x5523b0: log 'annexation'; 0x441040(U, O) (the human Annex request; answered by
0x5571e0/0x536430). Caller: think step 5, for every own vassal that is a neighbour.

### 10. Cancel vassalization (create 0x538f10, vt 0x5ee174, size 0x78; U is O's vassal)

Gates, in order:
1. cooldown;
2. p = trunc((200 - trunc(rel(U->O))) / 5); if the vassalage object containing U and O exists
   (0x4cbae0), p -= 2 x full years since its date (+0x2a); stop if rand%100 > p;
3. stop if allied with O (flag +0x3c);
4. stop if rand%10 < DIP(O); stop if rand%10 > DIP(U)   (two separate rolls);
5. stop if full years since U.+0x8ee[O] (date also used by the truce test 0x45f5d0; NIEPEWNE "last
   peace with O") < 3 x DIP(O) + 10;
6. if O is U's neighbour (U.+0x138a[O]): stop if S(U) < S(O)/3 with S(U) = sum of U's land units
   (0x48eec0 over +0x1248, vfunc 0x8c per unit) + U.+0xec0 (manpower pool) and S(O) = sum of O's
   units + O.+0xec4 (yearly manpower rate - literally a different field).
Score 1.0. Can-execute 0x5523a0 = 0x45b180(U, O) (DIPCANVAS rule). Execute 0x552330: 0x4414c0(U, O)
(find the vassalage object, leave it); log 'cancel-vassalization'. Caller: think step 4.

### 11. Allow trade = lift embargo (create 0x5391b0, vt 0x5ee364)

Gates: O exists; cooldown; p = 100 - U.aiprefs.+0x1e (monopoly);
if rel(U->O) >= -150 and rand%100 < trunc((rel + 100) x 0.25): p = 2p, else p = trunc(p/2);
if O is human (vfunc +0x20 false): p = trunc(p/2); stop if merchants/yr(O) > 3;
create if rand%1000 < p.
Score 0x5527a0 = B - A (below). Can-execute 0x5527d0 = U vfunc 0x380(O) (DIPATRADE rule).
Execute 0x552740: 0x443cd0(U, O) (human Allow Trade); log 'allowtrade'.
Caller: think step 2, every country flagged by 0x4cbf20 (embargo, see ai_notes).

Trade scores (O = target):
- A = 0x552420 ("reasons to embargo"): 10 if U has no tie with O (0x43b740 = 0x4cb900: marriage,
  alliance, +0x3d, vassal either way) else 0; +2 if U has a CB on O (flag +0x3f, or the temporary-CB
  object says so); +10 if at war with O; if F3(O) != 0 and r = F3(U)/F3(O) > 1: +10(r-1)
  (F3 = +0x360); for every centre of trade (list 0x647d40) in a province owned by U where O has a
  presence object (+0x28[O], +0x04 = its merchant count, NIEPEWNE): +15 if count > 5, +count if
  count > 3; +100 x 0x43b610(O) (O's trade efficiency); d = U.+0x20de - O.+0x20de (NIEZNANE):
  +0.25 d if d > 0; + badboy(O); then if at war with O: +20 if O is AI, +250 if human.
- B = 0x552620 ("reasons to trade"): 2 if royal marriage; +5 if 0x4cbe90(U, O) (vassal tie);
  +3 if allied; if F3(U) != 0 and r = F3(U)/F3(O) > 1: +10(r-1); for every CoT in U's provinces:
  +5 if O has no presence object there, +2 if O has fewer than 4 merchants there.

### 12. Refuse trade = embargo (create 0x539360, vt 0x5ee554)

Gates: cooldown; stop if U.+0xe6c > 3 (one of the two embargo counters, SPEC_TIME_ECONOMY);
p = U.aiprefs.+0x1e (monopoly).
- At war with O (flag +0x36): p += 999, skip to the final roll.
- Otherwise: if rel(U->O) >= -150 and rand%100 < trunc((rel + 100) x 0.25): p = trunc(p/2);
  if 0x43a9e0(U) ("trade minded", ai_answers §5): p = trunc(p/2); stop if stab(U) < 2; stop if
  merchants/yr(O) < 2; stop if O.+0xe80 (monthly income) < 10.
- Final: create if rand%1000 < trunc(25 x badboy(O) + p).
Score 0x552840 = A - B. Can-execute 0x552870 = U vfunc 0x37c(O) (DIPRTRADE rule).
Execute 0x5527e0: 0x443be0(U, O) (embargo request); log 'refusetrade'.
Callers: think step 3 (every enemy while at war) and 0x53f1b5 (peace logic).

### 13. Economic aid (0x53f4f0 -> 0x521ee0)

For each valid country i (0x45bce0): x = trunc(rel(U->i) - 120), skip unless x > 0; skip unless
annualIncome(U) > 100; x = max(x, 25); skip unless i is at war (0x43c000); skip unless
annualIncome(U) x x / 100 > annualIncome(i); then call 0x521ee0 on R(i). **0x521ee0 is a bare
`ret`** (an empty, linker-folded function with ten unrelated callers): the economic-aid plan
(string _CAI_PLAN_SEND_ECONOMIC_AID_) was compiled out of v1.09. The AI never sends aid; the port
can skip this loop (only its rand-free tests run, nothing changes).

### 14. Casus-belli / pressure plans for the worst enemy (0x53f5f0)

E = worst enemy (D+0x40); nothing if none. controller.+0x3c = E.
1. If U has military access through E (flag +0x41, 0x4cb880): create a **cancel-access** plan on E
   (vt 0x5f1414; score 295; can 0x45b5d0 = DIPCACCESS rule; exec 0x553090 -> 0x43edf0, human Cancel
   Access), add it, return.
2. If U has no CB on E (flag +0x3f clear and the temporary-CB object of R(E) returns true): build
   candidates on E and keep the best (strictly highest score, must be > 0; ties keep the earlier):
   - **insult** (vt 0x5f1224; exec 0x552c50 -> 0x43e2b0; can 0x45b2c0): score 0.2 x rel(U->E) if
     rel >= 20 else -1; -1 if allied; -1 if prov(E) >= trunc(prov(U)/2); -1 if U has no land unit;
   - only with a royal marriage to E: **claim throne** (vt 0x5f1034; exec 0x552da0 -> 0x43e580;
     can 0x45b420): 25 if annualIncome(U) >= 500 else -1; -1 if prov(E) >= trunc(prov(U)/2); -1 if
     0x4cb8c0(U, E) (NIEPEWNE: U guarantees E); -1 if allied;
   - **warning** (vt 0x5f0e44; exec 0x5531f0 -> 0x43e3a0; can 0x45b320): 2 x badboy(E) + 10; -1 if
     prov(E) >= trunc(prov(U)/2); -1 if U has any CB on E.
3. If U is not a vassal: for every valid country P with known capital, P != E, P != U, P borders E
   (P.+0x138a[E]) and more than 36 months since R(P).+0x18 (last action toward P):
   - if trunc(prov(U)/3) > prov(P) and annualIncome(U) > 500: **guarantee** P (vt 0x5f0c54; exec
     0x553150 -> 0x43e490; can 0x45b3b0): score 25; -1 if prov(P) >= trunc(prov(U)/2); -1 if
     0x4cb8c0(U, P) (already guaranteed, NIEPEWNE);
   - if E is not U's neighbour and P is: **ask military access** from P (vt 0x5f0a64; exec 0x552ff0
     -> 0x43ea30; can 0x45b530): score 35; -1 if prov(P) >= trunc(prov(U)/2); -1 if U already has
     access (flag +0x41).
   Each competes for "best" with the step-2 winner.
4. Add the best candidate (if any) to D.
Insult/claim/warning/guarantee are "money plans": start (0x552bf0 / 0x552d40 / 0x5530f0) sets
+0x78 = cost + 1 with cost = 10/250/100/100 x B (0x45b100/0x45b120/0x45b140; SPEC_UI costs) and
requests it from the controller (vfunc 0x1c8, an event, 0x44a100). Their update 0x551ca0:
if +0x78 - +0x7c > 0.05 (money not yet granted): wait if +0x4c != 0, else cancel; otherwise as the
standard update (§0 step 1-2). Whether the grant arrives before the first update is NIEZNANE
(the request is an event); port: execute when the treasury can pay cost + 1, else drop the plan.
Cancel-access and ask-access use the standard update.

### 15. Port mapping summary

| plan | gate (creation) | can-execute = menu rule | effect |
|---|---|---|---|
| war | not at war, [0x8d0d79]==0 | DIPWAR 0x45d520 + score>0 | declare war |
| royal marriage | no marriage, tag, cooldown, 50-15/-5 roll | DIPOFFDYN 0x45d1e0 | marriage offer (type 1) |
| invite | cooldown | join rules 0x45d390 + 0x45f8d0 | invite to U's alliance |
| leave | AI leader: rel<=-150; rand%10<=DIP(leader) | 0x45ced0 (never true, id unset) | none (defect) |
| new alliance | cooldown | DIPCNALL 0x45bd00 | alliance offer (type 4) |
| ask to join | cooldown, O allied, <=5 members | 0x45d390 | join request |
| vassalization | cooldown | DIPOFFVAS 0x45bf60 | vassal offer (type 2) |
| annexation | cooldown, prov(O)<=250 | DIPANNEX 0x45bdc0 | annex request |
| cancel vassal | §10 list | DIPCANVAS 0x45b180 | leave vassalage |
| allow trade | §11 roll /1000 | DIPATRADE vfunc 0x380 | lift embargo |
| refuse trade | §12 roll /1000 | DIPRTRADE vfunc 0x37c | embargo |
| economic aid | §13 | - | nothing (stub) |
| §14 plans | best score | 0x45b5d0/2c0/420/320/3b0/530 | cancel access, insult, claim, warning, guarantee, ask access |

Open: when 0x53fd00 runs (NIEZNANE, probably monthly); 0x540720; 0x4cab70; 0x471460; U.+0x11e;
U.+0x20de; ledger +0x1cc; the temporary-CB object (R+0x10, vfunc 8) semantics.


---

