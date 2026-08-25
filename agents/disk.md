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

There is no `$C800` expansion firmware in tree. SmartPort `$Csxx` claims the
card C800 latch when free. While that latch is held and 80-col is not
overlaid (no CXROM / no `$C3xx` overlay), PC at `$C800` / `$C89B` / `$C9AA`
is the missing ROM: `sp_host_trap` dispatches **STATUS / READ_BLOCK /
WRITE_BLOCK**. Other commands return `$27`. The trap is not a 6502 opcode
fetch.

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
ProDOS folders, event-driven refresh on the next SmartPort touch, file + directory
write-through, optional per-directory `hostfs.order`.

Durable invariants:

- The volume directory is exactly four blocks (2-5), with at most 51 entries.
  Subdirectories grow as needed within the 65535-block volume ceiling.
- Nodes live in a growable flat table capped at 65535. Cross-references are node
  indices, never pointers. Basenames live in an arena and nodes retain offsets, so
  table or arena growth cannot invalidate references. Rebuild host paths by walking
  parents, with `HOSTFS_MAX_DEPTH` as the hard bound.
- Colliding mangled ProDOS names receive deterministic `001`-`999` aliases while
  preserving the final extension when possible. Host basenames never change merely
  to match an alias. Root, node, depth, and alias drops always warn on stderr.
- Arm filesystem watching before scanning. Watcher threads only enqueue path
  invalidations and never mutate HostFS state. The machine owner verifies a file or
  reconciles the affected parent on the next unit touch.
- Event loss requests a full rescan; unavailable or incomplete watch coverage uses
  the periodic fallback. Guest write-through and sealed replay defer reconciliation.
- `hostfs.order` contains host basenames. Never rewrite the root manifest while the
  root catalog is truncated, because doing so would discard ordering for hidden
  entries.

SmartPort Insert can **Open** an image or **Use This Folder**. CLI/INI: path
kind selects HostFS vs image.

```bash
./build/a2m --noini --smart s7d0=./tests/fixtures/hostfs
```

Operator details: `manual/manual.md` HostFS section.

## Tests

`diskii` (incl. multi-image swap), `app_options_mounts` (queue append),
`peripherals` (SmartPort ports + `$C800` trap), `hostfs` (large directories,
deep-tree/node-ceiling stress, watcher targeting, aliases),
`runtime_smartport_boot`, `runtime_slot_resolve`.
