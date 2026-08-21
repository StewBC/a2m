; When using the command line assembler, asm6502, AM65 == 1 and the file= 
; parameter writes the compiled binary to that file.

.if AM65 .eq 1
    ; Command line builds the loader and game to disk
    .scope "loader" file ="mminer.system#FF2000"
        .segdef "code", $2000
        .segdef "data", $20B5
        .include "loader.s"
    .endscope
    .scope "game" file="mminer#064000"
.else 
    ; a2m built-in asssembler just builds the game to memory
    .scope "game" dest="map"
.endif

    .include "apple2.inc"
    CLR80       = $C00C ; apple2.inc has CLR80COL as $C000 - I call that CLR80STORE

    .segdef "ZEROPAGE", $50, noemit
    .segdef "LOWMEM", $800, noemit
    .segdef "HGR", $4000
    .segdef "CODE", $6000
    .segdef "RODATA", $89DC
    .segdef "DATA", $BE45

    ; Simulate what the loader would do, to the display (HGR). No harm doing it twice
    .segment "CODE"
    sta DHIRESOFF
    sta CLR80COL
    sta CLR80       ; This turns off 80 col mode (turns ON 40 col mode)
    bit TXTCLR
    bit MIXCLR
    bit HISCR
    bit HIRES

    .include "mminer.asm"
.endscope

; To add both the loader and the game to a floppy image, use something like CiderPress II command line:
; cp2 a disk_image.po mminer.system#FF2000
; cp2 a disk_image.po mminer#064000
; And now the disk_image.po is a bootable floppy disk in ProDOS order that will auto-boot Manic Miner
