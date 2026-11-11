# SPEC_SOUND — sound effects and music (eu2.exe v1.09)

Specification only (prose and tables). Every claim cites the eu2.exe addresses it was read from.
Dumps: `C:\temp\amiga_eu2\sr\snd_*.txt`, `sr\s_*.txt` (init `snd_init`, music `snd_music`,
pattern player `snd_pan` / `snd_ptick`, call sites `s_bwon`, `s_blost`, `s_ucomp`, `s_amove`,
`s_nmove*`, `s_detach`, `s_alert*`, `s_disb`, `s_chat`, `s_lead`, `s_col`, `snd_move`, `s_m1..3`).
Marks: **NIEPEWNE** = believed, not proven; **NIEZNANE** = not read.

## 1. The sound manager

- Global `0x89a760` = DirectSound manager (ctor `0x4c5650`, vtable `0x5ea294`), created in the
  start-up sequence `0x5b5ba6` at progress step "INIT_SOUND" (`0x5b681a..0x5b6865`).
- **`Config/Sfx.txt` is not read by the exe.** The string does not occur in eu2.exe; the table is
  compiled in: 65 calls `vfunc+4(id, "sfx\wave\<file>.wav", 0)` at `0x5b686d..0x5b6fc9`. Ids
  0x00..0x3E are exactly the 63 Sfx.txt keys **in file order**, each with the file Sfx.txt names.
  Two extra ids exist only in the exe: **0x3F `if_move_army.wav`**, **0x40 `if_move_cav.wav`**.
- After loading, the SFX volume is applied: `vfunc+0x38(settings+0x2e2)` (`0x5b700f..0x5b7037`).
- Vtable entries used by the game:
  - `+0x20` play(id) = `0x4c7380` → `+0x0c` (`0x4c5900`) with pan 0 (centre) and the manager's
    master volume. It is the **only** entry point the game uses (50 call sites, §3).
  - `+0x14 / +0x18 / +0x1c` (`0x4c7430 / 0x4c73e0 / 0x4c73a0`) = play one of 4 / 3 / 2 ids at random.
    **No caller** (all 193 references to `0x89a760` were checked): the `_1.._3` variants are picked
    only by the battle pattern player (§4), never by these helpers.
  - `+0x38` set SFX volume (`0x4c62f0`): master = v·100 (v = slider 0..100); each buffer gets
    DirectSound volume master − 10000 (hundredths of dB, i.e. (v − 100) dB; 0 dB at v = 100).
- Play `0x4c5900`: the id maps to its buffer (table at manager+0x16014, filled by the loader at
  `0x4c5f6a`); `SetVolume`, `SetPan`, `SetCurrentPosition(0)`, `Play` (not looping). With the flag
  manager+0x210+4·buf set it would look for a free one of 64 copies (`0x4c5927..0x4c595c`); the
  loader is always called with 0 and nothing else writes the flag, so **each id has one buffer and
  re-triggering an id restarts it** (NIEPEWNE). Different ids mix freely; there is no priority or
  channel limit in the exe.
- The options window applies the sliders live (`0x560908..0x560b62`): SFX `+0x30` → `+0x38`,
  music → `0x4c9590` (§5.4); on OK it stores them in settings +0x2e2 (SFX) / +0x2e6 (music).

## 2. Condition helpers used below

| Helper | Meaning |
|---|---|
| `0x434d90` (this) | this country **is the human player** (`[0x8bfa50]+6 == this+6`) |
| `0x4cabe0` (alliance, tag) | the alliance/agreement object contains the player's tag |
| `0x48f130` (side, country) | a combat side holds a unit owned by that country (unit+0xde) |
| `0x48fff0` (selection) | the selection contains at least one land unit (vfunc +0x14) |
| `0x48fad0` / `0x48fb20` | sum of infantry (unit+0xf0) / cavalry (unit+0xec) over the selection |
| province +0xca ≥ 5 | the province is a sea zone (terrain class), else land |

