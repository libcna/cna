// SPDX-License-Identifier: MS-PL
// Rebuild with tools/shader_package/generate_scene_hlsl.py.
#pragma once
#include <array>
#include <string_view>
namespace CNA::Graphics::detail::SceneHlslGenerated {
struct Stage { std::string_view label; std::string_view source; std::string_view sourceSha256; };
inline constexpr std::string_view kStage0 = R"CNA_HLSL(cbuffer PushConstants : register(b0)
{
    float2 viewportSize : packoffset(c0);
};


static float4 gl_Position;
static float2 aPos;
static float2 TexCoord;
static float2 aTexCoord;
static float4 SpriteColor;
static float4 aColor;

struct SPIRV_Cross_Input
{
    float2 aPos : POSITION;
    float2 aTexCoord : TEXCOORD;
    float4 aColor : COLOR;
};

struct SPIRV_Cross_Output
{
    float2 TexCoord : TEXCOORD0;
    float4 SpriteColor : TEXCOORD1;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    float2 ndc = ((aPos / viewportSize) * 2.0f) - 1.0f.xx;
    gl_Position = float4(ndc, 0.0f, 1.0f);
    TexCoord = aTexCoord;
    SpriteColor = aColor;
    gl_Position.y = -gl_Position.y;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    aPos = stage_input.aPos;
    aTexCoord = stage_input.aTexCoord;
    aColor = stage_input.aColor;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.TexCoord = TexCoord;
    stage_output.SpriteColor = SpriteColor;
    return stage_output;
}
)CNA_HLSL";
inline constexpr std::string_view kStage1 = R"CNA_HLSL(cbuffer Mat4Array : register(b4)
{
    row_major float4x4 uSkyMatrices[72] : packoffset(c0);
};

cbuffer Vec3Array : register(b5)
{
    float3 uSkyVectors[72] : packoffset(c0);
};

cbuffer FloatArray : register(b6)
{
    float uSkyScalars[72] : packoffset(c0);
};

Texture2D<float4> texture1 : register(t0);
SamplerState _texture1_sampler : register(s0);

static float2 TexCoord;
static float4 FragColor;
static float4 SpriteColor;

struct SPIRV_Cross_Input
{
    float2 TexCoord : TEXCOORD0;
    float4 SpriteColor : TEXCOORD1;
};

struct SPIRV_Cross_Output
{
    float4 FragColor : SV_Target0;
};

float cnaAirMass(float upwards)
{
    float up = clamp(upwards, 0.0f, 1.0f);
    float zenithDegrees = degrees(acos(up));
    return 1.0f / max(up + (0.50572001934051513671875f * pow(max(96.07994842529296875f - zenithDegrees, 0.001000000047497451305389404296875f), -1.6363999843597412109375f)), 9.9999997473787516355514526367188e-05f);
}

float cnaRayleighPhase(float cosAngle)
{
    return 0.0596831031143665313720703125f * (1.0f + (cosAngle * cosAngle));
}

float cnaMiePhase(float cosAngle)
{
    float gg = 0.577600002288818359375f;
    float d = (1.0f + gg) - (1.519999980926513671875f * cosAngle);
    return (0.079577468335628509521484375f * (1.0f - gg)) / max(pow(max(d, 9.9999997473787516355514526367188e-05f), 1.5f) * (2.0f + gg), 9.9999997473787516355514526367188e-05f);
}

float3 cnaScatteringAlongPath(float3 viewDirection, float3 sunDirection, float turbidity, float viewMass)
{
    float3 view = normalize(viewDirection);
    float3 toSun = -normalize(sunDirection);
    float cosAngle = dot(view, toSun);
    float param = toSun.y;
    float sunMass = cnaAirMass(param);
    float mie = 0.02099999971687793731689453125f * max(turbidity - 1.0f, 0.0f);
    float3 total = float3(0.0463999994099140167236328125f, 0.108499996364116668701171875f, 0.26499998569488525390625f) + mie.xxx;
    float param_1 = cosAngle;
    float param_2 = cosAngle;
    float3 scattered = (float3(0.0463999994099140167236328125f, 0.108499996364116668701171875f, 0.26499998569488525390625f) * cnaRayleighPhase(param_1)) + (mie * cnaMiePhase(param_2)).xxx;
    float3 alongView = 1.0f.xxx - exp((-total) * viewMass);
    float3 sunlight = exp((-total) * sunMass);
    return (((scattered / total) * alongView) * sunlight) * 24.0f;
}

