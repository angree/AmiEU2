# Windows, batch 4: recruitment, religion, event dialog, resource, loans, vassal, minor windows

Layout and rule facts only, read from the `eu2.exe` v1.09 disassembly (capstone over the raw exe)
and the Ghidra export. No code was copied. Texts are from `Config/text.csv` (the leading byte
`0xA7` in some texts is the colour escape: `§Y` yellow, `§W` white).

Conventions (as in SPEC_UI / SPEC_UI_WINDOWS3):
- Sidebar info windows are 224x344 at screen (0,103). Positions are **window-relative**. The code
  adds the window origin (`+0x12` x, `+0x16` y) everywhere; the sidebar's x origin is 0.
- Some windows draw their static text **once, into their own background surface** (`+0x62`) at
  setup, instead of every frame. This is noted per window ("pre-rendered").
- `+0x82` of every info window is the human player's country object, which also owns the main
  interface: `0x5271a0(id)` on it switches the sidebar to window id `id`.
- Text buttons are centred on x: x = cx − width/2. "centred x=112" therefore means the middle of
  the window.
- Fonts: g14 = garamond14, g14b = garamond14bold, g20b = garamond20bold, g24b = garamond24bold.
  Gold = `#FFBD00` (RGB 255,189,0). White = `0xFFFF`.

## 0. Window ids (main interface slot = `main+0x632A + 4·id`, ctor calls in `0x524840`)

| id | Ctor | Window | Evidence |
|---|---|---|---|
| 0x0B | `0x4f41e0` | iw_purchasearmy | store `0x5251c7` (`+0x6356`) |
| 0x0C | `0x4f5550` | iw_purchasenavy | `0x5251fe` (`+0x635A`) |
| 0x11 | `0x51afb0` | iw_Resource (trade good) | `0x525348` (`+0x636E`) |
| 0x13 | `0x50fa70` | iw_loans (offer a loan) | `0x5253b6` (`+0x6376`) |
| 0x15 | `0x5166e0` | iw_country (target of several "Back" buttons) | `0x525424` |
| 0x16 / 0x17 | `0x513540` / `0x514230` | iw_tech, land / naval military (see §5) | `0x52545b`, `0x525492` |
| 0x18 | `0x51a030` | iw_religion | `0x5254c9` (`+0x638A`) |
| 0x1B | `0x51e7f0` | iw_Vassal | `0x525577` (`+0x6396`) |

The full slot list read from the ctor (for reference): 0 none, 1 sea, 2 diplomacy, 3 army, 4 COT,
5 land battle, 6 siege, 7 naval battle, 8 navy, 9 own province, 0xA foreign province,
0xB/0xC purchase, 0xD treasury, 0xE colonist, 0xF `0x4fcc70`, 0x10 peace, 0x11 resource,
0x12 missions, 0x13 loans, 0x14 budget, 0x15 country, 0x16/0x17 tech, 0x18 religion,
0x19 domestic, 0x1A missionary, 0x1B vassal.

The **event choice dialog is not a sidebar window** (§3).

---

## 1. Recruitment: iw_purchasearmy / iw_purchasenavy

Both bitmaps are 224x344, 24-bit. Sidebar titles: HEAD_ARC "Army Construction" / HEAD_NAC "Navy
Construction" + province name (`0x4f4957`, `0x4f5a77`).

### 1.1 Classes

| | Ctor | Vtable | +0x04 events | +0x0C draw | +0x3C setup | +0x44 tooltip | +0x48 2nd tooltip | OK handler (`+0x7E` vtable slot 0) |
|---|---|---|---|---|---|---|---|---|
| Shared base | `0x4f3980` | `0x5eb4cc` | `0x4f3c70` | `0x4f3d90` | (`0x4f3fc0`, called by both setups) | – | – | `0x4f4190` (msg 0xB = CANCEL) |
| Army | `0x4f41e0` | `0x5eb540` | `0x4f43a0` | `0x4f47f0` | `0x4f48a0` | `0x4f5110` | `0x4f54f0` | `0x4f4980` (msg 0xA = OK) |
| Navy | `0x4f5550` | `0x5eb5b4` | `0x4f56b0` | `0x4f5950` | `0x4f59c0` | `0x4f61c0` | `0x4f6390` | `0x4f5aa0` |

Per-type vfuncs (army / navy): +0x4C load column pictures `0x4f4a30` / `0x4f5b50`;
+0x50 "order one more" `0x4f4ad0` / `0x4f5b90`; +0x54 "order one less" `0x4f4bf0` / `0x4f5cb0`;
+0x5C available row `0x4f4d40` / `0x4f5e00`; +0x60 cost row `0x4f4e60` / `0x4f5f10`;
+0x64 ordered row `0x4f4f50` / `0x4f6000`; +0x68 total `0x4f5080` / `0x4f6130`.
(+0x58 = `0x518080` is an empty stub.)

Members of the shared base:

