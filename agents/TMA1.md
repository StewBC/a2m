# TMA1 — Implement Inspector time travel (film / land / re-execute)

**Status:** Roadmap (implementation brief).  
**Epic:** [`timemachine.md`](timemachine.md)  
**Prev / Next:** [`TMA0.md`](TMA0.md) / [`TMA2.md`](TMA2.md) (delete TM1 tape-nav + second BP bank — required)  
**Depends on:** TMA0 A1–A15 (do not re-litigate); TM2 checkpoint + seal; TM3 enter/exit NOW; TM4 Misc tab chrome.

Related: [`TM4.md`](TM4.md) · [`rules.md`](rules.md) · [`testing.md`](testing.md) · [`frontend.md`](frontend.md).

This is the how-to. Product contract is TMA0. Do not rewrite TM0–TM6. **Stop when Landed. Do not start TMA2 until the human says so.**

---

## Goal

Rewire Misc → Inspector to A1–A15.

**Win:** slam the thumb left — UI stays live, CRT shows film or pink, Apple does not move. Release **lands** (snapshot load, ~ms). `[-]`/`[+]` frame-step that Apple. F10-family **re-executes** toward **live**. HST1 is not on this path.

---

## Non-goals

- Dropping the frame ring (A11)  
- Deleting the TM1 query engine in **this** phase — Inspector must **stop calling** it here; **TMA2** rips the engine, tests, and leftover tape keys. Do not “keep it around just in case.”  
- Deleting the TM5 second BP **store** in this phase — TMA1 must **stop using** it (Opt+B / tab / run all hit the **one** live list). TMA2 deletes the store.  
- Locking checkpoints to VBL  
- Reverse CPU  
- Promote (TM6)  
- Dual coarseness sliders  
- Renaming `mode=forensic` on the wire  
- Flattening a c64m-only brief  

---

## Order of work

Do it in this order so the 17 s path dies before the chrome is pretty.

### 1. Enter gate (A7)

`runtime_tm_can_enter` today requires TM on + HST1 recording + frame ring recording + ≥1 checkpoint + D17 `tm_window`.

**Change:** enter needs **TimeMachine on, machine ready, ≥1 checkpoint.** Film optional (preview then pink). HST1 is not a gate.

Inspector slider / labels use **oldest checkpoint cycle → live cycle** (NOW blob), not D17 intersection. Otherwise the bar still clips to ~5 s of film.

**Enter starts at live.** The machine is already NOW. Do **not** `SEEK_CYCLE` and do **not** land the newest cadence checkpoint (`runtime_tm_enter_forensic` today does). Slider at the right.

Update tests that expect enter-empty when `history-record` or `frame-ring-record` is off (`tests/runtime/test_runtime_tm_forensic.c`). Those pins were TM0/TM3; TMA0 A7 supersedes them for Inspect.

### 2. Land command (A4, A10)

New worker command, **not** `RUNTIME_COMMAND_TM_QUERY` / `SEEK_CYCLE`:

```text
runtime_client_tm_land(client, cycle, token)
→ if cycle ≥ live: restore NOW blob (already at live is a no-op publish)
→ else: load nearest checkpoint ≤ cycle into live apple2_t
→ apple2_video_reseed_from_cycles
→ paint (see Display, below)
→ publish CPU / machine / ARGB
```

Use existing `tm_nearest_cp` + `apple2_snapshot_load`. Target **is** the checkpoint cycle (A4), except the right end which **is** live. Do **not** HST1-walk to a film cycle. Do **not** re-execute toward the still.

Leave Inspector: unchanged (A8) — restore NOW blob, paused.

### 3. Time-travel execute is allowed (A6, A9, A13, A14)

Today `runtime_tm_command_mutates_machine` rejects live `STEP_*` / `RUN*` while in time travel; `main.c` sends `runtime_client_tm_step*` (HST1) instead.

**Carve out sealed execute, clamped to live.** Still reject pokes, reset, media, assemble, TM enable, paste, etc.

| Key | Live (unchanged) | Time travel after TMA1 |
|-----|------------------|------------------------|
| F10 | pause if running, else step insn | sealed step insn; **no-op at live** |
| F11 | step over | sealed step over; **no-op at live** |
| Shift+F10 | step out | sealed step out; **no-op at live** |
| F12 | run | sealed run until a **breakpoint** or **live**; stay in time travel |
| Shift+F12 | run to cursor | sealed run-to-cursor; stop at a breakpoint or live |
| Opt+Left | poke PC | **unbound** |
| Opt+B | toggle execute BP | **same one list** (not the TM5 bank) |

Wire `handle_step_key_event` / F12 time-travel branches to the **live** client APIs, not `runtime_client_tm_step*`. Clamp the worker so execute cannot pass live.

