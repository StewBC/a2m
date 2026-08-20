# Samples

This folder contains samples for use with the emulator and assembler.

## MMINER

This is a version of Manic Miner that I wrote for the Apple II using cc65's
assembler, ca65, converted to my assembly format.

These are the steps:

* Create a file (`mminer-a2m.asm`) that defines segments, turns on HGR and includes the root file.
* Fix evaluation order differences `row & $08 << 4` vs `(row & $08) << 4`
* Change `.ifblank` in a macro to `.if .defined` (and swap code generation to match)

This is what the `mminer-a2m.asm` file looks like. It defines two targets. The
first behaves differently depending on whether the assembler runs in the
emulator or from the command line. In the emulator, it puts the Apple II into
HGR mode and jumps directly to `main`. From the command line, it generates a
floppy boot loader that loads the game and then jumps to `main`.

The second target sets up the game segments and includes the main assembly file
written for `ca65`.

```
; When using the command line assembler, am65, AM65 == 1 and the file=
; parameter writes the compiled binary to that file.

; This is the ProDOS loader that puts the Apple II in graphics mode and loads the "game"
; When using the Emulator assembler, just ignore all this
.if AM65 .eq 1
    .scope "loader" file ="mminer.system#FF2000"
        .segdef "code", $2000
        .segdef "data", $20B5
        .include "loader.s"
    .endscope
.else
    ; When using the Emulator assembler, AM65 == 0 (it is not undefined)

    .include "apple2.inc"
    CLR80       = $C00C ; apple2.inc has CLR80COL as $C000 - I call that CLR80STORE

    ; Simulate what the loader would do, to the display
    .org $2000
    sta DHIRESOFF
    sta CLR80COL
    sta CLR80       ; This turns off 80 col mode (turns ON 40 col mode)
    bit TXTCLR
    bit MIXCLR
    bit HISCR
    bit HIRES
    jmp game::main  ; Start the game, like the loader would
.endif

; This is the Manic Miner Game.  The loader would load this from Floppy
.scope "game" file="mminer#064000" dest="map"
    .segdef "ZEROPAGE", $50, noemit
    .segdef "LOWMEM", $800, noemit
    .segdef "HGR", $4000
    .segdef "CODE", $6000
    .segdef "RODATA", $89C7
    .segdef "DATA", $BE30

    .include "mminer.asm"
.endscope

; To add both the loader and the game to a floppy image, use something like CiderPress II command line:
; cp2 a disk_image.po mminer.system#FF2000
; cp2 a disk_image.po mminer#064000
; And now the disk_image.po is a bootable floppy disk in ProDOS order that will auto-boot Manic Miner
```

Open **Misc → Assembler**. Browse to `samples/mminer/mminer-a2m.asm`. Check
**Assemble at**, set the address to `2000`, enable **Auto-run at** `2000` if you
want it to start immediately, then press **Assemble**. After the first setup,
**Shift+Opt+A** re-runs the same Assemble action globally.

Pretty much immediately Manic Miner should be running.

You can now open Debug Mode with **F9** and pause with **F10**, edit the
source, and press **Shift+Opt+A** (or **Assemble** again) to instantly see the
changes. You can also un-check **Auto-run at**, in which case you could press
**Opt+A** in the Disassembly view to jump to an address (say `6000`). Then press
**Opt+Left** to set the program counter to the cursor address. Pressing **F12**
will run from that address. With the mouse over the disassembly window and the
debugger paused, press **Opt+S** to open the symbols view, and type `main` to
show only symbols that match `main`. Click the `main` symbol itself and you
should be at the place where `main` is assembled. Press **Opt+Left** to set the
PC to the disassembly cursor (from clicking on `main`). Press **F12** to run,
**F10** to step one instruction, or **F11** to step over a `JSR`.

In the emulator, `dest="map"` pokes the game into RAM; `file="mminer#064000"`
also writes that binary beside the source. Command-line `am65` uses `file=` and
ignores `dest=`.

## golf.ini

This sample shows how breakpoints with fast and slow can be used to make an old
game a lot more fun. The site where I found the golf disks is named in the ini
file. With those files downloaded and correctly named in `golf.ini`, starting
`a2m -i golf.ini` will load World Class Leaderboard in Turbo mode, all the way
to the golfer standing on the tee. After a shot is taken, the game is run in
Turbo mode again, meaning the drawing of the course takes virtually no time,
versus being pretty slow at 1 MHz. So setting up an appropriate INI file can
automate data entry and make an old game a lot more fun.

## hostfs

Using `a2m --noini --smart s7d0=./samples/hostfs` mounts the `hostfs` folder as
a SmartPort ProDOS volume of about 32 MB. Host files can be updated externally
(for example the assembler can write `file=` output there) and the volume
refreshes. You can also copy files in and out without using container images
such as `.po` or `.2mg`. For a host file to appear in the volume it needs a NAPS
name: a valid ProDOS name plus `#TTAAAA`, where `TT` is the two-digit hex file
type and `AAAA` is the four-digit hex aux type. Host subdirectories appear as
ProDOS folders (plain names; no NAPS tag required on the folder itself).

The sample volume contains `ProDOS`, `QUIT.SYSTEM`, `BASIC.SYSTEM`, and cc65-Chess
V2.0 under the `cc65Chess` folder.

History:

- 29 November 2024
- Updated 24 Nov 2025
- Updated 4 Feb 2026
- Updated 5 Apr 2026
- Updated 19 Aug 2026
