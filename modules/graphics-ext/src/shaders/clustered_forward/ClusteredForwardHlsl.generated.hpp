// SPDX-License-Identifier: MS-PL
// Rebuild with tools/shader_package/generate_clustered_forward_hlsl.py.
#pragma once
#include <array>
#include <string>
#include <string_view>
namespace CNA::Graphics::detail::ClusteredForwardHlslGenerated {
inline constexpr std::string_view kVertexSourceSha256 = "f418ed961d5e0037e561902b25f6d20b638c40f28f89fe260a081e899a1a6cc5";
inline constexpr std::array<std::string_view, 1> kVertexParts{{
    R"CNA_HLSL(cbuffer Mat4Array : register(b0)
{
    row_major float4x4 uClusterMatrices[72] : packoffset(c0);
};


static float4 gl_Position;
static float3 aPosition;
static float3 vWorldPosition;
static float3 vWorldNormal;
static float3 aNormal;
static float4 vClipPosition;
static float vViewDistance;

struct SPIRV_Cross_Input
{
    float3 aPosition : POSITION;
    float3 aNormal : NORMAL;
};

struct SPIRV_Cross_Output
{
    float3 vWorldPosition : TEXCOORD0;
    float3 vWorldNormal : TEXCOORD1;
    float4 vClipPosition : TEXCOORD2;
    float vViewDistance : TEXCOORD3;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    float4 world = mul(float4(aPosition, 1.0f), uClusterMatrices[0]);
    float4 view = mul(world, uClusterMatrices[1]);
    gl_Position = mul(view, uClusterMatrices[2]);
    vWorldPosition = world.xyz;
    vWorldNormal = mul(aNormal, float3x3(uClusterMatrices[0][0].xyz, uClusterMatrices[0][1].xyz, uClusterMatrices[0][2].xyz));
    vClipPosition = gl_Position;
    vViewDistance = -view.z;
    gl_Position.y = -gl_Position.y;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    aPosition = stage_input.aPosition;
    aNormal = stage_input.aNormal;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.vWorldPosition = vWorldPosition;
    stage_output.vWorldNormal = vWorldNormal;
    stage_output.vClipPosition = vClipPosition;
    stage_output.vViewDistance = vViewDistance;
    return stage_output;
}
)CNA_HLSL",
}};
inline std::string VertexSource() {
    std::string result;
    result.reserve(1505);
    for (const auto part : kVertexParts) result.append(part);
    return result;
}
inline constexpr std::string_view kFragmentSourceSha256 = "088fdc85bb5330ef9a574f1faeb2cb5b9ce67c68dbc86e0e10e35e1c3583c13f";
inline constexpr std::array<std::string_view, 4> kFragmentParts{{
    R"CNA_HLSL(struct CnaClusteredLight
{
    float3 position;
    float range;
    float3 colour;
    float isSpot;
    float3 direction;
    float cosOuter;
    float cosInner;
};

ByteAddressBuffer _173 : register(t6);
cbuffer FloatArray : register(b4)
{
    float uClusterScalars[72] : packoffset(c0);
};

ByteAddressBuffer _318 : register(t7);
ByteAddressBuffer _329 : register(t8);
cbuffer Vec3Array : register(b5)
{
    float3 uClusterVectors[72] : packoffset(c0);
};

cbuffer Mat4Array : register(b6)
{
    row_major float4x4 uClusterMatrices[72] : packoffset(c0);
};

Texture2D<float4> uCnaAreaBrdf : register(t0);
SamplerState _uCnaAreaBrdf_sampler : register(s0);
Texture2D<float4> uOpaqueFrame : register(t1);
SamplerState _uOpaqueFrame_sampler : register(s1);

static float3 vWorldNormal;
static float3 vWorldPosition;
static float4 vClipPosition;
static float vViewDistance;
static float4 FragColor;

struct SPIRV_Cross_Input
{
    float3 vWorldPosition : TEXCOORD0;
    float3 vWorldNormal : TEXCOORD1;
    float4 vClipPosition : TEXCOORD2;
    float vViewDistance : TEXCOORD3;
};

struct SPIRV_Cross_Output
{
    float4 FragColor : SV_Target0;
};

int cnaClusterFromNdc(float2 ndc, float viewDistance)
{
    int tx = clamp(int(((ndc.x * 0.5f) + 0.5f) * float(int(uClusterScalars[18]))), 0, int(uClusterScalars[18]) - 1);
    int ty = clamp(int(((ndc.y * 0.5f) + 0.5f) * float(int(uClusterScalars[19]))), 0, int(uClusterScalars[19]) - 1);
    float span = log(uClusterScalars[23] / uClusterScalars[22]);
    float t = log(max(viewDistance, uClusterScalars[22]) / uClusterScalars[22]) / max(span, 9.9999999747524270787835121154785e-07f);
    int tz = clamp(int(floor(t * float(int(uClusterScalars[20])))), 0, int(uClusterScalars[20]) - 1);
    return (((tz * int(uClusterScalars[19])) + ty) * int(uClusterScalars[18])) + tx;
}

int cnaClusterLightCount(int cluster)
{
    return int(_318.Load(cluster * 8 + 4));
}

float3 cnaProbeIrradiance(float3 coefficients[72], float3 normal)
{
    float3 n = normalize(normal);
    float3 result = (((((coefficients[0] * 0.88622701168060302734375f) + ((((coefficients[1] * n.y) + (coefficients[2] * n.z)) + (coefficients[3] * n.x)) * 1.02332794666290283203125f)) + (((((coefficients[4] * n.x) * n.y) + ((coefficients[5] * n.y) * n.z)) + ((coefficients[7] * n.x) * n.z)) * 0.85808598995208740234375f)) + (((coefficients[6] * 0.743125021457672119140625f) * n.z) * n.z)) - (coefficients[6] * 0.2477079927921295166015625f)) + ((coefficients[8] * 0.429042994976043701171875f) * ((n.x * n.x) - (n.y * n.y)));
    return max(result, 0.0f.xxx);
}

float3 cnaFrameTangent(float3 axis)
{
    bool3 _836 = (abs(axis.z) < 0.89999997615814208984375f).xxx;
    float3 guess = float3(_836.x ? float3(0.0f, 0.0f, 1.0f).x : float3(1.0f, 0.0f, 0.0f).x, _836.y ? float3(0.0f, 0.0f, 1.0f).y : float3(1.0f, 0.0f, 0.0f).y, _836.z ? float3(0.0f, 0.0f, 1.0f).z : float3(1.0f, 0.0f, 0.0f).z);
    return normalize(cross(guess, axis));
}

void cnaAreaQuad(float3 surface, inout float3 quad[4])
{
    float3 right = uClusterVectors[16];
    float3 up = uClusterVectors[17];
    if (int(uClusterScalars[14]) == 1)
    {
        right *= 0.886226952075958251953125f;
        up *= 0.886226952075958251953125f;
    }
    else
    {
        if (int(uClusterScalars[14]) == 2)
        {
            float radius = length(up);
            float3 axis = normalize(right);
            float3 toSurface = surface - uClusterVectors[15];
            float3 facing = cross(toSurface, axis);
            float3 _896;
            if (dot(facing, facing) > 9.9999999600419720025001879548654e-13f)
            {
                _896 = normalize(facing);
            }
            else
            {
                float3 param = axis;
                _896 = cnaFrameTangent(param);
            }
            facing = _896;
            up = facing * radius;
        }
    }
    quad[0] = (uClusterVectors[15] - right) - up;
    quad[1] = (uClusterVectors[15] + right) - up;
    quad[2] = (uClusterVectors[15] + right) + up;
    quad[3] = (uClusterVectors[15] - right) + up;
}

float cnaIntegrateEdge(float3 a, float3 b)
{
    float cosine = clamp(dot(a, b), -0.99989998340606689453125f, 0.99989998340606689453125f);
    float angle = acos(cosine);
    return (cross(a, b).z * angle) / max(sin(angle), 9.9999997473787516355514526367188e-05f);
}

float cnaAreaCoverage(float3 quad[4], float3 surface, float3 lobeAxis, float lobeScale, bool twoSided)
{
    float3 axis = normalize(lobeAxis);
    float3 param = axis;
    float3 tangent = cnaFrameTangent(param);
    float3 bitangent = cross(axis, tangent);
    float inverseScale = 1.0f / max(lobeScale, 9.9999997473787516355514526367188e-05f);
    float3 p[5];
    for (int i = 0; i < 4; i++)
    {
        float3 relative = quad[i] - surface;
        p[i] = float3(dot(relative, tangent) * inverseScale, dot(relative, bitangent) * inverseScale, dot(relative, axis));
    }
    p[4] = p[0];
    int count = 0;
    float3 clipped[8];
    for (int i_1 = 0; i_1 < 4; i_1++)
    {
        float3 current = p[i_1];
        float3 next = p[i_1 + 1];
        bool currentIn = current.z > 0.0f;
        bool nextIn = next.z > 0.0f;
        if (currentIn)
        {
            clipped[count] = current;
            count++;
        }
        if (currentIn != nextIn)
        {
            float t = current.z / (current.z - next.z);
            clipped[count] = current + ((next - current) * t);
            count++;
        }
    }
    if (count < 3)
    {
        return 0.0f;
    }
    for (int i_2 = 0; i_2 < 8; i_2++)
    {
        if (i_2 >= count)
        {
            break;
        }
        clipped[i_2] = normalize(clipped[i_2]);
    }
    float sum = 0.0f;
    int _1125;
    for (int i_3 = 0; i_3 < 8; i_3++)
    {
        if (i_3 >= count)
        {
            break;
        }
        if ((i_3 + 1) == count)
        {
            _1125 = 0;
        }
        else
        {
            _1125 = i_3 + 1;
        }
        int next_1 = _1125;
        float3 param_1 = clipped[i_3];
        float3 param_2 = clipped[next_1];
        sum += cnaIntegrateEdge(param_1, param_2);
    }
    float _1146;
    if (twoSided)
    {
        _1146 = abs(sum);
    }
    else
    {
        _1146 = max(-sum, 0.0f);
    }
    sum = _1146;
    return clamp(sum / 6.283185482025146484375f, 0.0f, 1.0f);
}

float4 cnaAreaBrdfTerms(float nDotV, float roughness)
{
    float2 index = clamp(float2(nDotV, roughness), 0.0f.xx, 1.0f.xx);
    float2 uv = ((index * (uClusterScalars[15] - 1.0f)) + 0.5f.xx) / max(uClusterScalars[15], 1.0f).xx;
    return uCnaAreaBrdf.Sample(_uCnaAreaBrdf_sampler, uv);
}

float3 cnaAreaContribution(float3 surface, float3 normal, float3 viewDirection, float3 baseColor, float metallic, float roughness)
{
    if (int(uClusterScalars[14]) < 0)
    {
        return 0.0f.xxx;
    }
    float3 toLight = uClusterVectors[15] - surface;
    if (dot(toLight, toLight) >= (uClusterScalars[16] * uClusterScalars[16]))
    {
        return 0.0f.xxx;
    }
    float3 param = surface;
    float3 param_1[4];
    cnaAreaQuad(param, param_1);
    float3 quad[4] = param_1;
    bool twoSided = uClusterScalars[17] > 0.5f;
    float3 param_2[4] = quad;
    float3 param_3 = surface;
    float3 param_4 = normal;
    float param_5 = 1.0f;
    bool param_6 = twoSided;
    float diffuseCoverage = cnaAreaCoverage(param_2, param_3, param_4, param_5, param_6);
    float nDotV = clamp(dot(normal, viewDirection), 0.001000000047497451305389404296875f, 1.0f);
    float param_7 = nDotV;
    float param_8 = roughness;
    float4 terms = cnaAreaBrdfTerms(param_7, param_8);
    float3 tangentBase = (normal * nDotV) - viewDirection;
    float3 _1230;
    if (dot(tangentBase, tangentBase) > 9.9999999600419720025001879548654e-13f)
    {
        _1230 = normalize(tangentBase);
    }
    else
    {
        float3 param_9 = normal;
        _1230 = cnaFrameTangent(param_9);
    }
    float3 tangent = _1230;
)CNA_HLSL",
    R"CNA_HLSL(    float3 lobeAxis = normalize((tangent * terms.z) + (normal * terms.w));
    float lobeScale = max(roughness * roughness, 0.0199999995529651641845703125f);
    float3 param_10[4] = quad;
    float3 param_11 = surface;
    float3 param_12 = lobeAxis;
    float param_13 = lobeScale;
    bool param_14 = twoSided;
    float specularCoverage = cnaAreaCoverage(param_10, param_11, param_12, param_13, param_14);
    float scale = terms.x - terms.y;
    float bias = terms.y;
    float3 f0 = lerp(0.039999999105930328369140625f.xxx, baseColor, metallic.xxx);
    float3 specular = ((f0 * scale) + bias.xxx) * specularCoverage;
    float3 diffuse = (baseColor * diffuseCoverage) * (1.0f - metallic);
    return (diffuse + specular) * uClusterVectors[18];
}

int cnaClusterLightIndex(int cluster, int i)
{
    return int(_329.Load((_318.Load(cluster * 8 + 0) + uint(i)) * 4 + 0));
}

CnaClusteredLight cnaLoadLight(int index)
{
    int base = index * 4;
    CnaClusteredLight light;
    light.position = asfloat(_173.Load4(base * 16 + 0)).xyz;
    light.range = asfloat(_173.Load(base * 16 + 12));
    light.colour = asfloat(_173.Load4((base + 1) * 16 + 0)).xyz;
    light.isSpot = asfloat(_173.Load((base + 1) * 16 + 12));
    light.direction = asfloat(_173.Load4((base + 2) * 16 + 0)).xyz;
    light.cosOuter = asfloat(_173.Load((base + 2) * 16 + 12));
    light.cosInner = asfloat(_173.Load((base + 3) * 16 + 0));
    return light;
}

float cnaFalloff(float _distance, float range)
{
    float ratio = _distance / max(range, 9.9999997473787516355514526367188e-05f);
    float window = clamp(1.0f - (((ratio * ratio) * ratio) * ratio), 0.0f, 1.0f);
    return (window * window) / max(_distance * _distance, 9.9999997473787516355514526367188e-05f);
}

float3 cnaFresnel(float VoH, float3 f0)
{
    return f0 + ((1.0f.xxx - f0) * pow(clamp(1.0f - VoH, 0.0f, 1.0f), 5.0f));
}

float3 cnaFilmSchlick(float3 f0, float cosTheta)
{
    return f0 + ((1.0f.xxx - f0) * pow(clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f));
}

float cnaFilmSquare(float v)
{
    return v * v;
}

float cnaFilmIorToFresnel0(float transmitted, float incident)
{
    float param = (transmitted - incident) / (transmitted + incident);
    return cnaFilmSquare(param);
}

float cnaFilmSchlick(float f0, float cosTheta)
{
    return f0 + ((1.0f - f0) * pow(clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f));
}

float3 cnaFilmFresnel0ToIor(float3 fresnel0)
{
    float3 root = sqrt(clamp(fresnel0, 0.0f.xxx, 0.99989998340606689453125f.xxx));
    return (1.0f.xxx + root) / (1.0f.xxx - root);
}

float3 cnaFilmSquare(float3 v)
{
    return v * v;
}

float3 cnaFilmIorToFresnel0(float3 transmitted, float incident)
{
    float3 param = (transmitted - incident.xxx) / (transmitted + incident.xxx);
    return cnaFilmSquare(param);
}

float3 cnaFilmSensitivity(float opd, float3 shift)
{
    float phase = (6.283185482025146484375f * opd) * 9.999999717180685365747194737196e-10f;
    float3 value = float3(5.4856000384490011256843899900559e-13f, 4.4201000823883285217874572481378e-13f, 5.2481001315551889518928874167614e-13f);
    float3 position = float3(1681000.0f, 1795300.0f, 2208400.0f);
    float3 variance = float3(4327799808.0f, 9304599552.0f, 6612100096.0f);
    float param = phase;
    float3 xyz = ((value * sqrt(variance * 6.283185482025146484375f)) * cos((position * phase) + shift)) * exp(variance * (-cnaFilmSquare(param)));
    float param_1 = phase;
    xyz.x += ((1.6440829142538859741762280464172e-08f * cos((2239900.0f * phase) + shift.x)) * exp((-4528200192.0f) * cnaFilmSquare(param_1)));
    xyz /= 1.0685000262355970335192978382111e-07f.xxx;
    return float3(((3.240454196929931640625f * xyz.x) - (1.537138462066650390625f * xyz.y)) - (0.498531401157379150390625f * xyz.z), (((-0.969265997409820556640625f) * xyz.x) + (1.87601077556610107421875f * xyz.y)) + (0.04155600070953369140625f * xyz.z), ((0.0556433983147144317626953125f * xyz.x) - (0.2040258944034576416015625f * xyz.y)) + (1.05722522735595703125f * xyz.z));
}

float3 cnaThinFilmIridescence(float outsideIor, float filmIor, float cosTheta, float thicknessNm, float3 baseF0)
{
    float cosTheta1 = clamp(cosTheta, 0.0f, 1.0f);
    if (thicknessNm <= 0.0f)
    {
        float3 param = baseF0;
        float param_1 = cosTheta1;
        return cnaFilmSchlick(param, param_1);
    }
    float iridescenceIor = lerp(outsideIor, filmIor, smoothstep(0.0f, 0.02999999932944774627685546875f, thicknessNm));
    float param_2 = outsideIor / iridescenceIor;
    float param_3 = cosTheta1;
    float sinTheta2Squared = cnaFilmSquare(param_2) * (1.0f - cnaFilmSquare(param_3));
    float cosTheta2Squared = 1.0f - sinTheta2Squared;
    if (cosTheta2Squared < 0.0f)
    {
        return 1.0f.xxx;
    }
    float cosTheta2 = sqrt(cosTheta2Squared);
    float param_4 = iridescenceIor;
    float param_5 = outsideIor;
    float r0 = cnaFilmIorToFresnel0(param_4, param_5);
    float param_6 = r0;
    float param_7 = cosTheta1;
    float r12 = cnaFilmSchlick(param_6, param_7);
    float t121 = 1.0f - r12;
    float phi12 = (iridescenceIor < outsideIor) ? 3.1415927410125732421875f : 0.0f;
    float phi21 = 3.1415927410125732421875f - phi12;
    float3 param_8 = baseF0;
    float3 baseIor = cnaFilmFresnel0ToIor(param_8);
    float3 param_9 = baseIor;
    float param_10 = iridescenceIor;
    float3 param_11 = cnaFilmIorToFresnel0(param_9, param_10);
    float param_12 = cosTheta2;
    float3 r23 = cnaFilmSchlick(param_11, param_12);
    float3 phi23 = float3((baseIor.x < iridescenceIor) ? 3.1415927410125732421875f : 0.0f, (baseIor.y < iridescenceIor) ? 3.1415927410125732421875f : 0.0f, (baseIor.z < iridescenceIor) ? 3.1415927410125732421875f : 0.0f);
    float opd = ((2.0f * iridescenceIor) * thicknessNm) * cosTheta2;
    float3 phi = phi21.xxx + phi23;
    float3 r123 = clamp(r12.xxx * r23, 9.9999997473787516355514526367188e-06f.xxx, 0.99989998340606689453125f.xxx);
    float3 param_13 = t121.xxx;
    float3 rs = (cnaFilmSquare(param_13) * r23) / (1.0f.xxx - r123);
    float3 result = r12.xxx + rs;
    float3 cm = rs - t121.xxx;
    for (int order = 1; order <= 2; order++)
    {
        cm *= sqrt(r123);
        float param_14 = float(order) * opd;
        float3 param_15 = phi * float(order);
        result += ((cm * 2.0f) * cnaFilmSensitivity(param_14, param_15));
    }
    return max(result, 0.0f.xxx);
}

float cnaDistribution(float NoH, float roughness)
{
    float a = roughness * roughness;
    float aa = a * a;
    float d = ((NoH * NoH) * (aa - 1.0f)) + 1.0f;
    return aa / max((3.1415927410125732421875f * d) * d, 1.0000000116860974230803549289703e-07f);
}

float cnaGeometry(float NoV, float NoL, float roughness)
{
    float k = ((roughness + 1.0f) * (roughness + 1.0f)) / 8.0f;
    float gv = NoV / max((NoV * (1.0f - k)) + k, 1.0000000116860974230803549289703e-07f);
    float gl = NoL / max((NoL * (1.0f - k)) + k, 1.0000000116860974230803549289703e-07f);
    return gv * gl;
}

float cnaSheenDistribution(float NoH, float roughness)
{
    float alpha = max(roughness * roughness, 0.070000000298023223876953125f);
    float inverseAlpha = 1.0f / alpha;
    float sinSquared = max(1.0f - (NoH * NoH), 0.0078125f);
    return ((2.0f + inverseAlpha) * pow(sinSquared, inverseAlpha * 0.5f)) / 6.283185482025146484375f;
}

float cnaSheenVisibility(float NoV, float NoL)
{
    return 1.0f / max(4.0f * ((NoL + NoV) - (NoL * NoV)), 1.0000000116860974230803549289703e-07f);
}

float3 cnaShade(CnaClusteredLight light, float3 surface, float3 normal, float3 viewDirection, float3 baseColor, float metallic, float roughness, inout float3 diffuseOut)
{
    diffuseOut = 0.0f.xxx;
    float3 toLight = light.position - surface;
    float _distance = length(toLight);
    if ((_distance >= light.range) || (_distance <= 0.0f))
    {
        return 0.0f.xxx;
    }
    float3 L = toLight / _distance.xxx;
    float param = _distance;
    float param_1 = light.range;
    float attenuation = cnaFalloff(param, param_1);
    if (light.isSpot > 0.5f)
)CNA_HLSL",
    R"CNA_HLSL(    {
        float cosAngle = dot(-L, light.direction);
        attenuation *= clamp((cosAngle - light.cosOuter) / max(light.cosInner - light.cosOuter, 9.9999997473787516355514526367188e-05f), 0.0f, 1.0f);
    }
    if (attenuation <= 0.0f)
    {
        return 0.0f.xxx;
    }
    float3 halfSum = L + viewDirection;
    float3 _1513;
    if (dot(halfSum, halfSum) > 9.9999999392252902907785028219223e-09f)
    {
        _1513 = normalize(halfSum);
    }
    else
    {
        _1513 = normal;
    }
    float3 H = _1513;
    float rawNoL = dot(normal, L);
    float NoL = max(rawNoL, 0.0f);
    float subsurface = (uClusterVectors[13].x + uClusterVectors[13].y) + uClusterVectors[13].z;
    float wrappedNoL = NoL;
    if (subsurface > 0.0f)
    {
        float w = uClusterScalars[6];
        wrappedNoL = clamp((rawNoL + w) / ((1.0f + w) * (1.0f + w)), 0.0f, 1.0f);
    }
    float NoV = max(dot(normal, viewDirection), 9.9999997473787516355514526367188e-05f);
    float NoH = max(dot(normal, H), 0.0f);
    float VoH = max(dot(viewDirection, H), 0.0f);
    float backScatter = 0.0f;
    if (subsurface > 0.0f)
    {
        backScatter = pow(clamp(dot(viewDirection, -L), 0.0f, 1.0f), 4.0f);
    }
    if (((NoL <= 0.0f) && (wrappedNoL <= 0.0f)) && (backScatter <= 0.0f))
    {
        return 0.0f.xxx;
    }
    float3 f0 = lerp(0.039999999105930328369140625f.xxx, baseColor, metallic.xxx);
    float param_2 = VoH;
    float3 param_3 = f0;
    float3 fresnel = cnaFresnel(param_2, param_3);
    if (uClusterScalars[7] > 0.0f)
    {
        float param_4 = 1.0f;
        float param_5 = uClusterScalars[8];
        float param_6 = NoV;
        float param_7 = uClusterScalars[9];
        float3 param_8 = f0;
        float3 film = cnaThinFilmIridescence(param_4, param_5, param_6, param_7, param_8);
        fresnel = lerp(fresnel, film, uClusterScalars[7].xxx);
    }
    float param_9 = NoH;
    float param_10 = roughness;
    float param_11 = NoV;
    float param_12 = NoL;
    float param_13 = roughness;
    float3 specular = ((fresnel * cnaDistribution(param_9, param_10)) * cnaGeometry(param_11, param_12, param_13)) / max((4.0f * NoV) * NoL, 1.0000000116860974230803549289703e-07f).xxx;
    float3 diffuse = (((1.0f.xxx - fresnel) * (1.0f - metallic)) * baseColor) / 3.1415927410125732421875f.xxx;
    diffuseOut = ((diffuse * light.colour) * attenuation) * wrappedNoL;
    if (subsurface > 0.0f)
    {
        diffuseOut += (((uClusterVectors[13] * backScatter) * light.colour) * attenuation);
    }
    float3 layered = specular;
    if (((uClusterVectors[12].x + uClusterVectors[12].y) + uClusterVectors[12].z) > 0.0f)
    {
        float param_14 = NoH;
        float param_15 = uClusterScalars[5];
        float param_16 = NoV;
        float param_17 = NoL;
        layered += ((uClusterVectors[12] * cnaSheenDistribution(param_14, param_15)) * cnaSheenVisibility(param_16, param_17));
    }
    if (uClusterScalars[3] > 0.0f)
    {
        float ccRoughness = max(uClusterScalars[4], 0.039999999105930328369140625f);
        float ccFresnel = 0.039999999105930328369140625f + (0.959999978542327880859375f * pow(clamp(1.0f - VoH, 0.0f, 1.0f), 5.0f));
        float param_18 = NoH;
        float param_19 = ccRoughness;
        float param_20 = NoV;
        float param_21 = NoL;
        float param_22 = ccRoughness;
        float ccSpecular = ((ccFresnel * cnaDistribution(param_18, param_19)) * cnaGeometry(param_20, param_21, param_22)) / max((4.0f * NoV) * NoL, 1.0000000116860974230803549289703e-07f);
        layered = (layered * (1.0f - (uClusterScalars[3] * ccFresnel))) + (uClusterScalars[3] * ccSpecular).xxx;
    }
    return ((layered * light.colour) * attenuation) * NoL;
}

