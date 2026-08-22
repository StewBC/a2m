# Inspector F7 scrub spine — retired (lessons kept)

**Status:** **Retired.** The F7 Inspector second shell (I0–I3 scrub spine, I4a
frame-span disasm, I5a unified disasm) was built, then discarded unpushed in
favour of [`timemachine.md`](timemachine.md) (D14/D15).  
**Code:** archived at tag `archive/f7-inspector` (commits `19b2745..7c821fe`).
Nothing of the F7 shell remains on `master`; two neutral pieces were salvaged —
see **Salvaged** below.  
**Read this file for:** the handful of facts that cost real time to learn, so
TimeMachine does not re-learn them. It is **not** a phase record or a plan.

Related: [`timemachine.md`](timemachine.md) · [`sessions.md`](sessions.md) ·
[`remote-debug.md`](remote-debug.md) · [`frontend.md`](frontend.md) · [`status.md`](status.md).

---

## Why it was retired

F7 was a *second app*: its own shell, own `kind=ui` session, own pause policy,
own CPU panel, own navigation — beside the F9 live debugger. Two UIs for one
skill, and every forensic move was a client-side FIND/READ tape walk over the
wire. TimeMachine answers queries in the runtime and materializes the past into
the one true `apple2_t`, so the existing views just work. That makes the whole
second shell dead weight ([`timemachine.md`](timemachine.md) D1/D2, non-goals).

---

## Lessons worth keeping

**Join key is `machine_cycle`.** Frames and history are **not** one lock-step
sample stream. Scrubbing to a frame yields CPU-at-that-paint only if you take
`frame.machine_cycle` and focus history on the nearest record **at or before**
that cycle. Any TimeMachine seek/materialize path needs the same discipline.

**Frame density is not time density.** Under turbo/max, paints get rarer while
history stays instruction-dense. Never synthesize fake frames to even out a
scrubber — TM2's checkpoint cadence inherits this (a frame boundary is a cheap
anchor, not a uniform clock).

**Index frames positionally, not by frame number.** Retained frame numbers have
gaps. That is why the salvaged ring accessors address by slot index and expose
metadata without copying the pixel slab.

**Live step APIs must never drive forensic navigation.** Calling
`step-instruction` to move a forensic cursor mutates the machine and invalidates
every peer history cursor. Forensic movement is query-only — TimeMachine keeps
this as D4 read-only.

**Unified disasm was the right instinct, one shell too late.** A single view +
key router with mode-aware accessors (`get_focus_pc`, `fetch_bytes`, step verbs)
and NULL-able live-only verbs gives identical muscle memory in both modes.
Live-only mutation verbs — Opt+Left set-PC, Opt+B breakpoint — simply stay
unbound in forensic mode rather than being guarded at every call site.

**Peer mutation makes cursors stale, and that must be survivable.** A live step
or a socket agent's write can invalidate a held cursor at any moment; the honest
path is `state-changed` → mark stale → re-anchor, never crash and never show
silently wrong THEN state. TimeMachine D9 keeps this cooperative model.

---

## Salvaged onto master

| Piece | Where | For |
|-------|-------|-----|
| `debugger_disasm.[ch]` | `src/frontend/` | Mode-ops disasm view + key router, LIVE / FORENSIC. Built but **unwired** until TM4 supplies a forensic ops table. |
| `runtime_frame_ring_copy_by_index` / `meta_at_index` (+ `runtime_client` wrappers) | `src/runtime/` | Index-addressed frame access tolerating frame-number gaps; metadata without pixel copy. TM2. |

Everything else — the F7 shell in `frontend.c`, its `frontend.h` API, and the F7
key path and session tokens in `main.c` — is gone.

---

## Port note (c64m)

If the same product is built there, take the end model from
[`timemachine.md`](timemachine.md), not this arc. The transferable shape is
session + rings + `machine_cycle` join + THEN/browse cursors + unified disasm
chrome with live | forensic accessors. Do **not** port the F7 dual-shell.
