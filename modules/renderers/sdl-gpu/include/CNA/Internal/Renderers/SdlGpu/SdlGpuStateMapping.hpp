// SPDX-License-Identifier: MS-PL
#pragma once

// Pure implementation details shared with the renderer's context-free contract tests.
#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"
#include "CNA/Internal/Graphics/DxtUtil.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <stdexcept>

namespace CNA::Internal::Renderers::SdlGpu::Detail
{
        [[nodiscard]] inline SDL_GPUSamplerAddressMode ToAddressMode(int mode)
        {
            switch (mode)
            {
                case 0: return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
                case 2: return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
                default: return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            }
        }

        // Fills a complete SDL_GPUSamplerCreateInfo from a public XNA SamplerState.
        // Every TextureFilter ordinal is handled explicitly and separately for its three
        // components; there is no default branch that quietly turns an unrecognised value into
        // Point or Linear. Mirrors WebGPURenderer's FillWGPUSamplerDescriptor, which carries the
        // identical table.
        inline void FillSdlGpuSamplerCreateInfo(SDL_GPUSamplerCreateInfo& createInfo, int filter,
                                         int addressU, int addressV, int maxAnisotropy)
        {
            // XNA TextureFilter: 0=Linear,1=Point,2=Anisotropic,3=LinearMipPoint,4=PointMipLinear,
            // 5=MinLinearMagPointMipLinear,6=MinLinearMagPointMipPoint,
            // 7=MinPointMagLinearMipLinear,8=MinPointMagLinearMipPoint.
            SDL_GPUFilter magF = SDL_GPU_FILTER_LINEAR;
            SDL_GPUFilter minF = SDL_GPU_FILTER_LINEAR;
            SDL_GPUSamplerMipmapMode mipMode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
            bool enableAniso = false;
            switch (filter)
            {
                case 1: magF = SDL_GPU_FILTER_NEAREST; minF = SDL_GPU_FILTER_NEAREST; mipMode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST; break;
                case 2: magF = SDL_GPU_FILTER_LINEAR;  minF = SDL_GPU_FILTER_LINEAR;  mipMode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;  enableAniso = true; break;
                case 3: magF = SDL_GPU_FILTER_LINEAR;  minF = SDL_GPU_FILTER_LINEAR;  mipMode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST; break;
                case 4: magF = SDL_GPU_FILTER_NEAREST; minF = SDL_GPU_FILTER_NEAREST; mipMode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;  break;
                case 5: magF = SDL_GPU_FILTER_NEAREST; minF = SDL_GPU_FILTER_LINEAR;  mipMode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;  break;
                case 6: magF = SDL_GPU_FILTER_NEAREST; minF = SDL_GPU_FILTER_LINEAR;  mipMode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST; break;
                case 7: magF = SDL_GPU_FILTER_LINEAR;  minF = SDL_GPU_FILTER_NEAREST; mipMode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;  break;
                case 8: magF = SDL_GPU_FILTER_LINEAR;  minF = SDL_GPU_FILTER_NEAREST; mipMode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST; break;
                default: break; // Linear (0)
            }
            createInfo.min_filter = minF;
            createInfo.mag_filter = magF;
            createInfo.mipmap_mode = mipMode;
            createInfo.address_mode_u = ToAddressMode(addressU);
            createInfo.address_mode_v = ToAddressMode(addressV);
            createInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            createInfo.max_lod = 32.0f;
            createInfo.min_lod = 0.0f;
            createInfo.mip_lod_bias = 0.0f;
            // Only TextureFilter::Anisotropic names anisotropic filtering; every other ordinal
            // must leave it off, or a Point ordinal would silently become a filtered fetch. The
            // Vulkan driver under SDLGPU rejects a maxAnisotropy above its own device limit, so
            // the public value is clamped to the 16 the XNA API tops out at.
            createInfo.enable_anisotropy = enableAniso;
            createInfo.max_anisotropy =
                enableAniso ? static_cast<float>(std::clamp(maxAnisotropy, 1, 16)) : 1.0f;
        }


        // The exact XNA CompareFunction ordinal table:
        // Always=0, Never=1, Less=2, LessEqual=3, Equal=4, GreaterEqual=5, Greater=6, NotEqual=7.
        [[nodiscard]] inline SDL_GPUCompareOp ToCompareOp(int xnaCompare)
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
        // EasyGL's ToEasyGLStencilOp exactly): Keep=0, Zero=1,
        // Replace=2, Increment=3, Decrement=4, IncrementSaturation=5, DecrementSaturation=6, Invert=7.
        [[nodiscard]] inline SDL_GPUStencilOp ToStencilOp(int xnaOp)
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

        // SDLGPU-18: XNA Blend ordinals -> SDL_GPUBlendFactor (mirrors EasyGL's
        // ToEasyGLBlendFactor exactly): One=0, Zero=1, SourceColor=2,
        // InverseSourceColor=3, SourceAlpha=4, InverseSourceAlpha=5, DestinationColor=6,
        // InverseDestinationColor=7, DestinationAlpha=8, InverseDestinationAlpha=9, BlendFactor=10,
        // InverseBlendFactor=11, SourceAlphaSaturation=12.
        [[nodiscard]] inline SDL_GPUBlendFactor ToBlendFactor(int xnaBlend)
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
        // ReverseSubtract=2, Min=3, Max=4.
        [[nodiscard]] inline SDL_GPUBlendOp ToBlendOp(int xnaBlendFunc)
        {
            switch (xnaBlendFunc)
            {
                case 1: return SDL_GPU_BLENDOP_SUBTRACT;
                case 2: return SDL_GPU_BLENDOP_REVERSE_SUBTRACT;
                case 3: return SDL_GPU_BLENDOP_MIN;
                case 4: return SDL_GPU_BLENDOP_MAX;
                default: return SDL_GPU_BLENDOP_ADD;
            }
        }

        // SDLGPU-20: XNA CullMode ordinals -> SDL_GPUCullMode. Every pipeline in this renderer uses
        // front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE (matches this project's own EasyGL
        // renderer's real, hardware-validated "OpenGL default front face is CCW; CW faces are back
        // faces" convention -- confirmed this session that SDLGPU's 3D shaders, like EasyGL's,
        // need no NDC Y-flip, so no Vulkan-style winding-reversal adjustment applies here): None=0,
        // CullClockwiseFace=1 (cull the CW/back faces), CullCounterClockwiseFace=2 (cull the
        // CCW/front faces).
        [[nodiscard]] inline SDL_GPUCullMode ToCullMode(int xnaCullMode)
        {
            switch (xnaCullMode)
            {
                case 1: return SDL_GPU_CULLMODE_BACK;
                case 2: return SDL_GPU_CULLMODE_FRONT;
                default: return SDL_GPU_CULLMODE_NONE;
            }
        }

