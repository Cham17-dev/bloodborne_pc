"""Reference model of Bloodborne's final composite pixel shader (the 284 "standard" variants of
bb_tonemap_variants.h), as reimplemented by bbport's generic HDR pass (hdr/bb_composite_hdr.frag).
Checked against the game's own code, run by gcn_emu.py: python -I bb_composite_model.py <export>.

Inputs (per pixel): scene (RGBA, HDR), mult (RGB, the 1x1 exposure/tint texture), bloom (RGB),
grain (R), vig_uv (the vignette coordinate, attribute 2 in the vignette variants), cb (the
constant buffer, by word). Variant options: curve, luminance, vignette, gamma, grain."""
import math
import re

EPS_FLOOR = 6.103515625e-05  # 2^-14, the scene floor before the curves
EPS_LOG = 2.384185791015625e-07  # 2^-22, added before log2 / rcp
MAX_DIFF = 0.02  # largest difference with the game's code for a variant to be covered


def clamp01(x):
    return min(max(x, 0.0), 1.0)


def saturate_affine(rgb, cb, scale, offset, sat, scale2, offset2):
    """affine (scale, offset), saturation around the mean (sat), affine (scale2, offset2)."""
    a = [c * cb[scale] + cb[offset] for c in rgb]
    m = sum(a) / 3.0
    return [(m + cb[sat] * (c - m)) * cb[scale2] + cb[offset2] for c in a]


def vignette888(uv, cb):
    """Lens vignette (words 888-892): smoothstep ramp times a cos^n-like falloff."""
    r = math.sqrt(uv[0] * uv[0] + uv[1] * uv[1])
    a = clamp01(cb[888] * r + cb[889])
    s = max(min(cb[891], 3.0 * a * a - 2.0 * a * a * a), 0.0)
    return s * (1.0 / (1.0 + (cb[890] * r) ** 2)) ** cb[892]


def vignette_cos4(uv):
    """(1 - r^2)^4, clamped."""
    return clamp01(1.0 - (uv[0] * uv[0] + uv[1] * uv[1])) ** 4


def pre_exposure(v, cb, kind):
    """Exposure curves applied to the scene before the tone curve: 1 = c19 (v + sqrt(v (v + c16)
    + 1) - 1) (words 16-19), 2 = c11 (exp(c8 v) - 1) (words 8-11)."""
    if kind == 1:
        return cb[19] * (v + math.sqrt(v * (v + cb[16]) + 1.0) - 1.0)
    return cb[11] * (2.0 ** (cb[8] * v * 1.4426950408889634) - 1.0)


def curve(x, kind, luminance, cb, floor=True):
    """The game's tone curve on the exposed scene x (RGB): T (RGB), clamped to [0, 1]. floor: x is
    first raised to 2^-14 (all variants but some with the scene matrix)."""
    if kind == 3:  # none
        return [clamp01(c) for c in x]
    if floor:
        x = [max(c, EPS_FLOOR) for c in x]
    if kind == 0:  # exp: y = 1 - exp(-k x), T = y (c (y - 1) + 1)^2
        k, c = cb[832], cb[833]
        f = lambda v: (lambda y: y * (c * (y - 1.0) + 1.0) ** 2)(1.0 - 2.0 ** (-v * k * 1.4426950408889634))
    elif kind == 1:  # extended Reinhard: u = a x, T = u (1 + b u) / (1 + u)
        a, b = cb[833], cb[834]
        f = lambda v: (a * v + a * a * b * v * v) / (a * v + 1.0)
    else:  # log: T = a ln(1 + b x)
        a, b = cb[833], cb[834]
        f = lambda v: a * math.log(b * v + 1.0) if b * v + 1.0 > 0 else 0.0  # NaN: clamped to 0
    if not luminance:
        return [clamp01(f(v)) for v in x]
    lum = sum(x) / 3.0
    t = f(lum) if kind == 1 else clamp01(f(lum))  # the Reinhard value is not clamped here
    return [clamp01(v / (lum + EPS_LOG) * t) for v in x]


