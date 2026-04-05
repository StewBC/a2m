# Sample sources  
This folder contains sample for use with the Assembler.  
  
## MMINER  
This is a version of Manic Miner that I wrote for the Apple 2 using cc65's assembler, ca65, converted to my assembly format.  
  
These are the steps:  
* Create a file (`mminer-a2m.asm`) that defines segments, turns on HGR and includes the root file.
* Fix evaluation order differences `row & $08 << 4` vs `(row & $08) << 4`
* Change .ifblank in macro to .if .defined (and swap code generation to match)

This is what the `mminer-a2m.asm` file looks like. It defines two targets. The first behaves differently depending on whether the assembler runs in the emulator or from the command line. In the emulator, it puts the Apple II into HGR mode and jumps directly to `main`. From the command line, it generates a floppy boot loader that loads the game and then jumps to `main`.

The second target sets up the game segments and includes the main assembly file written for `ca65`.
```
; When using the command line assembler, asm6502, _asm6502_tool == 1 and the file= 
; parameter writes the compiled binary to that file.

; This is the ProDOS loader that puts the Apple2 in graphics mode and loads the "game"
; When using the Emulator assembler, just ignore all this
.if _asm6502_tool .eq 1
    .scope "loader" file ="mminer.system#FF2000"
        .segdef "code", $2000
        .segdef "data", $20B5
        .include "loader.s"
    .endscope
.else
    ; When using the Emulator assembler, _asm6502_tool == 0 (it is not undefined)

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
.scope "game" file="mminer#064000" dest="6502"
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
Press `CTRL + SHIFT + F4`.  Browse to `samples/mmminer/mminer-a2m.asm`.  Change the address to `2000`. Press `OK`.  
  
Press `CTRL + F4`.  Pretty much immediately Manic Miner should be running.  
  
You can now open the debugger with `F2` and stop with `F11`, open the source, modify it and press `CTRL-B` (when stopped) or `CTRL+F4` at any time, to instantly see the changes.  You can also un-check the box to auto-run, in which case you could press `CTRL-A` to jump to an address (say 6000).  Then press `CTRL-P` to set the program counter to the address of the cursor.  Now pressing `F5` will run the code from that address.  You can also, with the mouse over the dissasembly window and the debugger stopped, press `CTRL+S` to open the symbols view, and type `main` to show only symbols that match `main`.  Click on the `main` symbol itself and you should be at the place where `main` is compiled to.  Press `CTRL+left cursor` to set the PC to the disassembly cursor (from clicking on `main`).  Press `F5` to run or `F10` to step over or `F11` to step into.
  
29 November 2024  
Updated 24 Nov 2025
Updated  4 Feb 2026
Updated  5 Apr 2026
