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
a 32 MB volume — at a worst-case **node-and-name cost of roughly 4.75 MB** rather
than the 68 MB the current node layout would demand at that scale. The direct block
index and initial watcher queue add about 256 KB each; directory blocks, the block
map, and platform watch state scale with the content actually present.

Refresh is part of the mission, not an optimization on top of it. The once-per-second
rescan currently walks every file at every depth; at 60,000 nodes that is 60,000
`stat` calls per second under guest I/O. Lifting the node cap without fixing it yields
a volume that mounts and then crawls. Refresh therefore becomes **event-driven**:
native host filesystem notifications identify the path that changed, HostFS verifies
that path against the filesystem, and only the affected node or parent directory is
reconciled. A healthy watcher does no scan at all while the host tree is idle. The
target is **usable**, not merely enumerable.

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
| Refresh | [`hostfs_maybe_refresh`](../src/machine/hostfs.c) | Once per second under guest I/O, full-tree `readdir` + `stat` walk |
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
3. Make idle refresh **O(1)**: when the host tree has not changed, do no `readdir` and
   no `stat`. A host create, modify, rename, or delete must reconcile only the affected
   path or parent directory, normally before the next guest disk operation completes.
4. Enforce the ProDOS volume-directory limit (51) rather than inventing an extension.
5. Never drop an entry silently — every drop, cap, or rename gets a stderr line.
   (Non-NAPS host files are the one deliberate exception; see Decided.)
6. Give colliding host names a deterministic, extension-preserving ProDOS alias
   rather than dropping them.
7. Change no existing signature or semantic in
   [`hostfs.h`](../src/machine/hostfs.h). *Additions* are fine — the
   `/* Test helpers. */` block at the bottom of the header is the right home for
   anything the suite needs to observe.
8. Keep all 10 existing tests passing unmodified, and add the cases that would have
   caught this.
9. Never rewrite a user's `hostfs.order` with less than it already holds.

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

### Constants

`HOSTFS_MAX_FILES` is deleted. The ProDOS format constants move out of the private
enum at [`hostfs.c:38`](../src/machine/hostfs.c) and into
[`hostfs.h`](../src/machine/hostfs.h) beside the other `HOSTFS_*` limits — they
describe the on-disk ProDOS format, not an implementation choice, and the tests need
them. The 51 is then **derived, not asserted in a comment**:

```c
#define HOSTFS_ENTRY_LENGTH      39u   /* ProDOS directory entry */
#define HOSTFS_ENTRIES_PER_BLOCK 13u   /* (512 - 4) / 39 */
#define HOSTFS_ROOT_DIR_BLOCKS   4u    /* ProDOS volume directory is always 4 */
#define HOSTFS_ROOT_MAX_ENTRIES  (HOSTFS_ROOT_DIR_BLOCKS * HOSTFS_ENTRIES_PER_BLOCK - 1u)  /* 51 */
#define HOSTFS_MAX_NODES         65535 /* ProDOS 16-bit file_count / block ceiling */
```

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

Worst-case node-and-name storage for a maximally-full 32 MB volume: **≈ 4.75 MB**,
and only when the volume actually holds that many files. The fixed direct block index
adds 256 KB, and the initial watcher queue adds roughly another 256 KB; block-map
entries, synthesized directory blocks, and platform watch state are accounted
separately because they depend on the actual tree. A three-file folder needs only the
initial 16-slot node table and a small name arena rather than a maximum-sized table.

### Volume directory — 51, absolute

The volume directory becomes exactly `HOSTFS_ROOT_DIR_BLOCKS` (4) blocks, 2–5, per
spec. `hostfs_dir_blocks_for_files` is no longer consulted for the root;
subdirectories keep using it.

Root children beyond `HOSTFS_ROOT_MAX_ENTRIES` are **dropped in scan order**
(`hostfs.order` first, then case-insensitive sort), with one stderr line.
Subdirectories are unaffected — put the overflow in a folder.

**Subdirectory spare capacity is unchanged.** The `blocks < 2 → 2` floor at
[`hostfs.c:1976`](../src/machine/hostfs.c) stays for subdirectories; only the root
stops consulting it. Two consequences to keep in view:

- `vol->bitmap_block = 2 + vol->dir_block_count` at
  [`hostfs.c:1985`](../src/machine/hostfs.c) becomes a constant **6** rather than
  floating with root size. Useful determinism — but confirm no existing test pins the
  old floating value.
- Every directory block is an eager `malloc(HOSTFS_BLOCK_SIZE)` in
  [`hostfs_map_add_ram`](../src/machine/hostfs.c), so the floor costs 512 B of host
  RAM per non-empty subdirectory. Immaterial per directory; include it in PR 7's
  memory accounting for a tree with thousands of them.

### `hostfs.order` while the root is over the limit

[`hostfs_persist_order_manifest_in(vol, -1)`](../src/machine/hostfs.c) rebuilds the
manifest from the catalog and rewrites `hostfs.order` wholesale. After PR 1 the root
catalog is 51 names, so a user's 60-line file would be rewritten with only the 51
survivors the first time the guest reorders or deletes a root entry — their ordering
intent for the other 9 gone from their own file, unrecoverable by moving files into a
folder later.

Mount and eject are already safe: `current` (51) equals `vol->order_basenames` (51,
populated only from accepted children), so the early return fires. It is the first
guest-side mutation that does the damage.

**Resolution: do not rewrite the root manifest while the root is truncated.** When
`vol->root_truncated` is set, `hostfs_persist_order_manifest_in(vol, -1)` returns
early without writing. Putting the check inside `_in`, gated on `parent_index < 0`,
covers all three call sites — flush [`1927`](../src/machine/hostfs.c), reconcile
[`2564`](../src/machine/hostfs.c), rescan [`3055`](../src/machine/hostfs.c) — with
subdirectories unaffected.

**The flag has two set sites, not one.** The mount scan is the obvious one, but the
root can also *become* truncated after mount, and `hostfs_rescan` never re-runs
`hostfs_scan_into_files` — it uses the incremental `hostfs_rescan_dir` path, then
persists the root manifest at [`3055`](../src/machine/hostfs.c). So:

> Root holds 45 entries and a 60-line `hostfs.order` (the user pre-declared an order
> for files not yet copied in). While mounted they copy in 20 more. The rescan adds 6,
> the rest fail on `entry == NULL` and fire the root-full warning — the catalog is now
> 51, `root_truncated` was never set, and the manifest is rewritten with 51 names.

Same loss through the other door. Set the flag in the root branch of the
`entry == NULL` path in [`hostfs_add_node_from_scan`](../src/machine/hostfs.c) as
well. That is complete: `hostfs_add_node_from_scan` has exactly one call site
([`2964`](../src/machine/hostfs.c), inside `hostfs_rescan_dir`) and is the only
post-mount path that adds a node from a host scan.

