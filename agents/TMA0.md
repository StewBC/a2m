# TMA0 — Inspector rework contract (time travel)

**Status:** Roadmap (brief). **Not implementation.**  
**Product:** TimeMachine addendum. Inspector is **time travel** (checkpoint filmstrip + land + re-execute), not an HST1 cycle walk.  
**Epic:** [`timemachine.md`](timemachine.md) (TM0–TM6 stay landed history). **Not going to TM6.**  
**Prev / Next:** TM0–TM6 closed as written / [`TMA1.md`](TMA1.md) (implement).  
**Depends on:** TM2 checkpoint ring + sealed replay; TM3 enter/exit NOW; TM4 Misc tab (chrome to rewire, not salvage).

Related: [`TM4.md`](TM4.md) · [`rules.md`](rules.md) · [`snapshots.md`](snapshots.md).

This file is a **layer**. Do not rewrite TM0–TM6 or D1–D18. The destination story for c64m agents will be flattened later, when this Inspector is the one we want.

Wire/code still says `mode=forensic` / `enter-forensic`. That is the **time-travel** mode. Do not rename the protocol in TMA1/TMA2.

---

## Why this addendum exists

TM4 shipped a Misc → Inspector tab whose scrubber is a **cycle slider** that, on every thumb move, sends `SEEK_CYCLE`. That query starts at HST1 `newest_id` and calls `runtime_history_previous()` until it finds the instruction at that cycle, then materializes.

Measured (Debug, 4 s at 1 MHz, default budgets): **~17 s** to slam left, of which **~2 ms** was snapshot load + re-execute. The rest was the flight-recorder walk (~1.35 M `previous()` calls). Leave Inspector is slow for the same reason: the worker is inside that walk and does not pump the command queue.

That is the wrong problem. Inspector is **time travel**: scrub pictures, land the Apple on a snapshot, debug forward by re-execute. HST1 is a **different** stream (forensic FIND). It is not this UI.

TM2 already has the fast path: load nearest checkpoint ≤ C, sealed re-execute. Use that.

---

## Two streams (do not conflate)

| Stream | What it is | Product use |
|--------|------------|-------------|
| **Time Machine** | Checkpoint snapshots + input log + sealed re-execute | **Time travel.** Inspector slider, land, `±`, F10-family. |
| **Flight recorder (HST1)** | Instruction log | **Forensic.** FIND: “who wrote `$22` to `$2011`”. Not the Inspector slider, not F12. |

Time travel is **not** forensic. Docs that still say “forensic F10” about Inspector keys are the muddle this layer ends.

---

## Vocabulary (use these going forward)

| Term | Meaning |
|------|---------|
| **Time travel** | Inspector mode. Machine is a reconstructed point on the Time Machine timeline. |
| **Live** | Right end of that timeline: the paused NOW taken when Inspect was entered. Not “keep executing the live line.” |
| **Film** | Frame-ring ARGB stills (what was on the CRT while recording). Preview only. |
| **Film cursor** | Slider position on the timeline while the thumb is down. Does not move the Apple. |
| **Land** | Release the thumb: load nearest checkpoint ≤ that time (or restore **live** if the thumb is at the right end). One true state becomes that snapshot. Paint from it. |
| **Pink** | Missing-texture fill of the CRT client area when there is no film at the film cursor. Loud on purpose; colour can be tuned later. |
| **Frame-step** | `[-]` / `[+]` move **one guest video frame**, then paint. Clamped to the timeline. |

Do not say “boom.” Do not say the slider “seeks the tape.”

---

## Pinned addendum decisions (do not re-litigate)

These overlay TM4 Inspector UX. Backend pins D5 / D5a / D12 / D16 still hold.