Keep the seal (D16): observer off, no HST1 append, no `runtime_tm_after_step` checkpoints (recorder is already disarmed on enter), no frame-ring push (`publish_argb_frame` already skips the ring while `mode=forensic`). Input log still applies on a long F12 (TM2).

**Breakpoints:** the live `rt->breakpoints[]` list. Do not arm or match `tm_breakpoints[]`. Time-travel F12 / run-until / Opt+B / Breakpoints tab all use that one list. Leave Inspect keeps whatever you added or cleared (debugger state, not inside the snapshot).

The Breakpoints tab **“Run tape to breakpoint”** in time travel is the same run as F12 (to a breakpoint or live). Copy can wait for TMA2; behaviour must not HST1-scan.

### 4. Frame-step (A5)

New worker command, e.g. `runtime_client_tm_frame_step(client, dir, token)` with `dir` +1 / −1.

- **`[+]`:** sealed `apple2_step_cycle` until `apple2_video_take_frame_ready` (one guest frame), publish. Disable at live. If the next frame would pass live, stop at live.
- **`[-]`:** previous frame = land earlier CP, sealed re-execute to “one frame before current cycle” (use the machine’s own VBL/frame_ready, not a magic 17030). Disable if that target is before the oldest checkpoint.

Do not HST1-walk. Disable the buttons at the ends; they cannot fire during a drag.

### 5. Inspector chrome (A1–A3, A12)

`frontend_draw_misc_inspector`:

- Stop sending `FRONTEND_DEBUGGER_INTENT_TM_SEEK_CYCLE` from the slider.
- Slider domain = **cycle-linear oldest snapshot → live**. Thumb = current `apple2_cycles` when not dragging. After land / `±` / F10-family the thumb **follows cycles** (a notch ≈ 20 k-cycle snapshot spacing).
- **Thumb down:** film cursor only. CRT = frame-ring still at that time, else **pink** fill of the display client area (hot magenta `255,0,255`; tune later). Apple frozen.
- **Thumb up:** one `tm_land` at that cycle (right end = live). After land, CRT is the painted Apple (never leave the user on pink).
- `[-]` / `[+]` beside the slider → `tm_frame_step`. Disabled at oldest / live / during drag.
- Copy: bar is **retained snapshots → live**; stills where we have them; pink where we do not. Drop “Scale is cycles still retained.”

Preview must not wait on materialize. Prefer a UI-thread blit if `runtime_client` can lock the frame ring (the ring is documented as main-readable). Else a coalesced `tm_preview` worker command that only copies pixels or publishes a magenta buffer — still O(one frame), never `SEEK_CYCLE`.

Frontend already coalesces `TM_SEEK_CYCLE` intents; reuse that pattern for preview/land so a slam-left is one land, not 100.

### 6. Display after land

`apple2_snapshot_load` has no framebuffer (TM3). After land on a checkpoint (replay span 0):

1. `apple2_video_reseed_from_cycles`
2. `apple2_video_paint_full_frame` + publish  

Landing **live** restores the NOW blob; paint the same way if the framebuffer is stale.

Mid-frame split-screen can be wrong vs a recorded still (TM3). Accepted for TMA1: we landed on a checkpoint, not on the film cell. Frame-step `[+]` then beam-paints a real guest frame.

While dragging, the CRT is **film or pink**, not this paint.

---

## What must not happen

- Inspector drag/land/±/F10 → `tm_seek_cycle` / `runtime_history_previous` loop  
- Enter Inspect failing because HST1 or frame ring is off  
- Enter jumping to the last cadence checkpoint instead of staying at live  
- Slider still mapped through D17 intersection  
- Time-travel F10 calling `runtime_client_tm_step`  
- Time-travel Opt+Left poking PC or tape-seeking  
- Time-travel Opt+B / F12 using `tm_breakpoints[]`  
- Execute past live, or F12 exiting time travel / resuming the live line  
- New checkpoints or HST1 rows while in time travel  
- Auto-resume on leave  

---

## Code anchors

