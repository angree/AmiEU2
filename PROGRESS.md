# PROGRESS — facts with proofs, newest first

## 2026-11-11 — diplomacy shields, stability medallion, right-click menu, wheel, messages (user list 1-11)

- Diplomatic map modes (MM_DIPLOMATIC / MM_RELATIONS, eu2.exe 0x47fd20): owner shield (shield_<tag>.bmp
  frame 0) on every known capital at the bbox centre, then against F (diplomacy window's country, else
  the player) lions (vassal) / fist (alliance) / crown (marriage) / cloud (CB) / fire ring (war, 11
  frames); no units there. Shot RTG 800x600 France: lions on Provence/Orleans/Bourbon/Auvergne, fire on
  England (d2.png). Commit 99ab404.
- Stability medallion T-STABILITY.spr (7 frames 40x40, origin 23,40), frame = clamp(stability+3, 0..6),
  over every capital on the normal/political maps, before the city (0x47fa3d). Shot zoom 1 (s1.png).
  Not yet: mode 10, darkening under fog (NIEPEWNE). Commit 844adb4.
- Wheel: WinUAE 2.8.1 inputdevice.cpp (tag 2810) sends the host wheel only as key presses
  record_key(0x7a << 1) / 0x7b, no release, in the per-port mouse loop. Our game already zooms on
  0x7A/0x7B (autoinput key 122/123: zoom 2 -> 1 -> 2 in the log). Why the user's wheel does nothing is
  open; every rawkey 0x68-0x7F and "wheel up" are now logged to read it from the user's eu2.log.
- Right-click menu (0x52a190): all entries of the own-province table (View, Enter / Army, Navy, Fort /
  Manufactory, Shipyard, Conscription, Bailiff, Legal counsel, Mayor / Colonist, Trader), unowned
  (Colonist, Trader), foreign (View, View Country / War, Peace / Diplomat, Allow Trade, Embargo, Loan,
  only with the capital known); greyed by bld_can / env_*_can / diplo_action_state; separators;
  colours gold / hovered white / disabled grey; a click on a grey row does nothing (r1-r3.png: Build
  Army opens Army Construction). Commit 2c95803.
- Message log empty in the user's run: their game never left 1 January (0 "message:" lines in
  work-wbtest/eu2.log), and an empty log draws nothing; STABDROP / STABINCREASE are log-only by OUR
  default (options.c, not the exe) and the slider's -1 waits for the monthly check. Spec of the
  original's message windows / log / defaults being read.

## 2026-11-10 night, later — unit sizes per zoom, fog check, map-mode probes, map layer (dirty rectangles)

- Unit sizes (user: zoom 1 too big, zoom 3 too small, zoom 2 right): army / navy sprites are loaded
  Lanczos-scaled per zoom - 700 / 500 / 350 per mille (sprite_load_scaled, cached on disk:
  cache640 on AGA, now also cache800 on RTG). The exe's rule is +0x23e = clamp(obj+0xe + zoom
  shift, 0..4) (0x40fb1d) - pixel dropping by 2^z; the user's sizes replace it. Cities, sieges,
  rings keep the 2^z shift. Cost: the first draw at a zoom Lanczos-scales the visible sets
  (AROS: units 2.4 s first time, 0.4-0.7 s from the cache). Shots z1/z2/z3 checked.
- Fog "no neighbours' armies": 0x531460 read in full. Visible = own/controlled provinces + their
  land neighbours (sea from naval tech 18), provinces of tied countries WITHOUT their neighbours,
  provinces with our units, and units of countries flagged +0x3D (war allies) with neighbours.
  France 1419: Maine (owner 94), Orleanais (109), Berri (111) are vassal/appanage land - visible,
  but their neighbours not; Armor/Bretagne (Brittany, army in Bretagne) are not adjacent to
  French-owned land, so they stay dark; Morbihan (next to French Vendee) is visible. Our code
  matches the exe (temporary log, removed).
- Map mode switch probes (probemodes=1 in eu2.cfg; amiga_uclock; AGA 640x480 zoom 2, 240 tiles):
  1.1-1.6 s per switch = lightmap tree decode ~0.65 s (50 %, mode-independent) + Floyd-Steinberg
  dither ~0.53 s (40 %) + borders ~0.16 s + province scales 40-110 ms; overlays/units/ui/blit
  together ~0.1 s. RTG 800x600 the same proportions, blit 5 ms. A per-tile cache of ids + shade
  (2 bytes/pixel) would remove the tree half; not done yet.
- Map layer (user: "dirty rectangles" when scrolling): tiles + map_objects (terrain, cities,
  sieges, cores) in their own buffer, keyed by mv_gen (bumped on flush / selection remap), zoom,
  day, mode, player, owner epoch. A scroll memmoves it and draws only the new strips (tiles +
  objects clipped through mv_set_scr_off2); units, routes, log, bars over the copy as before.
  Unit animation with an unchanged layer blits only the map area. Not used with the map under
  the sidebar (tall RTG) - the old path. Proof: probe scrolls 8 x 32 px then the same view drawn
  from scratch - AGA 0 pixels differ; overlays per scroll step 30 ms -> 0.1 ms. The rest of a step
  is the decode of the 15-16 new tiles (inherent), the copies, ui ~25 ms, blit ~40 ms (AGA c2p).

## 2026-11-10 night — wide RTG screens, fog of war, unit animation (user's messages)

- RTG 16:9 / 16:10: eu2setup labels the sizes with their aspect (within 1 %); the game ran at
  1280x720 and 1920x1080 on AROS. The top bar is centred over the map, the map continues under the
  sidebar below y 600 (mv_set_left; a click there selects province 978). Tile cache width follows
  the screen (32 -> 64/128 tiles): the second draw at 1920 decodes 0 tiles. Commit bffb0af.
- Fog of war as in the exe (SPEC_MAPMODES 3): in the normal map mode the provinces we do not see
  are drawn at half brightness (MV_SHADE_HALF, nearest pen to colour/2), and other countries' units
  are drawn only where we see. Seen = own provinces, their neighbours, our units', allies' (all
  alliance kinds incl. royal marriage / vassal) and war sides', sea neighbours from naval tech 18.
  Shot: France bright with its neighbours, the rest half-dark, no foreign armies there. Commit 65e20aa.
- Unit animation (units.c): A-STAND frames run (100 ms a frame, Speed 5 - NIEPEWNE), a stack whose
  owner unit has a path shows A-WALK l-1 facing the path's next province (8 directions as
  0x4109c0, x wrap at |dx| > 15000; both file-name orders; nation's set, then MIN). No
  interpolation - the unit walks in place and jumps on arrival (SPEC_UNIT_UI 6). eu2.cfg anim=0|1,
  missing = on for RTG, off for AGA/EHB (a full map redraw every 100 ms); eu2setup checkbox "Unit
  animation". Proof on AROS RTG 800x600: frames 0..3 cycle (temporary log), after "Armee du Roi ->
  Nivernais" two shots 3.5 s apart differ (2516 px) and the army shows the walk sprite; AGA log
  "unit animation off"; eu2setup Save writes anim=1 on RTG. Sprite cache 128 -> 512 slots (the 8
  directions and the missing name tries take slots; a full cache would make units vanish).
  Note for tests: a snap of 800x600 takes seconds on the shared folder - two snaps 300 ms apart
  land in the same loop pass and are identical.
- RTG uses the original 800x600 UI and map pixels 1:1 (ui scale 1000 at >= 800 wide); the size
  list starts at 800x600, so there is no third asset set.

## 2026-11-10 late — settings tool, RTG sizes, title bar, system pointer, AHI (user list 4, 5, 8, 10)

- eu2setup (src/setup_amiga.c, GadTools): display AGA / RTG + RTG size list, title bar, pointer
  game/system, sound off/Paula/AHI, intro; Save / Play. The RTG list = the display database
  (NextDisplayInfo, ModeNotAvailable 0, >= 800x600, LUT8 on AmigaOS/AROS, deep on MorphOS) - the
  AmiTTD method. AROS uaegfx: 19 sizes 800x600 .. 1920x1080 (PROGDIR:eu2setup.log). eu2.cfg lines
  that are not its keys survive (checked). Test rig: PROGDIR:eu2setup.test -> log + window dump
  (RastPort read back, eu2setup.ppm) + Save; "play" in it -> Play: the game came up RTG 1024x768,
  bar, system pointer, ahi.device.
- rtgmode=WxH: amiga_gfx.c rtg_best_mode looks the exact size up in the database first. AROS
  1024x768: "RTG screen open 1024x768 depth 8". Taller than 600: sidebar_bottom_fill (224x13, loaded
  at 0x566777) tiled under the sidebar (where the exe draws it: not traced, NIEPEWNE); the frontend
  centred on black (draw + hit share the offset; single player opened by a click at 512,459).
- titlebar=1: "amiga: wb bar: screen 640x495, full game area 640x480 at y=15". Bar pens 17/19/18 =
  the pointer registers, set from the select cursor's colours (241904 815822 d9b251 in the map palette).
