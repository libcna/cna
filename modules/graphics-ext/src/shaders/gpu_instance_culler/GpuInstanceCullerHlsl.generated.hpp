// SPDX-License-Identifier: MS-PL
// Rebuild with tools/shader_package/generate_culler_hlsl.py.
#pragma once
#include <string_view>
namespace CNA::Graphics::detail::GpuInstanceCullerHlslGenerated {
inline constexpr std::string_view kCullComputeSource = R"CNA_HLSL(struct CnaInstance
{
    row_major float4x4 world;
    float4 centre;
    float4 extent;
};

static const uint3 gl_WorkGroupSize = uint3(64u, 1u, 1u);

RWByteAddressBuffer _21 : register(u3);
RWByteAddressBuffer _43 : register(u0);
RWByteAddressBuffer _71 : register(u2);
RWByteAddressBuffer _106 : register(u1);

static uint3 gl_GlobalInvocationID;
struct SPIRV_Cross_Input
{
    uint3 gl_GlobalInvocationID : SV_DispatchThreadID;
};

void comp_main()
{
    uint index = gl_GlobalInvocationID.x;
    if (index >= _21.Load(20))
    {
        return;
    }
    float3 centre = asfloat(_43.Load4(index * 96 + 64)).xyz;
    float3 extent = asfloat(_43.Load4(index * 96 + 80)).xyz;
    for (int i = 0; i < 6; i++)
    {
        float4 plane = asfloat(_71.Load4(i * 16 + 0));
        float radius = dot(extent, abs(plane.xyz));
        if (((dot(plane.xyz, centre) + plane.w) - radius) > 0.0f)
        {
            return;
        }
    }
    uint _102;
    _21.InterlockedAdd(4, 1u, _102);
    uint slot = _102;
    float4x4 _111 = asfloat(uint4x4(_43.Load4(index * 96 + 0), _43.Load4(index * 96 + 16), _43.Load4(index * 96 + 32), _43.Load4(index * 96 + 48)));
    _106.Store4(slot * 64 + 0, asuint(_111[0]));
    _106.Store4(slot * 64 + 16, asuint(_111[1]));
    _106.Store4(slot * 64 + 32, asuint(_111[2]));
    _106.Store4(slot * 64 + 48, asuint(_111[3]));
}

[numthreads(64, 1, 1)]
void main(SPIRV_Cross_Input stage_input)
{
    gl_GlobalInvocationID = stage_input.gl_GlobalInvocationID;
    comp_main();
}
)CNA_HLSL";
inline constexpr std::string_view kCullComputeSourceSha256 = "f914253a3cd47dd2b0ba0ac9059ef0618065c7af55148d8e14ed45ef6a5706e5";
}