| # | Topic | Decision |
|---|-------|----------|
| **A1** | Slider domain | Timeline from **oldest retained snapshot → live**. Not HST1, not “cycles since boot,” not D17 intersection. |
| **A2** | Grab | Thumb down = **preview only**. One true state stays wherever it was when grabbed. |
| **A3** | Preview | If the frame ring has a still at that time, blit it to the CRT. Else **pink** the CRT client area. |
| **A4** | Release | **Land:** load last checkpoint ≤ that time, paint from the reconstructed Apple. Far right = restore **live** (the NOW blob), not the last cadence checkpoint. May sit up to ~one 1 MHz frame *before* a stored still (cadence is cycle-capped, not VBL-locked). |
| **A5** | `[-]` / `[+]` | **One guest video frame.** `[+]` = re-execute forward to the next frame + paint. `[-]` = earlier checkpoint + re-execute to the previous frame + paint (D12, no reverse CPU). Disable `[-]` at the oldest snapshot, `[+]` at **live**. While the thumb is down they cannot be clicked (one pointer). A step that would pass live **stops at live**. |
| **A6** | F10 / F11 / Shift+F10 / F12 / Shift+F12 | **Re-execute** on the landed Apple — not HST1 TM1 queries. **F12** runs the timeline forward until a **breakpoint** or **live**, then **stops**. Stay in time travel. Do **not** exit, do **not** resume the live line, do **not** record more Time Machine history. **Shift+F12** = run-to-cursor, still stops at a breakpoint or live. |
| **A7** | Enter Inspect | Requires **checkpoints** only. Film is optional (then most of the bar is pink). HST1 is **not** a gate. Enter **starts at live** (machine is already NOW). Do not land the last cadence checkpoint. Slider at the right. |
| **A8** | Leave Inspect | Restore live **NOW**, still **paused**. Unchanged. |
| **A9** | Mutation | Pokes still rejected (read-only past). Execute-forward is how the cursor moves. Nothing executes **past live**. F10 / F11 / F12 / `[+]` at live are no-ops or disabled. |
| **A10** | HST1 | **Out of this UI.** It may keep recording in the background. Inspector must not `SEEK_CYCLE` / tape-walk to place the slider or to land. |
| **A11** | Stored film | **Keep the ring for now** as a preview cache. Whether 128 MB of ARGB is worth it — follow-up, not this brief. Source of truth is the checkpoint. |
| **A12** | Thumb follows cycles | Bar is short (~128 distinct pixels). Thumb position is the **current machine cycle** on oldest→live. After land / `±` / F10-family, if cycles cross a notch (~20 k-cycle snapshot spacing), the thumb moves. Not a dual coarseness slider. |
| **A13** | Opt+Left | **Unbound** in time travel. Not poke-PC, not HST1 run-to, not sealed run-to-cursor. |
| **A14** | Breakpoints | **One list.** Live and time travel share it. Opt+B and the Breakpoints tab always edit that list. Time-travel run stops on those breakpoints. The TM5 second bank is deleted in [`TMA2.md`](TMA2.md). |
| **A15** | TM6 | **Not this campaign.** No Promote / Branch. |

---

## Goal

Rewire Misc → Inspector so it matches A1–A15.

**Win:** slam the thumb to the far left and the UI stays live; release **lands** in ~a millisecond-scale snapshot load; `±` and F10-family move that Apple by re-execute, clamped to live.

---

## Non-goals

- Dropping the frame ring (A11 parked)  
- Locking checkpoints to VBL  
- Reverse-execution of the 6502  
- Using HST1 for scrub, land, or time-travel F10  
- Dual coarseness sliders  
- Promote / Branch (TM6) — not this campaign  
- Changing max-turbo “recording stops” or D10 media-write window cuts  
- Flattening this story into a c64m-only brief (later, when we are happy)  
- Renaming `mode=forensic` on the wire  
- Implementation — that is [`TMA1.md`](TMA1.md)

---

## UX

```text
Pause → Inspect (enter time travel at live). Leave still restores NOW, paused.

  [-]  [======== thumb ========]  [+]

Thumb  = current cycle on oldest snapshot → live (right = live).
         Drag is preview only; Apple frozen.
         After land / ± / F10-family, thumb follows cycles.

Thumb down  → CRT = stored film OR pink. Apple frozen.
Release     → land (checkpoint ≤ time, or live at the right end).
              CRT = paint of the landed Apple (never leave the user on pink).
Grab again  → preview again; Apple frozen until the next land.

After land (still in time travel):
  [-] / [+]          one guest video frame (disabled at oldest / live)
  F10 / F11 / Shift+F10
                     insn step / over / out  (no-op at live)
  F12                re-execute to a breakpoint or live; stay in time travel
  Shift+F12          run-to-cursor; stop at a breakpoint or live
  Opt+Left           unbound
  pokes              reject (read-only)
```

