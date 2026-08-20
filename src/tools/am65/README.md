# am65

`am65` is the assembler used by a2m and by the standalone `am65` command-line
program. It is designed to become the shared source of truth for c64m as well.
The directory is self-contained so it can be maintained as a Git subtree in
each consumer.

The initial CPU profile is NMOS 6502. It can be selected through the library
API (`assembler_set_cpu_profile`), with `am65 --cpu`, or changed within source:

| Directive | Accepted instructions |
|---|---|
| `.6502` | Portable documented NMOS 6502 set |
| `.65c02` | Core WDC/Rockwell-compatible 65C02 additions |
| `.rockwell` | 65C02 plus RMB/SMB and BBR/BBS bit operations |
| `.wdc` | Rockwell profile plus WAI and STP |

Profiles are cumulative. Selecting a profile establishes the initial state for
each assembly; an in-source directive affects subsequent lines. This lets c64m
and Apple ][+ select 6502, Apple //e Enhanced select 65C02, and standalone users
opt into Rockwell or WDC instructions explicitly.

## Named scopes and output targets

Named scopes provide namespaces, and symbols may be referenced with `::`:

```asm
.scope game
main:
    rts
.endscope

.word game::main
```

A named scope becomes a separate output target when it has `file=` or `dest=`:

```asm
.scope game file="game.bin" dest="map"
    .org $6000
    ; ...
.endscope
```

The assembler core passes both attributes to its host. Standalone `am65` uses
`file=` to create a binary and accepts but ignores `dest=`. Emulator hosts
advertise and validate their own destination names. In a2m the attributes are
orthogonal: `dest=` writes machine memory, `file=` writes a host file beside the
source, and both together do both. A `file=`-only scope does not poke memory.
This keeps machine banking out of the shared assembler.

Standalone `am65` predefines `AM65=1` and no machine symbol. Emulator hosts
predefine `AM65=0` plus their machine symbol, currently `APPLE2=1` in a2m and
`C64=1` in c64m.

Regenerate `gperf.c` after changing `gperf.gperf`:

```sh
gperf --language=ANSI-C -c --output-file=gperf.c gperf.gperf
```
