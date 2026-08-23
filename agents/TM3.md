# TM3 — Materialize into one true state

**Status:** Landed.  
**Epic:** [`timemachine.md`](timemachine.md)  
**Prev / Next:** [`TM2.md`](TM2.md) / [`TM4.md`](TM4.md)  
**V1 bar:** Required.  
**Depends on:** TM1 (tape focus/verbs), TM2 (checkpoint ring + sealed replay materialize).

Related: [`rules.md`](rules.md) · [`sessions.md`](sessions.md) · [`control-tools.md`](control-tools.md) ·
[`timemachine.md`](timemachine.md) (D2, D3, D4, D9, D12, D16, D17, D18).

---

## Goal

When forensic/Inspector mode is active, TimeMachine **replaces** the live `apple2_t`
with reconstructed past at the tape head. Existing views keep reading the machine;
as the head advances, state is rebuilt and display/softswitches update “for real.”

Exit forensic mode → restore **live NOW** anchor. Machine must not be left in a random past.

---

## Non-goals

- Misc Inspector tab (TM4). TM3 lands the runtime mode + client/control enter-exit; a
  debug hook is enough to smoke it — there is no F7 shell to borrow (D14)  
- Forensic BP store (TM5)  
- Promote/Branch (TM6)  
- Rewinding host file content, or scrubbing across a media write (D10)  
- Dual isolated agent world  

---

## Mode model

| Mode | Machine contents | Mutations |
|------|------------------|-----------|
| **Live** | NOW | read-write (existing rules) |
| **Forensic** | Materialized THEN at tape head | **read-only** (runtime rejects pokes; UI should too) |

**One true state (D2/D9):** `get-cpu` / `get-memory` / softswitches / published frame
reflect whatever is in `apple2_t` now — past while forensic scrubbed.

### Enter forensic

1. Pause if running (stable window).  
2. Take the **live NOW anchor**: a full `apple2_snapshot_save` blob. **Pinned** — this
   must be a complete snapshot, identical in kind to a TM2 checkpoint (D5). Anything
   less loses Disk II mechanical state and VIA/AY state on exit, which is a correctness
   bug, not a caveat.  
3. Ensure `tm_window` is non-empty (D17); if empty, fail enter honestly.  
4. Set mode=forensic; materialize to initial focus (usually newest or selected cycle).  
5. Publish state + `state-changed` (reason: forensic enter / seek).  

### Seek / step on tape (forensic)

1. TM1 query moves focus (id/cycle), clamped to `tm_window`.  
2. `materialize(focus.cycle)` into live `apple2_t` — load nearest checkpoint ≤ cycle,
   then **sealed re-execution** to the target (TM2 engine, D16).  
3. After the checkpoint load: `softswitch_apply_full_map` (page maps) and
   `apple2_video_reseed_from_cycles` (beam position). `apple2_snapshot_load` already does
   the banking rebuild — do not duplicate it, but do verify the beam.  
4. Publish CPU, memory-dependent views, softswitches, display.  
5. `state-changed` so peers see shared mutation.  

**Forward scrub:** keep the restored machine hot and continue executing under the seal —
no reload. **Backward:** rebuild from an earlier checkpoint and re-execute (D12). With a
~20k-cycle cadence the worst case is ~1 ms, so both paths can be naive first; optimize
only if measurement says so.

### Display under the head — pinned

`apple2_snapshot_load` does **not** restore the framebuffer, so the display is stale until
you act. Two sources, and they are not equivalent:

| Source | Truth? | Use |
|--------|--------|-----|
| **Sealed re-execution with beam paint on** | **Yes** — reproduces mid-frame softswitch changes | **Default.** Re-execute from the checkpoint at frame start so the beam repaints naturally |
| Frame ring sample ≤ cycle | Whole-frame only | Fallback when the head is outside checkpoint coverage, and for the TM4 scrubber's rough preview |
| `apple2_video_paint_full_frame` | **No** | Only as a last-resort fill. It paints the entire frame from *current* switch state, so any split-screen / mid-frame mode change renders wrong. Never present it as the recorded picture |

This is a real fidelity difference on any title that switches modes mid-frame. Pin which
path shipped, and its limits, in Landed.

### Exit forensic (non-promote)

1. Restore live NOW anchor (`apple2_snapshot_load`) into `apple2_t`, then re-apply the
   full softswitch map and reseed the beam.  
