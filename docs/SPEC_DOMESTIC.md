# Domestic policy sliders: time gate, command, window (eu2.exe v1.09)

Clean-room notes: prose, tables and addresses only, no code copied. Decoded 2026-11-10 from the
objdump disassembly. Work scripts are in `C:\temp\amiga_eu2\re\agent_dom\`, the dumps are
`sr\dom_*.txt`. Marks: **NIEPEWNE** = inferred, **NIEZNANE** = not traced.

## 1. The policy object (country+0x37E)

The object is created in the country ctor (`0x435c4d..0x435c6d`, vtable `0x5e5f64`). It is a
generic "track" object, the same base class the boost tracks use (SPEC_BOOST):

| offset | content | evidence |
|---|---|---|
| +0x04 | date stamp: day byte +4, month byte +5, year word +6. Zero (year 0) after the ctor | `0x435c59..0x435c63` |
| +0x08 + 4·i | slider i, int 0..10, i = 0 aristocracy, 1 centralization, 2 innovative, 3 mercantilism, 4 offensive, 5 land, 6 quality, 7 serfdom | getter `0x42bbf0(i)` |

Loader `0x42bc80` (vfunc +0xC) reads keyword ids into these fields: `date` (0x33) → +4 via
`0x4b1560`, `aristocracy` 0x1da → +8, `centralization` 0x1db → +0xC, `innovative` 0x1dc → +0x10,
`mercantilism` 0x1dd → +0x14, `offensive` 0x1de → +0x18, `land` 0x7b → +0x1C, `quality` 0x1df →
+0x20, `serfdom` 0x1e0 → +0x24. Every slider is clamped to 0..10 on load. Any other key logs
"unknown lhs in policy". The scenarios all write `date = { year = 0 month = january day = 0 }`, so
every country may change a slider on the first day.

**Polarity**, from the window (§4) and the stats (§5): value 0 = the `DOMNAME_*_L` end, value 10 =
the `DOMNAME_*_R` end. The minus button moves towards _L and the plus button towards _R.

| i | 0 (_L) | 10 (_R) |
|---|---|---|
| 0 | Plutocracy | Aristocracy |
| 1 | Decentralization | Centralization |
| 2 | Narrowminded | Innovativeness |
| 3 | Free Trade | Mercantilism |
| 4 | Defensive Doctrine | Offensive Doctrine |
| 5 | Naval | Land |
| 6 | Quantity | Quality |
| 7 | Free Subjects | Serfdom |

## 2. The time gate

- **Gate predicate** = policy vfunc +8 = `0x42a0d0`. It returns true when the cheat flag byte
  `0x8d0d7b` is set. Otherwise it returns `today >= stamp + 9 years`, where today = `[0x647c2c]+0x14`
  (date add `0x4173c0` with offset {day 0, month 0, year 9}, compare `0x417270` = a >= b).
- **Stamp on a change** = `0x429410`, the base-class method: `stamp = today + 1 year`
  (`0x4173c0` with {0,0,1}). It is called from the change handler (§3.3) unless the force flag is set.
- **Effective rule:** after a player change on date D, the buttons are locked until **D + 10 years,
  the same day and month** (inclusive: on D + 10 y the gate is open again). Only years are added,
  so the 30-day month arithmetic does not matter. One change (one step on one slider) uses up the
  whole gate, because the stamp is renewed on every step.
- **Tooltip date**: `DOMW_DELAY` shows `stamp + 9 years` (`0x417210(9)` adds to the year word) =
  the date the gate opens, formatted by `0x5b5b51` as `"%s %d, %d"` = month name, day, year
  (the month name comes from the text-key table `0x631060`).
- **Cheat** `robespierre` (console, `0x56582a`) toggles `0x8d0d7b`: "[CHEAT ON] You can now do any
  reform you like!" / "[CHEAT OFF] …". With it on, the gate is open for every country.
- **The AI has no slider logic of its own.** Only three places build the slider message objects
  (ctor `0x444cb0`): the window (`0x43f840`, id 0x117), the host validator (`0x4d6405`, 0x118) and the
  event command `domestic` (`0x59573b`, 0x118). So AI sliders move only by events, and events
  ignore the gate (§3.4). The AI never runs into the gate. (NIEPEWNE only in the sense that a
  direct write elsewhere was searched for by the pattern `[reg+reg*4+0x8]` and none was found.)

## 3. Command chain

### 3.1 Request 0x117 (from the window)

`0x43f7e0(i, v)` (ecx = country) builds message **0x117** (vtable `0x5e7680`). The payload is
+0xC slider i, +0x10 new value v, +0x14 country index (country+0x6), +0x18 force byte = 0, and the
destination is the static `0x645968` (the host, as in SPEC_BOOST). It is posted with `0x4b87a0`.
Only the domestic window calls it.

### 3.2 Host validator (`0x4d6250`, case 0x117 at `0x4d6313`)

1. The country `0x64e140[+0x14]` must exist.
2. It is accepted if `0x45b640(i, v)` is true or the force byte is set. `0x45b640` = gate open
   (§2) → true; **otherwise** true if stability (`0x43a230`) >= −2 and 0 <= v <= 10. NIEPEWNE:
   so the host would accept a change during the delay when stability >= −2. The UI never sends
   one then (the buttons are disabled), so in single player this branch is never reached.
   Note also that the gate-open branch does not check the range of v.
3. It posts **0x82** to (3, country) (`0x4d6373`), then **0x118** with the same i, v, country and
   force (`0x4d63fe`).

### 3.3 Country handlers (dispatcher `0x461e90`, country vtable `0x5e5ac0` / `0x5f55ec`)

- **0x82 → vfunc +0xE0 = `0x448270`: stability −1.** `0x436490(1)`: stability (tech+0x6C) −= 1,
  clamped to −3..+3 (`0x431900`). If the unclamped value would have been below −3, a host
  immediately runs a revolt roll `0x460450(0)` (SPEC_REVOLT_RELIGION). If the country is the
  player's, one notification `0x5783d0(0xF, tag, 0xD)` is posted per point. Then `0x443dc0`
  (NIEZNANE, a follow-up message, probably a UI refresh).
- **0x118 → vfunc +0x8C = `0x455600`: apply.** slider[i] = clamp(v, 0, 10). If force = 0, the
  stamp is renewed (`0x429410`, §2). Then `0x43ce50(0)` recomputes the yearly manpower rate
  C+0xEC4 (it uses the manpower policy factor `0x42b840`).

There is **no money cost and no other effect** in the chain. The cost of a step is the gate (10
years) plus 1 stability.

### 3.4 Event command `domestic` (code 09, `0x5956fc..0x595740`)

The event builds **0x118 directly** with v = current + n and force = 1. It skips the validator, so
there is no 0x82, no stability loss, no gate check and no stamp. The handler clamps v to 0..10.

## 4. The window (`iw_domestic`, ctor `0x517ac0`, 224x344 at screen (0,103))

Vtable `0x5ec164`: +0x04 events `0x517d20`, +0x0C draw `0x517ee0`, +0x3C setup `0x517f30`,
+0x44 tooltip `0x518090`, +0x48 2nd tooltip `0x5183b0`. The sidebar header is `0x52fba0(line1 =
[country+0x12], line2 = HEAD_DOMESTIC "Domestic Policies")` (`0x51804c`). NIEPEWNE: +0x12 is the
country name.

Per row i = 0..7 (window-relative, y = top):

| element | position | evidence |
|---|---|---|
| left name `DOMNAME_*_L` (`0x42ba70`) | x 16, y 16+36i, left-aligned, garamond14, colour 0xFFFF | `0x517be3` |
| right name `DOMNAME_*_R` (`0x42bb30`) | right edge at window width − 16 = 208, y 16+36i | `0x517c09` |
| minus `btn_minus.bmp` (16x16, 4 frames) | x = 44 − width = **28**, y **34+36i** | `0x517f5b..0x517f79` |
| slider `sprite_slider.bmp` 132x6 + knob `btn_slider.bmp` | x **46**, y **34+36i** | `0x517f45..0x517f58` |
| plus `btn_plus.bmp` | x = 46 + slider width + 2 = **180**, y **34+36i** | `0x517f7c..0x517f9b` |

- The slider is widget `0x427aa0` with (0, 10.0, 5.0, 1.0) (NIEPEWNE: min, max, start, step). Setup
  writes the value into it, clamped to the widget range (`0x517fc1..0x518003`), and **disables its
  knob** (+0x8A → +0x86 = 1). The slider is display-only and cannot be dragged.
- Events (`0x517d20`), every call, for each row: re-enable both buttons, then disable minus if
  value < 1, disable plus if value > 9, and disable **both** if the gate is closed. A click on minus
  sends `0x43f7e0(i, value − 1)`, a click on plus sends `0x43f7e0(i, value + 1)`. After each send
  setup (+0x3C) is called. The window does not change the value itself; it shows the value once the
  command has come back.
- There is no text drawn in the window apart from the 16 names. Every number is in tooltips.

### 4.1 Tooltips (`0x518090`)

Only for y >= window top. The rows are tested in order i = 0..7, and the row band is y < top + 52 + 36i:

1. Both buttons disabled (= gate closed) and the mouse over either one → `DOMW_DELAY` "We can
   modify these next around %s." with the date from §2.
2. Minus enabled, under the mouse, value > 0 → `DOMW_MODIFY` "By moving one step towards %s we'll
   get the following stats.\n" with %s = _L name, followed by the stats block (§5) **for value − 1**.
3. Plus enabled, under the mouse, value < 10 → the same with the _R name and value + 1.
4. Otherwise, inside the row band → the stats block for the current value.

For 2 and 3 the window writes the trial value into the live slider, builds the text from the
*player's* policy (`[0x8bfa50]+0x37E`), then puts the value back (`0x51824b`, `0x5182a6`).
Port: use a copy.

The 2nd tooltip `0x5183b0` uses the same row bands and returns `POLICYDESC_i` (`0x42b540`, the long
description). COWROLLDOM "Click to access Domestic Policy Settings" is on the country-window
shield (SPEC_UI §4.5).

## 5. Stats block (`0x42a130(i)`, buffer `0x64b4ac`)

d = s_i − 5. Each line is the text key with `%s` = "+" if the value > 0, else "" (a negative value
brings its own minus sign, 0 prints as 0.00). These are the **absolute** effects of the setting,
not the change from the current one. Values marked "int" are truncated (`0x5b98fc`) and printed with %d.

| i | lines (key: value) |
|---|---|
| 0 | A_CAV −1.4d · A_WAR 2d · DIP 0.4d · A_TE −d · S_PRO −d |
| 1 | C_REB 1.2d months · C_WAR 0.4d · C_TECH −2d % · C_PRO 2d |
| 2 | I_STAB 5d % · I_WAR 0.4d · I_TECH −3d % · I_MISS −0.4d · I_COL −0.4d |
| 3 | M_COL −0.2d · M_MER −0.4d · M_REF max(0, 2d) (%.0f) · M_COS −10d % |
| 4 | O_ART 2d · O_SHO int(0.25d) · O_SIE int(−0.25d) · O_MOR 0.04d · then `"%s: %s%.1f\%%"` with EYRTSFORTLEVEL "Fortress" and **5·s4** (the raw slider; NIEPEWNE meaning; the format really prints a backslash before %) |
| 5 | L_INF −0.6d · L_CAV −d · L_WAR 2d · L_GAL 0.6d · L_TRA d · L_LAN max(0, 0.1d) · L_NAV max(0, −0.1d) · L_COL −0.4d, halved if < 0 · A_TE −d · L_ADJ 2d % · S_PRO d · Q_MAN 5d % |
| 6 | Q_INF 0.4d · Q_CAV 0.4d · Q_LAN 0.05d · Q_FIR int(0.25d) · Q_MAN −5d % |
| 7 | S_INF −0.4d · S_PRO −2d · S_STA −10d % · S_LAN −0.05d |

(Keys are `DOMESTIC_<x>`. Evidence: the jump table `0x42b514`, case starts `0x42a15b`, `0x42a37a`, `0x42a52d`,
`0x42a762`, `0x42a930`, `0x42ab5c`, `0x42b0f1`, `0x42b30d`.) These match the policy terms in
SPEC_TIME_ECONOMY §3.1, SPEC_UI_WINDOWS4 §1.4 and SPEC_WAR_DIPLOMACY, and they **confirm the slider
order and polarity** that those specs marked NIEPEWNE. DOMESTIC_L_ART is defined in text.csv but not
used.

## 6. Port summary (compared with `src/main_amiga.c` ~357, `src/ui.c` domestic_draw)

1. Gate: `POLICY_DAYS = 10 years` is right. Make it exact: store `next = change date + 10 years`
   (same day and month; our 360-day years are fine as long as the tooltip shows the same date).
   The start state is "open" (scenario date year 0). Replace the hard-coded English note with
   `DOMW_DELAY` and the date formatted as "<Month> <day>, <year>", shown as a tooltip on the
   buttons when the gate is closed (the original draws no note in the window).
2. **Missing:** each step costs **−1 stability** (clamped at −3). If the country was already at −3,
   an immediate revolt roll follows. A notification goes to the player.
3. Disable both buttons while the gate is closed, and the minus/plus button at 0 / 10.
4. The AI never changes sliders itself. Events (`domestic`) move a slider without the gate, the
   stamp or the stability cost. After any change, recompute the manpower rate.
5. Layout: minus at (28, 34+36i), track at (46, 34+36i), plus at (180, 34+36i); names at y 16+36i
   (top of the text, not centred). Our minus x=16 / plus x=192 / +10 offset must move. The knob is
   not draggable.
6. Tooltips: the row hover shows the stats block (§5) for the current value. The button hover shows
   DOMW_MODIFY + stats for the value ±1, computed on a copy of the sliders. The 2nd tooltip is
   POLICYDESC_i. All values are fixed point (×100 is enough for %.2f).
7. Cheat `robespierre` (gate off) is optional and debug-only.
