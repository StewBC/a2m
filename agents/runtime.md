# Runtime

## Ownership

- Worker thread owns live `apple2_t`.  
- Frontend / control use **`runtime_client` only**.  
- Frames: mutexed latest-wins ARGB slot (`poll_argb_frame`).  
- No live machine pointers in queues.

## Turbo (Zip MHz + max)

Finite ladder entries are **MHz targets** (`N ×` Apple base Φ0 rate, beam paint).
**`max` / `-1`** free-runs with **machine full-frame (block) paint** ~60 Hz wall
(A-lite beam counters only; no beam pixel paint). Default ladder: **`1,max`**.

| Entry | Pacing | Video / free-run |
|-------|--------|------------------|
| Finite `N` | Aim `N ×` ~1.02 MHz (frame-quantum pace) | Beam path, Φ0 step |
| `max` | Free-run **instruction quanta** (S2) | No beam; block paint ~60 Hz wall; reseed beam on leave |

CLI `--turbo` / INI `turbo_speeds`. Opt+T cycles. Paste does **not** change turbo.
FAST → max; SLOW → 1 MHz. Control: `set-turbo` accepts MHz, `max`, `-1`.

Epics: [`turbo-zip.md`](turbo-zip.md) (ladder/paint) · [`max-free-run.md`](max-free-run.md) (S2 speed path).

## Client surface (product)

run/pause/reset/quit · step family · run_cycles/instructions · registers ·
memory via VIEW_FLAGS · breakpoints · turbo · gameport · keyboard · paste ·
mount helpers · poll events/frames/debug memory/breakpoints ·
**`save_state` / `load_state`** (`.a2state` — [`snapshots.md`](snapshots.md)) ·
machine-file load/save (raw, NAPS, AppleSingle, legacy DOS, Applesoft text).

Machine-file parsing and all live-memory mutation run on the worker. Binary Auto
uses AppleSingle magic first, then NAPS filename metadata, then a validated legacy
four-byte address/length header, with raw fallback at the dialog address. Applesoft
import sorts lines, rejects duplicates, tokenizes contextually, writes main RAM at
`$0801`, and resets TXTTAB/VARTAB/ARYTAB/STREND/FRETOP/PRGEND and execution/data
pointers. Export validates the linked program through VARTAB before detokenizing.

Machine snapshots include copied slot/card and per-device media/queue state.
Live media commands use `(slot, device)` for Disk II insert/eject/swap,
SmartPort insert/eject, and slot boot; the frontend never reads `apple2_t`.

## Breakpoints

Full epic: [`breakpoints.md`](breakpoints.md).

| Piece | Status |
|-------|--------|
| CREATE/UPDATE/enable/rearm + full snapshot | **Works** (Phase 0) |
| Free-run **execute** match (range, counter, condition) | **Works** (Phase 1) |
| Mapping Map/Aux/LC1/LC2/ROM | **Works** (Phase 2) |
| READ/WRITE access | **Works** (Phase 3) — `apple2` bus callback + hit-pending |
| FAST/SLOW, TYPE, SWAP | **Works** (P4a/c/d); TRON deferred (P4b) |
| INI `[DEBUG] break.*` | **Works** (P4e) — load at start; save on quit |
| Client API | Full command surface |
| Caveat | Host traps (e.g. SP `$C800`) are not opcode fetches |
| Control BP RPC | **Done** (A2M/3, remote-debug C1) |

Files: `runtime_thread.c`, `apple2` bus callback, `runtime_breakpoint_condition.*`, `runtime_breakpoint_ini.*`.

### Memory areas

Map · Main · Aux · LC1 · LC2 · ROM (`apple2_read_in_view` / `write_in_view`).

## Control port / remote debug

Full epic: [`remote-debug.md`](remote-debug.md).

| Item | Status |
|------|--------|
| Product wire | **Done** — A2M/12; `--control-port` windowed + headless |
| A2M/12 | A2M/11 + forensic `mode` / `exit-forensic` / `read-only-forensic` + forensic `state-changed` reasons · ops: [`control-tools.md`](control-tools.md) · [`TM3.md`](TM3.md) |
| Product `src/control` | Parked c64-shaped library (not linked) |
| Frame ring | **Done** — ARGB ring, live push, control wire |
| CPU history | **Done through C4c** — arena, observer, worker RPC, control wire |
| Options | `history_memory_mb`, `frame_ring_memory_mb`, `timemachine`, `timemachine_memory_mb` wired into runtime |
| Sessions | Fixed table N=4; per-session history cursors; `runtime_client_session_open/close`; control TCP binds `kind=control`; `RUNTIME_EVENT_STATE_CHANGED` |

## Deferred tests

`runtime_assembler`, frame ring, history, savestate, and `control_protocol` are
in the product ctest gate. Runtime assembly defaults to 6502 for Apple ][+ and
65C02 for Apple //e Enhanced, then honors CPU directives in the source. It
predefines `AM65=0` and `APPLE2=1`; named targets validate and resolve
`map/main/aux/lc1/lc2` through Apple `VIEW_FLAGS` before writing bytes.
