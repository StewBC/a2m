# Frontend / host

## Rules

- UI never holds a live `apple2_t *`.  
- Host loop: **c64m skeleton** in `main.c` — diff `main_c64m.c` before inventing paths.  
- Keys: see [`status.md`](status.md).

## Important files

| Path | Role |
|------|------|
| `src/main.c` | SDL loop, chords, gameport host, intents (incl. BP set/clear/enable) |
| `src/frontend/frontend.c` | Debugger UI, BP dialog/list, disasm toggle, config, LEDs, CRT |
| `src/frontend/debugger_layout.*` | Splitters |
| `src/frontend/help_view.*` | Help overlay: sections, search, hit highlighting |
| `src/frontend/frontend_joystick_input.*` | Kbd stick → Apple axes/buttons |
| `src/frontend/crt_renderer.*` | CRT presentation |
| `src/frontend/disk_led_data.*` | LED PNGs |
| `src/app_options.*` | CLI / INI (`--break` / `-b`) |

## Breakpoints (product)

- Plan: [`breakpoints.md`](breakpoints.md). **0–3 + P4a–P4e done** (FAST/SLOW, TYPE, SWAP, INI); P4b TRON deferred; P5 control open.  
- Mapping radios **Map/Aux/LC1/LC2/ROM**.  
- Intents: `FRONTEND_DEBUGGER_INTENT_BREAKPOINT_*` → `runtime_client_*` in `main.c`.  
- Read/Write access uses live bus observer (not debugger poke).

## Disk LEDs

Green = Disk II **motor-on** (level, `disk_motor_mask` on machine state + frames).  
Red = write activity hold (when wired).

## Keyboard stick

| Default | Swap (`keyboard_joystick_swap_buttons`) |
|---------|----------------------------------------|
| Option/KP0 → BUTN0, Space → BUTN1 | Space → BUTN0, Option → BUTN1 |

While stick is **on**, Option is a fire key (not latched Open-Apple). Host
Opt+Shift+0/1/2 clears solid-apple so chords do not stick BUTN0.

## Configure

Misc tabs: Machine, Debugger, Breakpoints, Hardware, Assembler, **Inspector**
(TimeMachine forensic entry; F7 unbound). Configure dialog is not F2. The Machine tab starts with model plus Slot
1–7 card selectors (Empty / Disk II / SmartPort / Mockingboard); selecting a
Mockingboard clears the previous Mockingboard because only one is supported.
Keyboard stick and the remaining machine options follow unchanged. Slot cards
persist in `[Slots]`; media selected from Misc → Machine persists in `[DiskII]` /
`[SmartPort]`. Configure → Paths contains only the Assembler, Floppy, SmartPort,
Binary, Basic, and Snapshot browser starting folders. OK (or Save INI now)
compares model/cards to the live machine: no change means no
reset; a model/card change is applied on the worker and followed by a cold,
power-cycle-style machine reset. The resulting machine snapshot refreshes Misc.
Configure → Emulator places **Original DEL behaviour** immediately below Scroll
Wheel Speed. It persists as `[config] original_del` and live-switches Backspace
between cursor-left `$08` (off) and Apple DEL `$7F` (on); Delete is always `$7F`.

## Machine media view

Misc → Machine begins with a runtime-backed dynamic slot list. It lists only
installed slot cards directly, without a collapsible section; Disk II and SmartPort
expose device 0/1 boot, eject, and insert controls. Disk II shows Swap only when
that drive has multiple queued images. A unified **Machine files** Load/Save pair
follows: Load Auto routes `.a2state` snapshots by extension and otherwise detects
AppleSingle, NAPS `#TTAAAA` (ProDOS BIN `$06` or SYS `$FF` with load address in
aux), legacy DOS/old-cc65 headers, or raw bytes; **Run after load** sets PC to
the decoded load address. Explicit Applesoft text import tokenizes at `$0801`
and repairs BASIC bookkeeping. Save offers snapshots, binary ranges (NAPS
`#06AAAA` default, Raw, or AppleSingle), and Applesoft ASCII listings.
Configure/reset remain below it. The superseded fixed Disk II, SmartPort,
separate binary, and separate state sections have been removed.

## Hardware view

Misc → Hardware is flat (no Soft Switches or Counters accordions). Display rows
show each switch's on-address and continuously tracked Actual state. Enabling
Override freezes a separate editable checkbox column for 80COL, ALTCHAR, TEXT,
MIXED, PAGE2, HIRES, and DHIRES. The override affects video paint only; real
soft switches, CPU-visible status, memory mapping, floating bus, and snapshots
continue using actual machine state. Banking remains read-only. Cycle, frame,
and turbo status live only on the Debugger tab.

## Memory search

The active Memory sub-view supports **Opt+F** Find, **Opt+G** Find Next, and
**Opt+Shift+G** Find Previous while stopped. String search optionally ignores
case; hex accepts byte pairs with optional spaces. Search uses that split view's
Map/Main/Aux/LC1/LC2/ROM snapshot, honors invalid bytes in partial planes, wraps
through 64K, and moves only the active view's cursor/scroll position.

## Help overlay

`manual/manual.md` is compiled into help content by `tools/gen_help.py`; keep
that file ASCII-safe. Help search highlights its hits: the span the search
jumped to is drawn inverse (black on yellow) and every other occurrence in the
visible section gets a yellow underline. Matching happens per *drawn line*
inside `help_draw_inline_at`, on the marker-stripped, lowercased text and with
the same `re_t` the search uses, which is what makes it work through word wrap;
a hit split across a wrap boundary is not highlighted. The band must be a real
`nk_fill_rect` because `nk_convert` drops `nk_draw_text`'s background colour.
`help_estimate_span_y` only estimates the scroll target (one row per span, so
it drifts on wrapped text); the render pass measures the row that actually
holds the hit - rows scrolled out of view are still laid out, so this works
even when the estimate left the hit off-screen - and corrects the group scroll
on the next frame, landing every hit a third of the way down the content area.
`help_view_search()` runs the same search as the nav bar's arrows and is what
`test_help_view` drives. This overlay is shared with the c64m project; keep the
two copies in step (they differ only in the `HELP_PALETTE_*` macro names).

## Assembler view

Misc → Assembler loads and saves its source filename as `[assembler] file=...`.
Browse opens beside the filename currently shown; when the filename is empty it
uses Configure → Paths → Assembler. The C64-only “Run BASIC (paste RUN)” mode is
not exposed on Apple II. Runtime named targets treat `file=` and `dest=` as
orthogonal: `dest=` writes `map` / `main` / `aux` / `lc1` / `lc2` (including
combinations such as `aux,lc2`), `file=` writes a host file beside the source,
and both together do both. A `file=`-only scope does not poke memory.
**Opt+Shift+A** globally queues the same configured Assemble action, including
reset, auto-run, and one-shot rearm options.
