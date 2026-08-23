# TM5 — Forensic breakpoint / watch system

**Status:** Not started. **V1.1** (not required for TimeMachine V1 bar).  
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

- [ ] Separate TM BP store; live list untouched by forensic arming  
- [ ] Exec (and write if in scope) hit on tape run  
- [ ] UI labeled; Opt+B forensic → TM store  
- [ ] ctest + build green  
- [ ] Landed filled  

---

## Agent script

```text
1. Read agents/timemachine.md, agents/breakpoints.md, TM4 Landed, TM5.md.
2. Implement TM BP store + tape run-until + UI binding.
3. ctest; manual smoke forensic Opt+B. Landed. Stop.
```

---

## Landed

_(empty until implemented)_
