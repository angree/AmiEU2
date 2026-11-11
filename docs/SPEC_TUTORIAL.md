# EU2 v1.09 tutorial engine: specification

Read out of `eu2.exe` (objdump `/home/angree/full.txt`, string/xref tools in `C:\temp\amiga_eu2\re`).
This is prose and tables only. Addresses are given so each statement can be checked again.
**NIEPEWNE** = inferred; **NIEZNANE** = not found.
Data: `Tutorial/Chapter1..9.eug` + `.csv`, `Config/text.csv` (keys `TUTORIAL_*`, `FETUTORIAL_*`).

---

## 1. Chapter flow (frontend → game → frontend)

1. **Main menu** button `FEMAINBTN_TUTORIAL` "TUTORIAL" (`0x5849bd`, id 0x74) switches the frontend
   to screen 1 (`0x58efd0` case 1). That screen is the tutorial screen, class ctor `0x5875a0`.
2. **Tutorial screen** (`bg_tutorial.bmp`, full screen) shows **10 buttons**
   `btn_frontend_big.bmp` (176x32 frames). Each one is centred at x = 400 − w/2, and their tops are at
   y = 100, 140, …, 460 (step 40). Labels are `FETUTORIAL_1..10` in garamond14bold: Interface,
   Units, Combat, Diplomacy, Religion, Colonisation, Economy, Development, Information,
   BACK TO MAIN. The chapter list is fixed: there is no file scan, and no lock on chapters already done.
   - Button 10 → back to the main menu (next screen 0).
   - Button i (1..9) (`0x587760`) → the scenario path becomes `Tutorial\Chapter<i>` (format
     `Tutorial\Chapter%d`, the extension `.eug` is added by the loader). Game type `[0x647d48]` is set to 0,
     the next frontend state is 0x0E (start the game), and two globals (`[0x91a73c]=1` and
     `[0x619fa0]=0x71b`) are set, meaning NIEZNANE. After that it is loaded like any single-player scenario.
3. **Scenario header**: `tutorial = yes` sets the tutorial flag. The test is `0x419700` =
   byte `[0x8ff63c]+0x2766`. `tutorialfile = "…csv"` (token 0x1EA) stores the script path at
   `[0x8ff63c]+0x2666`. `gametype = 3` is the mission game type. `optionmode = 1` + `set_*`
   force the options (difficulty 2, AI aggressiveness 0, speed 1, base VP 0, missions 0).
4. **Game start** (`0x43b82a`): when the tutorial flag is set, the human player's country object is
   created from the **tutorial subclass** (ctor `0x567770`, size 0x6754). It derives from the main
   interface/player class `0x524840`. Then the script is parsed (`0x568890`, see §2). An empty
   `tutorialfile` falls back to `db\tutorial.csv`.
5. **Saved games** store the current step number as `tutorial = N` (token 0x17F, written from
   `+0x671C` at `0x4a34a4`). On load, `0x567ab0(N)` sets the counter and throws away the first N−1
   records, so **the step that was running restarts** from its beginning.
6. **EndTutorial** makes the player the game's winner (`[0x647d4c]+0x0C` := player country). The main loop
   (`0x5b49a7`) sees a winner. In a tutorial game it skips the victory screen (`0x52ce90`,
   GAME_WON/GAME_MISSION), closes all windows and goes straight back to **frontend screen 1, the
   tutorial chapter list** (`0x5b4a20` → `0x58efd0(1)`). It does **not** go on to the next chapter
   by itself. Each chapter's last ShowMessage (`TUTORIAL_n_END` …) tells the player which chapter
   comes next.

## 2. Script file and parser (`0x568890`)

- The format is the usual `;`-CSV reader. The **first line is skipped** (that is the header, and it is why "Action" appears
  9 times in the counts: it is never an action). After that there is one record per line, and every line has **23 fields**:
  `Action;Bool All;Num;Level;D1..D19`. The trailing `x` is simply D19. It reads as 0, an unknown name or
  a tag, and is ignored.
- `Action` is looked up **case-insensitively** (`_stricmp` `0x5bc060`) in the table at `0x635074`
  (50 × {name, id}, ids 0..49 in the order of §4). If the name is unknown, the record gets id −1. It is still queued, and
  at run time it **blocks the script forever** (falls into the "no handler" case). Our parser should report it.
- `Bool All` → byte `rec+0x16` = (value ≠ 0). `Num` → `rec+0x18` (int). `Level` → `rec+0x2E` (int).
  Numbers are parsed with `atoi`, so an empty field or a text field reads as 0.