float3 cnaSkyRadiance(float3 viewDirection, float3 sunDirection, float turbidity)
{
    float param = normalize(viewDirection).y;
    float3 param_1 = viewDirection;
    float3 param_2 = sunDirection;
    float param_3 = turbidity;
    float param_4 = cnaAirMass(param);
    return cnaScatteringAlongPath(param_1, param_2, param_3, param_4);
}

void frag_main()
{
    float2 cameraUv = float2(TexCoord.x, 1.0f - TexCoord.y);
    float2 ndc = (cameraUv * 2.0f) - 1.0f.xx;
    float4 ray = mul(float4(ndc, 1.0f, 1.0f), uSkyMatrices[0]);
    float3 direction = normalize(ray.xyz / ray.w.xxx);
    float3 param = direction;
    float3 param_1 = uSkyVectors[0];
    float param_2 = uSkyScalars[0];
    float3 radiance = cnaSkyRadiance(param, param_1, param_2) * uSkyScalars[1];
    FragColor = float4(radiance, 1.0f) * SpriteColor;
    FragColor.w += (texture1.Sample(_texture1_sampler, TexCoord).w * 0.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    TexCoord = stage_input.TexCoord;
    SpriteColor = stage_input.SpriteColor;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
)CNA_HLSL";
inline constexpr std::string_view kStage2 = R"CNA_HLSL(cbuffer PushConstants : register(b0)
{
    float2 viewportSize : packoffset(c0);
    row_major float4x4 uInvViewProj : packoffset(c1);
    float4 uTintIntensity : packoffset(c5);
    float uYaw : packoffset(c6);
};


static float4 gl_Position;
static float2 aPos;
static float2 TexCoord;
static float2 aTexCoord;
static float4 SpriteColor;
static float4 aColor;

struct SPIRV_Cross_Input
{
    float2 aPos : POSITION;
    float2 aTexCoord : TEXCOORD;
    float4 aColor : COLOR;
};

struct SPIRV_Cross_Output
{
    float2 TexCoord : TEXCOORD0;
    float4 SpriteColor : TEXCOORD1;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    float2 ndc = ((aPos / viewportSize) * 2.0f) - 1.0f.xx;
    gl_Position = float4(ndc.x, -ndc.y, 0.0f, 1.0f);
    TexCoord = aTexCoord;
    SpriteColor = aColor;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    aPos = stage_input.aPos;
    aTexCoord = stage_input.aTexCoord;
    aColor = stage_input.aColor;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.TexCoord = TexCoord;
    stage_output.SpriteColor = SpriteColor;
    return stage_output;
}
)CNA_HLSL";
inline constexpr std::string_view kStage3 = R"CNA_HLSL(cbuffer PushConstants : register(b4)
{
    float2 viewportSize : packoffset(c0);
    row_major float4x4 uInvViewProj : packoffset(c1);
    float4 uTintIntensity : packoffset(c5);
    float uYaw : packoffset(c6);
};

TextureCube<float4> uEnvironment : register(t1);
SamplerState _uEnvironment_sampler : register(s1);
Texture2D<float4> texture1 : register(t0);
SamplerState _texture1_sampler : register(s0);

static float2 TexCoord;
static float4 FragColor;
static float4 SpriteColor;

struct SPIRV_Cross_Input
{
    float2 TexCoord : TEXCOORD0;
    float4 SpriteColor : TEXCOORD1;
};

struct SPIRV_Cross_Output
{
    float4 FragColor : SV_Target0;
};

