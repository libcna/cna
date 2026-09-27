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
inline constexpr std::string_view kAerialPerspectiveFragmentHlsl = R"CNA_HLSL(cbuffer FloatArray : register(b1)
{
    float uAerialScalars[72] : packoffset(c0);
};

cbuffer Mat4Array : register(b2)
{
    row_major float4x4 uAerialMatrices[72] : packoffset(c0);
};

cbuffer Vec3Array : register(b3)
{
    float3 uAerialVectors[72] : packoffset(c0);
};

Texture2D<float4> texture1 : register(t0);
SamplerState _texture1_sampler : register(s0);
Texture2D<float4> uDepthSampler : register(t1);
SamplerState _uDepthSampler_sampler : register(s1);

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
    if (uAerialScalars[4] < 0.5f)
    {
        return channels.x;
    }
    return dot(channels, float4(6.0308629201699659461155533790588e-08f, 1.5378700481960549950599670410156e-05f, 0.0039215688593685626983642578125f, 1.0f));
}

float cnaAirMass(float upwards)
{
    float up = clamp(upwards, 0.0f, 1.0f);
    float zenithDegrees = degrees(acos(up));
    return 1.0f / max(up + (0.50572001934051513671875f * pow(max(96.07994842529296875f - zenithDegrees, 0.001000000047497451305389404296875f), -1.6363999843597412109375f)), 9.9999997473787516355514526367188e-05f);
}

float cnaAerialAirMass(float3 viewDirection, float _distance, float scaleHeight)
{
    float param = normalize(viewDirection).y;
    float full = cnaAirMass(param);
    return min(max(_distance, 0.0f) / max(scaleHeight, 0.001000000047497451305389404296875f), full);
}