| Member | Object |
|---|---|
| +0xAE, +0xB2, +0xB6 | column pictures (bitmap objects, 52x32) |
| +0xBA, +0xBE, +0xC2 | "up" arrow buttons `reorganisation\btn_pil_upp.bmp` (80x20 = 4 frames of 20x20) |
| +0xC6, +0xCA, +0xCE | "down" arrow buttons `reorganisation\btn_pil_ner.bmp` |
| +0xD2 | text button OK (g14b, id 0xD) |
| +0xD6 | text button CANCEL (g14b, id 0xE) |
| +0xDE, +0xE2, +0xE6 | column centre x (computed = up-arrow x + half its frame width) |
| +0xDA | army: BUILDBARRACK "Build Conscription Center" text button; navy: BUILDSHIPYARD "Build Shipyard" |
| +0xEA | army: RAISEMERC "Recruit Mercenary Company" text button; navy: the order object |
| +0xEE | army: the order object |

The **order object** is a real unit object being filled in (army ctor `0x4859c0`, 0xF8 bytes;
navy ctor `0x488900`, 0xFE bytes). Its `+0x0C` is the province; the counts are floats:

| Field | Army | Navy |
|---|---|---|
| +0xF0 | infantry (men) | transports |
| +0xEC | cavalry (men) | galleys |
| +0xE8 | artillery (cannons) | warships |

### 1.2 Layout

| Element | Position | Font / colour | Content | Evidence |
|---|---|---|---|---|
| IWPTITLE "PURCHASE UNITS" | centred (112,19) | g14b, text is yellow by escape | | `0x4f3e55` |
| Column pictures | (26,40), (86,40), (146,40) | 52x32 | army: `reorganisation\infantry%d` / `cavalry%d` / `artillery%d`, %d = tech class `0x45b8a0` clamped to ≤3; navy: `warships` / `galleys` / `transports` | `0x4f4098..0x4f40c4`, `0x4f4a30`, `0x4f5b53` |
| IWPAVIA "Units Available in Province" | centred (112,86) | g14b yellow | | `0x4f3e92` |
| Available counts | centred on column centres, y=100 | g14 white | `"%d"`: army `0x45f350` / `0x45f3e0` / `0x45f470` (inf / cav / art); navy `0x45f500` for all three (same ship count) | `0x4f4d40`, `0x4f5e00` |
| IWPCOST "Cost per 1000 men (10 cannon)" / IWPCOSTNAV "Cost per ship" | centred (112,124) | g14b yellow | | `0x4f483d`, `0x4f599c` |
| Unit costs | column centres, y=139 | g14 white | `"%d$"`: army `0x429ab0` / `0x429b00` / `0x429b50`; navy `0x429ba0` / `0x429bf0` / `0x429c40` (§1.4) | `0x4f4e60`, `0x4f5f10` |
| Up arrows | (37,156), (101,156), (162,156) | | remove one step from the order | `0x4f3ffd..0x4f4073` |
| Down arrows | (37,185), (101,185), (162,185) | | add one step to the order | `0x4f4000..0x4f4092` |
| IWPORD "Units Ordered" | centred (112,213) | g14b yellow | | `0x4f3ed2` |
| Ordered counts | column centres, y=230 | g14 white | `"%d"` of the order fields | `0x4f4f50`, `0x4f6000` |
| Total | centred (112,258) | g14b | IWPTOTAL "§WTOTAL COST: §Y" + `"%d$"` (`"%s %d$"`), total = `0x486b80` / `0x489d20` | `0x4f5080`, `0x4f6130` |
| RAISEMERC button (army only) | centred x=112, y=276 | text button g14b | | `0x4f484f..0x4f4893` |
| BUILDBARRACK / BUILDSHIPYARD | centred x=112, y=296 | text button g14b | | `0x4f3eee..0x4f3f2c` |
| CANCEL | centred x=66, y=319 | text button | → window 9 (own province) | `0x4f3f60`, `0x4f41a5` |
| OK | centred x=158, y=319 | text button | places the order, → window 9 | `0x4f3f2f`, `0x4f4996..0x4f4a1a` |

The key IWPTLIMIT "Available in Total" exists in text.csv but is never used.

### 1.3 Controls and rules

**Steps:** army infantry and cavalry ±1000 men, artillery ±10 cannons; ships ±1. "Less" stops at 0
(`0x4f4bf0`, `0x4f5cb0`). "More" is allowed only while the type's can-build test passes
(`0x4f4ad0`).

**Can build one more** (army `0x45ed80` inf, `0x45ee40` cav, `0x45ef00` art; navy `0x45efc0`,
`0x45f0a0`, `0x45f180`). Each test, for province p:
1. The country's per-type permission vfunc must be true (inf +0x2C, cav +0x30, art +0x34,
   ships +0x38; **NIEPEWNE**: unit type unlocked by tech/culture).
2. Army: avail = min(cap(p)/1000, int(manpower pool `country[0xEC0]`)) − queued(p)/1000,
   in thousands. Infantry and cavalry need avail > 0 (×1000 men); artillery needs avail×10 > 0.
   Navy: p must have a city (`prov[0x408]`) and a port (`prov[0x41C]`); avail =
   (shipcap(p) − queued(p)) / 5000 ships > 0. The navy does **not** use the manpower pool.
3. One unit's cost ≤ int(treasury `country[0xE70]`).

The **available row** shows the same numbers: army max(0, avail)·1000 men (artillery ·10);
navy max(0, avail) ships (`0x45f350`, `0x45f3e0`, `0x45f470`, `0x45f500`).

