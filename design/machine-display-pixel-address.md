# Machine Display pixel address probe

| Field | Value |
|-------|-------|
| **Author** | swessels |
| **Date** | 2026-08-24 |
| **Status** | Landed |
| **Canonical path** | [`design/machine-display-pixel-address.md`](machine-display-pixel-address.md) |

---

## Overview

When the machine is paused in the debugger (or Inspector), hovering the Machine Display picture should report the Apple II memory address that produced the pixel under the cursor. The status line lives in the existing unused strip under the picture inside the Machine Display window — view size unchanged; only the image area already reserved by `bounds.h - 52` is used for text.

Format (illustrative):

```text
bank: $2000 ofs: $0202 adr: $2202
```

Mode awareness covers text 40/80, LORES, DLORES, HGR, and DHGR (including MIXED bottom text), using soft-switch state locked at pause (and Override flags when Override is on). Mid-frame raster mode changes can lie; that is accepted for v1. The goal is forensic: get an address quickly so Forensics FIND / memory search can answer “what wrote that sprite or glyph.”

---

## Background & Motivation

### Current state

| Layer | Role today |
|-------|------------|
| `src/machine/video.c` / `video.h` | Beam + block paint; `apple2_video_text_line_base`, `apple2_video_hgr_line_offset`; internal `text_host_addr` / `hgr_host_addr` / DLORES page bases |
| `src/machine/display_frame.h` | Host ARGB **560×192** contract only — no mode metadata |
| `src/frontend/frontend.c` (`frontend_draw_display_placeholder`) | Machine Display pane; `nk_layout_row_dynamic(..., bounds.h - 52.0f, 1)` already shrinks the image and leaves a bottom band empty |
| `src/frontend/crt_renderer.c` | Forward CRT map (scanlines, barrel curvature); no reverse map for hit-testing |
| `frontend_debug_state` | Paused soft switches via `apple_state_flags` / `has_apple_flags`; Inspector focus copies the same |

Debugger stop publishes the beam buffer (mid-frame mode switches stay visible). Hardware Override (Misc → Hardware) changes paint-only flags; real `state_flags` and floating bus stay on the machine.

### Pain points

1. Finding “which RAM byte is that pixel” today means manual page math (text line bases, HGR interlacing, aux vs main for DHGR/80-col).
2. Forensics can FIND writes once you know the address, but there is no visual probe from the picture into that address.
3. Scaling the Machine Display large enough for pixel picking does not help without a coordinate → address path.

### Why soft-switch lock (not per-pixel mode)

Exact address under mid-frame raster effects would need paint-time side data (at least per scanline). That is a video-path change and is deferred. Locking TEXT/MIXED/PAGE2/HIRES/COL80/DHIRES/80STORE at pause (or Override) is enough for normal software and for the forensics workflow. MIXED’s bottom four text rows are decoded from flags + `y ≥ 160`, not treated as a “lie.”

---

## Goals & Non-Goals

### Goals

1. While **paused** (live debugger stop or Inspector), hover over the Machine Display **image** shows a one-line address status in the existing bottom band.
2. Decode all host paint modes: text40/80, LORES, DLORES, HGR, DHGR, and MIXED (graphics + bottom text).
3. Report bank base, offset within page, and full address (aux called out when the hovered host pixel comes from aux).
4. Work with **True Aspect** on or off (fit vs fill) — same Apple `(x, y)` once `image_bounds` is known.
5. Prefer CRT curvature still working via inverse of the existing barrel map; acceptable v1 fallback: blank the status when curvature is on.
6. Same behavior in Inspector (flags from the inspected snapshot / Override presentation).
7. Layout stays stable: status strip space always present; text only when paused and hovering the picture.

### Non-Goals

- Per-pixel or per-scanline mode buffers in v1 (raster-demo truthfulness).
- Showing the probe while the machine is running.
- Changing `display_frame` geometry or adding mode planes to the frame ring.
- A separate probe tool window or Misc tab.
- Mandatory click-to-jump / Forensics FIND wiring in v1 (optional follow-ons).

---

## Proposed Design

### Layout

Reuse the existing Machine Display bottom band. Today:

```c
nk_layout_row_dynamic(ui->ctx, bounds.h - 52.0f, 1);
/* image drawn into that row; ~52px below stays empty (grey strip) */
```

v1 draws a mono status label into that reserved area. Do not grow the pane or steal more height from the picture beyond what is already reserved. When not paused or not hovering the image rect, leave the strip blank (or a dim idle placeholder — preference: blank).

```text
+---------------------------+
| Machine Display           |
+---------------------------+
|                           |
|      Apple picture        |
|      (image_bounds)       |
|                           |
+---------------------------+
| bank: $2000 ofs: … adr: … |  ← existing ~52px band
+---------------------------+
```

### When active

| Condition | Status line |
|-----------|-------------|
| Running | Blank |
| Paused, cursor outside image (matte / strip / chrome) | Blank |
| Paused, cursor over image, flags available | Computed address line |
| Paused, Override on | Decode with **override** display flags (what is painted) |
| Paused, curvature on and no inverse yet | Blank or `—` (see CRT) |