- pointer=system: pointerclass images per cursor kind (45x33 at 640, WordWidth 4), no software cursor
  at all. The image checked as a picture (debug dump, removed). On AGA OS 3.x: not tested (rule: no
  tests in the user's guests) - NIEPEWNE whether a 64-wide sprite shows there.
- audio=ahi: "sound: ahi.device" on AROS, music 0 underruns; default Paula unchanged.

## 2026-11-10 evening — user list (units, minimap, click stall, sound, intro, extract.bat)

- tools/eu2conv/extract.bat: drop the installer or the installed folder on it (or it finds them next to
  itself / in the GOG folders); writes "data" beside itself. Tested with the installer: 91 s, 0 errors.
  In cmd `if exist "file\"` is true for a file - the folder test is the "d" attribute (%%~aA).
- eu2conv Windows vs Linux (both x86-64): text / gfx / map / sfx entries byte-identical (only the pack
  order differs); the 14 intro stills differ by +-1 in a few palette values (libm of msvcrt vs glibc
  in the Bink decode). The Linux output is deterministic (two runs identical).
- eu2-play.uae had sound_output=interrupts = "emulated, no output": silent. Now exact.
- Click stall: every selection re-decoded ~95 tiles (bbox of old + new province, ~600 ms reference).
  Now a pen LUT on the cached tiles that hold the province (tile palette test): 0 tiles decoded.
- Map objects (units, cities, sieges) shrink by 2^(zoom-1) (exe +0x23e); numbers only at zoom 1.
- The AROS test guest stole the keyboard focus at start (user's typing reached the game): it starts
  iconified now (win32.start_iconified=true).

## 2026-11-10 afternoon — tutorials 7-9 complete, window 1:1, Ledger axes, release gate

- Tutorials 7, 8, 9 run to "tutorial: end" on AROS (C:/temp/amiga_eu2/gen_c7/8/9.py + `wait tut:`).
  Needed: merchant button = exe button 42 (0x52e2f7: window 4 for the selected province's CoT); the
  economic map button 0x52e0d6 (capital selected, window 0xA); chapter 9's messages over the open Ledger.
- WinUAE window: 640x480 game in 820x620 was scaled. 660x520 shows the buffer pixel-exact at +19,+8
  (snap vs PrintWindow: 98.6 % equal; the rest = the last 9 columns AROS itself does not display).
  640x480 is 1:1 too but cuts the top 12 rows; gfx_center_* / filter offsets / SA_DClip (AROS) change nothing.
- Ledger graph axes 0x4e0482: y labels trunc(k max 0.2), k = 5..1 from the top, a label equal to one
  above it is skipped (strcmp loop); x labels start + trunc(0.2 i (now - start)), i = 0..4.
- Land explore rejections: soak to 1519 has 14, all "no way by land" (was 135). hs2 A == A2, X == Y.
- build/release-gate.py: SHA-1 of every GOG v1.09 file + EU2P/EUI1/EUS1/EUM1 magics + original-only
  types. `--git` passes (198 files); work-aros and a renamed england.ai are refused.

## 2026-11-10 — RAM use (AmigaMemProbe -> Work:amiga_mem.log, AROS 68k guest, grand campaign, France)

- start 122 094 K free Fast, 2 032 K Chip; map view created -6.0 MB; first new year -13.3 MB in all.
- Zoom 2 + 3 only (lightmap2 4 MB + lightmap3 1.7 MB): **16.5 MB Fast**, flat from year 2 to year 6
  (105 548 K at every new year and at exit) - no leak. Chip: **~1.0 MB** (sound samples, screen).
- With zoom 1 as well (lightmap1 11.7 MB, loaded on first use): **27.8 MB Fast** (94 276 K free).
- So 32 MB is tight with all three zooms; 64 MB is comfortable. Possible saving: drop lightmap1 when
  leaving zoom 1 (reload costs a disk read) - not done.

## 2026-11-10 morning — sound port (docs/SPEC_SOUND.md), 31bf581

- Every effect id 0x00..0x40 wired as decoded: UI (map clicks only; buttons silent per §3.1),
  simulation through `world_sfx` / `world_sfx_at` (diplo.c, peace.c, military.c, world_note),
  completion messages (main_amiga.c message diff), battle patterns (`pat_service`).
- Pattern tick: the game timer is a 15 ms one-shot (`push 0xf; call 0x416400` at 0x584e77 / 0x5b7359,
  `/home/angree/full.txt`), the sound manager runs before the pause test (0x416570).
- Proof: hs2 `A == A2` / `X == Y`; hd2 differs in the timing lines only; AROS chapter 8 runs to
  "tutorial: end", no trap, `music list 15: 10 tracks` in 1500.

## 2026-11-10 late night — diplomatic action effects, goods market, exact load

- docs/SPEC_DIP_ACTIONS.md implemented (5ee3f00): enable rules and effects of every diplomatic action,
  timed CBs; relation drift per record: embargo -0.1, war -0.1, trade agreement +0.05, guarantee +0.08,
  military access +0.025. The 68k drift loop lacked the per-direction border-share ceiling; fixed,
  `hd2.sh` (test_diplo REF vs NEW) prints REF==NEW again (only the timing lines differ).
- docs/SPEC_MARKET.md implemented (337936d): market ids 0..20 by name, demand per real id with the caps,
  plantations COT/SUG/TOB, supply counts trading posts, production leader per owner over every goods.
  The support limit's grain goods was interned as "grain" (the data says GRAI) and never matched.
- Save/load exactness bug found by the market change (`ts.sh`: X/Y diverged at day 17, RYA declared on
  MOS only in the continued run): ai_strength recomputed and overwrote the AI's monthly FS mid-month, and
  the economy's strength inputs (maintl/maintn, te, pe, loans, ports) were not saved (0 after a load).
  Both fixed; hs2 X == Y, and `DAYS="40 100 200" ts.sh` all "same".
- Soak 100 years: 111 countries in 1519, 14-20 wars a decade; this history never formed Spain (ARA
  conquered, CAS separate), so no Spanish colonies - a different path, not a colonisation failure (NIEPEWNE).

## 2026-11-10 night — tutorial chapters 3-6 on AROS, domestic window, CoT / text fixes

- Tutorial runs on AROS (work-aros, autoinput; c3* .. c7* shots): chapters 3, 4, 5 and 6 run to
  EndTutorial. Fixed on the way: HRE election in a tutorial, tutorial mode (AI frozen etc.), an empty
  text entry showed its key (TUTORIAL_3_C2_NAME), the script needed one input event per record (a
  full redraw each: 0.7-3.5 s on the guest) - finished records now run in the same pass, scenario
  units' own leaders (chapter 6's conquistador / explorer) were ignored, AddMessage boxes hid the
  arrowed province on our 640x480 map (moved to the bottom then). Chapter 7 stalls only because the
  blind timed clicks race the modal dialogs (a dialog swallowed the Anglia click); 8 and 9 not run.
  Driving notes: one-button dialogs OK at (408,338), two-button at (343,338); script arrows are
  800x600 points x 0.8; the arrow's tip is its centre - (25, -25); scrolling by keys 78 / 79.
- docs/SPEC_DOMESTIC.md (agent): the 10-year gate was right; -1 stability per step, button layout,
  DOMW_DELAY / DOMW_MODIFY + the 41-line stats block tooltips, POLICYDESC; colour codes 0xA7 Y / W.
- AI money cheat branches (war: broke -> 12 D + 10; peace: broke -> 20 D + 10, else within 50 months
  of a bankruptcy -> D + 10).

## 2026-11-10 evening — strength indices, tutorial mode, last peace, fleet discovery

- docs/SPEC_AI_STRENGTH.md (agent): FS / FN / FE of 0x43c010, monthly for every country. First try
  had the troop term 1000x too small (an extra /1000): 28 wars in the 1420s and 91 countries in 1519;
  fixed: 10-21 wars a decade, 114 countries (normal range).
- Tutorial chapter 3 on AROS (c3*.png): England became HRE emperor (0x5b23e7 skips the election when
  the header flag +0x2766 is set) and the Welsh army kept attacking Lincoln. docs/SPEC_TUTORIAL_MODE.md
  (agent): +0x2766 = `tutorial = yes`; 25 call sites - the AI's route orders move nothing, no revolts,
  no missions for the player, envoys always succeed, manpower x12, no disband / save / autosave.
- Last peace dates (country+0x8ee, written at 0x442abf, read by the truce test 0x45f5d0): truce
  records kept 37 years; the AI vassal waits 10 + 3 DIP years after the last peace (0x539067).
- AI explorer fleets roll for the coast like the player's (miss counter > 3 for the AI).

## 2026-11-10 late afternoon — terra incognita, colonist rules, growth, AI war gates

- Terra incognita for the AI (4a05b85): baseline soak 2684 "no way to" / 39 explorer sends -> 3612 /
  51 (base_nw.sh builds HEAD's military.c beside the current tree). With "never through unknown water"
  literally no AI fleet reached its target (0 sends): explore-capable fleets may cross it (NIEPEWNE).
- Colonist / trader base rules 0x45c110 / 0x45c2e0 for everyone (542b6fb).
- Growth location term 0x47e940 decoded (e980645): +0xca is the terrain, the "neighbour" test means
  "not an island"; Asia -4, Africa -12, climate 4/6 -8, -0.5 x colonisation difficulty, scaled, >= -14.
- Spain's colonist plan waited for ever (ENVDBG trace: plan 145 St. Thomas, money never there): the
  AI's recruiting / fleets / buildings now spend only above what waiting envoy plans hold (w->ai_resv
  set by env_attach at load, so a load is exact). Soak: SPA 11 colonies.
- AI war gates: border balance 0x541570, recent European gains in 0x5413c0, alliance balance
  0x540cb0 (strength indices only; the troop sums it builds are unused).

## 2026-11-10 afternoon — AI explorers, CoT lifecycle

- docs/SPEC_AI_EXPLORE.md (agent): explore plans 0x32 sea / 0x33 land / 0x34 natives of the colonial
  agent; implemented in military.c (x_think, mil_ai_colonial). Units: nearest explore-capable one;
  ordinary requests now leave conquistador / explorer units out (0x48c1f0).
- The host test (test_save) had no leaders at all: lead_spawn / lead_assign monthly and lead_swap on a
  tag swap added (as main.c). Spain = Castile's object after the swap, keeps spain.ai.
- Soak 1519: SPA explores the Caribbean from 1486, 5 colonies; POR 17 trading posts; plans: 48 sea /
  234 land / 58 natives started, 135 land offers rejected (no route).
- docs/SPEC_COT.md (agent; my membership decode corrected in two terms): membership 0x49aa0f, site
  value 0x47f400, failure counter, yearly dissolve / found (limit owned land / 29, cap 69), events.
  Soak: 22 CoTs in 1519, 12 of the 1419 ones dissolved and replaced. Saved: tc (membership),
  amieu2fail, ch (historical modifier) - hs2 A == A2, X == Y.

## 2026-11-10 midday — AI envoys, trade-minded, trade agreements

- "Trade minded" 0x43a9e0 = owns a CoT (+0xfc6, counted in the recount at 0x43aed3) or domestic slider 3
  < 5 (0x42bbf0(3) < 4, or 0x42b8c0 = -0.4 (s3 - 5) > 0); fantasy flag off. `world_trade_minded`.
- AI envoys from docs/SPEC_AI_ENVOYS.md (agent-decoded): merchant agent (30-59 d, roulette over CoTs),
  colonial agent (20-39 d, expand-vs-new, site value, .ai lists, AI filters), monthly missionary roll,
  trade agreement offers (>= 5 merchants/yr). The King's pools are modelled as a resource tick when the
  treasury and stock cover the plan (NIEPEWNE). hs2: A == A2, X == Y.
- Soak to 1519 (re/colscan.py on save_l.eug): 283 merchants placed; only 13 colonies / 4 trading posts -
  the AI knows no new land because its explorer plans (0x32/0x33/0x34) are not implemented yet; Portugal
  fills Madeira (823) and stops (ENVDBG trace). No trade agreement by 1519: merchants/yr peaks at 5.2.

## 2026-11-10 morning — country indices, rebels, recruitment, morale, event names

- England had no AI: the exe's country slot 0 is empty and its loops start at 1; ours index by the
  country.csv row (ENG = 0). Raw exe indices elsewhere resolved by tag (exe index = row + 1; checked
  against ENG / PRU / MIN / GEN = 1 / 6 / 12 / 26).
- Rebels: soak counts 36 -> 124 armies came from MAX_BUILD 128 (one queue for the world; the AI used
  at most 96 slots) - `save_l.eug` + re/rebscan.py: 73 of 134 rebel armies sat in provinces of
  countries without any army. MAX_BUILD 1024: 22-42 all century. Table peaks now printed per decade
  (battles <= 8/64, plans <= 585/800, Generals <= 190/400, builds <= 284/1024; wars ~50 -> MAX_WAR 96).
- Recruitment 2 / 3 / 4 months, ships 9 + Low Wind / 5 / 6 (0x437d60, 0x438298); morale +-30 % of
  the maximum a month (0x487530). X != Y on the way: last_ctl / last_owner were not saved.
- Event files scanned for non-numeric which / value: gainmanufactory, gain/losebuilding,
  addcore / removecore with a tag, technology by group name were read as numbers. All trigger
  keywords are known.

## 2026-11-10 early morning — books, loans, AI budget, manufactory panel

- Ledger books (12 / 16 categories, this / last year; AROS lb1 / lb2: FRA after 3 months, vassals
  14.69 of 81 income) and the category line charts (lc1.png, 1419-1421). A2 once differed: the hs.sh
  binary was stale (drvfs clock ~6 min behind; `head -20` hides compile errors).
- Bank loans decoded (0x45bb00 rate, 0x45bc00 credit, 0x449210 extension); event `bank` / `stock`.
- AI budget sliders decoded (A.5). Soak: rebel armies drift up to ~104 by 1500 (54-75 with the old
  ad-hoc budget) - the AI now funds stability only from what the free progress (5 x academies +
  2 x ADM - WE) lacks. NIEPEWNE whether the original shows the same; compare when the original
  can be run.
- Build-manufactory sub-panel (mp1.png); improvement / fort / loan dialogs with the game's texts.

## 2026-11-10 night — blockade, weather, naval attrition, looting, supply

All with `hs2` A == A2, X == Y and the 100-year soak (`soak3.sh`: MILLOG, counts per decade):
- Blockade: 1422 save has 11 blockaded ports. NAT's VP was lost on a load (NAT held nothing at the
  save): `TSDAYS=90`, then a `world_vp` trace (VPT) showed +1000 on 1419-11-0 in the running game
  only. Fix: REB / NAT / PIR and any country with VP are always saved.
- Naval attrition: 10 small fleets lost in 1419-1422; soak 49 fleets in a century (24 in the 1420s).
- Weather: X != Y at day 35 was two battles in swapped slots (a packed restore); fixed with `bslot`.
  Unit attrition +15 abroad now only in winter (0x486056) - soak rebel armies 47-70 (were 25-48).
- Looting: ~70 loots a year in the soak (6625-6796 a century).
- Maximum attrition / supply: soak 1519 138-167 countries, 23-37 wars.

## 2026-11-10 — pirates, privateers, timed casus belli

- Pirates: 100-year soak (`MILLOG=1 test_save l`): 16 spawns, rising from 1 per decade at first
  (cap (shipyards + warships/100)/2 before 1550), 24 naval-battle log lines with PIR. hs2 exact.
- Privateers, host (`re/texp/tpriv.c`, ENG in the Bay of Biscay): 5 ships, treasury 100 -> 75, BRI
  gets a CB on ENG until day +90, gone on day +91. AROS (FRA, `select=923`, click 89,345):
  "3 pirate vessels in this area", treasury 400 -> 375 (pv2.png).
- Save: `prices = { GOODS value ... }` in amieu2_economy (mid-month trade values after a load);
  `amieu2cb = { a b until ... }` in globaldata.

## 2026-11-09 midnight — military access, a save hole

- `may_pass` (0x45f730) now gates the AI's moves too. hs2 failed X == Y with it; `TSDAYS=1` +
  an mrand caller trace (`__builtin_return_address`, addr2line) put the first split on day 1 in a
  blockade plan: province 569 (owner none, natives wiped) controlled by NAT in the running game, by
  nobody after a load. Countries were saved only when `present` or owning land, so NAT's controlled
  list was dropped. Now saved with `amieu2absent = yes`; hs2 `A == A2`, `X == Y` (ASan and plain -O1).
- The earlier bisection ("r alone fails, s alone fails") was noise: runs shared `/home/angree/sv`.
  Bisect in a private directory (`/tmp/sb`), one run at a time.

## 2026-11-09 late night — events, HRE, naval AI

- Events: `relation` (3339 uses), revolt (729), casusbelli (251), revoltrisk (254), wake/sleep monarch
  (137/99) and ~25 more command types were parsed but only counted as unimplemented; now applied. The
  soak shows Castile becoming Spain in 1476 (ailog: CAS's last line 1475, SPA from 1476, "GRA cedes
  Gibraltar to SPA" 1518). hs2 exact after each step.
- Accepted cultures: the scenario `culture = { type = turkish type = arabic ... }` kept only the first.
- HRE: save_x has `emperor = HUN` (1419 scenario); elections run on the emperor's monarch change.
- Naval AI (agent): soak 45 naval battles (3 before), 1356 blockades, 348 transports, 439 invasions.
- Ledger on AROS: the monarchs page shows Mehmed I 1402 (lg2.png).

## 2026-11-09 night — small rules, revolters, military AI v2, boosts

All host-checked with hs2 (`A == A2`, `X == Y`) after each commit; 100-year soak hl2 runs to 1519.
- Military AI v2 (agent, SPEC_MILITARY_AI): soak 1519 = 116 countries, 20-34 wars, armies ~5000 men
  (v1 ~700), 3698 battles, 722 fortress surrenders; AI CPU on the host 2.66 s / 100 years (v1 0.58).
- Revolters + defection (SPEC_REVOLTERS): soak 1519 = 139 countries alive (116 before), rebel-held
  provinces 11-21 a decade (22-42 before).
- War taxes on AROS: Budget window buttons at (66,299) / (158,299); after the click the button greys
  (TWR_WARTAX1), research costs rise by the +1 % inflation (7906 -> 7985). Shots wt2.png, wt4.png.
- Envoys: pending list saved (`amieu2_envoys ... pend = { c p kind ok due ... }`); merchants due one
  month later in save_x.
- Specials: `amieu2spec = 2050` in save_x (America 50 + Amazonia 10 = vpc[5] 60000). X == Y failed
  until the AI's known provinces were saved: after a load an AI without knownprovinces "knows the
  whole map" (world.c), so nobody claimed a special after a load.
- Tutorial chapter 2 on AROS (t2*.png): no events now (the chapter's .eug has no include of
  db\events.txt; before, "The Justices of Peace" popped up on day 2), manpower 40 read from the
  scenario (before: 1000 men available, the chapter asks for 4000), army bought, built, band-selected,
  marched to Lincoln, the script reached "Splitting a Unit".
- Message settings: rows from 25 on could not be toggled (hit codes 100.. collided with the buttons).

## 2026-11-09 evening — interface fidelity round (user reports), Stop hook

- Fonts: X.Org misc-fixed (public domain) made proportional by tools/mkfont/bdf2euf.py; 640x480 6x9
  texts / 6x10 titles / 5x8 fallback; "The Age of Exploration" 104 px (Not Jam Serif 141, Pixel
  Operator ~190). '$' is a coin. Shots: fe1.png, x2.png (C:\temp\amiga_eu2).
- AGA pointer: pens 17-19 from GetPrefs color17..19 (OpenScreen left them black: logged
  "pointer pens 17-19 = 0 0 0" before the fix, shots/ptr2.png after).
- Map modes from 0x478950 (docs/SPEC_MAPMODES.md), per-province shade in mapview; start mode normal.
  Map shading: Floyd-Steinberg 60 % instead of the 4x4 ordered pattern (y2crop.png). Terra incognita
  flat and empty (z1.png: America seen by Turkey).
- Setup screen country panel (docs/SPEC_FRONTEND_SP.md; sp1.png / sp2.png).
- Units (docs/SPEC_UNIT_UI.md): pick by sprite, rubber band (log "band: 2 armies selected", u4.png),
  routes with arrows filling green (y2.png), order animations, Shift waypoints, Ctrl groups.
  Bug found by the user: the band's release was also taken as a province click (cleared the
  selection) - fixed in 9b7e7c2.
- Host save test after the military.c change: A == A2, X == Y.


## 2026-11-09 afternoon — the diplomatic AI, war exhaustion, market

- `src/ai.c` + `docs/SPEC_AI.md`: the exe's diplomatic AI (think 0x53fd00, scores 0x535810 /
  0x535600, war predicate 0x540fc0, mode 0x5421a0, answers 0x535da0..0x5380c0, plan gates, peace
  0x53f240 / 0x5382f0 / 0x539e10 / 0x5366c0). Proof: `hs.sh` A == A2, X == Y after every step; the
  Amiga twin test (`Work:test_save a` + `b`) gives files identical to the host (ai.c at -O2).
