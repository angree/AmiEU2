# LEFTOFF — hand-off (read first)

## TODO — 2026-11-10 morning (user's list, in this order; one change per build, screenshots 1:1 from the game's buffer)
User: keep the code as light as possible (old CPUs); performance tests wait (reference guest busy / second PC); "out of scope" items stay out.

1. DONE (next commit): **Land dithering does nothing visible** (user) - band_phase looked only 9 entries deep, a brightened province (shade > 15, e.g. the economic map) got no diffusion at all; band centres were half a step off.: the land shows plain light bands, only the sea is
   dithered. `src/lightmap.c` `fs_pens` (60 % error diffusion): suspect the band-centre arithmetic vs
   the scales' 8 levels and the error reset per 32x32 tile. Proof: before/after of the game's own
   buffer (snap -> topng), political and economic mode, zoom 1-3.
2. DONE: mv_select (only that province's table, only the cached tiles in its boundbox). **Selected province drawn darker** (SPEC_MAPMODES §1.5, 0x482870): after colouring, shade -= 16
   (min 0) for the selected province unless mode == 4 (trade). Not implemented in mapmode.c.
3. DONE (PROGRESS 2026-11-10 RAM): 16.5 MB Fast with zooms 2+3, 27.8 MB with zoom 1 too, Chip ~1 MB, no leak over 5 years. **Measure the game's total RAM** (lightmaps alone: L1 11.3 + L2 4 + L3 1.7 MB compressed).
4. DONE for eu2-aros (660x520 window: the game buffer appears pixel-exact at +19,+8 - measured against snap, 98.6 % of the frame equal, the rest are the last 9 columns that AROS itself does not display; 640x480 is also 1:1 but WinUAE then cuts the top 12 rows; gfx_center_* and filter offsets have no effect). eu2-play.uae: the user's, same numbers apply (NIEPEWNE on Kickstart). **WinUAE window scaling**: 640x480 shown in an 820x620 window gives 1- and 2-pixel-wide pixels
   (looks like a broken resize). Set the AGA configs' window to an unscaled size; `eu2-play.uae` is
   the user's (ask before touching it).
5. DONE: **Rivers are drawn white instead of blue** (user) - special pixels' bits 5..8 name neighbour b of the province (0x476f84; adjacency[P][b] matches id.tbl's river ids in 95 %), and rivers count as known with a known neighbour (NIEPEWNE).
6. Tutorials 7, 8, 9 DONE on AROS (generators C:/temp/amiga_eu2/gen_c7/8/9.py; chapter 9 needed the Ledger under its messages). Was: Tutorial 7 stalls at C0 (IsButtonPress BUTTON_SET_BUDGET after click 138,20; generator
   `C:\temp\amiga_eu2\gen_c7.py`); chapter 9 not run yet.

Release gate (CLAUDE.md requirement): `python3 build/release-gate.py <dir>` (or `--git`) refuses any
file with the SHA-1 of a GOG v1.09 file (knownfiles.h, so renamed originals too), any EU2P / EUI1 /
EUS1 / EUM1 file (converted data), and the original-only types (.bik .eug .ai .tbl .csv, eu2.exe,
the installer). Every release script must run it on the tree it packs.

## 2026-11-10 late — user list items 4/5/8/10 done (commits 389db6b, 43d7022, 09aa205, bc0a8b3)

eu2setup = the settings tool (build.sh builds and deploys it beside eu2). Keys it writes: gfx, rtgmode,
titlebar, pointer, sound, audio, intro. Open: a release package must ship it (with an icon); the
system pointer on real AGA (64-wide sprite) is untested; in-game popups at sizes above 800x600 still
sit in the 800x600 layout's place (only the frontend is centred). work-play has the new eu2 + eu2setup
(old binary in C:/temp/amiga_eu2/backup_work-play_2026-11-10).

## 2026-11-10 night, late — wide RTG, fog of war, unit animation (commits bffb0af, 65e20aa + this one)

