# a2m-class video paint (epic record)

**Status:** **Done** (2026-08-05) — LORES / 40+80 text / HGR / DHGR into the beam;
geometry 560×192 end-to-end. Live paint facts: [`video.md`](video.md).  
**Follow-ons:** NTSC artifact campaign (do not start as the main track).
Live product: [`status.md`](status.md).

**Residual (non-blocking):** manual soft-title eyeball. Double-LORES is in both
the beam path and block paint (a2m `unk_apl2_screen_dlores`, plus PAGE2).
**Decision (kept):** a2m-quality modes as a **replaceable paint backend** on the
existing Φ0 beam — not whole-frame dump, not dual surfaces, not remote API first.

Related: [`video.md`](video.md).

---

## Goal

Software that needs real modes (//e 80-col, LORES games, HGR colour, DHGR titles)
is **lookable and usable** at a2m fidelity, while:

1. Beam timing / VBL / floating bus / mid-frame soft switches stay cycle-stepped.  
2. Host still receives ARGB frames via runtime (no UI↔machine pointer).  
3. Paint can later be swapped for artifact/NTSC without rewriting the product shell.

**Fidelity tier after this epic:** “a2m-class paint + beam structure”  
**Later tier:** cycle-accurate NTSC artifact colour (mode-by-mode).

---

## What we keep (do not regress)

| Piece | Location / rule |
|-------|-----------------|
| Φ0 video step | `apple2_video_step` per CPU cycle |
| Timing constants | 65×262, VBL line ≥ 192, H visible 0..39 |
| Floating bus | Scanner latch from active cell |
| Mid-frame PAGE2 / mode | Soft switches sampled at paint time |
| Worker owns machine | Runtime thread only |
| Frame to UI | Mutexed ARGB / `poll_argb_frame` |

Tests that must stay green: `video_beam`, `rom_boot`, `softswitch`.

---

## What we replace / extend

| Mode | Today (`video.c`) | Target (a2m-class) |
|------|-------------------|--------------------|
| 40-col text | Mono green; char ROM | a2m quality (flash/inverse as a2m) |
| LORES | Crude / incomplete | a2m 16-colour cells |
| HGR | Mono white bits | a2m artifact / palette path (as a2m does) |
| 80-col text | Not serious | a2m main/aux interleave |
| DHGR | Not serious | a2m double-hires path |

**Reference implementation:** `../a2m` video/UI paint (domain logic).  
**Not** a wholesale paste of a2m’s single-thread frame dump. Extract **decode +
pixel write** and call it from `paint_at_beam` (or a scanline helper the beam
invokes).

---

## Architecture (seam)

```text
                    ┌─────────────────────────────┐
  soft switches ───►│  mode / page / bank decode  │
  main/aux RAM  ───►│  (shared by all paint tiers)│
                    └─────────────┬───────────────┘
                                  │
                                  ▼
                    ┌─────────────────────────────┐
  beam (h,v)    ───►│  paint backend              │
                    │  tier A: a2m-class (now)    │
                    │  tier B: NTSC artifact (later)│
                    └─────────────┬───────────────┘
                                  │
                                  ▼
                         ARGB framebuffer
                                  │
                                  ▼
                         runtime frame slot → UI
```

### Suggested internal split (names illustrative)

| Unit | Responsibility |
|------|----------------|
| Beam / timing | Unchanged: step H/V, VBL, floating bus |
| `video_mode` helpers | TEXT/MIXED/HIRES/80COL/DHIRES/PAGE from `state_flags` |
| Scanner address | Existing line bases + aux/main for 80/DHGR |
| **Paint backend** | Bytes → ARGB at beam (or row buffer) |
| Host size policy | `display_frame` width/height + frontend upload |

Keep paint **inside `src/machine/`** (no SDL). Frontend only uploads what runtime publishes.

---

## Frame geometry — **decided: 560×192 throughout**

| Item | Decision |
|------|----------|
| Framebuffer | **560×192** ARGB8888 always (not mode-switched dual sizes) |
| Why | DHGR and 80-col need double horizontal resolution; 280 cannot carry them honestly |
| 40-col / HGR / LORES | Painted into the same 560-wide buffer (stretch or pixel-double horizontally as part of the port — **a2m paint must be adjusted**; it historically used a separate wide surface only for double-res) |
| Host | `display_frame`, runtime ARGB slot, frontend texture/CRT all move to 560×192; letterbox / true-aspect scale as today |
| Y | Stay **192** for this epic (no double-height) |

**Not open:** “maybe keep 280 for mono modes.” One size only.

### a2m port note

a2m used a normal surface plus `surface_wide` for 80-col / double-res. Product
will **not** mirror that dual-surface split. When porting paint algorithms from
a2m, rewrite pixel addressing for a **single 560-wide** framebuffer (e.g. HGR
dots occupy two horizontal pixels, or an equivalent a2m-faithful mapping). That
adjustment is expected work in this epic, not a surprise.

---

## Implementation steps (order)

1. **Contract** — **landed**  
   - Geometry is **560×192** (decided).  
   - `video.h` / `display_frame.h` / runtime ARGB slot / frontend upload use 560.  
   - 40-col/HGR/LORES pixel-double horizontally; single code path (no dual surface).

2. **Mode decode table**  
   - Centralize soft-switch → mode (text/lores/hgr/dhgr/mixed, page, 80col).  
   - Unit-test flag combinations (no pixel asserts required).

3. **Port paint by mode** — **done** (a2m as reference, adapted to beam)  
   - LORES: a2m `palette_16`, upper/lower nibble, mixed bottom text.  
   - 40 text: flash/inverse + ALTCHARSET; white on black; dots ×2.  
   - HGR: Holger-Picker colour LUT with neighbour bits; dots ×2.  
   - 80 text: main/aux interleave, 7 host px/glyph.  
   - DHGR: 5-bit window + LORES palette; full line paint at h=0.  
   - DLORES: COL80 + GR; aux then main 7-px cells; `double_aux_map`; PAGE2.  
   - Mixed mode: text window on lines 160..191 (80-col when COL80).

4. **Performance**  
   - Must stay ≥ ~1× real-time on the reference host with paint on
     (`bench_realtime` or wall turbo 1).  
   - Warp may skip paint (existing policy); do not break request-frame.

5. **Validation**  
   - Automated: `video_beam` + smoke that 80col/DHGR flags produce non-black
     pixels (lightweight).  
   - Manual: DOS banner, //e 80-col, a known HGR title, one DHGR title if
     available (Airheart only after SP+DHGR both exist — don’t blame SP for paint).

6. **Document** — **done**  
   - `video.md` fidelity table current.  
   - Epic closed in `status.md`; NTSC artifact colour remains later-tier.

---

## Explicit non-goals for this epic

- Full Sather/AppleWin-class composite NTSC simulation  
- Perfect vapour-lock demos that need sub-cycle colour phase  
- Franklin/Videx 80-col **card**  
- Wiring A2M/1 control  
- Replacing the beam with a2m’s whole-frame dump

---

## After this epic (do not start early)

1. **Artifact / NTSC campaign** — replace paint backend mode-by-mode; keep seam.  
2. **A2M/1** — when golden frames / scripted mode switches become painful by hand.  
3. SmartPort `$C800` host trap, media queues, tools re-graft.

---

## Exit criteria

- [x] LORES, DLORES, 40-col, 80-col, HGR, DHGR algorithms ported (a2m-class); manual soft titles residual  
- [x] Beam tests still green; mid-frame PAGE2 still works  
- [x] Frame path still runtime-owned; no machine pointer in UI  
- [x] Geometry **560×192** landed end-to-end; `video.md` matches
- [x] `bench_realtime` still real-time capable on reference host (~10× free-run smoke)  
- [x] `status.md` updated; this file marked core paint **landed**
