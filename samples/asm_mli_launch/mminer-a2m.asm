; Assemble ths shim into memory
.scope "mli_launch" dest = "map"
    .include "mli_launch.s"
.endscope

; Assemble the loader to a hsotfs volume
.scope "loader" file ="../hostfs/mminer/mminer.system#FF2000"
    .segdef "code", $2000
    .segdef "data", $20B5
    .include "../mminer/loader.s"
.endscope

; This is the Manic Miner Game.  The loader loads this from disk
.scope "game" file="../hostfs/mminer/mminer#064000"
    .segdef "ZEROPAGE", $50, noemit
    .segdef "LOWMEM", $800, noemit
    .segdef "HGR", $4000
    .segdef "CODE", $6000
    .segdef "RODATA", $89DF
    .segdef "DATA", $BE48

    .include "../mminer/apple2.inc"
    CLR80       = $C00C ; apple2.inc has CLR80COL as $C000 - I call that CLR80STORE

    .segment "CODE"
    .if AM65 .eq 0
        ; Simulate what the loader would do, to the display (HGR) when using
        ; a2m (vs am65) to build the code
        sta DHIRESOFF
        sta CLR80COL
        sta CLR80       ; This turns off 80 col mode (turns ON 40 col mode)
        bit TXTCLR
        bit MIXCLR
        bit HISCR
        bit HIRES
    .endif
    
    ; Now the actual game
    .include "../mminer/mminer.asm"
.endscope
