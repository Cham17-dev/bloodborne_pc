"""PsychoV30 derived constants for host_shaders/hdr/psychov30.glsl, evaluated in double precision
from RenoDX color/rgb.hlsl (commit 0ede35a). Usage: python -I psychov30_consts.py"""
import numpy as np
A = lambda *r: np.array(r, dtype=np.float64).reshape(3, 3)
BT709_TO_XYZ = A(0.4123907993, 0.3575843394, 0.1804807884, 0.2126390059, 0.7151686788, 0.0721923154, 0.0193308187, 0.1191947798, 0.9505321522)
XYZ_TO_BT709 = A(3.2409699419, -1.5373831776, -0.4986107603, -0.9692436363, 1.8759675015, 0.0415550574, 0.0556300797, -0.2039769589, 1.0569715142)
BT2020_TO_XYZ = A(0.6369580483, 0.1446169036, 0.1688809752, 0.2627002120, 0.6779980715, 0.0593017165, 0.0, 0.0280726930, 1.0609850577)
XYZ_TO_BT2020 = A(1.7166511880, -0.3556707838, -0.2533662814, -0.6666843518, 1.6164812366, 0.0157685458, 0.0176398574, -0.0427706133, 0.9421031212)
AP1_TO_XYZ = A(0.6624541811, 0.1340042065, 0.1561876870, 0.2722287168, 0.6740817658, 0.0536895174, -0.0055746495, 0.0040607335, 1.0103391003)
D60_TO_D65 = A(0.987223982, -0.00611322838, 0.0159532874, -0.00759837171, 1.00186145, 0.00533003592, 0.00307257706, -0.00509596150, 1.08168065)
BT709_TO_AP1 = A(0.6130974024, 0.3395231462, 0.0473794514, 0.0701937225, 0.9163538791, 0.0134523985, 0.0206155929, 0.1095697729, 0.8698146342)
XYZ_TO_LMS = A(0.2670502842655792, 0.8471990148492798, -0.03470416612462053, -0.38706882411220156, 1.165429935890458, 0.10302286696614202, 0.026727793989083093, -0.02729131667566509, 0.5333267257603284)
LMS_TO_XYZ = np.linalg.inv(XYZ_TO_LMS)
LMS_TO_XF = A(1.94735469, -1.41445123, 0.36476327, 0.68990272, 0.34832189, 0.0, 0.0, 0.0, 1.93485343)
YF_ROW = LMS_TO_XF[1]
MB_W = np.array([YF_ROW[0], YF_ROW[1], 0.0371597069161])

M = {}
M['BT709_TO_LMS'] = XYZ_TO_LMS @ BT709_TO_XYZ
M['LMS_TO_BT709'] = XYZ_TO_BT709 @ LMS_TO_XYZ
M['LMS_TO_BT2020'] = XYZ_TO_BT2020 @ LMS_TO_XYZ
M['BT2020_TO_LMS'] = XYZ_TO_LMS @ BT2020_TO_XYZ
M['AP1_TO_LMS'] = XYZ_TO_LMS @ (D60_TO_D65 @ AP1_TO_XYZ)
M['BT709_TO_BT2020'] = XYZ_TO_BT2020 @ BT709_TO_XYZ
M['BT709_TO_AP1'] = BT709_TO_AP1
V = {}
for src in ('BT709', 'BT2020', 'AP1'):
    c = YF_ROW @ M[f'{src}_TO_LMS']
    p = np.maximum(c, 0)
    V[f'{src}_SOURCE_YF_WEIGHTS'] = p / p.sum()
white = M['BT709_TO_LMS'] @ np.ones(3)
V['D65_WHITE_LMS'] = white
wyf = YF_ROW @ white
aL = YF_ROW[0] * white[0] / wyf; aM = YF_ROW[1] * white[1] / wyf
S = {'D65_WHITE_YF': wyf, 'D65_ALPHA_L': aL, 'D65_ALPHA_M': aM, 'D65_ALPHA_DELTA': aL - aM}
D_LMS = np.array([aM * white[0], -aL * white[1], -0.5 * (aL - aM) * white[2]])
T_LMS = np.array([0, 0, 0.5 * white[2]])
V['BT709_D_RGB'] = M['LMS_TO_BT709'] @ D_LMS
V['BT709_T_RGB'] = M['LMS_TO_BT709'] @ T_LMS
V['BT2020_D_RGB'] = M['LMS_TO_BT2020'] @ D_LMS
V['BT2020_T_RGB'] = M['LMS_TO_BT2020'] @ T_LMS
V['YF_LMS'] = YF_ROW
V['MB_WEIGHTS'] = MB_W

f = lambda x: f'{x:.10g}'
out = []
for k, m in M.items():
    # GLSL mat3 takes columns: the columns of the HLSL (row-major) matrix, so M * v = mul(M, v).
    cols = ',\n    '.join(f'vec3({", ".join(f(x) for x in c)})' for c in m.T)
    out.append(f'const mat3 RD_PSY30_{k} = mat3(  // columns\n    {cols});')
for k, v in V.items():
    out.append(f'const vec3 RD_PSY30_{k} = vec3({", ".join(f(x) for x in v)});')
for k, s in S.items():
    out.append(f'const float RD_PSY30_{k} = {f(s)};')
print('\n'.join(out))
