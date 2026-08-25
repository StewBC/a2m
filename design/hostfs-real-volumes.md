# HostFS for real ProDOS volumes

| Field | Value |
|-------|-------|
| **Author** | swessels |
| **Date** | 2026-08-24 |
| **Status** | Draft |
| **Canonical path** | [`design/hostfs-real-volumes.md`](hostfs-real-volumes.md) |

---

## Overview

HostFS today cannot mount an arbitrary real-world ProDOS volume. `HOSTFS_MAX_FILES`
is a hard **256-node budget for the entire volume** — files and directories, every
depth — and the mount scan silently stops when it runs out. A folder of 256 tunes
plus `PRODOS` plus one subdirectory is 258 nodes, so the scan drops whatever sorts
last and the volume comes up missing files with no diagnostic.

This design replaces the **storage, scan, and refresh layers** of
`src/machine/hostfs.c` so a HostFS folder can represent any volume ProDOS itself can
represent: 51 entries in the volume directory (the architectural ProDOS limit),
effectively unbounded entries per subdirectory, and up to the ~65535-block ceiling of
a 32 MB volume — at a worst case cost of roughly **4.75 MB** rather than the 68 MB the
current node layout would demand at that scale.

Refresh is part of the mission, not an optimization on top of it. The once-per-second
rescan currently walks every file at every depth; at 60,000 nodes that is 60,000
`stat` calls per second under guest I/O. Lifting the node cap without fixing it yields
a volume that mounts and then crawls, so the directory-mtime skip is a required phase.
The target is **usable**, not merely enumerable.

The reconciliation layer (guest-side create / rename / delete / cross-directory move
propagating back to real host files) is **preserved**, not rewritten.

---

## Background & Motivation

### Current state

| Element | Where | Today |
|---------|-------|-------|
| Node budget | [`hostfs.h:12`](../src/machine/hostfs.h) | `#define HOSTFS_MAX_FILES 256` — whole volume, all depths |
| Node table | [`hostfs.c:106`](../src/machine/hostfs.c) | `hostfs_file files[HOSTFS_MAX_FILES]` — fixed, in-struct |
| Node size | measured | `sizeof(hostfs_file)` = **1088 B**; `host_path[1024]` is 94% of it |
| Volume struct | measured | `sizeof(hostfs_volume)` = **345200 B (337 KB)**, allocated whether the folder holds 3 files or 256 |
| Block map | [`hostfs.c:109`](../src/machine/hostfs.c) | `map` / `map_count` / `map_cap` — **already dynamic** |
| Mount scan | [`hostfs_scan_dir_recursive`](../src/machine/hostfs.c) | Depth-first; `if (vol->file_slots >= HOSTFS_MAX_FILES) break;` — **silent** |
| Scan buffers | [`hostfs.c:1824`](../src/machine/hostfs.c), [`2937`](../src/machine/hostfs.c) | `calloc(HOSTFS_MAX_FILES, sizeof(hostfs_scan_ent))`, 1320 B/entry, **per recursion level** |
| Reconcile buffers | [`hostfs.c:2350`](../src/machine/hostfs.c), [`2355`](../src/machine/hostfs.c) | `dent ents[HOSTFS_MAX_FILES]` and `bool seen[…]` — **on the stack** |
| Other fixed arrays | [`1621`](../src/machine/hostfs.c), [`3021`](../src/machine/hostfs.c), [`123`](../src/machine/hostfs.c) | `used[]`, `matched[]` (stack); `order_basenames[MAX][256]` (64 KB, root-only in practice) |
| Volume directory | [`hostfs_build_volume_directory`](../src/machine/hostfs.c), [`1963`](../src/machine/hostfs.c) | Grown to fit via `hostfs_dir_blocks_for_files(root_children)` — chains blocks 2, 3, 4, 5, 6… |
| Name collisions | [`hostfs_scan_dir_recursive`](../src/machine/hostfs.c) | `hostfs_active_name_exists` → `continue` — **silently dropped** |
| Public API | [`hostfs.h`](../src/machine/hostfs.h) | 16 functions; the real contract is `hostfs_read_block` / `hostfs_write_block` + mount/eject/flush/refresh/rescan |
| Tests | [`tests/machine/test_hostfs.c`](../tests/machine/test_hostfs.c) | 1212 lines, 10 cases, covering NAPS naming, volume map, write-through, reconcile, order manifest, nested dirs, nested rescan, dir write-through, touch refresh |