void frag_main()
{
    float2 ndc = (TexCoord * 2.0f) - 1.0f.xx;
    float4 farPoint = mul(float4(ndc, 1.0f, 1.0f), uInvViewProj);
    float3 direction = normalize((farPoint.xyz / max(abs(farPoint.w), 9.9999999747524270787835121154785e-07f).xxx) * sign(farPoint.w));
    float yawSin = sin(uYaw);
    float yawCos = cos(uYaw);
    float3 rotated = float3((direction.x * yawCos) + (direction.z * yawSin), direction.y, ((-direction.x) * yawSin) + (direction.z * yawCos));
    float3 sky = uEnvironment.Sample(_uEnvironment_sampler, rotated).xyz;
    FragColor = float4(sky * uTintIntensity.xyz, 1.0f) * SpriteColor;
    FragColor.w = 1.0f + (texture1.Sample(_texture1_sampler, TexCoord).w * 0.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    TexCoord = stage_input.TexCoord;
    SpriteColor = stage_input.SpriteColor;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
)CNA_HLSL";
inline constexpr std::string_view kStage4 = R"CNA_HLSL(cbuffer ShadowMatrices : register(b0)
{
    row_major float4x4 matrices_uLightViewProjection : packoffset(c0);
    row_major float4x4 matrices_uWorld : packoffset(c4);
};


static float4 gl_Position;
static float3 aPosition;
static float vDistance;

struct SPIRV_Cross_Input
{
    float3 aPosition : POSITION;
};

struct SPIRV_Cross_Output
{
    float vDistance : TEXCOORD0;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    float4 lightSpace = mul(float4(aPosition, 1.0f), mul(matrices_uWorld, matrices_uLightViewProjection));
    gl_Position = float4(lightSpace.x, -lightSpace.y, (lightSpace.z + lightSpace.w) * 0.5f, lightSpace.w);
    vDistance = ((lightSpace.z / lightSpace.w) * 0.5f) + 0.5f;
    gl_Position.y = -gl_Position.y;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    aPosition = stage_input.aPosition;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.vDistance = vDistance;
    return stage_output;
}
)CNA_HLSL";
inline constexpr std::string_view kStage5 = R"CNA_HLSL(static float4 fragColor;
static float vDistance;

struct SPIRV_Cross_Input
{
    float vDistance : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float4 fragColor : SV_Target0;
};

void frag_main()
{
    fragColor = float4(vDistance, vDistance, vDistance, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    vDistance = stage_input.vDistance;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.fragColor = fragColor;
    return stage_output;
}
)CNA_HLSL";
inline constexpr std::string_view kStage6 = R"CNA_HLSL(cbuffer ShadowMatrices : register(b0)
{
    row_major float4x4 matrices_uLightViewProjection : packoffset(c0);
    row_major float4x4 matrices_uWorld : packoffset(c4);
};


static float4 gl_Position;
static float3 aPosition;
static float3 vWorldPos;

struct SPIRV_Cross_Input
{
    float3 aPosition : POSITION;
};

struct SPIRV_Cross_Output
{
    float3 vWorldPos : TEXCOORD0;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    float4 world = mul(float4(aPosition, 1.0f), matrices_uWorld);
    vWorldPos = world.xyz;
    gl_Position = mul(world, matrices_uLightViewProjection);
    gl_Position.y = -gl_Position.y;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    aPosition = stage_input.aPosition;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.vWorldPos = vWorldPos;
    return stage_output;
}
)CNA_HLSL";
inline constexpr std::string_view kStage7 = R"CNA_HLSL(cbuffer PushConstants : register(b4)
{
    float2 vpSize : packoffset(c0);
    row_major float4x4 uMatrix : packoffset(c1);
    float4 uLightPosition : packoffset(c5);
    float uLightRange : packoffset(c6);
};


static float3 vWorldPos;
static float4 fragColor;

struct SPIRV_Cross_Input
{
    float3 vWorldPos : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float4 fragColor : SV_Target0;
};

void frag_main()
{
    float _distance = clamp(length(vWorldPos - uLightPosition.xyz) / uLightRange, 0.0f, 1.0f);
    fragColor = float4(_distance, _distance, _distance, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    vWorldPos = stage_input.vWorldPos;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.fragColor = fragColor;
    return stage_output;
}
)CNA_HLSL";
inline constexpr std::string_view kStage8 = R"CNA_HLSL(cbuffer Mat4Array : register(b0)
{
    row_major float4x4 uBones[72] : packoffset(c0);
};

cbuffer ShadowMatrices : register(b1)
{
    row_major float4x4 matrices_uLightViewProjection : packoffset(c0);
    row_major float4x4 matrices_uWorld : packoffset(c4);
};

cbuffer PushConstants : register(b2)
{
    float2 vpSize : packoffset(c0);
    row_major float4x4 uMatrix : packoffset(c1);
    float4 uVector : packoffset(c5);
    float uWeightsPerVertex : packoffset(c6);
};


