# TMA2 — Delete TM1 tape-nav and the second BP bank (required cleanup)

**Status:** Landed.  
**Epic:** [`timemachine.md`](timemachine.md)  
**Prev / Next:** [`TMA1.md`](TMA1.md) / [`TMA3.md`](TMA3.md)  
**Depends on:** **TMA1 Landed.** Do not rip TM1 while the Inspector still calls it.

Related: [`TMA0.md`](TMA0.md) · [`TM1.md`](TM1.md) (history of what we are deleting) · [`TM5.md`](TM5.md).

This is a **layer**. TM1.md / TM5.md stay as the record of the tape-nav and second-bank experiments. The **code** for those experiments goes away so the next reader is not sent into a 17 s `previous()` walk or a second breakpoint list that no product surface uses.

**Not going to TM6.**

---

## Why

TM1 (`runtime_tm_query`, `SEEK_CYCLE`, tape step/over/out/run-to) was built **only** so the Inspector could move on HST1 instead of a FIND page loop. Inspector is **time travel**: film / **land** / re-execute. The socket never spoke TM1.

TM5’s second breakpoint bank existed so tape-scan would not confuse the live list. Time travel **re-executes** the Apple, so there is only **one** list ([`TMA0.md`](TMA0.md) A14). TMA1 already uses it; this phase deletes the empty store.

Leaving either after TMA1 is a trap: someone will wire a slider to `tm_seek_cycle` or Opt+B to `tm_breakpoints`.

---

## Goal

1. Remove the TM1 **query** layer and every product/test caller.  
2. Remove the TM5 **second breakpoint bank** and every product/test caller.

HST1 remains a **flight recorder** (FIND: “who wrote `$22` to `$2011`”). That is the forensic stream. Time travel stays TMA1: land + sealed execute to live.

**Win:** `rg runtime_tm_query` / `TM_QUERY` / `tm_seek_cycle` / `runtime_client_tm_step` / `tm_breakpoints` hits **nothing** in `src/` except maybe a comment pointing here.

---

## Keep (not this cleanup)

| Keep | Why |
|------|-----|
| HST1 **recording** (observer, ring, markers) | FIND / “who wrote this byte” |
| `history-find` / `history-next` / `history-read` / `history-close` / `history-info` | Socket + future UI FIND |
| `runtime_history_*` query (`has_address`, write, value) | Same |
| Frame ring | TMA0 preview (A11) |
| Checkpoint ring, land, sealed replay, enter/exit NOW | TMA1 |
| The **one** live breakpoint list | Time travel and live share it |
| `exit-forensic`, `mode=forensic`, `focus_cycle` on `get-state` | A2M honesty; `focus_cycle` = **landed `apple2_cycles`**, not an HST1 id. Wire name stays `forensic` = time travel. |

---

## Delete

### Engine (TM1 tape-nav)

- `runtime_tm_query` and all `tm_seek_*` / `tm_step*` / `tm_run_to_pc` / `tm_run_until_break` in `runtime_timemachine.c`
- Enums `runtime_tm_query_op` / `runtime_tm_query_status` (and `runtime_tm_focus` if it exists only to hold an HST1 tape head — replace wire `focus_cycle` with machine cycles)
- `RUNTIME_COMMAND_TM_QUERY`, `RUNTIME_COMMAND_TM_RUN_UNTIL_BREAK`
- `runtime_client_tm_query` / `_step` / `_step_over` / `_step_out` / `_run_to` / `_seek_id` / `_seek_cycle` / `_run_until_break`
- `RUNTIME_EVENT_TM_FOCUS`
- `runtime_coalesce_tm_tape_seeks` / `runtime_command_is_tm_tape_seek`

### Engine (TM5 second bank)

- `rt->tm_breakpoints[]` / `tm_breakpoint_count` / `tm_next_breakpoint_id`
- `runtime_tm_bp_add` / `_toggle_execute` / `_fill_snapshot` and TM_BP_* commands/events
- Forensic-only Opt+B path that writes the second store
- Breakpoints tab “Time Machine breakpoints” chrome and empty-in-live hint

### UI leftovers (if TMA1 did not already)

- `FRONTEND_DEBUGGER_INTENT_TM_SEEK_CYCLE`, `_TM_RUN_TO`, `_TM_RUN_UNTIL`
- Slider/coalesce/`dispatch_intent` cases for those
- Time-travel F10/F11/Shift+F10/F12 still calling `runtime_client_tm_*`
- Opt+Left in time travel → **unbound** (A13). Not tape run-to, not poke-PC, not sealed run-to-cursor.
- Breakpoints tab **“Run tape to breakpoint”** → do **not** add a button (A14). Time-travel **F12** is sealed execute from the landed state until a breakpoint on the **one** list or **live**. No HST1 scan.

### Tests / build

- Remove `tests/runtime/test_runtime_tm_query.c` and its `CMakeLists.txt` / `testing.md` row
- `test_runtime_tm_forensic.c`: seeks → **land**; drop tape-status asserts
- `test_runtime_tm_bp.c`: second-bank tests go away; remaining cases use the one list (time-travel F12 / run-until hits a live BP; leave Inspect still has it)