Scrubber copy should say the bar is **retained snapshots → live**, stills where we have them, pink where we do not — not “cycles still retained.”

---

## What this uses (and what it must not)

| Need it? | Piece |
|----------|--------|
| **Yes** | Checkpoint ring + `apple2_snapshot_load` |
| **Yes** | NOW blob as the **live** end (already TM3 enter/exit) |
| **Yes** | Paint after land (and after each frame-step) |
| **Yes** | Sealed re-execute after land (F10-family, `[+]`; `[-]` = earlier CP + re-execute) |
| **If F12 runs far** | Input log (already TM2) so keys/paddles match the original session |
| **Yes** | The **one** breakpoint list (live engine, sealed) |
| **Preview only** | Frame ring blit (`copy_by_cycle` / `copy_by_index`) |
| **No** | HST1 `SEEK_CYCLE` / `previous()` walk / TM1 tape keys |
| **No** | A second Time Machine breakpoint bank |

`tm_window` for **this tab** is oldest checkpoint → live. Do not empty Inspect because HST1 or the frame ring is missing.

---

## What TM4 did (keep as history)

| TM4 Inspector | TMA0 |
|---------------|------|
| Slider over `tm_window` **cycles** (HST1 ∩ frames ∩ checkpoints) | Slider oldest snapshot → **live**; thumb follows cycles |
| Drag sends `TM_SEEK_CYCLE` every tick | Drag **never** mutates the Apple |
| `SEEK_CYCLE` walks HST1 from newest | Land = `load(cp ≤ time)` or restore live |
| “Forensic” F10 = TM1 tape step | Time-travel F10 = re-execute, clamped to live |
| Enter needs HST1 + frames + checkpoints; seeks newest | Enter needs **checkpoints**; **starts at live** |
| Separate TM5 breakpoint bank | **One** breakpoint list |
| CRT always synthesized machine | CRT = film or pink until land, then paint |

TM4 Landed remains true as a historical phase. This addendum supersedes **that tab’s behaviour**.

---

## Code anchors (for TMA1)

| Area | Path |
|------|------|
| Inspector tab / slider | `src/frontend/frontend.c` (`frontend_draw_misc_inspector`) |
| Seek intent on drag | `FRONTEND_DEBUGGER_INTENT_TM_SEEK_CYCLE` — **stop using this for the thumb** |
| Worker seek + HST1 walk | `runtime_thread.c` `RUNTIME_COMMAND_TM_QUERY`; `tm_seek_cycle` in `runtime_timemachine.c` |
| Land | `runtime_tm_materialize` / `apple2_snapshot_load`; nearest CP already in `runtime_tm_recorder.c` (`tm_nearest_cp`); NOW blob for live |
| Film | `runtime_frame_ring_copy_by_cycle` / `copy_by_index` / `meta_at_index` |
| Enter gate | `runtime_tm_can_enter` — drop HST1 + frame-ring requirements; start at live, do not SEEK |
| Time-travel keys | `src/main.c` intent dispatch; TM4 mapped them to `runtime_client_tm_*` |
| Cadence | `RUNTIME_TM_CHECKPOINT_CADENCE_CYCLES` (20000) |

---

## Acceptance (brief — TMA0)

- [x] This file exists; TM0–TM6 untouched except a pointer to TMA0  
- [x] README / status / `timemachine.md` point at TMA0 as the **live Inspector UX**  
- [x] Vocabulary **time travel** / live / land / film / pink / frame-step pinned  
- [x] Two streams pinned; HST1 out of the Inspector loop  
- [x] One breakpoint list; Opt+Left unbound; F12 runs to live; enter starts at live  

Implementation acceptance lives in [`TMA1.md`](TMA1.md).

---

## Agent script

```text
TMA0: docs only (this file).
TMA1: implement A1–A15 (Inspector is time travel; stops calling TM1). Stop for a look.
TMA2: [`TMA2.md`](TMA2.md) delete the TM1 query engine and the TM5 second BP bank.
Do not start TM6. Do not re-open A11 (drop film) without a new addendum.
```

---

## Landed

Brief accepted as the Inspector contract 2026-08-23; vocabulary restated 2026-08-23 (time travel vs forensic; live end; one BP list). Code is TMA1.
