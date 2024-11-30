;-----------------------------------------------------------------------------
; zeropage.inc
; Part of manic miner, the zx spectrum game, made for Apple II
;
; Stefan Wessels, 2020
; This is free and unencumbered software released into the public domain.

;-----------------------------------------------------------------------------
; .segment "ZEROPAGE"
        __zeropage = ZEROPAGE
;-----------------------------------------------------------------------------
currPageH              = __zeropage
        __zeropage = __zeropage +  1           ; $02 or $04 - Hi for screen buffer to draw t0
backPage               = __zeropage
        __zeropage = __zeropage +  1           ; 0 or 1 - just used to toggle HISCR or LOSCR
monochrome             = __zeropage
        __zeropage = __zeropage +  1           ; 0 for color, 1 for black and white
leftEdge               = __zeropage
        __zeropage = __zeropage +  1           ; distance the screen has scrolled to the right
cameraMode             = __zeropage
        __zeropage = __zeropage +  1
tilesRendered          = __zeropage
        __zeropage = __zeropage +  1           ; count the number of tiles shown in a screen.  Used to time music delay

willyXPos              = __zeropage
        __zeropage = __zeropage +  1           ; column for willy
willyYPos              = __zeropage
        __zeropage = __zeropage +  1           ; pixel-row for willy
willyYRow              = __zeropage
        __zeropage = __zeropage +  1           ; row (YPos / 8) for willy
willyFrame             = __zeropage
        __zeropage = __zeropage +  1           ; willy animation frame
willyDir               = __zeropage
        __zeropage = __zeropage +  1           ; direction willy is facing
willyJumpCounter       = __zeropage
        __zeropage = __zeropage +  1           ; what phase of a jump willy is in
willyFallFromJump      = __zeropage
        __zeropage = __zeropage +  1           ; 0 or 1. 1 when willy starts jumping.  Affects falling death

numSprites             = __zeropage
        __zeropage = __zeropage +  1           ; how many sprites (excl. willy) to draw in a level (door is last)
spriteXPos             = __zeropage
        __zeropage = __zeropage +  MAX_SPRITES ; col position of the sprite
spriteYPos             = __zeropage
        __zeropage = __zeropage +  MAX_SPRITES ; pixel row
spriteDir              = __zeropage
        __zeropage = __zeropage +  MAX_SPRITES ; facing direction
spriteMin              = __zeropage
        __zeropage = __zeropage +  MAX_SPRITES ; min (turn/hold) point for path
spriteMax              = __zeropage
        __zeropage = __zeropage +  MAX_SPRITES ; max point for path
spriteSpeed            = __zeropage
        __zeropage = __zeropage +  MAX_SPRITES ; speed (frames/pixels) to move the sprite
spriteTick             = __zeropage
        __zeropage = __zeropage +  MAX_SPRITES ; counter how often a sprite animates (spritespeed as init)
spriteFramesIdx        = __zeropage
        __zeropage = __zeropage +  MAX_SPRITES ; Index into
spriteFrame            = __zeropage
        __zeropage = __zeropage +  MAX_SPRITES ; which frame the sprite is on
spriteClass            = __zeropage
        __zeropage = __zeropage +  MAX_SPRITES ; see CLASS_* in defs.inc for masks
spriteColor            = __zeropage
        __zeropage = __zeropage +  MAX_SPRITES ; index into masks<Left|Right> in rosystem.inc

movementMask           = __zeropage
        __zeropage = __zeropage +  1           ; movement that happened in willyMove.  See MASK_* in defs.inc
userKeyMask            = __zeropage
        __zeropage = __zeropage +  1           ; user desire to move based on keys pressed.  See MASK_* in defs.inc
conveyorMask           = __zeropage
        __zeropage = __zeropage +  1           ; 0, willy not on conveyor, 2 for left, 1 for right

currLevel              = __zeropage
        __zeropage = __zeropage +  1           ; level that's active
