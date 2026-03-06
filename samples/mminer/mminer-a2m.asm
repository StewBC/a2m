; When using the command line assembler, asm6502, the file= parameter writes
; the compiled binary to that file

; This is the ProDOS loader that puts the Apple2 in graphics mode and loads the "game"
; When using the In Emulator assembler, simply ignore this and Auto Run at $6000
; to run the game itself directly
.scope "loader" file ="mminer.system#FF2000" dest="6502"
	.segdef "code", $2000
	.segdef "data", $20B5
	.include "loader.s"
.endscope

; This is the Manic Miner Game.  The loader would load this from Floppy
.scope "game" file="mminer#064000" dest="6502"
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
.endscope

; To add both the loader and the game to a floppy image, use something like CiderPress II command line:
; cp2 a disk_image.po mminer.system#FF2000
; cp2 a disk_image.po mminer#064000
; And now the disk_image.po is a bootable floppy disk in ProDOS order that will auto-boot Manic Miner
