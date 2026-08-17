#pragma once

// plan_dx.md Phase DIRECTX3 (DX-15-embed): wires hlsl_shaders.hpp's checked-in, compiler-verified DXBC
// byte arrays (DX-14-compile) into real ID3D11VertexShader/ID3D11PixelShader objects, one pair per
// DX-13-hlsl stock shader variant. Deliberately narrow scope: this only proves/exposes the
// DXBC -> D3D11-shader-object path. Full pipeline wiring (constant buffers, input layouts, draw
// calls) is Phase DIRECTX8/DX-32 -- this cache is what that later phase calls into, not a replacement
// for it.

#include <d3d11.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>

namespace CNA::Internal::Renderers::D3DCommon
{
    using Microsoft::WRL::ComPtr;

    /// Identifies one of the stock HLSL shader variants ported in DX-13-hlsl and compiled to DXBC
    /// in DX-14-compile (hlsl_shaders.hpp) -- originally 10, plus AlphaTestColored3d (DX-136).
    /// Mirrors the .hlsl source filenames.
    enum class D3DShaderVariant
    {
        Colored3d,
        Textured3d,
        ColoredTextured3d,
        LitTextured3d,
        AlphaTest3d,
        DualTexture3d,
        EnvMap3d,
        Skinned3d,
        Sprite2d,
        Instanced3d,
        /// plan_dx.md DX-136: alpha_test3d's stride-24 (VertexPositionColorTexture) sibling --
        /// gives AlphaTestEffect.VertexColorEnabled a real vertex-color attribute to multiply
        /// against, which plain AlphaTest3d (stride 20, Position+UV only) never carries.
        AlphaTestColored3d,
        /// plan_graphics.md Phase 80 (Task 1106/1107): real per-vertex-lit siblings of
        /// LitTextured3d/Skinned3d, selected when GpuDrawParams::preferPerPixelLighting is false
        /// (XNA's real default) -- identical Blinn-Phong math, evaluated in the vertex stage.
        LitTextured3dVertexLit,
        Skinned3dVertexLit,
        /// plan_cnj.md CNB-58 follow-up: PbrEffect's metallic-roughness BRDF (unskinned), HLSL
        /// port of EasyGLRenderer::EnsurePbrProgram(). Stride 48
        /// (VertexPositionNormalTangentTexture).
        Pbr3d,
        /// GLTF-386: Pbr3d with the canonical stride-60 TEXCOORD_1 suffix.
        Pbr3dDualUv,
        /// plan_cnj.md CNB-58 follow-up: SkinnedPbrEffect -- Pbr3d's own BRDF plus bone skinning,
        /// HLSL port of EasyGLRenderer::EnsurePbrSkinnedProgram(). Stride 68
        /// (VertexPositionNormalTangentTextureSkinned).
        PbrSkinned3d,
        /// GLTF-386: PbrSkinned3d with the canonical stride-76 TEXCOORD_1 suffix.
        PbrSkinned3dDualUv,
        /// plan_gltf.md GLTF-463: PbrSkinned3dDualUv with a packed COLOR_0 appended -- stride 80,
        /// the layout a skinned vertex-coloured metallic-roughness primitive imports to.
        PbrSkinned3dDualUvColor,
        /// plan_cnj.md CNB-67 follow-up: Skinned3d's own stride-56 sibling carrying a per-vertex
        /// Color attribute (SkinnedEffect::VertexColorEnabled), HLSL port of
        /// EasyGLRenderer::EnsureSkinnedProgram()'s vertex-color wiring.
        Skinned3dColored,
        /// plan_cnj.md CNB-67 follow-up: Skinned3dVertexLit's own stride-56 vertex-color sibling,
        /// HLSL port of EasyGLRenderer::EnsureSkinnedVertexLitProgram()'s vertex-color wiring.
        Skinned3dVertexLitColored,
    };

    /// Returns the compiled DXBC bytecode (pointer + length) for a variant's vertex shader stage.
    /// Exposed separately from CreateVertexShaderForVariant() because DirectX11_CreateInputLayout also
    /// needs the vertex shader's raw bytecode (its input signature), not just the shader object --
    /// Phase DIRECTX5's input-layout cache will call this directly.
    void GetVertexShaderBytecode(D3DShaderVariant variant, const uint8_t*& bytes, std::size_t& size);

    /// Returns the compiled DXBC bytecode (pointer + length) for a variant's pixel shader stage.
    void GetPixelShaderBytecode(D3DShaderVariant variant, const uint8_t*& bytes, std::size_t& size);

    /// Creates the ID3D11VertexShader for the given stock variant from its checked-in DXBC bytes.
    /// Returns a null ComPtr (does not throw) if CreateVertexShader fails -- callers check the
    /// returned ComPtr, matching this project's existing D3D11 renderer error-handling style for
    /// resource-creation calls.
    ComPtr<ID3D11VertexShader> CreateVertexShaderForVariant(ID3D11Device* device, D3DShaderVariant variant);

    /// Creates the ID3D11PixelShader for the given stock variant from its checked-in DXBC bytes.
    /// Returns a null ComPtr (does not throw) if CreatePixelShader fails.
    ComPtr<ID3D11PixelShader> CreatePixelShaderForVariant(ID3D11Device* device, D3DShaderVariant variant);
}
