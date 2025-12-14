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
* SDL joystick support
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

Debug Mode (`F2`) also reveals the Miscellaneous view, and it is here where disks can be inserted, making the Apple\ 2 really useful.

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
a2m is primarily a GUI-based Apple\ 2 emulator. It also supports a text-based mode, enabled with `--ui text` (or `-u text`). This mode exists mostly as a demonstration of the cleaner architecture introduced after v1.0.

In text mode, the emulator boots into BASIC and accepts input directly from the terminal. If the Apple\ 2 switches to graphics, you simply will not see it. Typed keys are sent to `$C000`, and characters printed by the emulated machine appear in the terminal.  
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

## Basic UI Philosophy
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
| C+HOME        | Jump to address `$0000`                                                 |
| END           | Move the cursor to the last line of the view                            |
| C+END         | Jump to address `$FFFF`                                                 |
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
The memory view is a way of inspecting larger areas of the Apple\ 2 RAM. The display shows rows of memory in the format:

**`0000: 54 68 69 73 20 69 73 20 41 53 43 49 49 00 00 00 This is ASCII...`**

Broken down:

| Element    | Meaning |
|:-----------|:---------------------------------------------------------------------|
| 0000       | The hexadecimal address in RAM |
| 54 68 ..   | The bytes starting at the address at the start of the line |
| This is .. | The ASCII representation of the bytes (`54` is `T`, `68` is `h`, etc.) |

\Needspace{29\baselineskip}
### Keyboard controls
These keys apply **when emulation is stopped**:

| Key           | Action |
|:--------------|:------------------------------------------------------------------------|
| 0..9, a..f    | In the HEX portion, type hexadecimal characters to edit the memory |
| ASCII         | In the ASCII portion, type ASCII letters to set the memory |
| C+a           | Edit the memory address of the cursor line |
| C+f           | Open the find dialog box |
| C+n           | Search forward for the find string (find next) |
| C+S+n         | Search backward for the find string (find previous) |
| C+s           | Open the symbol lookup dialog |
| C+t           | Switch between Hex and ASCII editing |
| C+v           | Split the view, up to 16 times, into independent sub-views |
| C+j           | Join the sub-view with its neighbouring sub-view |
| A+UP          | Switch to the sub-view above the current sub-view |
| A+DOWN        | Switch to the sub-view below the current sub-view |
| ENTER         | Finish "edit memory address" mode |
| HOME          | Move the cursor to the start of the current line |
| C+HOME        | Move the cursor to the top of the view |
| END           | Move the cursor to the end of the current line |
| C+END         | Move the cursor to the end of the last line of the view |
| UP/DOWN       | Move the cursor up or down, scrolling if needed |
| C+UP/DOWN     | Scroll the view one line up or down |
| LEFT/RIGHT    | Move the cursor left or right, wrapping to previous/next row if needed |
| PAGE UP       | Page up by one full view |
| PAGE DOWN     | Page down by one full view |
| S+INSERT      | Paste from the OS clipboard (HEX into address and HEX, ASCII into ASCII) |

**A+, C+ and S+ mean ALT+, CONTROL+ and SHIFT+, respectively.**

### Mouse Controls
The **scrollbar** on the right scrolls from address `$0000` to `$FFFF`.  
A mouse **scroll wheel** scrolls by 4 lines. Scroll sensitivity can be configured (see **INI Files in Depth – Config**).

Click on any row, outside the address section, to place the cursor on that row. Click on the address section to set the address of that row (same as pressing CTRL+a).

## Miscellaneous View
The Miscellaneous View consists of sub-views that can be opened and closed at will. Each sub-view has a triangle to the left of its name; clicking the triangle opens or closes the sub-view. The Miscellaneous View also has a scrollbar on the right, making it possible to see all details without closing any sub-views.

\Needspace{6\baselineskip}
The sub-views are:

