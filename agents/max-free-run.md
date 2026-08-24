# Max free-run (instruction quanta) — balls-to-the-wall + 60 Hz paint

**Status:** **Landed** (M0–M2; perf residual vs 80 MHz floor)  
**Policy:** **S2** — max uses a different free-run path (instruction quanta); finite MHz stay beam-accurate  
**Related:** [`turbo-zip.md`](turbo-zip.md) (paint + ladder) · [`runtime.md`](runtime.md) · [`status.md`](status.md)

Source is authoritative once phases land. If this handoff and source disagree,
fix the handoff in the same change.

---

## Why

Turbo-zip made max show a live picture (block paint ~60 Hz) but left free-run on
the **Φ0 `step_cycle` + runtime tax** skeleton. Measured ~20–30 MHz machine /
product; a2m max is ~100+ MHz. Paint was not the bottleneck.

Product intent for **max**:

1. Go as fast as the host allows (C0+C1 correctness).
2. Still **draw** ~60 FPS presentation (not blank warp).
3. Leave max → finite: beam re-seeded; normal software still coherent.
4. Vapor-lock / beam-chasing demos may glitch under max — accepted.

---

## Correctness contract

| Class | Meaning | Max |
|-------|---------|-----|
| **C0** | Instruction + mem + softswitches + banking | **Keep** |
| **C1** | Devices on *elapsed* Φ0 (disk spin, VIA/AY budgets, paddles) | **Keep** (batch by insn cycle count) |
| **C1v** | VBL/HBL visible via `$C019` / beam counters | **Keep** at instruction granularity (O(1) A-lite). Frozen `$C019` hangs VBL waiters (Total Replay). Mid-insn exactness is not required. |
| **C2** | Per-Φ0 beam **pixel paint** / mid-insn scanner | **Drop** while max; reseed on exit |
| **C3** | Floating bus / vapor-lock pixels | **Drop** while max |
| **C4** | Debugger (history every insn) | Prefer keep; degrade only if gate fails |

---

## Behaviour

### Max free-run path

```text
each wall quantum (~1/60 s):
  while wall time remains in quantum:
    BP check at instruction boundary (if any BP / temp)
    apple2_step_instruction_max()   // whole insn; O(1) A-lite H/V; no paint
    type-script tick with ran cycles (cheap)
  block paint full frame + publish (live slot + ring)
```

- **No** per-Φ0 scanner / paint-at-beam. A-lite H/V (and `$C019`) advance O(1) per instruction from elapsed Φ0.
- **No** per-Φ0 audio PCM path (same as today free-run: AY reconcile optional / once per quantum max).
- Peripherals: **once per instruction** with `ran` cycles (`peripherals_step(ran)`).
- Finite N MHz: unchanged paced beam path.

### Enter / leave max

| Event | Action |
|-------|--------|
| Enter max | `paint_enabled = false`; start block-paint wall timer; paint once immediately |
| Leave max → finite | `apple2_video_reseed_from_cycles`; `paint_enabled = true`; reset pacer |
| Opt+T / set-turbo / FAST/SLOW | Same helpers |

### Non-goals

- Blank warp.
- Matching a2m MHz exactly.
- Beam-accurate pixels during max.
- Dual CPU cores (a2m opcode port) unless S2 plateaus below gate after measure.

---

## Phases

### M0 — API

| Task | Detail |
|------|--------|
| `apple2_step_instruction_max` | Full instruction; **no** pixel paint; O(1) A-lite H/V/VBL; `peripherals_step(ran)` once; return Φ0 ran |
| `apple2_video_reseed_from_cycles` | `line` / `cycle_in_line` from `cycles % frame_geometry` |

### M1 — Runtime max loop

| Task | Detail |
|------|--------|
| Free-run max | Wall-quantum insn loop (a2m-shaped), not 1024× `step_cycle` |
| Finite free-run | Keep existing Φ0 batch + pace |
| Audio | Max: no per-cycle produce; optional no-op / quantum reconcile |
| VBL | O(1) A-lite H/V per insn so `$C019` waiters complete; not per-Φ0 paint |
| Leave max | Reseed beam |

### M2 — Gate + docs

| Gate (Release) | Bar |
|----------------|-----|
| Machine or product max free-run | **≥ 80 MHz** emulated preferred; document if residual |
| Finite 1× | Still ≥ real-time with beam |
| ctest | Green |
| Leave max | Beam reseeds; no permanent desync for normal software |

| Task | Detail |
|------|--------|
| Bench | `bench_realtime` / product measure; record in turbo-zip or here **Results** |
| Agents | status, runtime, turbo-zip cross-link; close this epic when gate met or residual noted |

---

## Implementation map

| Area | Paths |
|------|--------|
| Machine | `apple2.c` / `apple2.h`, `video.c` / `video.h` |
| Runtime | `runtime_thread.c` free-run + turbo enter/leave |
| Tests | turbo / video reseed smoke; existing gate |
| Bench | `tests/machine/bench_realtime.c` |

---

## Results

`bench_realtime` Release machine-only (HGR white; no history observer):

| Metric | Value | Host / build |
|--------|-------|----------------|
| Pre-S2 max-ish (beam free-run) | ~22–28 MHz | Release beam |
| S2 instruction max (alite) | **~45–50 MHz** | Release `bench_realtime 3 alite` |
| S2 + block paint | ~43–46 MHz | Release `bench_realtime 3 block` |
| Gate ≥80 MHz | **Not met** | Residual: `cpu65_step` + mem map + MB/periph still short of a2m’s 100–160 class |
| Notes | Product max now uses S2 loop + 60 Hz paint (not blank warp). Leave max reseeds beam. Further MHz needs tighter mem/CPU (S3) or coarser device batching. | |