2. Mode=live; clear forensic read-only; re-arm the recorder if TM is enabled.  
3. Publish + `state-changed`.  
4. **Do not** auto-resume.  

Exit must be **infallible** — the anchor is held in memory for the whole forensic
session and restoring it cannot depend on the tape, the window, or a successful
materialize. If anything else has failed, exit still works.

---

## Read-only enforcement

Runtime must reject while forensic:

- Memory writes / fill / assemble-to-RAM  
- Register / PC pokes  
- Live step / run (machine execution) — tape queries only  
- Mount/reset/load-state: either reject or force exit forensic first — **pin: reject
  with clear error** unless exit is explicit  

UI setters should no-op/reject too (defense in depth); runtime is source of truth.

---

## Client / command surface

```text
runtime_client_tm_enter_forensic(...)
runtime_client_tm_exit_forensic(...)
runtime_client_tm_seek / step / step_over / step_out / run_to  // apply materialize
runtime_client_tm_mode / focus getters
```

Reuse TM1 query ops; TM3 wraps them with materialize-into-live.

### Control surface is required, not optional (D18)

Forensic mode is a **global** read-only state: while a UI user is scrubbed, every socket
peer's `get-memory` returns past bytes and every mutating verb fails. Shipping that
client-only would leave agents silently misled and, if the user walks away, with no way
out. TM3 therefore owns:

| Need | Surface |
|------|---------|
| "Am I looking at the past?" | `mode` (`live`\|`forensic`) + focus cycle on the existing status verb |
| "Get me out" | An exit-forensic verb any session may call |
| "Something moved" | `state-changed` gains `forensic-enter` / `forensic-seek` / `forensic-exit` reasons |
| "Why did my poke fail?" | A distinct error code for read-only-in-forensic, not a generic failure |

The new `state-changed` reasons are wire-visible, so **expect an A2M bump** — the
"client-only, no bump" wording from earlier drafts was wrong. Update
[`control-tools.md`](control-tools.md) and the capabilities list in the same change.

Seek/step verbs on the wire are still optional in TM3 — read-only agents can watch, and
the UI drives via `runtime_client`. Mode visibility and exit are not optional.

---

## Honesty

- Status: `forensic @ cycle …` (host **and** control).  
- Window start reason: when `tm_window.oldest` was set by a `MEDIA_CHANGED` marker, say so —
  `history starts here: disk write @ cycle N` — so a short window reads as a stated rule,
  not as data loss (D10).  
- If materialize fails (cycle outside `tm_window`): keep prior state or restore anchor;
  return error — never half-apply silently.  
- A failed materialize must never leave the seal engaged or the recorder detached.

---

## Code anchors

| Area | Path |
|------|------|
| TM1/TM2 module | `src/runtime/runtime_timemachine*` |
| Worker machine owner | `runtime_thread.c`, `runtime_internal.h` |
| Snapshot restore patterns | `apple2_snapshot_load`, post-load `softswitch_apply_full_map`, `apple2_video_reseed_from_cycles` |
| Sealed replay | TM2 `runtime_tm_materialize` + seal enter/exit (D16) |
| Control status / verbs | `src/control/`, `agents/control-tools.md` |
| Mutation invalidate set | `runtime_history_command_invalidates_cursor` / sessions |
| Publish CPU/mem | existing publish helpers after step/pause |
| state-changed | sessions / control push |

---

## Testing

| Test | Expect |
|------|--------|
| Enter/exit | exit restores CPU/mem **and Disk II / VIA** to pre-enter NOW |
| Exit is infallible | force a materialize failure mid-session, then exit — NOW still restored |
| Seek | after seek, `get-cpu`/mem match materialize golden |
| Read-only | poke/step-live fails in forensic with the distinct error code |
| state-changed | fired on enter/seek/exit with the new reasons |
| Backward seek | rebuild path matches forward materialize at same cycle |
| Window edge | seek outside `tm_window` errors; state unchanged |
| Control visibility | status reports `forensic` + cycle; exit verb works from a socket session |

---

## Acceptance checklist