| View Name     | Contents |
|:--------------|:--------------------------------------------------------------------------|
| Slots         | Devices inside the Apple\ 2 slots, such as Disk II or SmartPort cards |
| Debugger      | Cycle counters, call stack, breakpoints, etc. |
| Soft Switches | Memory locations in the $C000–$C0FF range that affect Apple\ 2 operations |

Each of these is discussed in more detail below.

### Slots Misc View
The Slots view shows the hardware installed in the slots of the Apple\ 2. It is also the panel used for working with disks, both SmartPort and floppy.

\Needspace{7\baselineskip}
With both SmartPort and floppy devices, there are a few buttons exposed. Next to Disk or Device 0, there is a button with the slot number, a dot, and a zero. Next to that is an Eject button, followed by an Insert button. The display looks something like this:

```
Slot 5: Smartport  
[5.0][Eject][Insert]  
Slot 6: Disk II  
[6.0][Eject][Insert]  
```


Clicking Insert opens the file browser, and if an appropriate file is selected (NIB for Disk II and any file for SmartPort), the file name is shown to the right of the Insert button. For a Disk II device, more than one file can be inserted. When this is done, a new button appears, labelled Swap. The button also indicates which disk is currently inserted (from the sequence) and how many disks are in the sequence. This looks like:

`[6.0][Eject][Insert][Swap (1/2)] This is disk 1.po`

The disk name, in this case, is "This is disk 1.po", and the button indicates that it is the first disk of two disks in the queue. Clicking the Swap button switches to the second disk, and the display might then look like this if the second disk is called "This is disk 2.po":

`[6.0][Eject][Insert][Swap (2/2)] This is disk 2.po`

Clicking Eject removes the currently selected disk from the drive and from the queue.

Clicking the button labelled `[Slot.0]` boots the disk in drive/device 0 of that slot.

\Needspace{15\baselineskip}
### Debugger Misc View
By default, the Debugger View shows status information under the heading **Debug Status**. This information is:

| Name           | Description |
|:---------------|:--------------------------------------------------------------------------|
| Run to PC nnnn | The button is selected when stepping over a JSR or using F6, and the       |
|                | destination address is in nnnn. Otherwise, the button is unselected and   |
|                | nnnn contains 0000                                                        |
| Step Out       | The button is selected when F11 is used to step out of the current        |
|                | subroutine                                                                |
| Step Cycles    | The number of cycles between stops of the emulator. For example,          |
|                | stepping over an INC of ZP will show 5 cycles, but stepping over a JSR    |
|                | will show however many cycles the subroutine cost, plus the 6 JSR cycles  |
| Total Cycles   | Shows the total cycles since the emulator started running the             |
|                | Apple\ 2 ROM code                                                         |

\Needspace{11\baselineskip}
There is also a **Call Stack** display. This is a window with its own scrollbar if the entries exceed what the window can show. The call stack entries have the form:

**`E69E JSR FF59 OLDRST`**

Broken down:

| Element | Meaning |
|:--------|:--------------------------------------------------------------------------|
| E69E    | The hexadecimal address where the JSR resides                              |
| JSR     | The instruction that caused the push onto the stack                        |
| FF59    | The destination address of the JSR—the address of the subroutine called   |
| OLDRST  | The symbol name for $FF59, from the loaded symbol files                    |

Note that clicking the address (`E69E` in this case) will set the disassembly view cursor to that address and show it in the disassembly view. The same is true for `FF59`; clicking it, or clicking to the right of the JSR, will place the disassembly cursor on that address and show it in the disassembly view.

\Needspace{22\baselineskip}
#### Breakpoints
The Debugger View has a breakpoints view that is only visible when at least one breakpoint is set. This view lists all configured breakpoints. Each breakpoint in this view has the format:

**`label [Edit][Disable][View PC][Clear]`**

The label describes the breakpoint. These are all possible label values, where nnnn is the address of the breakpoint:

