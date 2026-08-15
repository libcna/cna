// SPDX-License-Identifier: MS-PL
// D3D9 skinned-vertex-color porting task: CNA's own CNAEXT "SkinnedVertexColor3D" shader -- a
// per-pixel-lit, textured, skinned mesh shader with an added vertex-color modulate, mirroring
// EasyGLRenderer.cpp's EnsureSkinnedProgram() (GLSL/GLES 3.00) wiring for the stride-56
// vertex format (VertexPositionNormalTextureSkinned + a trailing normalized ubyte4 Color, see
// D3D9VertexDeclarations.hpp's own `case 56` doc comment).
//
// Not a Microsoft Stock Effect: real XNA 4.0's SkinnedEffect has no vertex-color input at all
// (Structures.fxh's own VSInputNmTxWeights declares Position/Normal/TexCoord/Indices/Weights only)
// -- this project's own design rule (D3D9EffectDraw.cpp's header comment) is that the vendored
// SkinnedEffect.fx bytecode stays byte-identical to what Microsoft shipped, never modified to add
// a feature XNA never had. So vertex-color-on-skinned-mesh needs a wholly separate custom shader,
// same category as Instanced3D.hlsl (D3D9InstancedDraw.cpp's own header comment gives the general
// "why a separate shader, not a stock-effect variant" rationale).
//
// Targets vs_3_0/ps_3_0, same as Pbr3D.hlsl/PbrSkinned3D.hlsl. Originally attempted at the lower,
// more broadly-compatible vs_2_0/ps_2_0 tier (matching Instanced3D.hlsl's own precedent of keeping
// a simple custom shader at the lowest shader model that fits, and the real, vendored
// SkinnedEffect.fx's own PSSkinnedPixelLighting pixel shader -- same order of complexity --
// compiling fine at ps_2_0). The vertex shader DOES compile at vs_2_0, but the pixel shader does
// not: compiling this file's PSSkinnedVertexColor3D at /T ps_2_0 against the real d3dcompiler_47.dll
// fails with "error X5608: Compiled shader code uses too many arithmetic instruction slots (81).
// Max. allowed by the target (ps_2_0) is 64." -- explicit per-light half-vector specular (3 lights,
// each its own normalize/dot/pow) plus the vertex-color gate pushes this over ps_2_0's budget even
// without any BRDF or texture-map-count increase. Moved BOTH stages to vs_3_0/ps_3_0 rather than
// mixing shader-model tiers within one draw call (a vs_2_0+ps_3_0 pairing would be legal but buys
// nothing: any device capable of ps_3_0 is already capable of vs_3_0). See this task's own final
// report for the exact compiler output that proved this, not an assumption.
//
// Bone skinning mirrors the real, vendored SkinnedEffect.fx's own Skin() function exactly -- see
// PbrSkinned3D.hlsl's own header comment for the shared citation (float4x3 Bones[72], the
// "(float3x3)skinning" rotation-only cast, BLENDINDICES0/BLENDWEIGHT0 accumulation).
//
// Deviation from EnsureSkinnedProgram()'s own GLSL (documented, not silent): that function's vertex
// shader transforms the skinned Normal by `mat3(skinMat)` only, with NO further `mat3(uWorld)`
// step -- meaning a non-identity-ROTATION World matrix would light an EnsureSkinnedProgram() mesh
// using its BONE-LOCAL normal orientation, not its true world-space orientation. This shader
// instead applies (float3x3)World after the skin-local rotation (`skinnedNormal` below), matching:
//   (a) the real, Microsoft-authored SkinnedEffect.fx's own proven-correct convention (Skin()
//       leaves the normal in skin-local space; Lighting.fxh's ComputeCommonVSOutputWithLighting()/
//       ComputeCommonVSOutputPixelLighting() then apply WorldInverseTranspose to it), and
//   (b) EnsurePbrSkinnedProgram()'s OWN identical extra `mat3(uWorld)` step for ITS normal
//       (`vNormal=normalize(mat3(uWorld)*(skinNormalMat*aNormal));`) -- i.e. EnsureSkinnedProgram()
//       looks like the outlier between EasyGL's own two skinned-lighting functions, not the
//       intentional convention.
// REMED-GFX-006: applies the World INVERSE-TRANSPOSE (computed in-shader via InverseTranspose3x3)
//       after the skin-local rotation, not raw (float3x3)World. This is correct under non-uniform
//       World scale as well as rotation, matching SkinnedEffect.fx's own WorldInverseTranspose and
//       the corrected cross-renderer skinned shaders (Vulkan/EasyGL/Bgfx/GPU). EasyGL itself is
//       out of scope for this task and was not touched.
//
// Vertex declaration (stride 56, D3D9VertexDeclarations.hpp): POSITION0 (FLOAT3, 0), NORMAL0
// (FLOAT3, 12), TEXCOORD0 (FLOAT2, 24), BLENDWEIGHT0 (FLOAT4, 32), BLENDINDICES0 (UBYTE4, 48),
// COLOR0 (UBYTE4N, 52).

