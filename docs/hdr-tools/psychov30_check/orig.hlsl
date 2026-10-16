#include "tonemap/psychov/test30.hlsl"
StructuredBuffer<float4> inputs : register(t0);
RWStructuredBuffer<float4> outputs : register(u1);
struct P { float peak; int mode; int boundary; float compression; };
[[vk::push_constant]] P pc;
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  float3 c = inputs[id.x].xyz;
  outputs[id.x] = float4(psychotm_test30(
      c, pc.peak, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 100.f, 1.f, 1.f, 0, 1.f, 0.18f, 0.18f,
      1.f, pc.mode, 1.f, pc.compression, pc.boundary), 1.f);
}
