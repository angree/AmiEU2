# Ledger (EYR) — screen composition, page catalogue, flow, history

This file records layout facts and data sources only, read from the `eu2.exe` v1.09 disassembly
and the Ghidra export. No code was copied. The Ledger is a full-screen 800x600 view and all
coordinates are **screen coordinates**.
- Sections 1–3 (framework, flow, history) were derived here.
- Section 4 (the per-page catalogue) was merged from `ledger_pages.md`.

## 1. Screen composition

The screen object is built by ctor `0x4e4250` and owned by the main interface (`0x525fdb`).

| Element | Bitmap | Position | Evidence |
|---|---|---|---|
| Top band | `eyr\eyr_top.bmp` | (0,0) | `0x4e42d5`, `0x4e42ef` |
| Bottom band (chapter tabs) | `eyr\eyr_bottom.bmp` | (0,522) | `0x4e4310`, `0x4e432e` |
| Right band | `eyr\eyr_right.bmp` | (696,33) | `0x4e434f`, `0x4e436e` |
| Extra background for 1280/1024-wide screens | `eyr_bg_top_1280` / `_1024` at (800,0); `eyr_bg_bottom_1280` / `_1024` at (0,600) | only at higher resolutions; not needed at 800x600 | `0x4e43ad..0x4e4484` |
| Page background | `eyr\eyr_page<n>.bmp`, n = (page id mod 4) + 1 | (0,33) | `0x4e10d4`, `0x4e130e..0x4e1353` |
| Title banner | `eyr_banner_small.bmp` at (180,46) if the title is narrower than 132 px; otherwise `eyr_banner_large.bmp` at (178,46) | | `0x4e1193`, `0x4e11a5`, `0x4e1378` |
| Page title | centred x=348, y=58 (small banner) or y=55 (large) | garamond18bold, black | `0x4e13a6`, `0x4e13e9` |
| Page-number frame | `eyr_sidenumber_frame.bmp` | (308,472) | `0x4e11ed` |
| Page number | centred x=348, y=480; text = page id + 1 | garamond18bold, colour escape `%000` (**NIEPEWNE**: black) | `0x4e141e`, `0x4e1445` |
| Back / Forward | `eyr_btn_back.bmp` / `eyr_btn_forward.bmp` (4-frame buttons) | (272,481) / (388,481) | `0x4e44cd`, `0x4e4528` |
| Exit | `eyr_btn_exit.bmp`, tooltip EYR_EXIT_ROLLOVER | (720,196) | `0x4e493e`, `0x4e5800` |

**Chapter buttons** (tab group `0x427700`, all y = 522, tooltips CHAPTER_*):

| Index | Bitmap | x |
|---|---|---|
| 0 | `eyr_btn_monarchy` (CHAPTER_MONARCH) | 50 |
| 1 | `eyr_btn_economy` | 146 |
| 2 | `eyr_btn_diplomacy` | 230 |
| 3 | `eyr_btn_technology` | 314 |
| 4 | `eyr_btn_military` | 398 |
| 5 | `eyr_btn_establishments` | 482 |
| 6 | `eyr_btn_history` | 562 |

Evidence: `0x4e45ec..0x4e48b4` (`+0xAA` = x, `+0xAE` = y).

### 1.1 Tablet (table page) frame

Built by `0x4e1f00(title, bodyHeight)`; columns are added with `0x4e1c70`.

- **Pieces:**
  - top edge: `eyr_tablet_uleft`, `ucent`, `uright`;
  - sides: `mleft`, `mright`;
  - bottom edge: `lleft`, `lcenter`, `lright`;
  - grid lines: `linev` (column separators), `lineh` (under the header row);
  - fill: `center`;
  - icon headers: `eyr_symbols_strip.bmp` (0x2E frames, palette `eyr_symbols_palette.bmp`).
  - Evidence: `0x4e207d..0x4e22d4`.
- **Columns:**
  - Column width = max(given width, header text width + 12).
  - Columns are packed left to right; each header is a clickable button centred in its column, 2 px below the tablet top (`0x4e1c70..0x4e1d6f`).
  - Clicking a header sorts by that column (`0x4e1770`; header colour escapes `%000` / `%067` / `%555`, normal / hover / selected — **NIEPEWNE**).
