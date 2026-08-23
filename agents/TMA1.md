# TMA1 — Implement Inspector film / land / re-execute

**Status:** Roadmap (implementation brief).  
**Epic:** [`timemachine.md`](timemachine.md)  
**Prev / Next:** [`TMA0.md`](TMA0.md) / [`TMA2.md`](TMA2.md) (delete TM1 tape-nav — required)  
**Depends on:** TMA0 A1–A12 (do not re-litigate); TM2 checkpoint + seal; TM3 enter/exit NOW; TM4 Misc tab chrome.

Related: [`TM4.md`](TM4.md) · [`rules.md`](rules.md) · [`testing.md`](testing.md) · [`frontend.md`](frontend.md).

This is the how-to. Product contract is TMA0. Do not rewrite TM0–TM6.

---

## Goal

Rewire Misc → Inspector to A1–A12.

**Win:** slam the thumb left — UI stays live, CRT shows film or pink, Apple does not move. Release **lands** (snapshot load, ~ms). `[-]`/`[+]` frame-step that Apple. F10-family **re-executes** it. HST1 is not on this path.

---

## Non-goals

- Dropping the frame ring (A11)  
- Deleting the TM1 query engine in **this** phase — Inspector must **stop calling** it here; **TMA2** rips the engine, tests, and forensic tape keys so nothing rotten is left for the next reader. Do not “keep it around just in case.”  
- Locking checkpoints to VBL  
- Reverse CPU  
- Promote (TM6)  
- Dual coarseness sliders  
- Flattening a c64m-only brief  

---

## Order of work

Do it in this order so the 17 s path dies before the chrome is pretty.

### 1. Enter gate (A7)

`runtime_tm_can_enter` today requires TM on + HST1 recording + frame ring recording + ≥1 checkpoint + D17 `tm_window`.

**Change:** enter needs **TimeMachine on, machine ready, ≥1 checkpoint.** Film optional (preview then pink). HST1 is not a gate.

Inspector slider / labels use **`runtime_tm_checkpoint_bounds`**, not D17 intersection. Otherwise the bar still clips to ~5 s of film.

Update tests that expect enter-empty when `history-record` or `frame-ring-record` is off (`tests/runtime/test_runtime_tm_forensic.c`). Those pins were TM0/TM3; TMA0 A7 supersedes them for Inspect.

### 2. Land command (A4, A10)

New worker command, **not** `RUNTIME_COMMAND_TM_QUERY` / `SEEK_CYCLE`:

```text
runtime_client_tm_land(client, cycle, token)
→ load nearest checkpoint ≤ cycle into live apple2_t
→ apple2_video_reseed_from_cycles
→ paint (see Display, below)
→ publish CPU / machine / ARGB
```

Use existing `tm_nearest_cp` + `apple2_snapshot_load`. Target **is** the checkpoint cycle (A4). Do **not** HST1-walk to a film cycle. Do **not** re-execute toward the still.

Enter forensic: land the **newest** checkpoint this way. Stop using `SEEK_CYCLE` on enter (`runtime_tm_enter_forensic` today does).

Leave Inspector: unchanged (A8) — restore NOW blob, paused.

### 3. Forensic execute is allowed (A6, A9)

Today `runtime_tm_command_mutates_machine` rejects live `STEP_*` / `RUN*` while forensic; `main.c` sends `runtime_client_tm_step*` (HST1) instead.

**Carve out sealed execute.** Still reject pokes, reset, media, assemble, TM enable, paste, etc.

| Key | Live (unchanged) | Forensic after TMA1 |
|-----|------------------|---------------------|
| F10 | pause if running, else step insn | **same live step insn** (sealed) |
| F11 | step over | **live step over** (sealed) |
| Shift+F10 | step out | **live step out** (sealed) |
| F12 | run | **live run** (sealed) until pause |
| Shift+F12 | run to cursor | **live run-to-cursor** (sealed) |

Wire `handle_step_key_event` / F12 forensic branches to the **live** client APIs, not `runtime_client_tm_step*`.

Keep the seal (D16): observer off, no HST1 append, no `runtime_tm_after_step` checkpoints (recorder is already disarmed on enter), no frame-ring push (`publish_argb_frame` already skips the ring while forensic). Input log still applies on a long F12 (TM2).

### 4. Frame-step (A5)

New worker command, e.g. `runtime_client_tm_frame_step(client, dir, token)` with `dir` +1 / −1.

- **`[+]`:** sealed `apple2_step_cycle` until `apple2_video_take_frame_ready` (one guest frame), publish. Disable if already at newest checkpoint cycle (nothing after).
- **`[-]`:** previous frame = land earlier CP, sealed re-execute to “one frame before current cycle” (NTSC frame ≈ 17030 Φ0; use the machine’s own VBL/frame_ready, not a magic constant, if it is already available). Disable if that target is before the oldest checkpoint.

Do not HST1-walk. Disable the buttons at the ends; they are post-land only (one mouse — they cannot fire during a drag).

### 5. Inspector chrome (A1–A3, A12)

`frontend_draw_misc_inspector`:

- Stop sending `FRONTEND_DEBUGGER_INTENT_TM_SEEK_CYCLE` from the slider.
- Slider domain = **checkpoint index** (oldest left, newest right). One checkpoint → thumb pegged **right**. Two → left and right. `nk_slider` range follows count (or 0…1000 mapped onto count — do not keep a cycle-linear 0…1000 over D17).
- **Thumb down:** film cursor only. CRT = frame-ring still at that time, else **pink** fill of the display client area (hot magenta `255,0,255`; tune later). Apple frozen.
- **Thumb up:** one `tm_land` at that checkpoint’s cycle. After land, CRT is the painted snapshot (never leave the user on pink).
- `[-]` / `[+]` beside the slider → `tm_frame_step`. Disabled at ends / before first land.
- Copy: bar is **retained snapshots**; stills where we have them; pink where we do not. Drop “Scale is cycles still retained.”