- How the data columns are read depends on the action (columns D1..D19, comments `#…` end up as 0 or unknown):

| Data kind | Actions | Rule |
|---|---|---|
| **K** text key | ShowMessage, AddMessage | D1 = key base `K`. The parser stores **`K_NAME`** and **`K_DESC`** (suffix table `0x635200`), and the rest of the line is ignored |
| **I** int list | SetNatives, Discover, Colonization, NativesCleared, AlwaysSuccess (read, unused), AddArrowsPid, CreateCot, AddMerchants, SetProvinceIW, CheckMerchant, CheckForCities, BuildArmy | all 19 read as ints; **non-zero** values are appended (province ids) |
| **P** int pairs | AddArrowsPos | read (x,y) pairs from D1; stop at the first pair where either value is 0 (at most 9 pairs); both values appended |
| **B** button names | DisableButtons, EnableButtons, IsButtonPress, AddArrows | 19 strings, each `BUTTON_*` mapped through the button table `0x62a618` (entries {id, name[256]}, 101 names, the `Config\buttons.txt` ids). Unknown names are dropped |
| **T** country tags | CheckMilAlliance, DiscoverCapital, DeclareWar, OfferRoyalMarriage, OfferAlliance | 19 strings → country index (`0x4696d0`). 0 or unknown is dropped |
| **–** none | every other action | D1..D19 are ignored. Only Bool All / Num / Level count |

- Record layout (0x32 bytes, for reference): +0 id, +4 int list, +0x16 BoolAll, +0x18 Num,
  +0x1C string list (NAME, DESC), +0x2E Level. Records are kept in a FIFO at `obj+0x66F0`.

## 3. Runtime (`0x569120`, called from the subclass event handler `0x568640` on message 0x18)

- `0x568640` runs once per UI update. Message type 0x18 is a frame/UI tick (NIEPEWNE). It must run
  while the game is paused, because the scripts do `StopTime` and then wait for clicks. It is skipped while
  byte `[0x647d4c]+0x116` is set (NIEZNANE flag).
- **There is at most one current record** (`obj+0x6728`). If there is none, the head of the FIFO is popped,
  the step counter `+0x671C` is incremented, `tutorial part:%s` is written to the debug log, and the
  record is **executed in the same tick**.
- When an action completes, the record is freed and the current record is cleared. The **next record starts on the next tick**,
  so the script advances at most one action per tick. A **blocking** action simply returns without
  completing and is tested again every tick. **Nothing ever times out.**
- After the step function, the same handler gives every live arrow its tick (`arrow vfunc+4`).
- **"Bool All" rule** (Discover, Colonization, NativesCleared, CheckForCities, CheckMilAlliance,
  CheckMerchant): `n` = how many Data items satisfy the test. If BoolAll = 0, the step completes when **n > 0**
  (any one item). If BoolAll ≠ 0, it completes when **n = the number of Data items** (all of them).
- **Level** is read by CheckTradeLevel only. **Num** is the count, threshold or single province, as given per row.

## 4. Actions

"Done" = completes the same tick (non-blocking). "Waits" = blocks until the condition holds.
Our = the human player's country. Sel = the current map selection list (`obj+0x63C8`, units).
P(n) = province record n (`0x8077d4 + n·0x4A0`). Used = count in the 9 chapters.

