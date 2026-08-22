# TM1 — Query engine on existing HST1 (fast tape verbs)

**Status:** Not started.  
**Epic:** [`timemachine.md`](timemachine.md)  
**Prev / Next:** [`TM0.md`](TM0.md) / [`TM2.md`](TM2.md)  
**V1 bar:** Required.  
**Depends on:** TM0 (config vocabulary); existing HST1 + sessions (remote-debug C3–C4, sessions S0–S4).

Related: [`rules.md`](rules.md) · [`remote-debug.md`](remote-debug.md) · [`sessions.md`](sessions.md) ·
[`inspector.md`](inspector.md) (I5a verb mapping — salvage ideas only).

---

## Goal

Make forensic navigation a **single worker query** over HST1, not a UI/session FIND page
loop. Prove the TimeMachine “SQL” API before checkpoints exist.

**Win:** step-out / step-over / run-to-PC feel local-fast.

---

## Non-goals

- Checkpoints / deltas / materialize into `apple2_t` (TM2–TM3)  
- Misc Inspector tab / kill F7 (TM4)  
- Replacing live `runtime_client_step_out` machine execution  
- Dual NOW+THEN panels  
- Perfect call-stack via SP reconstruction beyond JSR/RTS opcode nest on the tape  

---

## Product verbs (forensic)

| Verb | Meaning on tape |
|------|-----------------|
| **Focus** | Current HST1 record id + `machine_cycle` (tape head index) |
| **Step** | Next (or prev) instruction record from focus |
| **Step over** | From focus on JSR (or nest point): advance to record after matching RTS at same nest depth |
| **Step out** | From focus: advance until nest depth returns below entry depth (exit current routine) |
| **Run to PC** | Next record with `pc == target` at/after focus (optional cycle/span ceiling) |
| **Seek** | Set focus to history id or nearest insn ≤ cycle |

Nesting rules should match live step-over/out semantics where practical (same mental
model as `runtime_step_over` / `runtime_step_out` in `runtime_thread.c`), but operate
**only** on recorded HST1 instruction rows — never call live step APIs.

---

## Architecture

```text
UI / forensic accessors
        │
        ▼
runtime_client_timemachine_*  (or runtime_client_tm_*)
        │
        ▼
RUNTIME_COMMAND_TM_*  →  worker
        │
        ▼
runtime_timemachine_query(rt, op, focus, args) → result focus
        │
        └── scans runtime_history arena (HST1), no apple2 mutate
```

- Own module under `src/runtime/` (e.g. `runtime_timemachine.c` / `.h`).  
- Worker-serialized; no new thread (D7).  
- Session id: accept session for future/coop consistency; query itself is mostly
  stateless given absolute focus id — still emit `state-changed` only if focus is
  published as shared runtime forensic cursor (optional in TM1; **required** once TM3
  mutates machine). For TM1, publishing a runtime “TM focus” event to the UI is enough.  
- **Do not** require A2M wire verbs in TM1; add only if cheap and tests want them.
  Prefer `runtime_client` first (epic default).

---

## Scope

1. **Focus model** on runtime: current forensic focus `{history_id, epoch, cycle, pc, …}`
   (minimal fields). Initialize from “newest” or caller-provided seek.  
2. **Query ops** implemented against `runtime_history` (random access / iterate by id).  
3. **Client API** + command/event types for request/response (mirror existing token style).  
4. **ctest** with recorded fixture or programmatic HST1 fill: step, over, out, run-to.  
5. **Optional UI hookup:** point F7 forensic tape keys / I5a forensic ops at TM1 client
   APIs if F7 still exists — **do not** invest in F7 chrome. If hookup is messy, ship
   API + tests only and leave UI to TM4. Prefer at least one path exercised from UI or
   control for smoke if low cost.

---

## Nest / opcode notes

- Treat `JSR` / `RTS` (and 65C02 equivalents the live stepper already cares about) as
  nest ±1 when scanning forward from focus.  
- Interrupt records: define behavior (skip for nest depth, or treat as non-nest). Pin
  in Landed: **recommend skip markers/IRQ/NMI for depth**, only instruction rows.  
- If focus is not on a JSR, step-over ≡ step.  
- If cannot find end (tape truncated): return honest error / end-of-tape focus + flag.

---

## Code anchors

| Area | Path |
|------|------|
| History core | `src/runtime/runtime_history.c` / `.h` |
| History wire / FIND | `runtime_history_wire.*`, `runtime_thread.c` history_* |
| Live step-over/out (reference) | `runtime_thread.c` `runtime_step_over` / `runtime_step_out` |
| Client | `runtime_client.c` / `.h` |
| Commands / events | `runtime_command.h`, `runtime_event.h` |
| Forensic UI (optional consumer) | `src/frontend/debugger_disasm.*`, `frontend_inspector_*` |
| Tests | `tests/runtime/test_runtime_history_*.c`, `test_runtime_step_nested.c` (live — reference only) |

---

## Acceptance checklist

- [ ] Worker TimeMachine query module exists; UI never FIND-loops for over/out/run-to  
- [ ] step / step-over / step-out / run-to-PC / seek implemented on HST1  
- [ ] `runtime_client` wrappers + events with clear focus payload  
- [ ] ctest covers nest step-out/over and missing-target honesty  
- [ ] Does **not** mutate `apple2_t`  
- [ ] Build + full ctest green  
- [ ] Landed filled; note any A2M bump (expect none)

---

## Agent script

```text
1. Read agents/rules.md, agents/timemachine.md, agents/TM0.md (Landed), agents/TM1.md.
2. Implement runtime_timemachine query + client; ctest first.
3. Optionally retarget forensic step keys to TM1; do not build Misc tab.
4. Build + ctest. Landed. Stop — do not start TM2 unless brief says so.
```

---

## Landed

_(empty until implemented)_