Country event handlers live in the country vtable (base/AI `0x5f55ec`, human classes `0x5eca54`
(ctor `0x524933`) and `0x5f6240` (ctor `0x567832`)); the event → slot dispatcher is `0x461e90`;
event names are from `C:\temp\amiga_eu2\re\evnames.txt`. Handlers in the **base** vtable run for
every country and test `0x434d90` themselves; handlers that exist only in the **human** vtables play
unconditionally (they only ever run for a human country). In multiplayer every human's handlers run
on every machine — whether remote humans' events are audible there is NIEPEWNE.

## 3. Sound effects: key → trigger

Id = exe id = Sfx.txt line order. "player" = the human player (`0x434d90`).

| Id | Key (file) | Trigger | Address | Conditions |
|---|---|---|---|---|
| 0x00 | if_click (if_click) | left-click on a land province (map target 5–8), a foreign province (9), a sea zone (0x0B) | `0x52c418`, `0x52c51e`, `0x52c563` | see SPEC_UNIT_UI §click; ports/CoT/own-unit clicks are silent |
| | | left-click on a province in **map mode 5** (diplomatic ties) whose owner exists → opens that country | `0x52bcd7` | `0x480e20` ≠ 0 |
| | | CoT window open, click on a map province with a CoT (prov+0xaa) → window switches to it | `0x515358` | CoT window `0x5151e0` |
| | | recall selection group: key 0..9 without Ctrl | `0x535194` | after the group is re-selected |
| | | list widget item clicked (`0x425450`) | `0x425811` | NIEPEWNE which widget (vtable pointer at `0x63e644`) |
| 0x01 | if_close | — never played | | |
| 0x02 | if_error (if_dragover) | move order rejected (no path) — with `illegalmoveorder.bmp` | `0x531342` | `0x530f70` |
| 0x03 | if_chat (if_blip) | — never played (no button gets it, §3.1) | | |
| 0x04 | if_blip | **Ctrl+0..9** stores the selection as group N | `0x53504f` | |
| 0x05 | army_select (if_click) | left-click on an own **army** (map target 1/3) | `0x52c25b` | unit not in battle (`0x52dae0`) and not besieging (`0x48df30`); not on Shift add/remove |
| 0x06 | navy_select (if_click) | same, own **fleet** | `0x52c2da` | same |
| 0x07 | army_move (if_move_art) | unit panel button 0x0D (create detachment) with a land unit → event REQUEST_CREATE_DETACHMENT 0x3F | `0x4f94ae` | panel handler `0x4f93c0` |
| | | fleet panel button 0x15: the armies carried by the selected fleet(s) become the selection | `0x4f3712` | ≥ 1 army aboard (`0x4f3580`, vtable `0x5eb404`); meaning NIEPEWNE ("select / unload troops") |
| 0x08 | navy_move (if_move_navy) | **move order accepted** for a selection without land units | `0x5312ea` | `0x530f70` |
| | | unit panel button 0x0D with a fleet → REQUEST_CREATE_WARSHIPS_DETACHMENT 0x40 | `0x4f9491` | |
| 0x09 | army_assault (if_assault) | siege panel button 0x11 → SIEGE_ASSAULT 0x22 (`0x4432e0`) | `0x500cbe` | siege panel `0x500c50`, at the click (before the assault rules) |
| 0x0A | army_build (if_build_structure) | OK in the **army** recruit window | `0x4f49a0` | `0x4f4980` (msg 0xA) |
| | | OK in the **navy** recruit window (also 0x0A, not 0x0B) | `0x4f5ac0` | `0x4f5aa0` |
| 0x0B | navy_build (if_build_navy) | — never played (the navy window plays 0x0A) | | |
| 0x0C | army_complete (if_done_army) | UNIT_FINISH 0x4B on a land unit | `0x48c4a2` | unit owner == player |
| 0x0D | navy_complete (if_done_navy) | UNIT_FINISH 0x4B on a naval unit (vfunc +0x18) | `0x48c499` | unit owner == player |
| 0x0E | army_disband (if_disband_army) | main-UI command case 3 = disband the selection, first unit is land | `0x5332e9` | `0x533230` (table `0x53339c`) |
| 0x0F | navy_disband (if_disband_navy) | same, first unit naval | `0x5332f2` | |
| 0x10 | army_victory (land_cheer) | PROVBATTLEWON 0x8F, battle province is land; with message 0x7E | `0x44d2bf` | winner == player and the loser country exists |
| 0x11 | army_loss (land_death) | PROVBATTLELOST 0x90, land province | `0x452bad` | loser == player |
| 0x12 | navy_victory (sea_cheer) | PROVBATTLEWON 0x8F in a sea zone; after message 0x80 | `0x44d342` | winner == player |
| 0x13 | navy_loss (sea_death) | PROVBATTLELOST 0x90 in a sea zone | `0x452bbe` | loser == player |
| 0x14–0x16 | battle_siege_1..3 (land_cannon1..3) | — never played (the siege uses ids 0x21–0x23, §4) | | |
| 0x17 | detach_siege (if_done_army) | siege panel button 0x12 → REQUEST_DETACH_SIEGE_FORCE 0x43 (`0x443080`) | `0x500c9c` | |
| 0x18 | detach_cover (if_done_army) | siege panel button 0x13 → REQUEST_DETACH_COVER_FORCE 0x44 (`0x442f80`) | `0x500c7a` | |
| 0x19 / 0x1A | battle_initland / battle_initnavy | — never played | | |
| 0x1B–0x26 | land_sword1..3, land_musket1..3, land_cannon1..3, sea_cannon1..3 | battle patterns only (§4) | `0x4c8b71` | |
| 0x27 | game_victory (if_tb_merchant) | — never played | | |
| 0x28 | game_loss (if_revolt2) | — never played | | |
| 0x29 | historical_event (if_event) | — never played (events are silent) | | |
| 0x2A / 0x2B | general_success / general_failure | — never played | | |
| 0x2C | game_monarch (if_war) | ACTIVATE_MONARCH 0xBD (new ruler) | `0x52f5bd` | human handler `0x52f590`; monarch found (`0x488350`) and monarch+0x3a == 0 (NIEPEWNE: regency / "silent" flag) |
| 0x2D | revolt_init (if_booh) | — never played | | |
| 0x2E / 0x2F | random_event1 / 2 | — never played | | |
| 0x30 | historical_leader (if_event) | ACTIVATE_LEADER 0xB2 | `0x52f664` | human handler `0x52f620`; leader vfunc +0x18 true and leader+0x3a == 0 (both NIEPEWNE); message 0x7B in the same branch |
| 0x31 | leader_killed (if_booh) | KILL_LEADER 0xB5; with message 0x7A | `0x52f73f` | human handler `0x52f710`; leader+0x2a ≠ 0 (NIEPEWNE: "has a command") |
| 0x32 | diplomat_failure (sea_init) | EXEC_DECLINE_ANNEXATION 0x103 | `0x449a08` | receiver == player |
| | | EXEC_DECLINE_ALLIANCE_OFFER 0x10A | `0x44a92a` | receiver == player |
| | | EXEC_DECLINE_ALLIANCE_JOIN 0x10B | `0x44ab47` | receiver == player |
| 0x33 | diplomat_success (if_explore) | EXEC_SET_ANNEXATION 0x102 | `0x4496a5` | receiver == player (whether annexer or annexed: NIEPEWNE) |
| | | EXEC_JOIN_ALLIANCE 0x105 (alliance, royal marriage, vassalage) | `0x449b9b` | receiver == player |
| 0x34 | diplomat_breach (if_eyr_init) | EXEC_LEAVE_ALLIANCE 0x106 | `0x44a1bc` | the agreement contains the player (`0x4cabe0`) |
| | | EXEC_DISHONOR_ALLIANCE 0x110 | `0x44ca9e` | the agreement contains the player |
| 0x35 | diplomat_alert (if_tb_colonist) | an **answer popup** becomes active the first time | `0x520a17` | popup base class vtable `0x5ec680` (ctor `0x520650` sets +0xb2 = 1, cleared after playing): alliance invitation, join war, call to honour, call to arms, peace offer, province exchange, loan expired, loan offer, annexation offer, trade agreement offer, military access request (subclass ctors `0x520ea2..0x523714`) |
| 0x36 | diplomat_war (if_tb_diplomat) | THEY_DECLARE_WAR 0xE9 | `0x44b7d5` | the attacked country == player (declaring war yourself is silent) |
| 0x37 | diplomat_peace (if_tb_trade) | EXEC_ACCEPT_PEACE 0x10C | `0x44edf1` | a country in the loop before `0x44edf1` (each refreshed by `0x43bdf0`) is the player |
| | | WHITE_PEACE 0x171 | `0x454f3d` | one of the participants is the player (`0x454f12..`) |
| 0x38 | colony_build (if_nope) | Send Colonist (`0x43efc0`) / Send Trader (`0x43f230`) accepted | `0x43f017`, `0x43f287` | vfunc +0x370 (send allowed) and sender == player |
| 0x39 | colony_success (if_done_structure) | COLONISE 0x9E (colony founded / grown) | `0x52ee9e` | human handler `0x52ee90` (also called directly from `0x568545`) |
| | | ESTABLISH_TRADEPOST 0xA4 | `0x52ed2e` | human handler `0x52ed20` |
| 0x3A | colony_failure (if_GIM_failure) | COLONISATION_FAILED 0xA0, COLONISATION_BOUNCED 0xA1, ESTABLISH_TRADEPOST_FAILED 0xA5, ESTABLISH_TRADEPOST_BOUNCED 0xA6 | `0x52ef1e`, `0x52ef8e`, `0x52edae`, `0x52ee2e` | human handlers |
| 0x3B | manufactory_success (if_done_structure) | BUILD_FACILITY 0x3D (building finished) | `0x52ead2` | human handler `0x52eac0`, unconditional, before the message 0x0D |
| 0x3C | improvement_done (if_done_structure) | FINISH_BUILD_IMPROVEMENT 0x15B | `0x454573` | receiver == player |
| 0x3D / 0x3E | merchant_success / merchant_failure | — never played (CoT merchant results are silent) | | |
| 0x3F | (if_move_army) | **move order accepted**, selection has land units and infantry ≥ cavalry | `0x531276` → `0x5312ea` | `0x530f70` |
| 0x40 | (if_move_cav) | same, infantry < cavalry | `0x5312db` | |

