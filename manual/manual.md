
# Introduction

Welcome to the **a2m** manual. This document explains installation, usage, and advanced options.

# Prerequisites
The emulator requires SDL2 but all support files are included in the release package.

# Quick Start

1. Download the latest release appropriate for your operating system.
2. Run the emulator by running apple2 (apple2.exe on Windows).
3. Press F2 to open the debug display.
4. In the Miscellaneous window select a disk slot (floppy is Slot 6, Smartport is 5 or 7).
5. Click Insert to insert a disk image (floppy .nib file, or .po/.2mg SmartPort image file).
6. Click the 5.0, 6.0 or 7.0 button that corresponds to the image inserted, to boot that image.
7. Press F2 again to make the Apple II display area fill the window.

# Installation

a2m was tested under Windows | macOS | Linux.  There is no installation, you download a release image, extract the files to somewhere convenient, and run apple2.

# Customization

Running apple2 in a folder without an apple2.ini file will write a new apple2.ini file into that folder.  This apple2.ini file is used to configure the emulator when you simply launch apple2.  This is the commented file that is created if no apple2.ini file is present, and below that the sections are explained:
```
[Machine]  
Model = enh ; plus | enh (//e Enhanced)  
Turbo = 1, 8, 16, max ; Float.  F3 cycles - max is flat out  
  
[Display]  
scale = 1.0 ; Uniformly scale Application Window  
disk_leds = 1 ; Show disk activity LEDs in lower right corner of the window  
  
[Video]  
;slot = 3 ; Slot where an ][+ (Model = plus) 80 col Video card can be inserted  
;device = Franklin Ace Display ; 80 Column Videx like card for Apple ][+  
  
[DiskII]  
slot = 6 ; This says a slot contains a Disk II controller  
disk0 =  ; file name of a NIB floppy image, NOT in quotes  
disk1 =  ; ./disks/Apple DOS 3.3 January 1983.nib ; example usage  
  
[SmartPort]  
slot = 5 ; This says a slot contains a SmartPort  
disk0 =  ; file name of an image, NOT in quotes  
disk1 =  
boot = 0 ; any value other than 0 will cause a boot of slot 5, disk0  
slot = 7 ; There can be multiple slots that contain a SmartPort device  
disk0 =  
disk1 =  
boot = 0 ; last listed non-zero boot devices' disk0 will boot when apple2 is run  
  
[Debug]  
;break = <address[-address]>[,pc|read|write|access[,restore | fast | slow][, count[, reset]]]  
; | mean or and parameters between [ and ] are optional
;Defaults for options are = are: pc, restore, 0, 0  
;Addresses are in Hex, Octal or Decimal. If lo-high addresses are used, that range is covered
;So break = 0x8000 will break when executing an opcode at $8000; and  
;break = 0xC000,access will break on a read or write of location $C000; and  
;break = 0x8577,fast will put the emulator in fast mode (and not break) when executing an opcode at $8577  
```
## Machine Section explained

This section chooses:  
1a) if the emulated machine is an Apple ][+ using the line:  
Model = plus  
1b) an enhanced //e, using the line:  
Model = enh  

2) The Turbo = line sets a multiplier over 1 MHz.  Pressing the F3 key will switch between the modes on the line. In the example, the emulator will start running at 1 MHz and when F3 is pressed, it will, with each press, switch to 8, 16 and then the maximum it can achieve on your computer while trying to keep the framerate at 60 FPS.  After that it will cycle back to 1.  If you specify a multiplier that your computer cannot achieve at 60 FPS, the machine will likely run faster than the 'max' MHz, but the framerate will drop below 60 FPS.

