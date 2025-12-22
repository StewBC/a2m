#!/usr/bin/env python3
"""
aw-tapper.py

Learn per-phase FIR taps from an AppleWin "Color (NTSC Monitor) [Stepping]" BMP.

This version is intentionally "single-path" and deterministic:
- Segments AppleWin stepping BMP bands by row-mean color transitions (no black gaps assumed).
- Reads one bitstring per band (top-to-bottom) from patterns.txt (first token on each line).
- Builds an input +/-1 waveform by repeating the bitstring across the band's pixel width.
- Supports:
    * optional global bit reversal (default ON) via --no_reverse_bits to disable
    * optional global bit rotation (default 0) via --bit_rot
- Fits per-phase FIR (default 4 phases) in RGB or YIQ space via ridge regression.
- Brute-forces phase_bias in [0..phases-1] and picks best.
- Emits C arrays (firY/firI/firQ or firR/firG/firB) plus per-phase biases and phase_bias.

Usage example:
  python aw-tapper.py --image AppleWin_ScreenShot_000000000.bmp --patterns patterns.txt --space rgb --taps 17 --phases 8 --lam 1e-3 --emit learned_rgb_auto_bw.c
"""

import argparse
import math
from typing import List, Tuple, Dict

import numpy as np
from PIL import Image


# ----------------------------
# Color space helpers (optional)
# ----------------------------

# forward transform:
# r = Y + 0.956 I + 0.621 Q
# g = Y - 0.272 I - 0.647 Q
# b = Y - 1.106 I + 1.703 Q
A_RGB_FROM_YIQ = np.array([
    [1.0,  0.956,  0.621],
    [1.0, -0.272, -0.647],
    [1.0, -1.106,  1.703],
], dtype=np.float64)

A_YIQ_FROM_RGB = np.linalg.inv(A_RGB_FROM_YIQ)


def rgb_to_yiq(rgb: np.ndarray) -> np.ndarray:
    """
    rgb: (..., 3) in [0,1]
    returns yiq: (..., 3)
    """
    flat = rgb.reshape(-1, 3).astype(np.float64)
    yiq = flat @ A_YIQ_FROM_RGB.T
    return yiq.reshape(rgb.shape)


# ----------------------------
# Image parsing
# ----------------------------

def load_image_rgb01(path: str) -> np.ndarray:
    im = Image.open(path).convert("RGB")
    arr = np.asarray(im, dtype=np.float32) / 255.0
    return arr


def find_nonblack_bbox(img: np.ndarray, thr: float = 0.02) -> Tuple[int, int, int, int]:
    """
    Find bounding box of pixels whose max(R,G,B) > thr.
    Returns (x0, y0, x1, y1) with x1/y1 exclusive.
    """
    mask = (img.max(axis=2) > thr)
    ys, xs = np.where(mask)
    if xs.size == 0:
        raise RuntimeError("No non-black pixels found. Try lowering --thr or use a cropped image.")
    x0, x1 = int(xs.min()), int(xs.max()) + 1
    y0, y1 = int(ys.min()), int(ys.max()) + 1
    return x0, y0, x1, y1


def segment_bands_by_color_steps(strip: np.ndarray, diff_thr: float = 0.03) -> List[Tuple[int, int]]:
    """
    Segment horizontal bands based on row-mean color changes.
    AppleWin stepping BMPs typically have no black gaps between bands, but show strong
    transitions at band boundaries.
    Returns list of (y0, y1) bands.
    """
    h, w, _ = strip.shape
    row_mean = strip.mean(axis=1)  # (H,3)
    d = np.linalg.norm(np.diff(row_mean, axis=0), axis=1)  # (H-1,)

    edges = np.where(d > diff_thr)[0]  # boundary between y and y+1
    if edges.size == 0:
        return [(0, h)]

    bounds = [0] + (edges + 1).tolist() + [h]
    bands: List[Tuple[int, int]] = []
    for i in range(len(bounds) - 1):
        y0, y1 = int(bounds[i]), int(bounds[i + 1])
        if (y1 - y0) >= 2:
            bands.append((y0, y1))
    return bands


def sample_band_signal(band_img: np.ndarray) -> np.ndarray:
    """
    Convert a band image (band_h, w, 3) into a 1D RGB signal (w, 3)
    by averaging vertically across the band.
    """
    return band_img.mean(axis=0)  # (w, 3)


