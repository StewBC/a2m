.segdef "ZEROPAGE", $50, noemit
.segdef "LOWMEM", $800, noemit
.segdef "HGR", $4000
.segdef "CODE", $6000
.segdef "RODATA", $89CA
.segdef "DATA", $BE30

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