def composite(scene, mult, bloom, grain, uv, cb, o):
    """o: dict of options: curve, luminance, adaptive, cos4, vig888, gamma, grain."""
    ms = saturate_affine(mult, cb, 752, 753, 756, 754, 755)  # scene tint
    # Bloom base: affine (744, 745), saturation (748) clamped, then affine (746, 747).
    a = [m * cb[744] + cb[745] for m in mult]
    mean = sum(a) / 3.0
    mb = [clamp01(mean + cb[748] * (c - mean)) * cb[746] + cb[747] for c in a]
    gain = cb[616] if o.get('gain', 1) else 1.0
    if o['cos4']:
        gain *= vignette_cos4(uv)
    if o['vig888']:
        gain *= vignette888(uv, cb)
    sc = scene[:3]
    if o['pre'] == 3:  # alpha: scene clamped, times c12 (c14 a^2 + 1), words 12-15
        a = clamp01(scene[3])
        sc = [clamp01(c) * cb[12] * (cb[14] * a * a + 1.0) for c in sc]
    elif o['pre'] and o['pre_lum']:
        lum = sum(sc) / 3.0
        sc = [c * pre_exposure(lum, cb, o['pre']) / (lum + EPS_LOG) for c in sc]
    elif o['pre']:
        sc = [pre_exposure(c, cb, o['pre']) for c in sc]
    if o.get('scene_mat'):  # 3x4 matrix on the tinted scene (words 856..867), then the vignettes
        x = [sc[i] * ms[i] for i in range(3)]
        x = [(sum(cb[856 + 4 * r + j] * x[j] for j in range(3)) + cb[859 + 4 * r]) * gain
             for r in range(3)]
    else:
        x = [sc[i] * gain * ms[i] for i in range(3)]
    g = [mb[i] + bloom[i] * ms[i] for i in range(3)]
    if o.get('glow_mat'):  # 3x4 matrix on the glow, words 872..883, then max 0
        g = [sum(cb[872 + 4 * r + j] * g[j] for j in range(3)) + cb[875 + 4 * r] for r in range(3)]
        if not o['additive']:
            g = [max(v, 0.0) for v in g]
    else:
        g = [cb[617] * v for v in g]
    if o['additive']:  # no curve, no clamp: bloom added (HDR output)
        s = [x[i] + g[i] for i in range(3)]
    else:
        t = curve(x, o['curve'], o['luminance'], cb, o.get('floor', 1))
        u = [clamp01(v) for v in g]
        s = [t[i] + u[i] - t[i] * u[i] for i in range(3)]
    if o.get('grade', 1):  # color matrix, words 840..851
        out = [sum(cb[840 + 4 * r + j] * s[j] for j in range(3)) + cb[843 + 4 * r] for r in range(3)]
    else:
        out = list(s)
    if o['gamma']:
        out = [clamp01(v + EPS_LOG) ** cb[808] for v in out]
    if o['grain']:
        out = [v + cb[809] * grain + cb[810] for v in out]
    alpha = sum(cb[620 + i] * scene[i] for i in range(4))
    if o['alpha_clamp']:
        alpha = clamp01(alpha)
    return out + [alpha]


# ------------------------------------------------------------------ check against the game's code

def standard_roles(samples):
    """Texture roles from the sampling signature: (attr, dmask) -> role, or None if the variant is
    not a standard one."""
    roles = {}
    for _, attr, mask in samples:
        if mask == 0xf and attr == 0:
            roles[(attr, mask)] = 'scene'
        elif mask == 0x7 and attr == 1:
            roles[(attr, mask)] = 'bloom'
        elif mask == 0x7 and attr in (2, 3):
            roles[(attr, mask)] = 'mult'
        elif mask == 0x1 and attr in (3, 4):
            roles[(attr, mask)] = 'grain'
        else:
            return None
    return roles if {'scene', 'bloom', 'mult'} <= set(roles.values()) else None


