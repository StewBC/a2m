# TM3 — Materialize into one true state

**Status:** Not started.  
**Epic:** [`timemachine.md`](timemachine.md)  
**Prev / Next:** [`TM2.md`](TM2.md) / [`TM4.md`](TM4.md)  
**V1 bar:** Required.  
**Depends on:** TM1 (tape focus/verbs), TM2 (checkpoint/delta materialize).

Related: [`rules.md`](rules.md) · [`sessions.md`](sessions.md) · [`timemachine.md`](timemachine.md) (D2, D3, D4, D9, D12).

---

## Goal

When forensic/Inspector mode is active, TimeMachine **replaces** the live `apple2_t`
with reconstructed past at the tape head. Existing views keep reading the machine;
as the head advances, deltas apply and display/softswitches update “for real.”

Exit forensic mode → restore **live NOW** anchor. Machine must not be left in a random past.

---

## Non-goals

- Misc Inspector tab polish / F7 removal (TM4) — TM3 may add runtime mode + minimal
  client enter/exit even if UI still uses a thin F7 or debug hook  
- Forensic BP store (TM5)  
- Promote/Branch (TM6)  
- Disk/HostFS undo  
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
2. Take **live NOW anchor** (full checkpoint / snapshot of current machine — simplest
   correct; pin in Landed).  
3. Ensure recording window exists; if empty, fail enter honestly.  
4. Set mode=forensic; materialize to initial focus (usually newest or selected cycle).  
5. Publish state + `state-changed` (reason: forensic enter / seek).  

### Seek / step on tape (forensic)

1. TM1 query moves focus (id/cycle).  
2. `materialize(focus.cycle)` into live `apple2_t` (TM2 engine).  
3. Remap softswitches / page maps after load.  
4. Publish CPU, memory-dependent views, softswitches; display from RAM+flags (and/or
   nearest frame ≤ cycle — prefer live paint from materialized state when possible so
   mid-frame tape walk updates as writes land).  
5. `state-changed` so peers see shared mutation.  

**Forward scrub:** incremental apply if implementation can; **backward:** rebuild from
earlier CP + replay (D12).

### Exit forensic (non-promote)

1. Restore live NOW anchor into `apple2_t`.  
2. Mode=live; clear forensic read-only.  
3. Publish + `state-changed`.  
4. **Do not** auto-resume (same policy as Inspector today).  

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

Optional thin A2M verbs — only if agents need enter/seek; otherwise client-only until
TM4.

---

## Honesty

- Status: `forensic @ cycle …` (host and/or control).  
- Disk/HostFS: one-line caveat (media not rewound).  
- If materialize fails (cycle older than window): keep prior state or restore anchor;
  return error — never half-apply silently.

---

## Code anchors

| Area | Path |
|------|------|
| TM1/TM2 module | `src/runtime/runtime_timemachine*` |
| Worker machine owner | `runtime_thread.c`, `runtime_internal.h` |
| Snapshot restore patterns | `apple2_snapshot_load`, post-load `softswitch_apply_full_map` |
| Mutation invalidate set | `runtime_history_command_invalidates_cursor` / sessions |
| Publish CPU/mem | existing publish helpers after step/pause |
| state-changed | sessions / control push |

---

## Testing

| Test | Expect |
|------|--------|
| Enter/exit | exit restores CPU/mem to pre-enter NOW |
| Seek | after seek, `get-cpu`/mem match materialize golden |
| Read-only | poke/step-live fails in forensic |
| state-changed | fired on enter/seek/exit |
| Backward seek | rebuild path matches forward materialize at same cycle |

---

## Acceptance checklist

- [ ] Forensic mode replaces `apple2_t` with past; views can use normal get-*  
- [ ] Enter anchors NOW; exit restores NOW  
- [ ] Tape step/seek materializes; forward/back per D12  
- [ ] Runtime rejects mutating commands in forensic  
- [ ] `state-changed` on scrub/enter/exit  
- [ ] Media caveat documented for UI to show later  
- [ ] Build + ctest green  
- [ ] Landed filled  

---

## Agent script

```text
1. Read agents/rules.md, agents/timemachine.md D2–D4/D9/D12, TM1–TM2 Landed, TM3.md.
2. Implement enter/exit + seek-materialize-into-live + read-only guards.
3. ctest enter/exit/seek/reject. Optional minimal UI hook for manual smoke.
4. Do not build Misc tab (TM4). Stop.
```

---

## Landed

_(empty until implemented)_