### 3.1 Buttons

The common button class (vtable family around `0x5e50cc`, handler `0x41eac0`) has a sound id at
+0x98 and a toggle state at +0x9c: on press (+0x82 == 0) it plays +0x98 if +0x9c == 0
(`0x41ed14`), on release-toggle if +0x9c == 1 (`0x41ed5e`). All constructors set +0x98 = −1
(`0x41e785`, `0x41e89d`, `0x41e97f`, `0x41e9fb`, `0x40a039`, `0x53e29e`) and no write of a real id
was found (the only constant store, `0x5b186f`, is on a revolt structure from `db\revolt.txt`).
So **ordinary buttons are silent** in v1.09 (NIEPEWNE: a register store elsewhere could still set
it). This agrees with SPEC_FRONTEND_CREDITS §5 (frontend plays no effects).

## 4. Battle patterns (combat animation sounds)

A pattern player (`0x4c89a0` ctor, 0x20 bytes: pattern, position, countdown, last id, divider
countdown, divider, x, y) steps through a table of 0x1C-byte records:

| Record +0 | Meaning (`0x4c8ad0`) |
|---|---|
| 0 | play: id = one of the ids at +0x0A/+0x0E/+0x12, random `rand() % n` with n = byte +0x18 (0 → first); countdown = byte +4; next record |
| 1 | end: the player is removed |
| 2 | goto record (byte +4), no wait |
| 3 | wait: countdown = byte +4, next record (no sound) |