def random_case(rng):
    """Random constants and inputs in the ranges the game uses (grading near neutral, positive
    curve parameters), so that float32 cancellations do not hide real differences."""
    u = rng.uniform
    cb = {w: u(0.0, 1.0) for w in range(1024)}
    cb.update({744: u(0.6, 1.4), 745: u(-0.1, 0.1), 746: u(0.6, 1.4), 747: u(-0.1, 0.1),
               748: u(0.5, 1.5), 752: u(0.6, 1.4), 753: u(-0.1, 0.1), 754: u(0.6, 1.4),
               755: u(-0.05, 0.05), 756: u(0.5, 1.5), 616: u(0.2, 3), 617: u(0, 1.0),
               808: 1 / 2.2, 809: u(0, 0.02), 810: u(-0.01, 0), 832: u(0.5, 4), 833: u(0.05, 1.5),
               834: u(0.0, 2.0), 888: u(-2, 2), 889: u(0, 1), 890: u(0, 2), 891: u(0.5, 1.5),
               892: u(0.5, 3), 8: u(0.2, 3), 11: u(0.2, 2), 12: u(0.5, 2), 14: u(0, 2), 16: u(0, 2),
               19: u(0.5, 2)})
    for base in (856, 872):
        for r in range(3):
            for c in range(4):
                cb[base + 4 * r + c] = ((1.0 if r == c else 0.0) + u(-0.15, 0.15)) * (1.0 if base == 872 else u(0.5, 2.0)) if c < 3 else u(-0.03, 0.03)
    for r in range(3):
        for c in range(4):
            cb[840 + 4 * r + c] = (1.0 if r == c else 0.0) + u(-0.15, 0.15) if c < 3 else u(-0.05, 0.05)
    vals = {'scene': [u(0, 1) * 10 ** u(-3, 1.5) for _ in range(3)] + [u(0, 1)],
            'mult': [u(0.5, 1.5) for _ in range(3)] + [0],
            'bloom': [u(0, 1) for _ in range(3)] + [0],
            'grain': [u(0, 1), 0, 0, 0]}
    return cb, vals, (u(-1, 1), u(-1, 1))


def max_diff(code, roles, opts, rng, trials):
    import gcn_emu
    worst, case = 0.0, None
    for _ in range(trials):
        cb, vals, uv = random_case(rng)
        lane = gcn_emu.Lane(cb, lambda a, m: vals[roles[(a, m)]], {a: (uv[0], uv[1], 0, 1) for a in range(8)})
        game = lane.run(code)
        mine = composite(vals['scene'], vals['mult'][:3], vals['bloom'][:3], vals['grain'][0], uv, cb, opts)
        for g_, m_ in zip(game, mine):
            err = abs(g_ - m_) / max(1.0, abs(g_))  # absolute below 1, relative above
            if err > worst:
                worst, case = err, (game, mine)
    return worst, case


def immediate_words(code):
    import gcn_split
    words = set()
    for enc, op, w in gcn_split.split(code):
        if enc == 'SMRD' and op >= 8 and (w[0] >> 8) & 1:
            words.update(range(w[0] & 0xff, (w[0] & 0xff) + {8: 1, 9: 2, 10: 4, 11: 8, 12: 16}[op]))
    return words


def options_from_code(code, roles):
    """The options that can be read from the constants and literals the variant uses."""
    import bb_shader_scan, gcn_split
    words = bb_shader_scan.literal_words(code)
    imm = immediate_words(code)
    lits = {w[1] for _, _, w in gcn_split.split(code) if len(w) > 1}
    curve = 3
    if 832 in words:
        curve = 0
    elif 833 in words:
        curve = 2 if 0x3f317218 in lits else 1
    pre = 1 if 19 in imm else 2 if 11 in imm else 3 if 14 in imm else 0
    import gcn_disasm
    text = '\n'.join(gcn_disasm.Disasm().run(code))
    glow_mat = int(872 in words)
    clamped = (re.search(r'v_max_f32_e64 v\d+, v\d+, 0 clamp', text) if glow_mat else
               re.search(r'cb\[617\], v\d+ clamp', text))
    return dict(curve=curve, pre=pre, vig888=int(888 in words), cos4=int(0xc0000000 in lits),
                gamma=int(808 in words), grain=int('grain' in roles.values()),
                gain=int(616 in words), grade=int(840 in words), floor=int(curve == 3 or 0x38800000 in lits), scene_mat=int(856 in words), glow_mat=glow_mat,
                additive=int(clamped is None),
                alpha_clamp=int(re.search(r'cb\[620\], v\d+ clamp', text) is not None))


