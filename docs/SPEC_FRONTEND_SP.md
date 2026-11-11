# EU2 v1.09 — frontend SINGLE-PLAYER SETUP screen: country panel (spec)

Source: `eu2.exe` v1.09 disassembly (objdump + annot.py), data `Scenarios/*.eug`, `Scenarios/data/*.bmp`,
`DB/country.csv`, `Config/text.csv`. Layout facts only, no code copied. All coordinates are absolute
800x600 screen pixels, top-left of the sprite/text unless "centred". NIEPEWNE = guess, NIEZNANE = not found.
Colours: `0x4ca1b0(r,g,b)` builds a 16-bit colour (first argument = red, verified in the function);
`0xFFFF` = white.

## 0. Objects

| Item | Address | Notes |
|---|---|---|
| Setup screen ctor | `0x584ef0` | created by the screen factory `0x58efd0` with `bg_frontend.bmp`; vtable `0x5f6eec`: handler `0x5855c0`, draw `0x586280`, button dispatch `0x586580` (table `0x586824`) |
| Scenario/shield object (`S+0x182`) | ctor `0x58c410(path)` | size 0xAC; `+0xA0` = loaded scenario (also global `[0x8ff63c]`), `+0x9C` = number of selectable countries, `+0x10..` shield buttons, `+0x90` selected shield index (-1 = none), `+0x94` flare, `+0x98` shield shadow, `+0xA8` right-click plate. Handler `0x58cd80`, draw `0x58ce70`, click test `0x58cfa0`, select-by-country `0x58d210` |
| Country panel (`S+0x192`) | ctor `0x589ee0(scen, slot, btnBack2, btnStrategy)` | vtable `0x5f6fb4`: handler `0x58a340`, draw `0x58a450`; dtor `0x58a2d0`. Rebuilt every time the selected shield changes |
| Rating row helper | `0x58a9b0(value, y, prefix)` | see §3 |
| Header country block parser | `0x4a4e7d` (token switch, tables `0x4a5281`/`0x4a5249`); tag init + defaults `0x4a4e04`; default picture `0x4a4c86` | object field offsets below are +0x0E against the parser's `this` |

Setup-screen fields used here: `+0x52..+0x76` ten `btn_frontend` buttons (§5), `+0x42` scenario list,
`+0x46` saved-games list, `+0x182` scenario/shield object, `+0x18A` selected slot (-1 none),
`+0x18E` VICTORY overlay open, `+0x18F` INFO pressed, `+0x190` OPTIONS overlay open,
`+0x192` country panel, `+0x196` victory box (`0x58d2f0`), `+0x19A` options box (`0x58aaa0`),
`+0x19E` log text box.

## 1. Header fields — what the parser really stores (per selectable country)

| .eug key (inside `header = { TAG = { ... } }`) | Stored at | Default (set at `0x4a4e04` before parsing) |
|---|---|---|
| `desc` **and** `scenariotactics` (same slot; the later line in the block wins) | `+0x2132` "history" text key | `"<TAG>_DESC"` (`0x469720` = tag string) |
| `countrytactics` | `+0x2532` "strategy" text key | `"<TAG>_DESC"` (same as history!) |
| `missiontactics` | `+0x2932` mission strategy key | empty |
| `missioncountry` | `+0x2D32` mission history key | empty |
| `picture` | `+0x3132` bitmap path | culture fallback, §2.2 |
| `playername` | `+0x3232` | empty (multiplayer saves only, §6) |
| `countrydifficulty`, `countryeconomy`, `countrymilitary`, `countrydiplomacy` | `+0x3352`, `+0x3356`, `+0x335A`, `+0x335E` (int) | 0 |
| `missiontype` | `+0x3362` = value − 1 | 0 |
| `vpcondition` | list at `+0x3324`, count `+0x3340` | — |
| `numcities`, `numcolonies`, `numtradingposts`, `numgold` | **ignored** (case `0x4a5145` jumps straight to the loop end) | — |

So **no "stats lines" (cities / colonies / trading posts / gold) exist on this screen** — those four
keys are parsed and thrown away.