| Label                | Meaning                                                                 |
|:---------------------|:------------------------------------------------------------------------|
| nnnn (counters)      | Uses a counter                                                          |
| nnnn Fast            | Sets the Turbo Mode to Fast                                             |
| nnnn Restore         | Restores the Turbo Mode to the value before it was set to Fast          |
| nnnn Slow            | Sets the Turbo Mode to Slow                                             |
| nnnn Swap sxdy       | Swaps the Disk II disk in Slot x Drive y to the next disk in the queue  |
| nnnn Troff           | Turns file trace logging off                                            |
| nnnn Tron            | Turns file trace logging on, to the file ./trace.txt                    |
| nnnn Type            | Types keys on the Apple II keyboard (`\r`, `123`, or `\x0a`, for example) |
| z[Range]             | Stop on specified access to an address in the range                     |
| z[Range] (counters)  | Stop on specified access to an address in the range, subject to counters |
| z[nnnn]              | Stop on specified access to an address                                  |
| z[nnnn] (counters)   | Stop on specified access to an address, subject to counters             |

For type keys, the mechanism is similar to how paste works. When address `$C010` is accessed, another key is inserted into the `$C000` keyboard address for the Apple\ 2 to read. This is useful for pressing a key when, for example, there is a prompt that says "Insert disk 2 and press enter". Type can be used in conjunction with Swap to swap disk 2 in, press enter, and continue execution without user intervention.

\Needspace{10\baselineskip}
In the above table, these symbols mean:

| Symbol     | Meaning                                                                          |
|:-----------|:---------------------------------------------------------------------------------|
| nnnn       | Hexadecimal memory location                                                      |
| (counters) | Takes the form (x/y), where x means the address has been accessed x times and    |
|            | the stop will occur when x equals y                                              |
| [Range]    | Takes the form [xxxx-yyyy], where x and y are the start and end addresses of an  |
|            | address range in which any access will count                                    |
| z          | Represents R for read access, W for write access, and RW for read or write access |

* The `[Edit]` button opens the edit breakpoints dialog.
* The `[Disable]` button leaves the breakpoint in the list but ignores it. This is useful for leaving bookmarks in memory, as the `[View PC]` button will still jump to the location of that disabled breakpoint.
* The `[View PC]` button sets the disassembly view cursor to the nnnn address of the breakpoint and brings it into view.
* The `[Clear]` button deletes the breakpoint.
* When there is more than one breakpoint, a `[Clear All]` button appears at the top of the list to delete all breakpoints (including disabled ones).

### Soft Switches Misc View
The soft switches view shows the addresses of soft switches in the Apple\ //e (currently even in Apple\ ][+ mode). These are read-only, but there is a button to override the display switches. When the override is enabled, the interface does not change the hardware settings, but it behaves as though the hardware is set to the user settings from a UI perspective. 

For example, setting Mixed to ON will draw the Apple\ 2 screen as though Mixed is enabled. This is quite useful when drawing to an off-screen buffer. The override can be turned on and the off-screen buffer can be set as the visible buffer so that the drawing can be seen on-screen. When the display override is turned off, all switches return to showing their actual settings.

Note that the addresses work in pairs, and only the first address is shown in the table. The odd address turns the setting on, and the even address turns the setting off. For example, `$C000` sets `80STORE` and `$C001` clears `80STORE`.

\Needspace{18\baselineskip}
| Address | Meaning |
|:-------:|:---------------------------------------------------------------------------------|
| C000    | 80STORE |
| C003    | RAMRD |
| C005    | RAMWRT |
| C007    | CXROM |
| C009    | ALTZP |
| C00B    | C3ROM |
| C00D    | 80COL |
| C00F    | ALTCHAR |
| C051    | TEXT |
| C053    | MIXED |
| C055    | PAGE2 |
| C057    | HIRES |
| C05E    | DHGR |
| C08x    | LCBANK |
| C08x    | LCREAD |
| C08X    | LCPREWRITE |
| C08X    | LCWRITE |