float4x4 WorldViewProj : register(c0); // c0-c3
float4x4 World          : register(c4); // c4-c7
float4   FogParams      : register(c8); // REMED-GFX-010: only .w = WeightsPerVertex now (xyz unused)
float4x3 Bones[72]      : register(c9); // c9..c224 (216 registers)
float4   FogVector      : register(c225); // REMED-GFX-010: FNA view-space fog vector (SetFogVector)

struct VSInput
{
    float3 Position    : POSITION0;
    float3 Normal      : NORMAL0;
    float2 UV          : TEXCOORD0;
    float4 BoneWeights : BLENDWEIGHT0;
    int4   BoneIndices : BLENDINDICES0;
    float4 Color       : COLOR0;
};

struct VSOutput
{
    float4 Position  : SV_Position;
    float3 Normal    : TEXCOORD0;
    float2 UV        : TEXCOORD1;
    float3 WorldPos  : TEXCOORD2;
    float  FogFactor : TEXCOORD3;
    float4 Color     : COLOR0;
};

// REMED-GFX-006: transpose(inverse(m)) of a 3x3 (cofactor matrix over its determinant). HLSL has
// no built-in inverse(); vs_3_0 has ample instruction budget. Matches the corrected cross-renderer
// skinned normal transform.
float3x3 InverseTranspose3x3(float3x3 m)
{
    float3 c0 = cross(m[1], m[2]);
    float3 c1 = cross(m[2], m[0]);
    float3 c2 = cross(m[0], m[1]);
    float det = dot(m[0], c0);
    return float3x3(c0, c1, c2) / det;
}

VSOutput VSSkinnedVertexColor3D(VSInput vin)
{
    VSOutput vout;

    float weightsPerVertex = FogParams.w;
    float4x3 skinning = Bones[vin.BoneIndices.x] * vin.BoneWeights.x;
    if (weightsPerVertex >= 2.0)
        skinning += Bones[vin.BoneIndices.y] * vin.BoneWeights.y;
    if (weightsPerVertex >= 4.0)
        skinning += Bones[vin.BoneIndices.z] * vin.BoneWeights.z
                   + Bones[vin.BoneIndices.w] * vin.BoneWeights.w;

    float3 skinnedPos = mul(float4(vin.Position, 1.0), skinning);
    float3 skinnedNormal = mul(vin.Normal, (float3x3)skinning);

    vout.Position = mul(float4(skinnedPos, 1.0), WorldViewProj);
    // REMED-GFX-006: World inverse-transpose (not raw World) -- see header Deviation note.
    vout.Normal = normalize(mul(skinnedNormal, InverseTranspose3x3((float3x3)World)));
    vout.UV = vin.UV;
    vout.WorldPos = mul(float4(skinnedPos, 1.0), World).xyz;
    vout.Color = vin.Color;
    // REMED-GFX-010: FNA view-space fog on the POST-skin position (FNA Skin() mutates vin.Position
    // before ComputeFogFactor). keep = 1 - saturate(dot(skinPos, fogVector)); zero vector = no fog.
    vout.FogFactor = 1.0 - saturate(dot(float4(skinnedPos, 1.0), FogVector));

    return vout;
}

sampler2D Texture : register(s0);

float4 DiffuseColor              : register(c0);
float3 EmissiveColor              : register(c1); // pre-folded ambient+emissive, matches
                                                    // SkinnedEffect::FillGpuDrawParams()'s own convention
