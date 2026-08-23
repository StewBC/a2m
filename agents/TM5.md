# TM5 — Forensic breakpoint / watch system

**Status:** Landed. **V1.1** (not required for TimeMachine V1 bar).  
**TMA overlay:** the second Time Machine BP bank is leftover. TMA1 already uses the **one** live list in time travel; [`TMA2.md`](TMA2.md) deletes this store. This file is the record of the tape-scan experiment.  
**Epic:** [`timemachine.md`](timemachine.md)  
**Prev / Next:** [`TM4.md`](TM4.md) / [`TM6.md`](TM6.md)  
**Depends on:** TM1–TM3 (queries + sealed materialize); best after TM4 chrome.

Related: [`breakpoints.md`](breakpoints.md) · [`rules.md`](rules.md).

---

## Goal

“Stop when the **tape** hits condition X” with chrome that feels like live breakpoints,
without mutating or confusing the **live** BP engine used on free-run resume.

---

## Non-goals

- Replacing live BP product path  
- Disk-conditioned BPs  
- Guaranteed agent wire parity on day one (UI + runtime first; A2M optional)  
- Promote/Branch (TM6)  

---

## Model

| Store | When armed | When evaluated |
|-------|------------|----------------|
| **Live BPs** | Free-run / live step | Machine execution (existing) |
| **TimeMachine BPs** | Forensic mode | Tape seek / run-to / “run forward on tape” materialize loop |

- Separate list in runtime (or hard `forensic` flag on definitions **plus** separate
  arming — prefer **separate store** to avoid resume surprises).  
- UI: same Misc Breakpoints family, labeled **Time Machine** / forensic; empty hint
  when in live mode.  
- Opt+B in forensic arms TM store, not live.  
- No silent copy into live list; optional explicit “Copy to live” later (non-goal now).

### Match kinds (V1.1)

1. **Execute** PC (range optional if cheap)  
2. **Mem write** to address — HST1 already records `DATA_WRITE` accesses with address and
   value per instruction, and `runtime_history_find` already filters on
   `has_address` + `access_mask`. No new storage needed.  

Conditions (regs/mem-at-THEN): evaluate against **materialized** state at candidate
records — start minimal (PC only) if needed; extend in same phase if low cost.

### Run-tape-to-BP — two routes

| Route | How | When |
|-------|-----|------|
| **Index scan** | `runtime_history_find` matches PC / address / value on HST1, then a single materialize on the hit | **Default.** No state rebuild per candidate; fastest for simple match kinds |
| **Sealed re-run** | Load nearest checkpoint, then re-execute under the seal (D16) **with the live BP engine armed** | For conditions HST1 cannot express (register state, memory-at-THEN, complex predicates) |

The second route is the payoff from choosing re-execution over deltas (D5a): forensic
"run until condition" can reuse the existing breakpoint/condition evaluator instead of
reimplementing it against a reconstructed state. Keep the seal engaged throughout — a BP
hit during replay must not publish frames or append HST1 records.

Stop focus on hit; materialize; publish. End-of-tape → honest miss.

---

## Scope

1. Runtime TM BP table: add/update/enable/clear/list.  
2. Evaluate on forensic run-to / dedicated `tm_run_until_break`.  
3. UI list + Opt+B forensic binding.  
4. ctest: arm exec BP → run tape → focus lands on hit; live BP list unchanged.  
5. Docs: live vs TM BPs one paragraph in manual/status.

---

## Code anchors

| Area | Path |
|------|------|
| Live BP engine | `runtime_thread.c` breakpoint match, `runtime_event` BP snapshot |
| Misc BP UI | frontend debugger breakpoints panel |
| TM seek/run | TM1/TM3 APIs |
| Disasm Opt+B | `debugger_disasm` ops (NULL in forensic today — wire here) |

---

## Acceptance checklist

- [x] Separate TM BP store; live list untouched by forensic arming  
- [x] Exec (and write if in scope) hit on tape run  
- [x] UI labeled; Opt+B forensic → TM store  
- [x] ctest + build green  
- [x] Landed filled  

---

## Agent script

```text
1. Read agents/timemachine.md, agents/breakpoints.md, TM4 Landed, TM5.md.
2. Implement TM BP store + tape run-until + UI binding.
3. ctest; manual smoke forensic Opt+B. Landed. Stop.
```

---

## Landed

Handoff. Separate forensic BP store. Live `rt->breakpoints[]` is never written
by TM arming, Opt+B, or tape run-until. F7 stays unbound. No A2M verbs (brief
said optional). Conditions on TM BPs are stored but **not** evaluated; sealed
re-run with the live BP engine is **not** implemented (index scan only).

### Store

| Piece | Name |
|-------|------|
| Table | `rt->tm_breakpoints[RUNTIME_BREAKPOINT_CAPACITY]` (64) |
| Count / ids | `tm_breakpoint_count` / `tm_next_breakpoint_id` (starts at 1) |
| Add | `runtime_tm_bp_add` |
| Toggle exec | `runtime_tm_bp_toggle_execute` (Opt+B) |
| Snapshot | `runtime_tm_bp_fill_snapshot` → `RUNTIME_EVENT_TM_BREAKPOINTS_RESPONSE` |
| Not INI | session-only; `[DEBUG] break.*` remains live |

### Run-until (default route)

`RUNTIME_TM_QUERY_RUN_UNTIL_BREAK` / `runtime_client_tm_run_until_break`.
Must be forensic. Scans from `focus.history_id + 1` (or window oldest).

| TM BP access | HST1 query |
|--------------|------------|
| execute | `has_pc` range |
| write | `has_address` + `RUNTIME_HISTORY_ACCESS_DATA_WRITE` |
| both | two finds; earliest id wins |
| condition.term_count > 0 | skipped (no sealed re-run) |

Hit: `runtime_tm_query` SEEK_ID + live materialize (same TM3 path). Miss:
`END_OF_TAPE`, focus unchanged. Live list unchanged (`runtime_tm_bp` ctest).

### Commands (not on the wire)

`TM_BP_CREATE` / `UPDATE` / `CLEAR` / `CLEAR_ALL` / `SET_ENABLED` / `REQUEST` /
`TM_SET_EXECUTE_BREAKPOINT` / `TM_RUN_UNTIL_BREAK`. Reuse live command union
fields (`create_breakpoint.definition`, etc.).

### UI

Forensic Breakpoints tab: **Time Machine breakpoints**, New/Edit/Enable/Clear
work, **Run tape to breakpoint** button, Opt+B toggles execute at disasm
cursor into the TM store. Live tab shows live list plus a one-line note that
Inspector has a separate list. Intent dispatch: forensic BP intents go to
`runtime_client_tm_bp_*`, not live.

### Tests / docs

`runtime_tm_bp`: live exec BP stays; TM exec BP + seek oldest + run-until lands
on that PC. Gate **60**. `manual/manual.md` + `status.md`.

### What TM6 must not break

Leave Inspector still restores NOW and stays paused. TM BPs do not appear in
the live list after exit. F7 stays unbound.