- 100-year host soak (`hl2.sh`, AILOG=1): ~630 war declarations, ~6 a year after the start; 22-37
  wars at a time; 143 -> 121 countries by 1519; ~10000 peace offers, ~2 % accepted (the exe's rule:
  peace mostly after a lull in the fighting); 370 marriage proposals, 440 alliances accepted.
- CPU on the reference config (bench=1, `AI:` line in eu2.log): the AI went 42 s -> ~4 s a bench year
  (live-country lists, distance table, relation cache, war-score cache, daily ticks, -O2).
- War exhaustion (SPEC_TIME_ECONOMY 8.4): saved as `warexhaustion`; feeds stability investment and
  revolt risk. Market (9.1): `PRICES=1 test_save` prints e.g. CLO 17.85, GRAI 1.25, FISH 2.0,
  SALT 18.9, WINE 18.6 after a year of the Grand Campaign.
- Proposals to the player: `debugask=MILALLPROP` shows the herald dialog (shots/ask1.png).

## 2026-11-09 (morning) — decoded revolts and religion, intro stills

- Revolts (host 100-year soak): with the exe's roll (rand % 400 for the AI) 12-24 rebel armies and
  22-36 rebel-held provinces at a time; saves exact (hs.sh). Reference bench: month steps 2740 ms a
  bench year (diplomacy 1060 after the tolerance table).