| Term | Rule | Evidence |
|---|---|---|
| cap(p) | 0 if p is 0 or ≥ 0x7E4, if p is a colony/trading post (`prov[0x40C]` ≠ 0), or if p has no city (`prov[0x408]` = 0). Otherwise int(province tax `0x47e270(p)` × 1000); 0 if the city's vfunc +0x10 is true (**NIEPEWNE**: city in revolt/occupied). The result is **halved** unless country vfunc `+0x1350`→+0x24 (p) is true (**NIEPEWNE**: p is a core/national province) or the fantasy flag `[0x8d0d7a]` is set | `0x45f260` |
| conscription centre | army cap ×2 if `0x47cb10(p)` (conscription centre in p; PURCHASEBAR_D "Increases army build capacity by x2") | `0x45edc5` |
| shipcap(p) | cap(p), ×10 if `0x47caf0(p)` (shipyard; PURCHASESHIP_D "x10"); if 0 < value < 5000 it is raised to 5000 | `0x45f500`, `0x45f024` |
| queued(p) | country vfunc +0x384(p): sum over the country's build list `[country+0x1260]` of the entries for p (vfunc +0x1C of each entry; **NIEPEWNE** that +0x384 is `0x45f310`). The order being edited is already on that list | `0x45f310` |

**OK enable** (army `0x4f449d..0x4f45a6`):
- disabled unless `0x45e4b0(inf, cav, art)` is true: ci·(inf/1000) + cc·(cav/1000) + ca·(art/10)
  < treasury (the order is affordable);
- disabled when queued(p) − order total > 0 (another order already exists in this province,
  **NIEPEWNE**);
- disabled when the order is empty.

Navy: the same with `0x45e9c0`.

**OK click** (`0x4f4996`):
- builds a command from the three counts, the province and the current date (`0x6459a4`), and
  posts it through the country (`0x4435c0`; navy `0x4436c0`);
- deletes the temporary order and returns to window 9.

Recruitment **time** (decoded later, command 0x89 → `0x4520e0` → `0x437d60`): the order date + 2 months
(infantry, `0x429800`), 3 (cavalry, `0x429820`), 4 (artillery, `0x429840`) - the slowest kind in the order;
ships (`0x438298`): warships 9 + the naval level's Low Wind months (`0x429860`, record +0x34), galleys 5, transports 6.
The order ctor calls
`0x43a590(0,0,10,1)`; this may be a time span (**NIEPEWNE**).

**Opening:**
- window 0xB from the province window's build-army button (`0x503e69..0x503e92`) when
  `0x45e540(prov,0,0,0)` (can recruit anything here);
- the right-click menu (`0x505762`, `0x505782` → 0xB / 0xC);
- a hot path in the main handler (`0x52c649` `0x45ea10` → 0xC).

The setup also stores the current province into `player+0x2282`.

### 1.4 Unit costs (`0x429ab0` … `0x429c40`)

cost = max(1, int( int(base + D) × (1 + infl) )):
- infl = inflation `country[0xE84]`, clamped ≥ 0 (`0x43c8e0`);
- D = 0 for AI countries; for a human player (player type `[0x91a738 + 4·tag]` ∈ {1,2}),
  D = difficulty setting `[*0x8c0bfc+0x2CA]` − 2 (`0x4360a0`).

With d_i = policy slider s_i − 5 (block `country+0x37E`, names as in SPEC_TIME_ECONOMY §3.1,
**NIEPEWNE**):

| Unit | base | Evidence |
|---|---|---|
| Infantry (per 1000) | 9 − 0.6·d5 + 0.4·d6 − 0.4·d7 | `0x42b580` |
| Cavalry (per 1000) | 21 − 1.4·d0 − d5 + 0.4·d6 | `0x42b5d0` |
| Artillery (per 10) | 30 + 2·d4 | `0x42b610` |
| Warship | 50 + 2·d0 + 2·d5 | `0x42b630` |
| Galley | 10 + 0.6·d5 | `0x42b660` |
| Transport | 15 + d5 | `0x42b680` |

(0.6 = 3·0.2 etc.; the constants are 0.2, 3, 5, 7, 9, 10, 15, 21, 30 and 50 at `0x5e4630..0x5e5660`.)

### 1.5 Building and mercenary buttons

**BUILDBARRACK** (army, `+0xDA`):
- enabled when `0x45fe90(p, 1)`;
- click → `0x5333d0(8)` (build building 8 = conscription centre; the rules are in SPEC_BUILDINGS);
- tooltip (`0x4f5373..0x4f54d3`), first match wins:

| Condition | Tooltip |
|---|---|
| buildable | PURCHASEBAR_I1 "Cost %d$, will take 24 months to complete." (`0x45b790`) |
| one exists already | PURCHASEBAR_IF1 |
| land tech < 41 (`0x432df0`) | PURCHASEBAR_IF2 |
| not owned and controlled (+0x1350 vf+0x24) | PURCHASEBAR_IF5 |
| a construction is running (`prov+0x22` list) | PURCHASEBAR_IF4 |
| cost > treasury | PURCHASEBAR_IF3 |

The 2nd tooltip is PURCHASEBAR_D "Increases army build capacity by x2."

**BUILDSHIPYARD** (navy):
- enabled when `0x45fe90(p, 0)`;
- click → `0x5333d0(9)`;
- tooltips PURCHASESHIP_I1 (cost `0x45b750`, 36 months), _IF1 (has one), _IF5 (base tax
  `prov[0xB6]` ≤ 6, or no port, or not owned), _IF2 (naval tech < 11, `0x432e60`), _IF4, _IF3;