        // Pipeline caches use this value as their identity, not merely a bucket hash. The old
        // shift/add combiner aliased ordinary (sample count, depth format) pairs in the MRT
        // compatibility sweep. Avalanche every word before another state field is folded in.
        [[nodiscard]] inline std::size_t HashCombine(std::size_t seed, std::size_t value)
        {
            std::uint64_t mixed = static_cast<std::uint64_t>(seed) +
                                  static_cast<std::uint64_t>(value) + 0x9e3779b97f4a7c15ULL;
            mixed = (mixed ^ (mixed >> 30)) * 0xbf58476d1ce4e5b9ULL;
            mixed = (mixed ^ (mixed >> 27)) * 0x94d049bb133111ebULL;
            return static_cast<std::size_t>(mixed ^ (mixed >> 31));
        }

        [[nodiscard]] inline SDL_GPUVertexElementFormat ToSdlGpuStockVertexFormat(
            Microsoft::Xna::Framework::Graphics::VertexElementFormat format)
        {
            using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
            switch (format)
            {
                case VertexElementFormat::Single:           return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
                case VertexElementFormat::Vector2:          return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
                case VertexElementFormat::Vector3:          return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
                case VertexElementFormat::Vector4:          return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
                case VertexElementFormat::Color:            return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
                case VertexElementFormat::Byte4:            return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4;
                case VertexElementFormat::Short2:           return SDL_GPU_VERTEXELEMENTFORMAT_SHORT2;
                case VertexElementFormat::Short4:           return SDL_GPU_VERTEXELEMENTFORMAT_SHORT4;
                case VertexElementFormat::NormalizedShort2: return SDL_GPU_VERTEXELEMENTFORMAT_SHORT2_NORM;
                case VertexElementFormat::NormalizedShort4: return SDL_GPU_VERTEXELEMENTFORMAT_SHORT4_NORM;
                case VertexElementFormat::HalfVector2:      return SDL_GPU_VERTEXELEMENTFORMAT_HALF2;
                case VertexElementFormat::HalfVector4:      return SDL_GPU_VERTEXELEMENTFORMAT_HALF4;
            }
            throw std::invalid_argument(
                "CNA SDLGPU: unrecognized stock VertexElementFormat ordinal " +
                std::to_string(static_cast<int>(format)));
        }

        struct SdlGpuStockVertexStateEXT
        {
            std::array<SDL_GPUVertexBufferDescription,
                       CNA::Internal::Graphics::kMaxStockVertexStreamsEXT + 1> buffers{};
            std::array<SDL_GPUVertexAttribute,
                       CNA::Internal::Graphics::kMaxStockVertexAttributes> attributes{};
            Uint32 bufferCount = 0;
            Uint32 attributeCount = 0;
        };

        inline void BuildSdlGpuStockVertexStateEXT(
            const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& layout,
            SdlGpuStockVertexStateEXT& out)
        {
            out = {};
            const std::size_t streamCount = layout.streamCount > 0 ? layout.streamCount : 1u;
            for (std::size_t stream = 0; stream < streamCount; ++stream)
            {
                out.buffers[stream].slot = static_cast<Uint32>(stream);
                out.buffers[stream].pitch = static_cast<Uint32>(
                    stream < layout.streamCount ? layout.streams[stream].stride : layout.stride);
                out.buffers[stream].input_rate =
                    stream < layout.streamCount &&
                    layout.streams[stream].instanceFrequency > 0
                        ? SDL_GPU_VERTEXINPUTRATE_INSTANCE
                        : SDL_GPU_VERTEXINPUTRATE_VERTEX;
            }
            out.bufferCount = static_cast<Uint32>(streamCount);

            if (layout.usesNeutralRecord)
            {
                out.buffers[streamCount].slot = static_cast<Uint32>(streamCount);
                out.buffers[streamCount].pitch = static_cast<Uint32>(
                    sizeof(CNA::Internal::Graphics::kNeutralVertexRecordEXT));
                out.buffers[streamCount].input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;
                out.bufferCount = static_cast<Uint32>(streamCount + 1);
            }

            for (std::size_t i = 0; i < layout.count; ++i)
            {
                const auto& resolved = layout.attributes[i];
                SDL_GPUVertexAttribute& attribute = out.attributes[out.attributeCount++];
                attribute.location = static_cast<Uint32>(resolved.shaderLocation);
                attribute.buffer_slot = resolved.defaulted
                    ? static_cast<Uint32>(streamCount)
                    : static_cast<Uint32>(resolved.streamIndex);
                attribute.format = ToSdlGpuStockVertexFormat(resolved.format);
                attribute.offset = static_cast<Uint32>(resolved.offset);
            }
        }

        [[nodiscard]] inline std::size_t StockPipelineKey(
            std::size_t pipelineKey,
            const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& layout)
        {
            return HashCombine(
                pipelineKey,
                static_cast<std::size_t>(
                    CNA::Internal::Graphics::HashResolvedStockVertexLayoutEXT(layout)));
        }

        /// STREETS-0001: the shader locations of an instanced draw's four world-matrix columns --
        /// the retired Vulkan renderer's VULKAN-227 locations, past every stock per-vertex input
        /// here.
        constexpr int kInstanceColumnLocationEXT = 12;

        /// STREETS-0001: a fixed-layout family's vertex input state -- its own slot-0 record, plus,
        /// for an instanced draw, the per-instance matrix buffer at slot 1 feeding locations 12..15.
        struct FixedVertexInputEXT
        {
            std::array<SDL_GPUVertexBufferDescription, 2> buffers{};
            std::array<SDL_GPUVertexAttribute, 12> attributes{};
            Uint32 bufferCount = 0;
            Uint32 attributeCount = 0;
        };

        [[nodiscard]] inline FixedVertexInputEXT MakeFixedVertexInputEXT(
            const SDL_GPUVertexBufferDescription& record, const SDL_GPUVertexAttribute* attributes,
            Uint32 attributeCount, Uint32 instanceStride, const std::array<Uint32, 4>& columnOffsets)
        {
            FixedVertexInputEXT input;
            input.buffers[0] = record;
            input.bufferCount = 1;
            for (Uint32 i = 0; i < attributeCount; ++i)
                input.attributes[input.attributeCount++] = attributes[i];
            if (instanceStride == 0)
                return input;

            SDL_GPUVertexBufferDescription& instances = input.buffers[input.bufferCount++];
            instances.slot = 1;
            instances.pitch = instanceStride;
            instances.input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;
            for (std::size_t column = 0; column < columnOffsets.size(); ++column)
            {
                SDL_GPUVertexAttribute& attribute = input.attributes[input.attributeCount++];
                attribute.location = static_cast<Uint32>(kInstanceColumnLocationEXT + column);
                attribute.buffer_slot = 1;
                attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
                attribute.offset = columnOffsets[column];
            }
            return input;
        }

