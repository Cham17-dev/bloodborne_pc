// Runs hdr/bb_composite.glsl (check.comp, through SPIRV-Cross C++) on the cases of a file:
//   in:  repeated batches { float cb[1024]; int opt[8]; vec4 data[64 * 4]; }
//   out: per batch vec4 out[64 * 5]
#define GLM_FORCE_SWIZZLE
#include <glm/glm.hpp>
#include <cstdio>
#include <vector>
#include "spirv_cross/external_interface.h"
#include "glob.h"
float* g_cb; glm::vec4* g_data; glm::vec4* g_out; int* g_opt;
extern "C" const spirv_cross_interface* bbc_iface(void);
int main(int argc, char** argv) {
    FILE* in = fopen(argv[1], "rb");
    FILE* out = fopen(argv[2], "wb");
    std::vector<float> cb(1024);
    std::vector<int> opt(8);
    std::vector<glm::vec4> data(256), res(320);
    while (fread(cb.data(), 4, 1024, in) == 1024) {
        fread(opt.data(), 4, 8, in);
        fread(data.data(), 16, 256, in);
        g_cb = cb.data(); g_opt = opt.data(); g_data = data.data(); g_out = res.data();
        auto* sh = bbc_iface()->construct();
        glm::uvec3 groups(1, 1, 1), id(0, 0, 0);
        spirv_cross_set_builtin(sh, SPIRV_CROSS_BUILTIN_NUM_WORK_GROUPS, &groups, sizeof(groups));
        spirv_cross_set_builtin(sh, SPIRV_CROSS_BUILTIN_WORK_GROUP_ID, &id, sizeof(id));
        bbc_iface()->invoke(sh);
        bbc_iface()->destruct(sh);
        fwrite(res.data(), 16, 320, out);
    }
    fclose(out);
    return 0;
}
