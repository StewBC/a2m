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

After TM0, an implementer can open TM1 without re-arguing D1–D18.

---

## Non-goals

- Checkpoint ring / input log / sealed replay (TM2)  
- Materialize / replace `apple2_t`  
- Misc Inspector tab (TM4)  
- A2M protocol bump (unless a config wire verb is unavoidable — prefer not)  
- Changing default play performance (recording stays **off** by default)

---

## Pinned decisions this phase must honor

From [`timemachine.md`](timemachine.md): **D6** (opt-in), **D8** (runtime-owned name),
**D14** (F7 already retired — nothing to remove), **D15** (Misc tab later, net-new UI).
Full table is in the epic — do not re-litigate.

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
| INI | `[debug] timemachine=0\|1` |
| CLI | `--timemachine` / `--no-timemachine` |
| Default | **off** (Total Replay / play path stays cheap) |
| Budgets | Keep existing `history_memory_mb` / `frame_ring_memory_mb` (both read from `[debug]`, [`app_options.c:1892,1908`](../src/app_options.c)); **add `timemachine_memory_mb`** there too, default **128** (≈ 800 checkpoints at ~160K each) |
| Footprint | State the aggregate plainly: 256 + 128 + 128 = **512 MB** when TimeMachine is on. Opt-in, but it should be a documented number, not a discovered one |

**Semantics — pinned, not "preferred". Do not re-litigate:**

1. `timemachine=1` **implies** HST1 recording + frame ring + (from TM2) the checkpoint
   ring. One product path, one switch. This is the epic's own open-item default
   ("recording-on implies frame ring + HST1 always: **Yes** for V1").
2. `timemachine=0` forces the TimeMachine product path off. It does **not** remove the
   existing `history-record` / `frame-ring-record` control verbs — those keep working
   standalone for agents, and are the pre-TimeMachine behaviour.
3. Turning TM on from off is what arms the recorders; an agent that then calls
   `history-record off` **or** `frame-ring-record off` gets what it asked for, and
   Inspector enter (TM3) fails honestly because `tm_window` is empty. No hidden
   re-arming. Same rule, same test for both recorders — pin 1 is one product path,
   and D17 is the intersection, so either recorder off empties the window.
4. UI Inspector mode (TM4) **requires** `timemachine=1`.
5. `timemachine=1` with a required recorder budget of **0** is an **honest empty
   tape**, not a refuse and not a silent override of the typed 0. Reuse the existing
   `runtime_history_unavailable_reason` vocabulary (`DISABLED_BY_CONFIG` is what 0
   already maps to). Warn once at startup; TM4 shows *why* the window is empty.
   Garbage / out-of-range values follow existing `app_options` numeric handling.

Document the dual wording honestly in `control-tools.md` while both surfaces exist.

### 3. Runtime plumbing (minimal)

- Plumb the bool into `app_options` → runtime config (`runtime_internal` / create path).  
- Expose read API for worker + client if needed (`runtime_client` getter or existing
  options snapshot).  
- **Arm HST1 + frame ring on the TM off→on edge** (startup with `timemachine=1`, or a
  later runtime enable). Arming is an edge, not a hold: while TM stays on, the
  standalone `history-record` / `frame-ring-record` verbs remain the source of truth.
  **No checkpoint work** — TM2 adds the checkpoint ring as one line in this same
  arming function.  
- `timemachine=0` does not stop standalone recording; it only skips the TM arm.  
- ctest: options parse / INI round-trip; pin-3 no-rearm for **both** recorders;
  zero-budget honest empty tape (do not assert on warning text).

### 4. Vocabulary (use these names in code comments / APIs going forward)

| Term | Meaning |
|------|---------|
| TimeMachine | Runtime forensic engine (queries + later materialize) |
| Inspector mode | Debugger forensic mode (UI); drives TimeMachine |
| Tape / head | Retained timeline + current forensic focus |
| Checkpoint | Full `apple2_snapshot` blob in the TM2 ring (mention only in docs here) |
| Input log | Timestamped host input + nondeterminism record (TM2) |
| Sealed replay | Re-execution with observers/audio/media side effects muted (D16) |
| `tm_window` | Intersection of HST1 / frame ring / checkpoint coverage (D17) |
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
- [ ] `timemachine_memory_mb` budget option plumbed (consumed in TM2)  
- [ ] Flag reaches runtime config; readable where TM1 will gate  
- [ ] TM off→on arms HST1 + frame ring once; no hidden re-arm after standalone off  
- [ ] `timemachine=1` + budget `0` → honest empty tape (`DISABLED_BY_CONFIG`); one startup warning  
- [ ] `a2m.ini.example` gains a **`[debug]` section** — it has none today (sections are window/machine/Slots/config/DiskII/SmartPort/input), so `history_memory_mb` and `frame_ring_memory_mb` are undocumented there. Add them alongside `timemachine`  
- [ ] Doc note: off = play, on = debug recording path; state the 512 MB aggregate  
- [ ] `control-tools.md` notes the TM master switch vs standalone `history-record` / `frame-ring-record`  
- [ ] Epic + phase files linked; Inspector supersession consistent  
- [ ] Build + full ctest green  
- [ ] **Landed** section filled below  

---

## Agent script

```text
1. Read agents/rules.md, agents/timemachine.md (D1–D18), agents/TM0.md (this file).
2. Add config flag + plumb to runtime; arm HST1 + frame ring on the off→on edge;
   update ini example. No checkpoint work.
3. Align status/README/timemachine Landed pointers; do not implement TM1 APIs.
4. Build + ctest. Write Landed. Stop.
```

---

## Landed

_(empty until implemented)_