**The flag is sticky for the session.** It is never cleared live. Clearing is
*possible* — `hostfs_rescan_dir` re-`readdir`s the root every pass, so it could clear
when nothing was rejected and the count is under 51 — but the failure modes are not
symmetric:

| Wrong choice | Consequence |
|---|---|
| Sticky when it could have cleared | Reordering does not persist until remount. Visible, no data lost |
| Cleared when it should not have | Manifest rewritten short. Silent, permanent — the bug this exists to prevent |

Take the safe one. A new state transition guarding against a cosmetic annoyance is a
bad trade when getting it wrong costs the user's file.

Rejected alternative — "append any `previous` name not in `current`" — because the
persist function cannot distinguish the two reasons a name is missing:

| Reason absent from `current` | Correct action |
|---|---|
| Dropped by the 51 cap — file still on disk | Preserve |
| Deleted by the guest — file is gone | Prune |

Appending both makes deleted names accumulate forever. They are inert on load —
[`hostfs_apply_order_to_scans`](../src/machine/hostfs.c) matches order names against
actual scans and ignores unmatched ones — but a file that only ever grows is a new
silent wrongness in a change whose thesis is the opposite. Distinguishing the cases
needs a `stat` per orphan name or the dropped list plumbed from scan to persist, and
then raises a second question: names appended at the end lose their position relative
to the kept 51, so a file recovered later by getting under the cap comes back last
instead of where the user had it.

Accepted cost: guest-side **root** reordering does not persist while the folder is
over the limit. A 51-entry catalog cannot faithfully express a 60-entry ordering
anyway, and the folder is already warned about. Because the flag is sticky, this
clears **on the next mount** once the root is under 51 — not live in the same session.

Surfaced in the warning:

```text
a2m: HostFS <root>: hostfs.order left unchanged while the root is over the limit
```

**Ordering inside the function matters.** Run the existing "lists are equal" early
return *first*, the freeze check *second*, and give the freeze line its own
suppression flag like the two dir-full messages. Otherwise every mount+eject of an
over-limit folder emits the line even though no write was ever going to happen —
`hostfs_eject` → `hostfs_flush` → root persist runs unconditionally.

With that ordering the line fires only when a write genuinely would have occurred, and
the PR 1 assertion stays stable: `hostfs_mount` never persists at all (the only call
sites are flush, reconcile, and rescan), so `warn_count == 1` holds from the drop line
at mount and survives a no-change eject, where the equality check returns first. The
two `calloc`s still run ahead of the equality check; that is 128 KB today and sized to
reality after PR 3, which is nothing against warning about a write that never
happened.

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

### Block lookup at scale

[`hostfs_map_find`](../src/machine/hostfs.c) is a linear scan over `map_count`:

```c
for (i = 0; i < vol->map_count; ++i)
    if (vol->map[i].block == block) return i;
```

It runs on **every** `hostfs_read_block` and `hostfs_write_block`. At 256 nodes the
map is tiny. At 60,000 files it holds tens of thousands of entries, so every 512-byte
block the guest reads costs a scan of the whole map — this becomes the dominant cost
of guest disk I/O the moment the node cap is lifted.

Fix: the block space is small (`HOSTFS_TOTAL_BLOCKS` 65535) and densely bump-allocated
by [`hostfs_alloc_block`](../src/machine/hostfs.c), so a direct index is both trivial
and exact:

```c
int32_t *block_to_map;   /* HOSTFS_TOTAL_BLOCKS + 1 entries, -1 = unmapped */
```

256 KB, allocated once at mount in addition to the node-and-name budget, and lookup
becomes a single load. The index is maintained wherever `map` is inserted into,
removed from, or reordered; `hostfs_eject` frees it. Binary search over a sorted map
is the alternative, but it needs maintenance on every insert for no memory saving
worth having.

### Event-driven refresh at scale

This and block lookup are the two places where scale changes an algorithm rather than
an allocation, and neither shows up at 256 nodes.

[`hostfs_maybe_refresh`](../src/machine/hostfs.c) fires on any SmartPort STATUS /
READ / WRITE, rate-limited to `HOSTFS_REFRESH_PERIOD_MS` (1000 ms), and calls
[`hostfs_rescan`](../src/machine/hostfs.c), which walks the **entire host tree** —
`readdir` plus a `stat` per entry, at every depth. At 256 nodes that is free. At
60,000 nodes it is 60,000 `stat` calls per second for as long as the guest touches
the disk, which would make a large volume unusable.

The first proposed replacement was a directory-mtime skip. It is rejected after
tracing the actual semantics:

- Directory mtime is **not recursive**. In `A/B/C`, creating `C/D` changes `C`, not
  `A` or `B`; finding it still requires a `stat` of every known directory.
- Modifying or resizing an existing file normally changes no directory mtime at all.
  The current full rescan catches that. Contrary to the first draft of this design,
  `hostfs_read_block` opens the host file for bytes but does **not** `stat` it or
  refresh catalog EOF/storage metadata. A directory-mtime skip would therefore be a
  silent refresh semantic regression.
- Coarse timestamp granularity can miss multiple changes within one tick.

The replacement is a native cross-platform filesystem watcher. Watcher events are
**invalidation hints, never the source of truth**: they identify a path whose cached
HostFS state may be stale; HostFS then uses `stat` / `readdir` to verify and reconcile
the smallest authoritative scope.

#### Utility-layer watcher

The watcher lives in `src/util/fs_watch.*`, preserving the dependency rule
`machine -> util`. `util` already owns the cross-platform thread, mutex, condition,
and bounded message-queue wrappers. No callback may hold or mutate a
`hostfs_volume *`; the watcher thread owns only native watch handles and pushes
root-relative path events into its queue. The runtime/machine owner drains and
applies them from `hostfs_maybe_refresh`.

```c
typedef enum {
    FS_WATCH_CREATE = 1u << 0,
    FS_WATCH_REMOVE = 1u << 1,
    FS_WATCH_MODIFY = 1u << 2,
    FS_WATCH_RENAME = 1u << 3,
    FS_WATCH_METADATA = 1u << 4,
    FS_WATCH_DIRECTORY = 1u << 5
} fs_watch_flags;

typedef struct {
    uint32_t flags;
    char relative_path[FS_WATCH_PATH_MAX];
} fs_watch_event;

fs_watch *fs_watch_create(const char *root_path);
bool fs_watch_add_directory(fs_watch *watch, const char *relative_path);
bool fs_watch_try_pop(fs_watch *watch, fs_watch_event *out);
bool fs_watch_take_rescan_required(fs_watch *watch);
void fs_watch_destroy(fs_watch *watch);  /* stop, wake, join, close, free */
```

