// SPDX-License-Identifier: MS-PL
// Rebuild with tools/shader_package/generate_depth_normal_prepass_hlsl.py.
#pragma once
#include <string_view>
namespace CNA::Graphics::detail::DepthNormalPrepassHlslGenerated {
// rigid.vulkan.vert.glsl SHA-256: 6fcfdd02e56a00e79728b5472e5f9e2e86bc583c3e9242135b3ca1dc09ecdabc
inline constexpr std::string_view kRigidVertexHlsl = R"CNA_HLSL(cbuffer EngineMatrices : register(b0)
{
    row_major float4x4 uReservedLightViewProjection : packoffset(c0);
    row_major float4x4 uWorld : packoffset(c4);
    row_major float4x4 uView : packoffset(c8);
    row_major float4x4 uProjection : packoffset(c12);
    row_major float4x4 uPreviousWorld : packoffset(c16);
    row_major float4x4 uPreviousViewProjection : packoffset(c20);
};

cbuffer FloatArray : register(b1)
{
    float uPrepassScalars[72] : packoffset(c0);
};


static float4 gl_Position;
static float3 aPosition;
static float4 vCurrentClip;
static float4 vPreviousClip;
static float3 vViewNormal;
static float3 aNormal;
static float vViewDepth;

struct SPIRV_Cross_Input
{
    float3 aPosition : POSITION;
    float3 aNormal : NORMAL;
};

struct SPIRV_Cross_Output
{
    float3 vViewNormal : TEXCOORD0;
    float vViewDepth : TEXCOORD1;
    float4 vCurrentClip : TEXCOORD2;
    float4 vPreviousClip : TEXCOORD3;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    float4 world = mul(float4(aPosition, 1.0f), uWorld);
    float4 view = mul(world, uView);
    gl_Position = mul(view, uProjection);
    vCurrentClip = gl_Position;
    gl_Position.y = -gl_Position.y;
    vPreviousClip = mul(mul(float4(aPosition, 1.0f), uPreviousWorld), uPreviousViewProjection);
    vViewNormal = normalize(mul(aNormal, mul(float3x3(uWorld[0].xyz, uWorld[1].xyz, uWorld[2].xyz), float3x3(uView[0].xyz, uView[1].xyz, uView[2].xyz))));
    vViewDepth = clamp((-view.z) / uPrepassScalars[0], 0.0f, 1.0f);
    gl_Position.y = -gl_Position.y;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    aPosition = stage_input.aPosition;
    aNormal = stage_input.aNormal;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.vCurrentClip = vCurrentClip;
    stage_output.vPreviousClip = vPreviousClip;
    stage_output.vViewNormal = vViewNormal;
    stage_output.vViewDepth = vViewDepth;
    return stage_output;
}
)CNA_HLSL";
// skinned.vulkan.vert.glsl SHA-256: 226d89ce984129dd9b50a7162e71a0986755d430dfd94aa115935a4a0354b28b
inline constexpr std::string_view kSkinnedVertexHlsl = R"CNA_HLSL(cbuffer Mat4Array : register(b0)
{
    row_major float4x4 uBones[72] : packoffset(c0);
};

cbuffer EngineMatrices : register(b1)
{
    row_major float4x4 uReservedLightViewProjection : packoffset(c0);
    row_major float4x4 uWorld : packoffset(c4);
    row_major float4x4 uView : packoffset(c8);
    row_major float4x4 uProjection : packoffset(c12);
    row_major float4x4 uPreviousWorld : packoffset(c16);
    row_major float4x4 uPreviousViewProjection : packoffset(c20);
};

cbuffer FloatArray : register(b2)
{
    float uPrepassScalars[72] : packoffset(c0);
};

cbuffer PushConstants : register(b3)
{
    float2 viewportSize : packoffset(c0);
    row_major float4x4 uMatrix : packoffset(c1);
    float4 uVector : packoffset(c5);
    float uWeightsPerVertex : packoffset(c6);
};


static float4 gl_Position;
static uint4 aBoneIndices;
static float4 aBoneWeights;
static float3 aPosition;
static float4 vCurrentClip;
static float4 vPreviousClip;
static float3 vViewNormal;
static float3 aNormal;
static float vViewDepth;
static float2 aUV;

struct SPIRV_Cross_Input
{
    float3 aPosition : POSITION;
    float3 aNormal : NORMAL;
    float2 aUV : TEXCOORD;
    float4 aBoneWeights : BLENDWEIGHT;
    uint4 aBoneIndices : BLENDINDICES;
};