- **Placement:** the tablet is centred at **x = 346** (left = 346 − total width/2, `0x4e3ce0`); top **y = 110** (`0x4e3d00`, constants set in `0x4e10ac`).
- **Body:**
  - A scrolling list 520 px wide (`0x41f5b0(0x208, bodyHeight, 3)`) holds the rows; the header row is 23 px.
  - Rows that do not fit scroll inside the tablet; they do not spill onto further pages.
  - Frame pieces are placed from their own bitmap sizes (`0x4e2670`; exact offsets follow the piece sizes, **NIEPEWNE** to the pixel).
- **Totals row:** an optional EYRTOTAL row is added under numeric columns (`"%.2f $"`, `"%.1f%%"`) by `0x4e2e00`.

### 1.2 Graph pages

Ctor `0x4e0090`; draw `0x4e02c0`; the country-comparison variant is `0x4e0810`.

| Element | Position | Notes | Evidence |
|---|---|---|---|
| Y axis | `eyr_graph_vline.bmp` at (100,110) | | `0x4e0090` |
| X axis | `eyr_graph_hline.bmp` at (100,377) | | `0x4e0090` |
| Plot | origin (105,377) | scale = (vline height − 47) / max value over all series | `0x4e0388..0x4e03ed` |
| Y labels | right-aligned at x=100; y = 145, 192, 239, 286, 333 (table `0x6256ec`) | 5 ticks in steps of 0.2 × max; arial10 | `0x4e049f..0x4e0508` |
| X labels | y = 384; x = 103, 220, 335, 450, 565 (table `0x625700`) | 5 ticks: start year + 0.2·i·(now − start), as `"%%000%d"` | `0x4e0577..0x4e05b3` |
| Legend | entries at x = 102 + 86·k, y = 405; wraps to the next row (+20) past x = 600 | one check box (`eyr_btn_check_false.bmp`) + country name per series, in the country colour `0x43a5d0`; only checked series are plotted | `0x4e0378..0x4e03a5` |

- **Series:** one per country with `country[0x11E]` set (the countries the Ledger tracks), ≤ 301 float samples, values clamped to 0..1,000,000 (`0x4e0810`, `0x4e0a40`).
- **Category charts** (income/expense) are line graphs too: their vtables (`0x5eabf4`, `0x5eac40`) share the draw
  `0x4e02c0`; the record slot (`0x4e8210` / `0x4e8440`) appends, for the player only, one series per category named
  by its label (`0x42c8e0` / `0x42caf0`) with this year's value e+0x154+4i (12) / e+0x184+4i (16). The line colours
  are the palette entries 0..12 of `gfx/palette/income.bmp` / `expense.bmp` (32x32 bitmaps; 13 colours for 16 expense
  kinds: the mapping beyond 12 is NIEPEWNE).

## 2. Flow and page count

- **Pages:** there are 35, ids 0..34, one item per page. The page id (`[page+0x114]`) sets both the number shown (id + 1) and the background (id mod 4).
- **Chapter of a page** (`0x4e5210`):

| Chapter | Page ids | Count |
|---|---|---|
| Monarch | 0–3 | 4 |
| Economy | 4–15 | 12 |
| Diplomacy | 16–19 | 4 |
| Technology | 20–21 | 2 |
| Military | 22–28 | 7 |
| Establishments | 29–33 | 5 |
| History | 34 | 1 |

- **Construction:** the chapter lists live at ledger+0x16 + 0x20·chapter; pages are added in the order listed in §4 (`0x4e4a90`).
- **Navigation:**
  - A chapter button opens that chapter's current page.
  - Back / Forward step within the chapter (chapter methods `0x4df830` back, `0x4df7f0` go to page): +0x18 leaves the page, +0x14 opens it.
  - F6 / BUTTON_VIEW_STATISTICS opens the Ledger at the last page shown (**NIEPEWNE**).
  - Exit closes it.
- **Filling:** a table page fills its rows when it is opened (page vtable +0x10, e.g. `0x4e77b0` for Income), so the data is always current.

## 3. History series

