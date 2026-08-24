# Video (current)

Paint epic closed: [`video-paint.md`](video-paint.md).

## Files

| File | Role |
|------|------|
| `src/machine/video.h` / `video.c` | Beam, scanner, paint, floating bus |
| `src/machine/display_frame.h` | Host frame contract size |
| `tests/machine/test_video_beam.c` | Unit coverage (timing, LORES, DLORES, HGR, 80-col, DHGR) |

## Timing (NTSC Φ0)

| Constant | Value |
|----------|-------|
| Cycles/line | 65 |
| Lines/frame | 262 |
| Cycles/frame | 17030 |
| Visible lines | 0..191 |
| VBL (`$C019`) | line ≥ 192 |
| H active | 0..39 scanner columns (14 host pixels each → 560) |
| Framebuffer | **560×192** ARGB8888 always ([`video-paint.md`](video-paint.md))

## Beam

Each CPU Φ0 → `apple2_video_step`: paint cell → advance H → wrap V → frame ready.

Floating bus: active video = scanner byte; blanking = last latch.

## Paint quality (today)

| Mode | Quality |
|------|---------|
| Text 40 | a2m-class flash/inverse; white (or phosphor) on black; dots ×2 into 560 |
| Text 80 | a2m main/aux interleave; 7 host px/glyph into 560 |
| LORES | a2m 16-colour cells; Mono = 16 hand-spaced phosphor fills (not Rec.601) |
| DLORES | a2m aux/main 7-px half-columns + `double_aux_map`; PAGE2; mixed → 80-col text |
| HGR | Colour: Holger-Picker neighbour LUT, dots ×2. Mono: 7 data bits on/off, bit 7 ignored |
| DHGR | Colour: 5-bit window + LORES palette. Mono: the same 560 bits on/off |

Host monitor (`apple2_video_set_monitor`): Colour vs discrete Mono. Phosphor White / Green / Amber. Not snapshotted. CLI `--video-display`, INI `[Video] colour` + `mono_mode`, Shift+Opt+C, Configure → Emulator.

## Tests

VBL, floating bus varies by column, mid-frame PAGE2, boot paints pixels — `video_beam`.