struct SPIRV_Cross_Output
{
    float3 vViewNormal : TEXCOORD0;
    float vViewDepth : TEXCOORD1;
    float4 vCurrentClip : TEXCOORD2;
    float4 vPreviousClip : TEXCOORD3;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    float4x4 skin = uBones[int(aBoneIndices.x)] * aBoneWeights.x;
    if (uWeightsPerVertex >= 2.0f)
    {
        float4x4 _58 = uBones[int(aBoneIndices.y)] * aBoneWeights.y;
        skin = float4x4(skin[0] + _58[0], skin[1] + _58[1], skin[2] + _58[2], skin[3] + _58[3]);
    }
    if (uWeightsPerVertex >= 4.0f)
    {
        float4x4 _87 = uBones[int(aBoneIndices.z)] * aBoneWeights.z;
        float4x4 _96 = uBones[int(aBoneIndices.w)] * aBoneWeights.w;
        float4x4 _109 = float4x4(_87[0] + _96[0], _87[1] + _96[1], _87[2] + _96[2], _87[3] + _96[3]);
        skin = float4x4(skin[0] + _109[0], skin[1] + _109[1], skin[2] + _109[2], skin[3] + _109[3]);
    }
    float4 world = mul(float4(aPosition, 1.0f), mul(skin, uWorld));
    float4 view = mul(world, uView);
    gl_Position = mul(view, uProjection);
    vCurrentClip = gl_Position;
    gl_Position.y = -gl_Position.y;
    vPreviousClip = mul(mul(float4(aPosition, 1.0f), mul(skin, uPreviousWorld)), uPreviousViewProjection);
    vViewNormal = normalize(mul(aNormal, mul(float3x3(skin[0].xyz, skin[1].xyz, skin[2].xyz), mul(float3x3(uWorld[0].xyz, uWorld[1].xyz, uWorld[2].xyz), float3x3(uView[0].xyz, uView[1].xyz, uView[2].xyz)))));
    vViewDepth = clamp((-view.z) / uPrepassScalars[0], 0.0f, 1.0f);
    gl_Position.y = -gl_Position.y;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    aBoneIndices = stage_input.aBoneIndices;
    aBoneWeights = stage_input.aBoneWeights;
    aPosition = stage_input.aPosition;
    aNormal = stage_input.aNormal;
    aUV = stage_input.aUV;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.vCurrentClip = vCurrentClip;
    stage_output.vPreviousClip = vPreviousClip;
    stage_output.vViewNormal = vViewNormal;
    stage_output.vViewDepth = vViewDepth;
    return stage_output;
}
)CNA_HLSL";
// prepass.vulkan.frag.glsl SHA-256: f8d18544eb4e0625197a9404ff08b17933f5a98b921da06301b4e295ed8beee0
inline constexpr std::string_view kPrepassFragmentHlsl = R"CNA_HLSL(cbuffer FloatArray : register(b4)
{
    float uPrepassScalars[72] : packoffset(c0);
};


static float vViewDepth;
static float3 vViewNormal;
static float4 FragTarget0;
static float4 FragTarget1;

struct SPIRV_Cross_Input
{
    float3 vViewNormal : TEXCOORD0;
    float vViewDepth : TEXCOORD1;
};

struct SPIRV_Cross_Output
{
    float4 FragTarget0 : SV_Target0;
    float4 FragTarget1 : SV_Target1;
};

float4 cnaPackDepth(float value)
{
    float4 channels = frac(float4(16581375.0f, 65025.0f, 255.0f, 1.0f) * clamp(value, 0.0f, 0.999999940395355224609375f));
    channels -= (channels.xxyz * float4(0.0f, 0.0039215688593685626983642578125f, 0.0039215688593685626983642578125f, 0.0039215688593685626983642578125f));
    return channels;
}

void frag_main()
{
    float4 _53;
    if (uPrepassScalars[1] >= 0.5f)
    {
        float param = vViewDepth;
        _53 = cnaPackDepth(param);
    }
    else
    {
        _53 = float4(vViewDepth, vViewDepth, vViewDepth, 1.0f);
    }
    float4 depthOut = _53;
    float4 normalOut = float4((vViewNormal * 0.5f) + 0.5f.xxx, uPrepassScalars[3]);
    int outputMode = int(uPrepassScalars[2] + 0.5f);
    if (outputMode == 1)
    {
        FragTarget0 = depthOut;
        FragTarget1 = depthOut;
    }
    else
    {
        if (outputMode == 2)
        {
            FragTarget0 = normalOut;
            FragTarget1 = normalOut;
        }
        else
        {
            FragTarget0 = depthOut;
            FragTarget1 = normalOut;
        }
    }
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    vViewDepth = stage_input.vViewDepth;
    vViewNormal = stage_input.vViewNormal;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragTarget0 = FragTarget0;
    stage_output.FragTarget1 = FragTarget1;
    return stage_output;
}
)CNA_HLSL";
// prepass_velocity.vulkan.frag.glsl SHA-256: 3348d899ebf1c60bbb7655bc9a0e6dbf1d1e5c041c6f5e1e3079d9ec2c388c69
inline constexpr std::string_view kVelocityFragmentHlsl = R"CNA_HLSL(cbuffer FloatArray : register(b4)
{
    float uPrepassScalars[72] : packoffset(c0);
};


