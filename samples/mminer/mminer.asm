;-----------------------------------------------------------------------------
; mminer.asm
; Part of manic miner, the zx spectrum game, made for Apple II
;
; Stefan Wessels, 2020
; This is free and unencumbered software released into the public domain.

;-----------------------------------------------------------------------------
ZEROPAGE    = $50
LOWMEM      = $0800
HGR         = $4000

.include "apple2.inc"
.include "defs.inc"
.include "macros.inc"

.include "zeropage.inc"
.include "lowmem.inc"

.org HGR
.incbin "logo.hgr"

;-----------------------------------------------------------------------------
: CODE = :-                                      ; Current address, also CODE = * - 1

main:

    jsr mainInit                                 ; do some one-time global init
    jsr uiWaitForIntroEnter                      ; color cycle ENTER and wait for key

main_loop:
    jsr uiTitleScreen                            ; go to the ui
    and #EVENT_EXIT_GAME                         ; see if event to exit game is set
    bne main_quit
    jsr gameLoop                                 ; not main_quit, so run the gameplay (or demo)
    jmp main_loop                                ; go back to the ui

main_quit:
    jsr MLI                                      ; main_quit using the prodos mli

    .byte   $65                                  ; ProDOS Quit request
    .word   *+ 1
    .byte   4
    .byte   0
    .word   0000
    .byte   0
    .word   0000


;-----------------------------------------------------------------------------
.include "audio.inc"
.include "game.inc"
.include "input.inc"
.include "level.inc"
.include "screen.inc"
.include "sprite.inc"
.include "text.inc"
.include "tiles.inc"
.include "ui.inc"
.include "willy.inc"


;-----------------------------------------------------------------------------
mainInit:

    lda #0                                       ; init some one-time globals
    ldx #bit7Mask-ZEROPAGE
:   sta ZEROPAGE,x
    dex
    bne :-

    lda #AUDIO_MUSIC | AUDIO_SOUND               ; turn the music and in-game sounds on
    sta audioMask
    lda #>HGRPage1                               ; set the current hidden (back) page to page 1
    sta currPageH                                ; (page 2 was made visible by the loader)

    bit TXTCLR
    bit MIXCLR
    bit HISCR
    bit HIRES
    jsr screenSwap
    jsr screenSwap

    lda #$80                                     ; make a zero-page bit mask area for checking bits
    ldx #7                                       ; from 1 to 128, set each bit (backwards)
:
    sta bitMasks, x                              ; set the bits in the area called bitMasks
    lsr
    dex
    bpl :-

    rts

; RODATA
.include "roaudio.inc"
.include "rodata.inc"
.include "rofont.inc"
.include "rolevels.inc"
.include "rosprites.inc"
.include "rosystem.inc"
.include "rotext.inc"
.include "rotiles.inc"

; DATA
.include "variables.inc"
