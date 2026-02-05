# Sample sources  
This folder contains sample for use with the Assembler.  
  
## MMINER  
This is a version of Manic Miner that I wrote for the Apple 2 using cc65's assembler, ca65, converted to my assembly format.  
  
These are the steps:  
* Create a file (`mminer-a2m.asm`) that defines segments, turns on HGR and includes the root file.
* Fix evaluation order differences `row & $08 << 4` vs `(row & $08) << 4`
* Change .ifblank in macro to .if .defined (and swap code generation to match)

This is what the `mminer-a2m.asm` file looks like.  It defines the segments and puts the Apple II into HGR mode - the laoder would have done that in the ca65 version.  It then includes the main assembly file that was made for ca65.  
```
.segdef "ZEROPAGE", $50, noemit
.segdef "LOWMEM", $800, noemit
.segdef "HGR", $4000
.segdef "CODE", $6000
.segdef "RODATA", $89DC
.segdef "DATA", $BE40

SETAN3      = $C05F
CLR80STORE  = $C000
CLR80       = $C00C ; apple2.inc has CLR80COL as $C000 - I call that CLR80STORE

.segment "CODE"
    sta SETAN3
    sta CLR80STORE
    sta CLR80	; This turns off 80 col mode
    bit TXTCLR
    bit MIXCLR
    bit HISCR
    bit HIRES
; .proc main follows so this falls through intentionally
    
.include "mminer.asm"
```
Press `CTRL + SHIFT + F4`.  Browse to `samples/mmminer/mminer-a2m.asm`.  Change the address to `6000`. Press `OK`.  
  
Press `CTRL + F4`.  Pretty much immediately Manic Miner should be running.  
  
You can now open the debugger with `F2` and stop with `F11`, open the source, modify it and press `CTRL-B` (when stopped) or `CTRL+F4` at any time, to instantly see the changes.  You can also un-check the box to auto-run, in which case you could press `CTRL-A` to jump to an address (say 6000).  Then press `CTRL-P` to set the program counter to the address of the cursor.  Now pressing `F5` will run the code from that address.  You can also, with the mouse over the dissasembly window and the debugger stopped, press `CTRL+S` to open the symbols view, and type `main` to show only symbols that match `main`.  Click on the `main` symbol itself and you should be at the place where `main` is compiled to.  Press `CTRL+left cursor` to set the PC to the disassembly cursor (from clicking on `main`).  Press `F5` to run or `F10` to step over or `F11` to step into.
  
29 November 2024  
Updated 24 Nov 2025
Updated  4 Feb 2026