| Area | Path |
|------|------|
| Enter gate | `runtime_tm_can_enter` in `runtime_timemachine.c` |
| Enter currently SEEKs newest | `runtime_tm_enter_forensic` — start at live instead |
| Land / nearest CP | `runtime_tm_materialize` (replay-to-C; land should load CP only), `tm_nearest_cp` in `runtime_tm_recorder.c`; NOW blob for live |
| Mutate reject | `runtime_tm_command_mutates_machine` in `runtime_thread.c` |
| TM_QUERY + SEEK | `RUNTIME_COMMAND_TM_QUERY` in `runtime_thread.c` — UI must stop calling |
| Keys | `handle_step_key_event`, F12 / Opt+Left branches in `src/main.c` |
| Tab / slider | `frontend_draw_misc_inspector` in `src/frontend/frontend.c` |
| Seek intent coalesce | `frontend_push_tm_intent` — replace thumb use |
| Film | `runtime_frame_ring_copy_by_cycle` / `copy_by_index` / `meta_at_index` |
| Paint | `apple2_video_paint_full_frame`, `apple2_video_take_frame_ready` |
| One BP list | `rt->breakpoints[]`; ignore `tm_breakpoints[]` |
| Tests to update | `tests/runtime/test_runtime_tm_forensic.c` (enter-empty pins; enter stays at live) |
| Tests to keep until TMA2 | TM1 query tests; `SEEK_CYCLE` API may remain unused by UI |

---

## Tests

ctest stays green. Prefer:

| Case | Expect |
|------|--------|
| Enter with checkpoints, HST1 off | **OK** (A7) |
| Enter with checkpoints, frame ring off | **OK**; preview will be pink |
| Enter with 0 checkpoints | EMPTY, unchanged honesty |
| Enter starts at live | machine cycle == NOW cycle; not last cadence CP |
| Land oldest CP | machine cycle == that CP; HST1 `record_count` unchanged; no multi-second stall |
| Land does not call tape walk | if you assert anything, assert cycle equals `tm_nearest_cp` cycle |
| Land live | restores NOW |
| Poke in time travel | still `read-only-forensic` |
| Step insn in time travel | **succeeds**, cycle advances, still sealed; **no-op at live** |
| F12 in time travel | stops at live; still in time travel; paused |
| Leave | NOW restored, paused |

Do not add flaky UI automation. Manual smoke is the GUI gate.

---

## Manual smoke

1. `--timemachine`, run a few seconds, F10 pause, F9, Inspector → **Inspect**. You are at live (same regs as the pause).  
2. Grab thumb, slam left: UI stays live; CRT is film or pink; **Leave Inspector** is immediate (not “seconds later”).  
3. Release: land — regs/CRT update; not pink.  
4. `[-]`/`[+]` move one video frame; buttons disable at oldest / live; slider follows.  
5. F10/F11 step on that Apple. F12 runs to live (or a breakpoint) and **stays** in Inspect.  
6. Opt+Left does nothing. Opt+B toggles the same BP list as live. Poke mem still read-only.  
7. Leave → NOW, still paused; F12 runs live.  
8. Max turbo / disk-write window cut: existing honesty copy still true.

---

## Docs in the same change

- `manual/manual.md` Inspector: time travel, snapshots → live, film/pink + land; keys as A6/A13.  
- `status.md` Active: TMA1 Landed when done.  
- `TMA0.md` stays the contract; this file gets **Landed**.  
- Do not rewrite TM4 Landed; it is history.

---

## Acceptance checklist

- [x] Enter Inspect = checkpoints only; **starts at live**  
- [x] Slider = oldest snapshot → live; drag does not mutate the Apple; thumb follows cycles  
- [x] Drag CRT = film or pink (`255,0,255`)  
- [x] Release lands (`load(cp ≤ time)` or live); CRT painted; HST1 unused  
- [x] `[-]`/`[+]` = one guest frame; disabled at oldest / live  
- [x] F10-family = sealed re-execute; F12 stops at a breakpoint or live; stay in time travel  
- [x] Opt+Left unbound; one BP list  
- [x] Pokes still rejected; leave restores NOW paused  
- [x] Slam-left does not stall the worker  
- [x] ctest green; enter tests updated for A7 + start-at-live  
- [x] Manual smoke + manual.md  
- [x] **Landed** filled below  
- [x] **Stop.** Do not start TMA2.

---

## Agent script

```text
1. Read agents/rules.md, agents/TMA0.md (A1–A15), this file, TM3 Landed (seal / NOW).
2. Implement in the order above (gate → land → execute → frame-step → chrome).
3. Do not route Inspector through SEEK_CYCLE. Do not drop the frame ring.
4. Build + ctest. Manual smoke. Update manual/status. Landed. Stop.
   Do not start TMA2 unless the human says so.
```

---

## Landed

2026-08-23. Inspector is time travel: film / land / re-execute to live.

- Enter Inspect needs checkpoints only (A7); starts at live (no HST1 SEEK).
- Slider is oldest snapshot → live; drag previews film or pink; release lands.
- Thumb follows machine cycles. `[-]`/`[+]` frame-step; F10-family sealed execute.
- F12 runs to a breakpoint or live and stays in Inspect. Opt+Left unbound.
- One breakpoint list. TM1 query engine is unused by the Inspector (deleted in TMA2).
- ctest 60 green. Stop for a look before TMA2.
