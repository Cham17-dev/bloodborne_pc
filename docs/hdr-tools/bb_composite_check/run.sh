#!/usr/bin/env bash
# Checks hdr/bb_composite.glsl against bb_composite_model.py (itself checked against the game's code
# by gcn_emu.py): GLSLANG=... SPIRV_CROSS=<SPIRV-Cross checkout, built> GLM=... ./run.sh
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
hdr="$here/../../../gpu/shadps4/video_core/host_shaders/hdr"
work=$(mktemp -d)
"$GLSLANG" -V --target-env vulkan1.3 -I"$hdr" "$here/check.comp" -o "$work/bbc.spv" > /dev/null
"$SPIRV_CROSS/build/spirv-cross" "$work/bbc.spv" --cpp --output "$work/bbc.cpp"
python3 -I - "$work/bbc.cpp" <<'PY'
import re, sys
p = sys.argv[1]; s = open(p).read()
s = s.replace('#include <stdint.h>\n', '#include <stdint.h>\n#include "glob.h"\n', 1)
for name, glob in (('cb', 'g_cb'), ('data', 'g_data'), ('outv', 'g_out'), ('opt', 'g_opt')):
    s = re.sub(r'_\d+\.%s\[' % name, glob + '[', s)
open(p, 'w').write(s)
PY
flags="-std=c++17 -O1 -w -DGLM_FORCE_SWIZZLE -DGLM_FORCE_RADIANS -I$SPIRV_CROSS/include -I$GLM -I$here"
g++ $flags -DSPIRV_CROSS_BRANCH= -DSPIRV_CROSS_FLATTEN= -DSPIRV_CROSS_UNROLL= -DSPIRV_CROSS_LOOP= \
  -Dspirv_cross_get_interface=bbc_iface -c "$work/bbc.cpp" -o "$work/bbc.o"
g++ $flags "$here/main.cpp" "$work/bbc.o" -o "$work/check"
python3 -I "$here/compare.py" write "$work/cases.bin"
"$work/check" "$work/cases.bin" "$work/out.bin"
python3 -I "$here/compare.py" check "$work/cases.bin" "$work/out.bin"