- [x] Forensic mode replaces `apple2_t` with past; views can use normal get-*  
- [x] Enter anchors NOW as a **full snapshot**; exit restores it and is infallible  
- [x] Tape step/seek materializes via sealed replay; forward/back per D12  
- [x] Beam reseeded + page maps re-applied after every checkpoint load  
- [x] Display policy pinned in Landed with its mid-frame limits  
- [x] Runtime rejects mutating commands in forensic (distinct error code)  
- [x] `state-changed` on scrub/enter/exit; **mode + exit verb on the control port (D18)**  
- [x] `control-tools.md` + capabilities updated; A2M bump noted  
- [x] Window-start reason (incl. `MEDIA_CHANGED` cause) exposed for the UI to show (D10)  
- [x] Build + ctest green  
- [x] Landed filled  

---

## Agent script

```text
1. Read agents/rules.md, agents/timemachine.md D2-D4/D9/D12/D16-D18, TM1-TM2 Landed,
   agents/sessions.md, agents/control-tools.md, TM3.md.
2. Implement enter/exit + seek-materialize-into-live + read-only guards.
3. Add control mode/exit/state-changed reasons; bump A2M; update control-tools.md.
4. ctest enter/exit/seek/reject/window-edge/control-visibility.
5. Do not build Misc tab (TM4). Stop.
```

---

## Landed

Handoff for TM4. Forensic mode replaces live `apple2_t` with THEN. No Misc tab (TM4). F7 stays unbound.

### Names

| Piece | Name |
|-------|------|
| Mode | `runtime_tm_current_mode` / `runtime_tm_in_forensic` (`rt->tm_forensic`) |
| Can enter | `runtime_tm_can_enter` → `OK` / `UNAVAILABLE` / `EMPTY` / `FAILED` |
| Enter | `runtime_tm_enter_forensic` + `RUNTIME_COMMAND_TM_ENTER_FORENSIC` |
| Exit | `runtime_tm_exit_forensic` + `RUNTIME_COMMAND_TM_EXIT_FORENSIC` |
| Live materialize | `runtime_tm_materialize_live(rt, cycle)` — `dst` is `&rt->machine` |
| NOW blob | `rt->tm_now_blob` / `tm_now_size` (full `apple2_snapshot_save`, no flush) |
| Client | `runtime_client_tm_enter_forensic` / `tm_exit_forensic` (token) |
| Event | `RUNTIME_EVENT_TM_MODE` (`op` 0=enter 1=exit, `mode`, `status`, `focus`, start_*) |
| Query extra | `RUNTIME_TM_QUERY_MATERIALIZE_FAILED` |
| Error code | `RUNTIME_ERROR_READ_ONLY_FORENSIC` (`"read-only-forensic"`) |
| state-changed | `forensic-enter` / `forensic-seek` / `forensic-exit` |
| Wire | **A2M/12**. `get-state` gains `mode=` `focus_cycle=` `start=` `start_arg1=`. Verb `exit-forensic`. Capability `timemachine`. Seek/step **not** on the wire. |
| Window start | `runtime_tm_window_start_name`: `unknown` / `guest-write` / `host-directory` |

### Enter (worker)

1. Pause if running (instruction boundary). Do not auto-resume later.  
2. `can_enter`: TM on; HST1 available **and recording**; frame-ring budget > 0 **and recording**; checkpoint recorder recording with ≥1 slot; `runtime_tm_window_info` valid. TM off → `UNAVAILABLE`. Recorder off / budget 0 / empty window → `EMPTY` (pin 3).  
3. `runtime_tm_checkpoint_take` at live NOW.  
4. Save NOW blob (`apple2_snapshot_save`, no `flush_media`).  
5. `runtime_tm_recorder_set_enabled(false)` — detaches input/media callbacks so remount/replay cannot cut the tape. **Do not** `history_stop` (that would write `RECORDER_RESUME` on exit and truncate).  
6. Seek newest insn in `tm_window`; `runtime_tm_materialize_live`.  
7. Stay sealed: `replay_sealed`, observer NULL, mem callback NULL.  
8. Publish CPU + machine + live ARGB slot. `state-changed reason=forensic-enter`. `RUNTIME_EVENT_TM_MODE`.  

Failed enter restores the NOW blob (if saved), re-enables the recorder, reattaches observer/mem, clears the seal, does not set forensic.

Re-enter while already forensic is OK (no-op).

### Seek / step (forensic)

TM1 `runtime_tm_query` first. Then `materialize_live(focus.cycle)`. **Naive every time** (reload CP + sealed re-exec); no hot-forward path.