lives                  = __zeropage
        __zeropage = __zeropage +  1           ; lives in reserve
airCols                = __zeropage
        __zeropage = __zeropage +  1           ; screen columns that have an air bar in them
airTipGfx              = __zeropage
        __zeropage = __zeropage +  1           ; the bit pattern for the tip of the air bar
airFlow                = __zeropage
        __zeropage = __zeropage +  1           ; the "tick" till a unit of air is decreased
livesFrame             = __zeropage
        __zeropage = __zeropage +  1           ; anim frame for the walking willy's at the bottom
keysFrameNum           = __zeropage
        __zeropage = __zeropage +  1           ; animation frame for keys in the level (color)
conveyorFrameNum       = __zeropage
        __zeropage = __zeropage +  1           ; animation frame for conveyor tile to be active
keysToCollect          = __zeropage
        __zeropage = __zeropage +  1           ; number of keys that remain to be collected

eventState             = __zeropage
        __zeropage = __zeropage +  1           ; see EVENT_* in defs.inc for bitmask values
updateUICount          = __zeropage
        __zeropage = __zeropage +  1           ; updateUI is called when non-zero
uiComponent            = __zeropage
        __zeropage = __zeropage +  1           ; See UI_COMPONENT_* in defs.inc for mask values.  What UI to update
fullScreenClearCount   = __zeropage
        __zeropage = __zeropage +  1           ; 0 - clear only top, non-zero all.  Is counted down by gameAI

musicL                 = __zeropage
        __zeropage = __zeropage +  1           ; pointer or index for playing music
musicH                 = __zeropage
        __zeropage = __zeropage +  1           ; hi pointer for playing UI music
audioMask              = __zeropage
        __zeropage = __zeropage +  1           ; see AUDIO_* in defs.inc

demoMode               = __zeropage
        __zeropage = __zeropage +  1           ; 0 - not a demo, 1 when demo mode active
demoTimer              = __zeropage
        __zeropage = __zeropage +  1           ; in demo mode, scroll when counter is 0
demoDirection          = __zeropage
        __zeropage = __zeropage +  1           ; direction the demo scroll will move the screen

cheatIndex             = __zeropage
        __zeropage = __zeropage +  1           ; count cheat code entry or when active, if 6 is down
cheatActive            = __zeropage
        __zeropage = __zeropage +  1           ; non-zero when the cheat was successfully entered

                                             ;-----------------------------------------------------------------------------
tempBlock              = __zeropage
        __zeropage = __zeropage +  13          ; widely used z-page general memory
srcPtrL               = tempBlock + 0       ; often a pointer Lo
srcPtrH               = tempBlock + 1       ; often a pointer Hi
dstPtrL               = tempBlock + 2       ; often a pointer Lo
dstPtrH               = tempBlock + 3       ; often a pointer Hi
sizeL                 = tempBlock + 4       ; sometimes a size used in ptr operations
sizeH                 = tempBlock + 5       ; sometimes a size used in ptr operations
tmpBot                = tempBlock + 6       ; start of block of 6 zp values used randomly

                                             ;-----------------------------------------------------------------------------
bitMasks               = __zeropage           ; constant - used mostly for bit instruction
bit0Mask               = __zeropage
        __zeropage = __zeropage +  1           ; 1
bit1Mask               = __zeropage
        __zeropage = __zeropage +  1           ; 2
bit2Mask               = __zeropage
        __zeropage = __zeropage +  1           ; 4
bit3Mask               = __zeropage
        __zeropage = __zeropage +  1           ; 8
bit4Mask               = __zeropage
        __zeropage = __zeropage +  1           ; 16
bit5Mask               = __zeropage
        __zeropage = __zeropage +  1           ; 32
bit6Mask               = __zeropage
        __zeropage = __zeropage +  1           ; 64
bit7Mask               = __zeropage
        __zeropage = __zeropage +  1           ; 128
