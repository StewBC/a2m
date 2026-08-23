# TimeMachine: forensic debugger backend (roadmap)

**Status:** Roadmap (planning). **Not implementation.**  
**Product name:** TimeMachine — runtime-owned forensic engine; Inspector is the UI mode that drives it.  
**Supersedes:** F7 Inspector scrub-spine ([`inspector.md`](inspector.md) I0–I5a) — **already retired and removed** in `b738cef`; lessons (join key, unified disasm) salvaged, code at tag `archive/f7-inspector`.  
**Depends on:** remote-debug C0–C5b (history + frame ring); sessions S0–S4; **machine snapshots as the checkpoint format** ([`snapshots.md`](snapshots.md)) — TM2 uses `apple2_snapshot_save`/`load` directly and bumps its version, so that closed epic's doc is edited in TM2, not merely read.  
**Unblocks:** One-skin live + forensic debugger; later Promote/Branch (“make this live”).

Related: [`rules.md`](rules.md) · [`sessions.md`](sessions.md) · [`inspector.md`](inspector.md) ·
[`remote-debug.md`](remote-debug.md) · [`snapshots.md`](snapshots.md) · [`breakpoints.md`](breakpoints.md) ·
[`status.md`](status.md).

Source is authoritative once phases land. If this brief and code disagree, fix the brief in the same change.

---

## Why

Today:

| Piece | Role | Problem |
|-------|------|---------|
| Live debugger (F9) | UI ↔ Machine | Correct for live work |
| History + frame ring | Flight recorder / paint samples | Data exists |
| Inspector (F7) | *(retired `b738cef`)* | Taught the lesson: forensic nav as a client-side FIND/READ tape walk is too slow |
| Forensic UI | **none today** | No forensic surface at all until TM4 lands the Misc tab |

Target:

```text
Live:      UI  ↔  Machine          (execute opcodes)
Forensic:  UI  ↔  TimeMachine      (query / walk the tape; materialize past into the one true machine state)
```

TimeMachine answers **queries** (step-out, run-to-PC, materialize-at-cycle), not “next row” RPC loops. Analogy: SQL engine vs client-side next-row.

---

## Key decisions (pinned — do not re-litigate in phase scopes)

