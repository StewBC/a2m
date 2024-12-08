;-----------------------------------------------------------------------------
; rosystem.inc
; Part of manic miner, the zx spectrum game, made for Apple II
;
; Stefan Wessels, 2020
; This is free and unencumbered software released into the public domain.

;-----------------------------------------------------------------------------
; .segment "RODATA"

;-----------------------------------------------------------------------------
; the lo byte for the memory at the start of every screen row
rowL:
    .for  __Row=0,  __Row .lt $C0,  __Row++
        .byte   (__Row & $08) << 4 | (__Row & $C0) >> 1 | (__Row & $C0) >> 3
    .endfor

;-----------------------------------------------------------------------------
; the hi byte for the memory at the start of every screen row
rowH:
    .for  __Row=0,  __Row .lt $C0,  __Row++
        .byte   >$0000 | (__Row & $07) << 2 | (__Row & $30) >> 4
    .endfor

;-----------------------------------------------------------------------------
; Jump table for screen tile rendering, per row - unrolled loops
rowDrawL:
    .byte <tileDrawP0R0
    .byte <tileDrawP0R1
    .byte <tileDrawP0R2
    .byte <tileDrawP0R3
    .byte <tileDrawP0R4
    .byte <tileDrawP0R5
    .byte <tileDrawP0R6
    .byte <tileDrawP0R7
    .byte <tileDrawP0R8
    .byte <tileDrawP0R9
    .byte <tileDrawP0RA
    .byte <tileDrawP0RB
    .byte <tileDrawP0RC
    .byte <tileDrawP0RD
    .byte <tileDrawP0RE
    .byte <tileDrawP0RF
    .byte <tileDrawP1R0                         ; Page 1
    .byte <tileDrawP1R1
    .byte <tileDrawP1R2
    .byte <tileDrawP1R3
    .byte <tileDrawP1R4
    .byte <tileDrawP1R5
    .byte <tileDrawP1R6
    .byte <tileDrawP1R7
    .byte <tileDrawP1R8
    .byte <tileDrawP1R9
    .byte <tileDrawP1RA
    .byte <tileDrawP1RB
    .byte <tileDrawP1RC
    .byte <tileDrawP1RD
    .byte <tileDrawP1RE
    .byte <tileDrawP1RF

rowDrawH:
    .byte >tileDrawP0R0
    .byte >tileDrawP0R1
    .byte >tileDrawP0R2
    .byte >tileDrawP0R3
    .byte >tileDrawP0R4
    .byte >tileDrawP0R5
    .byte >tileDrawP0R6
    .byte >tileDrawP0R7
    .byte >tileDrawP0R8
    .byte >tileDrawP0R9
    .byte >tileDrawP0RA
    .byte >tileDrawP0RB
    .byte >tileDrawP0RC
    .byte >tileDrawP0RD
    .byte >tileDrawP0RE
    .byte >tileDrawP0RF
    .byte >tileDrawP1R0                         ; Page 1
    .byte >tileDrawP1R1
    .byte >tileDrawP1R2
    .byte >tileDrawP1R3
    .byte >tileDrawP1R4
    .byte >tileDrawP1R5
    .byte >tileDrawP1R6
    .byte >tileDrawP1R7
    .byte >tileDrawP1R8
    .byte >tileDrawP1R9
    .byte >tileDrawP1RA
    .byte >tileDrawP1RB
    .byte >tileDrawP1RC
    .byte >tileDrawP1RD
    .byte >tileDrawP1RE
    .byte >tileDrawP1RF

;-----------------------------------------------------------------------------
mult64H:
.for __Row=0, __Row .lt MAX_SPRITE_IFRAMES, __Row++
    .byte >(__Row * 64)
.endfor

mult64L:
.for __Row=0, __Row .lt MAX_SPRITE_IFRAMES, __Row++
    .byte <(__Row * 64)
.endfor

mult32H:
.for __Row=0, __Row .lt 15, __Row++
    .byte >(__Row * 32)
.endfor

mult32L:
.for __Row=0, __Row .lt 15, __Row++
    .byte <(__Row * 32)
.endfor

mult16:
.for __Row=0, __Row .lt 9, __Row++
    .byte __Row * 16
.endfor

mult8:
.for __Row=0, __Row .lt 24, __Row++
    .byte __Row * 8
.endfor

;-----------------------------------------------------------------------------
; color masks
masksLeft:
.byte %11111111                                 ; 00 MSB-BITS-11..1 White
.byte %00101010                                 ; 01   0-BITS-01..0 Green
.byte %10101010                                 ; 04   1-BITS-01..0 Orange
.byte %01010101                                 ; 01   0-BITS-10..1 Purple
.byte %11010101                                 ; 02   1-BITS-10..1 Blue

masksRight:
.byte %11111111                                 ; 00  MSB-BITS-11 White
.byte %01010101                                 ; 01    0-BITS-10..1 Green
.byte %11010101                                 ; 04    1-BITS-10..1 Orange
.byte %00101010                                 ; 01    0-BITS-01..0 Purple
.byte %10101010                                 ; 02    1-BITS-01..0 Blue

maskGreen:
.byte %00101010, %01010101                      ; Green

maskOrange:
.byte %10101010, %11010101                      ; Orange

maskGreenHi:
.byte %00100000, %01000000                      ; Green

maskNewTip:
.byte %01011111, %00111111                      ; White air graph tip (reversed order)

;-----------------------------------------------------------------------------
; sprite 32 to 64 byte bit-double helper table
binDouble:
.byte %00000000
.byte %00000011
.byte %00001100
.byte %00001111
.byte %00110000
.byte %00110011
.byte %00111100
.byte %00111111
.byte %11000000
.byte %11000011
.byte %11001100
.byte %11001111
.byte %11110000
.byte %11110011
.byte %11111100
.byte %11111111
