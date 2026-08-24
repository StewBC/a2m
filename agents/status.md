# Status

**Version:** 3.0.0

What the product is **now**. Source of truth is `src/`. User-facing catalog:
[`manual/manual.md`](../manual/manual.md). Rules: [`rules.md`](rules.md).

## Product line

| Layer | Reality |
|-------|---------|
| Shell | Debugger UI: Apple display, CPU, disasm, memory, Misc, Configure, CRT |
| Machine | Apple II `src/machine` (//e Enhanced default, or ][+). Disk II, SmartPort/HostFS, Mockingboard |
| Runtime | Two-thread; worker owns `apple2_t`; UI/control use `runtime_client` only |
| Display | ARGB **560×192** throughout (`display_frame` / runtime slot / frontend) |
| Video | Beam-stepped a2m-class: LORES, DLORES, 40/80 text, HGR colour, DHGR; host Colour vs discrete-bit Mono (White/Green/Amber). Max uses full-frame block paint |
| Memory areas | Map · Main · Aux · LC1 · LC2 · ROM |
| Control port | **A2M/13** on `--control-port` (windowed or headless). Ops: [`control-tools.md`](control-tools.md) |
| Snapshots | **`.a2state`** — drop, `--sna`, Opt+Shift+`.`/`,`, control, Machine tab. [`snapshots.md`](snapshots.md) |
| Time travel | Misc → Inspector: Record / Inspect / Leave. Land on checkpoints; HST1 is FIND only. [`timemachine.md`](timemachine.md) |
| Assembler | Misc → Assembler + standalone `am65`. Optional MLI launch. `src/tools/am65/` |

## Host keys (agent-relevant)

Full user list is in the manual. These are the chords the host loop actually
binds in `src/main.c`:

| Key | Action |
|-----|--------|
| **F9** | Toggle debugger (starts display-only) |
| **F8** / **Opt+F8** | Warm / cold reset |
| **F10** / Shift+F10 | Pause/step · step out (time travel: sealed step / step-out; no-op at live) |
| **F11** | Step over (time travel: sealed step-over; no-op at live) |
| **F12** / Shift+F12 | Run · run to cursor (time travel: re-execute to a breakpoint or **live**; stay in Inspect) |
| **Opt+B** | Toggle execute BP at disasm cursor (one list in live and time travel) |
| **Opt+T** | Cycle turbo ladder (MHz / max) |
| **Shift+Opt+C** | Toggle colour / configured mono phosphor (White, Green, Amber) |
| **Opt+Shift+.** / **,** | Quicksave / quickload `.a2state` |
| **Opt+Shift+A** | Assemble configured source (reset / auto-run / MLI / one-shot) |
| **Opt+H** | Help (pauses while open). **Esc** closes it. |
| **Opt+R** | Toggle Forensics↔debugger (full window, pauses on enter, stays paused on leave). |
| **Opt+M** | Memory area cycle (disasm is Map/ROM/Main; memory pane is six areas) |
| **Opt+F / Opt+G / Opt+Shift+G** | Active Memory view: Find / next / previous |
| **Opt+Shift+M** | Kbd stick layout numpad ↔ WASD |
| **Opt+Shift+0/1/2** | Kbd stick off / stick 1 / stick 2 |
| **Opt+1 / Opt+2** | Gamepad → stick map / swap |
| **Opt+Insert** | Paste clipboard → Apple keyboard (does not change turbo) |
| **Opt+Tab** | Cycle debugger focus (when F9 is up) |
| Letters/digits/… | Apple keyboard when machine input focus |

F8 stands in for CTRL+RESET (macOS often eats Control+F-keys). Opt+F8 adds
Open-Apple for cold start.

Keyboard stick (when on): Option/KP0 and Space are fire keys (optional swap in
Configure). Solid-apple is released on stick-assign chords so Opt+Shift+1 does
not latch BUTN0.

## What works (evidence)

| Area | Evidence |
|------|----------|
| Build / ctest | **64 green** — [`testing.md`](testing.md) |
| Assembler | Misc → Assembler; `file=` HostFS; optional Auto-run; **MLI launch** gates auto-run on CPU-visible `$BF00 == $4C` (mutually exclusive with Reset). Sample: `samples/asm_mli_launch/` |
| CLI / INI | model, mounts `sNdN` (multi-image queue), turbo MHz/`max`, `--video-display`, lifecycle, headless, `[DEBUG] break.*`, `--inspector` |
| Turbo / step / reset | Opt+T (default ladder `1,max`); F10–F12 family; F8 / Opt+F8 |
| Display | Full frame in display-only and F9 debugger |
| Breakpoints | Exec + R/W, mapping, FAST/SLOW, TYPE, SWAP, TRON/TROFF, INI, control RPC. [`breakpoints.md`](breakpoints.md) |
| Disk II / SmartPort | Cards in slots 1–7; Configure live-applies model/cards via power-cycle reset; live insert/eject + slot boot; `[SmartPort] boot_slot=N`; Disk II queue/swap with dirty flush; ProDOS `$C0s4/5`; `$C800` host trap; **HostFS** folder volumes |
| Gameport | Paddles, buttons, kbd stick, SDL pads, motor LEDs |
| Control | `--control-port` A2M/13; `tools/a2m_control_client.py` (`Ctl`); `tools/a2m_coop_watch.py` |

## Tree

```text
src/main.c                 host loop
src/frontend/              product UI
src/runtime/               Apple-backed runtime
src/machine/               Apple II
src/control/               A2M/13 control
src/platform/              SDL / fs / sockets / audio
src/tools/am65/            assembler library + CLI
src/util/                  files, queues, config helpers
manual/manual.md           user manual (compiled into help)
```
