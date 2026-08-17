// plan_dx9.md Phase D9-8 (D9-82b/c/d/e/f): real effect-aware DrawPrimitivesEx/
// DrawIndexedPrimitivesEx dispatch, ALL 5 XNA Stock Effects: BasicEffect (D9-82b), AlphaTestEffect
// (D9-82c), DualTextureEffect (D9-82d), EnvironmentMapEffect (D9-82e), SkinnedEffect (D9-82f) --
// matching the priority-cascade shape DirectX11Renderer::DrawPrimitivesExImpl already
// established (flag-driven effect selection, GpuDrawParams as the only per-draw input).
//
// BasicEffect coverage is bounded by which vertex layout this renderer actually supports (D9-22's
// 5 stride-keyed D3DVERTEXELEMENT9 declarations: 16/20/24/32/52 bytes) -- BasicEffect's 32
// ShaderIndex values need a VSInput shape (VSInput/VSInputVc/VSInputTx/VSInputTxVc/VSInputNm/
// VSInputNmVc/VSInputNmTx/VSInputNmTxVc, per Structures.fxh) that not every one of those 5 strides
// can supply:
//   - VSInput (Position only, 12 bytes)              -- no matching CNA vertex layout at all.
//   - VSInputNm (Position+Normal, 24 bytes)           -- collides with the existing 24-byte
//     Position+Color+TexCoord layout (same byte count, different semantics/offsets) -- no
//     distinct declaration exists for it.
//   - VSInputNmVc (Position+Normal+Color, 28 bytes)   -- no 28-byte layout exists.
//   - VSInputNmTxVc (+TexCoord, 36 bytes)             -- no 36-byte layout exists.
// Only 4 of BasicEffect's VSInput shapes ARE satisfiable by an existing stride, each mapping to a
// specific fog/vertex-color/texture/lighting combination (the "natural" one that uses every field
// the layout declares):
//   stride 16 (Position+Color)            -> vertexColorEnabled, no texture, no lighting.
//   stride 20 (Position+TexCoord)         -> textureEnabled, no vertex color, no lighting.
//   stride 24 (Position+Color+TexCoord)   -> vertexColorEnabled + textureEnabled, no lighting.
//   stride 32 (Position+Normal+TexCoord)  -> lightingEnabled + textureEnabled, no vertex color.
// That is 10 of BasicEffect's 32 ShaderIndex values via stride 16/20/24 (2/3/4/5/6/7) plus stride
// 32's vertex-lit/one-light textured buckets (12/13/20/21) -- every OTHER combination whose
// VSInput shape has no matching stride still throws a named "no matching CNA vertex layout" error
// rather than silently drawing with the wrong stride. This is an honest, reported gap (matches
// D3D11's own "dual_texture_colored3d was not ported" precedent), not a corner cut in secret.
// Stride 32's textured+lit bucket ALSO supplies BasicEffect's pixel-lighting-textured ShaderIndex
// values (28/29, VSBasicPixelLightingTx also declares a VSInputNmTx parameter, D9-81 item 1,
// resolved 2026-07-16 below) -- 12 of 32 ShaderIndex values are drawable in total, not 10; the
// untextured pixel-lighting bucket (24/25, VSBasicPixelLighting takes plain VSInput, Position
// only) remains blocked by the same missing-vertex-layout gap as the untextured vertex-lit bucket
// (8/9) above, unrelated to PreferPerPixelLighting itself.
//
// PreferPerPixelLighting (plan_dx9.md D9-81, item 1) and EnvironmentMapEffect's specularEnabled
// (item 4) are RESOLVED 2026-07-16: GpuDrawParams now carries both real fields (see
// IGraphicsRenderer.hpp), forwarded by BasicEffect/SkinnedEffect/EnvironmentMapEffect's own
// FillGpuDrawParams(). This file's dispatch functions read them directly instead of hardcoding
// false -- see each DrawXxxEffectEXT()'s own ComputeXxxShaderIndex() call, and each
// GetXxxRegisterTablesEXT()'s newly-added cases for the previously-unreachable ShaderIndex values.

