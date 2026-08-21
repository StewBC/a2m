#!/usr/bin/env python3
"""Cooperative live-debug watcher for a2m (the "coder at your shoulder" loop).

You play a windowed a2m; when you see a glitch you hit the frontend pause key
(F10). This daemon is blocked on `wait-paused`, wakes on that transition,
freezes-in-place, and writes an evidence pack (CPU, state, your named regions,
and recent write history from the flight recorder) to
`build/debug/snap-NNN.txt`. The machine stays frozen while you describe what you
saw in chat and the coder looks around. The coder steers the daemon through a
file inbox: arm a watchpoint, dump more state, scrub the frame ring, or resume.

Nothing here needs an a2m change; pure control-port orchestration on top of
`tools/a2m_control_client.Ctl`. Epic: agents/control-tools.md.

LAUNCH a2m (windowed, real-time so you play at speed and see real pixels):

    ./build/a2m --control-port 6510

Then run this daemon:

    tools/a2m_coop_watch.py --port 6510

WORKFLOW
    1. You play. Daemon prints "watching ...".
    2. You see a glitch -> hit F10 (pause).
    3. Daemon prints "SNAP NNN stop=pause ..." and writes snap-NNN.txt.
       Machine is frozen; daemon polls the inbox.
    4. Describe the glitch in chat. The coder reads snap-NNN.txt + your words,
       then drives the daemon via the inbox file.
    5. Coder writes `resume` (or arms a watchpoint first) -> you play on.

    When a bug is intermittent, arm a watchpoint matching your description
    (e.g. `arm write $C000`), resume, and reproduce: the machine auto-freezes
    at the exact instruction and the next SNAP is the smoking gun.

INBOX PROTOCOL (append one command per line to build/debug/coop_inbox)
    resume                         run and go back to watching
    arm  <access> <addr> [opts]    break-create ... actions=break
    count <access> <addr> [opts]   break-create ... actions=none
    clear                          break-clear-all
    dump <addr> <len> [mode]       get-memory (mode: map|main|aux|lc1|lc2|rom)
    hist <addr> [access] [limit]   history-find -> text into snap
    scrub [count]                  last N ring frames as ARGB PNGs
    frame <n>                      one ring frame + cycle metadata
    note <text...>                 append a line into the snap file
    ss / softswitches / vic        get-softswitches (latched flags; not $C0xx mem)
    quit                           clean shutdown

Commands are consumed only while the machine is paused (after a freeze).
Anything appended while still playing is picked up at the next freeze.
"""
from __future__ import annotations

import argparse
import datetime
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from a2m_control_client import (  # noqa: E402
    Ctl,
    HST1_KIND_DATA_WRITE,
    write_argb_png,
)


# --------------------------------------------------------------------------
# CONFIG -- fill in `regions` and `trace_writes` from the title's symbols.
# Everything else is generic and rarely needs editing.
# --------------------------------------------------------------------------
CONFIG = {
    "port": 6510,
    "out_dir": "build/debug",
    "wait_ms": 600000,  # per wait-paused; loop re-arms on timeout (max 600000)
    # Named game-state regions dumped on every freeze: name -> (addr, length[, mode]).
    "regions": {
        # "zero_page": (0x0000, 0x100, "map"),
        # "text_page1": (0x0400, 0x400, "main"),
        # "hgr_page1": (0x2000, 0x2000, "main"),
    },
    # Addresses whose recent WRITE history is decoded on every freeze:
    # each entry is (label, addr) or (label, addr, end) for a range.
    "trace_writes": [
        # ("softswitches", 0xC000, 0xC07F),
        # ("score", 0x0300),
    ],
    "trace_limit": 48,
}


def hexdump(addr: int, data: bytes) -> str:
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i : i + 16]
        hexs = " ".join(f"{b:02X}" for b in chunk)
        text = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"  {addr + i:04X}  {hexs:<47}  {text}")
    return "\n".join(lines)