void frag_main()
{
    float3 normal = normalize(vWorldNormal);
    float3 viewDirection = normalize(uClusterVectors[9] - vWorldPosition);
    float2 ndc = (vClipPosition.xy / max(abs(vClipPosition.w), 9.9999999747524270787835121154785e-07f).xx) * sign(vClipPosition.w);
    float2 param = ndc;
    float param_1 = vViewDistance;
    int cluster = cnaClusterFromNdc(param, param_1);
    int param_2 = cluster;
    int count = cnaClusterLightCount(param_2);
    float3 ambient = uClusterVectors[11] * uClusterVectors[10];
    if (uClusterScalars[0] > 0.5f)
    {
        float3 param_3[72];
        param_3[0] = uClusterVectors[0];
        param_3[1] = uClusterVectors[1];
        param_3[2] = uClusterVectors[2];
        param_3[3] = uClusterVectors[3];
        param_3[4] = uClusterVectors[4];
        param_3[5] = uClusterVectors[5];
        param_3[6] = uClusterVectors[6];
        param_3[7] = uClusterVectors[7];
        param_3[8] = uClusterVectors[8];
        param_3[9] = uClusterVectors[9];
        param_3[10] = uClusterVectors[10];
        param_3[11] = uClusterVectors[11];
        param_3[12] = uClusterVectors[12];
        param_3[13] = uClusterVectors[13];
        param_3[14] = uClusterVectors[14];
        param_3[15] = uClusterVectors[15];
        param_3[16] = uClusterVectors[16];
        param_3[17] = uClusterVectors[17];
        param_3[18] = uClusterVectors[18];
        param_3[19] = uClusterVectors[19];
        param_3[20] = uClusterVectors[20];
        param_3[21] = uClusterVectors[21];
        param_3[22] = uClusterVectors[22];
        param_3[23] = uClusterVectors[23];
        param_3[24] = uClusterVectors[24];
        param_3[25] = uClusterVectors[25];
        param_3[26] = uClusterVectors[26];
        param_3[27] = uClusterVectors[27];
        param_3[28] = uClusterVectors[28];
        param_3[29] = uClusterVectors[29];
        param_3[30] = uClusterVectors[30];
        param_3[31] = uClusterVectors[31];
        param_3[32] = uClusterVectors[32];
        param_3[33] = uClusterVectors[33];
        param_3[34] = uClusterVectors[34];
        param_3[35] = uClusterVectors[35];
        param_3[36] = uClusterVectors[36];
        param_3[37] = uClusterVectors[37];
        param_3[38] = uClusterVectors[38];
        param_3[39] = uClusterVectors[39];
        param_3[40] = uClusterVectors[40];
        param_3[41] = uClusterVectors[41];
        param_3[42] = uClusterVectors[42];
        param_3[43] = uClusterVectors[43];
        param_3[44] = uClusterVectors[44];
        param_3[45] = uClusterVectors[45];
        param_3[46] = uClusterVectors[46];
        param_3[47] = uClusterVectors[47];
        param_3[48] = uClusterVectors[48];
        param_3[49] = uClusterVectors[49];
        param_3[50] = uClusterVectors[50];
        param_3[51] = uClusterVectors[51];
        param_3[52] = uClusterVectors[52];
        param_3[53] = uClusterVectors[53];
        param_3[54] = uClusterVectors[54];
        param_3[55] = uClusterVectors[55];
        param_3[56] = uClusterVectors[56];
        param_3[57] = uClusterVectors[57];
        param_3[58] = uClusterVectors[58];
        param_3[59] = uClusterVectors[59];
        param_3[60] = uClusterVectors[60];
        param_3[61] = uClusterVectors[61];
        param_3[62] = uClusterVectors[62];
        param_3[63] = uClusterVectors[63];
        param_3[64] = uClusterVectors[64];
        param_3[65] = uClusterVectors[65];
        param_3[66] = uClusterVectors[66];
        param_3[67] = uClusterVectors[67];
        param_3[68] = uClusterVectors[68];
        param_3[69] = uClusterVectors[69];
        param_3[70] = uClusterVectors[70];
        param_3[71] = uClusterVectors[71];
        float3 param_4 = normal;
        ambient = (cnaProbeIrradiance(param_3, param_4) * uClusterVectors[10]) / 3.1415927410125732421875f.xxx;
    }
    float3 diffuseSum = ambient;
    float3 param_5 = vWorldPosition;
    float3 param_6 = normal;
    float3 param_7 = viewDirection;
    float3 param_8 = uClusterVectors[10];
    float param_9 = uClusterScalars[1];
    float param_10 = uClusterScalars[2];
    diffuseSum += cnaAreaContribution(param_5, param_6, param_7, param_8, param_9, param_10);
    float3 otherSum = 0.0f.xxx;
    float3 param_21;
    for (int i = 0; i < 128; i++)
    {
        if (i >= count)
)CNA_HLSL",
    R"CNA_HLSL(        {
            break;
        }
        int param_11 = cluster;
        int param_12 = i;
        int param_13 = cnaClusterLightIndex(param_11, param_12);
        CnaClusteredLight light = cnaLoadLight(param_13);
        CnaClusteredLight param_14 = light;
        float3 param_15 = vWorldPosition;
        float3 param_16 = normal;
        float3 param_17 = viewDirection;
        float3 param_18 = uClusterVectors[10];
        float param_19 = uClusterScalars[1];
        float param_20 = uClusterScalars[2];
        float3 _2099 = cnaShade(param_14, param_15, param_16, param_17, param_18, param_19, param_20, param_21);
        float3 lightDiffuse = param_21;
        otherSum += _2099;
        diffuseSum += lightDiffuse;
    }
    if (uClusterScalars[10] > 0.0f)
    {
        float3 refracted = refract(-viewDirection, normal, 1.0f / max(uClusterScalars[11], 1.0f));
        float3 exitPoint = vWorldPosition + (refracted * uClusterScalars[12]);
        float4 exitClip = mul(float4(exitPoint, 1.0f), uClusterMatrices[3]);
        float2 uv = (((exitClip.xy / max(abs(exitClip.w), 9.9999997473787516355514526367188e-05f).xx) * sign(exitClip.w)) * 0.5f) + 0.5f.xx;
        float3 behind = uOpaqueFrame.Sample(_uOpaqueFrame_sampler, clamp(uv, 0.0f.xx, 1.0f.xx)).xyz;
        float3 absorbed = 1.0f.xxx;
        bool _2172 = uClusterScalars[13] > 0.0f;
        bool _2178;
        if (_2172)
        {
            _2178 = uClusterScalars[12] > 0.0f;
        }
        else
        {
            _2178 = _2172;
        }
        if (_2178)
        {
            float3 sigma = (-log(clamp(uClusterVectors[14], 9.9999997473787516355514526367188e-05f.xxx, 1.0f.xxx))) / uClusterScalars[13].xxx;
            absorbed = exp((-sigma) * uClusterScalars[12]);
        }
        diffuseSum = lerp(diffuseSum, behind * absorbed, uClusterScalars[10].xxx);
    }
    FragColor = float4(diffuseSum + otherSum, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    vWorldNormal = stage_input.vWorldNormal;
    vWorldPosition = stage_input.vWorldPosition;
    vClipPosition = stage_input.vClipPosition;
    vViewDistance = stage_input.vViewDistance;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
)CNA_HLSL",
}};
inline std::string FragmentSource() {
    std::string result;
    result.reserve(26222);
    for (const auto part : kFragmentParts) result.append(part);
    return result;
}
}
