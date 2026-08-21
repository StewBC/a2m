# Sessions: multi-asker query foundation (Inspector prep)

**Status:** Active epic — foundation only. **No Inspector UI in this epic.**  
**Product name (later UI):** Inspector (F9-like second view; not this campaign).  
**Depends on:** remote-debug C0–C5b closed (A2M/10 control + history + frame ring).  
**Unblocks:** Inspector UI discussion / C5d history browser as a host view.

Related: [`remote-debug.md`](remote-debug.md) · [`control-tools.md`](control-tools.md) ·
[`rules.md`](rules.md) · [`runtime.md`](runtime.md) · [`status.md`](status.md).

Source is authoritative. If this brief and code disagree, fix the brief in the
same change.

---

## Why

a2m accepts **one TCP control client**. Coop wants an **AI on that socket** and a
**human at the glass** both interrogating the same paused machine (history FIND
pages, frame scrub, CPU/mem snapshots) without a separate app.

Today the *capability surface* exists (A2M verbs + `runtime_client`). The
*session state* does not:

| Piece | Today | Problem |
|-------|--------|---------|
| History cursor | **One** `runtime_history_cursor` on `runtime` | Second FIND stomps the first |
| Control deferred | `CONTROL_DEFERRED_CAPACITY = 1` | Fine for one socket; not the multi-cursor problem |
| UI path | Intents → `runtime_client` (no session id) | Cannot own a cursor alongside the socket |
| Mutation awareness | Peer only sees effects if it re-polls / hits `CURSOR_STALE` | AI mid-analysis does not get an explicit nudge |

**Sessions** are the fix. Inspector UI is a later consumer of the same API.

---

## Decisions (pinned — do not re-litigate in implementation)

| Topic | Decision |
|-------|----------|
| Name (product, later) | **Inspector** — out of scope for UI here |
| Unit of multi-asker | **`session`**: endpoint + stateful query bits + reply routing |
| Where sessions live | **Runtime-authoritative** for history cursors / mutation notify. Control binds the TCP client to one session. UI will later bind via `runtime_client` session APIs. |
| Capacity | Fixed small **N = 4** slots (product needs 2: UI + socket; 4 avoids a resize story) |
| Ownership / who may step | **Open mutation** — UI and socket may both step/run/poke. **No ask-to-step. No “socket owns the machine.”** |
| Inform | On mutation, publish **`state-changed`** (runtime event → control wire push to other live sessions). Awareness only, not a lock. |
| Soft “lead” badge | **Non-goal** this epic |
| Soft hold / steal control | **Non-goal** this epic |
| Whole-session “invalid” | **No.** Stale **cursors / scrub anchors** only; session object stays alive until endpoint disconnect / explicit close |
| Deferred table | Keep control deferred capacity **1** for socket waits/bulk unless a phase proves otherwise. Do **not** conflate deferred slots with sessions. |
| Protocol bump | Bump **A2M/N** when the wire gains session id and/or unsolicited `state-changed` events clients must understand |
| Headless | Sessions must work headless (socket-only: one session). Dual-session tests may be in-process (two runtime session ids, no UI). |
| Separate control app | **Non-goal** — humans use future Inspector; agents keep using the socket |

### Mutation set that invalidates history cursors

Already encoded in `runtime_history_command_invalidates_cursor()` (extend if a
new mutator appears). Includes step/run family, mem/reg writes, reset,
history record/clear, save/load state, assemble, media, machine config apply.

Inform (`state-changed`) should fire for the same class (and pause/run edge if
useful so agents notice freezes without only relying on wait latches).

---

## Shape (target model)

```text
                    ┌─────────────────────────────┐
  UI (later)  ─────►│ session[i]                  │
                    │  id, kind=ui|control        │
  TCP client  ─────►│  history_cursor             │
                    │  reply sink / epoch         │
                    └──────────────┬──────────────┘
                                   │ commands carry session_id
                                   ▼
                    runtime worker (serializes all cmds)
                    history arena + frame ring (shared, mostly stateless reads)
```

- **Stateless reads** (`get-cpu`, `get-memory`, `get-frame-at`, `history-read` by
  absolute id, info commands): any session; worker serializes; answer by
  request token / wire request id.
- **Stateful history page** (`history-find` / `next` / `close`): cursor lives
  **on the session**, not on a global `rt->history_cursor`.
- **Endpoints:**
  - `control` — created/replaced when TCP accepts; destroyed on disconnect
    (tie to existing `connection_epoch`).
  - `ui` — created via runtime API (tests now; Inspector later). At most one
    product UI session initially is fine; API should still be id-based.

Request correlation stays as today:

- Wire: client-chosen request id within the TCP session.
- `runtime_client`: request tokens.
- Do **not** invent a second id taxonomy (“UI ids vs socket ids”). Session id
  names the asker; request id/token names the question.