## Dialogs
The dialog boxes are activated from various views and reused wherever needed in a2m.

### File Browser Dialog
This is a simple single-file selection dialog. A single mouse click is used to activate an action. The actions are `select a file` or `select a folder`. Selecting a folder enters that folder and updates the display to show the files and folders within it. The folder `..` is the parent of the current folder. Folders are sorted to the top of the dialog, followed by files.

Press the `[OK]` button to close the dialog. If a file is highlighted when `[OK]` is pressed, that file becomes the selected file.

### Breakpoint Editor

### Symbols Dialog
The symbol search dialog has a search box at the top and two buttons, `[OK]` and `[Cancel]`, at the bottom. In the middle is a list of names, addresses, and a symbol source name.

Typing into the symbol search box performs a search on the name and the symbol source, and any matching symbols are shown in the middle section. For example, typing `PP` might show all symbols from the A`PP`LE2E source, as well as `A.TEMPP`T from the A2_BASIC source.

Clicking on any line in the middle section, for example `A.TEMPPT $0052 A2_BASIC`, sets the Disassembly View cursor to address `$0052` and makes the cursor visible in the Disassembly View.

Clicking `[OK]` closes the dialog.

### Find Dialog
The find dialog has selectors for string or hexadecimal searches. When searching for strings, type the ASCII characters to search for into the search box. When searching for hexadecimal values, type the hex values as two-digit hex numbers, not separated by any characters (for example `01abcd`), and press ENTER or click `[OK]`.

Below the `[OK][Cancel]` buttons is a status string. This normally reads `Okay`. If, for example, you enter a search of `012` into the search field and press ENTER, the status message will indicate an error. In this case, the error would be `Uneven # of hex digits`, meaning that the number of hex digits must be even (pairs of two) for the search to work.

# Using the Assembler
In the section titled **`Disassembly View`**, it was shown how to set an assembler file to assemble (`CTRL+SHIFT+b`) and how to invoke the assembler (`CTRL+b`). In the following sections, the assembler is covered in more detail, including syntax and commands.

In the remainder of the assembler documentation, 6502 means 6502 or 65C02, unless specifically stated otherwise.

## Invoking from the command line
The assembler can also be used stand-alone using the program `asm6502`. This is the same assembler used in the emulator, but with a command-line interface.

The `asm6502` executable, when used with no command-line arguments, displays this help message:
```
Usage: asm6502 <-i infile> [-o outfile] [-s symbolfile] [-v]
Where: infile is a 6502 assembly language file
       outfile will be a binary file containing the assembled 6502
       symbolfile contains a list of the addresses of all the named variables and labels
       -v turns on verbose and will dump the hex 6502 as it is assembled
```


It is worth noting that assembly files can include other assembly files. This can be seen in `samples/mminer/mminer.asm`.

\Needspace{12\baselineskip}
## Assembler Features and Syntax
The assembler supports these features:

| Feature        | Description                                                                        |
|:---------------|:-----------------------------------------------------------------------------------|
| 6502 mnemonics | All standard opcodes and addressing modes                                          |
| labels         | Labels start with `a-z` or `_` and can contain numbers. A label ends with `:`       |
| variables      | Values can be assigned and used in expressions                                     |
| .commands      | Dot commands are described below                                                   |
| comments       | The comment character is `;`; everything after `;` on a line is ignored            |
| address        | The address character is `*`; it can be assigned and read                          |
| expressions    | The assembler has a full expression parser                                         |

\Needspace{27\baselineskip}
There is a set of directives that control how a 6502 source file is assembled. These are referred to as `dot commands`, since each keyword starts with a `.`. The available `dot commands` are:

