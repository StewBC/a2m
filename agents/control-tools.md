# Control tools handoff: Python client + coop_watch

**Ergonomics layer** on A2M/6 so agents and humans drive remote debug the same
way every time — the railgunner-style `make coop` *workflow*, not a sample game.

**Status:** **done (T0–T5)** · **Depends on:** remote debug C0–C5b (landed) —
[`remote-debug.md`](remote-debug.md)

Related: [`status.md`](status.md) ·
[`rules.md`](rules.md) · c64m gold: `../c64m/tools/c64_control_client.py`,
`../c64m/tools/coop_watch.py` · workflow existence proof only:
`../railgunner/Makefile` target `coop`.

---

## Quick start

```bash
# Client (headless scripting)
./build/a2m --noini --headless --control-port 6510 &
python3 -c "
import sys; sys.path.insert(0, 'tools')
from a2m_control_client import Ctl
c = Ctl(port=6510)
print(c.cmd('hello'))
print(c.cmd('get-cpu'))
c.cmd('run'); c.wait_frame(2, 5000); c.cmd('pause'); c.wait_paused(2000)
r = c.history_find(limit=8)
assert r['payload'][:4] == b'HST1'
print(c.format_hst1_page(r, compact=True))
"

# Coop loop (windowed play + watcher)
# Terminal A:
./build/a2m --control-port 6510
# Terminal B:
tools/a2m_coop_watch.py --port 6510
# or: make coop   # prints the recipe; make coop-watch starts the daemon
```

| Tool | Role |
|------|------|
| `tools/a2m_control_client.py` | `Ctl` framing, mem modes, BPs, frames, HST1, waits, ARGB PNG |
| `tools/a2m_coop_watch.py` | pause → snap pack → inbox arm/hist/scrub/resume |

Snaps land in `build/debug/snap-NNN.txt` (+ optional `snap-NNN-frames/`).
Inbox: append lines to `build/debug/coop_inbox`.

**Not goals:** second protocol; history UI browser (C5d parked); VIC ring;
reimplementing recorder/frame ring in Python.

---

## Product acceptance

### Minimum (client only — highest leverage) — **met**

See Quick start. An agent can drive headless a2m **without hand-writing socket framing**.

### Full (coop loop) — **met**

```text
Terminal A:  ./build/a2m --control-port 6510   # windowed, playable
Terminal B:  tools/a2m_coop_watch.py --port 6510
```

1. Human plays; daemon blocks on `wait-paused`.  
2. Human pauses (or BP hits) → `build/debug/snap-NNN.txt` written; machine stays frozen.  
3. Agent/human appends inbox lines (`arm write $xxxx`, `hist …`, `scrub 60`, `resume`).  
4. On BP hit after `arm`, next snap is the smoking gun with history context.

---

## Phase plan

### T0 — Inventory (read-only, short)

Confirm wire still matches this note (source of truth if drift):

| Surface | Commands / facts |
|---------|------------------|
| Identity | `hello` → `name=a2m protocol=A2M/6` |
| Exec | `run` `pause` `reset` step family `set-turbo` |
| State | `get-state` `get-cpu` `get-softswitches` `get-memory` / `set-memory` (modes: map main aux lc1 lc2 rom) |
| Frame | `get-frame` → ARGB **560×192**, stride = width×4 |
| Frame ring | `frame-ring-info` `frame-ring-record` `frame-ring-clear` `get-frame-at frame=\|cycle=` |
| Breakpoints | `break-create` / update / list / enable / clear / rearm; `when=`; access exec/read/write |
| History | `history-info` `history-record` `history-clear` `history-find` `history-next` `history-read` `history-close` → `data history` **HST1** |
| Waits | `wait-paused` `wait-running` `wait-frame` `wait-event` |
| Gotchas | Headless often starts **paused** → first `wait-paused` returns once (latch consumed); then `run` before free-run waits; `set-turbo` takes MHz/`max`/`-1` (not 1/2/3); max still fills live frames via presentation paint; `quit-client` ≠ process exit; BP mapping axes are `ram=map\|main\|aux`, `c100=map\|rom`, `d000=map\|lc1\|lc2\|rom`; break-create/clear return **data** (breakpoints list), not bare ok; BP hit → `stop=breakpoint`; **`get-memory` of `$C0xx` is not softswitch state** (peeks RAM; use `get-softswitches`); **peer disconnect mid-wait frees the client slot** (socket watched while deferred; epoch cancel + drain — no port wedge) |

ctest gate: **37 green** (see [`testing.md`](testing.md)).

### T1 — `tools/a2m_control_client.py` (foundation)

Lift structure from `../c64m/tools/c64_control_client.py` (`Ctl` class). **Do not
blind rename** — Apple deltas below.

| Piece | Behaviour |
|-------|-----------|
| Framing | `<id> <cmd>\n` → ok / error / data + raw bytes + trailing `\n` |
| Core | `cmd`, `ok`, optional `pipeline` (responses may complete out of order if pipelined later) |
| Memory | `mem(addr, length, mode="map")` — modes **map / main / aux / lc1 / lc2 / rom** |
| Breakpoints | helpers that emit real `break-create` lines (access, `when=`, actions) |
| Frames | `get_frame()` → width/height/ARGB bytes |
| Frame ring | `frame_ring_info()`, `get_frame_at(frame=N)` / `cycle=N` |
| History | `history_info()`, `history_find(...)`, `history_read(...)`, `history_close` |
| HST1 | decode helper: header + 48-byte records + 8-byte accesses (see c64m client / `cpu-flight-recorder.md` in c64m agents) |
| Waits | `wait_paused`, `wait_frame` parsing metadata |

