# Disk II / SmartPort / HostFS

## Disk II

| File | Role |
|------|------|
| `diskii.c` / `.h` | Controller, motor, stepper, Q6/Q7 |
| `image.c` / `.h` | NIB / DSK / WOZ |
| `diskii_rom.c` | 13/16-sector PROMs |

Default card: **slot 6**. Cards are supported in peripheral slots **1–7**.
Two drives per controller. Each drive has a **multi-image queue**
(`DISKII_DRIVE.images`).

Formats: `.nib` `.dsk` `.do` `.po` (exactly 143360 bytes → Disk II) `.woz`.

| Kind | Write-back |
|------|------------|
| `.dsk` / `.do` / `.po` (143360) | Internal NIB; decode dirty tracks to the sector file |
| `.nib` | Raw NIB tracks |
| `.woz` | **Read-only** (writes fail) |

```bash
./build/a2m --noini -d path.nib
./build/a2m --noini --disk s6d0=a.nib --disk s6d1=b.nib
# Multi-image queue on one drive (BP Swap / Machine Swap steps it):
./build/a2m --noini -d s6d0=disk1.nib -d s6d0=disk2.nib
```

The Machine UI addresses live media by slot+drive. Dirty floppy data is
flushed before swap, eject, snapshots, and shutdown; a failed flush keeps the
image mounted.

Breakpoint **Swap** advances drive 0 on its selected slot (default 6; accepted
slot syntax is 0–7). Bare means next; `+N`/`-N` is relative; `N` is absolute
1-based. A slot without Disk II logs an error and pauses.

Motor soft-switch drives the **green disk LED** (level). Write activity is the
red LED when wired.

## SmartPort

Default card: **slot 7**. Two units. Softswitches `$C0s4` data / `$C0s5` status
(`sp_read` / `sp_write`).

There is no `$C800` expansion firmware in tree. SmartPort `$Csxx` does not
take the C800 map. `sp_host_trap` fires at `$C800` / `$C89B` / `$C9AA` when
the call is SmartPort: slot-ROM dispatch (last I/O SELECT was an SP `$Csxx`)
or `JSR` to that entry with inline cmd. 80-col firmware
`JMP $C800` from `$C3xx` is not trapped. Dispatch is **STATUS / READ_BLOCK /
WRITE_BLOCK** only; other commands return `$27`. The trap is not a 6502
opcode fetch.

```bash
./build/a2m --noini --hd s7d0=volume.po
```

`.po` that is **not** 143360 bytes is a SmartPort image. `.hdv` / `.2mg`
(optional `"2IMG"` header) likewise. Live insert/eject for both devices; no
multi-image queue.

INI `[SmartPort] boot_slot=N` redirects initial execution to `$Cn00` after
unit 0 mounts (Apple ][+ and non-slot-7 SmartPort boot). Invalid card or
missing media publishes a runtime error and keeps normal startup.

Live insert/eject updates the options used for Save INI / quit. Configure does
not own media mounts (Misc → Machine does).

## HostFS

A host **directory** mounted on a SmartPort unit becomes a ProDOS volume
(~32 MB, `HOSTFS_TOTAL_BLOCKS=65535`): NAPS `NAME#ttxxxx`, nested host dirs as
ProDOS folders, access-triggered host refresh (~1s wall-clock, skipped during
guest write-through and sealed replay), file + directory write-through,
optional `hostfs.order`.

SmartPort Insert can **Open** an image or **Use This Folder**. CLI/INI: path
kind selects HostFS vs image.

```bash
./build/a2m --noini --smart s7d0=./tests/fixtures/hostfs
```

Operator details: `manual/manual.md` HostFS section.

## Tests

`diskii` (incl. multi-image swap), `app_options_mounts` (queue append),
`peripherals` (SmartPort ports + `$C800` trap), `hostfs`,
`runtime_smartport_boot`, `runtime_slot_resolve`.