static float vViewDepth;
static float3 vViewNormal;
static float4 vCurrentClip;
static float4 vPreviousClip;
static float4 FragTarget0;
static float4 FragTarget1;
static float4 FragTarget2;

struct SPIRV_Cross_Input
{
    float3 vViewNormal : TEXCOORD0;
    float vViewDepth : TEXCOORD1;
    float4 vCurrentClip : TEXCOORD2;
    float4 vPreviousClip : TEXCOORD3;
};

struct SPIRV_Cross_Output
{
    float4 FragTarget0 : SV_Target0;
    float4 FragTarget1 : SV_Target1;
    float4 FragTarget2 : SV_Target2;
};

float4 cnaPackDepth(float value)
{
    float4 channels = frac(float4(16581375.0f, 65025.0f, 255.0f, 1.0f) * clamp(value, 0.0f, 0.999999940395355224609375f));
    channels -= (channels.xxyz * float4(0.0f, 0.0039215688593685626983642578125f, 0.0039215688593685626983642578125f, 0.0039215688593685626983642578125f));
    return channels;
}

float4 cnaNoVelocity()
{
    return float4(0.5f, 0.5f, 0.0f, 1.0f);
}

float4 cnaEncodeVelocity(float2 velocityUv)
{
    return float4(clamp((velocityUv * 0.5f) + 0.5f.xx, 0.0f.xx, 1.0f.xx), 0.0f, 0.0f);
}

float4 cnaVelocityOut(float4 currentClip, float4 previousClip)
{
    bool _72 = currentClip.w <= 0.0f;
    bool _79;
    if (!_72)
    {
        _79 = previousClip.w <= 0.0f;
    }
    else
    {
        _79 = _72;
    }
    if (_79)
    {
        return cnaNoVelocity();
    }
    float2 currentUv = ((currentClip.xy / currentClip.w.xx) * 0.5f) + 0.5f.xx;
    float2 previousUv = ((previousClip.xy / previousClip.w.xx) * 0.5f) + 0.5f.xx;
    float2 param = currentUv - previousUv;
    return cnaEncodeVelocity(param);
}

void frag_main()
{
    float4 _124;
    if (uPrepassScalars[1] >= 0.5f)
    {
        float param = vViewDepth;
        _124 = cnaPackDepth(param);
    }
    else
    {
        _124 = float4(vViewDepth, vViewDepth, vViewDepth, 1.0f);
    }
    float4 depthOut = _124;
    float4 normalOut = float4((vViewNormal * 0.5f) + 0.5f.xxx, uPrepassScalars[3]);
    float4 param_1 = vCurrentClip;
    float4 param_2 = vPreviousClip;
    float4 velocityOut = cnaVelocityOut(param_1, param_2);
    int outputMode = int(uPrepassScalars[2] + 0.5f);
    if (outputMode == 1)
    {
        FragTarget0 = depthOut;
        FragTarget1 = depthOut;
        FragTarget2 = depthOut;
    }
    else
    {
        if (outputMode == 2)
        {
            FragTarget0 = normalOut;
            FragTarget1 = normalOut;
            FragTarget2 = normalOut;
        }
        else
        {
            if (outputMode == 3)
            {
                FragTarget0 = velocityOut;
                FragTarget1 = velocityOut;
                FragTarget2 = velocityOut;
            }
            else
            {
                FragTarget0 = depthOut;
                FragTarget1 = normalOut;
                FragTarget2 = velocityOut;
            }
        }
    }
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    vViewDepth = stage_input.vViewDepth;
    vViewNormal = stage_input.vViewNormal;
    vCurrentClip = stage_input.vCurrentClip;
    vPreviousClip = stage_input.vPreviousClip;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragTarget0 = FragTarget0;
    stage_output.FragTarget1 = FragTarget1;
    stage_output.FragTarget2 = FragTarget2;
    return stage_output;
}
)CNA_HLSL";
}