| # | Topic | Decision |
|---|-------|----------|
| D1 | Product | **One debugger skin.** Inspector mode = forensic; leave mode = live. Same keys; mode-aware verbs. |
| D2 | Truth | **One true machine state.** In Inspector mode, TimeMachine **replaces** live `apple2_t` with reconstructed past (read-only). Views keep talking to RAM/CPU/softswitches; display/paint update as the tape head advances. |
| D3 | Cursor | Inspector active → forensic cursor (THEN). Exit Inspector → live NOW. No simultaneous NOW+THEN panels in V1. |
| D4 | Mutation | Forensic cursor is **read-only** (setters reject). Live is read-write. |
| D5 | Storage | **HST1** = instruction index / query accelerator. **Checkpoint ring + input log + sealed re-execution** = full state reconstruct. A checkpoint is `apple2_snapshot_save` into a ring slot (~160K + slots; already covers CPU/RAM/SOFT/VID/SLOT/DSKs/SPrt/MBrd). **No write-delta stream** — see D5a. Do not reconstruct banking/softswitches from HST1 alone. |
| D5a | Why not deltas | Rejected after measurement. A checkpoint is 160K; a 128MB budget holds ~800 of them. Capping cadence at ~20k cycles bounds replay to **&lt;1 ms** (beam free-run benches ~22–25 MHz). Re-execution reproduces beam paint, Disk II mechanics and VIA/AY microstate **for free**; a delta stream reproduces none of those and duplicates writes HST1 already records. Do not re-open without new measurements. |
| D6 | Recording | **Opt-in.** INI/CLI + Inspector UI toggle. Off = no TimeMachine burden (play Total Replay). On = checkpoints, input log, frames, HST1 as configured. |
| D7 | Threads | TimeMachine runs on the **runtime worker** first. No second thread in V1. |
| D8 | Ownership | Runtime-owned engine (`src/runtime/`), machine-*like* API. Not a peer Apple under `src/machine/`. UI still only via `runtime_client`. |
| D9 | Sessions / control | **Cooperative one state.** `get-memory` etc. see the one true state (past while scrubbed). Scrub is a shared mutation; `state-changed` informs peers. No exclusive lock / dual-world. |
| D10 | Media | **A guest media write that succeeds cuts the window.** (A refused write changes nothing and must not cut; for Disk II this is structural — the writable guard precedes the dirty flags.) Records/checkpoints/frames older than the write are dropped; recording continues forward. Rationale: media bytes live outside the checkpoint, so a replayed read would return present-day bytes. Cutting buys one clean invariant — *everything inside `tm_window` replays exactly* — instead of a flagged-but-unusable region. Disk II mechanical state (motor, quarter-track, latches) rides in the checkpoint as normal. Must key on **guest** writes, not housekeeping (eject flush, snapshot flush). |
| D11 | Audio / MB | VIA/AY chip state rides in the checkpoint (`MBrd`) and re-execution reproduces it, so scrubbed MB state is **correct, not deferred**. Host **audio output** is suppressed during replay (D16); no attempt at continuous scrub audio. Speaker may click on entry/exit. |
| D12 | Nav direction | Forward = keep executing under the seal / walk index. Backward = **rebuild** from earlier checkpoint + re-execute to target — not reverse-CPU. |
| D13 | Promote/Branch | Later phase: **bump the history `timeline`** at the focus (the mechanism already exists), keep materialized state as live NOW, exit read-only. Branch, not hard truncate. Not required for V1 ship of Inspector-on-TimeMachine. |
| D14 | Migration | **Done — F7 is already retired and removed** (`b738cef`; code at tag `archive/f7-inspector`). No `frontend_inspector_*` remains in `src/`. Salvaged: shared disasm chrome, join-key and frame-ring accessor lessons. No phase owns F7 removal. |
| D15 | Entry UX | Misc → **Inspector** tab: while running, primarily **Pause/Stop**; when paused + recording available, scrubber + forensic mode. This tab is **net-new UI** (nothing to retire — D14). |
| D16 | Sealed replay | Re-execution must run **sealed**: CPU observer off, memory-access callback off, no frame push / publish, no `runtime_produce_audio`. Media write suppression stays as a **safety net** — D10 means the window never spans a write, so replay cannot re-execute one. A leaky seal corrupts the tape being stood on. One ctest per gate. |
| D17 | TM window | HST1, frame ring and checkpoints age out independently. TimeMachine exposes **one `tm_window`** = their intersection (oldest/newest cycle). **Single interval, never a set of islands** — anything that breaks replay continuity (media change D10, recorder stop/resume under max) moves `oldest` to the marker rather than leaving a gap. TM1 clamps seeks to it; the TM4 scrubber renders it; materialize outside it is an honest error, never a partial apply. |
| D18 | Control honesty | Forensic mode is a **global** read-only state, so socket peers must be able to see it and leave it. Existing status verb reports `mode` + focus cycle; an exit verb exists; `state-changed` gains forensic reasons. This **is** wire-visible — expect an **A2M bump** in TM3, not "client-only". |

---

## Shape (target)

```text
                    ┌─────────────────────────────────────┐
  UI (debugger) ───►│ runtime_client                      │
                    └─────────────────┬───────────────────┘
                                      │ commands / events
                                      ▼
                    ┌─────────────────────────────────────┐
                    │ runtime worker                      │
                    │  ┌─────────────┐  ┌──────────────┐  │
                    │  │ Machine     │◄─│ TimeMachine  │  │
                    │  │ apple2_t    │─►│ query+material│  │
                    │  └──────┬──────┘  └──────▲───────┘  │
                    │         │ record when on        │   │
                    │         ▼                       │   │
                    │  checkpoints + input log + HST1 + frames
                    └─────────────────────────────────────┘
```

**Record path (opt-in, free-run):** cycle-capped checkpoint (`apple2_snapshot_save` → ring slot)
+ timestamped input/nondeterminism log + HST1 + frame grabs. **No write-delta stream** (D5a).

**Forensic path (paused, Inspector on):** query moves tape head → materialize = load nearest
checkpoint ≤ target, then **re-execute sealed** (D16) to the target cycle → publish state →
UI redraws normally (read-only). Backward jump = same, from an earlier checkpoint (D12).

---

## Non-goals (V1 product bar)

