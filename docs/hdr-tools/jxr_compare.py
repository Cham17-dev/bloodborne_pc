#!/usr/bin/env python3
"""Compares two HDR screenshots of the same still scene (decoded with jxr_nits.py's TIFF, -c 25).

    python3 jxr_compare.py reference.tif test.tif [left_fraction]

left_fraction (0..1, default 0): the part of the width left out on the left (an open menu).

Per band of the reference's luminance (nits): the mean luminance of both, their ratio, and the
mean chromaticity shift (r, g of r+g+b) on the pixels above 1 nit. Then per pixel: the median and
95th percentile of the luminance difference relative to the reference. Animations and grain make
per-pixel numbers noisier than the band means: compare still scenes, and read the bands first.
"""
import sys

import numpy as np

from jxr_nits import LUMA, read_tiff

BANDS = ((0, 0.5), (0.5, 2), (2, 10), (10, 50), (50, 150), (150, 250), (250, 600), (600, 10000))


def main():
    ref, test = read_tiff(sys.argv[1]), read_tiff(sys.argv[2])
    left = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0
    if ref.shape != test.shape:
        sys.exit(f"sizes differ: {ref.shape} vs {test.shape}")
    x0 = int(ref.shape[1] * left)
    ref, test = ref[:, x0:], test[:, x0:]
    lr, lt = 80.0 * (ref @ LUMA), 80.0 * (test @ LUMA)

    def chroma(rgb):
        s = np.maximum(rgb.sum(axis=-1, keepdims=True), 1e-6)
        return rgb[..., :2] / s

    cr, ct = chroma(ref), chroma(test)
    print(f"{'band (nits)':>14} {'pixels':>9} {'ref':>9} {'test':>9} {'ratio':>7} {'chroma shift':>13}")
    for lo, hi in BANDS:
        m = (lr >= lo) & (lr < hi)
        if m.sum() < 500:
            continue
        mc = m & (lr > 1.0)
        shift = np.abs(ct[mc] - cr[mc]).sum(axis=-1).mean() if mc.sum() else float('nan')
        print(f"{lo:>6.1f}-{hi:<7.0f} {m.sum():>9} {lr[m].mean():>9.3f} {lt[m].mean():>9.3f} "
              f"{lt[m].mean() / max(lr[m].mean(), 1e-6):>7.3f} {shift:>13.4f}")
    valid = lr > 0.5
    rel = np.abs(lt[valid] - lr[valid]) / lr[valid]
    print(f"per pixel (ref > 0.5 nit): median |diff| {np.median(rel) * 100:.1f} %, "
          f"p95 {np.percentile(rel, 95) * 100:.1f} %")
    print(f"max: ref {lr.max():.0f} nits, test {lt.max():.0f} nits")


if __name__ == "__main__":
    main()