Byte +6 (0x62 / 0x56 alternating) is not read by the player (NIEZNANE). Bytes +0x16/+0x17 are a
fixed volume/pan (0x80/0x80) used only when no position is set.

Tables (decoded from the exe):

| Table | Records | Used by |
|---|---|---|
| `0x624a78` swords | 9 × play {land_sword1..3} (waits 4,4,4,8,4,8,6,4,14), goto 0 | land battle, shock |
| `0x624be8` muskets | 9 × play {land_musket1..3} (waits 8,12,8,12,12,8,12,12,16), goto 0 | land battle, fire |
| `0x624d00` sea | 9 × play {sea_cannon1..3} (same waits as muskets), goto 0 | naval battle, both phases |
| `0x624b90` siege | play {land_cannon1..3} (wait 12), wait 16, goto 1 (= one shot, then silence) | siege |

Owners:
- Land combat ctor `0x46ec50` (new field battle, combat vtable `0x5e8010`): +0x4a = muskets with
  divider 4, +0x4e = swords with divider **2**. Ctor `0x46ee00` (the base of the siege combat):
  both dividers 4. Naval combat (`0x5e8178`): +0x4a = sea table divider 4, +0x4e = sea table
  divider 2. Position = battle province +0x3e4 / +0x3e8.
- Phase start `0x46f5b0` (after `0x46a2b0`, see SPEC_WAR_DIPLOMACY §7.2): **only if the player owns
  a unit on either side** (`0x48f130`), it stops one player and starts the other: phase flag ≠ 0 →
  +0x4a (fire: muskets / sea), else +0x4e (shock: swords / faster sea). The flag-to-phase naming is
  NIEPEWNE (fire = nonzero). Combat end (`0x46eee0`, `0x46f510`, `0x470110` mode change) stops both.