- Scrubbing back across a media write — the window is cut there instead (D10)  
- Rewinding host file content (disk images, HostFS)  
- Perfect mid-instruction CPU microstate (focus is always an instruction boundary)  
- Reverse-execution of the 6502 (backward = rebuild + re-execute, D12)  
- A write-delta stream (D5a)  
- Dual isolated UI vs agent worlds  
- Always-on recording by default  
- Audible audio while scrubbing (MB **state** is correct; output is muted, D11/D16)  
- Bit-exact replay across a host-file change under the tape  

---

## Campaign phases

Implement in order unless a phase explicitly says it can parallelize.
Each phase ends with: **build + ctest green**, **Landed** in that phase file, and a
one-line pointer update here if useful.

| Phase | Doc | V1? | Summary |
|-------|-----|-----|---------|
| **TM0** | [`TM0.md`](TM0.md) | Yes | Epic contract + opt-in config (**Landed**) |
| **TM1** | [`TM1.md`](TM1.md) | Yes | Fast HST1 tape queries (step/over/out/run-to) (**Landed**) |
| **TM2** | [`TM2.md`](TM2.md) | Yes | Checkpoint ring + input log + sealed replay; test materialize (**Landed**) |
| **TM3** | [`TM3.md`](TM3.md) | Yes | Materialize into one true `apple2_t`; enter/exit NOW; control honesty (**Landed**, A2M/12) |
| **TM4** | [`TM4.md`](TM4.md) | Yes | Misc Inspector tab; one skin (F7 already gone — D14) (**Landed**) |
| **TM5** | [`TM5.md`](TM5.md) | V1.1 | Forensic BP/watch store (**Landed**) |
| **TM6** | [`TM6.md`](TM6.md) | V1.1 | Promote / Branch |

Phase docs are the implementer briefs (goal, non-goals, anchors, acceptance, script).
Do not re-expand full scope here — edit the phase file.

### Addendum (do not rewrite the table above)

TM4 Inspector **behaviour** is superseded by **[`TMA0.md`](TMA0.md)** (film / land / re-execute). TM4 stays Landed as history. HST1 (TM1) stays in the product; it is not the Inspector slider. Implementer briefs: **[`TMA1.md`](TMA1.md)** (Inspector), **[`TMA2.md`](TMA2.md)** (delete TM1 tape-nav — required).

---

## Parallelism / ordering

```text
TM0 ──► TM1 ──► TM2 ──► TM3 ──► TM4 ──► TM5
                      └──────────────► TM6 (after TM3 trust; can trail TM4/TM5)
```

- **TM1** can ship UX value on HST1 alone while TM2 is built.  
- **TM2** no longer needs a format spike — checkpoint = `apple2_snapshot_save` (D5). The design
  work that remains is the **seal** (D16) and the **input/nondeterminism log**.  
- **TM4** needs TM3 for the “views just work” story; thin scrubber on TM1-only is optional interim, not the product bar.  
- **TM5** / **TM6** are power features after the one-skin loop works.

---

## Testing strategy (per phase)

| Phase | Prefer |
|-------|--------|
| TM1 | ctest: step-out / run-to on recorded fixture history; window clamp + epoch reject |
| TM2 | ctest: checkpoint round-trip; **sealed** re-execution equals golden mem/CPU/beam at cycle; one test per seal gate (D16); media-write truncation + housekeeping-does-not |
| TM3 | ctest: seek restore; exit restores NOW; reject poke in forensic; mode visible on control |
| TM4 | manual playbook + key doc, **including TM-on + max turbo**; no flaky UI automation required |
| TM5 | ctest: forensic BP hit on seek/run-tape |
| TM6 | ctest: promote bumps timeline; old future unreachable; live poke works after |

Keep [`testing.md`](testing.md) gate green every phase.

---

## Risks (acknowledge early)