---

## Non-goals (this epic)

- Inspector UI, key chord, layout, Nuklear panels
- Auto-pause-on-Inspector-open (UI policy later; may call existing `pause`)
- Multi-TCP clients
- Time travel / reverse execution
- Changing A2M command *semantics* for single-client scripts beyond additive
  session fields / events (old one-client scripts should keep working)
- Replacing coop_watch / `Ctl` (update them only as needed for new events)

---

## Current code anchors (start here)

| Area | Path |
|------|------|
| Single history cursor | `src/runtime/runtime_internal.h` (`runtime_history_cursor`, `history_cursor`) |
| Invalidate / FIND / NEXT / CLOSE | `src/runtime/runtime_thread.c` (`runtime_history_invalidate_cursor`, history_*_command) |
| History client API | `src/runtime/runtime_client.c` / `.h` |
| Control deferred (cap 1) | `src/control/control_deferred.h` |
| Control dispatch + waits | `src/control/control_dispatch.c` |
| Socket epoch / one client | `src/control/control_server.c` |
| Wire parse/format | `src/control/control_protocol.*` |
| History tests | `tests/runtime/test_runtime_history_query.c`, `test_runtime_history_commands.c` |
| Control protocol test | `tests/control/test_control_protocol.c` |
| Ops brief | `agents/control-tools.md` · `tools/a2m_control_client.py` |

---

## Phases

Implement in order. Each phase ends with **build + ctest green** and a short
note in this file (Landed line). Prefer small commits per phase.

### S0 — Session table in runtime (no wire change yet)

**Goal:** Replace the single `history_cursor` with a fixed session table; one
default session preserves today’s behaviour.

- Add `runtime_session` (name flexible) holding at least:
  - `uint32_t id` (or `uint64_t`; never 0)
  - `kind` (`none` / `ui` / `control`)
  - `active` flag
  - `runtime_history_cursor` (move fields from today’s global cursor)
  - optional `generation` / endpoint epoch for control binding
- `runtime` holds `sessions[RUNTIME_SESSION_CAPACITY]` with **CAPACITY = 4**
- APIs on runtime (worker-side helpers + client wrappers as needed):
  - allocate / release session by kind
  - lookup by id
  - “default” session id for backward-compatible commands that omit session
- Move FIND/NEXT/CLOSE / invalidate paths to **per-session** cursor
- `runtime_history_invalidate_cursor(rt)` becomes invalidate **all** active
  session cursors (or invalidate-all + optional by-id); generation bump stays
  global on the history arena as today
- Commands that touch history must carry `session_id` (0 = default session)

**Exit:**

- Existing history ctests still green with default session
- New unit/runtime test: **two session ids**, interleaved `HISTORY_FIND` with
  `more`, each `HISTORY_NEXT` only advances its own cursor; no cross-stomp
- Global `rt->history_cursor` removed (or reduced to a deprecated alias removed
  in the same phase — prefer delete)

**Landed:** Session table `sessions[4]`; default UI session at create; FIND/NEXT/CLOSE
per-session cursor; invalidate-all on mutation; `runtime_history_sessions` ctest;
global `rt->history_cursor` deleted.

---

### S1 — `runtime_client` session surface (still no Inspector)

**Goal:** Host/tests can open a UI-kind session and address history RPCs by id.

- `runtime_client_session_open(kind)` → session id (event or sync return — match
  local RPC style used elsewhere; prefer token + response event if that is the
  house pattern, else immediate id if allocation is main-thread bookkeeping
  only). Prefer **worker-allocated ids** so the table stays worker-owned.
- `runtime_client_session_close(id)`
- History helpers (`history_find` / `next` / `read` / `close`) take `session_id`
- Non-history snapshot APIs may ignore session id for now (optional arg ok)
- Document: omitting session id uses default/control-compat session

**Exit:**

- Test opens two sessions via client, dual FIND as in S0, then closes both
- No frontend changes

**Landed:** `runtime_client_session_open/close` + `RUNTIME_EVENT_SESSION_RESPONSE`;
history helpers take `session_id` (0 = default); dual-session test closes both;
control dispatch still passes 0 until S2.

---

### S2 — Control port binds TCP client → one session

**Goal:** Socket client is a first-class session; disconnect frees it.

- On accept: allocate `kind=control` session; store id on `control_dispatch`
- On disconnect / epoch bump: close that session; cancel deferred as today
- History wire commands use that session id when calling runtime
- Single-client scripts with no session syntax keep working (dispatch fills id)
- If a second conceptual control session is requested: still **one TCP client**
  max — do not add multi-TCP

**Exit:**

- Headless: `history-find` / `next` behave as A2M/10 today
- Disconnect mid-FIND leaves no orphan cursor (slot reusable)
- ctest / smoke via `Ctl` unchanged for happy path