- 2nd tooltip PURCHASESHIP_D "Increases ship build capacity by x10."

**RAISEMERC** (army, `+0xEA`, `0x4f4632..0x4f47d2`). Enabled only if all of these hold:
- the mercenary market `[0x64e680]` exists;
- the treasury covers the offered company's price (`0x55ba60(0x55ba30(country))`);
- a company is on offer;
- the capital's province field +0x106 is 2 (**NIEPEWNE**: the capital is in Europe);
- the recruiting province has the same +0x106 value as the capital.

Click → `0x55bb30(tag, p)` hires the company, then window 0.

Tooltips, in order:
- RAISEMERC_I4 "You don't have access to the European market of mercenaries." (either +0x106
  test fails);
- RAISEMERC_I1 "You cannot afford the %.0f …";
- RAISEMERC_I2 "There are no mercenary companies available.";
- RAISEMERC_I3 "There is a company with %d infantry, %d cavalry and %d artillery available for
  %.0f$." (company +0xF0 / +0xEC / +0xE8).

**Picture tooltips:** PURCHASEINF / PURCHASECAV / PURCHASEART (army), PURCHASEW / PURCHASEG /
PURCHASET (navy).

---

## 2. Religion window iw_religion (id 0x18)

- Class: ctor `0x51a030`, vtable `0x5ec290`.
- Methods: +0x04 events `0x51a330`; +0x0C draw `0x51a4d0`; +0x3C setup `0x51a530` (it
  pre-renders the texts); +0x40 close/apply `0x51aca0`; +0x44 tooltip `0x51ade0`; +0x48 2nd
  tooltip `0x51aea0`.
- Sidebar title: HEAD_REL "Religion" + player name.
- Opened from the country window's religion-shield button and from the province window's
  religion icon (`0x501628`).
- The setup also sets map mode 7 (`0x478950(player, 7)`; **NIEPEWNE**: the religion map).

Members:

| Member | Object |
|---|---|
| +0xB2 + 4·r | 11 sliders, one per religion r (`0x427aa0`: min −10, max +10, initial 0, step 0.1; track `sprite_slider.bmp`, knob `button area\btn_slider.bmp`, ± buttons `gfx\empty.bmp` = none) |
| +0xDE | RELWCONVERT "Convert" (text button g14b) |
| +0xE2 | RELWCLOSE "Back" (g14b, id 0xE) |
| +0xEE | RELWDEFEND "Claim: Sole Defender of the Faith" (g14b) |
| +0xEA | radio group (`btn_radio.bmp`): the conversion target, initial = state religion |
| +0xE6 | slider group (list of sliders with a common total; total = 0) |
| +0xAE | religion at setup time |

Religion ids (`0x431180`): 0 Protestant, 1 Reformed, 2 Counter-Reform Catholics, 3 Catholic,
4 Orthodox, 5 Sunni, 6 Shiite, 7 Pagan, 8 Konfucian, 9 Buddhist, 10 Hindu.
`religions.bmp` frame = id.

### 2.1 Layout (pre-rendered texts are drawn in gold `#FFBD00`)

| Element | Position | Font | Content | Evidence |
|---|---|---|---|---|
| RELWSTATE "STATE RELIGION" | centred (112,16) | g14b (**NIEPEWNE**) gold | | `0x51abfd` |
| Radio, state religion | (22,34) | `btn_radio` | selected at start | `0x51a723..0x51a745` |
| Its name | left (42,35) | g14 gold | `0x431180(state)` | `0x51a77d` |
| Radio per allowed conversion target | x=22, y = 51 + 17·k | | only religions with `0x45fc40(r)` true (table below) | `0x51a8fa..0x51a974` |
| Their names | left x=42, y = 52 + 17·k | g14 gold | | `0x51a961` |
| RELWTOLERANCE "TOLERANCE LEVELS" | centred (112,91) | gold | | `0x51ac4a` |
| State religion tolerance: name | centred (112,106) | g14 gold | | `0x51a7ec` |
| … its slider | (46,120) | slider | value = `country[0x228 + 4·state]` (float), clamped to −10..10 | `0x51a871..0x51a881` |
| Other shown religions: label | centred x=112, y = 133 + 27·k | g14 gold | name, or a group label (below) | `0x51aa4f..0x51ab33` |
| … slider | (46, 147 + 27·k) | | `country[0x228 + 4·r]` | `0x51ab96..0x51abb5` |
| RELWDEFEND | centred x=112, y=280 | text button | | `0x51a66d..0x51a6af` |
| RELWCONVERT | centred x=168, y=319 | text button | | `0x51a6b2` |
| RELWCLOSE "Back" | centred x=60, y=319 | text button | → window 0 | `0x51a6e2`, `0x51a471` |

Sliders that are not shown are moved to (−1000,−1000) (`0x51a97f`).

**Which tolerance sliders are shown** (s = state religion, r = other religion; `0x51a8e7..0x51aa40`):
- r is shown only if the religion exists (`0x4310f0(r)`: always in fantasy mode; otherwise
  0 Protestant needs world flag 1, 1 Reformed needs flag 2, 2 Counter-Reform needs flag 3 and not
  flag 4; the others always exist);