- Intro (Amiga RTG and AGA): Paradox logo, Napoleon's quote, the stained-glass cards in their own
  palettes, Esc to the menu (shots/intro_amiga.png, shots/in5.png). eu2conv: 14 stills, 1.7 MB,
  0 errors, 23 s.

## 2026-11-09 (night, 5) — separate peace, mercenaries, battle figures

- Separate peace (Amiga): ENG vs FRA's alliance, the enemy shield toggles France / Scotland; an offer
  to Scotland alone is accepted, "separate peace: ENG and SCO (1 provinces)".
- Mercenaries (Amiga): a month in, the purchase window's line says "You cannot afford the 220 it
  takes to recruit a mercenary company" (shots/me.png).
- Battle figures (Amiga): Scotland's soldier facing NE against England's facing SW (shots/bf1.png).
- Grant Independence (host tvas2): FRA after annexing Brittany releases BRI (11 provinces) as its
  vassal; no scenario starts with a candidate.
- Saves exact after all of it (hs.sh; Amiga twin earlier in the night: A == A2, X == Y, == host).
- The full game's save on the Amiga (leaders, missions, loans, mercenaries, tolerance, everything the
  test_save twin does not cover): ENG to October 1419, saved to slot 1, loaded (6.6 s), saved to slot
  2 - slot1.eug == slot2.eug (1136411 bytes).
- AROS 68k with the newest binary: battle window with figures (shots/aros2.png).

## 2026-11-09 (night, 4) — windows batch 4

- Purchase (Amiga): Cornwall offers 8000 / 8000 / 80, prices 9 / 17 / 30 (policy), 2000 inf +
  1000 cav ordered for 35 -> three queue entries (shots/pu.png).
- Religion (Amiga): ENG Catholic, sliders Catholic / Orthodox / Moslems; raising Catholic lowers the
  others, sum 0 (shots/re.png). Saves exact (hs.sh).
- Event popup (Amiga): The Lollard Heresy in the decoded layout; hovering an action lists
  "Stability: -2 / -50$" (shots/ev1.png).
- Trade good (Amiga): Fish - price 10, our production 516 units, infrastructure 14 %, leader Aragon,
  trade level 27 % (shots/rs1.png).
- Loans (Amiga): Offer Loan to Aragon 75 at 10 % for 36 months -> "Aragon rejected our loans
  proposal." (shots/lo2.png).
- MorphOS 3.20 (QEMU): the game runs ENG to March 1422: army window, message log, "Clarence has died",
  clean exit (E:\morphos_eu2\shots\n9c-*.png).

## 2026-11-09 (night, 3) — army commands, sieges, saves, menus

- Split / merge / disband: Amiga, Home Army 12000/2000 -> 2 x 6000/1000 -> merged -> disbanded
  after CONFIRMDISBAND (shots/rg.png). Siege: host tsiege, siege force 4032/968 at fort 1,
  Champagne taken on the 6th assault. Event units: Amiga, "DAI: New Army arrives in Lao Cai with 5000".
- Save slots: Amiga, saved to slot 1, loaded back in 4.6 s; the frontend lists Quick save /
  Autosave / Slot 1 with their dates (shots/sl*.png). Right-click menu (shots/pm.png), message
  settings (shots/ms.png), AGA 640x480 checked (shots/ag.png).
- Admirals: host, Age of Enlightenment - Benbow on the Royal Navy, Tourville on the Escadre Atlantique.
- Reference bench (020, no JIT, -70 %): month steps 2420 ms a bench year (was 2500), no regression.
- AROS 68k: the same binary, battle window + message log (shots/aros1.png).

## 2026-11-09 (night, 2) — invasions, explorers, terra incognita, messages

- AI invasions: test_war - "ENG army March Garrison sails with Home Fleet to invade Picardie (2 zones)",
  "ENG: 1 army unit(s) land in Picardie", POR into Tangiers; fleets return ("Home Fleet returns to
  Kent"). Amiga test_save: A == A2, X == Y, Amiga X == host X (after invasions and after explorers).
- Explorers (host texp, Age of Exploration as SPA): Colon arrives with 3 ships; an ordinary SPA fleet
  cannot be ordered into an unknown sea zone (-1), Colon can (265 days); +9 provinces in 120 days.
  Amiga: "POR: Pero da Covilha arrives in Tago with 1000 men", "SPA: Cristobal Colon sets sail".
- Message log on the map (shots/lg1.png); state messages: "Our stability dropped.", "Clarence awaits
  your command in Northumberland." (the generals of the start are now silent for 35 days).
- Rollover help: "Strength Ratio 1:1" + RATIO_D, "Infantry", PO_PD_E over the peace meter (shots/tt*.png).

## 2026-11-09 (night) — battle, peace, VICTORY / OPTIONS windows

- Battle window: Amiga RTG, Grand Campaign as ENG with debugsel=1 - "Land Battle / Northumberland",
  SCO vs ENG shields, gauge, 9867/4934/0 vs 11872/1979/0, fire phase 5 vs 4, morale 1.7 / 1.9
  (shots/bt1.png). mil_battle_view factors the roll modifier out of phase_losses: test_war output
  byte-identical before / after (twcmp.sh: "SAME").
- Peace window: host `test_peace t m ENG window` - demand Champagne + 50 ducats + access at war score
  52 % (value 15) accepted, the war ends. Amiga (debugpeace=FRA, day 3, score 0): a demand of 7 % is
  refused; an offer of Meath + Northumberland + 25 ducats (10 %) is accepted, both provinces go to
  FRA (shots/pw_all.png); AGA 640x480 page 1 checked (shots/pw_aga.png).
- VICTORY / OPTIONS: Amiga RTG frontend (shots/fo.png); typing "200" into the Supremacy box logs
  "<Victory Condition changed to Supremacy (200 Victory Points).>" and writes eu2opts.txt. With vp=10
  the game ends on the first day: "You got 0 Victory Points and regrettably did not win. (Auvergne)".
- Saves still exact (hs.sh A == A2, X == Y); 100-year soak clean (5.1 s).

## 2026-11-09 (later) — envoys, Ledger, monarchs, generals, loans, vassals, messages

- Amiga twin of test_save after envoys, AI envoys, revolts, generals' fields, trading posts:
  "Amiga A == A2 / Amiga X == Y / Amiga X == host X" (the 68k and the host run the same simulation
  byte for byte over two years).
- Revolts (src/revolts.c): 100-year soak, rebels from the 1460s, put down by the owners' armies.
- Trading posts: host, POR traders into Yukon 41 %, level 1 on the 4th try.
- Battle / siege messages: Amiga, "We have lost control over Nivernais to Burgundy."
- Missions (src/missions.c, docs/SPEC_MISSIONS.md): Amiga, The Age of Exploration as ENG - offers
  "Conquer Gascogne / Keep Jaemtland / Vassalize Hannover / Colonize Chesapeake / Royal Marriage with
  France", the description dialog (Time Limit 5 years, VP 30, penalty 19), accepted (shots/mi.png).
- Frontend INFO shows FRA_GC_HISTDESC word-wrapped (shots/fi.png). End of game (debugend=1):
  "You got 0 Victory Points and lost the game. (#3)". Map tooltip "Ile de France / France /
  City (Population: 60000)" (shots/mt.png). Ledger Monarchs page: Charles VI Average/Poor/Poor.
- Bench month split (reference, 12 months): economy 480, military 300, peace 60, diplomacy 860,
  revolts 100 ms.
- Province improvements (src/buildings.c, docs/SPEC_BUILDINGS.md): Lyonnais 1419, a tax collector
  refused for infrastructure 0 (rc -3, the decoded requirement). AI builds them; its first version cost
  the month steps 3680 ms a bench year, per-owner city lists brought it to 2500 ms (economy 480,
  military 280, peace 40, diplomacy 1000, revolts 120).
- Discovery (shots/fog.png): France 1419 sees the Atlantic west of Europe and Africa south of the
  Maghreb as terra incognita. Requiring knowledge for the AI's envoys broke X == Y (the AI's knowledge
  is not saved) - only the player's envoys use it.
- Map closer to the original (shots/st.png, ci.png, t1.png, co.png): the game opens on the normal map
  with country borders (+23 % per full zoom-1 decode: 860 vs 700 ms RTG), landscape sprites at the
  province.csv positions (zoom 1), cities / colonies / trading posts (T-CITY / T-BUILD sprites),
  our core shields on the political map. The Ledger history is saved (amieu2_ledger).

- Envoys (docs/SPEC_ENVOYS.md, src/envoys.c): Amiga, merchant into Ile de France 2/20 -> 3/20; a
  208-ducat mission cost showed 1 on the 68k only (v x 1000000 overflow, 32-bit long) - fixed.
  Host: a mission to Languedoc rolled a failure, after 31 months the MISSOFAIL note arrives and the
  province stays reformed. AI envoys: 60 ms per game year on the reference config.