The exact API may combine create/start, but the ownership and failure semantics do
not change. The event queue is bounded (initial target: 256 path events, roughly
256 KB per mounted volume). If the native queue or the HostFS queue overflows, the
watcher sets a separate sticky `rescan_required` bit; it must not rely on finding
space to enqueue an overflow event into the queue that just filled.

| Host | Backend | Scope / special handling |
|---|---|---|
| Windows | `ReadDirectoryChangesW` | Recursive root handle plus a filtered non-recursive parent watch, because the recursive call does not report rename/delete of the watched root itself; overlapped waits so stop can cancel and join; zero-byte / `ERROR_NOTIFY_ENUM_DIR` completion means loss |
| macOS | FSEvents with file-event flags | One recursive root stream with `WatchRoot`, `FileEvents`, and low-latency/no-defer delivery; dropped, must-scan, or root-change flags set `rescan_required` |
| Linux | `inotify` | One watch per directory; maintain watch-descriptor -> root-relative directory map; add watches before scanning new directories; root self-move/delete/unmount means loss |

Native wrappers are chosen over a new general-purpose dependency. Host watcher
libraries expose the same backend asymmetry — especially Linux's lack of a recursive
`inotify` watch — while adding a larger dependency surface to a C project. The native
surface needed here is deliberately small.

#### Closing setup races

Watching starts before the initial mount scan. On Windows and macOS the recursive
root watch is live first. On Linux, the root is watched first and every recursive
scan calls `fs_watch_add_directory` **before** `readdir` on that directory. Thus:

`fs_watch_create` does not report a healthy watcher until the backend has completed a
readiness handshake proving the root handle/stream/watch is armed. In particular, an
FSEvents worker thread being created is not sufficient; its stream must be scheduled
and started before mount scanning begins.

1. A file already present is seen by the scan.
2. A change after the watch is installed is queued.
3. A newly created or moved-in directory is watched before its initial subtree scan;
   content already inside it is found by that scan, and changes during the scan are
   queued.

Events accumulated during mount are not cleared afterward. Replaying an event whose
state the mount scan already captured is harmless and closes the scan/watch boundary
without a fragile generation handshake.

#### Targeted HostFS reconciliation

On every SmartPort touch, `hostfs_maybe_refresh` performs a cheap non-blocking queue
poll. No event means no `stat`, no `readdir`, and no one-second timer. Current queued
events are drained and duplicate paths coalesced before work begins.

`hostfs_maybe_refresh` already runs at the SmartPort boundary before the operation is
served, so an event that has reached the queue is reconciled before the guest command
observes the catalog or data. Delivery itself remains asynchronous: an event that has
not yet arrived is applied on a later touch, which is acceptable for the stated
near-real-time requirement.

Events normalize into two authoritative operations:

| Event shape | Verification / reconciliation |
|---|---|
| Pure modify / metadata event for a known regular file | `stat` that one path; update `host_mtime`, `host_size`, EOF, storage/block map, and its catalog entry. Same-size content needs no remap because data blocks already open the real host file on read |
| Create, remove, directory event, `hostfs.order`, unknown path, or an ambiguous non-rename event | Rescan the affected **immediate parent directory**; local matched marks add/update/remove only its direct children |
| Rename with incomplete in-root coverage, or any event whose affected parent cannot be identified safely | Set `rescan_required`; the next safe touch performs one full reconciliation |

A rename may dirty two parents and native backends may report its halves separately;
rescanning both parents is correct and avoids inventing cross-platform rename-cookie
semantics in HostFS. The backend emits both observable halves as path invalidations.
If it cannot prove that every in-root half was reported, it sets `rescan_required`
instead of guessing. In particular, Linux directory moves force watcher recreation
and a full rescan: the kernel watches follow the moved inodes, but the cached
watch-descriptor/path map is then stale. File moves with both parents known remain
targeted. A new directory is watched first, then its initial subtree is materialized
recursively. Existing child directories are not descended into merely because their
parent changed.

This requires splitting today's recursive `hostfs_rescan_dir` into an immediate-
directory reconcile helper plus an explicit recursive driver. Public
`hostfs_rescan()` keeps its existing unconditional full-walk semantics and remains
the ground-truth escape hatch.

Guest writes already reconcile synchronously and remain authoritative. Their later
watcher echoes are expected: targeted verification sees matching state and becomes a
no-op. Watch events are queued but not applied while `guest_write_depth > 0` or sealed
replay is active; they are processed afterward. An event storm may collapse into one
full rescan, but it may never mutate HostFS from the watcher thread.

Watcher events are not the only invalidation source. A failed guest→host operation
can change the guest catalog while producing **no host filesystem event**. PR 2's
failed cross-directory rename is the concrete case: `parent_index` must follow the
guest catalog to avoid deleting the real file, and the next host rescan repairs the
phantom/re-adopts the old path. With periodic scans removed, every failed host create,
rename, move, truncate, unlink, or `rmdir` path is audited. Any failure whose recovery
previously depended on a later rescan explicitly schedules the affected old/new
parent directory (or full uncertainty when the scope cannot be proven) for processing
after `guest_write_depth` returns to zero. Internal invalidations use the same dirty
set and owner-thread reconciliation path as watcher events.

#### Loss and degraded mode

Notifications cannot be treated as an infallible journal. Correctness wins over
performance whenever their state is uncertain:

- Native overflow, FSEvents dropped/must-scan flags, queue overflow, root-watch loss,
  a path that cannot be represented without truncation, or a failed Linux
  subdirectory watch sets `rescan_required`.
- The next safe SmartPort touch takes the loss bit, destroys/joins the uncertain
  watcher, arms a replacement root watcher, and performs one unconditional
  `hostfs_rescan`. The scan re-adds every Linux directory watch before enumerating it;
  events arriving on the replacement during recovery remain queued.
- The overflow bit is taken/cleared **before** the full scan. If another loss occurs
  during that scan it sets the bit again and cannot be accidentally acknowledged.
- If notifications are unsupported or cannot be made reliable for the mounted
  filesystem, HostFS warns once and falls back to the existing once-per-second full
  rescan for the rest of that mount. Remount retries watcher setup. This degraded path
  is expensive but complete; it never silently weakens host-change detection.
- If replacement succeeds at the root but rebuilding complete Linux subdirectory
  coverage fails during the recovery scan, the replacement is destroyed and the
  mount enters the same periodic fallback. It must not retry and full-scan on every
  guest touch while the system watch limit remains exhausted.

