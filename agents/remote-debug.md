# Remote debug: control port, frame ring, CPU history

**Active epic:** product remote observability (H2 + H6 + T3 + BP P5).  
**Architecture north star:** same *capability level* as c64m remote debug —
scriptable control, rolling screen log, always-on CPU flight recorder — with
**Apple II** memory/views, beam timing, Disk II / SmartPort, and **560×192**
frames. Not C64 metaphors.

Related: [`status.md`](status.md) ·
[`runtime.md`](runtime.md) · [`breakpoints.md`](breakpoints.md) ·
[`rules.md`](rules.md).

c64m gold (sibling tree `../c64m/agents/`):

| Doc | Role |
|-----|------|
| `control-port.md` | Wire protocol, deferred RPC, BP/history/frame-ring commands |
| `cpu-flight-recorder.md` | HST1 recorder contract, lifecycle, query API |
| `runtime-control.md` | Runtime thread ownership / tokens |
| `frame-ring-plan.md` | Rolling framebuffer black box |

Source in this tree is authoritative once phases land. If a handoff and source
disagree, fix the handoff in the same change.

---

## Why this epic

UI breakpoints (H7 0–4) answer *forward* questions: “stop when X happens next.”
They cannot answer:

1. **What did the screen show three frames ago?** (human pauses late)  
2. **What executed / touched memory before it went wrong?** (retrospective)  
3. **Can a script drive the machine the way the UI does?** (goldens, automation)

c64m solved these with three complementary mechanisms plus control-port BP RPC.
This epic ports that *stack* to a2m.

| Mechanism | Answers | c64m module | a2m target |
|-----------|---------|-------------|---------------|
| **Control port** | Scripted run/pause/mem/frame/BP | `src/control` + main | Product wire of `control` → **A2M/N** |
| **Frame ring** | Past completed screens | `runtime_frame_ring` | Product depth + wire (T3) |
| **CPU flight recorder** | Instruction + bus timeline | `runtime_history` + HST1 | Link real history; machine observer (H6) |
| **BP wire RPC** | Same engine as Misc tab | `break-create` / `when=` | Phase 5 of [`breakpoints.md`](breakpoints.md) |

Do **not** collapse these three. R/W breakpoints use a live access callback;
`write_history[addr]` (last writer PC) annotates the mem view; the flight
recorder is a bounded multi-million-record ring. Same separation as c64m.

---

## Decision (product / architecture)

| Question | Answer |
|----------|--------|
| Work first: control core or history hot path? | **Control product wire first** (H2 + P5). Unblocks goldens and proves RPC/token paths. |
| Replace a2m page maps with c64m bus graph? | **No.** Keep softswitch + page maps; observer hooks hang off existing bus choke points. |
| Protocol identity | **`A2M/N`**. Bump `N` when wire behaviour changes; no dual-path compatibility. |
| Current parked protocol | `src/control` advertises **A2M/1** (minimal). Product main is not wired. |
| Product `src/control` | Still c64-shaped; prefer **`control` → product** over resurrecting C64 wire as product. |
| History UI browser | **Follow-up.** Deliver recorder core + remote API first (c64m order). |
| Time travel / reverse exec | **Non-goal.** |
| VIC ring analogue | **Not first delivery.** Optional later: softswitch / beam line log if tools need it. |
| TRON (P4b) | **After** shared insn-complete record from the flight recorder — do not dual-path. |
| Thread rules | Unchanged: control/UI only via `runtime_client`; worker owns `apple2_t`. |

### Apple adaptations (not copy-paste)