- 2 (Counter-Reform) and 7 (Pagan) are never shown;
- s ∈ {5,6} (Muslim): also hide 1, 8, 9, 10;
- s ∈ {0..4} (Christian): also hide 6, 8, 9, 10;
- s ∈ {7..10}: also hide 0, 1, 4, 6.

Group labels:
- for a non-Muslim state religion, the Sunni slider is labelled MOSLEM "Moslems";
- for s ∈ {8,9,10}, the Catholic slider is labelled CHRISTIAN "Christian".

The hidden sliders inherit their group's value when the window is closed (below).

**Slider group** (`0x427820`, called every frame): the shown sliders are linked so that their sum
stays at the group total (0). When one moves, the difference is spread evenly over the sliders not
being held (**NIEPEWNE** as a game rule; the mechanism is certain).

### 2.2 Buttons and rules

**Convert** (`0x51a345..0x51a3d8`):
- disabled when the radio selection equals the state religion, or when `0x43c000()` is true
  (byte `[country+0x38E]+0x47CC`; **NIEPEWNE**: a conversion is already pending, or a cooldown);
- click → `player[0x2264]` = target, then `0x5333d0(4)` (a command/confirmation of type 4;
  **NIEPEWNE**).

Allowed targets, `0x45fc40(r)`. "F" = only in fantasy mode (`[0x8d0d7a]`); flags are world flags
`0x4d16a0(n)`:

| State religion | Targets |
|---|---|
| 0 Protestant | 3 Catholic; 1 Reformed if flag 2 |
| 1 Reformed | 0 Protestant |
| 2 Counter-Reform | 3 Catholic |
| 3 Catholic | normally: 0 if flag 1; 2 if flag 3 and not flag 4. In fantasy mode: 0 or 7 |
| 4 Orthodox | 3, 7 (F) |
| 5 Sunni / 6 Shiite | the other one, 7 (F) |
| 7 Pagan | 4, 5, 10 (F) |
| 8 / 9 | 7 (F) |
| 10 Hindu | 7, 8, 9 (F) |

**Defender of the Faith** (`0x460340`): enabled when all of these hold:
- the state religion can have a defender (`0x4312d0`: not 7..10, plus reformation-flag
  conditions for 1/2);
- the faith has no defender yet (table `0x64d4bc`, `0x4312a0`);
- treasury ≥ 1000 (`0x5e7200`).

Click → `0x43f6f0`.
- Tooltip: one line per religion, `"%s : %s"`, religion name and current defender country
  (or NONE) (`0x51ae10..0x51ae76`).
- 2nd tooltip: RELW_DEFENDER_DESC.

**Applying tolerance** (on close, `0x51aca0`):
1. This runs only if the state religion has not changed since setup.
2. The 11 slider values are copied out. Hidden group members take their representative's value:
   - Muslim state: Reformed := Protestant, and 8/9/10 := Pagan;
   - Christian state: Shiite := Sunni, and 8/9/10 := Pagan;
   - eastern/pagan state: Protestant, Reformed and Orthodox := Catholic, and Shiite := Sunni.
3. Σ|new − old| (old = `[0x8bfa50]+0x228`) is computed. If it is > 0.99, `0x4448f0(values)` posts
   the change.

---

## 3. Events: the choice dialog (and iw_event / iw_eventinfo)

**`iw_event.bmp` (224x344), `iw_eventinfo.bmp` and `iw_eventinfo_anim.bmp` (both 360x304) are
never loaded by v1.09.** No string with these names exists in the exe.

Historical and random events with choices are shown in a **framed popup** (the `win8` frame,
`0x428810`), not in the sidebar:
- class: ctor `0x590b80` (called from `0x598406`), vtable `0x5f71a0`, base popup `0x427130`;
- methods: +0x1C build/layout `0x590c50`; +0x20 choice `0x591430`; +0x28 button tooltip
  `0x5914a0`; +0x04 events `0x592340`;
- `[this+0xB0]` = the event object.

The plain message/event window `0x56bce0` (`bgbox_event.bmp`, SPEC_UI §7) is a separate class.

### 3.1 Layout (popup-relative; built top to bottom, y accumulates)