static float4 gl_Position;
static uint4 aBoneIndices;
static float4 aBoneWeights;
static float3 aPosition;
static float vDistance;
static float3 aNormal;
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
    float vDistance : TEXCOORD0;
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
    float4 lightSpace = mul(float4(aPosition, 1.0f), mul(skin, mul(matrices_uWorld, matrices_uLightViewProjection)));
    gl_Position = float4(lightSpace.x, -lightSpace.y, (lightSpace.z + lightSpace.w) * 0.5f, lightSpace.w);
    vDistance = ((lightSpace.z / lightSpace.w) * 0.5f) + 0.5f;
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
    stage_output.vDistance = vDistance;
    return stage_output;
}
)CNA_HLSL";
inline constexpr std::string_view kStage9 = R"CNA_HLSL(cbuffer PushConstants : register(b0)
{
    float2 viewportSize : packoffset(c0);
};


static float4 gl_Position;
static float2 aPos;
static float2 TexCoord;
static float2 aTexCoord;
static float4 SpriteColor;
static float4 aColor;

struct SPIRV_Cross_Input
{
    float2 aPos : POSITION;
    float2 aTexCoord : TEXCOORD;
    float4 aColor : COLOR;
};

struct SPIRV_Cross_Output
{
    float2 TexCoord : TEXCOORD0;
    float4 SpriteColor : TEXCOORD1;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    float2 ndc = ((aPos / viewportSize) * 2.0f) - 1.0f.xx;
    gl_Position = float4(ndc, 0.0f, 1.0f);
    TexCoord = aTexCoord;
    SpriteColor = aColor;
    gl_Position.y = -gl_Position.y;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    aPos = stage_input.aPos;
    aTexCoord = stage_input.aTexCoord;
    aColor = stage_input.aColor;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.TexCoord = TexCoord;
    stage_output.SpriteColor = SpriteColor;
    return stage_output;
}
)CNA_HLSL";
inline constexpr std::string_view kStage10 = R"CNA_HLSL(cbuffer FloatArray : register(b4)
{
    float uVolumetricBuildScalars[72] : packoffset(c0);
};

cbuffer Mat4Array : register(b5)
{
    row_major float4x4 uVolumetricBuildMatrices[72] : packoffset(c0);
};

cbuffer Vec3Array : register(b6)
{
    float3 uVolumetricBuildVectors[72] : packoffset(c0);
};

Texture2D<float4> uShadowSampler : register(t1);
SamplerState _uShadowSampler_sampler : register(s1);
Texture2D<float4> texture1 : register(t0);
SamplerState _texture1_sampler : register(s0);

static float2 TexCoord;
static float4 FragColor;
static float4 SpriteColor;

struct SPIRV_Cross_Input
{
    float2 TexCoord : TEXCOORD0;
    float4 SpriteColor : TEXCOORD1;
};

struct SPIRV_Cross_Output
{
    float4 FragColor : SV_Target0;
};

void cnaAtlasSplit(float2 atlasUv, inout float slice, out float2 inside)
{
    float scaled = atlasUv.x * uVolumetricBuildScalars[0];
    slice = min(floor(scaled), uVolumetricBuildScalars[0] - 1.0f);
    inside = float2(scaled - slice, atlasUv.y);
}

float cnaSliceDepth(float slice)
{
    float t = (slice + 0.5f) / uVolumetricBuildScalars[0];
    return (uVolumetricBuildScalars[4] * t) * t;
}

float cnaPhase(float cosAngle)
{
    float g = uVolumetricBuildScalars[3];
    float gg = g * g;
    float d = (1.0f + gg) - ((2.0f * g) * cosAngle);
    return (1.0f - gg) / (12.56637096405029296875f * max(pow(max(d, 9.9999997473787516355514526367188e-05f), 1.5f), 9.9999997473787516355514526367188e-05f));
}

