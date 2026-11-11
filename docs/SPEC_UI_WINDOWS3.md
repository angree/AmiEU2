# Windows, batch 3: frontend VICTORY and OPTIONS popups, battle window, peace window

This file records layout and rules only, read from the `eu2.exe` v1.09 disassembly and the Ghidra
export. No code was copied.
- The frontend popups use **screen coordinates** (800x600 frontend).
- The battle and peace sections were merged from `windows3_battle.md` and `windows3_peace.md`.

## 1. Frontend VICTORY popup (`0x58d2f0`; draw `0x58e120`; events `0x58db20`)

| Element | Position | Font / bitmap | Notes | Evidence |
|---|---|---|---|---|
| Header FEVIC_HEADER "VICTORY CONDITIONS" | left (262,128) | g14b, colour `0xFFFF` (white, **NIEPEWNE**) | | `0x58e16d` |
| Radio buttons ×4 | x = 246; y = 159, 199, 239, 279 | `gfx\settings\btn_radio.bmp` (4-frame button), group `0x427700`, index 0..3 | initial selection = `[0x647d48]` (`0x58d6e1`) | `0x58d4e3..0x58d5cf` |
| Mode names | left x = 262; y = 161 + 40·i | g14b; white if selected, grey `#808080` otherwise | FEVIC_STANDARD / SUPREMACY / DEATHMATCH / MISSION | `0x58e182..0x58e2f3`, `0x58e220` |
| Description boxes ×4 | x = 378; y = 161 + 40·i; 322x40 each | text box | FEVIC_STANDARD_DESC "Most victory points at the end of the Scenario wins." / SUPREMACY_DESC / DEATHMATCH_DESC / MISSION_DESC | `0x58d60d..0x58d6d2` |
| Edit box 1 (Standard: **end year**) | field at x = w1 + 394, y = 157, max 4 characters, default "1819"; frame `gfx\interface\frontend\editbox.bmp` (2 frames) at (w1 + 388, 155) | text g18b, colour escape `%777` | w1 = width of the Standard description text, capped at 322 (the box sits right after the text) | `0x58d6e6..0x58d7d8` |
| Edit box 2 (Supremacy: **target VP**) | field at x = w2 + 394, y = 197, 3 characters, default "500"; frame at (w2 + 386, 195) | g18b | | `0x58d804..0x58d8d2` |
| Edit box 3 (Deathmatch: **number of new provinces**) | field at x = w3 + 394, y = 237, 3 characters, default "500"; frame at (w3 + 386, 235) | g18b | | `0x58d8fe..0x58d9cb` |
| Mission mode | no edit box | | | |
| BACK FEBTN_BACK | centred x = 510, y = 305 | `GFX\interface\frontend\btn_frontend.bmp` (text button) | in multiplayer the popup has two buttons instead, at (374,305) and (520,305) (**NIEPEWNE**) | `0x58d3a7..0x58d3fc` |

**Values:**
- The numeric fields are read with atoi and clamped to **10..750**, then written back (`0x58dc0a..0x58dc1d`
  and the following repeats).
- The text buffers are `0x8ce7fc` / `0x8ce914`, plus the year field (**NIEPEWNE** which buffer
  belongs to which mode).

**Storage:**
- The chosen mode goes to **`[0x647d48]`** = 0 Standard, 1 Supremacy, 2 Deathmatch, 3 Mission
  (radio index).
- The three values are passed to `0x58e340(…, year, vp, provinces, mode)` (also the multiplayer
  sync entry), which copies them back into the edit boxes and sets `[0x647d48]`.