# ----------------------------
# Patterns
# ----------------------------

def read_patterns_bytes(path: str) -> List[Tuple[int, int, int, int]]:
    """
    Reads lines like:
      .byte $08, $11, $22, $44
    Returns list of tuples: (A0, M0, A1, M1) each masked to 7-bit (0..127).
    """
    out: List[Tuple[int, int, int, int]] = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or line.startswith(";"):
                continue
            if ".byte" not in line:
                continue

            # Remove everything before/including ".byte"
            tail = line.split(".byte", 1)[1].strip()

            # Split by commas and parse $xx hex
            parts = [p.strip() for p in tail.split(",")]
            if len(parts) != 4:
                raise RuntimeError(f"Expected 4 byte values on line: {line}")

            vals = []
            for p in parts:
                # accept $08 or 0x08 or 08
                p = p.replace("$", "0x")
                vals.append(int(p, 16) & 0x7F)

            out.append((vals[0], vals[1], vals[2], vals[3]))

    if not out:
        raise RuntimeError("No .byte patterns found in patterns file.")
    return out



def build_input_wave(bits: str, width: int, reverse_bits: bool, rot: int) -> np.ndarray:
    """
    Repeat bitstring across 'width' samples. Map '1'->+1.0, '0'->-1.0

    reverse_bits: if True, reverse the string before repeating.
    rot: rotate LEFT by rot bits after reversal choice (rot can be any int).
    """
    s = bits.strip()
    if len(s) == 0:
        raise ValueError("Empty bitstring.")

    if reverse_bits:
        s = s[::-1]

    if rot != 0:
        rot %= len(s)
        s = s[rot:] + s[:rot]

    b = np.fromiter((1.0 if c == '1' else -1.0 for c in s), dtype=np.float64)
    reps = int(math.ceil(width / b.size))
    wave = np.tile(b, reps)[:width]
    return wave


def build_wave_from_am_bytes(a0: int, m0: int, a1: int, m1: int, width: int) -> np.ndarray:
    """
    Reproduce C code's stream packing and LSB-first shift:

      stream = ((b3&0x7f)<<21) | ((b2&0x7f)<<14) | ((b1&0x7f)<<7) | (b0&0x7f)
      for bit in 0..27:
          row[i] = +1 if (stream & 1) else -1
          stream >>= 1

    Where bytes are AUX0, MAIN0, AUX1, MAIN1 (7-bit each).
    Then repeat the 28-sample unit to fill 'width'.
    """
    b0 = a0 & 0x7F
    b1 = m0 & 0x7F
    b2 = a1 & 0x7F
    b3 = m1 & 0x7F

    stream = (b3 << 21) | (b2 << 14) | (b1 << 7) | b0

    unit = np.empty(28, dtype=np.float64)
    s = stream
    for i in range(28):
        unit[i] = 1.0 if (s & 1) else -1.0
        s >>= 1

    reps = int(math.ceil(width / 28))
    return np.tile(unit, reps)[:width]


# ----------------------------
# Regression / FIR learning
# ----------------------------

def make_design_matrix(x: np.ndarray, taps: int) -> np.ndarray:
    """
    x: (N,) input wave
    returns X: (N, taps) where each row is the window centered on n with edge clamping.
    taps must be odd.
    """
    if taps % 2 == 0:
        raise ValueError("--taps must be odd")
    N = x.size
    R = taps // 2
    X = np.empty((N, taps), dtype=np.float64)
    for n in range(N):
        for t in range(taps):
            idx = n + (t - R)
            if idx < 0:
                idx = 0
            elif idx >= N:
                idx = N - 1
            X[n, t] = x[idx]
    return X


def ridge_solve(X: np.ndarray, y: np.ndarray, lam: float) -> Tuple[np.ndarray, float]:
    """
    Solve (X,bias)->y with ridge on weights (not on bias).
    We append a bias column of 1s, but regularize only the tap weights.
    Returns (w, bias)
    """
    N, T = X.shape
    ones = np.ones((N, 1), dtype=np.float64)
    Xa = np.hstack([X, ones])  # (N, T+1)

    Rt = np.diag(np.r_[np.full(T, lam, dtype=np.float64), 0.0])  # don't regularize bias
    lhs = Xa.T @ Xa + Rt
    rhs = Xa.T @ y
    beta = np.linalg.solve(lhs, rhs)
    w = beta[:T]
    bias = float(beta[T])
    return w, bias


