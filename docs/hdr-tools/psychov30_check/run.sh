#!/usr/bin/env bash
# Numerical check of host_shaders/hdr/psychov30.glsl against RenoDX's test30.hlsl, on the CPU:
# both are compiled to SPIR-V by glslang, turned into C++ by SPIRV-Cross (--cpp) and run on 768
# colors x 7 configurations. Needs glslang, spirv-cross (with include/), glm and a RenoDX checkout:
#   GLSLANG=... SPIRV_CROSS=<SPIRV-Cross checkout with build/spirv-cross> GLM=... RENODX=... ./run.sh
# Result on 16/10/2026 (RenoDX 0ede35a): max relative difference 1.5e-4 (constants computed in
# double here, in float there), 1.3e-3 for one far out-of-gamut color with the AP1 source boundary.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
hdr="$here/../../../gpu/shadps4/video_core/host_shaders/hdr"
work=$(mktemp -d)
python3 -I "$here/prepare_hlsl.py" "$RENODX" "$work/hlsl"
"$GLSLANG" -V --target-env vulkan1.3 -I"$hdr" "$here/port.comp" -o "$work/port.spv" > /dev/null
"$GLSLANG" -V -D -e main -S comp --target-env vulkan1.3 -UVULKAN -I"$work/hlsl" "$here/orig.hlsl" -o "$work/orig.spv" > /dev/null
for t in orig port; do
  "$SPIRV_CROSS/build/spirv-cross" "$work/$t.spv" --cpp --output "$work/$t.cpp"
  python3 -I - "$work/$t.cpp" <<'PY'
import re, sys
p = sys.argv[1]; s = open(p).read()
s = s.replace('#include <stdint.h>\n', '#include <stdint.h>\n#include "glob.h"\n', 1)
s = s.replace('inputs._data[', 'g_in[').replace('outputs._data[', 'g_out[')
s = re.sub(r'_\d+\.inputs\[', 'g_in[', s); s = re.sub(r'_\d+\.outputs\[', 'g_out[', s)
s = re.sub(r'_\d+\.pc\.', 'g_pc.', s)
s = s.replace('#define pc __res->pc.get()', '#define pc g_pc').replace('s.register_push_constant(pc__);', '')
open(p, 'w').write(s)
PY
done
flags="-std=c++17 -O1 -w -DGLM_FORCE_SWIZZLE -DGLM_FORCE_RADIANS -I$SPIRV_CROSS/include -I$GLM -I$here"
for t in orig port; do
  g++ $flags -DSPIRV_CROSS_BRANCH= -DSPIRV_CROSS_FLATTEN= -DSPIRV_CROSS_UNROLL= -DSPIRV_CROSS_LOOP= \
    -DImpl=Impl_$t -Dspirv_cross_get_interface=${t}_iface -Dspirv_cross_construct=${t}_construct \
    -Dspirv_cross_destruct=${t}_destruct -Dspirv_cross_invoke=${t}_invoke -c "$work/$t.cpp" -o "$work/$t.o"
done
g++ $flags "$here/main.cpp" "$work/orig.o" "$work/port.o" -o "$work/check" -Wl,--allow-multiple-definition
"$work/check"