Done from the user's messages: 16:9 RTG sizes, centred top bar, map under the sidebar, fog of war as
in the exe, unit animation (eu2.cfg anim=, eu2setup checkbox; off by default on AGA). Open: in-game
popups above 800x600 still sit at 800x600 positions; release package with eu2setup + icon;
animation frame time (100 ms) NIEPEWNE.

## RESUME HERE — 2026-11-10 night (newest)

Since the afternoon: terra incognita for the AI, colonist rules, growth location term, AI money
reservation / war gates / strength indices (docs/SPEC_AI_STRENGTH.md), last-peace dates, tutorial
mode (docs/SPEC_TUTORIAL_MODE.md), domestic window (docs/SPEC_DOMESTIC.md), tutorial chapters 3-6
run through on AROS (PROGRESS has the driving notes). Open:
- Tutorial 7 / 8 / 9 run-through (the engine handles every action; the automation must wait for the
  dialogs - e.g. snap, check, then click).
- (done, diplo.c call_to_arms: 0x557a20 with the tutorial flag) AI "call allies".
- Message box placement of tutorial AddMessage: the default cascade start is NIEPEWNE.

## State 2026-11-10 afternoon

Commits 5314a9c, f7eaba7: AI explore plans (docs/SPEC_AI_EXPLORE.md) and the CoT lifecycle
(docs/SPEC_COT.md), both from agent-written specs, hs2 exact, soaks fine. Open from them:
- (done 4a05b85: may_enter for every country) The AI exempt from terra incognita.
- (checked 2026-11-10: soak to 1519 now has 14 rejections, all "no way by land" - e.g. POR Tago -> Narvik through land it may not pass; the log line "rejected for exploring" names the reason) 135 land explore offers rejected per century.
- CoT: R+0x36 meaning, the free merchant on founding, global embargo flag +0xfda (NIEPEWNE/NIEZNANE).

## State 2026-11-10 midday

Commits add4c89..03e75ee: trade-minded predicate exact; AI envoys from docs/SPEC_AI_ENVOYS.md (merchant
agent, colonial agent, missionaries, trade agreement offers; state saved as `amieu2_envoys ai/ta`).
AROS run to April 1420 clean (sm1.png). **Next: the AI explorers** - docs/SPEC_AI_EXPLORE.md is being
written by an agent (plans 0x32/0x33/0x34 of the colonial agent). Without them the AI never learns new
land and colonises almost nothing (13 colonies by 1519; re/colscan.py on save_l.eug counts them).
Open in the envoy spec: tag 0x52 filter, Russia's U+0xfec, the King pools (modelled as a resource tick).

## State 2026-11-10 night, second round

Commits f9f02bb..c34ddd5 (PROGRESS.md has the facts, STATUS items 125-131). Decoded from the exe and
implemented, each with hs2 exact and a 100-year soak:
- Blockade state (ports), its tax / production / starvation / supply effects; PIW blockade + siege art.
- Province tax − 0.05 × revolt risk (rev_risk via a callback into the economy).
- Naval attrition (0x4896f0 / 0x489470 / sea supply 0x483570); value shown in the navy window.
- Monthly weather 0x4825b0 (wind / storm / winter by climate); sea window, sea supply, attrition.
- Looting 0x4861a0 / 0x486210 / 0x4862a0; population growth terms of SPEC_TIME_ECONOMY 8.1.
- Maximum attrition 0x4837f0 with the supply-base test 0x483b60; land supply value 0x483570.
- Save fixes found on the way: REB / NAT / PIR and countries with VP always saved; battles keep their
  slots (bslot); blockade / pirate / hostile flags saved (wars change after mil_day).
Debug recipe for X != Y: `TSDAYS=n` bisection, then an mrand caller trace or a targeted fprintf
(PROGRESS 2026-11-09 midnight). Run bisections in a private dir (/tmp/sb), one at a time.

