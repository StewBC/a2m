# Machine snapshots (save / load)

**Status:** **Done** (2026-08-10)  
**Goal:** Durable Apple II machine snapshots — save, load, drop-on-window,
`--sna`, control-port, and c64m-parity quicksave/quickload keys. **No Misc UI
load/save buttons** in this epic (shell intents may stay; no panel work).

**Sibling gold:** `../c64m` — reuse architecture, host loop, and runtime
orchestration; only the **machine payload** is Apple-shaped.

Related: [`runtime.md`](runtime.md) ·
[`rules.md`](rules.md) · [`disk.md`](disk.md).

---

## Why this is mostly a port, not a greenfield

c64m already has a complete product path. a2m already copied **much of the
shell wiring** during wholesale and then left the **worker + machine** empty.
The work is: **fill the middle** with an `apple2_snapshot` module and
`runtime_thread` handlers, then finish the few host gaps.

### Already present in a2m (do not reinvent)

| Layer | What exists | Files / notes |
|-------|-------------|----------------|
| Client API | `runtime_client_save_state` / `load_state` | `runtime_client.c` |
| Commands / events | `RUNTIME_COMMAND_{SAVE,LOAD}_STATE`, `RUNTIME_EVENT_{SAVE,LOAD}_STATE_COMPLETE` | `runtime_command.h`, `runtime_event.h` (`state_file.path`) |
| Host drop | `.a2state` → `runtime_client_load_state` | `main.c` `handle_drop_file` |
| Host intents | File-browser purpose → save/load state | `main.c` `dispatch_intent` |
| CLI | `--sna` → `options->sna_path` | `app_options.*` (**not yet applied** after start) |
| Control wire | `save-state` / `load-state` deferred RPC | `control/control_dispatch.c` already calls client |
| Paths browse slot | `[browse] snapshot` / quicksave folder | `app_options` + Configure Paths |
| ctest sketch | `tests/runtime/test_runtime_savestate.c` | **Deferred** — not in gate; expects live worker |

### Missing (this epic)

| Layer | Gap |
|-------|-----|
| Machine | No `apple2_snapshot.c` / `.h` (serialize/deserialize) |
| Runtime worker | No `RUNTIME_COMMAND_SAVE_STATE` / `LOAD_STATE` cases; no file I/O helpers; no post-load history/frame-ring clear |
| Host | No `--sna` apply after mounts; no Opt+Shift+./, quicksave/quickload (parked in `main_c64m.c` / live in sibling c64m) |
| Tests | Gate does not run savestate; no pure machine unit test |

---

## c64m reuse map (mandatory method)

**Rule:** For each surface below, open the c64m file first, port the *shape*,
then substitute Apple types. Do not invent a second snapshot architecture.

| Piece | c64m source | a2m target | Reuse notes |
|-------|-------------|---------------|-------------|
| Chunked binary format | `src/machine/c64_snapshot.c` / `.h` | `src/machine/apple2_snapshot.c` / `.h` | Same pattern: fixed header (magic + version), 4-char tags, little-endian writers, `size` / `save` / `load` API |
| Writer/reader helpers | static `w_u8`…`r_u64`, tag begin/end | Copy into apple2_snapshot (or share a tiny util later — **not** required for V1) | Keep machine-local first (c64m does) |
| Content modes | `CONTENT_REFERENCED` / `SELF_CONTAINED` | Same enum names with `A2_` prefix | V1 ships **referenced media paths** only |
| External media flags | `C64_SNAPSHOT_FLAG_EXTERNAL_MEDIA_REFERENCES` | Apple equivalent | Set when Disk II / SP paths are stored as paths |
| Runtime save path | `runtime_save_state` in `runtime_thread.c` | Same function names/shape in product runtime | malloc → serialize → write file → publish complete |
| Runtime load path | `runtime_load_state` | Same | read file → load → clear history + frame ring → restore exec pause/run → publish complete + CPU/machine |
| Finish mid-instruction | `runtime_finish_pending_state_snapshot_instruction` | Port adapted to Apple step | Save at instruction boundary when possible (c64m does this for VIC alignment; Apple needs coherent CPU micro state **or** finish micro) |
| File I/O helpers | `runtime_read_file_bytes` / `runtime_write_file_bytes` | Port into product `runtime_thread.c` if not already shared | Local statics in c64m — copy |
| Publish complete | `runtime_publish_state_file_complete` | Port | Echo path in event |
| Drop file | `handle_drop_file` + `.c64state` | Product already uses `.a2state` | Keep extension; ensure load works once worker exists |
| Quicksave path | `make_quicksave_path`, `sanitize_snapshot_stem`, `find_newest_state_file` | Port into product `main.c` | Stem from active disk/hd/`--sna`; extension **`.a2state`** |
| Quick keys | Opt+Shift+`.` save, Opt+Shift+`,` load | Port same chords | No UI panel |
| Startup `--sna` | after create/start + mounts | Wire in product `main.c` | c64m loads after worker ready |
| Control | deferred save/load | **Already wired** | Completes once worker emits events |
| Tests | `test_c64_snapshot.c` + runtime savestate | `test_apple2_snapshot.c` (machine) + enable `runtime_savestate` | Machine unit does not need SDL; runtime test can stay as-is with fixes |

