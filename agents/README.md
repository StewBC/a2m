# a2m agent handoff

Implementation brief for agents. **Source is authoritative** — if a doc and
the code disagree, fix the doc (or the code) in the same change. These files
describe the product as it is, not how it was built.

## Product

C99 **Apple ][+ / //e Enhanced** emulator (version **3.0.0**).

This tree is a descendant of V1–V2 notes in [`doc/a2m-v1-2/`](../doc/a2m-v1-2/README-v1-2.md).
It also shares a lot of DNA with the sibling C64 emulator **c64m**: the debugger
UI, remote control port, and Inspector / time-travel shape are intentionally
similar, so muscle memory transfers. Treat that as ancestry, not a second
source of truth — do not open c64m to decide how a2m should work.

**am65** (`src/tools/am65/`) is the shared 6502 / 65C02 assembler (in-emulator
Assembler tab and standalone `am65` CLI). It is designed to be the same
assembler other products consume. See `src/tools/am65/README.md`.

## Read order

1. [`status.md`](status.md) — what the product is now
2. [`rules.md`](rules.md) — constraints that must not be broken
3. [`testing.md`](testing.md) — build + ctest gate

Then open the component note for the area you are changing:

[`machine.md`](machine.md) · [`video.md`](video.md) · [`disk.md`](disk.md) ·
[`runtime.md`](runtime.md) · [`frontend.md`](frontend.md) ·
[`breakpoints.md`](breakpoints.md) · [`snapshots.md`](snapshots.md) ·
[`timemachine.md`](timemachine.md) · [`control-tools.md`](control-tools.md)

Parked / not-now work lives in [`known-gaps.md`](known-gaps.md). Read it only
if asked.

## Manual (users, not agents)

[`manual/manual.md`](../manual/manual.md) is the user-facing manual. It is also
compiled into the in-emulator help overlay (`tools/gen_help.py` →
`src/frontend/help_view.*`).

Before editing `manual/manual.md`, read [`manual/HELP_MARKDOWN.md`](../manual/HELP_MARKDOWN.md)
(ASCII-only subset; the help renderer is not full Markdown). If you add or
change a user-facing feature, update `manual/manual.md` in the same change.
Do not put agent notes or `agents/` links in the manual.
