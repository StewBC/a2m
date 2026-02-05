# Apple ][+ and //e Enhanced emulator
Some of the key features of a2m include:
* Fast, cycle-accurate CPU emulation (Apple II at around 140–160 MHz at approx. 60 FPS on an M2 Mac Mini)
* 60 FPS video display (not cycle-accurate by design, favoring performance and responsiveness)
* Built-in debugger with stepping modes, symbols, breakpoints, soft-switch overrides, and more
* Built-in macro assembler (assembles 12,000+ lines across 24 Manic Miner source files in milliseconds)
* Disk II NIB read support
* SmartPort block read/write
* SDL joystick support
* Partial Franklin 80-column card emulation for the Apple ][+ model
* Tested on Windows, Linux and macOS.  (Requires SDL2 and curses.)

a2m is designed for people who enjoy developing or exploring Apple 2 software.

## Introduction
a2m began as a small experiment after I discovered the Harte 6502 CPU tests. I wanted to see if I could write a cycle-accurate 6502 CPU emulator, just for fun. Once that worked, it became obvious that it wouldn't take much more code to wrap a minimal Apple 2 environment around it and run the Manic Miner clone I had written. That led to MMM [The Manic Miner Machine](https://github.com/StewBC/mminer-apple2/tree/master/src/mmm). Things escalated from there, and a2m V1.0 was done by the end of 2024. About a year later I started work on V2.0. Apparently, the emulation hook never really let go of me.

## What V1 looked like
This animated gif is from V1.0.  The look of V2.0 is still similar.
![15 FPS Animated Gif of the emulator in action](assets/a2m-15.gif) 
  
## License
a2m is released under the Unlicense, meaning it is free and unencumbered software placed in the public domain.

## User Manual
The user manual is in markdown, but written in a format that can be turned into a PDF.  The PDF is in the releases, but the markdown also available for viewing in a browser, with some extra mark-up, [here](https://github.com/StewBC/a2m/blob/master/manual/manual.md)  
  
The user manual is also available from within a2m, on the F1 key.  Tabs at the bottom of the screen and F1 to close the help again.  
  
Press F2 to reveal the interface to a2m - the debugger and access to disk drives, etc.  
  
## Known issues
* Audio  
  Audio quality could be improved, but playback timing is now stable and no longer drifts.
* Disk II  
  Only, read-only, NIB file-type support.
* SmartPort  
  Airheart & Archon both crash. I think they rely on SmartPort functionality beyond simple block read/write, but I haven't fully investigated.
* General  
  Other bugs may be present, as testing has been limited.
 
## Thank you  
* Brendan Robert - Author of Jace, for telling me how to think about sampling for speaker emulation.  
* Oliver Schmidt for, basically, teaching me all I know about the Apple II, but in this case showing me how easy it is to do SmartPort emulation.  
* Zellyn Hunter for A2Audit.  That helped me identify and fix my Language Card bug.  (https://github.com/zellyn/a2audit)  
  
## Initial revision  
The initial release was made in London, UK, on 31 October 2024.  

### Releases since:   
31 Oct 2024
:   Initial release

8 Dec 2024
:   Version 1.0 release

10 Dec 2024
:   Version 1.1 release

23 Dec 2025
:   Version 2.0 release  
    The version 2 release is a re-architecture of the entire code base, as well as a rewrite of the 6502 core, adding a 65C02 mode. The Apple //e is also supported, along with many new features such as a NIB Disk II, resizable window, window pane sliders, and many more.

4 Feb 2026
:   Version 2.1 release  
    This version fixes bugs and has tweaks above the V2.0 release, and it enhances the built-in assembler allowing it to compile the bulk of ca65 assembler targeted sources with only some edits.
 
## Contact  
Stefan Wessels  
swessels@email.com  
