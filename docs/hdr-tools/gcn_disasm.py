"""Small GCN (Sea Islands / PS4) disassembler for pixel shaders: enough of VALU, SMRD, VINTRP,
MIMG and EXP to read a post-process shader's math. Constant-buffer loads by literal offset are
followed so SGPRs print as cb[word]. Usage: python -I gcn_disasm.py eboot_shaders.bin <hash>..."""
import os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

FLOATS = {240: '0.5', 241: '-0.5', 242: '1.0', 243: '-1.0', 244: '2.0', 245: '-2.0', 246: '4.0',
          247: '-4.0'}
VOP1 = {0x00: 'v_nop', 0x01: 'v_mov_b32', 0x05: 'v_cvt_f32_i32', 0x06: 'v_cvt_f32_u32', 0x07: 'v_cvt_u32_f32',
        0x08: 'v_cvt_i32_f32', 0x0a: 'v_cvt_f16_f32', 0x0b: 'v_cvt_f32_f16', 0x20: 'v_fract_f32',
        0x21: 'v_trunc_f32', 0x22: 'v_ceil_f32', 0x23: 'v_rndne_f32', 0x24: 'v_floor_f32', 0x25: 'v_exp_f32',
        0x27: 'v_log_f32', 0x2a: 'v_rcp_f32', 0x2e: 'v_rsq_f32', 0x33: 'v_sqrt_f32', 0x35: 'v_sin_f32',
        0x36: 'v_cos_f32'}
VOP2 = {0x00: 'v_cndmask_b32', 0x03: 'v_add_f32', 0x04: 'v_sub_f32', 0x05: 'v_subrev_f32',
        0x06: 'v_mac_legacy_f32', 0x07: 'v_mul_legacy_f32', 0x08: 'v_mul_f32', 0x0f: 'v_min_f32',
        0x10: 'v_max_f32', 0x1b: 'v_and_b32', 0x1c: 'v_or_b32', 0x1d: 'v_xor_b32', 0x1f: 'v_mac_f32',
        0x20: 'v_madmk_f32', 0x21: 'v_madak_f32', 0x25: 'v_add_i32', 0x2f: 'v_cvt_pkrtz_f16_f32',
        0x16: 'v_lshlrev_b32', 0x14: 'v_lshrrev_b32', 0x12: 'v_min_u32', 0x13: 'v_max_u32'}
VOP3 = {0x141: 'v_mad_f32', 0x14a: 'v_fma_f32', 0x151: 'v_min3_f32', 0x154: 'v_max3_f32',
        0x157: 'v_med3_f32', 0x140: 'v_mad_legacy_f32', 0x148: 'v_bfe_u32', 0x15f: 'v_div_fixup_f32'}
CMP = ['f', 'lt', 'eq', 'le', 'gt', 'lg', 'ge', 'o', 'u', 'nge', 'nlg', 'ngt', 'nle', 'neq', 'nlt', 'tru']
SMRD = {0x0: 's_load_dword', 0x1: 's_load_dwordx2', 0x2: 's_load_dwordx4', 0x3: 's_load_dwordx8',
        0x4: 's_load_dwordx16', 0x8: 's_buffer_load_dword', 0x9: 's_buffer_load_dwordx2',
        0xa: 's_buffer_load_dwordx4', 0xb: 's_buffer_load_dwordx8', 0xc: 's_buffer_load_dwordx16'}
SCOUNT = {0x0: 1, 0x1: 2, 0x2: 4, 0x3: 8, 0x4: 16, 0x8: 1, 0x9: 2, 0xa: 4, 0xb: 8, 0xc: 16}
MIMG = {0x00: 'image_load', 0x20: 'image_sample', 0x24: 'image_sample_l', 0x25: 'image_sample_b',
        0x27: 'image_sample_lz', 0x26: 'image_sample_lz?', 0x28: 'image_sample_c'}


