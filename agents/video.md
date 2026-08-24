# Video

## Files

| File | Role |
|------|------|
| `src/machine/video.h` / `video.c` | Beam, scanner, paint, floating bus |
| `src/machine/display_frame.h` | Host frame contract (static-asserted to `APPLE2_VIDEO_*`) |
| `tests/machine/test_video_beam.c` | Timing + mode coverage |
| `tests/machine/test_video_block_paint.c` | Full-frame block paint |

## Timing (NTSC Φ0)

| Constant | Value |
|----------|-------|
| Cycles/line | 65 |
| Lines/frame | 262 |
| Cycles/frame | 17030 |
| Visible lines | 0..191 |
| VBL (`$C019`) | line ≥ 192 |
| H active | 0..39 scanner columns (14 host pixels each → 560) |
| Framebuffer | **560×192** ARGB8888 always |

## Two paint paths

1. **Beam** (finite turbo): each CPU Φ0 → `apple2_video_step` → paint cell →
   advance H → wrap V → frame ready.
2. **Block** (max turbo, snapshot restore, stop-path with Override):
   `apple2_video_paint_full_frame`. Does not advance the beam.

**A-lite** (`apple2_video_advance_alite`): O(1) H/V/`$C019` with no paint and
no scanner, so the max instruction loop stays flat-out. Leave max with
`apple2_video_reseed_from_cycles`. After max, floating bus is stale until paint
resumes.

Floating bus: active video = scanner byte; blanking = last latch.

## Paint quality (today)

| Mode | Quality |
|------|---------|
| Text 40 | Flash/inverse; white (or phosphor) on black; dots ×2 into 560 |
| Text 80 | Main/aux interleave; 7 host px/glyph into 560 |
| LORES | 16-colour cells; Mono = 16 hand-spaced phosphor fills (not Rec.601) |
| DLORES | Aux/main 7-px half-columns + `double_aux_map`; PAGE2; mixed → 80-col text |
| HGR | Colour: Holger-Picker neighbour LUT, dots ×2. Mono: 7 data bits on/off, bit 7 ignored |
| DHGR | Colour: 5-bit window + LORES palette. Mono: the same 560 bits on/off |

Host monitor (`apple2_video_set_monitor`): Colour vs discrete Mono. Phosphor
White / Green / Amber. **Not snapshotted.** CLI `--video-display`, INI
`[Video] colour` + `mono_mode`, Shift+Opt+C, Configure → Emulator.

Debugger **Override** (Hardware tab) is a RAM view of a page: it affects video
paint only. Real soft switches, mapping, floating bus, and snapshots keep
actual machine state. On debugger stop: Override on dumps that page and
publishes; Override off publishes the **beam buffer** so a mid-frame mode
switch stays visible.

## Tests

VBL, floating bus varies by column, mid-frame PAGE2, boot paints pixels —
`video_beam`. Block path — `video_block_paint`. Stop-path CRT —
`runtime_display_stop`.
