# TMA2 — Delete TM1 tape-nav (required cleanup)

**Status:** Roadmap (implementation brief).  
**Epic:** [`timemachine.md`](timemachine.md)  
**Prev / Next:** [`TMA1.md`](TMA1.md) / —  
**Depends on:** **TMA1 Landed.** Do not rip TM1 while the Inspector still calls it.

Related: [`TMA0.md`](TMA0.md) · [`TM1.md`](TM1.md) (history of what we are deleting) · [`TM5.md`](TM5.md).

This is a **layer**. TM1.md stays as the record of the tape-nav experiment. The **code** for that experiment goes away so the next reader is not sent into a 17 s `previous()` walk that no product surface uses.

---

## Why

TM1 (`runtime_tm_query`, `SEEK_CYCLE`, tape step/over/out/run-to) was built **only** so the Inspector could move on HST1 instead of a FIND page loop. The Inspector TMA0 wants is film / **land** / re-execute. The socket never spoke TM1.

Leaving the engine after TMA1 is a trap: someone will read `tm_seek_cycle`, wire a slider to it, and recreate the POS.

---

## Goal

Remove the TM1 **query** layer and every product/test caller. Forensic debug is TMA1: land + sealed execute. HST1 remains a **flight recorder** (FIND: “who wrote `$22` to `$2011`”).

**Win:** `rg runtime_tm_query` / `TM_QUERY` / `tm_seek_cycle` / `runtime_client_tm_step` hits **nothing** in `src/` except maybe a comment pointing here.

---

## Keep (not this cleanup)

| Keep | Why |
|------|-----|
| HST1 **recording** (observer, ring, markers) | FIND / “who wrote this byte” |
| `history-find` / `history-next` / `history-read` / `history-close` / `history-info` | Socket + future UI FIND |
| `runtime_history_*` query (`has_address`, write, value) | Same |
| Frame ring | TMA0 preview (A11) |
| Checkpoint ring, land, sealed replay, enter/exit NOW | TMA1 |
| TM5 **store** (add/clear/list, Opt+B forensic) | Still a separate BP list |
| `exit-forensic`, `mode=forensic`, `focus_cycle` on `get-state` | A2M honesty; `focus_cycle` = **landed `apple2_cycles`**, not an HST1 id |

---

## Delete

### Engine

- `runtime_tm_query` and all `tm_seek_*` / `tm_step*` / `tm_run_to_pc` / `tm_run_until_break` in `runtime_timemachine.c`
- Enums `runtime_tm_query_op` / `runtime_tm_query_status` (and `runtime_tm_focus` if it exists only to hold an HST1 tape head — replace wire `focus_cycle` with machine cycles)
- `RUNTIME_COMMAND_TM_QUERY`, `RUNTIME_COMMAND_TM_RUN_UNTIL_BREAK`
- `runtime_client_tm_query` / `_step` / `_step_over` / `_step_out` / `_run_to` / `_seek_id` / `_seek_cycle` / `_run_until_break`
- `RUNTIME_EVENT_TM_FOCUS`
- `runtime_coalesce_tm_tape_seeks` / `runtime_command_is_tm_tape_seek`

### UI leftovers (if TMA1 did not already)

- `FRONTEND_DEBUGGER_INTENT_TM_SEEK_CYCLE`, `_TM_RUN_TO`, `_TM_RUN_UNTIL`
- Slider/coalesce/`dispatch_intent` cases for those
- Forensic F10/F11/Shift+F10/F12 still calling `runtime_client_tm_*`
- Opt+Left in forensic → **must not** tape run-to. Pin: sealed **run-to-cursor** on the landed Apple (execute, not HST1). Poke-PC stays rejected (A9).
- Breakpoints tab **“Run tape to breakpoint”** → **“Run to Time Machine breakpoint”**: sealed execute from the landed state until a TM5 hit or the checkpoint window’s newest cycle. No HST1 scan.

### Tests / build

- Remove `tests/runtime/test_runtime_tm_query.c` and its `CMakeLists.txt` / `testing.md` row
- `test_runtime_tm_forensic.c`: seeks → **land**; drop tape-status asserts
- `test_runtime_tm_bp.c`: `seek_cycle` + `run_until_break` → land + sealed run-until-TM-BP

