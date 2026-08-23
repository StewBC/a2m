# a2m agent handoff

Implementation handoff for agents. **Source is authoritative** — if a doc and
the code disagree, fix the doc (or the code) in the same change. These files
are a live brief, not project history.

## Product

C99 **Apple ][+ / //e Enhanced** emulator:

- **Product shell:** c64m-style debugger UI, layout, Configure, CRT, turbo, intents
- **Machine:** Apple II (`src/machine`) — soft switches, banking, Disk II,
  SmartPort, Mockingboard, 6502/65C02, beam-stepped video

Siblings (not submodules): `../a2m` (Apple domain / paint reference),
`../c64m` (debugger product gold).

## Read order

| # | Doc | Why |
|---|-----|-----|
| 1 | **[`status.md`](status.md)** | What works / tree / keys **now** |
| 2 | **[`rules.md`](rules.md)** | Golden rules (threads, ownership, product) |
| 3 | **[`testing.md`](testing.md)** | Build + ctest gate |
| 4 | **[`snapshots.md`](snapshots.md)** | Closed: machine save/load (c64m port) |
| 5 | **[`remote-debug.md`](remote-debug.md)** | Closed epic: control / frame ring / history wire |
| 6 | **[`control-tools.md`](control-tools.md)** | **Drive a2m over the control port** (A2M/12 ops brief) |
| 7 | **[`turbo-zip.md`](turbo-zip.md)** | Closed: Zip MHz + max block paint |
| 8 | **[`sessions.md`](sessions.md)** | Closed foundation: multi-asker sessions + state-changed |
| 9 | **[`inspector.md`](inspector.md)** | **Retired F7 spine:** lessons only (join key, unified disasm) — no live code |
| 10 | **[`timemachine.md`](timemachine.md)** | Epic roadmap (TM0–TM6 landed history) + **TMA addendum** |
| 11 | **[`TMA0.md`](TMA0.md)** | Inspector contract: film / land / re-execute |
| 12 | **[`TMA1.md`](TMA1.md)** | **Implement** TMA0 (rewire TM4 tab) |
| 13 | **[`TMA2.md`](TMA2.md)** | Delete TM1 tape-nav (required cleanup; FIND stays) |

Open the component note only when the task touches that area:
[`video.md`](video.md) · [`video-paint.md`](video-paint.md) ·
[`machine.md`](machine.md) · [`frontend.md`](frontend.md) · [`runtime.md`](runtime.md) ·
[`disk.md`](disk.md) · [`breakpoints.md`](breakpoints.md) ·
[`snapshots.md`](snapshots.md) ·
[`remote-debug.md`](remote-debug.md) · [`control-tools.md`](control-tools.md) ·
[`turbo-zip.md`](turbo-zip.md) · [`max-free-run.md`](max-free-run.md) ·
[`sessions.md`](sessions.md) ·
[`inspector.md`](inspector.md) · [`timemachine.md`](timemachine.md) ·
[`TMA0.md`](TMA0.md) · [`TMA1.md`](TMA1.md) · [`TMA2.md`](TMA2.md).

## Document set

| Doc | Role |
|------|------|
| `status.md` | Live product snapshot |
| `rules.md` | Must-not-break architecture / host rules |
| `snapshots.md` | Closed: machine save/load — c64m reuse + Apple payload |
| `turbo-zip.md` | Closed: Zip MHz ladder + max presentation (block) paint |
| `max-free-run.md` | Closed: instruction-quanta free-run (S2) + 60 Hz block paint |
| `control-tools.md` | Agent ops: control-port scripting via `Ctl` + coop_watch (A2M/12) |
| `remote-debug.md` | Closed epic record: control/history/frame-ring wire |
| `sessions.md` | Closed foundation: runtime sessions + state-changed |
| `inspector.md` | Retired F7 Inspector: lessons kept, code archived at `archive/f7-inspector` |
| `timemachine.md` | TimeMachine epic (D1–D18; TM0–TM6 history) |
| `TM0.md` … `TM6.md` | Landed phase briefs (V1 = TM0–TM4; V1.1 = TM5–TM6) |
| `TMA0.md` | Addendum contract: Inspector film / land / re-execute |
| `TMA1.md` | Implementer brief for TMA0 |
| `TMA2.md` | Delete TM1 tape-nav; keep HST1 FIND |
| `breakpoints.md` | Debugger BP product path (done through P5 + TRON) |
| `video-paint.md` | Closed epic record: a2m-class paint into the beam |
| `testing.md` | Gate, fixtures, deferred tests |
| `video.md` | Current beam / paint facts |
| `machine.md` | Machine API / banking sketch |
| `frontend.md` | Host keys + frontend files |
| `runtime.md` | Runtime client / turbo / frames |
| `disk.md` | Disk II + SmartPort mount surface (incl. HostFS folder volumes) |

## Manual (users, not agents)

`manual/manual.md`, `RELEASE.md`, root `README.md`.
Before making changes to `manual/manual.md`, read `manual/HELP_MARKDOWN.md`.
