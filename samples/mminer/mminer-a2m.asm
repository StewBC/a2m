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
    jmp $6000       ; Start the game, like the loader would
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
