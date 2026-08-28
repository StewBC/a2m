# a2m — RETIRED (development moved to machines)

**This repository is frozen.** a2m development continues in the `machines`
monorepo (a2m + c64m share a debugger shell, not silicon).

- Last frozen SHA: `d863ad98487639445db16134d260221839cb72e9`
- Tag: `frozen-for-machines-d863ad98487639445db16134d260221839cb72e9`
- Do not open feature PRs here. Do not push further work here.

---

# a2m — Apple ][+ and //e Enhanced emulator

a2m is an Apple II emulator written in C99. It is aimed at people who enjoy
developing or exploring Apple II software.

- Fast, cycle-accurate CPU emulation (6502 / 65C02)
- 60 FPS video display
- Built-in debugger: stepping, symbols, breakpoints, watchpoints, soft-switch overrides
- Built-in two-pass 6502 / 65C02 assembler
- Disk II: NIB and DSK read/write; WOZ read
- SmartPort / ProDOS block devices (image files, or a host folder as a ~32 MB volume)
- Mockingboard
- SDL joystick / gameport
- Machine snapshots (`.a2state`)
- Tested on Windows, Linux, and macOS (requires SDL2)

System ROMs are embedded. No separate ROM files are required.

## Introduction

a2m began as a small experiment after I discovered the Harte 6502 CPU tests. I wanted
to see if I could write a cycle-accurate 6502 CPU emulator, just for fun. Once that
worked, it became obvious that it wouldn't take much more code to wrap a minimal
Apple II environment around it and run the Manic Miner clone I had written. That led
to MMM, [The Manic Miner Machine](https://github.com/StewBC/mminer-apple2/tree/master/src/mmm).
Things escalated from there, and a2m V1.0 was done by the end of 2024. About a year
later I started work on V2.0. Apparently the emulation hook never really let go of me.

This tree is **V3**: the Apple II machine with a rewritten debugger/product shell.
a2m now matches the layout and many of the keys of
[c64m](https://github.com/StewBC/c64m) so that the same muscle-memory works in
both emulators. V3 was created with the help of AI.

Earlier V1–V2 notes live in [`doc/a2m-v1-2/`](doc/a2m-v1-2/README-v1-2.md).

## What it does

a2m boots a real Apple ][+ or //e Enhanced ROM and runs a broad range of software:
BASIC programs, binaries, and games and demos loaded from disk images. It can save
and restore full machine snapshots to `.a2state` files.

The built-in debugger gives you a live disassembler, a hex memory editor, a full
breakpoint system with read/write/execute watchpoints, and a call-stack view. Both
the disassembly and memory views can be switched independently between the
CPU-mapped address space, main RAM, auxiliary RAM, the language-card banks, or the
physical ROM bytes — so you can inspect what the CPU sees, what is underneath it,
or what is in the ROM regardless of which is currently banked in.

The integrated two-pass 6502 / 65C02 assembler lets you assemble and run code
without leaving the emulator. Assembled labels feed straight into the
disassembler's symbol table.

Loading and saving host files is done from the Machine tab: binaries at any
address (raw, AppleSingle, NAPS `#TTAAAA` for BIN `$06` / SYS `$FF`, or legacy
DOS), optionally with a reset before load and optional run at the load
address; Applesoft listings as ASCII (tokenized by the emulator on load,
detokenized on save); and `.a2state` snapshots.

The manual is here: [a2m Manual](./manual/manual.md).  
There is a details section with more [technical details](./manual/manual.md#Details).

## Quick Start

```bash
# macOS
brew install cmake sdl2
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/a2m
```

Launch the application. The Apple display fills the window and the emulated
machine boots — you should see the familiar Apple startup screen.

Press **Opt+H** (macOS) or **Alt+H** (Windows/Linux) at any time to open the
built-in manual. Keyboard shortcuts, debugger controls, assembler syntax, and
the INI format are documented there.

Press **F9** to toggle the debugger layout.

Disks are added in the Machine tab of the **Misc** view (bottom right) in the
debugger layout. Drag-and-drop of disk images and `.a2state` files onto the
window also works.

## License

a2m is released under the Unlicense (public domain), except for the third-party
code under `external/`, which keeps its own licenses. See [LICENSE](LICENSE).

## Contact

Stefan Wessels  
swessels@email.com