class CoopWatch:
    def __init__(self, cfg):
        self.cfg = cfg
        self.out_dir = cfg["out_dir"]
        self.inbox = os.path.join(self.out_dir, "coop_inbox")
        os.makedirs(self.out_dir, exist_ok=True)
        # Socket must outlast a full server-side wait, plus margin.
        sock_timeout = cfg["wait_ms"] / 1000.0 + 30.0
        self.c = Ctl(port=cfg["port"], timeout=sock_timeout)
        self.snap_no = self._next_snap_no()
        self.cur_snap = None  # path of the snap file for the current freeze

    def setup(self):
        self.c.cmd("set-turbo 1")  # real-time, live pixels (never warp for play)
        self.c.cmd("history-record on")
        self.c.cmd("frame-ring-record on")
        info = self._safe(lambda: self.c.history_info())
        ring = self._safe(lambda: self.c.frame_ring_info())
        self._log(
            f"connected port={self.cfg['port']} turbo=1 history=on "
            f"frame-ring=on recorder={info} ring={ring}"
        )

    def _next_snap_no(self):
        n = 0
        for name in os.listdir(self.out_dir) if os.path.isdir(self.out_dir) else []:
            if name.startswith("snap-") and name.endswith(".txt"):
                try:
                    n = max(n, int(name[5:-4]))
                except ValueError:
                    pass
        return n + 1

    def _log(self, msg):
        print(f"[coop] {msg}", flush=True)

    def _safe(self, fn, default=None):
        try:
            return fn()
        except Exception as exc:
            return f"<error: {exc}>" if default is None else default

    def watch_loop(self):
        self._log(
            "watching -- hit F10 (pause) when you see a glitch "
            "(Ctrl-C to stop)"
        )
        while True:
            r = self.c.cmd(f"wait-paused {self.cfg['wait_ms']}")
            if r[0] == "error":
                if "timeout" in (r[1] or ""):
                    continue
                self._log(f"wait-paused error: {r[1]} -- retrying in 1s")
                time.sleep(1.0)
                continue
            meta = Ctl._metadata(r[1])
            self.dump_pack(meta)
            self.paused_command_loop()

    def dump_pack(self, wait_meta):
        path = os.path.join(self.out_dir, f"snap-{self.snap_no:03d}.txt")
        self.cur_snap = path
        stamp = datetime.datetime.now().isoformat(timespec="seconds")
        stop = wait_meta.get("stop", "?")
        lines = [
            f"=== SNAP {self.snap_no:03d}  {stamp}",
            f"stop={stop}  "
            + " ".join(
                f"{k}={v}" for k, v in wait_meta.items() if k != "stop"
            ),
            "",
            "--- CPU",
            "  " + self._ok("get-cpu"),
            "",
            "--- STATE",
            "  " + self._ok("get-state"),
            "",
            "--- SOFTSWITCHES (latched flags; not $C0xx memory)",
            "  " + self._ok("get-softswitches"),
            "",
        ]

        if self.cfg["regions"]:
            lines.append("--- REGIONS")
            for name, spec in self.cfg["regions"].items():
                if len(spec) == 2:
                    addr, length = spec
                    mode = "map"
                else:
                    addr, length, mode = spec
                data = self._safe(
                    lambda a=addr, n=length, m=mode: self.c.mem(a, n, m),
                    default=b"",
                )
                lines.append(f"  {name} (${addr:04X}, {length}, {mode}):")
                lines.append(hexdump(addr, data) if data else "    <read failed>")
            lines.append("")
        else:
            lines.append(
                "--- REGIONS (none configured; edit CONFIG['regions'] "
                "in a2m_coop_watch.py)"
            )
            lines.append("")

        lines.append("--- RECENT WRITES (flight recorder)")
        if self.cfg["trace_writes"]:
            for entry in self.cfg["trace_writes"]:
                lines.append(self._trace_writes(*entry))
        else:
            # Default: last few writes anywhere so empty CONFIG still useful.
            lines.append(self._trace_writes("any", None))
        lines.append("")

        # Optional live frame PNG next to the snap.
        frame_dir = os.path.splitext(path)[0] + "-frames"
        try:
            fr = self.c.get_frame()
            os.makedirs(frame_dir, exist_ok=True)
            png_path = os.path.join(frame_dir, "current.png")
            if write_argb_png(
                png_path,
                fr["width"],
                fr["height"],
                fr["pixels"],
                stride=fr.get("stride") or None,
            ):
                lines.append(f"--- FRAME current -> {png_path}")
                lines.append(
                    f"    width={fr['width']} height={fr['height']} "
                    f"frame={fr.get('frame')}"
                )
                lines.append("")
        except Exception as exc:
            lines.append(f"--- FRAME current: <error: {exc}>")
            lines.append("")

        with open(path, "w") as f:
            f.write("\n".join(lines) + "\n")
        self._log(
            f"SNAP {self.snap_no:03d} stop={stop} "
            f"frame={wait_meta.get('frame', '?')} -> {path}"
        )
        self.snap_no += 1

    def _ok(self, command):
        r = self.c.cmd(command)
        return r[1] if r[0] == "ok" else f"<{r[0]}: {r[1] if len(r) > 1 else ''}>"

    def _trace_writes(self, label, addr, end=None, limit=None):
        limit = limit or self.cfg["trace_limit"]
        opts = dict(
            access="write",
            direction="backward",
            from_="newest",
            limit=limit,
        )
        if addr is not None:
            opts["address"] = addr
            if end is not None:
                opts["end"] = end
        try:
            res = self.c.history_find(**opts)
        except Exception as exc:
            return f"  {label}: history-find failed: {exc}"
        if addr is None:
            span = "(any address)"
        else:
            span = f"${addr:04X}" + (f"..${end:04X}" if end is not None else "")
        out = [
            f"  {label} writes to {span}  (newest first, "
            f"{len(res['records'])} of budget):"
        ]
        lo = addr if addr is not None else 0
        hi = end if end is not None else (addr if addr is not None else 0xFFFF)
        for rec in res["records"]:
            hits = [
                a
                for a in rec["accesses"]
                if lo <= a["address"] <= hi and a["kind"] == HST1_KIND_DATA_WRITE
            ]
            if not hits and addr is not None:
                hits = [a for a in rec["accesses"] if lo <= a["address"] <= hi]
            if hits:
                wr = ", ".join(
                    f"${a['address']:04X}=${a['value']:02X}" for a in hits
                )
            else:
                wr = Ctl.format_hst1_record(rec, compact=True)
                out.append(f"    {wr}")
                continue
            out.append(
                f"    id={rec['id']} pc=${rec['pc']:04X} "
                f"cyc={rec['machine_cycle']} op=${rec['opcode']:02X}  {wr}"
            )
        if not res["records"]:
            out.append("    (no writes in the recorder window)")
        return "\n".join(out)

    def paused_command_loop(self):
        self._log(
            f"paused -- append commands to {self.inbox} "
            f"(arm/count/clear/dump/hist/scrub/frame/note/resume/quit)"
        )
        while True:
            for cmd in self._drain_inbox():
                action = self._handle(cmd)
                if action == "resume":
                    self.c.cmd("run")
                    self._log("resume -> running")
                    return
                if action == "quit":
                    self._log("quit")
                    self.c.close()
                    sys.exit(0)
            # If the user resumed from the frontend, rejoin the watch loop.
            st = Ctl._metadata(
                self._safe(lambda: self._ok("get-state"), default="")
            )
            if st.get("state") == "running":
                self._log("machine resumed from the frontend -> watching")
                return
            time.sleep(0.3)

    def _drain_inbox(self):
        if not os.path.exists(self.inbox):
            return []
        tmp = self.inbox + ".proc"
        try:
            os.rename(self.inbox, tmp)
        except OSError:
            return []
        with open(tmp) as f:
            lines = [ln.strip() for ln in f if ln.strip()]
        os.remove(tmp)
        return lines

    def _handle(self, line):
        self._log(f"inbox: {line}")
        parts = line.split()
        cmd = parts[0].lower()
        try:
            if cmd == "resume":
                return "resume"
            if cmd == "quit":
                return "quit"
            if cmd in ("arm", "count"):
                action = "break" if cmd == "arm" else "none"
                access, addr = parts[1], parts[2]
                extra = " ".join(parts[3:])
                r = self.c.cmd(
                    f"break-create {access} {addr} actions={action}"
                    + (f" {extra}" if extra else "")
                )
                self._log(
                    f"{cmd} {access} {addr} {extra} -> "
                    f"{r[1] if len(r) > 1 else r[0]}"
                )
            elif cmd == "clear":
                self._log("clear -> " + self._ok("break-clear-all"))
            elif cmd == "dump":
                addr = int(parts[1].lstrip("$").replace("0x", ""), 16)
                length = int(parts[2])
                mode = parts[3] if len(parts) > 3 else "map"
                data = self.c.mem(addr, length, mode)
                self._append_snap(
                    f"--- dump ${addr:04X} {length} {mode}\n"
                    + hexdump(addr, data)
                )
            elif cmd == "hist":
                addr = int(parts[1].lstrip("$").replace("0x", ""), 16)
                access = parts[2] if len(parts) > 2 else "write"
                limit = (
                    int(parts[3])
                    if len(parts) > 3
                    else self.cfg["trace_limit"]
                )
                res = self.c.history_find(
                    address=addr,
                    access=access,
                    direction="backward",
                    from_="newest",
                    limit=limit,
                )
                body = Ctl.format_hst1_page(res, compact=True, indent="    ")
                self._append_snap(
                    f"--- hist ${addr:04X} {access} (newest first)\n{body}"
                )
            elif cmd == "scrub":
                count = int(parts[1]) if len(parts) > 1 else 50
                self._scrub_frames(count)
            elif cmd == "frame":
                self._pull_frame(int(parts[1]))
            elif cmd in ("vic", "ss", "softswitches"):
                # Apple get-vic analogue: instantaneous latched softswitch dump.
                text = self._ok("get-softswitches")
                self._append_snap(f"--- softswitches\n  {text}")
            elif cmd == "note":
                self._append_snap("NOTE: " + " ".join(parts[1:]))
            else:
                self._log(f"unknown inbox command: {line!r}")
        except Exception as exc:
            self._log(f"inbox command failed ({line!r}): {exc}")
        return None

    def _ring_info(self):
        try:
            return self.c.frame_ring_info()
        except Exception:
            return {}

    def _frame_at(self, number):
        """Return (metadata dict, ARGB pixels) for one retained frame."""
        try:
            fr = self.c.get_frame_at(frame=number)
            return fr, fr["pixels"]
        except Exception:
            return None, None

    def _scrub_frames(self, count):
        info = self._ring_info()
        if not info or int(info.get("count", 0)) == 0:
            self._log("scrub: frame ring is empty")
            return
        newest = int(info["newest_frame"])
        oldest = int(info["oldest_frame"])
        first = max(oldest, newest - count + 1)
        out_dir = os.path.splitext(self.cur_snap or "frames")[0] + "-frames"
        os.makedirs(out_dir, exist_ok=True)

        written = 0
        for number in range(first, newest + 1):
            fr, pixels = self._frame_at(number)
            if fr is None:
                continue
            path = os.path.join(out_dir, f"frame-{number:06d}.png")
            if write_argb_png(
                path,
                int(fr["width"]),
                int(fr["height"]),
                pixels,
                stride=fr.get("stride") or None,
            ):
                written += 1
        self._append_snap(
            f"--- scrub frames {first}..{newest} -> {out_dir} "
            f"({written} written, ring dropped={info.get('dropped', '?')})"
        )
        self._log(f"scrub: wrote {written} frames to {out_dir}")

    def _pull_frame(self, number):
        fr, pixels = self._frame_at(number)
        if fr is None:
            self._log(f"frame {number}: not in the ring")
            self._append_snap(f"--- frame {number}: not in the retained window")
            return
        out_dir = os.path.splitext(self.cur_snap or "frames")[0] + "-frames"
        os.makedirs(out_dir, exist_ok=True)
        path = os.path.join(out_dir, f"frame-{number:06d}.png")
        write_argb_png(
            path,
            int(fr["width"]),
            int(fr["height"]),
            pixels,
            stride=fr.get("stride") or None,
        )
        meta = fr.get("meta") or {}
        cycle = fr.get("cycle", meta.get("cycle", "?"))
        self._append_snap(
            f"--- frame {number}\n"
            f"    {' '.join(f'{k}={v}' for k, v in meta.items())}\n"
            f"    png={path}\n"
            f"    (search the recorder around cycle {cycle})"
        )
        self._log(f"frame {number} -> {path} (cycle {cycle})")

    def _append_snap(self, text):
        target = self.cur_snap or os.path.join(self.out_dir, "coop_extra.txt")
        with open(target, "a") as f:
            f.write("\n" + text + "\n")
        self._log(f"appended to {target}")


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="a2m cooperative live-debug watcher (A2M/11)"
    )
    ap.add_argument("--port", type=int, default=CONFIG["port"])
    ap.add_argument("--out-dir", default=CONFIG["out_dir"])
    ap.add_argument("--wait-ms", type=int, default=CONFIG["wait_ms"])
    args = ap.parse_args(argv)
    cfg = dict(CONFIG, port=args.port, out_dir=args.out_dir, wait_ms=args.wait_ms)

    watcher = CoopWatch(cfg)
    watcher.setup()
    try:
        watcher.watch_loop()
    except KeyboardInterrupt:
        watcher._log("stopped")
    finally:
        watcher.c.close()


if __name__ == "__main__":
    sys.exit(main() or 0)