### The observed failure

`samples/hostfs/pt3plr/` holds `PRODOS#ff0000`, `pt3plr.system#ff2000`, and `PT3/`
with 256 tunes — 258 nodes. Root scan order is case-insensitive, so the scan
descends into `PT3` (slot 1), consumes slots 2–255 on tunes, hits the cap, and
`break`s before ever reaching `pt3plr.system`. The player is absent from the catalog
and ProDOS 2.4 falls through to Bitsy Bye. Two tunes are lost as well. Nothing is
printed.

Bisected threshold, same volume otherwise:

| tunes in `PT3/` | total nodes | result |
|---|---|---|
| 253 | 256 | player launches and plays |
| 254 | 257 | Bitsy Bye |
| 256 | 259 | Bitsy Bye |

### What ProDOS actually allows

A directory block is 512 B: 4 B of prev/next pointers, then 13 entries of 39 B
(`HOSTFS_ENTRY_LENGTH`, `HOSTFS_ENTRIES_PER_BLOCK` — already correct in source).

| Scope | ProDOS limit |
|-------|--------------|
| **Volume directory** | Always exactly 4 blocks (2–5), cannot be extended. First entry is the volume header → **51 file entries**. Hard. |
| **Subdirectory** | Grows by linking blocks: 13n − 1 entries. `file_count` in the header is 16-bit → **65535**. |
| **Volume** | 65535 blocks; every file needs ≥1 data block, so ~60k files in practice after boot / directory / bitmap overhead. |

There is no 256 anywhere in ProDOS. The current cap is invented.

### Pain points

1. Any real ProDOS volume with more than ~250 total entries cannot be mounted correctly.
2. Truncation is silent — no stderr, no UI, no error to the guest.
3. 337 KB is allocated per mounted unit regardless of content.
4. The scan buffers impose a *second*, undocumented 256-entry cap **per directory**.
5. Name collisions after mangling silently drop the loser.
6. The volume directory is grown past 4 blocks, which is outside the ProDOS spec.

---

## Goals & Non-Goals

### Goals

1. Mount any folder that can legally be a ProDOS volume: 51 root entries, unbounded
   subdirectory entries, up to the volume block ceiling.
2. Pay for what is present. A 3-file folder must not allocate for 65535.
3. Keep the once-per-second refresh proportional to the number of **directories**, not
   the number of files. A volume that mounts must also stay usable while the guest is
   doing disk I/O.
4. Enforce the ProDOS volume-directory limit (51) rather than inventing an extension.
5. Never drop an entry silently — every drop, cap, or rename gets a stderr line.
   (Non-NAPS host files are the one deliberate exception; see Decided.)
6. Give colliding host names a deterministic, extension-preserving ProDOS alias
   rather than dropping them.
7. Keep the public API in [`hostfs.h`](../src/machine/hostfs.h) unchanged.
8. Keep all 10 existing tests passing unmodified, and add the cases that would have
   caught this.

### Non-Goals

- Rewriting the reconciliation layer. Guest→host create / rename / delete / move
  semantics are preserved as-is.
- Changing the NAPS on-disk naming convention (`NAME#ttaaaa`).
- Changing the `hostfs.order` manifest format.
- Supporting volumes larger than 32 MB / 65535 blocks.
- Lazy or on-demand directory materialization (see Alternatives).
- Backward compatibility with existing HostFS folders that have >51 root entries.
  Single user; those folders get reorganized.

---

## Proposed Design

### Node table

Replace the in-struct fixed array with a grown array, mirroring the `map` /
`map_cap` pattern already present three fields above it:

