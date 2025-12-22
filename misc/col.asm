CLRRAMWRT   = $C004 ; CPU writes from main $0200-$BFFF and LC $D000-$FFFF, see SET80STORE
SETRAMWRT   = $C005 ; CPU writes from aux  $0200-$BFFF and LC $D000-$FFFF, see SET80STORE
CLR80COL    = $C00C ; 40-column display (turn 80-col display off)
SET80COL    = $C00D ; 80-column display (turn 80-col display on)
TXTCLR      = $C050 ; Enable graphics (lores or hires)
TXTSET      = $C051 ; Enable text
CLRPAGE2    = $C054 ; Page 1
SETPAGE2    = $C055 ; Page 2
CLRHIRES    = $C056 ; Enable Lores graphics (Disable Hires)
SETHIRES    = $C057 ; Enable Hires graphics (Disable Lores)
CLRAN3      = $C05E ; Enable DHGR
SETAN3      = $C05F ; Disable DHGR

* = $6000
    ; set double hires and clear the screen
    lda #$20
    sta c0+2
    sta c1+2
    lda #00
    tax
    sta TXTCLR
    sta CLRPAGE2
    sta SETHIRES
    sta SET80COL
    sta CLRAN3
    sta CLRRAMWRT
    ldy #$40
c0: sta $2000,x
    dex
    bne c0
    sta SETRAMWRT
c1: sta $2000,x
    dex
    bne c1
    sta CLRRAMWRT
    inc c0 + 2
    inc c1 + 2
    dey
    bne c0

    lda #0
    sta data_index  ; where in the data table
    sta row_index   ; which row on screen
    sta row_count   ; how many sceen rows this data set will need to be copied to
:   lda #3          ; num rows to repeat each pattern
    sta row_count

:   ldx data_index
    jsr set4
    inc row_index
    dec row_count
    bne :-

    stx data_index
    cpx #data_end - data
    bne :--
:   jmp :-

set4:
    ldy row_index
    lda rowL, y
    sta $50
    lda rowH, y
    sta $51

    ldy #0
    sta SETRAMWRT
    lda data,x  ; 0
    inx
    sta ($50),y ; aux 0
    sta CLRRAMWRT
    lda data,x  ; 1
    inx
    sta ($50),y ; main 0
    sta SETRAMWRT
    iny
    lda data,x  ; 2
    inx
    sta ($50),y ; aux 1
    lda data,x  ; 3
    inx
    sta CLRRAMWRT
    sta ($50),y ; main 1
    rts

data_index: .byte 0
row_index: .byte 0
row_count: .byte 0

rowL:
    .for row=0, row .lt $C0, row++
        .byte   (row & $08) << 4 | (row & $C0) >> 1 | (row & $C0) >> 3
    .endfor

rowH:
    .for row=0, row .lt $C0, row++
        .byte   >$2000 | (row & $07) << 2 | (row & $30) >> 4
    .endfor

data:
.byte $08, $11, $22, $44 ; 0001000100010001000100010001
.byte $44, $08, $11, $22 ; 0010001000100010001000100010
.byte $4c, $19, $33, $66 ; 0011001100110011001100110011
.byte $22, $44, $08, $11 ; 0100010001000100010001000100
.byte $2a, $55, $2a, $55 ; 0101010101010101010101010101
.byte $66, $4c, $19, $33 ; 0110011001100110011001100110
.byte $6e, $5d, $3b, $77 ; 0111011101110111011101110111
.byte $11, $22, $44, $08 ; 1000100010001000100010001000
.byte $19, $33, $66, $4c ; 1001100110011001100110011001
.byte $55, $2a, $55, $2a ; 1010101010101010101010101010
.byte $5d, $3b, $77, $6e ; 1011101110111011101110111011
.byte $33, $66, $4c, $19 ; 1100110011001100110011001100
.byte $3b, $77, $6e, $5d ; 1101110111011101110111011101
.byte $77, $6e, $5d, $3b ; 1110111011101110111011101110
.byte $7f, $7f, $7f, $7f ; 1111111111111111111111111111
.byte $10, $22, $44, $08 ; 00001000100010001000100010001
.byte $08, $11, $22, $44 ; 00010001000100010001000100010
.byte $18, $33, $66, $4c ; 00011001100110011001100110011
.byte $44, $08, $11, $22 ; 00100010001000100010001000100
.byte $54, $2a, $55, $2a ; 00101010101010101010101010101
.byte $4c, $19, $33, $66 ; 00110011001100110011001100110
.byte $5c, $3b, $77, $6e ; 00111011101110111011101110111
.byte $22, $44, $08, $11 ; 01000100010001000100010001000
.byte $32, $66, $4c, $19 ; 01001100110011001100110011001
.byte $2a, $55, $2a, $55 ; 01010101010101010101010101010
.byte $3a, $77, $6e, $5d ; 01011101110111011101110111011
.byte $66, $4c, $19, $33 ; 01100110011001100110011001100
.byte $76, $6e, $5d, $3b ; 01101110111011101110111011101
.byte $6e, $5d, $3b, $77 ; 01110111011101110111011101110
.byte $7e, $7f, $7f, $7f ; 01111111111111111111111111111
.byte $20, $44, $08, $11 ; 000001000100010001000100010001
.byte $10, $22, $44, $08 ; 000010001000100010001000100010
.byte $30, $66, $4c, $19 ; 000011001100110011001100110011
.byte $08, $11, $22, $44 ; 000100010001000100010001000100
.byte $28, $55, $2a, $55 ; 000101010101010101010101010101
.byte $18, $33, $66, $4c ; 000110011001100110011001100110
.byte $38, $77, $6e, $5d ; 000111011101110111011101110111
.byte $44, $08, $11, $22 ; 001000100010001000100010001000
.byte $64, $4c, $19, $33 ; 001001100110011001100110011001
.byte $54, $2a, $55, $2a ; 001010101010101010101010101010
.byte $74, $6e, $5d, $3b ; 001011101110111011101110111011
.byte $4c, $19, $33, $66 ; 001100110011001100110011001100
.byte $6c, $5d, $3b, $77 ; 001101110111011101110111011101
.byte $5c, $3b, $77, $6e ; 001110111011101110111011101110
.byte $7c, $7f, $7f, $7f ; 001111111111111111111111111111
.byte $40, $08, $11, $22 ; 0000001000100010001000100010001
.byte $20, $44, $08, $11 ; 0000010001000100010001000100010
.byte $60, $4c, $19, $33 ; 0000011001100110011001100110011
.byte $10, $22, $44, $08 ; 0000100010001000100010001000100
.byte $50, $2a, $55, $2a ; 0000101010101010101010101010101
.byte $30, $66, $4c, $19 ; 0000110011001100110011001100110
.byte $70, $6e, $5d, $3b ; 0000111011101110111011101110111
.byte $08, $11, $22, $44 ; 0001000100010001000100010001000
.byte $48, $19, $33, $66 ; 0001001100110011001100110011001
.byte $28, $55, $2a, $55 ; 0001010101010101010101010101010
.byte $68, $5d, $3b, $77 ; 0001011101110111011101110111011
.byte $18, $33, $66, $4c ; 0001100110011001100110011001100
.byte $58, $3b, $77, $6e ; 0001101110111011101110111011101
.byte $38, $77, $6e, $5d ; 0001110111011101110111011101110
.byte $00, $00, $00, $00 ; 0000000000000000000000000000000 <- Don't put last it will be clipped ;)
.byte $78, $7f, $7f, $7f ; 0001111111111111111111111111111
.byte $7f, $7f, $7f, $7f ; 1111111111111111111111111111111
data_end:
