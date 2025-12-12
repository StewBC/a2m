# Introduction
a2m began as a small experiment after I discovered the Harte 6502 CPU tests. I wanted to see if I could write a cycle-accurate 6502 CPU emulator, just for fun. Once that worked, it became obvious that it wouldn't take much more code to wrap a minimal Apple\ 2 environment around it and run the Manic Miner clone I had written. That led to MMM (The Manic Miner Machine - https://github.com/StewBC/mminer-apple2/tree/master/src/mmm). Things escalated from there, and a2m V1.0 was done by the end of 2024. About a year later I started work on V2.0. Apparently, the emulation hook never really let go of me.

a2m is released under the Unlicense, meaning it is free and unencumbered software placed in the public domain.

# Overview
a2m V2.0 emulates either an NTSC Apple\ ][+ or an Apple\ //e Enhanced. It runs on Windows, Linux, and macOS, with binaries available on the project's GitHub page (https://github.com/StewBC/a2m).

Some of the key features include:

* Fast, cycle-accurate CPU emulation (around 140-170 MHz at approx. 60 FPS on an M2 Mac Mini)
* 60 FPS video display (not cycle-accurate)
* Built-in debugger with stepping modes, symbols, breakpoints, soft-switch overrides, and more
* Built-in macro assembler (assembles 12,000+ lines across 32 Manic Miner source files in tens of milliseconds)
* Disk II NIB read support
* SmartPort block read/write
* SDL Joystick support
* Partial Franklin 80-column card emulation for the Apple\ ][+ model

a2m is designed for people who enjoy developing or exploring Apple\ 2 software.

\Needspace{11\baselineskip}
## Quick Key Reference
In both Normal and Debug Mode, these keys have the listed meanings:

| Key       | Action                    | Key | Action                 |
|:----------|:--------------------------|:----|:-----------------------|
| F1        | Show Help                 | F2  | Toggle Debug Mode View |
| F5        | Run                       | F6  | Run to cursor          |
| F7        |                           | F8  |                        |
| F9        | Toggle Breakpoint         | F10 | Step                   |
| F11       | Break                     | F12 | Monitor Select         |
| SHIFT+F11 | Step Out                  |     |                        |

Debug Mode (`F2`) also reveals the Miscelaneous view and it's here where disks can be inserted, making the Apple\ 2 really useful.

# Running a2m
a2m can be launched from the command line or as a GUI application without a console window.

## Command Line
Running a2m is as simple as executing the program with any desired switches.  
Use `--help` (or `-h`) to view the full command-line reference.

## INI Files
a2m supports configuration through INI files. They are optional but handy for storing common setups (for example, per-game configurations).

Use `--inifile <file>` (or `-i <file>`) to load a specific INI file at launch.  
By default, a2m loads `a2m.ini` from the current directory.

See **INI Files in Depth** for full details.

# User Interface
a2m is primarily a GUI-based Apple\ 2 emulator. It also supports a text-based mode, enabled with `--ui text` (or `-u text`). This mode exists mostly as a demonstration of the cleaner architecture introduced after V1.0.

In text mode, the emulator boots into BASIC and accepts input directly from the terminal. If the Apple\ 2 switches to graphics, you simply won't see it. Typed keys are sent to `$C000` and characters printed by the emulated machine appear in the terminal.  
This manual does not describe text mode further.

# Normal Mode
When launched normally, a2m displays the Apple\ 2's video output in the full application window. It behaves like a ][+ or //e.  
The Apple\ 2 will attempt to boot from:

* **Slot 6 Drive 0** (floppy), or
* **Slot 7 Device 0** (hard-disk image)

These defaults can be overridden in an INI file.

## Keyboard Usage
Regular keys are sent directly to the emulated Apple\ 2. Function keys control the debugger, which is always running "behind the scenes" even when the Apple\ 2 display fills the window.

On the //e model:

* **Open-Apple** - joystick button A or Left-ALT
* **Closed-Apple** - joystick button B or Right-ALT  

To paste text into the Apple\ 2, use **SHIFT+INSERT**.  
**PAUSE** acts as RESET. Some laptops require combinations such as **Fn+B** or **Fn+P**.

**F1** opens a help screen and pauses emulation.  
All other function keys remain debugger controls even while in Normal Mode.

## Resizing
The window can be resized using standard OS controls.  
The Apple\ 2 display is always drawn within the largest 4:3 region that fits inside the window's client area.

# Debug Mode
Debug Mode is where a2m really shines. Every part of the emulated Apple\ 2 can be inspected, and many parts can be modified.

Although Debug Mode is often described as a separate mode, it is always active. All function keys route to the debugger even when only the Apple\ 2 display is visible. Once the debugger views are shown, the full set of tools becomes available.

\Needspace{10\baselineskip}
***Keyboard controls***
The following keys control the debugger regardless of whether the Debug Mode View is open (via F2):

| Key       | Action                    | Key | Action                 |
|:----------|:--------------------------|:----|:-----------------------|
| F1        | Show Help                 | F2  | Toggle Debug Mode View |
| F5        | Run                       | F6  | Run to cursor          |
| F7        |                           | F8  |                        |
| F9        | Toggle Breakpoint         | F10 | Step                   |
| F11       | Break                     | F12 | Monitor Select         |
| SHIFT+F11 | Step Out                  |     |                        |

\Needspace{10\baselineskip}
## Opening the Debugger
Press **F2** to open the Debug Mode View. Press **F2** again to hide it.  The following table represents the layout of the Debugger Views, when opened. 

| Position             | View |
|:---------------------|:----------------------------------------------------------------------------|
|Upper left: | Apple\ 2 display |
|Upper right: | CPU view |
|Right, below CPU: | Disassembly view |
|Lower left: | Memory view (hex + text; any bank can be inspected) |
|Lower right: | Miscellaneous view (slot configuration, disks, soft-switches, breakpoints, etc.) |

## Basic Philosophy
a2m uses the **Nuklear** immediate-mode GUI library. Nuklear uses a *hover-active* model: when the CPU is stopped, whichever view the mouse is over becomes active and receives input. Active views have a green header; inactive ones use grey.

When the emulator is running, all keys except function keys and SHIFT+INSERT go to the Apple\ 2.

In the rest of this manual, "in Debug Mode" is omitted for brevity-assume Debug Mode unless noted.

The debugger layout is resizable:

* a **vertical slider** between the Apple\ 2 and the CPU/Disassembly views
* a **horizontal slider** between the Apple\ 2 and the Memory/Misc views  

Dragging these sliders resizes the layout.  
A "hot spot" in the lower-right corner of the Apple\ 2 view moves both sliders together, scaling the layout proportionally.  
If the Apple\ 2 view is letterboxed, clicking this hot spot snaps it back to a perfect 4:3 region.

## Apple\ 2 View
As in Normal Mode, the Apple\ 2 display is always shown in a 4:3 region inside the largest area available.

### Keyboard controls
When the emulator is running, regular keys go to the Apple\ 2, while function keys and SHIFT+INSERT always go to the debugger.

See **Disassembly View - Keyboard controls** for details on function-key behaviour.

## CPU View
The CPU view shows the program counter (PC), stack pointer (SP), registers, and flags.  
When the emulator is stopped, these can be edited by typing new values:

* PC and SP: 16-bit hex
* Registers: 8-bit hex
* Flags: 0 (off) or 1 (on)

Flags are:

* **N** – Negative
* **V** – oVerflow
* **E** – ignorEd
* **B** – Break
* **D** – Decimal mode
* **I** – Interrupt
* **Z** – Zero
* **C** – Carry

## Disassembly View
The disassembly view shows the code being executed by the CPU. When running or stepping, the current instruction (at the PC) is highlighted. Other highlighted lines include:

* the cursor  
* any addresses with a **stop** breakpoint  
  (breakpoints with non-stop actions do not appear highlighted)

\Needspace{11\baselineskip}
Each line follows this general format:

**`C27D: WAITKEY1      E6 4E       INC RNDL`**

Broken down:

| Element | Meaning |
|:--------|:--------------------------------------------------------------------------|
|C27D     | The hexadecimal address in RAM |
|WAITKEY1 | A label for that address (if present) |
|E6 4E    | The raw bytes at the address at the start of the line |
|INC RNDL | The disassembled instruction (with symbols resolved when available) |

See **Symbols Dialog** for more information.

\Needspace{25\baselineskip}
### Keyboard controls
These keys apply **when emulation is stopped**:

| Key           | Action                                                                  |
|:--------------|:------------------------------------------------------------------------|
| C+a           | Edit the memory address of the cursor line                              |
| C+S+b         | Open the Assembler Configuration Dialog                                 |
| C+b           | Assemble the configured source file                                     |
| C+e           | Show the assembler errors dialog                                        |
| C+p           | Set the PC to the cursor address                                        |
| C+s           | Open the symbol lookup dialog                                           |
| ENTER         | Finish "edit memory address" mode                                       |
| TAB           | Cycle through symbol / branch offset / raw hex display modes            |
| HOME          | Move the cursor to the top of the view                                  |
| C+HOME        | Jump to address $0000                                                   |
| END           | Move the cursor to the last line of the view                            |
| C+END         | Jump to address $FFFF                                                   |
| UP/DOWN       | Move the cursor, scrolling if needed                                    |
| LEFT          | Scroll to show the cursor                                               |
| C+LEFT        | Set PC to cursor and scroll to it                                       |
| RIGHT         | Scroll to show the PC                                                   |
| C+RIGHT       | Set cursor to PC and scroll to it                                       |
| PAGE UP       | Page up by one full view                                                |
| PAGE DOWN     | Page down by one full view                                              |

**C+ and S+ mean CONTROL+ and SHIFT+, respectively.**

\Needspace{9\baselineskip}
### Mouse Controls
At the bottom of the view are **selector buttons** that choose which memory bank to display.  
On the Apple\ ][+ model, the 128 K option is disabled.

| Label  | Action
|:-------|:---------------------------------------------------------------|
|6502    | Shows the CPU's current live memory map |
|64K     | Shows the first 64 K regardless of soft-switch configuration |
|128K    | On the //e, shows the auxiliary bank |
|LC Bank | Toggles between the two language-card banks |

The **scrollbar** on the right scrolls from address `$0000` to `$FFFF`.  
A mouse **scroll wheel** scrolls by 4 lines. Scroll sensitivity can be configured (see **INI Files in Depth - Config**).

Click on any row, outside the address section, to put the cursor on that row.  Click on the address section to set the address of that row (Same as pressing CTRL+a).

\Needspace{11\baselineskip}
## Memory View
The memory view is a way of inspecting larger areas of the Apple\ 2 RAM.  The display shows rows of memory in the format:

**`0000: 54 68 69 73 20 69 73 20 41 53 43 49 49 00 00 00 This is ASCII...`**

Broken down:

| Element    | Meaning |
|:-----------|:---------------------------------------------------------------------|
| 0000       | The hexadecimal address in RAM |
| 54 68 ..   | The bytes starting at the address at the start of the line |
| This is .. | The ASCII representation of the bytes (54 is T, 68 is h, etc.) |

\Needspace{29\baselineskip}
### Keyboard controls
These keys apply **when emulation is stopped**:

| Key           | Action |
|:--------------|:------------------------------------------------------------------------ |
| 0..9, a..f    | In the HEX portion, type hexadecimal characters to edit the memory |
| ASCII         | In the ASCII portion, type ascii letters to set the memory |
| C+a           | Edit the memory address of the cursor line |
| C+f           | Open the find dialogh box |
| C+n           | Search forward for the find string (find next) |
| C+S+n         | Search backward for the find string (find previous) |
| C+s           | Open the symbol lookup dialog |
| C+t           | Switch between Hex and ASCII editing |
| C+v           | Split the view, up to 16 times, into independent sub-views |
| C+j           | Join the sub-view with the its neighbout sub-view |
| A+UP          | Switch to the sub-view above the current sub-view |
| A+DOWN        | Switch to the sub-view below the current sub-view |
| ENTER         | Finish "edit memory address" mode |
| HOME          | Move the cursor to the start of the current line |
| C+HOME        | Move the cursor to the top of the view |
| END           | Move the cursor to the end of the current line |
| C+END         | Move the cursor to the end of the last line of the view |
| UP/DOWN       | Move the cursor up or down, scrolling if needed |
| C+UP/DOWN     | Scroll the view one line up or down |
| LEFT/Right    | Move the cursor left or right, wrapping to prev/next row if needed |
| PAGE UP       | Page up by one full view |
| PAGE DOWN     | Page down by one full view |
| S+INSERT      | Paste from the OS clipboard (HEX into address and HEX, ASCII into ASCII) |

**A+, C+ and S+ mean ALT+, CONTROL+ and SHIFT+, respectively.**

### Mouse Controls
The **scrollbar** on the right scrolls from address `$0000` to `$FFFF`.  
A mouse **scroll wheel** scrolls by 4 lines. Scroll sensitivity can be configured (see **INI Files in Depth - Config**).

Click on any row, outside the address section, to put the cursor on that row.  Click on the address section to set the address of that row (Same as pressing CTRL+a).

## Miscellaneous View

## Dialogs
### File Browser Dialog
### Breakpoint Editor
### Symbols Dialog
### Search Dialog

# Using the Assembler
## Invoking
### From the Emulator
### Command Line Invocation
## Syntax
## Examples
### Simple Examples
### Manic Miner Source Code (Full Example)

# INI Files in Depth
## Machine Section
## Config Section
## Video Section
## DiskII Section
## SmartPort Section
## Debug Section

# Known Issues & Future Work

# Appendix A: Keyboard Shortcuts
# Appendix B: Troubleshooting
# Appendix C: Version History
