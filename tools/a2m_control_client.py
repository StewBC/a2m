#!/usr/bin/env python3
"""A2M/13 control-port client for a2m.

Debug/introspection helper for driving headless or windowed a2m over its
localhost control port. Structure lifted from c64m's c64_control_client.py;
Apple deltas are intentional (do not blind-rename).

Launch the emulator, e.g.:
    ./build/a2m --headless --control-port 6510

Then:
    from a2m_control_client import Ctl
    c = Ctl(port=6510)
    print(c.cmd("hello"))
    print(c.cmd("get-cpu"))
    print(c.get_softswitches())  # latched flags; not $C0xx via get-memory
    c.cmd("run"); c.cmd("wait-frame 2 5000"); c.cmd("pause")
    c.cmd("wait-paused 2000")
    r = c.history_find(limit=8)
    assert r["records"]  # HST1 decoded
    c.mount("disks/game.nib")                      # kind inferred → diskii
    c.mount("disks/hd.hdv", kind="smartport")      # or omit kind for .hdv
    c.unmount(kind="diskii", drive=0)

GOTCHAS (Apple A2M/13):
  * Identity: hello -> name=a2m protocol=A2M/13
  * Inspector: get-state includes mode=live|inspector focus_cycle= start= start_arg1=.
    leave-inspector restores live NOW (no auto-resume). Mutating verbs fail with
    error read-only-inspector. Tape seek/step are not on the wire.
  * Unsolicited events use request id 0: `0 event state-changed …`.
    cmd()/pipeline() skip them (see drain_events / events list).
  * Assembler: assemble [address=] [run-address=] [auto-run=] [mli-launch=]
    [reset=] [auto-adjust-segments=] <path> (deferred); find-symbol <name>
  * Memory modes: map / main / aux / lc1 / lc2 / rom (not C64 ram/drive8/9)
  * Frames: ARGB 560x192, stride = width*4, format=argb8888 (not Pepto/indexed)
  * Softswitches: get-softswitches (Apple get-vic analogue). get-memory of $C0xx
    peeks RAM only — never the softswitch handler; do not infer video from it
  * No VIC/CIA/drive-cpu product surface
  * Headless often starts paused -> send `run` before free-run waits
  * Turbo: set-turbo <MHz|max|-1> (not C64 ladder indices 1/2/3)
  * Media: mount [kind=diskii|smartport] [slot] [drive] <path>; unmount similarly.
    Omit kind on mount to infer from path (floppy exts → diskii; dir/.hdv/.2mg →
    smartport; .po requires kind=). Omit slot to resolve live card map (Disk II
    prefer 6, SmartPort prefer 7). mount-disk is Disk II alias. See mount()/unmount().
  * Addresses parse base-0: prefix hex with '$' (mem() does this for you)
  * get-memory length is DECIMAL (mem() handles it)
  * quit-client closes the control socket, not the emulator process
  * Breakpoints: access exec|read|write|read-write; optional independent
    ram=map|main|aux, c100=map|rom, d000=map|lc1|lc2|rom; actions=break|none|…
  * break-create/clear/enable return data (breakpoint list); use break_* helpers
  * wait-paused consumes sticky latch after each success (edge for next pause)
  * BP hit → wait-paused/get-state stop=breakpoint (not pause)

Ops brief for agents: agents/control-tools.md (source of truth is still
src/control/ + this client).

Wire format:
  request : "<id> <command> [args]\\n"
  ok      : "<id> ok [text]\\n"
  error   : "<id> error <code> <message>\\n"
  data    : "<id> data <type> <byte_count> [metadata]\\n" + <bytes> + "\\n"
"""
from __future__ import annotations

import argparse
import socket
import struct
import sys
import zlib
from typing import Any, Dict, List, Optional, Sequence, Tuple

MEMORY_MODES = ("map", "main", "aux", "lc1", "lc2", "rom")
# Legacy breakpoint mapping shorthand; independent axes are also supported.
BREAK_MAPPINGS = ("map", "main", "ram", "aux", "lc1", "lc2", "rom")
BREAK_ACCESS = ("exec", "execute", "read", "load", "write", "store", "read-write", "load-store")
# mount/unmount kind= tokens (aliases accepted by the wire).
MEDIA_KINDS = ("diskii", "disk", "smartport", "sp", "hd")

