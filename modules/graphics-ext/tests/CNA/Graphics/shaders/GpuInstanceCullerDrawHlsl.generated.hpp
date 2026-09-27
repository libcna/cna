// SPDX-License-Identifier: MS-PL
// Rebuild with tools/shader_package/generate_culler_hlsl.py.
#pragma once
#include <string_view>
namespace CNA::Tests::GpuInstanceCullerDrawHlslGenerated {
inline constexpr std::string_view kDrawVertexSource = R"CNA_HLSL(ByteAddressBuffer _15 : register(t6);

static float4 gl_Position;
static int gl_InstanceIndex;
static float3 aPos;

struct SPIRV_Cross_Input
{
    float3 aPos : POSITION;
    uint gl_InstanceIndex : SV_InstanceID;
};

struct SPIRV_Cross_Output
{
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    float4x4 _23 = asfloat(uint4x4(_15.Load4(gl_InstanceIndex * 64 + 0), _15.Load4(gl_InstanceIndex * 64 + 16), _15.Load4(gl_InstanceIndex * 64 + 32), _15.Load4(gl_InstanceIndex * 64 + 48)));
    float4 world = mul(float4(aPos, 1.0f), _23);
    gl_Position = float4(world.x / 4.0f, world.y / 5.0f, 0.5f, 1.0f);
    gl_Position.y = -gl_Position.y;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_InstanceIndex = int(stage_input.gl_InstanceIndex);
    aPos = stage_input.aPos;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    return stage_output;
}
)CNA_HLSL";
inline constexpr std::string_view kDrawVertexSourceSha256 = "225c895a3db2bfd881233aab6b637df65a1ecf30760315661499f2506422f01f";
inline constexpr std::string_view kDrawFragmentSource = R"CNA_HLSL(static float4 FragColor;

struct SPIRV_Cross_Output
{
    float4 FragColor : SV_Target0;
};

void frag_main()
{
    FragColor = 1.0f.xxxx;
}

SPIRV_Cross_Output main()
{
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
)CNA_HLSL";
inline constexpr std::string_view kDrawFragmentSourceSha256 = "50fc1538740e6fb0e46b8aa425e37b1cdf52494a2cfb03151c2b3540abfb488f";
}
