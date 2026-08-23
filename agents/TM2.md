# TM2 — Checkpoint ring + input log + sealed replay

**Status:** Not started.  
**Epic:** [`timemachine.md`](timemachine.md)  
**Prev / Next:** [`TM1.md`](TM1.md) / [`TM3.md`](TM3.md)  
**V1 bar:** Required.  
**Depends on:** TM0 enable flag + budget; snapshot format ([`snapshots.md`](snapshots.md));
max free-run facts ([`max-free-run.md`](max-free-run.md)).

Related: [`rules.md`](rules.md) · [`machine.md`](machine.md) · [`snapshots.md`](snapshots.md) ·
[`turbo-zip.md`](turbo-zip.md) · D5, D5a, D10, D11, D16, D17.

---

## Goal

Record a retained window of **checkpoints + inputs** so tests (and TM3) can answer
"machine state at cycle C" by **re-executing** from the nearest checkpoint under a seal.

This phase records and can materialize into a **scratch** machine for tests. Wiring that
onto the live product `apple2_t` as Inspector mode is **TM3**.

---

## Non-goals

- Replacing live machine during Inspector scrub (TM3)  
- Misc UI (TM4)  
- Rewinding host file content, or scrubbing across a media write (D10 — the window is cut there)  
- Audible scrub audio (D11)  
- Changing HST1 format (HST1 remains the insn index; do not overload it as state)  
- **A write-delta stream** — rejected in D5a; do not reintroduce  
- Promote/Branch (TM6)

---

## Storage model

```text
When timemachine recording ON:

  [CP0] --inputs--> [CP1] --inputs--> ... --inputs--> live head
         ^                                  cadence: cycle-capped (~20k)
         HST1 records and frame ring continue as today (join key: machine_cycle)

  materialize(C) = load nearest CP <= C, then re-execute SEALED to C
```

| Layer | Role |
|-------|------|
| **Checkpoint** | Coherent full machine slice at cycle C_k — `apple2_snapshot_save` blob |
| **Input log** | Timestamped host inputs + nondeterminism seeds between checkpoints |
| **HST1** | Instruction index / TM1 queries (unchanged role) |
| **Frame ring** | Paint samples (unchanged role) |

### Checkpoint = `apple2_snapshot_save`

**Pin: reuse the existing serializer.** `apple2_snapshot_save(m, out, cap)` /
`apple2_snapshot_load(m, in, len)` are already memory-buffer based
([`apple2_snapshot.h`](../src/machine/apple2_snapshot.h)) and already cover
`META CPU_ RAM_ SOFT VID_ SLOT DSKs SPrt MBrd` — including Disk II motor cycles,
`quarter_track_pos` and latches, and full VIA timer state. Load rebuilds banking maps
from soft switches.

Size ≈ 160K (128K main + 32K LC) + slot state. Budget from TM0
(`timemachine_memory_mb`); ~128MB ≈ 800 checkpoints.

- **Do not** hand-roll a "lean" second layout for V1. If profiling later justifies one,
  it must pass the same round-trip tests.  
- **Never** call `apple2_snapshot_flush_media` on this path — it writes dirty disk
  images to the host filesystem, and checkpoint cadence would do that ~50×/second.  
- The snapshot does **not** serialize the framebuffer. After a load the display is
  stale until repainted — TM3 owns that policy.

### Input / nondeterminism log

Everything re-execution cannot derive from the checkpoint, timestamped by `machine_cycle`:

| Source | Where | Handling |
|--------|-------|----------|
| Keyboard | `apple2_set_key`, `key_held` | Log key + cycle |
| Paste | `paste_text` / `paste_index` | In snapshot; log `apple2_paste_on_kbdstrb` consumption if needed |
| Gameport | `apple2_gameport_set_axis/axes/buttons`, `ptrig` | Log value + cycle |
| Disk weak bits / track | `rand()` in [`diskii.c:234`](../src/machine/diskii.c), [`image.c:120,453`](../src/machine/image.c) | **Replace bare `rand()` with a seeded machine-local PRNG**; seed rides in the checkpoint |
| HostFS clock | `clock_gettime` in [`hostfs.c:133`](../src/machine/hostfs.c) | Log returned value per call, or freeze during replay |

The bare `rand()` calls are the one machine-side change this phase needs. Keep it minimal:
a small PRNG in `apple2_t`, seeded at reset, saved/restored with the snapshot. Bump
`A2_SNAPSHOT_VERSION` and keep load-compat for the previous version.

