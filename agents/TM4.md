# TM4 — One-skin UI (Misc Inspector tab)

**Status:** Landed.  
**Epic:** [`timemachine.md`](timemachine.md)  
**Prev / Next:** [`TM3.md`](TM3.md) / [`TM5.md`](TM5.md)  
**V1 bar:** Required (closes TimeMachine V1 with TM0–TM3).  
**Depends on:** TM3 materialize + forensic mode; TM1 verbs; TM0 recording enable.

Related: [`frontend.md`](frontend.md) · [`inspector.md`](inspector.md) (I5a verb-mapping
lessons only — no live code) · [`rules.md`](rules.md) · [`max-free-run.md`](max-free-run.md) ·
D1, D3, D4, D14, D15.

---

## Goal

One debugger interface: **live** and **forensic** share F9 chrome. Inspector is a
**Misc tab + mode**, not a second application.

Forensics uses the same keys as debugging; mode-aware verbs hit TimeMachine.

**Scope note:** the F7 second shell was already removed in `b738cef` (D14) — there is no
`frontend_inspector_*` in `src/` and no F7 binding in `main.c`. This phase is **net-new
UI only**; nothing to retire, nothing to alias. Earlier drafts of this doc budgeted for
that removal — they were stale.

---

## Non-goals

- FIND power browser / query templates (later)  
- Promote/Branch button (TM6)  
- Full forensic BP product (TM5) — in TM4 either disable BP edits in forensic with a
  clear message **or** stub “opens TM5 later”  
- Reviving any part of the F7 layout (D14)  
- Flaky automated UI tests  

---

## UX

### Misc → Inspector tab

| Machine state | Tab shows |
|---------------|-----------|
| Running, TM recording **off** | Enable recording control + short why; optional Pause |
| Running, TM recording **on** | Primary **Pause** (enter forensic when paused) |
| Paused | Scrubber (frame rough position) + focus cycle/id; recording toggle; exit forensic / stay paused |

Entering forensic (pause + Inspector engage) calls TM3 enter; scrubber/seeks call TM3
seek; leaving Inspector mode calls TM3 exit (restore NOW). **No auto-resume.**

### Whole debugger in forensic

- Visual cue: distinct background/canvas tint (unmistakable).  
- All value setters read-only (reject); rely on TM3 runtime reject too.  
- Display / mem / softswitches / regs: normal views on one true state.  
- Status line: `TIME MACHINE` / cycle / window extent.
- When the window was cut by a media write, say why at the scrubber's left edge —
  `disk write @ cycle N` (D10). A user whose 5 seconds became 0.2 must see the reason.

### Verb mapping (extend I5a to the shell)

| Chord / action | Live | Forensic |
|----------------|------|----------|
| F10 | step insn | TM step |
| F11 / Shift+F10 | step over / out | TM step over / out |
| F12 / Shift+F12 | run / run to cursor | TM run-to / run-tape-to-PC |
| Opt+Left (set PC) | poke PC | seek next (or first) hit of typed PC on tape |
| Opt+B (BP) | live BP | **disabled** or TM5 — not live list mutation |
| Memory type-in | write | reject |
| Scrubber | n/a | frame rough → seek cycle |

Reuse shared disasm ops table pattern (`debugger_disasm` mode ops).

### Key surface

- **Pinned: F7 stays unbound.** It was freed when the old Inspector was removed; do not
  rebind it. Rationale: `../c64m` binds no F7 either (`rules.md` rule 2 — match c64m for
  shared debugger interaction), and a dedicated top-level “go to Inspector” key re-creates
  the *F7 is the other thing* model that D14 removed. The Misc tab is the entry (D15).  
- Consequence: **no key-table or manual change for F7.** `manual/` is already clean of F7
  references; leave it that way.  
- If this is ever revisited, bind it to **navigation only** (open debugger + focus the
  Inspector tab) — never to auto-entering forensic. A key that sometimes switches tabs and
  sometimes flips the machine into read-only past is unpredictable; forensic entry has its
  own explicit control in the UX table above.  
- Update `status.md`, `manual/manual.md`, key tables for everything else that ships.

---

## Implementation notes

