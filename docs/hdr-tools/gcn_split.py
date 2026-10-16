"""Minimal GCN (Sea Islands / PS4) instruction splitter: yields (encoding, opcode, words).
Enough to fingerprint shaders (opcode histograms, sequences), not a full disassembler."""
import struct

VOP1 = {0x25: 'v_exp_f32', 0x27: 'v_log_f32', 0x2a: 'v_rcp_f32', 0x2e: 'v_rsq_f32', 0x33: 'v_sqrt_f32',
        0x01: 'v_mov_b32', 0x05: 'v_cvt_f32_i32', 0x0f: 'v_cvt_f16_f32', 0x0a: 'v_cvt_f16?', 0x20: 'v_fract_f32',
        0x1f: 'v_floor?'}
VOP2 = {0x03: 'v_add_f32', 0x04: 'v_sub_f32', 0x05: 'v_subrev_f32', 0x08: 'v_mul_f32', 0x0f: 'v_min_f32',
        0x10: 'v_max_f32', 0x1f: 'v_mac_f32', 0x20: 'v_madmk_f32', 0x21: 'v_madak_f32', 0x2f: 'v_cvt_pkrtz_f16_f32',
        0x00: 'v_cndmask_b32'}
VOP3_ONLY = {0x141: 'v_mad_f32', 0x14a: 'v_fma_f32', 0x151: 'v_min3_f32', 0x154: 'v_max3_f32',
             0x157: 'v_med3_f32'}
MIMG = {0x20: 'image_sample', 0x24: 'image_sample_l', 0x25: 'image_sample_b', 0x27: 'image_sample_lz',
        0x00: 'image_load', 0x08: 'image_store', 0x40: 'image_gather4', 0x2f: 'image_sample_lz_o?'}


def split(code):
    n = len(code) // 4
    w = struct.unpack(f'<{n}I', code[:n * 4])
    i = 0
    while i < n:
        x = w[i]
        top = x >> 23
        if (x >> 25) == 0x3f:  # VOP1
            op = (x >> 9) & 0xff
            size = 2 if (x & 0x1ff) == 255 else 1
            yield 'VOP1', op, w[i:i + size]
        elif (x >> 25) == 0x3e:  # VOPC
            op = (x >> 17) & 0xff
            size = 2 if (x & 0x1ff) == 255 else 1
            yield 'VOPC', op, w[i:i + size]
        elif x >> 31 == 0:  # VOP2
            op = (x >> 25) & 0x3f
            size = 2 if (x & 0x1ff) == 255 or op in (0x20, 0x21) else 1
            yield 'VOP2', op, w[i:i + size]
        elif top == 0x17f:  # SOPP
            op = (x >> 16) & 0x7f
            yield 'SOPP', op, w[i:i + 1]
            size = 1
            if op == 1:  # s_endpgm
                return
        elif top == 0x17d:  # SOP1
            size = 2 if (x & 0xff) == 255 else 1
            yield 'SOP1', (x >> 8) & 0xff, w[i:i + size]
        elif top == 0x17e:  # SOPC
            size = 2 if (x & 0xff) == 255 or ((x >> 8) & 0xff) == 255 else 1
            yield 'SOPC', (x >> 16) & 0x7f, w[i:i + size]
        elif (x >> 28) == 0xb:  # SOPK
            size = 1
            yield 'SOPK', (x >> 23) & 0x1f, w[i:i + 1]
        elif (x >> 30) == 2:  # SOP2
            size = 2 if (x & 0xff) == 255 or ((x >> 8) & 0xff) == 255 else 1
            yield 'SOP2', (x >> 23) & 0x7f, w[i:i + size]
        elif (x >> 27) == 0x18:  # SMRD
            size = 2 if (x & 0x1ff) == 255 else 1
            yield 'SMRD', (x >> 22) & 0x1f, w[i:i + size]
        else:
            enc6 = x >> 26
            size = 2
            if enc6 == 0x34:
                op = (x >> 17) & 0x1ff
                yield 'VOP3', op, w[i:i + 2]
            elif enc6 == 0x32:
                size = 1
                yield 'VINTRP', (x >> 16) & 3, w[i:i + 1]
            elif enc6 == 0x3c:
                yield 'MIMG', (x >> 18) & 0x7f, w[i:i + 2]
            elif enc6 == 0x3e:
                yield 'EXP', (x >> 4) & 0x3f, w[i:i + 2]  # target
            elif enc6 == 0x38:
                yield 'MUBUF', (x >> 18) & 0x7f, w[i:i + 2]
            elif enc6 == 0x3a:
                yield 'MTBUF', (x >> 16) & 7, w[i:i + 2]
            elif enc6 == 0x36:
                yield 'DS', (x >> 18) & 0xff, w[i:i + 2]
            elif enc6 == 0x37:
                yield 'FLAT', (x >> 18) & 0x7f, w[i:i + 2]
            else:
                yield 'UNK', x, w[i:i + 1]
                size = 1
        i += size


def name(enc, op):
    if enc == 'VOP1':
        return VOP1.get(op, f'vop1_{op:#x}')
    if enc == 'VOP2':
        return VOP2.get(op, f'vop2_{op:#x}')
    if enc == 'VOP3':
        if op >= 0x180:
            return VOP1.get(op - 0x180, f'vop1_{op - 0x180:#x}') + '_e64'
        if 0x100 <= op < 0x140:
            return VOP2.get(op - 0x100, f'vop2_{op - 0x100:#x}') + '_e64'
        return VOP3_ONLY.get(op, f'vop3_{op:#x}')
    if enc == 'MIMG':
        return MIMG.get(op, f'mimg_{op:#x}')
    return f'{enc.lower()}_{op:#x}'
