; Assembler MLI launch shim
; This is used with hostfs drives and assembler programs that assemble
; to file.  This shim assembles to memory and "boots" the file= 
; off of the hostfs volume, using MLI launch

; This shim is set to load either mminer.system or just mminer from a path
; /HOSTFS.S7D0/MMINER
; Edit the names to your needs.

; Look for EDIT_HERE for the knobs.

; Misc -> Assembler: Assemble/Auto-run at SHIM_ADDRESS, MLI launch on, Reset off.
; Keep SHIM_ADDRESS and IO_BUFFER away from any BIN load range.

; QUIT_TO_SYSTEM:
;   0 = OPEN/READ a type-$06 BIN, JMP RUN_ADDR
;   1 = launch a .SYSTEM the way the ProDOS dispatcher does:
;       OPEN/READ to $2000, store pathname at $0280, JMP $2000
;       (MLI QUIT $EE is GS/OS-only; on ProDOS 8 + Bitsy Bye it just returns
;       to the selector — so we do not use QUIT for .SYSTEM launch.)
; EDIT HERE:
QUIT_TO_SYSTEM  = 0

; EDIT HERE:
SHIM_ADDRESS    = $3000

MLI             = $BF00
SET_PREFIX_CALL = $C6
OPEN_CALL       = $C8
READ_CALL       = $CA
CLOSE_CALL      = $CC

PATHNAME        = $0280
SYSTEM_LOAD     = $2000

; EDIT HERE:
IO_BUFFER       = $0C00     ; 1K page-aligned; must not overlap loads

; EDIT HERE: (BIN path only)
LOAD_ADDR       = $4000
; EDIT HERE: (BIN path only)
RUN_ADDR        = $6000

        .org SHIM_ADDRESS

start:
        sta $C006           ; INTCXROM off — slot $Cnxx visible
        ldx #$FF
        txs

        ; Close leftover FCBs from Bitsy Bye / prior runs.
        jsr MLI
        .byte CLOSE_CALL
        .word close_all

.if QUIT_TO_SYSTEM .eq 1
        ; --- Launch .SYSTEM (dispatcher-compatible) ---
        jsr MLI
        .byte OPEN_CALL
        .word sys_open
        bcs error

        lda sys_ref
        sta sys_read_ref
        sta sys_close_ref

        jsr MLI
        .byte READ_CALL
        .word sys_read
        bcs error

        jsr MLI
        .byte CLOSE_CALL
        .word sys_close
        bcs error

        ; $0280 = length-prefixed pathname of the .SYSTEM (loader strips .SYSTEM).
        lda system_name
        sta PATHNAME
        tax
copy_pn:
        lda system_name,x
        sta PATHNAME,x
        dex
        bne copy_pn

        jmp SYSTEM_LOAD

sys_open:
        .byte $03
        .word system_name
        .word IO_BUFFER
sys_ref:
        .byte $00

sys_read:
        .byte $04
sys_read_ref:
        .byte $00
        .word SYSTEM_LOAD
        .word $FFFF
        .word $0000

sys_close:
        .byte $01
sys_close_ref:
        .byte $00

; EDIT HERE: full pathname of the .SYSTEM to launch
system_name:
        .byte sys_end - sys_start
sys_start:
        .byte "/HOSTFS.S7D0/MMINER/MMINER.SYSTEM"
sys_end:

.else
        ; --- BIN open/read/JMP ---
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

; EDIT HERE: 
; If this file being launched is expecting a machine state - set that state here, now.

        jmp RUN_ADDR

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
        .word $FFFF
        .word $0000

close_param:
        .byte $01
close_ref:
        .byte $00

; EDIT HERE:
prefix_name:
        .byte pref_end - pref_start
pref_start:
        .byte "/HOSTFS.S7D0/MMINER"
pref_end:

; EDIT HERE:
file_name:
        .byte name_end - name_start
name_start:
        .byte "MMINER"
name_end:
.endif

error:
        jmp error

close_all:
        .byte $01
        .byte $00           ; ref_num 0 = close all