**This phase is the first in the campaign to touch `src/machine/` and the `.a2state`
format.** [`snapshots.md`](snapshots.md) documents a *closed* epic and will be wrong the
moment the version bumps — update it in the same change (new field, new version, load-compat
rule). Do not leave that to a later phase.

### Cadence (initial pin — tune from metrics)

- **Cycle-capped**, not frame-capped: checkpoint every **~20,000 cycles** (≈ one frame at
  1 MHz) so worst-case replay stays under ~1 ms at beam free-run speed (~22–25 MHz,
  [`turbo-zip.md`](turbo-zip.md)).  
- Forced checkpoint on recorder start and on Inspector enter (TM3).  
- **Max free-run:** frame boundaries are ~750k cycles apart at ~45 MHz
  ([`max-free-run.md`](max-free-run.md)) — a frame-capped cadence collapses there. The
  cycle cap handles it, but recording cost per emulated second rises ~45×. Measure, then
  pin one of: cycle-capped as normal, degraded cadence under max, or recording refused
  in max with an honest message. **Document the choice in Landed.**
- Drop oldest checkpoint (and its input-log span) when over budget; keep honesty counters
  (dropped, oldest cycle retained).

---

## Sealed replay (D16) — the correctness core

Re-execution runs real code, so every side-effect path must be muted for the duration.
**A leaky seal corrupts the tape you are standing on.**

| Gate | Mechanism |
|------|-----------|
| CPU observer | `apple2_set_cpu_observer(m, NULL, NULL)` — else replay appends duplicate HST1 records ([`runtime_thread.c:133`](../src/runtime/runtime_thread.c)) |
| Memory-access callback | Detach `runtime_on_memory_access` — it is installed permanently ([`runtime_thread.c:4200`](../src/runtime/runtime_thread.c)) and would fire watchpoints/BPs spuriously |
| Frame ring / publish | No `runtime_frame_ring_push`, no live-slot publish until the head lands |
| Audio | Simply do not call `runtime_produce_audio` — it is runtime-driven, not machine-driven |
| Media write-through | **Safety net, not correctness.** The window never spans a guest media write (see below), so replay cannot re-execute one. Keep a machine-level replay flag that drops Disk II image writes and HostFS write-through anyway, so a bug in the truncation rule can never reach a user's files |

Implement the seal as one scoped enter/exit pair, not five scattered flags, and assert
on exit that recorder counters are unchanged.

---

## Media writes cut the window (D10)

**The rule: a guest media write that _succeeds_ truncates the TimeMachine window at that
cycle.** A write the drive refuses changes nothing, so it must not cut anything. Records,
checkpoints and frames older than the write are dropped; recording continues forward from
there. You can never scrub back across a disk write.

**Why, in one line:** media bytes live outside the checkpoint, so a replayed read returns
*present-day* bytes, not the bytes that were there at the target cycle. That is true for any
read of a location written anywhere between the target and now — not just within one replay
span. There is no cheap way to reconstruct it, and flagging it would leave the user equally
unable to debug. Cutting the window keeps one clean invariant instead:

> Everything inside `tm_window` replays exactly. No asterisk.

This also removes two problems rather than one: replay can never re-execute a write (the
window starts after the last one), and replayed reads are always correct.

### What counts as a guest media write

| Truncates | Anchor |
|-----------|--------|
| Disk II track write | the points that set `dirty_tracks[track] = 1` — [`image.c:516,556`](../src/machine/image.c) |
| HostFS guest block write | `hostfs_write_block` on success; use the existing `guest_write_depth` ([`hostfs.c:114,2629`](../src/machine/hostfs.c)) as the guest discriminator |
| Host directory changed underneath | order-manifest rewrite during refresh, which already short-circuits when the catalog is unchanged ([`hostfs.c:1762`](../src/machine/hostfs.c)). **Pinned: truncates.** The media moved under the tape, so the old window is not replayable — same rule, different cause code |

**"Succeeds" is structural for Disk II — do not add a redundant check.**
`image_put_byte` returns `A2_ERR` on `!nib->writable` *before* touching `file_data` or the
dirty flags ([`image.c:490`](../src/machine/image.c)), and `image_finish_write` is only
reachable once `write_active` was set inside that same guarded path. So anchoring on
`dirty_tracks[track] = 1` already means "a write that landed." A write-protected image
cannot cut the window, by construction.