```c
hostfs_node *nodes;     /* was: hostfs_file files[HOSTFS_MAX_FILES] */
int node_count;         /* high-water of used slots (active or not) */
int node_cap;
```

Growth: start at **16**, double on demand, hard ceiling `HOSTFS_MAX_NODES 65535`
(architectural — the ProDOS 16-bit `file_count` and block ceiling). Reaching the
ceiling is a stderr diagnostic and a refused node, not a silent `break`.

This is safe because **every reference to a node in the existing code is an `int`
index** — `parent_index`, `file_index` in the block map, `dir_index` — never a
stored pointer. Indices survive `realloc`.

### Node layout

`host_path[HOSTFS_PATH_MAX]` (1024 B) is removed. The host path is rebuilt on
demand by walking `parent_index`, which is bounded by `HOSTFS_MAX_DEPTH` (8) and
happens immediately before an `fopen` that costs orders of magnitude more.

The walk needs each ancestor's **host basename** — which is not `prodos_name`, since
NAPS tagging and collision aliasing make them differ. Basenames live in a per-volume
append-only string arena; the node stores a `uint32_t` offset into it, not a pointer.

```c
typedef struct {
    int64_t  host_mtime;
    uint32_t name_off;              /* host basename, offset into vol->names */
    int32_t  parent_index;          /* -1 = volume root */
    uint32_t eof;
    uint32_t host_size;
    uint16_t aux_type;
    uint16_t key_block;
    uint16_t blocks_used;
    uint16_t dir_block_count;       /* directories only */
    uint16_t parent_entry_block;
    uint8_t  active;
    uint8_t  kind;                  /* hostfs_kind */
    uint8_t  name_len;
    uint8_t  file_type;
    uint8_t  storage_type;
    uint8_t  parent_entry_number;
    char     prodos_name[HOSTFS_NAME_MAX];
} hostfs_node;                      /* 56 bytes */
```

Arena: `char *names; size_t names_len, names_cap;` — append-only. Rename appends a
new basename and abandons the old; the waste is bounded because `hostfs_rescan`
rebuilds the arena from scratch.

New internal helper:

```c
/* Rebuild "<root>/<a>/<b>/<basename>" for a node. Returns A2_OK / A2_ERR. */
static int hostfs_node_host_path(
    const hostfs_volume *vol, int index, char *out, size_t out_size);
```

Callers that today read `file->host_path` take a `char path[HOSTFS_PATH_MAX]` local
and call this first. That is the widest mechanical change in the work.

### Memory

| Layout | B/node | 51 | 4096 | 65535 |
|---|---|---|---|---|
| Current `hostfs_file` | 1088 | 54.2 KB | 4.25 MB | **68.00 MB** |
| Proposed `hostfs_node` | 56 | 2.8 KB | 0.22 MB | **3.50 MB** |
| + name arena (~20 B avg) | — | +1 KB | +0.08 MB | **+1.25 MB** |

Worst case for a maximally-full 32 MB volume: **≈ 4.75 MB**, and only when the volume
actually holds that many files. A three-file folder costs roughly 200 bytes plus the
initial 16-slot table.

### Volume directory — 51, absolute

The volume directory becomes exactly 4 blocks (2–5), per spec.
`hostfs_dir_blocks_for_files` is no longer consulted for the root; subdirectories keep
using it.

```c
#define HOSTFS_ROOT_MAX_ENTRIES 51   /* 4 blocks * 13 entries - 1 volume header */
```

Root children beyond 51 are **dropped in scan order** (`hostfs.order` first, then
case-insensitive sort), with one stderr line. Subdirectories are unaffected — put the
overflow in a folder.

### Collision-safe ProDOS names

Two host files can mangle to the same ProDOS name (`my-file.txt` and `my_file.txt`
both → `MYFILE.TXT`). Today the second is dropped. Instead, generate a deterministic
alias, **preserving the extension** so guest-side extension matching still works —
PT3PLR scans `PT3/` for names ending `.PT3`, and an alias that broke that would be
worse than useless.

