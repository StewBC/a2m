# TM6 — Promote / Branch (“make this live”)

**Status:** Not started. **V1.1** (not required for TimeMachine V1 bar).  
**Epic:** [`timemachine.md`](timemachine.md)  
**Prev / Next:** [`TM5.md`](TM5.md) / —  
**Depends on:** Trusted TM2–TM3 materialize; TM4 UI entry recommended for the button.

Related: [`timemachine.md`](timemachine.md) D10, D13 · [`snapshots.md`](snapshots.md) ·
[`sessions.md`](sessions.md).

---

## Goal

From a forensic cursor in the past: **Promote** so that past becomes the new live NOW,
history **after** the cursor is discarded, read-only ends, and the user can edit/step
and diverge from the old future.

```text
… CP … head(forensic) ||||| discarded future ||||| old NOW
                         ↓ Promote
… CP … new NOW (was head)   [tape continues recording forward if enabled]
```

---

## Non-goals

- Undo Promote  
- Rewinding disk/HostFS (D10) — warn only  
- Automatic Promote on scrub  
- Reverse-execution  

---

## Semantics

### Preconditions

- Forensic mode active  
- Materialize at focus succeeded  
- User confirms if media caveat applies (dirty disks / HostFS activity in discarded
  window — heuristic OK: always warn once if any disk mounted)

### Promote steps

1. Truncate TimeMachine tape (checkpoints/deltas/HST1/frames) to **≤ focus cycle**
   (drop strictly after).  
2. Discard live NOW anchor (will not restore on exit).  
3. Current `apple2_t` (already THEN) **is** NOW.  
4. Exit forensic read-only → live mode without restoring anchor.  
5. Invalidate peer cursors / `state-changed` (mutation class: like state load).  
6. Publish full state. Remain **paused** (user explicitly runs).  

### After Promote

- Live poke/step/run work normally.  
- Recording may continue from new NOW if TM still enabled.  
- Old “future” is gone; no claim it can be recovered.

---

## UI

- Inspector tab: **Promote** / “Make this live” button (disabled when not forensic).  
- Confirm dialog: short text on truncate + disk/file non-coverage.  
- Keys: optional chord later; button is enough for V1.1.

---

## API

```text
runtime_client_tm_promote(client, token)
→ worker: truncate rings, clear anchor, mode=live, state-changed
```

Reject if not in forensic mode.

---

## Testing

| Test | Expect |
|------|--------|
| Promote | after promote, poke mem succeeds; exit path does not restore old NOW |
| Truncate | history/TM window newest ≤ focus cycle |
| state-changed | peers notified |
| Reject | promote in live mode fails |

---

## Acceptance checklist

- [ ] Promote truncates future tape; materialized state stays as NOW  
- [ ] Read-only cleared; live edit works  
- [ ] Media warning present  
- [ ] `state-changed` / cursor invalidate  
- [ ] UI button + confirm  
- [ ] ctest + build green  
- [ ] Landed filled  

---

## Agent script

```text
1. Read agents/timemachine.md D10/D13, TM3 Landed (anchor/exit), TM4 UI, TM6.md.
2. Implement truncate + promote + client + UI confirm.
3. ctest; manual promote smoke. Landed. Stop.
```

---

## Landed

_(empty until implemented)_