- **Forward and back** both rebuild from the nearest CP ≤ cycle (D12). Cadence 20k → worst case ~1 ms; not measured again this phase.  
- Forensic `SEEK_CYCLE` / `SEEK_ID` **outside** `tm_window` → `NOT_RETAINED`, machine and focus unchanged (TM1 clamp is **not** applied on this overlay). Step off the end is still `END_OF_TAPE`.  
- Materialize failure restores the THEN blob taken before the seek, stays forensic, seal stays on, status `MATERIALIZE_FAILED`.  
- `state-changed reason=forensic-seek` only on a successful live apply.

### Exit (infallible)

`apple2_snapshot_load` of the NOW blob, `apple2_video_reseed_from_cycles`, unseal, reattach mem callback + `runtime_history_sync_observer`, `runtime_tm_recorder_set_enabled(true)` if TM is still on (takes a CP at restored NOW). Frees the NOW blob. **Does not auto-resume.** Exit with no forensic session is a no-op that still publishes `TM_MODE` + `forensic-exit`.

The NOW blob is independent of the tape. Wiping checkpoints mid-session then exiting still restores CPU, RAM, Disk II motor, and VIA T1 (ctest).

### Display (pinned)

Default path: **sealed re-execution with `paint_enabled = true`**. After CP load, `apple2_video_reseed_from_cycles` then `apple2_step_cycles` so the beam paints from the CP's position to the target.

`apple2_snapshot_load` still calls `apple2_video_paint_full_frame` as a **canvas** (pre-existing load behaviour). Pixels before the CP beam position in that frame can be wrong on mid-frame mode switches; re-exec overpaints from the CP onward. Frame-ring fallback is **not** used as the presented picture. Full-frame paint is never presented as the recorded image.

Live-slot publish after the head lands. **No** `runtime_frame_ring_push` while forensic (the ring is a recorder).

Stop-path CRT (F10-family / F12 / Pause) is **not** this land/materialize policy. Present per [`TMA0.md`](TMA0.md) A16 / [`TMA1.md`](TMA1.md) §6: beam buffer unless Override or paint-off, then RAM dump.

### Read-only

Worker rejects (before cursor-invalidate / state-changed) with `RUNTIME_EVENT_ERROR` `code=read-only-forensic`:

reset, run, live step family, mem/reg poke, save/load-state, load-bin, assemble, media insert/eject/swap, boot-slot, machine-config apply, paste, key, gameport, history-clear/record, `TM_SET_ENABLED`.

Tape queries + enter/exit + get-* + BP edits + turbo are allowed. Control dispatch mirrors the mutation set and returns `error read-only-forensic` immediately so `set-memory` / `set-reg` / `run` never post `ok`.

### Control (A2M/12)

- `hello` / `version`: `protocol=A2M/12`  
- `capabilities` adds `timemachine`  
- `get-state` … `mode=live|forensic focus_cycle=N start=<name> start_arg1=N` (`start` is the D10 left-edge reason; `start_arg1` is `(slot<<8)|device`)  
- `exit-forensic` — any session; `ok accepted=1` (fire-and-forget like pause)  
- Unsolicited `state-changed` reasons: `forensic-enter` / `forensic-seek` / `forensic-exit`  
- Enter / seek / step are **not** wire verbs (UI uses `runtime_client`)

### Tests / gate

`runtime_tm_forensic`: enter/exit restores PC/cycles/RAM **and** Disk II motor + VIA T1; pin-3 empty enter; TM-off unavailable; poke/step/run → `read-only-forensic`; seek matches scratch materialize at the focus cycle; seek outside → `NOT_RETAINED` + state unchanged; wipe CPs then exit still restores NOW; control socket `get-state mode=forensic`, `set-reg` rejected, `exit-forensic` leaves live.

`control_protocol` parses `exit-forensic`.

Full ctest **59** green.

### What TM4 must not break

- Enter is an edge that requires a non-empty `tm_window` **and** live recorders. Standalone `history-record off` / `frame-ring-record off` still fails enter honestly.  
- One true `apple2_t`: get-cpu/get-memory/softswitches/published frame are THEN while forensic.  
- Exit always restores the in-memory NOW blob; never re-read the tape.  
- F7 stays unbound. Misc Inspector tab is this next phase.  
- Do not add a second serializer.