        /// STREETS-0001: an instanced fixed-layout pipeline differs from the ordinary one only in
        /// its vertex input, so that input is folded into the key. An ordinary key is unchanged.
        [[nodiscard]] inline std::size_t FixedInstancePipelineKey(
            std::size_t pipelineKey, Uint32 instanceStride,
            const std::array<Uint32, 4>& columnOffsets)
        {
            if (instanceStride == 0)
                return pipelineKey;
            std::size_t key = HashCombine(pipelineKey, 0x1257a7ce5u);
            key = HashCombine(key, instanceStride);
            for (const Uint32 offset : columnOffsets)
                key = HashCombine(key, offset);
            return key;
        }

        struct PipelineDepthBias
        {
            bool enabled = false;
            float constantFactor = 0.0f;
            float slopeFactor = 0.0f;
        };

        [[nodiscard]] inline bool IsPolygonTopology(SDL_GPUPrimitiveType topology)
        {
            return topology == SDL_GPU_PRIMITIVETYPE_TRIANGLELIST
                || topology == SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
        }

        // SDL_gpu exposes the native D3D/Vulkan/Metal constant-factor convention, whereas XNA's
        // RasterizerState.DepthBias is a normalized depth offset (Direct3D 9's D3DRS_DEPTHBIAS,
        // which XNA sets unchanged). Fixed-point formats use all of their depth bits. A float
        // attachment's unit is 2^(e-23) for the primitive's largest depth exponent e, which is
        // -1 over [0.5, 1) -- where a perspective scene's depth lies -- so D32 takes D24's 2^24
        // and its offset is exact there; FNA3D's SDL_gpu driver uses 2^23 and halves every such
        // bias (SdlGpu_CrossFamily_DepthBiasMagnitudeContract on a device without D24, as AMD's
        // Vulkan drivers are). Below 0.5 the unit halves per octave, as the native API defines.
        // A depthless pass follows FNA3D's D16 fallback -- the value is inert there, but keeping
        // it deterministic preserves cache identity.
        [[nodiscard]] inline float XnaToSdlDepthBiasScale(SDL_GPUTextureFormat depthStencilFormat)
        {
            switch (depthStencilFormat)
            {
                case SDL_GPU_TEXTUREFORMAT_D32_FLOAT:
                case SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT:
                case SDL_GPU_TEXTUREFORMAT_D24_UNORM:
                case SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT:
                    return static_cast<float>((1u << 24) - 1u);
                case SDL_GPU_TEXTUREFORMAT_D16_UNORM:
                case SDL_GPU_TEXTUREFORMAT_INVALID:
                default:
                    return static_cast<float>((1u << 16) - 1u);
            }
        }

        // XNA/FNA depth bias is polygon offset: independent line/point primitives have no
        // polygon slope and do not receive the D3D/OpenGL/Vulkan rasterizer bias. Normalize them
        // to disabled so irrelevant public values neither alter native state nor fragment caches.
        // For triangles, signed zero is canonicalized to +0 and the explicit SDL enable bit is
        // true iff either public factor is nonzero. NaN/Inf remain unmodified rather than being
        // silently approximated; SDL/its native driver remains the capability/validation authority.
        [[nodiscard]] inline PipelineDepthBias NormalizeDepthBias(
            SDL_GPUPrimitiveType topology, SDL_GPUTextureFormat depthStencilFormat,
            float depthBias, float slopeScaleDepthBias)
        {
            if (!IsPolygonTopology(topology))
                return {};

            PipelineDepthBias result;
            result.constantFactor = depthBias == 0.0f
                ? 0.0f
                : depthBias * XnaToSdlDepthBiasScale(depthStencilFormat);
            result.slopeFactor =
                slopeScaleDepthBias == 0.0f ? 0.0f : slopeScaleDepthBias;
            result.enabled =
                result.constantFactor != 0.0f || result.slopeFactor != 0.0f;
            return result;
        }

        [[nodiscard]] inline std::size_t HashDepthBias(
            std::size_t key, SDL_GPUPrimitiveType topology,
            SDL_GPUTextureFormat depthStencilFormat,
            float depthBias, float slopeScaleDepthBias)
        {
            const PipelineDepthBias bias =
                NormalizeDepthBias(
                    topology, depthStencilFormat,
                    depthBias, slopeScaleDepthBias);
            key = HashCombine(key, bias.enabled ? 1u : 0u);
            if (bias.enabled)
            {
                key = HashCombine(
                    key, static_cast<std::size_t>(
                             std::bit_cast<std::uint32_t>(bias.constantFactor)));
                // Clamp is a pipeline-static SDL field too, but CNA exposes no clamp property:
                // it is fixed at 0 for every pipeline and therefore is not an identity axis.
                key = HashCombine(
                    key, static_cast<std::size_t>(
                             std::bit_cast<std::uint32_t>(bias.slopeFactor)));
            }
            return key;
        }

        inline void FillDepthBiasState(
            SDL_GPURasterizerState& out, SDL_GPUPrimitiveType topology,
            SDL_GPUTextureFormat depthStencilFormat,
            float depthBias, float slopeScaleDepthBias)
        {
            const PipelineDepthBias bias =
                NormalizeDepthBias(
                    topology, depthStencilFormat,
                    depthBias, slopeScaleDepthBias);
            // XNA clips primitives outside its 0..1 depth interval. SDL_gpu defaults this field
            // to false (depth clamp), so it must be enabled explicitly on every pipeline.
            out.enable_depth_clip = true;
            out.enable_depth_bias = bias.enabled;
            if (bias.enabled)
            {
                // The normalized XNA constant was converted to the attachment's native r-units;
                // SlopeScaleDepthBias already has the same slope-factor meaning in both APIs.
                out.depth_bias_constant_factor = bias.constantFactor;
                out.depth_bias_clamp = 0.0f;
                out.depth_bias_slope_factor = bias.slopeFactor;
            }
        }

