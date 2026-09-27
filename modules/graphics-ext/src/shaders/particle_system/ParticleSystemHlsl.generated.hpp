// SPDX-License-Identifier: MS-PL
// Rebuild with tools/shader_package/generate_particle_hlsl.py.
#pragma once
#include <string_view>
namespace CNA::Graphics::detail::ParticleSystemHlslGenerated {
inline constexpr std::string_view kSimulateComputeSource = R"CNA_HLSL(static const uint3 gl_WorkGroupSize = uint3(64u, 1u, 1u);

cbuffer ParticleSimulationParameters : register(b1)
{
    float4 _123_uSimulation0 : packoffset(c0);
    float4 _123_uSimulation1 : packoffset(c1);
    float4 _123_uOrigin : packoffset(c2);
    float4 _123_uDirection : packoffset(c3);
    float4 _123_uGravity : packoffset(c4);
};

RWByteAddressBuffer _253 : register(u0);

static uint3 gl_GlobalInvocationID;
struct SPIRV_Cross_Input
{
    uint3 gl_GlobalInvocationID : SV_DispatchThreadID;
};

uint cnaParticleHash(inout uint x)
{
    x ^= (x >> 16u);
    x *= 2146121005u;
    x ^= (x >> 15u);
    x *= 2221713035u;
    x ^= (x >> 16u);
    return x;
}

float cnaParticleRandom(uint seed)
{
    uint param = seed;
    uint _59 = cnaParticleHash(param);
    return float(_59 & 16777215u) / 16777216.0f;
}

float3 cnaNormaliseOr(float3 value, float3 fallbackValue)
{
    float valueLength = length(value);
    float3 _74;
    if (valueLength > 9.9999999747524270787835121154785e-07f)
    {
        _74 = value / valueLength.xxx;
    }
    else
    {
        _74 = fallbackValue;
    }
    return _74;
}

void cnaSpawn(uint index, uint generation, out float3 position, out float3 velocity, out float lifetime)
{
    uint param = (index * 747796405u) + (generation * 2891336453u);
    uint _95 = cnaParticleHash(param);
    uint seed = _95;
    uint param_1 = seed;
    float u = cnaParticleRandom(param_1);
    uint param_2 = seed + 1u;
    float v = cnaParticleRandom(param_2);
    uint param_3 = seed + 2u;
    float w = cnaParticleRandom(param_3);
    uint param_4 = seed + 3u;
    float x = cnaParticleRandom(param_4);
    float cosTheta = 1.0f + ((cos(_123_uSimulation0.z) - 1.0f) * u);
    float sinTheta = sqrt(max(1.0f - (cosTheta * cosTheta), 0.0f));
    float phi = 6.283185482025146484375f * v;
    float3 param_5 = _123_uDirection.xyz;
    float3 param_6 = float3(0.0f, 1.0f, 0.0f);
    float3 axis = cnaNormaliseOr(param_5, param_6);
    bool3 _164 = (abs(axis.y) < 0.9900000095367431640625f).xxx;
    float3 helper = float3(_164.x ? float3(0.0f, 1.0f, 0.0f).x : float3(1.0f, 0.0f, 0.0f).x, _164.y ? float3(0.0f, 1.0f, 0.0f).y : float3(1.0f, 0.0f, 0.0f).y, _164.z ? float3(0.0f, 1.0f, 0.0f).z : float3(1.0f, 0.0f, 0.0f).z);
    float3 param_7 = cross(helper, axis);
    float3 param_8 = float3(1.0f, 0.0f, 0.0f);
    float3 right = cnaNormaliseOr(param_7, param_8);
    float3 up = cross(axis, right);
    float3 direction = (axis * cosTheta) + (((right * cos(phi)) + (up * sin(phi))) * sinTheta);
    float speed = _123_uSimulation0.w * (1.0f + (_123_uSimulation1.x * ((w * 2.0f) - 1.0f)));
    position = _123_uOrigin.xyz;
    velocity = direction * speed;
    lifetime = max(_123_uSimulation1.y * (1.0f + (_123_uSimulation1.z * ((x * 2.0f) - 1.0f))), 0.001000000047497451305389404296875f);
}

void comp_main()
{
    uint index = gl_GlobalInvocationID.x;
    uint activeCount = uint(max(_123_uSimulation0.x, 0.0f) + 0.5f);
    if (index >= activeCount)
    {
        return;
    }
    uint base = index * 3u;
    float3 position = asfloat(_253.Load4(base * 16 + 0)).xyz;
    float3 velocity = asfloat(_253.Load4((base + 1u) * 16 + 0)).xyz;
    float4 state = asfloat(_253.Load4((base + 2u) * 16 + 0));
    float age = state.x;
    float lifetime = state.y;
    float generation = state.w;
    age += _123_uSimulation0.y;
    if (age >= lifetime)
    {
        generation += 1.0f;
        age -= lifetime;
        uint param = index;
        uint param_1 = uint(generation);
        float3 param_2;
        float3 param_3;
        float param_4;
        cnaSpawn(param, param_1, param_2, param_3, param_4);
        position = param_2;
        velocity = param_3;
        lifetime = param_4;
    }
    velocity += (_123_uGravity.xyz * _123_uSimulation0.y);
    velocity -= (velocity * min(_123_uSimulation1.w * _123_uSimulation0.y, 1.0f));
    position += (velocity * _123_uSimulation0.y);
    _253.Store4(base * 16 + 0, asuint(float4(position, 0.0f)));
    _253.Store4((base + 1u) * 16 + 0, asuint(float4(velocity, 0.0f)));
    _253.Store4((base + 2u) * 16 + 0, asuint(float4(age, lifetime, state.z, generation)));
}

[numthreads(64, 1, 1)]
void main(SPIRV_Cross_Input stage_input)
{
    gl_GlobalInvocationID = stage_input.gl_GlobalInvocationID;
    comp_main();
}
)CNA_HLSL";
inline constexpr std::string_view kSimulateComputeSourceSha256 = "48f6ef4a44dc289714ba9c868a0662f21bb1c7e733c3dc53bc596547da9fe632";
inline constexpr std::string_view kDrawVertexSource = R"CNA_HLSL(ByteAddressBuffer _23 : register(t7);
cbuffer FloatArray : register(b0)
{
    float uParticleScalars[72] : packoffset(c0);
};