| id | Action (used) | Params | Effect / wait condition | Addr |
|---|---|---|---|---|
| 0 | **ShowMessage** (129) | D1 = key | Posts a message of message type **TUTORIAL** to our country through the message system (`0x8c90d8`.`0x577b80`, type 1): title = text(`K_NAME`), body = text(`K_DESC`). It then posts a **PAUSE command (0xDA)** and remembers the id of the popup just created (`[0x6493a4]`, the last framed popup). **Waits until that popup no longer exists** (the player clicked OK or closed it), then cleans up, posts **UNPAUSE (0xDB)** and completes. See §5.1 | `0x56a010` |
| 1 | **SetNatives** (3) | Num = value, I = provinces | For each province: P+0x128 := Num. This is the **native-size cap**: the natives setter `0x482a20` clamps P+0x12C to it. Done. (NIEPEWNE whether natives appear at once: the current size +0x12C is not written) | `0x5691de` |
| 2 | **Discover** (2) | BoolAll, I = provinces | **Waits** until province(s) are discovered by us (byte P+0x256+our index ≠ 0). Bool All rule | `0x569ea0` |
| 3 | **CheckNumArmies** (1) | Num | **Waits** until the number of our units (list +0x1248) for which unit vfunc+0x14 holds (army) is ≥ Num | `0x56924a` |
| 4 | CheckNumNavies (0) | Num | Same with vfunc+0x18 (navy) | `0x56928d` |
| 5 | **Colonization** (1) | BoolAll, I | **Waits**: province has a settlement (P+0x408 ≠ 0) and owner (P+0x3C4) = controller (P+0x3C8) = us. Bool All rule | `0x569e00` |
| 6 | NativesCleared (0) | BoolAll, I | **Waits**: natives P+0x12C = 0 and every unit in the province is ours (or there are none). Bool All rule | `0x569f40` |
| 7 | **AlwaysSuccess** (2) | – | Sets flag +0x672C. From then on our colonist success chance (country vfunc +0x44, normally `0x436b90`, 1..95) is **100**. Permanent for the session. Done | `0x5692f6`, `0x567f30` |
| 8 | AddArrows (0) | B = buttons | Arrow on an interface button. It is built, but our reading is that the button id lands in the province slot (+0x22), so it probably never worked (NIEPEWNE). Unused | `0x5680d0` |
| 9 | **AddArrowsPos** (118) | P = (x,y) pairs | One arrow per pair pointing at **absolute screen pixel (x,y)** at 800x600 (§5.2). Done | `0x568240` |
| 10 | **AddArrowsPid** (24) | I = provinces | One arrow per province that follows the province on the map (§5.2). Done | `0x5683a0` |
| 11 | **RemoveArrows** (109) | – | Deletes **all** arrows. Done | `0x56930e` |
| 12 | CreateCot (0) | I | Creates a centre of trade in each province (`[0x647d40]`.`0x49a4b2`). Done | `0x569374` |
| 13 | AddMerchants (0) | Num, I = COT provinces | For each one: `0x444870(Num)` (a treasury-type post, NIEPEWNE), then a command 0xA7 to place one of our merchants in that province's COT. Done | `0x569c30` |
| 14 | **CheckMerchant** (1) | BoolAll, I = COT provinces | Has no step handler. It is evaluated in our **merchant-arrival hook** (country vfunc +0x158 = EXEC_MERCHANT_SUCCESS, normally `0x446ae0`). When one of our merchants arrives, n = the number of Data items equal to the arrival's COT province, and the Bool All rule applies. **Waits** for such an arrival (success is not required). Then the normal arrival logic runs | `0x568550` |
| 15 | CheckTradeLevel (0) | Level | **Waits** until our trade-tech level (`0x4333f0` → vfunc+0x34) is ≥ Level | `0x5693bc` |
| 16 | **SetProvinceIW** (12) | D1 = province | Selects that province (`0x476380`) and switches the sidebar to info window **9** (province window, `0x5271a0(9)`). Done | `0x56946b` |
| 17 | CheckMilAlliance (0) | BoolAll, T | **Waits**: we are in the same military alliance as the tag (relation flag +0x3C). Bool All rule | `0x569bb0` |
| 18 | DiscoverCapital (0) | T pairs (A,B) | Country A discovers B's capital (`0x481660`). Done | `0x569407` |
| 19 | CheckForCities (0) | BoolAll, I | **Waits**: P has a settlement that passes city vfunc +0x20 or +0xC (NIEPEWNE: "is a city"-type tests), and owner = controller = us. The first match is stored in +0x6720 for action 20. Bool All rule | `0x569a20` |
| 20 | CheckForArmies (0) | Num | **Waits** until the summed strength (unit vfunc+0x8C) of the armies in province `+0x6720` is ≥ Num | `0x569b30` |
| 21 | DeclareWar (0) | T pairs (A,B) | A declares war on B (`0x4418b0`). Done | `0x5694a9` |
| 22 | CheckIfAnyLoadedUnits (0) | – | **Waits** until one of our fleets carries troops (`0x488f40`) | `0x5694f6` |
| 23 | EnableCavalry (0) | – | Sets flag +0x672F. Country vfunc +0x30 returns it in the tutorial, so cavalry is unavailable until this runs (NIEPEWNE name of the vfunc). Done | `0x569543`, `0x56aa40` |
| 24 | CheckNumTradingPosts (0) | Num | **Waits** until our list +0x1308 (trading posts, judging by this use) has ≥ Num entries | `0x569572` |
| 25 | **DisableButtons** (13) | B | For each button: disable it (frame 3, `0x56a9a0`) and add it to the disabled list +0x6702. The interface's per-frame button refresh (`0x52e020`, ids 0..0x65) disables listed buttons again, so the normal logic cannot re-enable them. Done | `0x5695ab` |
| 26 | **EnableButtons** (6) | B | Removes each button from the list and re-enables it (button vfunc+0x44). Done | `0x569850` |
| 27 | **IsButtonPress** (12) | B | **Waits** until the *last clicked button id* (+0x6714, written by the button click handler `0x520093` in tutorial games) equals any listed id. This value is never cleared, so a click from before the step also counts | `0x5699c0` |
| 28 | **WaitForScroll** (1) | Num | **Waits** until the scroll counter +0x6718 is ≥ Num, then resets it to 0. The counter goes up by 1 for **each keyboard scroll command** (map message 0x15, `0x475867`) and for **each frame the map edge-scrolls** under the mouse (`0x475aef`). It is not reset when the step starts | `0x5698c0` |
| 29 | **EndTutorial** (9) | – | Winner := us → back to the chapter list (§1.6). Done | `0x569910` |
| 30 | **BuildArmy** (1) | Num = men, I = provinces | Creates one of our armies of Num men in each province (CREATE_ARMY `0x4437c0`). Done | `0x569950` |
| 31 | **StopTime** (15) | – | player+0x2306 := 1 (the game-paused flag, the same one the event dialog uses). Done | `0x56a280` |
| 32 | **StartTime** (10) | – | player+0x2306 := 0. Done | `0x56a2c0` |
| 33 | **ArmySelected** (4) | Num | **Waits** until ≥ Num armies are in Sel | `0x56a300` |
| 34 | **FleetSelected** (2) | Num | **Waits** until ≥ Num non-army units are in Sel | `0x56a360` |
| 35 | **ArmyIn** (5) | Num = province | **Waits** until an army **in Sel** has location (unit+0xC) = Num. **Num 0 = aboard a ship** (Chapter 2 uses this for "load"). The army must be selected | `0x56a3c0` |
| 36 | **FleetIn** (2) | Num | Same for a fleet in Sel | `0x56a430` |
| 37 | GameSpeed (0) | – | Parsed, **no handler: blocks forever**. Unused | – |
| 38 | SelectArmy (0) | Num | Clears the selection, then searches Sel for a unit at Num (logic looks broken, NIEPEWNE). Unused | `0x56a4a0` |
| 39 | DisableSelections (0) | – | Parsed, **no handler: blocks forever**. Unused | – |
| 40 | **NoEnemyIn** (3) | Num | **Waits** until every unit in province Num is ours (or there are none) | `0x56a520` |
| 41 | **WeControl** (1) | Num | **Waits** until controller P(Num)+0x3C8 = us | `0x56a590` |
| 42 | IsProvinceSelected (0) | Num | **Waits** until the selected province `[0x61f418]` = Num | `0x56a5e0` |
| 43 | **LockScroll** (5) | – | Sets the interface byte +0x6328 := 1. **No reader of this byte was found**, so it is probably a no-op in v1.09 (NIEPEWNE). Done | `0x56a660` |
| 44 | **OfferRoyalMarriage** (1) | T pairs (A,B) | A offers a royal marriage to B (`0x441140(1,B)` on A). Chapter 4: `WLS;ENG` = Wales proposes to the player. Done | `0x56a6a0` |
| 45 | **CenterMap** (8) | Num | Centres the map on province Num (P+0x3E4/+0x3E8 → `0x476960`, then redraw). Done | `0x56a700` |
| 46 | **AddMessage** (37) | D1 = key | Opens a **non-blocking** tutorial info popup (§5.1) and adds it to the list +0x6730. Done | `0x56a770` |
| 47 | **RemoveMessage** (39) | – | Closes **all** AddMessage popups. Done | `0x56a8d0` |
| 48 | OfferAlliance (0) | T pairs | A invites B into an alliance (`0x441140(4,B)` on A). Done | `0x56a940` |
| 49 | **IsIW** (6) | Num | **Waits** until the current sidebar info window id (+0x63D0) = Num. The ids are the decimal slot numbers of SPEC_UI_WINDOWS4 §0. In the scripts: 4 COT, 11 purchase army, 16 peace, 17 resource, 24 religion, 25 domestic | `0x56a620` |

