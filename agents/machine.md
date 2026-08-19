# Machine

## Scope

`src/machine/` — Apple ][+ / //e Enhanced. No SDL. Runtime thread owns the live
instance.

## Public API (sketch)

Init/shutdown/reset/cold_reset; model; attach Disk II / SmartPort / Mockingboard
in slots 1–7 (one Mockingboard total); slot+device mount/eject/swap and media
flush; step cycle; read/write views;
gameport set axes/buttons; keyboard paste helpers.

## Key files

| File | Role |
|------|------|
| `apple2.c` / `.h` | Core machine |
| `softswitch.c` | `$C0xx`, banking, gameport softswitches |
| `cpu65*` | Microcycle 6502 / 65C02 class |
| `video.c` | Beam + paint |
| `diskii.c` / `image.c` | Disk II + images |
| `smrtprt.c` | SmartPort block I/O |
| `mockingboard*` / `ay38910*` | MB |
| `keyboard.c` | Host key → strobe / solid-apple |

## Models

| Model | Notes |
|-------|--------|
| //e Enhanced (default) | 65C02 class, aux, 80-col hardware bits |
| ][+ | 6502 class; product `--model plus` |

## Banking

Soft-switch flags in `softswitch.h` (`A2S_*`). Language card, 80STORE, aux,
CXXX slot map — see `cxxx_map` tests.

CXXX / a2audit E000B (landed):

- Empty-slot `$Cn` shadows captured from RAM underlay **before** bank apply.
- `SETCXROM` (`$C007`): internal `$C100–$CFFF` ROM **hides** slot-card I/O
  (Mockingboard `$Cn` must not intercept while INTCXROM is on).
- Internal `$C800` latch after `$C3xx` select stays until `$CFFF`, including
  after `SETC3ROM` (`$C00B`) — matches original a2m `io_apply_c800_latch`.

## Gameport

Axes 0..255 (clamped max **254** so PTRIG bit7 can clear). Buttons OR with
Open/Closed Apple on `$C061`/`$C062`.

## Gaps

Snapshots: [`snapshots.md`](snapshots.md)
(`apple2_snapshot_*`, `.a2state`).

## Tests

`apple2_stub`, `softswitch`, `rom_boot`, `video_beam`, `diskii`, `peripherals`,
`cxxx_map`, `memview`, `cpu65_basic`.
