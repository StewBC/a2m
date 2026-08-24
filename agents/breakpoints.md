# Breakpoints

Runtime-owned table, capacity **64** (`src/runtime/runtime_thread.c`,
`runtime_event.h`). UI never touches live `apple2_t`. One list in live and
time travel.

Keep the Apple page-map memory model (`VIEW_FLAGS`). Do not replace it with a
generic bus graph.

## Access and actions

| Access | How it matches |
|--------|----------------|
| Execute | CPU fetch; range wrap; counter; condition |
| Read / Write | `apple2_set_memory_access_callback` when any R/W BP is armed; hit-pending pauses after the access |

| Action | Meaning |
|--------|---------|
| BREAK | Pause |
| FAST / SLOW | Turbo max / 1 MHz |
| TRON / TROFF | Instruction log file (`tron=` path, default `trace.log`) |
| TYPE | Inject type-script (not clipboard paste) |
| SWAP | Disk II multi-image queue step |

Host traps (SmartPort `$C800`) are not opcode fetches and will not fire as
execute BPs.

## Mapping

Same composite VIEW_FLAGS as the Memory window. Product radios:
**RAM Map/Main/Aux**, **C100 Map/ROM**, **D000 Map/LC1/LC2/ROM**.

INI / control tokens: `map`, `main`, `aux`, `c100map`, `c100rom`, `d000map`,
`lc1`, `lc2`, `rom` (`ram` accepted as legacy → map).

Control wire axes: `ram=map|main|aux`, `c100=map|rom`, `d000=map|lc1|lc2|rom`.

## Conditions

AND of ≤4 terms after address/access/mapping already matched
(`runtime_breakpoint_condition.*`). No OR, no grouping — arm two breakpoints
instead.

LHS: A/X/Y/SP/P, flags N/V/B/D/I/Z/C, `value` (access byte; illegal with
exec-only), `mem(addr)`, `raster` (beam line), `cycle_in_line`.
Ops: `== != < > <= >=` and mask set/clear. Empty = unguarded.

## TYPE script (BP Type field only)

| Form | Meaning |
|------|---------|
| plain text / newlines | `$C000` keys (Return for NL) |
| `\[OA]` `\[OA+]` `\[OA-]` | Open-Apple pulse / hold / release (`CA` same) |
| `\[B0]` `\[B1]` (+/-) | Gameport buttons |
| `\[J1X=n]` `\[J1Y=n]` `\[J2…]` | Axes 0..255 (128 center) |
| `\[J1XL]` `\[J1XR]` `\[J1YU]` `\[J1YD]` `\[J1XC]` `\[J1YC]` | Extremes / center one axis |
| `\[J1C]` `\[J2C]` | Both axes → 128 |
| `\[RESET]` `\[COLDRESET]` | Warm / cold reset |
| `\[W:N]` | Wait N units (~10 ms each at 1 MHz) |

Clipboard **Opt+Insert** remains plain `apple2_paste_begin` (no escapes).
Paste/TYPE do not change turbo.

## SWAP

Each Disk II drive has a multi-image queue. Repeat CLI `-d s6d0=a.nib -d s6d0=b.nib`
to append. Action steps **drive 0** on the selected slot (default 6; 0–7
accepted). Bare = next; `+N`/`-N` relative; `N` absolute 1-based. No Disk II
on that slot → error and pause. Event: `RUNTIME_EVENT_DISK_SWAP`.

## INI

`[DEBUG] break.*` load at worker start when `use_ini`; save on quit with
`--saveini` / `--remember` (not `--nosaveini`).

```text
break.E000       = execute,map,break
break.C000-C001  = write,aux,break,swap-slot=5,swap=+1
break.0801       = execute,map,break,fast
```

CLI `--break` / `-b` arms a simple execute BP.

## Files / tests

`runtime_thread.c` (match + commands), `runtime_breakpoint_condition.*`,
`runtime_breakpoint_ini.*`, `src/control/control_breakpoint.c`, host intents
in `main.c` / `frontend.c`.

Tests: `runtime_breakpoint`, `runtime_breakpoint_ini`, `apple_type_script`,
`runtime_inspector_bp`.
