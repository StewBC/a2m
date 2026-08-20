# Assembler MLI launch sample

Small ProDOS shim that **SET_PREFIX**es a HostFS volume, then **OPEN / READ / CLOSE** a BIN and **JMP**s to it.

## Requirements

- ProDOS already running (for example HostFS booted to Bitsy Bye)
- MLI present at `$BF00` (CPU-visible byte `$4C`)

## Assembler tab settings

1. **File Name** → `samples/asm_mli_launch/mli_launch.s`
2. **Assemble at** `$2000`
3. **Auto-run at** `$2000`
4. Check **MLI launch**
5. Leave **Reset machine** unchecked (mutually exclusive with MLI launch)
6. **Assemble** (or **Shift+Opt+A**)

Assemble always writes RAM. **MLI launch** only gates the post-success auto-run: if `$BF00 ≠ $4C`, assembly still succeeds and a notice explains that auto-run was skipped.

## Edit before use

In `mli_launch.s`:

| Placeholder | Default | Meaning |
|-------------|---------|---------|
| `prefix_name` | `/HOSTFS.S7D0` | HostFS volume `HOSTFS.SsDn` (slot 1–7, drive 0/1) |
| `file_name` | `MYPROG` | BIN to open (partial name uses the prefix) |
| `LOAD_ADDR` / `RUN_ADDR` | `$4000` | Where to read the BIN and where to jump |

HostFS names always look like `HOSTFS.S7D0` unless you rename the volume.

## Multi-file projects

`file=` scopes can write HostFS outputs while `dest="main"` (or map) places this shim in Apple RAM in one assemble. HostFS is not required for the shim itself to assemble.

## Alternate launch

Comments in `mli_launch.s` show a **QUIT** + `.SYSTEM` pathname path if you prefer that over BIN open/read/JMP.
