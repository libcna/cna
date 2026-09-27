// SPDX-License-Identifier: MS-PL
// Rebuild with tools/shader_package/generate_transparency_test_hlsl.py.
#pragma once
#include <string_view>
namespace CNA::Tests::TransparencyHlslGenerated {
// basic.vulkan.vert.glsl SHA-256: 776d5215c78990640065ef09013a993b20e4805f3071a57e68dcf67d0120d679
inline constexpr std::string_view kBasicVertexHlsl = R"CNA_HLSL(static float4 gl_Position;
static float3 aPos;

struct SPIRV_Cross_Input
{
    float3 aPos : POSITION;
};

struct SPIRV_Cross_Output
{
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    gl_Position = float4(aPos.xy, (aPos.z * 0.5f) + 0.5f, 1.0f);
    gl_Position.y = -gl_Position.y;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    aPos = stage_input.aPos;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    return stage_output;
}
)CNA_HLSL";
// direct.vulkan.vert.glsl SHA-256: f35a78121f7801b43e35571b17fba11d7cfe8f82eb3fb3c139c92cc12a45001a
inline constexpr std::string_view kDirectVertexHlsl = R"CNA_HLSL(static float4 gl_Position;
static float3 aPos;
static float PositionX;

struct SPIRV_Cross_Input
{
    float3 aPos : POSITION;
};

struct SPIRV_Cross_Output
{
    float PositionX : TEXCOORD0;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    gl_Position = float4(aPos.xy, (aPos.z * 0.5f) + 0.5f, 1.0f);
    PositionX = (aPos.x * 0.5f) + 0.5f;
    gl_Position.y = -gl_Position.y;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    aPos = stage_input.aPos;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.PositionX = PositionX;
    return stage_output;
}
)CNA_HLSL";
// emitter.vulkan.frag.glsl SHA-256: 4f3a2984288cee6af3da9185499d9fd934830d6f0fdb81e2c24430ee818ab003
inline constexpr std::string_view kEmitterFragmentHlsl = R"CNA_HLSL(cbuffer PushConstants : register(b4)
{
    float2 viewportSize : packoffset(c0);
    row_major float4x4 uMatrix : packoffset(c1);
    float4 uEffectParams : packoffset(c5);
    float uScalar : packoffset(c6);
};


static float4 cnaOitAccumulation;
static float4 cnaOitRevealage;

struct SPIRV_Cross_Output
{
    float4 cnaOitAccumulation : SV_Target0;
    float4 cnaOitRevealage : SV_Target1;
};

void frag_main()
{
    float alpha = uEffectParams.w;
    float z = clamp(uScalar, 0.0f, 1.0f);
    float weight = alpha * clamp(0.02999999932944774627685546875f / (9.9999997473787516355514526367188e-06f + pow(z, 4.0f)), 0.00999999977648258209228515625f, 3000.0f);
    cnaOitAccumulation = float4((uEffectParams.xyz * alpha) * weight, alpha * weight);
    cnaOitRevealage = float4(log(max(1.0f - alpha, 9.9999997473787516355514526367188e-05f)), 0.0f, 0.0f, 0.0f);
}

SPIRV_Cross_Output main()
{
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.cnaOitAccumulation = cnaOitAccumulation;
    stage_output.cnaOitRevealage = cnaOitRevealage;
    return stage_output;
}
)CNA_HLSL";
// flat.vulkan.frag.glsl SHA-256: 331c06008eeb349df23c17e43bad242bf7142a88d2c01f7c515afd4884b387e4
inline constexpr std::string_view kFlatFragmentHlsl = R"CNA_HLSL(cbuffer PushConstants : register(b4)
{
    float2 viewportSize : packoffset(c0);
    row_major float4x4 uMatrix : packoffset(c1);
    float4 uEffectParams : packoffset(c5);
};


static float4 FragColor;

struct SPIRV_Cross_Output
{
    float4 FragColor : SV_Target0;
};

void frag_main()
{
    FragColor = uEffectParams;
}

SPIRV_Cross_Output main()
{
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
)CNA_HLSL";
// weight_probe.vulkan.frag.glsl SHA-256: 5e20a04820826693398482a6c714f7a7d4c2d223c35eac0fbe51d932aa5355b8
inline constexpr std::string_view kWeightProbeFragmentHlsl = R"CNA_HLSL(cbuffer PushConstants : register(b4)
{
    float2 viewportSize : packoffset(c0);
    row_major float4x4 uMatrix : packoffset(c1);
    float4 uEffectParams : packoffset(c5);
};


static float PositionX;
static float4 FragColor;

struct SPIRV_Cross_Input
{
    float PositionX : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float4 FragColor : SV_Target0;
};

void frag_main()
{
    float depth = lerp(uEffectParams.y, uEffectParams.z, PositionX);
    float z = clamp(depth / max(uEffectParams.w, 9.9999997473787516355514526367188e-05f), 0.0f, 1.0f);
    float weight = uEffectParams.x * clamp(0.02999999932944774627685546875f / (9.9999997473787516355514526367188e-06f + pow(z, 4.0f)), 0.00999999977648258209228515625f, 3000.0f);
    float encoded = ((log(weight) / 2.3025848865509033203125f) + 2.0f) / 5.5f;
    FragColor = float4(clamp(encoded, 0.0f, 1.0f), 0.0f, 0.0f, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    PositionX = stage_input.PositionX;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
)CNA_HLSL";
}
