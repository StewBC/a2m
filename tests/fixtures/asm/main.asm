; Multi-file sample for am65.
.65c02
* = $6000

.include "defs.inc"

start:
    lda #MSG_LEN
    jsr print_len
    stz $00          ; 65C02-only
    bra done
done:
    rts

.include "util.inc"
