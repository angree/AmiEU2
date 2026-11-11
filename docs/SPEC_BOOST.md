# One-time boost (message 0x83 -> 0x84), eu2.exe v1.09

Clean-room notes: prose and addresses only.

## Route

1. **Posters** `0x43fc20` (type 2), `0x43fd20` (0), `0x43fe20` (1), `0x43ff20` (3), `0x440020` (4).
   They are called with ecx = the country, and payload +0x14 = country+0x6 (the country index).
   The message object has vtable `0x5e7300`. Its payload is 0x18 bytes: +0 id 0x83, +6 destination
   (WORD 2, DWORD 6; static `0x645968`, set by `0x413700`), +0xc float amount 200.0, +0x10 type,
   +0x14 country. The message is posted with `0x4b87a0`.
2. **Generic delivery** `0x4b88b0` (message vfunc +0x1c). The destination kind is 2, so the lookup
   `0x4141b0` returns the session object `[0x8a7828]+8` (vtable `0x5e4e84`, slot +0xc = `0x41ad00`).
   That object routes by destination id through `0x4db2b0` (switch `0x4db3a8`): id 6 goes to
   `[0x8ff5e2]` and `0x4d6250`.
3. **Host-side validator** `0x4d6250`. It runs only when `0x4196e0` is true (single player, or this
   machine is the host). It uses switch base 0x61, byte table `0x4d6de0` and jump table `0x4d6da0`.
   Case 0x83 is at `0x4d66f5`:
   - It **re-runs the enable test** for the requested type (table `0x4d6edc`): 0 → `0x45cc00`,
     1 → `0x45cc30`, 2 → `0x45cc60`, 3 → `0x45cca0`, 4 → `0x45ccd0`. If the test fails, the
     request is dropped silently.
   - If it passes, it builds message **0x84** (ctor `0x4d70f0`) addressed to (3, country). The
     payload is copied unchanged: +0xc amount, +0x10 type, +0x14 country. It is posted with
     `0x4b8770`.
   - So 0x83 is the request and 0x84 is the confirmed command, as in a multiplayer
     client/host model.
4. **Country** dispatcher `0x461e90`. Message 0x84 goes to `0x462546` and then to country vfunc
   +0x248 = **`0x450420`** (vtables `0x5e5ac0` and `0x5f55ec` both hold it). The handler switches
   on the type through table `0x45056c`.

## Effect (`0x450420`), A = payload float (+0xc) = 200.0

| type | button | expense (AddExpense `0x438ab0`) | progress added | adder → track | lock |
|---|---|---|---|---|---|
| 0 | trade efficiency | cat 11 (trade & infra), A | 0.5·A | `0x433a60` → tech +0x54 (trade) | `0x4333f0` |
| 1 | production efficiency | cat 11, A | 0.5·A | `0x4339c0` → +0x3c (infra) | `0x4333e0` |
| 2 | stability | cat 10 (stability), A | 0.5·A | `0x433b00` → +0x6c (stability) | +0x6c |
| 3 | land tech | cat 9 (technology), A | 0.5·A | `0x433860` → +0x0c (land) | `0x4333c0` |
| 4 | naval tech | cat 9, A | 0.5·A | `0x433900` → +0x24 (naval) | `0x4333d0` |

The 0/1 mapping is **confirmed** by the AI log strings at `0x554b40` and `0x554b90`:
"%s boosts trade efficiency" → `0x43fd20` (type 0), and "%s boosts production efficiency" →
`0x43fe20` (type 1).

- **Cost**: exactly 200.0. It is a constant: not adjusted for inflation, difficulty or tech group,
  and not clamped. Categories 9, 10 and 11 are not 0 or 1, so AddExpense takes no share from the
  budget pool. The handler books the amount in the ledger and subtracts it from the treasury
  (vfunc +0x3b0 with −A).
- **Gain**: exactly **+100** (A × 0.5, constant `0x5e4380`) of raw progress. It goes through the
  normal add-progress, track vfunc +0x28 = `0x432590`, so there is no extra multiplier. The cost
  factors (inflation, tech group, size and so on) are only in the level cost (§6.4). The rules
  are the same as the monthly budget:
  - Nothing happens if the track cannot rise. Research: level < max−1 (`0x432250`). Stability:
    level < 3 (`0x431570`). In that case the 200 is still spent.
  - Progress is floored at 0.
  - At most one level is gained, and the carry is kept.
  - Research level-up side effects are the same as the monthly step. If the country is the
    player's (`0x434d90`), a notification goes to `0x578540` with ids 0x18 land, 0x19 naval,
    0x1a infra and 0x1b trade. All armies (+0x124c) and fleets (+0x1260) are refreshed through
    vfunc +0x5c.
  - Stability: if the level rose and the country is the player's, notification `0x5783d0(0x10,
    country, 0xd)`.
- **Lock (the "one-time" part)**: afterwards `0x429410(track)` sets
  `track+4 = today + 1 year`. The date lives at `[0x647c2c]+0x14` and is stored as day byte, month
  byte and year word, added with 30-day months (`0x4173c0`). Track vfunc +8 = `0x4293f0` returns
  `today >= track+4` (compare `0x417270`). So each of the five tracks can be boosted **once per
  365 game days (one year)**.

## Enable test (same functions in the UI `0x51c688..`, the host check and the AI)

The test passes when **treasury (country+0xe70) >= 200.0** and the track lock has expired
(vfunc +8). The test does **not** check whether the track can still rise. NIEPEWNE whether the UI
hides the button at max level or at stability +3 by some other means. If it does not, a boost
there wastes 200.

## AI

Yes, the AI uses it. The AI plan actions `0x554a50` (stability), `0x554aa0` (naval), `0x554af0`
(land), `0x554b40` (trade) and `0x554b90` (production) call the same enable test and then the
same posters. Their plan vtables are `0x5f33fc`, `0x5f302c`, `0x5f3214`, `0x5f2e44` and
`0x5f2c5c`. Plan cost 200.0 is set at plan+0x74 by `0x5549f0` and `0x554a20`. The choice logic is
in SPEC_MILITARY_AI.md, around line 198. No other writer of WORD 0x83 exists (5 hits, all
posters). 0x84 is built only by `0x4d677e`.

NIEZNANE: the initial value of track+4 at game start or load. It is presumably a past date, so a
boost is available immediately.