**Apple-only docstring gotchas:** A2M/6 identity; memory modes; 560×192 ARGB;
`get-softswitches` (not `$C0xx` mem); no VIC/CIA/drive-cpu product surface;
headless start paused; turbo MHz/`max`.

**Exit:** scripted smoke (hello, get-cpu, pause/run, history-find HST1) green.

**Size:** ~200–350 lines.

### T2 — HST1 human text (optional slice after T1)

Turn find/read pages into readable lines for snap files and agents:

```text
id=… pc=… a=… opcode=… accesses: write $C000=xx @+1 …
```

Can live in the same module as the client.

### T3 — Frame → PNG

ARGB 560×192 → PNG (stdlib zlib/struct or Pillow). Needed for coop scrub
eyeballing. c64m used indexed Pepto; **do not** reuse Pepto for product frames.

### T4 — `tools/a2m_coop_watch.py` (or `coop_watch.py`)

Port from `../c64m/tools/coop_watch.py`; CONFIG + dump_pack + media helpers.

| Inbox cmd | Wire | Notes |
|-----------|------|--------|
| `resume` | `run` + re-arm `wait-paused` | same |
| `arm` / `count` / `clear` | break-create / break-clear-all | Apple mapping tokens |
| `dump` | get-memory | map/aux/… |
| `hist` | history-find/read | HST1 → text |
| `scrub` / `frame` | frame ring | ARGB PNG dumps |
| `note` | append to snap | same |
| `quit` | clean shutdown | same |

**Drop for v1:** `vic <frame>` (no VIC ring).

**Snap pack (minimal useful):**

```text
stop=… frame=… cycle=…
get-cpu / get-state
CONFIG regions (name → addr,len) — title-specific; start empty with comments
history-find on CONFIG trace_writes (writes)
optional: current frame PNG + scrub N frames under snap-NNN-frames/
```

**Exit:** windowed emu + watcher; human pause → snap; inbox `arm write $addr`;
resume; hit → snap with history.

### T5 — Docs + optional Makefile stub

- Point [`remote-debug.md`](remote-debug.md) acceptance bar at the new tools.  
- Short usage in this file + root README if user-facing.  
- Optional `make coop` **documentation only** (launch recipe), not railgunner.

---

## c64m → a2m transfer table

| c64m | Transfers? | a2m notes |
|------|------------|-----------|
| `Ctl` framing / cmd / ok | **Yes** | Same wire shape |
| `mem` / break helpers | **Yes — adapt modes** | Apple planes |
| HST1 decode | **Yes** | Shared layout |
| Frame PNG | **Adapt** | ARGB 560×192 |
| `coop_watch` inbox loop | **Yes** | Same idea |
| Snap dump_pack | **Adapt** | No VIC; Apple regions + `get-softswitches` |
| `vic` inbox | **Yes as ss** | `ss` / `softswitches` / `vic` → `get-softswitches` |
| Pepto palette | **No** | Product is ARGB |
| railgunner Makefile | **Reference only** | Workflow proof, not a port |

---

## Implementation notes

| Area | Path |
|------|------|
| New tools | `tools/a2m_control_client.py`, `tools/a2m_coop_watch.py` (names flexible) |
| Emulator binary | `./build/a2m` — do not require wire changes for T1–T4 |
| Protocol | `src/control/` — only touch if a real gap blocks tools |
| c64m reference | `../c64m/tools/c64_control_client.py`, `coop_watch.py` |
| HST1 spec | `../c64m/agents/cpu-flight-recorder.md` (wire section) |
| Wire reference | `../c64m/agents/control-port.md` (shape); a2m source for exact tokens |

**Rules:** tools talk only to the control port (or `runtime_client` is N/A for
Python). No live `apple2_t`. Prefer not changing C unless a wire bug is proven.

**Headless smoke pattern (already proven in epic):**

```bash
./build/a2m --headless --control-port 16520 &
# history-info; run; wait-frame; pause; wait-paused; history-find limit=8 → HST1
```

---

## Out of scope (unless unblocked by tools)

| Item | Why |
|------|-----|
| History UI browser | Separate frontend epic (C5d) |
| Softswitch / beam line ring | C5e; only if snaps insufficient |
| Richer history-find opcode patterns | Nice-to-have once client exists |
| Importing the `am65` subtree into c64m | Separate repository work |

---

## Suggested first commit of the new session

1. Read this file + `status.md` + smoke A2M/6 with a 10-line socket script if unsure.  
2. Add `tools/a2m_control_client.py` (T1) with framing + `cmd`/`ok`/`mem`/`history_find`.  
3. One smoke script or `python3 -c` in the commit message / tools README.  
4. Do **not** start coop_watch until T1 is usable.

---

## Status

| Phase | State |
|-------|--------|
| T0 Inventory | Done (wire matches this note; A2M/6 product-wired) |
| T1 Control client | **Done** — `tools/a2m_control_client.py` (framing, mem modes, BP helpers, frames/ring, HST1, waits, softswitches). Smoke green: hello A2M/6, get-cpu, get-softswitches, run/wait-frame/pause, history-find → HST1 |
| T2 HST1 text | **Done** — `format_hst1_access` / `format_hst1_record` / `format_hst1_page` on `Ctl` |
| T3 ARGB PNG | **Done** — `write_argb_png` (stdlib zlib; product ARGB→RGB, no Pepto) |
| T4 coop_watch | **Done** — `tools/a2m_coop_watch.py` (snap pack, inbox arm/dump/hist/scrub/frame/note; no vic). Headless smoke: pause snap + inbox note/dump/quit |
| T5 Docs / make coop recipe | **Done** — this file Quick start; remote-debug acceptance points here; README control tools; `Makefile` `coop` / `coop-watch` recipe |
