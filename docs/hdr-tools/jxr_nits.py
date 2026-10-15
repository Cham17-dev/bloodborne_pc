#!/usr/bin/env python3
"""Measure the luminance (nits) of a Windows HDR screenshot (.jxr, Xbox Game Bar: Win+Alt+PrtScn).

The .jxr is first decoded with jxrlib (https://github.com/4creators/jxrlib, `make`) to an
uncompressed half-float TIFF, its native format. Do NOT decode to 128bppRGBAFloat (-c 28): that
conversion is broken in jxrlib and gives a zoomed-in, cropped picture.

    JxrDecApp -i capture.jxr -o capture.tif -c 25      # 64bppRGBAHalf
    python3 jxr_nits.py capture.tif [preview.png]

Pixels are scRGB (BT.709 primaries, linear, 1.0 = 80 nits). Prints the luminance distribution,
a coarse map (99th percentile per tile) and optionally writes a preview PNG (paper white 203).
"""
import struct
import sys
import zlib

import numpy as np

LUMA = np.array([0.2126, 0.7152, 0.0722])


def read_tiff(path):
    data = open(path, "rb").read()
    ifd = struct.unpack("<I", data[4:8])[0]
    tags = {}
    for i in range(struct.unpack("<H", data[ifd:ifd + 2])[0]):
        tag, _, _, value = struct.unpack("<HHII", data[ifd + 2 + 12 * i:ifd + 14 + 12 * i])
        tags[tag] = value
    w, h, offset = tags[256], tags[257], tags[273]
    bpp = tags[279] * 8 // (w * h)
    if bpp != 64:
        sys.exit(f"expected 64bppRGBAHalf (decode with -c 25), got {bpp} bpp")
    px = np.frombuffer(data, dtype="<f2", offset=offset, count=w * h * 4).reshape(h, w, 4)
    return px[..., :3].astype(np.float32)


def write_png(path, rgb, paper_white=203.0, step=4):
    s = np.clip(rgb[::step, ::step] * 80.0 / paper_white, 0.0, 1.0) ** (1 / 2.2)
    img = (s * 255 + 0.5).astype(np.uint8)
    h, w, _ = img.shape
    raw = b"".join(b"\x00" + img[y].tobytes() for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def main():
    rgb = read_tiff(sys.argv[1])
    nits = 80.0 * (rgb @ LUMA)
    print(f"{rgb.shape[1]}x{rgb.shape[0]}  max {nits.max():.0f} nits")
    for p in (50, 90, 99, 99.9, 99.99):
        print(f"  p{p}: {np.percentile(nits, p):.1f} nits")
    for t in (203, 400, 600, 800, 1000):
        print(f"  > {t} nits: {(nits > t).sum()} px")
    rows, cols = 12, 16
    h, w = nits.shape
    tiles = nits[:h - h % rows, :w - w % cols].reshape(rows, h // rows, cols, w // cols)
    tiles = tiles.transpose(0, 2, 1, 3).reshape(rows, cols, -1)
    print("p99 per tile (nits):")
    for row in np.percentile(tiles, 99, axis=2):
        print(" ".join(f"{v:5.0f}" for v in row))
    if len(sys.argv) > 2:
        write_png(sys.argv[2], rgb)


if __name__ == "__main__":
    main()