def identify(code, roles, rng):
    """Options read from the code, plus the two that are not (curve on luminance, pre-exposure on
    luminance), found by trying both."""
    import itertools
    base = options_from_code(code, roles)
    best = None
    for lum, pre_lum in itertools.product((0, 1), (0, 1)):
        if (base['curve'] in (0, 3) and lum) or (base['pre'] in (0, 3) and pre_lum) or \
                (base['additive'] and lum):
            continue
        opts = dict(base, luminance=lum, pre_lum=pre_lum)
        worst, case = max_diff(code, roles, opts, rng, 12)
        if best is None or worst < best[0]:
            best = (worst, opts, case)
    return best


def interface(code, samples, roles):
    """Image index (order of the samples: bbport's recompiler lists images in program order) and
    attribute of each role, and the attribute the vignettes read (-1: none)."""
    import gcn_disasm
    out = {}
    for index, (_, attr, mask) in enumerate(samples):
        role = roles[(attr, mask)]
        out[role] = (index, attr)
    used = {a for _, a, _ in samples}
    interps = set(int(a) for a in re.findall(r'attr(\d+)\.', '\n'.join(gcn_disasm.Disasm().run(code))))
    other = sorted(interps - used)
    out['vig'] = (-1, other[0] if other else -1)
    out['images'] = len(samples)
    return out


HEADER = """// SPDX-FileCopyrightText: Copyright 2026 bbport
// SPDX-License-Identifier: GPL-2.0-or-later
//
// bbport HDR: the {n} variants of Bloodborne's final composite pixel shader (bloom + tone curve +
// color matrix + grain + gamma; words 840..851 or the tints 744, 752) found in eboot.bin 1.09, with what the generic HDR pass
// (hdr/bb_composite_hdr.frag) needs to reproduce each one. Generated by
// docs/hdr-tools/bb_composite_model.py, which checks every option set against the game's own
// code run by docs/hdr-tools/gcn_emu.py ({checked} reproduced, largest difference {worst:.1e} on
// a 0..1 output). Variants with standard = 0 (several scene samples: distortion effects, or not
// reproduced within {maxdiff}) are listed for the log only. Sorted by hash.

#pragma once

#include <array>

#include "common/types.h"

namespace Vulkan {{

struct TonemapVariant {{
    u32 hash;         ///< shadPS4 pgm_hash (OrbShdr shader_hash)
    u8 standard;      ///< reproduced by the generic HDR pass
    u8 curve;         ///< 0 exp (words 832-833), 1 extended Reinhard, 2 log (833-834), 3 none
    u8 luminance;     ///< the curve runs on the mean of RGB (color scaled), not per channel
    u8 pre;           ///< pre-exposure: 0 none, 1 c19 (v + sqrt(v (v + c16) + 1) - 1),
                      ///< 2 c11 (exp(c8 v) - 1), 3 scene clamped times c12 (c14 a^2 + 1)
    u8 pre_luminance; ///< pre-exposure on the mean of RGB
    u8 cos4;          ///< (1 - r^2)^4 vignette
    u8 vig888;        ///< lens vignette, words 888-892
    u8 gamma;         ///< output encoded with word 808 (1/2.2)
    u8 grain;         ///< grain added (words 809, 810)
    u8 additive;      ///< no curve, bloom added, unclamped: HDR output for a later pass
    u8 alpha_clamp;   ///< output alpha clamped
    u8 gain;          ///< scene gain, word 616 (else 1)
    u8 floor;         ///< curve input raised to 2^-14 first
    u8 scene_mat;     ///< 3x4 matrix on the tinted scene (words 856..867), vignettes after it
    u8 glow_mat;      ///< 3x4 matrix on the glow (872..883), max 0, instead of word 617
    u8 grade;         ///< color matrix 840..851 (else none: the screen blend is the output)
    s8 img_scene, img_mult, img_bloom; ///< image index (order of use) of each texture
    s8 attr_scene, attr_mult, attr_bloom, attr_vig; ///< interpolated attribute (-1: none)
}};

inline constexpr std::array<TonemapVariant, {n}> TonemapVariants{{{{
{body}
}}}};

}} // namespace Vulkan
"""


