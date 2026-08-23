# TMA0 — Inspector rework contract (film / land / re-execute)

**Status:** Roadmap (brief). **Not implementation.**  
**Product:** TimeMachine addendum. Inspector is a **checkpoint filmstrip**, not an HST1 cycle walk.  
**Epic:** [`timemachine.md`](timemachine.md) (TM0–TM6 stay landed history).  
**Prev / Next:** TM0–TM6 closed as written / [`TMA1.md`](TMA1.md) (implement).  
**Depends on:** TM2 checkpoint ring + sealed replay; TM3 enter/exit NOW; TM4 Misc tab (chrome to rewire, not salvage).

Related: [`TM4.md`](TM4.md) · [`rules.md`](rules.md) · [`snapshots.md`](snapshots.md).

This file is a **layer**. Do not rewrite TM0–TM6 or D1–D18. The destination story for c64m agents will be flattened later, when this Inspector is the one we want.

---

## Why this addendum exists

TM4 shipped a Misc → Inspector tab whose scrubber is a **cycle slider** that, on every thumb move, sends `SEEK_CYCLE`. That query starts at HST1 `newest_id` and calls `runtime_history_previous()` until it finds the instruction at that cycle, then materializes.

Measured (Debug, 4 s at 1 MHz, default budgets): **~17 s** to slam left, of which **~2 ms** was snapshot load + re-execute. The rest was the flight-recorder walk (~1.35 M `previous()` calls). Leave Inspector is slow for the same reason: the worker is inside that walk and does not pump the command queue.

That is the wrong problem. The Inspector the product wanted is **visual**: scrub pictures, land the Apple on a snapshot, debug forward by re-execute. HST1 answers instruction-log questions (step-out on recorded rows). It is not this UI.

TM2 already has the fast path: load nearest checkpoint ≤ C, sealed re-execute. Use that.

---

## Vocabulary (use these going forward)

| Term | Meaning |
|------|---------|
| **Film** | Frame-ring ARGB stills (what was on the CRT while recording). Preview only. |
| **Film cursor** | Slider index into the **checkpoint window**. Does not move the Apple while the thumb is down. |
| **Land** | Release the thumb: `machine = load(checkpoint ≤ film_time)`. One true state becomes that snapshot. Paint from it. (Code name remains materialize / `apple2_snapshot_load`.) |
| **Pink** | Missing-texture fill of the CRT client area when there is no film at the film cursor. Loud on purpose; colour can be tuned later. |
| **Frame-step** | After land: `[-]` / `[+]` move **one guest video frame**, then paint. |

Do not say “boom.” Do not say the slider “seeks the tape.”

---

## Pinned addendum decisions (do not re-litigate)

These overlay TM4 Inspector UX. Backend pins D5 / D5a / D12 / D16 still hold.

| # | Topic | Decision |
|---|-------|----------|
| **A1** | Slider domain | **Checkpoint window** (~16 s at 128 MB TM budget, 20 k-cycle cadence). Not HST1, not “cycles since boot.” |
| **A2** | Grab | Thumb down = **preview only**. One true state stays wherever it was when grabbed. |
| **A3** | Preview | If the frame ring has a still at that time, blit it to the CRT. Else **pink** the CRT client area. |
| **A4** | Release | **Land:** load last checkpoint ≤ that time, paint from the reconstructed Apple. May sit up to ~one 1 MHz frame *before* a stored still (cadence is cycle-capped, not VBL-locked). |
| **A5** | `[-]` / `[+]` | After land: **one guest video frame**. `[+]` = re-execute forward to the next frame + paint. `[-]` = earlier checkpoint + re-execute to the previous frame + paint (D12, no reverse CPU). Disable `[-]` at the oldest reconstructable frame, `[+]` at the newest. While the thumb is down they cannot be clicked (one pointer); treat them as **post-land only**. |
| **A6** | F10 / F11 / F12 | After land: **re-execute** on that Apple — instruction step / over / out / run. **Not** HST1 TM1 queries. |
| **A7** | Enter Inspect | Requires **checkpoints** only. Film is optional (then most of the bar is pink). HST1 is **not** a gate. |
| **A8** | Leave Inspect | Restore live **NOW**, still **paused**. Unchanged. |
| **A9** | Mutation | Pokes still rejected (read-only past). Execute-forward is how the cursor moves. |
| **A10** | HST1 | **Out of this UI.** It may keep recording in the background. Inspector must not `SEEK_CYCLE` / tape-walk to place the slider or to land. |
| **A11** | Stored film | **Keep the ring for now** as a preview cache. Whether 128 MB of ARGB is worth it — follow-up, not this brief. Source of truth is the checkpoint. |
| **A12** | Thumb resolution | Bar is short (~128 distinct pixels). Checkpoint window ≈ **960** NTSC frames in ~16 s → **~7.5 frames per tick**. That is why `±` exists. Not a dual coarseness slider. |

---

## Goal

Rewire Misc → Inspector so it matches A1–A12.