| c64m surface | Apple product meaning |
|--------------|------------------------|
| Memory modes `map/ram/rom/drive8/9` | **map / main / aux / lc1 / lc2 / rom** (and disk views only if product needs them) |
| `raster` / `cycle_in_line` | Beam **line** / **cycle_in_line** (`vic_cycle` still accepted as a parse alias) |
| `get-vic` / `get-cia` / drive-cpu | Softswitch / video / Disk II / SmartPort / MB snapshots — Apple hardware, not VIC/CIA |
| Frame payload | **560×192 ARGB** (and any future indexed form only if product adopts it) |
| Frame ring warp rule | Turbo 3: do **not** store geometric fakes as real frames; stall ring until live paint |
| Markers (PRG/CRT/KERNAL LOAD) | Reset, state load, assemble, direct poke, media mount/swap, program inject — Apple events |
| Access kinds | Reuse `cpu65_bus_access_kind` (same 6502 taxonomy as c6510) |
| HST1 binary | Keep **HST1** layout where possible so tools share decoders; document any Apple marker ID extensions |

---

## Current tree facts (as of epic start)

| Piece | Reality |
|-------|---------|
| BP UI/runtime engine | **Done** through P4e (TRON deferred) — see [`breakpoints.md`](breakpoints.md) |
| `apple2_set_memory_access_callback` | **Done** (R/W watchpoints) |
| `src/control` | Parked **A2M/1**: hello/version, run/pause/step, get-cpu/memory, crude break-exec/list/clear, wait-paused, key, mount-disk |
| Product main | **No** `--control-port` / headless control dispatch |
| `runtime_history.c` / wire | **Present** from wholesale port; **not linked** — CMake uses stub (“History deferred”) |
| `runtime_frame_ring.c` | **Linked**; product depth / control wire / coop residual open (T3) |
| Options | `history_memory_mb`, `frame_ring_memory_mb`, CLI `--history-memory` already exist |
| Tests deferred | `control_protocol`, `runtime_frame_ring` (and history tests when re-enabled) |

---

## Transfer table (c64m → a2m)

| Piece | Transfers? | Notes |
|-------|------------|--------|
| Line protocol + binary `data` framing | **Yes** | id · ok/error/data · payload + trailing `\n` |
| Deferred table + request tokens | **Yes** | Multi-outstanding bulk mem/cpu; exclusive history/waits |
| Sticky wait latches | **Yes** | Completions + execution-state; clear on new exec control |
| Headless + control-port | **Yes** | Localhost; quit-client ≠ process exit |
| `break-create` / `when=` / actions | **Yes — adapt mapping** | Engine already product; wire is P5 |
| `history-*` + HST1 | **Yes — link + observe** | Stub → real module; Apple markers |
| Frame ring commands | **Yes — Apple frame shape** | Immediate answers; ring mutex; pause for stable scrub |
| VIC ring | **No** (v1) | Not product center |
| `get-debug-memory` + write_history | **Partial** | Debug memory path exists; write_history pack still missing |
| Assembler / symbols RPC | **Later** | Prefer offline assemble until tools re-graft |
| Drive power / D64 mount | **Replace** | Disk II / SmartPort / multi-image queue |

---

## Phases

### Phase C0 — Product control skeleton (H2 start)

Wire control into product main without inventing a second ownership model.

- Prefer **`src/control`** as product control (grow A2M/1 → A2M/2+)  
- Main-loop dispatch: parse → `runtime_client` → deferred completion  
- `--control-port` / `--headless` paths match c64m operational rules  
- `hello` / `version` / `capabilities` / `ping` / `quit-client`  
- Execution: `reset`, `run`, `pause`, step family (as runtime already supports)  
- `get-state`, `get-cpu`, `get-memory` / `set-memory` (Apple modes)  
- `get-frame` (ARGB 560×192; document warp debug behaviour)  
- Waits: `wait-paused`, `wait-running`, `wait-frame`, `wait-event` (sticky set)  
- Restore / add **`control_protocol` ctest** for parser + response examples  

**Exit:** headless script can reset, run, pause, read CPU/memory/frame.  
**Landed:** product links `src/control` as A2M/2; main wires `--control-port`
(windowed + headless long-lived); `control_protocol` ctest green; smoke verified
hello/get-state/get-cpu/get-memory/get-frame/set-turbo.

### Phase C1 — Breakpoint wire RPC (BP P5)