Other changes in tutorial games (vtable `0x5f6240` overrides of the player class `0x5eca54`):
vfunc +0x378 (the DIPWAR "may declare war" rule) returns **false**, because its enabling flag +0x672E is never set.
**The player cannot declare war from the diplomacy menu in any chapter.** vfuncs +0x38 and +0x3C
always return false (NIEZNANE which features they gate).

Typical pattern in the scripts: `AddArrows…` + `AddMessage` (instruction box), then a waiting action
(ArmyIn / IsButtonPress / IsIW …), then `RemoveMessage` + `RemoveArrows`. `ShowMessage` is used for
explanations that the player confirms with OK.

## 5. Visuals

### 5.1 Message windows

| | ShowMessage | AddMessage |
|---|---|---|
| Mechanism | the normal message system, message type **TUTORIAL** (prefix table `0x6380a4`: "TUTORIAL"/"tut") | its own popup class (ctor `0x567af0`, vtable `0x5f6694`, base framed popup `0x427130`) |
| Texts | header `TUTORIAL_HEADER` "Tutorial Info%s", button `TUTORIAL_BTN1` "OK", log line `TUTORIAL_LOG` "%s: %s" (name: desc). The message-settings row is `TUTORIAL_SETUP` "When a scripted tutorial event occurs" | title = `K_NAME`, body = `K_DESC` |
| Look | the standard message popup with an OK button (SPEC_UI §7, NIEPEWNE which of the two popup classes) | framed `win16` popup (`0x428810(0,W,H,16)`), **W = 384**. Title garamond20bold **white**, centred, y = 32. Body garamond14 **gold `#FFBD00`**, wrapped to 320, centred, y = 52 + titleLines·lineH. H = ((bodyLines·lineH + bodyY + 36) & ~15) + 16. **No buttons** |
| Position | message popup placement | default popup placement: cascade, clamped to x ≥ 224, y ≥ 32 (SPEC_UI §1), NIEPEWNE |
| Modal / pause | blocks the **script**, and pauses the game with PAUSE 0xDA / UNPAUSE 0xDB while open. If the player has set this message type to "log only", no popup appears and the step ends at once (NIEPEWNE) | does not block or pause. Stays open until RemoveMessage |