The mounted root path itself is a boundary, not a catalog entry. If that directory is
moved, removed, or unmounted, the watcher reports loss, HostFS warns, and refresh uses
the configured original path in degraded mode. Recreating a directory there can
recover on a later periodic rescan; following a renamed root outside the configured
mount path is deliberately out of scope.

Healthy local filesystems therefore do zero scan work while idle, common file edits
cost one `stat`, structural changes cost one parent `readdir`, and event loss costs a
single full reconciliation.

Tests use two distinct seams. `tests/util/test_fs_watch.c` exercises each real native
backend with bounded waits. HostFS unit tests bypass OS scheduling by injecting a
normalized root-relative event or setting the loss bit, then call the ordinary
owner-thread refresh path and assert test-visible counters for file stats, immediate-
directory scans, and full rescans. Production and test events therefore share all
reconciliation code after queue ingestion without making the machine suite timing-
dependent.

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

All diagnostics route through one internal seam introduced in PR 1, so every message
is uniform and testable without a `freopen` pipe dance:

```c
static void hostfs_warn(hostfs_volume *vol, const char *fmt, ...);
```

It writes to stderr and bumps a per-volume counter (plus, optionally, the last
message) that tests can read. Roughly 15 lines, and it serves the HostFS diagnostics
across PRs 1, 2, 5 and 6.

```text
a2m: HostFS <root>: volume directory holds 51 entries (ProDOS limit); dropped 9: ZTOOLS, ZZ.TXT, ... (+7 more)
a2m: HostFS <root>/PT3: 'academy!.pt3#000000' -> ACADEMY001.PT3 (renamed for ProDOS; host name unchanged, will not survive copy to .po)
a2m: HostFS <root>: node limit 65535 reached; remaining entries ignored
a2m: HostFS <root>/DEEP: depth limit 8 reached; subtree ignored
```

Name lists cap at 5 plus a `(+N more)` tail so a badly-shaped folder cannot flood the
terminal.

**The existing "directory full" warning is reworded and split.** Today
[`hostfs.c:2848`](../src/machine/hostfs.c) prints one message for both causes, gated
by a single `vol->dir_full_warned` bool. That does not survive the 51 cap:

- [`hostfs.c:2842`](../src/machine/hostfs.c) only attempts `hostfs_grow_subdir_capacity`
  when `parent_index >= 0`, so the **root** always falls straight through to the bare
  `entry == NULL` path.
- After PR 1 the root physically cannot grow, so "remount to pick up new files" is
  false for the root — a remount will never help. The subdirectory case is a genuinely
  different cause (growth failed, out of blocks).
- One shared `dir_full_warned` means whichever cause fires first permanently silences
  the other.

So: two messages, two suppression flags, both via `hostfs_warn`.

```text
a2m: HostFS <root>: volume directory is full (51 entries, ProDOS limit); '<name>' not added - move it into a folder
a2m: HostFS <root>/SUB: directory full; remount to pick up new files
```

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
| `hostfs_warn()` | New; single seam for every diagnostic, with a test-visible counter |
| `vol->block_to_map` | New; direct block → map index replacing the `hostfs_map_find` scan |
| `fs_watch` (`src/util`) | New opaque cross-platform watcher; native thread + bounded event queue + sticky loss flag |
| `vol->watch` | New watcher handle; created before mount scan, stopped/joined on eject |
| `hostfs_rescan_directory_at()` | New immediate-directory host→catalog reconcile; does not descend into existing child directories |
| `hostfs_refresh_file_node()` | New single-file metadata refresh for modify events |
| `HOSTFS_MAX_FILES` | **Deleted.** Replaced by `HOSTFS_MAX_NODES` (65535) and `HOSTFS_ROOT_MAX_ENTRIES` (51, derived) |
| `HOSTFS_ENTRY_LENGTH`, `HOSTFS_ENTRIES_PER_BLOCK` | Promoted from the private enum in `hostfs.c` to `hostfs.h`; `HOSTFS_ROOT_DIR_BLOCKS` added |

---

## Data Model Changes

- `hostfs_volume` shrinks from 345200 B to roughly 200 B plus dynamic allocations.
- Node identity is still the `int` index; nothing persists node indices across a
  `hostfs_rescan`, which is already true today.
- `hostfs.order` format is unchanged. Root order still pins the first 51 entries.
- The watcher queue is bounded rather than proportional to node count. The initial
  256-event target costs roughly 256 KB per mounted volume; overflow deliberately
  trades precision for one full rescan.
- Linux additionally pays one kernel watch plus one small watch-descriptor/path-map
  entry per directory. PR 7's memory accounting includes this platform-specific cost.

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
of worst-case core node-and-name storage there is no problem to solve.

### 6. Directory-mtime skip

Rejected. An ancestor's mtime does not move when a descendant changes, so it still
requires one `stat` per known directory. Worse, resizing an existing file changes no
directory mtime; the current full refresh sees it, while the proposed skip would leave
catalog EOF and storage stale indefinitely. The first draft's claim that block reads
already refresh file metadata was false in source.

### 7. Bounded incremental polling

Rejected as the healthy-mode algorithm; retained as a possible degraded backend
implementation. Walking a fixed number of nodes per touch bounds each pause but makes
detection latency proportional to volume size. To find an assembler output within two
seconds on a 60,000-node volume still requires roughly 30,000 `stat` calls per second.

### 8. Background full scanner

Rejected as primary. Moving the existing walk off the machine thread hides the stall
but still pounds local, network, and sleeping filesystems while nothing changes, and
introduces a thread/queue lifetime problem anyway. A watcher uses that machinery to
perform actual targeted work. A background or synchronous full scan remains the
correct fallback when notification reliability is lost.

### 9. Refresh only when the guest reads a catalog

Rejected as incomplete. A program may open a known file without cataloguing its
directory; newly added files and removed cached directories can remain invisible.
Catalog-time validation may be useful defense-in-depth later but cannot replace host
change detection.

### 10. Third-party watcher library

Rejected for now. The required API is small, while the hard platform difference —
Linux needs one `inotify` watch per directory — remains even behind common wrappers.
Adding a general event-loop or C++ dependency does not remove HostFS's overflow,
watch-limit, queue, or owner-thread responsibilities.

---

## Observability

- All diagnostics to **stderr**, through the single `hostfs_warn` seam added in PR 1.
- Every diagnostic names the volume root and the affected directory.
- `hostfs_warn` keeps a per-volume counter (and optionally the last message) so tests
  assert that a warning fired without capturing stderr. The suite does no `freopen`
  anywhere and this work does not introduce one.
- Suppression is **per cause**, not one bool for the volume — otherwise the first
  warning hides every later one.
