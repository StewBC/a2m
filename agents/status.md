# Status

**As of:** 2026-08-23 · **Version:** 3.0.0 · **Branch:** `master`

**Rules:** [`rules.md`](rules.md)  
**Closed:** paint; SP `$C800`; BP 0–4e + P4b TRON; remote-debug **C0–C5b**; control tools **T1–T5**; `get-softswitches`; **turbo-zip**; **max free-run S2**; **snapshots** ([`snapshots.md`](snapshots.md) — `.a2state` save/load, drop, `--sna`, Opt+Shift+./,); **sessions foundation** ([`sessions.md`](sessions.md) — multi-asker + `state-changed`; A2M/11).  
**Active:** **TMA3 Landed** — max turbo remembers Record, wipes the tape, turns Record off; leave max restores Record into an empty window ([`TMA3.md`](TMA3.md)). Inspector is **time travel** ([`TMA0.md`](TMA0.md)): film / land / re-execute to **live**; one breakpoint list; Opt+Left unbound; HST1 FIND stays. Inspector V1 (TM0–TM4) accepted; TM4 Inspector UX superseded. F7 stays unbound. **Not going to TM6.** Lessons from retired F7: [`inspector.md`](inspector.md).  
**Look-later:** drop stored film? (TMA0 A11); debug ergonomics (mem workshop, live remount, frame gate / HW snaps). TM6 Promote/Branch is parked, not next.

---

## Product line (honest)

| Layer | Reality |
|-------|---------|
| Shell | c64m-style debugger (`debugger_layout`, CPU/disasm/mem/Misc/Configure/CRT) |
| Machine | Apple II `src/machine` (//e Enhanced or ][+, Disk II, SmartPort, Mockingboard) |
| Runtime | Two-thread; worker owns `apple2_t`; UI uses `runtime_client` only |
| Display | ARGB **560×192** throughout (`display_frame` / runtime slot / frontend) |
| Video paint | Beam-stepped **560×192** a2m-class: LORES, DLORES, 40/80 text, HGR colour, DHGR; max uses full-frame block paint |
| Memory areas | Map · Main · Aux · LC1 · LC2 · ROM |
| Control port | **A2M/13 product-wired** (A2M/12 + Inspector names: `mode=live\|inspector`, `leave-inspector`, `read-only-inspector`, `inspector-*` `state-changed` reasons, capability `inspector`). Epic: [`remote-debug.md`](remote-debug.md) · [`sessions.md`](sessions.md). |
| Snapshots | **`.a2state`** path save/load — drop, `--sna`, Opt+Shift+`.`/`,`, control. Epic: [`snapshots.md`](snapshots.md) |
| Machine files | Misc → Machine unified Load/Save: snapshots, raw/NAPS/AppleSingle/legacy DOS binaries, Applesoft ASCII import/export |

## Host keys

| Key | Action |
|-----|--------|
| **F9** | Toggle debugger (starts display-only) |
| **F8** / **Opt+F8** | Warm / cold reset |
| **F10** / Shift+F10 | Pause/step · step out (time travel: sealed step / step-out; no-op at live) |
| **F11** | Step over (time travel: sealed step-over; no-op at live) |
| **F12** / Shift+F12 | Run · run to cursor (time travel: re-execute to a breakpoint or **live**; stay in Inspect) |
| **Opt+B** | Toggle execute BP at disasm cursor (same list in live and time travel) |
| **Opt+T** | Cycle turbo ladder (MHz / max — [`turbo-zip.md`](turbo-zip.md)) |
| **Opt+Shift+.** / **,** | Quicksave / quickload `.a2state` |
| **Opt+Shift+A** | Assemble configured source; honor reset / auto-run / MLI launch / one-shot options |
| **Opt+H** / F1 | Help |
| **Opt+M** | Memory area cycle |
| **Opt+F / Opt+G / Opt+Shift+G** | Active Memory view: Find / next / previous (string or hex) |
| **Opt+Shift+M** | Kbd stick layout numpad ↔ WASD |
| **Opt+Shift+0/1/2** | Kbd stick off / stick 1 / stick 2 |
| **Opt+1 / Opt+2** | Gamepad → stick map / swap |
| **Opt+Insert** | Paste clipboard → Apple keyboard (no turbo change) |
| Letters/digits/… | Apple keyboard when machine input focus |

F8 stands in for CTRL+RESET (macOS often eats Control+F-keys). Opt+F8 adds Open-Apple for cold start.

Keyboard stick (when on): Option/KP0 and Space are fire keys (optional swap in Configure). Solid-apple is released on stick assign chords so Opt+Shift+1 does not latch BUTN0.

## What works (evidence)

| Area | Evidence |
|------|----------|
| Build / ctest | **61 green** (`testing.md`; includes sessions + state-changed, file-codec, assembler MLI launch, TM0/TM2–TM3 + TMA1/TMA2, PC-lock disasm wrap) |
| Assembler | Misc → Assembler: assemble to RAM / `file=` HostFS; optional Auto-run; **MLI launch** gates auto-run on CPU-visible `$BF00 == $4C` (mutually exclusive with Reset); sample shim in `samples/asm_mli_launch/` |
| CLI / INI | model, mounts sNdN (multi-image queue), turbo MHz/`max`, lifecycle, headless; `[DEBUG] break.*` |
| Turbo / step / reset | Opt+T (MHz/max); F10–F12 family; F8 / Opt+F8 |
| Display path | Full frame in display-only and F9 debugger |
| Memory RPC | `runtime_memory_rpc` (ctest) |
| Breakpoints | Epic: [`breakpoints.md`](breakpoints.md). **0–3 + P4a–P4e + P5 wire done**. **P4b TRON deferred** (C5b) |
| Machine domain | softswitch, rom_boot, video_beam, diskii, peripherals, cxxx_map, memview |
| a2audit CXXX | Softswitch section pass; INTCXROM hides MB `$Cn`; C800 latch survives SETC3ROM |
| Disk II / SmartPort | Cards in slots 1–7; Configure persists and live-applies model/per-slot Empty/Disk II/SmartPort/Mockingboard selection via power-cycle reset; live insert/eject + slot boot; INI `[SmartPort] boot_slot=N` startup; Disk II multi-image queue/swap with dirty flush; ProDOS `$C0s4/5`; pure SP `$C800` trap; **HostFS** directory mount (NAPS, nested host dirs as ProDOS folders, access-triggered refresh, file + directory write-through; SmartPort Insert **Use This Folder** or CLI/INI) |
| Gameport | Paddles, buttons, kbd stick, SDL pads, motor LEDs |

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Tree (product vs parked)

```text
src/main.c                 host loop
src/frontend/              product UI
src/runtime/               Apple-backed runtime
src/machine/               Apple II
src/control/         product A2M/2 control
src/tools/am65/            shared 6502/65C02/Rockwell/WDC assembler + CLI
```
C64 leftovers and parked duplicate trees were removed. Sibling `../c64m` is the C64 product.

## Restore pre-wholesale tag (emergency only)

```bash
git checkout departure/pre-c64m-wholesale
```
