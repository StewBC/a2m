# TM0 — Epic contract + config surface

**Status:** Not started.  
**Epic:** [`timemachine.md`](timemachine.md)  
**Prev / Next:** — / [`TM1.md`](TM1.md)  
**V1 bar:** Required (first phase).

Related: [`rules.md`](rules.md) · [`status.md`](status.md) · [`inspector.md`](inspector.md) ·
[`app_options`](../src/app_options.h) · [`testing.md`](testing.md).

---

## Goal

Lock vocabulary, opt-in recording switches, and agent docs so every later TM phase
shares one language. **No TimeMachine engine work in this phase.**

After TM0, an implementer can open TM1 without re-arguing D1–D15.

---

## Non-goals

- Checkpoint / delta format  
- Materialize / replace `apple2_t`  
- Misc Inspector tab / F7 retirement  
- A2M protocol bump (unless a config wire verb is unavoidable — prefer not)  
- Changing default play performance (recording stays **off** by default)

---

## Pinned decisions this phase must honor

From [`timemachine.md`](timemachine.md): **D6** (opt-in), **D8** (runtime-owned name),
**D14** (F7 disposable), **D15** (Misc tab later). Full table is in the epic — do not
re-litigate.

---

## Scope

### 1. Docs / handoff

- Ensure [`timemachine.md`](timemachine.md) is the active epic (already linked from
  README/status).  
- Mark Inspector I4b+ as parked/superseded (already noted; keep consistent).  
- Add a short **TimeMachine** stub in `status.md` Active (if missing detail) and a
  one-line pointer in `manual/manual.md` only if a user-visible config flag lands —
  otherwise defer full manual until TM4.  
- Each of `TM0.md`–`TM6.md` exists (this campaign); TM0 Landed when config+docs done.

### 2. Config surface (master enable)

Introduce a single **TimeMachine recording enable** (name flexible; prefer clear product
language):

| Surface | Behavior |
|---------|----------|
| INI | e.g. `[debug] timemachine=0\|1` or `timemachine_record=…` (pick one; document) |
| CLI | matching flag / `--timemachine` / `--no-timemachine` |
| Default | **off** (Total Replay / play path stays cheap) |
| Budgets | Keep existing `history_memory_mb` / `frame_ring_memory_mb`; document that when TM is on, V1 implies HST1 + frame ring + (from TM2) checkpoints/deltas under those budgets |

**Semantics for TM0 (even before TM2 exists):**

- `timemachine=0`: no new TM work; existing history/frame-ring may still be toggled by
  their own record APIs — **or** TM0 may define that master-off forces both recorders
  off. **Prefer:** master-off means “TimeMachine product path off”; do not break
  existing `history-record` / `frame-ring-record` for agents unless documented.  
- **Recommended pin for TM0:** add master enable that later phases gate checkpoint/delta
  **and** that UI Inspector mode requires on. Existing HST1/frame record commands remain
  independently usable until TM4 unifies the story — document the temporary dual wording
  honestly.

If unifying “TM on ⇒ history+frames on” in TM0 is cleaner and low-risk, do that and
document it; do not leave ambiguity for TM1.

### 3. Runtime plumbing (minimal)

- Plumb the bool into `app_options` → runtime config (`runtime_internal` / create path).  
- Expose read API for worker + client if needed (`runtime_client` getter or existing
  options snapshot).  
- **No** recorder behavior change required beyond reading the flag (TM1+ consume it).  
- ctest: options parse / INI round-trip if that is the project pattern; else a small
  unit on parse defaults.

### 4. Vocabulary (use these names in code comments / APIs going forward)

| Term | Meaning |
|------|---------|
| TimeMachine | Runtime forensic engine (queries + later materialize) |
| Inspector mode | Debugger forensic mode (UI); drives TimeMachine |
| Tape / head | Retained timeline + current forensic focus |
| Checkpoint / delta | TM2 storage (mention only in docs here) |
| Live NOW | Unscrubbed machine head |
| Forensic THEN | Materialized / focused past (TM3+) |

---

## Code anchors (start here)

| Area | Path |
|------|------|
| Options / INI / CLI | `src/app_options.c`, `src/app_options.h`, `a2m.ini.example` |
| Runtime config apply | `src/runtime/runtime.c` / `runtime_thread.c` (history_memory_mb already) |
| Status / README | `agents/status.md`, `agents/README.md`, `agents/timemachine.md` |
| Legacy Inspector | `agents/inspector.md` |

---

## Acceptance checklist

- [ ] Master TimeMachine enable in INI + CLI; **default off**  
- [ ] Flag reaches runtime config; readable where TM1 will gate  
- [ ] `a2m.ini.example` + short agent/doc note: off = play, on = debug recording path  
- [ ] Epic + phase files linked; Inspector supersession consistent  
- [ ] Build + full ctest green  
- [ ] **Landed** section filled below  

---

## Agent script

```text
1. Read agents/rules.md, agents/timemachine.md (D1–D15), agents/TM0.md (this file).
2. Add config flag + plumb to runtime; update ini example.
3. Align status/README/timemachine Landed pointers; do not implement TM1 APIs.
4. Build + ctest. Write Landed. Stop.
```

---

## Landed

_(empty until implemented)_