Expose the existing runtime BP table over A2M.

- `break-create` / `break-update` / `break-clear` / `break-enable` / `break-list`  
- `rearm-oneshots`, `break-clear-all`  
- Access tokens, ranges, counter, composite mapping (`ram=`, `c100=`, `d000=`), `when=`, actions
- Snapshot text records match engine fields (hits, cond, when)  
- Reject exec + `value` condition at parse (same as UI path)  

**Exit:** control-armed exec and write BPs fire like Misc tab; ctest covers create/list/hit.  
**Landed:** `break-create` / `break-update` / `break-enable` / `break-clear[-all]` /
`rearm-oneshots` / `break-list` (`get-breakpoints`); `when=` via
`runtime_bp_condition_parse`; data `breakpoints` payloads; empty-list framing fixed.

### Phase C2 — Frame ring product depth (T3)

Make the screen log real for humans and scripts.

- Ensure completed frames push to ring on free-run (worker path)  
- Config: `frame_ring_memory_mb` (0 = off); capacity/dropped reported honestly  
- Control: `frame-ring-info`, `frame-ring-record on|off`, `frame-ring-clear`,  
  `get-frame-at frame=<n>|cycle=<n>`  
- Lookup: nearest frame **at or before** target; pre-window → `not-found`  
- Warp (turbo 3): **do not** record fake live frames  
- State load: clear ring (discarded timeline)  
- Payload byte-identical to `get-frame` in the same format  

**Exit:** free-run N frames, pause, retrieve an earlier frame by index/cycle; ctest green.  
**Landed:** ARGB `runtime_ring_frame` storage; push on live frame publish (not warp);
options → config budget (default 128 MiB ≈ 312 frames); control
`frame-ring-info/record/clear` + `get-frame-at frame=|cycle=`; unit test
`runtime_frame_ring`.

### Phase C3 — Flight recorder core (H6)

Always-on bounded CPU forensic ring.

- Link **real** `runtime_history.c` / `runtime_history_wire.c` (drop stub)  
- Machine observer: instruction/IRQ/NMI begin, bus access, complete  
- Fold with R/W BP callback (one access path when either needs events)  
- Lifecycle: start/stop/clear, reset → new **timeline**, state load → new **epoch**  
- Markers (minimum): recorder start/stop/resume, reset, state-load, direct poke,  
  assemble, media inject/swap as applicable  
- Config already present: `history_memory_mb` / `--history-memory`  
- Performance gate (c64m bar): target ≤5% turbo-2 loss, hard ceiling 10%  
- Recording hot path: no alloc, no lock, no disassembly, no events by value  

**Exit:** free-run with history on retains millions of records at budget; unit tests
for order + pre-insn regs + partial mid-instruction pause.  
**Landed:** `runtime_history.c` linked (stub dropped); `apple2_set_cpu_observer`
(begin/access/complete); runtime installs observer when arena available+recording;
options `history_memory_mb` → config; `runtime_history_basic` ctest (~156k records
in 50 ms free-run). Markers: initial reset. Full partial/seal polish can refine
in C4/C5.

### Phase C4 — History remote API (sliced)

| Slice | Scope | State |
|-------|--------|--------|
| **C4a** | Worker `HISTORY_INFO` / `RECORD` / `CLEAR` + status events | **Done** |
| **C4b** | Worker `FIND` / `NEXT` / `READ` / `CLOSE` + HST1 payload pool | **Done** |
| **C4c** | Control wire A2M/5: `history-*` parse/dispatch | **Done** |
| **C4d** | Markers/lifecycle polish + docs | **Done** (residual markers optional) |

- Paused-only find/next/read (`busy machine-running` otherwise)  
- Token-keyed binary **HST1** pages; never put arena bytes in `runtime_event`  
- Filters: pc, address, access, value, opcodes, cycle, timeline, epoch, direction  
- One cursor; stale on mutation/execution/clear  
- Advertise `history` in `capabilities`; bump **A2M/N** on C4c  

