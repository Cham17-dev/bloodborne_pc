"""Writes test cases for check.comp (every standard variant of bb_tonemap_variants.h, random
constants and pixels in the game's ranges) and compares its results with bb_composite_model.py:
the SDR output must match; the HDR curve must equal the game's below the knee, stay continuous,
increase and stay under the peak. Usage: python -I compare.py write <cases> | check <cases> <out>"""
import os, random, re, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import bb_composite_model as M

HEADER = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'gpu', 'shadps4',
                      'video_core', 'renderer_vulkan', 'bb_tonemap_variants.h')
BATCHES_PER_VARIANT = 4


def variants():
    for line in open(HEADER):
        m = re.match(r'\s*\{0x([0-9a-f]{8}), ([-\d, ]+)\},', line)
        if m:
            v = [int(x) for x in m.group(2).split(',')]
            if v[0] == 1:
                yield int(m.group(1), 16), dict(curve=v[1], luminance=v[2], pre=v[3], pre_lum=v[4],
                                                cos4=v[5], vig888=v[6], gamma=v[7], grain=0,
                                                additive=v[9], alpha_clamp=v[10], gain=v[11],
                                                floor=v[12], scene_mat=v[13], glow_mat=v[14],
                                                grade=v[15])


def cases():
    rng = random.Random(7)
    for h, o in variants():
        for _ in range(BATCHES_PER_VARIANT):
            cb, _, _ = M.random_case(rng)
            pixels = []
            for _ in range(64):
                _, vals, uv = M.random_case(rng)
                pixels.append((vals, uv))
            yield h, o, cb, pixels


def write(path):
    with open(path, 'wb') as f:
        for h, o, cb, pixels in cases():
            f.write(struct.pack('<1024f', *[cb.get(w, 0.0) for w in range(1024)]))
            f.write(struct.pack('<13i', o['curve'], o['luminance'], o['pre'], o['pre_lum'], o['cos4'],
                                o['vig888'], o['gamma'], o['additive'], o['gain'], o['floor'],
                                o['scene_mat'], o['glow_mat'], o['grade']))
            for vals, uv in pixels:
                f.write(struct.pack('<16f', *vals['scene'], *vals['mult'][:3], 0, *vals['bloom'][:3], 0,
                                    uv[0], uv[1], 0, 0))


def check(path_out):
    data = open(path_out, 'rb').read()
    worst, bad_hdr, n, pos = 0.0, 0, 0, 0
    psy_checked, psy_bad = [0], [0]
    for h, o, cb, pixels in cases():
        res = struct.unpack_from('<1280f', data, pos)
        pos += 1280 * 4
        for k, (vals, uv) in enumerate(pixels):
            mine = res[20 * k:20 * k + 3]
            ref = M.composite(vals['scene'], vals['mult'][:3], vals['bloom'][:3], 0.0, uv, cb,
                              dict(o, grain=0))[:3]
            for a, b in zip(mine, ref):
                worst = max(worst, abs(a - b) / max(1.0, abs(b)))
            sdr_t, ok, hdr_t = res[20 * k + 4:20 * k + 7], res[20 * k + 7], res[20 * k + 8:20 * k + 11]
            grey_out, pk = res[20 * k + 12:20 * k + 15], res[20 * k + 15]
            lum_x, lum_4x, max_x = res[20 * k + 16:20 * k + 19]
            if pk and not o['additive']:
                psy_checked[0] += 1
                if any(abs(g - 0.18) > 2e-3 for g in grey_out) or lum_4x < lum_x - 1e-4 or max_x > 4.94:
                    psy_bad[0] += 1
            n += 1
            if ok and not o['additive']:
                for s, t in zip(sdr_t, hdr_t):
                    # Below the knee (with margin) the HDR curve is the game's; it never goes
                    # under it, and stays under the peak.
                    if (s < 0.55 and abs(s - t) > 2e-3 and not o['luminance']) or t < s - 2e-3 or t > 4.94:
                        bad_hdr += 1
                        break
    print(f'{n} pixels: SDR max difference with the model {worst:.2e}; HDR curve violations {bad_hdr}; '
          f'PsychoV30 (grey kept, rising, under the peak) violations {psy_bad[0]} of {psy_checked[0]}')
    return worst < 5e-3 and bad_hdr == 0 and psy_bad[0] == 0


if __name__ == '__main__':
    if sys.argv[1] == 'write':
        write(sys.argv[2])
    else:
        sys.exit(0 if check(sys.argv[3]) else 1)
