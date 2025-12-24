;-----------------------------------------------------------------------------
; text.inc
; Part of manic miner, the zx spectrum game, made for Apple II
;
; Stefan Wessels, 2020
; This is free and unencumbered software released into the public domain.

;-----------------------------------------------------------------------------
textSHowText:

    xpos     = srcPtrL
    ypos     = srcPtrH
    rows_72     = sizeH
    strIndex_72 = sizeL
    color_72    = dstPtrL                        ; and H
    fontL    = charGfx + 1
    fontH    = charGfx + 2

    stx xpos
    sty ypos

loop_72:
    lda #0
    sta fontH                                    ; init the pointer hi to 0 for later mult rol's
    ldx strIndex_72                              ; get the index into the string

read_72:
    lda PLACEHOLDER, x                           ; get the character in the string
    sec
    sbc #32                                      ; the font only starts at space (char 32 or $20)
    beq :+
    sbc #9                                       ; gap after space
    asl                                          ; mult by 16 as that's how wide a char is in bytes
    asl
    rol fontH
    asl
    rol fontH
    asl
    rol fontH                                    ; srcPtr now points at the char but $0000 based
:
    adc #<font                                   ; add the font start
    sta fontL
    lda fontH
    adc #>font
    sta fontH                                    ; srcPtr now point at the actual character memory

    lda #8                                       ; font is 8 rows_72 high
    sta rows_72
    ldy ypos                                     ; start each character on the same line

iLoop:
    lda strIndex_72                              ; strIndex_72 is also col offset from xpos
    asl                                          ; but font is 2 cols wide
    adc xpos                                     ; add the xpos to get screen col
    adc rowL, y                                  ; and add the row
    sta write_72 + 1                             ; lo byte of where to write_72 to screen buffer
    lda currPageH                                ; get the hi page
    adc rowH, y                                  ; add the row
    sta write_72 + 2                             ; and complete the screen buffer write_72 address

    ldx #1                                       ; set up for copying 2 font bytes (1 char)
charGfx:
    lda PLACEHOLDER, x                           ; get the font
eorMask:
    eor #$00                                     ; eor to invert if necessary
    and color_72, x                              ; and to get the color_72 needed
write_72:
    sta PLACEHOLDER, x                           ; and write_72 to screen memory
    dex                                          ; go left a byte
    bpl charGfx                                  ; and repeat to do 2 bytes
    dec rows_72                                  ; done_72 one of the 8 rows_72 needed
    beq nextChar                                 ; repeat for all 8 rows_72, then done_72
    iny                                          ; more rows_72 - next row in y
    lda fontL                                    ; move along 2 in the font
    adc #2
    sta fontL
    bcc iLoop
    inc fontH
    bcs iLoop                                    ; BRA

nextChar:
    dec strIndex_72                              ; string done_72 from the back, so move left in string
    bpl loop_72                                  ; if not all of string done_72, loop_72

done_72:
    rts



;-----------------------------------------------------------------------------
; Build a memory cache of the level name since this is re-drawn much more -
; every time the level scrolls left or right and copying from a cache is
; much faster than doing a text print
textSetLevelText:

    lda #0
    sta sizeL                                    ; index into string
    sta read_73 + 2                              ; hi byte of string address

    lda currLevel                                ; start with the level
    asl                                          ; multiply by 32
    asl
    asl
    asl
    rol read_73 + 2
    asl
    rol read_73 + 2
    adc #<roTextLevel                            ; add the base address of the string
    sta read_73 + 1
    lda #>roTextLevel
    adc read_73 + 2
    sta read_73 + 2                              ; read_73 + 1 as a ptr now points at the string for this level

    lda #<levelNameGfx0                          ; start at the top of the name cache
    sta write_73 + 1                             ; and make write_73 + 1 be a pointer to the cache
    lda #>levelNameGfx0
    sta write_73 + 2

    lda #32                                      ; These strings are 32 characters wide
    sta dstPtrL

loop_73:
    lda #0
    sta srcPtrH                                  ; init the pointer hi to 0 for later mult rol's
    ldx sizeL                                    ; get the index into the string
read_73:
    lda PLACEHOLDER, x                           ; get the character in the string
    sec
    sbc #32                                      ; the font only starts at space (char 32 or $20)
    beq :+                                       ; gap after space
    sbc #9
    asl                                          ; mult by 16 as that's how wide a char is in bytes
    asl
    rol srcPtrH
    asl
    rol srcPtrH
    asl
    rol srcPtrH                                  ; srcPtr now points at the char but $0000 based
