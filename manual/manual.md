# a2m - An Apple II emulator written by Stefan Wessels with AI assistance, 2024-2026

a2m is an Apple ][+ and Apple //e Enhanced emulator. It runs on Windows, Linux, and
macOS. It boots Disk II floppy images and SmartPort block devices (including a host
folder as a ProDOS volume), can save and restore full machine snapshots
(`.a2state`), and includes a debugger and assembler for Apple II development.

System ROMs are embedded. No separate ROM files are required.

## Overview

### Running a2m

Launch a2m from the command line or as a GUI application. Use `--help` or `-h` for the
full command-line reference.

Useful flags:

| Flag | Effect |
|------|--------|
| `--inifile <file>` / `-i` | Load a specific INI file at startup |
| `--noini` / `-n` | Skip INI file loading entirely |
| `--nosaveini` | Disable INI save on quit, regardless of other flags |
| `--saveini` / `-v` | Save INI on quit (one-time override) |
| `--remember` / `-r` | Force save-on-quit into the INI file |
| `--defaults` / `-f` | Start from built-in defaults |
| `--model enh\|plus` / `-m` | `enh` is Apple //e Enhanced (default); `plus` is Apple ][+ |
| `--disk <spec>` / `-d` | Mount a Disk II image; `path` or `s6d0=path` (repeatable) |
| `--hd <spec>` / `--smart` | Mount SmartPort media; image file or host folder; `path` or `s7d0=path` (repeatable) |
| `--mb-slot N` | Mockingboard slot `1..7`; `0` disables (default slot 4) |
| `--turbo <list>` / `-t` | Turbo ladder, e.g. `1,max` or `1,4,8,max` |
| `--sna <file>` | Load a machine snapshot (`.a2state`) at startup |
| `--kbdjoy <0\|1\|2>` | Keyboard joystick on gameport stick `1` or `2` (`0` disables) |
| `--kbdjoy-layout <numpad\|wasd>` | Keyboard joystick layout |
| `--break <addr>` / `-b` | Install an execute breakpoint at a hex address |
| `--symbols <file>` | Load a simple symbol file (`NAME` hex per line) |
| `--headless` | No window; short smoke exit unless `--control-port` is set |
| `--control-port N` | Listen on localhost TCP for A2M/6 remote control (`0`=off) |
| `--audio-smoke` | Emit a 440 Hz test tone to verify audio output |

By default, a2m loads `a2m.ini` from the current directory. The INI file stores
configuration, window size, debugger layout, media mounts, and breakpoints.

The Apple 2 attempts to boot from Slot 6 Drive 0 (Disk II) or Slot 7 Device 0
(SmartPort). Those defaults can be changed in Configure or in the INI file.

### Disk Images

Disk II supports `.nib` and `.dsk` / `.do` / `.po` for reading and writing, and `.woz`
for reading. A controller can occupy any slot 1-7; the usual slot is 6. Each
controller has two drives (`d0` and `d1`).

Repeat `--disk` on the same drive to build a multi-image queue:

```sh
./a2m -d s6d0=sideA.nib -d s6d0=sideB.nib -d s6d1=util.dsk
```

A bare path mounts Slot 6 Drive 0. The first image in a queue is mounted at startup.
Swap from the Machine tab or from a breakpoint Swap action.

### Hard Disk Images

SmartPort is a block device, usually a ProDOS volume. Typical images are `.po`,
`.hdv`, or `.2mg`. A controller can occupy any slot 1-7; the usual slot is 7, and
the Apple //e tries to boot device 0 in that slot.

```sh
./a2m --hd s7d0=prodos.po --hd s5d0=other.po
```

An INI-only `[SmartPort] boot_slot = N` setting forces startup through SmartPort unit 0
in slot `N`. This is useful for booting an Apple ][+ or a SmartPort installed outside
slot 7. The setting is ignored unless that slot contains a SmartPort card with unit 0
successfully mounted.

### HostFS (folder as a ProDOS volume)

If the SmartPort mount path is a **directory**, a2m treats it as **HostFS**: a
ProDOS volume built from files in that folder. No `.po` / `.hdv` image is required
for that unit. Image-backed and HostFS units can share the same card (for example
`s7d0=./host` and `s7d1=disk.po`).

Prepare the folder yourself. Only **NAPS**-tagged names are mounted:

```text
NAME#ttxxxx
```

`tt` is the ProDOS file type (two hex digits) and `xxxx` is the aux type (four hex
digits). The ProDOS name is the stem, uppercased and limited to 15 legal characters
(`A–Z`, `0–9`, `.`). Non-NAPS names and subdirectories are skipped. If a tool (for
example the built-in assembler) already supplies a NAPS name, HostFS observes the
stem and does not append a second `#ttxxxx`.

A bootable volume needs at least a ProDOS system file:

```text
PRODOS#FF0000
```

Optional: `BASIC.SYSTEM#FF2000`.

```sh
./a2m --noini --smart s7d0=./hostfs/d0
```

The emulated volume is advertised as about 32 MB (`65535` blocks). Its ProDOS volume
name is `HOSTFS.SNdM` (slot and unit), so `/PREFIX` stays unique when more than one
HostFS unit is mounted.

HostFS is read/write: ProDOS data writes update the host files, and create / delete /
rename in the catalog create, remove, or rename NAPS files in the folder. External
edits to files already on the volume are picked up by a periodic rescan (remount if
the directory was full when new files appeared).

ProDOS catalog **order** (which `.SYSTEM` file comes first, and so on) is remembered in
an optional `hostfs.order` text file in the folder — one NAPS basename per line, `#`
comments allowed. If the file is present at mount, those names are added in that
order and any other NAPS files are appended. Reordering the catalog in ProDOS (for
example with CAT.DOCTOR) rewrites `hostfs.order` automatically so the next launch
keeps that order. The file is not itself a ProDOS volume entry.

HostFS is selected by path kind only (directory vs file). Mount it from the command
line or `[SmartPort]` in the INI. The Machine **[Insert]** browser still selects
image files; folder insert from the UI is not provided yet.

### Machine Snapshots

`--sna <file>` loads a machine snapshot (`.a2state`) at startup. Disks from `--disk`
and `--hd` are mounted first; then the snapshot is restored after the runtime is
running.

You can also restore or write snapshots while the emulator is running:

- UI: Misc -> Machine **[Load...]** and **[Save...]** (see **Machine**)
- Drag and drop a `.a2state` file onto the window
- Quickload / quicksave: **Shift+Opt+<** / **Shift+Opt+>**
- Control port: `load-state <path>` and `save-state <path>` (see **Remote**)

Example:

```sh
./a2m --sna demos/midload.a2state
./a2m --headless --control-port 6510 --sna demos/midload.a2state
```

### Drag and Drop

Files can be dragged onto the a2m window while the emulator is running.
The file extension determines how the file is handled:

| Extension | Action |
|-----------|--------|
| `.nib` `.dsk` `.do` `.po` `.woz` | Add the image to the Disk II queue on slot 6 drive 0 |
| `.a2state` | Load a saved machine state snapshot |
| `.hdv` `.2mg` | Remember the path as SmartPort `s7d0` for the next launch |
| anything else | Ignored |

Extension matching is case-insensitive.

### Audio

The Apple speaker is a 1-bit click path mixed to both host channels. A Mockingboard
(two AY-3-8910 chips) in any slot produces stereo: left chip on the left, right chip
on the right, speaker centered. Windowed host audio is 48 kHz stereo through SDL.

At turbo `max`, AY chip time still advances so music stays aligned when you return to
1 MHz, but host PCM is not generated at free-run speed.

`--audio-smoke` emits a 440 Hz tone through the same path so you can confirm that
samples reach the host device.

## Interface

### Display

When launched, a2m shows the Apple 2 display filling the window. Regular keys are
forwarded to the emulated Apple 2. The window can be resized; the display always
scales to fit the available area. Whether it keeps a classic 4:3 monitor shape, or
simply stretches to fill, is the **True Aspect Ratio** setting. See **Display and
Scaling**.

The framebuffer is 560 x 192. 40-column text, LORES, and HGR are pixel-doubled
horizontally; 80-column text and DHGR use the full width.

Painted modes:

| Mode | What you see |
|------|----------------|
| 40-column text | Flash and inverse; white on black |
| 80-column text | Main/aux interleave |
| LORES | 16-colour cells |
| Double LORES | 80 half-columns from aux then main; 16-colour cells |
| HGR | Colour from neighbouring bits (green, violet, orange, blue, black, white) |
| DHGR | 16-colour lookup from a 5-bit window |
| Mixed | Graphics with four text lines at the bottom |

HGR and DHGR are shown in colour using a digital lookup of neighbouring bits (the usual
Apple II green, violet, orange, blue, black, and white). This is not a composite NTSC
simulation, so fringe colours, chroma bleed, and effects that depend on a real TV
decoder will not match hardware.

Double low-resolution graphics use the same 16-colour cells as LORES, but each
scanner column is two 7-pixel half-cells (auxiliary RAM, then main). Mixed
double LORES keeps 80-column text in the bottom four lines.

Press **F9** to open or close Debug Mode. Press **Opt+H** to open or close the
in-emulator help. On macOS, **Cmd+Q** quits; on Windows and Linux, **Opt+Q** quits.

### Window Title

The OS window title shows the model, active turbo mode, and runtime state,
even when Debug Mode is closed and no other indicator is visible:

| Title | Meaning |
|-------|---------|
| `a2m - //e Enhanced - 1 MHz - Running` | //e Enhanced at real-time speed, executing normally |
| `a2m - ][+ - max - Paused (reason)` | ][+ in max free-run; execution has stopped. `reason` is one of `breakpoint`, `BRK`, `step`, `reset`, `pause`, or `run complete` |
| `a2m - //e Enhanced - 8 MHz - Running` | Zip-class finite MHz, still live paint |
| `a2m - //e Enhanced - 1 MHz - Error` | The runtime hit an error and stopped |

This lets you tell whether the emulator is paused or running without opening the debugger.

### Debug Mode

In Debug Mode, the window is divided into four main areas:

| Area | Contents |
|------|----------|
| Upper left | Apple 2 display (scaled to fit its region) |
| Upper right | CPU register view |
| Right, below | Disassembly view |
| Lower left | Memory view |
| Lower right | Misc panel (Machine, Debugger, Breakpoints, Hardware, Assembler tabs) |

a2m tracks an active view for keyboard input. When no modal dialog is open, the active
Apple 2 display, Disassembly, Misc, or Memory view has a neutral gray outline. Click a
view to make it active, or press **Opt+Tab** to cycle Apple 2 -> Disassembly -> Misc ->
Memory. Press **Shift+Opt+Tab** to cycle in reverse. Modal dialogs keep input to
themselves, so these view-cycling keys do not work while a dialog is open.

### Layout

Two splitters divide the debug layout:

- A **vertical splitter** between the Apple 2 display region and the CPU/Disassembly pane.
- A **horizontal splitter** between the upper and lower halves.

Drag the splitters to resize the panes. A **corner handle** at the bottom-right of the
Apple 2 display region moves both splitters together; clicking it without dragging snaps
the display region to the true aspect (see **Display and Scaling**). The window size and
splitter positions are saved to the INI file on quit.

### Turbo Mode

**Opt+T** cycles through the configured turbo ladder (default `1,max`). The list is
stored in the INI file.

Turbo is a list of MHz targets for the whole emulated machine (CPU, video beam,
peripherals, and Mockingboard stay in lock-step):

| Entry | Title | Behaviour |
|-------|-------|-----------|
| `1` | `1 MHz` | Real-time Apple pace (about 1.02 MHz), live paint |
| `4`, `8`, ... | `4 MHz`, `8 MHz`, ... | Zip-class finite MHz (best-effort), live paint |
| `max` or `-1` | `max` | Free-run as fast as the host allows, still full live paint |

The first entry is the startup speed. Paste does not change turbo. By default the CPU
flight recorder is paused while turbo is `max` (Configure -> Machine, or
`--history-off-on-max` / `--no-history-off-on-max`); recording resumes when you leave
`max`.

### Help

Press **Opt+H** or **ESC** to open or close the in-emulator help overlay. The Apple 2
pauses while the overlay is open and resumes when it is dismissed.

The overlay shows one section of the manual at a time in a scrollable content area. A
navigation bar along the bottom of the overlay contains:

| Control | Action |
|---------|--------|
| **Prev** | Go to the previous section. Inert when already on the first section. |
| *Section name* (centre) | Shows the current section. Click to open a pop-up index of all sections; click any entry to jump directly to it. |
| **Next** | Go to the next section. Inert when already on the last section. |
| **Search:** field | Type a search term and press **Enter**, **->**, or **<-** to search. Supports regular expressions. |
| **<-** | Find the previous match, searching backward from the current match. |
| **->** | Find the next match, searching forward from the current match. |

Search is case-insensitive. When no match exists the search text turns red; it returns
to normal as soon as the term is changed. Both directions wrap around the full document.
Navigating to a new section via **Prev**, **Next**, or the index resets the search
starting point to the top of that section.

Matches are highlighted. The match you jumped to is drawn in black on a yellow band, and
every other match in the section on screen is underlined in yellow. The highlight follows
whatever is in the **Search:** field, so matches light up as you type and disappear when
the field is cleared. The view scrolls so the current match sits about a third of the way
down the content area. A match that falls across a line break is still found, but is not
highlighted.

Keyboard shortcuts active while the help overlay is open:

| Key | Action |
|-----|--------|
| **Left / Right** | Previous / next section |
| **PageUp / PageDown** | Scroll content up / down by one page |
| **Home** | Scroll to top of the current section |
| **End** | Scroll to bottom of the current section |
| **ESC** | Close the help overlay |

## CPU View

The CPU view shows the current state of the 6502 (Apple ][+) or 65C02 (Apple //e
Enhanced) as reported by the most recent runtime snapshot:

| Field | Width | Description |
|-------|-------|-------------|
| PC | 16-bit | Program counter |
| SP | 8-bit | Stack pointer (page 1 offset) |
| A | 8-bit | Accumulator |
| X | 8-bit | X index register |
| Y | 8-bit | Y index register |
| N V - B D I Z C | 1-bit each | Processor status flags |

When the CPU is paused, all fields are editable:

- PC: four hex digits.
- SP, A, X, Y: two hex digits.
- Flags: `0` (clear) or `1` (set).

The `-` position in the flag row represents the unused bit; it is always 1 and cannot
be modified.

## Disassembly View

The Disassembly view shows the code at and around the program counter. While running,
the current instruction (the PC line) scrolls into view. While paused, the cursor is
independent of the PC.

### Line Format

Each line follows this general format:

```
C27D: WAITKEY1      E6 4E       INC RNDL
```

| Column | Meaning |
|--------|---------|
| `C27D` | Hex address |
| `WAITKEY1` | Symbol name at that address (when available) |
| `E6 4E` | Raw bytes |
| `INC RNDL` | Disassembled instruction with resolved symbols |

Breakpoint addresses show an indicator in the left gutter.

### Effective Address and Value

When the emulator is paused, lines whose target address is not already obvious
from the operand gain a trailing annotation showing the resolved address and,
for memory reads and writes, the byte currently at that address:

```
C123:             B1 FB       LDA ($FB),Y   [$4050:25]
```

Here the pointer at `$FB/$FC` plus the current **Y** register resolves to
`$4050`, which currently holds `$25`. The address is computed from the current
CPU registers and the CPU-visible memory, so it reflects what the running CPU
would actually read or write.

The annotation appears for:

- indexed and indirect operands, such as `$40,X`, `$40,Y`, `($40,X)`, and `($FB),Y`,
  which show `[$addr:value]`;
- `JMP ($xxxx)` indirect jumps, which show the resolved `[$addr]` target;
- direct addresses, branches, `JMP`, and `JSR` operands that are shown as a
  **label** - data references show `[$addr:value]`, branch and jump targets
  show `[$addr]`.

It is deliberately omitted where the address is already plain in the operand,
such as `LDA #$00` (immediate), `LDA $4000` (literal absolute), and
`LDA $FB` (literal zero page). Because the annotation depends on the current
register and memory snapshot, it is shown only while the machine is paused, and
it is not drawn while the emulator is running.

### Display Modes

The disassembly view can show bytes from any of these sources:

| Mode | Meaning |
|------|---------|
| **Map** | CPU-visible address space (current bank configuration) |
| **Main** | Physical main 48K |
| **Aux** | Physical auxiliary 48K (//e) |
| **LC1** | Language Card bank 1 |
| **LC2** | Language Card bank 2 |
| **ROM** | System ROM bytes at ROM addresses, regardless of mapping |

Right-click anywhere in the view to open a **Source** menu listing all six modes,
with an asterisk next to the active choice. **Opt+M** from the keyboard cycles
Map -> ROM -> Main -> Map.

When the emulator is paused, the same popup also shows an **Access** group for the
address under the disassembly cursor. The four `XXXX` entries are the recorded program
counters of the last instructions that wrote to that address, oldest retained entry
first and newest entry last. `0000` means no writer has been recorded for that slot.
Selecting one of the entries moves the Disassembly cursor to that address, the same kind
of jump as entering the writer PC with `Opt+A`.

### Keyboard Controls

| Key | Action |
|-----|--------|
| `Opt+A` | Enter address-jump mode; type four hex digits then Enter |
| `Opt+B` | Toggle execute breakpoint at cursor (paused only) |
| `Opt+M` | Cycle source mode: Map -> ROM -> Main -> Map |
| `Opt+S` | Open the Symbol Lookup dialog |
| `Opt+Left` | Set PC to cursor address (paused only) |
| `Up` / `Down` | Move cursor one instruction |
| `PgUp` / `PgDn` | Scroll one page |
| `Home` / `End` | Jump to first or last line of the current view |
| `Opt+Home` | Jump to address `$0000` |
| `Opt+End` | Jump to address `$FFFF` |

### Symbol Lookup

**Opt+S** opens the Symbol Lookup dialog while the Disassembly view is active.

The dialog shows a searchable, sortable table of all symbols known to the debugger,
including labels exported from the assembler and symbols loaded from external symbol
files.

**Columns:**

| Column | Contents |
|--------|----------|
| `ADDR` | Symbol address in hex (`XXXX`) |
| `SCOPE` | Assembler scope path, e.g. `anon_0001` (up to 15 characters) |
| `LABEL` | Symbol name (leaf portion, up to 15 characters) |
| `SOURCE` | File basename (no extension), or `assembler` for inline assembly |

**Search:** the field at the top has focus when the dialog opens. Type to filter the
list. The pattern is matched against a combined string `"XXXX scope label source"` for
each row using simple regex syntax: `.` matches any character, `*` matches zero or more
of the previous character, `^` anchors to the start, `$` anchors to the end.

**Sorting:** clicking any column header sorts by that column ascending (`^`). Clicking
the same header again reverses to descending (`v`). The default sort is by address
ascending.

**Navigation:**

| Key / Action | Effect |
|--------------|--------|
| Type in search box | Filter rows to matching symbols |
| `Tab` | Switch keyboard focus between search box and table |
| `Up` / `Down` | Move the selection in the table (table focus) |
| `Enter` | Commit selected row (table focus) |
| Click a row | Commit that row |
| Click a column header | Sort by that column (toggle direction) |
| **[Close]** or `ESC` | Dismiss without navigating |

**On commit:** the Disassembly view cursor jumps to the symbol's address, equivalent to
entering the address with `Opt+A`.

## Memory View

The Memory view shows the full 64 K address space as 16-byte rows in hex and ASCII.

### Line Format

```
C123: 48 65 6C 6C 6F 20 57 6F 72 6C 64 21 00 00 00 00  Hello World!....
```

| Column | Meaning |
|--------|---------|
| `C123` | Hex address of the first byte in the row |
| `48 65 ...` | Byte values in hex |
| `Hello ...` | ASCII representation (`.` for non-print) |

### Display Modes

The memory view has source modes that control which address space is displayed:

| Mode | Mode border | Bytes shown |
|------|-------------|-------------|
| **Map** | none | CPU-visible address space (current bank configuration) |
| **Main** | blue | Physical main 48K |
| **Aux** | green | Physical auxiliary 48K (//e) |
| **LC1** | purple | Language Card bank 1 |
| **LC2** | magenta | Language Card bank 2 |
| **ROM** | amber | System ROM bytes at ROM addresses, regardless of mapping |

Non-Map modes draw a colored source-mode border inside the content area.
Map has no source-mode color; if the view is active, the separate neutral active-view
border is still shown.

Switch modes with **right-click** anywhere in the view (the **Source** group lists all
choices with an asterisk next to the active choice), or with **Opt+M** from the keyboard.
**Opt+M** cycles Map -> Main -> Aux -> LC1 -> LC2 -> ROM -> Map.

The memory and disassembly view modes are independent of each other. You can watch
auxiliary RAM in the memory view while the disassembler follows the CPU map.

### Status Row

The bottom of the Memory view shows the active edit field (`Hex`, `ASCII`, or
`Address`), the current cursor address as `Address: XXXX`, and whether memory editing
is currently `editable` or `read-only`.

### Virtual Views

The memory panel can be split into up to 16 independent virtual views stacked vertically.
Each virtual view maintains its own cursor, scroll position, source mode, and edit state.
A thin separator line marks the boundary between adjacent views.

**Splitting** inserts a new view directly below the active view. The new view inherits the
active view's source mode and starts with its cursor at the split address. Row height is
distributed proportionally among all views; each view has a minimum of one row.

**Dissolving** removes the active view and returns its rows proportionally to the remaining
views. If only one view exists, dissolving is a no-op. After dissolving, focus moves to
the view below, or to the view above if the dissolved view was the bottommost.

Each virtual view has a unique background color drawn from a 16-slot palette. Slots are
assigned in order and freed when a view is dissolved; a freed slot is reused by the next
split.

Click anywhere in a view to make it active. The mouse wheel scrolls the view under the
pointer regardless of which view is currently active.

Source-mode borders are drawn inside each view's own region. The neutral
active-panel selection border still wraps the entire memory panel regardless of how many
views are present.

The scrollbar on the right represents the active view's position in the 64 K space.
Switching the active view moves the thumb without scrolling the memory itself.

Right-clicking a memory view opens a popup for the view under the pointer. The
**Source** group changes that view's source mode. The **View** group can **Split** the
clicked view at the clicked address; when more than one virtual view exists it also
offers **Join** to dissolve the clicked view.

When the emulator is paused, the popup also shows an **Access** group for the clicked
address. The four `XXXX` entries are the write history for that address:

```
oldest  older  newer  newest
```

Each entry is the 16-bit program counter of an instruction that wrote to the address.
Selecting an entry moves the Disassembly cursor to that writer PC.

### Find

With emulation stopped and the Memory panel active, **Opt+F** opens Find.
Choose String (optionally ignoring case) or Hex; hex accepts complete byte pairs
with optional spaces, such as `DE AD BE EF`. **Opt+G** repeats forward and
**Opt+Shift+G** searches backward. Search follows the selected source of the
active split Memory view and wraps around the 64K address space.

### Keyboard Controls

| Key | Action |
|-----|--------|
| `Opt+A` | Toggle address-entry mode; type four hex digits to jump |
| `Opt+M` | Cycle source mode: Map -> Main -> Aux -> LC1 -> LC2 -> ROM -> Map |
| `Opt+S` | Open the Symbol Lookup dialog |
| `Opt+F` | Open Find (paused only) |
| `Opt+G` | Find next |
| `Opt+Shift+G` | Find previous |
| `Opt+X` | Toggle between hex and ASCII edit modes |
| `Opt+V` | Split active view at cursor |
| `Shift+Opt+V` | Split active view at the start of the cursor row |
| `Opt+J` | Dissolve active view (no-op when only one view exists) |
| `Opt+Up` | Switch focus to the view above |
| `Opt+Down` | Switch focus to the view below |
| `Up` / `Down` | Move cursor one row (16 bytes) |
| `Left` / `Right` | Move cursor one byte (or nibble in hex mode) |
| `PgUp` / `PgDn` | Scroll one page |
| `Home` | Move cursor to start of the current row |
| `Opt+Home` | Move cursor to the start of the visible window |
| `End` | Move cursor to end of the current row |
| `Opt+End` | Move cursor to the end of the visible window |
| `0-9`, `A-F` | Edit hex nibble at cursor (paused only, hex mode) |

**Opt+S** also opens the Symbol Lookup dialog from the Memory view. On commit, the
active virtual view scrolls so that the symbol's address is row-aligned (the row
containing that address appears at the top of the view) and the cursor is placed on
the exact byte. See **Symbol Lookup** under **Disassembly View** for full dialog
reference.

Memory editing is only possible while the CPU is paused. In hex mode, typing hex digits
overwrites the nibble at the cursor. In ASCII mode, printable characters overwrite the
byte at the cursor.

## Machine

The Machine tab is the first tab in the Misc panel and groups controls for slots,
media, machine files, and emulator management.

### Slots and Media

Each installed Disk II or SmartPort card is listed by slot. Empty slots and
Mockingboard cards have no media row. A Disk II or SmartPort row looks like:

```
Slot 6: Disk II
[6.0] [Eject] [Insert] [Swap (01/02)]  sideA.nib
[6.1] [Eject] [Insert]                 (empty)
```

**[N.0]** boots device 0 of that slot (jumps to `$CN00`). Drive 1 is labelled but
is not a boot button.

**[Insert]** opens a file browser and mounts the selected image. For Disk II, more
than one file can be queued; a **[Swap (current/total)]** button then appears.

**[Swap]** advances to the next image in that drive's queue (wraps).

**[Eject]** removes the current image from the drive and from the queue.

The name to the right of the buttons is the basename of the currently mounted image.

Configure the cards themselves (Empty / Disk II / SmartPort / Mockingboard) in
**Configure -> Machine**. Applying a changed model or card layout performs a
power-cycle reset.

### Machine Files

**[Load...]** opens the Load dialog:

| Field | Meaning |
|-------|---------|
| Name + Browse | Select the host file |
| Type | `Auto`, `Snapshot`, `Binary`, or `Applesoft text` |
| Format | For Auto/Binary: `Auto`, `Raw`, `NAPS #06AAAA`, `AppleSingle`, `Legacy DOS` |
| Raw address | Hex load address, used when the file has no embedded address |
| Reset before load | Reset the machine before injecting (off for snapshots) |
| Run after load | Jump to the load address after a binary load (off by default) |

`Auto` recognizes `.a2state` snapshots by extension. For binaries it recognizes
cc65 AppleSingle, NAPS filenames such as `demo#060803`, and the four-byte DOS header
used by older cc65 releases; otherwise it loads raw bytes at the entered address.

**Applesoft text** accepts an ASCII listing, sorts it by line number, tokenizes it
directly into `$0801`, clears variables, and repairs Applesoft's program pointers.
Duplicate or malformed lines are rejected.

**[Save...]** opens the Save dialog:

| Field | Meaning |
|-------|---------|
| Name + Browse | Choose the output filename |
| Type | `Snapshot`, `Binary`, or `Applesoft listing` |
| Format | For Binary: `NAPS #06AAAA` (default), `Raw`, or `AppleSingle` |
| Start / End | Inclusive hex range for a binary save |

**NAPS** appends `#06AAAA` (where `AAAA` is Start) and keeps the contents raw.
**Applesoft listing** validates and detokenizes the live program to a host ASCII
`.bas` file.

### State

Snapshots preserve the emulated machine: RAM (main and aux), CPU, soft switches,
video beam, Disk II, SmartPort, and Mockingboard. A failed load leaves the live
machine unchanged.

**Shift+Opt+>** quicksaves to the snapshot folder (Configure -> Paths -> `snapshot`,
which defaults to the current directory). Each quicksave creates a new timestamped
`.a2state` file; existing quicksaves are not overwritten. **Shift+Opt+<** quickloads
the newest `.a2state` in that folder.

At startup, use `--sna <file>` to load a snapshot from the command line. Over the
control port, use `load-state <path>` and `save-state <path>` (see **Remote**).

### Emulator Controls

**[Configure...]** opens the Configure dialog (see **Configure**).

**[Reset]** performs a warm reset of the emulated Apple 2 and preserves its running
state: it resumes automatically if it was running, or remains paused if it was
stopped. Any pending assembler-queued run is cancelled.

**F8** is the keyboard stand-in for CTRL+RESET (macOS often eats Control+F-keys).
**Opt+F8** is CTRL+Open-Apple+RESET (cold start / banner boot).

## Debugger

The Debugger tab shows runtime counters and the call stack.

### Counters

| Counter | Meaning |
|---------|---------|
| State / Turbo / PC / Stop | Current run state, turbo label, PC, and stop reason |
| CPU cycles | Total 6502/65C02 cycles executed |
| Machine | Master cycle counter |
| Frame | Frame number, cycle within frame, dropped frames |

### Step Counters

```
Step - CPU: 5  Machine: 5
```

The step counter line resets to zero each time a step or run command is issued (F10,
Shift+F10, F11, F12, Shift+F12) and freezes when the CPU stops. It shows how many
cycles elapsed during that operation.

Two values are shown because they can differ when video or a peripheral holds the
bus. **CPU** is cycles the processor actually spent executing. **Machine** is wall
clock of the Apple 2 chip, including stalls.

### Call Stack

The call stack shows the chain of active JSR calls reconstructed from the hardware
stack each frame. Entries have the form:

```
E69E | JSR FF59 OLDRST
```

| Part | Meaning |
|------|---------|
| `E69E` | Address of the JSR instruction |
| `FF59` | Destination address (subroutine entry) |
| `OLDRST` | Symbol name for `$FF59`, when available |

Clicking either address in an entry moves the Disassembly view cursor to that address.
Up to 16 entries are displayed; the list has its own scrollbar when there are more.

## Breakpoints

The Breakpoints tab lists all configured breakpoints. When no breakpoints exist, the
tab is empty. A **[Clear All]** button appears at the top when more than one
breakpoint is present. **[New]** opens the Breakpoint Editor.

### Breakpoint Types

| Type | Trigger condition |
|------|-------------------|
| Execute | CPU fetches an instruction at the address |
| Read | CPU reads from the address or range |
| Write | CPU writes to the address or range |

### BRK

When **Pause on BRK** is enabled (Configure -> Machine, or `[config] pause_on_brk`),
a `BRK` opcode (`$00`) pauses the emulator with no breakpoint needed. The CPU does
not execute it; the instruction is intercepted before the stack is touched, and the
window title ends with `Paused (BRK)`.

This usually indicates that execution has reached uninitialized memory, passed the
end of a program, or followed a corrupted jump vector. **Pause on BRK is off by
default.** A single explicit step (**F11**) always executes a BRK normally.

### Breakpoint List Format

Each entry in the list shows a label and action buttons:

```
W[C123-C1FF] (5/10)  [Edit] [Disable] [Clear]
```

| Part | Meaning |
|------|---------|
| `R`, `W`, `RW` | Access type (read, write, or either) |
| `[C123]` | Address; or `[C123-C1FF]` for a range |
| `(5/10)` | Counter: total hits / repeat countdown (shown when counter is active) |
| Action label | `Fast`, `Slow`, `Tron`, `Troff`, `Swap`, `Type`, or nothing (Break) |

- **[Edit]** opens the Breakpoint Editor.
- **[Disable]** / **[Enable]** toggles the breakpoint without removing it.
- **[Clear]** deletes the breakpoint.

Breakpoints are persisted in the INI file under the `[DEBUG]` section.

### Breakpoint Editor

The editor dialog lets you set the address (or address range), access type, mapping
filter, action, and counters for a breakpoint.

**Access** options: `Execute`, `Read`, `Write`.

**Mapping** is three independent rows:

| Row | Choices |
|-----|---------|
| RAM | Map / Main / Aux |
| C100 | Map / ROM |
| D000 | Map / LC1 / LC2 / ROM |

`Map` means fire regardless of the current banking for that region.

**Range**: check to enter a second address; any access in `[start, end]` triggers the
breakpoint.

**Actions**:

| Action | Parameter field | Effect |
|--------|-----------------|--------|
| Break | - | Pause execution (default) |
| Fast | - | Switch turbo to `max` |
| Slow | - | Switch turbo to 1 MHz |
| Troff | - | Disable per-instruction execution trace |
| Tron | Filename | Enable per-instruction execution trace; writes to the given file, or `trace.log` if the field is empty |
| Swap | Slot + queue step | Advance a Disk II queue (see below) |
| Type | Text | Inject text as Apple keystrokes when the breakpoint fires |

Tron and Troff are mutually exclusive: checking one automatically clears the other.
When Tron, Swap, or Type is unchecked, its parameter field is grayed out.

**Swap parameter format:**

| Form | Meaning |
|------|---------|
| `+N` | Move forward N steps in the queue (wraps) |
| `-N` | Move backward N steps in the queue (wraps) |
| `N` | Mount the Nth disk in the queue, 1-based (wraps if out of range) |
| empty | No-op (Swap flag is set but does nothing) |

The slot field is the Disk II controller slot (`1..7`).

**Counter**: enter a hit count and a repeat count. With hit count `N` and repeat count
`M`, the action fires on the Nth hit and then every Mth hit thereafter. Set repeat to
`0` to auto-disable the breakpoint after it fires once. Set both to 0 to disable
counting (the Use Counter checkbox controls this).

**Type text format:**

The Type field uses an escape-based input encoding. Literal printable characters
(space through `~`) are typed as Apple keystrokes; newlines become Return. `\[`
introduces a named token. Names are case-insensitive. Up to 128 events per sequence.

| Form | Meaning |
|------|---------|
| `\[OA]` `\[OA+]` `\[OA-]` | Open-Apple pulse / hold / release |
| `\[CA]` `\[CA+]` `\[CA-]` | Closed-Apple pulse / hold / release |
| `\[B0]` `\[B1]` | Gameport buttons (with `+` / `-` hold forms) |
| `\[RESET]` | Warm reset (CTRL+RESET) |
| `\[COLDRESET]` | Cold reset (CTRL+Open-Apple+RESET) |
| `\[W:N]` / `\[WAIT:N]` | Wait N keypress units |
| `\[J1X=n]` `\[J1Y=n]` | Stick 1 axes `0..255` (`128` = center); `J2` is stick 2 |
| `\[J1XL]` `\[J1XR]` `\[J1YU]` `\[J1YD]` | Stick 1 extremes |
| `\[J1XC]` `\[J1YC]` `\[J1C]` | Center one or both axes |

Bare `OA` / `CA` / `B0` / `B1` tokens pulse (assert, wait one unit, deassert).
Clipboard paste (**Opt+Ins**) is always plain text; it does not parse this language.

Examples:

```
\[OA]Y               Open-Apple+Y, then release
CATALOG\r            Type CATALOG and press Return
\[W:4]\[RESET]       Wait, then warm reset
\[J1YU]\[B0]         Stick 1 up and fire
```

Setting an execute breakpoint from the keyboard while the cursor is in the Disassembly
view is faster: position the cursor and press **Opt+B**. A second press removes the
breakpoint.

## Hardware

The Hardware tab provides a view of the emulated Apple 2 using data from the most
recent runtime snapshot.

The first line is the model (`//e Enhanced` or `][+`).

**Display** rows show the actual soft-switch state and, when **Override** is checked,
a display-only checkbox. Override does not change the hardware; it only changes what
the host paints. This is useful when drawing to an off-screen buffer: turn override
on, set PAGE2 or HIRES as needed, and watch the hidden page. Turning override off
restores the real switches.

| Address | Name | Meaning |
|---------|------|---------|
| $C00D | 80COL | 80-column display |
| $C00F | ALTCHAR | Alternate character set |
| $C051 | TEXT | Text mode |
| $C053 | MIXED | Graphics with four text lines at the bottom |
| $C055 | PAGE2 | Display from $800/$4000 instead of $400/$2000 |
| $C057 | HIRES | High-resolution graphics |
| $C05E | DHIRES | Double-resolution graphics |

**Banking** rows are read-only:

| Address | Name | Meaning |
|---------|------|---------|
| $C001 | 80STORE | Display memory follows PAGE2 independently of RAMRD/WRT |
| $C003 | RAMRD | CPU reads aux $0200-$BFFF (see 80STORE) |
| $C005 | RAMWRT | CPU writes aux $0200-$BFFF (see 80STORE) |
| $C009 | ALTZP | Zero page and stack are in aux |
| $C08X | LC_READ | RAM vs ROM at $D000-$FFFF |
| $C08X | LC_WRITE | Write-enable Language Card RAM |
| $C08X | LC_BANK2 | Language Card bank 2 vs bank 1 |
| $C007 | CXROM | Internal $C100-$CFFF ROM |
| $C00B | C3ROM off | Slot 3 ROM vs internal $C300 ROM |

Addresses work in pairs on hardware (even/odd set and clear). The table shows the
latched result, not the poke that produced it.

## Assembler

The Assembler tab provides access to the integrated two-pass 6502 assembler.

### Assembler Controls

| Field | Meaning |
|-------|---------|
| File Name | Path to the root assembly source file; use **Browse...** to pick |
| Assemble at | Optional host origin. When checked, assemble with this hex default (default `$8000`). When unchecked, the source must set its own origin (`* =` / `.org`); the host supplies `$0000` as a placeholder only |
| Auto-run at | When checked, after a successful assembly sets PC to this hex address and resumes |
| Reset machine | If checked, resets the machine before assembling |
| Rearm one-shots | If checked, re-enables every auto-disabled one-shot breakpoint (`repeat = 0`) and resets its hit counter before assembling |
| **[Assemble]** | Assembles the source and loads bytes into Apple RAM |

After configuring the source and options once, **Shift+Opt+A** invokes the same
action globally without opening the panel.

When **Reset machine** is checked (the default), assembly waits for the machine to
come back from reset before writing code. When it is unchecked, the assembler writes
directly into live RAM in whatever state the machine is in. If **Auto-run at** is
also set, the emulator immediately jumps to that address and resumes execution.

**Rearm one-shots** is useful during iterative development: set a breakpoint with
`repeat = 0` so it fires exactly once, then check this box so each re-assemble brings
it back to life without manual re-enabling.

Assembler labels are exported to the debugger symbol table immediately after a
successful assembly, and appear in the Disassembly view.

If assembly fails, a scrollable error dialog shows each error with its source file and
line number.

Apple ][+ starts in 6502 mode; Apple //e Enhanced starts in 65C02 mode. A source
directive can override that initial profile.

### Assembler INI persistence

The following assembler settings are saved in the `[assembler]` section of the INI
file and restored on next launch. All keys are optional.

```
[assembler]
file           = path/to/source.asm   ; path to source file (relative to INI or absolute)
address        = 8000                 ; hex load/assembly origin address
use_address    = yes                  ; apply address as host origin (default: yes)
run_address    = 8000                 ; hex auto-run address
auto_run       = no                   ; jump to run address after assembly (default: no)
reset          = yes                  ; Reset machine before assembling (default: yes)
rearm_oneshots = no                   ; Rearm one-shot breakpoints before assembling (default: no)
auto_adjust_segments = no             ; Retry overlapping segment layouts (default: no)
```

When `auto_adjust_segments` is enabled, an overlap detected during pass 1 causes up
to three fresh layout retries using the assembler's suggested starts. Pass 2 only
runs after the layout no longer overlaps. The source file is not changed; after a
successful adjusted assembly, an **Assembly Adjustments** dialog lists the `.segdef`
addresses to copy back into the source.

### Assembler Language

The assembler supports standard 6502 mnemonics and addressing modes. The //e Enhanced
profile also accepts 65C02 opcodes unless the source selects `.6502`.

**Comments:** `;` begins a comment; everything after it on the line is ignored.

**Labels:** start with a letter or `_`, may contain letters, digits, and `_`, and end
with `:`.

**Variables:** assigned with `ident = expr`. Postfix `++` and `--` modify the variable
as a prefix operation: `lda #i++` loads the value after increment.

**Current address:** `*` reads the current output address - the address of the
instruction or line being assembled, following the standard assembler convention. For
example `jmp *` assembles a self-loop.

```
* = $6000
a = *       ; a = $6000
jmp *       ; jmp $6000 (self-loop)
```

**Numbers:**

| Prefix | Base |
|--------|------|
| `$` | Hexadecimal |
| `%` | Binary |
| `0` | Octal |
| `1`-`9` | Decimal |

**Expressions** support the full C precedence table:

| Operators | Notes |
|-----------|-------|
| `*`, `:`, numbers, variables, `(` | Atoms |
| `+`, `-`, `<`, `>`, `~` | Unary: plus, minus, low byte, high byte, bitwise NOT |
| `**` | Exponentiation |
| `*`, `/`, `%` | Multiply, divide, modulo |
| `+`, `-` | Add, subtract |
| `<<`, `>>` | Shift left, shift right |
| `.lt`, `.le`, `.gt`, `.ge` | Relational comparison |
| `.eq`, `.ne` | Equality, inequality |
| `&` | Bitwise AND |
| `^` | Bitwise XOR |
| `\|` | Bitwise OR |
| `&&`, `\|\|` | Logical AND, OR |
| `?`, `:` | Ternary conditional |

The ternary form is `condition ? true-expr : false-expr`.

### Directives

| Directive | Meaning |
|-----------|---------|
| `.org n` or `* = n` | Set assembly origin to address n |
| `.byte b[,b]*` | Emit one or more bytes (strings accepted) |
| `.word w[,w]*` | Emit 16-bit little-endian words |
| `.addr w[,w]*` | Synonym for `.word` |
| `.dword dw[,dw]*` | Emit 32-bit little-endian double-words |
| `.qword qw[,qw]*` | Emit 64-bit little-endian quad-words |
| `.drow w[,w]*` | Emit 16-bit big-endian words |
| `.drowd dw[,dw]*` | Emit 32-bit big-endian double-words |
| `.drowq qw[,qw]*` | Emit 64-bit big-endian quad-words |
| `.res l[,b]` | Reserve `l` bytes, filled with `b` (default `$00`) |
| `.align v` | Advance to the next multiple of `v`, padding with zeros |
| `.string "s"` or `.asciiz "s"` | Emit string bytes; `.asciiz` appends a `\0` |
| `.strcode e` | Set a per-character mapping expression using `_`; see below |
| `.include "f"` | Include and assemble another source file |
| `.incbin "f"` | Include a binary file verbatim |
| `.define name text` | Text substitution on word boundaries, skipping string literals |
| `.if cond` | Conditional assembly; condition uses `.lt .le .gt .ge .eq .ne .defined` |
| `.else` | Alternate branch of `.if` |
| `.endif` | End of conditional block |
| `.for init, cond, iter` | Loop with initialization, condition, and iteration clauses |
| `.endfor` | End of `.for` loop |
| `.repeat n[,v]` | Repeat block `n` times; optional counter variable `v` |
| `.endrepeat` / `.endrep` | End of `.repeat` block |
| `.macro name [args]` | Define a macro with optional parameter list |
| `.endmacro` | End of macro definition |
| `.local name` | Macro-local label; expanded to a unique name at call time |
| `.scope [name] [file="f"] [dest="d"]` | Open a scope namespace; anonymous if no name given. `name` may be a bare identifier or a quoted identifier (`"name"`). `file=`/`dest=` create a host-resolved output target |
| `.endscope` | Close the innermost scope (and end any output redirect) |
| `.proc name` | Open a named procedure (a named scope) |
| `.endproc` | Close the innermost proc |
| `.segdef "n",addr[,flags]` | Define a named segment at `addr`. Flags (comma-separated) are any of `emit`/`noemit` (suppress output) and `locked` (pin the address; never auto-moved) |
| `.segdef "n",reclaim="host"` | Define a reclaim segment overlaying an emitted `host`: implicitly `noemit`, inherits the host's start, sized no larger than the host, and moves with it if auto-adjust relocates the host |
| `.segment "n"` | Activate the named segment; `.segment ""` returns to native mode |
| `.6502` | Restrict to 6502 opcodes |
| `.65c02` | Allow 65C02 opcodes |
| `.rockwell` | Allow 65C02 plus Rockwell RMB/SMB and BBR/BBS operations |
| `.wdc` | Allow the Rockwell profile plus WDC WAI and STP |

**Paths:** `.include` and `.incbin` resolve relative to the directory of the including
file.

**`.byte` and strings:** `.byte` accepts strings as well as numeric expressions.
`.strcode` is not applied to string arguments of `.byte`.

### Macros

```
.macro name [arg [, arg]*]
    body
.endmacro
```

Arguments are substituted as text. Arguments that contain a comma must be wrapped in
double quotes: `"($55),y"`. Use `.local` inside a macro body to generate label names
that are unique per invocation.

```
.macro add_a_b a b
    clc
    lda a
    adc b
.endmacro

    add_a_b $12, $34
    add_a_b "($55),y", #12
```

### Scopes and Procs

`.proc` and `.scope` define namespaces that allow label reuse:

```
.proc loader
start:
    lda $C000
.endproc

.proc display
start:
    sta loader::start + 1    ; access a label in another scope
.endproc
```

Use `::` at the start of a name to resolve from the root scope. Labels in different
scopes do not collide.

A scope name may be written bare (`.scope game`) or quoted (`.scope "game"`); the two
forms are equivalent. Quoting is allowed for symmetry with `.segdef`, but because a
scope name is used in `::` qualified references (`game::start`), a quoted name must
still be a legal identifier -- it cannot contain spaces or other non-identifier
characters.

Anonymous scopes (`.scope` without a name) are useful inside
loops to prevent iteration labels from clashing:

```
.for i=0, i .lt 8, i++
  .scope
    addr = $C000 + i
  start:
    lda addr
  .endscope
.endfor
```

### Segments

Named segments let you lay out memory regions and switch between them:

```
.segdef "ZEROPAGE", $50, noemit
.segdef "CODE", $6000
.segdef "DATA", $8000

.segment "ZEROPAGE"
ptr:    .res 2

.segment "CODE"
main:
    lda (ptr),y
    ...

.segment "DATA"
table:  .byte $01,$02,$03
```

`noemit` segments advance the location counter but produce no output -- useful for
mapping zero-page variables without emitting placeholder bytes. A `noemit` segment
may not overlap any other segment; to overlay memory intentionally, use a reclaim
segment (below).

A **reclaim** segment piggybacks on an emitted "host" segment so a region can be
reused at runtime once the host's contents are consumed. The classic case is a title
image loaded as part of the executable and then relocated as the program starts,
freeing its load area for buffers and variables:

```
.segdef "TITLE", $2000              ; the image, emitted into the program
.segdef "REUSE", reclaim="TITLE"    ; buffers that reuse TITLE's space at runtime

.segment "REUSE"
buffer: .res $100                    ; lives at $2000, emits nothing
```

A reclaim segment is implicitly `noemit`, takes no address of its own (its labels
resolve into the host's memory), and is sized no larger than its host. It follows the
host automatically: if auto-adjust moves the host, the reclaim segment moves with it.
The host must be a previously defined, emitted segment. Consuming or relocating the
host before the reclaimed region is used is the programmer's responsibility -- the
assembler enforces only the address and size relationship, not the runtime ordering.

Emitted segments are checked for overlap. Normally an overlap fails assembly and
prints a compacted set of suggested starts. With segment auto-adjust enabled, those
starts are applied temporarily and pass 1 is restarted up to three times. This
re-evaluates `.align` padding at each new start, so a first approximation that changes
segment sizes can converge on a later retry. The lowest segment remains the packing
anchor; subsequent emitted segments are packed contiguously in address order.

A `locked` segment is an anchor whose start address is never changed by auto-adjust --
use it for regions the hardware requires at a fixed address, such as an HGR page that
must stay at `$2000`. Lower segments are still packed around a locked segment, but if
they grow enough to overrun it, auto-adjust does not attempt to reshuffle the layout
around the anchor: assembly fails with an error naming the locked segment, leaving the
fix to the author.

```
.segdef "CODE", $0800
.segdef "HGR",  $2000, locked   ; hires page -- must stay at $2000
```

### Strcode

`.strcode e` remaps string characters using the expression `e`. The variable `_` holds
the current character's ASCII value:

```
.strcode _ .ge 'A' && _ .le 'Z' ? _ - 'A' : _ .ge 'a' && _ .le 'z' ? _ - 'a' : _
.string "Hello"
```

This remaps A-Z to 0-25 and a-z to 0-25, leaving other characters unchanged. Restore
default behavior with `.strcode _`. Quoted escape sequences (`\n`, `\r`, `\t`, `\0`,
`\\`, `\xNN`, `\0NN` octal) are processed in strings but are not passed through
`.strcode`.

### Named Output Targets

A named `.scope` can send its output to a separate file or to a named memory bank, so
a single source can build several binaries in one pass -- a loader plus a game, a main
program plus overlays, and so on:

```
* = $0800
        ; ... loader code, written to the default output ...

.scope game file="game.bin" dest="main"
    * = $6000
main:
        inc $C034
        jmp main
.endscope
```

Everything between the named `.scope file="..."` and its `.endscope` is assembled into
its own output image; code outside goes to the default output. Labels remain visible
across files through the normal scope rules (`game::main`), so the loader can reference
addresses in the game and vice versa.

The assembler core passes `file=` and `dest=` to its host. Command-line `am65` uses
`file=` to create a separate binary and accepts but ignores `dest=`. A `dest=`-only
scope therefore continues writing into its parent file image.

In the emulator the attributes are orthogonal:

| Attributes | Effect |
|---|---|
| `dest=` only | Write bytes into the named memory bank(s) |
| `file=` only | Write a host file beside the assembled source; do not poke memory |
| both | Write memory and a host file |
| neither | Default unnamed output (current map view) |

Valid Apple II destinations are `map`, `main`, `aux`, `lc1`, and `lc2`
(case-insensitive), and independent selections may be combined, for example
`dest="aux,lc2"`. Unsupported names are assembly errors. Relative `file=` paths
resolve against the directory of the assembled source. If that directory is also
a HostFS mount root (or under one), the volume is rescanned so the guest sees the
new file.

### Build-Time Detection

The define `AM65` distinguishes how the source is being assembled: it is `1` under the
command-line `am65` tool and `0` when assembled by the in-emulator Assembler tab. The
emulator also predefines `APPLE2=1`; standalone `am65` defines no machine symbol. Use
these to switch behaviour between a live-poke session and a file build:

```
.if AM65
    * = $0800           ; standalone build: normal load address
.else
    * = $6000           ; live in the emulator: assemble into spare RAM
.endif
```

Additional build flags can be injected from the command line with `-D name[=value]`
(see below) and tested the same way.

### Command-Line Assembler (am65)

`am65` is a standalone build of the same assembler, for use in scripts and makefiles.
It writes raw binary files rather than poking live memory.

```
am65 -i <infile> [-o <outfile>] [-a <addr>] [-s <symfile|->]
     [-C <6502|65c02|rockwell|wdc>] [-D name[=value]]...
     [--auto-adjust-segments] [-v] [-h]
```

| Switch | Effect |
|--------|--------|
| `-i <infile>` | Assembly source to assemble (required) |
| `-o <outfile>` | Binary output for the default (unnamed) target |
| `-a <addr>` | Origin/load address of the default target (default `$0000`; accepts `$hex`, `0xhex`, or decimal). Not needed if the source sets its own origin with `* =` / `.org` |
| `-s <symfile\|->` | Write a symbol + segment listing; `-` sends it to stdout |
| `-C`, `--cpu <name>` | Select the initial CPU profile (default `6502`) |
| `-D name[=value]` | Predefine a text define (value defaults to `1`); repeatable |
| `-A`, `--auto-adjust-segments` | Retry overlapping pass-1 layouts up to three times using suggested starts |
| `-v` | Verbose: hex-dump each target's emitted bytes |
| `-h` | Show usage |

Each output file contains exactly the range of addresses the source emitted into.
Named `.scope file="..."` targets are written to their own files (resolved relative to
the current directory). `dest=` is accepted but ignored. `AM65` is predefined to `1`;
no emulator machine symbol is predefined.

```
am65 -i demo.asm -o loader.bin -a $0800 -D VERSION=3 -s symbols.txt
```

Sample sources live in `samples/`.

## Configure

The Configure dialog (opened from **[Configure...]** in **Misc -> Machine**) has three
tabs: **Machine**, **Emulator**, and **Paths**. Shared controls for the INI file sit
below the tab body on every tab.

### Machine

| Control | Effect |
|---------|--------|
| Model | `//e Enhanced` or `][+`; applying a change power-cycles the machine |
| Slot 1-7 | Card in that slot: `Empty`, `Disk II`, `SmartPort`, or `Mockingboard`. Only one Mockingboard is allowed; choosing a second slot clears the first. Applying a changed layout power-cycles the machine |
| Keyboard Joystick | `Off`, `Stick 1`, or `Stick 2`, plus the `Numpad` or `WASD` key layout |
| Swap fire keys | While the stick is on: Space is button 0 and Option is button 1 (WASD-friendly). Off when the stick is Off |
| Turbo | Comma-separated ladder, e.g. `1,max` or `1,4,8,max` |
| History off on max | Pause the CPU flight recorder while turbo is `max` (faster free-run) |
| Pause on BRK | Auto-pause free-run at the next `BRK` (`$00`); off by default |
| Show disk LEDs | Draw green (read) and red (write) activity LEDs in the window corner |

The Keyboard Joystick stick selector matches the runtime **Shift+Opt+1** /
**Shift+Opt+2** assignment; either place can change the active stick. Change the
layout here or with **Shift+Opt+M**. Default at first launch is Stick 1 / Numpad so
titles that expect a gameport have a keyboard stick without a pad.

### Emulator

| Control | Effect |
|---------|--------|
| Scroll Wheel Speed | Number of rows scrolled per wheel click (1-100) |
| Original DEL behaviour | Backspace sends Apple II DEL (`$7F`) instead of cursor-left (`$08`). The physical Delete key always sends DEL |
| Symbol Files | Add symbol files and display the comma-separated list of selected files |
| True Aspect Ratio | Keep a classic 4:3 Apple monitor shape; off stretches the picture to fill the view |
| CRT Smoothing | Filter the picture instead of showing hard pixel edges; forced on by CRT Scanlines and CRT Curvature |
| CRT Scanlines | Simulate the dark gap between raster lines; the slider sets strength from 1-100% |
| CRT Curvature | Bend the picture toward a curved CRT surface; the slider sets amount from 1-100% |

The CRT controls are a live preview: checkboxes and sliders update the Apple 2 display
while Configure remains open. **[Cancel]** or the dialog close button restores the
values that were active when Configure opened. **[OK]** accepts them. All three
effects are optional and independent; with them disabled, a2m uses the original
rectangular render path.

### Paths

The Paths tab holds the default browse folders. Each field starts empty (the browser
then opens in the current working directory) and updates as you pick files. Paths are
shown and stored relative to the INI file's directory, and each row has a **[...]**
button that opens a folder picker:

| Field | Used by |
|-------|---------|
| assembler | Select Assembler Source |
| floppy | Insert Disk II images |
| smartport | Insert SmartPort volumes |
| binary | Load/Save Binary |
| basic | Load/Save Applesoft text |
| snapshot | Save/Load State - and the quicksave folder (Shift+Opt+> / <) |

Edits to the browse folders take effect on the next browse immediately. The folder
picker's **[Use This Folder]** button selects the folder currently shown. **[Save
Paths Only]** rewrites just the browse folders (and current media paths) into the
named INI file, leaving every other setting untouched; it is a silent no-op if no
INI file is set.

### INI File

Below the tab body, the Configure dialog shows the INI file path and a **[...]**
button. Changing the path prompts a2m to parse the selected file immediately.

**Auto-save INI on Quit** enables persistent save-on-quit behavior by writing `Save=yes`
under `[config]`. When that box is ticked, quitting the emulator writes the current
settings to the named INI file.

| Button | Effect |
|--------|--------|
| **[Save INI now]** | Apply the dialog settings and write the INI file immediately (including the current window size and layout splitters); Configure stays open |
| **[OK]** | Apply all changes without writing the INI (quit still saves when Auto-save is on) |
| **[Cancel]** | Discard dialog changes |

## INI Files

a2m reads `a2m.ini` from the current directory by default. Use `--inifile <path>` to
load a different file, or `--noini` to skip loading entirely.

Relative paths in the INI (disks, browse folders, assembler source, symbols) are
resolved against the directory that contains the INI file, not against the process
working directory. Absolute paths stay absolute. On save, paths near the INI (under
it, or a close sibling via at most two `..` steps) are rewritten relative so a
movable install stays portable; farther absolute paths are kept absolute.

Command-line media paths (`--disk`, `--hd`, and similar) stay relative to the shell's
current directory.

All section names are case-insensitive. Comment lines start with `#`. Saving from the
emulator removes comments.

### [machine]

| Key | Value |
|-----|-------|
| `model` | `enh` (//e Enhanced, default) or `plus` (][+) |

### [Slots]

| Key | Value |
|-----|-------|
| `slot1` ... `slot7` | `empty`, `diskii`, `smartport`, or `mockingboard` |

Default layout: slot 4 Mockingboard, slot 6 Disk II, slot 7 SmartPort, others empty.

### [config]

| Key | Value |
|-----|-------|
| `Save` | `yes` -- save INI on quit |
| `turbo_speeds` | Comma-separated turbo ladder, e.g. `1,max` |
| `history_off_on_max` | `true`/`false`; pause flight recorder on `max` (default true) |
| `scroll_wheel_lines` | Integer; lines scrolled per wheel click |
| `original_del` | `true`/`false`; Backspace sends `$7F` instead of `$08` |
| `symbol_files` | Comma-separated list of symbol file paths |
| `pause_on_brk` | `true`/`false`; when true, free-run auto-pauses at the next `BRK` |
| `disk_leds` | `true`/`false`; show disk activity LEDs (also written as `[disk] show_disk_leds`) |

### [Video]

| Key | Value |
|-----|-------|
| `true_aspect` | `true`/`false`; true keeps 4:3, false fills the view |
| `crt_smoothing` | `true`/`false`; filter the picture rather than show hard pixel edges |
| `crt_scanlines` | `true`/`false`; enable scanlines |
| `crt_scanline_strength` | Integer 1-100; scanline darkness (default `35`) |
| `crt_curvature` | `true`/`false`; enable curved-screen distortion |
| `crt_curvature_amount` | Integer 1-100; curvature amount (default `30`) |

### [input]

| Key | Value |
|-----|-------|
| `keyboard_joystick_layout` | `numpad` or `wasd` (default `numpad`) |
| `keyboard_joystick_port` | `0` (disabled), `1`, or `2` (default `1`) |
| `keyboard_joystick_swap_buttons` | `true`/`false`; Space=BUTN0 and Option=BUTN1 while the stick is on |

The stick can also be set for one launch with `--kbdjoy <0|1|2>`, and the layout with
`--kbdjoy-layout <numpad|wasd>`.

### [browse]

Default folders the file browser remembers per browse type (see the Configure
dialog's Paths tab). Any missing key defaults to the current working directory.

| Key | Used by |
|-----|---------|
| `assembler` | Select Assembler Source |
| `floppy` | Insert Disk II images |
| `smartport` | Insert SmartPort volumes |
| `binary` | Load/Save Binary |
| `basic` | Load/Save Applesoft text |
| `snapshot` | Save/Load State and the quicksave folder |

### [Window]

| Key | Value |
|-----|-------|
| `width` | Window width in pixels |
| `height` | Window height in pixels |

### [Layout]

| Key | Value |
|-----|-------|
| `split_display_right` | Float 0-1; vertical split between Apple 2 and debugger |
| `split_top_bottom` | Float 0-1; horizontal split between top and bottom |
| `split_memory_misc` | Float 0-1; split between memory and misc panel |

### [DiskII]

Each key is `sNdX` where `N` is the slot (`1..7`) and `X` is the drive (`0` or `1`).
The value is a path, or a comma-separated list of paths for a multi-image queue.

```
[DiskII]
s6d0 = ./disks/sideA.nib,./disks/sideB.nib
s6d1 = ./disks/util.dsk
```

The first image in the list is mounted at startup. The current position within the
queue is not saved; launching the emulator always starts from the first image.

Legacy `[disk] path=` is still accepted as Slot 6 Drive 0.

### [SmartPort]

Each key is `sNdX` where `N` is the slot and `X` is the unit (`0` or `1`).

| Key | Value |
|-----|-------|
| `s7d0`, `s5d0`, ... | Path to a block-device image **or** a HostFS directory |
| `boot_slot` | Force startup through SmartPort unit 0 in this slot |

A directory path mounts HostFS (NAPS folder volume with write-through). A file path
mounts the usual image backend. See **HostFS (folder as a ProDOS volume)**.

Legacy `[disk] hd=` is still accepted as Slot 7 Device 0.

### [assembler]

Persists the Assembler tab state. See **Assembler INI persistence**.

### [debug]

| Key | Value |
|-----|-------|
| `history_memory_mb` | CPU flight-recorder budget; `0` or `16..4096` (default `256`) |
| `frame_ring_memory_mb` | Frame-ring budget; `0` or `8..4096` (default `128`) |

### [DEBUG]

Breakpoints are stored as repeated `break.*` keys.

Each entry has the form:

```
break.<addr[-addr]> = <access>[,mapping][,actions][,count=N][,reset=N]
```

**Address and access:**

| Part | Meaning |
|------|---------|
| `address` | Hex address, e.g. `C000` or `$C000` |
| `-address` | Optional range end |
| `execute` | Execute breakpoint |
| `read` | Read-access breakpoint |
| `write` | Write-access breakpoint |
| `access` | Read or write |
| `map` / `main` / `aux` / `c100map` / `c100rom` / `d000map` / `lc1` / `lc2` / `rom` | Mapping filter |

**Actions:**

| Token | Meaning |
|-------|---------|
| `break` | Pause execution |
| `fast` | Switch turbo to `max` |
| `slow` | Switch turbo to 1 MHz |
| `troff` | Disable execution trace |
| `tron` | Enable execution trace; writes to `trace.log` |
| `tron=path` | Enable execution trace; writes to `path` |
| `swap=+N` | Advance the Disk II queue forward N steps (wraps) |
| `swap=-N` | Advance the Disk II queue backward N steps (wraps) |
| `swap=N` | Mount the Nth disk in the queue, 1-based |
| `type=text` | Inject text as Apple keystrokes (see **Type text format**) |
| `count=N` | Fire on the Nth hit |
| `reset=N` | Repeat interval after firing; `1` = every hit (default), `N>1` = every Nth hit, `0` = auto-disable after firing |
| `disabled` | Leave the breakpoint in the list but ignore it |

Examples:

```
break.C000 = execute,map,break
break.D000-D3FF = write,map,fast
break.C100 = execute,map,break,count=10,reset=2
break.55B5 = execute,map,swap=+1
break.55B8 = execute,map,type=\r
```

## Keyboard

Keys listed here are intercepted by the emulator before reaching the Apple 2. On
macOS, **Opt** = Option/Alt.

### Emulator Keys

| Key | Action |
|-----|--------|
| **F9** | Toggle Debug Mode on/off |
| **Opt+H** | Toggle in-emulator help on/off |
| **Shift+Opt+A** | Assemble the configured source file using the Assembler settings |
| **Shift+Opt+M** | Toggle keyboard joystick mapping between Numpad and WASD |
| **F10** | Step instruction (paused) or Pause (running) |
| **Shift+F10** | Step out of current subroutine |
| **F11** | Step over JSR |
| **F12** | Run (resume execution) |
| **Shift+F12** | Run to the cursor address in the Disassembly view |
| **F8** | Warm reset (CTRL+RESET) |
| **Opt+F8** | Cold reset (CTRL+Open-Apple+RESET) |
| **Opt+T** | Cycle turbo mode |
| **Opt+Tab** | Cycle active view: Apple 2 -> Disassembly -> Misc -> Memory |
| **Shift+Opt+Tab** | Cycle active view in reverse |
| **Opt+1** | Map gamepad to stick 1 |
| **Opt+2** | Map gamepad to stick 2 (or swap two pads) |
| **Shift+Opt+1** | Assign the keyboard joystick to stick 1 (press again to disable) |
| **Shift+Opt+2** | Assign the keyboard joystick to stick 2 (press again to disable) |
| **Shift+Opt+0** | Disable the keyboard joystick |
| **Shift+Opt+>** | Quicksave state to the snapshot folder (Configure -> Paths) |
| **Shift+Opt+<** | Quickload the newest state from the snapshot folder |
| **Cmd+Q** | Quit (macOS) |
| **Opt+Q** | Quit (Windows / Linux) |

### Paste and Clipboard

| Key | Action |
|-----|--------|
| **Opt+Ins** | Paste OS clipboard into the Apple keyboard (`$C000` / `$C010`). //e keeps case; ][+ uppercases. Does not change turbo. |

### Apple Key Mapping

The host keyboard maps to the Apple 2 keyboard. Common mappings:

| Host Key | Apple 2 |
|----------|---------|
| Letters A-Z | A-Z (//e keeps case; ][+ is uppercase) |
| Digits 0-9 | 0-9 |
| Shift + digit / symbol | The shifted Apple character (`!`, `@`, and so on) |
| Left Alt / Option | Open-Apple (`$C061` / BUTN0), unless the keyboard stick is on |
| Right Alt | Closed-Apple (`$C062` / BUTN1) |
| Ctrl | CONTROL |
| Escape | ESC |
| Tab | TAB |
| Backspace | Cursor-left (`$08`), or DEL (`$7F`) if Original DEL is on |
| Delete | Apple DEL (`$7F`) |
| Return | Return |
| Arrow keys | Arrow keys |
| Space | Space |

Host function keys are product-shell shortcuts and are not forwarded to the Apple 2.

### Keyboard Joystick

The host keyboard can also act as an Apple gameport stick. Default at first launch is
**Stick 1** / **Numpad**. Assign it with **Shift+Opt+1** / **Shift+Opt+2**, the
Keyboard Joystick control in the Configure dialog, or `--kbdjoy`.

| Layout | Directions | Diagonals | Fire 0 | Fire 1 |
|--------|------------|-----------|--------|--------|
| `numpad` | Keypad 8 / 2 / 4 / 6 | Keypad 7 / 9 / 1 / 3 | Option / Keypad 0 | Space |
| `wasd` | W / S / A / D | (hold two keys) | Option | Space |

**Swap fire keys** (Configure, or INI `keyboard_joystick_swap_buttons`) makes Space
primary and Option secondary while the stick is on. Stick off: Option is Open-Apple
again.

The `numpad` keys are not Apple keys, so that layout never interferes with typing.
The `wasd` keys are Apple keys, so while the stick is assigned they drive the
gameport instead of reaching the keyboard. They type normally when the stick is
disabled or when a debugger view has keyboard focus. The `numpad` layout uses
keypad key codes, so Num Lock must be on.

SDL gamepads: D-pad/stick, A=BUTN0, B=BUTN1. **Opt+1** / **Opt+2** map a single pad
to stick 1 or 2, or swap two pads.

## Remote

a2m has an opt-in localhost TCP control port for remote debugging and automation. It
is disabled by default. Start it with:

```sh
./a2m --control-port 6510
```

For automation without a visible window or host audio device, use headless mode:

```sh
./a2m --headless --control-port 6510
```

To restore a machine snapshot as soon as the process starts (before remote commands),
combine headless mode with `--sna`:

```sh
./a2m --headless --control-port 6510 --sna demos/midload.a2state
```

The server always binds to `127.0.0.1`. It accepts one client at a time. The socket
thread performs network I/O only; runtime commands and snapshot requests are dispatched
by the main loop, so remote control follows the same thread-ownership rules as the GUI
debugger. The current protocol name is `A2M/6`.

Python helpers:

```sh
./a2m --control-port 6510
python3 tools/a2m_control_client.py --port 6510 hello get-softswitches
```

### Quick Start

A remote session is line-oriented. Each request starts with a decimal request id, then a
command, then command arguments:

```text
1 ping
2 pause
3 get-cpu
4 get-memory $0400 64 map
5 set-memory $0400 4 main
<4 raw bytes>
6 quit-client
```

Responses begin with the same id:

```text
1 ok
2 ok accepted=1
3 ok pc=FF69 a=00 x=00 y=00 sp=F8 p=34 cycles=1712136
4 data memory 64 addr=0400 length=64 mode=0
<64 raw bytes>
5 ok addr=0400 length=4 mode=1
6 ok
```

`quit-client` closes the TCP client connection. It does not quit the emulator process.
Headless automation should terminate the process externally after the final client
command.

### Request Format

Requests are ASCII lines terminated by `\n`:

```text
<id> <command> [arguments...]\n
```

`<id>` is a decimal unsigned integer chosen by the client. a2m does not require ids to
be sequential, but sequential ids make logs easier to read. Commands are lower-case
words with hyphens. Hex addresses may be written as `0xC000` or `$C000`; decimal counts
and timeouts are written without a prefix.

Most commands are single-line. `set-memory` carries a length-prefixed raw payload after
the request line:

```text
<id> set-memory <addr> <length> <map|main|aux|lc1|lc2|rom>\n
<length raw bytes>\n
```

The payload may contain arbitrary bytes except that the framing still requires exactly
one trailing newline after the payload. `set-memory` payloads are limited to 1..1024
bytes. `get-memory` allows 1..65536 bytes in one call.

### Response Format

Text responses are ASCII lines terminated by `\n`:

```text
<id> ok [metadata...]\n
<id> error <code> <message>\n
```

Binary responses use a header line, exactly `<byte_count>` raw payload bytes, and one
trailing newline:

```text
<id> data <type> <byte_count> [metadata...]\n
<byte_count raw bytes>
\n
```

The client should parse the byte count from the `data` header and then read exactly that
many bytes before consuming the trailing newline. Do not treat binary payloads as
newline-delimited text.

Deferred responses use a multi-entry table. Token-matched commands such as `get-cpu`
and `get-memory` may be outstanding together. Other deferred work is still exclusive.
A second conflicting deferred command may return:

```text
<id> error busy deferred-response-active
<id> error busy wait already active
<id> error busy deferred-table-full
```

Clients may pipeline requests (send several without waiting); responses may complete
out of send order - correlate by request id. Duplicate outstanding ids are rejected
with `bad-id`.

Deferred commands time out with:

```text
<id> error timeout deferred response timed out
```

Headless mode wakes the main loop when a control request is queued. Prefer headless
for low-latency automation; a windowed session is still paced by present/vsync.

### Connection and Introspection

| Command | Response |
|---------|----------|
| `hello` | `ok name=a2m protocol=A2M/6` |
| `version` | `ok protocol=A2M/6 app=a2m` |
| `capabilities` | Space-separated capability names |
| `ping` | `ok` |
| `quit-client` | `ok`, then the server closes the client connection |

`capabilities` currently includes `connection`, `introspection`, `execution`,
`state`, `softswitches`, `step`, `turbo`, `frame`, `frame-ring`, `memory`,
`breakpoints`, `wait`, `key`, `disk`, `snapshot`, and `history`.

### Execution Control

Execution commands return after the command is accepted by the runtime, not after the
machine reaches a new state. Use `wait-*` commands when a script needs to synchronize.

| Command | Meaning |
|---------|---------|
| `reset` | Reset the emulated machine |
| `run` | Resume execution |
| `pause` | Pause execution |
| `step-cycle` | Execute one machine cycle |
| `step-instruction` | Execute one CPU instruction |
| `step-over` | Step over a JSR |
| `step-out` | Run until the current subroutine returns |
| `set-turbo <MHz\|max\|-1>` | Set turbo to a MHz target or `max` |

Accepted execution commands respond:

```text
<id> ok accepted=1
```

`set-turbo` changes only the active mode; it does not modify the configured Opt+T
turbo list. Its accepted response includes the requested mode:

```text
<id> ok accepted=1 turbo=max
```

### CPU Flight Recorder

The flight recorder continuously retains recent main-CPU execution and physical
bus accesses in a bounded memory arena. The default budget is 256 MiB. Set it
with `--history-memory=<MiB>` or `[debug] history_memory_mb`; `0` disables the
feature and other valid values are 16 through 4096.

| Command | Meaning |
|---------|---------|
| `history-info` | Report availability, recording state, epoch, timelines, retained IDs, records, and bytes |
| `history-record <on\|off>` | Resume or stop recording without discarding retained records |
| `history-clear` | Clear retained records and start a new epoch |
| `history-find [key=value ...]` | Search retained execution and markers |
| `history-next <cursor> [limit=1..256]` | Continue the current search |
| `history-read <id> [epoch=N] [before=0..256] [after=0..256]` | Read one record with surrounding context |
| `history-close <cursor>` | Close a cursor; closing an absent cursor is harmless |

Find, next, and read require the machine to be paused. Searches run newest-first
unless `direction=forward` is specified. Find accepts these keys:

```text
epoch timeline cycle from direction pc address access value opcodes limit
```

Ranges are inclusive. `from` is `oldest`, `newest`, or a retained record ID.
Access values are `execute`, `opcode`, `operand`, `data-read`, `data-write`,
`dummy-read`, `rmw-dummy-write`, `stack-read`, `stack-write`, or `vector-read`.
Aliases are `fetch`, `read`, `write`, and `data`. Opcode patterns contain 1..32
comma-separated bytes and may use `?` nibble wildcards, for example
`A9,??,8D`.

Find, next, and read return a counted binary `data history` response with
metadata:

```text
epoch=N count=N cursor=N more=0|1 oldest=N newest=N
```

The payload is little-endian HST1: a 24-byte header followed by records with a
48-byte header and 8-byte bus-access entries. Use
`tools/a2m_control_client.py` to validate and decode it. There is one search
cursor; execution, reset, recording control, state load, or direct machine
mutation makes it stale.

By default the recorder pauses while turbo is `max` and resumes when you leave
`max`.

### Frame Ring

The flight recorder retains what the CPU did; the frame ring retains what the
screen showed. It keeps the most recent completed frames in memory so a glitch
that lasted a single frame can still be retrieved after you notice it and pause.

The default budget is 128 MiB. Set it with `[debug] frame_ring_memory_mb`; `0`
disables the ring and other valid values are 8 through 4096. Frames are 560 x 192
ARGB.

| Command | Meaning |
|---------|---------|
| `frame-ring-info` | Report capacity, retained count, dropped frames, recording state, and the retained frame and cycle range |
| `frame-ring-record <on\|off>` | Resume or stop recording without discarding retained frames |
| `frame-ring-clear` | Discard retained frames |
| `get-frame-at <frame=N\|cycle=N>` | Fetch one retained frame |

The target must be named as either a frame number or a machine cycle, because a
bare number could be either and the wrong reading returns a plausible but wrong
frame. The lookup resolves to the nearest frame at or before the target. A target
past the newest returns the newest; a target older than the retained window
returns `not-found` rather than a substituted neighbour. Payloads are identical
to `get-frame`.

These commands answer immediately and work while the machine runs, although the
retained window keeps moving until you pause. Loading a machine state clears the
ring.

Each retained frame carries its machine cycle, which is the key for searching the
flight recorder for the same moment.

### State and Snapshots

| Command | Response |
|---------|----------|
| `get-state` | Text state summary: runtime state, CPU availability, frame, cycle, stop reason, turbo |
| `get-cpu` | Text CPU snapshot |
| `get-softswitches` | Latched soft-switch flags plus beam (not `$C0xx` memory) |
| `get-frame` | Binary 560 x 192 ARGB frame |
| `get-memory <addr> <length> <mode>` | Binary memory snapshot |
| `set-memory <addr> <length> <mode>` | Poke bytes (raw payload; auto-pauses) |
| `set-reg <name> <value>` | Set a CPU register (`pc`, `sp`, `a`, `x`, `y`, `p`) |
| `load-state <path>` | Load a `.a2state` snapshot |
| `save-state <path>` | Write a `.a2state` snapshot |

`get-state` is answered from the main loop's cached frontend debug state.
`get-frame` uses the latest completed frame cached by the main loop, or requests one
if no cached frame exists yet.

**Gotcha:** `get-memory` of `$C0xx` peeks RAM and never hits the soft-switch
handler. Use `get-softswitches` for video and banking state.

Memory modes:

| Mode | Meaning |
|------|---------|
| `map` | CPU-visible memory after current banking |
| `main` | Physical main 48K |
| `aux` | Physical auxiliary 48K |
| `lc1` | Language Card bank 1 |
| `lc2` | Language Card bank 2 |
| `rom` | System ROM |

### Waiting

| Command | Meaning |
|---------|---------|
| `wait-paused [timeout-ms]` | Return when the machine is paused |
| `wait-running [timeout-ms]` | Return when the machine is running |
| `wait-frame [delta]` | Return after `delta` frames (default 1) |
| `wait-event <name>` | Return when a named runtime event arrives |

### Input, Disks, and Breakpoints

| Command | Meaning |
|---------|---------|
| `key <byte>` | Inject one Apple keystroke (`$C000` path). `$8D` / CR becomes Return |
| `mount-disk <drive> <path>` | Mount a Disk II image on slot 6 drive `0` or `1` (legacy `8`/`9` also accepted) |
| `break-exec <addr>` | Set an execute breakpoint |
| `break-create <definition>` | Create a full breakpoint (same tokens as `[DEBUG]`) |
| `break-update <id> <definition>` | Replace an existing breakpoint |
| `break-list` | List breakpoints |
| `break-enable <id> <0\|1>` | Enable or disable a breakpoint |
| `break-clear <id>` | Clear one breakpoint (`0` = all) |
| `break-clear-all` | Clear every breakpoint |
| `rearm-oneshots` | Re-enable auto-disabled one-shot breakpoints |

## Details

This section records implementation details that are not necessary for day-to-day use
but are useful for understanding what the emulator actually does under the hood.

### Architecture

a2m uses a layered C99 architecture: Machine -> Runtime -> Frontend, with a shared
Tools layer. The machine (CPU, banking, video, Disk II, SmartPort, Mockingboard)
runs entirely on a dedicated runtime thread. The UI and renderer run on the main
thread. No live machine pointers cross threads -- all inter-thread data travels as
copied snapshots through a command/event queue. The snapshot rule is strictly
enforced: the frontend may only read runtime-provided copies and must never access
live machine state directly.

### CPU

Apple ][+ uses an NMOS 6502. Apple //e Enhanced uses a 65C02. The core is
cycle-stepped with the video beam: each CPU Phi0 advances one video cell, then
horizontal position, then vertical position. IRQ is level-sensitive. The //e
Enhanced instruction set is the documented 65C02 additions; Rockwell bit ops and
WDC WAI/STP are assembler profiles, not the emulated CPU.

### Memory and Banking

The machine has 64K of main RAM and, on the //e Enhanced, 64K of auxiliary RAM
plus Language Card banks at `$D000-$FFFF`. Soft switches in `$C000-$C0FF` select
the CPU-visible map (RAMRD, RAMWRT, ALTZP, 80STORE, CXROM, C3ROM, Language Card
read/write/bank). Slot ROM at `$Cn00` and the `$C800` shared space follow those
switches. SmartPort uses a `$C800` host trap for block commands.

### Video

Timing is NTSC Phi0: 65 cycles per line, 262 lines per frame, 17030 cycles per
frame. Visible lines are 0..191; VBL (`$C019`) is line 192 and above. Horizontal
active is scanner columns 0..39 (14 host pixels each, 560 wide). The floating bus
returns the scanner byte during active video and the last latch during blanking.
Mid-frame PAGE2 and mode switches are sampled at paint time.

Double low-resolution graphics use aux/main 7-pixel half-columns and the same
16-colour LORES palette (auxiliary nibbles are remapped as on a2m). Composite
NTSC artifact colour is not simulated; HGR and DHGR use a digital neighbour-bit
lookup.

### Disk II

A Disk II card can occupy any slot. Images are nibble streams (`.nib`), 140K DOS
order / ProDOS order (`.dsk` / `.do` / `.po`), or WOZ (read). Writes flush dirty
tracks back to `.dsk` and `.nib`. WOZ write fidelity is limited. Each drive keeps
an ordered multi-image queue; only one image is mounted at a time.

### SmartPort

A SmartPort card exposes two block units. ProDOS `$C0s4` / `$C0s5` and the `$C800`
trap handle the common read/write/status commands. `boot_slot` starts execution at
`$CN00` after unit 0 of that slot has mounted. Each unit is either an image file or
a HostFS directory (ProDOS map over NAPS-tagged host files, with live refresh and
write-through).

### Mockingboard

A Mockingboard is two AY-3-8910 chips behind a pair of 6522 VIAs. Only one card is
installed at a time. Enabling it costs host time even when silent. Host output is
stereo; at turbo `max` chip time advances without host PCM.

### Gameport

Paddles, buttons, a keyboard stick, and SDL pads feed `$C061`/`$C062`/`$C064`/`$C065`.
Open-Apple is button 0; Closed-Apple is button 1. Motor activity can light the disk
LEDs.

### Joystick

See **Keyboard Joystick**. A connected gamepad defaults to stick 2 mapping unless
**Opt+1** / **Opt+2** reassign it. A gamepad and the keyboard stick may share the
same stick, in which case their directions and fire are combined.

### Display and Scaling

The Apple 2 display is scaled to fit its panel. The display settings are under
**Misc -> Machine -> Configure -> Emulator**, and all of them preview live, with
Cancel restoring their previous values.

The `CRT` prefix marks the difference between them. **CRT Smoothing**, **CRT
Scanlines** and **CRT Curvature** are deliberate imitations of a CRT: they add
artifacts that were never in the Apple 2's digital signal, for looks. **True Aspect
Ratio** carries no prefix because it is not an imitation of anything - it is the
classic 4:3 monitor geometry.

**CRT Smoothing** filters the picture instead of drawing hard-edged pixels. CRT
Scanlines and CRT Curvature both force CRT Smoothing on, and show it ticked and
greyed out while they are enabled.

**True Aspect Ratio** decides the shape of the picture:

- **On** - the picture keeps a 4:3 Apple monitor shape. Letterbox or pillarbox
  fills the unused space (black).
- **Off** - no correction at all: the picture stretches to fill the view, whatever
  shape you have made it, and there are no bars.

In Debug Mode, clicking the corner handle at the bottom-right of the display region
without dragging snaps the region to the true aspect (see **Layout**), so the
correct geometry is one click away in either mode.

### Vendored third-party code

- `stb/stb_ds.h`, `stb/stb_image.h`
  - Upstream: https://github.com/nothings/stb
  - License: public domain or MIT
- `inih/ini.c`, `inih/ini.h`
  - Upstream: https://github.com/benhoyt/inih
  - License: BSD-3-Clause
- `logc/log.c`, `logc/log.h`
  - Upstream: https://github.com/rxi/log.c
  - License: MIT
- `argparse/argparse.c`, `argparse/argparse.h`
  - Upstream: https://github.com/cofyc/argparse
  - License: MIT
- `whereami/whereami.c`, `whereami/whereami.h`
  - Upstream: https://github.com/gpakosz/whereami
  - License: MIT or WTFPL v2
- `tiny-regex-c/re.c`, `tiny-regex-c/re.h`
  - Upstream: https://github.com/kokke/tiny-regex-c
  - License: The Unlicense (public domain)
- Nuklear (`src/frontend/nuklear.h`)
  - Immediate-mode debugger UI
  - License: public domain / MIT (see the header)

## Versions

15 July 2026
:   V3.00_start - Updated all the code to be a closer match to c64m so the same muscle memory works in both.