- Siege combat (`0x46f820`, +0x56 = siege table, divider 4): at every siege test (`0x4704a0`), if
  the player has a unit on either side, the player is reset (`0x4c8ab0`) and restarted → **one
  cannon shot per siege roll**. Siege dtor `0x470110..` stops it.
- Manager: static list object `0x89ae50` (`0x4c8bd0` add, `0x4c8c10` remove, mutex-protected),
  ticked by `0x4c8c40` from the game timer callback `0x416530` (a `timeSetEvent` one-shot re-armed
  with period [+0x2a]). The period is the timer ctor's argument, **15 ms** at both call sites
  (`0x584e77`, `0x5b7359`: `push 0xf; call 0x416400`), independent of the game speed; the callback
  calls the manager before and regardless of the pause test (`[0x8bfa50]+0x2306`, which only stops
  the day counter +0x8), so the patterns keep running while paused.
- Each tick: divider countdown −1; at 0 it reloads and the record countdown −1; at 0 the records run.
- Audibility (`0x4c89e0`): distance d = √(dx² + dy²) from the battle to the **centre of the visible
  map** (`0x4767f0` origin + half the view size `[0x7fb8c8]+4/+8`); volume = 0xC0 − min(0xC0, d/8).
  The sound plays only if that volume > 0, i.e. **within 1536 map pixels of the screen centre**
  (coordinate scale vs. zoom NIEPEWNE). The computed pan and volume are **not** passed on: play is
  `+0x20(id)` at full SFX volume, centred (`0x4c8b71`).

## 5. Music

### 5.1 Folder and files
- Music folder `[0x91ac88]` = `"music"` (`0x5b5bea`), overridden by `music.ini` (`0x5b5e52`): the
  file is read as three lines (CR and LF each end a line, so a CRLF file yields "[Common]", "",
  "music=…") and the folder becomes the **third line from its 7th character** (`0x5b5ecd`), i.e.
  the value after `music=`. The GOG `music.ini` says `music=nope`, which by this reading points the
  folder at `nope\` and loads no playlist (no music). **NIEPEWNE** — check against a real GOG install
  before relying on it.
- `settings.cfg`: a line whose 10th character is 'F' (`"MUSIC_OFF"`) sets `[0x91cf74]` = 1
  (`0x5b5ba6`, ghidra line "local_533 == 'F'") → music is never started (`0x4c93e0` returns at once).
- Playlists (ctor `0x4c8f10`, object `0x89ae6c`, 0x7A bytes, created once at INIT_SOUND by
  `0x4c8e70` from `0x5b703a`): five lists of track names, list i at object+0x20+0x12·i:
  0 = `frontend.txt`, 1 = `14.txt`, 2 = `15.txt`, 3 = `16.txt`, 4 = `17.txt` (lists 0–3 read inline, 17.txt through `0x4c95c0`). One name
  per line; lines of ≤ 2 characters are skipped (so the empty "lines" of CRLF files vanish). The
  file played is `<folder>\<name>.mp3` (`0x4c9488`), opened via the CD/alternate root `[0x91c200]`
  first if that is set, else directly.
- GOG data: `14.txt` starts with `early`, `17.txt` with `late` (both real files `early.mp3` /
  `late.mp3`); `frontend.txt` holds every track. `15.txt` names `JUCD75_5_Anonymus`, which does not
  exist (the file is `..._Anonymous.mp3`) — that pick plays nothing.

### 5.2 Which list (`0x4c93e0`)
```
list = 0                                   (frontend)
if (date object [0x647c2c] exists and a game is running (0x419690: [0x8ce939]==0 && [0x647d44]==1))
    list = clamp(year / 100 − 13, 0, 4)    (year = short at [0x647c2c]+0x16)
