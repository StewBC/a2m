;-----------------------------------------------------------------------------
; zeropage.inc
; Part of manic miner, the zx spectrum game, made for Apple II
;
; Stefan Wessels, 2020
; This is free and unencumbered software released into the public domain.

;-----------------------------------------------------------------------------
        _zp = ZEROPAGE
currPageH              = _zp
        _zp = _zp +  1                           ; $02 or $04 - Hi for screen buffer to draw t0
backPage               = _zp
        _zp = _zp +  1                           ; 0 or 1 - just used to toggle HISCR or LOSCR
monochrome             = _zp
        _zp = _zp +  1                           ; 0 for color, 1 for black and white
leftEdge               = _zp
        _zp = _zp +  1                           ; distance the screen has scrolled to the right
cameraMode             = _zp
        _zp = _zp +  1
tilesRendered          = _zp
        _zp = _zp +  1                           ; count the number of tiles shown in a screen.  Used to time music delay

willyXPos              = _zp
        _zp = _zp +  1                           ; column for willy
willyYPos              = _zp
        _zp = _zp +  1                           ; pixel-row for willy
willyYRow              = _zp
        _zp = _zp +  1                           ; row (YPos / 8) for willy
willyFrame             = _zp
        _zp = _zp +  1                           ; willy animation frame
willyDir               = _zp
        _zp = _zp +  1                           ; direction willy is facing
willyJumpCounter       = _zp
        _zp = _zp +  1                           ; what phase of a jump willy is in
willyFallFromJump      = _zp
        _zp = _zp +  1                           ; 0 or 1. 1 when willy starts jumping.  Affects falling death

numSprites             = _zp
        _zp = _zp +  1                           ; how many sprites (excl. willy) to draw in a level (door is last)
spriteXPos             = _zp
        _zp = _zp +  MAX_SPRITES                 ; col position of the sprite
spriteYPos             = _zp
        _zp = _zp +  MAX_SPRITES                 ; pixel row
spriteDir              = _zp
        _zp = _zp +  MAX_SPRITES                 ; facing direction
spriteMin              = _zp
        _zp = _zp +  MAX_SPRITES                 ; min (turn/hold) point for path
spriteMax              = _zp
        _zp = _zp +  MAX_SPRITES                 ; max point for path
spriteSpeed            = _zp
        _zp = _zp +  MAX_SPRITES                 ; speed (frames/pixels) to move the sprite
spriteTick             = _zp
        _zp = _zp +  MAX_SPRITES                 ; counter how often a sprite animates (spritespeed as init)
spriteFramesIdx        = _zp
        _zp = _zp +  MAX_SPRITES                 ; Index into
spriteFrame            = _zp
        _zp = _zp +  MAX_SPRITES                 ; which frame the sprite is on
spriteClass            = _zp
        _zp = _zp +  MAX_SPRITES                 ; see CLASS_* in defs.inc for masks
spriteColor            = _zp
        _zp = _zp +  MAX_SPRITES                 ; index into masks<Left|Right> in rosystem.inc

movementMask           = _zp
        _zp = _zp +  1                           ; movement that happened in willyMove.  See MASK_* in defs.inc
userKeyMask            = _zp
        _zp = _zp +  1                           ; user desire to move based on keys pressed.  See MASK_* in defs.inc
conveyorMask           = _zp
        _zp = _zp +  1                           ; 0, willy not on conveyor, 2 for left, 1 for right

currLevel              = _zp
        _zp = _zp +  1                           ; level that's active
lives                  = _zp
        _zp = _zp +  1                           ; lives in reserve
airCols                = _zp
        _zp = _zp +  1                           ; screen columns that have an air bar in them
airTipGfx              = _zp
        _zp = _zp +  1                           ; the bit pattern for the tip of the air bar
airFlow                = _zp
        _zp = _zp +  1                           ; the "tick" till a unit of air is decreased
livesFrame             = _zp
        _zp = _zp +  1                           ; anim frame for the walking willy's at the bottom
keysFrameNum           = _zp
        _zp = _zp +  1                           ; animation frame for keys in the level (color)
conveyorFrameNum       = _zp
        _zp = _zp +  1                           ; animation frame for conveyor tile to be active
keysToCollect          = _zp
        _zp = _zp +  1                           ; number of keys that remain to be collected

eventState             = _zp
        _zp = _zp +  1                           ; see EVENT_* in defs.inc for bitmask values
updateUICount          = _zp
        _zp = _zp +  1                           ; updateUI is called when non-zero
uiComponent            = _zp
        _zp = _zp +  1                           ; See UI_COMPONENT_* in defs.inc for mask values.  What UI to update
fullScreenClearCount   = _zp
        _zp = _zp +  1                           ; 0 - clear only top, non-zero all.  Is counted down by gameAI

musicL                 = _zp
        _zp = _zp +  1                           ; pointer or index for playing music
musicH                 = _zp
        _zp = _zp +  1                           ; hi pointer for playing UI music
audioMask              = _zp
        _zp = _zp +  1                           ; see AUDIO_* in defs.inc

demoMode               = _zp
        _zp = _zp +  1                           ; 0 - not a demo, 1 when demo mode active
demoTimer              = _zp
        _zp = _zp +  1                           ; in demo mode, scroll when counter is 0
demoDirection          = _zp
        _zp = _zp +  1                           ; direction the demo scroll will move the screen

cheatIndex             = _zp
        _zp = _zp +  1                           ; count cheat code entry or when active, if 6 is down
cheatActive            = _zp
        _zp = _zp +  1                           ; non-zero when the cheat was successfully entered

                                             ;-----------------------------------------------------------------------------
tempBlock              = _zp
        _zp = _zp +  13                          ; widely used z-page general memory
srcPtrL               = tempBlock + 0            ; often a pointer Lo
srcPtrH               = tempBlock + 1            ; often a pointer Hi
dstPtrL               = tempBlock + 2            ; often a pointer Lo
dstPtrH               = tempBlock + 3            ; often a pointer Hi
sizeL                 = tempBlock + 4            ; sometimes a size used in ptr operations
sizeH                 = tempBlock + 5            ; sometimes a size used in ptr operations
tmpBot                = tempBlock + 6            ; start of block of 6 zp values used randomly

                                             ;-----------------------------------------------------------------------------
bitMasks               = _zp                     ; constant - used mostly for bit instruction
bit0Mask               = _zp
        _zp = _zp +  1                           ; 1
bit1Mask               = _zp
        _zp = _zp +  1                           ; 2
bit2Mask               = _zp
        _zp = _zp +  1                           ; 4
bit3Mask               = _zp
        _zp = _zp +  1                           ; 8
bit4Mask               = _zp
        _zp = _zp +  1                           ; 16
bit5Mask               = _zp
        _zp = _zp +  1                           ; 32
bit6Mask               = _zp
        _zp = _zp +  1                           ; 64
bit7Mask               = _zp
        _zp = _zp +  1                           ; 128