- Watcher degradation and event loss also go through `hostfs_warn`:

  ```text
  a2m: HostFS <root>: filesystem notifications unavailable; using periodic full refresh
  a2m: HostFS <root>: filesystem events were lost; performing a full refresh
  ```

  The unavailable warning is once per mount. Event-loss warning suppression is by
  healthy→uncertain transition, so one storm produces one line but a later independent
  loss is visible again.
- Test-visible counters record targeted directory scans, targeted file stats, and
  overflow-triggered full rescans. Timing assertions are not the proof that unchanged
  siblings were untouched.
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
10. **Event-driven refresh is in scope, not a follow-on.** Lifting the node cap while
    retaining the once-per-second full scan produces a volume that mounts and then
    crawls. PRs 4–5 add a cross-platform watcher and targeted reconciliation; a
    healthy idle volume performs no scan work. "Handles any real ProDOS volume" means
    usable, not merely enumerable.
11. **Non-NAPS host files stay silently invisible.** A regular file whose name does not
    parse as `NAME#ttaaaa` is skipped with no diagnostic. This is the single
    deliberate exception to decision 2 — warning on every `README.md`, `.gitignore`,
    and `hostfs.order` would drown the signal from real drops. It also usefully keeps
    stray host files from consuming root slots.
12. **Block lookup gets a direct index, in scope as PR 3.** `hostfs_map_find` is a
    linear scan on every block read and write. Same class of problem as the refresh
    walk: invisible at 256 nodes, dominant at 60,000. 256 KB buys O(1).
13. **All diagnostics go through one `hostfs_warn` seam** with a test-visible counter.
    Warnings that no test asserts regress to silence, which is the bug this work
    exists to fix.
14. **ProDOS format constants live in `hostfs.h`, and 51 is derived** from
    `HOSTFS_ROOT_DIR_BLOCKS * HOSTFS_ENTRIES_PER_BLOCK - 1` rather than written as a
    literal with an explanatory comment.
15. **`hostfs.order` is frozen for the root while the root is truncated**, not merged.
    The persist function cannot tell a cap-dropped name from a guest-deleted one, and
    merging resurrects deleted names forever. Freezing loses nothing and needs no
    merge semantics.
16. **Docs are batched in PR 7, as a deliberate exception to `agents/rules.md`.** That
    note requires a user-visible change to update `manual/manual.md` in the same
    change set; the 51-entry root qualifies. Held anyway: PRs 1–6 land as one
    behavioral program, and documenting the root cap before the collision aliasing and
    diagnostics exist would mean writing the manual section three times. PR 7 is part
    of the same program of work, not a follow-on.
17. **Watcher events invalidate; they do not dictate state.** Every event is verified
    against the host filesystem on the machine-owner thread. Ambiguity widens the
    verification scope from one file to its parent, never to guessed mutation logic.
18. **Notification loss degrades performance, never correctness.** Overflow, watch
    failure, or unsupported filesystems force a full reconciliation and, if necessary,
    the legacy periodic full-refresh mode with a warning.
19. **No watcher callback mutates HostFS.** Native watcher threads own native handles
    and a bounded queue only. `hostfs_maybe_refresh` is the sole event consumer.

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
| Refresh | **Event-driven in PRs 4–5.** Healthy idle volumes do no scan work; file modify = one `stat`; structural change = one parent scan |
| Watch backends | Native: `ReadDirectoryChangesW` (Windows), file-event FSEvents (macOS), per-directory `inotify` (Linux) |
| Watcher ownership | Utility thread queues root-relative invalidations only; machine owner verifies and mutates HostFS |
| Event loss | Sticky out-of-band loss flag → one full rescan; unsupported/unreliable watch → warned periodic-full fallback |
| Mount/new-dir race | Watch before scan; Linux adds each directory watch before its `readdir` |
| Refresh latency | Drain queued events on the next SmartPort touch; no one-second delay in healthy watcher mode |
| Non-NAPS host files | **Stay silently skipped** — the one deliberate exception to "never drop silently" |
| Block lookup | **In scope as PR 3.** Direct `int32_t block_to_map[]` index, 256 KB, O(1) |
| Existing "directory full" warning | **Reworded and split** into root (permanent, ProDOS limit) vs subdirectory (growth failed), with separate suppression flags |
| Subdirectory spare-block floor | **Unchanged.** Root stops consulting it; `bitmap_block` becomes a constant 6 |
| Diagnostic testing | **`hostfs_warn` seam + counter**, not stderr capture. No `freopen` in the suite |
| Constant location | **`hostfs.h`**, with `HOSTFS_ENTRY_LENGTH` / `HOSTFS_ENTRIES_PER_BLOCK` promoted from the private enum so 51 is derived |
| Manual updates | **PR 7**, an explicit exception to `agents/rules.md` same-change-set docs |
| `HOSTFS_MAX_FILES` in PR 2 | **Split.** Node-indexed arrays moved immediately (they overflow once the table grows); per-directory buffers keep an interim `HOSTFS_SCAN_MAX 256` that PR 3 deletes, and warn while it exists |
| Failed host `rename` in reconcile | **`parent_index` moves anyway**, and the failure warns. Holding the node at its old parent makes that parent's next reconcile delete the real host file |
| `hostfs.order` when root is truncated | **Frozen, not merged.** Root persist returns early; subdirectories unaffected |
| `root_truncated` set sites | **Two** — the mount scan and `hostfs_add_node_from_scan`'s root full path. `hostfs_rescan` never re-runs the mount scan |
| `root_truncated` lifetime | **Sticky for the session.** Clears on next mount, never live — a wrong clear costs the user's file, a stale flag costs a reorder |
| Freeze check position | **After** the equality early return, own suppression flag. Otherwise every mount+eject of an over-limit folder warns about a write that never happens |
| Test-visible additions to `hostfs.h` | **Allowed** in the `/* Test helpers. */` block. Goal 7 forbids changing existing signatures, not adding |
| Constant suffix | **No `u`.** Pure move of the enum members; they are compared against `int` in ~10 places |
| Where the root pin lives | **`hostfs_mount`** (`hostfs.c:1963`), not `hostfs_build_volume_directory` |
| Root-overflow warning cadence | **One accumulated line per root scan**, not one per dropped entry |

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
| Full rescan once per second becomes O(volume) | **High** | Event-driven refresh (PRs 4–5). A healthy idle watcher performs no scan; targeted changes verify one file or parent directory |
| Watcher event queue or native queue overflows | **High** | Sticky out-of-band `rescan_required`; take it before one full rescan so a second loss during recovery remains pending |
| Mount or new-directory scan misses a change before watching starts | **High** | Root watch before mount scan; Linux adds each directory watch before `readdir`; never clear events accumulated during the scan |
| Watcher thread races eject / mutates freed HostFS state | **High** | Watcher owns no `hostfs_volume *`; eject stops, wakes, joins, closes, then frees |
| Existing host file resize is missed | **High** | Native modify event directly `stat`s the file and updates EOF/storage/catalog; explicit regression crosses seedling→sapling |
| Failed guest→host mutation emits no event, so state never converges | **High** | Audit every failure path; explicitly dirty proven old/new parents or request full reconciliation after guest-write depth unwinds |
| Linux watch limit or unsupported/network filesystem | Medium | Warn and fall back to complete periodic full refresh; never silently run with partial watch coverage |
| Rename events are split, reordered, or ambiguous | Medium | Treat them as invalidations and rescan old/new parent scopes; filesystem state, not event pairing, is authoritative |
| Guest write produces watcher echo | Low | Apply on machine owner after guest write; verification is idempotent and duplicate paths are coalesced |
| Event storm performs too many targeted scans | Medium | Bounded queue + coalescing; overflow deliberately collapses to one full reconciliation |
| `hostfs_map_find` linear scan on every block access | **High** | Direct `block_to_map` index (PR 3). Otherwise guest disk I/O degrades with volume size, not with transfer size |
| `block_to_map` drifts out of sync with `map` | Medium | Maintain it in the same helpers that insert/remove map entries; assert index/`map` agreement in a debug build |
| Reconcile deletes a host file after a failed rename | **High** | Traced and fixed in PR 2: `parent_index` follows the catalog even when the host rename fails, so the old parent never sees the node as an orphan. Covered by `test_failed_move_keeps_host_file`, which fails if the behavior is reintroduced |
| A directory over 256 entries drops the rest while `HOSTFS_SCAN_MAX` exists | Medium | Warned via the `truncated` out-parameter until PR 3 removes the bound |
| Reworded root warning fires on an existing sample folder | Low | `samples/hostfs/` root holds 5 entries; the message names the fix (move it into a folder) |
| `hostfs.order` rewritten with only the 51 survivors, losing the user's ordering for the rest | Medium | Root persist frozen while `root_truncated` (PR 1). Only bites an already-over-limit folder, and freezing means the file is never touched |
| Root reordering silently fails to persist while over the limit | Low | Second warning line states it explicitly; clears on next mount under 51 |
| Root fills up *after* mount and misses the freeze | Medium | `root_truncated` also set in `hostfs_add_node_from_scan`; `hostfs_rescan` uses the incremental path, not the mount scan |
| `hostfs_find_by_host_path` searches a deleted field | Medium | PR 2 converts it to parent + basename; a naive path-rebuild comparison would be quadratic |
| 51-entry cap breaks an existing sample folder | Low | Single user; `samples/hostfs/` root currently holds 5 entries |
| Memory at a maximally-full volume | Low | Core nodes + names are 4.75 MB; fixed block index and initial watcher queue add about 256 KB each; PR 7 measures block-map, directory-block, and platform-watch costs separately |
| Depth-8 limit still silent | Low | Add the diagnostic in PR 6 alongside the collision diagnostics |

