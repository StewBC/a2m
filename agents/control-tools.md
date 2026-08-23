# Control tools: drive a2m over the control port

**Audience:** agents and humans scripting the emulator (headless or windowed).

**Protocol today:** **A2M/12** (`CONTROL_PROTOCOL_VERSION` in
`src/control/control_protocol.h`). Sessions + unsolicited `state-changed`
events; forensic mode + `exit-forensic`; see [`sessions.md`](sessions.md) ·
[`TM3.md`](TM3.md).

## Source of truth

| Priority | Where |
|----------|--------|
| 1 | **Wire source** — `src/control/control_protocol.*`, `control_dispatch.*` |
| 2 | **Python client** — `tools/a2m_control_client.py` (`Ctl` + module docstring) |
| 3 | **User command catalog** — `manual/manual.md` (remote control section) |
| 4 | **This file** — ops brief / gotchas / workflow |

If this note disagrees with source, **fix the source** and update this file in
the same change. Epic history lives in [`remote-debug.md`](remote-debug.md)
(implementers); do not start there to learn scripting.

Related: [`status.md`](status.md) · [`rules.md`](rules.md) ·
[`testing.md`](testing.md) · [`sessions.md`](sessions.md).

---

## Quick start

```bash
# Headless scripting
./build/a2m --noini --headless --control-port 6510 &
python3 -c "
import sys; sys.path.insert(0, 'tools')
from a2m_control_client import Ctl
c = Ctl(port=6510)
print(c.cmd('hello'))          # name=a2m protocol=A2M/12
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
# or: make coop / make coop-watch
```

| Tool | Role |
|------|------|
| `tools/a2m_control_client.py` | `Ctl` framing, mem, BPs, frames, HST1, waits, **mount/unmount**, ARGB PNG |
| `tools/a2m_coop_watch.py` | pause → snap pack → inbox arm/hist/scrub/resume |

Snaps: `build/debug/snap-NNN.txt` (+ optional `snap-NNN-frames/`).  
Inbox: append lines to `build/debug/coop_inbox`.

---

## Wire inventory (A2M/12)

Framing: `<id> <command> [args]\n` → `ok` / `error` / `data` (+ binary + `\n`).
Unsolicited: `0 event state-changed reason=… session=… cycles=… frame=… epoch=…\n`
(request id **0** is the event channel; `Ctl.cmd` skips these into `Ctl.events`).

| Surface | Commands |
|---------|----------|
| Identity | `hello` `version` `capabilities` `ping` `quit-client` |
| Exec | `run` `pause` `reset` `step-cycle` `step-instruction` `step-over` `step-out` `set-turbo` |
| State | `get-state` `get-cpu` `get-softswitches` `get-memory` / `set-memory` · modes: **map main aux lc1 lc2 rom** · `set-reg` |
| Frame | `get-frame` → ARGB **560×192**, stride = width×4, `format=argb8888` |
| Frame ring | `frame-ring-info` `frame-ring-record` `frame-ring-clear` `get-frame-at frame=\|cycle=` |
| Breakpoints | `break-create` / `break-update` / `break-list` / `break-enable` / `break-clear` / `break-clear-all` / `rearm-oneshots` / `break-exec`; `when=`; access exec/read/write |
| History | `history-info` `history-record` `history-clear` `history-find` `history-next` `history-read` `history-close` → `data history` **HST1** (per TCP session cursor). Marker 13 = `MEDIA_CHANGED`; `arg0` is `0 unknown / 1 guest-write / 2 host-directory`, `arg1` is `(slot<<8)\|device`. Additive; no A2M bump. |
| TimeMachine | Master switch is still INI `[debug] timemachine=0\|1` / CLI `--timemachine` (default **off**). **A2M/12:** `get-state` reports `mode=live\|forensic focus_cycle=N start=… start_arg1=N` (`focus_cycle` = landed `apple2_cycles`; wire `forensic` = time travel). `exit-forensic` (any session) leaves time travel and restores live NOW; mutating verbs fail with `error read-only-forensic`. Tape seek/step **do not exist**. FIND stays (`history-find` / `history-next` / `history-read`). Inspector enter needs checkpoints (film optional). |
| Waits | `wait-paused` `wait-running` `wait-frame` `wait-event` (incl. `assemble-complete` / `assemble-error`) |
| Assembler | `assemble [address=] [run-address=] [auto-run=] [mli-launch=] [reset=] [auto-adjust-segments=] <path>` (deferred) |
| Symbols | `find-symbol <name>` → `ok address=$XXXX name=…` / `not-ready` / `not-found` |
| Input | `key <byte>` (`$8D` / CR → Return) |
| Snapshot | `save-state` `load-state` |
| Media | see below |
| Sessions / inform | TCP client auto-binds one runtime session; mutations publish `state-changed` (open mutation; no lock) |

### Media (Disk II + SmartPort)