### 5.2 Arrows ("info-arrow", class `0x5339a0` / ctors `0x533b10` pos, `0x533da0` province)

- **Not a bitmap.** `tutorialarrow.bmp` is never loaded. Each arrow has a 64x64 8-bit surface (hotspot
  32,32) drawn through the palette `gfx\palette\arrows.act` (`[0x645248]`). Every frame the arrow
  shape is **drawn as filled triangles**, rotated by the arrow angle around (32,32) (sin/cos table, `0x414b60`):
  points tip (0,−24), tail corners (−16,24) and (16,24), notch (0,12), which makes a 32-wide, 48-long chevron.
  It is drawn first as a **shadow offset (+5,+5) in palette index 1**, then the body in **index 3**
  (`0x534560`). The exact triangle split is NIEPEWNE.
- The angle is chosen so the **tip points at the target** (atan of dy/dist, mirrored for targets on the left, +90°).
  There is no other animation (no bobbing or blinking was found). Arrows live until RemoveArrows.
- **AddArrowsPos (x,y)**: the target is screen pixel (x,y), in fixed **800x600 screen coordinates**. The arrow
  centre sits at **(x+32, y−32)**, so it comes in from the upper right. Near the edges it flips: y−32 < 0 → y+32, and x+32 > screenW−1 → x−32.
  Coordinates in the scripts address the sidebar (minimap (110,519), zoom buttons (40/190,575), PIW
  elements while SetProvinceIW is shown) and the top bar ((520,20)).
  Note for the Amiga 640x480 layout: every script coordinate must be remapped (a table in our converter).
- **AddArrowsPid n**: the target is the centre of province n (midpoint of its bounding box, `0x475790`),
  converted to screen coordinates each tick from the current scroll/zoom. The arrow follows the map.
  When the province is far outside the viewport (beyond half the view + 100 px from its centre), the arrow is pulled
  in along the line toward it and parked inside the map viewport (about 32 px from the left/top, 48 from
  the right, 112 from the bottom, x offset by the 224-px sidebar) while still pointing at it. These numbers are NIEPEWNE.
- Drawn in main-interface **layer 4** (priority 2400: above the sidebar contents, below the top
  bar). They are created inside the layer-2 draw (`0x568690`): the pending AddArrows* record is turned into
  arrow objects there and completed, which happens within one frame.

## 6. Object fields (tutorial subclass of the player/main interface, for our port)

+0x66F0 record FIFO · +0x6702 disabled-button list · +0x6714 last clicked button · +0x6718 scroll
counter · +0x671C step number (saved) · +0x6720 city found by CheckForCities · +0x6724 open ShowMessage
popup id · +0x6728 current record · +0x672C AlwaysSuccess · +0x672E war-menu enable (never set) ·
+0x672F cavalry enabled · +0x6730 AddMessage popup list · +0x6742 arrow list.