# HST1 bus access kinds (cpu65 / c6510 taxonomy; matches runtime_history.h).
HST1_ACCESS_NAMES = {
    0: "read",
    1: "write",
    2: "opcode",
    3: "operand",
    4: "dummy_read",
    5: "rmw_dummy_write",
    6: "stack_read",
    7: "stack_write",
    8: "vector_read",
}
HST1_RECORD_KINDS = {
    0: "exec",
    1: "marker",
    2: "reserved2",
    3: "reserved3",
}
# Data-write kind used when filtering "writes" in snaps / hist.
HST1_KIND_DATA_WRITE = 1


class Ctl:
    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = 6510,
        timeout: float = 30.0,
    ) -> None:
        self.s = socket.create_connection((host, port), timeout=timeout)
        self.s.settimeout(timeout)
        self.buf = b""
        self.id = 0
        # Unsolicited `0 event …` lines collected here (newest last).
        self.events: List[str] = []

    def _readline(self) -> str:
        while b"\n" not in self.buf:
            chunk = self.s.recv(4096)
            if not chunk:
                raise EOFError("connection closed")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return line.decode("latin1")

    def _readbytes(self, n: int) -> bytes:
        while len(self.buf) < n + 1:  # +1 for trailing newline
            chunk = self.s.recv(65536)
            if not chunk:
                raise EOFError("connection closed")
            self.buf += chunk
        data = self.buf[:n]
        if self.buf[n : n + 1] != b"\n":
            raise ValueError("binary response missing trailing newline")
        self.buf = self.buf[n + 1 :]
        return data

    def _parse_response_header(
        self, line: str, expect_id: Optional[int] = None
    ) -> Tuple[int, tuple]:
        parts = line.split(" ")
        rid = int(parts[0])
        kind = parts[1]
        if kind == "event":
            # Unsolicited / out-of-band (normally id 0).
            return rid, ("event", " ".join(parts[2:]))
        if expect_id is not None:
            assert rid == expect_id, f"id mismatch: {line!r}"
        if kind == "ok":
            return rid, ("ok", " ".join(parts[2:]))
        if kind == "error":
            return rid, ("error", " ".join(parts[2:]))
        if kind == "data":
            # <id> data <type> <byte_count> [metadata...]
            byte_count = int(parts[3])
            meta = " ".join(parts[4:])
            payload = self._readbytes(byte_count)
            return rid, ("data", meta, payload)
        raise ValueError(f"unknown response: {line!r}")

    def _note_event(self, text: str) -> None:
        self.events.append(text)

    def drain_events(self, wait: float = 0.0) -> List[str]:
        """Return and clear queued events; optionally wait briefly for more."""
        if wait > 0:
            old = self.s.gettimeout()
            try:
                self.s.settimeout(wait)
                while True:
                    try:
                        line = self._readline()
                    except (socket.timeout, TimeoutError):
                        break
                    rid, result = self._parse_response_header(line)
                    if result[0] == "event":
                        self._note_event(result[1])
                        continue
                    raise RuntimeError(
                        f"unexpected non-event while draining: id={rid} {result!r}"
                    )
            finally:
                self.s.settimeout(old)
        out = list(self.events)
        self.events.clear()
        return out

    def cmd(self, text: str, payload: Optional[bytes] = None) -> tuple:
        """Send one command; return ('ok', text) | ('error', text) | ('data', meta, body).

        Skips intervening `event` lines (stores them on self.events).
        """
        self.id += 1
        rid = self.id
        self.s.sendall(f"{rid} {text}\n".encode("latin1"))
        if payload is not None:
            self.s.sendall(payload)
            self.s.sendall(b"\n")
        while True:
            line = self._readline()
            got_id, result = self._parse_response_header(line)
            if result[0] == "event":
                self._note_event(result[1])
                continue
            assert got_id == rid, f"id mismatch: {line!r}"
            return result

    def pipeline(self, commands: Sequence[str]) -> List[tuple]:
        """Send many requests without waiting, then collect responses by id.

        Server may complete out of order; returns results in the same order as
        `commands`. Unsolicited events are skipped into self.events.
        """
        ids: List[int] = []
        for text in commands:
            self.id += 1
            rid = self.id
            ids.append(rid)
            self.s.sendall(f"{rid} {text}\n".encode("latin1"))
        by_id: Dict[int, tuple] = {}
        pending = set(ids)
        while pending:
            line = self._readline()
            rid, result = self._parse_response_header(line)
            if result[0] == "event":
                self._note_event(result[1])
                continue
            if rid not in pending:
                raise RuntimeError(f"unexpected response id {rid}: {line!r}")
            by_id[rid] = result
            pending.remove(rid)
        return [by_id[i] for i in ids]

    def ok(self, text: str, payload: Optional[bytes] = None) -> str:
        """Require an `ok` response; raise on error or unexpected data."""
        r = self.cmd(text, payload=payload)
        if r[0] != "ok":
            raise RuntimeError(f"{text!r} -> {r}")
        return r[1]

    def ok_or_data(self, text: str, payload: Optional[bytes] = None) -> tuple:
        """Require success as either `ok` or `data` (break-* returns data list)."""
        r = self.cmd(text, payload=payload)
        if r[0] == "error":
            raise RuntimeError(f"{text!r} -> {r}")
        if r[0] not in ("ok", "data"):
            raise RuntimeError(f"{text!r} -> {r}")
        return r

    # ------------------------------------------------------------------ mem
    def mem(self, addr: int, length: int, mode: str = "map") -> bytes:
        if mode not in MEMORY_MODES:
            raise ValueError(f"memory mode must be one of {MEMORY_MODES}, got {mode!r}")
        # address: parse_u16 base-0, '$' forces hex. length: decimal.
        r = self.cmd(f"get-memory ${addr:04X} {length:d} {mode}")
        if r[0] != "data":
            raise RuntimeError(f"get-memory -> {r}")
        return r[2]

    def get_softswitches(self) -> Dict[str, Any]:
        """Latched softswitch / beam state (Apple analogue of c64m get-vic).

        Returns decoded key=value fields. Bool-like flags are int 0/1; flags
        and motors stay hex strings ($xxxxxxxx). Do **not** use get-memory on
        $C0xx for this — that path peeks RAM and never hits the softswitch
        handler.
        """
        text = self.ok("get-softswitches")
        meta = self._metadata(text)
        out: Dict[str, Any] = dict(meta)
        for key in (
            "TEXT",
            "MIXED",
            "PAGE2",
            "HIRES",
            "COL80",
            "DHIRES",
            "ALTCHAR",
            "80STORE",
            "RAMRD",
            "RAMWRT",
            "ALTZP",
            "LC_READ",
            "LC_WRITE",
            "LC_BANK2",
            "CXROM",
            "C3ROM_OFF",
            "LC_PRE_WRITE",
            "OA",
            "CA",
            "KEY_HELD",
            "line",
            "cycle",
            "frame",
        ):
            if key in out:
                try:
                    out[key] = int(out[key], 0)
                except ValueError:
                    pass
        return out

    def set_mem(self, addr: int, data: bytes, mode: str = "map") -> str:
        if mode not in MEMORY_MODES:
            raise ValueError(f"memory mode must be one of {MEMORY_MODES}, got {mode!r}")
        return self.ok(
            f"set-memory ${addr:04X} {len(data):d} {mode}",
            payload=data,
        )

    # --------------------------------------------------------------- media
    def mount(
        self,
        path: str,
        *,
        kind: Optional[str] = None,
        slot: Optional[int] = None,
        drive: int = 0,
    ) -> str:
        """mount [kind=…] [slot] [drive] <path> → ok text (includes kind/slot/drive).

        Omit kind to let the wire infer from path. Omit slot (or pass None) to
        resolve the installed Disk II / SmartPort card. drive/unit is 0 or 1.
        """
        if not path:
            raise ValueError("path is required")
        if drive not in (0, 1):
            raise ValueError("drive must be 0 or 1")
        tokens: List[str] = ["mount"]
        if kind is not None:
            k = kind.strip().lower()
            if k not in MEDIA_KINDS:
                raise ValueError(f"kind must be one of {MEDIA_KINDS}, got {kind!r}")
            tokens.append(f"kind={k}")
        if slot is not None:
            if slot < 1 or slot > 7:
                raise ValueError("slot must be 1..7 when explicit")
            tokens.append(str(int(slot)))
            tokens.append(str(int(drive)))
        elif drive != 0:
            tokens.append(str(int(drive)))
        tokens.append(path)
        return self.ok(" ".join(tokens))

    def mount_disk(
        self,
        path: str,
        *,
        slot: Optional[int] = None,
        drive: int = 0,
    ) -> str:
        """mount-disk alias → Disk II only (same progressive slot/drive forms)."""
        if not path:
            raise ValueError("path is required")
        if drive not in (0, 1):
            raise ValueError("drive must be 0 or 1")
        tokens: List[str] = ["mount-disk"]
        if slot is not None:
            if slot < 1 or slot > 7:
                raise ValueError("slot must be 1..7 when explicit")
            tokens.append(str(int(slot)))
            tokens.append(str(int(drive)))
        elif drive != 0:
            tokens.append(str(int(drive)))
        tokens.append(path)
        return self.ok(" ".join(tokens))

    def unmount(
        self,
        *,
        kind: Optional[str] = None,
        slot: Optional[int] = None,
        drive: int = 0,
    ) -> str:
        """unmount [kind=…] [slot] [drive] → ok text.

        Omit kind only when exactly one media card type is installed.
        """
        if drive not in (0, 1):
            raise ValueError("drive must be 0 or 1")
        tokens: List[str] = ["unmount"]
        if kind is not None:
            k = kind.strip().lower()
            if k not in MEDIA_KINDS:
                raise ValueError(f"kind must be one of {MEDIA_KINDS}, got {kind!r}")
            tokens.append(f"kind={k}")
        if slot is not None:
            if slot < 1 or slot > 7:
                raise ValueError("slot must be 1..7 when explicit")
            tokens.append(str(int(slot)))
            tokens.append(str(int(drive)))
        elif drive != 0:
            tokens.append(str(int(drive)))
        return self.ok(" ".join(tokens))

    def select_disk(
        self,
        index: int,
        *,
        slot: Optional[int] = None,
        drive: int = 0,
    ) -> str:
        """select-disk [slot] [drive] <index> — Disk II queue; index is 1-based."""
        if index < 1:
            raise ValueError("index must be >= 1")
        if drive not in (0, 1):
            raise ValueError("drive must be 0 or 1")
        tokens: List[str] = ["select-disk"]
        if slot is not None:
            if slot < 1 or slot > 7:
                raise ValueError("slot must be 1..7 when explicit")
            tokens.append(str(int(slot)))
            tokens.append(str(int(drive)))
            tokens.append(str(int(index)))
        elif drive != 0:
            tokens.append(str(int(drive)))
            tokens.append(str(int(index)))
        else:
            tokens.append(str(int(index)))
        return self.ok(" ".join(tokens))

    def set_disk_writable(
        self,
        writable: bool,
        *,
        slot: Optional[int] = None,
        drive: int = 0,
    ) -> str:
        """set-disk-writable [slot] [drive] <0|1> — Disk II write-protect notch."""
        if drive not in (0, 1):
            raise ValueError("drive must be 0 or 1")
        flag = 1 if writable else 0
        tokens: List[str] = ["set-disk-writable"]
        if slot is not None:
            if slot < 1 or slot > 7:
                raise ValueError("slot must be 1..7 when explicit")
            tokens.append(str(int(slot)))
            tokens.append(str(int(drive)))
            tokens.append(str(flag))
        elif drive != 0:
            tokens.append(str(int(drive)))
            tokens.append(str(flag))
        else:
            tokens.append(str(flag))
        return self.ok(" ".join(tokens))

    # ----------------------------------------------------------- breakpoints
    def break_create(
        self,
        access: str,
        address: int,
        *,
        end: Optional[int] = None,
        when: Optional[str] = None,
        actions: str = "break",
        mapping: str = "map",
        ram_mapping: Optional[str] = None,
        c100_mapping: Optional[str] = None,
        d000_mapping: Optional[str] = None,
        enabled: bool = True,
        counter: Optional[int] = None,
        reset: Optional[int] = None,
        extra: Optional[str] = None,
    ) -> Tuple[str, bytes]:
        """break-create … → (metadata, breakpoints payload). Wire returns data."""
        if access not in BREAK_ACCESS:
            raise ValueError(f"access must be one of {BREAK_ACCESS}, got {access!r}")
        if mapping not in BREAK_MAPPINGS:
            raise ValueError(
                f"mapping must be one of {BREAK_MAPPINGS}, got {mapping!r}"
            )
        tokens = [f"break-create {access} ${address:04X}"]
        tokens.append(f"enabled={'1' if enabled else '0'}")
        if end is not None:
            tokens.append(f"end=${end:04X}")
        if actions:
            tokens.append(f"actions={actions}")
        if mapping:
            tokens.append(f"mapping={mapping}")
        if ram_mapping is not None:
            if ram_mapping not in ("map", "main", "aux"):
                raise ValueError("ram_mapping must be map, main, or aux")
            tokens.append(f"ram={ram_mapping}")
        if c100_mapping is not None:
            if c100_mapping not in ("map", "rom"):
                raise ValueError("c100_mapping must be map or rom")
            tokens.append(f"c100={c100_mapping}")
        if d000_mapping is not None:
            if d000_mapping not in ("map", "lc1", "lc2", "rom"):
                raise ValueError("d000_mapping must be map, lc1, lc2, or rom")
            tokens.append(f"d000={d000_mapping}")
        if counter is not None:
            tokens.append(f"counter={int(counter)}")
        if reset is not None:
            tokens.append(f"reset={int(reset)}")
        if when:
            tokens.append(f"when={when}")
        if extra:
            tokens.append(extra.strip())
        r = self.ok_or_data(" ".join(tokens))
        if r[0] == "data":
            return r[1], r[2]
        return r[1], b""

    def break_clear(self, break_id: Optional[int] = None) -> Tuple[str, bytes]:
        """break-clear / break-clear-all → (metadata, breakpoints payload)."""
        if break_id is None:
            r = self.ok_or_data("break-clear-all")
        else:
            r = self.ok_or_data(f"break-clear {int(break_id)}")
        if r[0] == "data":
            return r[1], r[2]
        return r[1], b""

    def break_enable(self, break_id: int, enabled: bool = True) -> Tuple[str, bytes]:
        r = self.ok_or_data(
            f"break-enable {int(break_id)} {1 if enabled else 0}"
        )
        if r[0] == "data":
            return r[1], r[2]
        return r[1], b""

    def break_list(self) -> Tuple[str, bytes]:
        r = self.cmd("break-list")
        if r[0] != "data":
            raise RuntimeError(f"break-list -> {r}")
        return r[1], r[2]

    # --------------------------------------------------------------- frames
    def get_frame(self) -> Dict[str, Any]:
        """Live ARGB frame: {width, height, stride, format, frame, pixels}."""
        r = self.cmd("get-frame")
        if r[0] != "data":
            raise RuntimeError(f"get-frame -> {r}")
        meta = self._metadata(r[1])
        out = {
            "width": int(meta.get("width", "0"), 0),
            "height": int(meta.get("height", "0"), 0),
            "stride": int(meta.get("stride", "0"), 0),
            "format": meta.get("format", "argb8888"),
            "frame": int(meta.get("frame", "0"), 0),
            "pixels": r[2],
            "meta": meta,
        }
        return out

    def frame_ring_info(self) -> Dict[str, Any]:
        text = self.ok("frame-ring-info")
        meta = self._metadata(text)
        for key, value in list(meta.items()):
            try:
                meta[key] = int(value, 0)
            except ValueError:
                pass
        return meta

    def frame_ring_record(self, enabled: bool) -> str:
        return self.ok(f"frame-ring-record {'on' if enabled else 'off'}")

    def frame_ring_clear(self) -> str:
        return self.ok("frame-ring-clear")

    def get_frame_at(
        self,
        *,
        frame: Optional[int] = None,
        cycle: Optional[int] = None,
    ) -> Dict[str, Any]:
        if (frame is None) == (cycle is None):
            raise ValueError("pass exactly one of frame= or cycle=")
        if frame is not None:
            cmd = f"get-frame-at frame={int(frame)}"
        else:
            cmd = f"get-frame-at cycle={int(cycle)}"
        r = self.cmd(cmd)
        if r[0] != "data":
            raise RuntimeError(f"{cmd!r} -> {r}")
        meta = self._metadata(r[1])
        return {
            "width": int(meta.get("width", "0"), 0),
            "height": int(meta.get("height", "0"), 0),
            "stride": int(meta.get("stride", "0"), 0),
            "format": meta.get("format", "argb8888"),
            "frame": int(meta.get("frame", "0"), 0),
            "cycle": int(meta.get("cycle", "0"), 0) if "cycle" in meta else None,
            "pixels": r[2],
            "meta": meta,
        }

    # ---------------------------------------------------------------- waits
    def wait_paused(self, timeout_ms: int = 60000) -> Dict[str, Any]:
        """Wait until paused. Sticky latch is consumed after each success, so a
        second call blocks until a *new* pause edge (run then pause/BP).
        Headless startup is paused once (first call returns immediately).
        """
        text = self.ok(f"wait-paused {int(timeout_ms)}")
        meta = self._metadata(text)
        if "frame" in meta:
            meta["frame"] = int(meta["frame"], 0)
        return meta

    def wait_running(self, timeout_ms: int = 60000) -> Dict[str, Any]:
        text = self.ok(f"wait-running {int(timeout_ms)}")
        return self._metadata(text)

    def wait_frame(self, delta: int = 1, timeout_ms: int = 5000) -> Dict[str, Any]:
        text = self.ok(f"wait-frame {int(delta)} {int(timeout_ms)}")
        meta = self._metadata(text)
        for key in ("frame", "delta"):
            if key in meta:
                meta[key] = int(meta[key], 0)
        return meta

    def wait_event(self, name: str, timeout_ms: int = 60000) -> Dict[str, Any]:
        text = self.ok(f"wait-event {name} {int(timeout_ms)}")
        return self._metadata(text)

    # -------------------------------------------------------------- history
    @staticmethod
    def _metadata(text: str) -> Dict[str, str]:
        result: Dict[str, str] = {}
        if not text:
            return result
        for token in text.split():
            if "=" not in token:
                raise ValueError(f"malformed response metadata: {token!r}")
            key, value = token.split("=", 1)
            result[key] = value
        return result

    @staticmethod
    def decode_hst1(payload: bytes) -> Dict[str, Any]:
        """Decode and validate one HST1 version-1 history payload."""
        if len(payload) < 24 or payload[:4] != b"HST1":
            raise ValueError("invalid HST1 magic/header")
        version, flags = struct.unpack_from("<HH", payload, 4)
        epoch, count, reserved = struct.unpack_from("<QII", payload, 8)
        if version != 1:
            raise ValueError(f"unsupported HST1 version {version}")
        if flags != 0 or reserved != 0:
            raise ValueError("nonzero HST1 reserved header field")

        records = []
        offset = 24
        for _ in range(count):
            if offset + 48 > len(payload):
                raise ValueError("truncated HST1 record header")
            record_size = struct.unpack_from("<H", payload, offset)[0]
            kind = payload[offset + 2]
            record_flags = payload[offset + 3]
            access_count = payload[offset + 35]
            expected_size = 48 + access_count * 8
            if kind > 3:
                raise ValueError(f"invalid HST1 record kind {kind}")
            if record_size != expected_size or offset + record_size > len(payload):
                raise ValueError("invalid HST1 record size/access count")
            if struct.unpack_from("<H", payload, offset + 38)[0] != 0:
                raise ValueError("nonzero HST1 record reserved field")

            record = {
                "kind": kind,
                "partial": bool(record_flags & 0x01),
                "access_truncated": bool(record_flags & 0x02),
                "anchor_match": bool(record_flags & 0x04),
                "timing_truncated": bool(record_flags & 0x08),
                "timeline": struct.unpack_from("<I", payload, offset + 4)[0],
                "id": struct.unpack_from("<Q", payload, offset + 8)[0],
                "machine_cycle": struct.unpack_from("<Q", payload, offset + 16)[0],
                "pc": struct.unpack_from("<H", payload, offset + 24)[0],
                "a": payload[offset + 26],
                "x": payload[offset + 27],
                "y": payload[offset + 28],
                "sp": payload[offset + 29],
                "p": payload[offset + 30],
                "opcode": payload[offset + 31],
                "operands": bytes(payload[offset + 32 : offset + 34]),
                "instruction_length": payload[offset + 34],
                "marker_kind": struct.unpack_from("<H", payload, offset + 36)[0],
                "marker_arg0": struct.unpack_from("<I", payload, offset + 40)[0],
                "marker_arg1": struct.unpack_from("<I", payload, offset + 44)[0],
                "accesses": [],
            }
            access_offset = offset + 48
            for _access in range(access_count):
                address, cycle_offset = struct.unpack_from("<HH", payload, access_offset)
                value = payload[access_offset + 4]
                access_kind = payload[access_offset + 5]
                access_reserved = struct.unpack_from("<H", payload, access_offset + 6)[0]
                if access_kind > 8 or access_reserved != 0:
                    raise ValueError("invalid HST1 access entry")
                record["accesses"].append(
                    {
                        "address": address,
                        "cycle_offset": cycle_offset,
                        "value": value,
                        "kind": access_kind,
                    }
                )
                access_offset += 8
            records.append(record)
            offset += record_size
        if offset != len(payload):
            raise ValueError("trailing bytes in HST1 payload")
        return {"epoch": epoch, "records": records}

    @staticmethod
    def format_hst1_access(access: Dict[str, Any]) -> str:
        """One access as 'write $C000=xx @+1'."""
        kind = access.get("kind", 0)
        name = HST1_ACCESS_NAMES.get(kind, f"kind{kind}")
        addr = int(access.get("address", 0))
        value = int(access.get("value", 0))
        off = int(access.get("cycle_offset", 0))
        return f"{name} ${addr:04X}={value:02X} @+{off}"

    @staticmethod
    def format_hst1_record(record: Dict[str, Any], *, compact: bool = False) -> str:
        """One HST1 record as a single human line for snap files / agents.

        Example:
          id=13523 pc=$FCAC a=00 x=00 y=00 sp=F2 p=24 opcode=$D0
          cyc=1234 accesses: write $C000=xx @+1, read $FCAD=yy @+0
        """
        kind = int(record.get("kind", 0))
        kind_name = HST1_RECORD_KINDS.get(kind, f"kind{kind}")
        flags = []
        if record.get("partial"):
            flags.append("partial")
        if record.get("access_truncated"):
            flags.append("access_truncated")
        if record.get("anchor_match"):
            flags.append("anchor")
        if record.get("timing_truncated"):
            flags.append("timing_truncated")
        flag_s = (" [" + ",".join(flags) + "]") if flags else ""

        if kind != 0:
            # Marker / non-exec: surface marker fields.
            return (
                f"id={record.get('id')} kind={kind_name}{flag_s} "
                f"cyc={record.get('machine_cycle')} "
                f"marker={record.get('marker_kind')} "
                f"arg0={record.get('marker_arg0')} arg1={record.get('marker_arg1')}"
            )

        accesses = record.get("accesses") or []
        if compact:
            # Prefer data writes; fall back to all non-opcode/operand fetches.
            prefer = [a for a in accesses if a.get("kind") == HST1_KIND_DATA_WRITE]
            shown = prefer if prefer else [
                a for a in accesses if a.get("kind") not in (2, 3)
            ]
            if not shown:
                shown = accesses
        else:
            shown = accesses
        acc_s = ", ".join(Ctl.format_hst1_access(a) for a in shown)
        acc_part = f" accesses: {acc_s}" if acc_s else ""
        return (
            f"id={record.get('id')} pc=${int(record.get('pc', 0)):04X} "
            f"a={int(record.get('a', 0)):02X} x={int(record.get('x', 0)):02X} "
            f"y={int(record.get('y', 0)):02X} sp={int(record.get('sp', 0)):02X} "
            f"p={int(record.get('p', 0)):02X} opcode=${int(record.get('opcode', 0)):02X} "
            f"cyc={record.get('machine_cycle')}{flag_s}{acc_part}"
        )

    @staticmethod
    def format_hst1_page(
        page: Dict[str, Any],
        *,
        compact: bool = False,
        indent: str = "",
    ) -> str:
        """Format a history_find/read/next result (metadata + records) as text."""
        lines = [
            f"{indent}epoch={page.get('epoch')} count={page.get('count')} "
            f"cursor={page.get('cursor')} more={page.get('more')} "
            f"oldest={page.get('oldest')} newest={page.get('newest')}"
        ]
        for rec in page.get("records") or []:
            lines.append(indent + Ctl.format_hst1_record(rec, compact=compact))
        if not page.get("records"):
            lines.append(f"{indent}(no records)")
        return "\n".join(lines)

    def history_info(self) -> Dict[str, Any]:
        result = self.cmd("history-info")
        if result[0] != "ok":
            raise RuntimeError(f"history-info -> {result}")
        metadata: Dict[str, Any] = self._metadata(result[1])
        for key, value in list(metadata.items()):
            if key != "reason":
                try:
                    metadata[key] = int(value, 0)
                except ValueError:
                    pass
        return metadata

    def _history_result(self, command: str) -> Dict[str, Any]:
        result = self.cmd(command)
        if result[0] != "data":
            raise RuntimeError(f"{command!r} -> {result}")
        metadata: Dict[str, Any] = self._metadata(result[1])
        for key in ("epoch", "count", "cursor", "more", "oldest", "newest"):
            if key not in metadata:
                raise ValueError(f"missing history metadata {key}")
            metadata[key] = int(metadata[key], 0)
        decoded = self.decode_hst1(result[2])
        if decoded["epoch"] != metadata["epoch"]:
            raise ValueError("HST1 epoch disagrees with response metadata")
        if len(decoded["records"]) != metadata["count"]:
            raise ValueError("HST1 count disagrees with response metadata")
        metadata["records"] = decoded["records"]
        metadata["payload"] = result[2]
        return metadata

    def history_find(self, **options: Any) -> Dict[str, Any]:
        """history-find [key=value ...].

        Keys accepted by A2M (shared C parser): pc, address, access, direction,
        limit, from, epoch, timeline, cycle, value, opcodes.

        access names: execute|fetch, opcode, operand, data-read|read,
        data-write|write, data, dummy-read, rmw-dummy-write, stack-read,
        stack-write, vector-read. opcodes is 1..32 comma-separated bytes with
        optional ? nibble wildcards (e.g. A9,??,8D). value hex forms may use
        ? masks ($2?, 0x??). cycle ranges use '-' (decimal/0x, not $).
        """
        tokens = []
        for key, value in options.items():
            wire_key = key[:-1] if key.endswith("_") else key
            if wire_key == "direction" and value not in ("forward", "backward"):
                raise ValueError("direction must be forward or backward")
            if isinstance(value, (list, tuple)):
                value = ",".join(str(item) for item in value)
            tokens.append(f"{wire_key}={value}")
        command = "history-find"
        if tokens:
            command += " " + " ".join(tokens)
        return self._history_result(command)

    def history_next(self, cursor: int, limit: int = 64) -> Dict[str, Any]:
        return self._history_result(f"history-next {int(cursor)} limit={int(limit)}")

    def history_read(
        self,
        record_id: int,
        epoch: Optional[int] = None,
        before: int = 32,
        after: int = 8,
    ) -> Dict[str, Any]:
        options = []
        if epoch is not None:
            options.append(f"epoch={int(epoch)}")
        options.extend((f"before={int(before)}", f"after={int(after)}"))
        return self._history_result(
            f"history-read {int(record_id)} " + " ".join(options)
        )

    def history_record(self, enabled: bool) -> str:
        return self.ok(f"history-record {'on' if enabled else 'off'}")

    def history_clear(self) -> str:
        return self.ok("history-clear")

    def history_close(self, cursor: int) -> str:
        return self.ok(f"history-close {int(cursor)}")

    def close(self) -> None:
        try:
            self.s.close()
        except Exception:
            pass