- **Storage:** each graph page keeps one series per tracked country (up to 0x12D = **301** samples). Recording calls the page's value getter (vtable +0x48) per country and appends the result (`0x4e0a40`, page vtable +0x18).
- **When:**
  - The only direct trigger found is the game-start event 0x50 in the main interface (`0x527c6c` → `0x4e52e0`, which calls every chapter's "record all" `0x4df6e0`).
  - The periodic trigger was **not located (NIEZNANE)**. 301 = 1419..1719 suggests **one sample per year** (**NIEPEWNE**).
- **Persistence:** the series are saved in the savegame and reloaded by `0x4a4900` ("unknown EYR page") and `0x4e0c00` ("unknown country tag").
- **Yearly income/expense:** the Income and Expenses tables use the yearly accumulators in country[0xE74], this year at +0x154 / +0x184 and last year 0x70 below. They are rolled once a year.


## 4. Page catalogue (merged from ledger_pages.md)

# Ledger (EYR) — per-page catalog

This file records layout facts and data sources only. It was read from the Ghidra export plus a few
disassembly checks (`led_all.a.txt`, `0x4e5ba0..0x4f2de0`). No code was copied.

The screen ctor `0x4e4250` builds the pages in the order listed below. The chapter start is marked
by the pair of constants pushed before each chapter's first page: 0x16, 0x36, 0x56, 0x76, 0x96,
0xB6, 0xD6. The chapter of every page follows from that construction order (`0x4e4250` call
sequence).

## Page base classes

| Base | Kind | Evidence |
|---|---|---|
| `0x4e1f00(title, h)` | **Table**. `h` = body height in px: a scrolling text box 520×h is created (`0x41f5b0(0x208, h, 3, 0)` at `0x4e1f00`+78). Header row height 23 (`+0x17`). | `0x4e1f00` |
| `0x4e1c70(header, width, flag)` | Adds a text column. Width = max(width, textwidth(header)+12); width 0 = header width + 12. Columns are laid out left to right, each header centred over its column. flag is always 0 (stored at column+0xB2, meaning **NIEZNANE**). | `0x4e1c70` |
| `0x4e1dc0(symbolFrame, width, tooltipKey)` | Adds an icon column. The header is frame `symbolFrame` of `eyr_symbols_strip.bmp`; the tooltip is `_EYRSYMBOL_*`; width 0 = icon width. | `0x4e1dc0` |
| `0x4e0810(title)` | **Line graph over time**, one line per country with check boxes (`eyr_btn_check_false.bmp`). The subclass's vtable slot +0x48 returns the plotted value for a country; slot +0x40 gives the y-axis label text (tech pages only). | vtables `0x5eaa24`… |
| `0x4e0090(title)` | **Category chart** (`eyr_graph_hline/vline`) with a colour legend from a palette bitmap (`income.bmp` / `expense.bmp`) and one entry per category: a line graph over time (§1.2). | `0x4e80c0`, `0x4e82f0` |
| `0x4e3da0(title)` | **Text list** (history log / text box). | `0x4e6e80`, `0x4ed3c0` |

Row cells are formatted with:

| Format | Address |
|---|---|
| `"%d"` | `0x618d88` |
| `"%.1f"` | `0x625b84` |
| `"%.2f $"` | `0x625b7c` |
| `"%.1f%%"` | `0x625b74` |
| `"%s %d"` (month name + year) | `0x6264a4` |

Field `[0x45]` is the **page id** (0..0x22), not a row count.

---

## Chapter 1 — CHAPTER_MONARCH "Monarch & Victory Progress"

| id | Ctor (vtable) | Kind | Title | Content |
|---|---|---|---|---|
| 0 | `0x4f13e0` (`0x5eb1b8`) | line graph | EYRVP "Victory Points" | value = the score (`0x4f1420`): mode `[0x647d48]` 2 → country[0x1224], 3 → [0x121A], else [0x20DE] — the same as the country-window score |
| 1 | `0x4f1460` (`0x5eb208`) | text list (520×300) | EYRHISTVP "Missions" | mission history (EYRVPCOMPLETED / EYRVPINPROGRESS) — **NIEPEWNE** |
| 2 | `0x4f1600` (`0x5eb248`) | table h=140 | EYRBASEVP "Basic Victory Points" | columns EYRVPSOURCE "Source" (w 200), EYRVPEARNED "Total" (w 100) `"%.1f"`; values are floats from country+0x20E6 onward, one per VP source (`0x4f16c0`) |
| 3 | `0x4f1bb0` (`0x5eb290`) | table h=300 | EYRPREVMON "Monarchs" | see the table below |

Page 3 columns (fill `0x4f1e80`):

| Header | Width | Content |
|---|---|---|
| EYRMONNAME "Name" | 120 | monarch name |
| EYRMONADM "Administrative" | 80 | rating word: EYRVPOOR / POOR / AVERAGE / GOOD / VGOOD / EXCELLENT, from the skill (`0x4f1df0`) |
| EYRMONMIL "Military" | 80 | rating word, as above |
| EYRMONDIP "Diplomatic" | 80 | rating word, as above |
| EYRMONSTART "Start" | 60 | start year (short +0x102) |
| EYRMONEND "End" | 60 | end year |
| EYRMONVPS "VP:s" | 40 | VP gained during the reign = monarch[+0x114] − the previous cumulative value; the current monarch is appended last |

The past-monarch list is loaded from the save file (fields parsed in `0x4f12b0` / `0x4f1aa0`).

## Chapter 2 — CHAPTER_ECONOMY "Economy"

| id | Ctor (vtable) | Kind | Title | Plotted value / content |
|---|---|---|---|---|
| 4 | `0x4e7460` (`0x5eaa24`) | line graph | EYRWEALTH "Wealth" | `0x4e74b0`: Σ this-year incomes (e[0x154..0x180]) − e[0x184] army maint − e[0x188] fleet maint − e[0x1BC] interest, where e = country[0xE74] |
| 5 | `0x4e7500` (`0x5eaa74`) | line graph | EYRPE "Infrastructure" | infra tech (country+0xE46+0x3C): level + progress, i.e. level(vf+0x34) + vf+0x30 / vf+0x24 (`0x4e7540`) |
| 6 | `0x4e75b0` (`0x5eaac4`) | line graph | EYRTE "Trade Level" | trade tech (+0x54): level + progress (`0x4e75f0`) |
| 7 | `0x4e7660` (`0x5eab14`) | line graph | EYRINFLATION "Inflation" | inflation·100 (`0x4e76a0`) |
| 8 | `0x4e76c0` (`0x5eab64`) | table h=240 | EYRINCOME "Income" | income table (below) |
| 9 | `0x4e7bc0` (`0x5eabac`) | table h=320 | EYREXPENSE "Expenses" | expense table (below) |
| 10 | `0x4e80c0` (`0x5eabf4`) | category chart | EYRINCOMES "Income" | the 12 income categories, colours from `income.bmp` (+0x504), labels `0x42c8e0` |
| 11 | `0x4e82f0` (`0x5eac40`) | category chart | EYREXPENSES "Expense" | the 16 expense categories, colours from `expense.bmp`, labels `0x42caf0` |
| 12 | `0x4e8490` (`0x5eac8c`) | table h=300 | EYRPROVINCEVALUE "Taxation & Production" | province value table (below) |
| 13 | `0x4e89d0` (`0x5eacf0`) | table h=300 | EYRTRADE "Trade Information" | trade table (below) |
| 14 | `0x4e9010` (`0x5ead38`) | table h=300 | EYRLOANS "Loans" | loans table (below) |
| 15 | `0x4e9d20` (`0x5ead80`) | table h=140 | EYRMAINTENANCE "Maintenance" | maintenance table (below) |

**Pages 8 and 9 — income / expenses** (fills `0x4e77b0` / `0x4e7cb0`):

| Header | Width | Content |
|---|---|---|
| EYRSOURCE "Source" | 128 | category name |
| EYRLASTYEAR "Last Year" | auto | `"%.2f $"`, last-year value = this-year offset − 0x70 (e+0xE4+4i for income, e+0x114+4i for expenses) |
| EYRTHISYEAR "This Year" | auto | `"%.2f $"`, e+0x154+4i (income, i = 0..11) or e+0x184+4i (expenses, i = 0..15) |
| EYRPERCENTAGE "Percentage of Total Income" / EYRPERCENTAGEE "…Expenses" | auto | `"%.1f%%"` = this year ·100 / total (total = `0x42c610` / `0x42c630`; 1 if the total is ≤ 0) |

Income categories (`0x42c7b0`), i = 0..11:

| i | Key | Text |
|---|---|---|
| 0 | INCOMETAX | Taxation |
| 1 | INCOMETRADE | Trade |
| 2 | INCOMEPRODUCTION | Production |
| 3 | INCOMEPRODFAC | Manufactory |
| 4 | INCOMEGOLD | Gold |
| 5 | INCOMEVASSAL | Vassals |
| 6 | INCOMELOAN | Interests |
| 7 | INCOMEGIFT | Events |
| 8 | INCOMESOW | Peace |
| 9 | INCOMEDTAX | Census taxes |
| 10 | INCOMEEXPORTDUTIES | Tolls |
| 11 | INCOMEHARBORFEES | Trade Tariffs |

Expense categories (`0x42c970`), i = 0..15:

| i | Key | Text |
|---|---|---|
| 0 | EXPENSEAM | Army, maintenance |
| 1 | EXPENSEFM | Fleet, maintenance |
| 2 | EXPENSEARMY | Raising Armies |
| 3 | EXPENSEFLEET | Building Fleets |
| 4 | EXPENSEFORT | Building Fortress |
| 5 | EXPENSECOL | Building Colonies |
| 6 | EXPENSETRA | Building Trading Posts |
| 7 | EXPENSEMIS | Missionaries |
| 8 | EXPENSEMER | Merchants |
| 9 | EXPENSERSR | Technology |
| 10 | EXPENSESTAB | Stability |
| 11 | EXPENSEPE | Trade & Infrastructure |
| 12 | EXPENSEMAN | Building Manufactories |
| 13 | EXPENSELOAN | Repaid Loans |
| 14 | EXPENSEINTEREST | Interests |
| 15 | EXPENSEGIFT | Events |

So the yearly accumulators are e+0x154 (12 income floats), e+0x184 (16 expense floats), and the
previous year at −0x70 from each.

**Page 12 — province value** (fill `0x4e85a0`). Rows = our provinces.

| Header | Width | Content |
|---|---|---|
| EYRPVPROVINCE "Province" | 80 | province name |
| EYRPVTAX "Taxation" | auto | `"%.2f $"`, `0x47e270(prov)` |
| EYRPVMP "Manpower" | auto | `"%.1f"`, `0x47cc80(prov)` |
| EYRPVPRODVAL "Production" | auto | `"%.2f $"`, `0x480690()`·(the previous value) — **NIEPEWNE** |
| EYRPVPRODUCTION "Resource" | auto | trade-good symbol `0x481000(prov)` |

**Page 13 — trade** (fill `0x4e8ae0`). Rows = CoTs in which we have merchants (`0x4eaa50`).

| Header | Width | Content |
|---|---|---|
| EYRCOT "Center of Trade" | auto | CoT province name |
| EYRTRADEOUR "Our Activity" | auto | `"%.1f%%"`, our share (from cot+0x20) — **NIEPEWNE** |
| EYRTRADETOTAL "Competition" | auto | `"%.1f%%"` |
| EYRTRADEVALUE "Trade Value" | auto | `"%.2f $"`, cot+0x24 |
| EYRTCOST "Placement Cost" | auto | `"%.2f $"`, `0x4378d0(cot province)` (the merchant cost) |

**Page 14 — loans** (fill `0x4e9140`). Rows = country[0xED0] loan list, for both directions.

| Header | Width | Content |
|---|---|---|
| EYRLENDER "Lender" | 120 | country name, or EYRBANK "Bank" when the id equals the bank id `0x64d4b4` |
| EYRBORROWER "Borrower" | 120 | country name |
| EYRDUCATS "Amount" | auto | `"%.2f $"`, loan+0x1A |
| EYRINTERESTPERC "Interest" | auto | `"%.1f%%"`, loan+0x16 |
| EYRINTERESTCASH "Yearly Cost" | auto | `"%.2f $"`, amount·interest·K (K `0x5e5860`, probably 0.01) |
| EYRDUE "Due" | 120 | `"%s %d"` month (table `0x62a340`) + year |

**Page 15 — maintenance** (fill `0x4e9e00`). Six rows: infantry, cavalry, artillery, warships,
galleys, transports.

| Header | Width | Content |
|---|---|---|
| EYRCATEGORY "Category" | auto | unit category |
| EYRCURRENTNUM "Current Numbers" | auto | `"%d"` |
| EYRCOST "Current Monthly Cost" | auto | `"%.1f"`: `0x459c40`, `0x459e20`, `0x45a000` (land), `0x45a1f0`, `0x45a3f0`, `0x45a5f0` (naval) |

## Chapter 3 — CHAPTER_DIPLOMACY "Diplomacy"

| id | Ctor (vtable) | Kind | Title | Content |
|---|---|---|---|---|
| 16 | `0x4e5ba0` (`0x5ea8d4`) | table h=300 | EYRFRIEND "Friendly Relations of %s" (%s = our name; formatted in `0x4e5d60`) | friendly relations table (below) |
| 17 | `0x4e6720` (`0x5ea95c`) | table h=300 | EYRNMY "Hostile Relations of %s" | hostile relations table (below) |
| 18 | `0x4e6e80` (`0x5ea9a4`) | text list | EYRALLIANCES "Military Alliance" | alliance descriptions (EYRMILALLIANCE, EYRALEXPIRY "Expires %s %d", EYRALAGAINST) |
| 19 | `0x4e7080` (`0x5ea9e4`) | text list | EYRWARS "Current Wars" | EYRWAR "War between" … |

**Page 16 columns** (fill `0x4e5d60`). Rows = the countries in `0x64e140[]` with which we have any of
these relations; 8 cells per row (`0x4e7380(font, 8)`).

| Header | Width | Content |
|---|---|---|
| EYRFRNATION "Nation" | 80 | country name |
| EYRFRVAS "Vassal" | 32 | EYRYES / EYRNOPES (EYRNOPES is a dash glyph, 0x96) |
| EYRFRMIL "Alliance" | 32 | EYRYES / EYRNOPES |
| EYRFRRM "Royal Marriage" | 32 | EYRYES / EYRNOPES |
| EYRFRTR "Trade Embargo" | auto | EYRYES / EYRNOPES |
| EYRFRMA "Military Access" | auto | EYRYES / EYRNOPES |
| EYRFRTA "Trade Agreement" | auto | EYRYES / EYRNOPES |
| EYRFRCB "Casus Belli" | 80 | `"%s %d"` expiry month + year, or EYRPERMANENT "Permanent", or EYRNOPES |

The yes/no tests are `0x4cbe90`, `0x4cbf20`, `0x4cb880`, `0x4cb860` and the relation table
country+0x38E (entry ·0x36). Which test goes to which column is **NIEPEWNE**.

**Page 17 columns** (fill `0x4e6880`, 5 cells per row):

| Header | Width | Content |
|---|---|---|
| EYRENNATION "Nation" | 80 | country name |
| EYRENWAR "War" | 32 | EYRYES / EYRNOPES |
| EYRENTRADE "Refusing Trade" | auto | EYRYES / EYRNOPES |
| EYRENCB "Casus Belli" | 80 | expiry or EYRPERMANENT |

## Chapter 4 — CHAPTER_TECHNOLOGY "Technology"

| id | Ctor (vtable) | Kind | Title | Value |
|---|---|---|---|---|
| 20 | `0x4f2bf0` (`0x5eb308`) | line graph | EYRLANDTECH "Land Technology" | land tech level + progress fraction (`0x4f2c30`); the y-axis labels are the level names (`0x4f2ca0`, `"%%000%s"` of tech vf+0x18(level)) |
| 21 | `0x4f2cd0` (`0x5eb358`) | line graph | EYRNAVALTECH "Naval Technology" | the same for naval tech (`0x4f2d10`, `0x4f2d80`) |

## Chapter 5 — CHAPTER_MILITARY "Military"

| id | Ctor (vtable) | Kind | Title | Content |
|---|---|---|---|---|
| 22 | `0x4ed4d0` (`0x5eafb0`) | table h=300 | EYRLEADERS "Leaders" | leaders table (below) |
| 23 | `0x4edd30` (`0x5eaff8`) | line graph | EYRARMYSIZE "Land Forces" | `0x4edd70` = cav + 1000·art + inf. The artillery ×1000 is as found; the unit vfuncs are +0x388 inf, +0x38C cav, +0x390 art |
| 24 | `0x4eddb0` (`0x5eb048`) | line graph | NAVYSIZE "Naval Forces" | `0x4eddf0` = warships + galleys + transports |
| 25 | `0x4ede30` (`0x5eb098`) | table h=300 | EYRARMIES "Our Current Armies" | armies table (below) |
| 26 | `0x4ee570` (`0x5eb0e0`) | table h=300 | EYRFLEETS "Our Current Navies" | fleets table (below) |
| 27 | `0x4eecb0` (`0x5eb128`) | table h=300 | EYRRECRUITS "Under Development" | recruits table (below) |
| 28 | `0x4ef990` (`0x5eb170`) | table h=120 | EYRLOSSES "Our Total Losses" | losses table (below) |

**Page 22 columns** (fill `0x4ed650`). Rows = country+0x124C leader list.

| Header | Width | Content |
|---|---|---|
| EYRLNAME "Name" | 120 | leader name |
| EYRLTYPE "Type" | 40 | symbol frame type+0x29 (general / admiral / conquistador / explorer) |
| EYRLRANK "Rank" | auto | `"%d"` |
| EYRLCOMMISION "Commission" | 130 | date |
| EYRLACTIVE "Active" | auto | |
| maneuver / fire / shock / siege | icon columns, frames 0x25..0x28, w 32 | `"%d"` each (leader +0x22.., +0x26, vf+0x10, vf+0x14) |

**Page 25 columns** (fill `0x4edf70`). Rows = units in the +0x124C list where vf+0x14 is true
(armies).

| Header | Width | Content |
|---|---|---|
| EYRANAME "Name" | 160 | unit name |
| EYRAPROV "Province" | 100 | province |
| EYRAAREA "Area" | 100 | `0x471bf7` |
| EYRAREGION "Region" | 100 | `0x471c11` |
| infantry / cavalry / artillery | icon columns, frames 0x1F, 0x20, 0x21; w 64 / 64 / 32 | counts `"%d"` |

**Page 26 columns** (fill `0x4ee6b0`): the same with vf+0x18 (fleets).

| Header | Width | Content |
|---|---|---|
| EYRFNAME "Name" | 160 | fleet name |
| EYRFPROV "Seazone" | 100 | sea zone |
| EYRFAREA "Area" | 100 | |
| EYRFREGION "Region" | 100 | |
| warships / galleys / transports | icon columns, frames 0x22..0x24, w 64 | counts |

**Page 27 columns** (fill `0x4eee40`). Rows = the country+0x1260 build queue.

| Header | Width | Content |
|---|---|---|
| EYRRECLOCATION "Location" | 80 | province |
| EYRRECAREA "Area" | 80 | |
| EYRRECREGION "Region" | 80 | |
| EYRRECINF / CAV / ART / WARSHIPS / GALLEYS / TRANSPORTS | auto | `"%d"` each |
| EYRRECREADY "Ready" | 80 | `"%s %d"` month + year (entry +0xE2) |

**Page 28 columns** (fill `0x4efaa0`):

| Header | Width | Content |
|---|---|---|
| EYRLOTYPE "Type" | 64 | unit type |
| EYRLOTOTAL "Total Losses" | 100 | `"%.1f"`, sum of the three below |
| EYRLOCOMBAT "Due to Combat" | auto | `"%.1f"`, country+0x1290+4i |
| EYRLOATT "Due to Attrition" | auto | `"%.1f"`, country+0x1274+4i |
| EYRLODIS "Due to Disbanding" | auto | `"%.1f"`, country+0x12AC+4i |

Losses notes:
- There are up to 7 types (i = 0..6). The fill emits rows starting with i = 3,4,5 and then i = 0,1,…
- The row order and the labels are **NIEPEWNE**. The combat/attrition/disband assignment follows
  the column order.

## Chapter 6 — CHAPTER_ESTABLISHMENTS "Establishments"

| id | Ctor (vtable) | Kind | Title | Content |
|---|---|---|---|---|
| 29 | `0x4ead60` (`0x5eae00`) | line graph | EYRTSIZE "Territory Size" | count of the country+0x131A province list (`0x4eada0`) |
| 30 | `0x4eadc0` (`0x5eae50`) | table h=300 | EYRTSPROVINCES "Our Non-Colonial Territory" | provinces table (below) |
| 31 | `0x4eb660` (`0x5eae98`) | table h=300 | EYRCOLONIES "Our Colonial Possessions" | colonies table (below) |
| 32 | `0x4ebf90` (`0x5eaee0`) | table h=300 | EYRSTRUCTURES "Structures" | structures table (below) |
| 33 | `0x4ecc90` (`0x5eaf28`) | table h=300 | EYRCOMPARISON "Comparison Table" | comparison table (below). It is built in this chapter's block, so it belongs here (**NIEPEWNE**: its content would suit Technology or Economy) |

**Page 30 columns** (fill `0x4eaf20`). Rows = our provinces whose settlement is not a colony.

| Header | Width | Content |
|---|---|---|
| EYRTSPROVINCE "Province" | 100 | name |
| EYRTSAREA "Area" | 80 | `0x471bf7` |
| EYRTSREGION "Region" | 80 | `0x471c11` |
| EYRTSCITY "City" | 90 | settlement+0x14 name |
| EYRTSPOP "Population" | auto | `"%.1f"`, settlement+0x7E |
| EYRTSMP "Manpower" | auto | `"%.1f"`, `0x47cc80` |
| EYRTSFORTLEVEL "Fortress" | auto | EYRFORT0..6 (`0x4eac30`) |
| EYRTSCONTROL "Control" | auto | controller |

**Page 31 columns** (fill `0x4eb7a0`). Rows = our colonies (settlement vf+0x10 true) and our TPs.

| Header | Width | Content |
|---|---|---|
| EYRCOLCAT "Category" | auto | EYRCOLCOL "Colony" / EYRCOLTP "Trading Post" |
| EYRCOLPROV "Province" | auto | |
| EYRCOLREG "Region" | auto | |
| EYRCOLCONTINENT "Continent" | auto | |
| EYRCOLSIZE "Size" | auto | `"%.1f"`: settlement+0x7E population, or TP+0x04 level |
| EYRTSFORTLEVEL "Fortress" | auto | |
| EYRCOLCONTROL "Control" | auto | |

**Page 32 columns** (fill `0x4ec100`). Rows = our provinces; each structure column shows yes or no
(keys `0x6267fc` / `0x6268ec`, probably EYRYES / EYRNOPES).

| Header | Width | Test |
|---|---|---|
| EYRSTRUPROV "Province" | auto | |
| EYRSTRUMANU "Manufactory" | auto | `0x47cb30` (order **NIEPEWNE**) |
| EYRSTRUBAILIFF "Tax Collector" | auto | `0x47cb70` |
| EYRSTRUCOURT "Chief Judge" | auto | `0x47cb50` |
| EYRSTRUCITYR "Governor" | auto | `0x47caf0` |
| EYRSTRUSHIPYARD "Shipyard" | auto | `0x47cb10` |
| EYRSTRUBARRACK "Con. Center" | auto | (last test) |
| EYRTSFORTLEVEL | auto | |

**Page 33 columns** (fill `0x4ecdd0`). Rows = countries.

| Header | Width | Content |
|---|---|---|
| EYRFRNATION | 120 | name |
| EYRINFLATION | auto | `"%.1f"`, inflation·k |
| EYRINCOMES | auto | `"%.2f $"`, e[0x1CC] − e[0x20] (**NIEPEWNE**) |
| EYRTE | auto | `"%.1f"`, trade efficiency `0x43b610`·k |
| EYRPE | auto | `"%.1f"`, production efficiency `0x43b560`·k |
| EYRLANDTECH | auto | `"%d"`, land tech level |
| EYRNAVALTECH | auto | `"%d"`, naval tech level |

## Chapter 7 — CHAPTER_HISTORY "History"

| id | Ctor (vtable) | Kind | Title | Content |
|---|---|---|---|---|
| 34 | `0x4ed3c0` (`0x5eaf70`) | text list (520×300) | EYRHISTORY "History" | the game history log |

---

## Notes

- **Page counts per chapter:** Monarch 4, Economy 12, Diplomacy 4, Technology 2, Military 7,
  Establishments 5, History 1. That is 35 pages, ids 0..34.
- **Graph value getters** (vtable slot +0x48): `0x4f1420`, `0x4e74b0`, `0x4e7540`, `0x4e75f0`,
  `0x4e76a0`, `0x4edd70`, `0x4eddf0`, `0x4f2c30`, `0x4f2d10`, `0x4eada0`. The base class samples
  these per country. The sampling and history storage are in the framework (`0x4e41b0` "eyr
  history", parent's part).
- **Surprises:**
  - Army size counts artillery ×1000.
  - EYRNOPES is a single dash glyph.
  - EYRCOMPARISON sits in the Establishments chapter.
  - The Basic VP table reads its sources straight from country+0x20E6.