- Prefer driving everything through `runtime_client_tm_*` from TM1/TM3.  
- Frame scrubber: use frame ring info + TM3 seek(`frame.machine_cycle`).  
- Recording toggle: TM0 flag + history/frame/TM recorder enable (unified story).  
- Window extent and its start reason come from `runtime_tm_window_info` — do not infer
  either from frame-ring bounds.  
- Sessions: UI may keep a `kind=ui` session; not required to FIND-page for nav anymore.

---

## Code anchors

| Area | Path |
|------|------|
| Misc / debugger layout | `src/frontend/debugger_*.c`, layout misc pane |
| Host keys | `src/main.c` |
| Disasm mode ops | `src/frontend/debugger_disasm.*` |
| Client TM APIs | from TM1/TM3 Landed |
| Turbo ladder | `agents/turbo-zip.md`, `agents/max-free-run.md` |
| Docs | `agents/status.md`, `manual/manual.md` |

---

## Manual smoke (required)

1. TM off → play; confirm no obvious new cost; Inspector explains how to enable.  
2. TM on → run → **Pause** from tab → **Inspect (enter forensic)** → scrub the slider
   → mem/regs/display match past. (Scrubber is forensic-only, not on live pause.)  
3. F10/F11/F12 forensic nav without FIND lag.  
4. Try poke mem → memory footer says **read-only**; registers are locked; Inspector
   copy says memory/registers are read-only.  
5. Exit Inspector mode → NOW restored; still paused; F12 runs live again.  
6. **Boot a disk, scrub back through the load, exit** → drive still works; host image
   file unchanged.  
7. **Save to disk while recording** → window visibly cuts at the write, scrubber shows
   the reason, forward recording continues, and the host image is correct (D10).  
8. **TM on + Opt+T max** → whatever TM2 pinned for max actually happens (recording
   degraded or refused) and the tab says so honestly — no silent window collapse.  
9. Docs match keys.  

Desktop + reasonable window size; tint visible.

---

## Acceptance checklist

- [x] Misc Inspector tab is the only forensic entry  
- [x] Forensic tint + read-only views  
- [x] Scrubber + TM verbs wired; same-skin keys  
- [x] Scrubber extent reflects `tm_window`, not frame-ring extent alone (D17)  
- [x] Live BP list not silently mutated in forensic  
- [x] Max-turbo behaviour surfaced honestly in the tab  
- [x] Window-cut reason shown at the scrubber edge when a media write truncated it  
- [x] `manual/manual.md` states plainly: **a disk write drops earlier history**  
- [x] Docs/keys/manual updated  
- [x] Build + ctest green; GUI smoke is the human playbook below  
- [x] Landed filled — **TimeMachine V1 in tree; GUI smoke is the accept gate**  

---

## Agent script

```text
1. Read agents/rules.md, agents/timemachine.md D1/D14/D15/D17, TM3 Landed, TM4.md,
   agents/frontend.md, agents/max-free-run.md.
2. Implement Misc Inspector tab + mode chrome; wire TM client. Net-new UI — F7 is
   already gone; do not go looking for a shell to retire.
3. Manual smoke (incl. disk scrub + max turbo); update status/manual. Build + ctest.
4. Landed. Stop — do not start TM5/TM6 unless human accepts V1 and asks.
```

---

## Landed

Handoff. TimeMachine V1 (TM0–TM4) is in the tree. F7 stays unbound. GUI smoke
is the human accept gate (playbook in this file). Do not start TM5/TM6 until
that is accepted.

### Entry

Misc (F9 debugger) -> **Inspector** tab. Two rows of three tabs: Machine /
Debugger / Breakpoints, then Hardware / Assembler / Inspector.

| State | Tab |
|-------|-----|
| TM off | Recording checkbox + why; optional Pause |
| Running, TM on | **Pause**; then Inspect |
| Paused, window ready | **Inspect (enter forensic)** |
| Forensic | Scrubber over `tm_window`; **Leave Inspector (restore NOW)** |
| Max turbo / `history_off_on_max` | Banner: recording stopped; Opt+T discards the tape |
| Empty window | Honest: recorder off, budget 0, or nothing recorded |

No auto-resume on leave. F12 runs live after exit.

Recording toggle is `runtime_client_tm_set_enabled` (TM0 off→on arm). Locked
while forensic (TM3 rejects `TM_SET_ENABLED`).