float cnaLitFraction(float3 world)
{
    if (uVolumetricBuildScalars[5] < 0.5f)
    {
        return 1.0f;
    }
    float4 lightClip = mul(float4(world, 1.0f), uVolumetricBuildMatrices[2]);
    if (lightClip.w <= 0.0f)
    {
        return 1.0f;
    }
    float3 lightNdc = lightClip.xyz / lightClip.w.xxx;
    float2 lightUv = (lightNdc.xy * 0.5f) + 0.5f.xx;
    bool _155 = lightUv.x < 0.0f;
    bool _162;
    if (!_155)
    {
        _162 = lightUv.x > 1.0f;
    }
    else
    {
        _162 = _155;
    }
    bool _169;
    if (!_162)
    {
        _169 = lightUv.y < 0.0f;
    }
    else
    {
        _169 = _162;
    }
    bool _176;
    if (!_169)
    {
        _176 = lightUv.y > 1.0f;
    }
    else
    {
        _176 = _169;
    }
    if (_176)
    {
        return 1.0f;
    }
    float stored = uShadowSampler.Sample(_uShadowSampler_sampler, lightUv).x;
    float here = (lightNdc.z * 0.5f) + 0.5f;
    return ((here - 0.00200000009499490261077880859375f) > stored) ? 0.0f : 1.0f;
}

