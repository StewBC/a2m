# TM6 — Promote / Branch (“make this live”)

**Status:** Not started. **V1.1** (not required for TimeMachine V1 bar).  
**Epic:** [`timemachine.md`](timemachine.md)  
**Prev / Next:** [`TM5.md`](TM5.md) / —  
**Depends on:** Trusted TM2–TM3 sealed materialize; TM4 UI entry recommended for the button.

Related: [`timemachine.md`](timemachine.md) D10, D13 · [`snapshots.md`](snapshots.md) ·
[`sessions.md`](sessions.md).

---

## Goal

From a forensic cursor in the past: **Promote** so that past becomes the new live NOW,
history **after** the cursor is abandoned on a dead timeline, read-only ends, and the user can edit/step
and diverge from the old future.

```text
… CP … head(forensic) ||||| abandoned future ||||| old NOW
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
- User confirms if media caveat applies (dirty disks / HostFS activity in abandoned
  window — heuristic OK: always warn once if any disk mounted)

### Promote steps

1. **Branch, do not truncate.** History records already carry a `timeline` field and
   `runtime_history_transition_timeline()` already exists
   ([`runtime_history.h`](../src/runtime/runtime_history.h)) — bump the timeline at the
   focus cycle instead of destroying records. The old future stops being reachable by
   normal forward walks but costs nothing to keep, and it ages out naturally with the
   ring. Checkpoints and frames after the focus become dead weight; retire them lazily
   under budget pressure rather than in a hard delete.  
2. Discard live NOW anchor (will not restore on exit).  
3. Current `apple2_t` (already THEN) **is** NOW.  
4. Exit forensic read-only → live mode without restoring anchor.  
5. Invalidate peer cursors / `state-changed` (mutation class: like state load).  
6. Publish full state. Remain **paused** (user explicitly runs).  

If a genuine hard truncate is wanted later (budget reclaim), it is a separate,
explicit operation — not the default Promote path.

### After Promote

- Live poke/step/run work normally.  
- Recording may continue from new NOW (new timeline) if TM still enabled.  
- The old future is **not addressable** from the product. Do not build UI to browse it
  in this phase, and do not claim recovery.  

**Media reality (D10):** much smaller than it looks. The window can never span a guest
media write — it is cut there — so a promote target is always *after* the most recent
write. The promoted machine therefore has THEN's CPU/RAM, THEN's Disk II mechanics, and
media bytes that have not changed since that cycle. The residual is only that any write
made in the abandoned future stays written on the host. Keep the confirm dialog, but the
warning is narrow, not sweeping.

---

## UI

- Inspector tab: **Promote** / “Make this live” button (disabled when not forensic).  
- Confirm dialog: short text on abandoning the future + disk/file non-coverage.  
- Keys: optional chord later; button is enough for V1.1.

---

## API

```text
runtime_client_tm_promote(client, token)
→ worker: bump timeline, clear anchor, mode=live, state-changed
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

- [ ] Promote bumps the timeline (no hard delete); materialized state stays as NOW  
- [ ] Old timeline is unreachable by forward walks and ages out under budget  
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
2. Implement timeline-bump promote + client + UI confirm.
3. ctest; manual promote smoke. Landed. Stop.
```

---

## Landed

_(empty until implemented)_
