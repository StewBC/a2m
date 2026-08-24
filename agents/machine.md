# Machine

`src/machine/` — Apple ][+ / //e Enhanced. No SDL. The runtime worker owns the
live `apple2_t`.

## Models

| Model | Notes |
|-------|--------|
| //e Enhanced (default) | `APPLE2_MODEL_IIE_ENHANCED`, 65C02, aux, 80-col hardware bits |
| ][+ | `APPLE2_MODEL_II_PLUS`; CLI `--model plus`; 6502 class |

Φ0 rate: `APPLE2_CPU_FREQUENCY_HZ` = **1020484.4**. CPU is `cpu65_*`
(microcycle beam path; atomic `cpu65_step` on the max path).

Always allocated: `ram_main` 128K (][+ uses the first 64K), `ram_lc` 32K
(16K LC × main/aux).

## Public API

`apple2.h`: init / shutdown / warm `apple2_reset` / `apple2_cold_reset`;
`apple2_set_model`; attach Disk II / SmartPort / Mockingboard in slots **1–7**
(one Mockingboard total); slot+device mount/eject/swap/writable; step cycle /
instruction / max-instruction; `apple2_read_in_view` / `write_in_view`;
gameport; paste; observers (`apple2_set_memory_access_callback`,
`apple2_set_cpu_observer`); `apple2_set_replay_sealed`.

Snapshots: `apple2_snapshot_*` — [`snapshots.md`](snapshots.md).

## Key files

| File | Role |
|------|------|
| `apple2.c` / `.h` | Core machine |
| `softswitch.c` / `.h` | `$C0xx`, banking, gameport softswitches |
| `cpu65*` | Microcycle 6502 / 65C02 |
| `video.c` / `.h` | Beam + paint — [`video.md`](video.md) |
| `diskii.c` / `image.c` | Disk II + images — [`disk.md`](disk.md) |
| `smrtprt.c` / `hostfs.c` | SmartPort + HostFS |
| `mboard.c` / `ay38910.c` / `via6522.c` | Mockingboard |
| `keyboard.c` | Host key → strobe / Open-Apple |
| `memview.h` | VIEW_FLAGS debug areas |
| `rom_data.c` | Embedded ROMs |

## Slots

`EMPTY | DISKII | SMARTPORT | MOCKINGBOARD`. Defaults: Disk II in **slot 6**,
Mockingboard in **slot 4**. Configure / `[Slots]` can put cards in 1–7.
Selecting a second Mockingboard clears the previous one.

## Banking / CXXX

Soft-switch flags in `softswitch.h` (`A2S_*`). Language card, 80STORE, aux,
CXXX slot map — `cxxx_map` tests are the contract.

- Empty-slot `$Cn` shadows are captured from RAM underlay **before** bank apply.
- `SETCXROM` (`$C007`): internal `$C100–$CFFF` ROM **hides** slot-card I/O
  (Mockingboard `$Cn` must not intercept while INTCXROM is on).
- `$C800` card latch: first I/O SELECT (`$Cnxx` read or write) until `$CFFF`.
  SmartPort claims like an expansion-ROM card; there is no ROM image, so the
  host trap *is* that mapping. Internal `$C3xx` sets a motherboard 80-col
  overlay (does not drop the card latch). INTCXROM overlays `$C100–$CFFF`
  and restores the card latch when cleared. `$CFFF` drops both latches.
  `SETC3ROM` (`$C00B`) is not I/O SELECT; the overlay stays until `$CFFF`
  (a2audit E000B).

`apple2_debug_read` / `write` skip softswitch side effects. Live bus is
`apple2_bus_*`. Host traps (SmartPort `$C800`) are **not** 6502 opcode fetches
and will not fire as execute breakpoints.

## Gameport

Axes 0..255, clamped max **254** so PTRIG bit7 can still clear. Buttons OR with
Open/Closed Apple on `$C061` / `$C062`.

## Memory views

Three independent VIEW_FLAGS fields (`memview.h`):

| Field | Choices |
|-------|---------|
| 48K | Map / Main / Aux |
| `$C100` | Map / ROM |
| `$D000–$FFFF` | Map / LC1 / LC2 / ROM |

Product UI cycles named presets. Control/BP INI tokens: `map`, `main`, `aux`,
`c100map`, `c100rom`, `d000map`, `lc1`, `lc2`, `rom`.

## Tests

`apple2_stub`, `softswitch`, `rom_boot`, `video_beam`, `diskii`, `peripherals`,
`cxxx_map`, `memview`, `cpu65_basic`, `apple2_snapshot`.
