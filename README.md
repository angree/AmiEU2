# AmiEU2 — Europa Universalis II for the Amiga

A native port of Paradox Interactive's **Europa Universalis II** (2001) for classic 68k Amigas
(68040/68060 recommended, AGA or RTG), AROS 68k and MorphOS — one 68k binary for all of them.

The engine is our own code, written from scratch. **No game data is included**: you need your own copy
of the game (GOG release, v1.09) and convert it on a PC with the tool below.

## 1. Convert your game on a PC

You need the GOG installer `setup_europa_universalis2_2.0.0.1.exe` or the installed GOG game.

**Windows:** put `eu2conv.exe`, `extract.bat` and the installer in one folder and double-click
`extract.bat` (or drop the installer / the installed game folder onto it). After about 40 seconds
you get a folder `data` (~340 MB, most of it is the music).

**Linux:**

```
./eu2conv-linux setup_europa_universalis2_2.0.0.1.exe data
./eu2conv-linux --dir "/path/to/Europa Universalis II" data
```

Every file is checked against the GOG v1.09 release; anything else is refused. Nothing is downloaded or
sent anywhere.

## 2. Install on the Amiga

Copy the `AmiEU2` drawer (with its `fonts` drawer) to the Amiga and the converted `data` folder into it
(`AmiEU2/data`).
Run **eu2setup** once to pick the screen (AGA 640x480, RTG 640x480 … 1920x1080, 8–32 bit), sound and
options, then start **eu2**. 2 MB of stack is taken care of by the program itself.

Requirements: 68020+ (68040/060 to play comfortably), 64 MB fast RAM, AGA or an RTG card, AmigaOS 3.x,
AROS 68k or MorphOS.

## 3. Version 0.3.0

0.3.0: at 640x480 the texts that do not fit their panel are drawn with a 3x5 pixel face (Tom Thumb,
CC0) instead of running into the frame or into each other: the reputation line, the colonist table, the
budget percentages, the loan title, long leader names. The army / fleet weight and load number moved
into its own box (it stood on the siege value). 800x600 and larger keep the normal faces.

0.2.5 fixes the bugs reported from playing 0.2.2:

- refusing an ally's call to arms takes you out of the alliance as in the original (stability -1, the
  caller gets a casus belli, the "Dishonor!" message); diplomatic messages from AI countries now reach you;
- the peace offer herald names the country that sends it;
- Space always toggles the pause, also with message windows open;
- Shift + right click adds waypoints for fleets as well as armies; the route arrow ends in a point;
- fleets enter only ports they may use (own, vassal, military access, allies), and an order to the
  province the fleet is already in no longer fails;
- the map redraws at once when a province is discovered;
- exploring is as slow as in the original: terra incognita costs extra time for every country and for
  fleets too, Africa and arctic provinces cost double;
- View Relations shows your own country green;
- the land / naval maintenance knob can be dragged; budget sliders lock with a double click on the knob;
- the music is refilled by its own process and no longer stutters;
- the archive now contains the `fonts` drawer (the CC0 / public-domain faces for 640x480); without it the
  game fell back to the original fonts and some windows overlapped.

0.2.2: the army and fleet panel as in the original. Every unit without a historical leader has
the original's built-in leader (a name from the country's list, rank, maneuver / fire / shock / siege), the
rank icon follows the original's rank table, and the morale is shown as a word (Invincible … Panic).

0.2.1 fixes the Windows converter: the 0.2.0 converter zip held the Linux program under the name
`eu2conv.exe`, which Windows refuses to start. The Amiga program is unchanged apart from the version number.

0.2.0 was the first public release: an early, playable-but-incomplete version. Working:

- main menu, scenarios, tutorial, credits, save / load;
- the map with all map modes, three zoom levels, fog of war, terra incognita;
- armies and fleets: moving, battles, sieges, unit animation;
- economy, domestic policy, technology, trade, colonists / merchants / missionaries, diplomacy, events,
  AI countries, peace treaties, victory conditions;
- diplomacy map with shields and relation markers, the stability medallion over capitals;
- the right-click province menu as in the original;
- message windows as in the original (several at once, movable, Goto / Details, per-type settings),
  the message log;
- AGA (c2p) and RTG (8–32 bit) displays, Paula and AHI sound, music.

Known gaps: some windows and tooltips are still missing (e.g. the stability tooltip), and the balance
of the simulation is not verified against the original yet. Bug reports are welcome.

## Building

bebbo amiga-gcc 6.5 (`build/build.sh`, run in WSL/Linux) for the Amiga; `build/build-eu2conv.sh` for the
converter (Linux + Windows). The CyberGraphX developer headers are not redistributable and must be put
into `native/cgx-include/` by you. `build/release-gate.py` refuses any original game file in a release.

## Licence and credits

GPL-3.0 (see `LICENSE`). Port by Grzegorz Korycki.

- Platform layer from our earlier Amiga ports (OpenTTD 68k lineage, MIT).
- Chunky-to-planar routines by Mikael Kalms (public domain).
- LZMA decoder from the LZMA SDK by Igor Pavlov (public domain); minimp3 by lieff (CC0).
- Fonts for 640x480 made from the X11 misc-fixed fonts (public domain, `fonts/LICENSE-misc-fixed.txt`).

Europa Universalis II is a trademark of Paradox Interactive. This project is not affiliated with or
endorsed by Paradox Interactive. Please buy the game — this port only gives its owners one more way to play it.
