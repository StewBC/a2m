;-----------------------------------------------------------------------------
; mminer.asm
; Part of manic miner, the zx spectrum game, made for Apple II
;
; Stefan Wessels, 2020
; This is free and unencumbered software released into the public domain.


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
: CODE = :-     ; Current address, also CODE = * - 1

main: ;  .proc

    jsr mainInit
    jsr uiWaitForIntroEnter

main_loop:
    jsr uiTitleScreen
    and #EVENT_EXIT_GAME
    bne main_quit
    jsr gameLoop
    jmp main_loop

main_quit:
    jsr MLI

    .byte   $65
    .word   *+1
    .byte   4
    .byte   0
    .word   0000
    .byte   0
    .word   0000

; endproc

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
mainInit: ;  .proc

    lda #0
    sta backPage
    sta leftEdge
    sta cameraMode
    sta uiComponent
    sta cheatActive
    sta cheatIndex
    sta monochrome

    lda #AUDIO_MUSIC | AUDIO_SOUND
    sta audioMask

    lda #>HGRPage1
    sta currPageH
    lda #0
    sta backPage
    bit TXTCLR
    bit MIXCLR
    bit HISCR
    bit HIRES
    jsr screenSwap
    jsr screenSwap

    lda #$80
    ldx #7
:
    sta bitMasks, x
    lsr
    dex
    bpl :-

    rts

; endproc

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
