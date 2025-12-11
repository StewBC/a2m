# a2m User Manual

## Introduction
a2m began as a small experiment after discovering the Harte 6502 CPU tests. The idea was simple: see if a 6502 CPU emulator could be written for fun. Once that worked, it became obvious that it wouldn't take much more code to wrap a minimal Apple 2 environment around it and run the Manic Miner clone written earlier. That thought led to MMM (The Manic Miner Machine - https://github.com/StewBC/mminer-apple2/tree/master/src/mmm). Things escalated from there, and a2m V1.0 arrived at the end of 2024. About a year later, work on V2.0 began. Apparently, the hook never really let go.

a2m is released under the Unlicense, meaning it is free and unencumbered software placed in the public domain.

## Overview
a2m V2.0 emulates either an NTSC Apple ][+ or an Apple //e Enhanced. It runs on Windows, Linux, and macOS, with binaries available on the project's GitHub page (https://github.com/StewBC/a2m).

Some of the key features include:

* Fast, cycle-accurate CPU emulation (120 MHz at 60 FPS on an M2 Mac Mini)
* 60 FPS video display (not cycle-accurate)
* Built-in debugger with stepping modes, symbols, breakpoints, soft-switch overrides, and more
* Built-in macro assembler (assembles 12,000+ lines across 32 Manic Miner source files in under 20 ms)
* Disk II NIB read support
* SmartPort block read/write
* Joystick support
* Partial Franklin 80-column card emulation for the ][+ model

a2m was made for people who enjoy developing or exploring Apple 2 software.

## Running a2m
a2m can be launched from the command line, or as a GUI application without a console.

### Command Line
Running a2m is as simple as executing the program with any desired command-line switches. Use the `--help` switch (or simply `-h`) to view the full command-line reference.

### INI Files
a2m supports configuration through INI files. They are optional, but very useful for storing common setups (for example, per-game configurations).
Use `--inifile` (or `-i`) to choose a specific INI file at launch.
By default, a2m loads `a2m.ini` from the current directory.
See **INI Files in Depth** for more information.

## User Interface
a2m is primarily a GUI-based Apple 2 emulator. It can also run in a text-based mode using `--ui text` (or `-u text`). This mode exists mostly as a demonstration of the cleaner architecture introduced after V1.0.

In text mode, the emulator boots into BASIC, and commands can be typed directly into the terminal. If the Apple 2 switches to graphics, you simply won't see it. Keyboard input feeds into `$C000`, and characters written to `cout` appear in the terminal. This is the entirety of text mode, and nothing further in this manual describes it.

### Normal Mode
When launched normally, a2m displays the Apple 2's video output in the full application window. It behaves like a ][+ or //e. The Apple 2 will attempt to boot from Slot 6 Drive 0 (floppy) or Slot 7 Device 0 (hard-disk image). These defaults can be overridden through an INI file.

#### Keyboard Usage
Regular keys are sent directly to the emulated Apple 2. Function keys control the debugger, which is always running behind the scenes even when the Apple 2 display occupies the full window.

On the //e model, Open-Apple and Closed-Apple map to joystick buttons A/B and to Left ALT (Open Apple) and Right ALT (Closed Apple).

Pasting into the Apple 2 is done with **SHIFT+INSERT**.
**PAUSE** acts as RESET. On laptops this may require a key combination such as **Fn+B** or **Fn+P**.

**F1** opens a help screen (and pauses emulation). This help applies mostly to the debugger and assembler, which are described in their own sections. All other function keys behave as debugger controls, even while in Normal Mode.

#### Resizing
The window can be resized using standard OS controls. The Apple 2 display is always drawn within the largest 4:3 region that fits inside the window's client area.

### Debug Mode
Debug mode is where a2m shows its real strength. Every part of the emulated Apple 2 can be inspected, and many parts can be modified. Although Debug Mode is often described as a separate mode, it is actually always active. All function keys are routed to the debugger even when only the Apple 2 display is visible. Once the debugger views are shown, the full set of tools becomes available.

#### Opening the Debugger (including layout and resizing views)
Press **F2** to enter Debug Mode. Press **F2** again to leave it.

The default layout is:

* **Upper left:** Apple 2 display
* **Upper right:** CPU view (program counter, registers, flags, etc.)
* **Right, below CPU:** Disassembly view (shows the executing instruction or any RAM/ROM bank)
* **Lower left:** Memory view (hex and text representation; any bank can be examined)
* **Lower right:** Miscellaneous view (slot configuration, loaded disks, soft-switches, breakpoints, and more)

#### Basic Philosophy
a2m uses the **Nuklear** immediate-mode GUI library. Nuklear uses a "hover-active" model: when the CPU is stopped, whichever view the mouse is over becomes active and receives input. The active view is easily identified by its green header; inactive views have grey headers.

When the emulator is running, all keys (except function keys and SHIFT+INSERT) go straight to the Apple 2.

Throughout the rest of this manual, references to "in Debug Mode" will be omitted. Unless noted otherwise, everything described here applies to Debug Mode.

The debugger views can be resized. There are two sliders:

* a **vertical** slider between the Apple 2 display and the CPU/Disassembly views
* a **horizontal** slider below the Apple 2 display and above the Memory/Miscellaneous views

Left-click in these regions to drag and resize.
There is also a "hot spot" in the lower-right corner of the Apple 2 display; dragging here does move both sliders at once, scaling the layout proportionally.
If the Apple 2 view is letterboxed (not 4:3), clicking this hot spot also snaps the layout back to a perfect 4:3 Apple 2 display region.

#### Apple 2 View
As in Normal Mode, the Apple 2 display is always shown in a 4:3 aspect ratio inside the largest area that fits the Apple 2 view. When the emulator is running, regular keys go to the Apple 2, while function keys and SHIFT+INSERT go to the debugger.

#### CPU View
#### Disassembly View
#### Memory View
#### Miscellaneous View

#### Dialogs
##### File Browser Dialog
##### Breakpoint Editor
##### Symbols Dialog
##### Search Dialog

### Using the Assembler
#### Invoking
##### From the Emulator
##### Command Line Invocation
#### Syntax
#### Examples
##### Simple Examples
##### Manic Miner Source Code (Full Example)

### INI Files in Depth
#### Machine Section
#### Config Section
#### Video Section
#### DiskII Section
#### SmartPort Section
#### Debug Section

### Known Issues & Future Work

## Appendix A: Keyboard Shortcuts
## Appendix B: Troubleshooting
## Appendix C: Version History
