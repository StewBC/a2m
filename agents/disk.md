# Disk II / SmartPort

## Disk II

| File | Role |
|------|------|
| `diskii.c` / `.h` | Controller, motor, stepper, Q6/Q7 |
| `image.c` / `.h` | NIB / DSK / WOZ |
| `diskii_rom.c` | 13/16-sector PROMs |

Default: Disk II in **slot 6**. Cards are supported in peripheral slots **1–7**.
Formats: `.nib` `.dsk` `.do` `.po` `.woz`.

```bash
./build/a2m --noini -d path.nib
./build/a2m --noini --disk s6d0=a.nib --disk s6d1=b.nib
# Multi-image queue on one drive (SWAP steps the queue):
./build/a2m --noini -d s6d0=disk1.nib -d s6d0=disk2.nib
```

Each drive has a multi-image queue in the machine (`DISKII_DRIVE.images`).  
Breakpoint **Swap** advances drive 0 on its selected slot (default 6; accepted
slot syntax is 0–7). Bare means next; `+N`/`-N` is relative and `N` is
absolute. If the selected slot does not contain Disk II, the runtime logs an
error and pauses.
The Machine UI addresses live media by slot+drive. Dirty floppy data is flushed
before swap, eject, snapshots, and emulator shutdown; a failed flush keeps the
image mounted.

Motor soft-switch drives **green disk LED** (level). Motor axis max for paddles
is unrelated; motor_on is boolean.

## SmartPort

| Path | Status |
|------|--------|
| ProDOS block `$C0s4` / `$C0s5` | **Works** (`sp_read` / `sp_write`) |
| Pure SmartPort `$C800` protocol | **Host trap** — `$Cn` latches SP slot; PC trap at `$C800` / `$C89B` / `$C9AA` dispatches STATUS / READ_BLOCK / WRITE_BLOCK (`sp_host_trap`) |

```bash
./build/a2m --noini --hd s7d0=volume.po
```

Default SP **slot 7**.

The Machine UI supports live SmartPort insert/eject for both devices on any
configured SmartPort card. SmartPort does not use a multi-image queue.
Live insert/eject updates the options used for Save INI / quit; Configure does
not own media mounts (Misc → Machine does), so Apply / Save INI keeps the live
Disk II / SmartPort paths rather than a stale Configure snapshot.
SmartPort **[Insert]** can **Open** an image file or **Use This Folder** for
HostFS; Disk II Insert remains file-only.
The INI-only `[SmartPort] boot_slot=N` setting redirects initial execution to `$Cn00`
after unit 0 mounts, enabling Apple ][+ and non-slot-7 SmartPort boot. Invalid
card or missing-media selections publish a runtime error and keep normal startup.

## Empirical

| Media | Result |
|-------|--------|
| DOS 3.3 fixture NIB | Boots (ctest + manual) |
| a2audit | Softswitch / CXXX pass (manual + `cxxx_map`); re-check after banking/slot changes |
| ProDOS / Total Replay HD | Manual; needs paint for many titles |

## Gaps

Slot configuration UI and broader write fidelity remain open.
SP trap: min cmds only (STATUS/READ/WRITE); no full DIB / extended SP.

**HostFS** (mount a host folder as a ProDOS SmartPort volume):
Phase 0–3 + 5a/5b done — directory path → NAPS volume with host subdirs as
ProDOS folders, live host refresh, file + directory write-through, optional
`hostfs.order`; see [`hostfs.md`](hostfs.md).

```bash
./build/a2m --noini --smart s7d0=./tests/fixtures/hostfs
```

## Tests

`diskii` (incl. multi-image swap), `app_options_mounts` (queue append),  
`peripherals` (SmartPort softswitch ports + `$C800` host trap).