- Save: the build queue is one ordered block (amieu2_buildq) - two blocks reordered it after a load
  (X != Y with missionaries). Warnings and trade agreements are saved. test_save A == A2, X == Y;
  100-year soak clean (ASan, 4 s).
- Ledger (docs/SPEC_LEDGER.md, src/ledger.c): shots/l_all.png (VP graph, income table, friendly
  relations of France, armies with province/region), loans page shots/b2.png.
- Monarchs (src/monarchs.c): France 1419 Charles VI DIP 4 ADM 5 MIL 4 -> 3/3/3 pips (scale 9).
- Generals (src/leaders.c): host 1430 Armee du Roi - Richemont 5/5/5/1, Armee d'Italie - Dunois.
- Diplomacy: test_diplo DAN annexes its vassal SWE (4 -> 16 provinces); CAS-POR trade agreement.
- Messages: DECLWAR (eu2.cfg debugwar=CAS: "Castile declared war upon us!", shots/w1.png),
  MERCLOSS, MISSOSUCC / MISSOFAIL.
- Bench (reference, 360 days): month steps 1780 ms (economy + diplomacy ~148 ms a month).

## 2026-11-09 — the game screen window by window from eu2.exe

- Sea transport (manual p.17), host test_war: "transport: fleet 9/0/0 in Quiberon Bay, capacity 9000,
  army weight 7000 / embark: 0 / disembark into Vendee: 1 unit(s) / embark heavier army: -4 (expected
  -4) / ... naval battle in Irish Sea ... FRA fleet breaks off to Vendee / FRA army lands in port
  Vendee". test_save after it: A == A2, X == Y.
- Map: selection ring + order destination (shots/r1.png). Top bar rollover help (shots/bt.png):
  "You have 400 ducats (currently 0.00 per month)" - the rates read 0 until the first month is
  booked (the economy fills its report monthly).

- Method without the comparison VM: the draw methods (missing from the Ghidra export) disassembled
  with objdump and read with a stack/FPU-tracking annotator (re/symx.py, outside the repo): every
  text call with its key, x, y, font and colour. Results in SPEC_UI 4.3-4.10.
- Proof runs (reference guest, autoinput, snaps in C:\temp\amiga_eu2\shots):
  map modes `mm_*.png` / `a_mm_dip.png` (diplomatic: ENG and BUR at war in red); windows
  `w_all.png` (RTG) and `a_all.png` (AGA); tooltips `tt_all.png`; foreign province + sea
  `h_all.png`; diplomacy `d_all.png`, `e_all.png` (confirm popup with "We will lose 2 stability,
  -2 for lacking a Casus Belli").
- Build queue carries ships (kind 1..3) and fortress works (4), saved as `amieu2_shipbuilds`;
  economy saves the maintenance levels (`mc`). test_save: A == A2, X == Y (host).
- Ship build in Vendee: `war: FRA builds a warship in Vendee (50 ducats), ready in 360 days`.
  Fort expansion at land tech 0 is refused (-5 = MIL_RC_TECH, the cap is level 1), as SPEC 9.5 says.
- AGA first frame: 820 ms (map 540, units 240, ui 40); a first PIW at 640x480 costs ~11 s once
  (sprite rescale into cache640).

## 2026-11-08 — a real 640x480 font: EB Garamond (OFL), and a way to compare with the original

- User: "the fonts do not fit, nothing looks like the original - find a way to compare". 
- Fonts: `tools/mkfont` (stb_truetype, public domain) renders EB Garamond (Georg Duffner / Octavio
  Pardo, SIL OFL - `fonts/OFL.txt`) into EUF1 1-bit cells with +1 px tracking; `fonts/ebg11sb.euf`,
  `ebg13sb`, `ebg13b`, `ebg16b`. At 640x480 the UI uses ebg16b / ebg13b / ebg11sb (the game's own
  faces stay at 800x600). build.sh deploys `fonts/` to Work:. The frontend labels fit now
  (host test_ui, shots/ui_front0_640.png). ui_free now also frees the lazily loaded window sprites.
- Comparison with the original: Wine on WSL1 does not work (11.x: address-space assertion; 9.22
  wow64: segfault - WSL1 cannot run 32-bit code). Working route: a QEMU x86_64 VM on E:\eu2_origvm
  (QEMU copied from the morphos_emu lab, WHPX acceleration, Debian 12 nocloud image grown to 10 GB,
  root ssh with a key on 127.0.0.1:2299, serial console on :4599 via tools/orig/vmcon.py), Wine 8
  (wine32) + Xvfb + xdotool + ImageMagick inside. Nothing touches the Windows desktop.
- C: ran down to 0.1 GB during the Wine attempts (Wine trees in the WSL home): cleaned to 2.9 GB;
  everything for the VM lives on E:.

## 2026-11-08 — fleets: sailing and naval battles v1

- Naval graph: sea zones (terrain 5; 6 = rivers) linked by ADJACENT.TBL, ports (land with
  port_sea) linked to their zone. Naval speed is NIEZNANE (§11.1): ours 10 days per zone, 5 in/out
  of port. `mil_order_fleet` (Dijkstra), daily sailing, player: click selects our fleet when no army
  is there, right click orders the voyage. Sailing orders are saved.
- Naval battles v1 (ours, NIEPEWNE: §12 leaves wind, interception and the naval CRT use open):
  daily, strength 3 x warships + galleys, losses enemy strength x (d10 + 1) / 100, transports first;
  without fighting ships the transports sink; at half strength the fleet slips away to its nearest
  own port.
- Proof (test_war, host == Amiga): Escadre Ponant Vendée -> Meath 60 days / 7 steps; Home Fleet
  24 meets it in the Irish Sea: `ENG 24/0/5 vs FRA 10/0/1, losses 2 / 2`, FRA breaks off to Vendée.
  Saves keep the fleet order too (cycle sort like the armies): test_save A == A2, X == Y.
- A play guest for the user: `winuae/eu2-play.uae` (Work = C:\temp\amiga_eu2\work-play, data via a
  junction, `run` starts the game with the menu). Never run tests there.

## 2026-11-08 — in-game menu (SPEC_UI §9)

- MENU button or Esc: setupscreen_bg panel centred, btn_frontend_big buttons RESUME / Save Game /
  Load Game (greyed without a save) / EXIT (texts from text.csv); the game stands still. Esc no
  longer quits outright. Save/Load use the quick slot (one slot for now).
- Proof: Amiga AGA: Esc -> menu (shots/amiga_gamemenu_aga.png), Save Game -> `save: ... ok, 1500 ms`,
  Esc -> EXIT -> `eu2: bye`.

## 2026-11-08 — army window (iw_army, SPEC_UI §6)

- Selecting an own army shows iw_army in the sidebar: name in nameframe, infantry1/cavalry1/
  artillery1 icons with numberframes, moralestatusbar + morale_bar clipped to the share of the
  maximum (§8), location, march destination and days to the next step, total men. Row positions
  are ours (§6: NIEZNANE). `select=` in eu2.cfg also selects our army there.
- Proof: Amiga RTG, select=385: "Armée du Roi", 15000 / 4000 / 0, morale 1.7, 19000 men
  (shots/amiga_army_rtg.png).


## 2026-11-08 — attrition (SPEC_WAR_DIPLOMACY §10)

- Monthly, per land unit: a = stack size of the province (ceil(inf/1000) + ceil(cav/1000) +
  ceil(art/10)) + 15 off own soil + 5 tropical − supply value (§10.1: base tax × 5/4/3/2 + 10 × fort
  on friendly soil), capped by the maximum attrition (§10.2: 10 − land tech/6, +10 enemy-held =
  our stand-in for "out of supply", +5 tropical, +8 desert, +5 mountain), at least 1 %, none on the
  march. Not modelled: winter, looting, marsh, supply lines (NIEZNANE in the spec), randomness.
- 100-year soak: armies stay near 250 (recruiting replaces losses), 3-6 wars at a time, fewer
  annexations (151 countries in 1519 instead of 134). test_save: A == A2, X == Y, host == Amiga.

## 2026-11-08 — AI alliances and royal marriages; a livelier AI; 100-year soak

- `ai_alliances` (ours, NIEPEWNE): monthly 1/24 an AI country without a military alliance joins a
  friendly neighbour's alliance (<= 6 members, nobody it dislikes) or founds one (10 years, §3.2; a
  call to arms renews it); 1/36 a royal marriage with a liked (>= 50) dynasty of a compatible religion
  (§3.2 rule, 20 years). MAX_ALLY 128 -> 384.
- AI war chance doubled (war/1200 per month) and a target now only needs a negative relation (was
  < -50): 1419 + 20 years gives 14 declarations instead of 9, 27 marriages. All NIEPEWNE tuning.
- `test_save l` (host, ASan+UBSan, every module, 100 years): no errors, 3.9 s; 1519: 134 countries,
  ~190 alliances, 1-4 wars at a time, 325 live units in 331 slots (slot reuse holds the table).
- test_diplo and test_save (A == A2, X == Y) host == Amiga.

## 2026-11-08 — recruitment (player and AI)

- `mil_recruit`: unit cost from SPEC_TIME_ECONOMY §6.2 (9 / 21 / 30 x (1 + inflation)); the batch
  size, price-per-batch, 1000 manpower and 6 months are ours (NIEPEWNE, not in the spec). Recruits
  wait in `World.build[]` (saved as `amieu2_builds`) and join the owner's unit in the province or
  become a "New Army" in a destroyed unit's slot. AI (ours): monthly, with >= 60 ducats and 1000
  manpower, while its army is under 4000 + 2000 per province (x2 at war), 1000 infantry (cavalry
  1 in 4) in the capital. New game: the manpower pool starts at one year's rate (`econ_new_game`,
  NIEPEWNE - a scenario has no pool and the original's start value was not read).
- Player: army button with an own city selected -> dialog Infantry / Cavalry / Artillery / Close.
- Proof: test_save still A == A2 and X == Y (AI recruits 86 New Armies in 5 years, the unit table
  stays at 235 entries thanks to the slot reuse); Amiga as FRA, select=385: `FRA raises 1000
  infantry in Ile de France (9 ducats), ready in 180 days` (shots/amiga_recruit_aga.png).

## 2026-11-08 — main menu and single-player setup (SPEC_UI §8)