### Explicitly **do not** copy from c64m

| Item | Why |
|------|-----|
| VIC/CIA/SID/CART/1541 chunks | Wrong machine |
| `C64_SNAPSHOT_*` magic/version | New Apple magic/version |
| 1541 mid-load special tests | Not applicable |
| Misc UI “Load state…” polish | Out of scope this epic |

---

## Product decisions (locked for V1)

| Question | Decision |
|----------|----------|
| Extension | **`.a2state`** only (drop, quicksave, docs). Accept legacy typos only if cheap; test currently uses `.a2s` → fix to `.a2state`. |
| Format | Versioned **chunked LE** binary (c64m style), not `memcpy` of `apple2_t`. |
| Magic | Distinct 32-bit magic (suggest `0x41325354` = `'A''2''S''T'` LE — pick one, document in header). |
| Version | Start at **1**; `VERSION_MIN = 1`. Bump when chunk layouts change. |
| Media | **Referenced paths** for Disk II queue + SmartPort files. Re-open files on load. If a path is missing → fail load with clear error (do not silently boot empty). |
| Dirty Disk II | V1: **flush dirty tracks to disk image before save** when file-backed, *or* fail save if dirty and not flushable. Prefer flush-via existing `image_save` / `diskii_save` when available. Full embedded-track self-contained mode is **V2** (flag reserved). |
| ROM images | **Not embedded.** Use built-in product ROMs for model; optional hash check in META (warn or fail — prefer **fail on model mismatch**, soft-warn on ROM hash if easy). |
| Framebuffer | **Not serialized.** On load: restore beam counters + RAM; paint one frame (or let free-run paint). |
| Page maps / function pointers | **Never serialize.** After load: `softswitch_apply_full_map` (or equivalent full remap) from restored `state_flags` + slot types. |
| Observers / paste / write_history | Host/runtime: keep or clear. **Clear** paste on load; **clear** write_history (or leave zeroed); re-attach observers after load like c64m history sync. |
| History + frame ring | On load: **clear** CPU history and frame ring (c64m does — cycle epochs invalidate rings). |
| Pause policy | Preserve was-running vs paused across load (c64m). Drop-load does not force pause unless already paused. |
| Breakpoints | **Not** inside machine snapshot. BPs are runtime/debugger state and survive load (same as c64m). Document: BP PC may no longer make sense after load — user problem. |
| Model change | Snapshot stores model; load may change `//e` ↔ `][+` if file says so (re-apply CPU class). |
| UI panels | **Out of scope.** No Misc load button work. File-browser intents may already exist — leave as-is if free; do not polish. |
| Named slots / rewind UI | **E3 later** — this epic is path-based save/load only. |

---

## Architecture

```text
  Host (main.c)                    Runtime worker                 Machine
  ─────────────                    ──────────────                 ───────
  drop .a2state ──┐
  --sna ──────────┤
  Opt+Shift+. /,──┼─► runtime_client_save/load_state
  control save/ ──┤         │
  load-state ─────┘         ▼
                    RUNTIME_COMMAND_*_STATE
                            │
                    read/write file bytes
                            │
                    apple2_snapshot_save/load
                            │
                            ▼
                    apple2_t  (RAM, CPU, flags,
                              beam, slots, media
                              paths + mech state)
                            │
                    clear history + frame ring
                    remap banking / slots
                    publish COMPLETE + CPU state
```

**Ownership rules** ([`rules.md`](rules.md)): serialize only on the **runtime
thread**; host never touches `apple2_t`.

---

## Chunk plan (V1)

