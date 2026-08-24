# Frontend / host

UI never holds a live `apple2_t *`. Keys: [`status.md`](status.md). User
catalog: `manual/manual.md`.

## Important files

| Path | Role |
|------|------|
| `src/main.c` | SDL loop, chords, gameport host, intent dispatch |
| `src/frontend/frontend.c` | Debugger UI, BP dialog/list, Configure, CRT, Misc tabs |
| `src/frontend/debugger_layout.*` | Splitters |
| `src/frontend/debugger_disasm.*` | Disassembly pane, PC-lock |
| `src/frontend/help_view.*` | Help overlay: sections, search, hit highlighting |
| `src/frontend/forensics_view.*` | Forensics full-window mode (HST1 FIND UI; shell in PR3) |
| `src/frontend/frontend_input.*` | Guest keyboard map |
| `src/frontend/frontend_joystick_input.*` | Kbd stick → Apple axes/buttons |
| `src/frontend/crt_renderer.*` | CRT presentation |
| `src/frontend/memory_search.*` | Find in the active Memory view |
| `src/app_options.*` | CLI / INI |

Intents (`FRONTEND_DEBUGGER_INTENT_*`) are dispatched in `main.c` onto
`runtime_client_*`.

## Layout

Panes: Apple display, registers, disassembly, memory (splitable), misc.
Opt+Tab cycles Apple2 → Disassembly → Misc → Memory (when F9 is up).

Misc tabs: Machine | Debugger | Breakpoints | Hardware | Assembler | Inspector.

Configure dialog (not F2): Machine | Emulator | Paths.

## Memory / disasm areas

Memory pane Opt+M: **Map → Main → Aux → LC1 → LC2 → ROM**.
Disasm Opt+M: **Map → ROM → Main** only. Do not unify those cycles.

Opt+Left sets PC from the disasm cursor **in live mode**. In time travel it is
unbound.

## Configure

Machine tab: model + Slot 1–7 card selectors (Empty / Disk II / SmartPort /
Mockingboard). One Mockingboard. Slot cards persist in `[Slots]`; media from
Misc → Machine persists in `[DiskII]` / `[SmartPort]`.

OK / Save INI now: no model/card change means no reset; a model/card change is
applied on the worker then a cold power-cycle-style reset. The turbo ladder is
live-applied either way (keep current speed if it is still on the list).

Paths: Assembler, Floppy, SmartPort, Binary, Basic, Snapshot browser folders
only.

Emulator: **Original DEL behaviour** (`[config] original_del`) live-switches
Backspace between cursor-left `$08` (off) and Apple DEL `$7F` (on). Delete is
always `$7F`. CRT: Colour / Mono + phosphor White/Green/Amber live-preview;
Cancel restores the decoder from dialog open. INI `[Video] colour` +
`mono_mode`.

## Machine media view

Runtime-backed slot list of installed cards. Disk II and SmartPort expose
device 0/1 boot, eject, insert. Disk II shows Swap only when that drive has
multiple queued images.

Unified **Machine files** Load/Save: Load Auto routes `.a2state` by extension,
else AppleSingle / NAPS `#TTAAAA` (ProDOS BIN `$06` or SYS `$FF`) / legacy
DOS / raw. **Run after load** sets PC to the decoded load address. Applesoft
text import tokenizes at `$0801`. Save: snapshots, binary ranges (NAPS
`#06AAAA` default, Raw, or AppleSingle), Applesoft ASCII.

## Hardware view

Flat. Display rows show each switch's on-address and Actual state. Override
freezes an editable column for 80COL, ALTCHAR, TEXT, MIXED, PAGE2, HIRES,
DHIRES — paint only. Banking is read-only. Cycle / frame / turbo live on the
Debugger tab.

## Inspector tab

Record / Inspect / Leave. See [`timemachine.md`](timemachine.md). Window
**headers** are dark cobalt while inspecting (`nk_rgb(24, 62, 118)`); do not
tint the window background. Breakpoints tab chrome is the live panel in both
modes (one list).

**Forensics…** / **Opt+R** open full-window Forensics (no CRT behind — same
class as the debugger layout, not a Help overlay). Opens from display-only
too. **Pauses** on enter; Esc / Close / Opt+R / F9 leave to the debugger and
**stay paused**. Mutually exclusive with Help. See
[`design/forensics-ui.md`](../design/forensics-ui.md).

Entering **max** remembers Record, wipes the tape, turns Record off (checkbox
locked). Leaving max restores Record into an empty window.

## Keyboard stick

| Default | Swap (`keyboard_joystick_swap_buttons`) |
|---------|----------------------------------------|
| Option/KP0 → BUTN0, Space → BUTN1 | Space → BUTN0, Option → BUTN1 |

While stick is **on**, Option is a fire key (not latched Open-Apple). Host
Opt+Shift+0/1/2 clears solid-apple so chords do not stick BUTN0.

## Disk LEDs

Green = Disk II **motor-on**. Red = write activity hold.

## Memory search

Active Memory sub-view, while stopped: Opt+F / Opt+G / Opt+Shift+G. String
(optional case-fold) or hex byte pairs. Uses that split view's snapshot,
honors invalid bytes in partial planes, wraps 64K.

## Help overlay

`manual/manual.md` is compiled by `tools/gen_help.py`. Keep that file
ASCII-safe (`manual/HELP_MARKDOWN.md`).

Search highlights: the jumped-to span is inverse (black on yellow); other
hits in the visible section get a yellow underline. Matching is per *drawn
line* inside `help_draw_inline_at` on marker-stripped lowercased text with
the same `re_t` the search uses. A hit split across a wrap is not
highlighted. The band must be a real `nk_fill_rect` (`nk_convert` drops
`nk_draw_text` background colour). `help_estimate_span_y` only estimates;
the render pass measures the row that holds the hit and corrects group
scroll on the next frame. `help_view_search()` is what `test_help_view`
drives.

Opt+H opens (and pauses if running). Esc closes.

## Assembler view

Source filename is `[assembler] file=...`. Browse opens beside the shown
filename; empty uses Configure → Paths → Assembler. Opt+Shift+A queues the
same Assemble action. See [`runtime.md`](runtime.md) for `dest=` / `file=` /
MLI.