float3 Light0Dir                  : register(c2);
float3 Light0Diffuse              : register(c3);
float3 Light1Dir                  : register(c4);
float3 Light1Diffuse              : register(c5);
float3 Light2Dir                  : register(c6);
float3 Light2Diffuse              : register(c7);
float3 Light0Specular             : register(c8);
float3 Light1Specular             : register(c9);
float3 Light2Specular             : register(c10);
float3 SpecularColor              : register(c11);
float4 SpecularPowerPad           : register(c12); // x=SpecularPower
float3 EyePosition                : register(c13);
float4 AlphaTest                  : register(c14);
float3 FogColor                   : register(c15);
float4 VertexColorEnabledPad      : register(c16); // x=VertexColorEnabled

struct PSInput
{
    float3 Normal    : TEXCOORD0;
    float2 UV        : TEXCOORD1;
    float3 WorldPos  : TEXCOORD2;
    float  FogFactor : TEXCOORD3;
    float4 Color     : COLOR0;
};

float4 PSSkinnedVertexColor3D(PSInput pin) : SV_Target0
{
    float3 N = normalize(pin.Normal);
    float3 E = normalize(EyePosition - pin.WorldPos);

    float dotL0 = dot(N, -normalize(Light0Dir));
    float zeroL0 = step(0.0, dotL0);
    float NdotL0 = max(dotL0, 0.0);
    float dotL1 = dot(N, -normalize(Light1Dir));
    float zeroL1 = step(0.0, dotL1);
    float NdotL1 = max(dotL1, 0.0);
    float dotL2 = dot(N, -normalize(Light2Dir));
    float zeroL2 = step(0.0, dotL2);
    float NdotL2 = max(dotL2, 0.0);

    float3 lightSum = Light0Diffuse * NdotL0 + Light1Diffuse * NdotL1 + Light2Diffuse * NdotL2;
    // REMED-GFX-008: FNA composes lit = lightSum*DiffuseColor + EmissiveColor -- emissive is added
    // OUTSIDE the diffuse multiply (EmissiveColor already pre-folds (emissive + ambient*diffuse)*alpha
    // at the C++ level). The prior (EmissiveColor + lightSum)*DiffuseColor wrongly scaled the folded
    // emissive/ambient by DiffuseColor again.
    float3 litRGB = lightSum * DiffuseColor.rgb + EmissiveColor;

    float3 h0 = normalize(E - normalize(Light0Dir));
    float spec0 = pow(max(dot(h0, N), 0.0) * zeroL0, SpecularPowerPad.x);
    float3 h1 = normalize(E - normalize(Light1Dir));
    float spec1 = pow(max(dot(h1, N), 0.0) * zeroL1, SpecularPowerPad.x);
    float3 h2 = normalize(E - normalize(Light2Dir));
    float spec2 = pow(max(dot(h2, N), 0.0) * zeroL2, SpecularPowerPad.x);
    float3 specularRGB = (spec0 * Light0Specular + spec1 * Light1Specular + spec2 * Light2Specular)
                        * SpecularColor;

    float4 texColor = tex2D(Texture, pin.UV);
    float4 vc = (VertexColorEnabledPad.x > 0.5) ? pin.Color : float4(1.0, 1.0, 1.0, 1.0);

    float4 outColor = float4(litRGB * texColor.rgb, DiffuseColor.a * texColor.a * vc.a);
    outColor.rgb += specularRGB * outColor.a;
    // Vertex color modulates the whole combined diffuse+specular output, not just diffuse --
    // applied AFTER the specular add, matching EnsureSkinnedProgram()'s own established fix
    // (a black VertexColorEnabled=true color must genuinely zero the pixel, including any specular
    // highlight, not just the base diffuse term -- a specular highlight added after a
    // diffuse-only multiply would otherwise leak through unmodulated).
    outColor.rgb *= vc.rgb;

    float alphaTestResult = (AlphaTest.y > 0.0)
        ? ((abs(outColor.a - AlphaTest.x) < AlphaTest.y) ? AlphaTest.z : AlphaTest.w)
        : ((outColor.a < AlphaTest.x) ? AlphaTest.z : AlphaTest.w);
    clip(alphaTestResult);

    outColor.rgb = lerp(FogColor, outColor.rgb, pin.FogFactor);
    return outColor;
}
