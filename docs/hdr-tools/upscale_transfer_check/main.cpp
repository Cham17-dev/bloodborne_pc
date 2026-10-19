// Exercises the compiled production GLSL functions (SPIRV-Cross' CPU backend), including FP16
// storage boundaries. No game, Vulkan device, screenshots, or reimplementation of the shader.
#include <spirv_cross/external_interface.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>
#include "glob.h"

glm::vec4* g_in;
glm::vec4* g_out;

int main() {
    std::vector<glm::vec4> inputs;
    for (int i = 0; i < 512; ++i) {
        const float nits = std::pow(10.f, -5.f + 9.f * i / 511.f);
        const float encoded = std::pow(nits / 203.f, 1.f / 2.2f);
        inputs.push_back({encoded, encoded, encoded, 1.f});
        inputs.push_back({encoded, encoded * 0.13f, encoded * 0.02f, 1.f});
        inputs.push_back({encoded * 0.02f, encoded * 0.13f, encoded, 1.f});
    }
    inputs.push_back({0.f, 0.f, 0.f, 1.f});
    inputs.push_back({1.f, 1.f, 1.f, 1.f});
    inputs.push_back({0.5f, 0.5f, 0.5f, 1.f});
    inputs.push_back({-1.f, 0.f, 2.f, 1.f});
    inputs.push_back({65504.f, 1000.f, 100.f, 1.f});
    const auto tested = inputs.size();
    while (inputs.size() % 64) inputs.push_back({0.18f, 0.18f, 0.18f, 1.f});
    // A backend that accidentally executes zero invocations must fail, including black cases.
    std::vector<glm::vec4> outputs(inputs.size() * 6,
                                  glm::vec4(std::numeric_limits<float>::quiet_NaN()));
    g_in = inputs.data(); g_out = outputs.data();
    const auto* iface = spirv_cross_get_interface();
    auto* shader = iface->construct();
    glm::uvec3 groups(unsigned(inputs.size() / 64), 1, 1), id(0);
    spirv_cross_set_builtin(shader, SPIRV_CROSS_BUILTIN_NUM_WORK_GROUPS, &groups, sizeof(groups));
    spirv_cross_set_builtin(shader, SPIRV_CROSS_BUILTIN_WORK_GROUP_ID, &id, sizeof(id));
    for (unsigned group = 0; group < groups.x; ++group) {
        id.x = group;
        iface->invoke(shader);
    }
    iface->destruct(shader);

    int failures = 0;
    double worst_storage_nits = 0;
    const auto check = [&](bool ok, const char* what, size_t i, int channel) {
        if (!ok && failures++ < 8) std::printf("FAIL %s case %zu channel %d\n", what, i, channel);
    };
    for (size_t i = 0; i < tested; ++i) {
        for (int channel = 0; channel < 3; ++channel) {
            const double signal = std::max(double(inputs[i][channel]), 0.0);
            const double light = std::min(std::pow(signal, 2.2), 65504.0);
            for (int result = 0; result < 6; ++result)
                check(std::isfinite(outputs[6 * i + result][channel]), "finite", i, channel);
            const double decoded = outputs[6 * i][channel];
            check(std::abs(decoded - light) <= std::max(2e-7, light * 3e-6),
                  "linear light", i, channel);
            const double roundtrip = outputs[6 * i + 2][channel];
            const double bounded_signal = std::min(signal, std::pow(65504.0, 1.0 / 2.2));
            check(std::abs(roundtrip - bounded_signal) <= std::max(2e-7, bounded_signal * 3e-6),
                  "round trip", i, channel);
            const double stored = outputs[6 * i + 3][channel];
            const double reconstructed_nits = std::pow(std::max(stored, 0.0), 2.2) * 203.0;
            const double error = std::abs(reconstructed_nits - light * 203.0);
            check(error <= std::max(3e-5, light * 203.0 * 0.002),
                  "FP16 round trip", i, channel);
            if (light * 203.0 <= 1.0) worst_storage_nits = std::max(worst_storage_nits, error);
            const double half_signal = outputs[6 * i + 4][channel];
            const double half_light = std::pow(std::max(half_signal, 0.0), 2.2);
            check(std::abs(half_light - light * 0.5) <= std::max(2e-7, light * 3e-6),
                  "half-light averaging", i, channel);
        }
        if (i % 3 == 0 && i < 1536) {
            check(outputs[6 * i + 3].x == outputs[6 * i + 3].y &&
                  outputs[6 * i + 3].y == outputs[6 * i + 3].z, "neutral grey", i, 0);
        }
    }
    // A non-linear input of 0.5 is about 0.218 of the light. Averaging white and black in
    // linear light instead gives a signal about 0.730; the two contracts must not be confused.
    const size_t white_case = 1537;
    check(std::abs(outputs[6 * white_case + 4].x - 0.72974005f) < 2e-6f,
          "white/black energy", white_case, 0);
    std::printf("Mode 5 transfer: %zu inputs, linear-light/round-trip/FP16/neutral/energy checks; "
                "max FP16 error below 1 nit %.6g nits; %d failures\n",
                tested, worst_storage_nits, failures);
    return failures ? 1 : 0;
}
