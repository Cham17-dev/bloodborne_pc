"""Find every variant of Bloodborne's final composite pixel shader (bloom + tonemap + color
matrix + grain + gamma) in eboot.bin.

1. On Windows (PowerShell), export the shaders embedded in eboot.bin (about 9 MB):

   $b=[IO.File]::ReadAllBytes('<game>\\eboot.bin'); $s=[Text.Encoding]::GetEncoding(28591).GetString($b);
   $o=[IO.BinaryWriter]::new([IO.File]::Create("eboot_shaders.bin")); $i=0;
   while(($i=$s.IndexOf('OrbShdr',$i,[StringComparison]::Ordinal)) -ge 0){
     $len=[BitConverter]::ToUInt32($b,$i+8) -shr 8; $st=[Math]::Max(0,$i-$len-512);
     $en=[Math]::Min($b.Length,$i+64); $o.Write([BitConverter]::ToUInt64($b,$i+16)); $o.Write([int]$i);
     $o.Write([int]$len); $o.Write([int]$st); $o.Write([int]($en-$st)); $o.Write($b,$st,$en-$st);
     $i+=7 }; $o.Close()

   Each record: u64 shader_hash (shadPS4 pgm_hash), i32 offset of the OrbShdr footer, i32 code
   length, i32 window start, u32 window size, then the window (code ends at the footer).

2. python -I bb_shader_scan.py eboot_shaders.bin   (lists the family; the table itself is written by
   bb_composite_model.py eboot_shaders.bin ../../gpu/shadps4/video_core/renderer_vulkan/bb_tonemap_variants.h)

A shader belongs to the family when it loads the composite's constants by literal offset: words
616 (scene gain) and 840..851 (color matrix). Curves: see classify(). Results on 1.09
(16/10/2026): 5469 shaders, 300 variants (40 exp, 80 extended Reinhard, 80 log, 100 none).
The other .shaderbnd.dcx archives (dvdroot_ps4/shader, see bnd_extract.py) hold the named post
effects (ColorGrading = ed4c7668, DLAA = da1faca9, ToneMap_* building blocks) but not the
composite.
"""
import collections, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gcn_split as gcn

COUNT = {0x8: 1, 0x9: 2, 0xa: 4, 0xb: 8, 0xc: 16}


def code_of(blob, info, length):
    """The shader code before its OrbShdr footer: it starts with s_mov_b32 vcc_hi, <n> (0xBEEB03FF)
    where the footer sits (n + 1) * 2 dwords after the start (shadPS4 SearchBinaryInfo); `length`
    bytes long."""
    for start in range(info - 8, -1, -4):
        tok, n = struct.unpack('<II', blob[start:start + 8])
        if tok == 0xBEEB03FF and start + (n + 1) * 8 == info:
            return blob[start:start + length]
    return blob[info - length:info]


def read_export(path):
    data = open(path, 'rb').read()
    pos, shaders = 0, {}
    while pos < len(data):
        h, off, length, st, size = struct.unpack('<QiiiI', data[pos:pos + 24])
        blob = data[pos + 24:pos + 24 + size]
        pos += 24 + size
        info = off - st
        flags = struct.unpack('<I', blob[info + 8:info + 12])[0]
        shaders.setdefault(h, dict(type=(flags >> 2) & 0xf, code=code_of(blob, info, length)))
    return shaders


def literal_words(code):
    words = set()
    for enc, op, w in gcn.split(code):
        if enc == 'SMRD' and not (w[0] >> 8) & 1 and (w[0] & 0xff) == 255 and len(w) > 1:
            words.update(range(w[1], w[1] + COUNT.get(op, 1)))
    return words


def classify(code, lits, ops):
    """(curve, per_luminance, vignette). Curves: 0 exp 1 - exp(-k x) shaped by c (words 832-833),
    1 extended Reinhard u (1 + b u) / (1 + u), u = a x (words 833-834), 2 log a ln(1 + b x)
    (words 833-834), 3 none (clamp). per_luminance: the curve runs on luminance and the color is
    scaled, not per channel. vignette: words 890-892."""
    vignette = any(890 <= w <= 892 for w in lits)
    ln2 = sum(1 for e, o, w in gcn.split(code) if len(w) > 1 and w[1] == 0x3f317218)
    rcps = ops['v_rcp_f32'] + ops['v_rcp_f32_e64'] - int(vignette)
    if 832 in lits:
        return 0, 0, int(vignette)
    if 834 in lits and ln2:
        return 2, int(ln2 == 1), int(vignette)
    if 834 in lits:
        return 1, int(rcps < 3), int(vignette)
    return 3, 0, int(vignette)


def main():
    shaders = read_export(sys.argv[1])
    rows = []
    for h, s in sorted(shaders.items()):
        if s['type'] != 0:  # pixel shaders
            continue
        lits = literal_words(s['code'])
        if 616 not in lits or 840 not in lits:
            continue
        ops = collections.Counter(gcn.name(e, o) for e, o, _ in gcn.split(s['code']))
        exps = ops['v_exp_f32'] + ops['v_exp_f32_e64']
        logs = ops['v_log_f32'] + ops['v_log_f32_e64']
        tex = sum(v for k, v in ops.items() if k.startswith(('image', 'mimg')))
        rows.append((h, *classify(s['code'], lits, ops), int(808 in lits), int(809 in lits), tex,
                     exps, logs))
    print(len(shaders), 'shaders,', len(rows), 'composite variants:',
          dict(sorted(collections.Counter(r[1] for r in rows).items())),
          '(0 exp, 1 Reinhard, 2 log, 3 none)')
    if len(sys.argv) > 2:
        print('The variant table is now written by bb_composite_model.py <export> <header>, '
              'which checks every option set against the game\'s code.')


if __name__ == '__main__':
    main()