| Step | Element | Position | Font / colour | Content | Evidence |
|---|---|---|---|---|---|
| width | text width T | T = 400; 500 if the description is > 1500 chars; 600 if > 2000. Popup width W = T + 64 | | | `0x590c71..0x590cab`, `0x590ef0` |
| 1 | Picture | x = (W − picW)/2, y = 16 | | `gfx\interface\infopics\infopic%d.bmp`, %d = event+0x2C. **The GOG data has no `infopics` folder**, so the load fails and there is no picture (**NIEPEWNE** whether any version shipped them) | `0x590efe..0x590f67` |
| 2 | Title | centred, y = picture height + 32 (**NIEPEWNE**: `0x40ec10` = height) | g20b white | event name (event+0x1A; EVENTNAME%d) | `0x590f7d..0x590fad` |
| 3 | Description | centred, y += lines·lineheight + 20 | g14 gold | event+0x16 (EVENTDESC%d), wrapped to T | `0x590fc7..0x590feb` |
| 4 | Info line | y += lines·lh + 21 | g14b white | if only action A exists and it has 1..4 commands: a generated summary of its effects (vfunc +0x28, the EE_* texts); otherwise EVENTBUTTON_INFO "(For effects on your decision, check the information appearing when you move the mouse over the button.)" | `0x591024..0x5910c0` |
| 5 | Action buttons A..D | stacked, y += lines·lh + 5, then each button adds height + 4; width T, centred | text buttons (`0x423ae0`, ids 0..3) | label = the action's name (event+0x56 / +0x6C / +0x82 / +0x98); if empty, text key `EVENTACT<id>A` / `B` / `C` / `D` (id = event+0x0E). A button exists only if that action has commands (event+0x44 / +0x5A / +0x70 / +0x86 lists) | `0x5910d8..0x5912a4`, `0x5932b0..0x5933a6` |
| 6 | Single button | if only one button exists, its id is set to 0xD (default/Enter, **NIEPEWNE**) | | | `0x5912a8..0x5912c4` |
| 7 | Frame size | W x (((y + 15) & ~15) + 16) | | `0x428810(0, W, h, 16)` | `0x5912ce..0x5912e1` |
| 8 | Player shield | (W − shieldW − 14, 12) | `smallshield_<tag>` (`0x43b1d0`), 4 frames, `shield palette.bmp` | top-right corner | `0x591340..0x5913c3` |

Eight text boxes are created (`[this+0x6C..0x88]`, each T x 320). Only +0x70 (title), +0x74
(description) and +0x78 (info) are used in the layout above. Popup screen position: the default
popup placement (**NIEPEWNE**: centred/cascaded as in SPEC_UI §1).

### 3.2 Behaviour

- **Hovering a button** (`0x5914a0`) builds the effect list of that action from its command list:
  one line per command, each from a text key (`0x591538..0x592139`). Examples: EE_CASH "%s%d$",
  EE_STABILITY "Stability: %s%d", EE_MANPOWER, EE_LANDTECH, EE_INFLATION, EE_REVOLT texts,
  EE_GAIN_BUILDING / EE_LOSE_BUILDING (with the EYRSTRU* building names), EE_TRIGGER "Trigger the
  '%s' Event.", EE_WAR, EE_LOAN, EE_TECH (TECH_LATIN … TECH_EXOTIC), EE_INDY, and others.
- **Clicking button i** (`0x591430`) records the player as the deciding country (event+0x12) and
  executes action i (`0x594d80(i)`); the dialog closes.
- **Pause:** if `0x4196c0()` is false and `[0x8d0d7c]` is 0, the ctor sets `player+0x2306 = 1`
  (**NIEPEWNE**: the game is paused while the dialog is open). The dtor clears it.
- **Automatic choice:** if `[0x8d0d7c]` is set and buttons exist, the events handler picks action
  A at once (`0x592351..0x592373`; **NIEPEWNE**: automatic/observer/network mode).
- The history log line afterwards is EVENT_SELECTION "%s : We went with %s in %s." (or
  EVENT_SIMPLE "%s : %s happened to us." for events without choices) (`0x5951b8`, `0x5951e6`).

---

## 4. Trade good window iw_Resource (id 0x11)

- Class: ctor `0x51afb0`, vtable `0x5ec314`.
- Methods: +0x04 events `0x51b130`; +0x0C draw `0x51b2d0`; +0x3C setup `0x51b330`; refresh
  (pre-renders everything) `0x51b3a0`; +0x44 tooltip `0x51bca0`; +0x48 2nd tooltip `0x51bf70`.
- Sidebar title: HEAD_RES "Resource Information" + the good's name.
- The good is that of the selected province `[0x61f418]` (`0x481000(prov)` = trade-good object
  G).
- Opened by the resource icon of the province windows (window id 0x11) and from the main handler
  in some map modes (`0x52722b`, `0x52c44c`).
- The setup sets map mode 2 (`0x478950(player, 2)`; **NIEPEWNE**: the trade-goods map).

| Element | Position | Font / colour | Content | Evidence |
|---|---|---|---|---|
| Good name | centred (112,19) | g24b gold | G+0x14 text key | `0x51b501..0x51b539` |
| Good picture (`+0xB2`) | centred x=112, y=37 | 21-frame strip from G+0x136 (`0x40eec0`), frame = good id G+4 | | `0x51b410..0x51b463`, `0x51b2f6` |
| Labels | left x=26 | g14 gold | rows below | |
| Values | right-aligned x=194 | g14 white | rows below | |

| y | Label | Value (RW_NA "N/A" for gold, good id 2) |
|---|---|---|
| 90 | RWBASIC "Basic Price (100 units):" | `"%d$"` G+0x114 |
| 104 | RWSUPPLY "Supply:" | `"%d%%"` 100·G+0x11C |
| 118 | RWDEMAND "Demand:" | `"%d%%"` 100·G+0x120 |
| 132 | RWCURRENT "Market Price (100 units):" | `"%.2f$"` G+0 |
| 166 | RWPROD "PRODUCTION" (centred x=112, g14b white) | |
| 180 | RWOURP "Our Production:" | RWUNITS "%d Units" = 100·`player[0xF06 + 4·good]` |
| 194 | RWPE "Infrastructure:" | `"%d%%"` 100·production efficiency `0x43b560` |
| 208 | RWPINC "Our Production Income:" | `"%.1f$"` units × efficiency × price; for gold 12·`[player+0xE74]+0x14` |
| 222 | RWLEADING "Leading Nation:" | country with the largest `[0xF06 + 4·good]`, else RWNONE "(none)" |
| 254 | RWTRADE "TRADE REVENUES" (centred, g14b white) | |
| 269 | RWOURT "Our Trade:" | RWUNITS, 100·`player[0xF5A + 4·good]` |
| 283 | RWTE "Trade Level:" | `"%d%%"` 100·trade efficiency `0x43b610` |
| 297 | RWTINC "Our Trade Income:" | `"%.1f$"` trade units × trade efficiency × price |