cbuffer Mat4Array : register(b1)
{
    row_major float4x4 uParticleMatrices[72] : packoffset(c0);
};

cbuffer Vec3Array : register(b2)
{
    float3 uParticleColours[72] : packoffset(c0);
};


static float4 gl_Position;
static int gl_InstanceIndex;
static float3 aPos;
static float2 vTexCoord;
static float4 vColor;
static float vViewDepth;

struct SPIRV_Cross_Input
{
    float3 aPos : POSITION;
    uint gl_InstanceIndex : SV_InstanceID;
};

struct SPIRV_Cross_Output
{
    float2 vTexCoord : TEXCOORD0;
    float4 vColor : TEXCOORD1;
    float vViewDepth : TEXCOORD2;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    int base = gl_InstanceIndex * 3;
    float3 position = asfloat(_23.Load4(base * 16 + 0)).xyz;
    float4 state = asfloat(_23.Load4((base + 2) * 16 + 0));
    float t = clamp(state.x / max(state.y, 9.9999997473787516355514526367188e-05f), 0.0f, 1.0f);
    float size = lerp(uParticleScalars[2], uParticleScalars[3], t);
    if (float(gl_InstanceIndex) >= uParticleScalars[4])
    {
        size = 0.0f;
    }
    float3 viewPosition = mul(float4(position, 1.0f), uParticleMatrices[0]).xyz;
    float3 _97 = viewPosition;
    float2 _99 = _97.xy + (aPos.xy * size);
    viewPosition.x = _99.x;
    viewPosition.y = _99.y;
    gl_Position = mul(float4(viewPosition, 1.0f), uParticleMatrices[1]);
    vTexCoord = aPos.xy + 0.5f.xx;
    vColor = float4(lerp(uParticleColours[0], uParticleColours[1], t.xxx), lerp(uParticleScalars[0], uParticleScalars[1], t));
    vViewDepth = -viewPosition.z;
    gl_Position.y = -gl_Position.y;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_InstanceIndex = int(stage_input.gl_InstanceIndex);
    aPos = stage_input.aPos;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.vTexCoord = vTexCoord;
    stage_output.vColor = vColor;
    stage_output.vViewDepth = vViewDepth;
    return stage_output;
}
)CNA_HLSL";
inline constexpr std::string_view kDrawVertexSourceSha256 = "c1348ca26f08c855c2f2df0c31a1f14afcd0392d6919ddfe3bd08d4e0d841060";
inline constexpr std::string_view kDrawFragmentSource = R"CNA_HLSL(cbuffer FloatArray : register(b4)
{
    float uParticleScalars[72] : packoffset(c0);
};

Texture2D<float4> texture1 : register(t0);
SamplerState _texture1_sampler : register(s0);
Texture2D<float4> uSceneDepth : register(t1);
SamplerState _uSceneDepth_sampler : register(s1);

static float4 gl_FragCoord;
static float2 vTexCoord;
static float4 vColor;
static float vViewDepth;
static float4 FragColor;

struct SPIRV_Cross_Input
{
    float2 vTexCoord : TEXCOORD0;
    float4 vColor : TEXCOORD1;
    float vViewDepth : TEXCOORD2;
    float4 gl_FragCoord : SV_Position;
};

struct SPIRV_Cross_Output
{
    float4 FragColor : SV_Target0;
};

float cnaUnpackDepth(float4 channels)
{
    return dot(channels, float4(6.0308629201699659461155533790588e-08f, 1.5378700481960549950599670410156e-05f, 0.0039215688593685626983642578125f, 1.0f));
}

void frag_main()
{
    float4 colour = texture1.Sample(_texture1_sampler, vTexCoord) * vColor;
    bool _52 = uParticleScalars[5] > 0.5f;
    bool _60;
    if (_52)
    {
        _60 = uParticleScalars[6] > 0.0f;
    }
    else
    {
        _60 = _52;
    }
    if (_60)
    {
        float2 viewportSize = max(float2(uParticleScalars[8], uParticleScalars[9]), 1.0f.xx);
        float2 uv = gl_FragCoord.xy / viewportSize;
        float4 depthTexel = uSceneDepth.Sample(_uSceneDepth_sampler, uv);
        float4 param = depthTexel;
        float linearDepth = lerp(depthTexel.x, cnaUnpackDepth(param), clamp(uParticleScalars[10], 0.0f, 1.0f));
        float behind = linearDepth * uParticleScalars[7];
        colour.w *= clamp((behind - vViewDepth) / uParticleScalars[6], 0.0f, 1.0f);
    }
    FragColor = colour;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_FragCoord = stage_input.gl_FragCoord;
    gl_FragCoord.w = 1.0 / gl_FragCoord.w;
    vTexCoord = stage_input.vTexCoord;
    vColor = stage_input.vColor;
    vViewDepth = stage_input.vViewDepth;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
)CNA_HLSL";
inline constexpr std::string_view kDrawFragmentSourceSha256 = "042ce3489671bc162320beaaaa78927872f2fbf0f48794b3b0a86885cf9fb667";
}