Later the same night (603b4ad..d6c56a0): supply friends decoded (0x4cbe90 = vassal of, 0x4cb900 =
friendly), loot destroys a manufactory 5 %, the Ledger's books (12 income / 16 expense categories,
this / last year; pages 9-12 incl. the category line charts), vassals pay half their tax.
Note: hs.sh shows only 20 compile lines - a hidden error leaves the old binary (check the error list).
Then (de16909..95e0a8b): manufactory sub-panel, game texts in the build / loan dialogs, bank loans
decoded, AI budget decoded (watch the rebel count, PROGRESS), + 5 stability progress per academy.
Then (67e1cf1..880c299): exe country indices = country.csv row + 1 (PAP / KNI / STE / BOS / ENG / HOL
fixed in ai.c and economy.c), peace badboy and forced conversion (badboy, VP 20 / 100 for a featured
country, -100 with co-religionists, tolerances). hs.sh now deletes the old binary first.
Then (cf99e1e..a692f9e): war-end VP (+-2v, v = 0.05 |score|), border shares (+0x14de, was called
"grievance") in relations / AI, naval power, overseas trade factor and the no-land-connection tax.

SOLVED (9728f69, ad86866): the rebel growth came from MAX_BUILD 128 (one queue for the world: the AI
got 96 slots in all) - now 1024, rebels 22-42 all century; England had no AI at all (the exe's
country slot 0 is empty, ours is ENG: loops from 1, c > 0, valid_tag); rebels now dissolve instead
of retreating. `test_save ... l` writes save_l.eug; re/rebscan.py summarises the rebels in it.

Open from this round:
1. 0x47f400 is the CoT-site value (not membership); membership is still "nearest CoT" (NIEPEWNE).
2. Population growth location term; the two calm regions of the wind (−5); `0x4cbe90` /
   `0x4cb880` read as access / military alliance (NIEPEWNE); loot's manufactory effect 0x4d97c0.
3. The build-manufactory sub-panel (SPEC_UI 3.3, positions known) is still a dialog page; the PIW
   positions of the manufactory and officials buttons are NIEZNANE (not in the layout files).
4. The earlier list below (tutorial 3-9, CPU on the reference guest when the user is not playing).

## State 2026-11-10 after midnight

Commits 8cb7c56..388330b (facts in PROGRESS.md, Polish summary STATUS items 122-124):
- Military access rule `may_pass` (0x45f730) gates the AI's moves as well as the player's.
- Save hole fixed: a country that is not present but controls land or has units (NAT, REB, PIR, an
  occupier without land) is saved with `amieu2absent = yes`; before, its provinces came back
  uncontrolled. Debug recipe in PROGRESS (TSDAYS + mrand caller trace).
- Pirates (docs/SPEC_PIRATES.md): spawner, mover, always hostile, P per port (trade -0.5/ship,
  income value -0.5/ship without a fort), pirate naval tech, PIRATES news, Commission Privateers
  button in the sea window. Market prices are saved now.
- Timed casus belli (DF_CB_TMP + World tcb, `amieu2cb`): privateers 3 months, leaving an alliance
  12 months, event `casusbelli` for its months.

Open from this round:
1. (done: blockade state and effects)
2. (checked: no overlap - the button ends at y 356, the bottom panel starts at 357)
3. (done: SEA_WIND and the privateer tooltips)
4. The other permanent CBs (break vassal, cancel trade, revoke access, ban) have no known duration.

## State 2026-11-09 evening/night

A Stop hook (`.claude/hooks/stop_until_1to1.ps1`, global settings.json) keeps the session working
until `C:\temp\amiga_eu2\PORT_1TO1_DONE.txt` exists (`HOOK_PAUSE.txt` pauses it).
Done since the afternoon (commits 5f85494..cbd6789; facts in PROGRESS.md, Polish summary in
`C:\temp\amiga_eu2\STATUS_2026-11-09.txt` items 76-106):
- UI: CC0 fonts (X.Org misc-fixed 6x9 / 6x10 / 5x8, proportional), AGA pointer pens 17-19 from
  prefs, software game cursors (56x41, all kinds), Floyd-Steinberg 60 % map dither, fog of war /
  terra incognita, map modes per SPEC_MAPMODES, unit pick / rubber band / route ribbon / waypoints
  (Shift) / groups (Ctrl+1..0), frontend country panel, credits, the tutorial engine (SPEC_TUTORIAL).