- **RWBACK "Back to Main"** (`+0xAE`): centred x=112, y=319 → window 0.

**Tooltips** (row y bands from 90; `0x51bca0` / `0x51bf70`):
- gold: RW_I_GOLD / RW_D_GOLD for the whole window;
- 90–103: RW_I_BASICPRICE "%s is a '%s' value resource." The value word is chosen by the basic
  price: ≤ 5 RW_I_PRICE_L, 6–10 _M, 11–15 _H, > 15 _VH;
- 104–117: RW_I_SUPPLY / RW_D_SUPPLY ("Currently, %d provinces … produce %s");
- 118–159: RW_I_MAIN;
- 166–193: RW_I_PROD / RW_D_PROD;
- 194–207: RW_I_INFRA / RW_D_INFRA;
- 208–221: RW_I_OURPI / RW_D_OURPI;
- 222–235: RW_I_LEADER / RW_D_LEADER ("The leading nation gets %d VP each year.");
- 254–282: RW_I_OTRADE / RW_D_OTRADE;
- 283–296: RW_I_TRADE / RW_D_TRADE;
- 297–310: RW_I_OURTI / RW_D_OURTI.

**Map clicks** while the window is open (`0x51b176..0x51b2b3`, map mode 2 or 3, map-click event
0xE). For a land province (`prov[0xCA]` < 5) known to the player (byte +0x256 + player tag):
- if it produces the same good, the window refreshes for it;
- otherwise the map goes to mode 3 and window 0xA (foreign province) opens (**NIEPEWNE** about
  the intent).

---

## 5. Military overview: iw_military

**`iw_military.bmp` (224x344) is never loaded.** The military overview is the tech windows
(ids 0x16 land / 0x17 naval, ctors `0x513540` / `0x514230`, bitmap `iw_tech`). They show MILLAND
"LAND MILITARY" / MILNAVAL "NAVAL MILITARY", shock/morale/fire pips, supportable amount, unit
totals, maintenance cost and the maintenance slider (0.5..1.0). These are already decoded in
`layouts_iw.md` (table at "Title (112,20)…"). No new class.

---

## 6. Minor windows

### 6.1 iw_loans: offer a loan (id 0x13)

- Class: ctor `0x50fa70`, vtable `0x5ebe38`.
- Methods: +0x04 events `0x50fd40`; +0x0C draw `0x50fec0`; +0x3C setup `0x510250`; button
  handler `0x510400`.
- Opened by the diplomacy action Offer Loan (0x11 → window 0x13, layouts_iw2_dip.md).
- Sidebar title: HEAD_LOA "Loans". The target is `player+0x2308`.

Values (member, step, range, default):

| Member | Value | Step | Range | Default |
|---|---|---|---|---|
| +0xCA | amount | 10 | 1 .. treasury/2 (recomputed every frame) | 100 |
| +0xCE | interest % | 1 | 1 .. 10 | 20, so it is clamped to 10 on the first frame (**NIEPEWNE**: an original bug) |
| +0xD2 | length in months | 1 | 1 .. 300 | 36 |

The defaults are set in `0x5103ba`.

| Element | Position | Font | Content | Evidence |
|---|---|---|---|---|
| LWTITLE "You are about to offer a loan to" | centred (112,11) | g14 white | | `0x50ff27` |
| Target name | centred x=112, y = 8 + g14 line height | g24b white | | `0x50ff40..0x50ff71` |
| Amount value | centred (136,65) | g24b white | `"%d"` | `0x51001b` |
| LWAMOUNT "AMOUNT" | centred (136,106) | g14b white | | `0x50ff94` |
| Rate value | centred (136,131) | g24b | `"%d %%"` | `0x51005e` |
| LWRATE "INTEREST RATE" | centred (136,174) | g14b | | `0x50ffcc` |
| Length value | centred (136,199) | g24b | `"%d"` + LWMONTHS "MONTHS" | `0x5100a6` |
| LWLENGTH "LENGTH IN MONTHS" | centred (136,244) | g14b | | `0x510004` |
| Up arrows `btn_pil_upp` | (180,58), (180,124), (180,192) | | + step | `0x510303..0x510357` |
| Down arrows `btn_pil_ner` | (180,80), (180,146), (180,216) | | − step | `0x51035a..0x5103b7` |
| `anim_value.bmp` (312x52 = 6 frames of 52x52) | (20,58) amount, frame = min(amount/50, 5); (20,124) rate, frame = min(6·rate/10, 5) | | coin piles | `0x51012a..0x5101ca` |
| `timglas.bmp` (432x59 = 12 frames of 36x59 hourglass) | (34,187) | | frame = min(12·months/300, 11) | `0x5101d9..0x510227` |
| Send button = BUTTON_OFFER_LOAN (buttons-table id 0x39) | centred x=112, y=272 | | disabled if amount < 1 or `player[0xE98]` < 1 (**NIEPEWNE**: no diplomat available) | `0x510274..0x5102c5`, `0x50fe26..0x50fe9b` |
| BACK | centred x=112, y=302 | text button | → window 2 (diplomacy), or 0 when `player[0x2274]` is set | `0x5102c8..0x510300`, `0x50fd86` |