Algorithm, applied only to the second and later colliders in scan order:

1. Split the mangled name at the **last** `.` into `stem` and `ext`
   (no `.` → all stem, empty ext).
2. Ordinal is 3 decimal digits, `001`–`999`.
3. Truncate `stem` so `len(stem') + 3 + (ext ? 1 + len(ext) : 0) <= 15`.
4. Candidate is `stem' + NNN + ("." + ext)`. Increment `NNN` until unique within the
   parent, or fail past `999`.

The first character stays a letter because `hostfs_mangle_prodos_name` already
guarantees it, and truncation only shortens from the right.

| Host names in one folder | ProDOS names |
|---|---|
| `ACADEMY.PT3#000000`, `academy!.pt3#000000` | `ACADEMY.PT3`, `ACADEMY001.PT3` |
| `notes.txt`, `notes(1).txt`, `notes-1.txt` | `NOTES.TXT`, `NOTES001.TXT`, `NOTES002.TXT` |
| `averylongfilename.txt`, `averylongfilename2.txt` | `AVERYLONGF.TXT`, `AVERYLON001.TXT` |

Every alias emits a stderr line, because the alias is a **HostFS-only view**: the host
file keeps its real name, and copying that volume into a `.po` will not reproduce it.

Stability caveat: ordinals are assigned in scan order, so inserting a new colliding
file that sorts earlier renumbers the ones after it. `hostfs.order` pins root order;
subdirectories rely on the deterministic case-insensitive sort. Accepted — the
warning makes it visible.

### Buffers sized from reality

No buffer is sized by the node ceiling any more.

| Site | Today | Proposed |
|---|---|---|
| [`hostfs_scan_dir_recursive`](../src/machine/hostfs.c) scans | `calloc(MAX_FILES, 1320)` per level | Count `readdir` entries first, then allocate exactly; or grow. Removes the hidden per-directory 256 cap and the ~403 KB-per-level transient |
| [`hostfs_reconcile_directory_at`](../src/machine/hostfs.c) `ents` | `dent[MAX_FILES]` on the **stack** | Heap, sized from the directory's block chain × 13 |
| `used` / `seen` / `matched` | `bool[MAX_FILES]` on the **stack** | Heap, sized `node_count` |
| `order_basenames[MAX][256]` | 64 KB in-struct | Root-only (written under `parent_index < 0`), so `HOSTFS_ROOT_MAX_ENTRIES` → 13 KB, no longer scales |

### Refresh cost at scale

This is the one place where scale changes an algorithm rather than an allocation, and
it does not show up at 256 nodes.

[`hostfs_maybe_refresh`](../src/machine/hostfs.c) fires on any SmartPort STATUS /
READ / WRITE, rate-limited to `HOSTFS_REFRESH_PERIOD_MS` (1000 ms), and calls
[`hostfs_rescan`](../src/machine/hostfs.c), which walks the **entire host tree** —
`readdir` plus a `stat` per entry, at every depth. At 256 nodes that is free. At
60,000 nodes it is 60,000 `stat` calls per second for as long as the guest touches
the disk, which would make a large volume unusable.

Fix: descend only into directories whose own mtime changed. A directory's mtime moves
when an entry is added, removed, or renamed within it, which is exactly the set of
changes the rescan is looking for. Nodes already carry `host_mtime`; directory nodes
start using theirs as a skip test.

- `stat` the directory; if `mtime` is unchanged since the last scan, skip its
  `readdir` **and** its children entirely.
- Content edits to an existing file do not move the parent's mtime — but they do not
  need to, because file size and mtime are read through the node when the guest reads
  blocks, and a rewritten file is caught by the per-file `stat` in its parent's next
  scan.
- Steady-state cost becomes one `stat` per **directory**, not per file.

Caveat to accept: some filesystems have coarse (1 s) directory mtime granularity, so
a change made in the same second as a scan can be missed until the next change. That
is already true of the existing per-file `mtime` comparison, and `hostfs_rescan`
remains available as an explicit full walk.

### The one realloc hazard