#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9Buffers.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9ConstantUpload.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9ShaderCache.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9ShaderDispatch.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9Textures.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9VertexDeclarations.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9RenderTargets.hpp"
#include "CNA/Internal/Renderers/DirectX9/shaders/D3D9ShaderRegisters.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::DirectX9
{
    using Microsoft::Xna::Framework::Matrix;

    namespace
    {
        D3DPRIMITIVETYPE ToD3D9Topology(PrimitiveType pt)
        {
            switch (pt)
            {
            case PrimitiveType::TriangleList:  return D3DPT_TRIANGLELIST;
            case PrimitiveType::TriangleStrip: return D3DPT_TRIANGLESTRIP;
            case PrimitiveType::LineList:      return D3DPT_LINELIST;
            case PrimitiveType::LineStrip:     return D3DPT_LINESTRIP;
            case PrimitiveType::PointListEXT:  return D3DPT_POINTLIST;
            }
            throw std::runtime_error(
                "DirectX9 stock-effect draw does not support the requested PrimitiveType value");
        }

        /// Real root cause of the NEXT.md-documented "RenderTarget2D sampled as an ordinary
        /// texture crashes on the next draw" bug: GpuDrawParams::texture0/texture1 are declared as
        /// `const ITextureRenderer*`, and D3D9RenderTargetRenderer (a RenderTarget2D's own renderer)
        /// implements IRenderTargetRenderer : ITextureRenderer -- a real, legal runtime type this
        /// pointer can hold whenever an XNA game sets `effect.Texture = someRenderTarget2D`. Every
        /// call site below used to do `static_cast<const D3D9TextureRenderer*>(params.texture0)`
        /// unconditionally, which silently reinterprets a D3D9RenderTargetRenderer* as if it were a
        /// D3D9TextureRenderer* (an unrelated sibling class, not a base/derived relationship) --
        /// undefined behavior that read whichever field happens to sit at D3D9TextureRenderer's own
        /// `texture_` offset in a D3D9RenderTargetRenderer's actual, different layout, handed a
        /// garbage IDirect3DTexture9* to SetTexture(), and crashed later when DXVK's async
        /// shader-compiler thread actually tried to use it. Same two-concrete-type resolution as
        /// DirectX11Renderer.cpp's own (anonymous-namespace-local, not exported)
        /// GetSrvForTextureEXT -- duplicated per-file rather than factored into a shared header,
        /// matching that precedent's own documented rationale.
        IDirect3DTexture9* ResolveD3D9TextureEXT(const ITextureRenderer* tex)
        {
            if (tex == nullptr) return nullptr;
            if (const auto* t = dynamic_cast<const D3D9TextureRenderer*>(tex))
                return t->GetTextureEXT();
            if (const auto* rt = dynamic_cast<const D3D9RenderTargetRenderer*>(tex))
                return rt->GetTextureEXT();
            return nullptr;
        }

        /// Same two-concrete-type resolution as ResolveD3D9TextureEXT above, but for
        /// ITextureCubeRenderer (EnvironmentMapEffect's cube map) -- a plain D3D9TextureCubeRenderer,
        /// or a D3D9RenderTargetCubeRenderer used as a sampled cube texture.
        IDirect3DCubeTexture9* ResolveD3D9TextureCubeEXT(const ITextureCubeRenderer* tex)
        {
            if (tex == nullptr) return nullptr;
            if (const auto* t = dynamic_cast<const D3D9TextureCubeRenderer*>(tex))
                return t->GetTextureEXT();
            if (const auto* rt = dynamic_cast<const D3D9RenderTargetCubeRenderer*>(tex))
                return rt->GetTextureEXT();
            return nullptr;
        }

        /// D9-72/D9-82: EffectParameter.SetValue(Matrix)'s real XNA upload convention -- register
        /// k receives COLUMN k of the row-major matrix, i.e. the TRANSPOSE of
        /// Matrix::ToColumnMajor()'s own row-major-flat reading order (same derivation D9-82's
        /// simple DrawColoredPrimitives path already uses and verified live). Always computes the
        /// full 4-register (16-float) form; the caller's own registerCount (from the real,
        /// compiler-verified D9-72 table) determines how many of those registers actually get
        /// uploaded -- correct for BOTH a float4x4 missing its trailing (unused) register AND a
        /// float3x3-declared constant (WorldInverseTranspose), since a normal affine matrix's
        /// transpose always has row 4 = (0,0,0,1), making the two packings numerically identical
        /// for the registers that DO get uploaded (verified against FNA's own EffectParameter
        /// packing branches).
        void UploadMatrixConstantVS(IDirect3DDevice9* device, const Shaders::D3D9ShaderConstantSlot* table,
                                    int count, const char* name, const Matrix& m)
        {
            const Matrix transposed = Matrix::Transpose(m);
            float regs[16];
            transposed.ToColumnMajor(regs);
            TryUploadVertexShaderConstantEXT(device, table, count, name, regs);
        }

        /// GpuDrawParams stores 3-component colors/directions as `float[3]`, but every D3D9
        /// constant register is a full 4-float slot -- pads with a trailing 0 rather than reading
        /// past the end of a 3-float array.
        struct Vec4Pad { float v[4]; };
        Vec4Pad Pad3(const float v3[3]) { return Vec4Pad{{v3[0], v3[1], v3[2], 0.0f}}; }

        /// D9-81 (corrected during D9-82b -- see that row's own updated note): a light with BOTH
        /// diffuse and specular still (0,0,0) contributes exactly zero to Lighting.fxh's
        /// ComputeLights() regardless of whether that zero came from Enabled=false or from an
        /// Enabled=true light with literal black colors -- a linear multiply by an all-zero color
        /// is zero either way. So this derivation is provably lossless for shader-VARIANT
        /// selection, computed entirely from GpuDrawParams' own existing fields -- shared by every
        /// effect with a real one-light optimization bucket (BasicEffect, EnvironmentMapEffect,
        /// SkinnedEffect).
        bool ComputeOneLightEXT(const GpuDrawParams& params)
        {
            const bool light1Zero = params.light1Diffuse[0] == 0.0f && params.light1Diffuse[1] == 0.0f &&
                                    params.light1Diffuse[2] == 0.0f && params.light1Specular[0] == 0.0f &&
                                    params.light1Specular[1] == 0.0f && params.light1Specular[2] == 0.0f;
            const bool light2Zero = params.light2Diffuse[0] == 0.0f && params.light2Diffuse[1] == 0.0f &&
                                    params.light2Diffuse[2] == 0.0f && params.light2Specular[0] == 0.0f &&
                                    params.light2Specular[1] == 0.0f && params.light2Specular[2] == 0.0f;
            return light1Zero && light2Zero;
        }

        struct BasicEffectRegisterTables
        {
            const Shaders::D3D9ShaderConstantSlot* vs; int vsCount;
            const Shaders::D3D9ShaderConstantSlot* ps; int psCount;
        };

        /// D9-82b: the register table pair for each of the 10 ShaderIndex values this renderer can
        /// currently draw (see this file's own header comment for why only these 10). Selected by
        /// C++ symbol, not a re-typed string lookup -- a mismatch between the chosen shader and
        /// its constants would be a compile error, not a runtime one.
        BasicEffectRegisterTables GetBasicEffectRegisterTablesEXT(int shaderIndex)
        {
            using namespace Shaders;
            switch (shaderIndex)
            {
            case 2:  return {kBasicEffect_VSBasicVc_Registers,
                             static_cast<int>(std::size(kBasicEffect_VSBasicVc_Registers)),
                             kBasicEffect_PSBasic_Registers,
                             static_cast<int>(std::size(kBasicEffect_PSBasic_Registers))};
            case 3:  return {kBasicEffect_VSBasicVcNoFog_Registers,
                             static_cast<int>(std::size(kBasicEffect_VSBasicVcNoFog_Registers)),
                             nullptr, 0};
            case 4:  return {kBasicEffect_VSBasicTx_Registers,
                             static_cast<int>(std::size(kBasicEffect_VSBasicTx_Registers)),
                             kBasicEffect_PSBasicTx_Registers,
                             static_cast<int>(std::size(kBasicEffect_PSBasicTx_Registers))};
            case 5:  return {kBasicEffect_VSBasicTxNoFog_Registers,
                             static_cast<int>(std::size(kBasicEffect_VSBasicTxNoFog_Registers)),
                             nullptr, 0};
            case 6:  return {kBasicEffect_VSBasicTxVc_Registers,
                             static_cast<int>(std::size(kBasicEffect_VSBasicTxVc_Registers)),
                             kBasicEffect_PSBasicTx_Registers,
                             static_cast<int>(std::size(kBasicEffect_PSBasicTx_Registers))};
            case 7:  return {kBasicEffect_VSBasicTxVcNoFog_Registers,
                             static_cast<int>(std::size(kBasicEffect_VSBasicTxVcNoFog_Registers)),
                             nullptr, 0};
            case 12: return {kBasicEffect_VSBasicVertexLightingTx_Registers,
                             static_cast<int>(std::size(kBasicEffect_VSBasicVertexLightingTx_Registers)),
                             kBasicEffect_PSBasicVertexLightingTx_Registers,
                             static_cast<int>(std::size(kBasicEffect_PSBasicVertexLightingTx_Registers))};
            case 13: return {kBasicEffect_VSBasicVertexLightingTx_Registers,
                             static_cast<int>(std::size(kBasicEffect_VSBasicVertexLightingTx_Registers)),
                             nullptr, 0};
            case 20: return {kBasicEffect_VSBasicOneLightTx_Registers,
                             static_cast<int>(std::size(kBasicEffect_VSBasicOneLightTx_Registers)),
                             kBasicEffect_PSBasicVertexLightingTx_Registers,
                             static_cast<int>(std::size(kBasicEffect_PSBasicVertexLightingTx_Registers))};
            case 21: return {kBasicEffect_VSBasicOneLightTx_Registers,
                             static_cast<int>(std::size(kBasicEffect_VSBasicOneLightTx_Registers)),
                             nullptr, 0};
            // ShaderIndex 28/29 (pixel lighting + texture, fog on/off): BasicEffect.fx's own
            // VSIndices[28]==VSIndices[29]==18 and PSIndices[28]==PSIndices[29]==9 -- unlike every
            // other lit bucket above, the pixel-lighting VSArray/PSArray entries have NO separate
            // NoFog compiled variant at all (confirmed directly in the .fx source: only 4 VSArray
            // entries and 1 relevant PSArray entry exist for this whole bucket), so both cases
            // below intentionally reference the identical register table -- the shared
            // GpuDrawParams::fogVector is zero when fog is disabled, producing a no-op fog
            // contribution and allowing the single compiled shader to cover both cases.
            case 28: [[fallthrough]];
            case 29: return {kBasicEffect_VSBasicPixelLightingTx_Registers,
                              static_cast<int>(std::size(kBasicEffect_VSBasicPixelLightingTx_Registers)),
                              kBasicEffect_PSBasicPixelLightingTx_Registers,
                              static_cast<int>(std::size(kBasicEffect_PSBasicPixelLightingTx_Registers))};
            default:
                throw std::out_of_range(
                    "GetBasicEffectRegisterTablesEXT: ShaderIndex " + std::to_string(shaderIndex) +
                    " has no matching CNA vertex layout (plan_dx9.md D9-82b)");
            }
        }

        struct AlphaTestEffectRegisterTables
        {
            const Shaders::D3D9ShaderConstantSlot* vs; int vsCount;
            const Shaders::D3D9ShaderConstantSlot* ps; int psCount;
        };

        /// D9-82c: all 8 of AlphaTestEffect's ShaderIndex values are drawable (unlike BasicEffect --
        /// AlphaTestEffect's only two VSInput shapes, VSInputTx/VSInputTxVc, map 1:1 onto the
        /// existing stride-20/24 CNA layouts with no wasted or missing fields).
        AlphaTestEffectRegisterTables GetAlphaTestEffectRegisterTablesEXT(int shaderIndex)
        {
            using namespace Shaders;
            switch (shaderIndex)
            {
            case 0: return {kAlphaTestEffect_VSAlphaTest_Registers,
                            static_cast<int>(std::size(kAlphaTestEffect_VSAlphaTest_Registers)),
                            kAlphaTestEffect_PSAlphaTestLtGt_Registers,
                            static_cast<int>(std::size(kAlphaTestEffect_PSAlphaTestLtGt_Registers))};
            case 1: return {kAlphaTestEffect_VSAlphaTestNoFog_Registers,
                            static_cast<int>(std::size(kAlphaTestEffect_VSAlphaTestNoFog_Registers)),
                            kAlphaTestEffect_PSAlphaTestLtGtNoFog_Registers,
                            static_cast<int>(std::size(kAlphaTestEffect_PSAlphaTestLtGtNoFog_Registers))};
            case 2: return {kAlphaTestEffect_VSAlphaTestVc_Registers,
                            static_cast<int>(std::size(kAlphaTestEffect_VSAlphaTestVc_Registers)),
                            kAlphaTestEffect_PSAlphaTestLtGt_Registers,
                            static_cast<int>(std::size(kAlphaTestEffect_PSAlphaTestLtGt_Registers))};
            case 3: return {kAlphaTestEffect_VSAlphaTestVcNoFog_Registers,
                            static_cast<int>(std::size(kAlphaTestEffect_VSAlphaTestVcNoFog_Registers)),
                            kAlphaTestEffect_PSAlphaTestLtGtNoFog_Registers,
                            static_cast<int>(std::size(kAlphaTestEffect_PSAlphaTestLtGtNoFog_Registers))};
            case 4: return {kAlphaTestEffect_VSAlphaTest_Registers,
                            static_cast<int>(std::size(kAlphaTestEffect_VSAlphaTest_Registers)),
                            kAlphaTestEffect_PSAlphaTestEqNe_Registers,
                            static_cast<int>(std::size(kAlphaTestEffect_PSAlphaTestEqNe_Registers))};
            case 5: return {kAlphaTestEffect_VSAlphaTestNoFog_Registers,
                            static_cast<int>(std::size(kAlphaTestEffect_VSAlphaTestNoFog_Registers)),
                            kAlphaTestEffect_PSAlphaTestEqNeNoFog_Registers,
                            static_cast<int>(std::size(kAlphaTestEffect_PSAlphaTestEqNeNoFog_Registers))};
            case 6: return {kAlphaTestEffect_VSAlphaTestVc_Registers,
                            static_cast<int>(std::size(kAlphaTestEffect_VSAlphaTestVc_Registers)),
                            kAlphaTestEffect_PSAlphaTestEqNe_Registers,
                            static_cast<int>(std::size(kAlphaTestEffect_PSAlphaTestEqNe_Registers))};
            case 7: return {kAlphaTestEffect_VSAlphaTestVcNoFog_Registers,
                            static_cast<int>(std::size(kAlphaTestEffect_VSAlphaTestVcNoFog_Registers)),
                            kAlphaTestEffect_PSAlphaTestEqNeNoFog_Registers,
                            static_cast<int>(std::size(kAlphaTestEffect_PSAlphaTestEqNeNoFog_Registers))};
            default:
                throw std::out_of_range(
                    "GetAlphaTestEffectRegisterTablesEXT: ShaderIndex " + std::to_string(shaderIndex) +
                    " out of range [0, 8)");
            }
        }

        struct DualTextureEffectRegisterTables
        {
            const Shaders::D3D9ShaderConstantSlot* vs; int vsCount;
            const Shaders::D3D9ShaderConstantSlot* ps; int psCount;
        };

        /// D9-82d: only ShaderIndex 0/1 (no vertex color) are drawable -- the vertex-color variants
        /// (VSInputTx2Vc, 32 bytes) collide with the existing Position+Normal+TexCoord 32-byte
        /// layout, same category as BasicEffect's own D9-82b gaps. See DrawDualTextureEffectEXT's
        /// own combo check, which throws before this function would ever see ShaderIndex 2/3.
        DualTextureEffectRegisterTables GetDualTextureEffectRegisterTablesEXT(int shaderIndex)
        {
            using namespace Shaders;
            switch (shaderIndex)
            {
            case 0: return {kDualTextureEffect_VSDualTexture_Registers,
                            static_cast<int>(std::size(kDualTextureEffect_VSDualTexture_Registers)),
                            kDualTextureEffect_PSDualTexture_Registers,
                            static_cast<int>(std::size(kDualTextureEffect_PSDualTexture_Registers))};
            case 1: return {kDualTextureEffect_VSDualTextureNoFog_Registers,
                            static_cast<int>(std::size(kDualTextureEffect_VSDualTextureNoFog_Registers)),
                            nullptr, 0};
            default:
                throw std::out_of_range(
                    "GetDualTextureEffectRegisterTablesEXT: ShaderIndex " + std::to_string(shaderIndex) +
                    " has no matching CNA vertex layout (plan_dx9.md D9-82d)");
            }
        }

        struct EnvironmentMapEffectRegisterTables
        {
            const Shaders::D3D9ShaderConstantSlot* vs; int vsCount;
            const Shaders::D3D9ShaderConstantSlot* ps; int psCount;
        };

        /// D9-82e: all 16 ShaderIndex values are reachable -- specularEnabled now reads
        /// GpuDrawParams' real field (D9-81 item 4, resolved 2026-07-16; the specular ShaderIndex
        /// values 4-7/12-15 reuse the SAME vertex shader as their non-specular counterpart, only
        /// the pixel shader differs, confirmed directly against EnvironmentMapEffect.fx's own
        /// VSIndices table). VSInputNmTx matches the EXISTING stride-32 layout exactly -- no new
        /// vertex declaration needed here (unlike DualTextureEffect's D9-82d case).
        EnvironmentMapEffectRegisterTables GetEnvironmentMapEffectRegisterTablesEXT(int shaderIndex)
        {
            using namespace Shaders;
            switch (shaderIndex)
            {
            case 0: return {kEnvironmentMapEffect_VSEnvMap_Registers,
                            static_cast<int>(std::size(kEnvironmentMapEffect_VSEnvMap_Registers)),
                            kEnvironmentMapEffect_PSEnvMap_Registers,
                            static_cast<int>(std::size(kEnvironmentMapEffect_PSEnvMap_Registers))};
            case 1: return {kEnvironmentMapEffect_VSEnvMap_Registers,
                            static_cast<int>(std::size(kEnvironmentMapEffect_VSEnvMap_Registers)),
                            nullptr, 0};
            case 2: return {kEnvironmentMapEffect_VSEnvMapFresnel_Registers,
                            static_cast<int>(std::size(kEnvironmentMapEffect_VSEnvMapFresnel_Registers)),
                            kEnvironmentMapEffect_PSEnvMap_Registers,
                            static_cast<int>(std::size(kEnvironmentMapEffect_PSEnvMap_Registers))};
            case 3: return {kEnvironmentMapEffect_VSEnvMapFresnel_Registers,
                            static_cast<int>(std::size(kEnvironmentMapEffect_VSEnvMapFresnel_Registers)),
                            nullptr, 0};
            case 8: return {kEnvironmentMapEffect_VSEnvMapOneLight_Registers,
                            static_cast<int>(std::size(kEnvironmentMapEffect_VSEnvMapOneLight_Registers)),
                            kEnvironmentMapEffect_PSEnvMap_Registers,
                            static_cast<int>(std::size(kEnvironmentMapEffect_PSEnvMap_Registers))};
            case 9: return {kEnvironmentMapEffect_VSEnvMapOneLight_Registers,
                            static_cast<int>(std::size(kEnvironmentMapEffect_VSEnvMapOneLight_Registers)),
                            nullptr, 0};
            case 10: return {kEnvironmentMapEffect_VSEnvMapOneLightFresnel_Registers,
                             static_cast<int>(std::size(kEnvironmentMapEffect_VSEnvMapOneLightFresnel_Registers)),
                             kEnvironmentMapEffect_PSEnvMap_Registers,
                             static_cast<int>(std::size(kEnvironmentMapEffect_PSEnvMap_Registers))};
            case 11: return {kEnvironmentMapEffect_VSEnvMapOneLightFresnel_Registers,
                             static_cast<int>(std::size(kEnvironmentMapEffect_VSEnvMapOneLightFresnel_Registers)),
                             nullptr, 0};
            // ShaderIndex 4-7/12-15 (specular variants): EnvironmentMapEffect.fx's own VSIndices
            // table reuses the IDENTICAL vertex shader as the corresponding non-specular bucket
            // (specular only changes the PIXEL shader -- VSIndices[4]==VSIndices[0], VSIndices[6]==
            // VSIndices[2], VSIndices[12]==VSIndices[8], VSIndices[14]==VSIndices[10], confirmed
            // directly in the .fx source), so only the PS side differs here (PSEnvMapSpecular /
            // PSEnvMapSpecularNoFog instead of PSEnvMap / nullptr).
            case 4:  return {kEnvironmentMapEffect_VSEnvMap_Registers,
                             static_cast<int>(std::size(kEnvironmentMapEffect_VSEnvMap_Registers)),
                             kEnvironmentMapEffect_PSEnvMapSpecular_Registers,
                             static_cast<int>(std::size(kEnvironmentMapEffect_PSEnvMapSpecular_Registers))};
            case 5:  return {kEnvironmentMapEffect_VSEnvMap_Registers,
                             static_cast<int>(std::size(kEnvironmentMapEffect_VSEnvMap_Registers)),
                             kEnvironmentMapEffect_PSEnvMapSpecularNoFog_Registers,
                             static_cast<int>(std::size(kEnvironmentMapEffect_PSEnvMapSpecularNoFog_Registers))};
            case 6:  return {kEnvironmentMapEffect_VSEnvMapFresnel_Registers,
                             static_cast<int>(std::size(kEnvironmentMapEffect_VSEnvMapFresnel_Registers)),
                             kEnvironmentMapEffect_PSEnvMapSpecular_Registers,
                             static_cast<int>(std::size(kEnvironmentMapEffect_PSEnvMapSpecular_Registers))};
            case 7:  return {kEnvironmentMapEffect_VSEnvMapFresnel_Registers,
                             static_cast<int>(std::size(kEnvironmentMapEffect_VSEnvMapFresnel_Registers)),
                             kEnvironmentMapEffect_PSEnvMapSpecularNoFog_Registers,
                             static_cast<int>(std::size(kEnvironmentMapEffect_PSEnvMapSpecularNoFog_Registers))};
            case 12: return {kEnvironmentMapEffect_VSEnvMapOneLight_Registers,
                             static_cast<int>(std::size(kEnvironmentMapEffect_VSEnvMapOneLight_Registers)),
                             kEnvironmentMapEffect_PSEnvMapSpecular_Registers,
                             static_cast<int>(std::size(kEnvironmentMapEffect_PSEnvMapSpecular_Registers))};
            case 13: return {kEnvironmentMapEffect_VSEnvMapOneLight_Registers,
                             static_cast<int>(std::size(kEnvironmentMapEffect_VSEnvMapOneLight_Registers)),
                             kEnvironmentMapEffect_PSEnvMapSpecularNoFog_Registers,
                             static_cast<int>(std::size(kEnvironmentMapEffect_PSEnvMapSpecularNoFog_Registers))};
            case 14: return {kEnvironmentMapEffect_VSEnvMapOneLightFresnel_Registers,
                             static_cast<int>(std::size(kEnvironmentMapEffect_VSEnvMapOneLightFresnel_Registers)),
                             kEnvironmentMapEffect_PSEnvMapSpecular_Registers,
                             static_cast<int>(std::size(kEnvironmentMapEffect_PSEnvMapSpecular_Registers))};
            case 15: return {kEnvironmentMapEffect_VSEnvMapOneLightFresnel_Registers,
                             static_cast<int>(std::size(kEnvironmentMapEffect_VSEnvMapOneLightFresnel_Registers)),
                             kEnvironmentMapEffect_PSEnvMapSpecularNoFog_Registers,
                             static_cast<int>(std::size(kEnvironmentMapEffect_PSEnvMapSpecularNoFog_Registers))};
            default:
                throw std::out_of_range(
                    "GetEnvironmentMapEffectRegisterTablesEXT: ShaderIndex " + std::to_string(shaderIndex) +
                    " out of range [0, 16)");
            }
        }

        struct SkinnedEffectRegisterTables
        {
            const Shaders::D3D9ShaderConstantSlot* vs; int vsCount;
            const Shaders::D3D9ShaderConstantSlot* ps; int psCount;
        };

        /// D9-82f: all 18 ShaderIndex values are reachable -- preferPerPixelLighting now reads
        /// GpuDrawParams' real field (D9-81 item 1, resolved 2026-07-16; the pixel-lighting bucket,
        /// ShaderIndex 12-17, uses the SAME VSInputNmTxWeights vertex shape as the vertex-lit/
        /// one-light buckets above, confirmed directly against SkinnedEffect.fx's own VSIndices
        /// table). VSInputNmTxWeights matches the EXISTING stride-52 layout exactly -- no new
        /// vertex declaration needed here.
        SkinnedEffectRegisterTables GetSkinnedEffectRegisterTablesEXT(int shaderIndex)
        {
            using namespace Shaders;
            switch (shaderIndex)
            {
            case 0:  return {kSkinnedEffect_VSSkinnedVertexLightingOneBone_Registers,
                             static_cast<int>(std::size(kSkinnedEffect_VSSkinnedVertexLightingOneBone_Registers)),
                             kSkinnedEffect_PSSkinnedVertexLighting_Registers,
                             static_cast<int>(std::size(kSkinnedEffect_PSSkinnedVertexLighting_Registers))};
            case 1:  return {kSkinnedEffect_VSSkinnedVertexLightingOneBone_Registers,
                             static_cast<int>(std::size(kSkinnedEffect_VSSkinnedVertexLightingOneBone_Registers)),
                             nullptr, 0};
            case 2:  return {kSkinnedEffect_VSSkinnedVertexLightingTwoBones_Registers,
                             static_cast<int>(std::size(kSkinnedEffect_VSSkinnedVertexLightingTwoBones_Registers)),
                             kSkinnedEffect_PSSkinnedVertexLighting_Registers,
                             static_cast<int>(std::size(kSkinnedEffect_PSSkinnedVertexLighting_Registers))};
            case 3:  return {kSkinnedEffect_VSSkinnedVertexLightingTwoBones_Registers,
                             static_cast<int>(std::size(kSkinnedEffect_VSSkinnedVertexLightingTwoBones_Registers)),
                             nullptr, 0};
            case 4:  return {kSkinnedEffect_VSSkinnedVertexLightingFourBones_Registers,
                             static_cast<int>(std::size(kSkinnedEffect_VSSkinnedVertexLightingFourBones_Registers)),
                             kSkinnedEffect_PSSkinnedVertexLighting_Registers,
                             static_cast<int>(std::size(kSkinnedEffect_PSSkinnedVertexLighting_Registers))};
            case 5:  return {kSkinnedEffect_VSSkinnedVertexLightingFourBones_Registers,
                             static_cast<int>(std::size(kSkinnedEffect_VSSkinnedVertexLightingFourBones_Registers)),
                             nullptr, 0};
            case 6:  return {kSkinnedEffect_VSSkinnedOneLightOneBone_Registers,
                             static_cast<int>(std::size(kSkinnedEffect_VSSkinnedOneLightOneBone_Registers)),
                             kSkinnedEffect_PSSkinnedVertexLighting_Registers,
                             static_cast<int>(std::size(kSkinnedEffect_PSSkinnedVertexLighting_Registers))};
            case 7:  return {kSkinnedEffect_VSSkinnedOneLightOneBone_Registers,
                             static_cast<int>(std::size(kSkinnedEffect_VSSkinnedOneLightOneBone_Registers)),
                             nullptr, 0};
            case 8:  return {kSkinnedEffect_VSSkinnedOneLightTwoBones_Registers,
                             static_cast<int>(std::size(kSkinnedEffect_VSSkinnedOneLightTwoBones_Registers)),
                             kSkinnedEffect_PSSkinnedVertexLighting_Registers,
                             static_cast<int>(std::size(kSkinnedEffect_PSSkinnedVertexLighting_Registers))};
            case 9:  return {kSkinnedEffect_VSSkinnedOneLightTwoBones_Registers,
                             static_cast<int>(std::size(kSkinnedEffect_VSSkinnedOneLightTwoBones_Registers)),
                             nullptr, 0};
            case 10: return {kSkinnedEffect_VSSkinnedOneLightFourBones_Registers,
                             static_cast<int>(std::size(kSkinnedEffect_VSSkinnedOneLightFourBones_Registers)),
                             kSkinnedEffect_PSSkinnedVertexLighting_Registers,
                             static_cast<int>(std::size(kSkinnedEffect_PSSkinnedVertexLighting_Registers))};
            case 11: return {kSkinnedEffect_VSSkinnedOneLightFourBones_Registers,
                             static_cast<int>(std::size(kSkinnedEffect_VSSkinnedOneLightFourBones_Registers)),
                             nullptr, 0};
            // ShaderIndex 12-17 (pixel lighting, one/two/four bones, fog on/off): SkinnedEffect.fx's
            // own VSIndices/PSIndices show the SAME VS per bone count and the SAME PS
            // (PSSkinnedPixelLighting) regardless of fog -- unlike the vertex-lighting/one-light
            // buckets above, there is no separate NoFog compiled variant for pixel lighting at all
            // (confirmed directly in the .fx source), matching BasicEffect's own pixel-lighting
            // bucket shape (GetBasicEffectRegisterTablesEXT's own case 28/29).
            case 12: [[fallthrough]];
            case 13: return {kSkinnedEffect_VSSkinnedPixelLightingOneBone_Registers,
                              static_cast<int>(std::size(kSkinnedEffect_VSSkinnedPixelLightingOneBone_Registers)),
                              kSkinnedEffect_PSSkinnedPixelLighting_Registers,
                              static_cast<int>(std::size(kSkinnedEffect_PSSkinnedPixelLighting_Registers))};
            case 14: [[fallthrough]];
            case 15: return {kSkinnedEffect_VSSkinnedPixelLightingTwoBones_Registers,
                              static_cast<int>(std::size(kSkinnedEffect_VSSkinnedPixelLightingTwoBones_Registers)),
                              kSkinnedEffect_PSSkinnedPixelLighting_Registers,
                              static_cast<int>(std::size(kSkinnedEffect_PSSkinnedPixelLighting_Registers))};
            case 16: [[fallthrough]];
            case 17: return {kSkinnedEffect_VSSkinnedPixelLightingFourBones_Registers,
                              static_cast<int>(std::size(kSkinnedEffect_VSSkinnedPixelLightingFourBones_Registers)),
                              kSkinnedEffect_PSSkinnedPixelLighting_Registers,
                              static_cast<int>(std::size(kSkinnedEffect_PSSkinnedPixelLighting_Registers))};
            default:
                throw std::out_of_range(
                    "GetSkinnedEffectRegisterTablesEXT: ShaderIndex " + std::to_string(shaderIndex) +
                    " out of range [0, 18)");
            }
        }

        /// D9-82f: SkinnedEffect.fx declares `Bones[72]` as a FIXED-size array (216 registers,
        /// D9-72's own compiler-verified count -- 3 registers per bone, matching
        /// EffectParameter.SetValue(Matrix)'s ColumnCount==4/RowCount==3 packing branch, the same
        /// "take the first 3 columns of the transposed matrix" pattern UploadMatrixConstantVS
        /// already uses for World/WorldInverseTranspose) regardless of how many bones this draw's
        /// skeleton actually uses -- unused trailing slots stay zero, safely never read by a valid
        /// vertex's own BLENDINDICES0 (always < boneCount for real skinned geometry).
        void UploadBonesVS(IDirect3DDevice9* device, const Shaders::D3D9ShaderConstantSlot* table,
                          int count, const GpuDrawParams& params)
        {
            std::vector<float> regs(72 * 12, 0.0f);
            const int boneCount = params.boneCount < 72 ? params.boneCount : 72;
            for (int i = 0; i < boneCount; ++i)
            {
                const float* b = params.boneTransforms + i * 16;
                const Matrix boneMatrix(b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
                                        b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
                const Matrix transposed = Matrix::Transpose(boneMatrix);
                float full16[16];
                transposed.ToColumnMajor(full16);
                std::copy(full16, full16 + 12, regs.begin() + static_cast<std::ptrdiff_t>(i) * 12);
            }
            TryUploadVertexShaderConstantEXT(device, table, count, "Bones", regs.data());
        }
    }

    void DirectX9Renderer::DrawPrimitivesExImpl(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        // REMED-GFX-DECL-GUARD: before EnsureRenderReadyEXT, before any IDirect3DVertexDeclaration9
        // is created and before any draw is issued. This renderer selects that declaration from the
        // shared D3DCommon stride table (REMED-GFX-217), so a declaration the table's entry cannot
        // represent is refused rather than rendered from the wrong bytes. An out-of-table stride is
        // left to GetOrCreateVertexDeclarationEXT's own established rejection.
        RequireFaithfulDeclarationEXT(vb, ib != nullptr ? "ordinary-indexed" : "ordinary-nonindexed");
        EnsureRenderReadyEXT();

        const auto& d3dVb = static_cast<const D3D9VertexBufferRenderer&>(vb);
        const std::size_t stride = d3dVb.GetStrideEXT() > 0 ? d3dVb.GetStrideEXT() : 16;

        // D3D9 PBR porting task: params.pbr takes the HIGHEST priority of every flag below,
        // mirroring EasyGLRenderer::SelectProgram()'s own identical
        // "pbr-before-everything-else" cascade (checked before alpha test/dual texture/env
        // mapping/skinned, not after). PbrEffect/SkinnedPbrEffect are not Microsoft Stock Effects
        // at all, so this dispatches to D3D9PbrDraw.cpp's own self-contained shader, not one of
        // this cascade's own DrawXxxEffectEXT stock-effect helpers.
        if (params.pbr)
        {
            DrawPbrEffectEXT(vb, ib, stride, world, view, projection, primitive, primitiveCount, params);
            return;
        }

        // D9-82b: same flag priority cascade DirectX11Renderer::DrawPrimitivesExImpl already
        // established (GpuDrawParams is the only per-draw signal available to pick an effect).
        const bool needsAlphaTest = (params.alphaTest[3] < 0.0f || params.alphaTest[2] < 0.0f);
        const bool needsDualTex   = params.dualTexture && !needsAlphaTest;
        const bool needsEnvMap    = params.envMapping  && !needsAlphaTest && !needsDualTex;
        const bool needsSkinned   = params.skinned     && !needsAlphaTest && !needsDualTex && !needsEnvMap;

        if (needsAlphaTest)
        {
            DrawAlphaTestEffectEXT(vb, ib, stride, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (needsDualTex)
        {
            DrawDualTextureEffectEXT(vb, ib, stride, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (needsEnvMap)
        {
            DrawEnvironmentMapEffectEXT(vb, ib, stride, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (needsSkinned)
        {
            // D3D9 skinned-vertex-color porting task: stride 56 (VertexPositionNormalTextureSkinned
            // + a vertex Color) has no real XNA SkinnedEffect equivalent -- Microsoft's own
            // compiled SkinnedEffect.fx bytecode never carries a Color input (see
            // D3D9SkinnedVertexColorDraw.cpp's own header comment). Routed to CNA's own
            // SkinnedVertexColor3D custom shader instead of DrawSkinnedEffectEXT, which would
            // otherwise throw "no matching CNA vertex layout" for this stride (it only accepts 52).
            if (stride == 56)
            {
                DrawSkinnedVertexColorEXT(vb, ib, world, view, projection, primitive, primitiveCount, params);
                return;
            }
            DrawSkinnedEffectEXT(vb, ib, stride, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        DrawBasicEffectEXT(vb, ib, stride, world, view, projection, primitive, primitiveCount, params);
    }

    void DirectX9Renderer::DrawPrimitivesEx(
        const IVertexBufferRenderer& vb, const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        DrawPrimitivesExImpl(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
    }

    void DirectX9Renderer::DrawIndexedPrimitivesEx(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        DrawPrimitivesExImpl(vb, &ib, world, view, projection, primitive, primitiveCount, params);
    }

    void DirectX9Renderer::DrawBasicEffectEXT(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib, std::size_t stride,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        bool comboOk;
        switch (stride)
        {
        case 16: comboOk = !params.lightingEnabled && params.vertexColorEnabled && !params.textureEnabled; break;
        case 20: comboOk = !params.lightingEnabled && !params.vertexColorEnabled && params.textureEnabled; break;
        case 24: comboOk = !params.lightingEnabled && params.vertexColorEnabled && params.textureEnabled; break;
        case 32: comboOk = params.lightingEnabled && !params.vertexColorEnabled && params.textureEnabled; break;
        default: comboOk = false; break;
        }
        if (!comboOk)
        {
            throw std::runtime_error(
                "DirectX9Renderer::DrawPrimitivesEx (BasicEffect): stride " + std::to_string(stride) +
                " with lighting=" + (params.lightingEnabled ? "true" : "false") +
                " vertexColor=" + (params.vertexColorEnabled ? "true" : "false") +
                " texture=" + (params.textureEnabled ? "true" : "false") +
                " has no matching CNA vertex layout (plan_dx9.md D9-82b)");
        }

        const bool oneLight = ComputeOneLightEXT(params);

        // preferPerPixelLighting now reads GpuDrawParams' real field (plan_dx9.md D9-81 item 1,
        // resolved 2026-07-16 -- see this file's own header comment).
        const int shaderIndex = ComputeBasicEffectShaderIndex(
            params.fogEnabled, params.vertexColorEnabled, params.textureEnabled,
            params.lightingEnabled, params.preferPerPixelLighting, oneLight);

        const BasicEffectRegisterTables regs = GetBasicEffectRegisterTablesEXT(shaderIndex);

        if (!shaderCache_) shaderCache_ = std::make_unique<D3D9ShaderCache>(device_.Get());
        IDirect3DVertexShader9* vs = shaderCache_->GetVertexShader(GetBasicEffectVertexShaderNameEXT(shaderIndex));
        IDirect3DPixelShader9* ps = shaderCache_->GetPixelShader(GetBasicEffectPixelShaderNameEXT(shaderIndex));
        device_->SetVertexShader(vs);
        device_->SetPixelShader(ps);

        UploadMatrixConstantVS(device_.Get(), regs.vs, regs.vsCount, "WorldViewProj", world * view * projection);

        // DiffuseColor: BasicEffect::FillGpuDrawParams() already computes this identically to
        // FNA's own EffectHelpers.SetMaterialColor for BOTH the lit path (diffuseColor*alpha) and
        // the unlit path ((diffuseColor+emissiveColor)*alpha) -- no adjustment needed, verified
        // against the FNA source directly.
        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DiffuseColor", params.diffuseColor);
        // PreferPerPixelLighting's PSBasicPixelLighting*/PSSkinnedPixelLighting shaders declare
        // their OWN DiffuseColor pixel-shader constant (D3D9ShaderRegisters.hpp's
        // kBasicEffect_PSBasicPixelLightingTx_Registers) -- real XNA's ComputeLights() runs
        // entirely in the pixel stage for this bucket, so DiffuseColor must reach the pixel shader
        // too, not just the vertex shader every other bucket uses. TryUploadPixelShaderConstantEXT
        // silently no-ops for every non-pixel-lit bucket's PS table, which has no such name.
        TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "DiffuseColor", params.diffuseColor);

        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "FogVector", params.fogVector);
        TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "FogColor", Pad3(params.fogColor).v);

        if (params.lightingEnabled)
        {
            UploadMatrixConstantVS(device_.Get(), regs.vs, regs.vsCount, "World", world);
            const Matrix worldInverseTranspose = Matrix::Transpose(Matrix::Invert(world));
            UploadMatrixConstantVS(device_.Get(), regs.vs, regs.vsCount, "WorldInverseTranspose", worldInverseTranspose);

            // Real bug found and fixed 2026-07-16, while oracle-proving the PreferPerPixelLighting
            // bucket (plan_dx9.md D9-73's own outstanding obligation): FNA's Lighting.fxh's
            // ComputeLights() runs in the VERTEX shader for every bucket EXCEPT PixelLighting, where
            // it runs in the PIXEL shader instead -- confirmed directly against
            // D3D9ShaderRegisters.hpp, where kBasicEffect_PSBasicPixelLightingTx_Registers (not the
            // VS table) declares EmissiveColor/SpecularColor/SpecularPower/DirLight0-2*/EyePosition.
            // Every upload below now goes to BOTH stages via the soft, never-throws
            // TryUpload*ShaderConstantEXT helpers, which silently no-op wherever a stage's own
            // register table doesn't declare that name -- vertex-lit/one-light buckets are
            // unaffected (their PS tables have none of these names), only the pixel-lit bucket
            // actually receives the PS-side values it was silently missing before this fix.
            TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "EyePosition",
                                             Pad3(params.eyePositionWorld).v);
            TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "EyePosition",
                                             Pad3(params.eyePositionWorld).v);

            // EmissiveColor: FNA's Lighting.fxh folds the ambient contribution into this constant
            // (result.Diffuse = mul(diffuse, lightDiffuse) * DiffuseColor.rgb + EmissiveColor) so
            // the shader never needs a separate ambient add -- GpuDrawParams keeps ambientColor
            // and emissiveColor as separate, un-folded fields (other renderers fold them
            // differently in their own shader math), so this exact real-shader value must be
            // reconstructed here: emissiveColor + ambientColor*diffuseColor (diffuseColor already
            // carries the alpha multiply, matching FNA's own (emissiveColor+ambient*diffuse)*alpha
            // exactly -- verified against EffectHelpers.SetMaterialColor's lit branch).
            const float emissiveFolded[3] = {
                params.emissiveColor[0] + params.ambientColor[0] * params.diffuseColor[0],
                params.emissiveColor[1] + params.ambientColor[1] * params.diffuseColor[1],
                params.emissiveColor[2] + params.ambientColor[2] * params.diffuseColor[2],
            };
            TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "EmissiveColor",
                                             Pad3(emissiveFolded).v);
            TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "EmissiveColor",
                                             Pad3(emissiveFolded).v);
            TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "SpecularColor",
                                             Pad3(params.specularColor).v);
            TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "SpecularColor",
                                             Pad3(params.specularColor).v);
            const float specularPower4[4] = {params.specularPower, 0.0f, 0.0f, 0.0f};
            TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "SpecularPower", specularPower4);
            TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "SpecularPower", specularPower4);

            TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DirLight0Direction",
                                             Pad3(params.light0Dir).v);
            TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "DirLight0Direction",
                                             Pad3(params.light0Dir).v);
            TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DirLight0DiffuseColor",
                                             Pad3(params.light0Diffuse).v);
            TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "DirLight0DiffuseColor",
                                             Pad3(params.light0Diffuse).v);
            TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DirLight0SpecularColor",
                                             Pad3(params.light0Specular).v);
            TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "DirLight0SpecularColor",
                                             Pad3(params.light0Specular).v);
            TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DirLight1Direction",
                                             Pad3(params.light1Dir).v);
            TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "DirLight1Direction",
                                             Pad3(params.light1Dir).v);
            TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DirLight1DiffuseColor",
                                             Pad3(params.light1Diffuse).v);
            TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "DirLight1DiffuseColor",
                                             Pad3(params.light1Diffuse).v);
            TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DirLight1SpecularColor",
                                             Pad3(params.light1Specular).v);
            TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "DirLight1SpecularColor",
                                             Pad3(params.light1Specular).v);
            TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DirLight2Direction",
                                             Pad3(params.light2Dir).v);
            TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "DirLight2Direction",
                                             Pad3(params.light2Dir).v);
            TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DirLight2DiffuseColor",
                                             Pad3(params.light2Diffuse).v);
            TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "DirLight2DiffuseColor",
                                             Pad3(params.light2Diffuse).v);
            TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DirLight2SpecularColor",
                                             Pad3(params.light2Specular).v);
            TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "DirLight2SpecularColor",
                                             Pad3(params.light2Specular).v);
        }

        if (params.textureEnabled && params.texture0)
        {
            device_->SetTexture(0, ResolveD3D9TextureEXT(params.texture0));
        }

        device_->SetVertexDeclaration(GetOrCreateVertexDeclarationEXT(stride));
        const auto& d3dVb = static_cast<const D3D9VertexBufferRenderer&>(vb);
        device_->SetStreamSource(0, d3dVb.GetBufferEXT(), 0, static_cast<UINT>(stride));

        if (ib)
        {
            const auto& d3dIb = static_cast<const D3D9IndexBufferRenderer&>(*ib);
            device_->SetIndices(d3dIb.GetBufferEXT());
            // REMED-GFX-060: honor DrawIndexedPrimitives baseVertex/startIndex (was hardcoded
            // BaseVertexIndex=0/StartIndex=0). NumVertices is the vertex-buffer remainder from
            // baseVertex; MinIndex stays 0 (CNA carries no tighter min-vertex-index range).
            device_->DrawIndexedPrimitive(ToD3D9Topology(primitive), static_cast<INT>(params.baseVertex), 0,
                                          static_cast<UINT>(d3dVb.GetVertexCount() - params.baseVertex),
                                          static_cast<UINT>(params.startIndex), static_cast<UINT>(primitiveCount));
        }
        else
        {
            // REMED-GFX-060: honor DrawPrimitives vertexStart (was hardcoded StartVertex=0).
            device_->DrawPrimitive(ToD3D9Topology(primitive), static_cast<UINT>(params.vertexStart),
                                   static_cast<UINT>(primitiveCount));
        }
    }

    void DirectX9Renderer::DrawAlphaTestEffectEXT(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib, std::size_t stride,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        // AlphaTestEffect always samples a texture (no untextured VSInput shape exists at all in
        // AlphaTestEffect.fx) -- ComputeAlphaTestEffectShaderIndex() itself takes no textureEnabled
        // parameter, unlike BasicEffect's.
        if (!params.texture0)
            throw std::runtime_error(
                "DirectX9Renderer::DrawPrimitivesEx (AlphaTestEffect): requires a non-null "
                "texture0 (plan_dx9.md D9-82c)");

        // AlphaTestEffect's only two VSInput shapes map 1:1 onto the existing CNA strides -- no
        // collision/missing-layout gap like BasicEffect's (see D3D9EffectDraw.cpp's own header
        // comment for that one).
        bool comboOk;
        switch (stride)
        {
        case 20: comboOk = !params.vertexColorEnabled; break;
        case 24: comboOk = params.vertexColorEnabled;  break;
        default: comboOk = false; break;
        }
        if (!comboOk)
            throw std::runtime_error(
                "DirectX9Renderer::DrawPrimitivesEx (AlphaTestEffect): stride " + std::to_string(stride) +
                " with vertexColor=" + (params.vertexColorEnabled ? "true" : "false") +
                " has no matching CNA vertex layout (plan_dx9.md D9-82c)");

        // D9-81's own finding: alphaTest[1] (tolerance) > 0 is a lossless recovery of isEqNe --
        // AlphaTestEffect.cs's own OnApply() sets it to a fixed nonzero constant in EXACTLY the
        // Equal/NotEqual CompareFunction cases and leaves it at 0 everywhere else.
        const bool isEqNe = params.alphaTest[1] > 0.0f;

        const int shaderIndex = ComputeAlphaTestEffectShaderIndex(params.fogEnabled, params.vertexColorEnabled, isEqNe);
        const AlphaTestEffectRegisterTables regs = GetAlphaTestEffectRegisterTablesEXT(shaderIndex);

        if (!shaderCache_) shaderCache_ = std::make_unique<D3D9ShaderCache>(device_.Get());
        IDirect3DVertexShader9* vs = shaderCache_->GetVertexShader(GetAlphaTestEffectVertexShaderNameEXT(shaderIndex));
        IDirect3DPixelShader9* ps = shaderCache_->GetPixelShader(GetAlphaTestEffectPixelShaderNameEXT(shaderIndex));
        device_->SetVertexShader(vs);
        device_->SetPixelShader(ps);

        UploadMatrixConstantVS(device_.Get(), regs.vs, regs.vsCount, "WorldViewProj", world * view * projection);

        // DiffuseColor: AlphaTestEffect::FillGpuDrawParams() computes diffuseColor*alpha, alpha --
        // AlphaTestEffect has no lighting/emissive concept at all, so no adjustment is needed
        // (unlike BasicEffect's lit path, there is nothing to fold in here).
        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DiffuseColor", params.diffuseColor);

        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "FogVector", params.fogVector);
        TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "FogColor", Pad3(params.fogColor).v);

        // AlphaTest: GpuDrawParams::alphaTest is already exactly the real {refVal,tolerance,
        // passWeight,failWeight} register layout AlphaTestEffect.fx's own clip() expressions
        // expect -- confirmed against its own doc comment and the .fx source's clip() calls
        // directly, no reconstruction needed (unlike BasicEffect's EmissiveColor).
        TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "AlphaTest", params.alphaTest);

        device_->SetTexture(0, ResolveD3D9TextureEXT(params.texture0));

        device_->SetVertexDeclaration(GetOrCreateVertexDeclarationEXT(stride));
        const auto& d3dVb = static_cast<const D3D9VertexBufferRenderer&>(vb);
        device_->SetStreamSource(0, d3dVb.GetBufferEXT(), 0, static_cast<UINT>(stride));

        if (ib)
        {
            const auto& d3dIb = static_cast<const D3D9IndexBufferRenderer&>(*ib);
            device_->SetIndices(d3dIb.GetBufferEXT());
            // REMED-GFX-060: honor DrawIndexedPrimitives baseVertex/startIndex (was hardcoded
            // BaseVertexIndex=0/StartIndex=0). NumVertices is the vertex-buffer remainder from
            // baseVertex; MinIndex stays 0 (CNA carries no tighter min-vertex-index range).
            device_->DrawIndexedPrimitive(ToD3D9Topology(primitive), static_cast<INT>(params.baseVertex), 0,
                                          static_cast<UINT>(d3dVb.GetVertexCount() - params.baseVertex),
                                          static_cast<UINT>(params.startIndex), static_cast<UINT>(primitiveCount));
        }
        else
        {
            // REMED-GFX-060: honor DrawPrimitives vertexStart (was hardcoded StartVertex=0).
            device_->DrawPrimitive(ToD3D9Topology(primitive), static_cast<UINT>(params.vertexStart),
                                   static_cast<UINT>(primitiveCount));
        }
    }

    void DirectX9Renderer::DrawDualTextureEffectEXT(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib, std::size_t stride,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        if (!params.texture0 || !params.texture1)
            throw std::runtime_error(
                "DirectX9Renderer::DrawPrimitivesEx (DualTextureEffect): requires non-null "
                "texture0 AND texture1 (plan_dx9.md D9-82d)");

        // Only the no-vertex-color combination is drawable: VSInputTx2Vc (32 bytes) collides with
        // the existing Position+Normal+TexCoord layout, same category as BasicEffect's own D9-82b
        // gaps -- see D3D9VertexDeclarations.hpp's own header comment.
        if (stride != 28 || params.vertexColorEnabled)
            throw std::runtime_error(
                "DirectX9Renderer::DrawPrimitivesEx (DualTextureEffect): stride " + std::to_string(stride) +
                " with vertexColor=" + (params.vertexColorEnabled ? "true" : "false") +
                " has no matching CNA vertex layout (plan_dx9.md D9-82d)");

        const int shaderIndex = ComputeDualTextureEffectShaderIndex(params.fogEnabled, params.vertexColorEnabled);
        const DualTextureEffectRegisterTables regs = GetDualTextureEffectRegisterTablesEXT(shaderIndex);

        if (!shaderCache_) shaderCache_ = std::make_unique<D3D9ShaderCache>(device_.Get());
        IDirect3DVertexShader9* vs = shaderCache_->GetVertexShader(GetDualTextureEffectVertexShaderNameEXT(shaderIndex));
        IDirect3DPixelShader9* ps = shaderCache_->GetPixelShader(GetDualTextureEffectPixelShaderNameEXT(shaderIndex));
        device_->SetVertexShader(vs);
        device_->SetPixelShader(ps);

        UploadMatrixConstantVS(device_.Get(), regs.vs, regs.vsCount, "WorldViewProj", world * view * projection);

        // DiffuseColor: DualTextureEffect::FillGpuDrawParams() computes diffuseColor*alpha, alpha --
        // same pattern as AlphaTestEffect, no adjustment needed (no lighting/emissive concept here).
        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DiffuseColor", params.diffuseColor);

        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "FogVector", params.fogVector);
        TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "FogColor", Pad3(params.fogColor).v);

        // PSDualTexture's real formula (DualTextureEffect.fx): color = SampleTex(Texture,uv0);
        // color.rgb *= 2; color *= SampleTex(Texture2,uv1) * pin.Diffuse -- the classic XNA
        // lightmap-doubling blend. No CPU-side computation needed here; it's entirely in the real
        // compiled pixel shader.
        device_->SetTexture(0, ResolveD3D9TextureEXT(params.texture0));
        device_->SetTexture(1, ResolveD3D9TextureEXT(params.texture1));

        device_->SetVertexDeclaration(GetOrCreateVertexDeclarationEXT(stride));
        const auto& d3dVb = static_cast<const D3D9VertexBufferRenderer&>(vb);
        device_->SetStreamSource(0, d3dVb.GetBufferEXT(), 0, static_cast<UINT>(stride));

        if (ib)
        {
            const auto& d3dIb = static_cast<const D3D9IndexBufferRenderer&>(*ib);
            device_->SetIndices(d3dIb.GetBufferEXT());
            // REMED-GFX-060: honor DrawIndexedPrimitives baseVertex/startIndex (was hardcoded
            // BaseVertexIndex=0/StartIndex=0). NumVertices is the vertex-buffer remainder from
            // baseVertex; MinIndex stays 0 (CNA carries no tighter min-vertex-index range).
            device_->DrawIndexedPrimitive(ToD3D9Topology(primitive), static_cast<INT>(params.baseVertex), 0,
                                          static_cast<UINT>(d3dVb.GetVertexCount() - params.baseVertex),
                                          static_cast<UINT>(params.startIndex), static_cast<UINT>(primitiveCount));
        }
        else
        {
            // REMED-GFX-060: honor DrawPrimitives vertexStart (was hardcoded StartVertex=0).
            device_->DrawPrimitive(ToD3D9Topology(primitive), static_cast<UINT>(params.vertexStart),
                                   static_cast<UINT>(primitiveCount));
        }
    }

    void DirectX9Renderer::DrawEnvironmentMapEffectEXT(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib, std::size_t stride,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        if (!params.texture0 || !params.envMap)
            throw std::runtime_error(
                "DirectX9Renderer::DrawPrimitivesEx (EnvironmentMapEffect): requires non-null "
                "texture0 AND envMap (plan_dx9.md D9-82e)");

        // VSInputNmTx (Position+Normal+TexCoord) matches the existing stride-32 layout exactly --
        // no new vertex declaration needed here (unlike DualTextureEffect's D9-82d case).
        if (stride != 32)
            throw std::runtime_error(
                "DirectX9Renderer::DrawPrimitivesEx (EnvironmentMapEffect): stride " +
                std::to_string(stride) + " has no matching CNA vertex layout (plan_dx9.md D9-82e)");

        // specularEnabled now reads GpuDrawParams' real field (plan_dx9.md D9-81 item 4, resolved
        // 2026-07-16; same category as BasicEffect's PreferPerPixelLighting above).
        const bool oneLight = ComputeOneLightEXT(params);
        const int shaderIndex = ComputeEnvironmentMapEffectShaderIndex(
            params.fogEnabled, params.fresnelEnabled, params.specularEnabled, oneLight);

        const EnvironmentMapEffectRegisterTables regs = GetEnvironmentMapEffectRegisterTablesEXT(shaderIndex);

        if (!shaderCache_) shaderCache_ = std::make_unique<D3D9ShaderCache>(device_.Get());
        IDirect3DVertexShader9* vs = shaderCache_->GetVertexShader(GetEnvironmentMapEffectVertexShaderNameEXT(shaderIndex));
        IDirect3DPixelShader9* ps = shaderCache_->GetPixelShader(GetEnvironmentMapEffectPixelShaderNameEXT(shaderIndex));
        device_->SetVertexShader(vs);
        device_->SetPixelShader(ps);

        UploadMatrixConstantVS(device_.Get(), regs.vs, regs.vsCount, "WorldViewProj", world * view * projection);
        UploadMatrixConstantVS(device_.Get(), regs.vs, regs.vsCount, "World", world);
        const Matrix worldInverseTranspose = Matrix::Transpose(Matrix::Invert(world));
        UploadMatrixConstantVS(device_.Get(), regs.vs, regs.vsCount, "WorldInverseTranspose", worldInverseTranspose);

        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DiffuseColor", params.diffuseColor);

        // EmissiveColor: unlike BasicEffect, EnvironmentMapEffect::FillGpuDrawParams() already
        // pre-folds ambient*diffuse+emissive (confirmed at its own call site) -- upload verbatim.
        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "EmissiveColor",
                                         Pad3(params.emissiveColor).v);

        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "EyePosition",
                                         Pad3(params.eyePositionWorld).v);

        const float envMapAmount4[4] = {params.envMapAmount, 0.0f, 0.0f, 0.0f};
        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "EnvironmentMapAmount", envMapAmount4);
        const float fresnelFactor4[4] = {params.fresnelFactor, 0.0f, 0.0f, 0.0f};
        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "FresnelFactor", fresnelFactor4);
        // Real bug found and fixed 2026-07-16, alongside the same category of gap in
        // DrawBasicEffectEXT/DrawSkinnedEffectEXT above: EnvironmentMapSpecular was never uploaded
        // anywhere -- specularEnabled was hardcoded false (D9-81 item 4) so the specular pixel
        // shader variant (PSEnvMapSpecular/PSEnvMapSpecularNoFog, the only place
        // EnvironmentMapSpecular's 'p',0,1 register lives, per D3D9ShaderRegisters.hpp) was never
        // reachable, and the missing upload went unnoticed. Now that specularEnabled reads its
        // real GpuDrawParams field, the specular pixel shader IS reachable and needs this.
        TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "EnvironmentMapSpecular",
                                         Pad3(params.envMapSpecular).v);

        // No SpecularColor/SpecularPower/DirLight*SpecularColor here -- EnvironmentMapEffect.fx
        // #defines all of them to 0 rather than declaring real constants (Lighting.fxh needs the
        // symbols to compile but this effect never varies them), so no register exists to upload.
        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DirLight0Direction",
                                         Pad3(params.light0Dir).v);
        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DirLight0DiffuseColor",
                                         Pad3(params.light0Diffuse).v);
        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DirLight1Direction",
                                         Pad3(params.light1Dir).v);
        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DirLight1DiffuseColor",
                                         Pad3(params.light1Diffuse).v);
        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DirLight2Direction",
                                         Pad3(params.light2Dir).v);
        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DirLight2DiffuseColor",
                                         Pad3(params.light2Diffuse).v);

        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "FogVector", params.fogVector);
        TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "FogColor", Pad3(params.fogColor).v);

        device_->SetTexture(0, ResolveD3D9TextureEXT(params.texture0));
        device_->SetTexture(1, ResolveD3D9TextureCubeEXT(params.envMap));

        device_->SetVertexDeclaration(GetOrCreateVertexDeclarationEXT(stride));
        const auto& d3dVb = static_cast<const D3D9VertexBufferRenderer&>(vb);
        device_->SetStreamSource(0, d3dVb.GetBufferEXT(), 0, static_cast<UINT>(stride));

        if (ib)
        {
            const auto& d3dIb = static_cast<const D3D9IndexBufferRenderer&>(*ib);
            device_->SetIndices(d3dIb.GetBufferEXT());
            // REMED-GFX-060: honor DrawIndexedPrimitives baseVertex/startIndex (was hardcoded
            // BaseVertexIndex=0/StartIndex=0). NumVertices is the vertex-buffer remainder from
            // baseVertex; MinIndex stays 0 (CNA carries no tighter min-vertex-index range).
            device_->DrawIndexedPrimitive(ToD3D9Topology(primitive), static_cast<INT>(params.baseVertex), 0,
                                          static_cast<UINT>(d3dVb.GetVertexCount() - params.baseVertex),
                                          static_cast<UINT>(params.startIndex), static_cast<UINT>(primitiveCount));
        }
        else
        {
            // REMED-GFX-060: honor DrawPrimitives vertexStart (was hardcoded StartVertex=0).
            device_->DrawPrimitive(ToD3D9Topology(primitive), static_cast<UINT>(params.vertexStart),
                                   static_cast<UINT>(primitiveCount));
        }
    }

    void DirectX9Renderer::DrawSkinnedEffectEXT(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib, std::size_t stride,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        if (!params.texture0)
            throw std::runtime_error(
                "DirectX9Renderer::DrawPrimitivesEx (SkinnedEffect): requires non-null texture0 "
                "(plan_dx9.md D9-82f)");

        // VSInputNmTxWeights (Position+Normal+TexCoord+BlendWeight+BlendIndices) matches the
        // existing stride-52 layout exactly -- no new vertex declaration needed here.
        if (stride != 52)
            throw std::runtime_error(
                "DirectX9Renderer::DrawPrimitivesEx (SkinnedEffect): stride " + std::to_string(stride) +
                " has no matching CNA vertex layout (plan_dx9.md D9-82f)");

        const bool oneLight = ComputeOneLightEXT(params);
        // preferPerPixelLighting now reads GpuDrawParams' real field (plan_dx9.md D9-81 item 1,
        // resolved 2026-07-16; same gap as BasicEffect's own case above).
        const int shaderIndex = ComputeSkinnedEffectShaderIndex(
            params.fogEnabled, params.weightsPerVertex, params.preferPerPixelLighting, oneLight);

        const SkinnedEffectRegisterTables regs = GetSkinnedEffectRegisterTablesEXT(shaderIndex);

        if (!shaderCache_) shaderCache_ = std::make_unique<D3D9ShaderCache>(device_.Get());
        IDirect3DVertexShader9* vs = shaderCache_->GetVertexShader(GetSkinnedEffectVertexShaderNameEXT(shaderIndex));
        IDirect3DPixelShader9* ps = shaderCache_->GetPixelShader(GetSkinnedEffectPixelShaderNameEXT(shaderIndex));
        device_->SetVertexShader(vs);
        device_->SetPixelShader(ps);

        UploadMatrixConstantVS(device_.Get(), regs.vs, regs.vsCount, "WorldViewProj", world * view * projection);
        UploadMatrixConstantVS(device_.Get(), regs.vs, regs.vsCount, "World", world);
        const Matrix worldInverseTranspose = Matrix::Transpose(Matrix::Invert(world));
        UploadMatrixConstantVS(device_.Get(), regs.vs, regs.vsCount, "WorldInverseTranspose", worldInverseTranspose);

        // DiffuseColor/EmissiveColor: SkinnedEffect::FillGpuDrawParams() computes these identically
        // to BasicEffect's own lit path -- Skin() only transforms Position/Normal, then delegates
        // to the SAME ComputeCommonVSOutputWithLighting() BasicEffect's lit path uses. EmissiveColor
        // is already pre-folded with ambient*diffuse (like EnvironmentMapEffect's case) -- upload
        // both verbatim, no reconstruction needed.
        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DiffuseColor", params.diffuseColor);
        // Same real bug/fix as DrawBasicEffectEXT above (found 2026-07-16 while oracle-proving the
        // PreferPerPixelLighting bucket): SkinnedEffect.fx's PSSkinnedPixelLighting shader declares
        // its own PS-side DiffuseColor/EmissiveColor/SpecularColor/SpecularPower/DirLight0-2*/
        // EyePosition (ComputeLights() runs per-pixel for this bucket only) -- every upload below
        // now also goes to the pixel stage via the soft TryUploadPixelShaderConstantEXT, which
        // silently no-ops for every other bucket's PS table (none of them declare these names).
        // Bones stays vertex-stage-only for every bucket -- skinning transforms Position/Normal in
        // the vertex shader regardless of which lighting bucket is selected.
        TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "DiffuseColor", params.diffuseColor);
        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "EmissiveColor",
                                         Pad3(params.emissiveColor).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "EmissiveColor",
                                         Pad3(params.emissiveColor).v);
        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "SpecularColor",
                                         Pad3(params.specularColor).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "SpecularColor",
                                         Pad3(params.specularColor).v);
        const float specularPower4[4] = {params.specularPower, 0.0f, 0.0f, 0.0f};
        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "SpecularPower", specularPower4);
        TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "SpecularPower", specularPower4);
        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "EyePosition",
                                         Pad3(params.eyePositionWorld).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "EyePosition",
                                         Pad3(params.eyePositionWorld).v);

        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DirLight0Direction",
                                         Pad3(params.light0Dir).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "DirLight0Direction",
                                         Pad3(params.light0Dir).v);
        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DirLight0DiffuseColor",
                                         Pad3(params.light0Diffuse).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "DirLight0DiffuseColor",
                                         Pad3(params.light0Diffuse).v);
        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DirLight0SpecularColor",
                                         Pad3(params.light0Specular).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "DirLight0SpecularColor",
                                         Pad3(params.light0Specular).v);
        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DirLight1Direction",
                                         Pad3(params.light1Dir).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "DirLight1Direction",
                                         Pad3(params.light1Dir).v);
        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DirLight1DiffuseColor",
                                         Pad3(params.light1Diffuse).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "DirLight1DiffuseColor",
                                         Pad3(params.light1Diffuse).v);
        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DirLight1SpecularColor",
                                         Pad3(params.light1Specular).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "DirLight1SpecularColor",
                                         Pad3(params.light1Specular).v);
        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DirLight2Direction",
                                         Pad3(params.light2Dir).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "DirLight2Direction",
                                         Pad3(params.light2Dir).v);
        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DirLight2DiffuseColor",
                                         Pad3(params.light2Diffuse).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "DirLight2DiffuseColor",
                                         Pad3(params.light2Diffuse).v);
        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "DirLight2SpecularColor",
                                         Pad3(params.light2Specular).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "DirLight2SpecularColor",
                                         Pad3(params.light2Specular).v);

        UploadBonesVS(device_.Get(), regs.vs, regs.vsCount, params);

        TryUploadVertexShaderConstantEXT(device_.Get(), regs.vs, regs.vsCount, "FogVector", params.fogVector);
        TryUploadPixelShaderConstantEXT(device_.Get(), regs.ps, regs.psCount, "FogColor", Pad3(params.fogColor).v);

        device_->SetTexture(0, ResolveD3D9TextureEXT(params.texture0));

        device_->SetVertexDeclaration(GetOrCreateVertexDeclarationEXT(stride));
        const auto& d3dVb = static_cast<const D3D9VertexBufferRenderer&>(vb);
        device_->SetStreamSource(0, d3dVb.GetBufferEXT(), 0, static_cast<UINT>(stride));

        if (ib)
        {
            const auto& d3dIb = static_cast<const D3D9IndexBufferRenderer&>(*ib);
            device_->SetIndices(d3dIb.GetBufferEXT());
            // REMED-GFX-060: honor DrawIndexedPrimitives baseVertex/startIndex (was hardcoded
            // BaseVertexIndex=0/StartIndex=0). NumVertices is the vertex-buffer remainder from
            // baseVertex; MinIndex stays 0 (CNA carries no tighter min-vertex-index range).
            device_->DrawIndexedPrimitive(ToD3D9Topology(primitive), static_cast<INT>(params.baseVertex), 0,
                                          static_cast<UINT>(d3dVb.GetVertexCount() - params.baseVertex),
                                          static_cast<UINT>(params.startIndex), static_cast<UINT>(primitiveCount));
        }
        else
        {
            // REMED-GFX-060: honor DrawPrimitives vertexStart (was hardcoded StartVertex=0).
            device_->DrawPrimitive(ToD3D9Topology(primitive), static_cast<UINT>(params.vertexStart),
                                   static_cast<UINT>(primitiveCount));
        }
    }
}
