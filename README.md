# Apple ][+ Emulator  
This is an Apple ][+ emulator written in "C" using SDL and the Nuklear immediate mode GUI.  The emulator includes a cycle-accurate 6502 CPU (does not support undocumented opcodes), a Language Card and a SmartPort block device.  No Disk II support.  
  
This configuration allows booting and running Total Replay or other ProDOS disk volumes.  
  
![15 FPS Animated Gif of the emulator in action](assets/a2m-15.gif)  
  
## Starting the emulator  
All you need to start is the apple2 executable. An optional configuration file, apple2.ini, can be used to specify settings such as which slots contain a SmartPort interface and which disks are mounted as devices 0 & 1 on the interface. A boot parameter can also be set to boot the disk0 image. Here's an example .ini file:  
```
[Display]
scale = 1.0                   ; Uniformly scale Application Window

[Video]
slot = 3
device = Franklin Ace Display ; 80 Column Videx like card

[SmartPort]
; configure the slots on the Apple ][
slot = 7                      ; This says a slot contains a smartport
disk0 = ./disks/NewDisk.po    ; Path to a disk in the device (called disk in here)
disk1 = ./disks/Total Replay v5.2.hdv
boot = 0                      ; any value other than 0 will cause a boot of disk0
```  
Multiple slots can be assigned a SmartPort. If boot is enabled for multiple slots, the last enabled slot will be booted.  
Note: Pathnames are not enclosed in quotation marks.  
  
The Franklin Ace Display can be put in any slot 1..7.  When software, such as Davex, uses the card, the display must be manually switched to that card uing F12 (or F12 + Shift).  ProDOS Basic only partially works with the card.  PR#0 will not deactivate the card fully, but booting ProDOS will properly deactivate the card.  
  
Note: No error checking is done around putting any 2 cards in one slot.  The last one listed will be the active card.  
  
The emulator can also load symbol files.  It will look in a folder called symbols for the following files:  
```
└───symbols
        A2_BASIC.SYM
        APPLE2E.SYM
        USER.SYM
```  
A2_BASIC.SYM and APPLE2E.SYM are from `AppleWin`. USER.SYM is a custom symbol file with user-defined symbols. If these files are not found, they are silently ignored. They are loaded in the listed order. APPLE2E.SYM will not replace symbols defined in A2_BASIC.SYM, but USER.SYM will override symbols in either of the previous files. Lines that start with a hex number are treated as symbols, with the hex number indicating the address. Here’s an example from my Manic Miner game:  
```
007338 read
0072C0 screenDrawSprite
0072B1 screenDrawSprites
00669F collapseDraw
0066A7 allDrawn
006657 setupSwitch
```  
This USER.SYM file is generated for me by transforming the ca65 output (I used cc65's assembler, ca65, to write Manic Miner) using this SED command in the Makefile:  
```
sed "s/^al \([[0-9A-F]\+\)\ \./\1 /g" $(NAME).apple2.lbl > USER.SYM
```  
  
## Using the Emulator  
When the emulator starts, it opens as a 1120 x 840 window displaying the Apple ][+ screen. The resolution can be scaled using the apple2.ini file. If no SmartPort disk is booted, the emulator defaults to Applesoft BASIC. Remember to activate CAPS LOCK, as cursor controls require uppercase letters.  
  
The function keys control the emulator (or debugger), while other keys go to the Apple ][+ machine when it’s running. The function keys and their functions are as follows:  
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
When running, debug windows update at 60 FPS, but user control only applies to the Apple ][+ machine. Once stopped, you can interact with the debug windows. The window title changes to green when it is "active", with "active" determined by mouse hover and input going to the "active" window.  
  
## Using the CPU Window  
The CPU window, when stopped, has editable boxes for the PC, SP, registers, and flags. To edit a value, click in the box, keep the mouse pointer over the CPU window, type a new value, and press ENTER.  
  
## Using the Disassembly Window  
The following keys are supported in the disassembly window:  
```
CTRL + G - Set the address for viewing disassembly (cursor PC)
CTRL + P - Set the machine PC to the cursor PC
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
CTRL + S - Split the memory display (up to 16 virtual memory windows)
CTRL + T - Toggle between HEX and ASCII editing
ALT + 0-F - Select a virtual memory window
```  
Split views are differentiated by colored with a virtual window ID (e.g., 0:0000). This enables viewing up to 16 distinct memory regions from the one Memory Window.  
Type HEX digits to edit the memory in HEX mode and use any key to edit the memory in ASCII mode (switch using CTRL + T).  
  
## Using the Miscellaneous Window  
This window currently has 4 sections.  Each section can be shown/hidden by clicking on the triangle before the section name.  NOTE:  These sections only respond to input when the emulator is stopped.  
### SmartPort  
This shows which slot has a SmartPort interface.  In each slot, slot 0 display (example `7.0`) is a button.  Pressing that button will boot disk0 (if mounted).  The `Eject` button "removes" the disk from the slot and the `Insert` button brings up a file browser through which a disk can be mounted.  Note: Selecting a file that is not a disk image will work as _no_ validation is done.  The only check is if a file has `2IMG` in the header, the offset for the start in the file is set to 64 bytes from the start of the file (ignoring/skipping the header).  
This section is populated based on the apple2.ini file.  Making changes in the UI will _not_ update the apple2.ini file.  The apple2.ini file needs to be edited by hand.  
### Debugger  
The status display shows:  
`Run to PC O XXXX`.  While the emulator is "running to the address XXXX" the `O` is lit.  It is good to know that stepping over a `JSR` for example, is in progress.  `Step Out O` similarly is active when using SHIFT-F11 and the function doesn't "step out" quickly.  
The `Step Cycles` show the number of cycles that elapsed from the emulator being stopped, till stopped again. So, if the emulator just stepped over a 2-cycle opcode using F11, that will show 2.  This is very handy for profiling since stepping over a `JSR` subroutine shows the all-inclusive number of cycles it took to run the routine.  
`Total cycles` show the number of cycles executed since the start.  This number will roll over fairly quickly (minutes, not days of running the emulator).  
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
The Language Card section shows the state of the Language Card, whether ROM or RAM is active, which bank, and if writing to RAM is enabled.  
  
## Building from source  
Currently, building instructions are limited. On Windows, install SDL2 and SDL_mixer and use VS Code with CMake plugins. On Linux (WSL), SDL2 and mixer were installed from source to access find_package scripts. VS Code was used with Clang. Mac support is untested.  
  
## Something about the code  
In this project, a typedef struct is referred to as a "class." The main hardware class, APPLE2, manages hardware subcomponents like the speaker and SmartPort. The VIEWPORT class handles rendering, keyboard input, making sounds, and debugging windows. Most API calls use an APPLE2 instance as the first parameter (named m for machine).  
  
The APPLE2 structure is designed to be compact, with dynamic allocation for certain components to support potential "time travel" functionality.  
  
## The source files and what they do  
File | Description 
--- | --- 
6502.c | Cycle accurate 6502 implementation
apple2.c | Apple ][+ hardware config & softswitch implementation
breakpnt.c | Breakpoints.  Currently it only breaks on PC and run to or step out
dbgopcds.c | Arrays for disassembly printing of opcodes
dynarray.c | Dynamic arrays
frankdisp.c | Franklin Ace Display 80 Col card
header.h | One header file to include all needed header files
main.c | Define the Apple ][+ machine and view (Display) and main emulation loop
nuklear.h | GUI - see header (Modified very slightly for text background coloring)
nuklrsdl.h | SDL draw of Nuklear GUI - comes with nuklear.h
ramcard.c | Language card
roms.c | Apple ][+, Smartport & character ROMS.  Also Disk II (unused)
slot.c | Tracks plug-in cards in slots and activates C800 ROM as needed
sftswtch.h | Addresses where the emulator cares about soft switches
smrtprt.c | SmarPort block device
util.c | File and folder utilities, ini parser, etc.
viewapl2.c | HGR & Lowres drawing - view the Apple ][+ screens
viewcpu.c | Key and display handling of CPU window
viewdbg.c | Key and display handling of disassembly window
viewdlg.c | Modal dialog implementations
viewmem.c | Key and display handling of memory window
viewmisc.c | Key and display handling of the miscellaneous window
viewport.c | SDL2 initialization and manage all the other views, update the display
  
## Known issues  
Audio: Audio may not work great and at Max Speed (F3), not at all.  
Memory Cleanup: Not all malloc allocations are freed on exit.  
Other bugs may be present, as testing has been limited.  
  
## Future plans  
This project stemmed from an interest in creating a 6502 that could pass the Harte CPU tests, which led to making a small emulator for Manic Miner, which in turn led to the creation of this Emulator. While I may continue tinkering (possibly adding time travel), my passion is making games, and I want to get back to that.  
  
## Thank you  
* Brendan Robert - Author of Jace, for telling me how to think about sampling for speaker emulation.  
* Oliver Schmidt for, basically, teaching me all I know about the Apple II, but in this case showing me how easy it is to do SmartPort emulation.  
* Zellyn Hunter for A2Audit.  That helped me identify and fix my Language Card bug.  (https://github.com/zellyn/a2audit)  
  
## Initial revision  
This initial revision was made in London, UK, on 31 October 2024.  
  
## Contact  
Stefan Wessels  
swessels@email.com  
