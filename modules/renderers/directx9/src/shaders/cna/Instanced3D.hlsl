// SPDX-License-Identifier: MS-PL
// plans/plan_dx9.md Phase D9-8 (D9-83): CNA's own minimal hardware-instancing shader (CNAEXT) -- vs_2_0/
// ps_2_0, mirrors D3DCommon/shaders/instanced3d.vert.hlsl's/instanced3d.frag.hlsl's exact scope
// (Position + a per-instance world matrix, flat DiffuseColor output, no lighting/texture/fog).
//
// This is NOT a Microsoft Stock Effect port -- real XNA 4.0's own BasicEffect/AlphaTestEffect/
// DualTextureEffect/EnvironmentMapEffect/SkinnedEffect have no per-instance-aware vertex shader at
// all (their real VSInput* structs, transcribed verbatim from Structures.fxh across D9-82b-f,
// declare no per-instance semantic). Real XNA hardware instancing requires the game author's own
// custom Effect with a custom vertex shader that reads the per-instance stream -- there is no
// "instanced BasicEffect" in real XNA. This shader is CNA's own opinionated minimal stand-in for
// that custom-Effect role, matching every other renderer's own identical choice (D3D11/Vulkan/Bgfx
// all authored their own equivalent "instanced3d" shader rather than attempting to instance a
// stock effect).
//
// Register layout is fully self-chosen (unlike D9-72's Stock Effect register table, which had to
// be extracted from the real compiler's own disassembly because Microsoft's original source
// dictated it) -- verified against a real disassembly anyway, matching this project's own "verify,
// don't assume" discipline, not because the layout was ever in doubt.
//
// Vertex declaration (2 streams, NOT one of D3D9VertexDeclarations.hpp's single-stream stride-keyed
// layouts -- see DirectX9Renderer::GetOrCreateInstancedVertexDeclarationEXT()):
//   Stream 0 (per-vertex, step rate 1):      POSITION0 (FLOAT3, offset 0)
//   Stream 1 (per-instance, step rate 1):    TEXCOORD1..4 (FLOAT4 each, offsets 0/16/32/48,
//                                             stride 64) -- one float4 "row" per semantic, matching
//                                             the 64-byte-per-instance world-matrix convention
//                                             DirectX11Renderer::DrawInstancedPrimitivesEx()
//                                             already established for GpuDrawParams::instanceVb.

float4 ViewProj0 : register(c0);
float4 ViewProj1 : register(c1);
float4 ViewProj2 : register(c2);
float4 ViewProj3 : register(c3);
float4 DiffuseColor : register(c4);

struct VSInput
{
    float3 Position : POSITION0;
    float4 InstRow0  : TEXCOORD1;
    float4 InstRow1  : TEXCOORD2;
    float4 InstRow2  : TEXCOORD3;
    float4 InstRow3  : TEXCOORD4;
};

struct VSOutput
{
    float4 Position : POSITION0;
    float4 Color    : COLOR0;
};

VSOutput VSInstanced3D(VSInput vin)
{
    VSOutput vout;

    float4x4 world = float4x4(vin.InstRow0, vin.InstRow1, vin.InstRow2, vin.InstRow3);
    float4 worldPos = mul(float4(vin.Position, 1.0), world);

    float4x4 viewProj = float4x4(ViewProj0, ViewProj1, ViewProj2, ViewProj3);
    vout.Position = mul(worldPos, viewProj);
    vout.Color = DiffuseColor;

    return vout;
}

struct PSInput
{
    float4 Color : COLOR0;
};

float4 PSInstanced3D(PSInput pin) : COLOR0
{
    return pin.Color;
}