        // Packs (topology, depthTest, depthWrite, depthFunc, every MRT color format, sampleCount,
        // depthStencilFormat, full RenderStateSnapshot) into one cache key. INVALID means the
        // active pass has no depth/stencil attachment. Disabled dimensions (blend off, stencil off,
        // two-sided off) always collapse their own sub-fields out of the hash regardless of what
        // they're set to, so different "irrelevant" values don't create duplicate pipelines.
        // sampleCount (SDLGPU-38's MSAA fix) is a REQUIRED dimension, not an optional one to
        // collapse -- a pipeline created with the wrong sample_count for its render pass's actual
        // attachments is exactly finding #1 of the adversarial review this fixes.
        [[nodiscard]] inline std::size_t PipelineCacheKey(SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
                                                    const SdlGpuColorTargetFormatsEXT& colorFormats,
                                                    int colorTargetCount,
                                                    SDL_GPUSampleCount sampleCount,
                                                    SDL_GPUTextureFormat depthStencilFormat,
                                                    const SdlGpuRenderer::RenderStateSnapshot& rs)
        {
            std::size_t key = static_cast<std::size_t>(topology);
            key = HashCombine(key, depthTest ? 1u : 0u);
            key = HashCombine(key, depthWrite ? 1u : 0u);
            key = HashCombine(key, static_cast<std::size_t>(depthFunc));
            // SDLGPU/Vulkan render-pass compatibility is slot-aligned. SDLGPU-72 made several
            // native RT formats reachable, so slot 0 alone is insufficient: {Color,Color} and
            // {Color,HalfVector4} need different immutable pipelines even though their primary
            // attachment and every public render state are identical.
            key = HashCombine(key, static_cast<std::size_t>(colorTargetCount));
            for (int i = 0; i < colorTargetCount; ++i)
                key = HashCombine(key, static_cast<std::size_t>(colorFormats[i]));
            key = HashCombine(key, static_cast<std::size_t>(sampleCount));
            // Render-pass compatibility depends on attachment presence, format,
            // and samples independently of whether depth testing itself is enabled. A depthless
            // pass (INVALID) must never reuse a pipeline created for a depth-backed pass.
            key = HashCombine(key, static_cast<std::size_t>(depthStencilFormat));
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
            // The colour write mask is static pipeline state (applies to opaque
            // draws too), so it participates in the key. The default All(15) is a fixed contribution
            // ⇒ no cache fragmentation for the common case.
            for (int i = 0; i < colorTargetCount; ++i)
                key = HashCombine(key, static_cast<std::size_t>(rs.colorWriteMasks[i] & 0xF));
            key = HashCombine(key, static_cast<std::size_t>(rs.cullMode));
            key = HashCombine(key, rs.wireframe ? 1u : 0u);
            key = HashDepthBias(
                key, topology, depthStencilFormat,
                rs.depthBias, rs.slopeScaleDepthBias);
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
        // everywhere.
        inline void FillBlendState(SDL_GPUColorTargetBlendState& out, const SdlGpuRenderer::RenderStateSnapshot& rs,
                            int colorTargetIndex = 0)
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
            // BlendState.ColorWriteChannels. XNA bits (R=1,G=2,B=4,A=8) are identical
            // to SDL_GPU_COLORCOMPONENT_* (1<<0..1<<3). SDL writes all channels when
            // enable_color_write_mask is false, so only enable it for a non-All mask (keeps the
            // common default byte-identical to before).
            const Uint8 mask = static_cast<Uint8>(rs.colorWriteMasks[colorTargetIndex] & 0xF);
            if (mask != 0xF)
            {
                out.enable_color_write_mask = true;
                out.color_write_mask = mask;
            }
        }

        inline void FillColorTargetDescriptions(std::array<SDL_GPUColorTargetDescription, 4>& out,
                                         int colorTargetCount,
                                         const SdlGpuColorTargetFormatsEXT& colorFormats,
                                         const SdlGpuRenderer::RenderStateSnapshot& rs)
        {
            for (int i = 0; i < colorTargetCount; ++i)
            {
                out[i].format = colorFormats[i];
                FillBlendState(out[i].blend_state, rs, i);
            }
        }

        // Fills a pipeline's real front/back stencil-op state (SDLGPU-19) from a
        // RenderStateSnapshot, on top of the depthTest/depthWrite/depthFunc every pipeline already
        // threads through separately. XNA's TwoSidedStencilMode=false uses the SAME (ordinary,
        // clockwise) ops/func for both faces (matches this project's own EasyGL renderer's
        // identical fallback-to-front convention -- FNA's own real behavior: the CCW fields are
        // simply ignored when this is false, not reset to any default).
        inline void FillDepthStencilState(SDL_GPUDepthStencilState& out, bool depthTest, bool depthWrite, int depthFunc,
                                    const SdlGpuRenderer::RenderStateSnapshot& rs)
        {
            out.enable_depth_test = depthTest;
            out.enable_depth_write = depthWrite;
            out.compare_op = ToCompareOp(depthFunc);
            out.enable_stencil_test = rs.stencil.enable;
            // DepthStencilState.StencilMask/StencilWriteMask -- real values, not hardcoded 0xFF.
            out.compare_mask = static_cast<Uint8>(rs.stencil.readMask);
            out.write_mask = static_cast<Uint8>(rs.stencil.writeMask);
            // XNA's ordinary stencil fields govern clockwise-as-displayed faces and the
            // CounterClockwise* fields the other winding (GSC-0002, VKPAR-0005). Under this
            // renderer's COUNTER_CLOCKWISE front_face a clockwise triangle is the native BACK face
            // -- exactly what ToCullMode relies on when it maps CullClockwiseFace to BACK -- so
            // the ordinary operations belong in the back state and the CCW ones in the front.
            // Assigning them the other way round applied each face's operations to the opposite
            // winding (SdlGpu_EasyGLOracle_depthstencilstate_stencil_twosided).
            out.back_stencil_state.fail_op = ToStencilOp(rs.stencil.fail);
            out.back_stencil_state.pass_op = ToStencilOp(rs.stencil.pass);
            out.back_stencil_state.depth_fail_op = ToStencilOp(rs.stencil.depthFail);
            out.back_stencil_state.compare_op = ToCompareOp(rs.stencil.func);
            if (rs.stencil.twoSided)
            {
                out.front_stencil_state.fail_op = ToStencilOp(rs.stencil.ccwFail);
                out.front_stencil_state.pass_op = ToStencilOp(rs.stencil.ccwPass);
                out.front_stencil_state.depth_fail_op = ToStencilOp(rs.stencil.ccwDepthFail);
                out.front_stencil_state.compare_op = ToCompareOp(rs.stencil.ccwFunc);
            }
            else
            {
                out.front_stencil_state = out.back_stencil_state;
            }
        }

