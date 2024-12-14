.macro a b c
    lda b
.if .defined c
    lda c
.endif
.endmacro

a 1, 2
a 3

.if 0
    lda 4
.endif
