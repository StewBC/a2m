# Assembler MLI launch sample

ProDOS shim that **SET_PREFIX**es a HostFS volume, then **OPEN / READ / CLOSE** a
BIN and **JMP**s to it.

## Requirements

- ProDOS already running (e.g. HostFS booted to Bitsy Bye)
- MLI at `$BF00` (`$4C`)

## Assembler tab

1. File → `samples/asm_mli_launch/mli_launch.s`
2. **Assemble at** / **Auto-run at** `$3000` (keep the shim away from the loaded program)
3. **MLI launch** on, **Reset** off
4. Assemble

## Edit before use

| Symbol | Meaning |
|--------|---------|
| `QUIT_TO_SYSTEM` | 1 for .system files, 0 for binaries |
| `prefix_name` | HostFS volume, e.g. `/HOSTFS.S7D0` |
| `file_name` | BIN to open |
| `LOAD_ADDR` / `RUN_ADDR` | Where to read / where to jump |
| `IO_BUFFER` | 1K page-aligned, must not overlap the load range |

Always `STA $C006` before disk MLI calls (slot CXROM).

## mminer worked example

The sample will use ../mminer's code files and assemble its output
into `samples/hostfs/mminer`.

With `samples/hostfs` mounted as SmartPort `S7D0`, set `Auto-run at` to
`$3000` and enable `MLI launch`.

Then, with `samples/mminer/mminer-a2m.asm` selected in the assembly window,
pressing `Assemble` will assemble and launch one of the following:

- `samples/hostfs/mminer/mminer.system#FF2000`
- `samples/hostfs/mminer/mminer#064000`

Which file is launched depends on the `QUIT_TO_SYSTEM` setting.