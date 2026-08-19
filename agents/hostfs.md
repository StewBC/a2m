# HostFS — SmartPort folder volume

**Status:** Phase 0–3 done (read/write NAPS HostFS + live host refresh). Phase 4 optional.  
**Related:** [`disk.md`](disk.md) · [`machine.md`](machine.md) · [`rules.md`](rules.md) · [`testing.md`](testing.md).

HostFS mounts a **host directory** as a ProDOS 8 volume on an existing
**SmartPort** card unit. It is not a new slot protocol. Guest I/O stays
STATUS / READ_BLOCK / WRITE_BLOCK; only the media backend changes
(image file vs folder).

Apple II / ProDOS 8 only. No GS/OS forks or extended files.

---

## Goal (acceptance)

With a SmartPort card in a slot and unit 0 pointed at a folder that contains a
correctly tagged ProDOS system file, a2m boots ProDOS from that folder — no
`.po` / `.hdv` image required for that unit.

Illustrative success command (flag spelling may match existing `--hd` /
`--smart`; do not bikeshed in implementation PRs):

```bash
./build/a2m --noini --smart s7d0=./hostfs/d0
```

Folder contains at least:

```text
PRODOS#FF0000
```

NAPS form: `NAME#ttxxxx` — two hex digits **file type**, four hex digits
**aux type**. ProDOS 8 system file is type `$FF` (SYS), aux `$0000` →
`PRODOS#FF0000`.

Optional but useful later: `BASIC.SYSTEM#FF2000` (aux `$2000` is the usual
BASIC.SYSTEM aux). Phase 1 boot success is **ProDOS starting from HostFS**,
not a full BASIC prompt.

**Out of scope for this epic’s tooling:** creating/seeding that folder from
inside a2m UI. Operators prepare the directory by hand (copy tagged files).

---

## Decisions (locked)

| Question | Answer |
|----------|--------|
| Card type | Still **SmartPort** (`slotN = smartport`). HostFS = media kind. |
| How to select HostFS | Mount path is a **directory** → HostFS backend; file → existing image backend. |
| Mixed media on one card | **Yes**, per unit (`s7d0=./host/d0`, `s7d1=disk.po`). |
| Phase 1 I/O | **Read-only** guest view of host files (writes → error or no-op with protect). |
| Phase 1 layout | **Flat root only**; skip host subdirectories. |
| Phase 1 metadata | **NAPS only**; non-NAPS names are **skipped** (not mounted). |
| Capacity | Advertise a large ProDOS volume (target **65535** blocks, ~32 MB). |
| Block identity | **Stable** file→block assignment across rescans when possible. Never recycle overlapping block numbers. |
| Data storage | Metadata (boot/dir/bitmap/index) in RAM; file **data** blocks map to host file offsets (optional small LRU cache). Not a full 32 MB RAM disk. |
| Live host edits | Phase 2+. Prefer in-place content refresh; avoid full renumber rebuilds. |
| ProDOS→host write-back | Phase 3+. |

---

## Architecture

```text
ProDOS / SmartPort call
  → existing Cn ROM + sp_host_trap (unchanged protocol)
  → sp_status / sp_read / sp_write
       ├─ unit backend == image  → today’s UTIL_FILE fseek/fread path
       └─ unit backend == hostfs → hostfs_*_block (new)
```

Suggested files (names flexible; keep under `src/machine/`):

| File | Role |
|------|------|
| `hostfs.h` / `hostfs.c` | Volume object, mount/eject, block R/W, rescan hooks |
| `hostfs_prodos.c` (or section in `hostfs.c`) | Dir / bitmap / seedling-sapling-tree builders |
| `smrtprt.c` | Per-unit backend dispatch; `sp_mount` detects directory |
| `app_options.c` | Allow directory paths in SmartPort mount specs |
| Tests under `tests/machine/` | Map builder + block read; optional boot smoke |

Dependency rule: HostFS stays in **machine** (+ `platform_fs` / `util` as needed).
No SDL/frontend includes from machine ([`rules.md`](rules.md)).

### Per-unit state (sketch)

```text
SP unit
  kind: image | hostfs
  image: UTIL_FILE + header_size          (existing)
  hostfs:
    root_path
    volume_name, total_blocks
    dir blocks[], bitmap[]
    file table[]:
      prodos_name, type, aux, eof
      key_block, storage_type
      host_path
      data_extents[] (block → host offset)  or equivalent
    generation / mtime bookkeeping (phase 2+)
```