[`hostfs_scan_dir_recursive`](../src/machine/hostfs.c) currently does:

```c
fi = vol->file_slots++;
file = &vol->files[fi];
...
hostfs_scan_dir_recursive(vol, file->host_path, fi, depth + 1);   /* callee grows the table */
```

`file->host_path` is passed into a call that will `realloc` the table underneath it —
dangling for the whole subtree scan. With `host_path` removed the argument becomes a
local `char[]` from `hostfs_node_host_path`, which fixes it structurally. Every other
`hostfs_file *` capture in the file must still be audited for a growth point in
between; the create path at [`hostfs_add_node_from_scan`](../src/machine/hostfs.c)
allocates its slot first and does not grow again, but this must be re-checked, not
assumed.

### Diagnostics (stderr, one line each)

```text
a2m: HostFS <root>: volume directory holds 51 entries (ProDOS limit); dropped 9: ZTOOLS, ZZ.TXT, ... (+7 more)
a2m: HostFS <root>/PT3: 'academy!.pt3#000000' -> ACADEMY001.PT3 (renamed for ProDOS; host name unchanged, will not survive copy to .po)
a2m: HostFS <root>: node limit 65535 reached; remaining entries ignored
a2m: HostFS <root>/DEEP: depth limit 8 reached; subtree ignored
```

Name lists cap at 5 plus a `(+N more)` tail so a badly-shaped folder cannot flood the
terminal.

---

## API / Interface Changes

**Public ([`hostfs.h`](../src/machine/hostfs.h)): none.** All 16 functions keep their
signatures and semantics. `hostfs_file_count` continues to report active nodes.

Internal only:

| Change | Note |
|---|---|
| `hostfs_file` → `hostfs_node` | Renamed to make the sweep mechanical and reviewable |
| `vol->files` → `vol->nodes` + `node_count` / `node_cap` | Grown array |
| `vol->names` arena | New; `uint32_t` offsets, never pointers |
| `hostfs_node_host_path()` | New; replaces `file->host_path` reads |
| `hostfs_alloc_file_slot()` | Grows instead of returning −1 at 256 |
| `HOSTFS_MAX_FILES` | **Deleted.** Replaced by `HOSTFS_MAX_NODES` (65535) and `HOSTFS_ROOT_MAX_ENTRIES` (51) |

---

## Data Model Changes

- `hostfs_volume` shrinks from 345200 B to roughly 200 B plus dynamic allocations.
- Node identity is still the `int` index; nothing persists node indices across a
  `hostfs_rescan`, which is already true today.
- `hostfs.order` format is unchanged. Root order still pins the first 51 entries.

---

## Alternatives Considered

### 1. Just raise `HOSTFS_MAX_FILES`

Rejected. At 65535 the in-struct table alone is 68 MB, `dent ents[]` becomes a 2.4 MB
stack frame, and the scan buffers become 86 MB per recursion level. It also leaves
the number arbitrary.

### 2. Per-directory node arrays

Rejected. HostFS is a flat table with `parent_index` links, and the block map, the
reconcile pass, and the order manifest all address nodes by global index. Splitting
into per-directory arrays is a restructure with no behavioral benefit over one
growable flat array.

### 3. Keep `host_path` per node

Rejected. It is 94% of the node and it is redundant with `parent_index` plus the host
basename. Keeping it caps the practical ceiling at a few thousand nodes.

### 4. Full rewrite of `hostfs.c`

Rejected. The valuable, hard-won part is the reconciliation logic — guest create /
rename / delete / cross-directory move propagating to real files on disk. That is
where the subtle bugs live, it is the part that can destroy user data, and it is not
what is broken. Replace the storage layer; keep the semantics.

### 5. Lazy / on-demand directory materialization

Deferred. ProDOS can read any block at any time, so avoiding the full scan means
synthesizing directory blocks on demand and inventing an eviction policy. At 4.75 MB
worst case there is no problem to solve.

---

## Observability

- All diagnostics to **stderr**, matching the existing
  `"a2m: HostFS directory full; remount to pick up new files"` line.