**Exit:** control client finds last write to an address and reads context; ctest.  
Python helper / coop_watch: **landed** — [`control-tools.md`](control-tools.md)
(`tools/a2m_control_client.py`, `tools/a2m_coop_watch.py`).

### Phase C5 — Polish (scoped)

| Slice | Item | In this campaign? | Notes |
|-------|------|-------------------|--------|
| **C5a** | `write_history[addr]` | **Done** | Last writer PC pack on bus write; debug_memory fill |
| **C5b** | TRON / TROFF (P4b) | **Done** | File log on insn complete while enabled |
| C5c | Richer hardware snapshots | **Parked** | Softswitch / Disk II / SP dumps later |
| C5d | History UI browser | **Parked** | Remote API first; UI follow-up |
| C5e | Softswitch / beam line ring | **Parked** | Only if frame+history prove insufficient |

### Acceptance bar (support system, not a sample game)

The goal is the **same support surface** that lets agents run a `make coop`-style
loop against a program under debug — not porting railgunner itself. Reference
orchestration: `../c64m/tools/coop_watch.py` + `c64_control_client.py` (see
`../railgunner/Makefile` `coop` only as an existence proof of the workflow).

Minimum capabilities the wire must eventually expose for that class of tool:

| Coop need | Wire surface |
|-----------|--------------|
| Windowed play + remote | `--control-port` (windowed or headless) |
| Freeze on human pause / BP | `wait-paused`, sticky stop reason |
| Snap CPU + memory regions | `get-cpu`, `get-memory` |
| Arm / count / clear BPs | `break-create` (+ `when=`), `break-clear-all` |
| Recent writes / path | `history-find` / `history-read` (HST1) |
| Late pause recovered pixels | frame ring `get-frame-at` / scrub |
| Inbox steer while frozen | control remains responsive while paused |

Apple substitutions: no VIC ring required for v1; frame is 560×192; mapping
labels are Apple planes. Python client + coop_watch: **done** — see
[`control-tools.md`](control-tools.md) and `make coop` / `tools/a2m_coop_watch.py`.
This epic is the **host wire**; that epic is the agent ergonomics layer.

### Perf policy

c64m’s ≤5% / 10% history cost is **not a hard fail yet** (no optimization
campaign landed). Prefer the cheaper correct path when choosing designs; do not
deliberately tax the hot path. Aim to keep free-run as close as practical to
legacy a2m-class throughput on the reference host.

### Dependency

```text
C0 product control skeleton
 └─► C1 BP wire (P5)
       ├─► C2 frame ring product + wire (T3)   (order flexible after C0)
       └─► C3 history core + observer (H6)
             └─► C4 history remote API
                   └─► C5a write_history · C5b TRON
```

C2 and C3 may proceed in either order (or interleaved) after C0; both need
stable deferred/token infrastructure from C0. Commit after each phase (or
sensible sub-slices).

---

## Protocol versioning (planned)

| Version | Contents (cumulative) |
|---------|------------------------|
| **A2M/1** | Pre-product minimal control |
| **A2M/2** | Product wire: execution, state, memory modes, frame, waits, turbo, step family |
| **A2M/3** | Full breakpoint RPC (P5): create/update/list/enable/clear/rearm + when= |
| **A2M/4** | Frame ring commands + ARGB product ring |
| **A2M/5** | History / HST1 commands |
| **A2M/6** | `get-softswitches` (latched flags + beam; not `$C0xx` mem) |
| **A2M/7** | `select-disk` (absolute queue) + `set-disk-writable` (notch) |
| **A2M/8** | Disk II `mount-disk` / `select-disk` / `set-disk-writable` resolve installed slot (prefer 6); explicit `slot drive` forms |
| **A2M/9** | **Current.** Unified `mount` / `unmount` with `kind=diskii\|smartport` (path infer; slot resolve); `mount-disk` kept as Disk II alias |

Bump only when scripts must learn new behaviour; update this table and
`CONTROL_PROTOCOL_VERSION` in the same change.

---

