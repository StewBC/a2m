# Samples

This folder contains samples for use with the emulator and assembler.

## Assembler MLI launch

`asm_mli_launch/` is a small ProDOS shim for Misc → Assembler **MLI launch**:
SET_PREFIX a HostFS volume (`HOSTFS.SsDn`), OPEN/READ/CLOSE a BIN, then JMP.
Requires live ProDOS; see that folder's README.

## MMINER

This is a version of Manic Miner that I wrote for the Apple II using cc65's
assembler, ca65, and then converted to my assembly format.

These are the steps:

* Create a file (`mminer-a2m.asm`) that defines segments, turns on HGR and includes the root file.
* Fix evaluation order differences `row & $08 << 4` vs `(row & $08) << 4`
* Change `.ifblank` in a macro to `.if .defined` (and swap code generation to match)

The output differs depending on whether the assembler runs in the emulator or from
the command line. In the emulator, it assembles directly to RAM. From the command
line, it generates a floppy boot loader and the game, to files. To add both the
loader and the game to a floppy image, use something like CiderPress II command
line.  This assumes there is a floppy disk image called `disk_image.po`, which
already contains `PRODOS`.

```
cp2 a disk_image.po mminer.system#FF2000
cp2 a disk_image.po mminer#064000
```

`disk_image.po` is now a bootable floppy disk in ProDOS order that will auto-boot
Manic Miner.

Open **Misc → Assembler**. Browse to `samples/mminer/mminer-a2m.asm`. Enable
**Auto-run at** `6000` if you want it to start immediately, then press
**Assemble**. After the first setup, **Shift+Opt+A** re-runs the same Assemble
action globally.

Pretty much immediately Manic Miner should be running.

You can now open Debug Mode with **F9** and pause with **F10**, edit the
source, and press **Shift+Opt+A** (or **Assemble** again) to instantly see the
changes. You can also uncheck **Auto-run at**, in which case you could press
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
game a lot more fun. The site where I found the golf disks is named in the INI
file. With those files downloaded and correctly named in `golf.ini`, starting
`a2m -i golf.ini` will load World Class Leaderboard in Turbo mode, all the way
to the golfer standing on the tee. After a shot is taken, the game is run in
Turbo mode again, meaning the drawing of the course takes virtually no time,
versus being pretty slow at 1 MHz. So setting up an appropriate INI file can
automate data entry and make an old game a lot more fun.

## hostfs

Using `a2m --smart s7d0=./samples/hostfs` mounts the `hostfs` folder as
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
- Updated 20 Aug 2026