## Using the Emulator
When the emulator starts, it opens as a 1120 x 840 window displaying the Apple 2 text screen. The resolution can be scaled using the apple2.ini file. If no SmartPort disk is booted, the emulator defaults to Applesoft BASIC for a ][+ and waits for a floppy disk to be inserted on a //e. Remember to activate CAPS LOCK, as cursor controls require uppercase letters on the ][+.

The function keys control the emulator (or debugger), while other keys go to the Apple 2 machine when it’s running. The function keys and their functions are as follows:
```
F1 - Help  
F2 - Toggle debugger display ON/OFF  
F3 - Toggle Speed Limiter (1 MHz vs as fast as possible)  
When stopped:  
F5 - Run (exit stopped mode)  
F6 - Run to cursor  
F9 - Toggle breakpoint at PC  
F10 - Step (over)  
F11 - Step (into)  
F11 + SHIFT - Step (out)  
F12 - Toggle between Color / Mono and 40/80 Colum display based on screen mode  
F12 + SHIFT - Force toggle Color / Mono & 40 / 80 Column text display  
```

## The window layout when debugger is visible
Opening the debugger (F2) displays the following windows:
```
CPU: Shows the Program Counter (PC), Stack Pointer (SP), registers, and flags.  
Disassembly: Displays the currently executing program stream.  
Memory: Shows a page of RAM in HEX and ASCII.  
Miscellaneous:  
  SmartPort devices  
  Debugger status and breakpoints  
  Display status (text, low-res, or HGR and active page)  
  Language Card soft-switches  
```

## Using the Debugger
When running, debug windows update at 60 FPS, but user control only applies to the Apple ][+ machine. Once stopped, you can interact with the debug windows. The window title changes to green when it is "active", with "active" determined by mouse hover and input going to the "active" window.  The machine reset is mapped to SCRLK or PAUSE (so CTRL +) and on my Lenovo laptop that's FN + B.

## Using the CPU Window
The CPU window, when stopped, has editable boxes for the PC, SP, registers, and flags. To edit a value, click in the box, keep the mouse pointer over the CPU window, type a new value, and press ENTER.

## Using the Disassembly Window
The following keys are supported in the disassembly window:
```
CTRL + SHIFT + A - Configure an assembler source file for use with the built-in Assembler  
CTRL + A - Assemble the configured assembler source file (No error message, no errors)  
CTRL + E - Re-show the errors output from the Assemble process  
CTRL + G - Set the address for viewing disassembly (cursor PC)  
CTRL + P - Set the machine PC to the cursor PC  
CTRL + S - Serach the symbols  
TAB - Toggle symbol display between all/functions and labels/labels/none  
CURSOR UP/DOWN - Move the cursor PC up/down an address  
PAGE UP/DOWN - (Try to) Move the cursor PC up/down a page  
```

## Using the Memory Window
The address of the cursor is shown in the last line of the window. Memory can be edited at that address.
```
CTRL + F - Find dialog (strings or HEX)  
CTRL + G - Go to address  
CTRL + J - Join a split window with the one below  
CTRL + SHIFT + J - Join a split window with the one above  
CTRL + N - Find next (forward) using the CTRL + F search  
CTRL + SHIFT + N - Find previous (backward) using the CTRL + F search  
CTRL + S - Search the symbols  
CTRL + T - Toggle between HEX and ASCII editing  
CTRL + V - Split the memory View (up to 16 virtual memory views)  
ALT + 0-F - Select a virtual memory window  
```
Split views are differentiated by colored with a virtual window ID (e.g., 0:0000). This enables viewing up to 16 distinct memory regions from the one Memory Window.
Type HEX digits to edit the memory in HEX mode and use any key to edit the memory in ASCII mode (switch using CTRL + T).

## Using the Miscellaneous Window
This window currently has 4 sections.  Each section can be shown/hidden by clicking on the triangle before the section name.  NOTE:  These sections only respond to input when the emulator is stopped.
### SmartPort
This shows which slot has a SmartPort interface.  In each slot, slot 0 display (example `7.0`) is a button.  Pressing that button will boot disk0 (if mounted).  The `Eject` button "removes" the disk from the slot and the `Insert` button brings up a file browser through which a disk can be mounted.  Note: Selecting a file that is not a disk image will work as _no_ validation is done.  The only check is if a file has `2IMG` in the header, the offset for the start in the file is set to 64 bytes from the start of the file (ignoring/skipping the header).
This section is populated based on the apple2.ini file.  Making changes in the UI will _not_ update the apple2.ini file.  The apple2.ini file needs to be edited by hand.
### Disk II
This shows which slot has a Disk II controller.  The usage/interface is the same as for the SMartPort.
### Debugger
The status display shows:
`Run to PC O XXXX`.  While the emulator is "running to the address XXXX" the `O` is lit.  It is good to know that stepping over a `JSR` for example, is in progress.  `Step Out O` similarly is active when using SHIFT-F11 and the function doesn't "step out" quickly.
The `Step Cycles` show the number of cycles that elapsed from the emulator being stopped, till stopped again. So, if the emulator just stepped over a 2-cycle opcode using F11, that will show 2.  This is very handy for profiling since stepping over a `JSR` subroutine shows the all-inclusive number of cycles it took to run the routine.
`Total cycles` show the number of cycles executed since the start.  This number will roll over fairly quickly (minutes, not days of running the emulator).
The `Call Stack` has the address where the JSR was done, and where the destination address of the JSR. Click on either column to put the cursor at that address.  It is possible that a PHA will happen to result in a false positive (address at (*(SP+1)-2) == 0x20) but a PLA should restore the call stack.
Setting a breakpoint (Using `F9`) will add additional rows:
`XXXX` `Disable` `View PC` `Clear` and if multiple breakpoints are set `Clear All`.  These are:
`XXXX` This is the address to break on, or watch, and optionally on what type of access or on which count/pass to break.
`Disable` will disable the breakpoint and the button will read `Enable` to re-enable the breakpoint.
`View PC` will set the cursor PC to the address of the breakpoint, if it is a PC breakpoint.
`Clear` will erase the breakpoint.
`Clear All` will erase all breakpoints.
Breakpoints come in two types. The first type is a _PC_ breakpoint, which triggers when the program counter (PC) matches the breakpoint address. The second type is an _ACCESS_ breakpoint, which triggers when any access matches the breakpoint address. For access breakpoints, a range of addresses can be specified, along with the type of access — Read, Write, or either. Both types of breakpoints also support optional count settings: the first count specifies how many times the breakpoint should trigger before a stop is activated. Once this count is reached, a second count determines how frequently the breakpoint will trigger thereafter. This setup allows you to specify, "When address XXXX has been read or written N times, break, and after that, break every K times the address is accessed."
### Display
Display mode shows which of the `Text`, `Lowres` or `HGR` display modes is active.  `Mixed Mode` off indicates that the whole screen (in lores or HGR) is in that graphics mode, and `ON` shows that the last 4 "lines" of the display is shown in `Text` mode.  `Display page` indicates where from memory the display is showing what it is showing:
```
Text: Page 0 = $0400, Page 1 = $0800  
Lowes: Same as Text  
HGR: Page 0 = $2000, Page 1 - $4000  
```
`Override Yes` allows changing the values above to see non-active modes and pages.  This is especially useful when looking at how a program draws off-screen if it does page-flipping.
`Override No` restores the hardware view of the display.
These settings can only be altered when stopped, but stepping or running with `Override On` will keep the override settings.
### Language Card
The Language Card section shows the state of the Language Card, whether ROM or RAM is active, which bank, and if writing to RAM is enabled.  The Read ROM / RAM toggle can be set but the rest are read-only.

## 6502 Assembler
I added a 6502 assembler to the project.  The assembler is integrated into the emulator so assembled code just shows up in memory.  I made a stand-alone version of the assembler which shares the code with the emulator version - it's the same assembler but with command line execution to make it an otherwise useful tool, outside of the emulator.
```
Usage: asm6502.exe <-i infile> [-o outfile] [-s symbolfile] [-v]  
       infile is a 6502 assembly language file  
       outfile will be a binary file containing the assembled 6502  
       symbolfile contains a list of the addresses of all the named variables and labels  
       -v turns on verbose and will dump the hex 6502 as it was assembled  
```

### Assembler Features
The assembler supports these features:
Feature | Description
--- | ---
6502 Mnemonics | All standard opcodes and modes
labels | labels start with `[a-z\|_]` and can contain that and `numbers`, and end with a `:`
variables | Values can be assigned and used in [expressions](#assembler-expressions)
.commands | [dot commands](#assembler-dot-commands) are described below
comments | The comment character is `;` and everything after `;` on a line is ignored
address | The address character is `*` and it can be assigned and read
expressions | The assembler has a nice [expression](#assembler-expressions) parser

#### Assembler DOT Commands
In the table below, value can be any legal [expression](#assembler-expressions):
Command | Description
--- | ---
.align `value`| Aligns data to the next `value` boundary
.byte `value`[, value]* | Outputs `value` as 8 bits
.drow `value`[, value]* | Outputs `value` as 16 bits, hi to lo
.drowd `value`[, value]* | Outputs `value` as 32 bits, hi to lo
.drowq `value`[, value]* | Outputs `value` as 64 bits, hi to lo
.dword `value`[, value]* | Outputs `value` as 32 bits, lo to hi
.endfor | Ends a [for loop](#assembler-for-loops)
.endmacro | Ends a [macro](#assembler-macros)
.for | Starts a [for loop](#assembler-for-loops)
.incbin "file" | Include a binary file, byte for byte, at this point
.include "filename" | Includes the file, filename, at this point
.macro "name" `[...]` | [macro](#assembler-macros)
.org `value`| Another way to set the current address
.qword `value`[, value]* | Outputs `value` as 64 bits, lo to hi
.strcode | Sets a string [character parser](#assembler-strcode)
.string value | `string`[,[value \| `string`]]* \| [Outputs](#assembler-strcode) the values or characters
.word `value`[, value]* | Outputs `value` as 16 bits, lo to hi

#### Assembler Expressions
The assembler has a nice expression parser.  These are valid clauses, operands and operators, here called the tokens, and this also illustrates the order of precedence.
Token | Description
--- | ---
`*` `:` [Num](#assembler-numbers) [variables](#assembler-variables) `(` | Address, anonymous labels, variables and brackets
`+` `-` `<` `>` `~` | Unary plus, minus, lo byte, hi byte and not
`**` | Exponentiation (Raise to the power of)
`*` `/` `%` | Multiply, divide and modulus
`+` `-` | Additive (plus and minus)
`<<` `>>` | Shift left and shift right
relational | `.lt .le .gt .ge` for `<, <=, >, >=`
equality | `.ne .eq` for `!=, ==`
`&` | Bitwise and
`^` | Exclusive or
`\|` | Bitwise or
`&&` `\|\|` | Logical `and` and `or`
`?` `:` | [Ternary Conditional](#assembler-ternary)

#### Assembler Numbers
Numbers can be in these formats:
Prefix | Base
--- | ---
`$` | Hexadecimal
`0` | Octal
`%` | Binary
`1`-`9` | Decimal

Inside strings, numbers can be quoted as well.  In that case, the numbers are:
Quote prefix | Base
--- | ---
`\x[N]+` | Hex
`\0[N]+` | Octal
`\%[1\|0]+` | Binary
`\[0-9]+` | Decimal

#### Assembler Variables
Variables can be followed by assignment `=`, increment (`++`) and decrement (`--`) operators.  Note that, even though this looks like postfix "C" notation, it is actually executed as prefix operator.  See this example:
```
i = 0  
lda #i++  
```
will not load a with 0, but with 1.

NOTE: The address character, `*`, is intentionally returned as +1 from where it is read.  This means:
```
* = $8000  
a = *    ; a will now be $8001  
: b = :- ; but b will be $8000  
```
The reason for this is that in statements like `lda *` the `lda` will not yet have been emitted when `*` is evaluated, so reading `*` adds one.  `a = * - 1` is also valid.

#### Assembler Ternary
Much like "C", the ternary conditional has the form:
`(condition expression)` `?` `(when true condition)` `:` `(when false condition)`
In it's simplest form it works like this:
```
1: i = 1 ? 2 : 3  
 Will assign 2 to i, since 1 is true.  
  
A silly example:  
2: i = j .eq 1 ? 4 : j .eq 2 ? 5 : 6  
 if j == 1, i = 4; else if j == 2, i = 5; else for all other values of j, i = 6  
```
As can be seen in the second example, the conditions can be mixed and any valid expression, no matter how complex, is allowed for each of the 3 clauses (condition ? true : false).

#### Assembler For Loops
The for loop syntax of the assembler is useful for, for example, creating data tables.  The syntax is:
```
.for <initialization>, <condition>, <iteration>  
    ; fill in something here  
.endfor  
```
Here's an example that might better illustrate:
```
rowL:  
    .for row=0, row .lt $C0, row++  
        .byte   (row & $08) << 4 | (row & $C0) >> 1 | (row & $C0) >> 3  
    .endfor  
  
rowH:  
    .for row=0, row .lt $C0, row++  
        .byte   >$2000 | (row & $07) << 2 | (row & $30) >> 4  
    .endfor  
```
FWIW, the `row++` could also have been, for example, `row = row + 1`.  Any valid expression in any clause.  If a loop fails to stop (ie the `<condition>` is never true), the assembler will automatically stop after 64K iterations.
Here are some of the bytes that the for loops above will output.  These are the start line addresses for the rows of the Apple ][ high resolution screen at $2000.
```
rowL:  
0000: 00 00 00 00 00 00 00 00 80 80 80 80 80 80 80 80  
0010: 00 00 00 00 00 00 00 00 80 80 80 80 80 80 80 80  
0020: 00 00 00 00 00 00 00 00 80 80 80 80 80 80 80 80  
0030: 00 00 00 00 00 00 00 00 80 80 80 80 80 80 80 80  
0040: 28 28 28 28 28 28 28 28 A8 A8 A8 A8 A8 A8 A8 A8  
0050: 28 28 28 28 28 28 28 28 A8 A8 A8 A8 A8 A8 A8 A8  
<...>  
  
rowH:  
00C0: 20 24 28 2C 30 34 38 3C 20 24 28 2C 30 34 38 3C  
00D0: 21 25 29 2D 31 35 39 3D 21 25 29 2D 31 35 39 3D  
00E0: 22 26 2A 2E 32 36 3A 3E 22 26 2A 2E 32 36 3A 3E  
00F0: 23 27 2B 2F 33 37 3B 3F 23 27 2B 2F 33 37 3B 3F  
0100: 20 24 28 2C 30 34 38 3C 20 24 28 2C 30 34 38 3C  
0110: 21 25 29 2D 31 35 39 3D 21 25 29 2D 31 35 39 3D  
<...>  
```
Note that in rowH, the hi byte of $2000 is $20 and that is `|`'d with the other expressions.  The order of operations are not such that the `|` happens before the `>`.  If `|` was higher, the output for rowH would have simply been $20 as the lo byte portion that contains the data we are interested in, would have been discarded.  Also note that shift's `<<` and `>>` are higher than bitwise-and `&`, so the brackets are needed around the `&`.  The basic order of operations matches that of the "C" language.

#### Assembler Macros
The macros have the form
```
.macro <name> [arg [, arg]*]  
 <macro body>  
.endmacro  
```
The arguments are variables.  Best illustrated as an example:
```
.macro add_a_b a b  
    clc  
    lda a  
    adc b  
.endmacro  
  
    add_a_b 12, 25 ; This is a call to use the macro  
```
This will emit the code:
```
    clc  
    lda 12  
    adc 15  
```
Note the lack of #.  You also cannot call it with `add_a_b #12, #15` since `#12` is not a valid variable "value".  The `#` would have to go into the code.  This macro system is not all that powerful, but it does help make repetitive tasks a little less tedious.

NOTE (to self mainly): Maybe make variables that can be `immediate` so the parser can say if `lda` is followed by `#` or a `immediate` variable, this is an immediate load. That would make passing `#value` and things like `<value` possible.  Something to consider.

#### Assembler Strcode
`.strcode` can be used to map characters in a string to other values.  It uses the variable `_` to do the mapping.
An example might best illustrate:
```
.strcode _-1  
.string "1234"  
```
This will output `0000: 30 31 32 33` which is "0123".  Where this becomes relevant is when you have to map characters between modes, for example on a Commodore 64.  A printed `A` is 65 but an `A` poked into the screen needs to have a `0x01` value.  Using [Ternary Conditional](#assembler-ternary) expressions, it can help map characters from one range or ranges, to another range or ranges. Think about the example I gave of poking characters on the Commodore 64 needing A through Z as values 1 through 26. Maybe this expression, which maps upper and lower case letters in the assembly file "string" to uppercase values good for poking to the screen on the C64, can be used:
`.strcode _ .ge $61 ? _ - $60 : _ .ge $41 ? _ - $40 : _`
NOTE: `.strcode` assigns the characters in the string, one after the other, to `_` and then evaluates the `.strcode` expression.  To turn off processing, simply use `.strcode _`. If you use just `_` as a variable in your own code, then `.strcode` will overwrite it.

# License
This manual is licensed under **Unlicense**.  You are free to do whatever you want with the source code or binary.