## Wire command map (target)

Introspection / execution / turbo — as c64m, minus VIC-only step targets unless
Apple beam run-to-line is added later.

```text
hello  version  capabilities  ping  quit-client
get-state  reset  run  pause
step-cycle  step-instruction  step-over  step-out  step-frame
run-cycles  run-instructions  run-to  set-turbo
get-cpu  get-softswitches  get-memory  set-memory  get-frame  get-debug-memory
get-call-stack
```

Breakpoints:

```text
break-exec  break-create  break-update  break-clear  break-enable
break-list  get-breakpoints  break-clear-all  rearm-oneshots
```

Frame ring:

```text
frame-ring-info  frame-ring-record <on|off>  frame-ring-clear
get-frame-at frame=<n>|cycle=<n>
```

History:

```text
history-info  history-record <on|off>  history-clear
history-find [key=value ...]  history-next <cursor> [limit=]
history-read <id> [epoch=] [before=] [after=]  history-close <cursor>
```

Apple media / input (product-shaped; exact tokens land with C0/C5):

```text
key-down / key-up  (or unified key)
paste-text
mount [kind=diskii|smartport] [slot] [drive] <path>
unmount [kind=diskii|smartport] [slot] [drive]
mount-disk …  (Disk II alias)
select-disk / set-disk-writable  (Disk II queue / notch)
load-state  save-state
```

---

## Implementation notes

| Area | Files |
|------|--------|
| Protocol parse / format | `src/control/control_protocol.*` |
| Server / deferred | `control_server.*`, `control_deferred.*`, `control_dispatch.*` |
| Main dispatch | `src/main.c` (host loop skeleton; mirror `main_c64m.c` control sections) |
| Runtime client / thread | `runtime_client.*`, `runtime_thread.c`, `runtime_command.h`, `runtime_event.h` |
| Frame ring | `runtime_frame_ring.*` |
| History | `runtime_history.*`, `runtime_history_wire.*` (replace stub in CMake) |
| Machine observer | `apple2.*`, `cpu65.*` — begin/access/complete hooks |
| Options | `app_options.*` — history/frame budgets already sketched |
| Tests | `tests/control/*`, runtime history/frame-ring tests; gate counts in [`testing.md`](testing.md) |

**Rules:** UI and control never touch live `apple2_t`. Runtime owns rings and BP
table. Match c64m debugger *quality* for the same 6502 interactions
([`rules.md`](rules.md)); substitute Apple media and banking labels.

### Observer / BP sharing

When recording is off **and** no R/W BP is armed, the machine access observer may
be null (or a single cheap NULL test). When either needs events, one callback
path feeds both match and history append — do not pay twice per bus cycle.

### Non-goals (this epic)

| Item | Reason |
|------|--------|
| VIC-style per-line chip ring | Wrong machine; defer beam/softswitch log if needed |
| Drive CPU history | No dual 1541 CPU product |
| Persist history in snapshots / INI | Config budget only; arena is ephemeral |
| Concurrent search while running | Paused-only queries |
| Franklin/Videx card, a2m text UI | Product non-goals |

---

## Status (update when phases land)

| Phase | State |
|-------|--------|
| C0 Product control skeleton | **Done** — A2M/2 product wire via `control` |
| C1 Breakpoint wire (P5) | **Done** — A2M/3 break-create/update/list/enable/when= |
| C2 Frame ring product + wire | **Done** — ARGB ring + A2M/4 wire |
| C3 Flight recorder core | **Done** — real history linked + Apple CPU observer |
| C4 History remote API | **Done** (C4a–d; A2M/5) |
| C5a/b Polish | **Done** (C5c–e parked) |

**Manual acceptance (epic complete):**

1. `--headless --control-port N` automation: break-create → run → wait-paused → get-cpu.  
2. Free-run glitch, pause late, `get-frame-at` recovers earlier frame; `history-find` shows code path.  
3. `ctest` gate includes control + frame-ring + history coverage; turbo-2 history cost within gate.