| Risk | Mitigation |
|------|------------|
| Checkpoint size vs window depth | 160K per checkpoint; cycle-capped cadence under a `timemachine_memory_mb` budget. ~128MB ≈ 800 checkpoints. Measure and pin in TM2 Landed |
| Materialize latency on scrub | Bounded by cadence cap: ~20k cycles ≈ **&lt;1 ms** at beam-free-run speed. Keep the restored checkpoint hot and continue executing for forward steps; rebuild on backward jumps |
| **Leaky seal corrupts the tape** | D16 is the top correctness risk: replay re-entering the observer, watchpoints, frame push, audio or **host media writes**. One ctest per gate; assert recorder counters unchanged across a materialize |
| **Replay divergence** | Enumerate nondeterminism up front: `rand()` in `diskii.c` / `image.c`, `clock_gettime` in `hostfs.c`, host key/paddle/paste input. Seed + checkpoint the PRNG; log inputs with cycles. Test: materialize twice → identical |
| Window chopped by media writes | Expected consequence of D10, not a bug. Copy-protection write-checks and mid-play saves will cut the window during exactly the sessions users want to scrub. Surface the `MEDIA_CHANGED` marker so it reads as a stated rule, never as data loss |
| Max free-run discards the tape | **Pinned:** follow existing `history_off_on_max` (default true) — recording stops in max. Because re-execution cannot replay across a gap, resume moves `tm_window.oldest` to the `RECORDER_RESUME` marker: one Opt+T throws the tape away. Surface it on the turbo cycle, not only in the Inspector tab |
| Recording cost when on | Opt-in (D6); profile insn path; V1 assumes full TM when on |
| One-state coop confusion | `state-changed` + status “forensic @ cycle” + control-visible mode and exit verb (D18); document turn-taking (D9) |

---

## Acceptance bar (epic “TimeMachine V1”)

Epic V1 = **TM0–TM4** closed (accepted 2026-08-22 with a recording-speed caveat):

- [x] Opt-in recording; off path stays cheap  
- [x] Fast tape queries (no UI FIND loops for step-over/out/run-to), clamped to `tm_window`  
- [x] Checkpoint + sealed re-execution reconstructs CPU/mem/softswitches/beam/Disk II/VIA in window  
- [x] Seal proven: materialize leaves HST1, frame ring, watchpoints, audio and host files untouched  
- [x] Inspector mode replaces machine with past; views/display update under the head  
- [x] One debugger skin; Misc Inspector tab is the only forensic entry  
- [x] Read-only past; leave restores live NOW  
- [x] Guest media write cuts the window, with marker + honest UI text; housekeeping writes do not  
- [x] Cooperative one-state + `state-changed` + control-visible mode/exit (D18)  

**V1 caveat (blocks “in the money”):** TM-on live recording must hold **1 MHz** (product play, not just `bench_realtime` without observers). HST1 `get_status` must stay O(blocks). Do not open TM5/TM6 until that bar is met.

**TM5 / TM6** = V1.1 power features (tracked here, not required for V1 checkbox).

---

## Open items for phase scoping (not blocking this roadmap)

| Item | Default if unspecified when a phase opens |
|------|-------------------------------------------|
| Exact checkpoint cadence | **Cycle-capped** (start ~20k cycles ≈ one frame at 1 MHz), plus forced checkpoint on recorder start and Inspector enter. Never frame-capped alone — max free-run breaks that. Tune from metrics |
| Checkpoint encoding | **`apple2_snapshot_save` into a ring slot.** Do not hand-roll a second serializer for V1 |
| Input / nondeterminism log format | Versioned append-only ring, timestamped by `machine_cycle`; detail in TM2 scope |
| Live NOW anchor representation | **Full `apple2_snapshot`** at Inspector enter — identical to a checkpoint (D5), so exit restores Disk II and MB too |
| A2M verbs for TimeMachine | **Required in TM3**, not optional (D18): mode + focus on status, exit verb, forensic `state-changed` reasons. Expect a protocol bump |
| Whether recording-on implies frame ring + HST1 always | **Yes** for V1 when TM enabled |
| TM behaviour under max free-run | **Closed:** recording stops in max (`history_off_on_max`); resume truncates the window to the `RECORDER_RESUME` marker. Measured cost numbers still go in TM2 Landed |

---

## Agent script (when implementation starts)

```text
1. Read agents/rules.md, agents/timemachine.md (this file), then agents/TMn.md
   or agents/TMAn.md for the phase named in the human brief (only one unless told
   to continue). Live Inspector UX is TMA0, not TM4.
2. Also read deps listed in that phase doc (sessions/snapshots/remote-debug as cited).
3. Implement that phase → Landed in TMn.md + one-line status here if useful.
4. Stop. Do not start TM5/TM6 until TM0–TM4 V1 bar is accepted.
5. F7 is already gone (D14) — there is nothing to retire or salvage. If a phase doc
   sends you to `frontend_inspector_*`, that anchor is stale; fix the doc.
```