```text
mount [kind=diskii|smartport] [slot] [drive] <path>
unmount [kind=diskii|smartport] [slot] [drive]
mount-disk …                         # alias: mount kind=diskii …
select-disk [slot] [drive] <index>    # Disk II queue; index 1-based
set-disk-writable [slot] [drive] <0|1>
```

| Rule | Behaviour |
|------|-----------|
| `kind=` omitted on `mount` | Infer from path: floppy exts (`.nib` `.dsk` `.do` `.woz`) → Disk II; directory / `.hdv` / `.2mg` → SmartPort; **`.po` requires `kind=`** |
| `kind=` omitted on `unmount` | Only if exactly one of Disk II or SmartPort is installed; else `need-kind` |
| `slot` omitted (`0`) | Resolve live card map: Disk II prefer **6** then scan; SmartPort prefer **7** then scan |
| Explicit empty slot | Disk II mount may auto-attach a card; SmartPort requires an installed SP card |
| Ok text | Includes `kind=` `slot=` `drive=` (and `index=` / `writable=` where relevant) |

Aliases for `kind=`: `disk` → diskii; `sp` / `hd` → smartport.

`Ctl.mount(...)` / `Ctl.unmount(...)` / `Ctl.mount_disk(...)` wrap these forms.

---

## Gotchas (script killers)

- **Headless starts paused** — first `wait-paused` may return on the sticky latch; send `run` before free-run waits.
- **`set-turbo`** takes **MHz number**, `max`, or `-1` — not C64-style 1/2/3 ladder indices.
- **`get-memory $C0xx` is not softswitch state** — peeks RAM; use `get-softswitches`.
- **`quit-client`** closes the control socket, not the emulator process.
- BP mapping axes: `ram=map|main|aux`, `c100=map|rom`, `d000=map|lc1|lc2|rom`.
- `break-create` / clear / enable often return **data** (breakpoint list), not bare `ok` — use `Ctl.break_*` helpers.
- BP hit → `stop=breakpoint` on `wait-paused` / `get-state`.
- Peer disconnect mid-wait frees the client slot (no port wedge) and closes the
  bound control session (history cursor slot reusable).
- Addresses: prefix hex with `$` (`mem()` does this). `get-memory` length is **decimal**.
- **Events:** `0 event state-changed …` may arrive at any time; do not treat as
  the next reply for id N. Prefer `Ctl` (`drain_events` / `events` list).
- History FIND/NEXT cursors are **per session**; a step/poke/reset from any
  asker invalidates all cursors (`CURSOR_STALE` → re-FIND).
- **TimeMachine vs record verbs:** `--timemachine` / `[debug] timemachine=1` is the
  product master switch (arms history + frame ring). `history-record` /
  `frame-ring-record` remain the per-recorder controls. While `mode=forensic`,
  `get-memory`/`get-cpu` return THEN; `run`/`set-memory`/`set-reg`/mount/reset
  fail with `read-only-forensic`. `exit-forensic` restores live NOW and does
  **not** auto-resume. There is no enter/land/seek verb on the wire (UI uses
  `runtime_client`). Tape seek/step do not exist. FIND stays.

ctest gate: see [`testing.md`](testing.md) (expect full green after build).

---

## Coop loop (accepted)

```text
Terminal A:  ./build/a2m --control-port 6510
Terminal B:  tools/a2m_coop_watch.py --port 6510
```

1. Human plays; daemon blocks on `wait-paused`.  
2. Pause or BP → `build/debug/snap-NNN.txt`; machine stays frozen.  
3. Inbox lines: `arm write $xxxx`, `hist …`, `scrub 60`, `resume`, …  
4. After `arm`, next BP snap includes history context.

Inbox commands (watcher, not raw wire): `resume`, `arm` / `count` / `clear`,
`dump`, `hist`, `scrub` / `frame`, `note`, `quit`, `ss`/`softswitches`
(→ `get-softswitches`). No VIC ring.

---

## Client surface (`Ctl`)

| Area | Helpers |
|------|---------|
| Core | `cmd`, `ok`, `ok_or_data`, `pipeline` |
| Memory | `mem`, `set_mem`, `get_softswitches` |
| Breakpoints | `break_create`, `break_clear`, `break_enable`, `break_list` |
| Frames | `get_frame`, `frame_ring_info`, `frame_ring_record`, `frame_ring_clear`, `get_frame_at` |
| History | `history_info`, `history_find`, `history_next`, `history_read`, `history_record`, `history_clear`, `history_close`, HST1 formatters |
| Waits | `wait_paused`, `wait_running`, `wait_frame`, `wait_event` |
| Media | `mount`, `unmount`, `mount_disk`, `select_disk`, `set_disk_writable` |
| PNG | `write_argb_png` (stdlib zlib; product ARGB→RGB) |

c64m gold (shape only): `../c64m/tools/c64_control_client.py`,
`../c64m/tools/coop_watch.py`. Do not blind-rename C64 commands.

---

## Status

Tools epic **T0–T5 done**. Protocol id bumps with wire behaviour (`A2M/N`);
keep this file, the client docstring, and `manual/manual.md` in the same change
when media or wait semantics change.
