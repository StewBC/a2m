# TM3 — Materialize into one true state

**Status:** Not started.  
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

- [ ] Forensic mode replaces `apple2_t` with past; views can use normal get-*  
- [ ] Enter anchors NOW as a **full snapshot**; exit restores it and is infallible  
- [ ] Tape step/seek materializes via sealed replay; forward/back per D12  
- [ ] Beam reseeded + page maps re-applied after every checkpoint load  
- [ ] Display policy pinned in Landed with its mid-frame limits  
- [ ] Runtime rejects mutating commands in forensic (distinct error code)  
- [ ] `state-changed` on scrub/enter/exit; **mode + exit verb on the control port (D18)**  
- [ ] `control-tools.md` + capabilities updated; A2M bump noted  
- [ ] Window-start reason (incl. `MEDIA_CHANGED` cause) exposed for the UI to show (D10)  
- [ ] Build + ctest green  
- [ ] Landed filled  

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

_(empty until implemented)_