Tag names illustrative (4 bytes, same `TAG('A','B','C','D')` helper as c64m).

| Tag | Contents | Required |
|-----|----------|----------|
| **META** | version aux, flags, content_mode, model, mb_slot, optional ROM hashes | Yes |
| **CPU_** | `CPU` regs + `cpu65_t` micro state (opcode/phase/target/…) **without** function pointers (`user`, `read`, `write`, irq fns reattached after load) | Yes |
| **RAM_** | `ram_main` 128K + `ram_lc` 32K (][+ still stores 128K aux half zeros or only 64K — pick: always 128K+32K for simpler layout) | Yes |
| **SOFT** | `state_flags`, `key_held`, `strobed_slot`, speaker_level, gameport axes/buttons/ptrig_cycle | Yes |
| **VID_** | beam: `cycle_in_line`, `line`, `frame_number`, `last_video_byte`, `paint_enabled` (not fb) | Yes |
| **SLOT** | `slot_type[1..7]`, `diskii_present`, `mb_slot` | Yes |
| **DSKs** | Per Disk II slot that is present: controller active drive, each drive motor/head/q6/q7/latches, **image queue paths** + active index + kind metadata | If any Disk II |
| **SPrt** | Per SP slot: paths for devices 0/1, header sizes, buffer/status offsets | If any SP |
| **MBrd** | Per MB slot: VIA6522 + AY38910 **regs/timers** (not host render accum) | If any MB |

**Rebuild after load (order):**

1. Validate header/version/required chunks.  
2. Tear down old media handles carefully (eject/unmount) without freeing whole machine.  
3. Restore model + attach slot cards to match SLOT.  
4. Restore RAM bytes.  
5. Restore SOFT scalars; call **`softswitch_apply_full_map`**.  
6. Restore CPU payload; re-bind `cpu.user` / bus callbacks.  
7. Restore video beam; `frame_ready = false`; optional full-frame paint.  
8. Remount media from paths; restore mechanical Disk II fields; restore SP/MB chips.  
9. Clear paste; zero write_history if allocated.  
10. Runtime: clear history + frame ring; publish.

---

## Phases

### S0 — Spec in tree + extension hygiene

- Add `apple2_snapshot.h` with magic, version, public API:

  ```c
  size_t apple2_snapshot_size(const apple2_t *m);
  size_t apple2_snapshot_save(const apple2_t *m, uint8_t *out, size_t out_cap);
  bool   apple2_snapshot_load(apple2_t *m, const uint8_t *in, size_t in_len);
  ```

- Document decisions in this file (keep in sync).  
- Unify extension to **`.a2state`** in test + any stray `.a2s`.  
- **Done when:** header compiles; no behaviour yet.

### S1 — Core machine snapshot (no media)

Port c64m writer/reader skeleton; implement META + CPU_ + RAM_ + SOFT + VID_ + SLOT (empty cards OK).

- Unit test `test_apple2_snapshot` (no SDL): init machine → write RAM marker + step → save → mutate → load → assert PC/cycles/RAM/flags/beam.  
- After load, banking maps must match flags (read `$C013` style via debug or known softswitch).  
- **Done when:** ctest machine target green.

### S2 — Media + peripherals

- Disk II: paths + mechanical state + active index; remount on load.  
- SmartPort: remount paths; restore handshake buffer if cheap.  
- Mockingboard: VIA/AY state.  
- Dirty policy as locked above.  
- Extend unit test with mount fixture NIB if practical (or runtime test covers boot+save).  
- **Done when:** save mid-DOS / mid-title, load, still coherent (manual + automated where possible).

### S3 — Runtime worker (c64m port)

In product `runtime_thread.c`, port from c64m:

1. `runtime_read_file_bytes` / `runtime_write_file_bytes`  
2. `runtime_publish_state_file_complete`  
3. `runtime_finish_pending_state_snapshot_instruction` (Apple-adapted)  
4. `runtime_save_state` / `runtime_load_state` calling `apple2_snapshot_*`  
5. `case RUNTIME_COMMAND_SAVE_STATE` / `LOAD_STATE`  
6. On load: `runtime_history_clear_for_state_load` (or existing clear), `runtime_frame_ring_clear`, re-sync CPU observer, preserve pause/run  

Failures → `runtime_publish_error` (c64m pattern); success → COMPLETE event with path.

