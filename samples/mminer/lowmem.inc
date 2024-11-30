;-----------------------------------------------------------------------------
; lowmem.inc
; Part of manic miner, the zx spectrum game, made for Apple II
;
; Stefan Wessels, 2020
; This is free and unencumbered software released into the public domain.

;-----------------------------------------------------------------------------
; .segment "LOWMEM"

; These are instance buffers for sprites / tiles.  They are copied into this
; area and then masked for color from where they are rendered
        __lowmem = LOWMEM
levelLayout            = __lowmem
        __lowmem = __lowmem +  PLAY_COLS * PLAY_ROWS        ; Unpacked level (tile) info - keep 1st for alignment

; Instances for display
spriteInstances        = __lowmem
        __lowmem = __lowmem +  MAX_SPRITE_IFRAMES * SPRITE_BYTES
tilesInstances         = __lowmem
        __lowmem = __lowmem +  TILES_PER_LEVEL * TILE_BYTES ; instances of tiles in use bitmaps
keyAnimTiles           = __lowmem
        __lowmem = __lowmem +  KEYS_FRAMES * TILE_BYTES     ; color instances of key tile
conveyorAnimTiles      = __lowmem
        __lowmem = __lowmem +  CONVEYOR_FRAMES * TILE_BYTES ; instances of conveyor animated tiles

; cache of 8 pixel-rows for 32 double-byte characters. The level name is centered in here, and drawn from here
levelNameGfx0          = __lowmem
        __lowmem = __lowmem +  PLAY_COLS * 2 * 8
