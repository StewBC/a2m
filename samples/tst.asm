.repeat $1, counter
    .byte counter
.endrepeat

.repeat $2, counter
    .byte counter
.endrepeat

counter = 0

.repeat counter
    .byte counter
.endrepeat

counter = 10

.repeat counter, counter
    .byte counter
.endrepeat