### Mouse → Apple pixel

1. Hit-test against the same `image_bounds` used to draw the texture (True Aspect fit or fill).
2. Map mouse → normalized UV in that rect.
3. Map UV → crop space (560×192 active display; respect any crop helpers already used for the texture).
4. If CRT curvature is enabled, apply the **inverse** of `frontend_crt_process`’s barrel term before stepping to Apple `(px, py)`. Outside the curved glass → no address.
5. Clamp / reject if outside 0..559 × 0..191.

Scanlines do not affect addressing. Smoothing does not affect addressing.

### Address decode (soft-switch lock)

Inputs: Apple `(px, py)`, `uint32_t flags` (paused `apple_state_flags`, or Override flags when Override is enabled).

Reuse video’s existing mode predicates and address helpers. Prefer lifting a small public API rather than duplicating DHGR/80STORE rules in the frontend, e.g.:

```c
/* Proposed — names illustrative */
typedef struct apple2_video_pixel_addr {
    uint16_t bank_base;   /* e.g. $0400, $2000, $4000 */
    uint16_t offset;      /* within page */
    uint32_t host_addr;   /* CPU space; aux via high bit / 0x10000 style as video uses today */
    bool from_aux;
    /* optional: mode tag for status wording */
} apple2_video_pixel_addr;

bool apple2_video_pixel_address(
    uint32_t flags,
    uint16_t px,
    uint16_t py,
    apple2_video_pixel_addr *out);
```

Internals already exist as static helpers in `video.c` (`text_host_addr`, `hgr_host_addr`, `dlores_page_bases`, line-is-text/hgr/dhgr). Column within the scanner cell comes from `px` (14 host pixels per scanner column; 80-col / DHGR / DLORES use full width).

**Status line shape (normative enough for v1):**

- Single-bank modes: `bank: $2000 ofs: $0202 adr: $2202`
- Aux-sourced pixel: include aux clearly, e.g. `bank: aux $2000 ofs: $0202 adr: $102202` (match whatever host-address convention the Memory view / Forensics already use for aux — do not invent a second spelling)
- Optional later: both main and aux for the same scanner column when useful

### CRT curvature

`frontend_crt_process` maps output → source:

```text
sxn = nx * (1 + curve * ny^2)
syn = ny * (1 + curve * nx^2)
```

Hover needs source ← screen. Prefer a shared inverse helper (few Newton iterations inside the visible disc) so paint and probe cannot drift. If that is deferred in the first PR, gate the status line off when `crt_effects.curvature` is true rather than shipping a wrong hit-test.

### Inspector

Same draw path and mapping. Flags come from the debug snapshot already copied for Inspector (`apple_state_flags`). Override still wins for presentation decode when enabled.

### Tests (expected)

- Pure decode unit tests: known `(flags, px, py)` → bank/ofs/adr for text40, text80, LORES, DLORES, HGR page1/2, DHGR aux/main columns, MIXED `y=160`.
- Optional: mouse→pixel mapping helper with fixed `image_bounds` (no SDL), True Aspect vs fill.
- CRT inverse: round-trip a grid of points through forward+inverse within a tolerance when curvature is on.

---

## Follow-ons (not v1)

| Item | Notes |
|------|-------|
| Click copies `adr: $xxxx` | Clipboard for pasting into Forensics query |
| Click jumps Memory view | Bank-aware cursor at that address |
| Opt+click → Forensics FIND | Prefill `address=$… access=data-write` |
| Per-scanline mode byte (192 B/frame) | Closes the raster lie without full per-pixel metadata |
| Crosshair overlay | Helps when the pane is heavily scaled |

---

## Honesty / caveats (manual + help)

Document in `manual/manual.md` when the feature lands:

- Address uses soft switches at pause (or Override), not the mode that painted every scanline of the visible frame.
- Mid-frame PAGE2 / TEXT / HIRES / 80COL demos can disagree with the status line on lines painted under a different mode.
- MIXED bottom text is handled correctly from flags.

---

## Open questions (resolved at land)

1. Aux spelling: `bank: aux $… ofs: $… adr: $1….` using video host `+0x10000` (Memory view keeps a separate Aux plane with 16-bit cursors).
2. CRT: ship shared barrel + Newton inverse with the probe (not blank-when-curved).
3. Idle strip: blank (no permanent placeholder).

---

## Implementation sketch (when building)

1. Public `apple2_video_pixel_address` (or equivalent) in `video.*` + unit tests.
2. Frontend: mouse → Apple pixel from `image_bounds` (+ optional CRT inverse).
3. `frontend_draw_display_placeholder`: draw status into the existing bottom band when paused and hovering.
4. Manual blurb under Display / Debugger.
5. Index this doc **active → landed**; fold lasting UI rules into `agents/frontend.md` / `agents/video.md` if any become invariants.