- Combat: naval battles on the land engine, assault as a battle with the real garrison, stalemate
  rule, KILL_LEADER (1 % a side per phase, never dealt again), Tordesillas sieges without war.
- Military AI v2 from the exe (King / Generals / garrisons; no naval plans yet) - an agent's work.
- Small rules (docs/SPEC_SMALLRULES.md): war taxes + bank loan buttons, world flags 0..6 (religions,
  effective province religion, tolerance, hostility, CB, Edict), casus belli 0x4ce3b0, envoy travel
  times and outcome on arrival (pending envoys saved), first-discovery specials, join-war truce,
  COUNTRY_BROKEN, AI religion conversion / Defender of the Faith, OTHER news (capital-known rule).
- Revolter countries + defection (docs/SPEC_REVOLTERS.md): revolt.txt, Province.ctl_day.
- Budget one-time increases (docs/SPEC_BOOST.md).
- Events only when the scenario includes db\events.txt (tutorials have none); scenario manpower.
- Saves stay exact (hs2 A == A2, X == Y): AI map knowledge now saved (known = k<hex>), owners
  always know their provinces.

Later the same night (commits 08550b3..7379274): naval AI plans (agent), event commands that were only
counted (relation, alliances, revolts, inherit, war, independence, monarchs / leaders wake & sleep, tag
change CAS -> SPA by an identity swap, flagname, loansize, cultures), accepted cultures, event triggers
monarch / leader / neighbour / discovered, HRE (docs/SPEC_HRE.md: election, bonuses, AI), boosts,
support limit + surcharge, loan due dialog, PEACEPROP herald, OTHER news, Ledger leaders page.

### Missing / approximate now, in order
1. (done) Naval AI plans, army splitting - see the NIEPEWNE list in military.c (NAVALATTACK never engages,
   invasions limited by ship capacity).
2. Military AI CPU: ~4.6x v1 on the host (agent estimate ~360 ms a bench year on the reference);
   measure on eu2-ref020 when the user is not playing there.
3. (done: naval battle window with ships + smoke ui.c 2463, explorer / conquistador portraits ui.c 2306)
4. Tutorial chapters 3-9 run-through (ch. 1 done, ch. 2 up to "Splitting a Unit" on AROS).
5. Remaining NIEPEWNE in the new specs (revolter ctor values, boost lock at game start, etc.).
6. Multiplayer (not planned), live comparison with the original, balance play-test.

## State 2026-11-09 afternoon

