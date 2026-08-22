# TimeMachine: forensic debugger backend (roadmap)

**Status:** Roadmap (planning). **Not implementation.**  
**Product name:** TimeMachine — runtime-owned forensic engine; Inspector is the UI mode that drives it.  
**Supersedes:** F7 Inspector scrub-spine as forever architecture ([`inspector.md`](inspector.md) I0–I5a). That work taught join keys and unified disasm; it is disposable scaffolding if this lands.  
**Depends on:** remote-debug C0–C5b (history + frame ring); sessions S0–S4; machine snapshots shape ([`snapshots.md`](snapshots.md)).  
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
| Inspector (F7) | UI → session → FIND/READ → UI | Forensic nav is a long client-side tape walk — too slow |
| Dual shells | F7 vs F9 | Two UIs for one skill |

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
| D2 | Truth | **One true machine state.** In Inspector mode, TimeMachine **replaces** live `apple2_t` with reconstructed past (read-only). Views keep talking to RAM/CPU/softswitches; display/paint update as the tape head advances and deltas apply. |
| D3 | Cursor | Inspector active → forensic cursor (THEN). Exit Inspector → live NOW. No simultaneous NOW+THEN panels in V1. |
| D4 | Mutation | Forensic cursor is **read-only** (setters reject). Live is read-write. |
| D5 | Storage | **HST1** = instruction index / query accelerator. **Checkpoints + deltas** = full state reconstruct. Do not reconstruct banking/softswitches from HST1 alone. |
| D6 | Recording | **Opt-in.** INI/CLI + Inspector UI toggle. Off = no TimeMachine burden (play Total Replay). On = checkpoints, deltas, frames, HST1 as configured. |
| D7 | Threads | TimeMachine runs on the **runtime worker** first. No second thread in V1. |
| D8 | Ownership | Runtime-owned engine (`src/runtime/`), machine-*like* API. Not a peer Apple under `src/machine/`. UI still only via `runtime_client`. |
| D9 | Sessions / control | **Cooperative one state.** `get-memory` etc. see the one true state (past while scrubbed). Scrub is a shared mutation; `state-changed` informs peers. No exclusive lock / dual-world. |
| D10 | Media | **Disk II / HostFS / file side effects not undone.** RAM that received file data is covered via normal mem writes. Document honestly. |
| D11 | Audio / MB | **Not V1-critical.** Softswitches change; speaker may click. Mockingboard/VIA continuous audio accuracy deferred (chip microstate + timed replay later). |
| D12 | Nav direction | Forward = apply deltas / walk index. Backward = **rebuild** from earlier checkpoint + replay to target — not reverse-CPU. |
| D13 | Promote/Branch | Later phase: truncate future tape, keep materialized state as live NOW, exit read-only. Not required for V1 ship of Inspector-on-TimeMachine. |
| D14 | Migration | Unpushed F7 Inspector stack (`19b2745` forward) may be **thrown away** if this replaces it. No duty to preserve F7 chrome. |
| D15 | Entry UX | Misc → **Inspector** tab: while running, primarily **Pause/Stop**; when paused + recording available, scrubber + forensic mode. Retire separate F7 shell (or thin alias into same mode). |

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
                    │  │ apple2_t    │  │ query+material│  │
                    │  └──────┬──────┘  └──────▲───────┘  │
                    │         │ record when on        │   │
                    │         ▼                       │   │
                    │  checkpoints + deltas + HST1 + frames│
                    └─────────────────────────────────────┘
```

**Record path (opt-in, free-run):** periodic checkpoint → per-insn / bus deltas + HST1 + frame grabs.  
**Forensic path (paused, Inspector on):** query moves tape head → materialize into `apple2_t` → publish state → UI redraws normally (read-only).

---

## Non-goals (V1 product bar)

- Disk / HostFS undo  
- Perfect mid-instruction CPU microstate  
- Reverse-execution of the 6502  
- Dual isolated UI vs agent worlds  
- Always-on recording by default  
- Bit-perfect Mockingboard scrub audio  
- Keeping F7 as a permanent second app  

---

## Campaign phases

Implement in order unless a phase explicitly says it can parallelize.
Each phase ends with: **build + ctest green**, **Landed** in that phase file, and a
one-line pointer update here if useful.

| Phase | Doc | V1? | Summary |
|-------|-----|-----|---------|
| **TM0** | [`TM0.md`](TM0.md) | Yes | Epic contract + opt-in config |
| **TM1** | [`TM1.md`](TM1.md) | Yes | Fast HST1 tape queries (step/over/out/run-to) |
| **TM2** | [`TM2.md`](TM2.md) | Yes | Checkpoint + delta recorder + test materialize |
| **TM3** | [`TM3.md`](TM3.md) | Yes | Materialize into one true `apple2_t`; enter/exit NOW |
| **TM4** | [`TM4.md`](TM4.md) | Yes | Misc Inspector tab; one skin; retire F7 |
| **TM5** | [`TM5.md`](TM5.md) | V1.1 | Forensic BP/watch store |
| **TM6** | [`TM6.md`](TM6.md) | V1.1 | Promote / Branch |

Phase docs are the implementer briefs (goal, non-goals, anchors, acceptance, script).
Do not re-expand full scope here — edit the phase file.

---

## Parallelism / ordering

```text
TM0 ──► TM1 ──► TM2 ──► TM3 ──► TM4 ──► TM5
                      └──────────────► TM6 (after TM3 trust; can trail TM4/TM5)