def fit_per_phase_fir(
    x: np.ndarray,
    target: np.ndarray,
    taps: int,
    phases: int,
    phase_bias: int,
    lam: float,
) -> Dict[int, Dict[str, Tuple[np.ndarray, float]]]:
    """
    Fit per-phase FIR for a 3-channel target.
    x: (N,)
    target: (N,3)
    returns fits[ph]["c0"|"c1"|"c2"] = (w, bias)
    """
    N = x.size
    Xfull = make_design_matrix(x, taps)  # (N, taps)

    fits: Dict[int, Dict[str, Tuple[np.ndarray, float]]] = {}
    for ph in range(phases):
        idx = np.array([n for n in range(N) if ((n + phase_bias) % phases) == ph], dtype=np.int64)
        X = Xfull[idx, :]
        fits[ph] = {}
        for c in range(3):
            y = target[idx, c]
            w, b = ridge_solve(X, y, lam)
            fits[ph][f"c{c}"] = (w, b)
    return fits


def predict_from_fits(
    x: np.ndarray,
    fits: Dict[int, Dict[str, Tuple[np.ndarray, float]]],
    taps: int,
    phases: int,
    phase_bias: int,
    x_shift: int = 0,
) -> np.ndarray:
    """
    Produce (N,3) predictions from per-phase fits.

    x_shift means: output sample at n uses the input window centered at (n + x_shift).
    This maybe fixes 4-phase striping from a dot-clock alignment mismatch?
    """
    N = x.size
    Xfull = make_design_matrix(x, taps)
    out = np.zeros((N, 3), dtype=np.float64)

    def clamp(i: int) -> int:
        if i < 0:
            return 0
        if i >= N:
            return N - 1
        return i

    for n in range(N):
        nn = clamp(n + x_shift)
        ph = (n + phase_bias) % phases
        for c in range(3):
            w, b = fits[ph][f"c{c}"]
            out[n, c] = Xfull[nn, :].dot(w) + b
    return out


def mse(a: np.ndarray, b: np.ndarray) -> float:
    d = a - b
    return float(np.mean(d * d))


# ----------------------------
# C output formatting
# ----------------------------

def fmt_c_array_2d(name: str, arr: np.ndarray, decimals: int = 6) -> str:
    phases, taps = arr.shape
    lines = [f"static const float {name}[{phases}][{taps}] = {{"]
    for ph in range(phases):
        vals = ", ".join(f"{arr[ph, t]:.{decimals}f}f" for t in range(taps))
        lines.append(f"    {{{vals}}},")
    lines.append("};")
    return "\n".join(lines)


def fmt_c_bias(name: str, bias: np.ndarray, decimals: int = 6) -> str:
    phases = bias.size
    vals = ", ".join(f"{bias[ph]:.{decimals}f}f" for ph in range(phases))
    return f"static const float {name}[{phases}] = {{{vals}}};"