Also note only `IMG_NIB` can be written at all — `IMG_DSK` and `IMG_WOZ` fall through
`image_put_byte` to `A2_ERR` (mounted `.dsk` is decoded to NIB internally, per the
`dsk_backed` flag). **WOZ is read-only today**, which matters below.

**HostFS has no write-protect concept at all** (no writable/read-only flag anywhere in
`hostfs.c`), so "succeeds" there means checking the return code. Truncate on *both*
host-backed writes and RAM-backed block writes (`hostfs_map_add_ram` for meta/index/newly
allocated blocks) — no HostFS volume state rides in the checkpoint, so either kind breaks
replay.

| **Must not truncate** | Why |
|---|---|
| `hostfs_flush` on eject ([`hostfs.c:1905`](../src/machine/hostfs.c)) | housekeeping, not guest activity |
| `apple2_snapshot_flush_media` | save-path housekeeping |
| Writes dropped during a sealed replay | by construction there are none; if one appears, that is a bug, not a truncation trigger |

### Shape

- **Truncate old, do not clear all.** Advance `tm_window.oldest` to the write cycle and keep
  recording forward. A full `runtime_history_clear` would throw away recording the user wants
  back a second later.  
- Append **one** new `runtime_history_marker_kind` — `RUNTIME_HISTORY_MARKER_MEDIA_CHANGED`
  (next free value, 13) — with the cause in `marker_arg0`. **Do not add two marker kinds for
  the two triggers.** This follows the convention already set by `RESET_COMPLETE` /
  `runtime_history_reset_kind` and `CLOCK_DISCONTINUITY` /
  `runtime_history_clock_discontinuity_kind`:

  ```c
  typedef enum runtime_history_media_change_kind {
      RUNTIME_HISTORY_MEDIA_CHANGE_UNKNOWN = 0,
      RUNTIME_HISTORY_MEDIA_CHANGE_GUEST_WRITE = 1,
      RUNTIME_HISTORY_MEDIA_CHANGE_HOST_DIRECTORY = 2
  } runtime_history_media_change_kind;
  ```

  Keep the `UNKNOWN = 0` member — both existing sub-kind enums have one, and it is what an
  older reader falls back to. Put the slot/device in `marker_arg1` where known, so the UI can
  say `disk write, s6d1 @ cycle N` rather than just `disk write`.  
- **Order matters: append the marker, then truncate _to_ it, not past it.** The marker must
  survive as the new oldest retained record — drop it and the window loses the one thing that
  explains its own left edge.  
- **The marker enum is wire-visible.** Markers surface through history FIND/READ, so a new
  value is not purely internal: verify the encode/decode path in `runtime_history_wire.c`
  handles an unknown-to-older-clients value additively, and add a line to
  [`control-tools.md`](control-tools.md) so agents reading history know what the new marker
  means. Note in Landed whether this needed an A2M bump (expected: no, additive).  
- Keep an honesty counter of truncations, for the same reason as the drop counters.

### Known consequence (expected, not a bug)

Narrower than it first looks, because only a **successful** write counts:

- Write-protected images cannot truncate — the guard above fires first.
- WOZ cannot truncate — it is read-only in `image_put_byte` today. Since WOZ is the
  preservation format for copy-protected disks, the protection-check worry largely
  evaporates; and protected originals shipped notch-covered anyway.
- HostFS *can* truncate freely, having no write-protect concept.

So the realistic trigger is a user saving to a writable disk or a HostFS folder mid-session
— which will chop the window during exactly the sessions they may want to scrub. That is
the honest cost of the invariant. Do not paper over it — surface the marker.

---

## APIs (worker-internal + testable)

Illustrative names:

```text
runtime_tm_recorder_set_enabled(rt, bool)          // gated by TM0 master
runtime_tm_checkpoint_take(rt)                     // force CP at current cycle
runtime_tm_materialize(rt, cycle, apple2_t *dst)   // load CP<=cycle + sealed re-exec
runtime_tm_window_info(rt, ...)                    // tm_window: oldest/newest cycle, counts, dropped
```

- `runtime_tm_window_info` must report the **`tm_window` intersection** (D17) of HST1,
  frame ring and checkpoint coverage — not checkpoint coverage alone. TM1 clamps to it.  
- `materialize` in TM2 targets a **scratch** `apple2_t`; it must not disturb the live machine.  
- When recording **off**: zero cost on the insn path.

---

## Opt-in gating

