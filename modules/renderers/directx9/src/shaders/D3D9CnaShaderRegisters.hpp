// SPDX-License-Identifier: MS-PL
// D3D9 PBR + skinned-vertex-color porting task: the real, compiler-verified constant register
// layout for CNA's own 3 new CNAEXT shaders (Pbr3D, PbrSkinned3D, SkinnedVertexColor3D -- see
// shaders/cna/*.hlsl's own header comments), extracted the same way D9-72's own
// extract_shader_registers.py extracts the vendored XNA Stock Effects' tables: from a real
// D3DDisassemble()'s own "// Registers:" comment block, NOT hand-derived from each .hlsl file's own
// declared HLSL types.
//
// This mattered here too, not just for the Stock Effects: an initial hand-written draw-dispatch
// pass assumed `World` (declared `float4x4`) would occupy 4 constant registers per shader. The real
// compiler allocates it only 3 (matching D9-72's own documented EnvironmentMapEffect.fx finding --
// `World`'s 4th column solely feeds an output component this task's own HLSL never reads, e.g.
// `mul(float4(...), World).xyz` discards `.w`) -- and the register the compiler DIDN'T give to
// `World` is not simply unused, it is reassigned to the compiler's OWN internal literal constant
// (`def c7, ...` in Pbr3D's own vs_3_0 disassembly). Uploading a naive, hand-assumed 4th `World`
// register would silently corrupt that literal and break the shader. Reusing
// `D3D9ConstantUpload.cpp`'s existing, proven `TryUpload{Vertex,Pixel}ShaderConstantEXT()` helpers
// against THIS file's real register/count table (the same pattern D3D9EffectDraw.cpp already uses
// for every Stock Effect) avoids that class of bug entirely -- the very reason this table exists
// instead of a hand-rolled `SetVertexShaderConstantF(4, ..., 4)` call.
//
// Regenerate after any change to shaders/cna/Pbr3D.hlsl, shaders/cna/PbrSkinned3D.hlsl, or
// shaders/cna/SkinnedVertexColor3D.hlsl: compile each entry point (see d3d9_pbr_shaders.hpp /
// d3d9_skinned_vertex_color_shader.hpp's own header comments for the exact fxc_tool.exe
// invocations), run disasm_tool.exe over the resulting .bin, and transcribe each entry point's own
// "// Registers:" table verbatim (see extract_shader_registers.py for the exact parsing this file's
// content was produced by).
#pragma once
#include <cstdint>

// D3D9ShaderConstantSlot is declared in shaders/D3D9ShaderRegisters.hpp (D9-72's own auto-generated
// header) -- reused verbatim rather than re-declaring an identical struct here.
#include "CNA/Internal/Renderers/DirectX9/shaders/D3D9ShaderRegisters.hpp"

