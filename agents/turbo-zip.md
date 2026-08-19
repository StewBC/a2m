# Turbo as Zip Chip + max (presentation paint)

**Status:** **Landed** (phases 0–5)  
**Policy:** **A** (beam for all finite MHz targets; block paint only for **max**)  
**Related:** [`status.md`](status.md) ·
[`runtime.md`](runtime.md) · [`video.md`](video.md) · [`video-paint.md`](video-paint.md) ·
[`rules.md`](rules.md) · [`testing.md`](testing.md)

Reference (sibling): `../a2m` — free-run pacing in `src/runtime/rt.c`; full-frame
paint matrix in `src/ui/nuklear/unk_apl2.c` (`unk_apl2_screen_*`, mode switch).

Source is authoritative once phases land. If this handoff and source disagree,
fix the handoff in the same change.

---

## Why

a2m free-run with **beam paint every Φ0** is accurate but slow (~tens of MHz
Release machine-only; product Debug much less). Old a2m free-run is much faster
because video is a **periodic full-frame RAM→ARGB** pass, not a beam rider.

Users want both:

1. **Zip-class finite speeds** (1 / 4 / 8 MHz…) with **best-effort cycle + beam
   accuracy** (status checks, demos).
2. **Max** (“damn the torpedoes”) with a **live picture** and frame ring still
   useful — not blank “warp.”

Blank warp (old turbo 3) is a non-goal: no display, no joy, little gain.

---

## Product decision (locked)

| Topic | Decision |
|-------|----------|
| Philosophy | Finite MHz = same accurate emulator, paced faster. **Max** = all-out + presentation paint. |
| Policy | **A:** beam for **all finite** targets; **block (full-frame) paint only for max**. |
| Default ladder | `1,max` |
| Finite `N` | Target **`N × APPLE2_CPU_FREQUENCY_HZ`** emulated Φ0 per wall second. UI labels: “N MHz”. (`1` = real-time Apple.) |
| Max sentinel | INI/CLI/UI: **`max`** and **`-1`** (aliases). Internal: `is_max` + optional `target_hz`. |
| Block paint | **Full a2m mode matrix** (text40/80, lores, **dlores**, hgr, dhgr, mixed, page1/2, mono paths if a2m has them). Machine-owned → ARGB publish / frame ring. |
| Beam during max | **A-lite:** advance **cheap** beam timing (position + VBL-visible side effects); **no** `paint_at_beam`. If max misses perf gate and video step shows up hot, **B allowed**: skip beam in max free-run, **re-seed from cycle count** on enter finite. |
| Live display (max) | ~**60 Hz wall** block paint into latest-wins frame slot. |
| Frame ring (max) | Fed by those block paints; **every live paint** + existing MB budget. Depth is snapshot-limited (document). Not Φ0-rate. |
| Paste | **Does not change turbo.** Remove paste auto-warp / turbo save-restore. |
| BP FAST / SLOW | **FAST → max**; **SLOW → 1** (lowest finite / real-time). |
| INI past | **No migration** of `1,2,3` / warp. Pretend no before. |
| Build for gates | **Release** numbers, not Debug. |

### Non-goals

- Real Zip Chip cache / slot / bus protocol.
- Beam-accurate **pixels** during max.
- User-facing blank warp.
- Modeling //c+ accelerator quirks beyond “N× Φ0 rate.”
- UI peeks live `apple2_t` (thread rules unchanged).

### Thread / ownership (unchanged)

- Worker owns `apple2_t`, paint, frame publish, frame ring push.
- UI / control only via `runtime_client` (commands + events + frame copies).
- Block paint runs on the **worker**, same as beam paint today.

---

## Target behaviour

### Ladder

```text
turbo = 1,max          # default
turbo = 1,4,8,max      # example zip ladder
turbo = 1,2.5,max      # fractional MHz OK if parser allows
```

| Entry | Pacing | Video |
|-------|--------|--------|
| Finite `N` | Aim `N × APPLE2_CPU_FREQUENCY_HZ` Φ0/wall-s (frame-quantum pace like a2m) | Full **beam** path (current accurate path) |
| `max` / `-1` | Free-run (no cycle budget) | **No** beam paint; **block** full-frame paint ~60 Hz wall |

Opt+T cycles the configured list (same UX as today, new meanings).

### Enter / leave max

- **Enter max:** stop beam pixel paint; enable block-paint presentation path; free-run.
- **Leave max → finite:** restore beam path; if A-lite was used, beam counters should already be coherent; if B was used, **re-seed** `line` / `cycle_in_line` (and any derived VBL) from total Φ0 modulo frame geometry.
- Half-baked last presentation frame is OK; next finite frames are accurate again.

