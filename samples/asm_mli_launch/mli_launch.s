; Assembler MLI launch shim
;
; Requires live ProDOS (MLI at $BF00). Use Misc → Assembler with:
;   Auto-run at $2000, MLI launch checked, Reset machine unchecked.
;
; Primary path: SET_PREFIX + OPEN/READ/CLOSE + JMP into a BIN.
; Alternate for .SYSTEM targets (commented below): QUIT with pathname.
;
; Edit the placeholders before use. HostFS volume names are HOSTFS.SsDn
; (slot s = 1..7, drive n = 0 or 1), e.g. HOSTFS.S7D0.

MLI             = $BF00
SET_PREFIX_CALL = $C6
OPEN_CALL       = $C8
READ_CALL       = $CA
CLOSE_CALL      = $CC
; QUIT_CALL     = $65

IO_BUFFER       = $0800     ; 1K, page-aligned; must not overlap load/run
LOAD_ADDR       = $4000
RUN_ADDR        = $4000

        .org $2000

start:
        ldx #$FF
        txs

        ; Optional: set prefix to the HostFS volume (edit PREFIX_NAME).
        jsr MLI
        .byte SET_PREFIX_CALL
        .word prefix_param
        bcs error

        jsr MLI
        .byte OPEN_CALL
        .word open_param
        bcs error

        lda open_ref
        sta read_ref
        sta close_ref

        jsr MLI
        .byte READ_CALL
        .word read_param
        bcs error

        jsr MLI
        .byte CLOSE_CALL
        .word close_param
        bcs error

        jmp RUN_ADDR

error:
        ; Carry set / A = MLI error. Hang so the debugger can inspect.
        jmp error

; --- MLI parameter blocks ---

prefix_param:
        .byte $01
        .word prefix_name

open_param:
        .byte $03
        .word file_name
        .word IO_BUFFER
open_ref:
        .byte $00

read_param:
        .byte $04
read_ref:
        .byte $00
        .word LOAD_ADDR
        .word $FFFF         ; request up to EOF
        .word $0000         ; trans_count (result)

close_param:
        .byte $01
close_ref:
        .byte $00

; Length-prefixed ProDOS strings (edit these).
; PREFIX: "/HOSTFS.S7D0"  (12 chars) — match your SmartPort HostFS slot/drive.
prefix_name:
        .byte 12
        .byte "/HOSTFS.S7D0"

; BIN to load (partial name uses the prefix). Edit to your file.
file_name:
        .byte 6
        .byte "MYPROG"

; --- Alternate: QUIT into a .SYSTEM file (not used by this sample) ---
;
; QUIT with type $EE + pathname launches another system program.
; Example (replace the BIN path above if you prefer this):
;
;       jsr MLI
;       .byte QUIT_CALL
;       .word quit_param
;
; quit_param:
;       .byte $04
;       .byte $EE           ; quit type: launch pathname
;       .word system_name
;       .byte $00
;       .word $0000
;
; system_name:
;       .byte 13
;       .byte "MYPROG.SYSTEM"