namespace CNA::Internal::Renderers::DirectX9::Shaders
{

static constexpr D3D9ShaderConstantSlot kPbr3DVS_Registers[] = {
    {"WorldViewProj", 'v', 0, 4},
    {"World", 'v', 4, 3},
    {"NormalMatrix", 'v', 8, 3},
    {"FogParams", 'v', 11, 1}
};
static constexpr int kPbr3DVS_RegistersCount = 4;

static constexpr D3D9ShaderConstantSlot kPbr3DPS_Registers[] = {
    {"DiffuseColor", 'p', 0, 1},
    {"AmbientColor", 'p', 1, 1},
    {"EmissiveColor", 'p', 2, 1},
    {"MetallicRoughnessFactor", 'p', 3, 1},
    {"Light0Dir", 'p', 4, 1},
    {"Light0Diffuse", 'p', 5, 1},
    {"Light1Dir", 'p', 6, 1},
    {"Light1Diffuse", 'p', 7, 1},
    {"Light2Dir", 'p', 8, 1},
    {"Light2Diffuse", 'p', 9, 1},
    {"EyePosition", 'p', 10, 1},
    {"AlphaTest", 'p', 11, 1},
    {"FogColor", 'p', 12, 1},
    {"TextureTransformRows", 'p', 14, 10},
    {"SpecularFresnelInputs", 'p', 24, 1},
    {"SpecularMapFlags", 'p', 25, 1},
    {"SpecularTextureTransformRows", 'p', 26, 4}
};
static constexpr int kPbr3DPS_RegistersCount = 17;

static constexpr D3D9ShaderConstantSlot kPbrSkinned3DVS_Registers[] = {
    {"WorldViewProj", 'v', 0, 4},
    {"World", 'v', 4, 3},
    {"FogParams", 'v', 8, 1},
    {"Bones", 'v', 9, 216},
    {"FogVector", 'v', 225, 1}   // REMED-GFX-010: D3DDisassemble-verified (c225, after Bones c9..c224)
};
static constexpr int kPbrSkinned3DVS_RegistersCount = 5;

// Identical constant set/registers to kPbr3DPS_Registers (PbrSkinned3D.hlsl's own pixel shader is
// a straight copy of Pbr3D.hlsl's) -- kept as its own named table anyway, matching this file's own
// "transcribed verbatim per entry point" convention rather than aliasing.
static constexpr D3D9ShaderConstantSlot kPbrSkinned3DPS_Registers[] = {
    {"DiffuseColor", 'p', 0, 1},
    {"AmbientColor", 'p', 1, 1},
    {"EmissiveColor", 'p', 2, 1},
    {"MetallicRoughnessFactor", 'p', 3, 1},
    {"Light0Dir", 'p', 4, 1},
    {"Light0Diffuse", 'p', 5, 1},
    {"Light1Dir", 'p', 6, 1},
    {"Light1Diffuse", 'p', 7, 1},
    {"Light2Dir", 'p', 8, 1},
    {"Light2Diffuse", 'p', 9, 1},
    {"EyePosition", 'p', 10, 1},
    {"AlphaTest", 'p', 11, 1},
    {"FogColor", 'p', 12, 1},
    {"TextureTransformRows", 'p', 14, 10},
    {"SpecularFresnelInputs", 'p', 24, 1},
    {"SpecularMapFlags", 'p', 25, 1},
    {"SpecularTextureTransformRows", 'p', 26, 4}
};
static constexpr int kPbrSkinned3DPS_RegistersCount = 17;

static constexpr D3D9ShaderConstantSlot kSkinnedVertexColor3DVS_Registers[] = {
    {"WorldViewProj", 'v', 0, 4},
    {"World", 'v', 4, 3},
    {"FogParams", 'v', 8, 1},
    {"Bones", 'v', 9, 216},
    {"FogVector", 'v', 225, 1}   // REMED-GFX-010: D3DDisassemble-verified (c225, after Bones c9..c224)
};
static constexpr int kSkinnedVertexColor3DVS_RegistersCount = 5;

static constexpr D3D9ShaderConstantSlot kSkinnedVertexColor3DPS_Registers[] = {
    {"DiffuseColor", 'p', 0, 1},
    {"EmissiveColor", 'p', 1, 1},
    {"Light0Dir", 'p', 2, 1},
    {"Light0Diffuse", 'p', 3, 1},
    {"Light1Dir", 'p', 4, 1},
    {"Light1Diffuse", 'p', 5, 1},
    {"Light2Dir", 'p', 6, 1},
    {"Light2Diffuse", 'p', 7, 1},
    {"Light0Specular", 'p', 8, 1},
    {"Light1Specular", 'p', 9, 1},
    {"Light2Specular", 'p', 10, 1},
    {"SpecularColor", 'p', 11, 1},
    {"SpecularPowerPad", 'p', 12, 1},
    {"EyePosition", 'p', 13, 1},
    {"AlphaTest", 'p', 14, 1},
    {"FogColor", 'p', 15, 1},
    {"VertexColorEnabledPad", 'p', 16, 1}
};
static constexpr int kSkinnedVertexColor3DPS_RegistersCount = 17;

} // namespace CNA::Internal::Renderers::DirectX9::Shaders