**Win:** slam the thumb to the far left and the UI stays live; release **lands** in ~a millisecond-scale snapshot load; `±` and F10-family move that Apple by re-execute.

---

## Non-goals

- Dropping the frame ring (A11 parked)  
- Locking checkpoints to VBL  
- Reverse-execution of the 6502  
- Using HST1 for scrub, land, or forensic F10  
- Dual coarseness sliders  
- Promote / Branch (TM6)  
- Changing max-turbo “recording stops” or D10 media-write window cuts  
- Flattening this story into a c64m-only brief (later, when we are happy)  
- Implementation — that is [`TMA1.md`](TMA1.md)

---

## UX

```text
Pause → Inspect (enter forensic). Leave still restores NOW, paused.

  [-]  [======== thumb ========]  [+]

Thumb  = film cursor over the checkpoint window (oldest left, newest right).
         1 retained snapshot → thumb only at the right.
         2 → left and right. N → N discrete stops, or fewer if the widget
         has fewer pixels than snapshots (then one tick skips snapshots;
         ± still reaches every *frame* after land).

Thumb down  → CRT = stored film OR pink. Apple frozen.
Release     → land. CRT = paint of the landed snapshot (never leave the
              user on pink after land).
Grab again  → preview again; Apple frozen until the next land.

After land:
  [-] / [+]     one guest video frame (disabled at ends)
  F10 / F11 / F12 / Shift+F10 / Shift+F12
                insn step / over / out / run  on the landed Apple
  pokes         reject (read-only)
```

Scrubber copy should say the bar is **retained snapshots**, stills where we have them, pink where we do not — not “cycles still retained.”

---

## What this uses (and what it must not)

| Need it? | Piece |
|----------|--------|
| **Yes** | Checkpoint ring + `apple2_snapshot_load` |
| **Yes** | Paint after land (and after each frame-step) |
| **Yes** | Sealed re-execute after land (F10-family, `[+]`; `[-]` = earlier CP + re-execute) |
| **If F12 runs far** | Input log (already TM2) so keys/paddles match the original session |
| **Preview only** | Frame ring blit (`copy_by_cycle` / `copy_by_index`) |
| **No** | HST1 `SEEK_CYCLE` / `previous()` walk / TM1 forensic key mapping |

`tm_window` for **this tab** is the checkpoint bounds. Do not empty Inspect because HST1 or the frame ring is missing.

---

## What TM4 did (keep as history)

| TM4 Inspector | TMA0 |
|---------------|------|
| Slider over `tm_window` **cycles** (HST1 ∩ frames ∩ checkpoints) | Slider over **checkpoints** |
| Drag sends `TM_SEEK_CYCLE` every tick | Drag **never** mutates the Apple |
| `SEEK_CYCLE` walks HST1 from newest | Land = `load(cp ≤ time)` |
| Forensic F10 = TM1 tape step | Forensic F10 = re-execute |
| Enter needs HST1 + frames + checkpoints | Enter needs **checkpoints** |
| CRT always synthesized machine | CRT = film or pink until land, then paint |

TM4 Landed remains true as a historical phase. This addendum supersedes **that tab’s behaviour**.

---

## Code anchors (for TMA1)

| Area | Path |
|------|------|
| Inspector tab / slider | `src/frontend/frontend.c` (`frontend_draw_misc_inspector`) |
| Seek intent on drag | `FRONTEND_DEBUGGER_INTENT_TM_SEEK_CYCLE` — **stop using this for the thumb** |
| Worker seek + HST1 walk | `runtime_thread.c` `RUNTIME_COMMAND_TM_QUERY`; `tm_seek_cycle` in `runtime_timemachine.c` |
| Land | `runtime_tm_materialize` / `apple2_snapshot_load`; nearest CP already in `runtime_tm_recorder.c` (`tm_nearest_cp`) |
| Film | `runtime_frame_ring_copy_by_cycle` / `copy_by_index` / `meta_at_index` |
| Enter gate | `runtime_tm_can_enter` — drop HST1 + frame-ring requirements |
| Forensic keys | `src/main.c` intent dispatch; TM4 mapped them to `runtime_client_tm_*` |
| Cadence | `RUNTIME_TM_CHECKPOINT_CADENCE_CYCLES` (20000) |

---

## Acceptance (brief — TMA0)

- [x] This file exists; TM0–TM6 untouched except a pointer to TMA0  
- [x] README / status / `timemachine.md` point at TMA0 as the **live Inspector UX**  
- [x] Vocabulary **land** / film / pink / frame-step pinned  
- [x] HST1 explicitly out of the Inspector loop  

Implementation acceptance lives in [`TMA1.md`](TMA1.md).

---

## Agent script

```text
TMA0: docs only (done).
TMA1: implement A1–A12 (Inspector stops calling TM1).
TMA2: [`TMA2.md`](TMA2.md) delete the TM1 query engine (required cleanup).
Do not re-open A11 (drop film) without a new addendum.
```

---

## Landed

Brief accepted as the Inspector contract 2026-08-23. Measured stall (HST1 `SEEK_CYCLE`) is the reason this layer exists. Code is TMA1.