```

- **TM1** can ship UX value on HST1 alone while TM2 is designed.  
- **TM2** can start once checkpoint/delta grammar is agreed (short design spike inside TM2 scope).  
- **TM4** needs TM3 for the “views just work” story; thin scrubber on TM1-only is optional interim, not the product bar.  
- **TM5** / **TM6** are power features after the one-skin loop works.

---

## Testing strategy (per phase)

| Phase | Prefer |
|-------|--------|
| TM1 | ctest: step-out / run-to on recorded fixture history |
| TM2 | ctest: checkpoint round-trip; delta replay equals golden mem/CPU at cycle |
| TM3 | ctest: seek restore; exit restores NOW; reject poke in forensic |
| TM4 | manual playbook + key doc; no flaky UI automation required |
| TM5 | ctest: forensic BP hit on seek/run-tape |
| TM6 | ctest: promote truncates; live poke works after |

Keep [`testing.md`](testing.md) gate green every phase.

---

## Risks (acknowledge early)

| Risk | Mitigation |
|------|------------|
| Checkpoint size vs window depth | Sparse checkpoints + compact deltas; budget MB; measure before every-frame full RAM |
| Materialize latency on scrub | Cache last checkpoint; incremental apply when scrubbing forward; rebuild on large backward jumps |
| Recording cost when on | Opt-in (D6); profile insn path; allow HST1-only lite mode only if product agrees later — V1 assumes full TM when on |
| One-state coop confusion | `state-changed` + status “forensic @ cycle”; document turn-taking (D9) |
| Discarding F7 work | Intentional (D14); salvage only accessor ideas / join-key lessons |

---

## Acceptance bar (epic “TimeMachine V1”)

Epic V1 = **TM0–TM4** closed:

- [ ] Opt-in recording; off path stays cheap  
- [ ] Fast tape queries (no UI FIND loops for step-over/out/run-to)  
- [ ] Checkpoints + deltas reconstruct CPU/mem/softswitches in window  
- [ ] Inspector mode replaces machine with past; views/display update under the head  
- [ ] One debugger skin; Misc Inspector tab; F7 dual-app gone  
- [ ] Read-only past; leave restores live NOW  
- [ ] Disk/HostFS non-coverage documented  
- [ ] Cooperative one-state + `state-changed`  

**TM5 / TM6** = V1.1 power features (tracked here, not required for V1 checkbox).

---

## Open items for phase scoping (not blocking this roadmap)

| Item | Default if unspecified when a phase opens |
|------|-------------------------------------------|
| Exact checkpoint cadence | Start: every retained frame boundary + wall/cycle cap; tune from metrics |
| Delta encoding format | Append-only ring; versioned; detail in TM2 scope |
| Live NOW anchor representation | Full checkpoint at Inspector enter (simplest correct) |
| A2M verbs for TimeMachine | Add when agents need parity; UI can use `runtime_client` first |
| Whether recording-on implies frame ring + HST1 always | **Yes** for V1 when TM enabled |

---

## Agent script (when implementation starts)

```text
1. Read agents/rules.md, agents/timemachine.md (this file), then agents/TMn.md
   for the phase named in the human brief (only one phase unless told to continue).
2. Also read deps listed in that phase doc (sessions/snapshots/remote-debug as cited).
3. Implement that phase → Landed in TMn.md + one-line status here if useful.
4. Stop. Do not start TM5/TM6 until TM0–TM4 V1 bar is accepted.
5. Do not preserve F7 chrome for its own sake (D14).
```