### Scrubber (D17)

Slider **0..1000 maps `tm_window.oldest_cycle` .. `newest_cycle`**, from
`machine_state` (not frame-ring extent). Seek is `runtime_client_tm_seek_cycle`.
Left-edge text uses `tm_window_start_kind` / `start_arg1`:

- guest-write: `history starts here: disk write, sNdN @ cycle C` (drive is 1-based)
- host-directory: `history starts here: host folder change, sNdN @ cycle C`
- else: `history starts here: <name> @ cycle C`

### Keys (one skin)

| Chord | Forensic |
|-------|----------|
| F10 | `runtime_client_tm_step(+1)` |
| F11 | `tm_step_over` |
| Shift+F10 | `tm_step_out` |
| F12 / Shift+F12 | `tm_run_to` at disasm cursor |
| Opt+Left | `tm_run_to` at disasm cursor (not poke PC) |
| Opt+B | unbound |
| Memory / register type-in | no-op in UI; worker would `read-only-forensic` |

Live F10-while-running is still Pause (does **not** auto-enter forensic).

### Chrome

- Window background/header tint (warm brown) while `tm_forensic`.
- Title: `TIME MACHINE oldest-newest @ focus` via `frontend_format_window_title_ex`.
- Registers: `NK_EDIT_READ_ONLY` (same `editable` flag as running).
- Memory footer: `read-only` while forensic (not `editable`). Hex type-in is a no-op.
- Breakpoints tab: New / Edit / Duplicate / Enable / Disable / Clear / Clear All
  disabled + "forensic breakpoints are TM5". **View PC** stays (navigation).
  Opt+B unbound. BP create/update/clear/enable intents are dropped in
  `dispatch_intent` while forensic (worker still allows control-port BP edits).
- Host keys not sent to the Apple while forensic. F8, Opt+Insert paste,
  quicksave/load, Opt+Shift+A, and kbd-stick also skipped (worker would
  `read-only-forensic`). Configure OK (`CONFIG_APPLY`) is dropped too.

`debugger_disasm` ops table is still unused as a drop-in for the live disasm
panel (that view has its own router). Forensic verbs are the F-keys + Opt+Left
in the existing disasm handler.

### Machine snapshot fields (UI source)

Published on every `MACHINE_STATE_RESPONSE` and copied into
`frontend_debug_state`: `tm_mode`, `tm_enabled`, `tm_window_valid`,
recording flags, `tm_stopped_for_max`, start_kind/arg1, focus cycle/id,
oldest/newest cycle.

### Intents

`TM_SET_ENABLED`, `TM_ENTER_FORENSIC`, `TM_EXIT_FORENSIC`, `TM_SEEK_CYCLE`,
`TM_PAUSE`, `TM_RUN_TO`. Mutating intents are dropped in `dispatch_intent`
while forensic.

### Tests / docs

ctest **59** green. No new UI automation (brief). `manual/manual.md`: Inspector
path, disk-write drops history, max turbo, forensic F-keys. `status.md` keys
updated. F7 still absent from `manual/`.

### GUI smoke (human)

Playbook in this file, items 1–9. **Still the V1 accept gate.** This session
could not capture or click the SDL window (no Screen Recording / assistive
access). Control-port checks on a windowed `--timemachine` launch: `hello`
`protocol=A2M/12`, `capabilities` includes `timemachine`, history
`recording=1` while live, `get-state mode=live`. Enter/seek/step stay off the
wire, so forensic chrome (tint, slider, Inspect/Leave) needs a human.

Suggested launch:

```bash
./build/a2m --noini --nosaveini --timemachine --control-port 6530 \
  -d "tests/fixtures/Apple DOS 3.3 January 1983.nib"
```

F9 → Misc → Inspector. Off-path launch: omit `--timemachine`. For item 7,
copy the `.nib` first so the fixture stays clean, `set-disk-writable 1` or
Configure write-enable, SAVE a BASIC file, then Inspect and read the left-edge
reason.

### What TM5 must not break

Forensic must not silently edit the live BP list. Opt+B stays unbound until TM5
supplies a forensic store. Leave Inspector still restores NOW and stays paused.
