# Sample sources  
This folder contains sample for use with the Assembler.  
  
## MMINER  
This is a version of Manic Miner that I wrote for the Apple 2 using cc65's assembler, ca65, converted to my assembly format.  
  
These are the steps:  
* Create a file (mminer-a2m.asm) that defines segments, turns on HGR and includes the root file.
* Turn `.Repeat <count>, <var>` into `.for <var>=0, <var> .lt <count>, <var>++`
* Fix evaluation order differences `row & $08 << 4` vs `(row & $08) << 4`
* Change `.addr` to `.word`
* Make `.string` of data in quotes
* Fix .local in macro by replacing the alias with the actual names
* Change .ifblank in macro to .if .defined (and swap code generation to match)
  
Press `CTRL + SHIFT + F4`.  Browse to `samples/mmminer/mminer-a2m.asm`.  Change the address to `6000`. Press `OK`.  
  
Press `CTRL + F4`.  Pretty much immediately Manic Miner should be running.  
  
You can now open the debugger with `F2` and stop with `F11`, open the source, modify it and press `CTRL-B` (when stopped) or `CTRL+F4` at any time, to instantly see the changes.  You can also un-check the box to auto-run, in which case you could press `CTRL-A` to jump to an address (say 6000).  Then press `CTRL-P` to set the program counter to the address of the cursor.  Now pressing `F5` will run the code from that address.  You can also, with the mouse over the dissasembly window and the debugger stopped, press `CTRL+S` to open the symbols view, and type `main` to show only symbols that match `main`.  Click on the `main` symbol itself and you should be at the place where `main` is compiled to.  Press `CTRL+left cursor` to set the PC to the disassembly cursor (from clicking on `main`).  Press `F5` to run or `F10` to step over or `F11` to step into.
  
29 November 2024  
Updated 24 November 2025
