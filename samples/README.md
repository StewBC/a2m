# Sample sources  
This folder contains sample for use with the Assembler.  
  
## descope.py
This is the python script that I used to de-scope code that was written for ca65 and relies on scoping to avoind variable clashes.  

## MMINER  
This is a version of Manic Miner that I wrote for the Apple 2 using cc65's assembler, ca65, converted to my assembly format.  I used a python script to "de-scope" variables and labels that would have collisions and then I manually edited the files.  
  
It was actually not such a big effort after I used the python.  From memory:  
* De-scope names (python)
* Replace scoped access (`a::b`) with direct access
* Turn `proc <label>` into `label:`
* Remove all `.endproc`
* Change `:=` to `=`
* Turn `.Repeat <count>, <var>` into `.for <var>=0, <var> .lt <count>, <var>++`
* Fix evaluation order differences `row & $08 << 4` vs `(row & $08) << 4`
* Remove all `.segment` calls
* Change `.addr` to `.word`
* Make `.string` of data in quotes
* Replace `.res` with some other mechanism
* Moved all macros to a macros file
* Moved variables and zero-page defines around (and into new files where needed)
* Move include files around so I still get main at $6000
* Fix local issue (freq in game, for example, remove .local in macros)
  
That might have more or less been it!  
  
Press `F2` and `F11` (Debugger visible and stopped).  Put the mouse over the disassembly window and press `CTRL + SHIFT + B`.  Browse to `samples/mmminer/mminer.asm`.  Change the address to `6000`. Press `OK`.  
  
Press `CTRL + B`.  Pretty much immediately Manic Miner should be running.  On my system, the compile of the Manic Miner sources, all 12555 lines, took only 13 milliseconds.  That included the time to update the labels in the emulator and shut the assembler down again.  Pretty quick! `:)`  
  
You can now stop with `F11`, open the source, modify it and press `CTRL-B` back in the debugger to instantly see the changes.  You can also un-check the box to auto-run, in which cae you could press `CTRL-A` to jump to an address (say 6000).  Then press `CTRL-P` to set the program counter to the address of the cursor.  Now pressing `F5` will run the code from that address.  
  
29 November 2024  
Updated 24 November 2025
