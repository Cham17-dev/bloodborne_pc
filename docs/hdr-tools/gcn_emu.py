"""Tiny single-lane GCN (Sea Islands) interpreter for Bloodborne's composite pixel shaders, to check
bbport's reimplementation of them (bb_composite_model.py) against the game's own code.

Scope: the straight-line float code of a post-process pixel shader. Texture fetches return the
value given for the T# (by its first SGPR), interpolants return the value given per attribute,
s_buffer_load reads a constant array by word, s_load_* of descriptor tables only label SGPRs.
Unknown instructions raise, so nothing is silently skipped."""
import math, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gcn_disasm

F32 = struct.Struct('<f')
U32 = struct.Struct('<I')


def f2u(f):
    return U32.unpack(F32.pack(f))[0]


def u2f(u):
    return F32.unpack(U32.pack(u & 0xffffffff))[0]


def r32(x):  # round to float32
    try:
        return F32.unpack(F32.pack(x))[0]
    except OverflowError:
        return math.copysign(math.inf, x)


SCOUNT = gcn_disasm.SCOUNT


class Lane:
    def __init__(self, cb, textures, attrs):
        self.cb = cb            # dict word -> float
        self.textures = textures  # callable (attr, dmask) -> (r, g, b, a)
        self.attrs = attrs      # dict attr -> (x, y, z, w)
        self.s = {}             # sgpr -> uint32 ('tag' strings for descriptors)
        self.v = {}             # vgpr -> float
        self.vcc = False
        self.out = None
        self.cb_sgpr = {}       # sgpr -> constant word (for reads)
        self.samples = []       # (T# sgpr, attr, mask)

    # -- operands
    def src(self, v, lit=None):
        if v < 104:
            if v in self.cb_sgpr:
                return self.cb.get(self.cb_sgpr[v], 0.0)
            x = self.s.get(v, 0)
            return u2f(x) if isinstance(x, int) else 0.0
        if v == 128:
            return 0.0
        if 129 <= v <= 192:
            return u2f(v - 128)  # integer bits (used only as such)
        if 193 <= v <= 208:
            return u2f((-(v - 192)) & 0xffffffff)
        if v in (240, 241, 242, 243, 244, 245, 246, 247):
            return [0.5, -0.5, 1.0, -1.0, 2.0, -2.0, 4.0, -4.0][v - 240]
        if v == 255:
            return u2f(lit)
        if v >= 256:
            return self.v.get(v - 256, 0.0)
        if v == 106 or v == 107:
            return 0.0
        raise NotImplementedError(f'operand {v}')

    def run(self, code):
        n = len(code) // 4
        w = struct.unpack(f'<{n}I', code[:n * 4])
        i = 0
        while i < n:
            x = w[i]
            nxt = w[i + 1] if i + 1 < n else 0
            size = self.step(x, nxt)
            if size == 0:
                return self.out
            i += size
        return self.out

    def setv(self, d, val, clamp=False, omod=0):
        if omod == 1:
            val *= 2
        elif omod == 2:
            val *= 4
        elif omod == 3:
            val *= 0.5
        if clamp:
            val = min(max(val, 0.0), 1.0) if not math.isnan(val) else 0.0
        self.v[d] = r32(val)

    def alu(self, name, a):
        if name in ('v_add_f32',):
            return a[0] + a[1]
        if name == 'v_sub_f32':
            return a[0] - a[1]
        if name == 'v_subrev_f32':
            return a[1] - a[0]
        if name in ('v_mul_f32', 'v_mul_legacy_f32'):
            return a[0] * a[1]
        if name == 'v_min_f32':
            return min(a[0], a[1])
        if name == 'v_max_f32':
            return max(a[0], a[1])
        if name in ('v_mad_f32', 'v_fma_f32', 'v_mad_legacy_f32'):
            return a[0] * a[1] + a[2]
        if name == 'v_min3_f32':
            return min(a)
        if name == 'v_max3_f32':
            return max(a)
        if name == 'v_med3_f32':
            return sorted(a)[1]
        if name == 'v_mov_b32':
            return a[0]
        if name == 'v_exp_f32':
            try:
                return 2.0 ** a[0]
            except OverflowError:
                return math.inf
        if name == 'v_log_f32':
            return math.log2(a[0]) if a[0] > 0 else (-math.inf if a[0] == 0 else math.nan)
        if name == 'v_rcp_f32':
            return 1.0 / a[0] if a[0] != 0 else math.copysign(math.inf, a[0])
        if name == 'v_rsq_f32':
            return 1.0 / math.sqrt(a[0]) if a[0] > 0 else math.inf
        if name == 'v_sqrt_f32':
            return math.sqrt(a[0]) if a[0] >= 0 else math.nan
        if name == 'v_floor_f32':
            return math.floor(a[0])
        if name == 'v_fract_f32':
            return a[0] - math.floor(a[0])
        raise NotImplementedError(name)

    def step(self, x, nxt):
        D = gcn_disasm
        if (x >> 25) == 0x3f:  # VOP1
            op, s0, d = (x >> 9) & 0xff, x & 0x1ff, (x >> 17) & 0xff
            self.setv(d, self.alu(D.VOP1[op], [self.src(s0, nxt)]))
            return 2 if s0 == 255 else 1
        if (x >> 25) == 0x3e:  # VOPC
            op, s0, s1 = (x >> 17) & 0xff, x & 0x1ff, (x >> 9) & 0xff
            self.vcc = self.cmp(op, self.src(s0, nxt), self.v.get(s1, 0.0))
            return 2 if s0 == 255 else 1
        if x >> 31 == 0:  # VOP2
            op, s0, s1, d = (x >> 25) & 0x3f, x & 0x1ff, (x >> 9) & 0xff, (x >> 17) & 0xff
            a, b = self.src(s0, nxt), self.v.get(s1, 0.0)
            if op == 0x20:  # madmk: d = s0 * K + v1
                self.setv(d, a * u2f(nxt) + b)
                return 2
            if op == 0x21:  # madak: d = s0 * v1 + K
                self.setv(d, a * b + u2f(nxt))
                return 2
            if op == 0x1f:  # mac
                self.setv(d, a * b + self.v.get(d, 0.0))
            elif op == 0x00:  # cndmask: vcc ? v1 : s0
                self.setv(d, b if self.vcc else a)
            elif op == 0x2f:  # cvt_pkrtz: keep both floats
                self.v[d] = (r32(a), r32(b))
            else:
                self.setv(d, self.alu(D.VOP2[op], [a, b]))
            return 2 if s0 == 255 else 1
        if (x >> 23) == 0x17f:  # SOPP
            op = (x >> 16) & 0x7f
            if op == 1:
                return 0
            if op in (0, 0xc):
                return 1
            raise NotImplementedError(f'sopp {op:#x}')
        if (x >> 23) == 0x17d:  # SOP1
            op, s0, d = (x >> 8) & 0xff, x & 0xff, (x >> 16) & 0x7f
            size = 2 if s0 == 255 else 1
            if op == 3:  # s_mov_b32
                self.cb_sgpr.pop(d, None)
                if s0 == 255:
                    self.s[d] = nxt
                elif s0 < 104:
                    if s0 in self.cb_sgpr:
                        self.cb_sgpr[d] = self.cb_sgpr[s0]
                    else:
                        self.s[d] = self.s.get(s0, 0)
                elif s0 in (240, 241, 242, 243, 244, 245, 246, 247):
                    self.s[d] = f2u(self.src(s0))
                else:
                    self.s[d] = 0
            # s_mov_b64 / s_wqm / saveexec: exec bookkeeping only
            return size
        if (x >> 23) == 0x17e or (x >> 28) == 0xb or (x >> 30) == 2:
            raise NotImplementedError(f'scalar {x:08x}')
        if (x >> 27) == 0x18:  # SMRD
            op, d, imm, off = (x >> 22) & 0x1f, (x >> 15) & 0x7f, (x >> 8) & 1, x & 0xff
            count = SCOUNT[op]
            if op >= 8:  # s_buffer_load
                word = nxt if (not imm and off == 255) else (off if imm else None)
                if word is None:
                    raise NotImplementedError('buffer load with sgpr offset')
                for k in range(count):
                    self.cb_sgpr[d + k] = word + k
                return 2 if (not imm and off == 255) else 1
            # s_load from the descriptor table: label the SGPRs with their table dword
            for k in range(count):
                self.cb_sgpr.pop(d + k, None)
                self.s[d + k] = f'table{off + k}' if imm else 'table?'
            return 1
        enc6 = x >> 26
        if enc6 == 0x34:  # VOP3
            op, d, ab, clamp = (x >> 17) & 0x1ff, x & 0xff, (x >> 8) & 7, (x >> 11) & 1
            s = [nxt & 0x1ff, (nxt >> 9) & 0x1ff, (nxt >> 18) & 0x1ff]
            omod, neg = (nxt >> 27) & 3, (nxt >> 29) & 7
            vals = []
            for k in range(3):
                v = self.src(s[k])
                if ab >> k & 1:
                    v = abs(v)
                if neg >> k & 1:
                    v = -v
                vals.append(v)
            if op < 0x100:
                self.vcc = self.cmp(op, vals[0], vals[1])  # written to an SGPR pair: used as vcc
                return 2
            if op >= 0x180:
                name = D.VOP1[op - 0x180]
                val = self.alu(name, vals[:1])
            elif op >= 0x100 and op < 0x140:
                sub = op - 0x100
                if sub == 0x1f:
                    val = vals[0] * vals[1] + self.v.get(d, 0.0)
                elif sub == 0x00:
                    val = vals[1] if self.vcc else vals[0]
                else:
                    val = self.alu(D.VOP2[sub], vals[:2])
            else:
                val = self.alu(D.VOP3[op], vals)
            self.setv(d, val, clamp, omod)
            return 2
        if enc6 == 0x32:  # VINTRP
            chan, attr, op, d = (x >> 8) & 3, (x >> 10) & 0x3f, (x >> 16) & 3, (x >> 18) & 0xff
            self.v[d] = float(self.attrs[attr][chan])
            self.last_attr = getattr(self, 'last_attr', {})
            self.last_attr[d] = attr
            return 1
        if enc6 == 0x3c:  # MIMG
            dmask = (x >> 8) & 0xf
            vaddr, vdata, rsrc = nxt & 0xff, (nxt >> 8) & 0xff, (nxt >> 16) & 0x1f
            attr = getattr(self, 'last_attr', {}).get(vaddr, '?')
            tex = self.textures(attr, dmask)
            self.samples.append((rsrc * 4, attr, dmask))
            k = 0
            for c in range(4):
                if dmask >> c & 1:
                    self.v[vdata + k] = r32(tex[c])
                    k += 1
            return 2
        if enc6 == 0x3e:  # EXP
            en, tgt, compr = x & 0xf, (x >> 4) & 0x3f, (x >> 10) & 1
            vs = [(nxt >> (8 * k)) & 0xff for k in range(4)]
            if tgt == 0:
                if compr:
                    lo, hi = self.v[vs[0]], self.v[vs[1]]
                    self.out = (lo[0], lo[1], hi[0], hi[1])
                else:
                    self.out = tuple(self.v.get(v, 0.0) for v in vs)
            return 2
        raise NotImplementedError(f'{x:08x}')

    @staticmethod
    def cmp(op, a, b):
        k = op & 15
        return {1: a < b, 2: a == b, 3: a <= b, 4: a > b, 5: a != b, 6: a >= b, 9: not (a >= b),
                11: not (a > b), 12: not (a <= b), 13: not (a == b), 14: not (a < b), 15: True,
                0: False}[k]