### ProDOS map rules (Phase 1)

1. Scan mount directory (non-recursive).  
2. Parse NAPS (`#` + 6 hex digits). Invalid / non-NAPS → skip.  
3. ProDOS name = stem uppercased/truncated to 15 legal chars (`A–Z`, `0–9`, `.`).  
4. Size volume directory to fit file count (fixed for this mount).  
5. Place bitmap after directory; mark used blocks.  
6. For each file: allocate key (+ index if sapling/tree) with **unique** blocks;
   data blocks read through from host file (strip nothing — NAPS is
   filename-only metadata; file bytes are raw ProDOS payload).  
7. Block 0: boot loader that can find SYS file `PRODOS` (use a known-good
   ProDOS boot block template from an existing image or documented bytes;
   block 1 zeroed).  
8. STATUS reports total_blocks; READ_BLOCK serves map or host data;
   WRITE_BLOCK fails read-only (Phase 1).

Storage types: seedling ≤512, sapling ≤128KiB, tree above — standard ProDOS 8.

---

## Phases

Agents should implement **one phase per focused change series**, stop at the
phase exit gate, and leave later phases untouched unless the task says
otherwise.

### Phase 0 — Mount plumbing

**Intent:** Directory paths can be mounted as SmartPort units without yet
serving a real ProDOS map.

- Detect `platform_fs_is_dir(path)` (or equivalent) in mount path.  
- Extend `SP_DEVICE` / mount structs with per-unit backend kind.  
- `sp_mount`: if directory, attach HostFS stub; if file, existing image path.  
- Eject / shutdown / flush ignore or no-op HostFS cleanly.  
- Mixed image+HostFS on same slot works at the mount layer.  
- CLI/INI: existing `[SmartPort] sNdM=path` and `--hd`/`--smart` accept dirs
  (reconcile still requires `slotN=smartport`).

**Exit:** Mounting `s7d0=./some/dir` does not error; STATUS may still be empty
or fixed stub size; image mounts regress-free (`peripherals`,
`app_options_mounts`, `runtime_smartport_boot` as applicable).

### Phase 1 — Read-only volume + boot  ★ first product milestone

**Intent:** Folder of NAPS files becomes a bootable ProDOS volume.

- NAPS parser; skip non-NAPS and subdirs.  
- Build in-memory ProDOS structures (boot, dir, bitmap, indexes).  
- `hostfs_read_block` / STATUS wired through `sp_*`.  
- WRITE → `$2B` write-protect or `$27` I/O error (pick one; document;
  prefer write-protect).  
- Stable allocation within a mount session.  
- Manual/fixture folder with `PRODOS#FF0000` (+ optional extras).  
- ctest: unit tests for NAPS parse, seedling/sapling map, known block
  contents; manual or scripted boot smoke if automation is heavy.

**Exit (must):**

```bash
./build/a2m --noini --smart s7d0=<folder-with-PRODOS#FF0000>
```

boots ProDOS from HostFS (same class of success as booting a ProDOS `.po` on
SmartPort today). Catalog of the volume shows NAPS-derived files with correct
type/aux. Non-NAPS host files do not appear.

**Do not** implement live rescan or write-back in this phase.

### Phase 2 — Host → volume live refresh

**Intent:** Assembler/external edits to files already on the volume become
visible without remount, when safe.

- Periodic mtime/size poll of mounted root (main or runtime tick; keep
  machine free of SDL). Native FSEvents/inotify optional later.  
- Content change, same storage shape → rewrite data; patch EOF/blocks_used.  
- Growth needing new indexes → reallocate **that file only**; keep other
  files’ block numbers.  
- Add/delete/rename → patch directory + bitmap; log that reboot may be needed.  
- Avoid full-volume renumber on the hot path.  
- Explicit remount/rescan path for recovery.

**Exit:** Change a tagged host file’s bytes, reload via ProDOS (`BLOAD` /
rerun SYS); new contents observed without ejecting the unit. Document
failure cases (open file + storage-type change, etc.).

### Phase 3 — Volume → host write-through

**Intent:** ProDOS writes update the folder.

- Data block WRITE → `pwrite` host file.  
- CREATE → create host file with NAPS name from type/aux.  
- DESTROY → remove or side-away host file.  
- RENAME → rename host file; preserve `#ttxxxx`.  
- Directory/bitmap/index remain authoritative in RAM.  
- Flush policy on eject/shutdown/snapshot boundaries (align with Disk II
  dirty-flush discipline where applicable).