---

## References

- `src/machine/hostfs.c`, `src/machine/hostfs.h`, `src/machine/hostfs_boot.h`
- `src/util/fs_watch.*` — proposed native watcher portability boundary
- `src/util/apple2_file.c` — NAPS parse/compose (`apple2_naps_parse_path`)
- `tests/machine/test_hostfs.c` — 10 existing cases
- `manual/manual.md` — HostFS section (volume layout, `hostfs.order`)
- [Microsoft `ReadDirectoryChangesW`](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-readdirectorychangesw)
- [Apple File System Events Programming Guide](https://developer.apple.com/library/archive/documentation/Darwin/Conceptual/FSEvents_ProgGuide/UsingtheFSEventsFramework/UsingtheFSEventsFramework.html)
- [Linux `inotify(7)`](https://man7.org/linux/man-pages/man7/inotify.7.html)
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
  - [ ] Promote `HOSTFS_ENTRY_LENGTH` / `HOSTFS_ENTRIES_PER_BLOCK` from the private
        enum to `hostfs.h`; add `HOSTFS_ROOT_DIR_BLOCKS` and derive
        `HOSTFS_ROOT_MAX_ENTRIES`
  - [ ] `hostfs_warn(vol, fmt, ...)` seam: stderr + per-volume counter (+ last message)
  - [ ] Pin `dir_block_count` to `HOSTFS_ROOT_DIR_BLOCKS` in `hostfs_mount`
        (`hostfs.c:1963`, where the field is actually assigned);
        `hostfs_build_volume_directory` keeps consuming it
  - [ ] `vol->root_truncated` set in **both** places: the mount root scan, and the
        root branch of the `entry == NULL` path in `hostfs_add_node_from_scan`
        (`hostfs.c:2846`) so a root that fills up after mount is also caught
  - [ ] Freeze check inside `hostfs_persist_order_manifest_in` gated on
        `parent_index < 0`, so all three call sites (1927 / 2564 / 3055) are covered
        and subdirectory manifests are unaffected
  - [ ] Freeze check runs **after** the existing equality early return, with its own
        suppression flag, so the line fires only when a write would have happened
  - [ ] Flag is sticky for the session — never cleared live
  - [ ] Second warning line when the manifest is frozen
  - [ ] Root scan stops accepting children at `HOSTFS_ROOT_MAX_ENTRIES`
  - [ ] Root-overflow warning naming up to 5 dropped entries plus `(+N more)`
  - [ ] Split the existing `hostfs.c:2848` warning into root vs subdirectory text,
        with separate suppression flags; route both through `hostfs_warn`
  - [ ] `hostfs_dir_blocks_for_files` and the `blocks < 2 → 2` floor still apply to
        subdirectories only
  - [ ] Confirm no existing test pins `bitmap_block` to a value other than 6
  - [ ] Test: temp root of 60 NAPS files named `F00`…`F59` (unambiguous
        case-insensitive sort) mounts with exactly 51 active root entries across
        blocks 2–5; `F50` present, `F51` absent; block 2 `prev == 0` and block 5
        `next == 0`; `warn_count == 1` for the drop line
  - [ ] Test: with a 60-line `hostfs.order` present in an over-limit root, a guest-side
        root mutation leaves the file byte-identical
  - [ ] Test: root under the limit at mount, files added to the host until it fills,
        rescan → manifest still byte-identical (the post-mount set site)
  - [ ] `hostfs_warning_count()` / `hostfs_last_warning()` added to the
        `/* Test helpers. */` block
- **Description:** Small, self-contained, and independent of the node-table work.
  Lands the spec compliance decision and the diagnostic seam first, so later PRs build
  on the correct shape and have somewhere uniform to report.

### PR 2 — Dynamic node table + name arena

- **Title:** `hostfs: growable node table, host paths derived from parent walk`
- **Files:** `src/machine/hostfs.c`, `src/machine/hostfs.h`
- **Dependencies:** PR 1
- **Checklist:**
  - [ ] `hostfs_file` → `hostfs_node` with the 56-byte layout
  - [ ] `vol->nodes` / `node_count` / `node_cap`; start 16, double, ceiling `HOSTFS_MAX_NODES 65535`
  - [ ] `vol->names` arena; `uint32_t name_off`; no `char *` in a node
  - [ ] `hostfs_node_host_path()` and conversion of every `file->host_path` read
  - [ ] `hostfs_find_by_host_path` (`hostfs.c:586`) searches **over the field being
        deleted**. Reconstructing a path per node to compare would be quadratic;
        convert it to a `parent_index` + host-basename lookup against the arena. Both
        call sites are in `hostfs_rescan_dir` (2959, 2965)
  - [ ] Fix the `file->host_path` argument passed across the recursive scan
  - [ ] **Audit every `hostfs_node *` capture for an intervening growth point** — list them in the PR body
  - [x] `hostfs_alloc_file_slot` grows; ceiling hit emits stderr, not a silent break
  - [x] `HOSTFS_MAX_FILES` deleted — **partly deferred, see below**
  - [x] All existing tests pass **unmodified**
- **Description:** The load-bearing change, and the one that unblocks `pt3plr`. No
  behavior change is intended beyond the raised ceiling — the existing suite is the
  referee.

**Landed** as `b5c402b`, plus the follow-up below. `sizeof(hostfs_node)` is 56 as
designed; `hostfs_volume` drops from 345200 to 14488 bytes; `samples/hostfs/pt3plr`
mounts all 259 nodes with the player present.

Two things PR 3 inherits rather than rediscovers:

#### `HOSTFS_MAX_FILES` split, not deleted outright

Deleting the constant outright would have pulled all of PR 3's buffer work into PR 2.
The split that was taken instead:

| Buffer kind | Sized by | PR 2 |
|---|---|---|
| `seen` (reconcile), `matched` (rescan) | **node index** | **Moved now.** A fixed 256 array overflows the moment the table grows past it, so these became a small grown bitmap (`hostfs_marks`). Not optional |
| `scans`, `order_names`, `dent ents[]` | entries in **one directory** | Interim `HOSTFS_SCAN_MAX 256`, private to `hostfs.c` |
| `order_basenames` | root entries | Pulled forward: bounded by `HOSTFS_ROOT_MAX_ENTRIES`, which is what takes 64 KB out of the struct |

`HOSTFS_SCAN_MAX` is a **documented per-directory bound, not a volume node budget**,
and PR 3 deletes it. While it exists a directory holding more than 256 mountable
entries would silently drop the rest — the exact bug this work exists to kill — so
`hostfs_collect_scans_in` gained a `bool *truncated` out-parameter and both callers
warn:

```text
a2m: HostFS <dir>: more than 256 entries in one directory; the rest are not in the catalog
```

`hostfs_load_order_file` shares the cap. It was left alone because truncating there
only means later names are not pinned in the catalog order; nothing is dropped.

#### The failed-rename path, traced

PR 2 first made a cross-directory move commit `parent_index` only when the host
`rename` succeeded, on the reasoning that a node should not derive a path that does
not exist. Tracing it to its conclusion showed that is wrong, and destructively so:

> The node keeps `parent_index = SRC`. The guest has already removed its entry from
> SRC's catalog. SRC's next reconcile walks its children, finds a node claiming SRC as
> its parent with no matching directory entry, reads that as a guest-side delete, and
> calls `hostfs_destroy_reconciled_node` — which **unlinks the real host file**.

Measured against the PR 1 build on a forced rename failure (target directory chmod
`0500`): PR 1 keeps `SRC/DATA`, the first PR 2 draft deleted it. So:

- **`parent_index` moves unconditionally**, as it did before PR 2. The node then
  derives a path that does not exist, which the next `hostfs_rescan` resolves — the
  phantom entry is deactivated under the new parent and the real file is re-adopted
  under the old one. Verified end to end.
- **Every failed `rename` now warns.** All three sites were silent, and the file
  rename/retype site discarded the return value outright. A failed host rename is
  precisely the event a user needs to see:

```text
a2m: HostFS move failed: <old> -> <new> (catalog moved, host file did not)
a2m: HostFS rename failed: <old> -> <new> (catalog renamed, host file did not)
```

  Unsuppressed, unlike the directory-full warnings: reconcile runs only on a guest
  directory write, not on automatic host refresh, so these cannot flood.

### PR 3 — Buffers sized from actual counts + O(1) block lookup

- **Title:** `hostfs: size buffers from real counts, index the block map`
- **Files:** `src/machine/hostfs.c`, `tests/machine/test_hostfs.c`
- **Dependencies:** PR 2
- **Checklist:**
  - [x] Scan buffers sized from the `readdir` count for that directory
  - [x] **Delete `HOSTFS_SCAN_MAX`** (PR 2's interim per-directory bound) and with it
        the `truncated` out-parameter on `hostfs_collect_scans_in`, its two warning
        sites, and `test_directory_scan_truncation_warns`, which asserts the interim
        behavior and must be replaced by the 1000-entry cases below
  - [x] `dent ents[]` off the stack, sized from the directory's block chain
  - [x] `used` off the stack, sized from the scan count — `seen` / `matched` already
        moved in PR 2 (they are indexed by node index, so a fixed array overflowed
        as soon as the table grew)
  - [x] `order_basenames` bounded by `HOSTFS_ROOT_MAX_ENTRIES` — done in PR 2
  - [x] `int32_t *block_to_map` direct index replacing the `hostfs_map_find` linear
        scan; maintained wherever `map` is inserted into or removed from; freed in
        `hostfs_eject`
  - [x] Debug-build assertion that `block_to_map` and `map` agree
  - [x] Test: subdirectory with 1000 entries mounts, enumerates, and reads correctly
  - [x] Test: `hostfs_rescan` over a 1000-entry subdirectory
  - [x] Test: block read/write still correct after create + delete churn (index sync)
- **Description:** Removes the hidden per-directory 256 cap, the ~403 KB-per-level
  transient, and the O(map) cost of every block access. Without this, PR 2's raised
  ceiling is unreachable in practice and guest disk I/O degrades with volume size.

**Landed** as `a0d8c59`. The direct index is checked in both directions in Debug
builds; swap-delete updates the removed block and the block moved into its map slot.

### PR 4 — Cross-platform filesystem watcher

- **Title:** `util: add cross-platform filesystem watcher`
- **Files:** `src/util/fs_watch*.c`, `src/util/fs_watch.h`,
  `src/util/CMakeLists.txt`, `CMakeLists.txt`, `tests/util/test_fs_watch.c`
- **Dependencies:** none
- **Checklist:**
  - [ ] Opaque `fs_watch` API with root-relative paths, flags, non-blocking pop,
        Linux add-directory hook, and stop/join destruction
  - [ ] Bounded queue; queue-full and native-overflow use a separate sticky
        `rescan_required` bit
  - [ ] Watcher thread owns native handles only; no callback into callers
  - [ ] Backend readiness handshake: create/start succeeds only after the root watch
        is actually armed
  - [ ] Windows: recursive `ReadDirectoryChangesW`, cancellable overlapped wait,
        create/remove/modify/rename flags, zero-byte/notify-enum loss detection, and
        a filtered parent watch for rename/removal of the watched root itself
  - [ ] macOS: recursive FSEvents stream with file-event flags; dropped,
        must-scan, root-change flags force rescan; link the required system framework
  - [ ] Linux: nonblocking `inotify`; watch-descriptor/path map; create/delete/
        close-write/attrib/move/self/unmount masks; failed watch coverage and directory
        moves force rescan so descriptor paths cannot drift
  - [ ] `fs_watch_add_directory` is idempotent and safe against the watcher thread;
        no-op success on recursive Windows/macOS backends
  - [ ] Stop is safe while the backend is blocked: signal/cancel, wake, join,
        close native resources, destroy queue
  - [ ] Unit: create, modify, rename, and delete under a temp root produce a path
        invalidation or an explicit rescan-required state within a bounded wait
  - [ ] Unit: deterministic queue overflow sets the loss bit even though no queue
        slot is available
  - [ ] Unit: taking the loss bit before recovery does not clear a second loss
  - [ ] Build/test on macOS, Linux, and Windows before landing
- **Description:** Establishes a small independently testable portability boundary.
  It does not know HostFS and cannot mutate machine state.

### PR 5 — Event-driven targeted HostFS reconciliation

- **Title:** `hostfs: reconcile native filesystem events`
- **Files:** `src/machine/hostfs.c`, `src/machine/hostfs.h`,
  `tests/machine/test_hostfs.c`
- **Dependencies:** PR 3, PR 4
- **Checklist:**
  - [ ] `hostfs_volume` owns `fs_watch *`; eject/failure paths stop and destroy it
  - [ ] Watch root before the initial mount scan; Linux adds each directory watch
        before `readdir`; queued mount events are retained
  - [ ] Replace healthy-mode one-second full polling with non-blocking event drain on
        every SmartPort touch; no event performs no `stat` or `readdir`
  - [ ] Coalesce duplicate root-relative paths within one drain
  - [ ] Pure known-file modify → one-file `stat` and metadata reconcile, including
        EOF/storage transitions and block-map/catalog patching
  - [ ] Structural/ambiguous/order event → immediate-parent rescan with local matched
        marks; do not descend into existing child directories
  - [ ] New directory: establish watch first, then recursively materialize its initial
        subtree; depth/node/root limits and warnings remain unchanged
  - [ ] Rename invalidates both reported parents; if complete in-root coverage cannot
        be proven, widen to full rescan rather than leaving a stale old parent; no
        HostFS dependency on native rename pairing or cookies
  - [ ] Public `hostfs_rescan()` remains an unconditional full recursive walk and
        ensures complete Linux watch coverage
  - [ ] Loss bit → destroy uncertain watcher, arm replacement, then one full rescan;
        take bit before recovery and retain events queued by the replacement
  - [ ] Watch unavailable/unreliable → one warning and legacy periodic-full fallback
  - [ ] Guest-write echoes are idempotent; defer event application during
        `guest_write_depth` and sealed replay
  - [ ] Audit failed guest→host create/rename/move/truncate/unlink/rmdir paths; schedule
        old/new parent invalidation or full uncertainty when no watcher event will
        exist
  - [ ] Test-visible counters/seam prove targeted file stats, targeted parent scans,
        and overflow-triggered full scans without timing assertions
  - [ ] Test: deep host add and delete update the correct ProDOS directory while an
        untouched sibling has zero scans
  - [ ] Test: host resize crosses seedling→sapling, updates catalog EOF/storage, and
        the new block reads correctly — regression for the rejected mtime design
  - [ ] Test: external rename/move dirties the correct old/new parents
  - [ ] Test: `hostfs.order` host edit reorders only its directory
  - [ ] Test: synthetic overflow finds an otherwise-unannounced change via exactly
        one full rescan
  - [ ] Test: guest create/write plus its watcher echo does not duplicate, deactivate,
        or rename the node
  - [ ] Test: failed cross-directory move converges on the next safe touch without an
        explicit `hostfs_rescan`, preserves the real file, removes the phantom, and
        re-adopts the old path
- **Description:** A healthy idle volume does no scan work. Common modifications cost
  one `stat`; structural changes cost one parent scan; uncertainty widens once to a
  full authoritative reconciliation.

### PR 6 — Collision-safe ProDOS names + remaining diagnostics

- **Title:** `hostfs: alias colliding names instead of dropping them`
- **Files:** `src/machine/hostfs.c`, `tests/machine/test_hostfs.c`
- **Dependencies:** PR 2, PR 5
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

### PR 7 — Stress, samples, and docs

- **Title:** `hostfs: full-volume stress tests and documentation`
- **Files:** `tests/machine/test_hostfs.c`, `manual/manual.md`, `agents/disk.md`, `design/README.md`
- **Dependencies:** PR 1–6
- **Checklist:**
  - [ ] Stress: deep tree at `HOSTFS_MAX_DEPTH`, several thousand nodes, mount → read → rescan
  - [ ] Stress: node ceiling reached cleanly, with the diagnostic, no crash
  - [ ] Memory accounting for a thousands-of-directories tree: node table + name
        arena + `block_to_map` + watcher queue/Linux watch map + 512 B per directory
        block
  - [ ] Regression: `samples/hostfs/pt3plr/` (256 tunes) boots and the player launches — assert the catalog, not the player's own tune count (see Open question 3)
  - [ ] `manual/manual.md`: 51-entry root, unbounded subdirectories, alias behavior, diagnostics
  - [ ] `agents/disk.md`: durable invariants (51 root, indices-not-pointers, arena
        offsets, watcher events invalidate but never mutate off-owner)
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
