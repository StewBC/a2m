# TM2 — Checkpoint + delta recorder

**Status:** Not started.  
**Epic:** [`timemachine.md`](timemachine.md)  
**Prev / Next:** [`TM1.md`](TM1.md) / [`TM3.md`](TM3.md)  
**V1 bar:** Required.  
**Depends on:** TM0 enable flag; bus/CPU observers (remote-debug); snapshot field knowledge
([`snapshots.md`](snapshots.md)).

Related: [`rules.md`](rules.md) · [`machine.md`](machine.md) · [`snapshots.md`](snapshots.md).

---

## Goal

Record a retained window of **checkpoints + deltas** so tests (and TM3) can answer
“machine state at cycle C” for CPU + memory + softswitches/banking.

This phase **records and can materialize into a buffer / temp machine** for tests.
Wiring that buffer onto the live product `apple2_t` as Inspector mode is **TM3**.

---

## Non-goals

- Replacing live machine during Inspector scrub (TM3)  
- Misc UI / F7 retirement (TM4)  
- Disk II / HostFS undo (D10)  
- Bit-perfect Mockingboard/VIA audio scrub (D11) — omit or best-effort card bytes only  
- Changing HST1 format (HST1 remains the insn index; do not overload it as full state)  
- Promote/Branch (TM6)

---

## Storage model

```text
When timemachine recording ON (and machine running/paused as today):

  [CP0] --deltas--> [CP1] --deltas--> ... --deltas--> live head
         ^
         HST1 records and frame ring continue as today (join key: machine_cycle)
```

| Layer | Role |
|-------|------|
| **Checkpoint** | Coherent machine slice at cycle C_k |
| **Delta stream** | Ordered mutations between checkpoints (and toward live) |
| **HST1** | Instruction index / TM1 queries (unchanged role) |
| **Frame ring** | Paint samples (unchanged role) |

### Checkpoint contents (V1 required)

Must be enough to restore views that read “the machine”:

- CPU: PC, A, X, Y, SP, P, and any micro flags the emulator needs at insn boundary  
- Main RAM + Aux RAM (full or documented subset — **default full** both banks for //e)  
- Softswitch / banking / LC / related `state_flags` (and anything `softswitch_apply_full_map` needs)  
- Beam counters (line, cycle_in_line, frame counters) if display/debug panels need them  

**Omit / stale OK in V1:** Disk II controller head/track, HostFS, MB/VIA deep microstate.

Reuse knowledge from `apple2_snapshot_*` but prefer a **lean ring-resident** layout
(not necessarily on-disk `.a2state` chunk format). Version the blob.

### Delta grammar (V1)

Capture through existing choke points:

- Memory **writes** (address + value + space/bank tag as needed)  
- Softswitch / I/O side effects that change `state_flags` or maps  
- Optional: insn-boundary CPU reg packed delta if cheaper than full CPU each time  

Reads need not be deltas (HST1 already has access detail for forensics).

**Encoding:** versioned append-only ring (or arena) with budget from TM0/options.
Drop oldest checkpoint **and** its following deltas when over budget; keep honesty
counters (dropped, oldest cycle retained).

### Cadence (initial pin — tune later)

- Emit checkpoint at **retained frame boundary** (when a frame is pushed to the frame
  ring), **or** at least every N ms / M cycles if frames are sparse (turbo/max).  
- Always allow forced checkpoint on recorder start and on Inspector enter (TM3).  
- Document chosen constants in Landed.

---

## APIs (worker-internal + testable)

Illustrative names:

```text
runtime_tm_recorder_set_enabled(rt, bool)   // gated by TM0 master + live record
runtime_tm_checkpoint_take(rt)              // force CP at current cycle
runtime_tm_materialize(rt, cycle, apple2_t *dst)  // CP≤cycle + replay deltas → dst
runtime_tm_window_info(...)                 // oldest/newest cycle, counts, dropped
```

- `materialize` in TM2 may write to a **scratch** `apple2_t` or byte buffer for tests —
  must not disturb live machine unless explicitly testing in isolation.  
- When recording **off**: zero cost on the insn path (no delta append).

---

## Opt-in gating

- TM0 master off → no checkpoint/delta work.  
- When master on: start/stop with product record policy (align with history/frame record
  as pinned in TM0 Landed).  
- Clear on history clear / state load / reset as appropriate (same class of events that
  already invalidate rings — see sessions mutation set).

---

## Code anchors

| Area | Path |
|------|------|
| Snapshot field inventory | `src/machine/apple2_snapshot.c` / `.h`, `apple2.h` |
| Bus choke | `apple2_bus_read` / `apple2_bus_write`, memory access callback |
| CPU observer | `apple2_set_cpu_observer` → history today |
| Softswitch maps | softswitch apply / `state_flags` |
| Frame push | `runtime_frame_ring_push` call sites in `runtime_thread.c` |
| History lifecycle | `runtime_history_*` record on/off/clear |
| New module | `src/runtime/runtime_timemachine*.c` (extend TM1 module or sibling) |

---

## Testing

| Test | Expect |
|------|--------|
| CP round-trip | take CP → materialize at that cycle → CPU/mem/flags match |
| Mid-window | run N insns with recording → materialize at mid cycle → match golden capture |
| Budget drop | fill past budget → oldest advances; info honest |
| Off path | enable off → insn path does not grow TM buffers |

Prefer deterministic headless runtime tests (pattern: `tests/runtime/test_runtime_history_*.c`).

---

## Acceptance checklist

- [ ] Versioned checkpoint + delta recorder behind TM0 enable  
- [ ] V1 required state restored by `materialize(cycle)` in tests  
- [ ] Cadence + budget + drop policy documented in Landed  
- [ ] Recording off = no TM delta/CP cost on hot path  
- [ ] MB/Disk deep state explicitly out of V1 completeness  
- [ ] Build + full ctest green (new tests in gate)  
- [ ] Landed filled  

---

## Agent script

```text
1. Read agents/rules.md, agents/timemachine.md D5/D6/D10/D11, agents/TM0–TM1 Landed,
   agents/snapshots.md, agents/TM2.md.
2. Spike checkpoint layout + delta ops; implement recorder + materialize-to-dst.
3. ctest goldens; measure rough size/cadence; pin constants in Landed.
4. Do not wire Inspector replace-live yet (TM3). Stop.
```

---

## Landed

_(empty until implemented)_
