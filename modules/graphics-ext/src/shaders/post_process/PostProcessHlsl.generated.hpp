// SPDX-License-Identifier: MS-PL
// Rebuild with tools/shader_package/generate_post_process_hlsl.py.
#pragma once
#include <array>
#include <string_view>
namespace CNA::Graphics::detail::PostProcessHlslGenerated {
struct Fragment { std::string_view label; std::string_view source; std::string_view sourceSha256; };
inline constexpr std::string_view kFullscreenVertexHlsl = R"CNA_HLSL(cbuffer PushConstants : register(b0)
{
    float2 viewportSize : packoffset(c0);
    row_major float4x4 uMatrix : packoffset(c1);
    float4 uVector : packoffset(c5);
    float uScalar : packoffset(c6);
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
inline constexpr std::string_view kCrtFragmentHlsl = R"CNA_HLSL(cbuffer PushConstants : register(b1)
{
    float2 viewportSize : packoffset(c0);
    row_major float4x4 uMatrix : packoffset(c1);
    float4 uCrtParams : packoffset(c5);
    float uMaskType : packoffset(c6);
};

Texture2D<float4> texture1 : register(t0);
SamplerState _texture1_sampler : register(s0);

static float4 gl_FragCoord;
static float2 TexCoord;
static float4 FragColor;
static float4 SpriteColor;

struct SPIRV_Cross_Input
{
    float2 TexCoord : TEXCOORD0;
    float4 SpriteColor : TEXCOORD1;
    float4 gl_FragCoord : SV_Position;
};

struct SPIRV_Cross_Output
{
    float4 FragColor : SV_Target0;
};

float mod(float x, float y)
{
    return x - y * floor(x / y);
}

float2 mod(float2 x, float2 y)
{
    return x - y * floor(x / y);
}

float3 mod(float3 x, float3 y)
{
    return x - y * floor(x / y);
}

float4 mod(float4 x, float4 y)
{
    return x - y * floor(x / y);
}

float2 applyCurvature(float2 uv)
{
    float2 cc = uv - 0.5f.xx;
    float dist = dot(cc, cc) * uCrtParams.y;
    return uv + (cc * dist);
}

void frag_main()
{
    float2 param = TexCoord;
    float2 uv = applyCurvature(param);
    bool _55 = uv.x < 0.0f;
    bool _63;
    if (!_55)
    {
        _63 = uv.x > 1.0f;
    }
    else
    {
        _63 = _55;
    }
    bool _70;
    if (!_63)
    {
        _70 = uv.y < 0.0f;
    }
    else
    {
        _70 = _63;
    }
    bool _77;
    if (!_70)
    {
        _77 = uv.y > 1.0f;
    }
    else
    {
        _77 = _70;
    }
    if (_77)
    {
        FragColor = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    float4 texColor = texture1.Sample(_texture1_sampler, uv) * SpriteColor;
    float3 rgb = texColor.xyz;
    float2 easyGlFragCoord = float2(gl_FragCoord.x, viewportSize.y - gl_FragCoord.y);
    float rowParity = mod(floor(easyGlFragCoord.y), 2.0f);
    rgb *= lerp(1.0f, 1.0f - uCrtParams.x, rowParity);
    int maskType = int(uMaskType);
    if (maskType != 0)
    {
        float colBase = floor(easyGlFragCoord.x);
        if (maskType == 2)
        {
            float rowGroup = mod(floor(easyGlFragCoord.y / 2.0f), 2.0f);
            colBase += (rowGroup * 1.5f);
        }
        float col = mod(colBase, 3.0f);
        float3 mask = (1.0f - uCrtParams.w).xxx;
        if (col < 1.0f)
        {
            mask.x = 1.0f;
        }
        else
        {
            if (col < 2.0f)
            {
                mask.y = 1.0f;
            }
            else
            {
                mask.z = 1.0f;
            }
        }
        rgb *= mask;
    }
    float2 vc = TexCoord - 0.5f.xx;
    float vignette = 1.0f - ((uCrtParams.z * dot(vc, vc)) * 2.0f);
    rgb *= clamp(vignette, 0.0f, 1.0f);
    FragColor = float4(rgb, texColor.w);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_FragCoord = stage_input.gl_FragCoord;
    gl_FragCoord.w = 1.0 / gl_FragCoord.w;
    TexCoord = stage_input.TexCoord;
    SpriteColor = stage_input.SpriteColor;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
)CNA_HLSL";
inline constexpr std::string_view kDepthEffectFragmentHlsl = R"CNA_HLSL(static const float _94[16] = { 0.0f, 8.0f, 2.0f, 10.0f, 12.0f, 4.0f, 14.0f, 6.0f, 3.0f, 11.0f, 1.0f, 9.0f, 15.0f, 7.0f, 13.0f, 5.0f };
static const float _173[64] = { 0.0f, 32.0f, 8.0f, 40.0f, 2.0f, 34.0f, 10.0f, 42.0f, 48.0f, 16.0f, 56.0f, 24.0f, 50.0f, 18.0f, 58.0f, 26.0f, 12.0f, 44.0f, 4.0f, 36.0f, 14.0f, 46.0f, 6.0f, 38.0f, 60.0f, 28.0f, 52.0f, 20.0f, 62.0f, 30.0f, 54.0f, 22.0f, 3.0f, 35.0f, 11.0f, 43.0f, 1.0f, 33.0f, 9.0f, 41.0f, 51.0f, 19.0f, 59.0f, 27.0f, 49.0f, 17.0f, 57.0f, 25.0f, 15.0f, 47.0f, 7.0f, 39.0f, 13.0f, 45.0f, 5.0f, 37.0f, 63.0f, 31.0f, 55.0f, 23.0f, 61.0f, 29.0f, 53.0f, 21.0f };

cbuffer PushConstants : register(b1)
{
    float2 viewportSize : packoffset(c0);
    row_major float4x4 uMatrix : packoffset(c1);
    float4 uDepthParams : packoffset(c5);
    float unusedScalar : packoffset(c6);
};

Texture2D<float4> uPalette : register(t1);
SamplerState _uPalette_sampler : register(s1);
Texture2D<float4> texture1 : register(t0);
SamplerState _texture1_sampler : register(s0);

static float4 gl_FragCoord;
static float2 TexCoord;
static float4 SpriteColor;
static float4 FragColor;

struct SPIRV_Cross_Input
{
    float2 TexCoord : TEXCOORD0;
    float4 SpriteColor : TEXCOORD1;
    float4 gl_FragCoord : SV_Position;
};

struct SPIRV_Cross_Output
{
    float4 FragColor : SV_Target0;
};

float mod(float x, float y)
{
    return x - y * floor(x / y);
}

float2 mod(float2 x, float2 y)
{
    return x - y * floor(x / y);
}

float3 mod(float3 x, float3 y)
{
    return x - y * floor(x / y);
}

float4 mod(float4 x, float4 y)
{
    return x - y * floor(x / y);
}

float2 easyGlFragCoord()
{
    return float2(gl_FragCoord.x, viewportSize.y - gl_FragCoord.y);
}

float ditherThreshold()
{
    float2 fragment = easyGlFragCoord();
    int ditherMode = int(uDepthParams.y);
    if (ditherMode == 1)
    {
        int x = int(mod(fragment.x, 4.0f));
        int y = int(mod(fragment.y, 4.0f));
        return ((_94[(y * 4) + x] + 0.5f) / 16.0f) - 0.5f;
    }
    if (ditherMode == 2)
    {
        int x_1 = int(mod(fragment.x, 8.0f));
        int y_1 = int(mod(fragment.y, 8.0f));
        return ((_173[(y_1 * 8) + x_1] + 0.5f) / 64.0f) - 0.5f;
    }
    return 0.0f;
}

float quantizeChannel(float value, float levels)
{
    float dithered = value + (ditherThreshold() / (levels - 1.0f));
    return floor((clamp(dithered, 0.0f, 1.0f) * (levels - 1.0f)) + 0.5f) / (levels - 1.0f);
}

float3 nearestPaletteColor(float3 color)
{
    float3 dithered = clamp(color + (ditherThreshold() * 0.0625f).xxx, 0.0f.xxx, 1.0f.xxx);
    float3 best = dithered;
    float bestDist = 1000000000.0f;
    int paletteSize = int(uDepthParams.z);
    for (int i = 0; i < 256; i++)
    {
        if (i >= paletteSize)
        {
            break;
        }
        float3 candidate = uPalette.Load(int3(int2(i, 0), 0)).xyz;
        float3 difference = dithered - candidate;
        float distanceSquared = dot(difference, difference);
        if (distanceSquared < bestDist)
        {
            bestDist = distanceSquared;
            best = candidate;
        }
    }
    return best;
}

void frag_main()
{
    float4 texColor = texture1.Sample(_texture1_sampler, TexCoord) * SpriteColor;
    float3 rgb = texColor.xyz;
    int mode = int(uDepthParams.x);
    if (mode == 0)
    {
        float param = rgb.x;
        float param_1 = 32.0f;
        rgb.x = quantizeChannel(param, param_1);
        float param_2 = rgb.y;
        float param_3 = 64.0f;
        rgb.y = quantizeChannel(param_2, param_3);
        float param_4 = rgb.z;
        float param_5 = 32.0f;
        rgb.z = quantizeChannel(param_4, param_5);
    }
    else
    {
        if (mode == 1)
        {
            float param_6 = rgb.x;
            float param_7 = 8.0f;
            rgb.x = quantizeChannel(param_6, param_7);
            float param_8 = rgb.y;
            float param_9 = 8.0f;
            rgb.y = quantizeChannel(param_8, param_9);
            float param_10 = rgb.z;
            float param_11 = 4.0f;
            rgb.z = quantizeChannel(param_10, param_11);
        }
        else
        {
            if (((mode == 2) || (mode == 3)) || (mode == 4))
            {
                float _353;
                if (mode == 2)
                {
                    _353 = 16.0f;
                }
                else
                {
                    _353 = (mode == 3) ? 4.0f : 2.0f;
                }
                float levels = _353;
                float gray = dot(rgb, float3(0.2989999949932098388671875f, 0.58700001239776611328125f, 0.114000000059604644775390625f));
                float param_12 = gray;
                float param_13 = levels;
                rgb = quantizeChannel(param_12, param_13).xxx;
            }
            else
            {
                float3 param_14 = rgb;
                rgb = nearestPaletteColor(param_14);
            }
        }
    }
    FragColor = float4(rgb, texColor.w);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_FragCoord = stage_input.gl_FragCoord;
    gl_FragCoord.w = 1.0 / gl_FragCoord.w;
    TexCoord = stage_input.TexCoord;
    SpriteColor = stage_input.SpriteColor;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
)CNA_HLSL";
inline constexpr std::array<Fragment, 2> kFragments{{
    Fragment{"post_process/crt.vulkan.frag.spv", kCrtFragmentHlsl, "cc9d9b1e1110e0bd486722efa25cd514e04bf1667ad3950698a21edb94b8da4b"},
    Fragment{"post_process/depth_effect.vulkan.frag.spv", kDepthEffectFragmentHlsl, "64bec1bd3040f27905b3e2e1289e5e387dbd8f701eaf8fe951a7770f3b4b8778"},
}};
inline std::string_view FindFragment(std::string_view label) {
    for (const auto& fragment : kFragments)
        if (fragment.label == label) return fragment.source;
    return {};
}
}