### Docs in the same change

- `control-tools.md`: tape seek/step stay off the wire **because they do not exist**. FIND stays.
- `manual/manual.md`: forensic keys = land / frame-step / live-shaped execute, not “tape”
- `TM1.md`: one line at the top — **code removed in TMA2; this file is history**
- `TM5.md`: run-until is sealed re-execute, not “index scan then materialize” as the product path
- `status.md` / README: TMA2 Landed

Do **not** rewrite TM0–TM6 bodies. Layer, as with TMA0.

---

## TM5 after the cut

TM5’s own brief offered two routes. **Product path after TMA2 is sealed re-run only** (the D5a payoff):

1. Landed Apple, seal on, TM BP store armed, live BP engine not confused.  
2. Execute until a TM5 condition hits or you hit the newest reconstructable cycle.  
3. Stop, publish. Miss is honest.

Do not reimplement run-until as `runtime_history_find` + materialize. FIND stays available for “who wrote this”; it is not how F12/TM-BP run works.

---

## Order of work

1. Confirm TMA1 Inspector no longer calls TM1 (rg the delete list in `src/frontend` + `src/main.c`). If it still does, **stop** — finish TMA1.  
2. Rewire TM5 run-until + Opt+Left + any remaining intents to sealed execute / land.  
3. Delete engine + client + commands + events.  
4. Fix/remove tests.  
5. Docs. `rg` the delete list — empty in `src/`.  
6. Build + ctest. Manual: Inspect still lands; FIND over the socket still answers a write query; forensic TM BP run still stops.

---

## Code anchors

| Area | Path |
|------|------|
| Query engine | `src/runtime/runtime_timemachine.c` (`runtime_tm_query` down) |
| Commands / events | `runtime_command.h`, `runtime_event.h` (`TM_QUERY`, `TM_FOCUS`) |
| Client | `runtime_client.c` / `.h` `runtime_client_tm_step*` / `seek*` / `run_to` / `run_until_break` |
| Worker | `runtime_thread.c` `RUNTIME_COMMAND_TM_QUERY`, coalesce-seek, `TM_RUN_UNTIL_BREAK` |
| Keys / intents | `src/main.c`, `frontend.h` `TM_SEEK_CYCLE` / `TM_RUN_TO` / `TM_RUN_UNTIL` |
| “Run tape to BP” | `frontend.c` breakpoints tab ~“Run tape to breakpoint” |
| Opt+Left forensic | `frontend.c` disasm `SDLK_LEFT` + alt |
| Tests | `test_runtime_tm_query.c` (delete), `test_runtime_tm_forensic.c`, `test_runtime_tm_bp.c` |
| FIND (keep) | `runtime_history.c` query; `control_dispatch.c` `HISTORY_FIND` |

---

## Acceptance checklist

- [ ] `rg 'runtime_tm_query|TM_QUERY|tm_seek_cycle|runtime_client_tm_step|tm_run_until_break' src/` is empty (or comments citing TMA2 only)  
- [ ] `test_runtime_tm_query` gone from CMake + `testing.md`  
- [ ] HST1 FIND still works (existing `runtime_history_query` / control tests)  
- [ ] Inspector land / ± / F10-family unchanged from TMA1  
- [ ] TM5 run-until is sealed execute; button copy does not say “tape”  
- [ ] Socket: no new tape verbs; `history-find` still there; `focus_cycle` = machine cycle  
- [ ] TM1.md marked history-only  
- [ ] ctest green + short Inspector smoke  
- [ ] **Landed** filled  

---

## Agent script

```text
1. Read agents/TMA0.md A10, TMA1.md (must be Landed), this file.
2. If Inspector still calls TM1, stop and finish TMA1.
3. Rewire TM5 run-until + leftover intents to sealed execute.
4. Delete the TM1 query engine and its tests. Do not delete HST1 FIND.
5. rg the kill list. Docs. Build + ctest. Landed. Stop.
```

---

## Landed

Not yet.
