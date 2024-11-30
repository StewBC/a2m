;-----------------------------------------------------------------------------
; Macro that takes a lo and hi for the text with optional color and invert wanted
; sets up what's needed to call textShow which does the printing
.macro printXY xpos, ypos, text, len

    macro_color    = dstPtrL
    macro_strIndex = sizeL

    lda #$ff
    sta macro_color
    sta macro_color + 1

    lda #<text
    sta read_72 + 1
    lda #>text
    sta read_72 + 2
    ldx #len
    stx macro_strIndex
    lda #xpos
    asl
    tax
    ldy #ypos
    jsr textSHowText
.endmacro

.macro printXYlh xpos, ypos, textL, textH, len, colorMask

    macro_color    = dstPtrL
    strIndex       = sizeL

    lda #colorMask
    sta macro_color
    sta macro_color + 1

    lda #$7f
    sta eorMask + 1

    lda textL
    sta read_72 + 1
    lda textH
    sta read_72 + 2
    ldx len
    stx strIndex
    lda xpos
    asl
    tax
    ldy #ypos
    jsr textSHowText

.endmacro

;-----------------------------------------------------------------------------
; Prep uiShowMMText internal variables with data relevant for the
; specific text the macro is called with
.macro uiShowMMTextXY column, row, data, widths, colors

    lda #<data
    sta dataLoc + 1
    lda #>data
    sta dataLoc + 2
    lda #<widths
    sta widthLoc + 1
    lda #>widths
    sta widthLoc + 2
    lda #<colors
    sta colorLoc + 1
    lda #>colors
    sta colorLoc + 2
    ldx #column
    ldy #row
    jsr uiShowMMText

.endmacro


; ;-----------------------------------------------------------------------------
; ; Macro that takes a lo and hi for the text with optional color and invert wanted
; ; sets up what's needed to call textShow which does the printing
; .macro printXYlh xpos, ypos, textL, textH, len, colorMask, inverse

;     .local color, strIndex

;     color    = dstPtrL                          ; textShow expects color masks in dstPtr(L and H)
;     strIndex = sizeL

;     .ifblank colorMask
;         lda #$ff                                ; no color (white) is a mask of $ff left and right
;         sta color
;         sta color + 1
;     .else
;         ldx colorMask                           ; this is an index into mask[Left|Right]
;         lda masksLeft, x
;         sta color
;         lda masksRight, x
;         sta color + 1
;     .endif

;     .ifblank inverse
;         lda #0                                  ; eor o is not inverse
;     .else
;         lda #$7f                                ; eor $7f inverts the color (leave MSB)
;     .endif

;     sta eorMask + 1               ; textSHowText::eorMask set the eor in the code

;     lda textL                                   ; set the string pointer in the code
;     sta read ; textSHowText::read + 1
;     lda textH
;     sta read ; textSHowText::read + 2
;     ldx len                                     ; 0 based
;     stx strIndex
;     lda xpos                                    ; and x/y coords in x and y registers
;     asl
;     tax
;     ldy ypos
;     jsr textSHowText                            ; print that string

; .endmacro

; ;-----------------------------------------------------------------------------
; ; macro to take text address and split it into lo and hi (shorthand)
; .macro printXY xpos, ypos, text, len, colorMask, inverse
;     printXYlh xpos, ypos, #<text, #>text, len, colorMask, inverse
; .endmacro

; ;-----------------------------------------------------------------------------
; ; Prep uiShowMMText internal variables with data relevant for the
; ; specific text the macro is called with
; .macro uiShowMMTextXY column, row, data, widths, colors

;     lda #<data                                  ; MANIC or MINER bit-arrays
;     sta dataLoc ; uiShowMMText::dataLoc + 1
;     lda #>data
;     sta dataLoc ; uiShowMMText::dataLoc + 2
;     lda #<widths
;     sta widthLoc + 1              ; uiShowMMText::widthLoc array of letter-widths in MANIC or MINER
;     lda #>widths
;     sta widthLoc ; uiShowMMText::widthLoc + 2
;     lda #<colors                                ; array of letter colors in the words
;     sta colorLoc ; uiShowMMText::colorLoc + 1
;     lda #>colors
;     sta colorLoc ; uiShowMMText::colorLoc + 2
;     ldx column                                  ; screen location passed in x/y
;     ldy row
;     jsr uiShowMMText                            ; show the word on-screen

; .endmacro