| Command        | Meaning                                                                             |
|:---------------|:------------------------------------------------------------------------------------|
| .6502          | Only 6502 opcodes are valid. 65C02 opcodes are not valid and will cause errors      |
| .65c02         | Both 6502 and 65C02 opcodes are valid                                               |
| .org n         | Set the assembly location to address n. Another way to specify `* =`               |
| .align v       | Align to v bytes, inserting up to v-1 zeroes into the output                        |
| .byte b        | Insert b as a byte into the output                                                 |
| .word w        | Insert the word bytes w into the output                                            |
| .dword dw      | Insert the double-word bytes dw into the output (low byte first)                   |
| .qword qw      | Insert the quad-word bytes qw into the output                                      |
| .drow w        | Insert the word bytes w into the output in reverse order                           |
| .drowd dw      | Insert the double-word bytes dw into the output in reverse order                   |
| .drowq qw      | Insert the quad-word bytes qw into the output in reverse order                     |
| .if p          | Conditional assembly where p is a condition such as `.if c .eq 1`                  |
| .else          | The else part of a conditional `.if` directive                                     |
| .endif         | Ends a `.if` conditional assembler directive                                       |
| .for p         | Start a loop where p has the form `<initializer>, <condition>, <iteration>`        |
| .endfor        | Ends a `.for` loop assembler directive                                             |
| .macro n p     | Start a macro procedure with name n and parameters p                               |
| .endmacro      | Ends a `.macro` assembler definition                                               |
| .incbin "f"    | Include the contents of file f verbatim in the output                              |
| .include "f"   | Include a 6502 assembler file for assembly at this point                           |
| .string "s"    | Insert the string s into the output                                                |
| .strcode       | Set a string character parser; an expression is applied to each character          |

\Needspace{11\baselineskip}
The following dot directives work with dot commands:

| Directive   | Meaning                                                                             |
|:------------|:------------------------------------------------------------------------------------|
| .defined    | Used with `.if` to test whether a macro parameter was specified                     |
| .lt         | Less than (`<`)                                                                     |
| .le         | Less than or equal (`<=`)                                                           |
| .gt         | Greater than (`>`)                                                                  |
| .ge         | Greater than or equal (`>=`)                                                        |
| .eq         | Equal (`=` or `==`)                                                                 |
| .ne         | Not equal (`!=` or `<>`)                                                            |

\Needspace{19\baselineskip}
#### Assembler Expressions
The assembler has a full expression parser. The following table lists valid tokens and illustrates their order of precedence:

| Token                             | Description                                                 |
|:----------------------------------|:------------------------------------------------------------|
| `*`, `:`, `Num`, `variables`, `(` | Address, anonymous labels, numbers, variables, and brackets |
| `+`, `-`, `<`, `>`, `~`           | Unary plus, minus, low byte, high byte, and bitwise not     |
| `**`                              | Exponentiation                                              |
| `*`, `/`, `%`                     | Multiply, divide, and modulus                               |
| `+`, `-`                          | Addition and subtraction                                    |
| `<<`, `>>`                        | Shift left and shift right                                  |
| relational                        | `.lt .le .gt .ge` for `<, <=, >, >=`                        |
| equality                          | `.ne .eq` for `!=, ==`                                      |
| `&`                               | Bitwise AND                                                 |
| `^`                               | Bitwise exclusive OR                                        |
| `\|`                              | Bitwise OR                                                  |
| `&&`, `\|\|`                      | Logical AND and OR                                          |
| `?`, `:`                          | Ternary conditional                                         |

\Needspace{9\baselineskip}
#### Assembler Numbers
Numbers can be written in the following formats:

| Prefix  | Base                                                                              |
|:--------|:----------------------------------------------------------------------------------|
| `$`     | Hexadecimal                                                                       |
| `0`     | Octal                                                                             |
| `%`     | Binary                                                                            |
| `1`–`9` | Decimal                                                                           |

\Needspace{8\baselineskip}
Inside strings, numbers can also be quoted. In that case, the formats are:

