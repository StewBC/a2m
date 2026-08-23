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

- Checkpoints / sealed replay / materialize into `apple2_t` (TM2–TM3)  
- Misc Inspector tab (TM4)  
- Replacing live `runtime_client_step_out` machine execution  
- Dual NOW+THEN panels  
- A full reconstructed call-stack *display* (depth tracking is for nav only)  

---

## Product verbs (forensic)

| Verb | Meaning on tape |
|------|-----------------|
| **Focus** | Current HST1 record id + `machine_cycle` (tape head index) |
| **Step** | Next (or prev) instruction record from focus |
| **Step over** | From focus on JSR: advance to the first record whose `sp` has returned to entry level |
| **Step out** | From focus: advance until `sp` rises above entry `sp` (frame popped) |
| **Run to PC** | Next record with `pc == target` at/after focus (optional cycle/span ceiling) |
| **Seek** | Set focus to history id or nearest insn ≤ cycle |

Depth semantics should match live step-over/out where practical (same mental model as
`runtime_step_over` / `runtime_step_out` in `runtime_thread.c`), but operate **only** on
recorded HST1 rows — never call live step APIs. See **Nest / depth notes** below for why
the recorded `sp` is the signal, not JSR/RTS opcode counting.

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
4. **Window clamp (D17):** every op clamps to `tm_window`. In TM1 that is HST1 coverage
   alone; once TM2 lands it becomes the intersection, so route the clamp through one
   accessor rather than reading history status inline at each call site.  
5. **Epoch / timeline:** a seek that would cross an epoch boundary (reset, state load —
   history markers 4/5) is **rejected**, not silently satisfied. Focus carries `epoch`;
   compare it.  
6. **ctest** with recorded fixture or programmatic HST1 fill: step, over, out, run-to,
   window clamp, epoch reject.

There is **no UI to hook up** — F7 is gone (D14) and the Misc tab is TM4. Ship API +
tests. If a control-port smoke path is cheap, take it; otherwise leave all UI to TM4.

---

## Nest / depth notes

**Pin: use the recorded `sp`, not opcode nesting.** Every history record carries `sp`
([`runtime_history.h`](../src/runtime/runtime_history.h) `runtime_history_record`), so
depth is a direct comparison rather than an inference:

- **Step out** = first forward record with `sp > entry_sp` (stack unwound past the frame).  
- **Step over** from a `JSR` = first forward record with `sp >= entry_sp` after the push.  
- This is robust where opcode nesting is not: `RTI`, routines that end by manipulating the
  stack, `PLA`/`PLA` returns, tail-jumps, and IRQ/NMI frames pushed mid-routine. Opcode
  nesting mis-counts all of those.  
- Keep `JSR`/`RTS` opcodes as a corroborating hint (and for the "focus is on a JSR" test),
  not as the primary signal.  
- Stack **wrap** ($01FF→$0100) is the one case SP comparison gets wrong; guard it or
  bail honestly rather than returning a wrong focus.  
- Interrupt records (`RUNTIME_HISTORY_RECORD_IRQ` / `_NMI`) and markers are not
  instruction rows — skip them when evaluating depth, but do not lose them from the walk.  
- If focus is not on a JSR, step-over ≡ step.  
- If the end cannot be found (tape truncated): return honest error / end-of-tape focus + flag.

---

## Code anchors

| Area | Path |
|------|------|
| History core | `src/runtime/runtime_history.c` / `.h` |
| History wire / FIND | `runtime_history_wire.*`, `runtime_thread.c` history_* |
| Live step-over/out (reference) | `runtime_thread.c` `runtime_step_over` / `runtime_step_out` |
| Client | `runtime_client.c` / `.h` |
| Commands / events | `runtime_command.h`, `runtime_event.h` |
| Shared disasm chrome (TM4 consumer) | `src/frontend/debugger_disasm.*` |
| Tests | `tests/runtime/test_runtime_history_*.c`, `test_runtime_step_nested.c` (live — reference only) |

---

## Acceptance checklist

- [ ] Worker TimeMachine query module exists; UI never FIND-loops for over/out/run-to  
- [ ] step / step-over / step-out / run-to-PC / seek implemented on HST1, **depth via recorded `sp`**  
- [ ] All ops clamp to `tm_window`; epoch-crossing seek rejected  
- [ ] `runtime_client` wrappers + events with clear focus payload  
- [ ] ctest covers sp-depth step-out/over, missing-target honesty, window clamp, epoch reject  
- [ ] Does **not** mutate `apple2_t`  
- [ ] Build + full ctest green  
- [ ] Landed filled; note any A2M bump (expect none)

---

## Agent script

```text
1. Read agents/rules.md, agents/timemachine.md, agents/TM0.md (Landed), agents/TM1.md.
2. Implement runtime_timemachine query + client; ctest first.
3. No UI work — F7 is gone and the Misc tab is TM4.
4. Build + ctest. Landed. Stop — do not start TM2 unless brief says so.
```

---

## Landed

_(empty until implemented)_