Done since the morning (commits 7f58ea5..8aaafcf; details in PROGRESS.md):
- **The diplomatic AI decoded from the exe** (docs/SPEC_AI.md, src/ai.c): strategic mode
  (BUILDUP / COLONISE / DOMINANCE), worst enemy (score H), the war predicate, marriages, vassals,
  annexation, alliances, cancel-vassalage, embargoes, pressure plans (insult / warning / ask access),
  per-country tick dates (the exe's 0x5402fb interval; war ticks floored at 30 days for CPU).
  Answers to every proposal (alliance, marriage, vassal, annexation, access, trade, loan, loan due,
  call to arms, joining). AI peace: when it offers (0x53f240 / 0x5382f0), what (0x539e10), how any
  offer is answered (0x4ccda0 + 0x5366c0, the player's too), war score of a pair (0x4d0800).
  AI proposals to the player as message dialogs (ROYMARPROP, MILALLPROP, MILALLINVPROP,
  MILALLJOINPROP, WPINVPROP); answers / acts toward the player as message types.
- War exhaustion (8.4) with its stability / revolt terms; badboy decay and research by the monarch.
- Market (9.1): supply / demand / price with the exe's goods ids; production and trade use it.
- Revolt risk: nationalism by years since a province changed hands; the bankruptcy year.
- Bench (reference): AI ~4 s a bench year spread over the days (month steps ~3.5 s). ai.c is built
  with -O2 (Amiga twin test identical to the host). The month data of the AI is rebuilt when a game
  is saved and loaded, so a game going on after a save runs exactly as the loaded one (X == Y).

### Missing / approximate now, in order
1. Military AI of the exe (CAIMilitaryMinister: conquer / garrison plans with the .ai weights,
   BuildPlan recruitment, fleets) - ours still; huge (0x541xxx..0x55xxxx).
2. Victory points: the categories (an agent decoded them into C:\temp\amiga_eu2\re\vp.md -
   implement: per-category VP x1000, Ledger Basic VP table, the awards).
3. Assault: the exe runs it as a battle of type 5 (assault tables, no cavalry, fort level off the
   attacker without a breach, morale x1/3); the garrison size is NIEZNANE - ours is a dice roll.
4. AI details marked NIEPEWNE in SPEC_AI (grievance table +0x14de not kept, strength indices, the
   0x43ce30 flag, "known capital" = neighbour or same continent); claim throne / guarantee actions
   are not ported (the AI skips them); proposals of vassalisation/annexation/access to the player
   have no dialog (the AI does not send them).
5. Colonisation speed / travel time (SPEC_ENVOYS 2.4 NIEPEWNE); war taxes; reformation flags.
6. Not ported: Tutorial, Multiplayer, Credits, naval battle smoke, leader portraits in the army
   window, more message types (the "OTHER" news types).
7-9 as below (live comparison, balance play-test, the Bink provenance decision).

## State 2026-11-09 morning

The game is playable end to end on the Amiga (AGA 640x480 / RTG 800x600), AROS 68k and MorphOS 3.20.
Saves are exact (host == Amiga, and save -> load -> save of the full game is byte-identical).
Reference bench (020, no JIT, -70 %): month steps 2740 ms a bench year (diplomacy 1060, economy 520,
military 340, revolts 200). Docs written from the exe this session: docs/SPEC_UI_WINDOWS3.md,
SPEC_UI_WINDOWS4.md, SPEC_EXPLORERS.md, SPEC_BATTLE_MERC.md, SPEC_REVOLT_RELIGION.md.
Status for the user (Polish, numbered 1..68): C:\temp\amiga_eu2\STATUS_2026-11-09.txt.

Done this session (details in the "Done since" lines below and in PROGRESS.md): battle / siege /
peace / VICTORY / OPTIONS / purchase / religion / event choice / trade good / loans / vassal windows,
separate peace, AI invasions, explorers + conquistadors + terra incognita, admirals, mercenaries,
split / merge / disband, save slots, right-click menu, message settings + ~25 more message types +
message log, decoded revolts + religion, intro stills (eu2conv Bink decoder -> data/intro.pak).

### Missing / approximate (morning list; see the newer list above)
1. AI (all NIEPEWNE, ours): war declaration / alliance / peace choices, answers to the player's
   proposals (diplomacy, loans), recruitment mix, fleets beyond invasions. Decode the exe's AI
   (the .ai files are read for `war` and `combat` only).
2. Victory points: most VP categories (SPEC_MISSIONS 2.1: economy, peace, diplomacy, establishments,
   discovery bonuses SPEC_EXPLORERS 4.3); the end-of-year VP table.
3. Market: prices are the base values; supply / demand (goods.csv, 0x41ad30) not simulated.
4. Revolt-risk terms not kept: years since acquisition (nationalism 3/2/1), war exhaustion x m,
   war taxes, the bankruptcy year; reformation world flags (approximated by "someone holds the faith").
5. Assault resolution (ours), loan answers (ours), colonisation speed / travel time (SPEC_ENVOYS 2.4
   NIEPEWNE), the peace war score for a separate peace (uses the war's).
6. Not ported: Tutorial, Multiplayer, Credits (names are only in the exe), naval battle smoke,
   leaders shown in the army window portrait, ~180 of the 209 message types.
7. Never compared live with the running original (the comparison VM did not work).
8. A long human play-test for balance has not been done.
9. PROVENANCE decision for the author: tools/eu2conv/bink.c's coefficient decoding was written from
   general knowledge of the Bink format (the public description lacks it) - not copied, but not a
   strict clean room.

### Working notes that bit this session
- Bash-tool heredocs collapse `\\` to `\`: write Python patch files with the Write tool
  (C:\temp\amiga_eu2\re\*.py), use chr(10) / Edit for C escapes.
- Test hooks in eu2.cfg: debugwar=TAG, debugsel=1|2 (army in battle/siege), debugpeace=TAG,
  debugend=1, intro=0, menu=0, bench=1. autoinput holds 512 actions (`key 1` closes popups,
  `key 64` = space, `key 69` = Esc).
- Host tests: hs.sh (save exactness), hl.sh (100-year soak), tw/td/tpw/tm.sh; Amiga twin:
  `Work:test_save a` + `b`, compare with /home/angree/sv/save_x.eug.
- MorphOS lab: run E:\morphos_eu2\stop.ps1 before run-test.ps1 (an old QEMU kept port 4559); the ISO
  is built by C:\temp\amiga_eu2\iso2.sh (its eu2.cfg needs menu=0).
- After converting, copy /home/angree/eu2out/intro.pak to C:\temp\amiga_eu2\work\data.

## State 2026-11-09 night (on top of the 11-08 state below)
The in-game interface now follows eu2.exe window by window (layouts decoded from the disassembly,
docs/SPEC_UI.md 4.3-4.10; the reading helper is C:\temp\amiga_eu2\re\symx.py + annot.py):
- 7 map modes (src/mapmode.c), sidebar buttons as in SPEC_UI 2.2 (country F2, land/naval military
  with the maintenance slider, budget, financial summary), rollover help (buttons.txt keys), minimap.
- Province window buildings: barracks = recruit, port = ships, fort = expand. Foreign province and
  sea zone windows. Header line 2.
- Diplomacy = overview + action panel over the map + confirm popup; diplo_action() runs gifts,
  letter, warning, insult, marriage, alliances, access, embargo (vassals/annex/trade agreements/loans
  are listed but greyed).
- Army and fleet windows (SPEC_UI 4.10), selection ring + order mark on the map, sea transport
  (board = right-click our fleet's sea zone with an army selected; Unload Troops / key u), top bar
  rollover help.
- Done since: envoys (SPEC_ENVOYS) + AI envoys, the Ledger (SPEC_LEDGER), DECLWAR message, game
  speed (Ctrl +/-), monarchs (src/monarchs.c, country window).
- Also done: generals (src/leaders.c), bank loans, vassal/annex/trade agreements, notes -> messages
  (DECLWAR, MERCLOSS, MISSOSUCC/FAIL).
- Done since: revolts/rebels (src/revolts.c), trading posts, the Ledger history in the save, battle /
  siege / mission messages, missions (src/missions.c, SPEC_MISSIONS), map borders / landscape / cities /
  core shields, map tooltip, the game opens on the normal map.
- Done since: province improvements (src/buildings.c, SPEC_BUILDINGS) + AI, discovery / terra
  incognita (World.known; only the player's is saved, so only the player's envoys depend on it),
  siege towers on the map, frontend INFO, the end of the game (debugend=1 to test).
- Done since (docs/SPEC_UI_WINDOWS3.md): the battle window (iw_battle: land battle + siege, opens when
  the selected army fights; debugsel=1 picks one), the peace window (iw_peace: tribute direction, the
  offer with provinces / money / vassal / religion / access, annex; debugpeace=TAG opens it), the
  frontend VICTORY / OPTIONS popups (src/options.c, kept in Work:eu2opts.txt; speed, fog, forced
  annex, dynamic missions, base VP, autosave to save/autosave.eug, event kinds all act on the game;
  difficulty / AI aggressiveness stored only - their effect is NIEZNANE).
- Done since: the message log (5 lines over the map, from the Ledger history), rollover help in the
  battle / peace windows, AI invasions by sea + fleets returning home (ours), messages from the
  player's state (tech, stability, monarch, improvements, losses, generals), conquistadors /
  explorers (docs/SPEC_EXPLORERS.md: they arrive with an army of 1000 / a fleet of 3; terra
  incognita rules and discovery on arrival for the human player; the AI is exempt because its map
  knowledge is not saved), conquistadors in colonisation and attrition.
- Done since: event unit commands (infantry ... explorer), admirals on fleets, split / merge /
  disband + stack arrows in the army / fleet window, siege buttons (Assault - our resolution, NIEPEWNE -,
  detach siege / cover force), save / load slots (quick, auto, 1-8; the frontend lists them), the
  right-click province menu (RCDM_*), message settings (box / log / pause per type, menu button
  "Messages").
- Done since (docs/SPEC_UI_WINDOWS4.md): purchase windows (iw_purchasearmy / navy) with the decoded
  unit prices (policy + difficulty) and province capacity, the religion window (src/religion.c,
  tolerance saved; its revolt effect and the conversion cost are ours), the event choice popup with
  EE_* effects on hover, the trade good window (econ_goods; no market model: supply / demand 100 %),
  Offer Loan between countries (ours: answer / repayment), PEACEACCEPT / REJECT / WHITEPEACEUS.
  MorphOS 3.20 checked again (QEMU lab: the old QEMU of the day before still held port 4559 - stop.ps1
  first; the ISO's eu2.cfg needs menu=0).
- Done since: Grant Independence (iw_Vassal) + Create Vassal, AI recruiting by the province
  capacity, rollover help in purchase / trade good / religion, NBWIN / NBLOSS / LIBPROV / ARMYARRIVE /
  FLEETARRIVE (arrivals and stability log-only by default), separate peace (enemy shield toggles
  leader / alone; a non-leader settles apart).
- Done since (docs/SPEC_BATTLE_MERC.md): mercenaries (one market, the purchase window's RAISEMERC
  line), the battle window's figures (map sprites in FIRE pose; the window has no leader display in
  the exe), DECLWAROTHER in the log.
- Done since: revolt risk / revolt roll / tolerance defaults / conversion / Defender as decoded
  (docs/SPEC_REVOLT_RELIGION.md), the intro stills: eu2conv decodes the Bink videos (tools/eu2conv/
  bink.c, tables read from the player's binkplay.exe) into data/intro.pak, the game shows them before
  the menu (Esc skips; intro=0 turns it off). Copy intro.pak into Work:data after converting.
  PROVENANCE (for the author to decide): bink.c's coefficient decoding was written from general
  knowledge of the format, since the public description lacks it - not copied, but not a strict
  clean room either.
- Open: more of the 209 message types, the naval battle's smoke animation, the siege buttons (assault / detach / cover), the peace
  window's leader / separate-peace toggle, tooltips of the new windows.
- GOTCHA (tests): the autoinput clock starts before the first frame is drawn, and a `snap` blocks the
  game ~1 s while the 1.4 MB PPM is written to the shared folder - leave 2-3 s between an action and the
  snap that should show its result, or the snap shows the state before it.
- GOTCHA: host tests run with a 64-bit `long`; the 68k has 32 bits. `v * (1000000 + inflation_u)`
  overflowed only on the Amiga (a 208-ducat cost showed 1). Keep products of x1000 values below
  2^31 (split as `v / 1000 * k + v % 1000 * k / 1000`) and check new money paths on the Amiga.
- Envoys (src/envoys.c, docs/SPEC_ENVOYS.md): merchant / colonist / missionary windows open from a
  province click in the trade / colonisation / religious map mode.
- A guest once hung idle right after "display: RTG" (job78, 2026-11-09 00:24); a restart of our
  guest fixed it and it did not come back. If it does: suspect the shared-folder handler.

## State 2026-11-08 ~19:30
The game is playable from the menu on the reference machine (eu2-ref020.uae: 68020, no FPU, no JIT,
-70 %), AGA 640x480 and RTG 800x600; the same 68k binary runs on AROS 68k and MorphOS 3.20.
- Frontend: main menu over bg_start, single-player setup (7 scenarios, selectable countries' shields).
- Map, units, sidebar, top bar, province window; event dialog; diplomacy window (declare war / white
  peace / demand / surrender); AI peace offers; budget window (6 sliders); domestic policy window
  (8 sliders, one step per 10 years); recruitment (army button on an own city).
- Simulation: events, economy (+ AI budget), war (movement, CRT battles, morale, retreat, sieges,
  battle points, AI armies, recruitment + AI recruiting), peace (war score, AI terms, truces),
  diplomacy (drift, declarations with call to arms, AI wars from the .ai files, AI alliances and
  royal marriages).
- Save/load: F5 / F9 (PROGDIR:save/quick.eug), exact: test_save proves A == A2 and X == Y on host
  and Amiga. 100-year soak (`test_save l`, host ASan) clean.
- Sound: sfx + raw 8-bit PCM 28 kHz music (user decision), conversion 2.
- Tests without host input: `Work:autoinput.txt` ("<ms> key|click|rclick|move|snap ...");
  eu2.cfg `menu=0` (skip the menu), `select=<province>`, `country=TAG`, `gfx=aga|rtg`.
Facts, numbers, proofs: PROGRESS.md (newest first). Specs (prose from eu2.exe, never code):
docs/SPEC_EVENTS.md, SPEC_TIME_ECONOMY.md, SPEC_UI.md, SPEC_WAR_DIPLOMACY.md, MAP_FORMAT.md,
DATA_FORMAT.md.

### How to work
1. Guest: `powershell -File winuae\harness\start-eu2.ps1` once (AROS: `-Config eu2-aros.uae`).
2. Build: `C:\temp\amiga_eu2\t.sh` = build.sh + grep "built and deployed" (never trust `| tail`);
   run `wsl sh /mnt/c/temp/amiga_eu2/w.sh /mnt/c/temp/amiga_eu2/t.sh`. Host tests: gcc with ASan,
   build/host/test_* (test_econ, test_events, test_world, test_script, test_lightmap, test_mapview,
   test_ui). Amiga twins of the tests are deployed to Work: and compared line for line.
3. Run: `winuae\harness\job-eu2.ps1 -Script "Stack 2000000`nWork:eu2"`; Work:eu2.cfg keys
   gfx=aga|rtg, bench=1, title_ms, country, x y zoom, scenario. bench writes Work:snap_*.ppm.
4. MorphOS: E:\morphos_eu2 (run-test.ps1, port 4559, iso\eu2.iso built by C:\temp\amiga_eu2\iso.sh
   with only the century-14 music - mostest copies everything to RAM:; never touch other labs).
   AROS: eu2-aros.uae, Work: = C:\temp\amiga_eu2\work-aros (copy eu2 there; it runs Work:run once).
   Write the cfg/autoinput with LF endings; gotcha: a heredoc in the Bash tool turns `\\` into `\`.
5. Ghidra export (reading aid, OUTSIDE the repo): C:\temp\amiga_eu2\ghidra\eu2_all.c; raw-disassembly
   helpers (capstone, Windows python): C:\Users\Admin\AppData\Local\Temp\claude\...\scratchpad\t\.
Gotchas found today: sed with `\n` in a replacement inside C strings breaks the code (use the Edit
tool); `sed -i "${L}a ..."` with an empty L appends after EVERY line (happened twice: restore the file
with git checkout, then use the Edit tool); `w.sh` runs scripts with dash (no bash process substitution); starting a second guest used
to wipe the first one's job queue (fixed: per-config Work folder).

### Next, in order
1. Army window (§6: composition, morale bar, leader) and naval units (movement, combat §12).
2. Military gaps: leaders, attrition and supply (§10), reinforcement of damaged regiments.
3. Tooltips; message log; in-game menu (§9) with save/load slots instead of F5/F9 only.
4. Diplomacy windows for gifts, marriages, alliances, vassals; colonisation and missionaries.
5. Balance after playing: AI war/alliance rates, recruit cost/time (all marked NIEPEWNE in code).
6. Bink intros; startup ~4.4 s of parsing on the 68020 (binary cache candidate); load 7.6 s.