class Disasm:
    def __init__(self):
        self.sgpr_cb = {}  # sgpr -> 'cb[word]'

    def src(self, v, lit=None):
        if v < 104:
            return self.sgpr_cb.get(v, f's{v}')
        if v == 106:
            return 'vcc_lo'
        if v == 107:
            return 'vcc_hi'
        if v == 124:
            return 'm0'
        if v == 126:
            return 'exec_lo'
        if v == 128:
            return '0'
        if 129 <= v <= 192:
            return str(v - 128)
        if 193 <= v <= 208:
            return str(-(v - 192))
        if v in FLOATS:
            return FLOATS[v]
        if v == 255:
            f = struct.unpack('<f', struct.pack('<I', lit))[0]
            return f'{f:.7g}' if abs(f) > 1e-30 and abs(f) < 1e30 else f'0x{lit:08x}'
        if v >= 256:
            return f'v{v - 256}'
        return f'?{v}'

    def run(self, code):
        n = len(code) // 4
        w = struct.unpack(f'<{n}I', code[:n * 4])
        i, out = 0, []
        while i < n:
            x = w[i]
            nxt = w[i + 1] if i + 1 < n else 0
            pc = i * 4
            if (x >> 25) == 0x3f:  # VOP1
                op, s0, d = (x >> 9) & 0xff, x & 0x1ff, (x >> 17) & 0xff
                size = 2 if s0 == 255 else 1
                txt = f'{VOP1.get(op, f"vop1_{op:#x}")} v{d}, {self.src(s0, nxt)}'
            elif (x >> 25) == 0x3e:  # VOPC
                op, s0, s1 = (x >> 17) & 0xff, x & 0x1ff, (x >> 9) & 0xff
                size = 2 if s0 == 255 else 1
                name = f'v_cmp_{CMP[op & 15]}_f32' if op < 0x10 else f'vopc_{op:#x}'
                txt = f'{name} vcc, {self.src(s0, nxt)}, v{s1}'
            elif x >> 31 == 0:  # VOP2
                op, s0, s1, d = (x >> 25) & 0x3f, x & 0x1ff, (x >> 9) & 0xff, (x >> 17) & 0xff
                if op in (0x20, 0x21):
                    size = 2
                    k = struct.unpack('<f', struct.pack('<I', nxt))[0]
                    txt = (f'{VOP2[op]} v{d}, {self.src(s0)}, {k:.7g}, v{s1}' if op == 0x20
                           else f'{VOP2[op]} v{d}, {self.src(s0)}, v{s1}, {k:.7g}')
                else:
                    size = 2 if s0 == 255 else 1
                    extra = ', vcc' if op == 0 else ''
                    txt = f'{VOP2.get(op, f"vop2_{op:#x}")} v{d}, {self.src(s0, nxt)}, v{s1}{extra}'
            elif (x >> 23) == 0x17f:  # SOPP
                op, imm = (x >> 16) & 0x7f, x & 0xffff
                names = {0: 's_nop', 1: 's_endpgm', 2: 's_branch', 4: 's_cbranch_scc0', 5: 's_cbranch_scc1',
                         6: 's_cbranch_vccz', 7: 's_cbranch_vccnz', 8: 's_cbranch_execz',
                         9: 's_cbranch_execnz', 0xc: 's_waitcnt'}
                size = 1
                txt = f'{names.get(op, f"sopp_{op:#x}")} {imm:#x}'
                if op == 1:
                    out.append(f'{pc:5x}: {txt}')
                    break
            elif (x >> 23) == 0x17d:  # SOP1
                op, s0, d = (x >> 8) & 0xff, x & 0xff, (x >> 16) & 0x7f
                size = 2 if s0 == 255 else 1
                names = {3: 's_mov_b32', 4: 's_mov_b64', 0x24: 's_and_saveexec_b64', 0x0a: 's_wqm_b64'}
                txt = f'{names.get(op, f"sop1_{op:#x}")} s{d}, {self.src(s0, nxt)}'
                self.sgpr_cb.pop(d, None)
            elif (x >> 23) == 0x17e:
                size = 2 if (x & 0xff) == 255 or ((x >> 8) & 0xff) == 255 else 1
                txt = f'sopc_{(x >> 16) & 0x7f:#x}'
            elif (x >> 28) == 0xb:
                size = 1
                txt = f'sopk_{(x >> 23) & 0x1f:#x} s{(x >> 16) & 0x7f}, {x & 0xffff:#x}'
            elif (x >> 30) == 2:
                op, s0, s1, d = (x >> 23) & 0x7f, x & 0xff, (x >> 8) & 0xff, (x >> 16) & 0x7f
                size = 2 if s0 == 255 or s1 == 255 else 1
                txt = f'sop2_{op:#x} s{d}, {self.src(s0, nxt)}, {self.src(s1, nxt)}'
            elif (x >> 27) == 0x18:  # SMRD
                op, d, base, imm, off = (x >> 22) & 0x1f, (x >> 15) & 0x7f, (x >> 9) & 0x3f, (x >> 8) & 1, x & 0xff
                size = 1
                if not imm and off == 255:
                    size = 2
                    where = f'word {nxt}'
                    for k in range(SCOUNT.get(op, 1)):
                        self.sgpr_cb[d + k] = f'cb[{nxt + k}]'
                elif imm:
                    where = f'dword {off}'
                    for k in range(SCOUNT.get(op, 1)):
                        self.sgpr_cb.pop(d + k, None)
                else:
                    where = f's{off}'
                txt = f'{SMRD.get(op, f"smrd_{op:#x}")} s[{d}:{d + SCOUNT.get(op, 1) - 1}], s[{base * 2}:{base * 2 + 1}], {where}'
            else:
                enc6 = x >> 26
                size = 2
                if enc6 == 0x34:  # VOP3
                    op, d, ab, clamp = (x >> 17) & 0x1ff, x & 0xff, (x >> 8) & 7, (x >> 11) & 1
                    s0, s1, s2 = nxt & 0x1ff, (nxt >> 9) & 0x1ff, (nxt >> 18) & 0x1ff
                    omod, neg = (nxt >> 27) & 3, (nxt >> 29) & 7

                    def m(k, s):
                        t = self.src(s)
                        if ab >> k & 1:
                            t = f'|{t}|'
                        if neg >> k & 1:
                            t = f'-{t}'
                        return t
                    if op >= 0x180:
                        name, args = VOP1.get(op - 0x180, f'vop1_{op - 0x180:#x}'), [m(0, s0)]
                    elif 0x100 <= op < 0x140:
                        name, args = VOP2.get(op - 0x100, f'vop2_{op - 0x100:#x}'), [m(0, s0), m(1, s1)]
                        if op == 0x100:
                            args.append(self.src(s2))
                    elif op < 0x100:
                        name, args = (f'v_cmp_{CMP[op & 15]}_f32' if op < 0x10 else f'vopc_{op:#x}'), [m(0, s0), m(1, s1)]
                    else:
                        name, args = VOP3.get(op, f'vop3_{op:#x}'), [m(0, s0), m(1, s1), m(2, s2)]
                    dst = f's[{d}:{d + 1}]' if op < 0x100 else f'v{d}'
                    mods = (' clamp' if clamp else '') + ({1: ' mul:2', 2: ' mul:4', 3: ' div:2'}.get(omod, ''))
                    txt = f'{name}_e64 {dst}, {", ".join(args)}{mods}'
                elif enc6 == 0x32:  # VINTRP
                    size = 1
                    vs, chan, attr, op, d = x & 0xff, (x >> 8) & 3, (x >> 10) & 0x3f, (x >> 16) & 3, (x >> 18) & 0xff
                    txt = f'v_interp_{["p1", "p2", "mov"][op] if op < 3 else op}_f32 v{d}, v{vs}, attr{attr}.{"xyzw"[chan]}'
                elif enc6 == 0x3c:  # MIMG
                    op, dmask = (x >> 18) & 0x7f, (x >> 8) & 0xf
                    vaddr, vdata, rsrc, samp = nxt & 0xff, (nxt >> 8) & 0xff, (nxt >> 16) & 0x1f, (nxt >> 21) & 0x1f
                    txt = (f'{MIMG.get(op, f"mimg_{op:#x}")} v[{vdata}..] mask {dmask:#x}, v{vaddr}, '
                           f'T#s[{rsrc * 4}:{rsrc * 4 + 7}], S#s[{samp * 4}:{samp * 4 + 3}]')
                elif enc6 == 0x3e:  # EXP
                    en, tgt, compr, done = x & 0xf, (x >> 4) & 0x3f, (x >> 10) & 1, (x >> 11) & 1
                    vs = [(nxt >> (8 * k)) & 0xff for k in range(4)]
                    txt = f'exp mrt{tgt} {" ".join(f"v{v}" for v in vs)} en {en:#x}{" compr" if compr else ""}{" done" if done else ""}'
                else:
                    txt = f'unknown {x:08x} {nxt:08x}'
            out.append(f'{pc:5x}: {txt}')
            i += size
        return out


if __name__ == '__main__':
    import bb_shader_scan
    shaders = bb_shader_scan.read_export(sys.argv[1])
    for h in sys.argv[2:]:
        print(f'===== {h}')
        print('\n'.join(Disasm().run(shaders[int(h, 16)]['code'])))