# ----------------------------
# Main
# ----------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", required=True, help="Input AppleWin stepping BMP/PNG/etc.")
    ap.add_argument("--patterns", required=True, help="Text file containing bitstrings, one per band (top->bottom).")
    ap.add_argument("--emit", default="fir_out.c", help="Output C file path.")
    ap.add_argument("--space", choices=["rgb", "yiq"], default="rgb", help="Fit in RGB directly or in YIQ space.")
    ap.add_argument("--taps", type=int, default=9, help="Odd number of FIR taps (e.g. 9, 17, 33).")
    ap.add_argument("--phases", type=int, default=4, help="Phase buckets (4 recommended initially).")
    ap.add_argument("--thr", type=float, default=0.02, help="Non-black threshold in [0,1] for cropping.")
    ap.add_argument("--diff_thr", type=float, default=0.03, help="Row-mean diff threshold for band segmentation.")
    ap.add_argument("--lam", type=float, default=1e-3, help="Ridge lambda (regularization).")
    ap.add_argument("--no_reverse_bits", action="store_true",
                    help="Do not reverse bitstrings when building input wave (default is to reverse).")
    ap.add_argument("--bit_rot", type=int, default=0,
                    help="Rotate bitstrings LEFT by this many bits after reversal (0..3 usually enough).")
    ap.add_argument("--xshift_min", type=int, default=-2, help="Min horizontal sample shift to search (in pixels).")
    ap.add_argument("--xshift_max", type=int, default=2, help="Max horizontal sample shift to search (in pixels).")
    args = ap.parse_args()

    img = load_image_rgb01(args.image)
    x0, y0, x1, y1 = find_nonblack_bbox(img, thr=args.thr)
    strip = img[y0:y1, x0:x1, :]

    bands = segment_bands_by_color_steps(strip, diff_thr=args.diff_thr)
    patterns = read_patterns_bytes(args.patterns)

    if len(bands) != len(patterns):
        raise RuntimeError(
            f"Band count ({len(bands)}) != pattern count ({len(patterns)}).\n"
            f"Fix: make patterns.txt Match .byte lines to image.\n"
        )

    X_waves: List[np.ndarray] = []
    T_targets: List[np.ndarray] = []
    AM_bytes: List[Tuple[int, int, int, int]] = []

    for (y0b, y1b), (a0, m0, a1, m1) in zip(bands, patterns):
        band_img = strip[y0b:y1b, :, :]
        tgt_rgb = sample_band_signal(band_img)  # (W,3)

        # IMPORTANT: DHGR unit here is 28 dots (A0,M0,A1,M1 -> 28 bits).
        # AppleWin stepping strip is ~29px wide; the extra column is not part of the 28-dot unit.
        # Train on a width that's an exact multiple of 28 to avoid an unlearnable mismatch.
        W0 = tgt_rgb.shape[0]
        W = (W0 // 28) * 28
        if W == 0:
            raise RuntimeError(f"Band too narrow: width={W0}")
        tgt_rgb = tgt_rgb[:W, :]

        wave = build_wave_from_am_bytes(a0, m0, a1, m1, W)


        X_waves.append(wave)
        T_targets.append(tgt_rgb.astype(np.float64))
        AM_bytes.append((a0, m0, a1, m1))

    # Identify the black-reference band (A/M all zeros)
    black_band_idx = None
    for i, am in enumerate(AM_bytes):
        if am == (0, 0, 0, 0):
            black_band_idx = i
            break
    if black_band_idx is None:
        print("Warning: no .byte $00,$00,$00,$00 found; black calibration disabled.")

    # Identify the white-reference band (A/M all 0x7f)
    white_band_idx = None
    for i, am in enumerate(AM_bytes):
        if am == (0x7f, 0x7f, 0x7f, 0x7f):
            white_band_idx = i
            break
    if white_band_idx is None:
        print("Warning: no .byte $7f,$7f,$7f,$7f found; white calibration disabled.")

    x_all = np.concatenate(X_waves, axis=0)
    t_rgb_all = np.concatenate(T_targets, axis=0)

    if args.space == "yiq":
        t_all = rgb_to_yiq(t_rgb_all)
    else:
        t_all = t_rgb_all

    # Search best phase_bias
    best = None
    for pb in range(args.phases):
        fits = fit_per_phase_fir(
            x=x_all,
            target=t_all,
            taps=args.taps,
            phases=args.phases,
            phase_bias=pb,
            lam=args.lam,
        )

        for xs in range(args.xshift_min, args.xshift_max + 1):
            pred = predict_from_fits(
                x=x_all,
                fits=fits,
                taps=args.taps,
                phases=args.phases,
                phase_bias=pb,
                x_shift=xs,
            )
            err = mse(pred, t_all)
            if (best is None) or (err < best["err"]):
                best = {"phase_bias": pb, "x_shift": xs, "fits": fits, "err": err}


    assert best is not None
    pb = int(best["phase_bias"])
    fits = best["fits"]
    err = float(best["err"])

    taps = args.taps
    phases = args.phases

    # Compute per-phase black levels from the black-reference band
    black_levels = None
    if black_band_idx is not None:
        x_blk = X_waves[black_band_idx]  # (W,)
        Xb = make_design_matrix(x_blk, taps)  # (W, taps)

        black_levels = np.zeros((phases, 3), dtype=np.float64)
        counts = np.zeros(phases, dtype=np.int64)

        for n in range(x_blk.size):
            ph = (n + pb) % phases
            counts[ph] += 1
            for c in range(3):
                w, b = fits[ph][f"c{c}"]
                black_levels[ph, c] += Xb[n, :].dot(w) + b

        for ph in range(phases):
            if counts[ph] > 0:
                black_levels[ph, :] /= counts[ph]

    # Compute per-phase white levels from the white-reference band
    white_levels = None
    if white_band_idx is not None:
        x_wht = X_waves[white_band_idx]
        Xw = make_design_matrix(x_wht, taps)

        white_levels = np.zeros((phases, 3), dtype=np.float64)
        counts = np.zeros(phases, dtype=np.int64)

        for n in range(x_wht.size):
            ph = (n + pb) % phases
            counts[ph] += 1
            for c in range(3):
                w, b = fits[ph][f"c{c}"]
                white_levels[ph, c] += Xw[n, :].dot(w) + b

        for ph in range(phases):
            if counts[ph] > 0:
                white_levels[ph, :] /= counts[ph]

    c0 = np.zeros((phases, taps), dtype=np.float64)
    c1 = np.zeros((phases, taps), dtype=np.float64)
    c2 = np.zeros((phases, taps), dtype=np.float64)
    bc = np.zeros((phases, 3), dtype=np.float64)

    for ph in range(phases):
        for c in range(3):
            w, b = fits[ph][f"c{c}"]
            bc[ph, c] = b
            if c == 0:
                c0[ph, :] = w
            elif c == 1:
                c1[ph, :] = w
            else:
                c2[ph, :] = w

    out_lines: List[str] = []
    out_lines.append("// Auto-generated by aw-tapper.py")
    out_lines.append(f"// best phase_bias = {pb}, mse = {err:.8e}")
    out_lines.append("")

    if args.space == "yiq":
        out_lines.append(fmt_c_array_2d("firY", c0))
        out_lines.append("")
        out_lines.append(fmt_c_array_2d("firI", c1))
        out_lines.append("")
        out_lines.append(fmt_c_array_2d("firQ", c2))
        out_lines.append("")
        out_lines.append(fmt_c_bias("biasY", bc[:, 0]))
        out_lines.append(fmt_c_bias("biasI", bc[:, 1]))
        out_lines.append(fmt_c_bias("biasQ", bc[:, 2]))
    else:
        out_lines.append(fmt_c_array_2d("firR", c0))
        out_lines.append("")
        out_lines.append(fmt_c_array_2d("firG", c1))
        out_lines.append("")
        out_lines.append(fmt_c_array_2d("firB", c2))
        out_lines.append("")
        out_lines.append(fmt_c_bias("biasR", bc[:, 0]))
        out_lines.append(fmt_c_bias("biasG", bc[:, 1]))
        out_lines.append(fmt_c_bias("biasB", bc[:, 2]))
    if black_levels is not None:
        out_lines.append("")
        out_lines.append("// Per-phase black level measured from .byte $00,$00,$00,$00 band")
        out_lines.append(fmt_c_bias("blackR", black_levels[:, 0]))
        out_lines.append(fmt_c_bias("blackG", black_levels[:, 1]))
        out_lines.append(fmt_c_bias("blackB", black_levels[:, 2]))
    if white_levels is not None:
        out_lines.append("")
        out_lines.append("// Per-phase white level measured from .byte $7f,$7f,$7f,$7f band")
        out_lines.append(fmt_c_bias("whiteR", white_levels[:, 0]))
        out_lines.append(fmt_c_bias("whiteG", white_levels[:, 1]))
        out_lines.append(fmt_c_bias("whiteB", white_levels[:, 2]))

        # Per-phase gain so that (white - black) maps to 1.0
        if black_levels is not None:
            denom = (white_levels - black_levels)
            denom = np.where(np.abs(denom) < 1e-6, 1.0, denom)  # avoid divide-by-zero

            gains = 1.0 / denom
            out_lines.append("")
            out_lines.append("// Per-phase gain so (rgb - black) * gain maps white->1.0")
            out_lines.append(fmt_c_bias("gainR", gains[:, 0]))
            out_lines.append(fmt_c_bias("gainG", gains[:, 1]))
            out_lines.append(fmt_c_bias("gainB", gains[:, 2]))

    out_lines.append("")
    out_lines.append(f"static const int phase_bias = {pb};")
    xs = int(best["x_shift"])
    out_lines.append(f"static const int x_shift = {xs};")

    with open(args.emit, "w", encoding="utf-8") as f:
        f.write("\n".join(out_lines))

    print(f"Wrote: {args.emit}")
    print(f"Best phase_bias={pb}, x_shift={xs}, mse={err:.8e}")
    print("Tip: try --lam 1e-4 .. 1e-2 and/or --taps 17 if results are unstable.")


if __name__ == "__main__":
    main()