The text keys are looked up in text.csv (`0x41554b`); if the lookup returns the "not found" string
(`"UNKNOWN STRING WANTED\n"`, `0x633c34`) the **raw field string itself** is shown instead
(`0x58a1a1..0x58a2aa`). There is no `<TAG>_<code>_HISTDESC` construction anywhere — the key names
come verbatim from the .eug (e.g. Grand Campaign has no `desc`, so FRA shows `FRA_DESC` in both views;
Revolutions has only `scenariotactics ="USA_1773_S"`, so history = `USA_1773_S`, strategy = `USA_DESC`).

## 2. Country panel (drawn only when a shield is selected and neither VICTORY nor OPTIONS is open)

Draw order `0x58a450`:

| # | What | Position | Details | Address |
|---|---|---|---|---|
| 1 | Country shield | (521,123) | `0x43b1d0(country)` → `gfx\map\shields\shield_%s%s.bmp` (224x57, 4 frames of 56x57), frame 0, remapped through `gfx\palette\shield palette.bmp` | ctor `0x58a12f..0x58a186`, draw `0x58a475` |
| 2 | Country picture | (524,118), 232x120 | bitmap from field `+0x3132`; drawn **over** the shield, green `#00FF00` is transparent (§2.2) | ctor `0x58a0b2..0x58a0e1`, draw `0x58a485` |
| 3 | STRATEGY / BACK toggle | (310,304) | `btn_frontend` 136x32 (screen buttons `S+0x66` "STRATEGY" and `S+0x6E` "BACK"), only one is on screen, the other is parked at (-1000,-1000) | `0x58a0e4..0x58a110`, `0x58a340` |
| 4 | Engraved rule | (524,280)-(764,280) colour (218,185,110); (524,281)-(764,281) colour (137,109,64) | line `0x4010d0` | `0x58a4ed..0x58a55f` |
| 5 | Title | (246,128), left | garamond14bold, white, **upper-cased** (`0x414be0` = copy + `CharUpperBuffA`), §2.1 | `0x58a564..0x58a7c4` |
| 6 | Text box (history or strategy) | (246,150), 238x118 | word-wrapping list `0x41f5b0(238,118,0,0)`, text colour 0xFFFF; font = list default (garamond14, NIEPEWNE); scrolls via its own handler | ctor `0x589f03..0x58a04a` |
| 7 | Labels | x = 524; y = 262 / 290 / 304 / 318 | garamond14bold white, left: `FESTR_DIFFICULTY` "DIFFICULTY:", `FESTR_ECONOMY` "ECONOMY:", `FESTR_MILITARY` "MILITARY:", `FESTR_DIPLOMACY` "DIPLOMACY:" (difficulty sits above the rule, the other three below) | `0x58a7db..0x58a8d0` |
| 8 | Ratings | same y | §3 | `0x58a8d5..0x58a980` |

### 2.1 Title and the INFO/STRATEGY cycle

`name` = country name (`0x469750(country)`); `M` = victory mode `[0x647d48]` == 3 (Mission).

| View | Condition | Title (then upper-cased) | Text box contents |
|---|---|---|---|
| History (initial) | BACK parked, STRATEGY at (310,304) | `name` [+ `" - "` + `FEVIC_MISSION` if M] + `" "` + (M ? `FEBTN_INFO` : `FESTR_HISTORICALINFO`) → e.g. "SWEDEN HISTORICAL INFO", "SWEDEN - MISSION INFO" | M ? `missioncountry` (+0x2D32) : `desc`/`scenariotactics` (+0x2132) |
| Strategy | STRATEGY clicked → BACK moved to (310,304), STRATEGY parked | `name` [+ `" - MISSION"` if M] + `" - "` + `FESTR_STRATEGY` → "SWEDEN - STRATEGY" | M ? `missiontactics` (+0x2932) : `countrytactics` (+0x2532) |

Clicking BACK returns to the history view. The four text boxes are all filled in the ctor; the draw just
picks one. The view is not remembered — a new shield selection rebuilds the panel in history view.

**The right-column INFO button does not cycle anything.** Its handler (`0x5866ee`) only stores its
pressed state in `S+0x18F` (never read) and closes the VICTORY and OPTIONS overlays
(`S+0x18E = S+0x190 = 0`), i.e. INFO = "show the country panel again".

### 2.2 Picture and its fallback

