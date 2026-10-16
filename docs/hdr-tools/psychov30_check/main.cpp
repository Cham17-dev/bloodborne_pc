#include "spirv_cross/external_interface.h"
#define GLM_FORCE_SWIZZLE
#include <glm/glm.hpp>
#include <cmath>
#include <cstdio>
#include <vector>
#include "glob.h"
glm::vec4* g_in; glm::vec4* g_out; PCData g_pc;
extern "C" const spirv_cross_interface* orig_iface(void);
extern "C" const spirv_cross_interface* port_iface(void);

static void run(const spirv_cross_interface* iface, std::vector<glm::vec4>& in, std::vector<glm::vec4>& out) {
    auto* sh = iface->construct();
    g_in = in.data(); g_out = out.data();
    glm::uvec3 groups(in.size() / 64, 1, 1), id(0, 0, 0);
    spirv_cross_set_builtin(sh, SPIRV_CROSS_BUILTIN_NUM_WORK_GROUPS, &groups, sizeof(groups));
    spirv_cross_set_builtin(sh, SPIRV_CROSS_BUILTIN_WORK_GROUP_ID, &id, sizeof(id));
    for (unsigned g = 0; g < groups.x; ++g) { id.x = g; iface->invoke(sh); }
    iface->destruct(sh);
}

int main() {
    std::vector<glm::vec4> in;
    // Neutral ramp (1e-4 .. 1e3), saturated and pastel colors at many levels, out-of-gamut
    // (negative components), a few dark ones.
    for (int i = 0; i < 64; ++i) { float v = std::pow(10.f, -4.f + 7.f * i / 63.f); in.push_back({v, v, v, 1}); }
    const float hues[][3] = {{1,0,0},{0,1,0},{0,0,1},{1,1,0},{0,1,1},{1,0,1},{1,.5f,0},{1,.2f,.1f},
                             {.2f,1,.3f},{.3f,.4f,1},{1,.9f,.7f},{.6f,.7f,1},{1.2f,-.05f,-.02f},
                             {-.1f,1,.2f},{.1f,-.08f,1},{.5f,.5f,-.2f}};
    for (auto& h : hues) for (int i = 0; i < 32; ++i) {
        float v = std::pow(10.f, -3.f + 6.f * i / 31.f); in.push_back({h[0]*v, h[1]*v, h[2]*v, 1}); }
    while (in.size() % 64) in.push_back({0.18f, 0.18f, 0.18f, 1});
    std::vector<glm::vec4> a(in.size()), b(in.size());
    struct Cfg { const char* name; PCData pc; } cfgs[] = {
        {"default (BT.2020 cube, h=1, BT.709 source)", {1000.f/203.f, 1, 1, 1.f}},
        {"BT.709 cube", {1000.f/203.f, 0, 1, 1.f}},
        {"auto compression (h auto)", {1000.f/203.f, 1, 1, 0.f}},
        {"source boundary none", {1000.f/203.f, 1, 0, 1.f}},
        {"source boundary BT.2020", {1000.f/203.f, 1, 2, 1.f}},
        {"source boundary AP1", {1000.f/203.f, 1, 3, 1.f}},
        {"peak 400/203", {400.f/203.f, 1, 1, 1.f}},
    };
    int bad_total = 0;
    for (auto& c : cfgs) {
        g_pc = c.pc;
        run(orig_iface(), in, a); run(port_iface(), in, b);
        double worst = 0; int worst_i = 0, nan_mismatch = 0;
        for (size_t i = 0; i < in.size(); ++i) for (int k = 0; k < 3; ++k) {
            float x = a[i][k], y = b[i][k];
            if (std::isnan(x) != std::isnan(y)) { ++nan_mismatch; continue; }
            if (std::isnan(x)) continue;
            double rel = std::fabs(x - y) / std::max(1e-3, (double)std::fabs(x));
            if (rel > worst) { worst = rel; worst_i = i; }
        }
        bool ok = worst < 1e-3 && !nan_mismatch;
        bad_total += !ok;
        printf("%-46s max rel. diff %.2e  NaN mismatches %d  %s\n", c.name, worst, nan_mismatch, ok ? "OK" : "DIFF");
        if (!ok) printf("   worst input (%g %g %g): orig (%g %g %g) port (%g %g %g)\n", in[worst_i].x, in[worst_i].y, in[worst_i].z,
                        a[worst_i].x, a[worst_i].y, a[worst_i].z, b[worst_i].x, b[worst_i].y, b[worst_i].z);
    }
    g_pc = cfgs[0].pc; run(orig_iface(), in, a);
    printf("\nsamples (default): input -> output\n");
    for (int i : {0, 20, 30, 38, 45, 55, 63, 64 + 20, 64 + 31, 96 + 25, 64 + 32 * 12 + 20})
        printf("  (%9.4g %9.4g %9.4g) -> (%9.4g %9.4g %9.4g)\n", in[i].x, in[i].y, in[i].z, a[i].x, a[i].y, a[i].z);
    return bad_total;
}