- TM0 master off → no checkpoint/input-log work.  
- When master on: start/stop with product record policy (per TM0 Landed).  
- Clear on history clear / state load / reset (same class of events that already
  invalidate rings — see sessions mutation set). Checkpoints must not be replayable
  across an epoch boundary.

---

## Code anchors

| Area | Path |
|------|------|
| Checkpoint serializer | `src/machine/apple2_snapshot.c` / `.h` (`save`/`load`/`size`) |
| Step primitives | `apple2_step_cycles`, `apple2_cycles` in `src/machine/apple2.h` |
| Seal: CPU observer | `apple2_set_cpu_observer`, `runtime_thread.c:133` |
| Seal: mem callback | `apple2_set_memory_access_callback`, `runtime_thread.c:4200` |
| Seal: audio | `runtime_produce_audio` in `runtime_thread.c` |
| Seal: media | `src/machine/image.c`, `src/machine/hostfs.c` write paths |
| PRNG determinism | `diskii.c:234`, `image.c:120,453` |
| Frame push | `runtime_frame_ring_push` call sites in `runtime_thread.c` |
| History lifecycle | `runtime_history_*` record on/off/clear |
| New module | `src/runtime/runtime_timemachine*.c` (extend TM1 module or sibling) |

---

## Testing

| Test | Expect |
|------|--------|
| CP round-trip | take CP → load into scratch → CPU/mem/flags/Disk II/VIA match |
| Mid-window | run N insns with recording → materialize at mid cycle → match golden capture |
| **Determinism** | materialize same cycle twice → byte-identical results |
| **Seal: observer** | HST1 record count unchanged across a materialize |
| **Seal: watchpoints** | armed watchpoint does not fire during materialize |
| **Seal: frame ring** | frame ring count/contents unchanged across a materialize |
| **Seal: media** | mtime + bytes of a mounted image unchanged across a materialize |
| **Media truncate** | guest disk write → window oldest lands **on** the `MEDIA_CHANGED` marker (marker retained); forward recording continues |
| **Housekeeping does not truncate** | eject flush / snapshot flush / unchanged-catalog refresh leave the window intact |
| **Host dir change truncates** | add a file to a mounted HostFS folder from the host → window cuts with cause `HOST_DIRECTORY` |
| **Refused write does not truncate** | write-protected image: guest write attempt → window unchanged, no marker, image bytes unchanged |
| Budget drop | fill past budget → oldest advances; `tm_window` honest |
| Off path | enable off → insn path does not grow TM buffers |

Prefer deterministic headless runtime tests (pattern: `tests/runtime/test_runtime_history_*.c`).

---

## Acceptance checklist

- [ ] Checkpoint ring using `apple2_snapshot_save`/`load`, versioned, behind TM0 enable  
- [ ] Input/nondeterminism log; `rand()` replaced by seeded PRNG in the snapshot  
- [ ] `materialize(cycle, dst)` restores CPU/mem/softswitches/beam/Disk II/VIA in window  
- [ ] **Sealed replay with a test per gate** (observer, watchpoint, frame ring, audio, media)  
- [ ] `tm_window` reported as the intersection (D17)  
- [ ] Cadence + budget + drop policy + **max-free-run behaviour** documented in Landed  
- [ ] Recording off = no TM cost on hot path  
- [ ] Guest media write truncates the window + retained `MEDIA_CHANGED` marker; housekeeping and refused writes do not  
- [ ] `snapshots.md` updated for the PRNG field + `A2_SNAPSHOT_VERSION` bump + load-compat  
- [ ] `control-tools.md` documents the new history marker; wire path verified additive  
- [ ] Truncation counter exposed; `tm_window` reflects the cut immediately  
- [ ] Build + full ctest green (new tests in gate)  
- [ ] Landed filled  

---

## Agent script

```text
1. Read agents/rules.md, agents/timemachine.md D5/D5a/D10/D11/D16/D17,
   agents/TM0-TM1 Landed, agents/snapshots.md, agents/max-free-run.md, agents/TM2.md.
2. Seeded PRNG + snapshot version bump; checkpoint ring; input log.
3. Media truncation + MEDIA_CHANGED marker (get the guest-vs-housekeeping split right).
4. Implement the seal as one scoped enter/exit; materialize-to-scratch.
5. ctest goldens + one test per seal gate + truncation tests; measure size/cadence;
   pin constants in Landed.
6. Do not wire Inspector replace-live yet (TM3). Stop.
```

---

## Landed

_(empty until implemented)_