- Every diagnostic names the volume root and the affected directory.
- No UI surface in this work. If mount warnings should reach the frontend later,
  that is a follow-on.

---

## Key Decisions

1. **Volume directory is 51 entries, absolute.** Four blocks, per ProDOS. Extending it
   buys nothing — files must live somewhere regardless, and the volume ceiling is
   unchanged — while putting HostFS outside the spec.
2. **Mount and report, never refuse.** A root with 60 entries mounts with the first 51
   and prints what was dropped. Silent truncation is the whole reason for this work.
3. **Collisions get a deterministic alias, not a drop**, and the alias preserves the
   extension so guest-side matching keeps working.
4. **stderr only.** No UI plumbing in this change.
5. **No backward compatibility.** Folders with >51 root entries change behavior.
6. **One growable flat node table**, not per-directory arrays.
7. **`host_path` is derived, not stored**; host basenames live in a string arena
   addressed by offset.
8. **Reconciliation semantics are preserved verbatim.** Any behavior change there is a
   bug in this work, not a feature of it.
9. **65535 is the node ceiling**, matching the ProDOS 16-bit `file_count` and the
   volume block limit.
10. **The refresh walk is in scope, not a follow-on.** Lifting the node cap without it
    produces a volume that mounts and then crawls, so the directory-mtime skip is a
    required phase (PR 4) rather than an optimization. "Handles any real ProDOS
    volume" means usable, not merely enumerable.
11. **Non-NAPS host files stay silently invisible.** A regular file whose name does not
    parse as `NAME#ttaaaa` is skipped with no diagnostic. This is the single
    deliberate exception to decision 2 — warning on every `README.md`, `.gitignore`,
    and `hostfs.order` would drown the signal from real drops. It also usefully keeps
    stray host files from consuming root slots.

---

## Decided (closed questions)

| Topic | Decision |
|---|---|
| Root overflow | Mount 51, drop the rest, stderr line naming up to 5 |
| Collision handling | Extension-preserving 3-digit ordinal alias; stderr line each |
| Diagnostic channel | stderr only |
| Volume directory size | 4 blocks / 51 entries, absolute — no opt-in extension |
| Backward compatibility | None required |
| Rewrite vs replace-layer | Replace storage + scan; preserve reconciliation |
| Node ceiling | 65535 |
| Growth policy | Start 16, double |
| mtime width | `int64_t` (4 bytes over `uint32_t`, no 2038 question) |
| Refresh walk | **In scope as PR 4**, not a follow-on. Directory-mtime skip; steady state is one `stat` per directory |
| Non-NAPS host files | **Stay silently skipped** — the one deliberate exception to "never drop silently" |

---

## Open questions

1. **Do directories count against the 51?** Yes under this design — they are volume
   directory entries like any other. Stated for the record.
2. **PT3PLR appears to cap itself at 255 tunes.** With the node cap lifted in a scratch
   build and 400 files in `PT3/`, the player reported `001/255`. That looks like a
   byte-wide counter in the player, not a HostFS or ProDOS limit, so this work will
   not change it. Worth knowing before the `pt3plr` regression test asserts a count.
3. **Should `hostfs_file_count` report dropped entries?** It currently returns active
   nodes. A caller wanting "how much did we lose" has no way to ask. Probably not
   worth an API change while diagnostics are stderr-only.

---

## Risks