Preview must not wait on materialize. Prefer a UI-thread blit if `runtime_client` can lock the frame ring (the ring is documented as main-readable). Else a coalesced `tm_preview` worker command that only copies pixels or publishes a magenta buffer — still O(one frame), never `SEEK_CYCLE`.

Frontend already coalesces `TM_SEEK_CYCLE` intents; reuse that pattern for preview/land so a slam-left is one land, not 100.

### 6. Display after land

`apple2_snapshot_load` has no framebuffer (TM3). After land (target = CP cycle, replay span 0):

1. `apple2_video_reseed_from_cycles`
2. `apple2_video_paint_full_frame` + publish  

Mid-frame split-screen can be wrong vs a recorded still (TM3). Accepted for TMA1: we landed on a checkpoint, not on the film cell. Frame-step `[+]` then beam-paints a real guest frame.

While dragging, the CRT is **film or pink**, not this paint.

---

## What must not happen

- Inspector drag/land/±/F10 → `tm_seek_cycle` / `runtime_history_previous` loop  
- Enter Inspect failing because HST1 or frame ring is off  
- Slider still mapped through D17 intersection  
- Forensic F10 calling `runtime_client_tm_step`  
- New checkpoints or HST1 rows while forensic  
- Auto-resume on leave  

---

## Code anchors

| Area | Path |
|------|------|
| Enter gate | `runtime_tm_can_enter` in `runtime_timemachine.c` |
| Enter currently SEEKs newest | `runtime_tm_enter_forensic` |
| Land / nearest CP | `runtime_tm_materialize` (replay-to-C; land should load CP only), `tm_nearest_cp` in `runtime_tm_recorder.c` |
| Forensic mutate reject | `runtime_tm_command_mutates_machine` in `runtime_thread.c` |
| TM_QUERY + SEEK on forensic | `RUNTIME_COMMAND_TM_QUERY` in `runtime_thread.c` |
| Keys | `handle_step_key_event`, F12 branches in `src/main.c` |
| Tab / slider | `frontend_draw_misc_inspector` in `src/frontend/frontend.c` |
| Seek intent coalesce | `frontend_push_tm_intent` — replace thumb use |
| Film | `runtime_frame_ring_copy_by_cycle` / `copy_by_index` / `meta_at_index` |
| Paint | `apple2_video_paint_full_frame`, `apple2_video_take_frame_ready` |
| Tests to update | `tests/runtime/test_runtime_tm_forensic.c` (enter-empty pins) |
| Tests to keep | TM1 query tests; `SEEK_CYCLE` API may remain unused by UI |

---

## Tests

ctest stays green. Prefer:

| Case | Expect |
|------|--------|
| Enter with checkpoints, HST1 off | **OK** (A7) |
| Enter with checkpoints, frame ring off | **OK**; preview will be pink |
| Enter with 0 checkpoints | EMPTY, unchanged honesty |
| Land oldest CP | machine cycle == that CP; HST1 `record_count` unchanged; no multi-second stall |
| Land does not call tape walk | if you assert anything, assert cycle equals `tm_nearest_cp` cycle |
| Forensic poke | still `read-only-forensic` |
| Forensic step insn | **succeeds**, cycle advances, still sealed |
| Leave | NOW restored, paused |

Do not add flaky UI automation. Manual smoke is the GUI gate.

---

## Manual smoke

1. `--timemachine`, run a few seconds, F10 pause, F9, Inspector → **Inspect**.  
2. Grab thumb, slam left: UI stays live; CRT is film or pink; **Leave Inspector** is immediate (not “seconds later”).  
3. Release: land — regs/CRT update; not pink.  
4. `[-]`/`[+]` move one video frame; buttons disable at ends.  
5. F10/F11/F12 are insn step/over/run on that Apple, not a tape hitch.  
6. Poke mem still read-only.  
7. Leave → NOW, still paused; F12 runs live.  
8. Max turbo / disk-write window cut: existing honesty copy still true.

---

## Docs in the same change

- `manual/manual.md` Inspector: snapshots + film/pink + land; forensic keys are live step under seal, not tape.  
- `status.md` Active: TMA1 in progress / Landed when done.  
- `TMA0.md` stays the contract; this file gets **Landed**.  
- Do not rewrite TM4 Landed; it is history.

---

## Acceptance checklist

- [ ] Enter Inspect = checkpoints only  
- [ ] Slider = checkpoint window; drag does not mutate the Apple  
- [ ] Drag CRT = film or pink (`255,0,255`)  
- [ ] Release lands (`load(cp ≤ time)`); CRT painted; HST1 unused  
- [ ] `[-]`/`[+]` = one guest frame; disabled at ends  
- [ ] F10-family = sealed live step/over/out/run  
- [ ] Pokes still rejected; leave restores NOW paused  
- [ ] Slam-left does not stall the worker  
- [ ] ctest green; forensic enter tests updated for A7  
- [ ] Manual smoke + manual.md  
- [ ] **Landed** filled below  

---

## Agent script

```text
1. Read agents/rules.md, agents/TMA0.md (A1–A12), this file, TM3 Landed (seal / NOW).
2. Implement in the order above (gate → land → forensic execute → frame-step → chrome).
3. Do not route Inspector through SEEK_CYCLE. Do not drop the frame ring.
4. Build + ctest. Manual smoke. Update manual/status. Landed. Stop.
   Do not start TMA2 unless the human says so — but TMA2 is required; do not
   treat leftover TM1 as the finished product.
```

---

## Landed

Not yet.
