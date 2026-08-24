# Runtime

Worker thread owns live `apple2_t` (`src/runtime/runtime_internal.h`).
Frontend and control use **`runtime_client` only**. No live machine pointers
in queues.

| Side channel | Notes |
|--------------|-------|
| Command / event queues | 256 slots each |
| ARGB frame slot | Mutexed latest-wins (`poll_argb_frame`) |
| Frame ring, debug memory, breakpoints, symbols, RPC pool | Own mutexes |

Solicited RPC uses a non-zero `request_token` (echoed on completions). Token
`0` is unsolicited / UI telemetry and must not complete control deferred work.
See `src/runtime/runtime_command.h`.

Some identifiers still have leftover C64-shaped names
(`RUNTIME_MEMORY_MODE_CPU_MAP` / `RAM` / `DRIVE8_MAP`, history KERNAL marker
enums). They alias Apple meanings. Do not restore 1541/KERNAL product
behavior.

Sessions: **`RUNTIME_SESSION_CAPACITY = 4`**. Default UI session id 1. Control
TCP binds one `RUNTIME_SESSION_KIND_CONTROL` session. Mutations publish
`RUNTIME_EVENT_STATE_CHANGED` (no exclusive lock). History FIND cursors are
per session; a step/poke/reset from any asker marks them `CURSOR_STALE`.

## Turbo

Finite ladder entries are **MHz targets** (`N ×` Apple base Φ0, beam paint).
**`max` / `0` / `-1`** free-runs with **instruction quanta** and **machine
full-frame (block) paint** ~60 Hz wall. Encoding: milli-MHz, or
`RUNTIME_TURBO_MAX` (0). Default ladder: **`1,max`**.

| Entry | Pacing | Video / audio |
|-------|--------|----------------|
| Finite `N` | Aim `N ×` ~1.02 MHz (frame-quantum pace) | Beam path, Φ0 step; per-cycle audio |
| `max` | Instruction quanta, ≤2e6 insns / ~1/60 s wall | A-lite H/V/VBL (no pixel paint); one `paint_full_frame` per quantum; **no host PCM** (AY time still advanced); reseed beam on leave |

CLI `--turbo` / INI `turbo_speeds`. Opt+T cycles. Paste and TYPE do **not**
change turbo. FAST → max; SLOW → 1 MHz. Control: `set-turbo` accepts MHz,
`max`, or `-1` — not ladder indices.

Default `history_off_on_max` (true): entering max wipes TimeMachine Record.
See [`timemachine.md`](timemachine.md). Opt-out: `--no-history-off-on-max`.

## Client surface

run / pause / warm+cold reset / quit · step family · run_cycles/instructions ·
registers · memory via VIEW_FLAGS · breakpoints · turbo · gameport · keyboard ·
paste · media insert/eject/swap/boot · poll events/frames/debug memory ·
**`save_state` / `load_state`** · machine-file load/save · assembler ·
Inspector enter/leave/land/frame-step.

Machine-file parsing and all live-memory mutation run on the worker. Binary
Auto uses AppleSingle magic first, then NAPS filename metadata, then a
validated legacy four-byte address/length header, with raw fallback at the
dialog address. Applesoft import sorts lines, rejects duplicates, tokenizes
contextually, writes main RAM at `$0801`, and repairs BASIC pointers. Export
validates the linked program through VARTAB before detokenizing.

Live media commands use `(slot, device)`. The frontend never reads `apple2_t`.

## Assembler

`runtime_assembler.c` wraps **am65**. Default CPU profile: 6502 for ][+, 65C02
for //e Enhanced, then source directives. Predefines `AM65=0` and `APPLE2=1`.

Named targets: `dest=` writes `map` / `main` / `aux` / `lc1` / `lc2`
(combinations allowed); `file=` writes a host file beside the source; both
together do both. A `file=`-only scope does not poke memory. Standalone `am65`
uses `file=` and ignores `dest=` (and predefines `AM65=1`, no `APPLE2`).

**MLI launch:** mutually exclusive with Reset. Auto-run only if CPU-visible
`$BF00 == $4C`; otherwise a notice and skip. Sets PC = run address, SP =
`$01FF`, resumes.

## Inspector / history

Time travel engine is runtime-owned (`runtime_inspector.*`,
`runtime_history.*`, `runtime_frame_ring.*`). Product behavior:
[`timemachine.md`](timemachine.md). Breakpoints: [`breakpoints.md`](breakpoints.md).

Budgets (defaults): history 256 MiB, frame ring 128 MiB, inspector checkpoints
128 MiB. Master switch `[debug] inspector` / `--inspector` defaults **off**.
Off → on arms HST1 + frame ring + checkpoint recorder.

## Tests

Stepping, turbo, savestate, machine files, frame ring, history, sessions,
state-changed, inspector (enable / replay / mode / bp), assembler + MLI,
slot resolve, memory RPC, display stop.
