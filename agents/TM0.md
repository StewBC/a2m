# TM0 — Epic contract + config surface

**Status:** Landed.  
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

- [x] Master TimeMachine enable in INI + CLI; **default off**  
- [x] `timemachine_memory_mb` budget option plumbed (consumed in TM2)  
- [x] Flag reaches runtime config; readable where TM1 will gate  
- [x] TM off→on arms HST1 + frame ring once; no hidden re-arm after standalone off  
- [x] `timemachine=1` + budget `0` → honest empty tape (`DISABLED_BY_CONFIG`); one startup warning  
- [x] `a2m.ini.example` gains a **`[debug]` section** — it has none today (sections are window/machine/Slots/config/DiskII/SmartPort/input), so `history_memory_mb` and `frame_ring_memory_mb` are undocumented there. Add them alongside `timemachine`  
- [x] Doc note: off = play, on = debug recording path; state the 512 MB aggregate  
- [x] `control-tools.md` notes the TM master switch vs standalone `history-record` / `frame-ring-record`  
- [x] Epic + phase files linked; Inspector supersession consistent  
- [x] Build + full ctest green  
- [x] **Landed** section filled below  

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

Handoff for TM1. Source is the contract; this records what actually shipped.

### Names

| Surface | Name | Default |
|---------|------|---------|
| INI | `[debug] timemachine=0\|1` | **0** (off) |
| CLI | `--timemachine` / `--no-timemachine` | off |
| INI budget | `[debug] timemachine_memory_mb` | **128** |
| CLI budget | `--timemachine-memory=<MiB>` | 128 (extra CLI; the brief listed INI only) |
| Worker | `runtime_tm_set_enabled` / `runtime_tm_enabled` / `runtime_tm_memory_mb` | — |
| Client | `runtime_client_tm_set_enabled(client, enabled, token)` | replies with `HISTORY_STATUS_RESPONSE` |
| Command | `RUNTIME_COMMAND_TM_SET_ENABLED` | **not** on the A2M wire |

Module: `src/runtime/runtime_timemachine.c` / `.h`. No query/materialize APIs.

Budget range matches history: **0 or 16..4096**. INI garbage/out-of-range warns and uses 128; CLI rejects. Typed **0 is honoured**.

No A2M bump. Control-port `history-record` / `frame-ring-record` unchanged.

### Arming (the function TM2 extends)

`runtime_tm_set_enabled(rt, bool)` is the single off→on edge:

1. Sets `rt->timemachine_enabled`.
2. If transitioning **off→on**: `runtime_history_resume` + `runtime_frame_ring_set_recording(true)` (frame only if `frame_ring_memory_mb > 0`).
3. If already on, or turning off: **do not** touch recorders.
4. Turning TM off does **not** stop standalone recording.

Call sites: worker startup (`rt->config.timemachine`) **before** `history_off_on_max` is applied; `RUNTIME_COMMAND_TM_SET_ENABLED` for tests and later TM4 UI.

Max turbo: if TM enable happens while already on max and `history_off_on_max` is set, HST1 is **not** resumed (`history_paused_for_max = true` so leave-max restores). Frame ring is still armed; TM2 owns max-stop for the rest.

### What TM1 gates on

```c
runtime_tm_enabled(rt)          /* worker bool */
runtime_tm_memory_mb(rt)        /* checkpoint budget, unused until TM2 */
rt->history                     /* NULL when history_memory_mb==0 */
rt->frame_ring                  /* capacity 0 when frame_ring_memory_mb==0 */
```

There is no `tm_window` yet. Intersection (D17) is TM1/TM2. A recorder that is available but **not recording** (pin 3) currently still has whatever records it already kept — TM3 Inspector enter must treat “TM on but a required recorder is off / budget 0” as empty window.

Zero history budget: `runtime_history_status.available == false`, `unavailable_reason == RUNTIME_HISTORY_UNAVAILABLE_DISABLED_BY_CONFIG` (existing mapping; not a new enum).

### Discovery that contradicts a naive reading of the brief

HST1 and the frame ring **already record at startup** when their budgets are non-zero (`runtime_history_create` sets `recording=1`; `runtime_frame_ring_init` sets `recording=true`). Default play therefore already allocates 256+128 MB and records, TM or not. TM0 does **not** change that: `timemachine=0` skips the TM arm, it does not stop the existing recorders.

So `timemachine=1` at a default-budget boot is a no-op on the recorders themselves (already on). The TM0 behaviour that is new:

- the enable flag exists and is readable
- off→on after a standalone `history-record off` / `frame-ring-record off` **re-arms**
- while TM stays on, those standalone offs are **not** undone
- `timemachine=1` + typed 0 budget is empty tape + one stderr warning:
  `a2m: timemachine=1 but history_memory_mb=0; TimeMachine window will be empty`
  (and/or `frame_ring_memory_mb=0`)

There is no `--frame-ring-memory` CLI (pre-existing). Frame-ring 0 is INI-only.

### Footprint

Documented on-path aggregate when TM is on: **256 + 128 + 128 = 512 MB**. TM0 does not allocate the checkpoint 128 MB; `timemachine_memory_mb` is stored on the runtime for TM2. Play-cheap for TM is “no checkpoint work + default TM off”, not “HST1/frame rings absent”.

### Tests / gate

- `app_options_mounts` covers default off, CLI on/off, CLI budget 0 vs 5 (reject), INI 0 round-trip, INI garbage → 128.
- `runtime_timemachine` covers arm-at-start, pin-3 both recorders, off→on re-arm, standalone history-record while TM off, history 0 → `DISABLED_BY_CONFIG`, frame 0 → capacity 0.
- Full ctest **56** green (was 55).

### Docs touched

`a2m.ini.example` `[debug]` section; `manual/manual.md` flag + `[debug]` keys + 512 MB; `agents/control-tools.md` dual wording; `agents/status.md` / `testing.md` / `runtime.md`; this file.

### Not in this phase

Checkpoint ring, input log, seal, `tm_window`, query verbs, A2M TM commands, Misc Inspector tab. TM1 starts with `runtime_tm_enabled` + HST1 scans.