def write_header(path, shaders, family, results, skipped):
    rows, worst = [], 0.0
    for h in family:
        if h in results:
            o, w, itf = results[h]
            worst = max(worst, w)
            vals = [1, o['curve'], o['luminance'], o['pre'], o['pre_lum'], o['cos4'], o['vig888'],
                    o['gamma'], o['grain'], o['additive'], o['alpha_clamp'],
                    o['gain'], o['floor'], o['scene_mat'], o['glow_mat'], o['grade'],
                    itf['scene'][0], itf['mult'][0], itf['bloom'][0],
                    itf['scene'][1], itf['mult'][1], itf['bloom'][1], itf['vig'][1]]
        else:
            vals = [0] + [0] * 15 + [-1] * 7
        rows.append('    {0x%08x, %s},' % (h, ', '.join(map(str, vals))))
    with open(path, 'w') as f:
        f.write(HEADER.format(n=len(rows), checked=len(results), worst=worst, maxdiff=MAX_DIFF, body='\n'.join(rows)))


def main():
    import os, sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import random
    import bb_shader_scan, gcn_emu
    shaders = bb_shader_scan.read_export(sys.argv[1])
    family = [h for h, s in sorted(shaders.items()) if s['type'] == 0 and
              ({840, 851} <= bb_shader_scan.literal_words(s['code']) or
               {616, 840} <= bb_shader_scan.literal_words(s['code']) or
               {744, 752} <= bb_shader_scan.literal_words(s['code']))]  # tints: also without 840
    rng = random.Random(1)
    results, skipped, worst_all = {}, [], 0.0
    roles_of = {}
    for h in family:
        code = shaders[h]['code']
        probe = gcn_emu.Lane({w: 0.5 for w in range(1024)}, lambda a, m: (0.5,) * 4,
                             {a: (0.5, 0.5, 0, 1) for a in range(8)})
        try:
            probe.run(code)
        except NotImplementedError as e:
            skipped.append((h, str(e)))
            continue
        roles = standard_roles(probe.samples)
        if roles is None:
            skipped.append((h, 'texture roles'))
            continue
        roles_of[h] = (roles, probe.samples, probe)
        worst, opts, case = identify(code, roles, rng)
        worst, case = max_diff(code, roles, opts, rng, 200)  # confirm on more pixels
        if worst > 1e-3:
            print(f'{h:08x} {opts}: max diff {worst:.2e}  game {case[0]}  model {case[1]}')
        if worst > MAX_DIFF:  # not reproduced closely enough: left in SDR
            skipped.append((h, f'max diff {worst:.2e}'))
            continue
        results[h] = (opts, worst, interface(code, probe.samples, roles))
        worst_all = max(worst_all, worst)
    print(f'{len(results)} variants reproduced, max diff {worst_all:.2e} (absolute below 1, '
          f'relative above); '
          f'{len(skipped)} not standard: {" ".join(f"{h:08x}" for h, _ in skipped)}')
    # Shaders found by the tints only (no color matrix) are listed when reproduced: the tint words
    # are also used by other passes (motion blur).
    listed = [h for h in family if h in results or
              {840} <= bb_shader_scan.literal_words(shaders[h]['code'])]
    print(f'{len(listed)} listed in the table')
    if len(sys.argv) > 2:
        write_header(sys.argv[2], shaders, listed, results, skipped)


if __name__ == '__main__':
    main()