- `src/frontend.c` reads every `scenarios/*.eug` header (name, startyear, `selectable` tags); 7 of
  the 8 have a header with selectable countries. ui.c: main menu over `bg_start` (SINGLE PLAYER,
  Load Game (greyed without a quick save), EXIT; btn_frontend + shadow centred at x=400 as the spec
  says, y ours); setup over `bg_frontend`: shields of the selectable countries 10 per page with page
  arrows, the scenario list on `plate.bmp`, country name and START / BACK (layout ours, NIEPEWNE).
  The frontend playlist plays in the menu. After START the map opens on the capital.
- `menu=0` in eu2.cfg skips it (tests); bench skips it.
- Proof: Amiga AGA, autoinput: SINGLE PLAYER, The Grand Campaign, ENG shield, START ->
  `game: The Grand Campaign as ENG`, music list 14 (shots/amiga_setup_aga.png, amiga_game_eng.png).
  First run at 640x480: ~10 s to Lanczos-scale the two 800x600 backgrounds, then from cache640.

## 2026-11-08 — save / load, exact

- `src/savegame.c`: the world in the scenario format the loader already reads (globaldata with
  wars and alliances, province blocks, country blocks with diplomacy, owned/controlled/national
  provinces, cities, units, centres of trade, header) plus `amieu2` sub-blocks with exact integers
  (unit morale/march/index/dead, province income/manpower/revolt/popfrac, war clock and battle
  points, country vp/manpower/inflation/policy day...) and module blocks amieu2_economy (research,
  sliders, loans, ytd), amieu2_events (happened, flags, the message queue, the RNG), amieu2_military
  (RNG, battles in progress, sieges), amieu2_peace (truces, offer clock, RNG), amieu2_diplo (RNG).
  32-bit values are written as two 16-bit halves (a long is 32 bits on the 68k).
- Loading = world_load_db + world_load_scenario(<path on disk>) + the modules as for a new game +
  restore_game. Land units are put back in their saved order (the AI staggers by index).
- Proof (`test_save`): 1419, 3 years -> A, 2 more -> X; separately load A -> A2, 2 years -> Y.
  Host and Amiga: **A == A2 and X == Y** (cmp), and the Amiga's A and X equal the host's.
  Found on the way: battles/sieges, the unit order and the CoT blocks were missing at first (X != Y).
- In the game: F5 quick save `PROGDIR:save/quick.eug` (1.05 MB) in **2.3 s**, F9 quick load in **7.6 s**
  on the reference machine (`save: ... ok, 2340 ms`, `load: ..., 1419-4-30, player FRA, 7600 ms`).

## 2026-11-08 — domestic policy window; MorphOS re-checked

- iw_domestic (SPEC_UI §4.2): 8 rows at y = 16 + 36 i, DOMNAME_*_L left / _R right (slider 0 = left),
  minus / track / plus under the names (positions ours - NIEZNANE in the spec), names cut to their
  first word when both do not fit. Opened by btn_budget (the original route is NIEPEWNE). One step
  per 10 years (NIEPEWNE rule), note "Next change in <year>". The economy reads `policy[]` monthly.
- Proof: host test_ui (shots/ui_domestic_640.png); Amiga RTG: click minus on row 2 -> `domestic:
  slider 2 -> 1`, second click -> `change not allowed yet` (shots/amiga_domestic_rtg.png).
- MorphOS 3.20 (QEMU, ISO from winuae/harness/mos-iso.sh with the century-14 music only): as POR
  the event dialog 3265 shows (E:\morphos_eu2\shots\pcm-03.png), key 1, then the budget window with
  the land slider set (pcm-06.png, 26 February 1420), rc=0, END. The log did not come back through
  `ide0:` this time (NIEPEWNE why), so PCM playback on AHI is not proven by a log line yet.

## 2026-11-08 — AI reads the .ai files; AI budget; AROS re-checked

- `diplo_load_ai`: the scenario's `ai = "x.ai"` (default.ai otherwise), keys `war` (0..100) and
  `combat = { TAGS }`. 1419 reads 24 files. Monthly war chance = war/2400 (50 = the old 1/48);
  combat targets need no CB while the relation is under 100 and are picked first (ours, NIEPEWNE).
  test_diplo 20 y now: NVG->PSK, CAS->NAV, VEN->MAN, TIM->GUJ besides TUR->BYZ etc.; host == Amiga.
- AI budget sliders monthly (ours, NIEPEWNE; rules in economy.c `ai_budget`): test_econ 50 y moves
  as expected (1469: FRA treasury 1241 vs 2453, inflation 5.03 % vs 9.77 %); host == Amiga.
- AROS 68k, RTG 800x600 as POR with autoinput: event dialog 3265, key 1 -> action A, budget window
  slider click, PCM music `m40.pcm 28000 Hz`, 0 underruns, clean `bye`
  (shots/aros_budget_rtg.png). The first AROS start's WinUAE vanished before autoexit while
  start-eu2.ps1 waited in the background; the rerun was clean (NIEPEWNE cause; watch for it).

## 2026-11-08 — budget window (iw_treasury, SPEC_UI §4.1)

