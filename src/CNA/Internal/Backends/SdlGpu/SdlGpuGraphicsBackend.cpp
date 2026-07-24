// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.hpp"

#include "CNA/Logger.hpp"
#include "CNA/LogCategory.hpp"
#include "CNA/Internal/Backends/SdlGpu/shaders/spirv_shaders.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Backends::SdlGpu
{
    namespace
    {
        // Mirrors WebGPUGraphicsBackend::SamplerCacheIndex's exact indexing scheme so both
        // backends' sampler caches read the same way: filterIndex*9 + u*3 + v, 18 entries total.
        [[nodiscard]] int SamplerCacheIndex(int filter, int addressU, int addressV)
        {
            const int filterIndex = filter == 0 ? 0 : 1;
            const int u = std::clamp(addressU, 0, 2);
            const int v = std::clamp(addressV, 0, 2);
            return filterIndex * 9 + u * 3 + v;
        }

        [[nodiscard]] SDL_GPUSamplerAddressMode ToAddressMode(int mode)
        {
            switch (mode)
            {
                case 0: return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
                case 2: return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
                default: return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            }
        }

        // Mirrors VulkanGraphicsBackend::ToVkCompareOp's exact XNA CompareFunction ordinal table:
        // Always=0, Never=1, Less=2, LessEqual=3, Equal=4, GreaterEqual=5, Greater=6, NotEqual=7.
        [[nodiscard]] SDL_GPUCompareOp ToCompareOp(int xnaCompare)
        {
            switch (xnaCompare)
            {
                case 1: return SDL_GPU_COMPAREOP_NEVER;
                case 2: return SDL_GPU_COMPAREOP_LESS;
                case 3: return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
                case 4: return SDL_GPU_COMPAREOP_EQUAL;
                case 5: return SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
                case 6: return SDL_GPU_COMPAREOP_GREATER;
                case 7: return SDL_GPU_COMPAREOP_NOT_EQUAL;
                default: return SDL_GPU_COMPAREOP_ALWAYS;
            }
        }

        // SDLGPU-18/19/20: XNA StencilOperation ordinals -> SDL_GPUStencilOp (mirrors
        // VulkanGraphicsBackend::ToVkStencilOp/EasyGL's ToEasyGLStencilOp exactly): Keep=0, Zero=1,
        // Replace=2, Increment=3, Decrement=4, IncrementSaturation=5, DecrementSaturation=6, Invert=7.
        [[nodiscard]] SDL_GPUStencilOp ToStencilOp(int xnaOp)
        {
            switch (xnaOp)
            {
                case 1: return SDL_GPU_STENCILOP_ZERO;
                case 2: return SDL_GPU_STENCILOP_REPLACE;
                case 3: return SDL_GPU_STENCILOP_INCREMENT_AND_WRAP;
                case 4: return SDL_GPU_STENCILOP_DECREMENT_AND_WRAP;
                case 5: return SDL_GPU_STENCILOP_INCREMENT_AND_CLAMP;
                case 6: return SDL_GPU_STENCILOP_DECREMENT_AND_CLAMP;
                case 7: return SDL_GPU_STENCILOP_INVERT;
                default: return SDL_GPU_STENCILOP_KEEP;
            }
        }

        // SDLGPU-18: XNA Blend ordinals -> SDL_GPUBlendFactor (mirrors VulkanGraphicsBackend::
        // ToVkBlendFactor/EasyGL's ToEasyGLBlendFactor exactly): One=0, Zero=1, SourceColor=2,
        // InverseSourceColor=3, SourceAlpha=4, InverseSourceAlpha=5, DestinationColor=6,
        // InverseDestinationColor=7, DestinationAlpha=8, InverseDestinationAlpha=9, BlendFactor=10,
        // InverseBlendFactor=11, SourceAlphaSaturation=12.
        [[nodiscard]] SDL_GPUBlendFactor ToBlendFactor(int xnaBlend)
        {
            switch (xnaBlend)
            {
                case  1: return SDL_GPU_BLENDFACTOR_ZERO;
                case  2: return SDL_GPU_BLENDFACTOR_SRC_COLOR;
                case  3: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
                case  4: return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
                case  5: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                case  6: return SDL_GPU_BLENDFACTOR_DST_COLOR;
                case  7: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
                case  8: return SDL_GPU_BLENDFACTOR_DST_ALPHA;
                case  9: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
                case 10: return SDL_GPU_BLENDFACTOR_CONSTANT_COLOR;
                case 11: return SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR;
                case 12: return SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE;
                default: return SDL_GPU_BLENDFACTOR_ONE;
            }
        }

        // SDLGPU-18: XNA BlendFunction ordinals -> SDL_GPUBlendOp: Add=0, Subtract=1,
        // ReverseSubtract=2, Max=3, Min=4.
        [[nodiscard]] SDL_GPUBlendOp ToBlendOp(int xnaBlendFunc)
        {
            switch (xnaBlendFunc)
            {
                case 1: return SDL_GPU_BLENDOP_SUBTRACT;
                case 2: return SDL_GPU_BLENDOP_REVERSE_SUBTRACT;
                case 3: return SDL_GPU_BLENDOP_MAX;
                case 4: return SDL_GPU_BLENDOP_MIN;
                default: return SDL_GPU_BLENDOP_ADD;
            }
        }

        // SDLGPU-20: XNA CullMode ordinals -> SDL_GPUCullMode. Every pipeline in this backend uses
        // front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE (matches this project's own EasyGL
        // backend's real, hardware-validated "OpenGL default front face is CCW; CW faces are back
        // faces" convention -- confirmed this session that SDL_GPU's 3D shaders, like EasyGL's,
        // need no NDC Y-flip, so no Vulkan-style winding-reversal adjustment applies here): None=0,
        // CullClockwiseFace=1 (cull the CW/back faces), CullCounterClockwiseFace=2 (cull the
        // CCW/front faces).
        [[nodiscard]] SDL_GPUCullMode ToCullMode(int xnaCullMode)
        {
            switch (xnaCullMode)
            {
                case 1: return SDL_GPU_CULLMODE_BACK;
                case 2: return SDL_GPU_CULLMODE_FRONT;
                default: return SDL_GPU_CULLMODE_NONE;
            }
        }

        // boost::hash_combine's well-known mixing formula -- used to fold every render-state
        // dimension (SDLGPU-18/19/20) into one pipeline cache key without hand-packing bit ranges
        // per field (which VulkanGraphicsBackend's own PackBlendBits/PackDepthStencilBits comments
        // note it outgrew once every dimension was added -- this sidesteps that entirely).
        [[nodiscard]] std::size_t HashCombine(std::size_t seed, std::size_t value)
        {
            return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
        }

        // Packs (topology, depthTest, depthWrite, depthFunc, colorFormat, sampleCount, full
        // RenderStateSnapshot) into one cache key. Disabled dimensions (blend off, stencil off,
        // two-sided off) always collapse their own sub-fields out of the hash regardless of what
        // they're set to, so different "irrelevant" values don't create duplicate pipelines --
        // mirrors VulkanGraphicsBackend::PackBlendBits's identical "collapse to 0 when disabled" rule.
        // sampleCount (SDLGPU-38's MSAA fix) is a REQUIRED dimension, not an optional one to
        // collapse -- a pipeline created with the wrong sample_count for its render pass's actual
        // attachments is exactly finding #1 of the adversarial review this fixes.
        [[nodiscard]] std::size_t PipelineCacheKey(SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
                                                    SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
                                                    const SdlGpuGraphicsBackend::RenderStateSnapshot& rs)
        {
            std::size_t key = static_cast<std::size_t>(topology);
            key = HashCombine(key, depthTest ? 1u : 0u);
            key = HashCombine(key, depthWrite ? 1u : 0u);
            key = HashCombine(key, static_cast<std::size_t>(depthFunc));
            key = HashCombine(key, static_cast<std::size_t>(colorFormat));
            key = HashCombine(key, static_cast<std::size_t>(sampleCount));
            key = HashCombine(key, rs.blendEnabled ? 1u : 0u);
            if (rs.blendEnabled)
            {
                key = HashCombine(key, static_cast<std::size_t>(rs.blend.colorSrc));
                key = HashCombine(key, static_cast<std::size_t>(rs.blend.colorDst));
                key = HashCombine(key, static_cast<std::size_t>(rs.blend.alphaSrc));
                key = HashCombine(key, static_cast<std::size_t>(rs.blend.alphaDst));
                key = HashCombine(key, static_cast<std::size_t>(rs.blend.colorFunc));
                key = HashCombine(key, static_cast<std::size_t>(rs.blend.alphaFunc));
            }
            // REMED-GFX-077: the colour write mask is static pipeline state (applies to opaque
            // draws too), so it participates in the key. The default All(15) is a fixed contribution
            // ⇒ no cache fragmentation for the common case.
            key = HashCombine(key, static_cast<std::size_t>(rs.colorWriteMask & 0xF));
            key = HashCombine(key, static_cast<std::size_t>(rs.cullMode));
            key = HashCombine(key, rs.wireframe ? 1u : 0u);
            key = HashCombine(key, rs.stencil.enable ? 1u : 0u);
            if (rs.stencil.enable)
            {
                key = HashCombine(key, static_cast<std::size_t>(rs.stencil.func));
                key = HashCombine(key, static_cast<std::size_t>(rs.stencil.fail));
                key = HashCombine(key, static_cast<std::size_t>(rs.stencil.depthFail));
                key = HashCombine(key, static_cast<std::size_t>(rs.stencil.pass));
                // Truncated to the Uint8 range actually applied (FillDepthStencilState), so two
                // int values that truncate to the same byte don't fragment the pipeline cache.
                key = HashCombine(key, static_cast<std::size_t>(static_cast<Uint8>(rs.stencil.readMask)));
                key = HashCombine(key, static_cast<std::size_t>(static_cast<Uint8>(rs.stencil.writeMask)));
                key = HashCombine(key, rs.stencil.twoSided ? 1u : 0u);
                if (rs.stencil.twoSided)
                {
                    key = HashCombine(key, static_cast<std::size_t>(rs.stencil.ccwFunc));
                    key = HashCombine(key, static_cast<std::size_t>(rs.stencil.ccwFail));
                    key = HashCombine(key, static_cast<std::size_t>(rs.stencil.ccwDepthFail));
                    key = HashCombine(key, static_cast<std::size_t>(rs.stencil.ccwPass));
                }
            }
            return key;
        }

        // Fills a color target's real blend factors/op from a RenderStateSnapshot -- shared by
        // every pipeline-creation function so the exact same XNA->SDL_gpu mapping is used
        // everywhere (mirrors VulkanGraphicsBackend::FillBlendAttachmentState's identical role).
        void FillBlendState(SDL_GPUColorTargetBlendState& out, const SdlGpuGraphicsBackend::RenderStateSnapshot& rs)
        {
            out.enable_blend = rs.blendEnabled;
            if (rs.blendEnabled)
            {
                out.src_color_blendfactor = ToBlendFactor(rs.blend.colorSrc);
                out.dst_color_blendfactor = ToBlendFactor(rs.blend.colorDst);
                out.color_blend_op        = ToBlendOp(rs.blend.colorFunc);
                out.src_alpha_blendfactor = ToBlendFactor(rs.blend.alphaSrc);
                out.dst_alpha_blendfactor = ToBlendFactor(rs.blend.alphaDst);
                out.alpha_blend_op        = ToBlendOp(rs.blend.alphaFunc);
            }
            // REMED-GFX-077: BlendState.ColorWriteChannels. XNA bits (R=1,G=2,B=4,A=8) are identical
            // to SDL_GPU_COLORCOMPONENT_* (1<<0..1<<3). SDL writes all channels when
            // enable_color_write_mask is false, so only enable it for a non-All mask (keeps the
            // common default byte-identical to before).
            const Uint8 mask = static_cast<Uint8>(rs.colorWriteMask & 0xF);
            if (mask != 0xF)
            {
                out.enable_color_write_mask = true;
                out.color_write_mask = mask;
            }
        }

        // Fills a pipeline's real front/back stencil-op state (SDLGPU-19) from a
        // RenderStateSnapshot, on top of the depthTest/depthWrite/depthFunc every pipeline already
        // threads through separately. XNA's TwoSidedStencilMode=false uses the SAME (front,
        // clockwise) ops/func for both faces (matches this project's own EasyGL/Vulkan backends'
        // identical fallback-to-front convention -- FNA's own real behavior: the CCW fields are
        // simply ignored when this is false, not reset to any default).
        void FillDepthStencilState(SDL_GPUDepthStencilState& out, bool depthTest, bool depthWrite, int depthFunc,
                                    const SdlGpuGraphicsBackend::RenderStateSnapshot& rs)
        {
            out.enable_depth_test = depthTest;
            out.enable_depth_write = depthWrite;
            out.compare_op = ToCompareOp(depthFunc);
            out.enable_stencil_test = rs.stencil.enable;
            // DepthStencilState.StencilMask/StencilWriteMask -- real values, not hardcoded 0xFF.
            out.compare_mask = static_cast<Uint8>(rs.stencil.readMask);
            out.write_mask = static_cast<Uint8>(rs.stencil.writeMask);
            out.front_stencil_state.fail_op = ToStencilOp(rs.stencil.fail);
            out.front_stencil_state.pass_op = ToStencilOp(rs.stencil.pass);
            out.front_stencil_state.depth_fail_op = ToStencilOp(rs.stencil.depthFail);
            out.front_stencil_state.compare_op = ToCompareOp(rs.stencil.func);
            if (rs.stencil.twoSided)
            {
                out.back_stencil_state.fail_op = ToStencilOp(rs.stencil.ccwFail);
                out.back_stencil_state.pass_op = ToStencilOp(rs.stencil.ccwPass);
                out.back_stencil_state.depth_fail_op = ToStencilOp(rs.stencil.ccwDepthFail);
                out.back_stencil_state.compare_op = ToCompareOp(rs.stencil.ccwFunc);
            }
            else
            {
                out.back_stencil_state = out.front_stencil_state;
            }
        }

        // Fills a pipeline's real cull/fill-mode rasterizer state (SDLGPU-20) from a
        // RenderStateSnapshot. front_face stays hardcoded COUNTER_CLOCKWISE everywhere in this
        // backend (see ToCullMode's own doc comment).
        void FillRasterizerState(SDL_GPURasterizerState& out, const SdlGpuGraphicsBackend::RenderStateSnapshot& rs)
        {
            out.fill_mode = rs.wireframe ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
            out.cull_mode = ToCullMode(rs.cullMode);
            out.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        }

        // Mirrors VulkanGraphicsBackend's CalculateVulkanRTMipLevels / Texture2D.cpp's
        // CalculateMipLevels -- each backend keeps its own copy of this small helper.
        [[nodiscard]] int CalculateMipLevels(int w, int h)
        {
            int levels = 1;
            while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++levels; }
            return levels;
        }

        // SDLGPU-36: clamps an XNA multiSampleCount request down to the largest SDL_gpu sample
        // count this device/format actually supports, mirroring D3D12RenderTargetCubeBackend's own
        // ClampMultiSampleCount() convention (XNA's RenderTargetCube.MultiSampleCount is documented
        // to reflect the real clamped value, not the raw constructor request).
        [[nodiscard]] SDL_GPUSampleCount ClampSampleCount(SDL_GPUDevice* device, SDL_GPUTextureFormat format, int requested)
        {
            if (requested <= 1)
                return SDL_GPU_SAMPLECOUNT_1;
            SDL_GPUSampleCount candidate = requested >= 8 ? SDL_GPU_SAMPLECOUNT_8
                                          : requested >= 4 ? SDL_GPU_SAMPLECOUNT_4
                                                            : SDL_GPU_SAMPLECOUNT_2;
            while (candidate != SDL_GPU_SAMPLECOUNT_1 && !SDL_GPUTextureSupportsSampleCount(device, format, candidate))
            {
                candidate = candidate == SDL_GPU_SAMPLECOUNT_8 ? SDL_GPU_SAMPLECOUNT_4
                          : candidate == SDL_GPU_SAMPLECOUNT_4 ? SDL_GPU_SAMPLECOUNT_2
                                                                : SDL_GPU_SAMPLECOUNT_1;
            }
            return candidate;
        }

        [[nodiscard]] int SampleCountToInt(SDL_GPUSampleCount count)
        {
            switch (count)
            {
                case SDL_GPU_SAMPLECOUNT_2: return 2;
                case SDL_GPU_SAMPLECOUNT_4: return 4;
                case SDL_GPU_SAMPLECOUNT_8: return 8;
                default: return 0;
            }
        }

        // Mirrors VulkanGraphicsBackend::FillExtPushConst()'s 128-byte layout byte-for-byte
        // (DrawColoredPrimitives()'s hardcoded white/vertex-color-always-true behaviour).
        void FillColoredUniforms(std::array<float, 32>& out, const Matrix& world, const Matrix& view,
                                 const Matrix& projection)
        {
            const Matrix wvp = world * view * projection;
            wvp.ToColumnMajor(out.data());
            out[16] = 1.0f; out[17] = 1.0f; out[18] = 1.0f; out[19] = 1.0f;
            for (int i = 20; i < 31; ++i) out[i] = 0.0f;
            out[31] = 1.0f;
        }

        // Mirrors VulkanGraphicsBackend::FillExtPushConst()/WebGPUGraphicsBackend::FillExtUniforms()
        // field-for-field -- real GpuDrawParams, used by DrawPrimitivesEx()'s dispatch.
        void FillExtUniforms(std::array<float, 32>& out, const Matrix& wvp, const GpuDrawParams& p)
        {
            wvp.ToColumnMajor(out.data());
            out[16] = p.diffuseColor[0]; out[17] = p.diffuseColor[1];
            out[18] = p.diffuseColor[2]; out[19] = p.diffuseColor[3];
            out[20] = p.ambientColor[0]; out[21] = p.ambientColor[1]; out[22] = p.ambientColor[2];
            out[23] = p.lightingEnabled ? 1.0f : 0.0f;
            out[24] = p.light0Dir[0]; out[25] = p.light0Dir[1]; out[26] = p.light0Dir[2];
            out[27] = p.textureEnabled ? 1.0f : 0.0f;
            out[28] = p.light0Diffuse[0]; out[29] = p.light0Diffuse[1]; out[30] = p.light0Diffuse[2];
            out[31] = p.vertexColorEnabled ? 1.0f : 0.0f;
        }

        // REMED-GFX-009/-010: shared 32-byte FogParams block, byte-identical to
        // VulkanGraphicsBackend's own FogParams shape -- vec4 fogColorEnabled (xyz = FogColor, w =
        // fogEnabled 0/1) + vec4 fogVector (REMED-GFX-010 FNA view-space fog vector). Bound to the
        // vertex stage only; the vertex shader computes keep = 1 - saturate(dot(vec4(pos,1),
        // fogVector)) -- object-space position for non-skinned, POST-skin for skinned -- and forwards
        // FogColor + keep to the fragment stage as a varying. A default-constructed all-zero block
        // means no fog (dot -> 0 -> keep 1) -- correct for the no-GpuDrawParams FillColoredUniforms
        // path (DrawColoredPrimitives), which never sets fog.
        void FillFogUniforms(std::array<float, 8>& out, const GpuDrawParams& p)
        {
            out[0] = p.fogColor[0]; out[1] = p.fogColor[1]; out[2] = p.fogColor[2];
            out[3] = p.fogEnabled ? 1.0f : 0.0f;
            // REMED-GFX-010: FNA fog vector (bakes World*View + fogStart/fogEnd; zero when disabled).
            out[4] = p.fogVector[0]; out[5] = p.fogVector[1];
            out[6] = p.fogVector[2]; out[7] = p.fogVector[3];
        }

        // Secondary UBO for lit_textured3d.glsl: DirectionalLight1/DirectionalLight2, EmissiveColor,
        // World (the vertex shader computes its own normal matrix via GLSL's built-in inverse(),
        // unlike WebGPUGraphicsBackend's WGSL-forced CPU-side precomputation -- no normal-matrix
        // slots needed here), EyePosition, per-light SpecularColor, material SpecularColor/Power.
        // Mirrors VulkanGraphicsBackend's LitLightParams UBO field-for-field. Fog is not carried
        // here -- REMED-GFX-009 supplies it in a separate FogParams block (FillFogUniforms).
        void FillLitLightUniforms(std::array<float, 56>& out, const GpuDrawParams& p)
        {
            out[0] = p.light1Dir[0]; out[1] = p.light1Dir[1]; out[2] = p.light1Dir[2]; out[3] = 0.0f;
            out[4] = p.light1Diffuse[0]; out[5] = p.light1Diffuse[1]; out[6] = p.light1Diffuse[2]; out[7] = 0.0f;
            out[8] = p.light2Dir[0]; out[9] = p.light2Dir[1]; out[10] = p.light2Dir[2]; out[11] = 0.0f;
            out[12] = p.light2Diffuse[0]; out[13] = p.light2Diffuse[1]; out[14] = p.light2Diffuse[2]; out[15] = 0.0f;
            out[16] = p.emissiveColor[0]; out[17] = p.emissiveColor[1]; out[18] = p.emissiveColor[2]; out[19] = 0.0f;
            for (int wi = 0; wi < 16; ++wi) out[20 + wi] = p.worldColMajor[wi];
            out[36] = p.eyePositionWorld[0]; out[37] = p.eyePositionWorld[1]; out[38] = p.eyePositionWorld[2]; out[39] = 0.0f;
            out[40] = p.light0Specular[0]; out[41] = p.light0Specular[1]; out[42] = p.light0Specular[2]; out[43] = 0.0f;
            out[44] = p.light1Specular[0]; out[45] = p.light1Specular[1]; out[46] = p.light1Specular[2]; out[47] = 0.0f;
            out[48] = p.light2Specular[0]; out[49] = p.light2Specular[1]; out[50] = p.light2Specular[2]; out[51] = 0.0f;
            out[52] = p.specularColor[0]; out[53] = p.specularColor[1]; out[54] = p.specularColor[2]; out[55] = p.specularPower;
        }

        // pbr3d.frag.glsl's tertiary PbrParams block: MetallicFactor, RoughnessFactor, 2 floats
        // of padding (not consumed by anything, kept purely for std140 vec4 alignment symmetry
        // with every other uniform block in this backend).
        void FillPbrParams(std::array<float, 4>& out, const GpuDrawParams& p)
        {
            out[0] = p.pbrMetallicFactor;
            out[1] = p.pbrRoughnessFactor;
            out[2] = 0.0f;
            out[3] = 0.0f;
        }

        // Mirrors VulkanGraphicsBackend::FillAlphaTestPushConst()/WebGPUGraphicsBackend::
        // FillAlphaTestUniforms() field-for-field (minus fog): [20..23]=alphaTest params
        // (refVal, tolerance, passWeight, failWeight), [24]=vertexColorEnabled -- the
        // ambient/light0/textureEnabled slots FillExtUniforms uses are repurposed since
        // AlphaTestEffect has no lighting.
        void FillAlphaTestUniforms(std::array<float, 32>& out, const Matrix& wvp, const GpuDrawParams& p)
        {
            wvp.ToColumnMajor(out.data());
            out[16] = p.diffuseColor[0]; out[17] = p.diffuseColor[1];
            out[18] = p.diffuseColor[2]; out[19] = p.diffuseColor[3];
            out[20] = p.alphaTest[0]; out[21] = p.alphaTest[1];
            out[22] = p.alphaTest[2]; out[23] = p.alphaTest[3];
            out[24] = p.vertexColorEnabled ? 1.0f : 0.0f;
            for (int i = 25; i < 32; ++i) out[i] = 0.0f;
        }

        // env_map3d.glsl's primary PC block: mvp(16) + diffuseColor(4) + emissiveAmount(4) = 24
        // floats. EnvironmentMapEffect::FillGpuDrawParams() already pre-sums emissive+ambient*diffuse
        // and pre-multiplies diffuseColor/emissiveColor by Alpha, so no extra alpha handling needed.
        void FillEnvMapUniforms(std::array<float, 24>& out, const Matrix& wvp, const GpuDrawParams& p)
        {
            wvp.ToColumnMajor(out.data());
            out[16] = p.diffuseColor[0]; out[17] = p.diffuseColor[1];
            out[18] = p.diffuseColor[2]; out[19] = p.diffuseColor[3];
            out[20] = p.emissiveColor[0]; out[21] = p.emissiveColor[1];
            out[22] = p.emissiveColor[2]; out[23] = p.envMapAmount;
        }

        // env_map3d.glsl's secondary EnvMapParams block: world(16) + 8 vec4 (32) = 48 floats.
        // Mirrors VulkanGraphicsBackend::env_map3d's EnvMapParams field-for-field. Fog is not
        // carried here -- REMED-GFX-009 supplies it via a separate FogParams block.
        void FillEnvMapParams(std::array<float, 48>& out, const GpuDrawParams& p)
        {
            for (int wi = 0; wi < 16; ++wi) out[wi] = p.worldColMajor[wi];
            out[16] = p.eyePositionWorld[0]; out[17] = p.eyePositionWorld[1];
            out[18] = p.eyePositionWorld[2]; out[19] = p.fresnelEnabled ? 1.0f : 0.0f;
            out[20] = p.light0Dir[0]; out[21] = p.light0Dir[1];
            out[22] = p.light0Dir[2]; out[23] = p.fresnelFactor;
            out[24] = p.light0Diffuse[0]; out[25] = p.light0Diffuse[1]; out[26] = p.light0Diffuse[2]; out[27] = 0.0f;
            out[28] = p.light1Dir[0]; out[29] = p.light1Dir[1]; out[30] = p.light1Dir[2]; out[31] = 0.0f;
            out[32] = p.light1Diffuse[0]; out[33] = p.light1Diffuse[1]; out[34] = p.light1Diffuse[2]; out[35] = 0.0f;
            out[36] = p.light2Dir[0]; out[37] = p.light2Dir[1]; out[38] = p.light2Dir[2]; out[39] = 0.0f;
            out[40] = p.light2Diffuse[0]; out[41] = p.light2Diffuse[1]; out[42] = p.light2Diffuse[2]; out[43] = 0.0f;
            out[44] = p.envMapSpecular[0]; out[45] = p.envMapSpecular[1]; out[46] = p.envMapSpecular[2]; out[47] = 0.0f;
        }

        // skinned3d.vert.glsl's SkinnedLightParams block -- byte-identical layout to
        // FillLitLightUniforms()'s LitLightParams, with WeightsPerVertex packed into the
        // eyePos_weightsPerVertex.w slot FillLitLightUniforms leaves as pad (mirrors
        // VulkanGraphicsBackend's own skinned3d.vert.glsl packing convention). This identical
        // layout is exactly why skinned3d's fragment stage can reuse lit_textured3d's fragment
        // shader unchanged.
        void FillSkinnedLightUniforms(std::array<float, 56>& out, const GpuDrawParams& p)
        {
            FillLitLightUniforms(out, p);
            out[39] = static_cast<float>(p.weightsPerVertex);
        }

        // skinned3d.vert.glsl's BoneBlock: 72 mat4 = 1152 floats (4608 bytes), column-major,
        // straight from GpuDrawParams::boneTransforms (already column-major per
        // SkinnedEffect::FillGpuDrawParams()).
        void FillSkinnedBoneUniforms(std::array<float, 72 * 16>& out, const GpuDrawParams& p)
        {
            const int count = std::min(p.boneCount, 72);
            for (int i = 0; i < count * 16; ++i)
                out[i] = p.boneTransforms[i];
            for (int i = count * 16; i < 72 * 16; ++i)
                out[i] = 0.0f;
        }

        // ---- Runtime GLSL->SPIR-V compile for SdlGpuEffectBackend (SDLGPU-42/43) ----
        // No libshaderc-dev package is available in this environment (see CMakeLists.txt's own
        // find_library fallback comment), so there is no shaderc.h to include -- these extern "C"
        // prototypes are hand-declared to match the real C ABI exactly, the same minimal subset
        // compile_shaders.py's own ctypes bindings already prove correct against the identical
        // shared library at build time. Opaque handles are all void* (matches ctypes.c_void_p);
        // shaderc_shader_kind/shaderc_optimization_level are plain C enums, passed as int.
        extern "C"
        {
            void* shaderc_compiler_initialize();
            void shaderc_compiler_release(void*);
            void* shaderc_compile_options_initialize();
            void shaderc_compile_options_release(void*);
            void shaderc_compile_options_set_optimization_level(void*, int);
            void* shaderc_compile_into_spv(void* compiler, const char* source_text, std::size_t source_text_size,
                                          int shader_kind, const char* input_file_name,
                                          const char* entry_point_name, void* options);
            int shaderc_result_get_compilation_status(void*);
            const char* shaderc_result_get_error_message(void*);
            std::size_t shaderc_result_get_length(void*);
            const char* shaderc_result_get_bytes(void*);
            void shaderc_result_release(void*);
        }

        constexpr int kShadercVertexShader = 0;    // shaderc_glsl_vertex_shader
        constexpr int kShadercFragmentShader = 1;  // shaderc_glsl_fragment_shader
        constexpr int kShadercOptPerformance = 2;  // shaderc_optimization_level_performance

        // Compiles @p source (GLSL) to SPIR-V, appending the raw bytes to @p outSpirv. Returns
        // true on success; on failure, @p outError holds shaderc's own error message and
        // @p outSpirv is left untouched.
        bool CompileGlslToSpirv(const std::string& source, int shaderKind, const char* filename,
                                std::vector<std::uint8_t>& outSpirv, std::string& outError)
        {
            void* compiler = shaderc_compiler_initialize();
            void* options = shaderc_compile_options_initialize();
            shaderc_compile_options_set_optimization_level(options, kShadercOptPerformance);

            void* result = shaderc_compile_into_spv(compiler, source.data(), source.size(), shaderKind,
                                                    filename, "main", options);

            const int status = shaderc_result_get_compilation_status(result);
            if (status != 0)
            {
                const char* err = shaderc_result_get_error_message(result);
                outError = err != nullptr ? err : "shader compilation failed (no error message)";
                shaderc_result_release(result);
                shaderc_compile_options_release(options);
                shaderc_compiler_release(compiler);
                return false;
            }

            const std::size_t length = shaderc_result_get_length(result);
            const char* bytes = shaderc_result_get_bytes(result);
            outSpirv.assign(bytes, bytes + length);

            shaderc_result_release(result);
            shaderc_compile_options_release(options);
            shaderc_compiler_release(compiler);
            return true;
        }
    }

    SdlGpuGraphicsBackend::SdlGpuGraphicsBackend(SDL_Window* window, int virtualWidth, int virtualHeight,
                                                  CnaPresentationMode presentationMode, int swapInterval)
        : window_(window),
          virtualWidth_(virtualWidth),
          virtualHeight_(virtualHeight),
          presentationMode_(presentationMode)
    {
        if (window_ == nullptr)
            throw std::invalid_argument("CNA SDL_GPU: SDL window cannot be null");

        // plan_sdlgpu.md SDLGPU-6: request SPIR-V first -- the only shader format this device's
        // vendored SDL3 compiles a driver for on Linux (Vulkan). DXBC/DXIL/MSL support (Windows/
        // macOS drivers) is deferred to plan_sdlgpu.md's Phase SDLGPU-13.
        // debug_mode mirrors D3D11GraphicsBackend::CreateDeviceResources()'s own #ifndef NDEBUG
        // CNA-side toggle (design decision 12: the validation/debug layer is a debug-build
        // convenience, never a hard requirement) -- a debug build asks the Vulkan driver for
        // SDL_gpu's own validation layer, a release build does not.
#ifndef NDEBUG
        debugModeEnabled_ = true;
#else
        debugModeEnabled_ = false;
#endif
        device_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, debugModeEnabled_, /*name=*/nullptr);
        if (device_ == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: SDL_CreateGPUDevice failed: ") + SDL_GetError());

        if (!SDL_ClaimWindowForGPUDevice(device_, window_))
        {
            const std::string error = SDL_GetError();
            SDL_DestroyGPUDevice(device_);
            device_ = nullptr;
            throw std::runtime_error("CNA SDL_GPU: SDL_ClaimWindowForGPUDevice failed: " + error);
        }

        SetSwapInterval(swapInterval);
        QueryDepthStencilFormat();
        CreateSpriteResources();
        CreateColoredResources();
        CreateTexturedResources();
        CreateLitTexturedResources();
        CreateAlphaTestResources();
        CreateDualTextureResources();
        CreateEnvMapResources();
        CreateSkinnedResources();
        CreatePbrResources();

        int w = 0;
        int h = 0;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        physicalWidth_ = w;
        physicalHeight_ = h;

        IGraphicsBackend::RegisterForWindow(window_, this);

        SDL_Log("[SDL_GPU] Backend initialised (%dx%d), debug mode %s",
                physicalWidth_, physicalHeight_, debugModeEnabled_ ? "enabled" : "disabled");
    }

    SdlGpuGraphicsBackend::~SdlGpuGraphicsBackend()
    {
        IGraphicsBackend::UnregisterForWindow(window_);
        ReleaseSceneDrawBuffers();
        DestroyPbrResources();
        DestroySkinnedResources();
        DestroyEnvMapResources();
        DestroyDualTextureResources();
        DestroyAlphaTestResources();
        DestroyLitTexturedResources();
        DestroyTexturedResources();
        DestroyColoredResources();
        DestroySpriteResources();
        // Any render target destroyed earlier but never followed by another real frame (e.g. the
        // game shut down right after) leaves its GPU texture handles deferred -- release them now
        // rather than relying solely on SDL_DestroyGPUDevice's own implicit cleanup below.
        for (SDL_GPUTexture* texture : pendingTextureReleases_)
            SDL_ReleaseGPUTexture(device_, texture);
        pendingTextureReleases_.clear();
        if (depthStencilTexture_ != nullptr)
            SDL_ReleaseGPUTexture(device_, depthStencilTexture_);
        if (device_ != nullptr)
        {
            SDL_ReleaseWindowFromGPUDevice(device_, window_);
            SDL_DestroyGPUDevice(device_);
        }
    }

    void SdlGpuGraphicsBackend::QueryDepthStencilFormat()
    {
        // plan_sdlgpu.md: SDL_gpu guarantees at most one of D24_UNORM_S8_UINT/D32_FLOAT_S8_UINT
        // per device -- must query, never assume either is available. Queried once here (not
        // lazily inside EnsureDepthStencilTexture) so pipeline creation has a stable answer for
        // SDL_GPUGraphicsPipelineTargetInfo before any frame has actually rendered.
        if (SDL_GPUTextureSupportsFormat(device_, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
                                          SDL_GPU_TEXTURETYPE_2D,
                                          SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
        {
            depthStencilFormat_ = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
        }
        else if (SDL_GPUTextureSupportsFormat(device_, SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
                                               SDL_GPU_TEXTURETYPE_2D,
                                               SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
        {
            depthStencilFormat_ = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
        }
        else
        {
            // Genuine SDL_gpu/device capability gap, not a "not implemented yet" stub -- warn
            // and keep running with no depth/stencil attachment rather than throw.
            CNA::Logger::Warn(
                "CNA SDL_GPU: no combined depth+stencil texture format is supported by this "
                "device; depth/stencil clearing and depth-tested draws will have no effect.",
                CNA::LogCategory::GPU);
        }
    }

    void SdlGpuGraphicsBackend::QueueTextureRelease(SDL_GPUTexture* texture)
    {
        if (texture != nullptr)
            pendingTextureReleases_.push_back(texture);
    }

    void SdlGpuGraphicsBackend::EnsureDepthStencilTexture(Uint32 width, Uint32 height)
    {
        if (width == 0 || height == 0 || depthStencilFormat_ == SDL_GPU_TEXTUREFORMAT_INVALID)
            return;

        if (depthStencilTexture_ != nullptr && depthStencilWidth_ == width && depthStencilHeight_ == height)
            return;

        if (depthStencilTexture_ != nullptr)
        {
            SDL_ReleaseGPUTexture(device_, depthStencilTexture_);
            depthStencilTexture_ = nullptr;
        }

        SDL_GPUTextureCreateInfo createInfo{};
        createInfo.type = SDL_GPU_TEXTURETYPE_2D;
        createInfo.format = depthStencilFormat_;
        createInfo.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        createInfo.width = width;
        createInfo.height = height;
        createInfo.layer_count_or_depth = 1;
        createInfo.num_levels = 1;
        createInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;

        depthStencilTexture_ = SDL_CreateGPUTexture(device_, &createInfo);
        if (depthStencilTexture_ == nullptr)
        {
            CNA::Logger::Warn(
                std::string("CNA SDL_GPU: failed to create depth/stencil texture: ") + SDL_GetError(),
                CNA::LogCategory::GPU);
            depthStencilWidth_ = 0;
            depthStencilHeight_ = 0;
            return;
        }
        depthStencilWidth_ = width;
        depthStencilHeight_ = height;
    }

    bool SdlGpuGraphicsBackend::EnsureFrameRendered()
    {
        if (!framePending_)
            return true;

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
        if (cmd == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: SDL_AcquireGPUCommandBuffer failed: ") + SDL_GetError());

        SDL_GPUTexture* swapchainTexture = nullptr;
        Uint32 swapchainWidth = 0;
        Uint32 swapchainHeight = 0;
        const bool acquired = SDL_WaitAndAcquireGPUSwapchainTexture(
            cmd, window_, &swapchainTexture, &swapchainWidth, &swapchainHeight);
        if (!acquired)
        {
            // Per SDL_gpu.h: it is an error to cancel a command buffer once
            // SDL_WaitAndAcquireGPUSwapchainTexture has been called on it -- must always submit.
            const std::string error = SDL_GetError();
            SDL_SubmitGPUCommandBuffer(cmd);
            throw std::runtime_error("CNA SDL_GPU: SDL_WaitAndAcquireGPUSwapchainTexture failed: " + error);
        }

        if (swapchainTexture == nullptr)
        {
            // Documented, non-error case (e.g. a minimized window) -- still must submit.
            SDL_SubmitGPUCommandBuffer(cmd);
            return false;
        }

        physicalWidth_ = static_cast<int>(swapchainWidth);
        physicalHeight_ = static_cast<int>(swapchainHeight);
        EnsureDepthStencilTexture(swapchainWidth, swapchainHeight);

        // Sprite/3D vertex data must be uploaded via a copy pass BEFORE BeginGPURenderPass --
        // SDL_gpu forbids a copy pass nested inside a render pass. Covers every target's draws
        // regardless of which render pass below will consume them.
        UploadSpriteVertexData(cmd);
        UploadSceneDrawData(cmd);

        // Phase SDLGPU-8: every off-screen render target (2D and cube face) used this frame gets
        // its own pass FIRST, in first-bind order, so a target bound-then-unbound earlier in the
        // frame can safely be sampled by a later swapchain-targeted draw within the same frame
        // (SDLGPU-35's own "bound in one pass, sampled in a later pass" contract). A target with
        // isMrtSibling set is rendered as an extra attachment of its primary's own multi-attachment
        // pass below, not as its own separate pass (SDLGPU-37: real MRT).
        for (const auto& target : usedRenderTargetsThisFrame_)
        {
            if (target->isMrtSibling)
                continue;
            RenderToTarget(cmd, target);
        }
        for (auto& [cube, face] : usedRenderTargetCubeFacesThisFrame_)
            RenderToTargetCubeFace(cmd, cube, face);

        SDL_GPUColorTargetInfo colorTarget{};
        colorTarget.texture = swapchainTexture;
        colorTarget.clear_color = clearColor_;
        colorTarget.load_op = clearColorPending_ ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        colorTarget.store_op = SDL_GPU_STOREOP_STORE;

        SDL_GPUDepthStencilTargetInfo depthStencilTarget{};
        if (depthStencilTexture_ != nullptr)
        {
            depthStencilTarget.texture = depthStencilTexture_;
            depthStencilTarget.clear_depth = clearDepth_;
            depthStencilTarget.load_op = clearDepthPending_ ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            depthStencilTarget.store_op = SDL_GPU_STOREOP_STORE;
            depthStencilTarget.clear_stencil = clearStencil_;
            depthStencilTarget.stencil_load_op = clearStencilPending_ ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            depthStencilTarget.stencil_store_op = SDL_GPU_STOREOP_STORE;
        }

        const SDL_GPUTextureFormat swapchainFormat = SDL_GetGPUSwapchainTextureFormat(device_, window_);
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(
            cmd, &colorTarget, 1, depthStencilTexture_ != nullptr ? &depthStencilTarget : nullptr);
        // REMED-GFX-068: the scissor (like the viewport, REMED-GFX-064) is applied PER DRAW inside
        // RenderQueuedDraws from each queued draw's own captured state, not once per pass here -- a
        // per-pass read of the live scissor would apply the post-unbind full-backbuffer rect (see
        // ApplyScissorForRef / QueuedDrawRef). SDL sets a default full-target scissor at pass begin,
        // which each draw's ApplyScissorForRef then overrides.
        // Real chronological draw order (adversarial-review finding #4) -- see drawOrder_'s own
        // doc comment; replaces the old fixed "all 3D families, then all sprites" sequence.
        const DrawTarget swapchainTarget{};
        // The swapchain surface is never MSAA in this backend -- SDL_GPU_SAMPLECOUNT_1 always.
        RenderQueuedDraws(pass, cmd, swapchainTarget, swapchainFormat, SDL_GPU_SAMPLECOUNT_1);
        SDL_EndGPURenderPass(pass);

        // Cube mip regen is real GPU work -- must happen on this command buffer BEFORE submission
        // (per SDL_gpu.h: SDL_GenerateMipmapsForGPUTexture must not be called inside any pass, but
        // is otherwise fine any time before submit). No per-layer control on this call -- it
        // regenerates all 6 faces' chains; harmless for a face untouched this frame (same
        // unchanged level-0 data produces an identical result).
        {
            std::vector<SdlGpuRenderTargetCubeState*> mipRegenerated;
            for (auto& [cube, face] : usedRenderTargetCubeFacesThisFrame_)
            {
                if (cube->mipMap && std::find(mipRegenerated.begin(), mipRegenerated.end(), cube.get()) == mipRegenerated.end())
                {
                    SDL_GenerateMipmapsForGPUTexture(cmd, cube->cubeTexture);
                    mipRegenerated.push_back(cube.get());
                }
            }
        }

        if (!SDL_SubmitGPUCommandBuffer(cmd))
            throw std::runtime_error(std::string("CNA SDL_GPU: SDL_SubmitGPUCommandBuffer failed: ") + SDL_GetError());

        // Whatever was pending has now been handed to the GPU (recorded into a submitted command
        // buffer) -- any render target destroyed earlier this frame can have its own GPU texture
        // handles safely released now (see QueueTextureRelease's own doc comment). SDL_gpu's own
        // "release as soon as safe" internal fencing takes it from here.
        for (SDL_GPUTexture* texture : pendingTextureReleases_)
            SDL_ReleaseGPUTexture(device_, texture);
        pendingTextureReleases_.clear();

        ReleaseSceneDrawBuffers();

        spriteCommands_.clear();
        drawOrder_.clear();
        clearColorPending_ = false;
        clearDepthPending_ = false;
        clearStencilPending_ = false;
        for (auto& target : usedRenderTargetsThisFrame_)
        {
            target->clearColorPending = false;
            target->clearDepthPending = false;
            target->clearStencilPending = false;
        }
        usedRenderTargetsThisFrame_.clear();
        for (auto& [cube, face] : usedRenderTargetCubeFacesThisFrame_)
        {
            cube->clearColorPending[face] = false;
            cube->clearDepthPending[face] = false;
            cube->clearStencilPending[face] = false;
        }
        usedRenderTargetCubeFacesThisFrame_.clear();
        framePending_ = false;
        return true;
    }

    namespace
    {
        // Shared by RenderToTarget's primary + each SDLGPU-37 MRT sibling -- fills one
        // SDL_GPUColorTargetInfo entry from a render-target state exactly like the single-target
        // case always did.
        void FillColorTargetInfo(SDL_GPUColorTargetInfo& out, const SdlGpuRenderTarget2DState& state)
        {
            out.texture = state.msaaTexture != nullptr ? state.msaaTexture : state.colorTexture;
            out.clear_color = state.clearColor;
            out.load_op = state.clearColorPending ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            if (state.msaaTexture != nullptr)
            {
                out.resolve_texture = state.colorTexture;
                out.store_op = SDL_GPU_STOREOP_RESOLVE;
            }
            else
            {
                out.store_op = SDL_GPU_STOREOP_STORE;
            }
        }
    }

    void SdlGpuGraphicsBackend::RenderToTarget(SDL_GPUCommandBuffer* cmd, const std::shared_ptr<SdlGpuRenderTarget2DState>& target)
    {
        constexpr SDL_GPUTextureFormat kRenderTargetFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

        // SDLGPU-37: real MRT -- target->mrtSiblings is non-empty only when this target is the
        // primary (rts[0]) of the most recent SetRenderTargets(count>1) call, in which case this
        // one render pass gets 1+mrtSiblings.size() real simultaneous color attachments, not just
        // this target's own.
        std::vector<SDL_GPUColorTargetInfo> colorTargets(1 + target->mrtSiblings.size());
        FillColorTargetInfo(colorTargets[0], *target);
        for (std::size_t i = 0; i < target->mrtSiblings.size(); ++i)
            FillColorTargetInfo(colorTargets[i + 1], *target->mrtSiblings[i]);
        const int colorTargetCount = static_cast<int>(colorTargets.size());

        SDL_GPUDepthStencilTargetInfo depthStencilTarget{};
        const bool hasDepth = target->depthTexture != nullptr;
        if (hasDepth)
        {
            depthStencilTarget.texture = target->depthTexture;
            depthStencilTarget.clear_depth = target->clearDepth;
            depthStencilTarget.load_op = target->clearDepthPending ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            depthStencilTarget.store_op = SDL_GPU_STOREOP_STORE;
            depthStencilTarget.clear_stencil = target->clearStencil;
            depthStencilTarget.stencil_load_op = target->clearStencilPending ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            depthStencilTarget.stencil_store_op = SDL_GPU_STOREOP_STORE;
        }

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, colorTargets.data(), static_cast<Uint32>(colorTargetCount),
                                                          hasDepth ? &depthStencilTarget : nullptr);
        // REMED-GFX-068: scissor applied per draw in RenderQueuedDraws (see the swapchain pass note).
        const DrawTarget dt{target.get(), nullptr, -1};
        RenderQueuedDraws(pass, cmd, dt, kRenderTargetFormat, target->sampleCount, colorTargetCount);
        SDL_EndGPURenderPass(pass);

        // Per SDL_gpu.h: SDL_GenerateMipmapsForGPUTexture must not be called inside any pass --
        // matches FNA3D's OPENGL_ResolveTarget semantics (mip chain regenerated once this target's
        // contents are final for the frame).
        if (target->mipMap)
            SDL_GenerateMipmapsForGPUTexture(cmd, target->colorTexture);
        for (const auto& sibling : target->mrtSiblings)
            if (sibling->mipMap)
                SDL_GenerateMipmapsForGPUTexture(cmd, sibling->colorTexture);
    }

    void SdlGpuGraphicsBackend::RenderToTargetCubeFace(SDL_GPUCommandBuffer* cmd, const std::shared_ptr<SdlGpuRenderTargetCubeState>& cube, int face)
    {
        constexpr SDL_GPUTextureFormat kRenderTargetFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

        SDL_GPUColorTargetInfo colorTarget{};
        if (cube->msaaTexture != nullptr)
        {
            // msaaTexture is a single-layer 2D texture shared across faces (see its own doc
            // comment on SdlGpuRenderTargetCubeState for why) -- layer_or_depth_plane 0, not face.
            colorTarget.texture = cube->msaaTexture;
            colorTarget.layer_or_depth_plane = 0;
        }
        else
        {
            colorTarget.texture = cube->cubeTexture;
            colorTarget.layer_or_depth_plane = static_cast<Uint32>(face);
        }
        colorTarget.clear_color = cube->clearColor[face];
        colorTarget.load_op = cube->clearColorPending[face] ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        // msaaTexture is reused across every face's own pass this frame (it's a disposable scratch
        // resource, immediately resolved away each time) -- SDL_gpu's own doc comment on this cycle
        // flag says reusing a bound resource across passes without cycling "will produce unexpected
        // results" (this was the real cause of a genuine Vulkan validation layout-hazard error found
        // 2026-07-16 once SDLGPU-6 turned debug_mode on for real). cubeTexture must NEVER cycle here
        // (whether as the direct target below or as resolve_texture above) -- cycling wipes the
        // ENTIRE persistent 6-layer resource, including every other face already written this frame.
        colorTarget.cycle = (cube->msaaTexture != nullptr);
        if (cube->msaaTexture != nullptr)
        {
            // Automatic render-pass-end resolve -- SDL_gpu has no multisampled cube texture type,
            // so the (single-layer, shared) MSAA color target resolves directly into the active
            // face of the real (single-sample, 6-layer) cube texture.
            colorTarget.resolve_texture = cube->cubeTexture;
            colorTarget.resolve_layer = static_cast<Uint32>(face);
            colorTarget.store_op = SDL_GPU_STOREOP_RESOLVE;
        }
        else
        {
            colorTarget.store_op = SDL_GPU_STOREOP_STORE;
        }

        SDL_GPUDepthStencilTargetInfo depthStencilTarget{};
        const bool hasDepth = cube->depthTexture != nullptr;
        if (hasDepth)
        {
            depthStencilTarget.texture = cube->depthTexture;
            depthStencilTarget.clear_depth = cube->clearDepth[face];
            depthStencilTarget.load_op = cube->clearDepthPending[face] ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            depthStencilTarget.store_op = SDL_GPU_STOREOP_STORE;
            depthStencilTarget.clear_stencil = cube->clearStencil[face];
            depthStencilTarget.stencil_load_op = cube->clearStencilPending[face] ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            depthStencilTarget.stencil_store_op = SDL_GPU_STOREOP_STORE;
        }

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &colorTarget, 1, hasDepth ? &depthStencilTarget : nullptr);
        // REMED-GFX-068: scissor applied per draw in RenderQueuedDraws (see the swapchain pass note).
        const DrawTarget dt{nullptr, cube.get(), face};
        RenderQueuedDraws(pass, cmd, dt, kRenderTargetFormat, cube->sampleCount);
        SDL_EndGPURenderPass(pass);
    }

    void SdlGpuGraphicsBackend::Clear(float r, float g, float b, float a)
    {
        if (currentRenderTargetCube_ != nullptr)
        {
            currentRenderTargetCube_->QueueClear(currentActiveCubeFace_, SDL_FColor{r, g, b, a});
        }
        else if (currentRenderTarget_ != nullptr)
        {
            currentRenderTarget_->QueueClear(SDL_FColor{r, g, b, a});
            for (SdlGpuRenderTargetBackend* extra : currentExtraMrtTargets_)
                extra->QueueClear(SDL_FColor{r, g, b, a});
        }
        else
        {
            clearColor_ = SDL_FColor{r, g, b, a};
            clearColorPending_ = true;
        }
        framePending_ = true;
    }

    void SdlGpuGraphicsBackend::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        Clear(r, g, b, a);
        ClearDepth(depth);
    }

    void SdlGpuGraphicsBackend::ClearDepth(float depth)
    {
        if (currentRenderTargetCube_ != nullptr)
        {
            currentRenderTargetCube_->QueueClearDepth(currentActiveCubeFace_, depth);
        }
        else if (currentRenderTarget_ != nullptr)
        {
            currentRenderTarget_->QueueClearDepth(depth);
            for (SdlGpuRenderTargetBackend* extra : currentExtraMrtTargets_)
                extra->QueueClearDepth(depth);
        }
        else
        {
            clearDepth_ = depth;
            clearDepthPending_ = true;
        }
        framePending_ = true;
    }

    void SdlGpuGraphicsBackend::ClearStencil(int stencil)
    {
        if (currentRenderTargetCube_ != nullptr)
        {
            currentRenderTargetCube_->QueueClearStencil(currentActiveCubeFace_, static_cast<Uint8>(stencil));
        }
        else if (currentRenderTarget_ != nullptr)
        {
            currentRenderTarget_->QueueClearStencil(static_cast<Uint8>(stencil));
            for (SdlGpuRenderTargetBackend* extra : currentExtraMrtTargets_)
                extra->QueueClearStencil(static_cast<Uint8>(stencil));
        }
        else
        {
            clearStencil_ = static_cast<Uint8>(stencil);
            clearStencilPending_ = true;
        }
        framePending_ = true;
    }

    SdlGpuGraphicsBackend::DrawTarget SdlGpuGraphicsBackend::CurrentDrawTarget() const
    {
        if (currentRenderTargetCube_ != nullptr)
            return DrawTarget{nullptr, currentRenderTargetCube_->State().get(), currentActiveCubeFace_};
        if (currentRenderTarget_ != nullptr)
            return DrawTarget{currentRenderTarget_->State().get(), nullptr, -1};
        return DrawTarget{};
    }

    void SdlGpuGraphicsBackend::ClearDepthAndStencil(float depth, int stencil)
    {
        ClearDepth(depth);
        ClearStencil(stencil);
    }

    void SdlGpuGraphicsBackend::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        Clear(r, g, b, a);
        ClearStencil(stencil);
    }

    void SdlGpuGraphicsBackend::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil)
    {
        Clear(r, g, b, a);
        ClearDepth(depth);
        ClearStencil(stencil);
    }

    void SdlGpuGraphicsBackend::Present()
    {
        // SDL_gpu automatically presents the acquired swapchain texture once the command buffer
        // that acquired it is submitted -- there is no separate explicit present call.
        EnsureFrameRendered();
    }

    SdlGpuGraphicsBackend::LogicalViewport SdlGpuGraphicsBackend::ComputeLogicalViewport() const
    {
        LogicalViewport viewport{};
        viewport.width = static_cast<float>(std::max(0, physicalWidth_));
        viewport.height = static_cast<float>(std::max(0, physicalHeight_));
        viewport.logicalWidth = viewport.width;
        viewport.logicalHeight = viewport.height;
        if (physicalWidth_ <= 0 || physicalHeight_ <= 0)
            return viewport;
        if (presentationMode_ == CnaPresentationMode::NativeBackBuffer || virtualWidth_ <= 0 || virtualHeight_ <= 0)
            return viewport;

        float logicalWidth = static_cast<float>(virtualWidth_);
        float logicalHeight = static_cast<float>(virtualHeight_);
        if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth)
        {
            logicalHeight = static_cast<float>(virtualHeight_);
            logicalWidth = logicalHeight * static_cast<float>(physicalWidth_) / static_cast<float>(physicalHeight_);
            viewport.logicalWidth = logicalWidth;
            viewport.logicalHeight = logicalHeight;
            return viewport;
        }

        viewport.logicalWidth = logicalWidth;
        viewport.logicalHeight = logicalHeight;
        if (presentationMode_ == CnaPresentationMode::Stretch)
            return viewport;
        const float sx = static_cast<float>(physicalWidth_) / logicalWidth;
        const float sy = static_cast<float>(physicalHeight_) / logicalHeight;
        const float scale = presentationMode_ == CnaPresentationMode::Overscan ? std::max(sx, sy) : std::min(sx, sy);
        viewport.width = logicalWidth * scale;
        viewport.height = logicalHeight * scale;
        viewport.x = (static_cast<float>(physicalWidth_) - viewport.width) * 0.5f;
        viewport.y = (static_cast<float>(physicalHeight_) - viewport.height) * 0.5f;
        return viewport;
    }

    void SdlGpuGraphicsBackend::GetViewportSize(int& width, int& height)
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        width = static_cast<int>(std::lround(viewport.logicalWidth));
        height = static_cast<int>(std::lround(viewport.logicalHeight));
    }

    void SdlGpuGraphicsBackend::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void SdlGpuGraphicsBackend::SetPresentationMode(int mode)
    {
        if (mode < static_cast<int>(CnaPresentationMode::Letterbox) ||
            mode > static_cast<int>(CnaPresentationMode::FixedHeightDynamicWidth))
            throw std::out_of_range("CNA SDL_GPU: invalid presentation mode");
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    void SdlGpuGraphicsBackend::SetSwapInterval(int interval)
    {
        interval = std::max(0, interval);
        swapInterval_ = interval;

        // plan_sdlgpu.md: SDL_gpu has no "half-rate" present mode -- XNA's PresentInterval.Two
        // (swapInterval==2) falls back to plain VSYNC, same as swapInterval==1.
        SDL_GPUPresentMode presentMode = SDL_GPU_PRESENTMODE_VSYNC;
        if (interval == 0)
        {
            if (SDL_WindowSupportsGPUPresentMode(device_, window_, SDL_GPU_PRESENTMODE_IMMEDIATE))
                presentMode = SDL_GPU_PRESENTMODE_IMMEDIATE;
            else if (SDL_WindowSupportsGPUPresentMode(device_, window_, SDL_GPU_PRESENTMODE_MAILBOX))
                presentMode = SDL_GPU_PRESENTMODE_MAILBOX;
        }

        if (!SDL_SetGPUSwapchainParameters(device_, window_, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, presentMode))
        {
            // Genuine per-device/driver capability gap, not a "not implemented yet" stub.
            CNA::Logger::Warn(
                std::string("CNA SDL_GPU: SDL_SetGPUSwapchainParameters failed: ") + SDL_GetError(),
                CNA::LogCategory::GPU);
        }
    }

    bool SdlGpuGraphicsBackend::TransformWindowToLogical(float windowX, float windowY, float& logicalX, float& logicalY) const
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.width == 0.0f || viewport.height == 0.0f)
            return false;
        logicalX = (windowX - viewport.x) * viewport.logicalWidth / viewport.width;
        logicalY = (windowY - viewport.y) * viewport.logicalHeight / viewport.height;
        return windowX >= viewport.x && windowX < viewport.x + viewport.width &&
               windowY >= viewport.y && windowY < viewport.y + viewport.height;
    }

    bool SdlGpuGraphicsBackend::TransformLogicalToWindow(float logicalX, float logicalY, float& windowX, float& windowY) const
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.logicalWidth == 0.0f || viewport.logicalHeight == 0.0f)
            return false;
        windowX = viewport.x + logicalX * viewport.width / viewport.logicalWidth;
        windowY = viewport.y + logicalY * viewport.height / viewport.logicalHeight;
        return true;
    }

    std::unique_ptr<ITextureBackend> SdlGpuGraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<SdlGpuTextureBackend>(*this, data);
    }

    std::unique_ptr<ISpriteBatchBackend> SdlGpuGraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<SdlGpuSpriteBatchBackend>(*this);
    }

    void SdlGpuGraphicsBackend::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                                int colorDstBlend, int alphaDstBlend,
                                                int colorBlendFunc, int alphaBlendFunc,
                                                const BlendWriteState& writeState)
    {
        // Blend::One=0, Blend::Zero=1 -> Opaque preset: src=One, dst=Zero -> no blending. Matches
        // VulkanGraphicsBackend::ApplyBlendState's own derivation exactly.
        blendEnabled_ = !(colorSrcBlend == 0 && colorDstBlend == 1 &&
                          alphaSrcBlend == 0 && alphaDstBlend == 1);
        blendParams_.colorSrc  = colorSrcBlend;
        blendParams_.colorDst  = colorDstBlend;
        blendParams_.alphaSrc  = alphaSrcBlend;
        blendParams_.alphaDst  = alphaDstBlend;
        blendParams_.colorFunc = colorBlendFunc;
        blendParams_.alphaFunc = alphaBlendFunc;
        // REMED-GFX-077: BlendState.ColorWriteChannels (slot 0) is baked into the color target's
        // SDL_GPUColorTargetBlendState (static → part of the pipeline cache key; see FillBlendState
        // + PipelineCacheKey). BlendState.MultiSampleMask is NOT supported: SDL 3.5.0 documents
        // SDL_GPUMultisampleState::sample_mask / enable_mask as "Reserved for future use, must be
        // set to 0 / false" — a genuine backend capability gap (REMED-GFX-086), not a silent drop.
        colorWriteMask_ = writeState.colorWriteChannels[0];
    }

    void SdlGpuGraphicsBackend::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                                       bool stencilEnable, int stencilFunc,
                                                       int stencilPass, int stencilFail, int stencilDepthFail,
                                                       int stencilMask, int stencilWriteMask, int referenceStencil,
                                                       bool twoSidedStencilMode,
                                                       int ccwStencilFunc, int ccwStencilPass,
                                                       int ccwStencilFail, int ccwStencilDepthFail)
    {
        depthTestEnabled_  = depthEnable;
        depthWriteEnabled_ = depthWriteEnable;
        depthCompareFunction_ = depthFunc;
        stencilParams_.enable       = stencilEnable;
        stencilParams_.func         = stencilFunc;
        stencilParams_.fail         = stencilFail;
        stencilParams_.depthFail    = stencilDepthFail;
        stencilParams_.pass         = stencilPass;
        stencilParams_.twoSided     = twoSidedStencilMode;
        stencilParams_.ccwFunc      = ccwStencilFunc;
        stencilParams_.ccwFail      = ccwStencilFail;
        stencilParams_.ccwDepthFail = ccwStencilDepthFail;
        stencilParams_.ccwPass      = ccwStencilPass;
        stencilParams_.readMask  = stencilMask;
        stencilParams_.writeMask = stencilWriteMask;
        // FNA applies a DepthStencilState's own ReferenceStencil atomically as part of the whole
        // native state struct (matches GraphicsDevice::setDepthStencilStateProperty's own
        // "keep GraphicsDevice.ReferenceStencil in sync" comment) -- SetReferenceStencil() is the
        // single place that actually stores referenceStencil_, reused here too.
        SetReferenceStencil(referenceStencil);
    }

    void SdlGpuGraphicsBackend::ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                                     float depthBias, float slopeScaleDepthBias)
    {
        cullMode_ = cullMode;
        fillModeWireframe_ = (fillMode == 1);  // XNA FillMode::WireFrame = 1
        scissorEnabled_ = scissorTestEnable;
        // Stored but deliberately not yet applied -- see this method's own header doc comment
        // (SDL_gpu has no per-draw-dynamic depth-bias equivalent to Vulkan's vkCmdSetDepthBias).
        depthBias_ = depthBias;
        slopeScaleDepthBias_ = slopeScaleDepthBias;
    }

    void SdlGpuGraphicsBackend::SetBlendFactor(float r, float g, float b, float a)
    {
        // REMED-GFX-069: store only. The value is snapshotted per draw into each QueuedDrawRef at
        // Queue*Draw()/QueueSprite() time (PushDrawOrder) and applied at Present-time replay
        // (ApplyBlendFactorForRef) -- never applied here, mirroring how every other per-command
        // state (viewport, scissor rect, stencil reference) is captured on this deferred backend.
        blendFactorR_ = r;
        blendFactorG_ = g;
        blendFactorB_ = b;
        blendFactorA_ = a;
    }

    void SdlGpuGraphicsBackend::SetReferenceStencil(int value)
    {
        referenceStencil_ = value;
    }

    void SdlGpuGraphicsBackend::SetScissorRect(int x, int y, int w, int h)
    {
        scissorX_ = x;
        scissorY_ = y;
        scissorW_ = w;
        scissorH_ = h;
    }

    void SdlGpuGraphicsBackend::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        // REMED-GFX-064: store only. The value is snapshotted per draw into each QueuedDrawRef at
        // Queue*Draw()/QueueSprite() time (PushDrawOrder) and applied at Present-time replay
        // (ApplyViewportForRef) -- never read back here, mirroring how every other per-command
        // state (scissor rect, blend, stencil) is captured on this deferred backend.
        viewportSet_ = true;
        viewportX_ = x;
        viewportY_ = y;
        viewportW_ = w;
        viewportH_ = h;
        viewportMinDepth_ = minDepth;
        viewportMaxDepth_ = maxDepth;
    }

    void SdlGpuGraphicsBackend::ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy)
    {
        if (slot < 0 || slot >= static_cast<int>(samplerSlots_.size()))
            return;
        samplerSlots_[slot].filter = filter;
        samplerSlots_[slot].addressU = addressU;
        samplerSlots_[slot].addressV = addressV;
        samplerSlots_[slot].maxAnisotropy = maxAnisotropy;
    }

    void SdlGpuGraphicsBackend::ApplyScissorForRef(SDL_GPURenderPass* pass, const QueuedDrawRef& ref,
                                                   int targetWidth, int targetHeight) const
    {
        SDL_Rect rect{};
        if (ref.scissorEnabled && ref.scissorW > 0 && ref.scissorH > 0)
        {
            // A scissor is a CLIP (unlike the viewport, which is an NDC->framebuffer transform), so
            // clamp the captured rectangle to the target's physical extent in 64-bit space -- this
            // keeps the SDL_Rect within [0,target] with a non-negative size even for a rect that
            // begins outside or overhangs the target edge (SDL's Vulkan/Metal/D3D12 scissor VUIDs
            // require offset >= 0 and offset+extent within the framebuffer). Mirrors
            // VulkanGraphicsBackend::computeScissor (REMED-GFX-013). SDL_Rect is top-left origin,
            // matching XNA ScissorRectangle directly -- no Y-flip.
            const int64_t x0 = std::clamp<int64_t>(ref.scissorX, 0, targetWidth);
            const int64_t y0 = std::clamp<int64_t>(ref.scissorY, 0, targetHeight);
            const int64_t x1 = std::clamp<int64_t>(static_cast<int64_t>(ref.scissorX) + ref.scissorW, 0, targetWidth);
            const int64_t y1 = std::clamp<int64_t>(static_cast<int64_t>(ref.scissorY) + ref.scissorH, 0, targetHeight);
            rect.x = static_cast<int>(x0);
            rect.y = static_cast<int>(y0);
            rect.w = static_cast<int>(x1 > x0 ? x1 - x0 : 0);
            rect.h = static_cast<int>(y1 > y0 ? y1 - y0 : 0);
        }
        else
        {
            // Disabled (or a not-yet-set, zero-size) scissor rect means "no clipping" -- use the
            // full render target/swapchain extents, matching VulkanGraphicsBackend's own
            // scissorEnabled_-gated fallback-to-full-target convention.
            rect.x = 0;
            rect.y = 0;
            rect.w = targetWidth;
            rect.h = targetHeight;
        }
        SDL_SetGPUScissor(pass, &rect);
    }

    void SdlGpuGraphicsBackend::ApplyBlendFactorForRef(SDL_GPURenderPass* pass, const QueuedDrawRef& ref) const
    {
        // REMED-GFX-069: the constant blend color for the Blend::BlendFactor/InverseBlendFactor
        // modes (mapped to SDL_GPU_BLENDFACTOR_CONSTANT_COLOR / ONE_MINUS_CONSTANT_COLOR in
        // FillBlendState). SDL_FColor's channels are the same normalized [0,1] linear factors that
        // GraphicsDevice::setBlendFactorProperty produced (Color byte / 255) -- direct R/G/B/A,
        // no channel swap, no premultiply, no sRGB transfer (a numeric pipeline factor, not a
        // framebuffer color). The alpha channel feeds the alpha blend factor slot when
        // Alpha{Source,Destination}Blend uses BlendFactor (CONSTANT_COLOR uses .a for the alpha
        // component). Applied unconditionally: it is inert for pipelines whose blend factors don't
        // reference the constant, so no gating on the current BlendState is required -- the same
        // "always set, harmlessly overwritten before the next real draw" property as the viewport
        // and scissor.
        const SDL_FColor bc{ref.blendFactorR, ref.blendFactorG, ref.blendFactorB, ref.blendFactorA};
        SDL_SetGPUBlendConstants(pass, bc);
    }

    void SdlGpuGraphicsBackend::PushDrawOrder(DrawKind kind, std::size_t index)
    {
        // REMED-GFX-064/068/069: snapshot the current viewport, scissor AND blend factor into the
        // ref so a later SetRenderTarget reset / SetViewport / SetScissorRect / ApplyRasterizerState
        // / SetBlendFactor change never retroactively alters an already-queued draw's dynamic state.
        QueuedDrawRef ref{kind, index, viewportSet_, viewportX_, viewportY_,
                          viewportW_, viewportH_, viewportMinDepth_, viewportMaxDepth_};
        ref.scissorEnabled = scissorEnabled_;
        ref.scissorX = scissorX_;
        ref.scissorY = scissorY_;
        ref.scissorW = scissorW_;
        ref.scissorH = scissorH_;
        ref.blendFactorR = blendFactorR_;
        ref.blendFactorG = blendFactorG_;
        ref.blendFactorB = blendFactorB_;
        ref.blendFactorA = blendFactorA_;
        drawOrder_.push_back(ref);
    }

    void SdlGpuGraphicsBackend::ApplyViewportForRef(SDL_GPURenderPass* pass, const QueuedDrawRef& ref,
                                                    int targetWidth, int targetHeight) const
    {
        SDL_GPUViewport vp{};
        if (ref.viewportSet && ref.viewportW > 0 && ref.viewportH > 0)
        {
            // Sub-region viewport is a NDC->framebuffer transform, NOT a clip: pass it through
            // unclamped (clamping would distort placement). SDL_GPUViewport.y is top-left origin,
            // matching XNA Viewport.Y directly -- no extra Y-flip (the CNA sprite/3D shaders already
            // handle SDL's clip-space Y, GFX-011-style). Depth range clamped to SDL's [0,1] domain.
            vp.x = static_cast<float>(ref.viewportX);
            vp.y = static_cast<float>(ref.viewportY);
            vp.w = static_cast<float>(ref.viewportW);
            vp.h = static_cast<float>(ref.viewportH);
            vp.min_depth = std::clamp(ref.viewportMinDepth, 0.0f, 1.0f);
            vp.max_depth = std::clamp(ref.viewportMaxDepth, 0.0f, 1.0f);
        }
        else
        {
            // Unset or degenerate viewport => full render target (SDL's own default full-target
            // viewport at pass begin), byte-identical to the pre-REMED-GFX-064 behavior.
            vp.x = 0.0f;
            vp.y = 0.0f;
            vp.w = static_cast<float>(targetWidth);
            vp.h = static_cast<float>(targetHeight);
            vp.min_depth = 0.0f;
            vp.max_depth = 1.0f;
        }
        SDL_SetGPUViewport(pass, &vp);
    }

    SdlGpuGraphicsBackend::RenderStateSnapshot SdlGpuGraphicsBackend::CaptureRenderState() const
    {
        RenderStateSnapshot rs;
        rs.blendEnabled = blendEnabled_;
        rs.blend = blendParams_;
        rs.colorWriteMask = colorWriteMask_; // REMED-GFX-077
        rs.cullMode = cullMode_;
        rs.wireframe = fillModeWireframe_;
        rs.stencil = stencilParams_;
        rs.stencilReference = referenceStencil_;
        return rs;
    }

    std::unique_ptr<IVertexBufferBackend> SdlGpuGraphicsBackend::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<SdlGpuVertexBufferBackend>(*this, vertex_capacity);
    }

    std::unique_ptr<IIndexBufferBackend> SdlGpuGraphicsBackend::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<SdlGpuIndexBufferBackend>(*this, index_capacity, false);
    }

    std::unique_ptr<IRenderTargetBackend> SdlGpuGraphicsBackend::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool /*preserveContents*/, bool mipMap, int multiSampleCount)
    {
        return std::make_unique<SdlGpuRenderTargetBackend>(*this, w, h, depthFormat, mipMap, multiSampleCount);
    }

    void SdlGpuGraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* rt)
    {
        if (rt != nullptr)
        {
            auto* backend = static_cast<SdlGpuRenderTargetBackend*>(rt);
            backend->BindAsRenderTarget();
            // A stale mrtSiblings/isMrtSibling from an earlier SetRenderTargets(count>1) call
            // (this same target as either a primary or an extra) must not silently carry over into
            // a plain single-target bind -- SetRenderTargets() itself repopulates these right after
            // this call when count>1, so this reset is always safe to do unconditionally here.
            backend->State()->isMrtSibling = false;
            backend->State()->mrtSiblings.clear();
        }
        else
        {
            // Restoring the swapchain must clear whichever kind of target was previously bound --
            // 2D and cube-face binding are mutually exclusive (see BindAsRenderTarget/
            // BindAsRenderTargetFace, which each clear the other's current-target pointer too).
            if (currentRenderTarget_ != nullptr) currentRenderTarget_->UnbindAsRenderTarget();
            if (currentRenderTargetCube_ != nullptr) currentRenderTargetCube_->UnbindAsRenderTarget();
        }
    }

    std::unique_ptr<IRenderTargetCubeBackend> SdlGpuGraphicsBackend::CreateRenderTargetCube(
        int size, int depthFormat, bool mipMap, int multiSampleCount)
    {
        return std::make_unique<SdlGpuRenderTargetCubeBackend>(*this, size, depthFormat, mipMap, multiSampleCount);
    }

    std::unique_ptr<ITexture3DBackend> SdlGpuGraphicsBackend::CreateTexture3D(
        int w, int h, int depth, bool mipMap, int /*surfaceFormat*/)
    {
        return std::make_unique<SdlGpuTexture3DBackend>(*this, w, h, depth, mipMap);
    }

    std::unique_ptr<ITextureCubeBackend> SdlGpuGraphicsBackend::CreateTextureCube(
        int size, bool mipMap, int /*surfaceFormat*/)
    {
        return std::make_unique<SdlGpuTextureCubeBackend>(*this, size, mipMap);
    }

    void SdlGpuGraphicsBackend::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        currentExtraMrtTargets_.clear();
        if (count <= 0 || renderTargets == nullptr)
        {
            SetRenderTarget2D(nullptr);
            return;
        }
        if (count == 1 && renderTargets[0].IsRenderTargetCubeFace())
        {
            SetRenderTargetCubeFace(
                renderTargets[0].GetRenderTargetCube(),
                renderTargets[0].GetCubeFace());
            return;
        }
        for (int i = 0; i < count; ++i)
            if (renderTargets[i].IsRenderTargetCubeFace())
                throw std::runtime_error(
                    "SdlGpuGraphicsBackend::SetRenderTargets: cube faces in a multi-target "
                    "set are not implemented by this CNA backend.");

        // Stock (single-output) draws remain single-target (rts[0] only) -- no stock shader family
        // in this codebase declares more than one fragment output, matching this project's
        // D3D11/D3D12 stock-effect behavior. A custom multi-output ShaderEffect drawn as a sprite
        // WHILE this binding is active genuinely renders into all `count` targets simultaneously,
        // in one real render pass (see RenderToTarget's own mrtSiblings-driven multi-attachment
        // build) -- extra targets are no longer just Clear()-only placeholders.
        SetRenderTarget2D(renderTargets[0].GetRenderTarget2D());
        auto* primary = static_cast<SdlGpuRenderTargetBackend*>(
            renderTargets[0].GetRenderTarget2D());
        for (int i = 1; i < count; ++i)
        {
            auto* extra = static_cast<SdlGpuRenderTargetBackend*>(
                renderTargets[i].GetRenderTarget2D());
            extra->MarkUsedThisFrame();
            extra->State()->isMrtSibling = true;
            extra->State()->mrtSiblings.clear();  // an extra doesn't carry its own sibling list
            primary->State()->mrtSiblings.push_back(extra->State());
            currentExtraMrtTargets_.push_back(extra);
        }
    }

    void SdlGpuGraphicsBackend::CreateSpriteResources()
    {
        if (spriteVertexShader_ != nullptr)
            return;

        SDL_GPUShaderCreateInfo vsInfo{};
        vsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kSprite2dVertSpv);
        vsInfo.code_size = Shaders::kSprite2dVertSpv_size;
        vsInfo.entrypoint = "main";
        vsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        vsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        vsInfo.num_uniform_buffers = 1;
        spriteVertexShader_ = SDL_CreateGPUShader(device_, &vsInfo);
        if (spriteVertexShader_ == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create sprite vertex shader: ") + SDL_GetError());

        SDL_GPUShaderCreateInfo fsInfo{};
        fsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kSprite2dFragSpv);
        fsInfo.code_size = Shaders::kSprite2dFragSpv_size;
        fsInfo.entrypoint = "main";
        fsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        fsInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        fsInfo.num_samplers = 1;
        spriteFragmentShader_ = SDL_CreateGPUShader(device_, &fsInfo);
        if (spriteFragmentShader_ == nullptr)
        {
            SDL_ReleaseGPUShader(device_, spriteVertexShader_);
            spriteVertexShader_ = nullptr;
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create sprite fragment shader: ") + SDL_GetError());
        }
    }

    void SdlGpuGraphicsBackend::DestroySpriteResources()
    {
        for (auto& [key, pipeline] : spritePipelines_)
            if (pipeline != nullptr) SDL_ReleaseGPUGraphicsPipeline(device_, pipeline);
        spritePipelines_.clear();
        for (SDL_GPUSampler*& sampler : samplerCache_)
        {
            if (sampler != nullptr)
                SDL_ReleaseGPUSampler(device_, sampler);
            sampler = nullptr;
        }
        if (spriteVertexBuffer_ != nullptr)
        {
            SDL_ReleaseGPUBuffer(device_, spriteVertexBuffer_);
            spriteVertexBuffer_ = nullptr;
        }
        if (spriteFragmentShader_ != nullptr)
        {
            SDL_ReleaseGPUShader(device_, spriteFragmentShader_);
            spriteFragmentShader_ = nullptr;
        }
        if (spriteVertexShader_ != nullptr)
        {
            SDL_ReleaseGPUShader(device_, spriteVertexShader_);
            spriteVertexShader_ = nullptr;
        }
    }

    SDL_GPUGraphicsPipeline* SdlGpuGraphicsBackend::GetOrCreateSpritePipeline(
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount, bool depthTest, bool depthWrite, int depthFunc,
        const RenderStateSnapshot& renderState)
    {
        const std::size_t key = PipelineCacheKey(SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, depthTest, depthWrite, depthFunc,
                                                  colorFormat, sampleCount, renderState);
        const auto it = spritePipelines_.find(key);
        if (it != spritePipelines_.end())
            return it->second;

        SDL_GPUVertexBufferDescription vbDesc{};
        vbDesc.slot = 0;
        vbDesc.pitch = sizeof(SpriteVertex);
        vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        SDL_GPUVertexAttribute attrs[3]{};
        attrs[0].location = 0;
        attrs[0].buffer_slot = 0;
        attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attrs[0].offset = offsetof(SpriteVertex, x);
        attrs[1].location = 1;
        attrs[1].buffer_slot = 0;
        attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attrs[1].offset = offsetof(SpriteVertex, u);
        attrs[2].location = 2;
        attrs[2].buffer_slot = 0;
        attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attrs[2].offset = offsetof(SpriteVertex, r);

        // SDLGPU-18: real BlendState mapping. Note the DEFAULT (BlendState.AlphaBlend, what
        // SpriteBatch.Begin() applies when the game passes no explicit BlendState) is
        // src=SourceAlpha/dst=InverseSourceAlpha/Add for color, src=One/dst=InverseSourceAlpha/Add
        // for alpha -- i.e. exactly the standard non-premultiplied alpha blend this pipeline used
        // to hardcode unconditionally; FillBlendState now derives the same result from real
        // BlendState data instead, and genuinely reflects whatever BlendState is actually current.
        SDL_GPUColorTargetDescription colorTarget{};
        colorTarget.format = colorFormat;
        FillBlendState(colorTarget.blend_state, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = spriteVertexShader_;
        pipelineInfo.fragment_shader = spriteFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = 3;
        pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        FillRasterizerState(pipelineInfo.rasterizer_state, renderState);
        pipelineInfo.multisample_state.sample_count = sampleCount;
        // SDLGPU-19: real DepthStencilState mapping -- SpriteBatch.Begin() defaults to
        // DepthStencilState.None (depth test/write both off) when the game passes no explicit
        // state, matching this pipeline's own former hardcoded always-off behavior, but a game CAN
        // now genuinely enable depth-tested sprite layering by passing a different state.
        FillDepthStencilState(pipelineInfo.depth_stencil_state, depthTest, depthWrite, depthFunc, renderState);
        pipelineInfo.target_info.color_target_descriptions = &colorTarget;
        pipelineInfo.target_info.num_color_targets = 1;
        pipelineInfo.target_info.has_depth_stencil_target = (depthStencilFormat_ != SDL_GPU_TEXTUREFORMAT_INVALID);
        pipelineInfo.target_info.depth_stencil_format = depthStencilFormat_;

        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);
        if (pipeline == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create sprite pipeline: ") + SDL_GetError());
        spritePipelines_[key] = pipeline;
        return pipeline;
    }

    SDL_GPUSampler* SdlGpuGraphicsBackend::GetOrCreateSampler(int textureFilter, int addressU, int addressV)
    {
        const int index = SamplerCacheIndex(textureFilter, addressU, addressV);
        if (samplerCache_[index] != nullptr)
            return samplerCache_[index];

        const SDL_GPUFilter filter = textureFilter == 0 ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
        SDL_GPUSamplerCreateInfo createInfo{};
        createInfo.min_filter = filter;
        createInfo.mag_filter = filter;
        createInfo.mipmap_mode = textureFilter == 0 ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        createInfo.address_mode_u = ToAddressMode(addressU);
        createInfo.address_mode_v = ToAddressMode(addressV);
        createInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        createInfo.max_lod = 32.0f;

        samplerCache_[index] = SDL_CreateGPUSampler(device_, &createInfo);
        if (samplerCache_[index] == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create sampler: ") + SDL_GetError());
        return samplerCache_[index];
    }

    void SdlGpuGraphicsBackend::UploadSpriteVertexData(SDL_GPUCommandBuffer* cmd)
    {
        if (spriteCommands_.empty())
            return;

        const Uint32 requiredBytes = static_cast<Uint32>(spriteCommands_.size() * sizeof(SpriteVertex) * 6);
        if (spriteVertexBuffer_ == nullptr || spriteVertexCapacityBytes_ < requiredBytes)
        {
            if (spriteVertexBuffer_ != nullptr)
                SDL_ReleaseGPUBuffer(device_, spriteVertexBuffer_);
            SDL_GPUBufferCreateInfo bufferInfo{};
            bufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
            bufferInfo.size = requiredBytes;
            spriteVertexBuffer_ = SDL_CreateGPUBuffer(device_, &bufferInfo);
            if (spriteVertexBuffer_ == nullptr)
                throw std::runtime_error(std::string("CNA SDL_GPU: failed to create sprite vertex buffer: ") + SDL_GetError());
            spriteVertexCapacityBytes_ = requiredBytes;
        }

        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = requiredBytes;
        SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(device_, &transferInfo);
        if (transferBuffer == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create sprite transfer buffer: ") + SDL_GetError());

        void* mapped = SDL_MapGPUTransferBuffer(device_, transferBuffer, false);
        if (mapped == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device_, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to map sprite transfer buffer: ") + SDL_GetError());
        }
        auto* dest = static_cast<SpriteVertex*>(mapped);
        for (std::size_t i = 0; i < spriteCommands_.size(); ++i)
            std::memcpy(dest + i * 6, spriteCommands_[i].vertices.data(), sizeof(SpriteVertex) * 6);
        SDL_UnmapGPUTransferBuffer(device_, transferBuffer);

        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation source{};
        source.transfer_buffer = transferBuffer;
        SDL_GPUBufferRegion destRegion{};
        destRegion.buffer = spriteVertexBuffer_;
        destRegion.size = requiredBytes;
        SDL_UploadToGPUBuffer(copyPass, &source, &destRegion, true);
        SDL_EndGPUCopyPass(copyPass);

        SDL_ReleaseGPUTransferBuffer(device_, transferBuffer);
    }

    // Adversarial-review finding #4 (draw ordering): the body of the old whole-vector RenderSprites
    // loop, now issuing exactly ONE sprite so RenderQueuedDraws() can interleave it with any other
    // kind in real chronological order. boundPipeline is passed by reference from the caller's own
    // single running variable, so this still skips a redundant rebind across consecutive
    // same-pipeline sprites (SDLGPU-42/43's own rationale, now also true across kind switches).
    void SdlGpuGraphicsBackend::IssueSpriteDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                const SpriteCommand& command, std::size_t index,
                                                const float* viewportSize, SDL_GPUTextureFormat colorFormat,
                                                SDL_GPUSampleCount sampleCount, int colorTargetCount,
                                                SDL_GPUGraphicsPipeline*& boundPipeline)
    {
        if (command.customEffect != nullptr)
        {
            SDL_GPUGraphicsPipeline* pipeline = command.customEffect->GetOrCreatePipeline(colorFormat, sampleCount, colorTargetCount);
            if (pipeline == nullptr)
                return;  // compile/pipeline-creation failure -- skip, matches IsValid()-gated sibling backends
            if (pipeline != boundPipeline)
            {
                SDL_BindGPUGraphicsPipeline(pass, pipeline);
                boundPipeline = pipeline;
            }
            // vpSize is a render-time fact (this render pass's target size), not a Draw()-time
            // one, so it's stamped into the snapshot here rather than at QueueSprite time.
            std::array<float, 32> uniforms = command.customUniforms;
            uniforms[0] = viewportSize[0];
            uniforms[1] = viewportSize[1];
            SDL_PushGPUVertexUniformData(cmd, 0, uniforms.data(), sizeof(uniforms));
            SDL_PushGPUFragmentUniformData(cmd, 0, uniforms.data(), sizeof(uniforms));
        }
        else
        {
            SDL_GPUGraphicsPipeline* pipeline = GetOrCreateSpritePipeline(
                colorFormat, sampleCount, command.depthTest, command.depthWrite, command.depthFunc, command.renderState);
            if (pipeline != boundPipeline)
            {
                SDL_BindGPUGraphicsPipeline(pass, pipeline);
                boundPipeline = pipeline;
            }
            // A genuine per-draw dynamic value (not pipeline-baked) -- set every sprite
            // regardless of whether the pipeline itself changed (two sprites can share a
            // pipeline yet want different stencil references).
            SDL_SetGPUStencilReference(pass, static_cast<Uint8>(command.renderState.stencilReference));
            SDL_PushGPUVertexUniformData(cmd, 0, viewportSize, 2 * sizeof(float));
        }

        SDL_GPUBufferBinding vbBinding{};
        vbBinding.buffer = spriteVertexBuffer_;
        vbBinding.offset = static_cast<Uint32>(index * sizeof(SpriteVertex) * 6);
        SDL_BindGPUVertexBuffers(pass, 0, &vbBinding, 1);

        SDL_GPUTextureSamplerBinding samplerBinding{};
        samplerBinding.texture = command.texture;
        samplerBinding.sampler = GetOrCreateSampler(command.textureFilter, command.addressU, command.addressV);
        SDL_BindGPUFragmentSamplers(pass, 0, &samplerBinding, 1);

        SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
    }

    void SdlGpuGraphicsBackend::QueueSprite(const ITextureBackend& texture, SDL_GPUTexture* nativeTexture,
                                             const Rectangle& destination,
                                             const Rectangle& source,
                                             const Color& color,
                                             float rotation,
                                             const Vector2& origin,
                                             SpriteEffects effects,
                                             float /*layerDepth*/,
                                             const Matrix& transform,
                                             int textureFilter,
                                             int addressU,
                                             int addressV,
                                             SdlGpuEffectBackend* customEffect)
    {
        if (destination.Width == 0 || destination.Height == 0 || source.Width == 0 || source.Height == 0)
            return;
        // A render-target-bound sprite draws in the target's own 1:1 pixel space -- the swapchain's
        // virtual-resolution letterbox/presentation-mode scaling (ComputeLogicalViewport) only
        // applies when drawing to the actual window.
        LogicalViewport viewport;
        if (currentRenderTarget_ != nullptr)
        {
            viewport.width = viewport.logicalWidth = static_cast<float>(currentRenderTarget_->GetWidth());
            viewport.height = viewport.logicalHeight = static_cast<float>(currentRenderTarget_->GetHeight());
        }
        else
        {
            viewport = ComputeLogicalViewport();
            if (physicalWidth_ <= 0 || physicalHeight_ <= 0)
                return;
        }
        if (viewport.logicalWidth <= 0.0f || viewport.logicalHeight <= 0.0f)
            return;

        // REMED-GFX-072: when a custom sub-Viewport is active, XNA/FNA make SpriteBatch coordinates
        // VIEWPORT-LOCAL -- sprite (0,0) is the viewport's top-left and the projection extent is
        // Viewport.Width/Height (CreateOrthographicOffCenter(0, Viewport.Width, Viewport.Height, 0)).
        // Bake the raw viewport-local pixel coordinates here (no presentation-mode letterbox offset/
        // scale) and let IssueSpriteDraw divide by Viewport.W/H instead of the full target (see
        // RenderQueuedDraws' Sprite case, which reads the same per-draw QueuedDrawRef viewport).
        // Only a genuine sub-region (differs from the physical target extent) overrides -- the
        // default full-target viewport keeps the existing letterbox/1:1 path byte-identical.
        const int spritePhysW = currentRenderTarget_ != nullptr ? currentRenderTarget_->GetWidth()
                                                                 : physicalWidth_;
        const int spritePhysH = currentRenderTarget_ != nullptr ? currentRenderTarget_->GetHeight()
                                                                 : physicalHeight_;
        if (viewportSet_ && viewportW_ > 0 && viewportH_ > 0 &&
            (viewportX_ != 0 || viewportY_ != 0 || viewportW_ != spritePhysW || viewportH_ != spritePhysH))
        {
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = viewport.logicalWidth = 1.0f;   // ratio 1, offset 0 => px = points.X
            viewport.height = viewport.logicalHeight = 1.0f;
        }

        const float scaleX = static_cast<float>(destination.Width) / static_cast<float>(source.Width);
        const float scaleY = static_cast<float>(destination.Height) / static_cast<float>(source.Height);
        const float left = -origin.X * scaleX;
        const float top = -origin.Y * scaleY;
        const float right = left + static_cast<float>(destination.Width);
        const float bottom = top + static_cast<float>(destination.Height);
        std::array<Vector2, 4> points{Vector2{left, top}, Vector2{right, top}, Vector2{left, bottom}, Vector2{right, bottom}};
        const float s = std::sin(rotation);
        const float c = std::cos(rotation);
        for (Vector2& point : points)
        {
            const float rotatedX = point.X * c - point.Y * s + static_cast<float>(destination.X);
            const float rotatedY = point.X * s + point.Y * c + static_cast<float>(destination.Y);
            point.X = rotatedX * transform.M11 + rotatedY * transform.M21 + transform.M41;
            point.Y = rotatedX * transform.M12 + rotatedY * transform.M22 + transform.M42;
        }

        float u0 = static_cast<float>(source.X) / static_cast<float>(texture.GetWidth());
        float v0 = static_cast<float>(source.Y) / static_cast<float>(texture.GetHeight());
        float u1 = static_cast<float>(source.X + source.Width) / static_cast<float>(texture.GetWidth());
        float v1 = static_cast<float>(source.Y + source.Height) / static_cast<float>(texture.GetHeight());
        const int effectBits = static_cast<int>(effects);
        if ((effectBits & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0) std::swap(u0, u1);
        if ((effectBits & static_cast<int>(SpriteEffects::FlipVertically)) != 0) std::swap(v0, v1);
        const std::array<Vector2, 4> uv{Vector2{u0, v0}, Vector2{u1, v0}, Vector2{u0, v1}, Vector2{u1, v1}};
        constexpr int indices[6] = {0, 1, 2, 2, 1, 3};

        SpriteCommand command{};
        command.texture = nativeTexture;
        command.textureFilter = textureFilter;
        command.addressU = addressU;
        command.addressV = addressV;
        command.target = CurrentDrawTarget();
        // SDLGPU-18/19/20: snapshot the current BlendState/DepthStencilState/RasterizerState NOW,
        // matching every 3D Queue*Draw's own identical per-command snapshot convention.
        command.depthTest = depthTestEnabled_;
        command.depthWrite = depthWriteEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.renderState = CaptureRenderState();
        // SDLGPU-42/43: snapshot the custom effect's uniform state NOW, not at Present() time --
        // see SpriteCommand's own doc comment for why.
        if (customEffect != nullptr && customEffect->IsValid())
        {
            command.customEffect = customEffect;
            command.customUniforms = customEffect->SnapshotUniforms();
        }
        const float rgba[4] = {
            static_cast<float>(color.getRProperty()) / 255.0f,
            static_cast<float>(color.getGProperty()) / 255.0f,
            static_cast<float>(color.getBProperty()) / 255.0f,
            static_cast<float>(color.getAProperty()) / 255.0f
        };
        for (int i = 0; i < 6; ++i)
        {
            const int corner = indices[i];
            const float px = viewport.x + points[corner].X * viewport.width / viewport.logicalWidth;
            const float py = viewport.y + points[corner].Y * viewport.height / viewport.logicalHeight;
            SpriteVertex& vertex = command.vertices[static_cast<std::size_t>(i)];
            vertex.x = px;
            vertex.y = py;
            vertex.u = uv[corner].X;
            vertex.v = uv[corner].Y;
            vertex.r = rgba[0];
            vertex.g = rgba[1];
            vertex.b = rgba[2];
            vertex.a = rgba[3];
        }
        spriteCommands_.push_back(command);
        PushDrawOrder(DrawKind::Sprite, spriteCommands_.size() - 1);
        framePending_ = true;
    }

    // ---- Phase SDLGPU-6: colored3d / textured3d / colored_textured3d / lit_textured3d ----

    SDL_GPUPrimitiveType SdlGpuGraphicsBackend::ToTopology(PrimitiveType primitive) const
    {
        switch (primitive)
        {
            case PrimitiveType::TriangleList: return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            case PrimitiveType::TriangleStrip: return SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
            case PrimitiveType::LineList: return SDL_GPU_PRIMITIVETYPE_LINELIST;
            case PrimitiveType::LineStrip: return SDL_GPU_PRIMITIVETYPE_LINESTRIP;
            case PrimitiveType::PointListEXT: return SDL_GPU_PRIMITIVETYPE_POINTLIST;
        }
        throw std::invalid_argument("CNA SDL_GPU: unsupported primitive topology");
    }

    int SdlGpuGraphicsBackend::PrimitiveVertexCount(PrimitiveType primitive, int primitiveCount) const
    {
        switch (primitive)
        {
            case PrimitiveType::TriangleList: return primitiveCount * 3;
            case PrimitiveType::TriangleStrip: return primitiveCount + 2;
            case PrimitiveType::LineList: return primitiveCount * 2;
            case PrimitiveType::LineStrip: return primitiveCount + 1;
            case PrimitiveType::PointListEXT: return primitiveCount;
        }
        return 0;
    }

    int SdlGpuGraphicsBackend::PrimitiveIndexCount(PrimitiveType primitive, int primitiveCount) const
    {
        return PrimitiveVertexCount(primitive, primitiveCount);
    }

    void SdlGpuGraphicsBackend::CreateColoredResources()
    {
        if (coloredVertexShader_ != nullptr)
            return;

        SDL_GPUShaderCreateInfo vsInfo{};
        vsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kColored3dVertSpv);
        vsInfo.code_size = Shaders::kColored3dVertSpv_size;
        vsInfo.entrypoint = "main";
        vsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        vsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        vsInfo.num_uniform_buffers = 2;  // + FogParams (REMED-GFX-009)
        coloredVertexShader_ = SDL_CreateGPUShader(device_, &vsInfo);
        if (coloredVertexShader_ == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create colored3d vertex shader: ") + SDL_GetError());

        SDL_GPUShaderCreateInfo fsInfo{};
        fsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kColored3dFragSpv);
        fsInfo.code_size = Shaders::kColored3dFragSpv_size;
        fsInfo.entrypoint = "main";
        fsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        fsInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        coloredFragmentShader_ = SDL_CreateGPUShader(device_, &fsInfo);
        if (coloredFragmentShader_ == nullptr)
        {
            SDL_ReleaseGPUShader(device_, coloredVertexShader_);
            coloredVertexShader_ = nullptr;
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create colored3d fragment shader: ") + SDL_GetError());
        }
    }

    void SdlGpuGraphicsBackend::DestroyColoredResources()
    {
        for (auto& [key, pipeline] : coloredPipelines_)
            if (pipeline != nullptr) SDL_ReleaseGPUGraphicsPipeline(device_, pipeline);
        coloredPipelines_.clear();
        if (coloredFragmentShader_ != nullptr) { SDL_ReleaseGPUShader(device_, coloredFragmentShader_); coloredFragmentShader_ = nullptr; }
        if (coloredVertexShader_ != nullptr) { SDL_ReleaseGPUShader(device_, coloredVertexShader_); coloredVertexShader_ = nullptr; }
    }

    SDL_GPUGraphicsPipeline* SdlGpuGraphicsBackend::GetOrCreatePipelineColored3D(
        SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount, const RenderStateSnapshot& renderState)
    {
        const std::size_t key = PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, colorFormat, sampleCount, renderState);
        const auto it = coloredPipelines_.find(key);
        if (it != coloredPipelines_.end())
            return it->second;

        SDL_GPUVertexBufferDescription vbDesc{};
        vbDesc.slot = 0;
        vbDesc.pitch = 16;  // VertexPositionColor
        vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        SDL_GPUVertexAttribute attrs[2]{};
        attrs[0].location = 0; attrs[0].buffer_slot = 0; attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[0].offset = 0;
        attrs[1].location = 1; attrs[1].buffer_slot = 0; attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM; attrs[1].offset = 12;

        SDL_GPUColorTargetDescription colorTarget{};
        colorTarget.format = colorFormat;
        FillBlendState(colorTarget.blend_state, renderState);  // SDLGPU-18

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = coloredVertexShader_;
        pipelineInfo.fragment_shader = coloredFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = 2;
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(pipelineInfo.rasterizer_state, renderState);  // SDLGPU-20
        pipelineInfo.multisample_state.sample_count = sampleCount;
        FillDepthStencilState(pipelineInfo.depth_stencil_state, depthTest, depthWrite, depthFunc, renderState);  // SDLGPU-19
        pipelineInfo.target_info.color_target_descriptions = &colorTarget;
        pipelineInfo.target_info.num_color_targets = 1;
        pipelineInfo.target_info.has_depth_stencil_target = (depthStencilFormat_ != SDL_GPU_TEXTUREFORMAT_INVALID);
        pipelineInfo.target_info.depth_stencil_format = depthStencilFormat_;

        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);
        if (pipeline == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create colored3d pipeline: ") + SDL_GetError());
        coloredPipelines_[key] = pipeline;
        return pipeline;
    }

    void SdlGpuGraphicsBackend::CreateTexturedResources()
    {
        if (texturedVertexShader_ != nullptr)
            return;

        SDL_GPUShaderCreateInfo vsInfo{};
        vsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kTextured3dVertSpv);
        vsInfo.code_size = Shaders::kTextured3dVertSpv_size;
        vsInfo.entrypoint = "main";
        vsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        vsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        vsInfo.num_uniform_buffers = 2;  // + FogParams (REMED-GFX-009)
        texturedVertexShader_ = SDL_CreateGPUShader(device_, &vsInfo);
        if (texturedVertexShader_ == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create textured3d vertex shader: ") + SDL_GetError());

        SDL_GPUShaderCreateInfo cvsInfo{};
        cvsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kColoredTextured3dVertSpv);
        cvsInfo.code_size = Shaders::kColoredTextured3dVertSpv_size;
        cvsInfo.entrypoint = "main";
        cvsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        cvsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        cvsInfo.num_uniform_buffers = 2;  // + FogParams (REMED-GFX-009)
        coloredTexturedVertexShader_ = SDL_CreateGPUShader(device_, &cvsInfo);
        if (coloredTexturedVertexShader_ == nullptr)
        {
            SDL_ReleaseGPUShader(device_, texturedVertexShader_);
            texturedVertexShader_ = nullptr;
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create colored_textured3d vertex shader: ") + SDL_GetError());
        }

        SDL_GPUShaderCreateInfo fsInfo{};
        fsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kTextured3dFragSpv);
        fsInfo.code_size = Shaders::kTextured3dFragSpv_size;
        fsInfo.entrypoint = "main";
        fsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        fsInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        fsInfo.num_samplers = 1;
        fsInfo.num_uniform_buffers = 1;
        texturedFragmentShader_ = SDL_CreateGPUShader(device_, &fsInfo);
        if (texturedFragmentShader_ == nullptr)
        {
            SDL_ReleaseGPUShader(device_, coloredTexturedVertexShader_);
            coloredTexturedVertexShader_ = nullptr;
            SDL_ReleaseGPUShader(device_, texturedVertexShader_);
            texturedVertexShader_ = nullptr;
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create textured3d fragment shader: ") + SDL_GetError());
        }
    }

    void SdlGpuGraphicsBackend::DestroyTexturedResources()
    {
        for (auto& [key, pipeline] : texturedPipelines_)
            if (pipeline != nullptr) SDL_ReleaseGPUGraphicsPipeline(device_, pipeline);
        texturedPipelines_.clear();
        for (auto& [key, pipeline] : coloredTexturedPipelines_)
            if (pipeline != nullptr) SDL_ReleaseGPUGraphicsPipeline(device_, pipeline);
        coloredTexturedPipelines_.clear();
        if (texturedFragmentShader_ != nullptr) { SDL_ReleaseGPUShader(device_, texturedFragmentShader_); texturedFragmentShader_ = nullptr; }
        if (coloredTexturedVertexShader_ != nullptr) { SDL_ReleaseGPUShader(device_, coloredTexturedVertexShader_); coloredTexturedVertexShader_ = nullptr; }
        if (texturedVertexShader_ != nullptr) { SDL_ReleaseGPUShader(device_, texturedVertexShader_); texturedVertexShader_ = nullptr; }
    }

    SDL_GPUGraphicsPipeline* SdlGpuGraphicsBackend::GetOrCreatePipelineTextured3D(
        SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount, const RenderStateSnapshot& renderState)
    {
        const std::size_t key = PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, colorFormat, sampleCount, renderState);
        const auto it = texturedPipelines_.find(key);
        if (it != texturedPipelines_.end())
            return it->second;

        SDL_GPUVertexBufferDescription vbDesc{};
        vbDesc.slot = 0;
        vbDesc.pitch = 20;  // VertexPositionTexture
        vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        SDL_GPUVertexAttribute attrs[2]{};
        attrs[0].location = 0; attrs[0].buffer_slot = 0; attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[0].offset = 0;
        attrs[1].location = 1; attrs[1].buffer_slot = 0; attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; attrs[1].offset = 12;

        SDL_GPUColorTargetDescription colorTarget{};
        colorTarget.format = colorFormat;
        FillBlendState(colorTarget.blend_state, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = texturedVertexShader_;
        pipelineInfo.fragment_shader = texturedFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = 2;
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(pipelineInfo.rasterizer_state, renderState);
        pipelineInfo.multisample_state.sample_count = sampleCount;
        FillDepthStencilState(pipelineInfo.depth_stencil_state, depthTest, depthWrite, depthFunc, renderState);
        pipelineInfo.target_info.color_target_descriptions = &colorTarget;
        pipelineInfo.target_info.num_color_targets = 1;
        pipelineInfo.target_info.has_depth_stencil_target = (depthStencilFormat_ != SDL_GPU_TEXTUREFORMAT_INVALID);
        pipelineInfo.target_info.depth_stencil_format = depthStencilFormat_;

        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);
        if (pipeline == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create textured3d pipeline: ") + SDL_GetError());
        texturedPipelines_[key] = pipeline;
        return pipeline;
    }

    SDL_GPUGraphicsPipeline* SdlGpuGraphicsBackend::GetOrCreatePipelineColoredTextured3D(
        SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount, const RenderStateSnapshot& renderState)
    {
        const std::size_t key = PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, colorFormat, sampleCount, renderState);
        const auto it = coloredTexturedPipelines_.find(key);
        if (it != coloredTexturedPipelines_.end())
            return it->second;

        SDL_GPUVertexBufferDescription vbDesc{};
        vbDesc.slot = 0;
        vbDesc.pitch = 24;  // VertexPositionColorTexture
        vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        SDL_GPUVertexAttribute attrs[3]{};
        attrs[0].location = 0; attrs[0].buffer_slot = 0; attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[0].offset = 0;
        attrs[1].location = 1; attrs[1].buffer_slot = 0; attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM; attrs[1].offset = 12;
        attrs[2].location = 2; attrs[2].buffer_slot = 0; attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; attrs[2].offset = 16;

        SDL_GPUColorTargetDescription colorTarget{};
        colorTarget.format = colorFormat;
        FillBlendState(colorTarget.blend_state, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = coloredTexturedVertexShader_;
        pipelineInfo.fragment_shader = texturedFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = 3;
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(pipelineInfo.rasterizer_state, renderState);
        pipelineInfo.multisample_state.sample_count = sampleCount;
        FillDepthStencilState(pipelineInfo.depth_stencil_state, depthTest, depthWrite, depthFunc, renderState);
        pipelineInfo.target_info.color_target_descriptions = &colorTarget;
        pipelineInfo.target_info.num_color_targets = 1;
        pipelineInfo.target_info.has_depth_stencil_target = (depthStencilFormat_ != SDL_GPU_TEXTUREFORMAT_INVALID);
        pipelineInfo.target_info.depth_stencil_format = depthStencilFormat_;

        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);
        if (pipeline == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create colored_textured3d pipeline: ") + SDL_GetError());
        coloredTexturedPipelines_[key] = pipeline;
        return pipeline;
    }

    void SdlGpuGraphicsBackend::CreateLitTexturedResources()
    {
        if (litTexturedVertexShader_ != nullptr)
            return;

        SDL_GPUShaderCreateInfo vsInfo{};
        vsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kLitTextured3dVertSpv);
        vsInfo.code_size = Shaders::kLitTextured3dVertSpv_size;
        vsInfo.entrypoint = "main";
        vsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        vsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        vsInfo.num_uniform_buffers = 3;  // + FogParams (REMED-GFX-009)
        litTexturedVertexShader_ = SDL_CreateGPUShader(device_, &vsInfo);
        if (litTexturedVertexShader_ == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create lit_textured3d vertex shader: ") + SDL_GetError());

        SDL_GPUShaderCreateInfo fsInfo{};
        fsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kLitTextured3dFragSpv);
        fsInfo.code_size = Shaders::kLitTextured3dFragSpv_size;
        fsInfo.entrypoint = "main";
        fsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        fsInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        fsInfo.num_samplers = 1;
        fsInfo.num_uniform_buffers = 2;
        litTexturedFragmentShader_ = SDL_CreateGPUShader(device_, &fsInfo);
        if (litTexturedFragmentShader_ == nullptr)
        {
            SDL_ReleaseGPUShader(device_, litTexturedVertexShader_);
            litTexturedVertexShader_ = nullptr;
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create lit_textured3d fragment shader: ") + SDL_GetError());
        }
    }

    void SdlGpuGraphicsBackend::DestroyLitTexturedResources()
    {
        for (auto& [key, pipeline] : litTexturedPipelines_)
            if (pipeline != nullptr) SDL_ReleaseGPUGraphicsPipeline(device_, pipeline);
        litTexturedPipelines_.clear();
        if (litTexturedFragmentShader_ != nullptr) { SDL_ReleaseGPUShader(device_, litTexturedFragmentShader_); litTexturedFragmentShader_ = nullptr; }
        if (litTexturedVertexShader_ != nullptr) { SDL_ReleaseGPUShader(device_, litTexturedVertexShader_); litTexturedVertexShader_ = nullptr; }
    }

    SDL_GPUGraphicsPipeline* SdlGpuGraphicsBackend::GetOrCreatePipelineLitTextured3D(
        SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount, const RenderStateSnapshot& renderState)
    {
        const std::size_t key = PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, colorFormat, sampleCount, renderState);
        const auto it = litTexturedPipelines_.find(key);
        if (it != litTexturedPipelines_.end())
            return it->second;

        SDL_GPUVertexBufferDescription vbDesc{};
        vbDesc.slot = 0;
        vbDesc.pitch = 32;  // VertexPositionNormalTexture
        vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        SDL_GPUVertexAttribute attrs[3]{};
        attrs[0].location = 0; attrs[0].buffer_slot = 0; attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[0].offset = 0;
        attrs[1].location = 1; attrs[1].buffer_slot = 0; attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[1].offset = 12;
        attrs[2].location = 2; attrs[2].buffer_slot = 0; attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; attrs[2].offset = 24;

        SDL_GPUColorTargetDescription colorTarget{};
        colorTarget.format = colorFormat;
        FillBlendState(colorTarget.blend_state, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = litTexturedVertexShader_;
        pipelineInfo.fragment_shader = litTexturedFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = 3;
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(pipelineInfo.rasterizer_state, renderState);
        pipelineInfo.multisample_state.sample_count = sampleCount;
        FillDepthStencilState(pipelineInfo.depth_stencil_state, depthTest, depthWrite, depthFunc, renderState);
        pipelineInfo.target_info.color_target_descriptions = &colorTarget;
        pipelineInfo.target_info.num_color_targets = 1;
        pipelineInfo.target_info.has_depth_stencil_target = (depthStencilFormat_ != SDL_GPU_TEXTUREFORMAT_INVALID);
        pipelineInfo.target_info.depth_stencil_format = depthStencilFormat_;

        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);
        if (pipeline == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create lit_textured3d pipeline: ") + SDL_GetError());
        litTexturedPipelines_[key] = pipeline;
        return pipeline;
    }

    void SdlGpuGraphicsBackend::QueueColoredDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                                  PrimitiveType primitive, int primitiveCount,
                                                  const GpuDrawParams* params)
    {
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferBackend&>(vb);
        if (sdlGpuVb.Stride() != 16)
            throw std::invalid_argument("CNA SDL_GPU: DrawColoredPrimitives requires a stride-16 "
                                        "(VertexPositionColor) vertex buffer");

        ColoredDrawCommand command;
        const int vertexStart = params != nullptr ? params->vertexStart : 0;
        const auto& shadow = sdlGpuVb.ShadowData();
        const std::size_t byteOffset = static_cast<std::size_t>(vertexStart) * 16u;
        if (byteOffset <= shadow.size())
            command.vertexData.assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset), shadow.end());
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        command.renderState = CaptureRenderState();
        if (params != nullptr)
        {
            const Matrix wvp = world * view * projection;
            FillExtUniforms(command.uniforms, wvp, *params);
            FillFogUniforms(command.fogUniforms, *params);  // REMED-GFX-009
        }
        else
        {
            FillColoredUniforms(command.uniforms, world, view, projection);
        }

        if (ib != nullptr)
        {
            const auto& sdlGpuIb = static_cast<const SdlGpuIndexBufferBackend&>(*ib);
            command.indexed = true;
            command.index32 = sdlGpuIb.IsThirtyTwoBit();
            command.indexData = sdlGpuIb.ShadowData();
            command.indexCount = static_cast<Uint32>(PrimitiveIndexCount(primitive, primitiveCount));
            command.vertexCount = static_cast<Uint32>(sdlGpuVb.GetVertexCount()) - static_cast<Uint32>(vertexStart);
        }
        else
        {
            command.vertexCount = static_cast<Uint32>(PrimitiveVertexCount(primitive, primitiveCount));
        }

        command.target = CurrentDrawTarget();
        coloredDrawCommands_.push_back(std::move(command));
        PushDrawOrder(DrawKind::Colored, coloredDrawCommands_.size() - 1);
        framePending_ = true;
    }

    void SdlGpuGraphicsBackend::QueueTexturedDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                                   PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferBackend&>(vb);
        const std::size_t stride = sdlGpuVb.Stride();

        TexturedDrawCommand command;
        command.hasVertexColor = (stride == 24);
        const int vertexStart = params.vertexStart;
        const auto& shadow = sdlGpuVb.ShadowData();
        const std::size_t byteOffset = static_cast<std::size_t>(vertexStart) * stride;
        if (byteOffset <= shadow.size())
            command.vertexData.assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset), shadow.end());
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        command.renderState = CaptureRenderState();
        const Matrix wvp = world * view * projection;
        FillExtUniforms(command.uniforms, wvp, params);
        FillFogUniforms(command.fogUniforms, params);  // REMED-GFX-009
        command.texture = static_cast<const SdlGpuTextureBackend*>(params.texture0);
        // SDLGPU-21: real per-slot dynamic sampler state (GraphicsDevice.SamplerStates[0]).
        command.textureFilter = samplerSlots_[0].filter;
        command.addressU = samplerSlots_[0].addressU;
        command.addressV = samplerSlots_[0].addressV;

        if (ib != nullptr)
        {
            const auto& sdlGpuIb = static_cast<const SdlGpuIndexBufferBackend&>(*ib);
            command.indexed = true;
            command.index32 = sdlGpuIb.IsThirtyTwoBit();
            command.indexData = sdlGpuIb.ShadowData();
            command.indexCount = static_cast<Uint32>(PrimitiveIndexCount(primitive, primitiveCount));
            command.vertexCount = static_cast<Uint32>(sdlGpuVb.GetVertexCount()) - static_cast<Uint32>(vertexStart);
        }
        else
        {
            command.vertexCount = static_cast<Uint32>(PrimitiveVertexCount(primitive, primitiveCount));
        }

        command.target = CurrentDrawTarget();
        texturedDrawCommands_.push_back(std::move(command));
        PushDrawOrder(DrawKind::Textured, texturedDrawCommands_.size() - 1);
        framePending_ = true;
    }

    void SdlGpuGraphicsBackend::QueueLitTexturedDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                                      const Matrix& world, const Matrix& view, const Matrix& projection,
                                                      PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferBackend&>(vb);
        if (sdlGpuVb.Stride() != 32)
            throw std::invalid_argument("CNA SDL_GPU: lit_textured3d requires a stride-32 "
                                        "(VertexPositionNormalTexture) vertex buffer");

        LitTexturedDrawCommand command;
        const int vertexStart = params.vertexStart;
        const auto& shadow = sdlGpuVb.ShadowData();
        const std::size_t byteOffset = static_cast<std::size_t>(vertexStart) * 32u;
        if (byteOffset <= shadow.size())
            command.vertexData.assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset), shadow.end());
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        command.renderState = CaptureRenderState();
        const Matrix wvp = world * view * projection;
        FillExtUniforms(command.uniforms, wvp, params);
        FillFogUniforms(command.fogUniforms, params);  // REMED-GFX-009
        FillLitLightUniforms(command.lightUniforms, params);
        command.texture = static_cast<const SdlGpuTextureBackend*>(params.texture0);
        command.textureFilter = samplerSlots_[0].filter;
        command.addressU = samplerSlots_[0].addressU;
        command.addressV = samplerSlots_[0].addressV;

        if (ib != nullptr)
        {
            const auto& sdlGpuIb = static_cast<const SdlGpuIndexBufferBackend&>(*ib);
            command.indexed = true;
            command.index32 = sdlGpuIb.IsThirtyTwoBit();
            command.indexData = sdlGpuIb.ShadowData();
            command.indexCount = static_cast<Uint32>(PrimitiveIndexCount(primitive, primitiveCount));
            command.vertexCount = static_cast<Uint32>(sdlGpuVb.GetVertexCount()) - static_cast<Uint32>(vertexStart);
        }
        else
        {
            command.vertexCount = static_cast<Uint32>(PrimitiveVertexCount(primitive, primitiveCount));
        }

        command.target = CurrentDrawTarget();
        litTexturedDrawCommands_.push_back(std::move(command));
        PushDrawOrder(DrawKind::LitTextured, litTexturedDrawCommands_.size() - 1);
        framePending_ = true;
    }

    void SdlGpuGraphicsBackend::CreateAlphaTestResources()
    {
        if (alphaTestVertexShader_ != nullptr)
            return;

        SDL_GPUShaderCreateInfo vsInfo{};
        vsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kAlphaTest3dVertSpv);
        vsInfo.code_size = Shaders::kAlphaTest3dVertSpv_size;
        vsInfo.entrypoint = "main";
        vsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        vsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        vsInfo.num_uniform_buffers = 2;  // + FogParams (REMED-GFX-009)
        alphaTestVertexShader_ = SDL_CreateGPUShader(device_, &vsInfo);
        if (alphaTestVertexShader_ == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create alpha_test3d vertex shader: ") + SDL_GetError());

        SDL_GPUShaderCreateInfo cvsInfo{};
        cvsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kAlphaTestColored3dVertSpv);
        cvsInfo.code_size = Shaders::kAlphaTestColored3dVertSpv_size;
        cvsInfo.entrypoint = "main";
        cvsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        cvsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        cvsInfo.num_uniform_buffers = 2;  // + FogParams (REMED-GFX-009)
        alphaTestColoredVertexShader_ = SDL_CreateGPUShader(device_, &cvsInfo);
        if (alphaTestColoredVertexShader_ == nullptr)
        {
            SDL_ReleaseGPUShader(device_, alphaTestVertexShader_);
            alphaTestVertexShader_ = nullptr;
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create alpha_test_colored3d vertex shader: ") + SDL_GetError());
        }

        SDL_GPUShaderCreateInfo fsInfo{};
        fsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kAlphaTest3dFragSpv);
        fsInfo.code_size = Shaders::kAlphaTest3dFragSpv_size;
        fsInfo.entrypoint = "main";
        fsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        fsInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        fsInfo.num_samplers = 1;
        fsInfo.num_uniform_buffers = 1;
        alphaTestFragmentShader_ = SDL_CreateGPUShader(device_, &fsInfo);
        if (alphaTestFragmentShader_ == nullptr)
        {
            SDL_ReleaseGPUShader(device_, alphaTestColoredVertexShader_);
            alphaTestColoredVertexShader_ = nullptr;
            SDL_ReleaseGPUShader(device_, alphaTestVertexShader_);
            alphaTestVertexShader_ = nullptr;
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create alpha_test3d fragment shader: ") + SDL_GetError());
        }
    }

    void SdlGpuGraphicsBackend::DestroyAlphaTestResources()
    {
        for (auto& [key, pipeline] : alphaTestPipelines_)
            if (pipeline != nullptr) SDL_ReleaseGPUGraphicsPipeline(device_, pipeline);
        alphaTestPipelines_.clear();
        for (auto& [key, pipeline] : alphaTestColoredPipelines_)
            if (pipeline != nullptr) SDL_ReleaseGPUGraphicsPipeline(device_, pipeline);
        alphaTestColoredPipelines_.clear();
        if (alphaTestFragmentShader_ != nullptr) { SDL_ReleaseGPUShader(device_, alphaTestFragmentShader_); alphaTestFragmentShader_ = nullptr; }
        if (alphaTestColoredVertexShader_ != nullptr) { SDL_ReleaseGPUShader(device_, alphaTestColoredVertexShader_); alphaTestColoredVertexShader_ = nullptr; }
        if (alphaTestVertexShader_ != nullptr) { SDL_ReleaseGPUShader(device_, alphaTestVertexShader_); alphaTestVertexShader_ = nullptr; }
    }

    SDL_GPUGraphicsPipeline* SdlGpuGraphicsBackend::GetOrCreatePipelineAlphaTest3D(
        std::size_t stride, SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount, const RenderStateSnapshot& renderState)
    {
        // stride 24 (vertex colour) uses its own dedicated map, keyed the same way every other
        // stride-specific map here is. strides 20/32 share alphaTestVertexShader_ but need
        // DIFFERENT vertex_input_state (different attribute offsets), so that map's key folds in
        // the stride explicitly.
        if (stride == 24)
        {
            const std::size_t key = PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, colorFormat, sampleCount, renderState);
            const auto it = alphaTestColoredPipelines_.find(key);
            if (it != alphaTestColoredPipelines_.end())
                return it->second;

            SDL_GPUVertexBufferDescription vbDesc{};
            vbDesc.slot = 0;
            vbDesc.pitch = 24;
            vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
            SDL_GPUVertexAttribute attrs[3]{};
            attrs[0].location = 0; attrs[0].buffer_slot = 0; attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[0].offset = 0;
            attrs[1].location = 1; attrs[1].buffer_slot = 0; attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM; attrs[1].offset = 12;
            attrs[2].location = 2; attrs[2].buffer_slot = 0; attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; attrs[2].offset = 16;

            SDL_GPUColorTargetDescription colorTarget{};
            colorTarget.format = colorFormat;
            FillBlendState(colorTarget.blend_state, renderState);

            SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.vertex_shader = alphaTestColoredVertexShader_;
            pipelineInfo.fragment_shader = alphaTestFragmentShader_;
            pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
            pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
            pipelineInfo.vertex_input_state.vertex_attributes = attrs;
            pipelineInfo.vertex_input_state.num_vertex_attributes = 3;
            pipelineInfo.primitive_type = topology;
            FillRasterizerState(pipelineInfo.rasterizer_state, renderState);
            pipelineInfo.multisample_state.sample_count = sampleCount;
            FillDepthStencilState(pipelineInfo.depth_stencil_state, depthTest, depthWrite, depthFunc, renderState);
            pipelineInfo.target_info.color_target_descriptions = &colorTarget;
            pipelineInfo.target_info.num_color_targets = 1;
            pipelineInfo.target_info.has_depth_stencil_target = (depthStencilFormat_ != SDL_GPU_TEXTUREFORMAT_INVALID);
            pipelineInfo.target_info.depth_stencil_format = depthStencilFormat_;

            SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);
            if (pipeline == nullptr)
                throw std::runtime_error(std::string("CNA SDL_GPU: failed to create alpha_test_colored3d pipeline: ") + SDL_GetError());
            alphaTestColoredPipelines_[key] = pipeline;
            return pipeline;
        }

        const std::size_t key = HashCombine(static_cast<std::size_t>(stride),
            PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, colorFormat, sampleCount, renderState));
        const auto it = alphaTestPipelines_.find(key);
        if (it != alphaTestPipelines_.end())
            return it->second;

        SDL_GPUVertexBufferDescription vbDesc{};
        vbDesc.slot = 0;
        vbDesc.pitch = static_cast<Uint32>(stride);
        vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        SDL_GPUVertexAttribute attrs[2]{};
        attrs[0].location = 0; attrs[0].buffer_slot = 0; attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[0].offset = 0;
        attrs[1].location = 1; attrs[1].buffer_slot = 0; attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attrs[1].offset = (stride == 32) ? 24 : 12;  // stride 32: UV past the 3-float normal; stride 20: UV right after position

        SDL_GPUColorTargetDescription colorTarget{};
        colorTarget.format = colorFormat;
        FillBlendState(colorTarget.blend_state, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = alphaTestVertexShader_;
        pipelineInfo.fragment_shader = alphaTestFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = 2;
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(pipelineInfo.rasterizer_state, renderState);
        pipelineInfo.multisample_state.sample_count = sampleCount;
        FillDepthStencilState(pipelineInfo.depth_stencil_state, depthTest, depthWrite, depthFunc, renderState);
        pipelineInfo.target_info.color_target_descriptions = &colorTarget;
        pipelineInfo.target_info.num_color_targets = 1;
        pipelineInfo.target_info.has_depth_stencil_target = (depthStencilFormat_ != SDL_GPU_TEXTUREFORMAT_INVALID);
        pipelineInfo.target_info.depth_stencil_format = depthStencilFormat_;

        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);
        if (pipeline == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create alpha_test3d pipeline: ") + SDL_GetError());
        alphaTestPipelines_[key] = pipeline;
        return pipeline;
    }

    void SdlGpuGraphicsBackend::CreateDualTextureResources()
    {
        if (dualTextureVertexShader_ != nullptr)
            return;

        SDL_GPUShaderCreateInfo vsInfo{};
        vsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kDualTexture3dVertSpv);
        vsInfo.code_size = Shaders::kDualTexture3dVertSpv_size;
        vsInfo.entrypoint = "main";
        vsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        vsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        vsInfo.num_uniform_buffers = 2;  // + FogParams (REMED-GFX-009)
        dualTextureVertexShader_ = SDL_CreateGPUShader(device_, &vsInfo);
        if (dualTextureVertexShader_ == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create dual_texture3d vertex shader: ") + SDL_GetError());

        SDL_GPUShaderCreateInfo cvsInfo{};
        cvsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kDualTextureColored3dVertSpv);
        cvsInfo.code_size = Shaders::kDualTextureColored3dVertSpv_size;
        cvsInfo.entrypoint = "main";
        cvsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        cvsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        cvsInfo.num_uniform_buffers = 2;  // + FogParams (REMED-GFX-009)
        dualTextureColoredVertexShader_ = SDL_CreateGPUShader(device_, &cvsInfo);
        if (dualTextureColoredVertexShader_ == nullptr)
        {
            SDL_ReleaseGPUShader(device_, dualTextureVertexShader_);
            dualTextureVertexShader_ = nullptr;
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create dual_texture_colored3d vertex shader: ") + SDL_GetError());
        }

        SDL_GPUShaderCreateInfo fsInfo{};
        fsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kDualTexture3dFragSpv);
        fsInfo.code_size = Shaders::kDualTexture3dFragSpv_size;
        fsInfo.entrypoint = "main";
        fsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        fsInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        fsInfo.num_samplers = 2;
        dualTextureFragmentShader_ = SDL_CreateGPUShader(device_, &fsInfo);
        if (dualTextureFragmentShader_ == nullptr)
        {
            SDL_ReleaseGPUShader(device_, dualTextureColoredVertexShader_);
            dualTextureColoredVertexShader_ = nullptr;
            SDL_ReleaseGPUShader(device_, dualTextureVertexShader_);
            dualTextureVertexShader_ = nullptr;
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create dual_texture3d fragment shader: ") + SDL_GetError());
        }
    }

    void SdlGpuGraphicsBackend::DestroyDualTextureResources()
    {
        for (auto& [key, pipeline] : dualTexturePipelines_)
            if (pipeline != nullptr) SDL_ReleaseGPUGraphicsPipeline(device_, pipeline);
        dualTexturePipelines_.clear();
        for (auto& [key, pipeline] : dualTextureColoredPipelines_)
            if (pipeline != nullptr) SDL_ReleaseGPUGraphicsPipeline(device_, pipeline);
        dualTextureColoredPipelines_.clear();
        if (dualTextureFragmentShader_ != nullptr) { SDL_ReleaseGPUShader(device_, dualTextureFragmentShader_); dualTextureFragmentShader_ = nullptr; }
        if (dualTextureColoredVertexShader_ != nullptr) { SDL_ReleaseGPUShader(device_, dualTextureColoredVertexShader_); dualTextureColoredVertexShader_ = nullptr; }
        if (dualTextureVertexShader_ != nullptr) { SDL_ReleaseGPUShader(device_, dualTextureVertexShader_); dualTextureVertexShader_ = nullptr; }
    }

    SDL_GPUGraphicsPipeline* SdlGpuGraphicsBackend::GetOrCreatePipelineDualTexture3D(
        std::size_t stride, SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount, const RenderStateSnapshot& renderState)
    {
        auto& cache = (stride == 24) ? dualTextureColoredPipelines_ : dualTexturePipelines_;
        const std::size_t key = PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, colorFormat, sampleCount, renderState);
        const auto it = cache.find(key);
        if (it != cache.end())
            return it->second;

        SDL_GPUVertexBufferDescription vbDesc{};
        vbDesc.slot = 0;
        vbDesc.pitch = static_cast<Uint32>(stride);
        vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        SDL_GPUVertexAttribute attrs[3]{};
        Uint32 numAttrs;
        if (stride == 24)
        {
            attrs[0].location = 0; attrs[0].buffer_slot = 0; attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[0].offset = 0;
            attrs[1].location = 1; attrs[1].buffer_slot = 0; attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM; attrs[1].offset = 12;
            attrs[2].location = 2; attrs[2].buffer_slot = 0; attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; attrs[2].offset = 16;
            numAttrs = 3;
        }
        else
        {
            attrs[0].location = 0; attrs[0].buffer_slot = 0; attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[0].offset = 0;
            attrs[1].location = 1; attrs[1].buffer_slot = 0; attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; attrs[1].offset = 12;
            numAttrs = 2;
        }

        SDL_GPUColorTargetDescription colorTarget{};
        colorTarget.format = colorFormat;
        FillBlendState(colorTarget.blend_state, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = (stride == 24) ? dualTextureColoredVertexShader_ : dualTextureVertexShader_;
        pipelineInfo.fragment_shader = dualTextureFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = numAttrs;
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(pipelineInfo.rasterizer_state, renderState);
        pipelineInfo.multisample_state.sample_count = sampleCount;
        FillDepthStencilState(pipelineInfo.depth_stencil_state, depthTest, depthWrite, depthFunc, renderState);
        pipelineInfo.target_info.color_target_descriptions = &colorTarget;
        pipelineInfo.target_info.num_color_targets = 1;
        pipelineInfo.target_info.has_depth_stencil_target = (depthStencilFormat_ != SDL_GPU_TEXTUREFORMAT_INVALID);
        pipelineInfo.target_info.depth_stencil_format = depthStencilFormat_;

        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);
        if (pipeline == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create dual_texture3d pipeline: ") + SDL_GetError());
        cache[key] = pipeline;
        return pipeline;
    }

    void SdlGpuGraphicsBackend::CreateEnvMapResources()
    {
        if (envMapVertexShader_ != nullptr)
            return;

        SDL_GPUShaderCreateInfo vsInfo{};
        vsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kEnvMap3dVertSpv);
        vsInfo.code_size = Shaders::kEnvMap3dVertSpv_size;
        vsInfo.entrypoint = "main";
        vsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        vsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        vsInfo.num_uniform_buffers = 3;  // + FogParams (REMED-GFX-009)
        envMapVertexShader_ = SDL_CreateGPUShader(device_, &vsInfo);
        if (envMapVertexShader_ == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create env_map3d vertex shader: ") + SDL_GetError());

        SDL_GPUShaderCreateInfo fsInfo{};
        fsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kEnvMap3dFragSpv);
        fsInfo.code_size = Shaders::kEnvMap3dFragSpv_size;
        fsInfo.entrypoint = "main";
        fsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        fsInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        fsInfo.num_samplers = 2;  // uTexture (2D) + uEnvMap (cube)
        fsInfo.num_uniform_buffers = 2;
        envMapFragmentShader_ = SDL_CreateGPUShader(device_, &fsInfo);
        if (envMapFragmentShader_ == nullptr)
        {
            SDL_ReleaseGPUShader(device_, envMapVertexShader_);
            envMapVertexShader_ = nullptr;
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create env_map3d fragment shader: ") + SDL_GetError());
        }
    }

    void SdlGpuGraphicsBackend::DestroyEnvMapResources()
    {
        for (auto& [key, pipeline] : envMapPipelines_)
            if (pipeline != nullptr) SDL_ReleaseGPUGraphicsPipeline(device_, pipeline);
        envMapPipelines_.clear();
        if (envMapFragmentShader_ != nullptr) { SDL_ReleaseGPUShader(device_, envMapFragmentShader_); envMapFragmentShader_ = nullptr; }
        if (envMapVertexShader_ != nullptr) { SDL_ReleaseGPUShader(device_, envMapVertexShader_); envMapVertexShader_ = nullptr; }
    }

    SDL_GPUGraphicsPipeline* SdlGpuGraphicsBackend::GetOrCreatePipelineEnvMap3D(
        SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount, const RenderStateSnapshot& renderState)
    {
        const std::size_t key = PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, colorFormat, sampleCount, renderState);
        const auto it = envMapPipelines_.find(key);
        if (it != envMapPipelines_.end())
            return it->second;

        // Stride 32: VertexPositionNormalTexture -- identical vertex layout to lit_textured3d.
        SDL_GPUVertexBufferDescription vbDesc{};
        vbDesc.slot = 0;
        vbDesc.pitch = 32;
        vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        SDL_GPUVertexAttribute attrs[3]{};
        attrs[0].location = 0; attrs[0].buffer_slot = 0; attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[0].offset = 0;
        attrs[1].location = 1; attrs[1].buffer_slot = 0; attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[1].offset = 12;
        attrs[2].location = 2; attrs[2].buffer_slot = 0; attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; attrs[2].offset = 24;

        SDL_GPUColorTargetDescription colorTarget{};
        colorTarget.format = colorFormat;
        FillBlendState(colorTarget.blend_state, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = envMapVertexShader_;
        pipelineInfo.fragment_shader = envMapFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = 3;
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(pipelineInfo.rasterizer_state, renderState);
        pipelineInfo.multisample_state.sample_count = sampleCount;
        FillDepthStencilState(pipelineInfo.depth_stencil_state, depthTest, depthWrite, depthFunc, renderState);
        pipelineInfo.target_info.color_target_descriptions = &colorTarget;
        pipelineInfo.target_info.num_color_targets = 1;
        pipelineInfo.target_info.has_depth_stencil_target = (depthStencilFormat_ != SDL_GPU_TEXTUREFORMAT_INVALID);
        pipelineInfo.target_info.depth_stencil_format = depthStencilFormat_;

        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);
        if (pipeline == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create env_map3d pipeline: ") + SDL_GetError());
        envMapPipelines_[key] = pipeline;
        return pipeline;
    }

    void SdlGpuGraphicsBackend::CreateSkinnedResources()
    {
        if (skinnedVertexShader_ != nullptr)
            return;

        SDL_GPUShaderCreateInfo vsInfo{};
        vsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kSkinned3dVertSpv);
        vsInfo.code_size = Shaders::kSkinned3dVertSpv_size;
        vsInfo.entrypoint = "main";
        vsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        vsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        vsInfo.num_uniform_buffers = 3;  // PC, SkinnedLightParams + FogParams (REMED-GFX-009)
        vsInfo.num_storage_buffers = 1;  // BoneBlock (4608 bytes) -- see SkinnedDrawCommand's doc comment
        skinnedVertexShader_ = SDL_CreateGPUShader(device_, &vsInfo);
        if (skinnedVertexShader_ == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create skinned3d vertex shader: ") + SDL_GetError());

        SDL_GPUShaderCreateInfo colVsInfo{};
        colVsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kSkinnedColored3dVertSpv);
        colVsInfo.code_size = Shaders::kSkinnedColored3dVertSpv_size;
        colVsInfo.entrypoint = "main";
        colVsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        colVsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        colVsInfo.num_uniform_buffers = 3;  // PC, SkinnedLightParams + FogParams (REMED-GFX-009)
        colVsInfo.num_storage_buffers = 1;  // BoneBlock
        skinnedColoredVertexShader_ = SDL_CreateGPUShader(device_, &colVsInfo);
        if (skinnedColoredVertexShader_ == nullptr)
        {
            SDL_ReleaseGPUShader(device_, skinnedVertexShader_);
            skinnedVertexShader_ = nullptr;
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create skinned_colored3d vertex shader: ") + SDL_GetError());
        }

        SDL_GPUShaderCreateInfo colFsInfo{};
        colFsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kSkinnedColored3dFragSpv);
        colFsInfo.code_size = Shaders::kSkinnedColored3dFragSpv_size;
        colFsInfo.entrypoint = "main";
        colFsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        colFsInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        colFsInfo.num_samplers = 1;
        colFsInfo.num_uniform_buffers = 2;
        skinnedColoredFragmentShader_ = SDL_CreateGPUShader(device_, &colFsInfo);
        if (skinnedColoredFragmentShader_ == nullptr)
        {
            SDL_ReleaseGPUShader(device_, skinnedColoredVertexShader_);
            skinnedColoredVertexShader_ = nullptr;
            SDL_ReleaseGPUShader(device_, skinnedVertexShader_);
            skinnedVertexShader_ = nullptr;
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create skinned_colored3d fragment shader: ") + SDL_GetError());
        }
    }

    void SdlGpuGraphicsBackend::DestroySkinnedResources()
    {
        for (auto& [key, pipeline] : skinnedPipelines_)
            if (pipeline != nullptr) SDL_ReleaseGPUGraphicsPipeline(device_, pipeline);
        skinnedPipelines_.clear();
        for (auto& [key, pipeline] : skinnedColoredPipelines_)
            if (pipeline != nullptr) SDL_ReleaseGPUGraphicsPipeline(device_, pipeline);
        skinnedColoredPipelines_.clear();
        if (skinnedColoredFragmentShader_ != nullptr) { SDL_ReleaseGPUShader(device_, skinnedColoredFragmentShader_); skinnedColoredFragmentShader_ = nullptr; }
        if (skinnedColoredVertexShader_ != nullptr) { SDL_ReleaseGPUShader(device_, skinnedColoredVertexShader_); skinnedColoredVertexShader_ = nullptr; }
        if (skinnedVertexShader_ != nullptr) { SDL_ReleaseGPUShader(device_, skinnedVertexShader_); skinnedVertexShader_ = nullptr; }
    }

    SDL_GPUGraphicsPipeline* SdlGpuGraphicsBackend::GetOrCreatePipelineSkinned3D(
        bool hasVertexColor, SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount, const RenderStateSnapshot& renderState)
    {
        const std::size_t key = PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, colorFormat, sampleCount, renderState);
        auto& cache = hasVertexColor ? skinnedColoredPipelines_ : skinnedPipelines_;
        const auto it = cache.find(key);
        if (it != cache.end())
            return it->second;

        // Stride 52: VertexPositionNormalTextureSkinned -- pos(12) + normal(12) + uv(8) +
        // blendWeight(16) + blendIndices(4, UBYTE4, non-normalized -> uvec4 in the shader).
        // Stride 56 appends a normalized ubyte4 Color at offset 52 (location 5), matching
        // EasyGLGraphicsBackend::ApplyLayout's stride==56 case exactly.
        SDL_GPUVertexBufferDescription vbDesc{};
        vbDesc.slot = 0;
        vbDesc.pitch = hasVertexColor ? 56 : 52;
        vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        SDL_GPUVertexAttribute attrs[6]{};
        attrs[0].location = 0; attrs[0].buffer_slot = 0; attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[0].offset = 0;
        attrs[1].location = 1; attrs[1].buffer_slot = 0; attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[1].offset = 12;
        attrs[2].location = 2; attrs[2].buffer_slot = 0; attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; attrs[2].offset = 24;
        attrs[3].location = 3; attrs[3].buffer_slot = 0; attrs[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4; attrs[3].offset = 32;
        attrs[4].location = 4; attrs[4].buffer_slot = 0; attrs[4].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4; attrs[4].offset = 48;
        attrs[5].location = 5; attrs[5].buffer_slot = 0; attrs[5].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM; attrs[5].offset = 52;

        SDL_GPUColorTargetDescription colorTarget{};
        colorTarget.format = colorFormat;
        FillBlendState(colorTarget.blend_state, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = hasVertexColor ? skinnedColoredVertexShader_ : skinnedVertexShader_;
        // Stride 52 reuses litTexturedFragmentShader_ unchanged; stride 56 needs its own
        // fragment shader to multiply vertex color into the post-specular output (see
        // SkinnedDrawCommand's own doc comment).
        pipelineInfo.fragment_shader = hasVertexColor ? skinnedColoredFragmentShader_ : litTexturedFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = hasVertexColor ? 6 : 5;
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(pipelineInfo.rasterizer_state, renderState);
        pipelineInfo.multisample_state.sample_count = sampleCount;
        FillDepthStencilState(pipelineInfo.depth_stencil_state, depthTest, depthWrite, depthFunc, renderState);
        pipelineInfo.target_info.color_target_descriptions = &colorTarget;
        pipelineInfo.target_info.num_color_targets = 1;
        pipelineInfo.target_info.has_depth_stencil_target = (depthStencilFormat_ != SDL_GPU_TEXTUREFORMAT_INVALID);
        pipelineInfo.target_info.depth_stencil_format = depthStencilFormat_;

        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);
        if (pipeline == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create skinned3d pipeline: ") + SDL_GetError());
        cache[key] = pipeline;
        return pipeline;
    }

    void SdlGpuGraphicsBackend::EnsureDefaultPbrTextures()
    {
        if (defaultWhiteTexture_ == nullptr)
        {
            ImageData white{1, 1, {255, 255, 255, 255}, 1};
            defaultWhiteTexture_ = std::make_unique<SdlGpuTextureBackend>(*this, white);
        }
        if (defaultFlatNormalTexture_ == nullptr)
        {
            // (128,128,255,255) decodes (via the shader's rgb*2-1) to a tangent-space normal of
            // ~(0,0,1) -- the unperturbed geometric normal -- mirrors EasyGLGraphicsBackend::
            // EnsureDefaultFlatNormalTexture()'s identical encoding exactly.
            ImageData flatNormal{1, 1, {128, 128, 255, 255}, 1};
            defaultFlatNormalTexture_ = std::make_unique<SdlGpuTextureBackend>(*this, flatNormal);
        }
    }

    void SdlGpuGraphicsBackend::CreatePbrResources()
    {
        if (pbrVertexShader_ != nullptr)
            return;

        SDL_GPUShaderCreateInfo vsInfo{};
        vsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kPbr3dVertSpv);
        vsInfo.code_size = Shaders::kPbr3dVertSpv_size;
        vsInfo.entrypoint = "main";
        vsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        vsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        vsInfo.num_uniform_buffers = 3;  // PC, LitLightParams + FogParams (REMED-GFX-009)
        pbrVertexShader_ = SDL_CreateGPUShader(device_, &vsInfo);
        if (pbrVertexShader_ == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create pbr3d vertex shader: ") + SDL_GetError());

        SDL_GPUShaderCreateInfo skinnedVsInfo{};
        skinnedVsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kPbrSkinned3dVertSpv);
        skinnedVsInfo.code_size = Shaders::kPbrSkinned3dVertSpv_size;
        skinnedVsInfo.entrypoint = "main";
        skinnedVsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        skinnedVsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        skinnedVsInfo.num_uniform_buffers = 3;  // PC, SkinnedLightParams + FogParams (REMED-GFX-009)
        skinnedVsInfo.num_storage_buffers = 1;  // BoneBlock
        pbrSkinnedVertexShader_ = SDL_CreateGPUShader(device_, &skinnedVsInfo);
        if (pbrSkinnedVertexShader_ == nullptr)
        {
            SDL_ReleaseGPUShader(device_, pbrVertexShader_);
            pbrVertexShader_ = nullptr;
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create pbr_skinned3d vertex shader: ") + SDL_GetError());
        }

        SDL_GPUShaderCreateInfo fsInfo{};
        fsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kPbr3dFragSpv);
        fsInfo.code_size = Shaders::kPbr3dFragSpv_size;
        fsInfo.entrypoint = "main";
        fsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        fsInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        fsInfo.num_samplers = 5;  // base color, normal, metallic-roughness, emissive, occlusion
        fsInfo.num_uniform_buffers = 3;  // PC, LitLightParams, PbrParams
        pbrFragmentShader_ = SDL_CreateGPUShader(device_, &fsInfo);
        if (pbrFragmentShader_ == nullptr)
        {
            SDL_ReleaseGPUShader(device_, pbrSkinnedVertexShader_);
            pbrSkinnedVertexShader_ = nullptr;
            SDL_ReleaseGPUShader(device_, pbrVertexShader_);
            pbrVertexShader_ = nullptr;
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create pbr3d fragment shader: ") + SDL_GetError());
        }
    }

    void SdlGpuGraphicsBackend::DestroyPbrResources()
    {
        for (auto& [key, pipeline] : pbrPipelines_)
            if (pipeline != nullptr) SDL_ReleaseGPUGraphicsPipeline(device_, pipeline);
        pbrPipelines_.clear();
        for (auto& [key, pipeline] : pbrSkinnedPipelines_)
            if (pipeline != nullptr) SDL_ReleaseGPUGraphicsPipeline(device_, pipeline);
        pbrSkinnedPipelines_.clear();
        if (pbrFragmentShader_ != nullptr) { SDL_ReleaseGPUShader(device_, pbrFragmentShader_); pbrFragmentShader_ = nullptr; }
        if (pbrSkinnedVertexShader_ != nullptr) { SDL_ReleaseGPUShader(device_, pbrSkinnedVertexShader_); pbrSkinnedVertexShader_ = nullptr; }
        if (pbrVertexShader_ != nullptr) { SDL_ReleaseGPUShader(device_, pbrVertexShader_); pbrVertexShader_ = nullptr; }
        // Destroyed here (not left to ~SdlGpuGraphicsBackend()'s generic pendingTextureReleases_
        // sweep) since these are owned SdlGpuTextureBackend instances, not raw handles.
        defaultFlatNormalTexture_.reset();
        defaultWhiteTexture_.reset();
    }

    SDL_GPUGraphicsPipeline* SdlGpuGraphicsBackend::GetOrCreatePipelinePbr3D(
        bool skinned, SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount, const RenderStateSnapshot& renderState)
    {
        const std::size_t key = PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, colorFormat, sampleCount, renderState);
        auto& cache = skinned ? pbrSkinnedPipelines_ : pbrPipelines_;
        const auto it = cache.find(key);
        if (it != cache.end())
            return it->second;

        SDL_GPUVertexBufferDescription vbDesc{};
        vbDesc.slot = 0;
        vbDesc.pitch = skinned ? 68 : 48;
        vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        // Stride 48 (VertexPositionNormalTangentTexture): pos(12) + normal(12) + tangent(16) +
        // uv(8). Stride 68 (VertexPositionNormalTangentTextureSkinned) appends blendWeight(16) +
        // blendIndices(4) after the same 48-byte prefix, matching EasyGLGraphicsBackend::
        // ApplyLayout's stride==48/68 cases exactly.
        SDL_GPUVertexAttribute attrs[6]{};
        attrs[0].location = 0; attrs[0].buffer_slot = 0; attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[0].offset = 0;
        attrs[1].location = 1; attrs[1].buffer_slot = 0; attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[1].offset = 12;
        attrs[2].location = 2; attrs[2].buffer_slot = 0; attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4; attrs[2].offset = 24;
        attrs[3].location = 3; attrs[3].buffer_slot = 0; attrs[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; attrs[3].offset = 40;
        attrs[4].location = 4; attrs[4].buffer_slot = 0; attrs[4].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4; attrs[4].offset = 48;
        attrs[5].location = 5; attrs[5].buffer_slot = 0; attrs[5].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4; attrs[5].offset = 64;

        SDL_GPUColorTargetDescription colorTarget{};
        colorTarget.format = colorFormat;
        FillBlendState(colorTarget.blend_state, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = skinned ? pbrSkinnedVertexShader_ : pbrVertexShader_;
        pipelineInfo.fragment_shader = pbrFragmentShader_;  // shared unchanged by both variants
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = skinned ? 6 : 4;
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(pipelineInfo.rasterizer_state, renderState);
        pipelineInfo.multisample_state.sample_count = sampleCount;
        FillDepthStencilState(pipelineInfo.depth_stencil_state, depthTest, depthWrite, depthFunc, renderState);
        pipelineInfo.target_info.color_target_descriptions = &colorTarget;
        pipelineInfo.target_info.num_color_targets = 1;
        pipelineInfo.target_info.has_depth_stencil_target = (depthStencilFormat_ != SDL_GPU_TEXTUREFORMAT_INVALID);
        pipelineInfo.target_info.depth_stencil_format = depthStencilFormat_;

        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);
        if (pipeline == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create pbr3d pipeline: ") + SDL_GetError());
        cache[key] = pipeline;
        return pipeline;
    }

    void SdlGpuGraphicsBackend::QueueAlphaTestDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                                    const Matrix& world, const Matrix& view, const Matrix& projection,
                                                    PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferBackend&>(vb);
        const std::size_t stride = sdlGpuVb.Stride();

        AlphaTestDrawCommand command;
        command.stride = stride;
        const int vertexStart = params.vertexStart;
        const auto& shadow = sdlGpuVb.ShadowData();
        const std::size_t byteOffset = static_cast<std::size_t>(vertexStart) * stride;
        if (byteOffset <= shadow.size())
            command.vertexData.assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset), shadow.end());
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        command.renderState = CaptureRenderState();
        const Matrix wvp = world * view * projection;
        FillAlphaTestUniforms(command.uniforms, wvp, params);
        FillFogUniforms(command.fogUniforms, params);  // REMED-GFX-009
        command.texture = static_cast<const SdlGpuTextureBackend*>(params.texture0);
        command.textureFilter = samplerSlots_[0].filter;
        command.addressU = samplerSlots_[0].addressU;
        command.addressV = samplerSlots_[0].addressV;

        if (ib != nullptr)
        {
            const auto& sdlGpuIb = static_cast<const SdlGpuIndexBufferBackend&>(*ib);
            command.indexed = true;
            command.index32 = sdlGpuIb.IsThirtyTwoBit();
            command.indexData = sdlGpuIb.ShadowData();
            command.indexCount = static_cast<Uint32>(PrimitiveIndexCount(primitive, primitiveCount));
            command.vertexCount = static_cast<Uint32>(sdlGpuVb.GetVertexCount()) - static_cast<Uint32>(vertexStart);
        }
        else
        {
            command.vertexCount = static_cast<Uint32>(PrimitiveVertexCount(primitive, primitiveCount));
        }

        command.target = CurrentDrawTarget();
        alphaTestDrawCommands_.push_back(std::move(command));
        PushDrawOrder(DrawKind::AlphaTest, alphaTestDrawCommands_.size() - 1);
        framePending_ = true;
    }

    void SdlGpuGraphicsBackend::QueueDualTextureDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                                      const Matrix& world, const Matrix& view, const Matrix& projection,
                                                      PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferBackend&>(vb);
        const std::size_t stride = sdlGpuVb.Stride();

        DualTextureDrawCommand command;
        command.hasVertexColor = (stride == 24);
        const int vertexStart = params.vertexStart;
        const auto& shadow = sdlGpuVb.ShadowData();
        const std::size_t byteOffset = static_cast<std::size_t>(vertexStart) * stride;
        if (byteOffset <= shadow.size())
            command.vertexData.assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset), shadow.end());
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        command.renderState = CaptureRenderState();
        const Matrix wvp = world * view * projection;
        FillExtUniforms(command.uniforms, wvp, params);
        FillFogUniforms(command.fogUniforms, params);  // REMED-GFX-009
        command.texture0 = static_cast<const SdlGpuTextureBackend*>(params.texture0);
        command.texture1 = static_cast<const SdlGpuTextureBackend*>(params.texture1);
        // SDLGPU-21: texture0/texture1 are independent slots (GraphicsDevice.SamplerStates[0]/[1]
        // -- real XNA lets DualTextureEffect's two textures sample differently).
        command.texture0Filter = samplerSlots_[0].filter;
        command.texture0AddressU = samplerSlots_[0].addressU;
        command.texture0AddressV = samplerSlots_[0].addressV;
        command.texture1Filter = samplerSlots_[1].filter;
        command.texture1AddressU = samplerSlots_[1].addressU;
        command.texture1AddressV = samplerSlots_[1].addressV;

        if (ib != nullptr)
        {
            const auto& sdlGpuIb = static_cast<const SdlGpuIndexBufferBackend&>(*ib);
            command.indexed = true;
            command.index32 = sdlGpuIb.IsThirtyTwoBit();
            command.indexData = sdlGpuIb.ShadowData();
            command.indexCount = static_cast<Uint32>(PrimitiveIndexCount(primitive, primitiveCount));
            command.vertexCount = static_cast<Uint32>(sdlGpuVb.GetVertexCount()) - static_cast<Uint32>(vertexStart);
        }
        else
        {
            command.vertexCount = static_cast<Uint32>(PrimitiveVertexCount(primitive, primitiveCount));
        }

        command.target = CurrentDrawTarget();
        dualTextureDrawCommands_.push_back(std::move(command));
        PushDrawOrder(DrawKind::DualTexture, dualTextureDrawCommands_.size() - 1);
        framePending_ = true;
    }

    void SdlGpuGraphicsBackend::QueueEnvMapDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                                const Matrix& world, const Matrix& view, const Matrix& projection,
                                                PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferBackend&>(vb);
        if (sdlGpuVb.Stride() != 32)
            throw std::invalid_argument("CNA SDL_GPU: env_map3d requires a stride-32 "
                                        "(VertexPositionNormalTexture) vertex buffer");

        // params.envMap (ITextureCubeBackend*) may be either a plain, uploaded TextureCube
        // (SdlGpuTextureCubeBackend, SDLGPU-51) or a RenderTargetCube sampled after being rendered
        // into (SdlGpuRenderTargetCubeBackend, SDLGPU-36) -- both implement ITextureCubeBackend but
        // are unrelated concrete classes, so resolve whichever one this actually is to get the raw
        // SDL_GPUTexture* to bind (mirrors SpriteBatch::Draw's own dual-backend resolve for
        // ITextureBackend/SdlGpuTextureBackend vs SdlGpuRenderTargetBackend).
        SDL_GPUTexture* envMapTexture = nullptr;
        if (const auto* plainCube = dynamic_cast<const SdlGpuTextureCubeBackend*>(params.envMap))
            envMapTexture = plainCube->Texture();
        else if (const auto* rtCube = dynamic_cast<const SdlGpuRenderTargetCubeBackend*>(params.envMap))
            envMapTexture = rtCube->CubeTexture();
        else
            throw std::invalid_argument("CNA SDL_GPU: EnvironmentMapEffect received a cube texture from another graphics backend");

        EnvMapDrawCommand command;
        const int vertexStart = params.vertexStart;
        const auto& shadow = sdlGpuVb.ShadowData();
        const std::size_t byteOffset = static_cast<std::size_t>(vertexStart) * 32u;
        if (byteOffset <= shadow.size())
            command.vertexData.assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset), shadow.end());
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        command.renderState = CaptureRenderState();
        const Matrix wvp = world * view * projection;
        FillEnvMapUniforms(command.uniforms, wvp, params);
        FillFogUniforms(command.fogUniforms, params);  // REMED-GFX-009
        FillEnvMapParams(command.envMapUniforms, params);
        command.texture = static_cast<const SdlGpuTextureBackend*>(params.texture0);
        command.envMapTexture = envMapTexture;
        // SDLGPU-21: diffuse texture0 gets real per-slot dynamic sampler state; the env map
        // itself stays fixed Linear+Clamp regardless (see RenderEnvMapDraws' own comment).
        command.textureFilter = samplerSlots_[0].filter;
        command.addressU = samplerSlots_[0].addressU;
        command.addressV = samplerSlots_[0].addressV;

        if (ib != nullptr)
        {
            const auto& sdlGpuIb = static_cast<const SdlGpuIndexBufferBackend&>(*ib);
            command.indexed = true;
            command.index32 = sdlGpuIb.IsThirtyTwoBit();
            command.indexData = sdlGpuIb.ShadowData();
            command.indexCount = static_cast<Uint32>(PrimitiveIndexCount(primitive, primitiveCount));
            command.vertexCount = static_cast<Uint32>(sdlGpuVb.GetVertexCount()) - static_cast<Uint32>(vertexStart);
        }
        else
        {
            command.vertexCount = static_cast<Uint32>(PrimitiveVertexCount(primitive, primitiveCount));
        }

        command.target = CurrentDrawTarget();
        envMapDrawCommands_.push_back(std::move(command));
        PushDrawOrder(DrawKind::EnvMap, envMapDrawCommands_.size() - 1);
        framePending_ = true;
    }

    void SdlGpuGraphicsBackend::QueueSkinnedDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                                 const Matrix& world, const Matrix& view, const Matrix& projection,
                                                 PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferBackend&>(vb);
        const std::size_t stride = sdlGpuVb.Stride();
        if (stride != 52 && stride != 56)
            throw std::invalid_argument("CNA SDL_GPU: skinned3d requires a stride-52 "
                                        "(VertexPositionNormalTextureSkinned) or stride-56 "
                                        "(+ per-vertex Color) vertex buffer");

        SkinnedDrawCommand command;
        command.hasVertexColor = (stride == 56);
        const int vertexStart = params.vertexStart;
        const auto& shadow = sdlGpuVb.ShadowData();
        const std::size_t byteOffset = static_cast<std::size_t>(vertexStart) * stride;
        if (byteOffset <= shadow.size())
            command.vertexData.assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset), shadow.end());
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        command.renderState = CaptureRenderState();
        const Matrix wvp = world * view * projection;
        FillExtUniforms(command.uniforms, wvp, params);
        FillFogUniforms(command.fogUniforms, params);  // REMED-GFX-009
        FillSkinnedBoneUniforms(command.boneUniforms, params);
        FillSkinnedLightUniforms(command.lightUniforms, params);
        command.texture = static_cast<const SdlGpuTextureBackend*>(params.texture0);
        command.textureFilter = samplerSlots_[0].filter;
        command.addressU = samplerSlots_[0].addressU;
        command.addressV = samplerSlots_[0].addressV;

        if (ib != nullptr)
        {
            const auto& sdlGpuIb = static_cast<const SdlGpuIndexBufferBackend&>(*ib);
            command.indexed = true;
            command.index32 = sdlGpuIb.IsThirtyTwoBit();
            command.indexData = sdlGpuIb.ShadowData();
            command.indexCount = static_cast<Uint32>(PrimitiveIndexCount(primitive, primitiveCount));
            command.vertexCount = static_cast<Uint32>(sdlGpuVb.GetVertexCount()) - static_cast<Uint32>(vertexStart);
        }
        else
        {
            command.vertexCount = static_cast<Uint32>(PrimitiveVertexCount(primitive, primitiveCount));
        }

        command.target = CurrentDrawTarget();
        skinnedDrawCommands_.push_back(std::move(command));
        PushDrawOrder(DrawKind::Skinned, skinnedDrawCommands_.size() - 1);
        framePending_ = true;
    }

    void SdlGpuGraphicsBackend::QueuePbrDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                             const Matrix& world, const Matrix& view, const Matrix& projection,
                                             PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferBackend&>(vb);
        const std::size_t stride = sdlGpuVb.Stride();
        const bool skinned = params.skinned;
        const std::size_t expectedStride = skinned ? 68u : 48u;
        if (stride != expectedStride)
            throw std::invalid_argument(skinned
                ? "CNA SDL_GPU: pbr_skinned3d requires a stride-68 "
                  "(VertexPositionNormalTangentTextureSkinned) vertex buffer"
                : "CNA SDL_GPU: pbr3d requires a stride-48 "
                  "(VertexPositionNormalTangentTexture) vertex buffer");

        EnsureDefaultPbrTextures();

        PbrDrawCommand command;
        command.skinned = skinned;
        const int vertexStart = params.vertexStart;
        const auto& shadow = sdlGpuVb.ShadowData();
        const std::size_t byteOffset = static_cast<std::size_t>(vertexStart) * stride;
        if (byteOffset <= shadow.size())
            command.vertexData.assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset), shadow.end());
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        command.renderState = CaptureRenderState();
        const Matrix wvp = world * view * projection;
        FillExtUniforms(command.uniforms, wvp, params);
        FillFogUniforms(command.fogUniforms, params);  // REMED-GFX-009
        FillLitLightUniforms(command.lightUniforms, params);
        if (skinned)
        {
            // FillLitLightUniforms already wrote eyePos_pad's xyz; only the otherwise-unused w
            // slot (WeightsPerVertex) needs the same SkinnedLightParams packing
            // FillSkinnedLightUniforms uses -- see pbr_skinned3d.vert.glsl's own doc comment.
            command.lightUniforms[39] = static_cast<float>(params.weightsPerVertex);
            FillSkinnedBoneUniforms(command.boneUniforms, params);
        }
        FillPbrParams(command.pbrParams, params);
        command.texture = static_cast<const SdlGpuTextureBackend*>(params.texture0);
        command.normalMap = static_cast<const SdlGpuTextureBackend*>(params.pbrNormalMap);
        command.metallicRoughnessMap = static_cast<const SdlGpuTextureBackend*>(params.pbrMetallicRoughnessMap);
        command.emissiveMap = static_cast<const SdlGpuTextureBackend*>(params.pbrEmissiveMap);
        command.occlusionMap = static_cast<const SdlGpuTextureBackend*>(params.pbrOcclusionMap);
        command.textureFilter = samplerSlots_[0].filter;
        command.addressU = samplerSlots_[0].addressU;
        command.addressV = samplerSlots_[0].addressV;

        if (ib != nullptr)
        {
            const auto& sdlGpuIb = static_cast<const SdlGpuIndexBufferBackend&>(*ib);
            command.indexed = true;
            command.index32 = sdlGpuIb.IsThirtyTwoBit();
            command.indexData = sdlGpuIb.ShadowData();
            command.indexCount = static_cast<Uint32>(PrimitiveIndexCount(primitive, primitiveCount));
            command.vertexCount = static_cast<Uint32>(sdlGpuVb.GetVertexCount()) - static_cast<Uint32>(vertexStart);
        }
        else
        {
            command.vertexCount = static_cast<Uint32>(PrimitiveVertexCount(primitive, primitiveCount));
        }

        command.target = CurrentDrawTarget();
        pbrDrawCommands_.push_back(std::move(command));
        PushDrawOrder(DrawKind::Pbr, pbrDrawCommands_.size() - 1);
        framePending_ = true;
    }

    void SdlGpuGraphicsBackend::IssueAlphaTestDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                   const AlphaTestDrawCommand& command, SDL_GPUTextureFormat colorFormat,
                                                   SDL_GPUSampleCount sampleCount, SDL_GPUGraphicsPipeline*& boundPipeline)
    {
        SDL_GPUGraphicsPipeline* pipeline = GetOrCreatePipelineAlphaTest3D(
            command.stride, command.topology, command.depthTest, command.depthWrite, command.depthFunc, colorFormat, sampleCount, command.renderState);
        if (pipeline != boundPipeline) { SDL_BindGPUGraphicsPipeline(pass, pipeline); boundPipeline = pipeline; }
        SDL_SetGPUStencilReference(pass, static_cast<Uint8>(command.renderState.stencilReference));
        SDL_PushGPUVertexUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));
        SDL_PushGPUVertexUniformData(cmd, 1, command.fogUniforms.data(), sizeof(command.fogUniforms));  // REMED-GFX-009
        SDL_PushGPUFragmentUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));

        SDL_GPUBufferBinding vbBinding{};
        vbBinding.buffer = command.uploadedVertexBuffer;
        SDL_BindGPUVertexBuffers(pass, 0, &vbBinding, 1);

        SDL_GPUTextureSamplerBinding samplerBinding{};
        samplerBinding.texture = command.texture->Texture();
        samplerBinding.sampler = GetOrCreateSampler(command.textureFilter, command.addressU, command.addressV);
        SDL_BindGPUFragmentSamplers(pass, 0, &samplerBinding, 1);

        if (command.indexed && command.uploadedIndexBuffer != nullptr)
        {
            SDL_GPUBufferBinding ibBinding{};
            ibBinding.buffer = command.uploadedIndexBuffer;
            SDL_BindGPUIndexBuffer(pass, &ibBinding,
                                   command.index32 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT);
            SDL_DrawGPUIndexedPrimitives(pass, command.indexCount, 1, 0, 0, 0);
        }
        else
        {
            SDL_DrawGPUPrimitives(pass, command.vertexCount, 1, 0, 0);
        }
    }

    void SdlGpuGraphicsBackend::IssueDualTextureDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                     const DualTextureDrawCommand& command, SDL_GPUTextureFormat colorFormat,
                                                     SDL_GPUSampleCount sampleCount, SDL_GPUGraphicsPipeline*& boundPipeline)
    {
        SDL_GPUGraphicsPipeline* pipeline = GetOrCreatePipelineDualTexture3D(
            command.hasVertexColor ? 24 : 20, command.topology, command.depthTest, command.depthWrite, command.depthFunc, colorFormat, sampleCount, command.renderState);
        if (pipeline != boundPipeline) { SDL_BindGPUGraphicsPipeline(pass, pipeline); boundPipeline = pipeline; }
        SDL_SetGPUStencilReference(pass, static_cast<Uint8>(command.renderState.stencilReference));
        SDL_PushGPUVertexUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));
        SDL_PushGPUVertexUniformData(cmd, 1, command.fogUniforms.data(), sizeof(command.fogUniforms));  // REMED-GFX-009

        SDL_GPUBufferBinding vbBinding{};
        vbBinding.buffer = command.uploadedVertexBuffer;
        SDL_BindGPUVertexBuffers(pass, 0, &vbBinding, 1);

        // SDLGPU-21: texture0/texture1 are independent GraphicsDevice.SamplerStates[0]/[1]
        // slots in real XNA -- each gets its own sampler object, not a shared one.
        SDL_GPUTextureSamplerBinding samplerBindings[2]{};
        samplerBindings[0].texture = command.texture0->Texture();
        samplerBindings[0].sampler = GetOrCreateSampler(command.texture0Filter, command.texture0AddressU, command.texture0AddressV);
        samplerBindings[1].texture = command.texture1->Texture();
        samplerBindings[1].sampler = GetOrCreateSampler(command.texture1Filter, command.texture1AddressU, command.texture1AddressV);
        SDL_BindGPUFragmentSamplers(pass, 0, samplerBindings, 2);

        if (command.indexed && command.uploadedIndexBuffer != nullptr)
        {
            SDL_GPUBufferBinding ibBinding{};
            ibBinding.buffer = command.uploadedIndexBuffer;
            SDL_BindGPUIndexBuffer(pass, &ibBinding,
                                   command.index32 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT);
            SDL_DrawGPUIndexedPrimitives(pass, command.indexCount, 1, 0, 0, 0);
        }
        else
        {
            SDL_DrawGPUPrimitives(pass, command.vertexCount, 1, 0, 0);
        }
    }

    void SdlGpuGraphicsBackend::IssueEnvMapDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                const EnvMapDrawCommand& command, SDL_GPUTextureFormat colorFormat,
                                                SDL_GPUSampleCount sampleCount, SDL_GPUGraphicsPipeline*& boundPipeline)
    {
        SDL_GPUGraphicsPipeline* pipeline = GetOrCreatePipelineEnvMap3D(
            command.topology, command.depthTest, command.depthWrite, command.depthFunc, colorFormat, sampleCount, command.renderState);
        if (pipeline != boundPipeline) { SDL_BindGPUGraphicsPipeline(pass, pipeline); boundPipeline = pipeline; }
        SDL_SetGPUStencilReference(pass, static_cast<Uint8>(command.renderState.stencilReference));
        SDL_PushGPUVertexUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));
        SDL_PushGPUVertexUniformData(cmd, 1, command.envMapUniforms.data(), sizeof(command.envMapUniforms));
        SDL_PushGPUVertexUniformData(cmd, 2, command.fogUniforms.data(), sizeof(command.fogUniforms));  // REMED-GFX-009
        SDL_PushGPUFragmentUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));
        SDL_PushGPUFragmentUniformData(cmd, 1, command.envMapUniforms.data(), sizeof(command.envMapUniforms));

        SDL_GPUBufferBinding vbBinding{};
        vbBinding.buffer = command.uploadedVertexBuffer;
        SDL_BindGPUVertexBuffers(pass, 0, &vbBinding, 1);

        // The env map is always sampled Linear+Clamp regardless of texture0's own filter/
        // address settings -- matches this project's other backends' fixed reflection-map
        // sampling convention (address mode is largely moot for a direction-addressed cube map).
        SDL_GPUTextureSamplerBinding samplerBindings[2]{};
        samplerBindings[0].texture = command.texture->Texture();
        samplerBindings[0].sampler = GetOrCreateSampler(command.textureFilter, command.addressU, command.addressV);
        samplerBindings[1].texture = command.envMapTexture;
        samplerBindings[1].sampler = GetOrCreateSampler(0, 1, 1);
        SDL_BindGPUFragmentSamplers(pass, 0, samplerBindings, 2);

        if (command.indexed && command.uploadedIndexBuffer != nullptr)
        {
            SDL_GPUBufferBinding ibBinding{};
            ibBinding.buffer = command.uploadedIndexBuffer;
            SDL_BindGPUIndexBuffer(pass, &ibBinding,
                                   command.index32 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT);
            SDL_DrawGPUIndexedPrimitives(pass, command.indexCount, 1, 0, 0, 0);
        }
        else
        {
            SDL_DrawGPUPrimitives(pass, command.vertexCount, 1, 0, 0);
        }
    }

    void SdlGpuGraphicsBackend::IssueSkinnedDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                 const SkinnedDrawCommand& command, SDL_GPUTextureFormat colorFormat,
                                                 SDL_GPUSampleCount sampleCount, SDL_GPUGraphicsPipeline*& boundPipeline)
    {
        SDL_GPUGraphicsPipeline* pipeline = GetOrCreatePipelineSkinned3D(
            command.hasVertexColor, command.topology, command.depthTest, command.depthWrite, command.depthFunc, colorFormat, sampleCount, command.renderState);
        if (pipeline != boundPipeline) { SDL_BindGPUGraphicsPipeline(pass, pipeline); boundPipeline = pipeline; }
        SDL_SetGPUStencilReference(pass, static_cast<Uint8>(command.renderState.stencilReference));
        SDL_PushGPUVertexUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));
        SDL_PushGPUVertexUniformData(cmd, 1, command.lightUniforms.data(), sizeof(command.lightUniforms));
        SDL_PushGPUVertexUniformData(cmd, 2, command.fogUniforms.data(), sizeof(command.fogUniforms));  // REMED-GFX-009
        // Both the stride-52 (litTexturedFragmentShader_, reused unchanged) and stride-56
        // (skinnedColoredFragmentShader_) fragment shaders expect PC at slot 0 and a
        // LitLightParams-shaped block at slot 1 -- SkinnedLightParams is byte-identical to both.
        SDL_PushGPUFragmentUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));
        SDL_PushGPUFragmentUniformData(cmd, 1, command.lightUniforms.data(), sizeof(command.lightUniforms));

        SDL_GPUBufferBinding vbBinding{};
        vbBinding.buffer = command.uploadedVertexBuffer;
        SDL_BindGPUVertexBuffers(pass, 0, &vbBinding, 1);
        // The 72-bone palette -- a real storage buffer, not a uniform push (see
        // SkinnedDrawCommand's own doc comment for why).
        SDL_BindGPUVertexStorageBuffers(pass, 0, &command.uploadedBoneBuffer, 1);

        SDL_GPUTextureSamplerBinding samplerBinding{};
        samplerBinding.texture = command.texture->Texture();
        samplerBinding.sampler = GetOrCreateSampler(command.textureFilter, command.addressU, command.addressV);
        SDL_BindGPUFragmentSamplers(pass, 0, &samplerBinding, 1);

        if (command.indexed && command.uploadedIndexBuffer != nullptr)
        {
            SDL_GPUBufferBinding ibBinding{};
            ibBinding.buffer = command.uploadedIndexBuffer;
            SDL_BindGPUIndexBuffer(pass, &ibBinding,
                                   command.index32 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT);
            SDL_DrawGPUIndexedPrimitives(pass, command.indexCount, 1, 0, 0, 0);
        }
        else
        {
            SDL_DrawGPUPrimitives(pass, command.vertexCount, 1, 0, 0);
        }
    }

    void SdlGpuGraphicsBackend::IssuePbrDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                             const PbrDrawCommand& command, SDL_GPUTextureFormat colorFormat,
                                             SDL_GPUSampleCount sampleCount, SDL_GPUGraphicsPipeline*& boundPipeline)
    {
        SDL_GPUGraphicsPipeline* pipeline = GetOrCreatePipelinePbr3D(
            command.skinned, command.topology, command.depthTest, command.depthWrite, command.depthFunc, colorFormat, sampleCount, command.renderState);
        if (pipeline != boundPipeline) { SDL_BindGPUGraphicsPipeline(pass, pipeline); boundPipeline = pipeline; }
        SDL_SetGPUStencilReference(pass, static_cast<Uint8>(command.renderState.stencilReference));
        SDL_PushGPUVertexUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));
        SDL_PushGPUVertexUniformData(cmd, 1, command.lightUniforms.data(), sizeof(command.lightUniforms));
        SDL_PushGPUVertexUniformData(cmd, 2, command.fogUniforms.data(), sizeof(command.fogUniforms));  // REMED-GFX-009
        SDL_PushGPUFragmentUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));
        SDL_PushGPUFragmentUniformData(cmd, 1, command.lightUniforms.data(), sizeof(command.lightUniforms));
        SDL_PushGPUFragmentUniformData(cmd, 2, command.pbrParams.data(), sizeof(command.pbrParams));

        SDL_GPUBufferBinding vbBinding{};
        vbBinding.buffer = command.uploadedVertexBuffer;
        SDL_BindGPUVertexBuffers(pass, 0, &vbBinding, 1);
        // The unskinned pbrVertexShader_ declares zero storage buffers -- only bind the bone
        // palette for the skinned variant (pbrSkinnedVertexShader_), mirroring
        // SkinnedDrawCommand's own storage-buffer-over-uniform-push rationale.
        if (command.skinned)
            SDL_BindGPUVertexStorageBuffers(pass, 0, &command.uploadedBoneBuffer, 1);

        // 5 samplers: base color, normal, metallic-roughness, emissive, occlusion. Optional maps
        // fall back to the lazily-created default textures (EnsureDefaultPbrTextures(), already
        // invoked at Queue-time) so "map absent" reads as the correct neutral value per semantic
        // (flat normal, factor-only, no emissive tint, fully lit) -- mirrors
        // EasyGLGraphicsBackend::BindDrawParams()'s identical fallback set. All 5 share the base
        // color texture's own sampler state (GraphicsDevice.SamplerStates[0]) -- GpuDrawParams has
        // no independent per-PBR-map sampler state to select from.
        SDL_GPUTextureSamplerBinding samplerBindings[5]{};
        SDL_GPUSampler* sampler = GetOrCreateSampler(command.textureFilter, command.addressU, command.addressV);
        samplerBindings[0].texture = command.texture->Texture();
        samplerBindings[0].sampler = sampler;
        samplerBindings[1].texture = command.normalMap != nullptr ? command.normalMap->Texture() : defaultFlatNormalTexture_->Texture();
        samplerBindings[1].sampler = sampler;
        samplerBindings[2].texture = command.metallicRoughnessMap != nullptr ? command.metallicRoughnessMap->Texture() : defaultWhiteTexture_->Texture();
        samplerBindings[2].sampler = sampler;
        samplerBindings[3].texture = command.emissiveMap != nullptr ? command.emissiveMap->Texture() : defaultWhiteTexture_->Texture();
        samplerBindings[3].sampler = sampler;
        samplerBindings[4].texture = command.occlusionMap != nullptr ? command.occlusionMap->Texture() : defaultWhiteTexture_->Texture();
        samplerBindings[4].sampler = sampler;
        SDL_BindGPUFragmentSamplers(pass, 0, samplerBindings, 5);

        if (command.indexed && command.uploadedIndexBuffer != nullptr)
        {
            SDL_GPUBufferBinding ibBinding{};
            ibBinding.buffer = command.uploadedIndexBuffer;
            SDL_BindGPUIndexBuffer(pass, &ibBinding,
                                   command.index32 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT);
            SDL_DrawGPUIndexedPrimitives(pass, command.indexCount, 1, 0, 0, 0);
        }
        else
        {
            SDL_DrawGPUPrimitives(pass, command.vertexCount, 1, 0, 0);
        }
    }

    void SdlGpuGraphicsBackend::UploadSceneDrawData(SDL_GPUCommandBuffer* cmd)
    {
        if (coloredDrawCommands_.empty() && texturedDrawCommands_.empty() && litTexturedDrawCommands_.empty() &&
            alphaTestDrawCommands_.empty() && dualTextureDrawCommands_.empty() && envMapDrawCommands_.empty() &&
            skinnedDrawCommands_.empty() && pbrDrawCommands_.empty())
            return;

        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);

        auto uploadOne = [&](const std::vector<std::uint8_t>& data, SDL_GPUBufferUsageFlags usage) -> SDL_GPUBuffer*
        {
            if (data.empty())
                return nullptr;
            const Uint32 sizeBytes = static_cast<Uint32>(data.size());
            SDL_GPUBufferCreateInfo bufferInfo{};
            bufferInfo.usage = usage;
            bufferInfo.size = sizeBytes;
            SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(device_, &bufferInfo);
            if (buffer == nullptr)
                throw std::runtime_error(std::string("CNA SDL_GPU: failed to create scene draw buffer: ") + SDL_GetError());

            SDL_GPUTransferBufferCreateInfo transferInfo{};
            transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            transferInfo.size = sizeBytes;
            SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(device_, &transferInfo);
            if (transferBuffer == nullptr)
            {
                SDL_ReleaseGPUBuffer(device_, buffer);
                throw std::runtime_error(std::string("CNA SDL_GPU: failed to create scene draw transfer buffer: ") + SDL_GetError());
            }
            void* mapped = SDL_MapGPUTransferBuffer(device_, transferBuffer, false);
            if (mapped == nullptr)
            {
                SDL_ReleaseGPUTransferBuffer(device_, transferBuffer);
                SDL_ReleaseGPUBuffer(device_, buffer);
                throw std::runtime_error(std::string("CNA SDL_GPU: failed to map scene draw transfer buffer: ") + SDL_GetError());
            }
            std::memcpy(mapped, data.data(), sizeBytes);
            SDL_UnmapGPUTransferBuffer(device_, transferBuffer);

            SDL_GPUTransferBufferLocation source{};
            source.transfer_buffer = transferBuffer;
            SDL_GPUBufferRegion destRegion{};
            destRegion.buffer = buffer;
            destRegion.size = sizeBytes;
            SDL_UploadToGPUBuffer(copyPass, &source, &destRegion, true);
            SDL_ReleaseGPUTransferBuffer(device_, transferBuffer);
            return buffer;
        };

        for (ColoredDrawCommand& command : coloredDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty())
                continue;
            command.uploadedVertexBuffer = uploadOne(command.vertexData, SDL_GPU_BUFFERUSAGE_VERTEX);
            if (command.indexed && !command.indexData.empty())
                command.uploadedIndexBuffer = uploadOne(command.indexData, SDL_GPU_BUFFERUSAGE_INDEX);
        }
        for (TexturedDrawCommand& command : texturedDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty())
                continue;
            command.uploadedVertexBuffer = uploadOne(command.vertexData, SDL_GPU_BUFFERUSAGE_VERTEX);
            if (command.indexed && !command.indexData.empty())
                command.uploadedIndexBuffer = uploadOne(command.indexData, SDL_GPU_BUFFERUSAGE_INDEX);
        }
        for (LitTexturedDrawCommand& command : litTexturedDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty())
                continue;
            command.uploadedVertexBuffer = uploadOne(command.vertexData, SDL_GPU_BUFFERUSAGE_VERTEX);
            if (command.indexed && !command.indexData.empty())
                command.uploadedIndexBuffer = uploadOne(command.indexData, SDL_GPU_BUFFERUSAGE_INDEX);
        }
        for (AlphaTestDrawCommand& command : alphaTestDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty())
                continue;
            command.uploadedVertexBuffer = uploadOne(command.vertexData, SDL_GPU_BUFFERUSAGE_VERTEX);
            if (command.indexed && !command.indexData.empty())
                command.uploadedIndexBuffer = uploadOne(command.indexData, SDL_GPU_BUFFERUSAGE_INDEX);
        }
        for (DualTextureDrawCommand& command : dualTextureDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty())
                continue;
            command.uploadedVertexBuffer = uploadOne(command.vertexData, SDL_GPU_BUFFERUSAGE_VERTEX);
            if (command.indexed && !command.indexData.empty())
                command.uploadedIndexBuffer = uploadOne(command.indexData, SDL_GPU_BUFFERUSAGE_INDEX);
        }
        for (EnvMapDrawCommand& command : envMapDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty())
                continue;
            command.uploadedVertexBuffer = uploadOne(command.vertexData, SDL_GPU_BUFFERUSAGE_VERTEX);
            if (command.indexed && !command.indexData.empty())
                command.uploadedIndexBuffer = uploadOne(command.indexData, SDL_GPU_BUFFERUSAGE_INDEX);
        }
        for (SkinnedDrawCommand& command : skinnedDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty())
                continue;
            command.uploadedVertexBuffer = uploadOne(command.vertexData, SDL_GPU_BUFFERUSAGE_VERTEX);
            if (command.indexed && !command.indexData.empty())
                command.uploadedIndexBuffer = uploadOne(command.indexData, SDL_GPU_BUFFERUSAGE_INDEX);
            // Uploaded as a storage buffer, not pushed via SDL_PushGPUVertexUniformData -- see
            // SkinnedDrawCommand's own doc comment for why.
            const auto* boneBytes = reinterpret_cast<const std::uint8_t*>(command.boneUniforms.data());
            const std::vector<std::uint8_t> boneData(boneBytes, boneBytes + sizeof(command.boneUniforms));
            command.uploadedBoneBuffer = uploadOne(boneData, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
        }
        for (PbrDrawCommand& command : pbrDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty())
                continue;
            command.uploadedVertexBuffer = uploadOne(command.vertexData, SDL_GPU_BUFFERUSAGE_VERTEX);
            if (command.indexed && !command.indexData.empty())
                command.uploadedIndexBuffer = uploadOne(command.indexData, SDL_GPU_BUFFERUSAGE_INDEX);
            if (command.skinned)
            {
                const auto* boneBytes = reinterpret_cast<const std::uint8_t*>(command.boneUniforms.data());
                const std::vector<std::uint8_t> boneData(boneBytes, boneBytes + sizeof(command.boneUniforms));
                command.uploadedBoneBuffer = uploadOne(boneData, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
            }
        }

        SDL_EndGPUCopyPass(copyPass);
    }

    void SdlGpuGraphicsBackend::IssueColoredDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                 const ColoredDrawCommand& command, SDL_GPUTextureFormat colorFormat,
                                                 SDL_GPUSampleCount sampleCount, SDL_GPUGraphicsPipeline*& boundPipeline)
    {
        SDL_GPUGraphicsPipeline* pipeline = GetOrCreatePipelineColored3D(command.topology, command.depthTest,
                                                                          command.depthWrite, command.depthFunc, colorFormat,
                                                                          sampleCount, command.renderState);
        if (pipeline != boundPipeline) { SDL_BindGPUGraphicsPipeline(pass, pipeline); boundPipeline = pipeline; }
        SDL_SetGPUStencilReference(pass, static_cast<Uint8>(command.renderState.stencilReference));
        SDL_PushGPUVertexUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));
        SDL_PushGPUVertexUniformData(cmd, 1, command.fogUniforms.data(), sizeof(command.fogUniforms));  // REMED-GFX-009

        SDL_GPUBufferBinding vbBinding{};
        vbBinding.buffer = command.uploadedVertexBuffer;
        SDL_BindGPUVertexBuffers(pass, 0, &vbBinding, 1);

        if (command.indexed && command.uploadedIndexBuffer != nullptr)
        {
            SDL_GPUBufferBinding ibBinding{};
            ibBinding.buffer = command.uploadedIndexBuffer;
            SDL_BindGPUIndexBuffer(pass, &ibBinding,
                                   command.index32 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT);
            SDL_DrawGPUIndexedPrimitives(pass, command.indexCount, 1, 0, 0, 0);
        }
        else
        {
            SDL_DrawGPUPrimitives(pass, command.vertexCount, 1, 0, 0);
        }
    }

    void SdlGpuGraphicsBackend::IssueTexturedDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                  const TexturedDrawCommand& command, SDL_GPUTextureFormat colorFormat,
                                                  SDL_GPUSampleCount sampleCount, SDL_GPUGraphicsPipeline*& boundPipeline)
    {
        SDL_GPUGraphicsPipeline* pipeline = command.hasVertexColor
            ? GetOrCreatePipelineColoredTextured3D(command.topology, command.depthTest, command.depthWrite, command.depthFunc, colorFormat, sampleCount, command.renderState)
            : GetOrCreatePipelineTextured3D(command.topology, command.depthTest, command.depthWrite, command.depthFunc, colorFormat, sampleCount, command.renderState);
        if (pipeline != boundPipeline) { SDL_BindGPUGraphicsPipeline(pass, pipeline); boundPipeline = pipeline; }
        SDL_SetGPUStencilReference(pass, static_cast<Uint8>(command.renderState.stencilReference));
        SDL_PushGPUVertexUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));
        SDL_PushGPUVertexUniformData(cmd, 1, command.fogUniforms.data(), sizeof(command.fogUniforms));  // REMED-GFX-009
        SDL_PushGPUFragmentUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));

        SDL_GPUBufferBinding vbBinding{};
        vbBinding.buffer = command.uploadedVertexBuffer;
        SDL_BindGPUVertexBuffers(pass, 0, &vbBinding, 1);

        SDL_GPUTextureSamplerBinding samplerBinding{};
        samplerBinding.texture = command.texture->Texture();
        samplerBinding.sampler = GetOrCreateSampler(command.textureFilter, command.addressU, command.addressV);
        SDL_BindGPUFragmentSamplers(pass, 0, &samplerBinding, 1);

        if (command.indexed && command.uploadedIndexBuffer != nullptr)
        {
            SDL_GPUBufferBinding ibBinding{};
            ibBinding.buffer = command.uploadedIndexBuffer;
            SDL_BindGPUIndexBuffer(pass, &ibBinding,
                                   command.index32 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT);
            SDL_DrawGPUIndexedPrimitives(pass, command.indexCount, 1, 0, 0, 0);
        }
        else
        {
            SDL_DrawGPUPrimitives(pass, command.vertexCount, 1, 0, 0);
        }
    }

    void SdlGpuGraphicsBackend::IssueLitTexturedDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                     const LitTexturedDrawCommand& command, SDL_GPUTextureFormat colorFormat,
                                                     SDL_GPUSampleCount sampleCount, SDL_GPUGraphicsPipeline*& boundPipeline)
    {
        SDL_GPUGraphicsPipeline* pipeline = GetOrCreatePipelineLitTextured3D(
            command.topology, command.depthTest, command.depthWrite, command.depthFunc, colorFormat, sampleCount, command.renderState);
        if (pipeline != boundPipeline) { SDL_BindGPUGraphicsPipeline(pass, pipeline); boundPipeline = pipeline; }
        SDL_SetGPUStencilReference(pass, static_cast<Uint8>(command.renderState.stencilReference));
        SDL_PushGPUVertexUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));
        SDL_PushGPUVertexUniformData(cmd, 1, command.lightUniforms.data(), sizeof(command.lightUniforms));
        SDL_PushGPUVertexUniformData(cmd, 2, command.fogUniforms.data(), sizeof(command.fogUniforms));  // REMED-GFX-009
        SDL_PushGPUFragmentUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));
        SDL_PushGPUFragmentUniformData(cmd, 1, command.lightUniforms.data(), sizeof(command.lightUniforms));

        SDL_GPUBufferBinding vbBinding{};
        vbBinding.buffer = command.uploadedVertexBuffer;
        SDL_BindGPUVertexBuffers(pass, 0, &vbBinding, 1);

        SDL_GPUTextureSamplerBinding samplerBinding{};
        samplerBinding.texture = command.texture->Texture();
        samplerBinding.sampler = GetOrCreateSampler(command.textureFilter, command.addressU, command.addressV);
        SDL_BindGPUFragmentSamplers(pass, 0, &samplerBinding, 1);

        if (command.indexed && command.uploadedIndexBuffer != nullptr)
        {
            SDL_GPUBufferBinding ibBinding{};
            ibBinding.buffer = command.uploadedIndexBuffer;
            SDL_BindGPUIndexBuffer(pass, &ibBinding,
                                   command.index32 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT);
            SDL_DrawGPUIndexedPrimitives(pass, command.indexCount, 1, 0, 0, 0);
        }
        else
        {
            SDL_DrawGPUPrimitives(pass, command.vertexCount, 1, 0, 0);
        }
    }

    // Adversarial-review finding #4 (draw ordering): replaces the old fixed
    // "RenderColoredDraws(); RenderTexturedDraws(); ... RenderSprites();" sequence -- drawOrder_ is
    // already in real chronological Queue*Draw()/QueueSprite() issue order (see that field's own
    // doc comment), so a single pass over it, dispatching each ref to its own Issue*Draw()
    // function, is all real interleaving needs. Each case's own readiness/target filter mirrors
    // exactly what the old per-family loop used to skip -- only the ORDER changed.
    void SdlGpuGraphicsBackend::RenderQueuedDraws(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                  const DrawTarget& target, SDL_GPUTextureFormat colorFormat,
                                                  SDL_GPUSampleCount sampleCount, int colorTargetCount)
    {
        const float viewportSize[2] = {
            static_cast<float>(target.rt != nullptr ? target.rt->width
                              : target.cube != nullptr ? target.cube->size : physicalWidth_),
            static_cast<float>(target.rt != nullptr ? target.rt->height
                              : target.cube != nullptr ? target.cube->size : physicalHeight_)};

        SDL_GPUGraphicsPipeline* boundPipeline = nullptr;
        for (const QueuedDrawRef& ref : drawOrder_)
        {
            // REMED-GFX-064: apply this draw's own captured GraphicsDevice.Viewport before it is
            // issued. SDL_SetGPUViewport is pass-state that persists until changed, so setting it
            // per draw makes each draw honor the viewport it was enqueued under -- essential here
            // because SetRenderTarget resets the frame-global viewport on unbind, so the live
            // viewport at Present is not the sub-region an RT draw used. Refs not targeting this
            // pass do not draw; their apply is harmlessly overwritten before the next real draw.
            ApplyViewportForRef(pass, ref, static_cast<int>(viewportSize[0]), static_cast<int>(viewportSize[1]));
            // REMED-GFX-068: apply this draw's own captured scissor (rect + ScissorTestEnable) for
            // exactly the same deferred-model reason as the viewport above -- SetRenderTarget resets
            // ScissorRectangle to the full target on unbind, so a per-pass read would clip RT draws
            // with the post-unbind full-backbuffer rect. Same "harmlessly overwritten for non-target
            // refs" property as the viewport.
            ApplyScissorForRef(pass, ref, static_cast<int>(viewportSize[0]), static_cast<int>(viewportSize[1]));
            // REMED-GFX-069: apply this draw's own captured GraphicsDevice.BlendFactor before it is
            // issued, for the same deferred-model reason as the viewport/scissor above -- SdlGpu
            // replays queued draws at Present, so a live-member read here would give every draw the
            // last BlendFactor set that frame. Same "harmlessly overwritten for non-target refs"
            // property; inert for pipelines that don't use the constant blend factor.
            ApplyBlendFactorForRef(pass, ref);
            switch (ref.kind)
            {
                case DrawKind::Colored:
                {
                    const ColoredDrawCommand& c = coloredDrawCommands_[ref.index];
                    if (c.uploadedVertexBuffer != nullptr && c.target == target)
                        IssueColoredDraw(pass, cmd, c, colorFormat, sampleCount, boundPipeline);
                    break;
                }
                case DrawKind::Textured:
                {
                    const TexturedDrawCommand& c = texturedDrawCommands_[ref.index];
                    if (c.uploadedVertexBuffer != nullptr && c.texture != nullptr && c.target == target)
                        IssueTexturedDraw(pass, cmd, c, colorFormat, sampleCount, boundPipeline);
                    break;
                }
                case DrawKind::LitTextured:
                {
                    const LitTexturedDrawCommand& c = litTexturedDrawCommands_[ref.index];
                    if (c.uploadedVertexBuffer != nullptr && c.texture != nullptr && c.target == target)
                        IssueLitTexturedDraw(pass, cmd, c, colorFormat, sampleCount, boundPipeline);
                    break;
                }
                case DrawKind::AlphaTest:
                {
                    const AlphaTestDrawCommand& c = alphaTestDrawCommands_[ref.index];
                    if (c.uploadedVertexBuffer != nullptr && c.texture != nullptr && c.target == target)
                        IssueAlphaTestDraw(pass, cmd, c, colorFormat, sampleCount, boundPipeline);
                    break;
                }
                case DrawKind::DualTexture:
                {
                    const DualTextureDrawCommand& c = dualTextureDrawCommands_[ref.index];
                    if (c.uploadedVertexBuffer != nullptr && c.texture0 != nullptr && c.texture1 != nullptr && c.target == target)
                        IssueDualTextureDraw(pass, cmd, c, colorFormat, sampleCount, boundPipeline);
                    break;
                }
                case DrawKind::EnvMap:
                {
                    const EnvMapDrawCommand& c = envMapDrawCommands_[ref.index];
                    if (c.uploadedVertexBuffer != nullptr && c.texture != nullptr && c.envMapTexture != nullptr && c.target == target)
                        IssueEnvMapDraw(pass, cmd, c, colorFormat, sampleCount, boundPipeline);
                    break;
                }
                case DrawKind::Skinned:
                {
                    const SkinnedDrawCommand& c = skinnedDrawCommands_[ref.index];
                    if (c.uploadedVertexBuffer != nullptr && c.uploadedBoneBuffer != nullptr && c.texture != nullptr && c.target == target)
                        IssueSkinnedDraw(pass, cmd, c, colorFormat, sampleCount, boundPipeline);
                    break;
                }
                case DrawKind::Pbr:
                {
                    const PbrDrawCommand& c = pbrDrawCommands_[ref.index];
                    if (c.uploadedVertexBuffer != nullptr && c.texture != nullptr
                        && (!c.skinned || c.uploadedBoneBuffer != nullptr) && c.target == target)
                        IssuePbrDraw(pass, cmd, c, colorFormat, sampleCount, boundPipeline);
                    break;
                }
                case DrawKind::Sprite:
                {
                    const SpriteCommand& c = spriteCommands_[ref.index];
                    if (c.target == target)
                    {
                        // REMED-GFX-072: the sprite2d shader divides pixel positions by this size to
                        // reach NDC. A custom sub-Viewport makes sprite coordinates viewport-local
                        // (QueueSprite baked raw local coords for this ref), so divide by Viewport.W/H
                        // instead of the full target -- the ApplyViewportForRef call above already
                        // positions the [-1,1] result at Viewport.X/Y. Default viewport keeps the
                        // full-target divisor (byte-identical for every existing sprite).
                        float spriteVpSize[2] = { viewportSize[0], viewportSize[1] };
                        if (ref.viewportSet && ref.viewportW > 0 && ref.viewportH > 0 &&
                            (ref.viewportX != 0 || ref.viewportY != 0 ||
                             ref.viewportW != static_cast<int>(viewportSize[0]) ||
                             ref.viewportH != static_cast<int>(viewportSize[1])))
                        {
                            spriteVpSize[0] = static_cast<float>(ref.viewportW);
                            spriteVpSize[1] = static_cast<float>(ref.viewportH);
                        }
                        IssueSpriteDraw(pass, cmd, c, ref.index, spriteVpSize, colorFormat, sampleCount, colorTargetCount, boundPipeline);
                    }
                    break;
                }
            }
        }
    }

    void SdlGpuGraphicsBackend::ReleaseSceneDrawBuffers()
    {
        for (ColoredDrawCommand& command : coloredDrawCommands_)
        {
            if (command.uploadedVertexBuffer != nullptr) SDL_ReleaseGPUBuffer(device_, command.uploadedVertexBuffer);
            if (command.uploadedIndexBuffer != nullptr) SDL_ReleaseGPUBuffer(device_, command.uploadedIndexBuffer);
        }
        coloredDrawCommands_.clear();
        for (TexturedDrawCommand& command : texturedDrawCommands_)
        {
            if (command.uploadedVertexBuffer != nullptr) SDL_ReleaseGPUBuffer(device_, command.uploadedVertexBuffer);
            if (command.uploadedIndexBuffer != nullptr) SDL_ReleaseGPUBuffer(device_, command.uploadedIndexBuffer);
        }
        texturedDrawCommands_.clear();
        for (LitTexturedDrawCommand& command : litTexturedDrawCommands_)
        {
            if (command.uploadedVertexBuffer != nullptr) SDL_ReleaseGPUBuffer(device_, command.uploadedVertexBuffer);
            if (command.uploadedIndexBuffer != nullptr) SDL_ReleaseGPUBuffer(device_, command.uploadedIndexBuffer);
        }
        litTexturedDrawCommands_.clear();
        for (AlphaTestDrawCommand& command : alphaTestDrawCommands_)
        {
            if (command.uploadedVertexBuffer != nullptr) SDL_ReleaseGPUBuffer(device_, command.uploadedVertexBuffer);
            if (command.uploadedIndexBuffer != nullptr) SDL_ReleaseGPUBuffer(device_, command.uploadedIndexBuffer);
        }
        alphaTestDrawCommands_.clear();
        for (DualTextureDrawCommand& command : dualTextureDrawCommands_)
        {
            if (command.uploadedVertexBuffer != nullptr) SDL_ReleaseGPUBuffer(device_, command.uploadedVertexBuffer);
            if (command.uploadedIndexBuffer != nullptr) SDL_ReleaseGPUBuffer(device_, command.uploadedIndexBuffer);
        }
        dualTextureDrawCommands_.clear();
        for (EnvMapDrawCommand& command : envMapDrawCommands_)
        {
            if (command.uploadedVertexBuffer != nullptr) SDL_ReleaseGPUBuffer(device_, command.uploadedVertexBuffer);
            if (command.uploadedIndexBuffer != nullptr) SDL_ReleaseGPUBuffer(device_, command.uploadedIndexBuffer);
        }
        envMapDrawCommands_.clear();
        for (SkinnedDrawCommand& command : skinnedDrawCommands_)
        {
            if (command.uploadedVertexBuffer != nullptr) SDL_ReleaseGPUBuffer(device_, command.uploadedVertexBuffer);
            if (command.uploadedIndexBuffer != nullptr) SDL_ReleaseGPUBuffer(device_, command.uploadedIndexBuffer);
            if (command.uploadedBoneBuffer != nullptr) SDL_ReleaseGPUBuffer(device_, command.uploadedBoneBuffer);
        }
        skinnedDrawCommands_.clear();
        for (PbrDrawCommand& command : pbrDrawCommands_)
        {
            if (command.uploadedVertexBuffer != nullptr) SDL_ReleaseGPUBuffer(device_, command.uploadedVertexBuffer);
            if (command.uploadedIndexBuffer != nullptr) SDL_ReleaseGPUBuffer(device_, command.uploadedIndexBuffer);
            if (command.uploadedBoneBuffer != nullptr) SDL_ReleaseGPUBuffer(device_, command.uploadedBoneBuffer);
        }
        pbrDrawCommands_.clear();
    }

    void SdlGpuGraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                                       const Matrix& world, const Matrix& view, const Matrix& projection,
                                                       PrimitiveType primitive, int primitiveCount)
    {
        QueueColoredDraw(vb, nullptr, world, view, projection, primitive, primitiveCount);
    }

    void SdlGpuGraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                                              const IIndexBufferBackend& ib,
                                                              const Matrix& world, const Matrix& view, const Matrix& projection,
                                                              PrimitiveType primitive, int primitiveCount)
    {
        QueueColoredDraw(vb, &ib, world, view, projection, primitive, primitiveCount);
    }

    void SdlGpuGraphicsBackend::DrawPrimitivesEx(const IVertexBufferBackend& vb,
                                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                                  PrimitiveType primitive, int primitiveCount,
                                                  const GpuDrawParams& params)
    {
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferBackend&>(vb);
        const std::size_t stride = sdlGpuVb.Stride();
        // Matches VulkanGraphicsBackend/WebGPUGraphicsBackend's own dispatch precedence: alpha
        // test wins over dual-texture/env-map/pbr/skinned (an AlphaTestEffect draw on any of those
        // shapes never reaches those shaders); env-map/pbr/skinned win over plain lit_textured3d
        // (env-map shares stride 32, skinned has its own stride 52/56, pbr its own stride 48/68).
        // needsPbr is checked (and excluded from needsSkinned) before needsSkinned since
        // SkinnedPbrEffect::FillGpuDrawParams() sets BOTH params.pbr and params.skinned -- the PBR
        // shader variant, not the plain SkinnedEffect one, must win for that combination.
        const bool needsAlphaTest = params.alphaTest[2] < 0.0f || params.alphaTest[3] < 0.0f;
        const bool needsDualTexture = !needsAlphaTest && params.dualTexture;
        const bool needsEnvMap = !needsAlphaTest && !needsDualTexture && params.envMapping;
        const bool needsPbr = !needsAlphaTest && !needsDualTexture && !needsEnvMap && params.pbr;
        const bool needsSkinned = !needsAlphaTest && !needsDualTexture && !needsEnvMap && !needsPbr && params.skinned;
        if (needsAlphaTest && (stride == 20 || stride == 24 || stride == 32) && params.texture0 != nullptr)
        {
            QueueAlphaTestDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (needsDualTexture && (stride == 20 || stride == 24) && params.texture0 != nullptr && params.texture1 != nullptr)
        {
            QueueDualTextureDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (needsEnvMap && stride == 32 && params.texture0 != nullptr && params.envMap != nullptr)
        {
            QueueEnvMapDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (needsPbr && ((params.skinned && stride == 68) || (!params.skinned && stride == 48)) && params.texture0 != nullptr)
        {
            QueuePbrDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (needsSkinned && (stride == 52 || stride == 56) && params.texture0 != nullptr)
        {
            QueueSkinnedDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (stride == 16)
        {
            QueueColoredDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, &params);
            return;
        }
        if ((stride == 20 || stride == 24) && params.texture0 != nullptr)
        {
            QueueTexturedDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (stride == 32 && params.texture0 != nullptr)
        {
            QueueLitTexturedDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        DrawColoredPrimitives(vb, world, view, projection, primitive, primitiveCount);
    }

    void SdlGpuGraphicsBackend::DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                                         const Matrix& world, const Matrix& view, const Matrix& projection,
                                                         PrimitiveType primitive, int primitiveCount,
                                                         const GpuDrawParams& params)
    {
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferBackend&>(vb);
        const std::size_t stride = sdlGpuVb.Stride();
        const bool needsAlphaTest = params.alphaTest[2] < 0.0f || params.alphaTest[3] < 0.0f;
        const bool needsDualTexture = !needsAlphaTest && params.dualTexture;
        const bool needsEnvMap = !needsAlphaTest && !needsDualTexture && params.envMapping;
        const bool needsPbr = !needsAlphaTest && !needsDualTexture && !needsEnvMap && params.pbr;
        const bool needsSkinned = !needsAlphaTest && !needsDualTexture && !needsEnvMap && !needsPbr && params.skinned;
        if (needsAlphaTest && (stride == 20 || stride == 24 || stride == 32) && params.texture0 != nullptr)
        {
            QueueAlphaTestDraw(vb, &ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (needsDualTexture && (stride == 20 || stride == 24) && params.texture0 != nullptr && params.texture1 != nullptr)
        {
            QueueDualTextureDraw(vb, &ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (needsEnvMap && stride == 32 && params.texture0 != nullptr && params.envMap != nullptr)
        {
            QueueEnvMapDraw(vb, &ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (needsPbr && ((params.skinned && stride == 68) || (!params.skinned && stride == 48)) && params.texture0 != nullptr)
        {
            QueuePbrDraw(vb, &ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (needsSkinned && (stride == 52 || stride == 56) && params.texture0 != nullptr)
        {
            QueueSkinnedDraw(vb, &ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (stride == 16)
        {
            QueueColoredDraw(vb, &ib, world, view, projection, primitive, primitiveCount, &params);
            return;
        }
        if ((stride == 20 || stride == 24) && params.texture0 != nullptr)
        {
            QueueTexturedDraw(vb, &ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (stride == 32 && params.texture0 != nullptr)
        {
            QueueLitTexturedDraw(vb, &ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        DrawIndexedColoredPrimitives(vb, ib, world, view, projection, primitive, primitiveCount);
    }

    // ---- SdlGpuTextureBackend ----

    SdlGpuTextureBackend::SdlGpuTextureBackend(SdlGpuGraphicsBackend& owner, const ImageData& data)
        : owner_(&owner), width_(data.width), height_(data.height)
    {
        SDL_GPUTextureCreateInfo createInfo{};
        createInfo.type = SDL_GPU_TEXTURETYPE_2D;
        createInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        createInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        createInfo.width = static_cast<Uint32>(width_);
        createInfo.height = static_cast<Uint32>(height_);
        createInfo.layer_count_or_depth = 1;
        createInfo.num_levels = 1;
        createInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;

        texture_ = SDL_CreateGPUTexture(owner_->Device(), &createInfo);
        if (texture_ == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create Texture2D: ") + SDL_GetError());

        UpdatePixels(data.pixels.data(), width_ * 4);
    }

    SdlGpuTextureBackend::~SdlGpuTextureBackend()
    {
        if (texture_ != nullptr)
            SDL_ReleaseGPUTexture(owner_->Device(), texture_);
    }

    void SdlGpuTextureBackend::UpdatePixels(const uint8_t* rgba, int stride)
    {
        SDL_GPUDevice* device = owner_->Device();
        const Uint32 rowBytes = static_cast<Uint32>(width_) * 4;
        const Uint32 sizeBytes = rowBytes * static_cast<Uint32>(height_);

        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = sizeBytes;
        SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
        if (transferBuffer == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create texture transfer buffer: ") + SDL_GetError());

        void* mapped = SDL_MapGPUTransferBuffer(device, transferBuffer, false);
        if (mapped == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to map texture transfer buffer: ") + SDL_GetError());
        }
        if (stride == static_cast<int>(rowBytes))
        {
            std::memcpy(mapped, rgba, sizeBytes);
        }
        else
        {
            auto* dst = static_cast<uint8_t*>(mapped);
            for (int y = 0; y < height_; ++y)
                std::memcpy(dst + static_cast<std::size_t>(y) * rowBytes,
                            rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride), rowBytes);
        }
        SDL_UnmapGPUTransferBuffer(device, transferBuffer);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        if (cmd == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: SDL_AcquireGPUCommandBuffer (texture upload) failed: ") + SDL_GetError());
        }
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transferBuffer;
        source.pixels_per_row = static_cast<Uint32>(width_);
        source.rows_per_layer = static_cast<Uint32>(height_);
        SDL_GPUTextureRegion destination{};
        destination.texture = texture_;
        destination.w = static_cast<Uint32>(width_);
        destination.h = static_cast<Uint32>(height_);
        destination.d = 1;
        SDL_UploadToGPUTexture(copyPass, &source, &destination, true);
        SDL_EndGPUCopyPass(copyPass);
        if (!SDL_SubmitGPUCommandBuffer(cmd))
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: SDL_SubmitGPUCommandBuffer (texture upload) failed: ") + SDL_GetError());
        }
        SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
    }

    // ---- SdlGpuTexture3DBackend (Phase SDLGPU-9, SDLGPU-40/SDLGPU-41) ----

    SdlGpuTexture3DBackend::SdlGpuTexture3DBackend(SdlGpuGraphicsBackend& owner, int width, int height,
                                                    int depth, bool mipMap)
        : owner_(&owner), width_(width), height_(height), depth_(depth), mipMap_(mipMap)
    {
        SDL_GPUTextureCreateInfo createInfo{};
        createInfo.type = SDL_GPU_TEXTURETYPE_3D;
        createInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        // SDL_gpu's own debug validation requires COLOR_TARGET usage (in addition to SAMPLER) on
        // any texture SDL_GenerateMipmapsForGPUTexture is called on ("GenerateMipmaps texture must
        // be created with SAMPLER and COLOR_TARGET usage flags!") -- found 2026-07-16 once
        // SDLGPU-6 wired debug_mode to a real CNA-side toggle for the first time (previously a
        // silently-tolerated violation that produced a genuine hang under Vulkan validation, not
        // just a warning). Only widened when mipMap is actually requested, matching this
        // constructor's own minimal-usage convention for the common non-mipmap case.
        createInfo.usage = mipMap_ ? (SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET)
                                    : SDL_GPU_TEXTUREUSAGE_SAMPLER;
        createInfo.width = static_cast<Uint32>(width);
        createInfo.height = static_cast<Uint32>(height);
        createInfo.layer_count_or_depth = static_cast<Uint32>(depth);
        // Mirrors FNA's Texture3D constructor: LevelCount = mipMap ? CalculateMipLevels(width, height)
        // : 1 -- depth does not participate in the mip-level count.
        createInfo.num_levels = mipMap_ ? static_cast<Uint32>(CalculateMipLevels(width, height)) : 1;
        createInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;

        texture_ = SDL_CreateGPUTexture(owner_->Device(), &createInfo);
        if (texture_ == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create Texture3D: ") + SDL_GetError());
    }

    SdlGpuTexture3DBackend::~SdlGpuTexture3DBackend()
    {
        if (texture_ != nullptr)
            SDL_ReleaseGPUTexture(owner_->Device(), texture_);
    }

    void SdlGpuTexture3DBackend::SetData(int level, int x, int y, int z, int w, int h, int depth,
                                         const void* data, int dataLength)
    {
        if (w <= 0 || h <= 0 || depth <= 0)
            return;
        const Uint32 sizeBytes = static_cast<Uint32>(w) * static_cast<Uint32>(h) * static_cast<Uint32>(depth) * 4;
        if (static_cast<Uint32>(dataLength) < sizeBytes)
            throw std::out_of_range("CNA SDL_GPU: Texture3D::SetData: dataLength too small for the requested region");

        SDL_GPUDevice* device = owner_->Device();
        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = sizeBytes;
        SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
        if (transferBuffer == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: Texture3D::SetData: failed to create transfer buffer: ") + SDL_GetError());

        void* mapped = SDL_MapGPUTransferBuffer(device, transferBuffer, false);
        if (mapped == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: Texture3D::SetData: failed to map transfer buffer: ") + SDL_GetError());
        }
        std::memcpy(mapped, data, sizeBytes);
        SDL_UnmapGPUTransferBuffer(device, transferBuffer);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        if (cmd == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: Texture3D::SetData: SDL_AcquireGPUCommandBuffer failed: ") + SDL_GetError());
        }
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transferBuffer;
        source.pixels_per_row = static_cast<Uint32>(w);
        source.rows_per_layer = static_cast<Uint32>(h);
        SDL_GPUTextureRegion destination{};
        destination.texture = texture_;
        destination.mip_level = static_cast<Uint32>(level);
        destination.x = static_cast<Uint32>(x);
        destination.y = static_cast<Uint32>(y);
        destination.z = static_cast<Uint32>(z);
        destination.w = static_cast<Uint32>(w);
        destination.h = static_cast<Uint32>(h);
        destination.d = static_cast<Uint32>(depth);
        // cycle=false: unlike SdlGpuTextureBackend::UpdatePixels's single full-texture replace
        // (where cycle=true's "swap to a fresh resource" avoids stalling on an in-flight read),
        // Texture3D content is built up via multiple independent sub-volume/per-level SetData
        // calls that must all land on the SAME underlying resource -- cycle=true here silently
        // orphaned earlier partial writes onto an abandoned resource (found via a real byte-exact
        // round-trip test: an off-center sub-volume written first came back as zero/uninitialized
        // once a second sub-volume was written afterward).
        SDL_UploadToGPUTexture(copyPass, &source, &destination, false);
        SDL_EndGPUCopyPass(copyPass);

        // "Generated case" (SDLGPU-41): a full level-0 upload with mips requested regenerates the
        // whole chain immediately -- real XNA/FNA has no explicit "regenerate mips" call for
        // Texture3D, so this is the natural trigger point. Per SDL_gpu.h, must run outside any pass.
        const bool isFullLevel0Upload = level == 0 && x == 0 && y == 0 && z == 0 &&
                                        w == width_ && h == height_ && depth == depth_;
        if (mipMap_ && isFullLevel0Upload)
            SDL_GenerateMipmapsForGPUTexture(cmd, texture_);

        if (!SDL_SubmitGPUCommandBuffer(cmd))
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: Texture3D::SetData: SDL_SubmitGPUCommandBuffer failed: ") + SDL_GetError());
        }
        SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
    }

    void SdlGpuTexture3DBackend::GetData(int level, int x, int y, int z, int w, int h, int depth,
                                         void* data, int dataLength) const
    {
        if (w <= 0 || h <= 0 || depth <= 0)
            return;
        const Uint32 sizeBytes = static_cast<Uint32>(w) * static_cast<Uint32>(h) * static_cast<Uint32>(depth) * 4;
        if (static_cast<Uint32>(dataLength) < sizeBytes)
            throw std::out_of_range("CNA SDL_GPU: Texture3D::GetData: dataLength too small for the requested region");

        SDL_GPUDevice* device = owner_->Device();
        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        transferInfo.size = sizeBytes;
        SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
        if (transferBuffer == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: Texture3D::GetData: failed to create transfer buffer: ") + SDL_GetError());

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        if (cmd == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: Texture3D::GetData: SDL_AcquireGPUCommandBuffer failed: ") + SDL_GetError());
        }

        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureRegion region{};
        region.texture = texture_;
        region.mip_level = static_cast<Uint32>(level);
        region.x = static_cast<Uint32>(x);
        region.y = static_cast<Uint32>(y);
        region.z = static_cast<Uint32>(z);
        region.w = static_cast<Uint32>(w);
        region.h = static_cast<Uint32>(h);
        region.d = static_cast<Uint32>(depth);
        SDL_GPUTextureTransferInfo dest{};
        dest.transfer_buffer = transferBuffer;
        dest.pixels_per_row = static_cast<Uint32>(w);
        dest.rows_per_layer = static_cast<Uint32>(h);
        SDL_DownloadFromGPUTexture(copyPass, &region, &dest);
        SDL_EndGPUCopyPass(copyPass);

        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        if (fence == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: Texture3D::GetData: SDL_SubmitGPUCommandBufferAndAcquireFence failed: ") + SDL_GetError());
        }
        SDL_WaitForGPUFences(device, true, &fence, 1);
        SDL_ReleaseGPUFence(device, fence);

        void* mapped = SDL_MapGPUTransferBuffer(device, transferBuffer, false);
        if (mapped == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: Texture3D::GetData: SDL_MapGPUTransferBuffer failed: ") + SDL_GetError());
        }
        std::memcpy(data, mapped, sizeBytes);
        SDL_UnmapGPUTransferBuffer(device, transferBuffer);
        SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
    }

    // ---- SdlGpuTextureCubeBackend (Phase SDLGPU-9, SDLGPU-51) ----

    SdlGpuTextureCubeBackend::SdlGpuTextureCubeBackend(SdlGpuGraphicsBackend& owner, int size, bool mipMap)
        : owner_(&owner), size_(size), mipMap_(mipMap)
    {
        SDL_GPUTextureCreateInfo createInfo{};
        createInfo.type = SDL_GPU_TEXTURETYPE_CUBE;
        createInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        // SDL_gpu's own debug validation requires COLOR_TARGET usage (in addition to SAMPLER) on
        // any texture SDL_GenerateMipmapsForGPUTexture is called on -- see
        // SdlGpuTexture3DBackend's own identical constructor comment for the real finding this fix
        // came from. Only widened when mipMap is actually requested.
        createInfo.usage = mipMap_ ? (SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET)
                                    : SDL_GPU_TEXTUREUSAGE_SAMPLER;
        createInfo.width = static_cast<Uint32>(size);
        createInfo.height = static_cast<Uint32>(size);
        createInfo.layer_count_or_depth = 6;
        // Mirrors FNA's TextureCube constructor: LevelCount = mipMap ? CalculateMipLevels(size, size) : 1.
        createInfo.num_levels = mipMap_ ? static_cast<Uint32>(CalculateMipLevels(size, size)) : 1;
        createInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;

        texture_ = SDL_CreateGPUTexture(owner_->Device(), &createInfo);
        if (texture_ == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create TextureCube: ") + SDL_GetError());
    }

    SdlGpuTextureCubeBackend::~SdlGpuTextureCubeBackend()
    {
        if (texture_ != nullptr)
            SDL_ReleaseGPUTexture(owner_->Device(), texture_);
    }

    void SdlGpuTextureCubeBackend::SetData(int face, int level, int x, int y, int w, int h,
                                           const void* data, int dataLength)
    {
        if (w <= 0 || h <= 0)
            return;
        const Uint32 sizeBytes = static_cast<Uint32>(w) * static_cast<Uint32>(h) * 4;
        if (static_cast<Uint32>(dataLength) < sizeBytes)
            throw std::out_of_range("CNA SDL_GPU: TextureCube::SetData: dataLength too small for the requested region");

        SDL_GPUDevice* device = owner_->Device();
        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = sizeBytes;
        SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
        if (transferBuffer == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: TextureCube::SetData: failed to create transfer buffer: ") + SDL_GetError());

        void* mapped = SDL_MapGPUTransferBuffer(device, transferBuffer, false);
        if (mapped == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: TextureCube::SetData: failed to map transfer buffer: ") + SDL_GetError());
        }
        std::memcpy(mapped, data, sizeBytes);
        SDL_UnmapGPUTransferBuffer(device, transferBuffer);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        if (cmd == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: TextureCube::SetData: SDL_AcquireGPUCommandBuffer failed: ") + SDL_GetError());
        }
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transferBuffer;
        source.pixels_per_row = static_cast<Uint32>(w);
        source.rows_per_layer = static_cast<Uint32>(h);
        SDL_GPUTextureRegion destination{};
        destination.texture = texture_;
        destination.mip_level = static_cast<Uint32>(level);
        destination.layer = static_cast<Uint32>(face);
        destination.x = static_cast<Uint32>(x);
        destination.y = static_cast<Uint32>(y);
        destination.w = static_cast<Uint32>(w);
        destination.h = static_cast<Uint32>(h);
        destination.d = 1;
        // cycle=false, same rationale as SdlGpuTexture3DBackend::SetData: a cube map is built up via
        // multiple independent per-face SetData calls that must all land on the SAME resource --
        // cycle=true would silently orphan earlier faces' writes (the bug SDLGPU-40 found and fixed).
        SDL_UploadToGPUTexture(copyPass, &source, &destination, false);
        SDL_EndGPUCopyPass(copyPass);

        // "Generated case": a full level-0 upload of a given face with mips requested regenerates
        // the whole chain (all 6 faces) immediately -- matches SdlGpuRenderTargetCubeBackend's own
        // "regenerates all faces" convention (SDL_gpu has no per-layer mip-regen control), and
        // real XNA/FNA has no explicit "regenerate mips" call for TextureCube either. Must run
        // outside any pass, per SDL_gpu.h.
        const bool isFullLevel0Upload = level == 0 && x == 0 && y == 0 && w == size_ && h == size_;
        if (mipMap_ && isFullLevel0Upload)
            SDL_GenerateMipmapsForGPUTexture(cmd, texture_);

        if (!SDL_SubmitGPUCommandBuffer(cmd))
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: TextureCube::SetData: SDL_SubmitGPUCommandBuffer failed: ") + SDL_GetError());
        }
        SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
    }

    void SdlGpuTextureCubeBackend::GetData(int face, int level, int x, int y, int w, int h,
                                           void* data, int dataLength) const
    {
        if (w <= 0 || h <= 0)
            return;
        const Uint32 sizeBytes = static_cast<Uint32>(w) * static_cast<Uint32>(h) * 4;
        if (static_cast<Uint32>(dataLength) < sizeBytes)
            throw std::out_of_range("CNA SDL_GPU: TextureCube::GetData: dataLength too small for the requested region");

        SDL_GPUDevice* device = owner_->Device();
        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        transferInfo.size = sizeBytes;
        SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
        if (transferBuffer == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: TextureCube::GetData: failed to create transfer buffer: ") + SDL_GetError());

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        if (cmd == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: TextureCube::GetData: SDL_AcquireGPUCommandBuffer failed: ") + SDL_GetError());
        }

        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureRegion region{};
        region.texture = texture_;
        region.mip_level = static_cast<Uint32>(level);
        region.layer = static_cast<Uint32>(face);
        region.x = static_cast<Uint32>(x);
        region.y = static_cast<Uint32>(y);
        region.w = static_cast<Uint32>(w);
        region.h = static_cast<Uint32>(h);
        region.d = 1;
        SDL_GPUTextureTransferInfo dest{};
        dest.transfer_buffer = transferBuffer;
        dest.pixels_per_row = static_cast<Uint32>(w);
        dest.rows_per_layer = static_cast<Uint32>(h);
        SDL_DownloadFromGPUTexture(copyPass, &region, &dest);
        SDL_EndGPUCopyPass(copyPass);

        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        if (fence == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: TextureCube::GetData: SDL_SubmitGPUCommandBufferAndAcquireFence failed: ") + SDL_GetError());
        }
        SDL_WaitForGPUFences(device, true, &fence, 1);
        SDL_ReleaseGPUFence(device, fence);

        void* mapped = SDL_MapGPUTransferBuffer(device, transferBuffer, false);
        if (mapped == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: TextureCube::GetData: SDL_MapGPUTransferBuffer failed: ") + SDL_GetError());
        }
        std::memcpy(data, mapped, sizeBytes);
        SDL_UnmapGPUTransferBuffer(device, transferBuffer);
        SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
    }

    // ---- SdlGpuRenderTargetBackend (Phase SDLGPU-8, SDLGPU-35) ----

    SdlGpuRenderTarget2DState::~SdlGpuRenderTarget2DState()
    {
        // Deferred, NOT released directly here -- some other still-pending (not yet submitted)
        // draw command may sample one of these textures as an INPUT (e.g. a SpriteBatch draw
        // sampling this render target's contents elsewhere) via a raw handle captured at
        // Queue*Draw() time -- see QueueTextureRelease's own doc comment for why.
        owner->QueueTextureRelease(depthTexture);
        owner->QueueTextureRelease(msaaTexture);
        owner->QueueTextureRelease(colorTexture);
    }

    SdlGpuRenderTargetBackend::SdlGpuRenderTargetBackend(SdlGpuGraphicsBackend& owner, int width, int height,
                                                          int depthFormat, bool mipMap, int multiSampleCount)
        : owner_(&owner)
    {
        state_ = std::make_shared<SdlGpuRenderTarget2DState>();
        state_->owner = owner_;
        state_->width = width;
        state_->height = height;

        SDL_GPUDevice* device = owner_->Device();
        constexpr SDL_GPUTextureFormat kFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

        const SDL_GPUSampleCount sampleCount = ClampSampleCount(device, kFormat, multiSampleCount);
        multiSampleCount_ = SampleCountToInt(sampleCount);
        state_->sampleCount = sampleCount;
        // MSAA and mip generation are mutually exclusive on the same attachment, same rationale
        // SdlGpuRenderTargetCubeBackend's own MSAA support already established.
        mipMap_ = mipMap && multiSampleCount_ == 0;
        state_->mipMap = mipMap_;

        SDL_GPUTextureCreateInfo colorInfo{};
        colorInfo.type = SDL_GPU_TEXTURETYPE_2D;
        colorInfo.format = kFormat;
        colorInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        colorInfo.width = static_cast<Uint32>(width);
        colorInfo.height = static_cast<Uint32>(height);
        colorInfo.layer_count_or_depth = 1;
        colorInfo.num_levels = mipMap_ ? static_cast<Uint32>(CalculateMipLevels(width, height)) : 1;
        colorInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;  // the sampleable texture itself is always single-sample

        state_->colorTexture = SDL_CreateGPUTexture(device, &colorInfo);
        if (state_->colorTexture == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create RenderTarget2D color texture: ") + SDL_GetError());

        if (multiSampleCount_ > 0)
        {
            // The real multisampled render target, resolved into colorTexture automatically via
            // SDL_GPUColorTargetInfo.resolve_texture at render-pass end -- same mechanism as
            // SdlGpuRenderTargetCubeBackend's own MSAA support.
            SDL_GPUTextureCreateInfo msaaInfo{};
            msaaInfo.type = SDL_GPU_TEXTURETYPE_2D;
            msaaInfo.format = kFormat;
            msaaInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
            msaaInfo.width = static_cast<Uint32>(width);
            msaaInfo.height = static_cast<Uint32>(height);
            msaaInfo.layer_count_or_depth = 1;
            msaaInfo.num_levels = 1;
            msaaInfo.sample_count = sampleCount;
            state_->msaaTexture = SDL_CreateGPUTexture(device, &msaaInfo);
            if (state_->msaaTexture == nullptr)
            {
                SDL_ReleaseGPUTexture(device, state_->colorTexture);
                throw std::runtime_error(std::string("CNA SDL_GPU: failed to create RenderTarget2D MSAA texture: ") + SDL_GetError());
            }
        }

        // DepthFormat::None (0) requests no depth attachment at all; otherwise this target gets its
        // own depth/stencil texture sized to its own width/height (which may differ from the
        // swapchain's) -- reuses the one combined format this device supports, same simplification
        // the swapchain's own depthStencilTexture_ already makes (see QueryDepthStencilFormat).
        if (depthFormat != 0 && owner_->depthStencilFormat_ != SDL_GPU_TEXTUREFORMAT_INVALID)
        {
            SDL_GPUTextureCreateInfo depthInfo{};
            depthInfo.type = SDL_GPU_TEXTURETYPE_2D;
            depthInfo.format = owner_->depthStencilFormat_;
            depthInfo.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
            depthInfo.width = static_cast<Uint32>(width);
            depthInfo.height = static_cast<Uint32>(height);
            depthInfo.layer_count_or_depth = 1;
            depthInfo.num_levels = 1;
            depthInfo.sample_count = sampleCount;  // MSAA depth matches MSAA color
            state_->depthTexture = SDL_CreateGPUTexture(device, &depthInfo);
            if (state_->depthTexture == nullptr)
            {
                if (state_->msaaTexture != nullptr) SDL_ReleaseGPUTexture(device, state_->msaaTexture);
                SDL_ReleaseGPUTexture(device, state_->colorTexture);
                throw std::runtime_error(std::string("CNA SDL_GPU: failed to create RenderTarget2D depth texture: ") + SDL_GetError());
            }
        }
    }

    SdlGpuRenderTargetBackend::~SdlGpuRenderTargetBackend()
    {
        if (owner_->currentRenderTarget_ == this)
            owner_->currentRenderTarget_ = nullptr;
        // state_ itself is NOT removed from usedRenderTargetsThisFrame_ here -- if it's still
        // referenced there (or by a queued DrawCommand's DrawTarget), this shared_ptr going out of
        // scope just drops OUR reference; the state survives via those other references until
        // EnsureFrameRendered() finishes with it, so this target's own pending Clear()/draws still
        // render correctly even though this wrapper is gone. See SdlGpuRenderTarget2DState's own
        // doc comment.
    }

    void SdlGpuRenderTargetBackend::BindAsRenderTarget()
    {
        owner_->currentRenderTarget_ = this;
        // Mutually exclusive with a bound RenderTargetCube face -- matches real XNA
        // single-current-target semantics (SetRenderTarget always replaces whatever was there).
        if (owner_->currentRenderTargetCube_ != nullptr)
        {
            owner_->currentRenderTargetCube_ = nullptr;
            owner_->currentActiveCubeFace_ = -1;
        }
        MarkUsedThisFrame();
    }

    void SdlGpuRenderTargetBackend::MarkUsedThisFrame()
    {
        auto& used = owner_->usedRenderTargetsThisFrame_;
        const bool alreadyUsed = std::any_of(used.begin(), used.end(),
            [this](const std::shared_ptr<SdlGpuRenderTarget2DState>& sp) { return sp.get() == state_.get(); });
        if (!alreadyUsed)
            used.push_back(state_);
        owner_->framePending_ = true;
    }

    void SdlGpuRenderTargetBackend::UnbindAsRenderTarget()
    {
        if (owner_->currentRenderTarget_ == this)
            owner_->currentRenderTarget_ = nullptr;
    }

    void SdlGpuRenderTargetBackend::GetData(int level, int x, int y, int w, int h,
                                            void* data, int dataLength) const
    {
        if (w <= 0 || h <= 0)
            return;
        const Uint32 sizeBytes = static_cast<Uint32>(w) * static_cast<Uint32>(h) * 4;
        if (static_cast<Uint32>(dataLength) < sizeBytes)
            throw std::out_of_range("CNA SDL_GPU: RenderTarget2D::GetData: dataLength too small for the requested region");

        // Must reflect this frame's draws, not stale/uninitialized GPU memory -- a no-op if
        // nothing is pending (matches EnsureFrameRendered's own early-return contract).
        owner_->EnsureFrameRendered();

        SDL_GPUDevice* device = owner_->Device();
        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        transferInfo.size = sizeBytes;
        SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
        if (transferBuffer == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: RenderTarget2D::GetData: failed to create transfer buffer: ") + SDL_GetError());

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        if (cmd == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: RenderTarget2D::GetData: SDL_AcquireGPUCommandBuffer failed: ") + SDL_GetError());
        }

        // Always downloads from the single-sample, sampleable colorTexture -- already
        // resolved-into by the time any frame's pass has run, even when this target is MSAA.
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureRegion region{};
        region.texture = state_->colorTexture;
        region.mip_level = static_cast<Uint32>(level);
        region.x = static_cast<Uint32>(x);
        region.y = static_cast<Uint32>(y);
        region.w = static_cast<Uint32>(w);
        region.h = static_cast<Uint32>(h);
        region.d = 1;
        SDL_GPUTextureTransferInfo dest{};
        dest.transfer_buffer = transferBuffer;
        dest.pixels_per_row = static_cast<Uint32>(w);
        dest.rows_per_layer = static_cast<Uint32>(h);
        SDL_DownloadFromGPUTexture(copyPass, &region, &dest);
        SDL_EndGPUCopyPass(copyPass);

        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        if (fence == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: RenderTarget2D::GetData: SDL_SubmitGPUCommandBufferAndAcquireFence failed: ") + SDL_GetError());
        }
        SDL_WaitForGPUFences(device, true, &fence, 1);
        SDL_ReleaseGPUFence(device, fence);

        void* mapped = SDL_MapGPUTransferBuffer(device, transferBuffer, false);
        if (mapped == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: RenderTarget2D::GetData: SDL_MapGPUTransferBuffer failed: ") + SDL_GetError());
        }
        std::memcpy(data, mapped, sizeBytes);
        SDL_UnmapGPUTransferBuffer(device, transferBuffer);
        SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
    }

    // ---- SdlGpuRenderTargetCubeBackend (Phase SDLGPU-8, SDLGPU-36) ----

    SdlGpuRenderTargetCubeState::~SdlGpuRenderTargetCubeState()
    {
        // Deferred -- see SdlGpuRenderTarget2DState's own destructor / QueueTextureRelease's doc
        // comment for why (some other still-pending draw may sample cubeTexture as an
        // EnvironmentMapEffect input, independent of usedRenderTargetCubeFacesThisFrame_).
        owner->QueueTextureRelease(depthTexture);
        owner->QueueTextureRelease(msaaTexture);
        owner->QueueTextureRelease(cubeTexture);
    }

    SdlGpuRenderTargetCubeBackend::SdlGpuRenderTargetCubeBackend(SdlGpuGraphicsBackend& owner, int size,
                                                                  int depthFormat, bool mipMap, int multiSampleCount)
        : owner_(&owner), mipMap_(mipMap)
    {
        state_ = std::make_shared<SdlGpuRenderTargetCubeState>();
        state_->owner = owner_;
        state_->size = size;

        SDL_GPUDevice* device = owner_->Device();
        constexpr SDL_GPUTextureFormat kFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

        const SDL_GPUSampleCount sampleCount = ClampSampleCount(device, kFormat, multiSampleCount);
        multiSampleCount_ = SampleCountToInt(sampleCount);
        state_->sampleCount = sampleCount;
        // MSAA and mip generation are mutually exclusive on the same attachment, same rationale
        // D3D12RenderTargetCubeBackend's own MSAA follow-up already established.
        mipMap_ = mipMap_ && multiSampleCount_ == 0;
        state_->mipMap = mipMap_;

        SDL_GPUTextureCreateInfo cubeInfo{};
        cubeInfo.type = SDL_GPU_TEXTURETYPE_CUBE;
        cubeInfo.format = kFormat;
        cubeInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        cubeInfo.width = static_cast<Uint32>(size);
        cubeInfo.height = static_cast<Uint32>(size);
        cubeInfo.layer_count_or_depth = 6;
        cubeInfo.num_levels = mipMap_ ? static_cast<Uint32>(CalculateMipLevels(size, size)) : 1;
        cubeInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;  // the cube texture itself is always single-sample
        state_->cubeTexture = SDL_CreateGPUTexture(device, &cubeInfo);
        if (state_->cubeTexture == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create RenderTargetCube color texture: ") + SDL_GetError());

        if (multiSampleCount_ > 0)
        {
            // SDL_GPU_TEXTURETYPE_CUBE has no multisampled variant. The original design used a
            // 6-layer 2D_ARRAY MSAA texture, one layer per face -- but SDL_gpu's own debug
            // validation forbids sample_count>1 on ANY array texture ("For array textures:
            // sample_count must be SDL_GPU_SAMPLECOUNT_1"), found 2026-07-16 once SDLGPU-6 wired
            // debug_mode to a real CNA-side toggle for the first time (previously silently
            // tolerated -- a genuine hang under Vulkan validation, not just a warning). Fixed to a
            // single-layer SDL_GPU_TEXTURETYPE_2D texture instead, shared across whichever face is
            // currently the active render target -- the exact same "one shared resource, only one
            // face active at a time" convention depthTexture below already uses. Resolves into
            // cubeTexture's active face via SDL_GPUColorTargetInfo.resolve_texture/resolve_layer at
            // render-pass end (see RenderToTargetCubeFace) -- no manual ResolveSubresource-equivalent
            // needed.
            SDL_GPUTextureCreateInfo msaaInfo{};
            msaaInfo.type = SDL_GPU_TEXTURETYPE_2D;
            msaaInfo.format = kFormat;
            msaaInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
            msaaInfo.width = static_cast<Uint32>(size);
            msaaInfo.height = static_cast<Uint32>(size);
            msaaInfo.layer_count_or_depth = 1;
            msaaInfo.num_levels = 1;
            msaaInfo.sample_count = sampleCount;
            state_->msaaTexture = SDL_CreateGPUTexture(device, &msaaInfo);
            if (state_->msaaTexture == nullptr)
            {
                SDL_ReleaseGPUTexture(device, state_->cubeTexture);
                throw std::runtime_error(std::string("CNA SDL_GPU: failed to create RenderTargetCube MSAA texture: ") + SDL_GetError());
            }
        }

        if (depthFormat != 0 && owner_->depthStencilFormat_ != SDL_GPU_TEXTUREFORMAT_INVALID)
        {
            SDL_GPUTextureCreateInfo depthInfo{};
            depthInfo.type = SDL_GPU_TEXTURETYPE_2D;
            depthInfo.format = owner_->depthStencilFormat_;
            depthInfo.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
            depthInfo.width = static_cast<Uint32>(size);
            depthInfo.height = static_cast<Uint32>(size);
            depthInfo.layer_count_or_depth = 1;
            depthInfo.num_levels = 1;
            depthInfo.sample_count = sampleCount;
            state_->depthTexture = SDL_CreateGPUTexture(device, &depthInfo);
            if (state_->depthTexture == nullptr)
            {
                if (state_->msaaTexture != nullptr) SDL_ReleaseGPUTexture(device, state_->msaaTexture);
                SDL_ReleaseGPUTexture(device, state_->cubeTexture);
                throw std::runtime_error(std::string("CNA SDL_GPU: failed to create RenderTargetCube depth texture: ") + SDL_GetError());
            }
        }
    }

    SdlGpuRenderTargetCubeBackend::~SdlGpuRenderTargetCubeBackend()
    {
        if (owner_->currentRenderTargetCube_ == this)
        {
            owner_->currentRenderTargetCube_ = nullptr;
            owner_->currentActiveCubeFace_ = -1;
        }
        // state_ is NOT removed from usedRenderTargetCubeFacesThisFrame_ here -- same rationale as
        // SdlGpuRenderTargetBackend's own destructor (see SdlGpuRenderTarget2DState's doc comment).
    }

    void SdlGpuRenderTargetCubeBackend::BindAsRenderTargetFace(int face)
    {
        owner_->currentRenderTargetCube_ = this;
        owner_->currentActiveCubeFace_ = face;
        // Mutually exclusive with a bound RenderTarget2D -- matches real XNA single-current-target
        // semantics (SetRenderTargetCubeFace always replaces whatever was there).
        owner_->currentRenderTarget_ = nullptr;
        auto& used = owner_->usedRenderTargetCubeFacesThisFrame_;
        const bool alreadyUsed = std::any_of(used.begin(), used.end(),
            [this, face](const auto& pair) { return pair.first.get() == state_.get() && pair.second == face; });
        if (!alreadyUsed)
            used.push_back(std::make_pair(state_, face));
        owner_->framePending_ = true;
    }

    void SdlGpuRenderTargetCubeBackend::UnbindAsRenderTarget()
    {
        if (owner_->currentRenderTargetCube_ == this)
        {
            owner_->currentRenderTargetCube_ = nullptr;
            owner_->currentActiveCubeFace_ = -1;
        }
    }

    void SdlGpuRenderTargetCubeBackend::GetData(int face, int level, int x, int y, int w, int h,
                                                void* data, int dataLength) const
    {
        if (w <= 0 || h <= 0)
            return;
        const Uint32 sizeBytes = static_cast<Uint32>(w) * static_cast<Uint32>(h) * 4;
        if (static_cast<Uint32>(dataLength) < sizeBytes)
            throw std::out_of_range("CNA SDL_GPU: RenderTargetCube::GetData: dataLength too small for the requested region");

        // Must reflect this frame's draws, not stale/uninitialized GPU memory -- a no-op if
        // nothing is pending (matches EnsureFrameRendered's own early-return contract).
        owner_->EnsureFrameRendered();

        SDL_GPUDevice* device = owner_->Device();
        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        transferInfo.size = sizeBytes;
        SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
        if (transferBuffer == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: RenderTargetCube::GetData: failed to create transfer buffer: ") + SDL_GetError());

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        if (cmd == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: RenderTargetCube::GetData: SDL_AcquireGPUCommandBuffer failed: ") + SDL_GetError());
        }

        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureRegion region{};
        region.texture = state_->cubeTexture;
        region.mip_level = static_cast<Uint32>(level);
        region.layer = static_cast<Uint32>(face);
        region.x = static_cast<Uint32>(x);
        region.y = static_cast<Uint32>(y);
        region.w = static_cast<Uint32>(w);
        region.h = static_cast<Uint32>(h);
        region.d = 1;
        SDL_GPUTextureTransferInfo dest{};
        dest.transfer_buffer = transferBuffer;
        dest.pixels_per_row = static_cast<Uint32>(w);
        dest.rows_per_layer = static_cast<Uint32>(h);
        SDL_DownloadFromGPUTexture(copyPass, &region, &dest);
        SDL_EndGPUCopyPass(copyPass);

        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        if (fence == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: RenderTargetCube::GetData: SDL_SubmitGPUCommandBufferAndAcquireFence failed: ") + SDL_GetError());
        }
        SDL_WaitForGPUFences(device, true, &fence, 1);
        SDL_ReleaseGPUFence(device, fence);

        void* mapped = SDL_MapGPUTransferBuffer(device, transferBuffer, false);
        if (mapped == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: RenderTargetCube::GetData: SDL_MapGPUTransferBuffer failed: ") + SDL_GetError());
        }
        std::memcpy(data, mapped, sizeBytes);
        SDL_UnmapGPUTransferBuffer(device, transferBuffer);
        SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
    }

    // ---- SdlGpuVertexBufferBackend ----

    SdlGpuVertexBufferBackend::SdlGpuVertexBufferBackend(SdlGpuGraphicsBackend& owner, int vertexCapacity)
        : owner_(&owner), vertexCapacity_(vertexCapacity)
    {
    }

    SdlGpuVertexBufferBackend::~SdlGpuVertexBufferBackend()
    {
        if (buffer_ != nullptr)
            SDL_ReleaseGPUBuffer(owner_->Device(), buffer_);
    }

    void SdlGpuVertexBufferBackend::SetData(const void* data, int vertexCount, std::size_t strideInBytes)
    {
        // SetDataOptions::None/Discard both cycle -- see SetDataWithOptions's own doc comment.
        SetDataWithOptions(data, vertexCount, strideInBytes, SetDataOptions::Discard);
    }

    void SdlGpuVertexBufferBackend::SetDataWithOptions(const void* data, int vertexCount,
                                                        std::size_t strideInBytes, SetDataOptions options)
    {
        const bool cycle = (options != SetDataOptions::NoOverwrite);
        SDL_GPUDevice* device = owner_->Device();
        const Uint32 sizeBytes = static_cast<Uint32>(vertexCount) * static_cast<Uint32>(strideInBytes);
        // SDL_gpu's Vulkan driver asserts on GPU buffers smaller than 4 bytes (e.g. a skinned
        // model part with vertexCount == 0), so the backing allocation is clamped to that
        // minimum; vertexCount_/shadowData_ below still report the real (possibly zero) size.
        const Uint32 allocSizeBytes = std::max<Uint32>(sizeBytes, 4);
        if (buffer_ == nullptr || capacityBytes_ < allocSizeBytes)
        {
            if (buffer_ != nullptr)
                SDL_ReleaseGPUBuffer(device, buffer_);
            SDL_GPUBufferCreateInfo createInfo{};
            createInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
            createInfo.size = allocSizeBytes;
            buffer_ = SDL_CreateGPUBuffer(device, &createInfo);
            if (buffer_ == nullptr)
                throw std::runtime_error(std::string("CNA SDL_GPU: failed to create vertex buffer: ") + SDL_GetError());
            capacityBytes_ = allocSizeBytes;
        }

        if (sizeBytes == 0)
        {
            vertexCount_ = vertexCount;
            stride_ = strideInBytes;
            shadowData_.clear();
            return;
        }

        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = sizeBytes;
        SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
        if (transferBuffer == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create vertex transfer buffer: ") + SDL_GetError());
        void* mapped = SDL_MapGPUTransferBuffer(device, transferBuffer, false);
        if (mapped == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to map vertex transfer buffer: ") + SDL_GetError());
        }
        std::memcpy(mapped, data, sizeBytes);
        SDL_UnmapGPUTransferBuffer(device, transferBuffer);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        if (cmd == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: SDL_AcquireGPUCommandBuffer (vertex upload) failed: ") + SDL_GetError());
        }
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation source{};
        source.transfer_buffer = transferBuffer;
        SDL_GPUBufferRegion destRegion{};
        destRegion.buffer = buffer_;
        destRegion.size = sizeBytes;
        SDL_UploadToGPUBuffer(copyPass, &source, &destRegion, cycle);
        SDL_EndGPUCopyPass(copyPass);
        if (!SDL_SubmitGPUCommandBuffer(cmd))
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: SDL_SubmitGPUCommandBuffer (vertex upload) failed: ") + SDL_GetError());
        }
        SDL_ReleaseGPUTransferBuffer(device, transferBuffer);

        vertexCount_ = vertexCount;
        stride_ = strideInBytes;
        shadowData_.assign(static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + sizeBytes);
    }

    // ---- SdlGpuIndexBufferBackend ----

    SdlGpuIndexBufferBackend::SdlGpuIndexBufferBackend(SdlGpuGraphicsBackend& owner, int indexCapacity, bool thirtyTwoBit)
        : owner_(&owner), indexCapacity_(indexCapacity), thirtyTwoBit_(thirtyTwoBit)
    {
    }

    SdlGpuIndexBufferBackend::~SdlGpuIndexBufferBackend()
    {
        if (buffer_ != nullptr)
            SDL_ReleaseGPUBuffer(owner_->Device(), buffer_);
    }

    // SetDataOptions::None/Discard both cycle -- see SdlGpuVertexBufferBackend::SetDataWithOptions's
    // own doc comment for the real Discard/NoOverwrite rationale.
    void SdlGpuIndexBufferBackend::SetData16(const void* data, int indexCount) { Upload(data, indexCount, false, true); }
    void SdlGpuIndexBufferBackend::SetData32(const void* data, int indexCount) { Upload(data, indexCount, true, true); }
    void SdlGpuIndexBufferBackend::SetData16WithOptions(const void* data, int indexCount, SetDataOptions options)
    {
        Upload(data, indexCount, false, options != SetDataOptions::NoOverwrite);
    }
    void SdlGpuIndexBufferBackend::SetData32WithOptions(const void* data, int indexCount, SetDataOptions options)
    {
        Upload(data, indexCount, true, options != SetDataOptions::NoOverwrite);
    }

    void SdlGpuIndexBufferBackend::Upload(const void* data, int indexCount, bool dataIsThirtyTwoBit, bool cycle)
    {
        SDL_GPUDevice* device = owner_->Device();
        const std::size_t elementSize = dataIsThirtyTwoBit ? sizeof(std::uint32_t) : sizeof(std::uint16_t);
        const Uint32 sizeBytes = static_cast<Uint32>(indexCount) * static_cast<Uint32>(elementSize);
        // See SdlGpuVertexBufferBackend::SetDataWithOptions's own comment: SDL_gpu's Vulkan
        // driver asserts on GPU buffers smaller than 4 bytes (e.g. a skinned model part with
        // indexCount == 0), so the backing allocation is clamped to that minimum.
        const Uint32 allocSizeBytes = std::max<Uint32>(sizeBytes, 4);
        if (buffer_ == nullptr || capacityBytes_ < allocSizeBytes)
        {
            if (buffer_ != nullptr)
                SDL_ReleaseGPUBuffer(device, buffer_);
            SDL_GPUBufferCreateInfo createInfo{};
            createInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
            createInfo.size = allocSizeBytes;
            buffer_ = SDL_CreateGPUBuffer(device, &createInfo);
            if (buffer_ == nullptr)
                throw std::runtime_error(std::string("CNA SDL_GPU: failed to create index buffer: ") + SDL_GetError());
            capacityBytes_ = allocSizeBytes;
        }

        if (sizeBytes == 0)
        {
            indexCount_ = indexCount;
            thirtyTwoBit_ = dataIsThirtyTwoBit;
            shadowData_.clear();
            return;
        }

        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = sizeBytes;
        SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
        if (transferBuffer == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create index transfer buffer: ") + SDL_GetError());
        void* mapped = SDL_MapGPUTransferBuffer(device, transferBuffer, false);
        if (mapped == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to map index transfer buffer: ") + SDL_GetError());
        }
        std::memcpy(mapped, data, sizeBytes);
        SDL_UnmapGPUTransferBuffer(device, transferBuffer);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        if (cmd == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: SDL_AcquireGPUCommandBuffer (index upload) failed: ") + SDL_GetError());
        }
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation source{};
        source.transfer_buffer = transferBuffer;
        SDL_GPUBufferRegion destRegion{};
        destRegion.buffer = buffer_;
        destRegion.size = sizeBytes;
        SDL_UploadToGPUBuffer(copyPass, &source, &destRegion, cycle);
        SDL_EndGPUCopyPass(copyPass);
        if (!SDL_SubmitGPUCommandBuffer(cmd))
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: SDL_SubmitGPUCommandBuffer (index upload) failed: ") + SDL_GetError());
        }
        SDL_ReleaseGPUTransferBuffer(device, transferBuffer);

        indexCount_ = indexCount;
        thirtyTwoBit_ = dataIsThirtyTwoBit;
        shadowData_.assign(static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + sizeBytes);
    }

    // ---- SdlGpuEffectBackend (Phase SDLGPU-10, SDLGPU-42/43) ----

    SdlGpuEffectBackend::SdlGpuEffectBackend(SdlGpuGraphicsBackend& owner)
        : owner_(&owner)
    {
    }

    SdlGpuEffectBackend::~SdlGpuEffectBackend()
    {
        for (auto& [key, pipeline] : pipelines_)
            if (pipeline != nullptr) SDL_ReleaseGPUGraphicsPipeline(owner_->Device(), pipeline);
        if (fragmentShader_ != nullptr) SDL_ReleaseGPUShader(owner_->Device(), fragmentShader_);
        if (vertexShader_ != nullptr) SDL_ReleaseGPUShader(owner_->Device(), vertexShader_);
    }

    bool SdlGpuEffectBackend::CompileProgram(const std::string& vertSrc, const std::string& fragSrc)
    {
        compileError_.clear();
        valid_ = false;
        for (auto& [key, pipeline] : pipelines_)
            if (pipeline != nullptr) SDL_ReleaseGPUGraphicsPipeline(owner_->Device(), pipeline);
        pipelines_.clear();
        if (fragmentShader_ != nullptr) { SDL_ReleaseGPUShader(owner_->Device(), fragmentShader_); fragmentShader_ = nullptr; }
        if (vertexShader_ != nullptr) { SDL_ReleaseGPUShader(owner_->Device(), vertexShader_); vertexShader_ = nullptr; }

        std::vector<std::uint8_t> vertSpirv;
        if (!CompileGlslToSpirv(vertSrc, kShadercVertexShader, "ShaderEffect_vs", vertSpirv, compileError_))
            return false;
        std::vector<std::uint8_t> fragSpirv;
        if (!CompileGlslToSpirv(fragSrc, kShadercFragmentShader, "ShaderEffect_fs", fragSpirv, compileError_))
            return false;

        SDL_GPUShaderCreateInfo vsInfo{};
        vsInfo.code = vertSpirv.data();
        vsInfo.code_size = vertSpirv.size();
        vsInfo.entrypoint = "main";
        vsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        vsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        vsInfo.num_uniform_buffers = 1;
        vertexShader_ = SDL_CreateGPUShader(owner_->Device(), &vsInfo);
        if (vertexShader_ == nullptr)
        {
            compileError_ = std::string("SDL_CreateGPUShader (vertex) failed: ") + SDL_GetError();
            return false;
        }

        SDL_GPUShaderCreateInfo fsInfo{};
        fsInfo.code = fragSpirv.data();
        fsInfo.code_size = fragSpirv.size();
        fsInfo.entrypoint = "main";
        fsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        fsInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        fsInfo.num_samplers = 1;
        fsInfo.num_uniform_buffers = 1;
        fragmentShader_ = SDL_CreateGPUShader(owner_->Device(), &fsInfo);
        if (fragmentShader_ == nullptr)
        {
            compileError_ = std::string("SDL_CreateGPUShader (fragment) failed: ") + SDL_GetError();
            SDL_ReleaseGPUShader(owner_->Device(), vertexShader_);
            vertexShader_ = nullptr;
            return false;
        }

        valid_ = true;
        return true;
    }

    SDL_GPUGraphicsPipeline* SdlGpuEffectBackend::GetOrCreatePipeline(SDL_GPUTextureFormat colorFormat,
                                                                       SDL_GPUSampleCount sampleCount,
                                                                       int colorTargetCount)
    {
        if (!valid_)
            return nullptr;
        // colorTargetCount (real MRT, SDLGPU-37 -- every RenderTarget2D in this backend is
        // R8G8B8A8_UNORM, so all N simultaneous attachments always share one colorFormat) folded
        // into bits [0..3], sampleCount (0/2/4/8) into bits [4..7] -- so a pipeline built for the
        // wrong sample_count or the wrong attachment count is never wrongly reused (SDLGPU-38's
        // MSAA fix established the same "don't let an untracked dimension collide" rule for
        // sample_count; this extends it to attachment count).
        const int key = (static_cast<int>(colorFormat) << 8) | (SampleCountToInt(sampleCount) << 4) | colorTargetCount;
        const auto it = pipelines_.find(key);
        if (it != pipelines_.end())
            return it->second;

        // Fixed SpriteVertex-shaped contract (x,y|u,v|r,g,b,a, 32 bytes), matching the stock
        // sprite pipeline's own vertex layout exactly (see GetOrCreateSpritePipeline) -- this is a
        // SpriteBatch-custom-shader facility, not a general arbitrary-vertex-format one.
        SDL_GPUVertexBufferDescription vbDesc{};
        vbDesc.slot = 0;
        vbDesc.pitch = sizeof(SdlGpuGraphicsBackend::SpriteVertex);
        vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        SDL_GPUVertexAttribute attrs[3]{};
        attrs[0].location = 0; attrs[0].buffer_slot = 0; attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attrs[0].offset = offsetof(SdlGpuGraphicsBackend::SpriteVertex, x);
        attrs[1].location = 1; attrs[1].buffer_slot = 0; attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attrs[1].offset = offsetof(SdlGpuGraphicsBackend::SpriteVertex, u);
        attrs[2].location = 2; attrs[2].buffer_slot = 0; attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attrs[2].offset = offsetof(SdlGpuGraphicsBackend::SpriteVertex, r);

        // Same standard (non-premultiplied) alpha blend as the stock sprite pipeline, applied to
        // every simultaneous attachment alike -- this backend has no per-attachment BlendState
        // concept to draw a different one from (real XNA doesn't either; GraphicsDevice.BlendState
        // is one value for the whole draw).
        std::vector<SDL_GPUColorTargetDescription> colorTargets(std::max(1, colorTargetCount));
        for (SDL_GPUColorTargetDescription& colorTarget : colorTargets)
        {
            colorTarget.format = colorFormat;
            colorTarget.blend_state.enable_blend = true;
            colorTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            colorTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            colorTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            colorTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            colorTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            colorTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        }

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = vertexShader_;
        pipelineInfo.fragment_shader = fragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = 3;
        pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pipelineInfo.multisample_state.sample_count = sampleCount;
        pipelineInfo.depth_stencil_state.enable_depth_test = false;
        pipelineInfo.depth_stencil_state.enable_depth_write = false;
        pipelineInfo.depth_stencil_state.enable_stencil_test = false;
        pipelineInfo.target_info.color_target_descriptions = colorTargets.data();
        pipelineInfo.target_info.num_color_targets = static_cast<Uint32>(colorTargets.size());
        pipelineInfo.target_info.has_depth_stencil_target = (owner_->depthStencilFormat_ != SDL_GPU_TEXTUREFORMAT_INVALID);
        pipelineInfo.target_info.depth_stencil_format = owner_->depthStencilFormat_;

        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(owner_->Device(), &pipelineInfo);
        if (pipeline == nullptr)
        {
            compileError_ = std::string("SDL_CreateGPUGraphicsPipeline failed: ") + SDL_GetError();
            return nullptr;
        }
        pipelines_[key] = pipeline;
        return pipeline;
    }

    // Fixed 128-byte layout mirroring D3D11EffectBackend's own convention byte-for-byte: [0..15]=
    // vpSize (vec4, xy used), [16..79]=mat4 matrix, [80..95]=vec4 color, [96..99]=float/int slot 0.
    // `name` is deliberately ignored, matching every sibling EffectBackend's own convention.
    void SdlGpuEffectBackend::SetUniformMat4(const char* /*name*/, const float* matrix)
    {
        std::memcpy(pushConst_.data() + 4, matrix, 64);
    }

    void SdlGpuEffectBackend::SetUniformVec4(const char* /*name*/, float x, float y, float z, float w)
    {
        pushConst_[20] = x; pushConst_[21] = y; pushConst_[22] = z; pushConst_[23] = w;
    }

    void SdlGpuEffectBackend::SetUniformVec3(const char* /*name*/, float x, float y, float z)
    {
        pushConst_[20] = x; pushConst_[21] = y; pushConst_[22] = z;
    }

    void SdlGpuEffectBackend::SetUniformVec2(const char* /*name*/, float x, float y)
    {
        pushConst_[20] = x; pushConst_[21] = y;
    }

    void SdlGpuEffectBackend::SetUniformFloat(const char* /*name*/, float value)
    {
        pushConst_[24] = value;
    }

    void SdlGpuEffectBackend::SetUniformInt(const char* /*name*/, int value)
    {
        pushConst_[24] = static_cast<float>(value);
    }

    void SdlGpuEffectBackend::SetViewportSizeEXT(float width, float height)
    {
        pushConst_[0] = width;
        pushConst_[1] = height;
    }

    std::unique_ptr<IEffectBackend> SdlGpuGraphicsBackend::CreateEffectBackend(
        const std::string& vertSrc, const std::string& fragSrc)
    {
        auto backend = std::make_unique<SdlGpuEffectBackend>(*this);
        if (!vertSrc.empty() && !fragSrc.empty())
            backend->CompileProgram(vertSrc, fragSrc);
        return backend;
    }

    // ---- SdlGpuSpriteBatchBackend ----

    SdlGpuSpriteBatchBackend::SdlGpuSpriteBatchBackend(SdlGpuGraphicsBackend& owner)
        : owner_(&owner)
    {
    }

    void SdlGpuSpriteBatchBackend::Begin()
    {
        if (begun_)
            throw std::logic_error("CNA SDL_GPU SpriteBatch.Begin called twice without End");
        begun_ = true;
    }

    void SdlGpuSpriteBatchBackend::End()
    {
        if (!begun_)
            throw std::logic_error("CNA SDL_GPU SpriteBatch.End called without Begin");
        begun_ = false;
    }

    void SdlGpuSpriteBatchBackend::SetCustomEffect(Effect* effect)
    {
        customEffect_ = effect;
    }

    void SdlGpuSpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y)
    {
        const Rectangle source{0, 0, texture.GetWidth(), texture.GetHeight()};
        const Rectangle destination{static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight()};
        Draw(texture, destination, source, Color::White);
    }

    void SdlGpuSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                        const Rectangle& destinationRectangle,
                                        const Rectangle& sourceRectangle,
                                        const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
    }

    void SdlGpuSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                        const Rectangle& destinationRectangle,
                                        const Rectangle& sourceRectangle,
                                        const Color& color,
                                        float rotation,
                                        const Vector2& origin,
                                        SpriteEffects effects,
                                        float layerDepth)
    {
        if (!begun_)
            throw std::logic_error("CNA SDL_GPU SpriteBatch.Draw called outside Begin/End");
        // A drawn texture is either a plain Texture2D (SdlGpuTextureBackend) or a RenderTarget2D
        // sampled after being rendered into (SdlGpuRenderTargetBackend, Phase SDLGPU-8) -- both
        // implement ITextureBackend but are unrelated concrete classes, so resolve whichever one
        // this actually is to get the raw SDL_GPUTexture* to bind.
        SDL_GPUTexture* nativeTexture = nullptr;
        if (const auto* plainTexture = dynamic_cast<const SdlGpuTextureBackend*>(&texture))
            nativeTexture = plainTexture->Texture();
        else if (const auto* renderTarget = dynamic_cast<const SdlGpuRenderTargetBackend*>(&texture))
            nativeTexture = renderTarget->ColorTexture();
        else
            throw std::invalid_argument("CNA SDL_GPU: SpriteBatch received a texture from another graphics backend");
        // SDLGPU-42/43: resolve the custom effect NOW (Draw()-call time), not once per Begin/End --
        // a game may reasonably change uniforms between individual Draw() calls within one
        // Begin/End cycle using the same custom effect object (see SpriteCommand's own doc comment).
        SdlGpuEffectBackend* customEffectBackend = customEffect_
            ? dynamic_cast<SdlGpuEffectBackend*>(customEffect_->GetEffectBackendPtr())
            : nullptr;
        owner_->QueueSprite(texture, nativeTexture, destinationRectangle, sourceRectangle, color, rotation,
                            origin, effects, layerDepth, transform_, textureFilter_, addressU_, addressV_,
                            customEffectBackend);
    }

}

namespace CNA::Internal::Backends
{
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<SdlGpu::SdlGpuGraphicsBackend>(
            args.window, args.virtualWidth, args.virtualHeight, args.presentationMode, args.swapInterval);
    }
}