        struct CustomSpritePipelineState
        {
            SDL_GPUDepthStencilState depthStencil{};
            std::size_t key = 0;
        };

        [[nodiscard]] inline CustomSpritePipelineState BuildCustomSpritePipelineState(
            bool depthTest, bool depthWrite, int depthFunc,
            const SdlGpuColorTargetFormatsEXT& colorFormats, int colorTargetCount,
            SDL_GPUSampleCount sampleCount, SDL_GPUTextureFormat depthStencilFormat,
            const SdlGpuRenderer::RenderStateSnapshot& renderState)
        {
            CustomSpritePipelineState result;
            FillDepthStencilState(result.depthStencil, depthTest, depthWrite, depthFunc, renderState);
            result.key = PipelineCacheKey(
                SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, depthTest, depthWrite, depthFunc,
                colorFormats, colorTargetCount, sampleCount, depthStencilFormat, renderState);
            return result;
        }

        // Fills a pipeline's real cull/fill-mode rasterizer state (SDLGPU-20) from a
        // RenderStateSnapshot. front_face stays hardcoded COUNTER_CLOCKWISE everywhere in this
        // renderer (see ToCullMode's own doc comment).
        inline void FillRasterizerState(
            SDL_GPURasterizerState& out,
            const SdlGpuRenderer::RenderStateSnapshot& rs,
            SDL_GPUPrimitiveType topology,
            SDL_GPUTextureFormat depthStencilFormat)
        {
            out.fill_mode = rs.wireframe ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
            out.cull_mode = ToCullMode(rs.cullMode);
            out.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
            FillDepthBiasState(
                out, topology, depthStencilFormat,
                rs.depthBias, rs.slopeScaleDepthBias);
        }