def write_argb_png(
    path: str,
    width: int,
    height: int,
    pixels: bytes,
    *,
    stride: Optional[int] = None,
) -> bool:
    """Write product ARGB8888 pixels as a 24-bit RGB PNG (stdlib zlib/struct).

    Wire/layout is little-endian ARGB in memory as B,G,R,A byte order on LE hosts
    when stored as uint32 0xAARRGGBB — same as the emulator display_frame pack.
    Accept either packed width*4 rows or an explicit stride (bytes per row).
    Returns False if the buffer is too short.
    """
    if width <= 0 or height <= 0 or not pixels:
        return False
    row_stride = stride if stride is not None else width * 4
    if row_stride < width * 4:
        return False
    need = row_stride * (height - 1) + width * 4
    if len(pixels) < need:
        return False

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload))
            + tag
            + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
        )

    # Convert ARGB8888 (A R G B in big-endian value / B G R A on LE memory for
    # uint32 0xAARRGGBB) → RGB for PNG. Prefer the LE memory layout used by
    # a2m display frames: bytes [B, G, R, A] per pixel.
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter None
        row = memoryview(pixels)[y * row_stride : y * row_stride + width * 4]
        for x in range(width):
            o = x * 4
            b, g, r = row[o], row[o + 1], row[o + 2]
            # If host stored as A,R,G,B instead, alpha channel would often be 0xFF
            # in byte0; product path is BGRA for ARGB uint32 on LE — keep BGRA.
            raw.extend((r, g, b))

    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(bytes(raw), 6))
        + chunk(b"IEND", b"")
    )
    with open(path, "wb") as f:
        f.write(png)
    return True


