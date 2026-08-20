# Control tools: drive a2m over the control port

**Audience:** agents and humans scripting the emulator (headless or windowed).

**Protocol today:** **A2M/10** (`CONTROL_PROTOCOL_VERSION` in
`src/control/control_protocol.h`).

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
[`testing.md`](testing.md).

---

## Quick start

```bash
# Headless scripting
./build/a2m --noini --headless --control-port 6510 &
python3 -c "
import sys; sys.path.insert(0, 'tools')
from a2m_control_client import Ctl
c = Ctl(port=6510)
print(c.cmd('hello'))          # name=a2m protocol=A2M/10
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

## Wire inventory (A2M/10)

Framing: `<id> <command> [args]\n` → `ok` / `error` / `data` (+ binary + `\n`).

| Surface | Commands |
|---------|----------|
| Identity | `hello` `version` `capabilities` `ping` `quit-client` |
| Exec | `run` `pause` `reset` `step-cycle` `step-instruction` `step-over` `step-out` `set-turbo` |
| State | `get-state` `get-cpu` `get-softswitches` `get-memory` / `set-memory` · modes: **map main aux lc1 lc2 rom** · `set-reg` |
| Frame | `get-frame` → ARGB **560×192**, stride = width×4, `format=argb8888` |
| Frame ring | `frame-ring-info` `frame-ring-record` `frame-ring-clear` `get-frame-at frame=\|cycle=` |
| Breakpoints | `break-create` / `break-update` / `break-list` / `break-enable` / `break-clear` / `break-clear-all` / `rearm-oneshots` / `break-exec`; `when=`; access exec/read/write |
| History | `history-info` `history-record` `history-clear` `history-find` `history-next` `history-read` `history-close` → `data history` **HST1** |
| Waits | `wait-paused` `wait-running` `wait-frame` `wait-event` (incl. `assemble-complete` / `assemble-error`) |
| Assembler | `assemble [address=] [run-address=] [auto-run=] [mli-launch=] [reset=] [auto-adjust-segments=] <path>` (deferred) |
| Symbols | `find-symbol <name>` → `ok address=$XXXX name=…` / `not-ready` / `not-found` |
| Input | `key <byte>` (`$8D` / CR → Return) |
| Snapshot | `save-state` `load-state` |
| Media | see below |

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
- Peer disconnect mid-wait frees the client slot (no port wedge).
- Addresses: prefix hex with `$` (`mem()` does this). `get-memory` length is **decimal**.

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