| Risk | Severity | Mitigation |
|---|---|---|
| Refactor breaks reconcile and damages real host files | **High** | Reconcile logic untouched; all tests run in temp dirs; PR 2 is a pure storage swap with no semantic change, verified by the existing 10 cases before anything else lands |
| A surviving `hostfs_node *` held across a growth point | **High** | `host_path` removal fixes the known one structurally; audit every `hostfs_node *` capture as an explicit PR 2 checklist item |
| Arena offsets confused with pointers | Medium | `uint32_t name_off` only; no `char *` stored in a node |
| Collision ordinals renumber when a file is inserted | Medium | Deterministic scan order; `hostfs.order` pins root; stderr line every time an alias is used |
| Aliased names surprise on copy to `.po` | Medium | Warning text says so explicitly |
| Full rescan once per second becomes O(volume) | **High** | Directory-mtime skip (PR 4). Without it a 60k-node volume issues 60k `stat` calls/second under guest I/O |
| Directory-mtime skip misses a same-second change | Low | Coarse-granularity filesystems only; already true of the per-file comparison; `hostfs_rescan` stays as an explicit full walk |
| 51-entry cap breaks an existing sample folder | Low | Single user; `samples/hostfs/` root currently holds 5 entries |
| Memory at a maximally-full volume | Low | 4.75 MB measured, allocated only if the files exist |
| Depth-8 limit still silent | Low | Add the diagnostic in PR 4 alongside the others |

---

## References

- `src/machine/hostfs.c`, `src/machine/hostfs.h`, `src/machine/hostfs_boot.h`
- `src/util/apple2_file.c` — NAPS parse/compose (`apple2_naps_parse_path`)
- `tests/machine/test_hostfs.c` — 10 existing cases
- `manual/manual.md` — HostFS section (volume layout, `hostfs.order`)
- *ProDOS 8 Technical Reference Manual* — directory format, 4-block volume directory,
  16-bit `file_count`
- Observed failure: `samples/hostfs/pt3plr/` (256 tunes + `PRODOS` + `PT3/`)

---

## PR Plan

### PR 1 — Volume directory is 51 entries, absolute

- **Title:** `hostfs: enforce ProDOS 51-entry volume directory`
- **Files:** `src/machine/hostfs.c`, `src/machine/hostfs.h`, `tests/machine/test_hostfs.c`
- **Dependencies:** none
- **Checklist:**
  - [ ] `HOSTFS_ROOT_MAX_ENTRIES 51`
  - [ ] `hostfs_build_volume_directory` pins `dir_block_count` to 4 (blocks 2–5)
  - [ ] Root scan stops accepting children at 51
  - [ ] stderr line naming up to 5 dropped entries plus `(+N more)`
  - [ ] `hostfs_dir_blocks_for_files` still used for subdirectories only
  - [ ] Test: 60-entry root mounts with exactly 51, warning emitted, 52nd absent
- **Description:** Small, self-contained, and independent of the node-table work.
  Lands the spec compliance decision first so later PRs build on the correct shape.

### PR 2 — Dynamic node table + name arena

- **Title:** `hostfs: growable node table, host paths derived from parent walk`
- **Files:** `src/machine/hostfs.c`, `src/machine/hostfs.h`
- **Dependencies:** PR 1
- **Checklist:**
  - [ ] `hostfs_file` → `hostfs_node` with the 56-byte layout
  - [ ] `vol->nodes` / `node_count` / `node_cap`; start 16, double, ceiling `HOSTFS_MAX_NODES 65535`
  - [ ] `vol->names` arena; `uint32_t name_off`; no `char *` in a node
  - [ ] `hostfs_node_host_path()` and conversion of every `file->host_path` read
  - [ ] Fix the `file->host_path` argument passed across the recursive scan
  - [ ] **Audit every `hostfs_node *` capture for an intervening growth point** — list them in the PR body
  - [ ] `hostfs_alloc_file_slot` grows; ceiling hit emits stderr, not a silent break
  - [ ] `HOSTFS_MAX_FILES` deleted
  - [ ] All 10 existing tests pass **unmodified**
- **Description:** The load-bearing change, and the one that unblocks `pt3plr`. No
  behavior change is intended beyond the raised ceiling — the existing suite is the
  referee.

### PR 3 — Buffers sized from actual counts

- **Title:** `hostfs: size scan and reconcile buffers from real entry counts`
- **Files:** `src/machine/hostfs.c`, `tests/machine/test_hostfs.c`
- **Dependencies:** PR 2
- **Checklist:**
  - [ ] Scan buffers sized from the `readdir` count for that directory
  - [ ] `dent ents[]` off the stack, sized from the directory's block chain
  - [ ] `used` / `seen` / `matched` off the stack, sized `node_count`
  - [ ] `order_basenames` bounded by `HOSTFS_ROOT_MAX_ENTRIES`
  - [ ] Test: subdirectory with 1000 entries mounts, enumerates, and reads correctly
  - [ ] Test: `hostfs_rescan` over a 1000-entry subdirectory