The panel loads whatever path `+0x3132` holds. None of the shipped scenarios has a per-country
`picture` line (only the scenario-level `picture` in Fantasia/Mercantilism, whose files do not even
exist; that one is not used on this screen), so in practice **every country shows the default picture**
chosen at `0x4a4c86` from `DB/country.csv` (static table `0x64f730 + idx*0x144`):

| Condition (column "City Nationality" = field +0x11C, a country index) | Picture |
|---|---|
| OHI (44) | `scenarios\data\northamerica_data.bmp` |
| CHI (62), index 63 | `scenarios\data\china_data.bmp` |
| MOG (64) | `scenarios\data\hindu_data.bmp` |
| AZT (65) | `scenarios\data\southamerica_data.bmp` |
| BAN (76) | `scenarios\data\afrika_data.bmp` |
| index 0x14F (MUS, NIEPEWNE: index not verified) | `scenarios\data\muslim_data.bmp` |
| otherwise, techgroup (field +0x110) = orthodox | `scenarios\data\orthodox_data.bmp` |
| otherwise | `scenarios\data\latin_data.bmp` |

The eight `*_data.bmp` files have a pure-green window at local (10..38, 13..51) = screen (534..562,
131..169): the shield drawn first shows through it. The per-scenario pictures (`TUR1617.bmp`,
`SWE1700.bmp`, `usa1776.bmp`, ...) have no window and are not referenced by any shipped .eug
(NIEPEWNE: unused leftovers).

## 3. Rating rows (`0x58a9b0`)

For each row: `v = field + adj`, clamped to 1..5.
`adj` (applied to **all four** rows) comes from the frontend DIFFICULTY option `[[0x8c0bfc]+0x2CA]`
(0..4, labels FE_DIFFI5..FE_DIFFI1): 0 (Very Easy) → +2, 1 → +1, 2 → 0, 3 → −1, 4 (Very Hard) → −2
(jump table `0x58a994`). `adj` = 0 when scenario byte `+0x2CC4` is set (NIEPEWNE: saved game).

| Part | Position | Details |
|---|---|---|
| Value text | (690, y), left (`0x40cc29`, alignment NIEPEWNE) | garamond14, white; key = `"FE_DIFFI%d"` (difficulty row) or `"FE_STAT%d"` (other rows) |
| Pips | x = 622 + 10·i, y = row y + 2, i = 0..v−1 | `gfx\interface\prickar.bmp` 40x6 = 5 frames of 8x6 (6x6 dot + 2 transparent columns); **frame v−1 drawn v times**: 1 red, 2 orange, 3 yellow, 4 light green, 5 green |

FE_DIFFI1..5 = Very Hard / Hard / Normal / Easy / Very Easy (higher = easier);
FE_STAT1..5 = Very Weak / Weak / Medium / Strong / Very Strong. Row y: difficulty 262, economy 290,
military 304, diplomacy 318.

## 4. What the middle area shows in each state (`0x586280`)

| State | Drawn at the panel area |
|---|---|
| No scenario loaded, options closed | `FESTR_NOSCENARIO` "Please select a scenario in the list to the left." centred (494,226), garamond20bold white |
| Scenario loaded, no shield selected, no overlay | `FESTR_NOCOUNTRY` "Please select a country among the shields above." centred (494,220) and `FESTR_CHANGECOUNTRY` "RIGHT-CLICK ON A SHIELD TO CHANGE IT." centred (494,240), both garamond20bold white; then the shield row |
| Shield selected, no overlay | country panel (§2) + shield row |
| VICTORY open | victory box instead of the panel (+ shield row) |
| OPTIONS open | options box (`box_options.bmp` at (238,146)) |

There is **no scenario-level picture or description** on this screen (the header-level `desc`/`picture`
are never read by the setup code). Always drawn: `FESTR_SCENARIOS` centred (112,66) and
`FESTR_SAVEDGAMES` centred (112,390), garamond20bold white; scenario list 134x220 at (36,110);
saved-games list 134x142 at (36,420); the log box (§6); the buttons (§5).

## 5. Buttons, shields, clicks

Right column (`btn_frontend` 136x32, garamond14bold labels, shadow `btn_frontend_shadow` at (x−8,y)):
INFO (642,367), VICTORY (642,412), OPTIONS (642,458), BACK (642,504), START (642,552).
ACCEPT/STRATEGY/the other two BACKs/CANCEL are not drawn by the screen (used by the boxes and the panel).
Enable state is recomputed on every event: START/OPTIONS/INFO/BACK are disabled (frame 3) while
VICTORY is open; START/VICTORY/INFO/BACK while OPTIONS is open; otherwise all enabled.