- `econ_budget` / `econ_set_slider` (one slider set, the others scaled to keep the sum at 1000,
  rounding to the treasury). The treasury button toggles the sidebar between the province and the
  budget window: six rows (TWLANDTECH .. TWTREASURY at y = 16 + 47 i), sprite_slider track at x=62,
  btn_slider knob, percentage at x=194, research/stability progress "done/cost" (the progress
  bars' place is NIEPEWNE). Click or drag on a track sets the slider.
- Numbers use a smaller face (garamond12 at 800, arial10 at 640): with garamond12bold the 640 window
  overlapped (shots/ui_budget_640.png before/after in the session).
- Proof: host test_ui renders both sizes, `ui_budget_hit` maps (120,184) to row 1; Amiga AGA: click
  treasury, click land track at x=150 -> land 100 %, others 0 % (shots/amiga_budget_aga.png).

## 2026-11-08 — player diplomacy: declare war, peace offers both ways

- Diplomat button with a foreign province selected opens a modal window (the event window type):
  relation, truce/CB note, war score; Declare war (diplo_declare_war with all §4.2 effects) or
  White peace / Demand / Surrender / Close. AI answers (ours, NIEPEWNE): white peace unless the AI
  leads by > 10; demands only when the player leads by >= 10 (terms = peace_terms for the score);
  surrender always. Only the side leaders make peace; a human ally follows its AI leader.
- A winning AI leader sends the human leader its terms (peace_set_offer), Accept/Decline; a declined
  offer returns after 6 months.
- `select=<province>` in eu2.cfg preselects a province (scripted tests).
- Proof: Amiga AGA as FRA, select=424 (Gascogne, ENG): dialog (shots/amiga_diplomacy_aga.png),
  key 1 -> `white peace: FRA and ENG`; key 2 (Demand at score 0) -> refused. Host test_peace with a
  human: ENG human -> `OFFER ... 5 provinces Lancashire Artois Gascogne Armor Northumberland`
  (1421, score 32); FRA human -> ENG's offer of 7 provinces + 75 ducats (1420, score -53).
  AI-only test_diplo still identical to the reference, host and Amiga.

## 2026-11-08 — event dialog for the player; scripted input (Work:autoinput.txt)

- events.c keeps `desc` and the action names; `events_set_choice` calls the game for a human
  country (the RNG is still drawn, so AI-only runs are unchanged: test_events/test_econ identical).
- ui.c `ui_event_draw`: bgbox_event (SPEC_UI §7: title y=24 gold, text from y=72), text pages with
  btn_eventpage_left/right + box_pagenumber, choices as tb_* text buttons (two columns when the
  labels fit, else one per row) - the A-D layout of v1.09 is NIEZNANE, this one is ours. Modal in
  main: the game stops until a click or keys 1-4; Esc = AI pick.
- `Work:autoinput.txt` ("<ms> key|click|rclick|move|snap ...") feeds the game's event queue from
  inside the guest - no host input is ever synthesised.
- Proof: host test_ui renders 800 and 640 (shots/ui_ev0_800.png, ui_ev0_640.png); Amiga AGA as POR:
  `event dialog: 3265 "The Social Revolution of João I"`, snap (shots/amiga_event_aga.png), `key 1`
  -> `choice 0` -> `action A`.
- Seen in the same shot, open: at 640x480 the sidebar PLEASESELECT text overflows its panel
  (font too big for the scaled sidebar - the CC0 font item).

## 2026-11-08 — military 2.2x faster on the 68k, identical results

- `mil_prof` (clock per phase) on the reference machine, test_diplo 20 years: move 100 ms,
  battles 9969, lists 1459, **sieges 15845, AI 16628** (total 44.8 s). Cause: `mil_at_war` scanned
  every war's member lists ~1.4 M times (gprof call counts), the AI cleared 2 x 1616 Dijkstra
  entries per search and recomputed enemies over 340 countries per army, and every unit/province
  scanned all 64 battle slots.
- Fixes: a war bit matrix rebuilt once per mil_day (`atw`), `atwar_any`, generation-stamped Dijkstra,
  edge-days table, per-province battle marker. Result: **20.5 s per 20 years = 2.8 ms per game day**
  (move 1199 incl. the matrix, battles 3075, lists 1328, sieges 8159, AI 6256).
- Proof of equality: test_diplo log (all wars, peaces, relations checksum) identical to the run
  before the change, host and Amiga (`cmp`).

## 2026-11-08 — diplomacy v1: relations, alliances, war declarations, AI wars

- Scenario diplomacy is read: `relation = { tag value casusbelli access refusetrade }` into one
  shared value per pair (`World.rel_m`, x1000) + `dipflag[a][b]`; globaldata `alliance` blocks
  (1419: 13 military, 24 vassal, 15 dynastic); the war `date`.
- `src/diplo.c`: monthly drift (§2.2 ceiling, §2.3 terms; tolerance and grievance not modelled, the
  two directions' deltas averaged - NIEPEWNE), alliance expiry, `diplo_declare_war` with the §4.2
  effects (truce break, stability, VP, badboy with/without CB, relation hits, vassal), call to arms
  (vassals always, allies by relation - ours), AI declarations (ours, NIEPEWNE: ~1/48 per month, worst
  neighbour with relation < -50 or a CB, enemy troops + allies < 2/3 of ours, no truce).
- Events: `atwar`, `war`, `alliance`, `dynastic`, `vassal`, `relation` triggers now read the real state.
- Proof: `test_diplo` 1419, 20 years: TUR->BYZ 1420 (TRE joins), HAB->HEL (BAD vassal joins), CAS->GRA
  1429, ENG->EIR (BRI, BUR join), alliances expire 1429; host (ASan) == Amiga (cmp, timing line aside).
  The 68k-shaped drift loop (`drift`, tables + symmetric terms once) == the readable reference loop
  (`-DDIPLO_REF`, cmp) and is 4x faster on the Amiga: diplomacy 61.3 s -> 15.8 s per 20 years
  (66 ms per month). Military is now the big one: 45 s per 20 years (6 ms per day).
- test_events (20 y) and test_econ (50 y): host == Amiga except the timing lines.

## 2026-11-08 — music as raw 8-bit PCM 28 kHz (user decision), conversion 2

- User decision: music is **raw PCM, 8-bit mono 28 kHz, not ADPCM** — decoding costs 68k CPU and
  every percent counts. eu2conv writes `music/mNN.pcm` (EUM1 header + signed bytes; windowed-sinc
  resampler, peak normalised, TPDF dither); `sound.c` streams with a plain `Read()` into the Paula
  buffer; `amiga_adpcm.c` is no longer linked. CONVERSION_VERSION 2; the game refuses data < 2.
- Proof: `eu2conv eu2setup.exe` 32 s, errors 0, text/map/gfx/sfx packs byte-identical to conversion 1
  (`cmp`), 53 tracks / 9522 s / **267 MB** (ADPCM was 105 MB). Reference machine AGA, 40 s run:
  `data: conversion 2`, `music: JUCD75_18_Falalalan (m32.pcm), 28000 Hz`, **0 buffer underruns**.
  Period rounded: 3546895/28000 -> 127 (27929 Hz on PAL). AROS/MorphOS with PCM: not yet run (NIEPEWNE).

## 2026-11-08 — war score and peace

- `src/peace.c` (SPEC_WAR_DIPLOMACY §5): battle points per war member pair (capped 95; points per
  battle NIEZNANE in the exe, ours 1 + loser losses/2000, max 10), war score = battle term ±95 +
  occupied province values (§5.3, V taken as 0), ±100 on total occupation of the leader; AI peace
  every ~3 months (thresholds and demands are ours, NIEPEWNE), white peace, automatic peace after
  36 months without combat and occupation, 5-year truces, +1 badboy per non-core province.
  Wars with a human member only end by the automatic peace until the peace window exists.
- Proof: `test_peace` 1419, 6 years: FRA vs ENG score 3 -> 10 after Gascony (4 battle points + value
  6), white peaces VEN-CRO 1422, POR-FEZ 1424, FRA-ENG 1424; host (ASan/UBSan) == Amiga (`cmp` IDENTICAL).
  ASan also caught a unit with location -1 indexing loc_head (guarded in battles_day).

## 2026-11-08 — sound effects and music

- `src/sound.c` over `native/amiga_audio` (Paula through audio.device; AHI on MorphOS) and
  `amiga_adpcm`: config/sfx.txt names -> sfx.pak EUS1 samples cached in Chip RAM (army_select,
  army_move, if_click wired so far), music streamed from data/music/mNN.wav (IMA ADPCM 22 kHz) on
  channels 2+3, playlist music/<century>.txt (14..17), names via music/index.txt, next track at
  the end. `sound=0` in eu2.cfg disables it.
- Reference machine, AGA: `music list 14: 10 tracks`, `music: JUCD3_17_Anonymous (m24.wav), 22050 Hz`,
  **0 buffer underruns** in a 40 s run. Audibility itself not checked (WinUAE has no recorder in the
  rig; the MorphOS lab can record WAV - NIEPEWNE until then).

## 2026-11-08 — war v1: movement, battles, sieges; player move orders

- `docs/SPEC_WAR_DIPLOMACY.md` (from eu2.exe, prose). `src/military.c`: wars from the scenario
  (1419: FRA+AUV+BOU+ORL+PRO+SCO vs ENG+BRI+BUR, POR vs FEZ+ALD, VEN vs CRO+HUN), ADJACENT.TBL,
  Dijkstra paths with the §11.1 edge time (terrain factor x 30 / speed; speed 1.5/2/3), land battles
  (§7: 5-day fire/shock phases, die 0..9 + modifiers, CRT rows/letters by era, losses =
  CRT% x strength x 0.2 per day, morale x 0.2 x sqrt(max morale / 2), break at 0 -> retreat at
  0.4, overrun < 10 %), max morale (§8.1), sieges every 30 days (§9.3) and control.
- 1419 test (`test_war`, identical on host and 68020): the Armée du Roi (15 000 inf, 4 000 cav)
  walks Ile de France -> Gascogne in 92 days, beats the Guyenne Detachment (14 000 / 2 000) in
  ~10 days (England -3 600, France -2 100), England retreats to Poitou, the fortress of Gascogne
  surrenders later; Portugal takes Tangiers. Magnitudes match the spec's worked example.
- In the game: left click selects our army in the province, right click orders the move; the map
  redraws when armies move or die. Per-province unit lists: a game year with war costs 1.9 s on the
  reference machine (was 11.1 s with the first O(n^2) loops).
- Not yet: leaders, attrition, supply, naval combat, river/landing modifiers, AI movement, peace.

## 2026-11-08 — Lanczos-3 interface for AGA 640x480, cached on disk

- The 0.8 scale for the 640x480 screen is now Lanczos-3 (integer only: kernel table generated once,
  16-bit planes so every multiply is a muls.w; colour premultiplied by coverage so the green key does
  not bleed; separate shadow plane). Done per frame at load time - only there is a strip's frame
  count known (button strips, PIW buildings), so a converter-side rescale could not keep strips aligned.
- On the 68020 reference: first start ui_init 5.2 s (32-bit) -> 3.0 s (16-bit), first province window
  14.6 -> 10.9 s; then every result is written to `PROGDIR:cache640/<hash>.spc` (pens + size +
  checksum of the palette's inverse table): **second start ui 280 ms, province window 460 ms**.
  64 files, 629 KB. `C:\temp\amiga_eu2\shots\amiga_ui_aga_lanczos.png`.

## 2026-11-08 — the EU2 main screen from its own bitmaps (RTG 800x600 and AGA 640x480)

- `docs/SPEC_UI.md` (from eu2.exe: coordinates, bitmaps, layers; prose only). `src/ui.c`: sidebar
  (header, info window, banner, bottom, minimap, 16 buttons), floating top bar from win8 tiles with
  merchant/colonist/diplomat/missionary/stability/manpower/treasury values and the pause button;
  map viewport x >= 224. Colour key green, red = shadow (darkening table from the palette) - unit
  sprites now cast their ground shadows too. Zoom +/-, pause buttons and province clicks work in the
  map rectangle.
- On the reference machine, after one simulated year, France: 1 January 1420, merchants 2,
  colonists 4, diplomats 4, missionaries 2, stability 0, manpower 13, treasury 411.
  `C:\temp\amiga_eu2\shots\amiga_ui_rtg.png` (RTG), `amiga_ui_aga.png` (AGA, layout and bitmaps
  scaled 0.8 with nearest sampling until the converter's Lanczos set exists; smaller fonts).
- Province window (SPEC_UI §3) on click: iw_province frame, terrain background, coast strip,
  buildings at the positions of PIW/inland.txt / coastal.txt (frame counts from filelist.txt, except
  resource.bmp: 21 frames of 56 px, not the 23 the list claims), culture building sets by country.csv
  City Nationality (NIEPEWNE mapping), crest with the trade-good icon (frame order read off the
  strip, NIEPEWNE for tea/orient/coffee/mineral), base tax and manpower numbers (9 / 5 for
  Nivernais = province.csv), owner shield. Colour keys are by closeness (this strip uses
  (4,255,4) / (252,4,4)). `C:\temp\amiga_eu2\shots\amiga_piw_rtg.png`.
  Tree choice by climate is NIEPEWNE (palms show up in France).
- Province window (SPEC_UI §3) on click: iw_province frame, terrain background, coast strip,
  buildings at the positions of PIW/inland.txt / coastal.txt (frame counts from filelist.txt, except
  resource.bmp: 21 frames of 56 px, not the 23 the list claims), culture building sets by country.csv
  City Nationality (NIEPEWNE mapping), crest with the trade-good icon (frame order read off the
  strip, NIEPEWNE for tea/orient/coffee/mineral), base tax and manpower numbers (9 / 5 for
  Nivernais = province.csv), owner shield. Colour keys are matched by closeness (this strip uses
  (4,255,4) / (252,4,4)). `C:\temp\amiga_eu2\shots\amiga_piw_rtg.png`. Tree choice by climate is
  NIEPEWNE (palms show up in France).
- Player's shield in the header hole (shield_TAG.bmp frame 0 at 84,3, under sidebar_mask).
- Not yet: the other info windows, tooltips,
  button hover/press frames.

## 2026-11-08 — the simulation runs: events + economy v1, host == 68020 over 50 years

- Specs written from eu2.exe (prose, no code): `docs/SPEC_EVENTS.md`, `docs/SPEC_TIME_ECONOMY.md`.
  Calendar settled: 30-day months, 360-day year (display day = day x real length / 30).
- `src/events.c`: 1528 events from 124 listed files; trigger trees, dated queue with one-time offset
  and 16-74-day retries until deathdate, one random chain per country (attempt cap added - the exe
  loops forever), AI choice 85/5/5/5, commands on the modelled state (others counted).
  81 years from 1419 on the host: 4182 events (111 dated), 109 ms.
- `src/economy.c` v1: France after month 1 of 1419 = spec worked example: tax **7.831** (7.83),
  manpower rate **13.387**/yr (13.39), maintenance **4.547** (4.55), production 0.743 (~0.78).
- `test_econ` 50 years (seed 12345): host and Amiga output **identical line for line**.
  Amiga (eu2-ref020): 50 years in 66.5 s; in the game a year costs ~1.6 s of simulation = ~5 % of
  real time at the original's speed 3 (100 ms/day). Events + economy load 4.0 s at start.
- Added after: centre-of-trade income (scenario CoTs + merchant levels, nearest-CoT membership,
  NIEPEWNE), bank loans (200, 5 years, max 5; interest 10 %/yr assumed - NIEZNANE in the spec) and
  bankruptcy (§7.2). Venice 1439: trade 7.35/month. Turkey (no AI to disband armies) goes bankrupt
  10 times in 100 years. 50-year run host == Amiga again (Amiga 57.6 s).
- Load-time and event-step speed (identical results, checked): event trigger `countrysize`
  cached per day + event ids binary-searched: game year 1.64 -> 0.46 s CPU on the Amiga; hashed
  country tags / interned names / keywords: world load 3.06 -> 1.24 s, events load 3.9 -> 2.8 s.
  Startup is now ~5.3 s of loading on eu2-ref020 (raw parsing of ~3.5 MB of script text).
- Per-owner province lists rebuilt each month instead of 340 x 1615 scans: identical output,
  host 781 -> 218 ms, **Amiga 50 years 57.6 -> 27.1 s** (~0.54 s of CPU per game year).
- Known gaps: wars, diplomacy, AI (budget sliders, armies), market prices, revolt risk.
  AI budget sliders (fixed default split) - tech barely advances in 100 years without trade income.

## 2026-11-08 — MorphOS 3.20 (QEMU pegasos2) runs the same 68k binary

- Lab `E:\morphos_eu2` (see CLAUDE.md). Log: `MorphOS detected - RTG deep screen`, deep 32
  `$41120051` 800x600, blit through `WriteLUTPixelArray`; map, units, status bar and font correct
  (QEMU screendump `E:\morphos_eu2\shots\a-04.png`). rc=0, clean exit.
- QEMU timings (not transferable to hardware): world load 19.1 s (2.3-2.9 s on the WinUAE
  configs - suspiciously slow, NIEPEWNE: file I/O of ~160 includes from RAM: under Trance?), first
  frame 4.1 s, zoom 1.9-3.3 s.
- `amigagfx_dump_screen` produces garbage on a deep screen (it reads the 8-bit bitmap): use the QEMU
  screendump on MorphOS.

## 2026-11-08 — armies and fleets on the map

- `src/sprite.c` (.spr text + strip bitmap, colour key 0,255,0; pure red = ground shadow, not drawn
  yet), `src/units.c`: one standing sprite per province with armies / fleets, nationality set from
  country.csv (fallback MIN). Host render `host_units_z2.png`; Amiga snapshot `amiga_units.png`.
- Opaque-span sprite drawing (identical output): units cost ~23 ms per redraw (16 scroll steps
  2140 ms). Before: palette inverse 940 ms at start; units added ~75 ms per redraw
  (16 scroll steps 1800 -> 2980 ms) - per-pixel key test, to be replaced by RLE spans.
  MIN sprites are not yet recoloured per country (Armypalette*.bmp) - NIEPEWNE how the game does it.
- Harness bug found: start-eu2.ps1 hard-coded `work/jobs`, so starting the AROS guest wiped the
  reference guest's queue and job-eu2 refused ("guest not up") - an Out-Null hid it and an old
  binary's log was read as a result. Fixed: Work: folder taken from the config.

## 2026-11-08 — AROS 68k runs the same binary

- `winuae/eu2-aros.uae`: AROS m68k nightly ROM + system (local copy of `E:\aros\sys` in
  `C:\temp\amiga_eu2\aros-sys`), own Work: (`work-aros`, data via junction). `eu2` bench with
  `gfx=rtg`: RTG 800x600 `$10041100`, LockBitMap, world load 2.3 s, map + status bar correct
  (`C:\temp\amiga_eu2\shots\aros_bar.png`, from the game's own frame dump). No code change needed.
  MorphOS (QEMU lab) not tried yet.

## 2026-11-08 — status bar with the original EU2 font, date ticking, province selection

- `src/font.c`: EU2 fonts are a W-wide strip of WxW cells from '!'; pen 1 (+ odd pen-0 pixels) = ink,
  advance = rightmost ink + 2 (first guess "pen 0 = width marker" was wrong: only some glyphs have
  it, the rest got 14 px - "Februa ry"). Screenshot `C:\temp\amiga_eu2\shots\amiga_bar.png`.
- Space pauses/unpauses (one day per 150 ms, real month lengths - NIEPEWNE), left click selects a
  province (name + owner in the bar), drag scrolls, cursor keys, +/- and wheel zoom, Esc quits.
- Lesson: `build.sh | tail` hid a failed build and the guest ran the OLD binary. The build step
  now greps for "built and deployed" and stops otherwise.

## 2026-11-08 — the 1419 political map on the Amiga (AGA 640x480), scroll + 3 zoom levels

- `src/lightmap.c` (tile decoder, port of tools/maptool.py): level 3 decoded in C on the host is
  **identical to the Python reference on all 8 638 464 pixels** (shade and ids, 0 differences);
  the 68020 build gives the same checksums (`shade a1c536ae ids 28f3bccb`).
  Full level 3 on the reference config: 14.9 s = 1.77 ms per tile (each tile also decodes 3
  neighbours for the shading edges) - to optimise.
- Colours: `Map/colorscales.csv` holds 26 scales (my first loader lost "Red" because its header
  line looks like the column header). Country colour names map to scale positions exactly like
  eu2.exe 0x4690e0 (unknown -> LightOrange). 26 scales x 8 light levels = pens 32..239, 4x4
  ordered dither between levels.
- `src/mapview.c`: direct-mapped tile cache (32x32 slots of 32x32 pens), redraw = memcpy.
  `eu2` with `bench=1` on eu2-ref020.uae (020, no JIT, -70 %), AGA 640x480:
  world load (DB + Grand Campaign) 3000 ms; first frame 1480 ms (336 tiles); scroll step of
  32 px: 16 new tiles, draw 60-120 ms + c2p blit 20-40 ms; zoom change 860-1680 ms; cached
  redraw 0-40 ms. Screenshots from the Amiga's own frame dump:
  `C:\temp\amiga_eu2\shots\amiga_map_z1..3.png` (England red with Normandy/Gascony/Poitou in 1419).
- `test_world` on the Amiga: all 8 scenarios load in 19 s total, output identical to the host.
- Fused shade->pen tile path + flat-leaf fill (identical images, checked on host at all 3 zooms):
  first frame 780 ms, zoom change 0.5-0.9 s. Phase split on the Amiga, whole L3: leaf decoding
  2.4 s of 12.3 s - the per-pixel bilinear shading is the cost (~1.2 us/px on the reference
  config); about half of the interpolated pixels are in flat leaves. RTG 800x600: first frame
  1120 ms (520 tiles), scroll step ~74 ms total, blit ~0.
- Leaf cache (2 tile rows): first frame 1480 -> 880 ms, zoom change 0.9-1.0 s, scroll draw ~60 ms;
  checksums unchanged (host and 68020). test_lightmap time is dominated by its own per-pixel hash.


## 2026-11-08 — first Amiga binary shows the EU2 title screen (AGA and RTG)

- `eu2` 0.0.1 (64 KB), platform layer from `Ami_ReMoM/native` unchanged, our `src/pak.c`,
  `src/eui.c`, `src/main_amiga.c`. Build `wsl sh /mnt/c/temp/amiga_eu2/w.sh build/build.sh`: no
  warnings, libm881 gate clean.
- Test rig: guest started once (`winuae/harness/start-eu2.ps1`), `Work:jobrun` (native/jobrun.c)
  runs scripts the host drops into `Work:jobs/next` (`job-eu2.ps1`). No emulator restarts.
- Reference config `eu2-ref020.uae` (020, no FPU, no JIT, -70 %), `gfx=aga`:
  AGA 640x480 HIRES INTERLACED ($00008004), chip 300 KB. `load_800.bmp` (RGB565 800x600):
  palette inverse table 1020 ms, read 0-20 ms, decode+map 440 ms, scale+blit 140 ms.
  Screenshot `C:\temp\amiga_eu2\shots\aga_load.png`.
- `gfx=rtg`: uaegfx 800x600x8 `$50041000`, LockBitMap+memcpy; inverse 780 ms, decode+map 300 ms,
  blit 100 ms. Screenshot `rtg_load.png`.
- Colours are a placeholder 6x7x6 cube (banding in the background map) - the real game palette is
  still to be designed; the 1 s inverse table belongs in the converter.


## 2026-11-08 — eu2conv 1: the GOG installer converts end to end

- `setup_europa_universalis2_2.0.0.1.exe` is Inno Setup 5.5.0 (unicode). Loader table found by magic
  `rDlPtS\xCD\xE6\xD7\x7B\x0B\x2A`; header at 310638835, data at 242688; layout
  magic(12) rev ? exe exe_size exe_crc header data crc(@40, over the first 40 bytes).
- 5561 data entries of 74 bytes (2-byte option set). Every file is its own LZMA1 chunk (`zlb\x1a`).
  `innodump setup.exe verify`: **5543 SHA-1 good, 18 bad** — the 18 are x86-filtered exe/dll
  (flag 0x10), which we never use.
- innoextract 1.8 writes 5561 files, 1:1 with the data entries; `knownfiles.h` keeps the 5413 the
  port uses (SHA-1 + size).
- `build/host/eu2conv /home/angree/eu2setup.exe /home/angree/eu2out`: 2874 images, 59 sounds,
  53 music tracks, 2423 data files, **0 errors, 24 s**. Output 191 MB: text.pak 8.6 MB,
  map.pak 23.7 MB, gfx.pak 44.4 MB (from 108 MB of BMP), sfx.pak 2.4 MB, music ~112 MB.
- `tools/paktool.py check`: 2874/2874 EUI1 and 59/59 EUS1 decode. Pixel compare with the originals:
  8-bit images identical (maxdiff 0), RGB565 maxdiff 7 (expected). The RLE8 `T-STRAIT.bmp` differs
  only because **Pillow decodes it as all zeros**; ours gives the boat (raw RLE stream checked by hand).