        // Mirrors Texture2D.cpp's CalculateMipLevels -- each renderer keeps its own copy of this
        // small helper.
        [[nodiscard]] inline int CalculateMipLevels(int w, int h)
        {
            int levels = 1;
            while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++levels; }
            return levels;
        }

        using Microsoft::Xna::Framework::Graphics::DepthFormat;
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

        [[nodiscard]] constexpr bool IsClassicDxtFormat(SurfaceFormat format) noexcept
        {
            return format == SurfaceFormat::Dxt1 || format == SurfaceFormat::Dxt3 ||
                   format == SurfaceFormat::Dxt5;
        }

        [[nodiscard]] constexpr int LogicalTextureBlockBytes(SurfaceFormat format) noexcept
        {
            switch (format)
            {
                case SurfaceFormat::Alpha8:
                    return 1;
                case SurfaceFormat::Bgr565:
                case SurfaceFormat::Bgra5551:
                case SurfaceFormat::Bgra4444:
                case SurfaceFormat::NormalizedByte2:
                case SurfaceFormat::HalfSingle:
                    return 2;
                case SurfaceFormat::Dxt1:
                case SurfaceFormat::Vector2:
                case SurfaceFormat::HalfVector4:
                case SurfaceFormat::HdrBlendable:
                case SurfaceFormat::Rgba64:
                    return 8;
                case SurfaceFormat::Dxt3:
                case SurfaceFormat::Dxt5:
                case SurfaceFormat::Vector4:
                    return 16;
                case SurfaceFormat::Color:
                case SurfaceFormat::NormalizedByte4:
                case SurfaceFormat::Single:
                case SurfaceFormat::HalfVector2:
                case SurfaceFormat::Rg32:
                case SurfaceFormat::Rgba1010102:
                    return 4;
                default:
                    return 0;
            }
        }

        [[nodiscard]] constexpr SDL_GPUTextureFormat PreferredTextureFormat(
            SurfaceFormat format) noexcept
        {
            switch (format)
            {
                case SurfaceFormat::Color:           return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
                case SurfaceFormat::Bgr565:          return SDL_GPU_TEXTUREFORMAT_B5G6R5_UNORM;
                case SurfaceFormat::Bgra5551:        return SDL_GPU_TEXTUREFORMAT_B5G5R5A1_UNORM;
                case SurfaceFormat::Bgra4444:        return SDL_GPU_TEXTUREFORMAT_B4G4R4A4_UNORM;
                case SurfaceFormat::Dxt1:            return SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM;
                case SurfaceFormat::Dxt3:            return SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM;
                case SurfaceFormat::Dxt5:            return SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM;
                // D3D9 expands missing channels to one. Store two caller bytes in four SNORM
                // channels so every stock/custom sampling route sees (R,G,1,1) without requiring
                // a format-dependent shader variant.
                case SurfaceFormat::NormalizedByte2: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM;
                case SurfaceFormat::NormalizedByte4: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM;
                // Apple M4 AM4-184: the same rule for the classic float and wide
                // formats. Those with fewer than four channels are stored in four, with D3D9's
                // fill materialized at upload -- one for an absent R/G/B/A, and (0, 0, 0, A) for
                // Alpha8 -- so every route (stock, custom, sprite) samples what XNA samples.
                case SurfaceFormat::Single:
                case SurfaceFormat::Vector2:
                case SurfaceFormat::Vector4:         return SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
                case SurfaceFormat::HalfSingle:
                case SurfaceFormat::HalfVector2:
                case SurfaceFormat::HalfVector4:
                case SurfaceFormat::HdrBlendable:    return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
                case SurfaceFormat::Rg32:
                case SurfaceFormat::Rgba64:          return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM;
                case SurfaceFormat::Rgba1010102:     return SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM;
                case SurfaceFormat::Alpha8:          return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
                default:                             return SDL_GPU_TEXTUREFORMAT_INVALID;
            }
        }

        // AM4-184: the formats whose logical texels are widened to four stored channels.
        [[nodiscard]] constexpr bool TextureStorageExpandsChannels(SurfaceFormat format) noexcept
        {
            switch (format)
            {
                case SurfaceFormat::Single:
                case SurfaceFormat::Vector2:
                case SurfaceFormat::HalfSingle:
                case SurfaceFormat::HalfVector2:
                case SurfaceFormat::Rg32:
                case SurfaceFormat::Alpha8:
                    return true;
                default:
                    return false;
            }
        }

        // AM4-184: the formats that have no lossless RGBA8 stand-in, so a device without their
        // native storage refuses them rather than substituting.
        [[nodiscard]] constexpr bool TextureFormatNeedsExactStorage(SurfaceFormat format) noexcept
        {
            switch (format)
            {
                case SurfaceFormat::Single:
                case SurfaceFormat::Vector2:
                case SurfaceFormat::Vector4:
                case SurfaceFormat::HalfSingle:
                case SurfaceFormat::HalfVector2:
                case SurfaceFormat::HalfVector4:
                case SurfaceFormat::HdrBlendable:
                case SurfaceFormat::Rg32:
                case SurfaceFormat::Rgba64:
                case SurfaceFormat::Rgba1010102:
                    return true;
                default:
                    return false;
            }
        }

        struct RenderTargetFormatInfo
        {
            SDL_GPUTextureFormat nativeFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
            Uint32 bytesPerPixel = 0;
        };

        // Exact XNA render-target storage shared by classification, allocation, pipeline
        // compatibility and readback. This is the same nine-format set EasyGL actually creates;
        // unsupported classic values are refused instead of being substituted with Color.
        [[nodiscard]] constexpr bool TryGetRenderTargetFormatInfo(
            SurfaceFormat format, RenderTargetFormatInfo& out) noexcept
        {
            switch (format)
            {
                case SurfaceFormat::Color:
                    out = {SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 4};
                    return true;
                case SurfaceFormat::Rgba64:
                    out = {SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM, 8};
                    return true;
                case SurfaceFormat::Single:
                    out = {SDL_GPU_TEXTUREFORMAT_R32_FLOAT, 4};
                    return true;
                case SurfaceFormat::Vector2:
                    out = {SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT, 8};
                    return true;
                case SurfaceFormat::Vector4:
                    out = {SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT, 16};
                    return true;
                case SurfaceFormat::HalfSingle:
                    out = {SDL_GPU_TEXTUREFORMAT_R16_FLOAT, 2};
                    return true;
                case SurfaceFormat::HalfVector2:
                    out = {SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT, 4};
                    return true;
                case SurfaceFormat::HalfVector4:
                case SurfaceFormat::HdrBlendable:
                    out = {SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, 8};
                    return true;
                default:
                    return false;
            }
        }


        [[nodiscard]] inline std::array<float, 8> SpriteChannelExpansion(int surfaceFormat)
        {
            // mask.rgba followed by fill.rgba. XNA/D3D9 samples absent channels as one, whereas
            // Vulkan exposes zero for absent G/B channels. EasyGL applies this same correction in
            // its sprite shader; Color and four-channel formats retain the identity pair.
            std::array<float, 8> result{1.0f, 1.0f, 1.0f, 1.0f,
                                        0.0f, 0.0f, 0.0f, 0.0f};
            switch (static_cast<SurfaceFormat>(surfaceFormat))
            {
                case SurfaceFormat::Single:
                case SurfaceFormat::HalfSingle:
                    result[1] = result[2] = result[3] = 0.0f;
                    result[5] = result[6] = result[7] = 1.0f;
                    break;
                case SurfaceFormat::Vector2:
                case SurfaceFormat::HalfVector2:
                    result[2] = result[3] = 0.0f;
                    result[6] = result[7] = 1.0f;
                    break;
                default:
                    break;
            }
            return result;
        }


        [[nodiscard]] constexpr std::uint8_t Expand5To8(std::uint16_t value) noexcept
        {
            return static_cast<std::uint8_t>((value << 3) | (value >> 2));
        }

        [[nodiscard]] constexpr std::uint8_t Expand6To8(std::uint16_t value) noexcept
        {
            return static_cast<std::uint8_t>((value << 2) | (value >> 4));
        }

        [[nodiscard]] constexpr std::uint8_t Expand4To8(std::uint16_t value) noexcept
        {
            return static_cast<std::uint8_t>((value << 4) | value);
        }

        [[nodiscard]] inline std::vector<std::uint8_t> ConvertTextureLevelForFallback(
            SurfaceFormat format, const std::uint8_t* source, std::size_t sourceBytes,
            int width, int height)
        {
            if (source == nullptr)
                throw std::invalid_argument("CNA SDLGPU: texture conversion source cannot be null");

            if (IsClassicDxtFormat(format))
            {
                using CNA::Internal::Graphics::DxtUtil;
                if (format == SurfaceFormat::Dxt1)
                    return DxtUtil::DecompressDxt1(source, sourceBytes, width, height);
                if (format == SurfaceFormat::Dxt3)
                    return DxtUtil::DecompressDxt3(source, sourceBytes, width, height);
                return DxtUtil::DecompressDxt5(source, sourceBytes, width, height);
            }

            const std::size_t texelCount = static_cast<std::size_t>(width) * height;
            // AM4-184: widen a one- or two-channel float/wide texel to four, filling as D3D9 does.
            const auto widen = [&](std::size_t channelBytes, std::size_t presentChannels,
                                   std::uint32_t one) {
                std::vector<std::uint8_t> wide(texelCount * channelBytes * 4u);
                const std::size_t sourceTexel = channelBytes * presentChannels;
                const std::size_t wideTexel = channelBytes * 4u;
                for (std::size_t i = 0; i < texelCount; ++i)
                {
                    std::uint8_t* texel = wide.data() + i * wideTexel;
                    std::memcpy(texel, source + i * sourceTexel, sourceTexel);
                    for (std::size_t c = presentChannels; c < 4u; ++c)
                        std::memcpy(texel + c * channelBytes, &one, channelBytes);
                }
                return wide;
            };
            constexpr std::uint32_t floatOne = 0x3F800000u;  // 1.0f
            constexpr std::uint32_t halfOne = 0x3C00u;       // 1.0 in IEEE half
            constexpr std::uint32_t unorm16One = 0xFFFFu;
            switch (format)
            {
                case SurfaceFormat::Single:      return widen(4u, 1u, floatOne);
                case SurfaceFormat::Vector2:     return widen(4u, 2u, floatOne);
                case SurfaceFormat::HalfSingle:  return widen(2u, 1u, halfOne);
                case SurfaceFormat::HalfVector2: return widen(2u, 2u, halfOne);
                case SurfaceFormat::Rg32:        return widen(2u, 2u, unorm16One);
                case SurfaceFormat::Alpha8:
                {
                    std::vector<std::uint8_t> rgba(texelCount * 4u, 0u);
                    for (std::size_t i = 0; i < texelCount; ++i)
                        rgba[i * 4u + 3u] = source[i];
                    return rgba;
                }
                default:
                    break;
            }

            std::vector<std::uint8_t> result(texelCount * 4u);
            if (format == SurfaceFormat::NormalizedByte2)
            {
                for (std::size_t i = 0; i < texelCount; ++i)
                {
                    result[i * 4u + 0u] = source[i * 2u + 0u];
                    result[i * 4u + 1u] = source[i * 2u + 1u];
                    result[i * 4u + 2u] = 0x7Fu;
                    result[i * 4u + 3u] = 0x7Fu;
                }
                return result;
            }

            for (std::size_t i = 0; i < texelCount; ++i)
            {
                const std::uint16_t packed = static_cast<std::uint16_t>(source[i * 2u]) |
                    static_cast<std::uint16_t>(source[i * 2u + 1u]) << 8;
                std::uint8_t r = 0, g = 0, b = 0, a = 255;
                if (format == SurfaceFormat::Bgr565)
                {
                    r = Expand5To8(static_cast<std::uint16_t>((packed >> 11) & 0x1Fu));
                    g = Expand6To8(static_cast<std::uint16_t>((packed >> 5) & 0x3Fu));
                    b = Expand5To8(static_cast<std::uint16_t>(packed & 0x1Fu));
                }
                else if (format == SurfaceFormat::Bgra5551)
                {
                    r = Expand5To8(static_cast<std::uint16_t>((packed >> 10) & 0x1Fu));
                    g = Expand5To8(static_cast<std::uint16_t>((packed >> 5) & 0x1Fu));
                    b = Expand5To8(static_cast<std::uint16_t>(packed & 0x1Fu));
                    a = (packed & 0x8000u) != 0 ? 255 : 0;
                }
                else if (format == SurfaceFormat::Bgra4444)
                {
                    r = Expand4To8(static_cast<std::uint16_t>((packed >> 8) & 0xFu));
                    g = Expand4To8(static_cast<std::uint16_t>((packed >> 4) & 0xFu));
                    b = Expand4To8(static_cast<std::uint16_t>(packed & 0xFu));
                    a = Expand4To8(static_cast<std::uint16_t>((packed >> 12) & 0xFu));
                }
                else
                {
                    throw std::invalid_argument(
                        "CNA SDLGPU: no renderer-side conversion for SurfaceFormat ordinal " +
                        std::to_string(static_cast<int>(format)));
                }
                result[i * 4u + 0u] = r;
                result[i * 4u + 1u] = g;
                result[i * 4u + 2u] = b;
                result[i * 4u + 3u] = a;
            }
            return result;
        }


        [[nodiscard]] inline int SampleCountToInt(SDL_GPUSampleCount count)
        {
            switch (count)
            {
                case SDL_GPU_SAMPLECOUNT_2: return 2;
                case SDL_GPU_SAMPLECOUNT_4: return 4;
                case SDL_GPU_SAMPLECOUNT_8: return 8;
                default: return 0;
            }
        }

        // The 128-byte per-draw layout, filled with DrawColoredPrimitives()'s hardcoded
        // white/vertex-color-always-true behaviour.
        inline void FillColoredUniforms(std::array<float, 32>& out, const Matrix& wvp)
        {
            wvp.ToColumnMajor(out.data());
            out[16] = 1.0f; out[17] = 1.0f; out[18] = 1.0f; out[19] = 1.0f;
            for (int i = 20; i < 31; ++i) out[i] = 0.0f;
            out[31] = 1.0f;
        }

        // Mirrors WebGPURenderer::FillExtUniforms() field-for-field -- real GpuDrawParams, used
        // by DrawPrimitivesEx()'s dispatch.
        inline void FillExtUniforms(std::array<float, 32>& out, const Matrix& wvp, const GpuDrawParams& p)
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

        // Shared 32-byte FogParams block -- vec4 fogColorEnabled (xyz =
        // FogColor, w = fogEnabled 0/1) + vec4 fogVector (FNA's view-space fog vector).
        // Bound to the vertex stage only; the vertex shader computes keep = 1 -
        // saturate(dot(vec4(pos,1), fogVector)) -- object-space position for non-skinned, POST-skin
        // for skinned -- and forwards FogColor + keep to the fragment stage as a varying. A
        // default-constructed all-zero block means no fog (dot -> 0 -> keep 1) -- correct for the
        // no-GpuDrawParams FillColoredUniforms path (DrawColoredPrimitives), which never sets fog.
        inline void FillFogUniforms(std::array<float, 8>& out, const GpuDrawParams& p)
        {
            out[0] = p.fogColor[0]; out[1] = p.fogColor[1]; out[2] = p.fogColor[2];
            out[3] = p.fogEnabled ? 1.0f : 0.0f;
            // FNA fog vector (bakes World*View + fogStart/fogEnd; zero when disabled).
            out[4] = p.fogVector[0]; out[5] = p.fogVector[1];
            out[6] = p.fogVector[2]; out[7] = p.fogVector[3];
        }

        // Secondary UBO for lit_textured3d.glsl: DirectionalLight1/DirectionalLight2, EmissiveColor,
        // World (the vertex shader computes its own normal matrix via GLSL's built-in inverse(),
        // unlike WebGPURenderer's WGSL-forced CPU-side precomputation -- no normal-matrix
        // slots needed here), EyePosition, per-light SpecularColor, material SpecularColor/Power.
        // Fog is not carried here -- it travels in a separate FogParams block
        // (FillFogUniforms).
        inline void FillLitLightUniforms(std::array<float, 56>& out, const GpuDrawParams& p)
        {
            out[0] = p.light1Dir[0]; out[1] = p.light1Dir[1]; out[2] = p.light1Dir[2]; out[3] = 0.0f;
            out[4] = p.light1Diffuse[0]; out[5] = p.light1Diffuse[1]; out[6] = p.light1Diffuse[2]; out[7] = 0.0f;
            out[8] = p.light2Dir[0]; out[9] = p.light2Dir[1]; out[10] = p.light2Dir[2]; out[11] = 0.0f;
            out[12] = p.light2Diffuse[0]; out[13] = p.light2Diffuse[1]; out[14] = p.light2Diffuse[2]; out[15] = 0.0f;
            // The fourth lane was padding. It now carries the public lighting-family selector to
            // both stages without changing this established 56-float block: zero is XNA's default
            // per-vertex/Gouraud path, one requests the per-pixel path.
            out[16] = p.emissiveColor[0]; out[17] = p.emissiveColor[1]; out[18] = p.emissiveColor[2];
            // SMG-0032: a draw that receives a shadow takes the
            // per-pixel path whatever PreferPerPixelLighting says. Gouraud lighting would evaluate
            // the lookup at the corners and interpolate it, so a ground plane of two triangles
            // would carry a gradient rather than a shadow. WebGPU and EasyGL apply the same
            // rule, so "the default per-vertex lighting still receives a shadow" means the same
            // thing on all three.
            const bool receivesShadow = p.shadowsEnabled && p.shadowMap != nullptr;
            out[19] = (p.preferPerPixelLighting || receivesShadow) ? 1.0f : 0.0f;
            for (int wi = 0; wi < 16; ++wi) out[20 + wi] = p.worldColMajor[wi];
            out[36] = p.eyePositionWorld[0]; out[37] = p.eyePositionWorld[1]; out[38] = p.eyePositionWorld[2]; out[39] = 0.0f;
            out[40] = p.light0Specular[0]; out[41] = p.light0Specular[1]; out[42] = p.light0Specular[2]; out[43] = 0.0f;
            out[44] = p.light1Specular[0]; out[45] = p.light1Specular[1]; out[46] = p.light1Specular[2]; out[47] = 0.0f;
            out[48] = p.light2Specular[0]; out[49] = p.light2Specular[1]; out[50] = p.light2Specular[2]; out[51] = 0.0f;
            out[52] = p.specularColor[0]; out[53] = p.specularColor[1]; out[54] = p.specularColor[2]; out[55] = p.specularPower;
        }

        // HND-TQ-25 D9: the vertex-stage tail of LitLightParams, SkinnedLightParams and
        // EnvMapParams. Those vertex shaders derive the normal matrix from World with inverse();
        // a base-Effect/EffectMaterial clone of a stock effect instead feeds the stock shader its
        // raw WorldInverseTranspose (GpuDrawParams::rawStockTransforms). Three vec4 columns of
        // that 3x3; the first column's w is 1 exactly when the shader is to use them.
        inline void FillClassicNormalMatrixOverride(std::array<float, 12>& out, const GpuDrawParams& p)
        {
            const float* n = p.worldInverseTransposeColMajor;
            for (int column = 0; column < 3; ++column)
            {
                out[column * 4 + 0] = p.rawStockTransforms ? n[column * 3 + 0] : 0.0f;
                out[column * 4 + 1] = p.rawStockTransforms ? n[column * 3 + 1] : 0.0f;
                out[column * 4 + 2] = p.rawStockTransforms ? n[column * 3 + 2] : 0.0f;
                out[column * 4 + 3] = 0.0f;
            }
            out[3] = p.rawStockTransforms ? 1.0f : 0.0f;
        }

        // The vertex-stage block: a fragment-stage block of the same name ends before the tail.
        template <std::size_t N>
        [[nodiscard]] inline std::array<float, N + 12> WithClassicNormalMatrixOverride(
            const std::array<float, N>& block, const std::array<float, 12>& normalMatrix)
        {
            std::array<float, N + 12> out{};
            for (std::size_t i = 0; i < N; ++i) out[i] = block[i];
            for (std::size_t i = 0; i < 12; ++i) out[N + i] = normalMatrix[i];
            return out;
        }

        // pbr3d.frag.glsl's tertiary PbrParams block: material/map factors, alpha coverage,
        // transfer flags, specular Fresnel inputs, and fourteen affine texture-transform rows.
        inline void FillPbrParams(std::array<float, 72>& out, const GpuDrawParams& p)
        {
            out[0] = p.pbrMetallicFactor;
            out[1] = p.pbrRoughnessFactor;
            out[2] = p.pbrNormalScale;
            out[3] = p.pbrOcclusionStrength;
            out[4] = p.alphaTest[0];
            out[5] = p.alphaTest[1];
            out[6] = p.alphaTest[2];
            out[7] = p.alphaTest[3];
            out[8] = p.pbrBaseColorTextureIsSrgb ? 1.0f : 0.0f;
            out[9] = p.pbrEmissiveTextureIsSrgb ? 1.0f : 0.0f;
            out[10] = p.pbrEncodeOutputToSrgb ? 1.0f : 0.0f;
            out[11] = p.pbrSpecularColorTextureIsSrgb ? 1.0f : 0.0f;
            out[12] = p.pbrDielectricF0Unclamped[0];
            out[13] = p.pbrDielectricF0Unclamped[1];
            out[14] = p.pbrDielectricF0Unclamped[2];
            out[15] = p.pbrSpecularFactor;
            for (int row = 0; row < 10; ++row)
                for (int component = 0; component < 4; ++component)
                    out[16 + row * 4 + component] =
                        p.pbrTextureTransformRows[row][component];
            for (int row = 0; row < 4; ++row)
                for (int component = 0; component < 4; ++component)
                    out[56 + row * 4 + component] =
                        p.pbrSpecularTextureTransformRows[row][component];
        }

        // Mirrors WebGPURenderer::FillAlphaTestUniforms() field-for-field (minus fog):
        // [20..23]=alphaTest params (refVal, tolerance, passWeight, failWeight),
        // [24]=vertexColorEnabled -- the ambient/light0/textureEnabled slots FillExtUniforms uses
        // are repurposed since AlphaTestEffect has no lighting.
        inline void FillAlphaTestUniforms(std::array<float, 32>& out, const Matrix& wvp, const GpuDrawParams& p)
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
        inline void FillEnvMapUniforms(std::array<float, 24>& out, const Matrix& wvp, const GpuDrawParams& p)
        {
            wvp.ToColumnMajor(out.data());
            out[16] = p.diffuseColor[0]; out[17] = p.diffuseColor[1];
            out[18] = p.diffuseColor[2]; out[19] = p.diffuseColor[3];
            out[20] = p.emissiveColor[0]; out[21] = p.emissiveColor[1];
            out[22] = p.emissiveColor[2]; out[23] = p.envMapAmount;
        }

        // env_map3d.glsl's secondary EnvMapParams block: world(16) + 8 vec4 (32) = 48 floats.
        // Fog is not carried here -- it travels in a separate FogParams block.
        inline void FillEnvMapParams(std::array<float, 48>& out, const GpuDrawParams& p)
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
        // eyePos_weightsPerVertex.w slot FillLitLightUniforms leaves as pad. This identical
        // layout is exactly why skinned3d's fragment stage can reuse lit_textured3d's fragment
        // shader unchanged.
        inline void FillSkinnedLightUniforms(std::array<float, 56>& out, const GpuDrawParams& p)
        {
            FillLitLightUniforms(out, p);
            out[39] = static_cast<float>(p.weightsPerVertex);
        }

        // skinned3d.vert.glsl's bone palette: 72 mat4 = 1152 floats (4608 bytes), column-major,
        // straight from GpuDrawParams::boneTransforms (already column-major per
        // SkinnedEffect::FillGpuDrawParams()).
        inline void FillSkinnedBoneUniforms(std::array<float, 72 * 16>& out, const GpuDrawParams& p)
        {
            const int count = std::min(p.boneCount, 72);
            for (int i = 0; i < count * 16; ++i)
                out[i] = p.boneTransforms[i];
            for (int i = count * 16; i < 72 * 16; ++i)
                out[i] = 0.0f;
        }
}