```
So 1400–1499 → 14.txt, 1500s → 15.txt, 1600s → 16.txt, 1700 and later → 17.txt. There is no
war/peace or event-specific music.

### 5.3 Order and switching
- Track = `rand() % count` entries into the list (`0x4c93e0`), **random with repeats** (the same
  track can come twice in a row). Empty list → the previous name field (object+0x1c, initially 0)
  → nothing plays.
- The first track is chosen in the ctor (`0x4c92c6`) — at start-up, so from `frontend.txt`.
- The main loop calls `0x4c93c0` every iteration (`0x5b74f0`, ghidra "if (DAT_0089ae6c != 0)"):
  a new track is chosen only when the stream reports finished (vfunc +0x24) or its state field
  (+4) is 1. **Music never switches immediately**: entering a game, loading, or a new century
  changes the list only for the next track. NIEPEWNE: whether a failed open retries every loop.
- Not looping: each track plays once (`vfunc+4(path, 1)`, `vfunc+8(0)`).

### 5.4 Volume
`(v − 100) · 20` hundredths of dB with v = settings +0x2e6 (0..100); results below −1900 become
−10000 (silent), i.e. v < 5 mutes (tail of `0x4c93e0`; live from the options window via
`0x4c9590`).

## 6. Our call sites (src/, as they were BEFORE the 2026-11-10 port - kept for reference; see §7)

- `sound.c`: loads `Config/Sfx.txt` as key → file (`sound_play(name)`), 4-channel Paula, effects on
  channels 0/1 while music plays, round-robin steal; `sound_music_list(list)` loads
  `music/<list>.txt` and plays random tracks (LCG, up to 8 retries), next track on finish.
- `main_amiga.c`: `if_click` on almost every UI button and page/list change (≈ 60 sites, e.g.
  391, 516, 933, 1018, 1928, 2056, 2722, 3012, 3052–3191, 3212–3370); `army_select` at 3398
  (band selection), 3429 (click on own army), 3465 (province click that picks an army);
  `army_move` at 3201 (assault accepted), 3453 (unload), 3478 (embark), 3495 / 3508 (army /
  fleet move order); music `sound_music_list("frontend")` at 2872 and the century list at 3602.
- `world.c` `world_sfx()` / `world_sfx_at()` (queue for the human, played by `sfx_service` in main_amiga.c) - called
  from diplo.c, peace.c, military.c and world_note (as of 2026-11-10).

## 7. Differences from our code

Status 2026-11-10: items 1-8 of the list that stood here are implemented as decoded (main_amiga.c
UI sounds, `world_sfx` / `world_sfx_at` from the simulation, `pat_service` for §4 with the 15 ms
tick, music list switch at the next track with the clamp). What remains different:

1. **Mixing**: exe has unlimited DirectSound mixing with one buffer per id (a re-trigger restarts
   that id); we have 2–4 Paula channels (2 while music plays) with round-robin stealing (deliberate,
   CPU budget); an id still playing restarts on its own channel, as in the exe.
2. **Volume sliders, `MUSIC_OFF` and the `music.ini` folder override** are not modelled.
3. **Phase naming** of the battle patterns (fire = muskets) and the assault's dividers (4 / 4, taken
   from the siege-combat base ctor) are NIEPEWNE, as above. Improvement vs manufactory completion:
   which of 0x3B / 0x3C fires for which building is NIEPEWNE (both are `if_done_structure`, so the
   sound is the same). KILL_LEADER's trigger slot is ours (SPEC_WAR), so is the leader_killed moment.
