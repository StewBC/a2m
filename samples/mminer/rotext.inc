;-----------------------------------------------------------------------------
; rotext.inc
; Part of manic miner, the zx spectrum game, made for Apple II
;
; Stefan Wessels, 2020
; This is free and unencumbered software released into the public domain.

;-----------------------------------------------------------------------------
; .segment "RODATA"

roTextLevel:
.string "         CENTRAL CAVERN         "
.string "          THE COLD ROOM         "
.string "          THE MENAGERIE         "
.string "   ABANDONED URANIUM WORKINGS   "
.string "         EUGENE+S LAIR          "
.string "       PROCESSING PLANT         "
.string "            THE VAT             "
.string "MINER WILLY MEETS THE KONG BEAST"
.string "        WACKY AMOEBATRONS       "
.string "       THE ENDORIAN FOREST      "
.string "ATTACK OF THE MUTANT TELEPHONES "
.string " RETURN OF THE ALIEN KONG BEAST "
.string "          ORE REFINERY          "
.string "       SKYLAB LANDING BAY       "
.string "            THE BANK            "
.string "      THE SIXTEENTH CAVERN      "
.string "         THE WAREHOUSE          "
.string "      AMOEBATRONS+ REVENGE      "
.string "     SOLAR POWER GENERATOR      "
.string "        THE FINAL BARRIER       "
roTextAir:
.string "AIR  "
roTextScore:
.string "SCORE"
roTextHighScore:
.string "HIGH"
roTextGame:
.string "GAME"
roTextOver:
.string "OVER"
roTextPressEnter:
.string "PRESS ENTER TO START"
roTextEnter = (roTextPressEnter + 6)
roTextAppleIIVersion:
.string " APPLE II  V1.1A BY "
roTextStefan:
.string "STEFAN WESSELS, 2020"
roTextIntro:
.string ".  .  .  .  .  .  . MANIC MINER . . [ BUG-BYTE LTD.  1983 . . BY "
.string "MATTHEW SMITH . . . Q, O * W, P = LEFT * RIGHT . . SPACE = JUMP . . M = MUSIC "
.string "ON/OFF . . S = IN GAME SOUND ON/OFF . . B = MONOCHROME/COLOR . . "
.string "C = LEVEL SCROLL MODE . . ESC = QUIT . . . GUIDE MINER WILLY THROUGH 20 LETHAL "
.string "CAVERNS .  .  .  .  .  .  ", 0
roTextMono:
.string " MONO"
roTextColor:
.string "COLOR"
roTextCheatCode:
.string "6031769"