- Enable **`runtime_savestate`** in CMake/ctest gate.  
- Fix test expectations if event has no `ok` field (c64m: success = COMPLETE, failure = ERROR).  
- **Done when:** `test_runtime_savestate` green in gate.

### S4 — Host surface (minimal UI)

Port from c64m `main.c` / parked `main_c64m.c` into product `main.c`:

| Surface | Action |
|---------|--------|
| Drop | Already `.a2state` → load; verify + log success/fail from events if easy |
| `--sna` | After runtime start + initial mounts, `runtime_client_load_state(client, options.sna_path)` |
| Quicksave | Opt+Shift+`.` → timestamped file under snapshot browse dir, stem from active media |
| Quickload | Opt+Shift+`,` → newest `.a2state` in that dir |
| Control | Smoke: `save-state` / `load-state` via existing dispatch (no protocol change) |

**Not in S4:** Misc panel buttons, Configure snapshot list, multi-slot UI (E3).

- **Done when:** drop + `--sna` + quick keys work manually; control optional smoke.

### S5 — Docs + gate

- Update [`status.md`](status.md), [`runtime.md`](runtime.md), [`testing.md`](testing.md), user [`manual/manual.md`](../manual/manual.md) (keys + `--sna` + drop).  
- Mark this epic **Done** when exit criteria met. Residuals: named slots, self-contained media.

---

## Exit criteria

- [x] `apple2_snapshot_{size,save,load}` in `src/machine/`, linked into product  
- [x] Round-trip preserves CPU, main/aux/LC RAM, softswitches, beam, slots, Disk II/SP/MB as specified  
- [x] Worker handles save/load; history + frame ring cleared on load  
- [x] Drop `.a2state` loads; `--sna` loads after start  
- [x] Opt+Shift+`.` / `,` quicksave/quickload (c64m chords, `.a2state`)  
- [x] Control `save-state` / `load-state` complete successfully against live worker  
- [x] `test_apple2_snapshot` + `runtime_savestate` in ctest gate and green  
- [x] No new Misc UI required for the feature to be usable  

---

## Testing plan

| Test | Level | Asserts |
|------|-------|---------|
| `test_apple2_snapshot` | machine | size/save/load round-trip; bad magic/version fail; RAM+PC |
| `runtime_savestate` | runtime | save → mutate → load → PC/cycles/RAM; COMPLETE events |
| Manual | product | boot DOS fixture → quicksave → run → quickload; drop file; `--sna` |
| Control | optional | `a2m_control_client.py` save-state / load-state |

---

## Risks / sharp edges

| Risk | Mitigation |
|------|------------|
| Serializing pointers / page maps | Never write pointers; always `softswitch_apply_full_map` |
| Mid-instruction CPU micro state | Finish instruction before save **or** fully serialize micro fields and rebind callbacks |
| Missing media path on another machine | Fail load loudly; V2 self-contained optional |
| Dirty NIB/WOZ not flushed | Flush or fail save |
| Large files | Referenced media keeps snapshot ~160KB+ RAM; fine |
| History/frame after load | Clear rings (wrong cycle domain) |
| Extension confusion `.a2s` vs `.a2state` | One extension only |

---

## Residuals (not this epic)

| Item | Backlog |
|------|---------|
| Named slots / rewind UI | E3 |
| Self-contained embedded disk images | Future flag / content mode |
| Misc Load/Save buttons + file dialog polish | Later UI pass |
| Snapshot of breakpoint set / turbo ladder | Optional later; not machine state |

---

## Implementation order (execute)

```text
S0 header/spec → S1 core machine + unit test → S2 media
    → S3 runtime worker + enable runtime_savestate
    → S4 host drop/--sna/quick keys
    → S5 docs / status
```

Prefer **one PR-sized commit per phase** when possible; keep ctest green after S1+.

---

## Reference paths (absolute relative to workspace)

| Role | Path |
|------|------|
| Gold format | `../c64m/src/machine/c64_snapshot.c` |
| Gold runtime | `../c64m/src/runtime/runtime_thread.c` (`runtime_save_state` / `load_state`) |
| Gold host | `../c64m/src/main.c` (quicksave, drop, `--sna`) |
| Parked host copy | `src/main_c64m.c` (same patterns, may lag sibling) |
| Product host gaps | `src/main.c` |
| Product machine | `src/machine/apple2.h` + new `apple2_snapshot.*` |
| Existing test | `tests/runtime/test_runtime_savestate.c` |