void frag_main()
{
    float2 param = TexCoord;
    float param_1;
    float2 param_2;
    cnaAtlasSplit(param, param_1, param_2);
    float slice = param_1;
    float2 inside = param_2;
    float param_3 = slice;
    float depth = cnaSliceDepth(param_3);
    float2 cameraUv = float2(inside.x, 1.0f - inside.y);
    float4 ray = mul(float4((cameraUv * 2.0f) - 1.0f.xx, 1.0f, 1.0f), uVolumetricBuildMatrices[0]);
    float3 direction = ray.xyz / ray.w.xxx;
    float3 viewPosition = direction * (depth / max(-direction.z, 9.9999999747524270787835121154785e-07f));
    float4 world = mul(float4(viewPosition, 1.0f), uVolumetricBuildMatrices[1]);
    float4 cameraWorld = mul(float4(0.0f, 0.0f, 0.0f, 1.0f), uVolumetricBuildMatrices[1]);
    float3 toCamera = normalize(cameraWorld.xyz - world.xyz);
    float param_4 = dot(normalize(uVolumetricBuildVectors[0]), toCamera);
    float phase = cnaPhase(param_4);
    float3 param_5 = world.xyz;
    float lit = cnaLitFraction(param_5);
    float3 scattered = ((uVolumetricBuildVectors[1] * lit) * phase) * uVolumetricBuildScalars[2];
    FragColor = float4(scattered, uVolumetricBuildScalars[2]) * SpriteColor;
    FragColor.w += (texture1.Sample(_texture1_sampler, TexCoord).w * 0.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    TexCoord = stage_input.TexCoord;
    SpriteColor = stage_input.SpriteColor;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
)CNA_HLSL";
inline constexpr std::string_view kStage11 = R"CNA_HLSL(cbuffer FloatArray : register(b4)
{
    float uVolumetricResolveScalars[72] : packoffset(c0);
};

Texture2D<float4> texture1 : register(t0);
SamplerState _texture1_sampler : register(s0);
Texture2D<float4> uDepthSampler : register(t1);
SamplerState _uDepthSampler_sampler : register(s1);
Texture2D<float4> uVolumeSampler : register(t2);
SamplerState _uVolumeSampler_sampler : register(s2);

static float2 TexCoord;
static float4 FragColor;
static float4 SpriteColor;

struct SPIRV_Cross_Input
{
    float2 TexCoord : TEXCOORD0;
    float4 SpriteColor : TEXCOORD1;
};

struct SPIRV_Cross_Output
{
    float4 FragColor : SV_Target0;
};

float cnaDecodeLinearDepth(float4 channels)
{
    if (uVolumetricResolveScalars[4] < 0.5f)
    {
        return channels.x;
    }
    return dot(channels, float4(6.0308629201699659461155533790588e-08f, 1.5378700481960549950599670410156e-05f, 0.0039215688593685626983642578125f, 1.0f));
}

float cnaSliceDepth(float slice)
{
    float t = (slice + 0.5f) / uVolumetricResolveScalars[0];
    return (uVolumetricResolveScalars[3] * t) * t;
}

float2 cnaAtlasJoin(float slice, float2 inside)
{
    float sliceWidth = 1.0f / uVolumetricResolveScalars[0];
    float texelWidth = sliceWidth / uVolumetricResolveScalars[1];
    float u = ((slice * sliceWidth) + (0.5f * texelWidth)) + ((inside.x * texelWidth) * (uVolumetricResolveScalars[1] - 1.0f));
    return float2(u, inside.y);
}

void frag_main()
{
    float4 source = texture1.Sample(_texture1_sampler, TexCoord);
    float4 param = uDepthSampler.Sample(_uDepthSampler_sampler, TexCoord);
    float depth = cnaDecodeLinearDepth(param);
    float _129;
    if ((depth <= 0.0f) || (depth >= 0.999000012874603271484375f))
    {
        _129 = uVolumetricResolveScalars[3];
    }
    else
    {
        _129 = min(depth * uVolumetricResolveScalars[2], uVolumetricResolveScalars[3]);
    }
    float travelled = _129;
    float3 scattered = 0.0f.xxx;
    float transmittance = 1.0f;
    float previousDepth = 0.0f;
    for (int i = 0; i < 64; i++)
    {
        if (float(i) >= uVolumetricResolveScalars[0])
        {
            break;
        }
        float param_1 = float(i);
        float sliceDepth = cnaSliceDepth(param_1);
        if (sliceDepth > travelled)
        {
            break;
        }
        float thickness = sliceDepth - previousDepth;
        previousDepth = sliceDepth;
        float param_2 = float(i);
        float2 param_3 = TexCoord;
        float4 froxel = uVolumeSampler.Sample(_uVolumeSampler_sampler, cnaAtlasJoin(param_2, param_3));
        float extinction = froxel.w * thickness;
        scattered += ((froxel.xyz * thickness) * transmittance);
        transmittance *= exp(-extinction);
    }
    FragColor = float4((source.xyz * transmittance) + scattered, source.w) * SpriteColor;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    TexCoord = stage_input.TexCoord;
    SpriteColor = stage_input.SpriteColor;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
)CNA_HLSL";
inline constexpr std::array<Stage, 12> kStages{{
    Stage{"atmospheric_sky/sky.vulkan.vert.spv", kStage0, "c347a0738d726e58833e35577e9a5bf70e9a213fc0129af71e57783672f912bd"},
    Stage{"atmospheric_sky/sky.vulkan.frag.spv", kStage1, "f2f31bf33d1b932787c01c8711d117a41a8b50fbb24f4de04f29e0f9b4f82c3c"},
    Stage{"skybox/skybox.vulkan.vert.spv", kStage2, "d8d812609ab7b2b33e27b5035269850b1cdd5e60401332182f36566edf059ea7"},
    Stage{"skybox/skybox.vulkan.frag.spv", kStage3, "5bcbe4a6a9c038029cab676901ed0d36585dcdb6bb38cedbd00ddfba1e30fc79"},
    Stage{"shadow_caster/directional.vulkan.vert.spv", kStage4, "f0019a636135abed778de772e6bad3ff7afd4621f49df7790c858b2ef1f28acc"},
    Stage{"shadow_caster/directional.vulkan.frag.spv", kStage5, "5e262ad1c8a11624d48584be234d910f905cb5066f75bc8d367e6775d79a04a3"},
    Stage{"shadow_caster/punctual.vulkan.vert.spv", kStage6, "e10d4844db68b7d0a80b4800b82f6cd72884e496a6089db1177acd207aaa2310"},
    Stage{"shadow_caster/punctual.vulkan.frag.spv", kStage7, "a67396a2cb899371afcc3cd110114bd92eedd1dde5cdf1adf08e11649abe864e"},
    Stage{"shadow_caster/skinned.vulkan.vert.spv", kStage8, "323eca70cdfbce76839083df2774b34b52b1e609aac00fe32735dad030bb6295"},
    Stage{"volumetric_fog/fullscreen.vulkan.vert.spv", kStage9, "c347a0738d726e58833e35577e9a5bf70e9a213fc0129af71e57783672f912bd"},
    Stage{"volumetric_fog/build.vulkan.frag.spv", kStage10, "02fee4fcf958e2df95e9926081fd6b88707d90aa86f67bbfaddddbc61bec079a"},
    Stage{"volumetric_fog/resolve.vulkan.frag.spv", kStage11, "98b5783e225fc2d1738912b73dcea81cea4d42f56a2349fc7792bef6ad8f6350"},
}};
inline std::string_view FindStage(std::string_view label) {
    for (const auto& stage : kStages)
        if (stage.label == label) return stage.source;
    return {};
}
}