Shield row (`0x58c410`): selectable country i at (246 + 64·i, 27) with `shield_<TAG>.bmp`; if more than 9
countries, `smallshield_<TAG>.bmp` at (206 + 24·i, 27). Unselected shields get `shield_shadow` at (x, y−4).
Selected shield (≤ 9 countries only): `shield_flare.bmp` (100 frames of 120x120, frame advanced once per
event) at (x−34, y−34), selected shield drawn last.

| Action | Effect |
|---|---|
| Click a scenario / saved game (list event 0x0E) | old shield object deleted; `"<dir>\<file>"` loaded into a new `0x58c410`; selected slot = −1; panel deleted; overlays closed; victory box reset to the scenario defaults (`+0x2CBC`, `+0x2CC0`, mode `+0x2CE8` if ≠ −1); VICTORY/START temporarily disabled (re-enabled on the next event). **No country is auto-selected** (except §6) |
| Left-click a shield | toggles it; others released; slot = shield index, or −1 if it was un-toggled. New slot → panel rebuilt (`0x589ee0`); −1 → panel deleted, the "Please select a country" texts return |
| Right-click a shield (event 0x0F with the right button, `0x4196e0`) | opens the plate `0x58c090` (`plate.bmp`) listing other countries; picking one sends event 0x14 → `0x58ca80` replaces that shield's country (NIEPEWNE on details; see SPEC_UI §8) |
| INFO | closes VICTORY/OPTIONS, shows the panel (§2.1) |
| STRATEGY / BACK at (310,304) | toggles history ↔ strategy view (§2.1) |
| VICTORY / OPTIONS | open their boxes; the panel is hidden while open |
| START (`0x586669`) | calls `0x5b25d0`, sets `[0x91a738 + country*4] = 1` (NIEPEWNE: "human player") and stores the country in the session; START is enabled even with no shield selected (slot −1; behaviour then NIEZNANE) |

## 6. Lower text panel = a green "log" box, not a description

`S+0x19E`: word-wrapping list `0x41f5b0(354,161,1,0)` at **(246,390)**, drawn every frame.
**The setup screen never loads `bg_multiplayer.bmp`** — the multiplayer screens blit it at (230,366)
(`0x57eca2`), and the same art is already baked into `bg_frontend.bmp` (pixel-identical at that spot),
so nothing extra is drawn.

Rows: garamond14, width 346, colour (0,255,0) (row `+0x0C`; NIEPEWNE whether green renders, it is also
the key colour). Rows are only appended (no clearing seen):
- after VICTORY → accept: `FEVIC_STANDARD_LOG` (year) / `FEVIC_SUPREMACY_LOG` (VP) / `FEVIC_DEATHMATCH_LOG`
  (provinces) / `FEVIC_MISSION_LOG`; choosing Supremacy with base VP and missions both off forces
  base VP on and logs that too (`0x5860e2`);
- after OPTIONS → OK: one row per changed option, `FECLI_HOSTSET1`+`FECLI_HOSTSET2`
  "<%s changed to %s>" with option label and value label (`0x586840`);
- on scenario load: each non-empty `playername` field (multiplayer saves, "Country: player") is added;
  if it names the local player, that country is auto-selected (`0x5869e0`).

## 7. Differences from our current code (src/ui.c, src/main_amiga.c)

- `scen_code()` + `<TAG>_<code>_HISTDESC/VPTACTIC/MISSTACT` is a guess; the original uses the header
  fields of §1 verbatim with the raw-string fallback and the `<TAG>_DESC` defaults.
- INFO cycling history → VP tactics → mission tactics → off does not exist; the original has a
  STRATEGY/BACK toggle at (310,304) and mission mode swaps in `missioncountry`/`missiontactics`.
- Blitting `bg_multiplayer.bmp` is redundant (already in `bg_frontend.bmp`); the lower box is the log.
- Picture + shield-through-the-window, the rule lines, the difficulty adjustment of the ratings and
  the pips at x = 622 should be checked against §2–§3.