def main(argv: Optional[Sequence[str]] = None) -> int:
    ap = argparse.ArgumentParser(description="a2m control client (A2M/13)")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=6510)
    ap.add_argument("--timeout", type=float, default=30.0)
    ap.add_argument(
        "commands",
        nargs="*",
        help='commands without id, e.g. hello pause "get-memory $0 16 map"',
    )
    args = ap.parse_args(argv)

    ctl = Ctl(host=args.host, port=args.port, timeout=args.timeout)
    try:
        if not args.commands:
            print(ctl.cmd("hello"))
            print(ctl.cmd("get-cpu"))
            return 0

        for c in args.commands:
            r = ctl.cmd(c)
            if r[0] == "data":
                print(f"data {r[1]}  ({len(r[2])} bytes)")
                # history payloads start with HST1
                if r[2][:4] == b"HST1":
                    decoded = Ctl.decode_hst1(r[2])
                    print(f"  HST1 epoch={decoded['epoch']} records={len(decoded['records'])}")
                    for rec in decoded["records"][:16]:
                        print("   ", Ctl.format_hst1_record(rec, compact=True))
                elif len(r[2]) <= 64:
                    print(" ", r[2].hex())
            else:
                print(r)
        return 0
    finally:
        ctl.close()


if __name__ == "__main__":
    sys.exit(main())