| Prefix      | Base                                                                        |
|:------------|:----------------------------------------------------------------------------|
| `\x[N]+`    | Hexadecimal                                                                 |
| `\0[N]+`    | Octal                                                                       |
| `\%[1\|0]+` | Binary                                                                      |
| `\[0-9]+`   | Decimal                                                                     |

\Needspace{6\baselineskip}
#### Assembler Variables
Variables can be followed by assignment (`=`), increment (`++`), and decrement (`--`) operators. Although this looks like postfix C notation, it is actually executed as a prefix operator. For example:
```
i = 0
lda #i++
```
This will not load A with 0, but with 1.

\Needspace{6\baselineskip}
NOTE: The address character `*` is intentionally returned as +1 from where it is read. This means:
```
* = $8000
a = *    ; a will now be $8001
: b = :- ; but b will be $8000
```
The reason is that in statements such as `lda *`, the instruction has not yet been emitted when `*` is evaluated, so reading `*` adds one. Using `a = * - 1` is also valid.

\Needspace{7\baselineskip}
#### Assembler Ternary
Like C, the ternary conditional has the form:
`(condition expression) ? (when true expression) : (when false expression)`

In its simplest form:
```
1: i = 1 ? 2 : 3
; i is assigned 2, since 1 is true
```

\Needspace{7\baselineskip}
A slightly more complex example:
```
2: i = j .eq 1 ? 4 : j .eq 2 ? 5 : 6
; if j == 1, i = 4
; else if j == 2, i = 5
; otherwise, i = 6
```

Any valid expression, no matter how complex, is allowed in each of the three clauses.

\Needspace{5\baselineskip}
#### Assembler For Loops
The assembler for-loop syntax is useful for tasks such as creating data tables:
```
.for <initialization>, <condition>, <iteration>
    ; body
.endfor
```

\Needspace{10\baselineskip}
Example:
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

The `row++` could also be written as `row = row + 1`. Any valid expression may be used in any clause. If a loop fails to terminate (that is, the condition is never false), the assembler automatically stops after 64K iterations.

Note that in `rowH`, the high byte of `$2000` is `$20`, which is ORed with the other expressions. The order of operations does not cause `|` to occur before `>`. If it did, the output would simply be `$20`, as the low-byte data would be discarded. Shift operators (`<<`, `>>`) have higher precedence than bitwise AND (`&`), so parentheses are required. The overall precedence rules match those of the C language.

\Needspace{5\baselineskip}
#### Assembler Macros
Macros have the form:
```
.macro <name> [arg [, arg]*]
 <macro body>
.endmacro
```

\Needspace{8\baselineskip}
Arguments are variables. For example:
```
.macro add_a_b a b
    clc
    lda a
    adc b
.endmacro

    add_a_b 12, 25 ; This is a call to use the macro
```

\Needspace{4\baselineskip}
This emits:
```
    clc
    lda 12
    adc 15
```

Note the lack of `#`. You cannot call the macro with `add_a_b #12, #25`, since `#12` is not a valid variable value. The `#` must be part of the macro body. This macro system is not yet very powerful, but it helps reduce repetition.

\Needspace{7\baselineskip}
#### Assembler Strcode
`.strcode` maps characters in a string to other values. It uses the variable `_` to perform the mapping. For example:
```
.strcode _-1
.string "1234"
```
This outputs `0000: 30 31 32 33`, which corresponds to `"0123"`. This is useful when mapping characters between display modes.

To disable processing, use `.strcode _`. Note that if you use `_` as a variable elsewhere, `.strcode` will overwrite it.

### Assembler Sample
The sample folder contains code for use with the assembler. The `Manic Miner` folder contains the full source code for the Manic Miner game, and most of the constructs discussed above are used there.

There is also a Python script to help de-scope ca65 assembler source files. This is how the Manic Miner sources were created.

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