### Paste / breakpoints

- Paste: inject keys only; **no** turbo mutation.
- FAST action → select **max**.
- SLOW action → select **1** (real-time).

### Headless

- Max still free-runs; UI paint optional. Frame ring / request-frame still meaningful if enabled.

---

## Architecture sketch

```text
                    ┌─────────────────────────────┐
  finite N          │  step Φ0 + beam + paint     │  pace to N× Hz
                    │  (current accurate path)    │
                    └──────────────┬──────────────┘
                                   │ publish ARGB / ring
  max               ┌──────────────▼──────────────┐
                    │  step Φ0 (CPU + devices)    │  free-run
                    │  A-lite beam counters only  │
                    │  ~60 Hz: paint_full_from_ram│  a2m algorithms
                    └──────────────┬──────────────┘
                                   │ same frame slot + ring API
                                   ▼
                              runtime_client / UI / control
```

**Block paint source of truth:** port algorithms from a2m `unk_apl2_screen_*`
into `src/machine` (e.g. `video_block_paint.c` / extend `video.c`) so machine
has no UI dependency. Output: existing **560×192 ARGB** contract
(`display_frame` / `APPLE2_VIDEO_*`).

**Double LORES:** in both block paint and the beam path (`paint_dlores_column`;
a2m `double_aux_map` + aux/main 7-px cells; PAGE2 when 80STORE is off).

---

## Phases (execute in order)

### Phase 0 — Spec in code constants + kill warp vocabulary

**Goal:** Types and names match the product; no behaviour guarantee yet.

| Task | Detail |
|------|--------|
| Model | Replace “turbo mode 1/2/3” with ladder of `{ target_hz or is_max }`. |
| Parse | CLI/INI CSV: numbers (int or float MHz), `max`, `-1`. Default `1,max`. |
| Defaults | `runtime_config_set_turbo_defaults` → `1,max` only. |
| Remove | Product meaning of `RUNTIME_TURBO_MODE_WARP` / blank warp path; paste turbo warp. |
| Docs touch | This file status stays Active until epic closes. |

**Done when:** clean build; old warp IDs not part of public client API comments; defaults parse to 1 + max.

---

### Phase 1 — Pacing (finite MHz + max free-run)

**Goal:** Zip pacing without changing video policy yet (still beam everywhere, including max, temporarily OK).

| Task | Detail |
|------|--------|
| Finite pace | Per host quantum (~60 Hz), run `cycles ≈ target_hz / 60` (or a2m-equivalent overhead compensation). |
| Max | Free-run batch; no cycle budget (current free-run style). |
| Cycle Opt+T | Walk ladder; publish machine snapshot with active target label. |
| BP FAST/SLOW | Map to max / 1. |
| Paste | Confirm no turbo side effects. |

**Done when:** with Release build, finite `1` holds ~real-time; finite `4`/`8` approach targets when host allows; max free-runs (still may be beam-bound ~33 MHz class).

**Tests:** update `runtime_turbo` (and any set-turbo control tests) for new CSV + max.

---

### Phase 2 — Machine block paint (a2m full-frame matrix)

**Goal:** Worker can produce a full ARGB frame from RAM + softswitches **without** beam.

| Task | Detail |
|------|--------|
| API | e.g. `apple2_video_paint_full_frame(apple2_t *)` writing `m->video.fb`. |
| Modes | Port a2m matrix: lores, dlores, text40/80, hgr, dhgr, mixed combos, page2, mono if present. |
| Independence | No SDL/Nuklear; machine → util only. |
| Optional | Beam-path dlores parity while porting. |

**Done when:** unit or smoke can force block paint and get non-garbage HGR/text; visual parity with a2m on a few known screens (manual OK).

**Reference:** `../a2m/src/ui/nuklear/unk_apl2.c` (and headers).

---

### Phase 3 — Wire max → block paint; finite → beam

**Goal:** Policy A in the free-run / pace loops.

| Task | Detail |
|------|--------|
| Finite | Keep beam step + paint-at-beam (unchanged accuracy story). |
| Max | Disable beam pixel paint; A-lite counters; each ~16 ms wall (or host present quantum) call block paint + publish. |
| request-frame | Still works on max (block paint on demand). |
| Enter/leave | Correct flags; B re-seed only if A-lite abandoned for perf. |
| Frame ring | On max, push block-painted frames with real `machine_cycle` / frame numbers; every live paint; respect `frame_ring_memory_mb`. |
| Audio | Keep producing as today for free-run unless it blocks the perf gate (profile before gutting). |

**Done when:** max shows continuous live video; finite 1 still beam-accurate; leaving max does not permanently desync beam for normal software.