- **Description:** Removes the hidden per-directory 256 cap and the ~403 KB-per-level
  transient. Without this, PR 2's raised ceiling is unreachable in practice.

### PR 4 — Refresh skips unchanged directories

- **Title:** `hostfs: rescan only directories whose mtime moved`
- **Files:** `src/machine/hostfs.c`, `tests/machine/test_hostfs.c`
- **Dependencies:** PR 2
- **Checklist:**
  - [ ] Directory nodes record `host_mtime` at scan
  - [ ] `hostfs_rescan_dir` `stat`s the directory and skips `readdir` + descent when unchanged
  - [ ] `hostfs_rescan` remains a full unconditional walk when called directly
  - [ ] Test: add a file in a deep subdirectory → picked up within one refresh period
  - [ ] Test: delete a file in a deep subdirectory → node deactivated
  - [ ] Test: untouched sibling subtrees are not re-walked (count `stat`s via a seam or a large fixture timing assertion)
- **Description:** Without this, PR 2 and PR 3 make large volumes *mountable* but not
  *usable* — the once-per-second rescan is O(total files). Steady state becomes one
  `stat` per directory.

### PR 5 — Collision-safe ProDOS names + remaining diagnostics

- **Title:** `hostfs: alias colliding names instead of dropping them`
- **Files:** `src/machine/hostfs.c`, `tests/machine/test_hostfs.c`
- **Dependencies:** PR 2
- **Checklist:**
  - [ ] Extension-preserving 3-digit ordinal alias, `001`–`999`
  - [ ] Fails past `999` with a stderr line rather than looping
  - [ ] Alias is deterministic for a fixed scan order
  - [ ] stderr line per alias, stating the host name is unchanged
  - [ ] Depth-limit diagnostic added
  - [ ] Test: three host names mangling to one ProDOS name → all three visible
  - [ ] Test: alias keeps the extension (`.PT3` still matches)
  - [ ] Test: alias stability across remount with unchanged contents
- **Description:** Closes the last silent-drop path.

### PR 6 — Stress, samples, and docs

- **Title:** `hostfs: full-volume stress tests and documentation`
- **Files:** `tests/machine/test_hostfs.c`, `manual/manual.md`, `agents/disk.md`, `design/README.md`
- **Dependencies:** PR 1–5
- **Checklist:**
  - [ ] Stress: deep tree at `HOSTFS_MAX_DEPTH`, several thousand nodes, mount → read → rescan
  - [ ] Stress: node ceiling reached cleanly, with the diagnostic, no crash
  - [ ] Regression: `samples/hostfs/pt3plr/` (256 tunes) boots and the player launches — assert the catalog, not the player's own tune count (see Open question 3)
  - [ ] `manual/manual.md`: 51-entry root, unbounded subdirectories, alias behavior, diagnostics
  - [ ] `agents/disk.md`: the durable invariants (51 root, indices-not-pointers, arena offsets)
  - [ ] `design/README.md`: this doc **Draft → Landed**
- **Description:** Proves the mission statement and folds the lasting rules into the
  agent handoff surface.

---

## Follow-ons (not in scope)

| Item | Notes |
|------|-------|
| Mount diagnostics in the UI | stderr only for now; a Machine/Disk surface could show drops and aliases |
| NAPS hex case consistency | `apple2_naps_make_path` writes `%02x%04x`, `hostfs_compose_naps_filename` writes `%02X%04X`; harmless but inconsistent |
| Arena compaction on rename | Currently reclaimed only by `hostfs_rescan`; bounded, so not worth complexity yet |
| Volumes beyond 32 MB | Would need a 32-bit block path throughout; no ProDOS 8 use case |