### Docs in the same change

- `control-tools.md`: tape seek/step stay off the wire **because they do not exist**. FIND stays.
- `manual/manual.md`: time-travel keys = land / frame-step / re-execute to live; one BP list; Opt+Left unbound
- `TM1.md`: one line at the top — **code removed in TMA2; this file is history**
- `TM5.md`: one line at the top — **second bank removed in TMA2; this file is history**
- `status.md` / README: TMA2 Landed

Do **not** rewrite TM0–TM6 bodies. Layer, as with TMA0.

---

## After the cut

Time-travel run-until is sealed re-execute only (the D5a payoff):

1. Landed Apple, seal on, **one** BP list armed.  
2. Execute until a breakpoint hits or you reach **live**.  
3. Stop, publish, **stay in time travel**. Miss (no BP) is: you are at live.

Do not reimplement run-until as `runtime_history_find` + materialize. FIND stays available for “who wrote this”; it is not how F12 works.

---

## Order of work

1. Confirm TMA1 Inspector no longer calls TM1 (rg the delete list in `src/frontend` + `src/main.c`). If it still does, **stop** — finish TMA1.  
2. Confirm TMA1 already uses the one BP list. If Opt+B still writes `tm_breakpoints`, **stop**.  
3. Delete TM1 query engine + TM5 second bank + leftover intents.  
4. Fix/remove tests.  
5. Docs. `rg` the delete list — empty in `src/`.  
6. Build + ctest. Manual: Inspect still lands at live; FIND over the socket still answers a write query; F12 in time travel still stops at a BP or live.

---

## Code anchors

| Area | Path |
|------|------|
| Query engine | `src/runtime/runtime_timemachine.c` (`runtime_tm_query` down) |
| Commands / events | `runtime_command.h`, `runtime_event.h` (`TM_QUERY`, `TM_FOCUS`, `TM_BP_*`) |
| Client | `runtime_client.c` / `.h` `runtime_client_tm_step*` / `seek*` / `run_to` / `run_until_break` / `tm_bp_*` |
| Worker | `runtime_thread.c` `RUNTIME_COMMAND_TM_QUERY`, coalesce-seek, `TM_RUN_UNTIL_BREAK`, TM_BP_* |
| Keys / intents | `src/main.c`, `frontend.h` `TM_SEEK_CYCLE` / `TM_RUN_TO` / `TM_RUN_UNTIL` |
| “Run tape to BP” | `frontend.c` breakpoints tab |
| Opt+Left time travel | `frontend.c` disasm `SDLK_LEFT` + alt — must stay unbound |
| Second bank | `runtime_timemachine.c` `tm_breakpoints` |
| Tests | `test_runtime_tm_query.c` (delete), `test_runtime_tm_forensic.c`, `test_runtime_tm_bp.c` |
| FIND (keep) | `runtime_history.c` query; `control_dispatch.c` `HISTORY_FIND` |

---

## Acceptance checklist

- [x] `rg 'runtime_tm_query|TM_QUERY|tm_seek_cycle|runtime_client_tm_step|tm_run_until_break|tm_breakpoints' src/` is empty (or comments citing TMA2 only)  
- [x] `test_runtime_tm_query` gone from CMake + `testing.md`  
- [x] HST1 FIND still works (existing `runtime_history_query` / control tests)  
- [x] Inspector land / ± / F10-family unchanged from TMA1  
- [x] One BP list; Opt+B in time travel edits it; second bank gone  
- [x] Opt+Left unbound in time travel  
- [x] F12 / run-until is sealed execute to a breakpoint or live; button copy does not say “tape”  
- [x] Socket: no new tape verbs; `history-find` still there; `focus_cycle` = machine cycle  
- [x] TM1.md and TM5.md marked history-only  
- [x] ctest green + short Inspector smoke  
- [x] **Landed** filled  

---

## Agent script

```text
1. Read agents/TMA0.md A10/A13/A14, TMA1.md (must be Landed), this file.
2. If Inspector still calls TM1, or Opt+B still writes tm_breakpoints, stop and finish TMA1.
3. Delete the TM1 query engine, the TM5 second bank, and leftover intents.
4. Do not delete HST1 FIND. Do not start TM6.
5. rg the kill list. Docs. Build + ctest. Landed. Stop.
```

---

## Landed

2026-08-23. TM1 tape-nav and the TM5 second breakpoint bank are gone.

- `runtime_tm_query` / `SEEK_CYCLE` / tape step/over/out/run-to deleted. Inspector land / frame-step / F10-family unchanged.
- `tm_breakpoints[]` deleted. One list in live and time travel. Breakpoints tab is the live panel (no “Run to breakpoint” button); F12 in Inspect is sealed execute to a BP or live.
- Opt+Left stays unbound in time travel. `focus_cycle` is landed `apple2_cycles`. Wire `mode=forensic` is time travel.
- HST1 FIND kept (`history-find` / query tests). No tape verbs on the socket.
- ctest 59 green. **Not going to TM6.**