**Landed:** TCP client binds `kind=control` session (epoch-tracked); history
wire uses that session id; disconnect / epoch bump / shutdown releases the
slot. Ctl history-find/next smoke green (scripts still omit session syntax).

---

### S3 — Inform: `state-changed` events

**Goal:** Other askers learn the machine/timeline moved — awareness only.

- Add `RUNTIME_EVENT_STATE_CHANGED` (name flexible) with a small payload:
  - `reason` enum/string: at least `step`, `run`, `pause`, `poke`, `reset`,
    `load-state`, `history-clear`, `media`, `other`
  - `source_session_id` (0 if unknown / internal)
  - useful anchors: `cycles`, `frame` (if cheap), history `epoch` if available
- Emit from the same places that invalidate history cursors (and pause/run
  edges as needed). Avoid event spam floods: coalesce per command completion,
  not per Φ0.
- Control dispatch: when event source session ≠ TCP session (or always for
  non-solicited), push an **unsolicited** wire line to the client, e.g.

  ```text
  0 event state-changed reason=step session=2 cycles=12345 epoch=1
  ```

  Pin exact framing in `control_protocol` + manual + `control-tools.md`.
  Using request id `0` for events is fine if documented; alternatively a
  dedicated `event` verb — pick one and test it.
- Bump **A2M/N** (`CONTROL_PROTOCOL_VERSION`); advertise in `capabilities`
  (e.g. `sessions` and/or `state-changed`).
- Python: teach `Ctl` to parse/ignore or surface events without breaking
  `cmd()` request matching (events must not steal the next reply for id N).

**Exit:**

- Test: session A steps (or write mem) → session B (or control client) observes
  `state-changed` with expected reason
- Dual history test: after step, NEXT on old cursor → `CURSOR_STALE`; re-FIND
  works
- `make` / existing coop smoke still works; watcher may log events

**Landed:** _pending_

---

### S4 — Polish + docs gate (still no UI)

**Goal:** Agent-ready surface; Inspector discussion can start.

- Update [`control-tools.md`](control-tools.md), [`runtime.md`](runtime.md),
  [`status.md`](status.md), manual remote section as needed
- Point [`remote-debug.md`](remote-debug.md) C5d at this epic: UI consumes
  sessions; do not invent a second history path
- Optional: `frame-ring` scrub “focus” is **not** required in sessions yet —
  absolute `get-frame-at` is enough for UI later. Only add per-session scrub
  state if a test proves it belongs in the table.
- Confirm rules: no `apple2_t *` across threads; sessions hold no live machine
  pointers — only cursor metadata + ids

**Exit:**

- Checklist below all checked
- This file: Status → Closed (foundation); UI remains follow-up

**Landed:** _pending_

---

## Acceptance checklist (foundation done)

- [ ] Fixed session table (N=4) in runtime; no global history cursor
- [ ] Two in-process sessions can page history independently
- [ ] TCP client maps to one control session; disconnect cleans up
- [ ] Open mutation; no permission protocol
- [ ] `state-changed` inform reaches the socket client; `Ctl` safe with events
- [ ] A2M version bumped + capabilities + docs
- [ ] Full ctest green
- [ ] **No** Inspector UI / host chord / frontend layout work required to claim done

---

## Agent script (follow exactly)

```text
1. Read agents/rules.md, agents/sessions.md (this file), agents/remote-debug.md
   (history/control anchors only), agents/testing.md.
2. Implement S0 → ctest. Update "Landed" line.
3. Implement S1 → ctest. Update "Landed".
4. Implement S2 → ctest + quick Ctl history-find smoke. Update "Landed".
5. Implement S3 → ctest + Ctl event parse. Bump A2M/N. Update docs in same
   change as the wire bump. Update "Landed".
6. S4 doc/status polish. Mark epic foundation closed in status.md.
7. Stop. Do not start Inspector UI. Next human discussion: UI shape on top of
   runtime_client session + existing A2M verbs.
```

If blocked (e.g. event framing choice), pick the smallest option that keeps
request/response matching correct for `Ctl.cmd`, document it, continue.

---

## Out-of-scope notes for the *next* discussion (UI)

Not binding — only so implementers do not “helpfully” build UI now:

- Inspector toggle key (distinct from F9 debugger)
- Auto-pause on enter; resume policy on exit
- Scrub UX for frame ring + history; how human says “look at frame N” to the AI
- Whether UI talks A2M verbs locally or only `runtime_client` session APIs
  (both are viable once S0–S3 exist; prefer one mental model with the agent)

---

## Protocol versioning note

Current product wire at epic start: **A2M/10**. This epic bumps when S3 (or
S2 if session ids become visible on the wire) lands. Update the table in
`remote-debug.md` and `CONTROL_PROTOCOL_VERSION` in the same commit.