**Exit:** `BSAVE` / create/delete from ProDOS visibly changes the host
directory; remount or rescan still consistent.

### Phase 4 — Product polish (optional)

- UI: browse/insert **folder** as SmartPort media (not only files).  
- Optional “seed ProDOS into folder” helper (explicitly deferred from Phase 1).  
- Native directory watchers instead of poll.  
- Snapshot story: reference folder path like external media refs.  
- Cross-links in manual + [`disk.md`](disk.md) empirical table.  
- Volume name, size overrides, read-only flag in INI.

---

## Non-goals (explicit)

- Disk II nibble-level “host floppy.”  
- Overlapping/recycled ProDOS block numbers to fake capacity.  
- GS/OS / forked files.  
- Host subdirectories as ProDOS dirs in Phase 1–2 (revisit later).  
- AppleSingle intake in Phase 1 (NAPS only; AppleSingle may join Phase 4+).  
- Replacing image-backed SmartPort.  
- MLI call interception as the primary design (block device only).

---

## Test plan (minimum)

| Layer | What |
|-------|------|
| Unit | NAPS parse / reject; name mangling; seedling & sapling layouts; bitmap used bits |
| Unit | `read_block(0)` boot signature; dir block 2 has volume header + `PRODOS` entry |
| Unit | Mixed card: HostFS unit 0 + image unit 1 mount/eject |
| Regression | Existing SmartPort image tests unchanged |
| Manual | Boot `PRODOS#FF0000` folder; `CAT` shows expected files; BRUN/BLOAD a small SYS/BIN |

Prefer deterministic fixtures under `tests/fixtures/hostfs/` (tiny files +
checked-in or generated boot/PRODOS bytes — do not require a 32 MB file).

---

## Implementation order checklist (Phase 1 agent)

1. Read this doc + [`disk.md`](disk.md) SmartPort section + `smrtprt.c` mount/read/status.  
2. Phase 0 mount kind + directory accept; prove image path unbroken.  
3. NAPS scan + ProDOS map builder (no SP wire yet if easier to test).  
4. Wire STATUS/READ; WRITE protect.  
5. Boot block + real `PRODOS#FF0000` fixture; manual boot.  
6. ctest for map/NAPS; update [`disk.md`](disk.md) / this doc status when done.  
7. **Stop.** Do not start Phase 2 unless asked.

---

## Choices made

| Choice | Decision |
|--------|----------|
| `total_blocks` | **65535** |
| Volume name | **`HOSTFS.SNdM`** (e.g. `HOSTFS.S7D0`) so `/PREFIX` is unique per unit |
| Phase 1 Guest WRITE | Was **`$2B` write-protect**; superseded by Phase 3 write-through |
| Boot block 0 | Embedded from **`disks/dos.po`** (HD/SmartPort loader) and embedded in `hostfs_boot.h`. *Not* `ProDOS_2_4_2.po` — that floppy boot uses Disk II and cannot boot a SmartPort unit. |
| `PRODOS#FF0000` fixture | Extracted SYS payload from `disks/ProDOS_2_4_2.po` into `tests/fixtures/hostfs/` |
| Phase 2 poll | ~1e6 Φ0 via `apple2_peripherals_step` → `hostfs_poll`; suppressed while guest write-through is active |
| Phase 3 CREATE name | Always host `NAME#ttxxxx`; if the name is already NAPS-tagged (assembler), observe the stem and do not double-tag; reuse existing host file with matching stem |
| Phase 3 DESTROY | **`unlink`** host file |
| Phase 3 EOF shrink | **Truncate** host file |
| Phase 3 catalog sync | Dir-diff reconcile after directory block WRITEs |
| Catalog order | Optional `hostfs.order` in the folder (one NAPS basename per line). Mount applies listed order then appends unlisted NAPS files (alpha). Guest reorder (e.g. CAT.DOCTOR) or rescan composition change rewrites the file. Missing entries skipped. |

---

## Status log

| Date | Note |
|------|------|
| 2026-08-19 | Plan written; implementation not started. |
| 2026-08-19 | Phase 0 + Phase 1 implemented: `hostfs.c`, SmartPort backend dispatch, fixtures, `ctest` `hostfs`. |
| 2026-08-19 | Phase 2 + Phase 3: live host rescan/poll, write-through, dir-diff CREATE/DESTROY/RENAME. |
| 2026-08-19 | Optional `hostfs.order` manifest preserves ProDOS catalog order across remount. |
