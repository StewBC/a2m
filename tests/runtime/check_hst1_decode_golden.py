#!/usr/bin/env python3
"""Validate an HST1 payload against the synthetic golden used by
test_runtime_history_wire_decode.c (must stay in sync with that fixture)."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from a2m_control_client import Ctl  # noqa: E402


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_hst1_decode_golden.py <payload.bin>", file=sys.stderr)
        return 2
    payload = Path(sys.argv[1]).read_bytes()
    decoded = Ctl.decode_hst1(payload)
    assert decoded["epoch"] == 7
    assert len(decoded["records"]) == 2
    r0, r1 = decoded["records"]
    assert r0["id"] == 13523 and r0["kind"] == 0 and r0["pc"] == 0xFCAC
    assert r0["machine_cycle"] == 1234 and r0["access_truncated"] and r0["anchor_match"]
    assert len(r0["accesses"]) == 2
    assert r0["accesses"][0]["kind"] == 1
    assert r0["accesses"][0]["address"] == 0xC000
    assert r0["accesses"][0]["value"] == 0x22
    assert r1["id"] == 13524 and r1["kind"] == 3
    assert r1["marker_kind"] == 13 and r1["marker_arg0"] == 1
    assert not r1["anchor_match"]
    print("python-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
