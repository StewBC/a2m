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
| 6 | **[`control-tools.md`](control-tools.md)** | **Drive a2m over the control port** (A2M/9 ops brief) |
| 7 | **[`turbo-zip.md`](turbo-zip.md)** | Closed: Zip MHz + max block paint |

Open the component note only when the task touches that area:
[`video.md`](video.md) · [`video-paint.md`](video-paint.md) ·
[`machine.md`](machine.md) · [`frontend.md`](frontend.md) · [`runtime.md`](runtime.md) ·
[`disk.md`](disk.md) · [`breakpoints.md`](breakpoints.md) ·
[`snapshots.md`](snapshots.md) ·
[`remote-debug.md`](remote-debug.md) · [`control-tools.md`](control-tools.md) ·
[`turbo-zip.md`](turbo-zip.md).

## Document set

| Doc | Role |
|------|------|
| `status.md` | Live product snapshot |
| `rules.md` | Must-not-break architecture / host rules |
| `snapshots.md` | Closed: machine save/load — c64m reuse + Apple payload |
| `turbo-zip.md` | Closed: Zip MHz ladder + max presentation (block) paint |
| `control-tools.md` | Agent ops: control-port scripting via `Ctl` + coop_watch (A2M/9) |
| `remote-debug.md` | Closed epic record: control/history/frame-ring wire |
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