- Their final globals were not traced (**NIEZNANE**). The log texts FEVIC_*_LOG print them ("to
  year %d", "%d Victory Points", "%d Provinces").

## 2. Frontend OPTIONS popup (`0x58aaa0` ctor / `0x58ac20` setup / `0x58b402` OK handler / `0x58b7ec` draw)

| Element | Position | Notes | Evidence |
|---|---|---|---|
| Panel `gfx\interface\frontend\box_options.bmp` | (238,146) | | `0x58ab0f..0x58ab30` |
| Header FEOPT_HEADER "OPTIONS" | left (246,128), g14b, `0xFFFF` | | `0x58b82f` |
| Help text box | (246,150), 224x118 | text box `0x41f5b0(0xE0, 0x76)` | `0x58acb8..0x58acda` |
| OK button FEOPT_OK | centred x = 510, y = 306 (`btn_frontend.bmp`); multiplayer pair at (374,305) / (520,305) | | `0x58ab5a..0x58abb2`, `0x58ac51..0x58ac85` |
| Option rows | one row widget each (`0x58b950` → `0x57c570`): label (g14b) + a radio set of value names (`btn_radio.bmp`) | row placement inside the panel **NIEZNANE** (computed by the row widget, not traced) | `0x58ad3e..0x58b387` |

The options, in display order. All are stored in the settings block `*[0x8c0bfc]`; the OK handler
copies 12 dwords from +0x2C6 (`0x58b6db..0x58b6e5`).

| # | Label | Values (text keys) | Settings field |
|---|---|---|---|
| 0 | FEOPT_DIFFICULTY "DIFFICULTY" | 5: FE_DIFFI5..1 "Very Easy, Easy, Normal, Hard, Very Hard" | +0x2CA |
| 1 | FEOPT_AIAGGRE "A.I. AGGRESSIVENESS" | 5: FEOPT_AI_LEVEL1..5 "Coward, Weakling, Normal, Aggressive, Furious" | +0x2C6 |
| 2 | FEOPT_GAMESPEED "GAME SPEED" | 5: FEOPT_GAMESPEED2..6 (Below Normal … Very Fast); the shown index is the field − 2, clamped 0..4 | +0x2CE |
| 3 | FEOPT_FOGOFWAR "FOG OF WAR" | OFF / ON | +0x2D2 |
| 5 | FEOPT_CONQCAP "FORCED ANNEXATION" | OFF / ON | +0x2D6 |
| 6 | FEOPT_MISSIONS "DYNAMIC MISSIONS" | OFF / ON | +0x2DA (the mission offers check this, SPEC_MISSIONS) |
| 7 | FEOPT_BASEVP "BASE VICTORY POINTS" | OFF / ON | +0x2DE (VP are added only when on, SPEC_MISSIONS §2.1) |
| 8 | FEOPT_AUTOSAVE "AUTOSAVE" | 5: Never, Yearly, every 3 / 5 / 10 years | +0x2EA |
| 9 | FEOPT_GAMEEVENTS "GAME EVENTS" | None / Random only / Historical only / Both (row built by `0x43e1d0`, **NIEPEWNE**) | +0x2E2 or +0x2E6 (**NIEPEWNE**) |
| – | one more ON/OFF row with id −1 | **NIEZNANE** | `0x58b33c..0x58b387` |

The setting row ids pushed to `0x58b950` are 0, 1, 2, 3, 5, 6, 7, 8 (id 4 unused).


---

# 3. Battle window (merged from windows3_battle.md)

# Battle window (iw_battle): land, naval, siege

This file records layout facts only, read from the `eu2.exe` v1.09 disassembly. No code was
copied. Positions are window-relative (224x344 at screen (0,103)).

## Classes

| Variant | Ctor | Vtable | Draw (+0x0C) | Setup (+0x3C) | Tooltips (+0x44 / +0x48) | Unit column (+0x54) | Sidebar title |
|---|---|---|---|---|---|---|---|
| Land | `0x4ff110` | `0x5eb8c8` | `0x4ff240` | `0x4ff2d0` | `0x4fee50` / `0x4fed90` | `0x4ff340` | HEAD_LAN "Land Battle" + province |
| Naval | `0x4ff550` | `0x5eb928` | `0x4ff7f0` | `0x4ffb20` | shared | `0x5000d0` | HEAD_NAV "Naval Battle" |
| Siege | `0x500250` | `0x5eb988` | `0x500850` | `0x500ac0` | `0x500e90` / `0x501030` | `0x500ce0` | HEAD_SIE "Siege" |

- **Shared base:** ctor `0x4fdbb0`. It loads the background `iw_battle.bmp` (224x344) and the
  members below. The shared draw is `0x4fdf60` + `0x4fe110`.
- **Battle object:** `[this+0xAE]`. Its sides are +0x30 = **attacker** (drawn on the left) and
  +0x34 = **defender** (drawn on the right); RATIO_D: "The attacker's numbers are shown to the left
  the defender's to the right".

Base members:

| Member | Bitmap |
|---|---|
| +0xB6 | country shield |
| +0xBA | background |
| +0xBE | `reorganisation\attrition.bmp` (144x12, 4 frames) |
| +0xC2 | `fire.bmp` (36x14) |
| +0xC6 | `shock.bmp` (36x14) |
| +0xCA | `crb\winner_gauge.bmp` (720x40 = 9 frames of 80x40) |
| +0xCE / +0xD2 / +0xD6 | three unit-type pictures, reloaded per variant by vtable +0x4C (`0x4fe730` land / `0x4ffff0` naval) |

## 1. Common layout (land and naval)

| Element | Position | Content / rule | Evidence |
|---|---|---|---|
| Winner gauge | (72,11) | `winner_gauge` frame 0..8 from the strength ratio attacker/defender (`0x4fecc0`): ≥4 → 0, ≥3 → 1, ≥2 → 2, ≥1.5 → 3, ≥0.66 → 4, ≥0.5 → 5, ≥0.33 → 6, ≥0.25 → 7, else 8. Tooltip RATIO0..8 "Strength Ratio >4:1 … <1:4" and RATIO_D | `0x4fe40d..0x4fe516`, `0x4fee88` |
| Attacker shield | (14,11) | small shield of the side's country, `shield palette.bmp`, 4 frames, frame 0 | `0x4fe7f7..0x4fe892` |
| Defender shield | (150,11) | as above | same |
| Attrition icon | attacker (68,56) / defender (120,56) | `attrition.bmp` frame 0..3: 3 = storm (sea province byte +0x248) or the side's vfunc +0x18 false with `0x48e930` false; 2 = hard terrain class 4/6 or other cases; 1 = winter/ice (+0x488) or attrition > 0; 0 = none (**NIEPEWNE** mapping) | `0x4fe895..0x4fe9a7` |
| Attrition value | centred (90,56) attacker / (142,56) defender | `"%.0f"` side attrition × 100, g14 white | `0x4fe9aa..0x4feaf4` |
| Battlefield background | (20,74), 184x98 | `crb\combatbg_<name>.bmp`: `assault` if the battle type +0x24 = 5; otherwise by province terrain class `prov[0xCA]`: 0 plains1, 1 forest1 (forestwinter1 in winter `0x481910`), 2 mountain1 / mountainwinter1, 3 desert1, 4 marsh1; anything else (sea) sea1 | `0x4fe5cc..0x4fe6f5` |
| Unit sprites on the battlefield | land: defender at (88,81), attacker at (84,98) | the side's lead-unit sprite strip (unit graphics +0x0C, frames +0x4E/+0x52, e.g. `crb\T-Armystripp L-n`) | `0x4ff28f..0x4ff2b9`, `0x4fdff0` |
| Phase icon | attacker bottom-left (20,158), defender top-right (168,74) | `fire.bmp` in the fire phase (battle byte +0x13 set), else `shock.bmp`. Tooltips CRBFIRE / CRBSHOCK | `0x4fe175..0x4fe27d` |
| Phase share | attacker right-aligned @43, y=159; defender right @199, y=75 | `"%d"` = int(10 × side power / (attacker + defender)), from vfunc +0x18 (fire) or +0x1C (shock); g14b white | `0x4fe1d1..0x4fe3a4` |
| Roll difference | right @59, y=159 | `"+%d"` / `"%d"` = attacker roll − defender roll (vfunc +0x30 fire / +0x2C shock); g14b white. Tooltips CRB_ROLL_AFIRE / ASHOCK / DFIRE / DSHOCK + CRB_ROLL_DADA | `0x4fe3a9..0x4fe408`, `0x4feeea..0x4fef74` |
| Morale value | centred attacker (60,175) / defender (164,175) | morale text (`0x48b3b0`), g14 white | `0x4fe9d4..0x4feab7` |
| Morale bar | attacker (20,192), defender (120,192), 83x6 | filled to side morale / highest max morale of the countries involved (`0x4feb40`, `0x4febe0`) | `0x4fea48..0x4feb2e` |
| Unit-type pictures | (86,208), (86,244), (86,279), 52x32 each | land: `infantry%d` / `cavalry%d` / `artillery%d` (%d = tech class 1..3, `0x45b8a0`); naval: `warships` / `galleys` / `transports` | `0x4fe556..0x4fe58c`, `0x4fe752..0x4fe7ad` |
| Unit counts | centred at x=50 (attacker) / 174 (defender); y = 217, 253, 288 | land: infantry `0x48f880`, cavalry `0x48f8c0`, artillery `0x48f900`. Naval: warships, galleys, transports. g14 white. Land cavalry is **grey `#A4A4A4`** when `prov[0xCA]` ∈ {1, 2, 4} (forest, mountain, marsh); artillery grey when `prov[0xCA]` = 4 | `0x4ff3c2..0x4ff508`, `0x50014a..0x50020d` |
| Advantage icon | centred x=50 / 174, y=304 | land: `crb\advantage_cavalry.bmp` (36x14) when the side has the cavalry advantage (vfunc +0x7C); naval: `crb\advantage_wind.bmp` | `0x4ff357..0x4ff399`, `0x50011f` |
| Leader | — | tooltips CRBLEADERNAME / CRBLEADEFIRE / CRBLEADERSHOCK / CRBLEADERMOVE exist; where the leader is drawn was not located (**NIEZNANE**) | |

Tooltip zones for the unit rows (`0x4fefa5..0x4ff0a9`), x 86..137:

| y | Land | Naval |
|---|---|---|
| 208..237 | CRBINF | CRBWAR |
| 238..274 | CRBCAV | CRBGAL |
| 275..310 | CRBART | CRBTRA |

**Naval differences:**
- **Sprites:** the two ship sprites (+0xDE attacker, +0xE2 defender) are placed from position
  tables in memory (`0x8abfc8`, `0x8ac068`, `0x8ac088`, `0x8abfe8`), chosen by ship count and
  direction.
- **Smoke:** the animations `T-NAVY A-SMOKE D-SE.bmp` / `D-NW.bmp` (3240x60, 8-bit) play over them
  (`0x4ffb20..0x4fff25`).
- Where those tables are filled from is **NIEZNANE**.

## 2. Siege variant

Extra members:
- +0xDE `crb\siege_levels.bmp` (2208x98 = 12 frames of 184x98);
- +0xE2 `crb\breach.bmp` (40x21);
- +0xE6 `crb\siege_ratings.bmp` (212x32);
- +0xEA `siege_lock.bmp` (32x32).

| Element | Position | Content | Evidence |
|---|---|---|---|
| Siege picture | (20,74), replaces the battlefield | `siege_levels` frame 0..11 = siege state (vfunc +0x84 of the siege, clamped to 11) | `0x500915..0x50095d` |
| Siege rating strip | (14,127) | `siege_ratings`; tooltip CRBSIEGE1..6 by the siege value (≤ −5 "can hold out for years" … "will surrender any day now"; steps at 3, 5, 9; **NIEPEWNE** exact thresholds) | `0x500960..0x500982`, `0x500ed0..0x500f56` |
| Siege values | centred (22,133) attacker / (208,133) defender | `"%d"` (side vfunc +0x34: siege strength / fort value, **NIEPEWNE**), g14b white | `0x5009d3..0x500a74` |
| Shields, morale etc. | as in §1 | the shared per-side draw `0x4fe7c0` | `0x500985..0x50099a` |
| Breach | (150,149) | shown if the fortress is breached (`0x471590`); tooltip CRBBREACH | `0x5009a1..0x5009d0`, `0x500f7d` |
| Lock | (194,164) | `siege_lock.bmp` if province byte +0x489 is set (fort locked / blockaded, **NIEPEWNE**) | `0x500a79..0x500aa0` |
| Garrison column | defender side: "GARRISON" centred at (x,203), counts at y = 217 / 253 / 288 | **grey `#808080`**, g14 | `0x500ce0..0x500e72` |
| Buttons | y=319, centred x = 38 / 112 / 186 | BUTTON_ASSAULT (17, tooltip BUTTON_0088 "Assault Enemy Fortress"; ASSAULT_NOINF without infantry), BUTTON_DETACH_SIEGE (18, BUTTON_0089), BUTTON_CREATE_COVER (19, BUTTON_0090) | `0x500ac0..0x500bf7` |

- The siege picture and values are drawn only when the battle type +0x24 is 3, 4 or 5 (siege /
  assault, `0x5008fb..0x50090b`).
- When the besieged city no longer has a siege, the window switches back (`0x50087c`).

## 3. When it opens

The window ids in the main interface are **5 = land battle**, **6 = siege**, **7 = naval battle**
(`main+0x632A + 4·id`: `0x633E`, `0x6342`, `0x6346`; evidence `0x206255..0x206273` in the export).

**Selecting units** (`0x530d60`):
1. When the selected unit stack is in a battle (unit +0x48 or +0x4C) and both sides still have
   units, the naval battle (id 7) opens if the selection is a fleet; otherwise the land battle
   (id 5).
2. If the unit is besieging or is besieged (`0x48df30` / `0x48df10`), the siege window (id 6)
   opens.
3. Otherwise the normal army (3) or fleet (8) window opens.

**Clicking a province** whose city is under siege also opens id 6 (`0x527490`, `0x208568`).

**After a battle ends:** there is no separate result screen. Once one side has no units, the next
refresh of the selection drops back to the army or fleet window. Battle results are reported
through messages.

---

# 4. Peace window (merged from windows3_peace.md)

## 4. Peace window (`iw_peace.bmp` / `iw_peace2.bmp`)

Layout and control facts only, from the `eu2.exe` v1.09 disassembly. No code was copied.
- The window is a normal **sidebar window**, 224x344 at screen (0,103). It is not a second panel.
- Coordinates are window-relative.
- It is opened from the diplomacy action Offer Peace (window id 0x10). The sidebar title is
  HEAD_PEA "Peace Resolution" (`0x510ad9`).

Classes and methods:
- ctor `0x5105c0`, vtable `0x5ebecc`;
- +0x04 per-frame enable logic `0x511b90`;
- +0x0C draw `0x512960`;
- +0x3C setup `0x510aa0`;
- +0x40 free offer `0x510e80`;
- +0x44 tooltips `0x5132e0`;
- click handling `0x5121e8..0x51292d`.

The enemy is `[this+0xAE]`. The offer object is `[this+0xC4]` (0x3F6C bytes): tribute money at
+0x3F54, a percentage at +0x28, province list count +0x2C, provinces +0x34[], receivers
+0x1FC4[], demand flags +0x3F5C..+0x3F5F.

### 4.1 Pages (`[this+0x2094]`)

| Page | Background | Shown when | Contents |
|---|---|---|---|
| 0 — choose tribute direction | `iw_peace.bmp` | normal start | meter, two side shields + names, three big buttons, CANCEL |
| 1 — build the offer | **`iw_peace2.bmp`** (`0x51254f`, `0x512627`) | after Offer Tribute or Demand Tribute | demand check boxes, province list, receiver list, money ± controls, value, SEND, CANCEL |
| 2 — annex / convert | `iw_peace.bmp` | at setup, if `0x45cd00(enemy)` (crushing victory) | message box + PCANNEX or PCCONVERT button, CANCEL |

When page 2 applies (`0x45cd00`):
- the option `[*0x8c0bfc+0x2D6]` is on;
- the enemy has fewer than 2 provinces in [0xE5C], or has state religion 7;
- we control the enemy capital;
- `0x4cbc00(enemy)` is true;
- war score `0x4d0800` ≥ 100.

### 4.2 Common elements

| Element | Position | Font / colour | Content | Evidence |
|---|---|---|---|---|
| `peace resolution\heavens.bmp` | (20,0) | | meter background / scale | `0x512faf` |
| `peace_meter.bmp` (marker) | x = 20 + int(0.91·S + 91), y = 14 (pages 0/1) or 117 (page 2) | | S = war score `[this+0xB2]` = `0x4d0800(player…)`, −100..+100; x runs 20..202 | `0x513021..0x513065`, `0x5126fc` |
| Meter label | centred on the marker x (clamped to stay inside 20..202), just below it | `#FFC000` | `"%s%d%%"`: `"+"` if S > 0, then S | `0x512ff9..0x5130d1` |
| Our-side shield button (`[this+0x2084]`) | (24,14) | `smallshield_<tag>` (4 frames) | leader of our side; clicking toggles leader/alone and recomputes S | `0x5119d7`, `0x512647..0x512701` |
| Enemy shield button (`[this+0x2088]`) | (24,49) | same | toggles the same way | `0x5119f0` |
| Line 1 (not on page 1) | centred (112,24) | g14b `#FFC000` | POLEADER "%s as LEADER of alliance" or PONATION "%s ALONE (separate peace)", depending on our shield toggle | `0x5130e3..0x513180` |
| Line 2 | centred (112,41) | g14b `#FFC000` | PONEGBLABLA "negotiates Peace with" | `0x5131c8` |
| Line 3 | centred (112,58) | g14b `#FFC000` | enemy side, POLEADER / PONATION | `0x5131d3..0x513275` |
| CANCEL "Cancel" | page 0: centred x=112; pages 1/2: centred x=66; y=319 | text button g14b | page 2 → page 0 (normal negotiation); pages 0/1 → leave | `0x512dea`, `0x5129e3`, `0x5128c0..0x5128e2` |

### 4.3 Page 0 — tribute direction

| Element | Position | Notes | Evidence |
|---|---|---|---|
| `btn_demand.bmp` (192x42, 4-frame button) | (20,191) | label PODEMAND "Demand Tribute", g14, x=80 | `0x510e27`, `0x512f6d` |
| `btn_notribute.bmp` | (20,232) | label PONO "No Tribute" | `0x510e42`, `0x512f00` |
| `btn_offer.bmp` | (20,274) | label POOFFER "Offer Tribute" | `0x510e5d`, `0x512e93` |

Clicks:
- **Offer** → money 0, mode "offer" (`[this+0x2098]` = 0), page 1 (`0x5124e6..0x512556`).
- **Demand** → mode "demand" (= 1), page 1 (`0x5125b1..0x51262e`).
- **No Tribute** → sends a white-peace proposal at once (`0x51256b..0x512595` → `0x511a00`).

### 4.4 Page 1 — building the offer

| Element | Position | Font / colour | Content | Evidence |
|---|---|---|---|---|
| Check box `btn_kryssruta_small` + PO_VASSAL "Demand Vassalisation" | box (20,62), label (41,66) | label g14b gold | offer flag +0x3F5C | `0x512a9e`, `0x512b4a`, `0x511d39` |
| Check box + PO_RELIGION "Demand Religious Conversion" | box (20,84), label (41,88) | g14b gold | flag +0x3F5F | `0x512ac3`, `0x512b90` |
| Check box + PO_ACCESS "Demand Military Access" | box (20,107), label (41,111) | g14b gold | flag +0x3F5D | `0x512ae8`, `0x512bd6` |
| Column headers | POPROVINCE "PROVINCE" (20,128), POSCORE "Value" (88,128), PORECIVER "Receiver" (118,128) | g14 (white, **NIEPEWNE**) | | `0x512c2d..0x512c99` |
| Separator lines | y = 145 and y = 232, x 20..196 | | | `0x512cbd`, `0x512ce1` |
| Province list (`[this+0x2058]`) | (20,147), 168x83 | g14 | rows: province name, its value (`0x47ca60`), receiver | `0x511803`, `0x51182f`, `0x511481`, `0x51158d` |
| Receiver list (`[this+0x2074]`) | (20,240), 100x74 | g14 | countries that can receive provinces (our allies, **NIEPEWNE**) | `0x51169e`, `0x5116ca`, `0x511761` |
| Offer value | centred (174,240) | g14b white | `"%d%%"` offer+0x28 (**NIEPEWNE**: value as a percent of what the war score entitles) | `0x512db6` |
| Tribute money | centred (174,276) | g14b `#FFC000` | `"%d"` offer+0x3F54 | `0x512d57` |
| `btn_plus` / `btn_minus` (16x16, 4 frames) | (152,293) / (180,293) | | ±25 $ (`0x5e564c`); minus stops at 0; plus stops at the treasury (offer mode) and at 2500 (`0x5e88b4`) | `0x510da2`, `0x510dbf`, `0x5121e8..0x512340` |
| PCSEND "Send Offer" | centred x=158, y=319 | text button g14b | enabled by `0x45dd30` / `0x4ccde0` (offer valid) | `0x510d85`, `0x512037..0x5120f0` |

Building the offer:
1. **Add provinces.** Clicking a province row adds it to the offer list (+0x34[], count +0x2C, at
   most 0x7E4). The receiver is the country currently chosen in the receiver list (+0x1FC4[]); if
   the selected entry is the special first row, the receiver is 0 (**NIEPEWNE**: "to the alliance
   leader"). Evidence `0x5123c2..0x51242a`.
   - Which provinces are offered comes from `0x4ccd30` / `0x4cca90`: our provinces when offering,
     the enemy's when demanding (**NIEPEWNE**).
2. **Set money** with + / −.
3. **Tick demands** (demand mode). The enable rules are in `0x511b90`:
   - vassalisation uses `0x4cc150` / `0x45b7d0`;
   - religion uses `0x45fda0` with the enemy side's shield toggles;
   - access has its own rule;
   - details **NIEPEWNE**.
4. **SEND** → `0x511a00` → `0x442560(player, offer)` posts the proposal; then back to the diplomacy
   window (id 2) (`0x511b62`, `0x511b7a`). The AI answer is decided elsewhere (**NIEZNANE** here).

Tooltips (`0x5132e0`):

| Hover | Text |
|---|---|
| province row | POROLLPROV "Remove %s from %s." |
| + / − | POROLLPLUS / POROLLMINUS "Add/Remove 25$ to …" |
| (both, as the `%s` target) | POROLLDEMAND / POROLLOFFER = "the peace demand" / "the peace offer" |
| (list entries) | POROLLDEMPROV "Demand %s in tribute." / POROLLOFFPROV "Offer %s in tribute." |

The advice texts PO_PD_A..J ("You should offer them at least … provinces or …$") match the war
score bands. Where they are shown was not located (**NIEZNANE**).

### 4.5 Page 2 — annex / convert

| Element | Position | Content | Evidence |
|---|---|---|---|
| Message box (`[this+0x2070]`) | (20,191), (W−40) x (H−190), white text | PCAMSG "Sire, we have won a devastating victory over %s and have the possibility to completely annex them…" if annexation is possible; otherwise PCRMSG "This will automatically convert %s to %s and end the war…" | `0x510b17..0x510cfe` |
| Action button (`[this+0x2064]`) | centred x=158, y=319 | PCANNEX "Annex" or PCCONVERT "Convert" (text button, g14) | `0x510bfc`, `0x510c83`, `0x510d54` |
| Meter | y = 117 | as in §4.2 | `0x5129ed` |
| CANCEL | centred x=66 | goes to page 0 | |

Surprises:
- **No Tribute sends immediately.** It is a one-click white-peace proposal; there is no page 1 for
  it.
- **"Leader vs alone" is a click on the shields**, not a separate control. It changes the shown war
  score.
- **iw_peace2.bmp is only the background of page 1.**