---

### Phase 4 — Product surface

**Goal:** UI, control wire, help, agent docs speak MHz + max.

| Task | Detail |
|------|--------|
| Configure / Misc | Labels “1 MHz”, “4 MHz”, “max” (not turbo 1/2/3). |
| Control | `set-turbo` accepts MHz number, `max`, `-1`; status reports same. |
| Help / manual | Short turbo-as-zip note; max = presentation paint. |
| Agents | Update [`runtime.md`](runtime.md), [`status.md`](status.md). |
| Remove | Warp/blank-screen copy in UI strings and control-tools gotchas. |

**Done when:** grep for product “warp” / “turbo 3” as user-facing turbo is clean (code comments about history OK).

---

### Phase 5 — Perf gate + polish

**Goal:** Prove the point of the epic.

| Gate (Release, reference host) | Bar |
|--------------------------------|-----|
| Finite `1` + beam | ≥ 1.0× real-time |
| Max + block paint (live path on) | **Aim > 100 MHz** emulated CPU; **floor ≥ 80 MHz** acceptable for first land |
| Max vs old beam free-run | Clearly ≫ ~33 MHz machine-only beam class |
| Regression | ctest gate green; no blank max |

| Task | Detail |
|------|--------|
| Measure | Extend or add bench (machine block paint free-run vs beam; product max if practical). |
| Profile | If &lt; 80 MHz: drop A-lite → B; trim per-cycle audio/BP fat; ensure paint is wall-paced only. |
| Document | Record measured numbers in this file under **Results**. |

**Done when:** floors met or explicit residual with profile note + follow-up row.

---

## Implementation map (likely files)

| Area | Paths |
|------|--------|
| Turbo config / parse | `src/runtime/runtime.h`, `runtime.c`, `app_options.*` |
| Free-run / pace / paste | `src/runtime/runtime_thread.c` |
| Client API | `runtime_client.*`, `runtime_command.h`, `runtime_event.h` |
| Block paint | `src/machine/video.c` / new `video_block*.c`, `video.h`, `apple2.h` |
| Frame publish / ring | `runtime_thread.c`, `runtime_frame_ring.*` |
| UI labels | `src/frontend/*` Configure / status |
| Control | `src/control/*` |
| Tests | `tests/runtime/test_runtime_turbo.c`, control protocol tests |
| a2m reference | `../a2m/src/ui/nuklear/unk_apl2.c`, `../a2m/src/runtime/rt.c` |

---

## Agent execution notes

1. Read [`rules.md`](rules.md) — no live machine pointers in UI; no frontend includes from machine.  
2. Prefer **Release** (`-DCMAKE_BUILD_TYPE=Release`) for Phase 5; Debug is fine for Phase 0–4 logic.  
3. Do **not** reintroduce blank warp as a ladder rung.  
4. Do **not** silently switch finite targets to block paint if the host cannot sustain N MHz — under-run is honest; policy change is not.  
5. Keep ARGB **560×192** contract unless a separate epic changes it.  
6. When closing: update status, runtime turbo section; add **Results** below.

---

## Results

Measured with `./build-release/bench_realtime <sec> <beam|alite|block>`
(machine-only free-run; HGR white screen; Release, this host).

| Metric | Value | Host / build |
|--------|-------|----------------|
| Finite 1× | ≥ 1.0× (frame pacer; Debug/Release both hold) | product path |
| Beam free-run | ~22–25 MHz emulated | Release `bench_realtime 5 beam` |
| A-lite free-run (no beam paint) | ~29 MHz emulated | Release `bench_realtime 5 alite` |
| A-lite + ~60 Hz block paint | ~24–30 MHz class | Release `bench_realtime 5 block` |
| Notes | **Floor 80 MHz not met.** Paint is no longer the dominant cost on this host (alite only ~30% above beam). Product max uses A-lite counters + wall-paced block paint (live picture). Closing residual: machine free-run core is ~30 MHz class vs a2m’s 140+ MHz — needs a separate free-run/CPU path epic, not more paint policy. Policy B (skip beam entirely) not required for correctness; residual is core throughput. | |

---

## Phase checklist

| Phase | Goal | Status |
|-------|------|--------|
| 0 | Model + parse + kill warp vocab | **Done** |
| 1 | MHz pacing + max free-run + FAST/SLOW/paste | **Done** |
| 2 | Machine full-frame block paint (a2m matrix + dlores) | **Done** |
| 3 | Policy A wiring + ring/live 60 Hz max | **Done** |
| 4 | UI / control / docs surface | **Done** |
| 5 | Release perf gate + profile fallback B if needed | **Done** (see Results) |