float3 cnaAtmosphereTransmittance(float turbidity, float viewMass)
{
    float mie = 0.02099999971687793731689453125f * max(turbidity - 1.0f, 0.0f);
    return exp((-(float3(0.0463999994099140167236328125f, 0.108499996364116668701171875f, 0.26499998569488525390625f) + mie.xxx)) * viewMass);
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

void frag_main()
{
    float4 source = texture1.Sample(_texture1_sampler, TexCoord);
    float4 param = uDepthSampler.Sample(_uDepthSampler_sampler, TexCoord);
    float depth = cnaDecodeLinearDepth(param);
    if ((depth <= 0.0f) || (depth >= 0.999000012874603271484375f))
    {
        FragColor = source * SpriteColor;
        return;
    }
    float2 cameraUv = float2(TexCoord.x, 1.0f - TexCoord.y);
    float2 ndc = (cameraUv * 2.0f) - 1.0f.xx;
    float4 world = mul(float4(ndc, 1.0f, 1.0f), uAerialMatrices[0]);
    float3 direction = normalize(world.xyz / world.w.xxx);
    float4 viewRay = mul(float4(ndc, 1.0f, 1.0f), uAerialMatrices[1]);
    float3 view = viewRay.xyz / viewRay.w.xxx;
    float alongRay = (depth * uAerialScalars[3]) * (length(view) / max(-view.z, 9.9999997473787516355514526367188e-05f));
    float3 param_1 = direction;
    float param_2 = alongRay;
    float param_3 = uAerialScalars[2];
    float airMass = cnaAerialAirMass(param_1, param_2, param_3);
    float param_4 = uAerialScalars[0];
    float param_5 = airMass;
    float3 param_6 = direction;
    float3 param_7 = uAerialVectors[0];
    float param_8 = uAerialScalars[0];
    float param_9 = airMass;
    float3 graded = (source.xyz * cnaAtmosphereTransmittance(param_4, param_5)) + (cnaScatteringAlongPath(param_6, param_7, param_8, param_9) * uAerialScalars[1]);
    FragColor = float4(graded, source.w) * SpriteColor;
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
inline constexpr std::string_view kBloomBlurFragmentHlsl = R"CNA_HLSL(cbuffer PushConstants : register(b1)
{
    float2 viewportSize : packoffset(c0);
    row_major float4x4 uMatrix : packoffset(c1);
    float4 uBloomParams : packoffset(c5);
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

void frag_main()
{
    float2 direction = uBloomParams.xy;
    float3 sum = texture1.Sample(_texture1_sampler, TexCoord).xyz * 0.2270270287990570068359375f;
    sum += (texture1.Sample(_texture1_sampler, TexCoord + (direction * 1.0f)).xyz * 0.1945945918560028076171875f);
    sum += (texture1.Sample(_texture1_sampler, TexCoord - (direction * 1.0f)).xyz * 0.1945945918560028076171875f);
    sum += (texture1.Sample(_texture1_sampler, TexCoord + (direction * 2.0f)).xyz * 0.12162162363529205322265625f);
    sum += (texture1.Sample(_texture1_sampler, TexCoord - (direction * 2.0f)).xyz * 0.12162162363529205322265625f);
    sum += (texture1.Sample(_texture1_sampler, TexCoord + (direction * 3.0f)).xyz * 0.0540540553629398345947265625f);
    sum += (texture1.Sample(_texture1_sampler, TexCoord - (direction * 3.0f)).xyz * 0.0540540553629398345947265625f);
    sum += (texture1.Sample(_texture1_sampler, TexCoord + (direction * 4.0f)).xyz * 0.01621621660888195037841796875f);
    sum += (texture1.Sample(_texture1_sampler, TexCoord - (direction * 4.0f)).xyz * 0.01621621660888195037841796875f);
    FragColor = float4(sum, 1.0f) * SpriteColor;
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
inline constexpr std::string_view kBloomCombineFragmentHlsl = R"CNA_HLSL(cbuffer PushConstants : register(b1)
{
    float2 viewportSize : packoffset(c0);
    row_major float4x4 uMatrix : packoffset(c1);
    float4 uBloomParams : packoffset(c5);
};

Texture2D<float4> texture1 : register(t0);
SamplerState _texture1_sampler : register(s0);
Texture2D<float4> uBloomSampler : register(t1);
SamplerState _uBloomSampler_sampler : register(s1);

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
    float4 scene = texture1.Sample(_texture1_sampler, TexCoord);
    float3 bloom = uBloomSampler.Sample(_uBloomSampler_sampler, TexCoord).xyz;
    FragColor = float4(scene.xyz + (bloom * uBloomParams.x), scene.w) * SpriteColor;
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
inline constexpr std::string_view kBloomExtractFragmentHlsl = R"CNA_HLSL(cbuffer PushConstants : register(b1)
{
    float2 viewportSize : packoffset(c0);
    row_major float4x4 uMatrix : packoffset(c1);
    float4 uBloomParams : packoffset(c5);
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

void frag_main()
{
    float3 color = texture1.Sample(_texture1_sampler, TexCoord).xyz;
    float threshold = uBloomParams.x;
    float luminance = dot(color, float3(0.2125999927520751953125f, 0.715200006961822509765625f, 0.072200000286102294921875f));
    float knee = max(threshold * 0.5f, 9.9999997473787516355514526367188e-05f);
    float contribution = clamp(((luminance - threshold) + knee) / (2.0f * knee), 0.0f, 1.0f);
    contribution *= contribution;
    FragColor = float4(color * contribution, 1.0f) * SpriteColor;
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
inline constexpr std::string_view kBloomUpsampleFragmentHlsl = R"CNA_HLSL(cbuffer PushConstants : register(b1)
{
    float2 viewportSize : packoffset(c0);
    row_major float4x4 uMatrix : packoffset(c1);
    float4 uBloomParams : packoffset(c5);
};

Texture2D<float4> texture1 : register(t0);
SamplerState _texture1_sampler : register(s0);
Texture2D<float4> uSmallerSampler : register(t1);
SamplerState _uSmallerSampler_sampler : register(s1);

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
    float2 smallerTexel = uBloomParams.xy;
    float3 larger = texture1.Sample(_texture1_sampler, TexCoord).xyz;
    float3 smaller;
    if (uBloomParams.z < 0.5f)
    {
        smaller = uSmallerSampler.Sample(_uSmallerSampler_sampler, TexCoord).xyz;
    }
    else
    {
        float2 halfTexel = smallerTexel * 0.5f;
        smaller = uSmallerSampler.Sample(_uSmallerSampler_sampler, TexCoord + float2(-halfTexel.x, -halfTexel.y)).xyz;
        smaller += uSmallerSampler.Sample(_uSmallerSampler_sampler, TexCoord + float2(halfTexel.x, -halfTexel.y)).xyz;
        smaller += uSmallerSampler.Sample(_uSmallerSampler_sampler, TexCoord + float2(-halfTexel.x, halfTexel.y)).xyz;
        smaller += uSmallerSampler.Sample(_uSmallerSampler_sampler, TexCoord + float2(halfTexel.x, halfTexel.y)).xyz;
        smaller *= 0.25f;
    }
    FragColor = float4(larger + smaller, 1.0f) * SpriteColor;
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
inline constexpr std::string_view kChromaticFragmentHlsl = R"CNA_HLSL(cbuffer PushConstants : register(b1)
{
    float2 viewportSize : packoffset(c0);
    row_major float4x4 uMatrix : packoffset(c1);
    float4 uVector : packoffset(c5);
    float uStrength : packoffset(c6);
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

void frag_main()
{
    float2 fromCentre = TexCoord - 0.5f.xx;
    float2 redUv = 0.5f.xx + (fromCentre * (1.0f + uStrength));
    float2 blueUv = 0.5f.xx + (fromCentre * (1.0f - uStrength));
    FragColor = float4(texture1.Sample(_texture1_sampler, redUv).x, texture1.Sample(_texture1_sampler, TexCoord).y, texture1.Sample(_texture1_sampler, blueUv).z, texture1.Sample(_texture1_sampler, TexCoord).w) * SpriteColor;
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
inline constexpr std::string_view kColorGradeInterpolatedStripFragmentHlsl = R"CNA_HLSL(cbuffer PushConstants : register(b1)
{
    float2 viewportSize : packoffset(c0);
    row_major float4x4 uMatrix : packoffset(c1);
    float4 uColorGradeParams : packoffset(c5);
};

Texture2D<float4> uLutSampler : register(t1);
SamplerState _uLutSampler_sampler : register(s1);
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

float3 cnaLutFetch(int3 index)
{
    int slices = int(uColorGradeParams.x);
    return uLutSampler.Load(int3(int2((index.z * slices) + index.x, index.y), 0)).xyz;
}

float3 cnaLutTetrahedral(float3 colour)
{
    float last = uColorGradeParams.x - 1.0f;
    float3 p = clamp(colour, 0.0f.xxx, 1.0f.xxx) * last;
    int3 i0 = int3(floor(p));
    int3 i1 = min((i0 + int3(1, 1, 1)), int(last).xxx);
    float3 f = p - float3(i0);
    int3 param = int3(i0.x, i0.y, i0.z);
    float3 c000 = cnaLutFetch(param);
    int3 param_1 = int3(i1.x, i1.y, i1.z);
    float3 c111 = cnaLutFetch(param_1);
    if (f.x > f.y)
    {
        if (f.y > f.z)
        {
            int3 param_2 = int3(i1.x, i0.y, i0.z);
            float3 c100 = cnaLutFetch(param_2);
            int3 param_3 = int3(i1.x, i1.y, i0.z);
            float3 c110 = cnaLutFetch(param_3);
            return ((c000 + ((c100 - c000) * f.x)) + ((c110 - c100) * f.y)) + ((c111 - c110) * f.z);
        }
        if (f.x > f.z)
        {
            int3 param_4 = int3(i1.x, i0.y, i0.z);
            float3 c100_1 = cnaLutFetch(param_4);
            int3 param_5 = int3(i1.x, i0.y, i1.z);
            float3 c101 = cnaLutFetch(param_5);
            return ((c000 + ((c100_1 - c000) * f.x)) + ((c101 - c100_1) * f.z)) + ((c111 - c101) * f.y);
        }
        int3 param_6 = int3(i0.x, i0.y, i1.z);
        float3 c001 = cnaLutFetch(param_6);
        int3 param_7 = int3(i1.x, i0.y, i1.z);
        float3 c101_1 = cnaLutFetch(param_7);
        return ((c000 + ((c001 - c000) * f.z)) + ((c101_1 - c001) * f.x)) + ((c111 - c101_1) * f.y);
    }
    if (f.z > f.y)
    {
        int3 param_8 = int3(i0.x, i0.y, i1.z);
        float3 c001_1 = cnaLutFetch(param_8);
        int3 param_9 = int3(i0.x, i1.y, i1.z);
        float3 c011 = cnaLutFetch(param_9);
        return ((c000 + ((c001_1 - c000) * f.z)) + ((c011 - c001_1) * f.y)) + ((c111 - c011) * f.x);
    }
    if (f.z > f.x)
    {
        int3 param_10 = int3(i0.x, i1.y, i0.z);
        float3 c010 = cnaLutFetch(param_10);
        int3 param_11 = int3(i0.x, i1.y, i1.z);
        float3 c011_1 = cnaLutFetch(param_11);
        return ((c000 + ((c010 - c000) * f.y)) + ((c011_1 - c010) * f.z)) + ((c111 - c011_1) * f.x);
    }
    int3 param_12 = int3(i0.x, i1.y, i0.z);
    float3 c010_1 = cnaLutFetch(param_12);
    int3 param_13 = int3(i1.x, i1.y, i0.z);
    float3 c110_1 = cnaLutFetch(param_13);
    return ((c000 + ((c010_1 - c000) * f.y)) + ((c110_1 - c010_1) * f.x)) + ((c111 - c110_1) * f.z);
}

float3 cnaLutTrilinear(float3 colour)
{
    float last = uColorGradeParams.x - 1.0f;
    float3 p = clamp(colour, 0.0f.xxx, 1.0f.xxx) * last;
    int3 i0 = int3(floor(p));
    int3 i1 = min((i0 + int3(1, 1, 1)), int(last).xxx);
    float3 f = p - float3(i0);
    int3 param = int3(i0.x, i0.y, i0.z);
    float3 c000 = cnaLutFetch(param);
    int3 param_1 = int3(i1.x, i0.y, i0.z);
    float3 c100 = cnaLutFetch(param_1);
    int3 param_2 = int3(i0.x, i1.y, i0.z);
    float3 c010 = cnaLutFetch(param_2);
    int3 param_3 = int3(i1.x, i1.y, i0.z);
    float3 c110 = cnaLutFetch(param_3);
    int3 param_4 = int3(i0.x, i0.y, i1.z);
    float3 c001 = cnaLutFetch(param_4);
    int3 param_5 = int3(i1.x, i0.y, i1.z);
    float3 c101 = cnaLutFetch(param_5);
    int3 param_6 = int3(i0.x, i1.y, i1.z);
    float3 c011 = cnaLutFetch(param_6);
    int3 param_7 = int3(i1.x, i1.y, i1.z);
    float3 c111 = cnaLutFetch(param_7);
    return lerp(lerp(lerp(c000, c100, f.x.xxx), lerp(c010, c110, f.x.xxx), f.y.xxx), lerp(lerp(c001, c101, f.x.xxx), lerp(c011, c111, f.x.xxx), f.y.xxx), f.z.xxx);
}

void frag_main()
{
    float4 source = texture1.Sample(_texture1_sampler, TexCoord);
    float3 colour = clamp(source.xyz, 0.0f.xxx, 1.0f.xxx);
    float3 _574;
    if (uColorGradeParams.z >= 0.5f)
    {
        float3 param = colour;
        _574 = cnaLutTetrahedral(param);
    }
    else
    {
        float3 param_1 = colour;
        _574 = cnaLutTrilinear(param_1);
    }
    float3 graded = _574;
    FragColor = float4(lerp(source.xyz, graded, uColorGradeParams.y.xxx), source.w) * SpriteColor;
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
inline constexpr std::string_view kColorGradeStripFragmentHlsl = R"CNA_HLSL(cbuffer PushConstants : register(b1)
{
    float2 viewportSize : packoffset(c0);
    row_major float4x4 uMatrix : packoffset(c1);
    float4 uColorGradeParams : packoffset(c5);
};

Texture2D<float4> uLutSampler : register(t1);
SamplerState _uLutSampler_sampler : register(s1);
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

float3 cnaSampleSlice(float3 colour, float slice)
{
    float lutSize = uColorGradeParams.x;
    float sliceWidth = 1.0f / lutSize;
    float texelWidth = sliceWidth / lutSize;
    float u = ((slice * sliceWidth) + (0.5f * texelWidth)) + ((colour.x * texelWidth) * (lutSize - 1.0f));
    float v = (0.5f / lutSize) + ((colour.y * (lutSize - 1.0f)) / lutSize);
    return uLutSampler.Sample(_uLutSampler_sampler, float2(u, v)).xyz;
}

void frag_main()
{
    float4 source = texture1.Sample(_texture1_sampler, TexCoord);
    float3 colour = clamp(source.xyz, 0.0f.xxx, 1.0f.xxx);
    float last = uColorGradeParams.x - 1.0f;
    float blue = colour.z * last;
    float lower = floor(blue);
    float upper = min(lower + 1.0f, last);
    float3 param = colour;
    float param_1 = lower;
    float3 param_2 = colour;
    float param_3 = upper;
    float3 graded = lerp(cnaSampleSlice(param, param_1), cnaSampleSlice(param_2, param_3), (blue - lower).xxx);
    FragColor = float4(lerp(source.xyz, graded, uColorGradeParams.y.xxx), source.w) * SpriteColor;
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
inline constexpr std::string_view kColorGradeVolumeFragmentHlsl = R"CNA_HLSL(cbuffer PushConstants : register(b1)
{
    float2 viewportSize : packoffset(c0);
    row_major float4x4 uMatrix : packoffset(c1);
    float4 uColorGradeParams : packoffset(c5);
};

Texture3D<float4> uLutVolume : register(t1);
SamplerState _uLutVolume_sampler : register(s1);
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

float3 cnaLutFetch(int3 index)
{
    return uLutVolume.Load(int4(index, 0)).xyz;
}

float3 cnaLutTetrahedral(float3 colour)
{
    float last = uColorGradeParams.x - 1.0f;
    float3 p = clamp(colour, 0.0f.xxx, 1.0f.xxx) * last;
    int3 i0 = int3(floor(p));
    int3 i1 = min((i0 + int3(1, 1, 1)), int(last).xxx);
    float3 f = p - float3(i0);
    int3 param = int3(i0.x, i0.y, i0.z);
    float3 c000 = cnaLutFetch(param);
    int3 param_1 = int3(i1.x, i1.y, i1.z);
    float3 c111 = cnaLutFetch(param_1);
    if (f.x > f.y)
    {
        if (f.y > f.z)
        {
            int3 param_2 = int3(i1.x, i0.y, i0.z);
            float3 c100 = cnaLutFetch(param_2);
            int3 param_3 = int3(i1.x, i1.y, i0.z);
            float3 c110 = cnaLutFetch(param_3);
            return ((c000 + ((c100 - c000) * f.x)) + ((c110 - c100) * f.y)) + ((c111 - c110) * f.z);
        }
        if (f.x > f.z)
        {
            int3 param_4 = int3(i1.x, i0.y, i0.z);
            float3 c100_1 = cnaLutFetch(param_4);
            int3 param_5 = int3(i1.x, i0.y, i1.z);
            float3 c101 = cnaLutFetch(param_5);
            return ((c000 + ((c100_1 - c000) * f.x)) + ((c101 - c100_1) * f.z)) + ((c111 - c101) * f.y);
        }
        int3 param_6 = int3(i0.x, i0.y, i1.z);
        float3 c001 = cnaLutFetch(param_6);
        int3 param_7 = int3(i1.x, i0.y, i1.z);
        float3 c101_1 = cnaLutFetch(param_7);
        return ((c000 + ((c001 - c000) * f.z)) + ((c101_1 - c001) * f.x)) + ((c111 - c101_1) * f.y);
    }
    if (f.z > f.y)
    {
        int3 param_8 = int3(i0.x, i0.y, i1.z);
        float3 c001_1 = cnaLutFetch(param_8);
        int3 param_9 = int3(i0.x, i1.y, i1.z);
        float3 c011 = cnaLutFetch(param_9);
        return ((c000 + ((c001_1 - c000) * f.z)) + ((c011 - c001_1) * f.y)) + ((c111 - c011) * f.x);
    }
    if (f.z > f.x)
    {
        int3 param_10 = int3(i0.x, i1.y, i0.z);
        float3 c010 = cnaLutFetch(param_10);
        int3 param_11 = int3(i0.x, i1.y, i1.z);
        float3 c011_1 = cnaLutFetch(param_11);
        return ((c000 + ((c010 - c000) * f.y)) + ((c011_1 - c010) * f.z)) + ((c111 - c011_1) * f.x);
    }
    int3 param_12 = int3(i0.x, i1.y, i0.z);
    float3 c010_1 = cnaLutFetch(param_12);
    int3 param_13 = int3(i1.x, i1.y, i0.z);
    float3 c110_1 = cnaLutFetch(param_13);
    return ((c000 + ((c010_1 - c000) * f.y)) + ((c110_1 - c010_1) * f.x)) + ((c111 - c110_1) * f.z);
}

float3 cnaLutTrilinear(float3 colour)
{
    float last = uColorGradeParams.x - 1.0f;
    float3 p = clamp(colour, 0.0f.xxx, 1.0f.xxx) * last;
    int3 i0 = int3(floor(p));
    int3 i1 = min((i0 + int3(1, 1, 1)), int(last).xxx);
    float3 f = p - float3(i0);
    int3 param = int3(i0.x, i0.y, i0.z);
    float3 c000 = cnaLutFetch(param);
    int3 param_1 = int3(i1.x, i0.y, i0.z);
    float3 c100 = cnaLutFetch(param_1);
    int3 param_2 = int3(i0.x, i1.y, i0.z);
    float3 c010 = cnaLutFetch(param_2);
    int3 param_3 = int3(i1.x, i1.y, i0.z);
    float3 c110 = cnaLutFetch(param_3);
    int3 param_4 = int3(i0.x, i0.y, i1.z);
    float3 c001 = cnaLutFetch(param_4);
    int3 param_5 = int3(i1.x, i0.y, i1.z);
    float3 c101 = cnaLutFetch(param_5);
    int3 param_6 = int3(i0.x, i1.y, i1.z);
    float3 c011 = cnaLutFetch(param_6);
    int3 param_7 = int3(i1.x, i1.y, i1.z);
    float3 c111 = cnaLutFetch(param_7);
    return lerp(lerp(lerp(c000, c100, f.x.xxx), lerp(c010, c110, f.x.xxx), f.y.xxx), lerp(lerp(c001, c101, f.x.xxx), lerp(c011, c111, f.x.xxx), f.y.xxx), f.z.xxx);
}

void frag_main()
{
    float4 source = texture1.Sample(_texture1_sampler, TexCoord);
    float3 colour = clamp(source.xyz, 0.0f.xxx, 1.0f.xxx);
    float3 _563;
    if (uColorGradeParams.z >= 0.5f)
    {
        float3 param = colour;
        _563 = cnaLutTetrahedral(param);
    }
    else
    {
        float3 param_1 = colour;
        _563 = cnaLutTrilinear(param_1);
    }
    float3 graded = _563;
    FragColor = float4(lerp(source.xyz, graded, uColorGradeParams.y.xxx), source.w) * SpriteColor;
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
inline constexpr std::string_view kContactShadowFragmentHlsl = R"CNA_HLSL(cbuffer FloatArray : register(b1)
{
    float uContactScalars[72] : packoffset(c0);
};

cbuffer Mat4Array : register(b2)
{
    row_major float4x4 uContactMatrices[72] : packoffset(c0);
};

cbuffer Vec2Array : register(b3)
{
    float2 uContactVectors[72] : packoffset(c0);
};

cbuffer Vec3Array : register(b4)
{
    float3 uContactDirections[72] : packoffset(c0);
};

Texture2D<float4> texture1 : register(t0);
SamplerState _texture1_sampler : register(s0);
Texture2D<float4> uDepthSampler : register(t1);
SamplerState _uDepthSampler_sampler : register(s1);

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

float2 cnaSnapToTexel(float2 uv)
{
    return (floor(uv * uContactVectors[0]) + 0.5f.xx) / uContactVectors[0];
}

float cnaDecodeLinearDepth(float4 channels)
{
    if (uContactScalars[6] < 0.5f)
    {
        return channels.x;
    }
    return dot(channels, float4(6.0308629201699659461155533790588e-08f, 1.5378700481960549950599670410156e-05f, 0.0039215688593685626983642578125f, 1.0f));
}

float3 cnaViewPositionFromDepth(float2 uv, float linearDepth)
{
    float2 cameraUv = float2(uv.x, 1.0f - uv.y);
    float4 clip = float4((cameraUv * 2.0f) - 1.0f.xx, 1.0f, 1.0f);
    float4 ray = mul(clip, uContactMatrices[1]);
    float3 direction = ray.xyz / ray.w.xxx;
    return direction * (linearDepth / max(-direction.z, 9.9999999747524270787835121154785e-07f));
}

float2 cnaTextureUvFromClip(float4 clip)
{
    float2 ndc = clip.xy / clip.w.xx;
    return float2((ndc.x * 0.5f) + 0.5f, 0.5f - (ndc.y * 0.5f));
}

bool cnaContactOccluded(float rayViewDepth, float sceneViewDepth, float bias, float thickness)
{
    float difference = rayViewDepth - sceneViewDepth;
    return (difference > bias) && (difference < thickness);
}

void frag_main()
{
    float4 scene = texture1.Sample(_texture1_sampler, TexCoord);
    float2 param = TexCoord;
    float4 param_1 = uDepthSampler.Sample(_uDepthSampler_sampler, cnaSnapToTexel(param));
    float centerDepth = cnaDecodeLinearDepth(param_1);
    if ((centerDepth <= 0.0f) || (centerDepth >= 0.999000012874603271484375f))
    {
        FragColor = scene * SpriteColor;
        return;
    }
    float2 param_2 = TexCoord;
    float param_3 = centerDepth;
    float3 position = cnaViewPositionFromDepth(param_2, param_3) * uContactScalars[0];
    float stepLength = uContactScalars[1] / uContactScalars[5];
    float occluded = 0.0f;
    for (int i = 1; i <= 64; i++)
    {
        if (float(i) > uContactScalars[5])
        {
            break;
        }
        float3 samplePosition = position + (uContactDirections[0] * (stepLength * float(i)));
        if (samplePosition.z >= (-9.9999999747524270787835121154785e-07f))
        {
            break;
        }
        float4 clip = mul(float4(samplePosition, 1.0f), uContactMatrices[0]);
        if (clip.w <= 0.0f)
        {
            break;
        }
        float4 param_4 = clip;
        float2 sampleUv = cnaTextureUvFromClip(param_4);
        bool _280 = sampleUv.x < 0.0f;
        bool _287;
        if (!_280)
        {
            _287 = sampleUv.x > 1.0f;
        }
        else
        {
            _287 = _280;
        }
        bool _294;
        if (!_287)
        {
            _294 = sampleUv.y < 0.0f;
        }
        else
        {
            _294 = _287;
        }
        bool _301;
        if (!_294)
        {
            _301 = sampleUv.y > 1.0f;
        }
        else
        {
            _301 = _294;
        }
        if (_301)
        {
            break;
        }
        float2 param_5 = sampleUv;
        float4 param_6 = uDepthSampler.SampleLevel(_uDepthSampler_sampler, cnaSnapToTexel(param_5), 0.0f);
        float sceneDepth = cnaDecodeLinearDepth(param_6);
        if ((sceneDepth <= 0.0f) || (sceneDepth >= 0.999000012874603271484375f))
        {
            continue;
        }
        float param_7 = -samplePosition.z;
        float param_8 = sceneDepth * uContactScalars[0];
        float param_9 = uContactScalars[3];
        float param_10 = uContactScalars[2];
        if (cnaContactOccluded(param_7, param_8, param_9, param_10))
        {
            occluded = 1.0f;
            break;
        }
    }
    float visibility = 1.0f - (occluded * clamp(uContactScalars[4], 0.0f, 1.0f));
    FragColor = float4(scene.xyz * visibility, scene.w) * SpriteColor;
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
inline constexpr std::string_view kDecalFragmentHlsl = R"CNA_HLSL(cbuffer FloatArray : register(b1)
{
    float uDecalScalars[72] : packoffset(c0);
};

cbuffer Mat4Array : register(b2)
{
    row_major float4x4 uDecalMatrices[72] : packoffset(c0);
};

cbuffer Vec3Array : register(b3)
{
    float3 uDecalVectors[72] : packoffset(c0);
};

Texture2D<float4> texture1 : register(t0);
SamplerState _texture1_sampler : register(s0);
Texture2D<float4> uNormalSampler : register(t2);
SamplerState _uNormalSampler_sampler : register(s2);
Texture2D<float4> uDecalSampler : register(t1);
SamplerState _uDecalSampler_sampler : register(s1);

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
    if (uDecalScalars[4] < 0.5f)
    {
        return channels.x;
    }
    return dot(channels, float4(6.0308629201699659461155533790588e-08f, 1.5378700481960549950599670410156e-05f, 0.0039215688593685626983642578125f, 1.0f));
}

float3 cnaViewPositionFromDepth(float2 uv, float linearDepth)
{
    float2 cameraUv = float2(uv.x, 1.0f - uv.y);
    float4 clip = float4((cameraUv * 2.0f) - 1.0f.xx, 1.0f, 1.0f);
    float4 ray = mul(clip, uDecalMatrices[0]);
    float3 direction = ray.xyz / ray.w.xxx;
    return direction * (linearDepth / max(-direction.z, 9.9999999747524270787835121154785e-07f));
}

void frag_main()
{
    float4 param = texture1.Sample(_texture1_sampler, TexCoord);
    float depth = cnaDecodeLinearDepth(param);
    if (depth >= 0.999000012874603271484375f)
    {
        discard;
    }
    float2 param_1 = TexCoord;
    float param_2 = depth;
    float3 viewPosition = cnaViewPositionFromDepth(param_1, param_2) * uDecalScalars[0];
    float3 local = mul(float4(viewPosition, 1.0f), uDecalMatrices[1]).xyz;
    float3 _141 = abs(local);
    if (any(bool3(_141.x > 0.5f.xxx.x, _141.y > 0.5f.xxx.y, _141.z > 0.5f.xxx.z)))
    {
        discard;
    }
    if (uDecalScalars[3] > 0.5f)
    {
        float3 normal = normalize((uNormalSampler.Sample(_uNormalSampler_sampler, TexCoord).xyz * 2.0f) - 1.0f.xxx);
        if (dot(normal, -uDecalVectors[0]) < uDecalScalars[2])
        {
            discard;
        }
    }
    float4 decal = uDecalSampler.Sample(_uDecalSampler_sampler, local.xy + 0.5f.xx);
    FragColor = float4(decal.xyz * uDecalVectors[1], decal.w * uDecalScalars[1]) * SpriteColor;
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
inline constexpr std::string_view kDepthOfFieldFragmentHlsl = R"CNA_HLSL(static const float2 _234[16] = { float2(0.21649999916553497314453125f, 0.074500001966953277587890625f), float2(-0.18629999458789825439453125f, 0.2549000084400177001953125f), float2(-0.085100002586841583251953125f, -0.3777000010013580322265625f), float2(0.3966000080108642578125f, 0.21539999544620513916015625f), float2(-0.464399993419647216796875f, 0.15090000629425048828125f), float2(0.22599999606609344482421875f, -0.4706999957561492919921875f), float2(0.1750999987125396728515625f, 0.541100025177001953125f), float2(-0.552699983119964599609375f, -0.233799993991851806640625f), float2(0.60329997539520263671875f, -0.24670000374317169189453125f), float2(-0.2249000072479248046875f, 0.64139997959136962890625f), float2(-0.3224999904632568359375f, -0.63209998607635498046875f), float2(0.710799992084503173828125f, 0.211600005626678466796875f), float2(-0.73570001125335693359375f, 0.24199999868869781494140625f), float2(0.294099986553192138671875f, -0.7627999782562255859375f), float2(0.24969999492168426513671875f, 0.8004000186920166015625f), float2(-0.80980002880096435546875f, -0.2646000087261199951171875f) };

cbuffer FloatArray : register(b1)
{
    float uDofScalars[72] : packoffset(c0);
};

Texture2D<float4> texture1 : register(t0);
SamplerState _texture1_sampler : register(s0);
Texture2D<float4> uDepthSampler : register(t1);
SamplerState _uDepthSampler_sampler : register(s1);

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
    if (uDofScalars[5] < 0.5f)
    {
        return channels.x;
    }
    return dot(channels, float4(6.0308629201699659461155533790588e-08f, 1.5378700481960549950599670410156e-05f, 0.0039215688593685626983642578125f, 1.0f));
}

float cnaCircleOfConfusionMm(float depthWorld)
{
    float focusDistance = uDofScalars[1];
    float focalLength = uDofScalars[2];
    float fNumber = uDofScalars[3];
    if (((depthWorld <= 0.0f) || (focusDistance <= 0.0f)) || (fNumber <= 0.0f))
    {
        return 0.0f;
    }
    float focusMm = focusDistance * 1000.0f;
    float depthMm = depthWorld * 1000.0f;
    if (focusMm <= focalLength)
    {
        return 0.0f;
    }
    return (((focalLength * focalLength) / (fNumber * (focusMm - focalLength))) * abs(depthMm - focusMm)) / depthMm;
}

float cnaBlurRadius(float linearDepth)
{
    float param = linearDepth * uDofScalars[0];
    float diameterMm = cnaCircleOfConfusionMm(param);
    return min((0.5f * diameterMm) / 24.0f, uDofScalars[4]);
}

void frag_main()
{
    float3 centerColor = texture1.Sample(_texture1_sampler, TexCoord).xyz;
    float4 param = uDepthSampler.Sample(_uDepthSampler_sampler, TexCoord);
    float centerDepth = cnaDecodeLinearDepth(param);
    if ((centerDepth <= 0.0f) || (centerDepth >= 0.999000012874603271484375f))
    {
        FragColor = float4(centerColor, 1.0f) * SpriteColor;
        return;
    }
    float param_1 = centerDepth;
    float centerRadius = cnaBlurRadius(param_1);
    float3 sum = centerColor;
    float weight = 1.0f;
    for (int i = 0; i < 16; i++)
    {
        float2 offset = _234[i] * centerRadius;
        float2 tapUv = TexCoord + offset;
        bool _248 = tapUv.x < 0.0f;
        bool _255;
        if (!_248)
        {
            _255 = tapUv.x > 1.0f;
        }
        else
        {
            _255 = _248;
        }
        bool _263;
        if (!_255)
        {
            _263 = tapUv.y < 0.0f;
        }
        else
        {
            _263 = _255;
        }
        bool _270;
        if (!_263)
        {
            _270 = tapUv.y > 1.0f;
        }
        else
        {
            _270 = _263;
        }
        if (_270)
        {
            continue;
        }
        float4 param_2 = uDepthSampler.Sample(_uDepthSampler_sampler, tapUv);
        float tapDepth = cnaDecodeLinearDepth(param_2);
        if ((tapDepth <= 0.0f) || (tapDepth >= 0.999000012874603271484375f))
        {
            continue;
        }
        float param_3 = tapDepth;
        float tapRadius = cnaBlurRadius(param_3);
        float accept = smoothstep(0.0f, max(length(offset), 9.9999997473787516355514526367188e-06f), tapRadius);
        sum += (texture1.Sample(_texture1_sampler, tapUv).xyz * accept);
        weight += accept;
    }
    FragColor = float4(sum / max(weight, 9.9999997473787516355514526367188e-06f).xxx, 1.0f) * SpriteColor;
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
inline constexpr std::string_view kFilmGrainFragmentHlsl = R"CNA_HLSL(cbuffer PushConstants : register(b1)
{
    float2 viewportSize : packoffset(c0);
    row_major float4x4 uMatrix : packoffset(c1);
    float4 uGrainParams : packoffset(c5);
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

float cnaHash(float2 p)
{
    return frac(sin(dot(p, float2(12.98980045318603515625f, 78.233001708984375f))) * 43758.546875f);
}

void frag_main()
{
    float4 source = texture1.Sample(_texture1_sampler, TexCoord);
    float2 pixel = floor(TexCoord * uGrainParams.xy);
    float2 param = pixel + float2(uGrainParams.w * 71.0f, uGrainParams.w * 113.0f);
    float _noise = cnaHash(param) - 0.5f;
    float luma = dot(source.xyz, float3(0.2989999949932098388671875f, 0.58700001239776611328125f, 0.114000000059604644775390625f));
    float weight = 1.0f - abs((clamp(luma, 0.0f, 1.0f) * 2.0f) - 1.0f);
    FragColor = float4(source.xyz + ((_noise * uGrainParams.z) * weight).xxx, source.w) * SpriteColor;
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
inline constexpr std::string_view kFxaaFragmentHlsl = R"CNA_HLSL(cbuffer PushConstants : register(b1)
{
    float2 viewportSize : packoffset(c0);
    row_major float4x4 uMatrix : packoffset(c1);
    float4 uTexelSize : packoffset(c5);
    float uEdgeThreshold : packoffset(c6);
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

float luma(float3 c)
{
    return dot(c, float3(0.2989999949932098388671875f, 0.58700001239776611328125f, 0.114000000059604644775390625f));
}

void frag_main()
{
    float3 center = texture1.Sample(_texture1_sampler, TexCoord).xyz;
    float3 param = center;
    float lumaCenter = luma(param);
    float3 param_1 = texture1.Sample(_texture1_sampler, TexCoord + float2(-uTexelSize.x, -uTexelSize.y)).xyz;
    float lumaNW = luma(param_1);
    float3 param_2 = texture1.Sample(_texture1_sampler, TexCoord + float2(uTexelSize.x, -uTexelSize.y)).xyz;
    float lumaNE = luma(param_2);
    float3 param_3 = texture1.Sample(_texture1_sampler, TexCoord + float2(-uTexelSize.x, uTexelSize.y)).xyz;
    float lumaSW = luma(param_3);
    float3 param_4 = texture1.Sample(_texture1_sampler, TexCoord + float2(uTexelSize.x, uTexelSize.y)).xyz;
    float lumaSE = luma(param_4);
    float lumaMin = min(lumaCenter, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaCenter, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
    if ((lumaMax - lumaMin) < uEdgeThreshold)
    {
        FragColor = float4(center, 1.0f);
        return;
    }
    float2 direction = float2(-((lumaNW + lumaNE) - (lumaSW + lumaSE)), (lumaNW + lumaSW) - (lumaNE + lumaSE));
    float scale = 1.0f / (min(abs(direction.x), abs(direction.y)) + 0.125f);
    direction = clamp(direction * scale, (-8.0f).xx, 8.0f.xx) * uTexelSize.xy;
    float3 blended = (texture1.Sample(_texture1_sampler, TexCoord + (direction * (-0.16666667163372039794921875f))).xyz + texture1.Sample(_texture1_sampler, TexCoord + (direction * 0.16666667163372039794921875f)).xyz) * 0.5f;
    float3 wider = (blended * 0.5f) + ((texture1.Sample(_texture1_sampler, TexCoord + (direction * (-0.5f))).xyz + texture1.Sample(_texture1_sampler, TexCoord + (direction * 0.5f)).xyz) * 0.25f);
    float3 param_5 = wider;
    float lumaWider = luma(param_5);
    bool3 _243 = ((lumaWider < lumaMin) || (lumaWider > lumaMax)).xxx;
    FragColor = float4(float3(_243.x ? blended.x : wider.x, _243.y ? blended.y : wider.y, _243.z ? blended.z : wider.z), 1.0f) * SpriteColor;
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
inline constexpr std::string_view kHdrDisplayFragmentHlsl = R"CNA_HLSL(cbuffer PushConstants : register(b1)
{
    float2 viewportSize : packoffset(c0);
    row_major float4x4 uMatrix : packoffset(c1);
    float4 uHdrDisplayParams : packoffset(c5);
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

float cnaRollOff(float nits, float peak)
{
    float _79;
    if (nits <= 0.0f)
    {
        _79 = 0.0f;
    }
    else
    {
        _79 = (peak * nits) / (peak + nits);
    }
    return _79;
}

float3 cnaRec709ToRec2020(float3 c)
{
    return float3(dot(c, float3(0.627403914928436279296875f, 0.3292829990386962890625f, 0.04331310093402862548828125f)), dot(c, float3(0.069097302854061126708984375f, 0.9195404052734375f, 0.011362300254404544830322265625f)), dot(c, float3(0.0163914002478122711181640625f, 0.088013298809528350830078125f, 0.8955953121185302734375f)));
}

float3 cnaEncodePq(float3 nits)
{
    float3 l = clamp(nits / 10000.0f.xxx, 0.0f.xxx, 1.0f.xxx);
    float3 p = pow(l, 0.1593017578125f.xxx);
    return pow((0.8359375f.xxx + (p * 18.8515625f)) / (1.0f.xxx + (p * 18.6875f)), 78.84375f.xxx);
}

void frag_main()
{
    int space = int(uHdrDisplayParams.x);
    float4 source = texture1.Sample(_texture1_sampler, TexCoord);
    if (space == 0)
    {
        FragColor = source * SpriteColor;
        return;
    }
    if (space == 1)
    {
        FragColor = float4(source.xyz * (uHdrDisplayParams.y / 80.0f), source.w) * SpriteColor;
        return;
    }
    float3 nits = source.xyz * uHdrDisplayParams.y;
    float param = nits.x;
    float param_1 = uHdrDisplayParams.z;
    float param_2 = nits.y;
    float param_3 = uHdrDisplayParams.z;
    float param_4 = nits.z;
    float param_5 = uHdrDisplayParams.z;
    nits = float3(cnaRollOff(param, param_1), cnaRollOff(param_2, param_3), cnaRollOff(param_4, param_5));
    float3 param_6 = nits;
    float3 param_7 = cnaRec709ToRec2020(param_6);
    FragColor = float4(cnaEncodePq(param_7), source.w) * SpriteColor;
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
inline constexpr std::string_view kHeightFogFragmentHlsl = R"CNA_HLSL(cbuffer FloatArray : register(b1)
{
    float uFogScalars[72] : packoffset(c0);
};

cbuffer Mat4Array : register(b2)
{
    row_major float4x4 uFogMatrices[72] : packoffset(c0);
};

cbuffer Vec3Array : register(b3)
{
    float3 uFogVectors[72] : packoffset(c0);
};

Texture2D<float4> texture1 : register(t0);
SamplerState _texture1_sampler : register(s0);
Texture2D<float4> uDepthSampler : register(t1);
SamplerState _uDepthSampler_sampler : register(s1);

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
    if (uFogScalars[4] < 0.5f)
    {
        return channels.x;
    }
    return dot(channels, float4(6.0308629201699659461155533790588e-08f, 1.5378700481960549950599670410156e-05f, 0.0039215688593685626983642578125f, 1.0f));
}

float3 cnaViewPositionFromDepth(float2 uv, float linearDepth)
{
    float4 clip = float4((uv * 2.0f) - 1.0f.xx, 1.0f, 1.0f);
    float4 ray = mul(clip, uFogMatrices[0]);
    float3 direction = ray.xyz / ray.w.xxx;
    return direction * (linearDepth / max(-direction.z, 9.9999999747524270787835121154785e-07f));
}

float cnaOpticalDepth(float cameraHeight, float rayHeightStep, float _distance)
{
    float atCamera = uFogScalars[1] * exp((-uFogScalars[2]) * (cameraHeight - uFogScalars[3]));
    float climb = uFogScalars[2] * rayHeightStep;
    if (abs(climb) < 9.9999997473787516355514526367188e-06f)
    {
        return max(atCamera * _distance, 0.0f);
    }
    return max((atCamera * (1.0f - exp((-climb) * _distance))) / climb, 0.0f);
}

void frag_main()
{
    float4 source = texture1.Sample(_texture1_sampler, TexCoord);
    float4 param = uDepthSampler.Sample(_uDepthSampler_sampler, TexCoord);
    float depth = cnaDecodeLinearDepth(param);
    float farPlane = uFogScalars[0];
    float _173;
    if ((depth <= 0.0f) || (depth >= 0.999000012874603271484375f))
    {
        _173 = farPlane;
    }
    else
    {
        _173 = depth * farPlane;
    }
    float travelled = _173;
    float2 param_1 = TexCoord;
    float param_2 = max(depth, 9.9999997473787516355514526367188e-05f);
    float3 viewPosition = cnaViewPositionFromDepth(param_1, param_2) * farPlane;
    float4 world = mul(float4(viewPosition, 1.0f), uFogMatrices[1]);
    float4 cameraWorld = mul(float4(0.0f, 0.0f, 0.0f, 1.0f), uFogMatrices[1]);
    float3 alongRay = world.xyz - cameraWorld.xyz;
    float rayLength = max(length(alongRay), 9.9999997473787516355514526367188e-05f);
    float param_3 = cameraWorld.y;
    float param_4 = alongRay.y / rayLength;
    float param_5 = travelled;
    float optical = cnaOpticalDepth(param_3, param_4, param_5);
    float fog = 1.0f - exp(-optical);
    FragColor = float4(lerp(source.xyz, uFogVectors[0], clamp(fog, 0.0f, 1.0f).xxx), source.w) * SpriteColor;
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
inline constexpr std::string_view kLensFlareFragmentHlsl = R"CNA_HLSL(cbuffer PushConstants : register(b1)
{
    float2 viewportSize : packoffset(c0);
    row_major float4x4 uMatrix : packoffset(c1);
    float4 uLensFlareParams : packoffset(c5);
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

float3 cnaBright(float2 uv)
{
    bool _22 = uv.x < 0.0f;
    bool _30;
    if (!_22)
    {
        _30 = uv.x > 1.0f;
    }
    else
    {
        _30 = _22;
    }
    bool _38;
    if (!_30)
    {
        _38 = uv.y < 0.0f;
    }
    else
    {
        _38 = _30;
    }
    bool _45;
    if (!_38)
    {
        _45 = uv.y > 1.0f;
    }
    else
    {
        _45 = _38;
    }
    if (_45)
    {
        return 0.0f.xxx;
    }
    return max(texture1.Sample(_texture1_sampler, uv).xyz - uLensFlareParams.x.xxx, 0.0f.xxx);
}

void frag_main()
{
    float4 source = texture1.Sample(_texture1_sampler, TexCoord);
    float2 toCentre = 0.5f.xx - TexCoord;
    float3 ghosts = 0.0f.xxx;
    for (int i = 1; i <= 8; i++)
    {
        if (i > int(uLensFlareParams.w))
        {
            break;
        }
        float2 uv = TexCoord + (toCentre * (1.0f + (float(i) * uLensFlareParams.z)));
        float2 param = uv;
        ghosts += (cnaBright(param) / float(i).xxx);
    }
    FragColor = float4(source.xyz + (ghosts * uLensFlareParams.y), source.w) * SpriteColor;
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
inline constexpr std::string_view kLightShaftFragmentHlsl = R"CNA_HLSL(cbuffer PushConstants : register(b1)
{
    float2 viewportSize : packoffset(c0);
    row_major float4x4 uMatrix : packoffset(c1);
    float4 uLightShaftParams : packoffset(c5);
    float uDecay : packoffset(c6);
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

float3 cnaBright(float2 uv)
{
    bool _22 = uv.x < 0.0f;
    bool _30;
    if (!_22)
    {
        _30 = uv.x > 1.0f;
    }
    else
    {
        _30 = _22;
    }
    bool _38;
    if (!_30)
    {
        _38 = uv.y < 0.0f;
    }
    else
    {
        _38 = _30;
    }
    bool _45;
    if (!_38)
    {
        _45 = uv.y > 1.0f;
    }
    else
    {
        _45 = _38;
    }
    if (_45)
    {
        return 0.0f.xxx;
    }
    return max(texture1.Sample(_texture1_sampler, uv).xyz - uLightShaftParams.z.xxx, 0.0f.xxx);
}

void frag_main()
{
    float4 source = texture1.Sample(_texture1_sampler, TexCoord);
    float2 lightPosition = uLightShaftParams.xy;
    float2 outside = max(0.0f.xx - lightPosition, lightPosition - 1.0f.xx);
    float offScreen = max(max(outside.x, outside.y), 0.0f);
    float reach = clamp(1.0f - (offScreen * 2.0f), 0.0f, 1.0f);
    if (reach <= 0.0f)
    {
        FragColor = source * SpriteColor;
        return;
    }
    float2 _step = (lightPosition - TexCoord) / 24.0f.xx;
    float3 gathered = 0.0f.xxx;
    float weight = 1.0f;
    float2 uv = TexCoord;
    for (int i = 0; i < 24; i++)
    {
        uv += _step;
        float2 param = uv;
        gathered += (cnaBright(param) * weight);
        weight *= uDecay;
    }
    FragColor = float4(source.xyz + ((gathered * (uLightShaftParams.w / 24.0f)) * reach), source.w) * SpriteColor;
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
inline constexpr std::string_view kMotionBlurFragmentHlsl = R"CNA_HLSL(cbuffer FloatArray : register(b1)
{
    float uMotionScalars[72] : packoffset(c0);
};

cbuffer Mat4Array : register(b2)
{
    row_major float4x4 uMotionMatrices[72] : packoffset(c0);
};

Texture2D<float4> texture1 : register(t0);
SamplerState _texture1_sampler : register(s0);
Texture2D<float4> uVelocitySampler : register(t2);
SamplerState _uVelocitySampler_sampler : register(s2);
Texture2D<float4> uDepthSampler : register(t1);
SamplerState _uDepthSampler_sampler : register(s1);

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

float2 cnaDecodeVelocity(float4 channels)
{
    return (channels.xy - 0.5f.xx) * 2.0f;
}

float4 cnaGather(float4 source, inout float2 velocity, int sampleCount)
{
    float _distance = length(velocity);
    if (_distance > uMotionScalars[3])
    {
        velocity *= (uMotionScalars[3] / _distance);
    }
    float3 sum = source.xyz;
    float weight = 1.0f;
    for (int i = 1; i <= 16; i++)
    {
        if (i >= sampleCount)
        {
            break;
        }
        float2 uv = TexCoord - (velocity * (float(i) / float(sampleCount - 1)));
        bool _120 = uv.x < 0.0f;
        bool _127;
        if (!_120)
        {
            _127 = uv.x > 1.0f;
        }
        else
        {
            _127 = _120;
        }
        bool _135;
        if (!_127)
        {
            _135 = uv.y < 0.0f;
        }
        else
        {
            _135 = _127;
        }
        bool _142;
        if (!_135)
        {
            _142 = uv.y > 1.0f;
        }
        else
        {
            _142 = _135;
        }
        if (_142)
        {
            continue;
        }
        sum += texture1.Sample(_texture1_sampler, uv).xyz;
        weight += 1.0f;
    }
    return float4(sum / weight.xxx, source.w);
}

float cnaDecodeLinearDepth(float4 channels)
{
    if (uMotionScalars[5] < 0.5f)
    {
        return channels.x;
    }
    return dot(channels, float4(6.0308629201699659461155533790588e-08f, 1.5378700481960549950599670410156e-05f, 0.0039215688593685626983642578125f, 1.0f));
}

void frag_main()
{
    float4 source = texture1.Sample(_texture1_sampler, TexCoord);
    int sampleCount = int(uMotionScalars[4] + 0.5f);
    if (uMotionScalars[0] > 0.5f)
    {
        float4 stored = uVelocitySampler.Sample(_uVelocitySampler_sampler, TexCoord);
        if (stored.w < 0.5f)
        {
            float4 param = stored;
            float4 param_1 = source;
            float2 param_2 = cnaDecodeVelocity(param) * uMotionScalars[2];
            int param_3 = sampleCount;
            float4 _212 = cnaGather(param_1, param_2, param_3);
            FragColor = _212 * SpriteColor;
            return;
        }
    }
    float4 param_4 = uDepthSampler.Sample(_uDepthSampler_sampler, TexCoord);
    float depth = cnaDecodeLinearDepth(param_4);
    if ((depth <= 0.0f) || (depth >= 0.999000012874603271484375f))
    {
        FragColor = source * SpriteColor;
        return;
    }
    float4 clip = float4((TexCoord * 2.0f) - 1.0f.xx, 1.0f, 1.0f);
    float4 ray = mul(clip, uMotionMatrices[0]);
    float3 direction = ray.xyz / ray.w.xxx;
    float3 viewPosition = (direction * (depth / max(-direction.z, 9.9999999747524270787835121154785e-07f))) * uMotionScalars[1];
    float4 world = mul(float4(viewPosition, 1.0f), uMotionMatrices[1]);
    float4 previousClip = mul(world, uMotionMatrices[2]);
    if (previousClip.w <= 0.0f)
    {
        FragColor = source * SpriteColor;
        return;
    }
    float2 previousUv = ((previousClip.xy / previousClip.w.xx) * 0.5f) + 0.5f.xx;
    float2 velocity = (TexCoord - previousUv) * uMotionScalars[2];
    float4 param_5 = source;
    float2 param_6 = velocity;
    int param_7 = sampleCount;
    float4 _323 = cnaGather(param_5, param_6, param_7);
    FragColor = _323 * SpriteColor;
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
inline constexpr std::string_view kSpatialUpscaleFragmentHlsl = R"CNA_HLSL(cbuffer PushConstants : register(b1)
{
    float2 viewportSize : packoffset(c0);
    row_major float4x4 uMatrix : packoffset(c1);
    float4 uUpscaleParams : packoffset(c5);
    float uIdentity : packoffset(c6);
};

Texture2D<float4> texture1 : register(t0);
SamplerState _texture1_sampler : register(s0);

static float4 FragColor;
static float2 TexCoord;
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

float3 cnaFetch(float2 texel)
{
    float2 sourceSize = uUpscaleParams.xy;
    return texture1.Sample(_texture1_sampler, clamp(texel, 0.5f.xx, sourceSize - 0.5f.xx) / sourceSize).xyz;
}

float cnaLuma(float3 colour)
{
    return dot(colour, float3(0.2125999927520751953125f, 0.715200006961822509765625f, 0.072200000286102294921875f));
}

void frag_main()
{
    if (uIdentity > 0.5f)
    {
        FragColor = texture1.Sample(_texture1_sampler, TexCoord) * SpriteColor;
        return;
    }
    float2 position = (TexCoord * uUpscaleParams.xy) - 0.5f.xx;
    float2 base = floor(position);
    float2 f = position - base;
    float2 param = base + 0.5f.xx;
    float3 c00 = cnaFetch(param);
    float2 param_1 = base + float2(1.5f, 0.5f);
    float3 c10 = cnaFetch(param_1);
    float2 param_2 = base + float2(0.5f, 1.5f);
    float3 c01 = cnaFetch(param_2);
    float2 param_3 = base + 1.5f.xx;
    float3 c11 = cnaFetch(param_3);
    float3 upscaled = lerp(lerp(c00, c10, f.x.xxx), lerp(c01, c11, f.x.xxx), f.y.xxx);
    if (uUpscaleParams.w > 0.5f)
    {
        float3 param_4 = c00;
        float l00 = cnaLuma(param_4);
        float3 param_5 = c10;
        float l10 = cnaLuma(param_5);
        float3 param_6 = c01;
        float l01 = cnaLuma(param_6);
        float3 param_7 = c11;
        float l11 = cnaLuma(param_7);
        float2 gradient = float2((l10 + l11) - (l00 + l01), (l01 + l11) - (l00 + l10));
        float strength = length(gradient);
        if (strength > 9.9999997473787516355514526367188e-05f)
        {
            float2 edge = normalize(float2(-gradient.y, gradient.x));
            float2 param_8 = ((base + 0.5f.xx) + f) + edge;
            float2 param_9 = ((base + 0.5f.xx) + f) - edge;
            float3 along = cnaFetch(param_8) + cnaFetch(param_9);
            float trust = clamp(strength * 2.0f, 0.0f, 1.0f) * 0.5f;
            upscaled = lerp(upscaled, along * 0.5f, trust.xxx);
        }
    }
    if (uUpscaleParams.z > 0.0f)
    {
        float2 param_10 = base + float2(0.5f, -0.5f);
        float3 up = cnaFetch(param_10);
        float2 param_11 = base + float2(0.5f, 1.5f);
        float3 down = cnaFetch(param_11);
        float2 param_12 = base + float2(-0.5f, 0.5f);
        float3 left = cnaFetch(param_12);
        float2 param_13 = base + float2(1.5f, 0.5f);
        float3 right = cnaFetch(param_13);
        float3 neighbourhood = (((up + down) + left) + right) * 0.25f;
        float3 sharpened = upscaled + ((upscaled - neighbourhood) * uUpscaleParams.z);
        float3 lowest = min(min(min(up, down), min(left, right)), upscaled);
        float3 highest = max(max(max(up, down), max(left, right)), upscaled);
        upscaled = clamp(sharpened, lowest, highest);
    }
    FragColor = float4(upscaled, 1.0f) * SpriteColor;
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
inline constexpr std::string_view kSsaoComposeFragmentHlsl = R"CNA_HLSL(cbuffer Vec2Array : register(b1)
{
    float2 uSsaoComposeVectors[72] : packoffset(c0);
};

cbuffer FloatArray : register(b2)
{
    float uSsaoComposeScalars[72] : packoffset(c0);
};

Texture2D<float4> uOcclusionSampler : register(t1);
SamplerState _uOcclusionSampler_sampler : register(s1);
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
    float blurred = 0.0f;
    for (int y = -2; y <= 2; y++)
    {
        for (int x = -2; x <= 2; x++)
        {
            blurred += uOcclusionSampler.Sample(_uOcclusionSampler_sampler, TexCoord + (float2(float(x), float(y)) * uSsaoComposeVectors[0])).x;
        }
    }
    blurred /= 25.0f;
    float visibility = clamp(1.0f - ((1.0f - blurred) * uSsaoComposeScalars[0]), 0.0f, 1.0f);
    float4 scene = texture1.Sample(_texture1_sampler, TexCoord);
    FragColor = float4(scene.xyz * visibility, scene.w) * SpriteColor;
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
inline constexpr std::string_view kSsaoOcclusionFragmentHlsl = R"CNA_HLSL(cbuffer FloatArray : register(b1)
{
    float uSsaoScalars[72] : packoffset(c0);
};

cbuffer Vec2Array : register(b2)
{
    float2 uSsaoVectors[72] : packoffset(c0);
};

cbuffer Vec3Array : register(b3)
{
    float3 uSsaoKernel[72] : packoffset(c0);
};

Texture2D<float4> texture1 : register(t0);
SamplerState _texture1_sampler : register(s0);
Texture2D<float4> uNormalSampler : register(t1);
SamplerState _uNormalSampler_sampler : register(s1);
Texture2D<float4> uNoiseSampler : register(t2);
SamplerState _uNoiseSampler_sampler : register(s2);

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
    if (uSsaoScalars[4] < 0.5f)
    {
        return channels.x;
    }
    return dot(channels, float4(6.0308629201699659461155533790588e-08f, 1.5378700481960549950599670410156e-05f, 0.0039215688593685626983642578125f, 1.0f));
}

void frag_main()
{
    float4 param = texture1.Sample(_texture1_sampler, TexCoord);
    float centerDepth = cnaDecodeLinearDepth(param);
    if (centerDepth <= 0.0f)
    {
        FragColor = 1.0f.xxxx;
        return;
    }
    float3 rawNormal = (uNormalSampler.Sample(_uNormalSampler_sampler, TexCoord).xyz * 2.0f) - 1.0f.xxx;
    float3 _84;
    if (length(rawNormal) > 9.9999997473787516355514526367188e-05f)
    {
        _84 = normalize(rawNormal);
    }
    else
    {
        _84 = float3(0.0f, 0.0f, 1.0f);
    }
    float3 normal = _84;
    float3 rawRandom = float3((uNoiseSampler.Sample(_uNoiseSampler_sampler, TexCoord * uSsaoVectors[0]).xy * 2.0f) - 1.0f.xx, 0.0f);
    float3 _116;
    if (length(rawRandom) > 9.9999997473787516355514526367188e-05f)
    {
        _116 = normalize(rawRandom);
    }
    else
    {
        _116 = float3(1.0f, 0.0f, 0.0f);
    }
    float3 randomVector = _116;
    float3 rawTangent = randomVector - (normal * dot(randomVector, normal));
    float3 _136;
    if (length(rawTangent) > 9.9999997473787516355514526367188e-05f)
    {
        _136 = normalize(rawTangent);
    }
    else
    {
        _136 = normalize(cross(normal, float3(0.0f, 1.0f, 0.0f)) + float3(0.001000000047497451305389404296875f, 0.0f, 0.0f));
    }
    float3 tangent = _136;
    float3 bitangent = cross(normal, tangent);
    float3x3 tbn = float3x3(float3(tangent), float3(bitangent), float3(normal));
    float occlusion = 0.0f;
    int count = int(uSsaoScalars[3] + 0.5f);
    for (int i = 0; i < 64; i++)
    {
        if (i >= count)
        {
            break;
        }
        float3 samplePosition = mul(uSsaoKernel[i], tbn);
        float2 sampleUv = TexCoord + (float2(samplePosition.x, -samplePosition.y) * uSsaoScalars[0]);
        float4 param_1 = texture1.SampleLevel(_texture1_sampler, sampleUv, 0.0f);
        float sampleDepth = cnaDecodeLinearDepth(param_1);
        if (sampleDepth <= 0.0f)
        {
            continue;
        }
        if (sampleDepth < (centerDepth - uSsaoScalars[1]))
        {
            float rangeCheck = smoothstep(0.0f, 1.0f, uSsaoScalars[2] / max(abs(centerDepth - sampleDepth), 9.9999997473787516355514526367188e-06f));
            occlusion += rangeCheck;
        }
    }
    float visibility = 1.0f - (occlusion / float(count));
    FragColor = float4(visibility, visibility, visibility, 1.0f) * SpriteColor;
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
inline constexpr std::string_view kSsrFragmentHlsl = R"CNA_HLSL(cbuffer FloatArray : register(b1)
{
    float uSsrScalars[72] : packoffset(c0);
};

cbuffer Mat4Array : register(b2)
{
    row_major float4x4 uSsrMatrices[72] : packoffset(c0);
};

cbuffer Vec2Array : register(b3)
{
    float2 uSsrVectors[72] : packoffset(c0);
};

Texture2D<float4> texture1 : register(t0);
SamplerState _texture1_sampler : register(s0);
Texture2D<float4> uDepthSampler : register(t1);
SamplerState _uDepthSampler_sampler : register(s1);
Texture2D<float4> uNormalSampler : register(t2);
SamplerState _uNormalSampler_sampler : register(s2);

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

float2 cnaSnapToTexel(float2 uv)
{
    return (floor(uv * uSsrVectors[0]) + 0.5f.xx) / uSsrVectors[0];
}

float cnaDecodeLinearDepth(float4 channels)
{
    if (uSsrScalars[8] < 0.5f)
    {
        return channels.x;
    }
    return dot(channels, float4(6.0308629201699659461155533790588e-08f, 1.5378700481960549950599670410156e-05f, 0.0039215688593685626983642578125f, 1.0f));
}

float3 cnaViewPositionFromDepth(float2 uv, float linearDepth)
{
    float2 cameraUv = float2(uv.x, 1.0f - uv.y);
    float4 clip = float4((cameraUv * 2.0f) - 1.0f.xx, 1.0f, 1.0f);
    float4 ray = mul(clip, uSsrMatrices[1]);
    float3 direction = ray.xyz / ray.w.xxx;
    return direction * (linearDepth / max(-direction.z, 9.9999999747524270787835121154785e-07f));
}

float2 cnaTextureUvFromClip(float4 clip)
{
    float2 ndc = clip.xy / clip.w.xx;
    return float2((ndc.x * 0.5f) + 0.5f, 0.5f - (ndc.y * 0.5f));
}

void frag_main()
{
    float3 sourceColor = texture1.SampleLevel(_texture1_sampler, TexCoord, 0.0f).xyz;
    float2 param = TexCoord;
    float4 param_1 = uDepthSampler.SampleLevel(_uDepthSampler_sampler, cnaSnapToTexel(param), 0.0f);
    float centerDepth = cnaDecodeLinearDepth(param_1);
    if ((centerDepth <= 0.0f) || (centerDepth >= 0.999000012874603271484375f))
    {
        FragColor = float4(sourceColor, 1.0f) * SpriteColor;
        return;
    }
    float2 param_2 = TexCoord;
    float4 normalTexel = uNormalSampler.SampleLevel(_uNormalSampler_sampler, cnaSnapToTexel(param_2), 0.0f);
    float roughness = clamp(normalTexel.w, 0.0f, 1.0f);
    float3 rawNormal = (normalTexel.xyz * 2.0f) - 1.0f.xxx;
    float3 _209;
    if (length(rawNormal) > 9.9999997473787516355514526367188e-05f)
    {
        _209 = normalize(rawNormal);
    }
    else
    {
        _209 = float3(0.0f, 0.0f, 1.0f);
    }
    float3 normal = _209;
    float2 param_3 = TexCoord;
    float param_4 = centerDepth;
    float3 position = cnaViewPositionFromDepth(param_3, param_4);
    float3 _227;
    if (length(position) > 9.9999999747524270787835121154785e-07f)
    {
        _227 = normalize(position);
    }
    else
    {
        _227 = float3(0.0f, 0.0f, -1.0f);
    }
    float3 incident = _227;
    float3 reflected = normalize(reflect(incident, normal));
    float stepLength = uSsrScalars[1] / uSsrScalars[7];
    float3 hitColor = 0.0f.xxx;
    float hit = 0.0f;
    float3 lastClear = position;
    float3 _479;
    float _504;
    for (int i = 1; i <= 64; i++)
    {
        if (float(i) > uSsrScalars[7])
        {
            break;
        }
        float3 samplePosition = position + (reflected * (stepLength * float(i)));
        if (samplePosition.z >= (-9.9999999747524270787835121154785e-07f))
        {
            break;
        }
        float4 clip = mul(float4(samplePosition * uSsrScalars[0], 1.0f), uSsrMatrices[0]);
        if (clip.w <= 0.0f)
        {
            break;
        }
        float4 param_5 = clip;
        float2 sampleUv = cnaTextureUvFromClip(param_5);
        bool _311 = sampleUv.x < 0.0f;
        bool _318;
        if (!_311)
        {
            _318 = sampleUv.x > 1.0f;
        }
        else
        {
            _318 = _311;
        }
        bool _325;
        if (!_318)
        {
            _325 = sampleUv.y < 0.0f;
        }
        else
        {
            _325 = _318;
        }
        bool _332;
        if (!_325)
        {
            _332 = sampleUv.y > 1.0f;
        }
        else
        {
            _332 = _325;
        }
        if (_332)
        {
            break;
        }
        float2 param_6 = sampleUv;
        float2 snappedUv = cnaSnapToTexel(param_6);
        float4 param_7 = uDepthSampler.SampleLevel(_uDepthSampler_sampler, snappedUv, 0.0f);
        float sceneDepth = cnaDecodeLinearDepth(param_7);
        if ((sceneDepth <= 0.0f) || (sceneDepth >= 0.999000012874603271484375f))
        {
            continue;
        }
        float difference = (-samplePosition.z) - sceneDepth;
        bool _364 = difference > uSsrScalars[2];
        bool _372;
        if (_364)
        {
            _372 = difference < uSsrScalars[3];
        }
        else
        {
            _372 = _364;
        }
        if (_372)
        {
            float3 nearPoint = lastClear;
            float3 farPoint = samplePosition;
            for (int k = 0; k < 6; k++)
            {
                float3 middle = (nearPoint + farPoint) * 0.5f;
                float4 middleClip = mul(float4(middle * uSsrScalars[0], 1.0f), uSsrMatrices[0]);
                if (middleClip.w <= 0.0f)
                {
                    break;
                }
                float4 param_8 = middleClip;
                float2 param_9 = cnaTextureUvFromClip(param_8);
                float2 middleUv = cnaSnapToTexel(param_9);
                float4 param_10 = uDepthSampler.SampleLevel(_uDepthSampler_sampler, middleUv, 0.0f);
                float middleDepth = cnaDecodeLinearDepth(param_10);
                bool _429 = (middleDepth > 0.0f) && (middleDepth < 0.999000012874603271484375f);
                bool _440;
                if (_429)
                {
                    _440 = ((-middle.z) - middleDepth) > uSsrScalars[2];
                }
                else
                {
                    _440 = _429;
                }
                bool behind = _440;
                if (behind)
                {
                    farPoint = middle;
                }
                else
                {
                    nearPoint = middle;
                }
            }
            float4 hitClip = mul(float4(farPoint * uSsrScalars[0], 1.0f), uSsrMatrices[0]);
            float4 param_11 = hitClip;
            float2 param_12 = cnaTextureUvFromClip(param_11);
            float2 hitUv = cnaSnapToTexel(param_12);
            float3 rawHitNormal = (uNormalSampler.SampleLevel(_uNormalSampler_sampler, hitUv, 0.0f).xyz * 2.0f) - 1.0f.xxx;
            if (length(rawHitNormal) > 9.9999997473787516355514526367188e-05f)
            {
                _479 = normalize(rawHitNormal);
            }
            else
            {
                _479 = float3(0.0f, 0.0f, 1.0f);
            }
            float3 hitNormal = _479;
            if (dot(reflected, hitNormal) > 0.0f)
            {
                break;
            }
            float2 toEdge = min(hitUv, 1.0f.xx - hitUv);
            if (uSsrScalars[5] > 0.0f)
            {
                _504 = min(smoothstep(0.0f, uSsrScalars[5], toEdge.x), smoothstep(0.0f, uSsrScalars[5], toEdge.y));
            }
            else
            {
                _504 = 1.0f;
            }
            float fade = _504;
            float2 blur = (roughness * uSsrScalars[6]).xx;
            float3 gathered = (((texture1.SampleLevel(_texture1_sampler, hitUv, 0.0f).xyz + texture1.SampleLevel(_texture1_sampler, hitUv + float2(blur.x, 0.0f), 0.0f).xyz) + texture1.SampleLevel(_texture1_sampler, hitUv + float2(-blur.x, 0.0f), 0.0f).xyz) + texture1.SampleLevel(_texture1_sampler, hitUv + float2(0.0f, blur.y), 0.0f).xyz) + texture1.SampleLevel(_texture1_sampler, hitUv + float2(0.0f, -blur.y), 0.0f).xyz;
            hitColor = gathered * 0.20000000298023223876953125f;
            hit = fade;
            break;
        }
        lastClear = samplePosition;
    }
    FragColor = float4(lerp(sourceColor, hitColor, (hit * uSsrScalars[4]).xxx), 1.0f) * SpriteColor;
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
inline constexpr std::string_view kTonemapFragmentHlsl = R"CNA_HLSL(cbuffer PushConstants : register(b1)
{
    float2 viewportSize : packoffset(c0);
    row_major float4x4 uMatrix : packoffset(c1);
    float4 uTonemapParams : packoffset(c5);
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

float3 reinhard(float3 c)
{
    return c / (1.0f.xxx + c);
}

float3 filmic(float3 c)
{
    float3 x = max(0.0f.xxx, c - 0.0040000001899898052215576171875f.xxx);
    return (x * ((x * 6.19999980926513671875f) + 0.5f.xxx)) / ((x * ((x * 6.19999980926513671875f) + 1.7000000476837158203125f.xxx)) + 0.0599999986588954925537109375f.xxx);
}

float3 aces(float3 c)
{
    return clamp((c * ((c * 2.5099999904632568359375f) + 0.02999999932944774627685546875f.xxx)) / ((c * ((c * 2.4300000667572021484375f) + 0.589999973773956298828125f.xxx)) + 0.14000000059604644775390625f.xxx), 0.0f.xxx, 1.0f.xxx);
}

float3 uncharted2Curve(float3 x)
{
    return (((x * ((x * 0.1500000059604644775390625f) + 0.0500000007450580596923828125f.xxx)) + 0.0040000001899898052215576171875f.xxx) / ((x * ((x * 0.1500000059604644775390625f) + 0.5f.xxx)) + 0.0599999986588954925537109375f.xxx)) - 0.066666670143604278564453125f.xxx;
}

float3 uncharted2(float3 c)
{
    float3 param = c;
    float3 param_1 = 11.19999980926513671875f.xxx;
    return uncharted2Curve(param) / uncharted2Curve(param_1);
}

float cnaDitherHash(float2 position)
{
    return frac(sin(dot(position, float2(12.98980045318603515625f, 78.233001708984375f))) * 43758.546875f);
}

float cnaTriangularDither(float2 position)
{
    float2 param = position;
    float2 param_1 = position + float2(17.0f, 23.0f);
    return cnaDitherHash(param) - cnaDitherHash(param_1);
}

void frag_main()
{
    int mode = int(uTonemapParams.x);
    float4 source = texture1.Sample(_texture1_sampler, TexCoord);
    float3 color = source.xyz * uTonemapParams.y;
    if (mode == 1)
    {
        float3 param = color;
        color = reinhard(param);
    }
    else
    {
        if (mode == 2)
        {
            float3 param_1 = color;
            color = filmic(param_1);
        }
        else
        {
            if (mode == 3)
            {
                float3 param_2 = color;
                color = aces(param_2);
            }
            else
            {
                if (mode == 4)
                {
                    float3 param_3 = color;
                    color = uncharted2(param_3);
                }
            }
        }
    }
    color = clamp(color, 0.0f.xxx, 1.0f.xxx);
    if (mode != 2)
    {
        color = pow(color, uTonemapParams.z.xxx);
    }
    if (uTonemapParams.w > 0.0f)
    {
        float2 param_4 = gl_FragCoord.xy;
        color += (cnaTriangularDither(param_4) * uTonemapParams.w).xxx;
    }
    FragColor = float4(color, source.w) * SpriteColor;
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
inline constexpr std::string_view kWeightedTransparencyResolveFragmentHlsl = R"CNA_HLSL(Texture2D<float4> texture1 : register(t0);
SamplerState _texture1_sampler : register(s0);
Texture2D<float4> uRevealage : register(t1);
SamplerState _uRevealage_sampler : register(s1);

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
    float4 accumulation = texture1.Sample(_texture1_sampler, TexCoord);
    float revealage = clamp(exp(uRevealage.Sample(_uRevealage_sampler, TexCoord).x), 0.0f, 1.0f);
    if (revealage > 0.99989998340606689453125f)
    {
        discard;
    }
    float3 colour = accumulation.xyz / max(accumulation.w, 9.9999997473787516355514526367188e-06f).xxx;
    FragColor = float4(colour, 1.0f - revealage);
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
inline constexpr std::array<Fragment, 27> kFragments{{
    Fragment{"post_process/aerial_perspective.vulkan.frag.spv", kAerialPerspectiveFragmentHlsl, "5c45775d44ff1b1f8c3185db8e82bb25cd234c38bfc8a96c70ab1cc7fde6571f"},
    Fragment{"post_process/bloom_blur.vulkan.frag.spv", kBloomBlurFragmentHlsl, "08427f39eee25462f84670c22a8bd713b10a7c9605b129e0292fa951925634db"},
    Fragment{"post_process/bloom_combine.vulkan.frag.spv", kBloomCombineFragmentHlsl, "cb54f72ecadc78813385a73358e9074da923151ddf5cde45a813c645e1a89604"},
    Fragment{"post_process/bloom_extract.vulkan.frag.spv", kBloomExtractFragmentHlsl, "6fbb3429cce34154dcb5cc926ba0e08bd2cd7e37f1e4cf4fa5e0015e083d9e8a"},
    Fragment{"post_process/bloom_upsample.vulkan.frag.spv", kBloomUpsampleFragmentHlsl, "d548699956ba5de5de240ed823fa26d5df72f8f83fb0f41924777bb0aad9c3cc"},
    Fragment{"post_process/chromatic.vulkan.frag.spv", kChromaticFragmentHlsl, "ba84740abf65410ffe208652abb9f7e2cf9c915f2c28c2e1a8bd7d18b2537f32"},
    Fragment{"post_process/color_grade_interpolated_strip.vulkan.frag.spv", kColorGradeInterpolatedStripFragmentHlsl, "b3953ca618c907b8d868ce575e5e410d4575f221515785726b6e965f263a24c6"},
    Fragment{"post_process/color_grade_strip.vulkan.frag.spv", kColorGradeStripFragmentHlsl, "97d13bba7d44a6e64a31550a7493c71b6e7389be1abdb921281cd94c6dbb5908"},
    Fragment{"post_process/color_grade_volume.vulkan.frag.spv", kColorGradeVolumeFragmentHlsl, "b1d51788e372e0b90a767d408a4a6a0dbff18fecb249cb258fbddb974a525424"},
    Fragment{"post_process/contact_shadow.vulkan.frag.spv", kContactShadowFragmentHlsl, "43d1e86ce0209e628ece72df95f404161cacab42d4712ab88f3950c4e4cf2037"},
    Fragment{"post_process/crt.vulkan.frag.spv", kCrtFragmentHlsl, "cc9d9b1e1110e0bd486722efa25cd514e04bf1667ad3950698a21edb94b8da4b"},
    Fragment{"post_process/decal.vulkan.frag.spv", kDecalFragmentHlsl, "a4c3bc04d5176d6c0f6c76814b629aab016c4559dab1d77bc6c986ae867871fc"},
    Fragment{"post_process/depth_effect.vulkan.frag.spv", kDepthEffectFragmentHlsl, "64bec1bd3040f27905b3e2e1289e5e387dbd8f701eaf8fe951a7770f3b4b8778"},
    Fragment{"post_process/depth_of_field.vulkan.frag.spv", kDepthOfFieldFragmentHlsl, "7c41daae1cba57cde44d4bbb1f5d16c990ad559fcca6dbdfd9ae5ac4931ed27b"},
    Fragment{"post_process/film_grain.vulkan.frag.spv", kFilmGrainFragmentHlsl, "9ea7dbc92fdf62ba4e437064106ba1d11686de66aabdb2ef0bab36d34101dc01"},
    Fragment{"post_process/fxaa.vulkan.frag.spv", kFxaaFragmentHlsl, "590117f758c2224108abe2f177d7acd19fbee0dd9747528df6fc4fc9d3d7eaf5"},
    Fragment{"post_process/hdr_display.vulkan.frag.spv", kHdrDisplayFragmentHlsl, "aa6b745e6b15e93befc802147837e235becaf739e5cb6ec34ef674d4eb4751ba"},
    Fragment{"post_process/height_fog.vulkan.frag.spv", kHeightFogFragmentHlsl, "e15f99f858107a25d0ab9a1686d32aa9f960d47bb3a9356a454cbb848a88a161"},
    Fragment{"post_process/lens_flare.vulkan.frag.spv", kLensFlareFragmentHlsl, "beb9462227f8eb7fbfa0ab74248575a6f472a60e630d33495296f48934040057"},
    Fragment{"post_process/light_shaft.vulkan.frag.spv", kLightShaftFragmentHlsl, "ba1048fde7f7acf50df753933bca2c86a7e22b938ae7e998f632326b6f511d0d"},
    Fragment{"post_process/motion_blur.vulkan.frag.spv", kMotionBlurFragmentHlsl, "1a7d40091ea339f82f81a3de563fa8636773a0878e6b7a3012c9054b2f8ebc40"},
    Fragment{"post_process/spatial_upscale.vulkan.frag.spv", kSpatialUpscaleFragmentHlsl, "705634f28a74334ec9928097fd8b247194bc680966c2e25404ae8d0d65b5895e"},
    Fragment{"post_process/ssao_compose.vulkan.frag.spv", kSsaoComposeFragmentHlsl, "7cc060eb485c29bde1f6e9ca6ef3b04d62e84615a65884879cc6fadbeb474aa6"},
    Fragment{"post_process/ssao_occlusion.vulkan.frag.spv", kSsaoOcclusionFragmentHlsl, "27bd8083e787bb6fd09d436a6d5d676f7bdfcceda0c13986eb91ea8ae8b81d7a"},
    Fragment{"post_process/ssr.vulkan.frag.spv", kSsrFragmentHlsl, "1fb0fb8d6cb20e26ac79388a76dccd01c361e49796b978a4874d37bf3af01dfa"},
    Fragment{"post_process/tonemap.vulkan.frag.spv", kTonemapFragmentHlsl, "c2e7dfb599f0c0368d6e380394c9c551c7b81e9f84a27c6c65a498b931337028"},
    Fragment{"post_process/weighted_transparency_resolve.vulkan.frag.spv", kWeightedTransparencyResolveFragmentHlsl, "b4bf191f8928db43d5ef4b2e491029a8774d1375977fc35003ef940c89048257"},
}};
inline std::string_view FindFragment(std::string_view label) {
    for (const auto& fragment : kFragments)
        if (fragment.label == label) return fragment.source;
    return {};
}
}