:
    adc #<font                                   ; add the font start
    sta srcPtrL
    lda srcPtrH
    adc #>font
    sta srcPtrH                                  ; srcPtr now point at the actual character memory

    lda #8                                       ; copy 8 rows / character
    sta sizeH
    ldy #1                                       ; start at the right hand side character
lrLoop:
    ldx #1                                       ; load x for the right hand side as well
copyLoop:
    lda (srcPtrL), y                             ; get the line pixel
    eor #$7f                                     ; invert
maskName:
    and maskGreen, x                             ; mask for color (always green but left vs right hand side)
write_73:
    sta PLACEHOLDER, x                           ; store to the cache
    dey                                          ; back up a byte
    dex
    bpl copyLoop                                 ; do 2 bytes, right and left hand
    lda write_73 + 1                             ; get the cache ptr low
    clc
step:
    adc #64                                      ; step to the next line - 64 bytes along
    sta write_73 + 1                             ; update the pointer
    bcc :+
    inc write_73 + 2                             ; if it wrapped, update the hi byte
:
    iny                                          ; y is now at minus 1 the last byte written
    iny                                          ; so advance it by 4 to get to the next right hand side byte
    iny
    iny
    dec sizeH                                    ; done_73 one line
    bne lrLoop                                   ; if any lines left, keep going

    sec
    lda write_73 + 1
    sbc #<((64*8)-2)
    sta write_73 + 1
    lda write_73 + 2
    sbc #>((64*8)-2)
    sta write_73 + 2
    clc

    inc sizeL                                    ; move to the next character in the string
    dec dstPtrL                                  ; see if 32 characters were done_73
    bne loop_73                                  ; if not, keep going

done_73:
    rts


;-----------------------------------------------------------------------------
; x has digit (5 means 1's, 4 means 10's, etc)
; a is the number to add 0..9
textAddScore:

    clc
    adc score, x                                 ; get the current digit
    cmp #'9'+1                                   ; has it rolled over
    bcc okay_74                                  ; then simply increment
    sec
    sbc #10
    sta score, x                                 ; and save over the 9
    lda #1
    dex                                          ; previous digit
    bmi over                                     ; if it rolls over 999999
    cpx #1                                       ; if the digit is now the 010000 (1)
    bne textAddScore                             ; no, then work with this digit
    ldy lives
    cpy #9                                       ; max out at 9 lives (keeps cheat boot on-screen)
    bcs textAddScore
    inc lives                                    ; yes, then add a life
    bne textAddScore                             ; and then work with this digit
okay_74:
    sta score, x                                 ; and store it
over:
    lda #UI_COMPONENT_SCORE                      ; mark the score texts as needing an update
    jmp uiUpdateComponent

done_74:
    rts


;-----------------------------------------------------------------------------
textCheckHighScore:

    ldx #0                                       ; start at the most significant digit
:
    lda score, x                                 ; get the score
    cmp highScore, x                             ; compare to the high score
    bcc done_75                                  ; if smaller then highscore gt score
    bne newHigh                                  ; if ne then highscore gt score
    inx                                          ; digits equal so check next digit
    cpx #6                                       ; compare x to max digits (+ 1)
    bcc :-                                       ; x is 5 or less, keep checking digits

done_75:
    rts

newHigh:
    ldx #5                                       ; copy the 6 score digits over the highscore digits
:
    lda score, x
    sta highScore, x
    dex
    bpl :-
    lda #UI_COMPONENT_HIGHSCORE
    jmp uiUpdateComponent


;-----------------------------------------------------------------------------
textColorCycle:

    color_76 = tmpBot + 1
    xPos_76  = tmpBot + 2
    yPos_76  = tmpBot + 3
    textL_76 = tmpBot + 4
    textH_76 = tmpBot + 5
    len_76   = tmpBot + 6

prntLoop:
    printXYlh xPos_76, yPos_76, textL_76, textH_76, #0, color_76

    dec len_76                                   ; one less character to color_76
    beq done_76                                  ; all characters done_76?
    inc xPos_76                                  ; move to the next character on screen
    inc textL_76                                 ; and move to the next character in the sting
    bne :+                                       ; did the string wrap a buffer
    inc textH_76                                 ; yes, up the Hi
:
    dec color_76                                 ; prev color_76
    bpl :+                                       ; still ge 0
    lda #4                                       ; no, wrap to index 4
    sta color_76                                 ; and save that as the color_76
:
    jmp prntLoop                                 ;and print this character

done_76:
    rts

