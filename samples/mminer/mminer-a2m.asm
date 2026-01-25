.segdef "ZEROPAGE", $50, noemit
.segdef "LOWMEM", $800, noemit
.segdef "HGR", $4000
.segdef "CODE", $6000
.segdef "RODATA", $89DC
.segdef "DATA", $BE40

SETAN3      = $C05F
CLR80STORE  = $C000

.segment "CODE"
    sta SETAN3
    sta CLR80STORE
    sta CLR80COL
    bit TXTCLR
    bit MIXCLR
    bit HISCR
    bit HIRES
; .proc main follows so this falls through intentinally
    
.include "mminer.asm"
