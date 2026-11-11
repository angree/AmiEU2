# EU2 v1.09 tutorial mode flag: what changes when `tutorial = yes`

Read out of `eu2.exe` (objdump `/home/angree/full.txt`, dumps `C:\temp\amiga_eu2\sr\tut_*.txt`,
scripts `C:\temp\amiga_eu2\re\agent_tut\`). Prose and tables only. Addresses are evidence.
**NIEPEWNE** = inferred; **NIEZNANE** = not found. Companion of `SPEC_TUTORIAL.md` (the script
engine); this file covers only the global flag and every place that tests it.

---

## 1. The flag

| Item | Fact | Evidence |
|---|---|---|
| Getter | `0x419700` returns byte `[[0x8ff63c]+0x2766]` (the loaded scenario header). `ecx` is loaded with `[0x647d4c]` at every call site but ignored | `0x419700..0x41970b` |
| Keyword | Header parser `0x4a6c4f`: the token compare chain sends token **0x17F `tutorial`** to `0x4a6f4c` (`cmp …,0x17f; je 0x4a6f4c` at `0x4a6de8`). The case stores (value token == **0x4C `yes`**) into +0x2766. Any other value (`no`, a number) stores 0 | `0x4a6de8`, `0x4a6f4c..0x4a6f61`, `tokens.txt` |
| Default | Cleared to 0 by the header constructors (`0x4a15d8`, `0x5b1721`) and at the start of every header parse (`0x4a6c79`). So only a scenario whose header says `tutorial = yes` sets it; the 9 `Tutorial\ChapterN.eug` files do | |
| Header write-back | The header writer `0x4a76ee` copies +0x2766 into a boolean node (`0x4a79cc`), so a saved tutorial game keeps the flag. Key name of that node NIEPEWNE (`tutorial`) | `0x4a79cc..0x4a79fd` |
| Same token, other meaning | In the game-state part of a save, token 0x17F carries the tutorial **step number** (`tutorial = N`, §2 row 13); this is not the header flag | `0x4a34e6` |

The flag is **constant for the whole game**. No caller writes it. Earlier specs called it a
debug/cheat/"no revolts" flag (SPEC_HRE §2.1, SPEC_NATIVES §4.3, SPEC_REVOLT_RELIGION §2,
SPEC_TIME_ECONOMY §8.2, SPEC_AI): it is the tutorial flag.

## 2. Every call site (25)

"Player" = the local player's object `[0x8bfa50]` (in a tutorial it is the tutorial subclass,
ctor `0x567770`, SPEC_TUTORIAL §6). "Host" = `0x4196e0()`.

| # | Call | Function (game terms) | Effect when the flag is set |
|---|---|---|---|
| 1 | `0x43b830` | Country object factory `0x43b790`, case class type 1 (local human, table `0x91a738`; jump table `0x43bbb8`) | The player is built as the **tutorial subclass** (`0x567770`, 0x6754 bytes) instead of the normal player class `0x524840` (0x66F0 bytes), then the script named at header +0x2666 is parsed (`0x568890`) |
| 2 | `0x43cfeb` | Yearly manpower rate `0x43ce50` (end) | For the player's country only: ec4 (yearly manpower rate) is multiplied by **12** after the vassal sum. The monthly accrual is ec4/12 × factor, so the player gets roughly a year's manpower every month, capped at 2·ec4 |
| 3 | `0x43f0aa` | Colonist send `0x43efc0` | The failure roll is skipped: the travel record keeps success = 1 (set at `0x43f07e`), so the colonist **always succeeds** and the native-attack roll on failure (event 0xD0) never happens. Travel time and cost are unchanged |
| 4 | `0x43f322` | Trader send `0x43f230` | Same as row 3: always succeeds, no native attack. Success byte set at `0x43f318` |
| 5 | `0x43f6bc` | Merchant send `0x43f4b0` | After the normal roll (chance clamped 1..99, `0x43f65d..0x43f68a`) and the 6-merchants rule, success flag rec+0x20 is **forced to 1**. Every merchant leaves pre-rolled as a success |
| 6 | `0x4471f0` | Merchant arrival, contested branch `0x446ae0` (EXEC_MERCHANT_SUCCESS 0xA9) | When a competitor j exists and the second roll (chance2, SPEC_ENVOYS §3) fails, the loss (0xA9 re-post with flag 0 → MERCFAIL) is skipped and the code takes the **win** path (event 0x9C against j). The contest is always won. The non-contested failures (full CoT, someone holds 6) still apply |
| 7 | `0x450952` | Country initialisation `0x4505d0` (tail) | Skips: (a) the initial mission offers (host, tracked country +0x11E, dynamic missions option `[*0x8c0bfc+0x2DA]`: one offer for each of the 11 types without one, `0x4361d0`); (b) the clamp of the first record of the three lists at +0x2110/+0x2116/+0x211C to at most "current year + 20" (lists NIEZNANE). The manpower recompute after it still runs |
| 8 | `0x451680` | Country monthly `0x451300`, mission block | For the local player with dynamic missions on, skips the whole block: no automatic pick of the best offer when no mission is current (+0x210C; random scores, message 4 MISSNEW, `0x436350`), and invalid offers (+0x2122 list, test vfunc +0x24) are not replaced. Note: the code gates this on `0x434d90` (local player), not on AI as SPEC_MISSIONS §1.4 says (NIEPEWNE which is right) |
| 9 | `0x45dfb8` | "May disband army A" `0x45dfb0(A)` | Returns **false**. Callers: the unit window disables button 0x10 DISBAND (`0x4f9f0d`), the right-click map menu greys `RCDM_DISBAND` (`0x52aa9e`). The player cannot disband in any chapter |
| 10 | `0x460478` | Monthly revolt roll `0x460450(C, bonus)` | Returns at once for every country: **no revolts** and no low-stability uprising, including the immediate rolls (stability < −3, bankruptcy, war taxes, religion change …) |
| 11 | `0x475858` | Map keyboard handler `0x4757f0` (message 0x15, scroll keys 1..4) | After setting the ±10000 scroll offset, increments the player's scroll counter +0x6718 (for WaitForScroll) and returns |
| 12 | `0x475adf` | Map edge-scroll tick `0x475910` | After scrolling, increments +0x6718 by 1 per frame |
| 13 | `0x4a3455` | Save-game writer `0x4a2ff0` | Adds a node `tutorial = <player+0x671C>` (token 0x17F, current step number) to the save |
| 14 | `0x51f953` | Button state refresh `0x51f940` (shared by 5 button classes, vtables `0x5ec4ac`…`0x5ec65c`) | If the button's id (+0x92) is in the player's disabled-button list +0x6702, the button is disabled (vfunc +0x54, flag +0x86 = 1). Otherwise a button that was disabled this way is re-enabled (vfunc +0x48) |
| 15 | `0x52007e` | Button click `0x520060` (same 5 classes) | Stores the clicked button's id into player+0x6714 (for IsButtonPress), then the normal click |
| 16 | `0x527f3f` | Interface handler `0x527490`, message **0x17 TIMER_YEAR** (host) | Skips the **yearly autosave** block: no rotation `autosave.eug` → `oldautosave.eug`, no save, and the countdown +0x66EC (reset from option `+0x2EA`: 0 never, 1/3/5/10 years) is neither decremented nor reloaded |
| 17 | `0x52e024` | Sidebar button layout `0x52de10` (end) | Loops button ids 0..0x65 (102) and disables every button id present in +0x6702 (same as row 14), so a rebuilt sidebar keeps the script's locks |
| 18 | `0x52eaa9` | Player vtable slot +0x3B8 `0x52eaa0` (both player classes) | **No effect**: the result is discarded, it always calls `0x446ae0` |
| 19 | `0x538388` | AI peace-offer planner `0x5382f0` (gate 4) | Returns false: the **AI never starts an OfferPeace plan** (SPEC_AI §3) |
| 20 | `0x557a2a` | AI country vtable slot +0x3B4 `0x557a20` ("call allies", from WE_DECLARE_WAR `0x44b755` and THEY_DECLARE_WAR `0x44bb7d` when the country is in an alliance) | Normally an AI with state religion 0..4 (Christian) calls its allies only if the opponent's capital is on continent 2 (Europe); others always do. With the flag (or fantasy `[0x8d0d7a]`) the filter is skipped: allies are **always** asked (REQUEST_HONOR_ALLIANCE 0xEA to each member, `0x442260`) |
| 21 | `0x55a038` | AI agent "send unit to province by direct route" `0x55a010` (used by land plans `0x559d90`/`0x559eb0`, sea `0x559f60`, explorers `0x553980`) | Returns **true at once without doing anything**: the plan believes the unit was sent, but no merge, halt or move order (`0x4b87e0`) is issued. In practice **AI armies and fleets do not move** under AI plans (NIEPEWNE whether other order paths exist, e.g. transports `0x55a320`) |
| 22 | `0x55bd10` | Natives' PROVCHANGECONTROL override `0x55bce0` | After the normal handler, skips the settlement damage when natives win a province (SPEC_NATIVES §4.3) |
| 23 | `0x55fb17` | In-game menu handler `0x55faf0` (class vtable `0x5f5f3c`, ctor `0x55f5bb`) | Disables button +0x18 **OPTBTN_SAVE** and +0x14 **OPTBTN_LOAD** every time. Options, Hints, Quit, Back stay |
| 24 | `0x5b23e7` | Globals month step `0x5b22f0` (TIMER_MONTH / GAME_STARTED, host) | Skips the tail: (a) **automatic white peace** — for each war in `[0x8d0d80]+0x2E894`, 3 years (`0x417210(3)`) after war+0x1A, with both sides non-empty and neither side holding the other's provinces (`0x5b2540` both ways), message 0x171 is posted; (b) the **HRE election** when no emperor exists (SPEC_HRE §2.1) |
| 25 | `0x5b49c8` | Main loop `0x5b47c9`, winner set (`[0x647d4c]+0x0C`) | Skips the victory screen (`0x52ce90`) and returns to frontend screen 1, the chapter list (`0x5b4a20`) (SPEC_TUTORIAL §1.6) |

## 3. Effects grouped by purpose

- **Tutorial machinery**: rows 1, 11, 12, 13, 14, 15, 17, 25 (script object, scroll counter, step in
  the save, button locks, last click, return to chapter list).
- **Player always wins the dice**: colonists, traders and merchants never fail (3, 4, 5, 6).
- **Player is helped**: 12× manpower (2); no revolts (10); no automatic missions (7, 8).
- **World is frozen**: AI does not move units (21), never offers peace (19), always calls allies
  (20), no automatic white peace or HRE election (24), natives do not damage settlements (22).
- **Player is restricted**: no save/load from the menu (23), no disbanding (9), and (from
  SPEC_TUTORIAL §4, vtable overrides, not flag tests) no war declaration from the diplomacy menu.
- **No autosave** (16).

Not affected (no flag test found): events, combat, sieges, economy other than manpower, AI
diplomacy other than peace offers and alliance calls, the date/speed (forced by `optionmode`).

## 4. Open points

- Row 7 (b): what the lists at country+0x2110/+0x2116/+0x211C are — NIEZNANE.
- Row 8: player-only mission auto-assignment vs SPEC_MISSIONS "AI accepts" — NIEPEWNE.
- Row 21: whether AI units have any other move path in a tutorial — NIEPEWNE.
- Row 24: `0x5b2540` meaning ("side holds a province of the other side") — NIEPEWNE.
- Header write-back key name (§1) — NIEPEWNE.

## 5. Port checklist

- [ ] Header parser: `tutorial = yes` → `hdr.tutorial = 1`; anything else 0; reset per header.
- [ ] Header writer: write the flag back; save writer: write `tutorial = <step>` when set.
- [ ] One accessor `is_tutorial()`; no other code writes the flag.
- [ ] Local player object: tutorial variant + script load when set.
- [ ] Manpower: player's yearly rate ×12 after the vassal sum.
- [ ] Colonist/trader send: skip failure roll and native-attack roll.
- [ ] Merchant send: force success; contested arrival: always win the second roll.
- [ ] Missions: no initial offers, no monthly auto-assignment/replacement; skip the +0x2110 clamp.
- [ ] Disband: never allowed (unit-window button 0x10 and right-click entry greyed).
- [ ] Revolt roll: return immediately for every country.
- [ ] Map scroll keys and edge scroll: bump the tutorial scroll counter.
- [ ] Buttons: apply the disabled-id list on every refresh and after sidebar rebuild; record last click.
- [ ] Yearly autosave: skip entirely.
- [ ] AI: no peace-offer plans; always call allies; direct-route order returns success and does nothing.
- [ ] Natives: no settlement damage on control change.
- [ ] Monthly globals: no automatic white peace, no HRE election.
- [ ] In-game menu: Save and Load disabled.
- [ ] Winner: back to the tutorial chapter list, no victory screen.
