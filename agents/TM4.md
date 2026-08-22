# TM4 — One-skin UI (Misc Inspector tab)

**Status:** Not started.  
**Epic:** [`timemachine.md`](timemachine.md)  
**Prev / Next:** [`TM3.md`](TM3.md) / [`TM5.md`](TM5.md)  
**V1 bar:** Required (closes TimeMachine V1 with TM0–TM3).  
**Depends on:** TM3 materialize + forensic mode; TM1 verbs; TM0 recording enable.

Related: [`frontend.md`](frontend.md) · [`inspector.md`](inspector.md) (I5a salvage) ·
[`rules.md`](rules.md) · D1, D3, D4, D14, D15.

---

## Goal

One debugger interface: **live** and **forensic** share F9 chrome. Inspector is a
**Misc tab + mode**, not a second application. F7-as-separate-shell is retired (removed
or thin alias into Inspector mode).

Forensics uses the same keys as debugging; mode-aware verbs hit TimeMachine.

---

## Non-goals

- FIND power browser / query templates (later)  
- Promote/Branch button (TM6)  
- Full forensic BP product (TM5) — in TM4 either disable BP edits in forensic with a
  clear message **or** stub “opens TM5 later”  
- Preserving F7 layout for nostalgia (D14)  
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
- Status line: `TIME MACHINE` / cycle / media caveat once.

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

### F7 retirement

- Remove exclusive F7 Inspector view **or** make F7 = “open debugger + Inspector tab +
  enter forensic if possible.”  
- Update `status.md`, `manual/manual.md`, key tables.  
- Delete or gut `frontend_inspector_*` second shell once Misc tab works — salvage only
  useful helpers.

---

## Implementation notes

- Prefer driving everything through `runtime_client_tm_*` from TM1/TM3.  
- Frame scrubber: use frame ring info + TM3 seek(`frame.machine_cycle`).  
- Recording toggle: TM0 flag + history/frame/TM recorder enable (unified story).  
- Sessions: UI may keep a `kind=ui` session; not required to FIND-page for nav anymore.

---

## Code anchors

| Area | Path |
|------|------|
| Misc / debugger layout | `src/frontend/debugger_*.c`, layout misc pane |
| Host keys | `src/main.c` |
| Disasm mode ops | `src/frontend/debugger_disasm.*` |
| Legacy F7 | `frontend_inspector_*` in `frontend.c` / `frontend.h` |
| Client TM APIs | from TM1/TM3 Landed |
| Docs | `agents/status.md`, `manual/manual.md` |

---

## Manual smoke (required)

1. TM off → play; confirm no obvious new cost; Inspector explains how to enable.  
2. TM on → run → Pause from tab → scrub frames → mem/regs/display match past.  
3. F10/F11/F12 forensic nav without FIND lag.  
4. Try poke mem → rejected.  
5. Exit Inspector mode → NOW restored; still paused; F12 runs live again.  
6. F7 gone or aliased; docs match keys.  

Desktop + reasonable window size; tint visible.

---

## Acceptance checklist

- [ ] Misc Inspector tab is the entry; dual F7 app gone from product  
- [ ] Forensic tint + read-only views  
- [ ] Scrubber + TM verbs wired; same-skin keys  
- [ ] Live BP list not silently mutated in forensic  
- [ ] Docs/keys/manual updated  
- [ ] Build + ctest green; manual smoke above  
- [ ] Landed filled — **TimeMachine V1 bar (TM0–TM4) claimable**  

---

## Agent script

```text
1. Read agents/rules.md, agents/timemachine.md D1/D14/D15, TM3 Landed, TM4.md,
   agents/frontend.md.
2. Implement Misc Inspector tab + mode chrome; wire TM client; retire F7 shell.
3. Manual smoke; update status/manual. Build + ctest.
4. Landed. Stop — do not start TM5/TM6 unless human accepts V1 and asks.
```

---

## Landed

_(empty until implemented)_