Send (`0x510416`): `0x440740(target tag, amount, rate, months)` posts the offer, then window 2
(or 0).

### 6.2 iw_Vassal: "Grant Independence" (id 0x1B)

- Class: ctor `0x51e7f0`, vtable `0x5ec40c`.
- Methods: +0x04 events `0x51e9d0`; +0x0C draw `0x51eae0`; +0x3C setup `0x51eca0`.
- Opened by COW_VASSAL "Create Vassal" in the country window. It lets the player release a
  country, chosen from a list, as a new vassal.
- Sidebar title: HEAD_VASSAL "Grant Independence" + player name.

| Element | Position | Font | Content | Evidence |
|---|---|---|---|---|
| VASSAL_TIT1 "Select Vassal" | centred (112,18) | g24b white | | `0x51ec22` |
| VASSAL_TIT2 "to create" | centred (112,36) | g24b white | | `0x51ec6b` |
| Candidate list (`+0xAE`) | (20,82), 160x179 | list box | filled by `0x43d0d0` with filter `0x43d8c0` (**NIEPEWNE**: the tags that have cores in the player's provinces and do not exist) | `0x51ecce..0x51ed94` |
| Selection line | centred (112,296) | g14b white | VASSAL_NONE "Nothing selected" or VASSAL_CURRENT "Create: %s" (name `0x469750`) | `0x51eb18..0x51ebd9` |
| VASSAL_GRANT "Grant" (`+0xB6`) | centred x=168, y=319 | text button | disabled when nothing is selected or the tag already exists (`[0x64e140 + 4·tag]` ≠ 0); click → `0x442470(tag)`, then window 0 | `0x51ea48..0x51eac5`, `0x51ed97` |
| BACK (`+0xBA`) | centred x=60, y=319 | | → window 0x15 (country) | `0x51e9f7` |

A confirmation popup class sits next to it:
- ctor `0x51ee90`, vtable `0x5ec458`;
- 320 px wide at screen x = screenW/2 − 160;
- texts DWMLORD "Please Confirm" (g24b), DWAREYOUSURE1 / 2 "(Effects are immediate and / cannot
  be undone.)", OK / CANCEL.

Whether Grant goes through this popup is **NIEPEWNE**: `0x442470` is called directly in the
window code.

### 6.3 iw_eco_aid

**Never loaded** (no string in the exe). Economic aid exists only as the diplomacy actions Send
Gift / Personal Gift / Offer Loan (layouts_iw2_dip.md §3).

### 6.4 iw_mecenatplacement: "select a province" prompt

This is not a window of its own. The info-window base ctor `0x5148a0` creates, for every info
window:
- `+0x8A`: a 224x344 surface with `iw_mecenatplacement.bmp`, with MECENATPLACE "SELECT A LEGAL
  (GREEN) PROVINCE ON THE MAP TO SEND A COLONIST OR TRADER TO" pre-rendered;
- `+0x8E`: the same with MECENATPLACE2 "SELECT A PROVINCE WHICH YOU OWN ON THE MAP TO SEND YOUR
  MISSIONARY TO".

Text: text box 146x230, g14b gold, at x=34, vertically centred on y=150 (`0x5149cc..0x514b3d`).

The colonist window draws `+0x8A` over itself at (0,0) while no target is chosen (`0x4fbd71`);
the missionary window draws `+0x8E` (`0x4fdaca`). The same base also loads `prickar.bmp` (40x6,
5 frames) for the rating pips (`0x514c50`). MECENATROLLOVER "Settler from %s en route to %s" is a
map tooltip (`0x41cd53`).

### 6.5 iw_drlw / iw_draw (diplomacy)

Already decoded in `layouts_iw2_dip.md` (merged into `layouts_iw2.md` / SPEC_UI §5). Summary:
- one object (ctor `0x50a3b0`, vtable `0x5ebd9c`, window id 2);
- `iw_drlw` = the sidebar overview with radio View Alliances / View Relations, relation and
  nation lists, Back to Main;
- `iw_draw` = a second 224x344 action panel at **screen (224,103)**, right of the sidebar over
  the map. It is opened when a foreign country is chosen and lists the diplomatic actions with
  their costs and conditions, plus the confirm popup.

---

## 7. Open points

1. Recruitment time and where the order's completion date is set (the order object `0x4859c0` /
   `0x488900`, or the command posted by `0x4435c0`).
2. The meaning of the `+0x106` province field used by the mercenary test (Europe?), and of the
   country permission vfuncs +0x2C..+0x38.
3. `0x43c000` (Convert disabled), and command type 4 sent by Convert (`0x5333d0(4)`).
4. The popup screen position of the event dialog, and whether `infopics` ever existed.
5. The vassal candidate filter `0x43d8c0`.
