// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"
#include "CNA/Internal/Renderers/Common/VertexColourPbrSupport.hpp"
#include "CNA/Platform/Detail/Sdl3RendererInterop.hpp"

#include "CNA/Logger.hpp"
#include "CNA/LogCategory.hpp"
#include "shaders/spirv_shaders.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
#include "CNA/Internal/Renderers/SdlGpu/SdlGpuCompiledEffect.hpp"
#include "CNA/Internal/Renderers/SdlGpu/SdlGpuCompiledEffectVertexLayout.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#endif

#include <SDL3/SDL.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers::SdlGpu
{
    namespace
    {
        enum class ConstructionShader : std::size_t
        {
            SpriteVertex,
            SpriteFragment,
            ColoredVertex,
            ColoredFragment,
            TexturedVertex,
            ColoredTexturedVertex,
            TexturedFragment,
            LitTexturedVertex,
            LitTexturedFragment,
            AlphaTestVertex,
            AlphaTestColoredVertex,
            AlphaTestFragment,
            DualTextureVertex,
            DualTextureColoredVertex,
            DualTextureFragment,
            EnvMapVertex,
            EnvMapFragment,
            SkinnedVertex,
            SkinnedColoredVertex,
            SkinnedColoredFragment,
            PbrVertex,
            PbrSkinnedVertex,
            PbrFragment,
            Count
        };

        [[nodiscard]] const char* FailurePointName(SdlGpuFailurePointEXT point)
        {
            switch (point)
            {
                case SdlGpuFailurePointEXT::DeviceCreation: return "device creation";
                case SdlGpuFailurePointEXT::WindowClaim: return "window claiming";
                case SdlGpuFailurePointEXT::SwapchainSetup: return "swapchain setup";
                case SdlGpuFailurePointEXT::DepthStencilFormatQuery: return "depth/stencil format query";
                case SdlGpuFailurePointEXT::SpriteVertexShaderCreation: return "sprite vertex shader creation";
                case SdlGpuFailurePointEXT::SpriteFragmentShaderCreation: return "sprite fragment shader creation";
                case SdlGpuFailurePointEXT::ColoredVertexShaderCreation: return "colored vertex shader creation";
                case SdlGpuFailurePointEXT::ColoredFragmentShaderCreation: return "colored fragment shader creation";
                case SdlGpuFailurePointEXT::TexturedVertexShaderCreation: return "textured vertex shader creation";
                case SdlGpuFailurePointEXT::ColoredTexturedVertexShaderCreation: return "colored-textured vertex shader creation";
                case SdlGpuFailurePointEXT::TexturedFragmentShaderCreation: return "textured fragment shader creation";
                case SdlGpuFailurePointEXT::LitTexturedVertexShaderCreation: return "lit-textured vertex shader creation";
                case SdlGpuFailurePointEXT::LitTexturedFragmentShaderCreation: return "lit-textured fragment shader creation";
                case SdlGpuFailurePointEXT::AlphaTestVertexShaderCreation: return "alpha-test vertex shader creation";
                case SdlGpuFailurePointEXT::AlphaTestColoredVertexShaderCreation: return "alpha-test colored vertex shader creation";
                case SdlGpuFailurePointEXT::AlphaTestFragmentShaderCreation: return "alpha-test fragment shader creation";
                case SdlGpuFailurePointEXT::DualTextureVertexShaderCreation: return "dual-texture vertex shader creation";
                case SdlGpuFailurePointEXT::DualTextureColoredVertexShaderCreation: return "dual-texture colored vertex shader creation";
                case SdlGpuFailurePointEXT::DualTextureFragmentShaderCreation: return "dual-texture fragment shader creation";
                case SdlGpuFailurePointEXT::EnvMapVertexShaderCreation: return "environment-map vertex shader creation";
                case SdlGpuFailurePointEXT::EnvMapFragmentShaderCreation: return "environment-map fragment shader creation";
                case SdlGpuFailurePointEXT::SkinnedVertexShaderCreation: return "skinned vertex shader creation";
                case SdlGpuFailurePointEXT::SkinnedColoredVertexShaderCreation: return "skinned-colored vertex shader creation";
                case SdlGpuFailurePointEXT::SkinnedColoredFragmentShaderCreation: return "skinned-colored fragment shader creation";
                case SdlGpuFailurePointEXT::PbrVertexShaderCreation: return "PBR vertex shader creation";
                case SdlGpuFailurePointEXT::PbrSkinnedVertexShaderCreation: return "skinned PBR vertex shader creation";
                case SdlGpuFailurePointEXT::PbrFragmentShaderCreation: return "PBR fragment shader creation";
                case SdlGpuFailurePointEXT::WindowMetricsInitialization: return "window metrics initialization";
                case SdlGpuFailurePointEXT::RendererRegistration: return "renderer registration";
                case SdlGpuFailurePointEXT::AfterRendererRegistration: return "post-registration commit";
                case SdlGpuFailurePointEXT::FrameCommandBufferAcquisition: return "frame command-buffer acquisition";
                case SdlGpuFailurePointEXT::GraphicsPipelineCreation: return "graphics pipeline creation";
                case SdlGpuFailurePointEXT::SamplerCreation: return "sampler creation";
                case SdlGpuFailurePointEXT::DefaultWhiteTextureCreation: return "default white texture creation";
                case SdlGpuFailurePointEXT::DefaultFlatNormalTextureCreation: return "default flat-normal texture creation";
                case SdlGpuFailurePointEXT::None: return "none";
            }
            return "unknown";
        }

        void NotifyResource(const SdlGpuTestHooksEXT& hooks, SdlGpuResourceKindEXT resource,
                            SdlGpuResourceEventEXT event) noexcept
        {
            if (hooks.resourceEvent != nullptr)
                hooks.resourceEvent(hooks.context, resource, event);
        }

        void InjectFailure(const SdlGpuTestHooksEXT& hooks, bool& injected,
                           SdlGpuFailurePointEXT point)
        {
            if (!injected && hooks.failAt == point)
            {
                injected = true;
                throw std::runtime_error(
                    std::string("CNA SDL_GPU: injected failure during ") + FailurePointName(point));
            }
        }

        class FrameCommandBufferOwner
        {
        public:
            FrameCommandBufferOwner(SDL_GPUCommandBuffer* commandBuffer,
                                    const SdlGpuTestHooksEXT& testHooks)
                : commandBuffer_(commandBuffer), hooks_(testHooks)
            {
                NotifyResource(hooks_, SdlGpuResourceKindEXT::FrameCommandBuffer,
                               SdlGpuResourceEventEXT::Acquired);
            }

            FrameCommandBufferOwner(const FrameCommandBufferOwner&) = delete;
            FrameCommandBufferOwner& operator=(const FrameCommandBufferOwner&) = delete;

            ~FrameCommandBufferOwner()
            {
                FinishForFailure();
            }

            [[nodiscard]] SDL_GPUCommandBuffer* Get() const { return commandBuffer_; }

            void SwapchainAcquisitionStarted() { mustSubmit_ = true; }

            [[nodiscard]] bool Submit()
            {
                if (finished_)
                    return true;
                finished_ = true;
                const bool result = SDL_SubmitGPUCommandBuffer(commandBuffer_);
                NotifyResource(hooks_, SdlGpuResourceKindEXT::FrameCommandBuffer,
                               SdlGpuResourceEventEXT::Released);
                return result;
            }

            void FinishForFailure() noexcept
            {
                if (finished_)
                    return;
                finished_ = true;
                if (mustSubmit_)
                    (void)SDL_SubmitGPUCommandBuffer(commandBuffer_);
                else
                    (void)SDL_CancelGPUCommandBuffer(commandBuffer_);
                NotifyResource(hooks_, SdlGpuResourceKindEXT::FrameCommandBuffer,
                               SdlGpuResourceEventEXT::Released);
            }

        private:
            SDL_GPUCommandBuffer* commandBuffer_ = nullptr;
            SdlGpuTestHooksEXT hooks_{};
            bool mustSubmit_ = false;
            bool finished_ = false;
        };

        class RenderPassOwner
        {
        public:
            explicit RenderPassOwner(SDL_GPURenderPass* pass) : pass_(pass) {}
            RenderPassOwner(const RenderPassOwner&) = delete;
            RenderPassOwner& operator=(const RenderPassOwner&) = delete;
            ~RenderPassOwner() { End(); }

            [[nodiscard]] SDL_GPURenderPass* Get() const { return pass_; }
            void End() noexcept
            {
                if (pass_ != nullptr)
                {
                    SDL_EndGPURenderPass(pass_);
                    pass_ = nullptr;
                }
            }

        private:
            SDL_GPURenderPass* pass_ = nullptr;
        };

        class CopyPassOwner
        {
        public:
            explicit CopyPassOwner(SDL_GPUCopyPass* pass) : pass_(pass) {}
            CopyPassOwner(const CopyPassOwner&) = delete;
            CopyPassOwner& operator=(const CopyPassOwner&) = delete;
            ~CopyPassOwner() { End(); }

            [[nodiscard]] SDL_GPUCopyPass* Get() const { return pass_; }
            void End() noexcept
            {
                if (pass_ != nullptr)
                {
                    SDL_EndGPUCopyPass(pass_);
                    pass_ = nullptr;
                }
            }

        private:
            SDL_GPUCopyPass* pass_ = nullptr;
        };

        // REMED-GFX-170: the cache key is the COMPLETE sampler description, exactly like
        // WebGPURenderer::GetOrCreateSlotSampler's own. It used to be `filterIndex*9+u*3+v`
        // with `filterIndex = filter == 0 ? 0 : 1`, an 18-entry array that collapsed all eight
        // non-Linear ordinals onto ONE entry -- so even a corrected descriptor would have been
        // handed the first non-Linear sampler ever built for that address-mode pair.
        [[nodiscard]] std::uint32_t SamplerCacheKey(int filter, int addressU, int addressV,
                                                    int maxAnisotropy)
        {
            return (static_cast<std::uint32_t>(filter) & 0xFFu)
                 | ((static_cast<std::uint32_t>(addressU) & 0xFFu) << 8)
                 | ((static_cast<std::uint32_t>(addressV) & 0xFFu) << 16)
                 | ((static_cast<std::uint32_t>(maxAnisotropy) & 0xFFu) << 24);
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

        // REMED-GFX-170: fills a complete SDL_GPUSamplerCreateInfo from a public XNA SamplerState.
        // Every TextureFilter ordinal is handled explicitly and separately for its three
        // components; there is no default branch that quietly turns an unrecognised value into
        // Point or Linear. Mirrors WebGPURenderer's FillWGPUSamplerDescriptor and
        // VulkanRenderer::ApplySamplerState, which carry the identical table.
        void FillSdlGpuSamplerCreateInfo(SDL_GPUSamplerCreateInfo& createInfo, int filter,
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
            // Only TextureFilter::Anisotropic names anisotropic filtering; every other ordinal
            // must leave it off, or a Point ordinal would silently become a filtered fetch. The
            // Vulkan driver under SDL_GPU rejects a maxAnisotropy above its own device limit, so
            // the public value is clamped to the 16 the XNA API tops out at.
            createInfo.enable_anisotropy = enableAniso;
            createInfo.max_anisotropy =
                enableAniso ? static_cast<float>(std::clamp(maxAnisotropy, 1, 16)) : 1.0f;
        }

        // REMED-GFX-170: the public TextureFilter ordinal's own name, for CNA_SDLGPU_SAMPLER_TRACE.
        [[nodiscard]] const char* TextureFilterName(int filter)
        {
            switch (filter)
            {
                case 0: return "Linear";
                case 1: return "Point";
                case 2: return "Anisotropic";
                case 3: return "LinearMipPoint";
                case 4: return "PointMipLinear";
                case 5: return "MinLinearMagPointMipLinear";
                case 6: return "MinLinearMagPointMipPoint";
                case 7: return "MinPointMagLinearMipLinear";
                case 8: return "MinPointMagLinearMipPoint";
                default: return "<out-of-range>";
            }
        }

        [[nodiscard]] const char* SdlFilterName(SDL_GPUFilter f)
        {
            return f == SDL_GPU_FILTER_LINEAR ? "Linear" : "Nearest";
        }

        [[nodiscard]] const char* SdlMipmapModeName(SDL_GPUSamplerMipmapMode m)
        {
            return m == SDL_GPU_SAMPLERMIPMAPMODE_LINEAR ? "Linear" : "Nearest";
        }

        [[nodiscard]] const char* SdlAddressModeName(SDL_GPUSamplerAddressMode m)
        {
            switch (m)
            {
                case SDL_GPU_SAMPLERADDRESSMODE_REPEAT: return "Repeat";
                case SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT: return "MirrorRepeat";
                default: return "ClampToEdge";
            }
        }

        [[nodiscard]] bool SamplerTraceEnabled()
        {
            static const bool enabled = std::getenv("CNA_SDLGPU_SAMPLER_TRACE") != nullptr;
            return enabled;
        }

        // REMED-GFX-173: EnvironmentMapEffect binds TWO sampled resources from TWO public sampler
        // slots, and CNA_SDLGPU_SAMPLER_TRACE reports one binding per line with no way to tell
        // which pair belonged to the same draw. This prints the whole two-slot binding on one
        // line, at both ends of the deferred pipeline: what the public draw call CAPTURED, and
        // what replay actually BOUND.
        [[nodiscard]] bool EnvMapTraceEnabled()
        {
            static const bool enabled = std::getenv("CNA_SDLGPU_ENVMAP_TRACE") != nullptr;
            return enabled;
        }

        // REMED-GFX-176: a Texture2D's declared level count, the count SDL was really asked for and
        // every level upload's exact destination, on one line each. Without it "the texture has a
        // chain" can only be inferred from pixels, and a texture that allocates one level looks
        // identical from the public API to one that allocates four and never had them written.
        [[nodiscard]] bool TextureTraceEnabled()
        {
            static const bool enabled = std::getenv("CNA_SDLGPU_TEXTURE_TRACE") != nullptr;
            return enabled;
        }

        /// Distinguishes textures in CNA_SDLGPU_TEXTURE_TRACE; never read for anything else.
        [[nodiscard]] int NextTextureTraceId()
        {
            static int next = 0;
            return ++next;
        }

        /**
         * REMED-GFX-186: `CNA_SDLGPU_TRACE_TARGET_READBACK=1` prints, for every render-target
         * `GetData`, exactly which native resource was selected and what was asked of it.
         *
         * The defect this exists for was a SIGSEGV inside `VULKAN_DownloadFromTexture`, reached
         * with an `SDL_GPUTextureRegion.mip_level` that the chosen texture did not own. Nothing
         * in the public call, the handles or SDL's own debug mode said so -- the only way to see
         * it is to print the requested level beside the level count the resource was really
         * created with, which is what this line does.
         */
        [[nodiscard]] bool TargetReadbackTraceEnabled()
        {
            static const bool enabled = std::getenv("CNA_SDLGPU_TRACE_TARGET_READBACK") != nullptr;
            return enabled;
        }

        /** REMED-GFX-187/GFX-188: traces exact per-level GPU target-mip blits. */
        [[nodiscard]] bool TargetMipTraceEnabled()
        {
            static const bool enabled = std::getenv("CNA_SDLGPU_TRACE_TARGET_MIPS") != nullptr;
            return enabled;
        }

        /**
         * Regenerates exactly the allocated 2D target mip chain with the FNA/XNA level extents.
         *
         * SDL3's Vulkan `GenerateMipmaps` path does not clamp an axis after right-shifting it,
         * unlike SDL3's D3D12 path. Calling SDL's public blit operation for each declared level
         * keeps the correction inside CNA's SDL_GPU integration while retaining GPU generation,
         * the existing command buffer, and the existing post-resolve ordering.
         */
        void GenerateRenderTargetMipChain(SDL_GPUCommandBuffer* commandBuffer,
                                          SDL_GPUTexture* texture,
                                          int width, int height, int levelCount)
        {
            for (int level = 1; level < levelCount; ++level)
            {
                SDL_GPUBlitInfo blit{};
                blit.source.texture = texture;
                blit.source.mip_level = static_cast<Uint32>(level - 1);
                blit.source.w = static_cast<Uint32>(std::max(1, width >> (level - 1)));
                blit.source.h = static_cast<Uint32>(std::max(1, height >> (level - 1)));

                blit.destination.texture = texture;
                blit.destination.mip_level = static_cast<Uint32>(level);
                blit.destination.w = static_cast<Uint32>(std::max(1, width >> level));
                blit.destination.h = static_cast<Uint32>(std::max(1, height >> level));

                blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
                blit.filter = SDL_GPU_FILTER_LINEAR;

                if (TargetMipTraceEnabled())
                {
                    std::fprintf(stderr,
                                 "[cna-sdlgpu-target-mips] texture=%p base=%dx%d levels=%d "
                                 "source=%u:%ux%u destination=%u:%ux%u\n",
                                 static_cast<void*>(texture), width, height, levelCount,
                                 static_cast<unsigned>(blit.source.mip_level), blit.source.w,
                                 blit.source.h,
                                 static_cast<unsigned>(blit.destination.mip_level),
                                 blit.destination.w, blit.destination.h);
                    std::fflush(stderr);
                }

                SDL_BlitGPUTexture(commandBuffer, &blit);
            }
        }

        /**
         * REMED-GFX-188: regenerates one resolved cube face, and only that face, after its pass.
         *
         * SDL's whole-resource mip generator cannot express this ordering. Explicit GPU blits do:
         * every source and destination names the rendered cube layer, while clamped dimensions
         * retain the GFX-187 narrow/NPOT contract. The separate multisample attachment never
         * participates and therefore remains level-zero only.
         */
        void GenerateCubeRenderTargetMipChain(SDL_GPUCommandBuffer* commandBuffer,
                                              SDL_GPUTexture* texture,
                                              int size, int levelCount, int face)
        {
            for (int level = 1; level < levelCount; ++level)
            {
                SDL_GPUBlitInfo blit{};
                blit.source.texture = texture;
                blit.source.mip_level = static_cast<Uint32>(level - 1);
                blit.source.layer_or_depth_plane = static_cast<Uint32>(face);
                blit.source.w = static_cast<Uint32>(std::max(1, size >> (level - 1)));
                blit.source.h = static_cast<Uint32>(std::max(1, size >> (level - 1)));

                blit.destination.texture = texture;
                blit.destination.mip_level = static_cast<Uint32>(level);
                blit.destination.layer_or_depth_plane = static_cast<Uint32>(face);
                blit.destination.w = static_cast<Uint32>(std::max(1, size >> level));
                blit.destination.h = static_cast<Uint32>(std::max(1, size >> level));

                blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
                blit.filter = SDL_GPU_FILTER_LINEAR;

                if (TargetMipTraceEnabled())
                {
                    std::fprintf(stderr,
                                 "[cna-sdlgpu-target-mips] cube=%p face=%d base=%dx%d levels=%d "
                                 "source=%u:%u:%ux%u destination=%u:%u:%ux%u\n",
                                 static_cast<void*>(texture), face, size, size, levelCount,
                                 static_cast<unsigned>(blit.source.layer_or_depth_plane),
                                 static_cast<unsigned>(blit.source.mip_level), blit.source.w,
                                 blit.source.h,
                                 static_cast<unsigned>(blit.destination.layer_or_depth_plane),
                                 static_cast<unsigned>(blit.destination.mip_level),
                                 blit.destination.w, blit.destination.h);
                    std::fflush(stderr);
                }

                SDL_BlitGPUTexture(commandBuffer, &blit);
            }
        }

        // REMED-GFX-170: XNA's SamplerState.MaxAnisotropy default. ISpriteBatchRenderer carries the
        // filter ordinal and the two address modes and nothing else, so a sprite cannot express a
        // non-default anisotropy on ANY renderer.
        constexpr int kSpriteBatchMaxAnisotropy = 4;

        // Mirrors VulkanRenderer::ToVkCompareOp's exact XNA CompareFunction ordinal table:
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
        // VulkanRenderer::ToVkStencilOp/EasyGL's ToEasyGLStencilOp exactly): Keep=0, Zero=1,
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

        // SDLGPU-18: XNA Blend ordinals -> SDL_GPUBlendFactor (mirrors VulkanRenderer::
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

        // SDLGPU-20: XNA CullMode ordinals -> SDL_GPUCullMode. Every pipeline in this renderer uses
        // front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE (matches this project's own EasyGL
        // renderer's real, hardware-validated "OpenGL default front face is CCW; CW faces are back
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
        // per field (which VulkanRenderer's own PackBlendBits/PackDepthStencilBits comments
        // note it outgrew once every dimension was added -- this sidesteps that entirely).
        [[nodiscard]] std::size_t HashCombine(std::size_t seed, std::size_t value)
        {
            return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
        }

        struct PipelineDepthBias
        {
            bool enabled = false;
            float constantFactor = 0.0f;
            float slopeFactor = 0.0f;
        };

        [[nodiscard]] bool IsPolygonTopology(SDL_GPUPrimitiveType topology)
        {
            return topology == SDL_GPU_PRIMITIVETYPE_TRIANGLELIST
                || topology == SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
        }

        // XNA/FNA depth bias is polygon offset: independent line/point primitives have no
        // polygon slope and do not receive the D3D/OpenGL/Vulkan rasterizer bias. Normalize them
        // to disabled so irrelevant public values neither alter native state nor fragment caches.
        // For triangles, signed zero is canonicalized to +0 and the explicit SDL enable bit is
        // true iff either public factor is nonzero. NaN/Inf remain unmodified rather than being
        // silently approximated; SDL/its native driver remains the capability/validation authority.
        [[nodiscard]] PipelineDepthBias NormalizeDepthBias(
            SDL_GPUPrimitiveType topology, float depthBias, float slopeScaleDepthBias)
        {
            if (!IsPolygonTopology(topology))
                return {};

            PipelineDepthBias result;
            result.constantFactor = depthBias == 0.0f ? 0.0f : depthBias;
            result.slopeFactor =
                slopeScaleDepthBias == 0.0f ? 0.0f : slopeScaleDepthBias;
            result.enabled =
                result.constantFactor != 0.0f || result.slopeFactor != 0.0f;
            return result;
        }

        [[nodiscard]] std::size_t HashDepthBias(
            std::size_t key, SDL_GPUPrimitiveType topology,
            float depthBias, float slopeScaleDepthBias)
        {
            const PipelineDepthBias bias =
                NormalizeDepthBias(topology, depthBias, slopeScaleDepthBias);
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

        void FillDepthBiasState(
            SDL_GPURasterizerState& out, SDL_GPUPrimitiveType topology,
            float depthBias, float slopeScaleDepthBias)
        {
            const PipelineDepthBias bias =
                NormalizeDepthBias(topology, depthBias, slopeScaleDepthBias);
            out.enable_depth_bias = bias.enabled;
            if (bias.enabled)
            {
                // SDL_GPU names these native polygon-offset factors, not normalized depth
                // offsets. CNA/XNA DepthBias is already expressed as the constant factor in
                // minimum-resolvable-depth (r) units, while SlopeScaleDepthBias is already the
                // slope factor, so both map directly. SDL/the native driver applies r and m.
                out.depth_bias_constant_factor = bias.constantFactor;
                out.depth_bias_clamp = 0.0f;
                out.depth_bias_slope_factor = bias.slopeFactor;
            }
        }

        // Packs (topology, depthTest, depthWrite, depthFunc, colorFormat, sampleCount,
        // depthStencilFormat, full RenderStateSnapshot) into one cache key. INVALID means the
        // active pass has no depth/stencil attachment. Disabled dimensions (blend off, stencil off,
        // two-sided off) always collapse their own sub-fields out of the hash regardless of what
        // they're set to, so different "irrelevant" values don't create duplicate pipelines --
        // mirrors VulkanRenderer::PackBlendBits's identical "collapse to 0 when disabled" rule.
        // sampleCount (SDLGPU-38's MSAA fix) is a REQUIRED dimension, not an optional one to
        // collapse -- a pipeline created with the wrong sample_count for its render pass's actual
        // attachments is exactly finding #1 of the adversarial review this fixes.
        [[nodiscard]] std::size_t PipelineCacheKey(SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
                                                    SDL_GPUTextureFormat colorFormat, int colorTargetCount,
                                                    SDL_GPUSampleCount sampleCount,
                                                    SDL_GPUTextureFormat depthStencilFormat,
                                                    const SdlGpuRenderer::RenderStateSnapshot& rs)
        {
            std::size_t key = static_cast<std::size_t>(topology);
            key = HashCombine(key, depthTest ? 1u : 0u);
            key = HashCombine(key, depthWrite ? 1u : 0u);
            key = HashCombine(key, static_cast<std::size_t>(depthFunc));
            // SDL_GPU/Vulkan render-pass compatibility is slot-aligned. CNA presently exposes
            // one SDL_GPU RT colour format, but count is still a distinct compatibility axis and
            // each active slot is deliberately represented in this key.
            key = HashCombine(key, static_cast<std::size_t>(colorTargetCount));
            for (int i = 0; i < colorTargetCount; ++i)
                key = HashCombine(key, static_cast<std::size_t>(colorFormat));
            key = HashCombine(key, static_cast<std::size_t>(sampleCount));
            // REMED-GFX-097: render-pass compatibility depends on attachment presence, format,
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
            // REMED-GFX-077: the colour write mask is static pipeline state (applies to opaque
            // draws too), so it participates in the key. The default All(15) is a fixed contribution
            // ⇒ no cache fragmentation for the common case.
            for (int i = 0; i < colorTargetCount; ++i)
                key = HashCombine(key, static_cast<std::size_t>(rs.colorWriteMasks[i] & 0xF));
            key = HashCombine(key, static_cast<std::size_t>(rs.cullMode));
            key = HashCombine(key, rs.wireframe ? 1u : 0u);
            key = HashDepthBias(
                key, topology, rs.depthBias, rs.slopeScaleDepthBias);
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
        // everywhere (mirrors VulkanRenderer::FillBlendAttachmentState's identical role).
        void FillBlendState(SDL_GPUColorTargetBlendState& out, const SdlGpuRenderer::RenderStateSnapshot& rs,
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
            // REMED-GFX-077: BlendState.ColorWriteChannels. XNA bits (R=1,G=2,B=4,A=8) are identical
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

        void FillColorTargetDescriptions(std::array<SDL_GPUColorTargetDescription, 4>& out,
                                         int colorTargetCount, SDL_GPUTextureFormat colorFormat,
                                         const SdlGpuRenderer::RenderStateSnapshot& rs)
        {
            for (int i = 0; i < colorTargetCount; ++i)
            {
                out[i].format = colorFormat;
                FillBlendState(out[i].blend_state, rs, i);
            }
        }

        // Fills a pipeline's real front/back stencil-op state (SDLGPU-19) from a
        // RenderStateSnapshot, on top of the depthTest/depthWrite/depthFunc every pipeline already
        // threads through separately. XNA's TwoSidedStencilMode=false uses the SAME (front,
        // clockwise) ops/func for both faces (matches this project's own EasyGL/Vulkan renderers'
        // identical fallback-to-front convention -- FNA's own real behavior: the CCW fields are
        // simply ignored when this is false, not reset to any default).
        void FillDepthStencilState(SDL_GPUDepthStencilState& out, bool depthTest, bool depthWrite, int depthFunc,
                                    const SdlGpuRenderer::RenderStateSnapshot& rs)
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
        // renderer (see ToCullMode's own doc comment).
        void FillRasterizerState(
            SDL_GPURasterizerState& out,
            const SdlGpuRenderer::RenderStateSnapshot& rs,
            SDL_GPUPrimitiveType topology)
        {
            out.fill_mode = rs.wireframe ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
            out.cull_mode = ToCullMode(rs.cullMode);
            out.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
            FillDepthBiasState(
                out, topology, rs.depthBias, rs.slopeScaleDepthBias);
        }

        // Mirrors VulkanRenderer's CalculateVulkanRTMipLevels / Texture2D.cpp's
        // CalculateMipLevels -- each renderer keeps its own copy of this small helper.
        [[nodiscard]] int CalculateMipLevels(int w, int h)
        {
            int levels = 1;
            while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++levels; }
            return levels;
        }

        // SDLGPU-36: clamps an XNA multiSampleCount request down to the largest SDL_gpu sample
        // count this device/format actually supports, mirroring D3D12RenderTargetCubeRenderer's own
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

        // Mirrors VulkanRenderer::FillExtPushConst()'s 128-byte layout byte-for-byte
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

        // Mirrors VulkanRenderer::FillExtPushConst()/WebGPURenderer::FillExtUniforms()
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
        // VulkanRenderer's own FogParams shape -- vec4 fogColorEnabled (xyz = FogColor, w =
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
        // unlike WebGPURenderer's WGSL-forced CPU-side precomputation -- no normal-matrix
        // slots needed here), EyePosition, per-light SpecularColor, material SpecularColor/Power.
        // Mirrors VulkanRenderer's LitLightParams UBO field-for-field. Fog is not carried
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

        // pbr3d.frag.glsl's tertiary PbrParams block: material/map factors, alpha coverage,
        // transfer flags, specular Fresnel inputs, and fourteen affine texture-transform rows.
        void FillPbrParams(std::array<float, 72>& out, const GpuDrawParams& p)
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

        // Mirrors VulkanRenderer::FillAlphaTestPushConst()/WebGPURenderer::
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
        // Mirrors VulkanRenderer::env_map3d's EnvMapParams field-for-field. Fog is not
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
        // VulkanRenderer's own skinned3d.vert.glsl packing convention). This identical
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

        // ---- Runtime GLSL->SPIR-V compile for SdlGpuEffectRenderer (SDLGPU-42/43) ----
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

    struct SdlGpuRenderer::ConstructionResources
    {
        SDL_Window* window = nullptr;  // Borrowed from the caller; never destroyed here.
        SDL_GPUDevice* device = nullptr;
        bool windowClaimed = false;
        bool rendererRegistered = false;
        bool failureInjected = false;
        bool debugModeEnabled = false;
        int swapInterval = 1;
        int physicalWidth = 0;
        int physicalHeight = 0;
        SDL_GPUTextureFormat depthStencilFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
        SdlGpuTestHooksEXT hooks{};
        std::array<SDL_GPUShader*, static_cast<std::size_t>(ConstructionShader::Count)> shaders{};

        ConstructionResources(SDL_Window* constructionWindow, const SdlGpuTestHooksEXT& testHooks)
            : window(constructionWindow), hooks(testHooks)
        {
        }

        ConstructionResources(const ConstructionResources&) = delete;
        ConstructionResources& operator=(const ConstructionResources&) = delete;

        ~ConstructionResources()
        {
            if (rendererRegistered)
                IGraphicsRenderer::UnregisterForWindow(SDL_GetWindowID(window));

            for (auto it = shaders.rbegin(); it != shaders.rend(); ++it)
            {
                if (*it != nullptr)
                {
                    SDL_ReleaseGPUShader(device, *it);
                    NotifyResource(hooks, SdlGpuResourceKindEXT::Shader,
                                   SdlGpuResourceEventEXT::Released);
                }
            }

            if (windowClaimed)
            {
                SDL_ReleaseWindowFromGPUDevice(device, window);
                NotifyResource(hooks, SdlGpuResourceKindEXT::WindowClaim,
                               SdlGpuResourceEventEXT::Released);
            }
            if (device != nullptr)
            {
                SDL_DestroyGPUDevice(device);
                NotifyResource(hooks, SdlGpuResourceKindEXT::Device,
                               SdlGpuResourceEventEXT::Released);
            }
        }

        void FailAt(SdlGpuFailurePointEXT point)
        {
            InjectFailure(hooks, failureInjected, point);
        }

        SDL_GPUShader* CreateShader(ConstructionShader slot, SdlGpuFailurePointEXT failurePoint,
                                    const SDL_GPUShaderCreateInfo& createInfo,
                                    const char* diagnostic)
        {
            FailAt(failurePoint);
            SDL_GPUShader* shader = SDL_CreateGPUShader(device, &createInfo);
            if (shader == nullptr)
                throw std::runtime_error(std::string(diagnostic) + SDL_GetError());
            shaders[static_cast<std::size_t>(slot)] = shader;
            NotifyResource(hooks, SdlGpuResourceKindEXT::Shader,
                           SdlGpuResourceEventEXT::Acquired);
            return shader;
        }

        void CommitTo(SdlGpuRenderer& owner) noexcept
        {
            owner.device_ = device;
            owner.debugModeEnabled_ = debugModeEnabled;
            owner.swapInterval_ = swapInterval;
            owner.physicalWidth_ = physicalWidth;
            owner.physicalHeight_ = physicalHeight;
            owner.depthStencilFormat_ = depthStencilFormat;
            owner.testHooks_ = hooks;
            owner.testFailureInjected_ = failureInjected;
            owner.registeredForWindow_ = rendererRegistered;

            owner.spriteVertexShader_ = shaders[static_cast<std::size_t>(ConstructionShader::SpriteVertex)];
            owner.spriteFragmentShader_ = shaders[static_cast<std::size_t>(ConstructionShader::SpriteFragment)];
            owner.coloredVertexShader_ = shaders[static_cast<std::size_t>(ConstructionShader::ColoredVertex)];
            owner.coloredFragmentShader_ = shaders[static_cast<std::size_t>(ConstructionShader::ColoredFragment)];
            owner.texturedVertexShader_ = shaders[static_cast<std::size_t>(ConstructionShader::TexturedVertex)];
            owner.coloredTexturedVertexShader_ =
                shaders[static_cast<std::size_t>(ConstructionShader::ColoredTexturedVertex)];
            owner.texturedFragmentShader_ = shaders[static_cast<std::size_t>(ConstructionShader::TexturedFragment)];
            owner.litTexturedVertexShader_ =
                shaders[static_cast<std::size_t>(ConstructionShader::LitTexturedVertex)];
            owner.litTexturedFragmentShader_ =
                shaders[static_cast<std::size_t>(ConstructionShader::LitTexturedFragment)];
            owner.alphaTestVertexShader_ = shaders[static_cast<std::size_t>(ConstructionShader::AlphaTestVertex)];
            owner.alphaTestColoredVertexShader_ =
                shaders[static_cast<std::size_t>(ConstructionShader::AlphaTestColoredVertex)];
            owner.alphaTestFragmentShader_ =
                shaders[static_cast<std::size_t>(ConstructionShader::AlphaTestFragment)];
            owner.dualTextureVertexShader_ =
                shaders[static_cast<std::size_t>(ConstructionShader::DualTextureVertex)];
            owner.dualTextureColoredVertexShader_ =
                shaders[static_cast<std::size_t>(ConstructionShader::DualTextureColoredVertex)];
            owner.dualTextureFragmentShader_ =
                shaders[static_cast<std::size_t>(ConstructionShader::DualTextureFragment)];
            owner.envMapVertexShader_ = shaders[static_cast<std::size_t>(ConstructionShader::EnvMapVertex)];
            owner.envMapFragmentShader_ = shaders[static_cast<std::size_t>(ConstructionShader::EnvMapFragment)];
            owner.skinnedVertexShader_ = shaders[static_cast<std::size_t>(ConstructionShader::SkinnedVertex)];
            owner.skinnedColoredVertexShader_ =
                shaders[static_cast<std::size_t>(ConstructionShader::SkinnedColoredVertex)];
            owner.skinnedColoredFragmentShader_ =
                shaders[static_cast<std::size_t>(ConstructionShader::SkinnedColoredFragment)];
            owner.pbrVertexShader_ = shaders[static_cast<std::size_t>(ConstructionShader::PbrVertex)];
            owner.pbrSkinnedVertexShader_ =
                shaders[static_cast<std::size_t>(ConstructionShader::PbrSkinnedVertex)];
            owner.pbrFragmentShader_ = shaders[static_cast<std::size_t>(ConstructionShader::PbrFragment)];

            shaders.fill(nullptr);
            device = nullptr;
            windowClaimed = false;
            rendererRegistered = false;
        }
    };

    SdlGpuRenderer::SdlGpuRenderer(SDL_Window* window, int virtualWidth,
                                                  int virtualHeight,
                                                  CnaPresentationMode presentationMode,
                                                  int swapInterval)
        : SdlGpuRenderer(window, virtualWidth, virtualHeight, presentationMode,
                                swapInterval, SdlGpuTestHooksEXT{})
    {
    }

    SdlGpuRenderer::SdlGpuRenderer(SDL_Window* window, int virtualWidth,
                                                  int virtualHeight,
                                                  CnaPresentationMode presentationMode,
                                                  int swapInterval,
                                                  const SdlGpuTestHooksEXT& testHooks)
        : window_(window),
          virtualWidth_(virtualWidth),
          virtualHeight_(virtualHeight),
          presentationMode_(presentationMode)
    {
        if (window_ == nullptr)
            throw std::invalid_argument("CNA SDL_GPU: SDL window cannot be null");

        ConstructionResources resources(window_, testHooks);

        // plan_sdlgpu.md SDLGPU-6: request SPIR-V first -- the only shader format this device's
        // vendored SDL3 compiles a driver for on Linux (Vulkan). DXBC/DXIL/MSL support (Windows/
        // macOS drivers) is deferred to plan_sdlgpu.md's Phase SDLGPU-13.
        // debug_mode mirrors DirectX11Renderer::CreateDeviceResources()'s own #ifndef NDEBUG
        // CNA-side toggle (design decision 12: the validation/debug layer is a debug-build
        // convenience, never a hard requirement) -- a debug build asks the Vulkan driver for
        // SDL_gpu's own validation layer, a release build does not.
#ifndef NDEBUG
        resources.debugModeEnabled = true;
#else
        resources.debugModeEnabled = false;
#endif
        resources.FailAt(SdlGpuFailurePointEXT::DeviceCreation);
        resources.device = SDL_CreateGPUDevice(
            SDL_GPU_SHADERFORMAT_SPIRV, resources.debugModeEnabled, /*name=*/nullptr);
        if (resources.device == nullptr)
            throw std::runtime_error(
                std::string("CNA SDL_GPU: SDL_CreateGPUDevice failed: ") + SDL_GetError());
        NotifyResource(testHooks, SdlGpuResourceKindEXT::Device,
                       SdlGpuResourceEventEXT::Acquired);

        resources.FailAt(SdlGpuFailurePointEXT::WindowClaim);
        if (!SDL_ClaimWindowForGPUDevice(resources.device, window_))
        {
            const std::string error = SDL_GetError();
            throw std::runtime_error(
                "CNA SDL_GPU: SDL_ClaimWindowForGPUDevice failed: " + error);
        }
        resources.windowClaimed = true;
        NotifyResource(testHooks, SdlGpuResourceKindEXT::WindowClaim,
                       SdlGpuResourceEventEXT::Acquired);

        resources.FailAt(SdlGpuFailurePointEXT::SwapchainSetup);
        resources.swapInterval = std::max(0, swapInterval);
        ConfigureSwapchain(resources.device, window_, resources.swapInterval);

        resources.FailAt(SdlGpuFailurePointEXT::DepthStencilFormatQuery);
        resources.depthStencilFormat = QueryDepthStencilFormat(resources.device);

        CreateSpriteResources(resources);
        CreateColoredResources(resources);
        CreateTexturedResources(resources);
        CreateLitTexturedResources(resources);
        CreateAlphaTestResources(resources);
        CreateDualTextureResources(resources);
        CreateEnvMapResources(resources);
        CreateSkinnedResources(resources);
        CreatePbrResources(resources);

        resources.FailAt(SdlGpuFailurePointEXT::WindowMetricsInitialization);
        if (!SDL_GetWindowSizeInPixels(
                window_, &resources.physicalWidth, &resources.physicalHeight))
        {
            const std::string error = SDL_GetError();
            throw std::runtime_error(
                "CNA SDL_GPU: SDL_GetWindowSizeInPixels failed: " + error);
        }

        resources.FailAt(SdlGpuFailurePointEXT::RendererRegistration);
        IGraphicsRenderer::RegisterForWindow(SDL_GetWindowID(window_), this);
        resources.rendererRegistered = true;
        resources.FailAt(SdlGpuFailurePointEXT::AfterRendererRegistration);

        // Every operation above may throw. Raw member handles become owning only here, after all
        // fallible initialization and registration have succeeded.
        resources.CommitTo(*this);

        // REMED-GFX-143: the backbuffer is the initially selected target and owns a real segment,
        // so open its first bind cycle here. Without it every draw issued before the first
        // SetRenderTarget would carry the kSwapchainSegment sentinel that no segment answers to and
        // would never be recorded at all.
        BeginBackbufferSegment();

        SDL_Log("[SDL_GPU] Renderer initialised (%dx%d), debug mode %s",
                physicalWidth_, physicalHeight_, debugModeEnabled_ ? "enabled" : "disabled");
    }

    SdlGpuRenderer::~SdlGpuRenderer()
    {
        if (registeredForWindow_)
        {
            IGraphicsRenderer::UnregisterForWindow(SDL_GetWindowID(window_));
            registeredForWindow_ = false;
        }
#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
        // plan_fx.md FX-061: released before the device it was created against.
        if (mojoShaderContext_ != nullptr)
        {
            MOJOSHADER_sdlDestroyContext(mojoShaderContext_);
            mojoShaderContext_ = nullptr;
        }
#endif
        // Drops every queued command, and with it every SdlGpuSampledTextureEXT::keepAlive a
        // command still holds (REMED-GFX-152). This must happen HERE, while the renderer and its
        // device are both fully alive: a resource whose last reference was one of those commands
        // releases through QueueTextureRelease, and the drain of pendingTextureReleases_ a few
        // lines below is what actually frees it. ReleaseSceneDrawBuffers() clears the 3D command
        // vectors; sprites and pending segments hold references of their own.
        ReleaseSceneDrawBuffers();
        spriteCommands_.clear();
        passSegments_.clear();
        drawOrder_.clear();
        DestroyPbrResources();
        DestroySkinnedResources();
        DestroyEnvMapResources();
        DestroyDualTextureResources();
        DestroyAlphaTestResources();
        DestroyLitTexturedResources();
        DestroyTexturedResources();
        DestroyColoredResources();
        DestroySpriteResources();
#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
        // plan_fx.md FX-071: unlike every stock family's own Destroy*Resources, there are no fixed
        // shader fields here to release -- every shader module compiled-effect pipelines reference
        // is owned by mojoShaderContext_, already destroyed above.
        for (auto& [key, pipeline] : compiledEffectPipelines_)
            ReleaseGraphicsPipeline(pipeline);
        compiledEffectPipelines_.clear();
#endif
        // Any render target destroyed earlier but never followed by another real frame (e.g. the
        // game shut down right after) leaves its GPU texture handles deferred -- release them now
        // rather than relying solely on SDL_DestroyGPUDevice's own implicit cleanup below.
        for (SDL_GPUTexture* texture : pendingTextureReleases_)
            SDL_ReleaseGPUTexture(device_, texture);
        pendingTextureReleases_.clear();
        if (depthStencilTexture_ != nullptr)
            SDL_ReleaseGPUTexture(device_, depthStencilTexture_);
        if (backbufferProxy_ != nullptr)
            SDL_ReleaseGPUTexture(device_, backbufferProxy_);   // REMED-GFX-165 readback proxy
        if (device_ != nullptr)
        {
            SDL_ReleaseWindowFromGPUDevice(device_, window_);
            NotifyResourceEvent(SdlGpuResourceKindEXT::WindowClaim,
                                SdlGpuResourceEventEXT::Released);
            SDL_DestroyGPUDevice(device_);
            NotifyResourceEvent(SdlGpuResourceKindEXT::Device,
                                SdlGpuResourceEventEXT::Released);
            device_ = nullptr;
        }
    }

    SDL_GPUTextureFormat SdlGpuRenderer::QueryDepthStencilFormat(SDL_GPUDevice* device)
    {
        // plan_sdlgpu.md: SDL_gpu guarantees at most one of D24_UNORM_S8_UINT/D32_FLOAT_S8_UINT
        // per device -- must query, never assume either is available. Queried once here (not
        // lazily inside EnsureDepthStencilTexture) so pipeline creation has a stable answer for
        // SDL_GPUGraphicsPipelineTargetInfo before any frame has actually rendered.
        if (SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
                                          SDL_GPU_TEXTURETYPE_2D,
                                          SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
        {
            return SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
        }
        if (SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
                                               SDL_GPU_TEXTURETYPE_2D,
                                               SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
        {
            return SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
        }

        // Genuine SDL_gpu/device capability gap, not a "not implemented yet" stub -- warn
        // and keep running with no depth/stencil attachment rather than throw.
        CNA::Logger::Warn(
            "CNA SDL_GPU: no combined depth+stencil texture format is supported by this "
            "device; depth/stencil clearing and depth-tested draws will have no effect.",
            CNA::LogCategory::GPU);
        return SDL_GPU_TEXTUREFORMAT_INVALID;
    }

    void SdlGpuRenderer::QueueTextureRelease(SDL_GPUTexture* texture)
    {
        if (texture != nullptr)
            pendingTextureReleases_.push_back(texture);
    }

    void SdlGpuRenderer::EnsureDepthStencilTexture(Uint32 width, Uint32 height)
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

    bool SdlGpuRenderer::EnsureFrameRendered()
    {
        if (!framePending_)
            return true;

        MaybeFailForTest(SdlGpuFailurePointEXT::FrameCommandBufferAcquisition);
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
        if (cmd == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: SDL_AcquireGPUCommandBuffer failed: ") + SDL_GetError());
        FrameCommandBufferOwner commandBuffer(cmd, testHooks_);

        try
        {
            SDL_GPUTexture* swapchainTexture = nullptr;
            Uint32 swapchainWidth = 0;
            Uint32 swapchainHeight = 0;
            // Per SDL_gpu.h, a command buffer that has attempted swapchain acquisition must be
            // submitted rather than cancelled, including every exceptional exit below.
            commandBuffer.SwapchainAcquisitionStarted();
            const bool acquired = SDL_WaitAndAcquireGPUSwapchainTexture(
                cmd, window_, &swapchainTexture, &swapchainWidth, &swapchainHeight);
            if (!acquired)
            {
                const std::string error = SDL_GetError();
                (void)commandBuffer.Submit();
                throw std::runtime_error(
                    "CNA SDL_GPU: SDL_WaitAndAcquireGPUSwapchainTexture failed: " + error);
            }

            if (swapchainTexture == nullptr)
            {
                // Documented, non-error case (e.g. a minimized window) -- still must submit.
                (void)commandBuffer.Submit();
                return false;
            }

        physicalWidth_ = static_cast<int>(swapchainWidth);
        physicalHeight_ = static_cast<int>(swapchainHeight);
        EnsureDepthStencilTexture(swapchainWidth, swapchainHeight);

        // REMED-GFX-165: once the backbuffer has been read at least once, render every backbuffer pass
        // into a self-owned readable proxy instead of straight into the write-only swapchain texture,
        // and blit the proxy to the swapchain for present. The proxy is the resource ReadBackbuffer
        // downloads from. Zero cost until the first read (backbufferReadbackEnabled_ stays false).
        SDL_GPUTexture* backbufferColorTarget = swapchainTexture;
        bool renderedThroughProxy = false;
        if (backbufferReadbackEnabled_ && EnsureBackbufferProxy(swapchainWidth, swapchainHeight))
        {
            backbufferColorTarget = backbufferProxy_;
            renderedThroughProxy = true;
        }

        // Sprite/3D vertex data must be uploaded via a copy pass BEFORE BeginGPURenderPass --
        // SDL_gpu forbids a copy pass nested inside a render pass. Covers every target's draws
        // regardless of which render pass below will consume them.
        UploadSpriteVertexData(cmd);
        UploadSceneDrawData(cmd);

        // Phase SDLGPU-8 / REMED-GFX-145: every off-screen render-pass SEGMENT (one public bind
        // cycle of a RenderTarget2D or of one RenderTargetCube face) gets its own native pass
        // FIRST, in the exact order the cycles were opened, so a target bound-then-unbound earlier
        // in the frame can safely be sampled by a later swapchain-targeted draw within the same
        // frame (SDLGPU-35's own "bound in one pass, sampled in a later pass" contract) AND two
        // cycles of ONE target stay two passes with two load actions. An MRT extra is an
        // attachment of the segment its primary bind opened, never a segment of its own
        // (SDLGPU-37: real MRT).
        {
            // One walk over drawOrder_ answers "does this segment draw anything" for every
            // segment at once; a segment that neither draws, clears, nor still owes a resource its
            // first-use initialization needs no native pass at all.
            std::vector<std::uint64_t> segmentsWithDraws;
            segmentsWithDraws.reserve(drawOrder_.size());
            for (const QueuedDrawRef& ref : drawOrder_)
                segmentsWithDraws.push_back(ref.segment);
            std::sort(segmentsWithDraws.begin(), segmentsWithDraws.end());
            segmentsWithDraws.erase(std::unique(segmentsWithDraws.begin(), segmentsWithDraws.end()),
                                    segmentsWithDraws.end());

            // REMED-GFX-143: the acquired swapchain texture must be written and left presentable
            // every rendered frame even when the game drew nothing to it, which the unconditional
            // trailing pass gave for free. The LAST backbuffer segment always gets its pass, so a
            // frame with no backbuffer work records exactly the one pass it always did; empty
            // EARLIER backbuffer segments issue none.
            nativePassIndex_ = 0;   // REMED-GFX-156: numbering for the native order trace.
            std::size_t lastBackbuffer = passSegments_.size();
            for (std::size_t i = 0; i < passSegments_.size(); ++i)
                if (passSegments_[i].rt == nullptr && passSegments_[i].cube == nullptr)
                    lastBackbuffer = i;
            bool firstBackbufferPass = true;

            for (std::size_t i = 0; i < passSegments_.size(); ++i)
            {
                const PassSegment& segment = passSegments_[i];
                const bool isBackbuffer = (segment.rt == nullptr && segment.cube == nullptr);
                const bool draws = std::binary_search(segmentsWithDraws.begin(),
                                                      segmentsWithDraws.end(), segment.id);
                const bool clears = segment.clearColorRequested || segment.clearDepthRequested ||
                                    segment.clearStencilRequested;
                if (isBackbuffer)
                {
                    if (!draws && !clears && i != lastBackbuffer)
                        continue;
                    RenderToSwapchain(cmd, segment, backbufferColorTarget, firstBackbufferPass);
                    firstBackbufferPass = false;
                    continue;
                }
                if (!draws && !clears && !SegmentOwesFirstUseClear(segment))
                    continue;
                // REMED-GFX-156: a segment whose colour resource a later segment loads again must
                // STORE its multisample contents, not merely resolve them.
                const bool colorLoadedLater = SegmentColorLoadedLater(i);
                if (segment.rt != nullptr)
                    RenderToTarget(cmd, segment, colorLoadedLater);
                else
                    RenderToTargetCubeFace(cmd, segment, colorLoadedLater);
            }
        }

        // REMED-GFX-165: the frame's backbuffer content is in the proxy -- present it by copying the
        // proxy into the acquired (write-only) swapchain texture. A 1:1 same-size, same-format blit,
        // so it is a straight copy with no scaling. Must be outside any render/copy pass and before
        // submit, exactly like the mip regen above.
        if (renderedThroughProxy)
            BlitBackbufferProxyToSwapchain(cmd, swapchainTexture);

            if (!commandBuffer.Submit())
                throw std::runtime_error(
                    std::string("CNA SDL_GPU: SDL_SubmitGPUCommandBuffer failed: ") +
                    SDL_GetError());

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
        // REMED-GFX-145: the per-resource FIRST-USE clear flags are consumed by whichever segment
        // actually recorded that resource (RenderToTarget/RenderToTargetCubeFace do it), not here
        // -- a skipped empty segment must leave a still-uninitialized texture owing its clear.
        passSegments_.clear();
        // A flush can happen while a target is still bound (a GetData or Present issued without
        // unbinding first). The segment list is gone, so re-open one for whatever is current;
        // otherwise every later draw of this cycle would carry an id no segment answers to and
        // would never be recorded at all.
        ReopenSegmentForBoundTarget();
            framePending_ = false;
            return true;
        }
        catch (...)
        {
            // Finish the real SDL command resource before releasing transient buffers. The RAII
            // render/copy-pass owners below guarantee no pass remains open. Keep queued commands
            // intact so the same frame can be retried after the one-shot injected/native error.
            commandBuffer.FinishForFailure();
            ReleaseSceneDrawBuffers(false);
            throw;
        }
    }

    namespace
    {
        // Shared by RenderToTarget's primary + each SDLGPU-37 MRT extra attachment -- fills one
        // SDL_GPUColorTargetInfo entry.
        //
        // REMED-GFX-145: the load action belongs to the SEGMENT, not to the resource. An explicit
        // Clear() inside this bind cycle wins; otherwise the resource's own first-use flag decides
        // whether this is the pass that must initialize it; otherwise LOAD, which is what makes a
        // second cycle genuinely reload what the first cycle stored.
        void FillColorTargetInfo(SDL_GPUColorTargetInfo& out, const SdlGpuRenderTarget2DState& state,
                                 bool segmentClearsColor, SDL_FColor segmentClearColor,
                                 bool colorLoadedLater)
        {
            out.texture = state.msaaTexture != nullptr ? state.msaaTexture : state.colorTexture;
            out.clear_color = segmentClearsColor ? segmentClearColor : state.clearColor;
            out.load_op = (segmentClearsColor || state.clearColorPending) ? SDL_GPU_LOADOP_CLEAR
                                                                         : SDL_GPU_LOADOP_LOAD;
            if (state.msaaTexture != nullptr)
            {
                out.resolve_texture = state.colorTexture;
                // REMED-GFX-156: plain RESOLVE explicitly leaves the multisample contents
                // undefined. That is fine for the LAST pass to touch this resource this frame, and
                // wrong for any earlier one, because a following pass -- which is exactly what an
                // ordered Clear() that does not name the colour attachment creates -- LOADs those
                // samples. Same correction REMED-GFX-141 made for preserving cube faces.
                out.store_op = colorLoadedLater ? SDL_GPU_STOREOP_RESOLVE_AND_STORE
                                                : SDL_GPU_STOREOP_RESOLVE;
            }
            else
            {
                out.store_op = SDL_GPU_STOREOP_STORE;
            }
        }
    }

    bool SdlGpuRenderer::SegmentOwesFirstUseClear(const PassSegment& segment)
    {
        if (segment.cube != nullptr)
        {
            const int face = segment.face;
            if (face < 0 || face >= 6) return false;
            return segment.cube->clearColorPending[face] || segment.cube->clearDepthPending[face] ||
                   segment.cube->clearStencilPending[face];
        }
        if (segment.rt == nullptr) return false;
        if (segment.rt->clearColorPending || segment.rt->clearDepthPending ||
            segment.rt->clearStencilPending)
            return true;
        for (const auto& extra : segment.extraAttachments)
            if (extra->clearColorPending) return true;
        return false;
    }

    // REMED-GFX-143: one backbuffer bind cycle, into the swapchain texture acquired ONCE for this
    // frame. This replaces the single trailing swapchain pass, which was passed kSwapchainSegment
    // and so replayed every backbuffer draw of the frame regardless of when it was issued. Load
    // actions are per cycle and per aspect: a cycle that issued its own Clear() clears that aspect,
    // anything else LOADs what the previous cycle stored, which is what makes a later cycle compose
    // with an earlier one instead of wiping or re-clearing it. The pre-fix rule -- clear only if a
    // Clear() was actually issued, otherwise load the acquired texture -- is preserved exactly, so a
    // frame with one backbuffer cycle is byte-for-byte unchanged. The frame-global
    // clearColor_/clearColorPending_ trio survives as the fallback for a Clear() issued while no
    // segment is open at all (the kSwapchainSegment sentinel state, reachable only before the first
    // cycle is opened) and is consumed by the frame's FIRST backbuffer pass, which is where a
    // pre-segment clear request logically belongs.
    void SdlGpuRenderer::RenderToSwapchain(SDL_GPUCommandBuffer* cmd, const PassSegment& segment,
                                                  SDL_GPUTexture* swapchainTexture,
                                                  bool isFirstBackbuffer)
    {
        const bool clearColor   = segment.clearColorRequested   || (isFirstBackbuffer && clearColorPending_);
        const bool clearDepth   = segment.clearDepthRequested   || (isFirstBackbuffer && clearDepthPending_);
        const bool clearStencil = segment.clearStencilRequested || (isFirstBackbuffer && clearStencilPending_);

        SDL_GPUColorTargetInfo colorTarget{};
        colorTarget.texture = swapchainTexture;
        colorTarget.clear_color = segment.clearColorRequested ? segment.clearColor : clearColor_;
        colorTarget.load_op = clearColor ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        colorTarget.store_op = SDL_GPU_STOREOP_STORE;

        SDL_GPUDepthStencilTargetInfo depthStencilTarget{};
        if (depthStencilTexture_ != nullptr)
        {
            depthStencilTarget.texture = depthStencilTexture_;
            depthStencilTarget.clear_depth = segment.clearDepthRequested ? segment.clearDepth : clearDepth_;
            depthStencilTarget.load_op = clearDepth ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            // STORE, always: a following backbuffer cycle LOADs the depth this one wrote, so a
            // depth test issued after an intervening render-target cycle still sees it.
            depthStencilTarget.store_op = SDL_GPU_STOREOP_STORE;
            depthStencilTarget.clear_stencil = segment.clearStencilRequested ? segment.clearStencil : clearStencil_;
            depthStencilTarget.stencil_load_op = clearStencil ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            depthStencilTarget.stencil_store_op = SDL_GPU_STOREOP_STORE;
        }

        const SDL_GPUTextureFormat swapchainFormat = SDL_GetGPUSwapchainTextureFormat(device_, window_);
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(
            cmd, &colorTarget, 1, depthStencilTexture_ != nullptr ? &depthStencilTarget : nullptr);
        RenderPassOwner passOwner(pass);
        TracePassSegment(nativePassIndex_++, segment,
                         clearColor ? "clear" : "load", "store",
                         depthStencilTexture_ == nullptr ? "none" : (clearDepth ? "clear" : "load"),
                         depthStencilTexture_ == nullptr ? "none" : (clearStencil ? "clear" : "load"),
                         std::count_if(drawOrder_.begin(), drawOrder_.end(),
                                       [&](const QueuedDrawRef& r) { return r.segment == segment.id; }));
        // REMED-GFX-068: the scissor (like the viewport, REMED-GFX-064) is applied PER DRAW inside
        // RenderQueuedDraws from each queued draw's own captured state, not once per pass here -- a
        // per-pass read of the live scissor would apply the post-unbind full-backbuffer rect (see
        // ApplyScissorForRef / QueuedDrawRef). SDL sets a default full-target scissor at pass begin,
        // which each draw's ApplyScissorForRef then overrides.
        // Real chronological draw order (adversarial-review finding #4) -- see drawOrder_'s own
        // doc comment; replaces the old fixed "all 3D families, then all sprites" sequence.
        const DrawTarget swapchainTarget{};
        // The swapchain surface is never MSAA in this renderer -- SDL_GPU_SAMPLECOUNT_1 always.
        RenderQueuedDraws(pass, cmd, swapchainTarget, swapchainFormat, SDL_GPU_SAMPLECOUNT_1,
                          depthStencilTexture_ != nullptr
                              ? depthStencilFormat_
                              : SDL_GPU_TEXTUREFORMAT_INVALID,
                          1, segment.id);
        passOwner.End();
    }

    bool SdlGpuRenderer::EnsureBackbufferProxy(Uint32 width, Uint32 height)
    {
        if (width == 0 || height == 0)
            return false;
        const SDL_GPUTextureFormat format = SDL_GetGPUSwapchainTextureFormat(device_, window_);
        if (backbufferProxy_ != nullptr && backbufferProxyWidth_ == static_cast<int>(width) &&
            backbufferProxyHeight_ == static_cast<int>(height) && backbufferProxyFormat_ == format)
            return true;

        if (backbufferProxy_ != nullptr)
        {
            SDL_ReleaseGPUTexture(device_, backbufferProxy_);
            backbufferProxy_ = nullptr;
        }
        // Same format as the swapchain so the backbuffer pipelines (keyed on the swapchain format)
        // render into it unchanged, and the proxy->swapchain blit is a straight same-format copy.
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = format;
        info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = width;
        info.height = height;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        backbufferProxy_ = SDL_CreateGPUTexture(device_, &info);
        if (backbufferProxy_ == nullptr)
        {
            CNA::Logger::Warn(
                std::string("CNA SDL_GPU: backbuffer readback proxy creation failed: ") + SDL_GetError(),
                CNA::LogCategory::GPU);
            backbufferProxyWidth_ = 0;
            backbufferProxyHeight_ = 0;
            return false;
        }
        backbufferProxyWidth_ = static_cast<int>(width);
        backbufferProxyHeight_ = static_cast<int>(height);
        backbufferProxyFormat_ = format;
        return true;
    }

    void SdlGpuRenderer::BlitBackbufferProxyToSwapchain(SDL_GPUCommandBuffer* cmd,
                                                               SDL_GPUTexture* swapchainTexture)
    {
        SDL_GPUBlitInfo blit{};
        blit.source.texture = backbufferProxy_;
        blit.source.w = static_cast<Uint32>(backbufferProxyWidth_);
        blit.source.h = static_cast<Uint32>(backbufferProxyHeight_);
        blit.destination.texture = swapchainTexture;
        blit.destination.w = static_cast<Uint32>(physicalWidth_);
        blit.destination.h = static_cast<Uint32>(physicalHeight_);
        blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
        blit.filter = SDL_GPU_FILTER_NEAREST;   // proxy and swapchain are the same size: a 1:1 copy
        SDL_BlitGPUTexture(cmd, &blit);
    }

    void SdlGpuRenderer::ReadBackbuffer(int x, int y, int w, int h, std::uint8_t* pixels)
    {
        if (w <= 0 || h <= 0 || pixels == nullptr)
            return;
        const std::size_t outBytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4;

        // Lazily switch to proxy-backed presentation, then flush so the read observes this frame's
        // draws (a no-op if nothing is pending, matching the render-target GetData() contract).
        backbufferReadbackEnabled_ = true;
        EnsureFrameRendered();

        if (const char* trace = std::getenv("CNA_BACKBUFFER_READ_TRACE"); trace != nullptr && *trace != '\0')
        {
            std::fprintf(stderr,
                         "[GFX-165][SDL_GPU] ReadBackbuffer req=(%d,%d,%dx%d) physical=%dx%d proxy=%dx%d "
                         "fmt=%d virtual=%dx%d\n",
                         x, y, w, h, physicalWidth_, physicalHeight_, backbufferProxyWidth_,
                         backbufferProxyHeight_, static_cast<int>(backbufferProxyFormat_),
                         virtualWidth_, virtualHeight_);
            std::fflush(stderr);
        }

        std::memset(pixels, 0, outBytes);
        if (backbufferProxy_ == nullptr)
            return;   // nothing was rendered (e.g. a minimized window acquired no swapchain texture)

        // The request is in logical backbuffer coordinates; the proxy is the physical (swapchain)
        // surface. Clamp the copy to the proxy and leave anything outside it zero-filled, so an odd
        // logical/physical rounding can never read outside the resource (mirrors the WebGPU read).
        if (x >= backbufferProxyWidth_ || y >= backbufferProxyHeight_)
            return;
        const int readX = std::max(0, x);
        const int readY = std::max(0, y);
        const int readW = std::min(w - (readX - x), backbufferProxyWidth_ - readX);
        const int readH = std::min(h - (readY - y), backbufferProxyHeight_ - readY);
        if (readW <= 0 || readH <= 0)
            return;

        const Uint32 regionBytes = static_cast<Uint32>(readW) * static_cast<Uint32>(readH) * 4;
        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        transferInfo.size = regionBytes;
        SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(device_, &transferInfo);
        if (transferBuffer == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: ReadBackbuffer: failed to create transfer buffer: ") + SDL_GetError());

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
        if (cmd == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device_, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: ReadBackbuffer: SDL_AcquireGPUCommandBuffer failed: ") + SDL_GetError());
        }

        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureRegion region{};
        region.texture = backbufferProxy_;
        region.x = static_cast<Uint32>(readX);
        region.y = static_cast<Uint32>(readY);
        region.w = static_cast<Uint32>(readW);
        region.h = static_cast<Uint32>(readH);
        region.d = 1;
        SDL_GPUTextureTransferInfo dest{};
        dest.transfer_buffer = transferBuffer;
        dest.pixels_per_row = static_cast<Uint32>(readW);
        dest.rows_per_layer = static_cast<Uint32>(readH);
        SDL_DownloadFromGPUTexture(copyPass, &region, &dest);
        SDL_EndGPUCopyPass(copyPass);

        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        if (fence == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device_, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: ReadBackbuffer: SDL_SubmitGPUCommandBufferAndAcquireFence failed: ") + SDL_GetError());
        }
        SDL_WaitForGPUFences(device_, true, &fence, 1);
        SDL_ReleaseGPUFence(device_, fence);

        void* mapped = SDL_MapGPUTransferBuffer(device_, transferBuffer, false);
        if (mapped == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device_, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: ReadBackbuffer: SDL_MapGPUTransferBuffer failed: ") + SDL_GetError());
        }

        // The proxy carries the swapchain's own channel order; CNA's public Color is RGBA. Swap R<->B
        // for a BGRA swapchain so the returned bytes are RGBA whatever the native surface format is.
        const bool isBgra = (backbufferProxyFormat_ == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM ||
                             backbufferProxyFormat_ == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB);
        const std::uint8_t* srcBase = static_cast<const std::uint8_t*>(mapped);
        for (int row = 0; row < readH; ++row)
        {
            for (int col = 0; col < readW; ++col)
            {
                const std::uint8_t* s = srcBase + (static_cast<std::size_t>(row) * readW + col) * 4;
                const int dx = (readX - x) + col;
                const int dy = (readY - y) + row;
                std::uint8_t* d = pixels + (static_cast<std::size_t>(dy) * w + dx) * 4;
                if (isBgra) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3]; }
                else        { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3]; }
            }
        }
        SDL_UnmapGPUTransferBuffer(device_, transferBuffer);
        SDL_ReleaseGPUTransferBuffer(device_, transferBuffer);
    }

    void SdlGpuRenderer::RenderToTarget(SDL_GPUCommandBuffer* cmd, const PassSegment& segment,
                                               bool colorLoadedLater)
    {
        constexpr SDL_GPUTextureFormat kRenderTargetFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        const std::shared_ptr<SdlGpuRenderTarget2DState>& target = segment.rt;

        // SDLGPU-37: real MRT -- extraAttachments holds rts[1..] of the SetRenderTargets(count>1)
        // call that opened THIS cycle, so this one render pass gets 1+extraAttachments.size() real
        // simultaneous color attachments. REMED-GFX-145: recorded per segment rather than on the
        // primary's shared state, so a later {a,c} bind cannot retroactively drop b out of the
        // earlier {a,b} pass.
        std::vector<SDL_GPUColorTargetInfo> colorTargets(1 + segment.extraAttachments.size());
        FillColorTargetInfo(colorTargets[0], *target, segment.clearColorRequested, segment.clearColor,
                            colorLoadedLater);
        for (std::size_t i = 0; i < segment.extraAttachments.size(); ++i)
            FillColorTargetInfo(colorTargets[i + 1], *segment.extraAttachments[i],
                                segment.clearColorRequested, segment.clearColor, colorLoadedLater);
        const int colorTargetCount = static_cast<int>(colorTargets.size());

        SDL_GPUDepthStencilTargetInfo depthStencilTarget{};
        const bool hasDepth = target->depthTexture != nullptr;
        if (hasDepth)
        {
            depthStencilTarget.texture = target->depthTexture;
            depthStencilTarget.clear_depth = segment.clearDepthRequested ? segment.clearDepth
                                                                         : target->clearDepth;
            depthStencilTarget.load_op = (segment.clearDepthRequested || target->clearDepthPending)
                                             ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            depthStencilTarget.store_op = SDL_GPU_STOREOP_STORE;
            depthStencilTarget.clear_stencil = segment.clearStencilRequested ? segment.clearStencil
                                                                             : target->clearStencil;
            depthStencilTarget.stencil_load_op =
                (segment.clearStencilRequested || target->clearStencilPending) ? SDL_GPU_LOADOP_CLEAR
                                                                              : SDL_GPU_LOADOP_LOAD;
            depthStencilTarget.stencil_store_op = SDL_GPU_STOREOP_STORE;
        }

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, colorTargets.data(), static_cast<Uint32>(colorTargetCount),
                                                         hasDepth ? &depthStencilTarget : nullptr);
        RenderPassOwner passOwner(pass);
        TracePassSegment(nativePassIndex_++, segment,
                         colorTargets[0].load_op == SDL_GPU_LOADOP_CLEAR ? "clear" : "load",
                         colorTargets[0].store_op == SDL_GPU_STOREOP_RESOLVE ? "resolve"
                             : colorTargets[0].store_op == SDL_GPU_STOREOP_RESOLVE_AND_STORE
                                 ? "resolve+store" : "store",
                         !hasDepth ? "none"
                             : (depthStencilTarget.load_op == SDL_GPU_LOADOP_CLEAR ? "clear" : "load"),
                         !hasDepth ? "none"
                             : (depthStencilTarget.stencil_load_op == SDL_GPU_LOADOP_CLEAR ? "clear"
                                                                                           : "load"),
                         std::count_if(drawOrder_.begin(), drawOrder_.end(),
                                       [&](const QueuedDrawRef& r) { return r.segment == segment.id; }));
        // REMED-GFX-068: scissor applied per draw in RenderQueuedDraws (see the swapchain pass note).
        const DrawTarget dt{target.get(), nullptr, -1};
        RenderQueuedDraws(pass, cmd, dt, kRenderTargetFormat, target->sampleCount,
                          hasDepth ? depthStencilFormat_ : SDL_GPU_TEXTUREFORMAT_INVALID,
                          colorTargetCount, segment.id);
        passOwner.End();

        // Whatever this pass just stored is this resource's content from here on: a LATER segment
        // of the same resource must LOAD it, not clear it again.
        target->clearColorPending = false;
        target->clearDepthPending = false;
        target->clearStencilPending = false;
        for (const auto& extra : segment.extraAttachments)
        {
            extra->clearColorPending = false;
            extra->clearDepthPending = false;
            extra->clearStencilPending = false;
        }

        // REMED-GFX-187: regenerate through clamped per-level GPU blits outside the pass. This
        // matches FNA3D's OPENGL_ResolveTarget semantics (the chain is regenerated once this
        // target's contents are final for the segment) and keeps the existing resolve-before-mips
        // ordering. Per segment, since each segment's result is what a LATER segment or the
        // swapchain pass may sample.
        if (target->mipMap && target->levelCount > 1)
            GenerateRenderTargetMipChain(cmd, target->colorTexture, target->width,
                                         target->height, target->levelCount);
        for (const auto& extra : segment.extraAttachments)
            if (extra->mipMap && extra->levelCount > 1)
                GenerateRenderTargetMipChain(cmd, extra->colorTexture, extra->width,
                                             extra->height, extra->levelCount);
    }

    void SdlGpuRenderer::RenderToTargetCubeFace(SDL_GPUCommandBuffer* cmd,
                                                       const PassSegment& segment,
                                                       bool colorLoadedLater)
    {
        constexpr SDL_GPUTextureFormat kRenderTargetFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        const std::shared_ptr<SdlGpuRenderTargetCubeState>& cube = segment.cube;
        const int face = segment.face;

        const std::size_t faceSlot = static_cast<std::size_t>(face);
        SDL_GPUTexture* const faceMsaa = cube->msaaTextures[faceSlot];
        const bool cubeMsaa = faceMsaa != nullptr;

        SDL_GPUColorTargetInfo colorTarget{};
        if (cubeMsaa)
        {
            // REMED-GFX-141: THIS face's own single-layer multisample texture -- there are six now,
            // so layer_or_depth_plane is 0 on a texture that belongs to exactly one face, not 0 on
            // one shared by all six.
            colorTarget.texture = faceMsaa;
            colorTarget.layer_or_depth_plane = 0;
        }
        else
        {
            colorTarget.texture = cube->cubeTexture;
            colorTarget.layer_or_depth_plane = static_cast<Uint32>(face);
        }
        colorTarget.clear_color = segment.clearColorRequested ? segment.clearColor
                                                              : cube->clearColor[face];
        // REMED-GFX-136: LOAD is what makes a cube face preserve its contents across bind cycles.
        // REMED-GFX-141: the multisampled path can finally use it. It used to be forced to CLEAR,
        // because the ONE shared scratch texture held another face's samples and therefore had to
        // be cycled -- and SDL_gpu forbids cycling together with LOAD out loud ('Cannot cycle color
        // target when load op is LOAD!'), which is exactly what a PreserveContents cube reached the
        // second time a multisampled face was bound with no Clear() in between. With one texture
        // per face there is nothing to cycle and nothing stale to load, so both legs now ask the
        // same question: an explicit Clear() in this cycle wins, otherwise this face's own first-use
        // flag, otherwise LOAD.
        colorTarget.load_op =
            (segment.clearColorRequested || cube->clearColorPending[face])
                ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        // REMED-GFX-141: never cycle. cubeTexture must NEVER cycle (whether as the direct target
        // below or as resolve_texture) -- cycling wipes the ENTIRE persistent 6-layer resource,
        // including every other face already written this frame -- and a per-face multisample
        // texture must not either, since its previous contents are that same face's own samples,
        // which is precisely what this pass may be about to load.
        colorTarget.cycle = false;
        if (cubeMsaa)
        {
            // Automatic render-pass-end resolve -- SDL_gpu has no multisampled cube texture type,
            // so this face's MSAA color target resolves directly into that face's layer of the real
            // (single-sample, 6-layer) cube texture.
            // REMED-GFX-141: RESOLVE_AND_STORE on a preserving target -- plain RESOLVE explicitly
            // leaves the multisample contents undefined, which is the store half of the same defect
            // the shared texture was the allocation half of. A discarding target keeps plain
            // RESOLVE: the shared layer clears it on every bind, so its samples are never loaded.
            colorTarget.resolve_texture = cube->cubeTexture;
            colorTarget.resolve_layer = static_cast<Uint32>(face);
            // REMED-GFX-156: `colorLoadedLater` extends the same rule to a DISCARDING cube whose
            // face this frame splits into several passes -- the later pass loads these samples, so
            // leaving them undefined would be the same defect on a target that merely happens not
            // to preserve across bind cycles.
            colorTarget.store_op = (cube->preserveContents || colorLoadedLater)
                                       ? SDL_GPU_STOREOP_RESOLVE_AND_STORE
                                       : SDL_GPU_STOREOP_RESOLVE;
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
            depthStencilTarget.clear_depth = segment.clearDepthRequested ? segment.clearDepth
                                                                         : cube->clearDepth[face];
            depthStencilTarget.load_op =
                (segment.clearDepthRequested || cube->clearDepthPending[face]) ? SDL_GPU_LOADOP_CLEAR
                                                                               : SDL_GPU_LOADOP_LOAD;
            depthStencilTarget.store_op = SDL_GPU_STOREOP_STORE;
            depthStencilTarget.clear_stencil = segment.clearStencilRequested
                                                   ? segment.clearStencil : cube->clearStencil[face];
            depthStencilTarget.stencil_load_op =
                (segment.clearStencilRequested || cube->clearStencilPending[face])
                    ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            depthStencilTarget.stencil_store_op = SDL_GPU_STOREOP_STORE;
        }

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &colorTarget, 1, hasDepth ? &depthStencilTarget : nullptr);
        RenderPassOwner passOwner(pass);
        TracePassSegment(nativePassIndex_++, segment,
                         colorTarget.load_op == SDL_GPU_LOADOP_CLEAR ? "clear" : "load",
                         colorTarget.store_op == SDL_GPU_STOREOP_RESOLVE ? "resolve"
                             : colorTarget.store_op == SDL_GPU_STOREOP_RESOLVE_AND_STORE
                                 ? "resolve+store" : "store",
                         !hasDepth ? "none"
                             : (depthStencilTarget.load_op == SDL_GPU_LOADOP_CLEAR ? "clear" : "load"),
                         !hasDepth ? "none"
                             : (depthStencilTarget.stencil_load_op == SDL_GPU_LOADOP_CLEAR ? "clear"
                                                                                           : "load"),
                         std::count_if(drawOrder_.begin(), drawOrder_.end(),
                                       [&](const QueuedDrawRef& r) { return r.segment == segment.id; }));
        // REMED-GFX-068: scissor applied per draw in RenderQueuedDraws (see the swapchain pass note).
        const DrawTarget dt{nullptr, cube.get(), face};
        RenderQueuedDraws(pass, cmd, dt, kRenderTargetFormat, cube->sampleCount,
                          hasDepth ? depthStencilFormat_ : SDL_GPU_TEXTUREFORMAT_INVALID, 1,
                          segment.id);
        passOwner.End();

        // REMED-GFX-188: the pass end has either stored this face directly or resolved its own
        // level-zero MSAA attachment into this face of the single-sample cube. Generate that
        // face's lower levels now, outside the pass and before any later pass can sample them.
        // A one-level target deliberately issues no mip work.
        if (cube->mipMap && cube->levelCount > 1)
            GenerateCubeRenderTargetMipChain(cmd, cube->cubeTexture, cube->size,
                                             cube->levelCount, face);

        // Same rationale as RenderToTarget's: this face now HAS content, so a later cycle of it
        // must LOAD rather than clear again.
        cube->clearColorPending[face] = false;
        cube->clearDepthPending[face] = false;
        cube->clearStencilPending[face] = false;
    }

    SdlGpuRenderer::PassSegment* SdlGpuRenderer::CurrentSegment()
    {
        if (currentSegment_ == kSwapchainSegment || passSegments_.empty())
            return nullptr;
        // Segments are appended on bind and never reordered, so the open one is always the last.
        PassSegment& back = passSegments_.back();
        return back.id == currentSegment_ ? &back : nullptr;
    }

    void SdlGpuRenderer::BeginRenderTargetSegment(
        const std::shared_ptr<SdlGpuRenderTarget2DState>& rt)
    {
        PassSegment segment;
        segment.id = nextSegmentId_++;
        segment.rt = rt;
        passSegments_.push_back(std::move(segment));
        currentSegment_ = passSegments_.back().id;
        framePending_ = true;
    }

    void SdlGpuRenderer::BeginCubeFaceSegment(
        const std::shared_ptr<SdlGpuRenderTargetCubeState>& cube, int face)
    {
        PassSegment segment;
        segment.id = nextSegmentId_++;
        segment.cube = cube;
        segment.face = face;
        passSegments_.push_back(std::move(segment));
        currentSegment_ = passSegments_.back().id;
        framePending_ = true;
    }

    // REMED-GFX-143: a backbuffer bind cycle is a segment like any other. Selecting the backbuffer
    // is not work, so framePending_ is deliberately NOT set here -- the first Clear() or draw issued
    // inside the cycle sets it, exactly as it did when the backbuffer had no segment at all.
    void SdlGpuRenderer::BeginBackbufferSegment()
    {
        PassSegment segment;
        segment.id = nextSegmentId_++;
        passSegments_.push_back(std::move(segment));
        currentSegment_ = passSegments_.back().id;
    }

    // REMED-GFX-143: unbinding a target returns to the backbuffer, which opens a NEW backbuffer
    // cycle rather than resuming a frame-wide "swapchain" bucket. That is what makes
    // `draw A; bind t; draw; unbind; draw B` three ordered passes instead of one target pass
    // followed by a trailing pass holding both A and B.
    void SdlGpuRenderer::EndRenderTargetSegment()
    {
        BeginBackbufferSegment();
    }

    /**
     * @brief REMED-GFX-156: whether the native clear-order trace is switched on for this process.
     *
     * Off unless `CNA_SDLGPU_TRACE_CLEAR_ORDER` is set in the environment, read exactly once.
     */
    static bool TraceClearOrder() noexcept
    {
        static const bool enabled = std::getenv("CNA_SDLGPU_TRACE_CLEAR_ORDER") != nullptr;
        return enabled;
    }

    const char* SdlGpuRenderer::SegmentDestinationName(const PassSegment& segment)
    {
        if (segment.cube != nullptr)
        {
            static const char* const kFaceNames[6] = {
                "rendertargetcube-face0", "rendertargetcube-face1", "rendertargetcube-face2",
                "rendertargetcube-face3", "rendertargetcube-face4", "rendertargetcube-face5"};
            if (segment.face >= 0 && segment.face < 6) return kFaceNames[segment.face];
            return "rendertargetcube-face?";
        }
        return segment.rt != nullptr ? "rendertarget2d" : "backbuffer";
    }

    void SdlGpuRenderer::TracePassSegment(std::size_t passIndex, const PassSegment& segment,
                                                 const char* colorLoad, const char* colorStore,
                                                 const char* depthLoad, const char* stencilLoad,
                                                 std::size_t draws) const
    {
        if (!TraceClearOrder()) return;
        std::fprintf(stderr,
                     "[sdlgpu-order] pass #%zu segment=%llu opened-by=%s destination=%s "
                     "clear=%s%s%s colorLoad=%s colorStore=%s depthLoad=%s stencilLoad=%s draws=%zu\n",
                     passIndex, static_cast<unsigned long long>(segment.id),
                     segment.openedByClear ? "clear" : "bind", SegmentDestinationName(segment),
                     segment.clearColorRequested ? "target" : "",
                     segment.clearDepthRequested ? "|depth" : "",
                     segment.clearStencilRequested ? "|stencil" : "",
                     colorLoad, colorStore, depthLoad, stencilLoad, draws);
    }

    SdlGpuRenderer::PassSegment* SdlGpuRenderer::SegmentForOrderedClear()
    {
        PassSegment* open = CurrentSegment();
        if (open == nullptr) return nullptr;
        // Nothing observable has happened since this segment opened, so the request folds into its
        // load action -- a leading Clear(), or a run of Clear()s, costs no extra pass. Coalescing is
        // safe precisely BECAUSE nothing separates them: the caller overwrites whichever aspects it
        // names and leaves the others as the earlier request left them, which is what "Clear(Target)
        // then Clear(Depth)" must mean.
        if (!open->hasDraws) return open;

        // A draw of this cycle precedes the Clear, so the clear is observable and needs a load
        // action of its own: close the segment and open another over the SAME destination.
        PassSegment split;
        split.id = nextSegmentId_++;
        split.rt = open->rt;
        split.cube = open->cube;
        split.face = open->face;
        split.extraAttachments = open->extraAttachments;
        split.openedByClear = true;
        passSegments_.push_back(std::move(split));
        currentSegment_ = passSegments_.back().id;
        framePending_ = true;
        if (TraceClearOrder())
        {
            std::fprintf(stderr,
                         "[sdlgpu-order] clear splits segment %llu -> %llu destination=%s\n",
                         static_cast<unsigned long long>(open->id),
                         static_cast<unsigned long long>(passSegments_.back().id),
                         SegmentDestinationName(passSegments_.back()));
        }
        return &passSegments_.back();
    }

    bool SdlGpuRenderer::SegmentColorLoadedLater(std::size_t index) const
    {
        if (index >= passSegments_.size()) return false;
        const PassSegment& segment = passSegments_[index];
        for (std::size_t i = index + 1; i < passSegments_.size(); ++i)
        {
            const PassSegment& later = passSegments_[i];
            if (segment.rt != nullptr && later.rt == segment.rt) return true;
            if (segment.cube != nullptr && later.cube == segment.cube && later.face == segment.face)
                return true;
        }
        return false;
    }

    void SdlGpuRenderer::ReopenSegmentForBoundTarget()
    {
        currentSegment_ = kSwapchainSegment;
        if (currentRenderTargetCube_ != nullptr && currentActiveCubeFace_ >= 0)
            BeginCubeFaceSegment(currentRenderTargetCube_->State(), currentActiveCubeFace_);
        else if (currentRenderTarget_ != nullptr)
            BeginRenderTargetSegment(currentRenderTarget_->State());
        else
            BeginBackbufferSegment();  // REMED-GFX-143: the backbuffer needs its cycle back too.
        // Re-opening must not itself make the frame look dirty -- nothing has been queued yet.
        framePending_ = false;
    }

    void SdlGpuRenderer::Clear(float r, float g, float b, float a)
    {
        // REMED-GFX-145: an explicit Clear() belongs to the ONE bind cycle it was issued in, not to
        // the target. It used to be stored on the target state, so the frame's LAST Clear() decided
        // the load-op colour of that target's single merged pass and every earlier cycle's clear
        // was lost. The segment also covers every MRT attachment of that same cycle, which is what
        // the old currentExtraMrtTargets_ propagation did by hand.
        // REMED-GFX-156: and to the ordered POSITION inside that cycle, which is what
        // SegmentForOrderedClear resolves -- a Clear() after a draw opens its own segment.
        if (PassSegment* segment = SegmentForOrderedClear())
        {
            segment->clearColorRequested = true;
            segment->clearColor = SDL_FColor{r, g, b, a};
        }
        else
        {
            clearColor_ = SDL_FColor{r, g, b, a};
            clearColorPending_ = true;
        }
        framePending_ = true;
    }

    void SdlGpuRenderer::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        Clear(r, g, b, a);
        ClearDepth(depth);
    }

    void SdlGpuRenderer::ClearDepth(float depth)
    {
        // REMED-GFX-145 / REMED-GFX-156: per bind cycle and per ordered position, exactly like
        // Clear() above. A multi-aspect public Clear() reaches here through ClearColorAndDepth and
        // friends, whose first call already opened the segment this one folds into -- so one public
        // Clear() is one pass boundary however many aspects it names.
        if (PassSegment* segment = SegmentForOrderedClear())
        {
            segment->clearDepthRequested = true;
            segment->clearDepth = depth;
        }
        else
        {
            clearDepth_ = depth;
            clearDepthPending_ = true;
        }
        framePending_ = true;
    }

    void SdlGpuRenderer::ClearStencil(int stencil)
    {
        // REMED-GFX-145 / REMED-GFX-156: per bind cycle and per ordered position, exactly like
        // Clear() above.
        if (PassSegment* segment = SegmentForOrderedClear())
        {
            segment->clearStencilRequested = true;
            segment->clearStencil = static_cast<Uint8>(stencil);
        }
        else
        {
            clearStencil_ = static_cast<Uint8>(stencil);
            clearStencilPending_ = true;
        }
        framePending_ = true;
    }

    SdlGpuRenderer::DrawTarget SdlGpuRenderer::CurrentDrawTarget() const
    {
        if (currentRenderTargetCube_ != nullptr)
            return DrawTarget{nullptr, currentRenderTargetCube_->State().get(), currentActiveCubeFace_};
        if (currentRenderTarget_ != nullptr)
            return DrawTarget{currentRenderTarget_->State().get(), nullptr, -1};
        return DrawTarget{};
    }

    void SdlGpuRenderer::ClearDepthAndStencil(float depth, int stencil)
    {
        ClearDepth(depth);
        ClearStencil(stencil);
    }

    void SdlGpuRenderer::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        Clear(r, g, b, a);
        ClearStencil(stencil);
    }

    void SdlGpuRenderer::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil)
    {
        Clear(r, g, b, a);
        ClearDepth(depth);
        ClearStencil(stencil);
    }

    void SdlGpuRenderer::Present()
    {
        // SDL_gpu automatically presents the acquired swapchain texture once the command buffer
        // that acquired it is submitted -- there is no separate explicit present call.
        EnsureFrameRendered();
    }

    SdlGpuRenderer::LogicalViewport SdlGpuRenderer::ComputeLogicalViewport() const
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

    void SdlGpuRenderer::GetViewportSize(int& width, int& height)
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        width = static_cast<int>(std::lround(viewport.logicalWidth));
        height = static_cast<int>(std::lround(viewport.logicalHeight));
    }

    void SdlGpuRenderer::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void SdlGpuRenderer::SetPresentationMode(int mode)
    {
        if (mode < static_cast<int>(CnaPresentationMode::Letterbox) ||
            mode > static_cast<int>(CnaPresentationMode::FixedHeightDynamicWidth))
            throw std::out_of_range("CNA SDL_GPU: invalid presentation mode");
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    void SdlGpuRenderer::ConfigureSwapchain(SDL_GPUDevice* device, SDL_Window* window,
                                                    int interval)
    {
        // plan_sdlgpu.md: SDL_gpu has no "half-rate" present mode -- XNA's PresentInterval.Two
        // (swapInterval==2) falls back to plain VSYNC, same as swapInterval==1.
        SDL_GPUPresentMode presentMode = SDL_GPU_PRESENTMODE_VSYNC;
        if (interval == 0)
        {
            if (SDL_WindowSupportsGPUPresentMode(device, window, SDL_GPU_PRESENTMODE_IMMEDIATE))
                presentMode = SDL_GPU_PRESENTMODE_IMMEDIATE;
            else if (SDL_WindowSupportsGPUPresentMode(device, window, SDL_GPU_PRESENTMODE_MAILBOX))
                presentMode = SDL_GPU_PRESENTMODE_MAILBOX;
        }

        if (!SDL_SetGPUSwapchainParameters(
                device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, presentMode))
        {
            // Genuine per-device/driver capability gap, not a "not implemented yet" stub.
            CNA::Logger::Warn(
                std::string("CNA SDL_GPU: SDL_SetGPUSwapchainParameters failed: ") + SDL_GetError(),
                CNA::LogCategory::GPU);
        }
    }

    void SdlGpuRenderer::SetSwapInterval(int interval)
    {
        interval = std::max(0, interval);
        ConfigureSwapchain(device_, window_, interval);
        swapInterval_ = interval;
    }

    void SdlGpuRenderer::MaybeFailForTest(SdlGpuFailurePointEXT point)
    {
        InjectFailure(testHooks_, testFailureInjected_, point);
    }

    void SdlGpuRenderer::NotifyResourceEvent(
        SdlGpuResourceKindEXT resource, SdlGpuResourceEventEXT event) const noexcept
    {
        NotifyResource(testHooks_, resource, event);
    }

    SDL_GPUGraphicsPipeline* SdlGpuRenderer::CreateGraphicsPipeline(
        const SDL_GPUGraphicsPipelineCreateInfo& createInfo, const char* diagnostic)
    {
        MaybeFailForTest(SdlGpuFailurePointEXT::GraphicsPipelineCreation);
        SDL_GPUGraphicsPipeline* pipeline =
            SDL_CreateGPUGraphicsPipeline(device_, &createInfo);
        if (pipeline == nullptr)
            throw std::runtime_error(std::string(diagnostic) + SDL_GetError());
        NotifyResourceEvent(SdlGpuResourceKindEXT::GraphicsPipeline,
                            SdlGpuResourceEventEXT::Acquired);
        return pipeline;
    }

    SDL_GPUGraphicsPipeline* SdlGpuRenderer::CacheGraphicsPipeline(
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*>& cache,
        std::size_t key, SDL_GPUGraphicsPipeline* pipeline)
    {
        try
        {
            const auto [it, inserted] = cache.emplace(key, pipeline);
            if (!inserted)
                ReleaseGraphicsPipeline(pipeline);
            return it->second;
        }
        catch (...)
        {
            ReleaseGraphicsPipeline(pipeline);
            throw;
        }
    }

    void SdlGpuRenderer::ReleaseGraphicsPipeline(
        SDL_GPUGraphicsPipeline* pipeline) noexcept
    {
        if (pipeline == nullptr)
            return;
        SDL_ReleaseGPUGraphicsPipeline(device_, pipeline);
        NotifyResourceEvent(SdlGpuResourceKindEXT::GraphicsPipeline,
                            SdlGpuResourceEventEXT::Released);
    }

    void SdlGpuRenderer::ReleaseShader(SDL_GPUShader*& shader) noexcept
    {
        if (shader == nullptr)
            return;
        SDL_ReleaseGPUShader(device_, shader);
        shader = nullptr;
        NotifyResourceEvent(SdlGpuResourceKindEXT::Shader,
                            SdlGpuResourceEventEXT::Released);
    }

    void SdlGpuRenderer::ReleaseSampler(SDL_GPUSampler*& sampler) noexcept
    {
        if (sampler == nullptr)
            return;
        SDL_ReleaseGPUSampler(device_, sampler);
        sampler = nullptr;
        NotifyResourceEvent(SdlGpuResourceKindEXT::Sampler,
                            SdlGpuResourceEventEXT::Released);
    }

    bool SdlGpuRenderer::TransformWindowToLogical(float windowX, float windowY, float& logicalX, float& logicalY) const
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.width == 0.0f || viewport.height == 0.0f)
            return false;
        logicalX = (windowX - viewport.x) * viewport.logicalWidth / viewport.width;
        logicalY = (windowY - viewport.y) * viewport.logicalHeight / viewport.height;
        return windowX >= viewport.x && windowX < viewport.x + viewport.width &&
               windowY >= viewport.y && windowY < viewport.y + viewport.height;
    }

    bool SdlGpuRenderer::TransformLogicalToWindow(float logicalX, float logicalY, float& windowX, float& windowY) const
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.logicalWidth == 0.0f || viewport.logicalHeight == 0.0f)
            return false;
        windowX = viewport.x + logicalX * viewport.width / viewport.logicalWidth;
        windowY = viewport.y + logicalY * viewport.height / viewport.logicalHeight;
        return true;
    }

    std::unique_ptr<ITextureRenderer> SdlGpuRenderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<SdlGpuTextureRenderer>(*this, data);
    }

    std::unique_ptr<ISpriteBatchRenderer> SdlGpuRenderer::CreateSpriteBatch()
    {
        return std::make_unique<SdlGpuSpriteBatchRenderer>(*this);
    }

    void SdlGpuRenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                                int colorDstBlend, int alphaDstBlend,
                                                int colorBlendFunc, int alphaBlendFunc,
                                                const BlendWriteState& writeState)
    {
        // Blend::One=0, Blend::Zero=1 -> Opaque preset: src=One, dst=Zero -> no blending. Matches
        // VulkanRenderer::ApplyBlendState's own derivation exactly.
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
        // set to 0 / false" — a genuine renderer capability gap (REMED-GFX-086), not a silent drop.
        for (int i = 0; i < 4; ++i)
            colorWriteMasks_[i] = writeState.colorWriteChannels[i];
    }

    void SdlGpuRenderer::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
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

    void SdlGpuRenderer::ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                                     float depthBias, float slopeScaleDepthBias)
    {
        cullMode_ = cullMode;
        fillModeWireframe_ = (fillMode == 1);  // XNA FillMode::WireFrame = 1
        scissorEnabled_ = scissorTestEnable;
        // REMED-GFX-051: store the raw XNA/FNA factors. CaptureRenderState snapshots them into
        // every queued command; pipeline selection/creation performs the topology/zero
        // normalization because SDL 3.5 exposes bias only as graphics-pipeline-static state.
        depthBias_ = depthBias;
        slopeScaleDepthBias_ = slopeScaleDepthBias;
    }

    void SdlGpuRenderer::SetBlendFactor(float r, float g, float b, float a)
    {
        // REMED-GFX-069: store only. The value is snapshotted per draw into each QueuedDrawRef at
        // Queue*Draw()/QueueSprite() time (PushDrawOrder) and applied at Present-time replay
        // (ApplyBlendFactorForRef) -- never applied here, mirroring how every other per-command
        // state (viewport, scissor rect, stencil reference) is captured on this deferred renderer.
        blendFactorR_ = r;
        blendFactorG_ = g;
        blendFactorB_ = b;
        blendFactorA_ = a;
    }

    void SdlGpuRenderer::SetReferenceStencil(int value)
    {
        referenceStencil_ = value;
    }

    void SdlGpuRenderer::SetScissorRect(int x, int y, int w, int h)
    {
        scissorX_ = x;
        scissorY_ = y;
        scissorW_ = w;
        scissorH_ = h;
    }

    void SdlGpuRenderer::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        // REMED-GFX-064: store only. The value is snapshotted per draw into each QueuedDrawRef at
        // Queue*Draw()/QueueSprite() time (PushDrawOrder) and applied at Present-time replay
        // (ApplyViewportForRef) -- never read back here, mirroring how every other per-command
        // state (scissor rect, blend, stencil) is captured on this deferred renderer.
        viewportSet_ = true;
        viewportX_ = x;
        viewportY_ = y;
        viewportW_ = w;
        viewportH_ = h;
        viewportMinDepth_ = minDepth;
        viewportMaxDepth_ = maxDepth;
    }

    void SdlGpuRenderer::ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy)
    {
        if (slot < 0 || slot >= static_cast<int>(samplerSlots_.size()))
            return;
        samplerSlots_[slot].filter = filter;
        samplerSlots_[slot].addressU = addressU;
        samplerSlots_[slot].addressV = addressV;
        samplerSlots_[slot].maxAnisotropy = maxAnisotropy;
    }

    void SdlGpuRenderer::ApplyScissorForRef(SDL_GPURenderPass* pass, const QueuedDrawRef& ref,
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
            // VulkanRenderer::computeScissor (REMED-GFX-013). SDL_Rect is top-left origin,
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
            // full render target/swapchain extents, matching VulkanRenderer's own
            // scissorEnabled_-gated fallback-to-full-target convention.
            rect.x = 0;
            rect.y = 0;
            rect.w = targetWidth;
            rect.h = targetHeight;
        }
        SDL_SetGPUScissor(pass, &rect);
    }

    void SdlGpuRenderer::ApplyBlendFactorForRef(SDL_GPURenderPass* pass, const QueuedDrawRef& ref) const
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

    void SdlGpuRenderer::PushDrawOrder(DrawKind kind, std::size_t index)
    {
        // REMED-GFX-064/068/069: snapshot the current viewport, scissor AND blend factor into the
        // ref so a later SetRenderTarget reset / SetViewport / SetScissorRect / ApplyRasterizerState
        // / SetBlendFactor change never retroactively alters an already-queued draw's dynamic state.
        // REMED-GFX-145: the segment is snapshotted here too, and for the same reason -- this is
        // the single choke point every Queue*Draw()/QueueSprite() already routes through, so no
        // call site can forget which bind cycle its draw belongs to.
        QueuedDrawRef ref{kind, index, currentSegment_, viewportSet_, viewportX_, viewportY_,
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
        // REMED-GFX-156: this segment now holds something observable, so the NEXT Clear() issued
        // inside it is an ordered command that needs its own pass rather than a load action here.
        if (PassSegment* segment = CurrentSegment())
            segment->hasDraws = true;
        if (TraceClearOrder())
        {
            std::fprintf(stderr, "[sdlgpu-order] enqueue #%zu draw kind=%d segment=%llu\n",
                         drawOrder_.size() - 1, static_cast<int>(kind),
                         static_cast<unsigned long long>(currentSegment_));
        }
    }

    void SdlGpuRenderer::ApplyViewportForRef(SDL_GPURenderPass* pass, const QueuedDrawRef& ref,
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

    SdlGpuRenderer::RenderStateSnapshot SdlGpuRenderer::CaptureRenderState() const
    {
        RenderStateSnapshot rs;
        rs.blendEnabled = blendEnabled_;
        rs.blend = blendParams_;
        rs.colorWriteMasks = colorWriteMasks_; // REMED-GFX-077/-098
        rs.cullMode = cullMode_;
        rs.wireframe = fillModeWireframe_;
        rs.depthBias = depthBias_;
        rs.slopeScaleDepthBias = slopeScaleDepthBias_;
        rs.stencil = stencilParams_;
        rs.stencilReference = referenceStencil_;
        return rs;
    }

    std::unique_ptr<IVertexBufferRenderer> SdlGpuRenderer::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<SdlGpuVertexBufferRenderer>(*this, vertex_capacity);
    }

    std::unique_ptr<IIndexBufferRenderer> SdlGpuRenderer::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<SdlGpuIndexBufferRenderer>(*this, index_capacity, false);
    }

    std::unique_ptr<IIndexBufferRenderer> SdlGpuRenderer::CreateIndexBuffer32(int index_capacity)
    {
        return std::make_unique<SdlGpuIndexBufferRenderer>(*this, index_capacity, true);
    }

    std::unique_ptr<IRenderTargetRenderer> SdlGpuRenderer::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool /*preserveContents*/, bool mipMap, int multiSampleCount)
    {
        return std::make_unique<SdlGpuRenderTargetRenderer>(*this, w, h, depthFormat, mipMap, multiSampleCount);
    }

    void SdlGpuRenderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        if (rt != nullptr)
        {
            auto* renderer = static_cast<SdlGpuRenderTargetRenderer*>(rt);
            // REMED-GFX-145: this opens a NEW segment even when `rt` is already the current target.
            // A stale attachment set can no longer leak in from an earlier SetRenderTargets call
            // either -- the set lives on the segment, and this one starts empty (SetRenderTargets
            // repopulates it right after this call when count>1).
            renderer->BindAsRenderTarget();
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

    std::unique_ptr<IRenderTargetCubeRenderer> SdlGpuRenderer::CreateRenderTargetCube(
        int size, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        // REMED-GFX-136 threaded `preserveContents` here and left it unused: RenderToTargetCubeFace()
        // already picks SDL_GPU_LOADOP_LOAD unless a real Clear() is pending for that face
        // (clearColorPending[face]), and the only clear a cube target gets without the game asking
        // is the one GraphicsDevice::SetRenderTargets issues for a DiscardContents target -- so a
        // single-sample face is preserved by construction. REMED-GFX-141 gives it a real consumer:
        // a multisampled face now owns its own multisample texture, and this flag decides whether
        // that texture's store op keeps the samples (RESOLVE_AND_STORE) or drops them (RESOLVE).
        return std::make_unique<SdlGpuRenderTargetCubeRenderer>(*this, size, depthFormat,
                                                               preserveContents, mipMap,
                                                               multiSampleCount);
    }

    std::unique_ptr<ITexture3DRenderer> SdlGpuRenderer::CreateTexture3D(
        int w, int h, int depth, bool mipMap, int /*surfaceFormat*/)
    {
        return std::make_unique<SdlGpuTexture3DRenderer>(*this, w, h, depth, mipMap);
    }

    std::unique_ptr<ITextureCubeRenderer> SdlGpuRenderer::CreateTextureCube(
        int size, bool mipMap, int /*surfaceFormat*/)
    {
        return std::make_unique<SdlGpuTextureCubeRenderer>(*this, size, mipMap);
    }

    void SdlGpuRenderer::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
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
                    "SdlGpuRenderer::SetRenderTargets: cube faces in a multi-target "
                    "set are not implemented by this CNA renderer.");

        // Stock (single-output) draws remain single-target (rts[0] only) -- no stock shader family
        // in this codebase declares more than one fragment output, matching this project's
        // D3D11/D3D12 stock-effect behavior. A custom multi-output ShaderEffect drawn as a sprite
        // WHILE this binding is active genuinely renders into all `count` targets simultaneously,
        // in one real render pass (see RenderToTarget's own segment-attachment-driven multi-attachment
        // build) -- extra targets are no longer just Clear()-only placeholders.
        SetRenderTarget2D(renderTargets[0].GetRenderTarget2D());
        for (int i = 1; i < count; ++i)
        {
            auto* extra = static_cast<SdlGpuRenderTargetRenderer*>(
                renderTargets[i].GetRenderTarget2D());
            // REMED-GFX-145: an extra attachment of the segment the primary bind just opened.
            extra->AttachToCurrentSegment();
        }
    }

    void SdlGpuRenderer::CreateSpriteResources(ConstructionResources& resources)
    {
        SDL_GPUShaderCreateInfo vsInfo{};
        vsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kSprite2dVertSpv);
        vsInfo.code_size = Shaders::kSprite2dVertSpv_size;
        vsInfo.entrypoint = "main";
        vsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        vsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        vsInfo.num_uniform_buffers = 1;
        resources.CreateShader(
            ConstructionShader::SpriteVertex,
            SdlGpuFailurePointEXT::SpriteVertexShaderCreation, vsInfo,
            "CNA SDL_GPU: failed to create sprite vertex shader: ");

        SDL_GPUShaderCreateInfo fsInfo{};
        fsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kSprite2dFragSpv);
        fsInfo.code_size = Shaders::kSprite2dFragSpv_size;
        fsInfo.entrypoint = "main";
        fsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        fsInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        fsInfo.num_samplers = 1;
        resources.CreateShader(
            ConstructionShader::SpriteFragment,
            SdlGpuFailurePointEXT::SpriteFragmentShaderCreation, fsInfo,
            "CNA SDL_GPU: failed to create sprite fragment shader: ");
    }

    void SdlGpuRenderer::DestroySpriteResources()
    {
        for (auto& [key, pipeline] : spritePipelines_)
            ReleaseGraphicsPipeline(pipeline);
        spritePipelines_.clear();
        for (auto& [key, sampler] : samplerCache_)
            ReleaseSampler(sampler);
        samplerCache_.clear();
        if (spriteVertexBuffer_ != nullptr)
        {
            SDL_ReleaseGPUBuffer(device_, spriteVertexBuffer_);
            spriteVertexBuffer_ = nullptr;
        }
        ReleaseShader(spriteFragmentShader_);
        ReleaseShader(spriteVertexShader_);
    }

    SDL_GPUGraphicsPipeline* SdlGpuRenderer::GetOrCreateSpritePipeline(
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
        SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, bool depthTest, bool depthWrite, int depthFunc,
        const RenderStateSnapshot& renderState)
    {
        const std::size_t key = PipelineCacheKey(SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, depthTest, depthWrite, depthFunc,
                                                  colorFormat, colorTargetCount, sampleCount, depthStencilFormat, renderState);
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
        std::array<SDL_GPUColorTargetDescription, 4> colorTargets{};
        FillColorTargetDescriptions(colorTargets, colorTargetCount, colorFormat, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = spriteVertexShader_;
        pipelineInfo.fragment_shader = spriteFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = 3;
        pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        FillRasterizerState(
            pipelineInfo.rasterizer_state, renderState,
            pipelineInfo.primitive_type);
        pipelineInfo.multisample_state.sample_count = sampleCount;
        // SDLGPU-19: real DepthStencilState mapping -- SpriteBatch.Begin() defaults to
        // DepthStencilState.None (depth test/write both off) when the game passes no explicit
        // state, matching this pipeline's own former hardcoded always-off behavior, but a game CAN
        // now genuinely enable depth-tested sprite layering by passing a different state.
        FillDepthStencilState(pipelineInfo.depth_stencil_state, depthTest, depthWrite, depthFunc, renderState);
        pipelineInfo.target_info.color_target_descriptions = colorTargets.data();
        pipelineInfo.target_info.num_color_targets = static_cast<Uint32>(colorTargetCount);
        pipelineInfo.target_info.has_depth_stencil_target =
            (depthStencilFormat != SDL_GPU_TEXTUREFORMAT_INVALID);
        pipelineInfo.target_info.depth_stencil_format = depthStencilFormat;

        SDL_GPUGraphicsPipeline* pipeline =
            CreateGraphicsPipeline(pipelineInfo,
                                   "CNA SDL_GPU: failed to create sprite pipeline: ");
        return CacheGraphicsPipeline(spritePipelines_, key, pipeline);
    }

    SDL_GPUSampler* SdlGpuRenderer::GetOrCreateSampler(int textureFilter, int addressU,
                                                              int addressV, int maxAnisotropy,
                                                              const char* family)
    {
        const int clampedAniso = std::clamp(maxAnisotropy, 1, 16);
        const std::uint32_t key = SamplerCacheKey(textureFilter, addressU, addressV, clampedAniso);
        const auto it = samplerCache_.find(key);
        const bool hit = it != samplerCache_.end();

        SDL_GPUSamplerCreateInfo createInfo{};
        FillSdlGpuSamplerCreateInfo(createInfo, textureFilter, addressU, addressV, clampedAniso);

        SDL_GPUSampler* sampler = nullptr;
        if (hit)
        {
            sampler = it->second;
        }
        else
        {
            MaybeFailForTest(SdlGpuFailurePointEXT::SamplerCreation);
            sampler = SDL_CreateGPUSampler(device_, &createInfo);
            if (sampler == nullptr)
                throw std::runtime_error(std::string("CNA SDL_GPU: failed to create sampler: ") + SDL_GetError());
            NotifyResourceEvent(SdlGpuResourceKindEXT::Sampler,
                                SdlGpuResourceEventEXT::Acquired);
            samplerCache_[key] = sampler;
        }
        // REMED-GFX-170: the whole public->native translation on one line, so a wrong ordinal
        // mapping is readable directly instead of being inferred from pixels.
        if (SamplerTraceEnabled())
        {
            std::fprintf(stderr,
                         "[cna-sdlgpu-sampler] family=%s filter=%d(%s) mag=%s min=%s mip=%s "
                         "aniso=%s/%.1f addrU=%s addrV=%s key=0x%08x sampler=%p %s\n",
                         family, textureFilter, TextureFilterName(textureFilter),
                         SdlFilterName(createInfo.mag_filter),
                         SdlFilterName(createInfo.min_filter),
                         SdlMipmapModeName(createInfo.mipmap_mode),
                         createInfo.enable_anisotropy ? "on" : "off",
                         static_cast<double>(createInfo.max_anisotropy),
                         SdlAddressModeName(createInfo.address_mode_u),
                         SdlAddressModeName(createInfo.address_mode_v),
                         static_cast<unsigned>(key), static_cast<void*>(sampler),
                         hit ? "HIT" : "CREATE");
        }
        return sampler;
    }

    void SdlGpuRenderer::InitializeSpritePipelineAndSamplerForTestEXT()
    {
        const SDL_GPUTextureFormat colorFormat =
            SDL_GetGPUSwapchainTextureFormat(device_, window_);
        (void)GetOrCreateSpritePipeline(
            colorFormat, SDL_GPU_SAMPLECOUNT_1, depthStencilFormat_, 1,
            /*depthTest=*/false, /*depthWrite=*/false, /*depthFunc=*/3,
            CaptureRenderState());
        (void)GetOrCreateSampler(/*textureFilter=*/0, /*addressU=*/1, /*addressV=*/1,
                                 kSpriteBatchMaxAnisotropy, "SpriteBatch/warmup");
    }

    void SdlGpuRenderer::UploadSpriteVertexData(SDL_GPUCommandBuffer* cmd)
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
        CopyPassOwner copyPassOwner(copyPass);
        SDL_GPUTransferBufferLocation source{};
        source.transfer_buffer = transferBuffer;
        SDL_GPUBufferRegion destRegion{};
        destRegion.buffer = spriteVertexBuffer_;
        destRegion.size = requiredBytes;
        SDL_UploadToGPUBuffer(copyPass, &source, &destRegion, true);
        copyPassOwner.End();

        SDL_ReleaseGPUTransferBuffer(device_, transferBuffer);
    }

    // Adversarial-review finding #4 (draw ordering): the body of the old whole-vector RenderSprites
    // loop, now issuing exactly ONE sprite so RenderQueuedDraws() can interleave it with any other
    // kind in real chronological order. boundPipeline is passed by reference from the caller's own
    // single running variable, so this still skips a redundant rebind across consecutive
    // same-pipeline sprites (SDLGPU-42/43's own rationale, now also true across kind switches).
    void SdlGpuRenderer::IssueSpriteDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                const SpriteCommand& command, std::size_t index,
                                                const float* viewportSize, SDL_GPUTextureFormat colorFormat,
                                                SDL_GPUSampleCount sampleCount,
                                                SDL_GPUTextureFormat depthStencilFormat,
                                                int colorTargetCount,
                                                SDL_GPUGraphicsPipeline*& boundPipeline)
    {
        if (command.customEffect != nullptr)
        {
            SDL_GPUGraphicsPipeline* pipeline = command.customEffect->GetOrCreatePipeline(
                colorFormat, sampleCount, depthStencilFormat, colorTargetCount,
                command.renderState.colorWriteMasks,
                command.renderState.depthBias,
                command.renderState.slopeScaleDepthBias);
            if (pipeline == nullptr)
                return;  // compile/pipeline-creation failure -- skip, matches IsValid()-gated sibling renderers
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
#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
        else if (command.compiledEffect.vertexShader != nullptr)
        {
            // Uniform bytes and (for slots > 0) sampler bindings were already captured at
            // QueueSprite time -- see CompiledEffectBinding's own doc comment for why. The
            // trailing command.texture sampler bind below still runs unconditionally after this
            // branch and overrides slot 0, exactly matching FNA's SpriteBatch.DrawPrimitives,
            // which sets GraphicsDevice.Textures[0] = texture AFTER applying a custom effect's
            // pass -- unconditionally, regardless of what the effect itself bound there.
            BindCompiledEffectForDrawEXT(
                pass, cmd, command.compiledEffect, sizeof(SpriteVertex),
                SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, command.depthTest, command.depthWrite,
                command.depthFunc, command.renderState, colorFormat, sampleCount,
                depthStencilFormat, colorTargetCount, boundPipeline);
        }
#endif
        else
        {
            SDL_GPUGraphicsPipeline* pipeline = GetOrCreateSpritePipeline(
                colorFormat, sampleCount, depthStencilFormat, colorTargetCount, command.depthTest, command.depthWrite,
                command.depthFunc, command.renderState);
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
        samplerBinding.texture = command.texture.texture;
        samplerBinding.sampler = GetOrCreateSampler(command.textureFilter, command.addressU,
                                                   command.addressV, kSpriteBatchMaxAnisotropy,
                                                   "SpriteBatch");
        SDL_BindGPUFragmentSamplers(pass, 0, &samplerBinding, 1);

        SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
    }

    void SdlGpuRenderer::QueueSprite(const ITextureRenderer& texture, const SdlGpuSampledTextureEXT& nativeTexture,
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
                                             SdlGpuEffectRenderer* customEffect,
                                             ICompiledEffectRuntime* compiledEffect)
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
#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
        // plan_fx.md FX-071: the compiled-effect counterpart of the customEffect snapshot above --
        // captured NOW for the same reason (SpriteCommand's own doc comment) and against the same
        // fixed SpriteVertex layout every sprite shares, since SpriteBatch's vertex data is CNA's
        // own generated geometry, never a caller-supplied VertexDeclaration.
        if (compiledEffect != nullptr)
        {
            auto* sdlGpuEffect =
                dynamic_cast<CNA::Internal::Renderers::SdlGpu::SdlGpuCompiledEffect*>(compiledEffect);
            if (sdlGpuEffect == nullptr)
            {
                throw std::runtime_error(
                    "CNA SDL_GPU: the applied compiled effect was not created by this renderer.");
            }
            using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
            using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
            static const std::vector<VertexElement> kSpriteVertexDeclaration = {
                VertexElement(offsetof(SpriteVertex, x), VertexElementFormat::Vector2,
                             VertexElementUsage::Position, 0),
                VertexElement(offsetof(SpriteVertex, u), VertexElementFormat::Vector2,
                             VertexElementUsage::TextureCoordinate, 0),
                VertexElement(offsetof(SpriteVertex, r), VertexElementFormat::Vector4,
                             VertexElementUsage::Color, 0),
            };
            command.compiledEffect =
                BuildCompiledEffectBindingEXT(*sdlGpuEffect, kSpriteVertexDeclaration);
        }
#endif
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

    SDL_GPUPrimitiveType SdlGpuRenderer::ToTopology(PrimitiveType primitive) const
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

    int SdlGpuRenderer::PrimitiveVertexCount(PrimitiveType primitive, int primitiveCount) const
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

    int SdlGpuRenderer::PrimitiveIndexCount(PrimitiveType primitive, int primitiveCount) const
    {
        return PrimitiveVertexCount(primitive, primitiveCount);
    }

    SdlGpuRenderer::NativeIndexedRange SdlGpuRenderer::ResolveIndexedRange(
        const SdlGpuIndexBufferRenderer& ib, const SdlGpuVertexBufferRenderer& vb,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams* params) const
    {
        const int startIndex = params != nullptr ? params->startIndex : 0;
        const int baseVertex = params != nullptr ? params->baseVertex : 0;
        const int minVertexIndex = params != nullptr ? params->minVertexIndex : 0;
        const int numVertices = params != nullptr ? params->numVertices : 0;

        if (primitiveCount <= 0)
        {
            throw System::ArgumentOutOfRangeException(
                "primitiveCount", std::to_string(primitiveCount),
                "CNA SDL_GPU: an indexed draw must consume at least one primitive.");
        }
        if (startIndex < 0)
        {
            throw System::ArgumentOutOfRangeException(
                "startIndex", std::to_string(startIndex),
                "CNA SDL_GPU: startIndex is an index-element offset and cannot be negative.");
        }
        if (baseVertex < 0)
        {
            throw System::ArgumentOutOfRangeException(
                "baseVertex", std::to_string(baseVertex),
                "CNA SDL_GPU: baseVertex cannot be negative.");
        }
        if (minVertexIndex < 0)
        {
            throw System::ArgumentOutOfRangeException(
                "minVertexIndex", std::to_string(minVertexIndex),
                "CNA SDL_GPU: minVertexIndex cannot be negative.");
        }
        if (numVertices < 0)
        {
            throw System::ArgumentOutOfRangeException(
                "numVertices", std::to_string(numVertices),
                "CNA SDL_GPU: numVertices cannot be negative.");
        }

        // Computed in 64-bit so an overflowing request is rejected instead of wrapping, exactly as
        // GraphicsDevice's own CheckedPrimitiveElementCount does for the public entry points.
        std::int64_t consumedIndexCount = 0;
        switch (primitive)
        {
            case PrimitiveType::TriangleList:
                consumedIndexCount = static_cast<std::int64_t>(primitiveCount) * 3;
                break;
            case PrimitiveType::TriangleStrip:
                consumedIndexCount = static_cast<std::int64_t>(primitiveCount) + 2;
                break;
            case PrimitiveType::LineList:
                consumedIndexCount = static_cast<std::int64_t>(primitiveCount) * 2;
                break;
            case PrimitiveType::LineStrip:
                consumedIndexCount = static_cast<std::int64_t>(primitiveCount) + 1;
                break;
            case PrimitiveType::PointListEXT:
                consumedIndexCount = primitiveCount;
                break;
            default:
                throw std::invalid_argument("CNA SDL_GPU: unsupported primitive topology");
        }

        const std::int64_t availableIndexCount = static_cast<std::int64_t>(ib.GetIndexCount());
        if (static_cast<std::int64_t>(startIndex) > availableIndexCount ||
            consumedIndexCount > availableIndexCount - static_cast<std::int64_t>(startIndex))
        {
            throw System::ArgumentOutOfRangeException(
                "primitiveCount", std::to_string(primitiveCount),
                "CNA SDL_GPU: the requested index range exceeds the bound index buffer.");
        }

        // baseVertex shifts every decoded index, so the caller-declared decoded range
        // [minVertexIndex, minVertexIndex + numVertices) must still land inside the bound vertex
        // buffer once shifted. The hints never change addressing; they only bound it. A zero
        // numVertices means the caller declared no range at all -- the DrawUser* path, whose data
        // GraphicsDevice has already copied and rebased -- so only baseVertex itself is checked.
        const std::int64_t availableVertexCount = static_cast<std::int64_t>(vb.GetVertexCount());
        const std::int64_t declaredVertexEnd = static_cast<std::int64_t>(baseVertex) +
                                               static_cast<std::int64_t>(minVertexIndex) +
                                               static_cast<std::int64_t>(numVertices);
        if (static_cast<std::int64_t>(baseVertex) > availableVertexCount ||
            (numVertices > 0 && declaredVertexEnd > availableVertexCount))
        {
            throw System::ArgumentOutOfRangeException(
                "baseVertex", std::to_string(baseVertex),
                "CNA SDL_GPU: the declared vertex range exceeds the bound vertex buffer.");
        }

        // SDL takes num_indices/first_index as Uint32 and vertex_offset as Sint32; reject anything
        // the native argument types cannot represent rather than narrowing it silently.
        if (consumedIndexCount > static_cast<std::int64_t>(std::numeric_limits<Uint32>::max()) ||
            static_cast<std::int64_t>(startIndex) >
                static_cast<std::int64_t>(std::numeric_limits<Uint32>::max()) ||
            static_cast<std::int64_t>(baseVertex) >
                static_cast<std::int64_t>(std::numeric_limits<Sint32>::max()))
        {
            throw System::ArgumentOutOfRangeException(
                "primitiveCount", std::to_string(primitiveCount),
                "CNA SDL_GPU: the requested indexed draw range is too large for SDL_gpu.");
        }

        NativeIndexedRange range;
        range.indexCount = static_cast<Uint32>(consumedIndexCount);
        range.firstIndex = static_cast<Uint32>(startIndex);
        range.vertexOffset = static_cast<Sint32>(baseVertex);
        return range;
    }

    void SdlGpuRenderer::CreateColoredResources(ConstructionResources& resources)
    {
        SDL_GPUShaderCreateInfo vsInfo{};
        vsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kColored3dVertSpv);
        vsInfo.code_size = Shaders::kColored3dVertSpv_size;
        vsInfo.entrypoint = "main";
        vsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        vsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        vsInfo.num_uniform_buffers = 2;  // + FogParams (REMED-GFX-009)
        resources.CreateShader(
            ConstructionShader::ColoredVertex,
            SdlGpuFailurePointEXT::ColoredVertexShaderCreation, vsInfo,
            "CNA SDL_GPU: failed to create colored3d vertex shader: ");

        SDL_GPUShaderCreateInfo fsInfo{};
        fsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kColored3dFragSpv);
        fsInfo.code_size = Shaders::kColored3dFragSpv_size;
        fsInfo.entrypoint = "main";
        fsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        fsInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        resources.CreateShader(
            ConstructionShader::ColoredFragment,
            SdlGpuFailurePointEXT::ColoredFragmentShaderCreation, fsInfo,
            "CNA SDL_GPU: failed to create colored3d fragment shader: ");
    }

    void SdlGpuRenderer::DestroyColoredResources()
    {
        for (auto& [key, pipeline] : coloredPipelines_)
            ReleaseGraphicsPipeline(pipeline);
        coloredPipelines_.clear();
        ReleaseShader(coloredFragmentShader_);
        ReleaseShader(coloredVertexShader_);
    }

    SDL_GPUGraphicsPipeline* SdlGpuRenderer::GetOrCreatePipelineColored3D(
        SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
        SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, const RenderStateSnapshot& renderState)
    {
        const std::size_t key = PipelineCacheKey(
            topology, depthTest, depthWrite, depthFunc, colorFormat, colorTargetCount, sampleCount,
            depthStencilFormat, renderState);
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

        std::array<SDL_GPUColorTargetDescription, 4> colorTargets{};
        FillColorTargetDescriptions(colorTargets, colorTargetCount, colorFormat, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = coloredVertexShader_;
        pipelineInfo.fragment_shader = coloredFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = 2;
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(
            pipelineInfo.rasterizer_state, renderState,
            pipelineInfo.primitive_type);  // SDLGPU-20 / REMED-GFX-051
        pipelineInfo.multisample_state.sample_count = sampleCount;
        FillDepthStencilState(pipelineInfo.depth_stencil_state, depthTest, depthWrite, depthFunc, renderState);  // SDLGPU-19
        pipelineInfo.target_info.color_target_descriptions = colorTargets.data();
        pipelineInfo.target_info.num_color_targets = static_cast<Uint32>(colorTargetCount);
        pipelineInfo.target_info.has_depth_stencil_target =
            (depthStencilFormat != SDL_GPU_TEXTUREFORMAT_INVALID);
        pipelineInfo.target_info.depth_stencil_format = depthStencilFormat;

        SDL_GPUGraphicsPipeline* pipeline =
            CreateGraphicsPipeline(pipelineInfo,
                                   "CNA SDL_GPU: failed to create colored3d pipeline: ");
        return CacheGraphicsPipeline(coloredPipelines_, key, pipeline);
    }

    void SdlGpuRenderer::CreateTexturedResources(ConstructionResources& resources)
    {
        SDL_GPUShaderCreateInfo vsInfo{};
        vsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kTextured3dVertSpv);
        vsInfo.code_size = Shaders::kTextured3dVertSpv_size;
        vsInfo.entrypoint = "main";
        vsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        vsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        vsInfo.num_uniform_buffers = 2;  // + FogParams (REMED-GFX-009)
        resources.CreateShader(
            ConstructionShader::TexturedVertex,
            SdlGpuFailurePointEXT::TexturedVertexShaderCreation, vsInfo,
            "CNA SDL_GPU: failed to create textured3d vertex shader: ");

        SDL_GPUShaderCreateInfo cvsInfo{};
        cvsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kColoredTextured3dVertSpv);
        cvsInfo.code_size = Shaders::kColoredTextured3dVertSpv_size;
        cvsInfo.entrypoint = "main";
        cvsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        cvsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        cvsInfo.num_uniform_buffers = 2;  // + FogParams (REMED-GFX-009)
        resources.CreateShader(
            ConstructionShader::ColoredTexturedVertex,
            SdlGpuFailurePointEXT::ColoredTexturedVertexShaderCreation, cvsInfo,
            "CNA SDL_GPU: failed to create colored_textured3d vertex shader: ");

        SDL_GPUShaderCreateInfo fsInfo{};
        fsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kTextured3dFragSpv);
        fsInfo.code_size = Shaders::kTextured3dFragSpv_size;
        fsInfo.entrypoint = "main";
        fsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        fsInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        fsInfo.num_samplers = 1;
        fsInfo.num_uniform_buffers = 1;
        resources.CreateShader(
            ConstructionShader::TexturedFragment,
            SdlGpuFailurePointEXT::TexturedFragmentShaderCreation, fsInfo,
            "CNA SDL_GPU: failed to create textured3d fragment shader: ");
    }

    void SdlGpuRenderer::DestroyTexturedResources()
    {
        for (auto& [key, pipeline] : texturedPipelines_)
            ReleaseGraphicsPipeline(pipeline);
        texturedPipelines_.clear();
        for (auto& [key, pipeline] : coloredTexturedPipelines_)
            ReleaseGraphicsPipeline(pipeline);
        coloredTexturedPipelines_.clear();
        ReleaseShader(texturedFragmentShader_);
        ReleaseShader(coloredTexturedVertexShader_);
        ReleaseShader(texturedVertexShader_);
    }

    SDL_GPUGraphicsPipeline* SdlGpuRenderer::GetOrCreatePipelineTextured3D(
        SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
        SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, const RenderStateSnapshot& renderState)
    {
        const std::size_t key = PipelineCacheKey(
            topology, depthTest, depthWrite, depthFunc, colorFormat, colorTargetCount, sampleCount,
            depthStencilFormat, renderState);
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

        std::array<SDL_GPUColorTargetDescription, 4> colorTargets{};
        FillColorTargetDescriptions(colorTargets, colorTargetCount, colorFormat, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = texturedVertexShader_;
        pipelineInfo.fragment_shader = texturedFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = 2;
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(
            pipelineInfo.rasterizer_state, renderState,
            pipelineInfo.primitive_type);
        pipelineInfo.multisample_state.sample_count = sampleCount;
        FillDepthStencilState(pipelineInfo.depth_stencil_state, depthTest, depthWrite, depthFunc, renderState);
        pipelineInfo.target_info.color_target_descriptions = colorTargets.data();
        pipelineInfo.target_info.num_color_targets = static_cast<Uint32>(colorTargetCount);
        pipelineInfo.target_info.has_depth_stencil_target =
            (depthStencilFormat != SDL_GPU_TEXTUREFORMAT_INVALID);
        pipelineInfo.target_info.depth_stencil_format = depthStencilFormat;

        SDL_GPUGraphicsPipeline* pipeline =
            CreateGraphicsPipeline(pipelineInfo,
                                   "CNA SDL_GPU: failed to create textured3d pipeline: ");
        return CacheGraphicsPipeline(texturedPipelines_, key, pipeline);
    }

    SDL_GPUGraphicsPipeline* SdlGpuRenderer::GetOrCreatePipelineColoredTextured3D(
        SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
        SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, const RenderStateSnapshot& renderState)
    {
        const std::size_t key = PipelineCacheKey(
            topology, depthTest, depthWrite, depthFunc, colorFormat, colorTargetCount, sampleCount,
            depthStencilFormat, renderState);
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

        std::array<SDL_GPUColorTargetDescription, 4> colorTargets{};
        FillColorTargetDescriptions(colorTargets, colorTargetCount, colorFormat, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = coloredTexturedVertexShader_;
        pipelineInfo.fragment_shader = texturedFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = 3;
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(
            pipelineInfo.rasterizer_state, renderState,
            pipelineInfo.primitive_type);
        pipelineInfo.multisample_state.sample_count = sampleCount;
        FillDepthStencilState(pipelineInfo.depth_stencil_state, depthTest, depthWrite, depthFunc, renderState);
        pipelineInfo.target_info.color_target_descriptions = colorTargets.data();
        pipelineInfo.target_info.num_color_targets = static_cast<Uint32>(colorTargetCount);
        pipelineInfo.target_info.has_depth_stencil_target =
            (depthStencilFormat != SDL_GPU_TEXTUREFORMAT_INVALID);
        pipelineInfo.target_info.depth_stencil_format = depthStencilFormat;

        SDL_GPUGraphicsPipeline* pipeline =
            CreateGraphicsPipeline(
                pipelineInfo,
                "CNA SDL_GPU: failed to create colored_textured3d pipeline: ");
        return CacheGraphicsPipeline(coloredTexturedPipelines_, key, pipeline);
    }

    void SdlGpuRenderer::CreateLitTexturedResources(ConstructionResources& resources)
    {
        SDL_GPUShaderCreateInfo vsInfo{};
        vsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kLitTextured3dVertSpv);
        vsInfo.code_size = Shaders::kLitTextured3dVertSpv_size;
        vsInfo.entrypoint = "main";
        vsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        vsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        vsInfo.num_uniform_buffers = 3;  // + FogParams (REMED-GFX-009)
        resources.CreateShader(
            ConstructionShader::LitTexturedVertex,
            SdlGpuFailurePointEXT::LitTexturedVertexShaderCreation, vsInfo,
            "CNA SDL_GPU: failed to create lit_textured3d vertex shader: ");

        SDL_GPUShaderCreateInfo fsInfo{};
        fsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kLitTextured3dFragSpv);
        fsInfo.code_size = Shaders::kLitTextured3dFragSpv_size;
        fsInfo.entrypoint = "main";
        fsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        fsInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        fsInfo.num_samplers = 1;
        fsInfo.num_uniform_buffers = 2;
        resources.CreateShader(
            ConstructionShader::LitTexturedFragment,
            SdlGpuFailurePointEXT::LitTexturedFragmentShaderCreation, fsInfo,
            "CNA SDL_GPU: failed to create lit_textured3d fragment shader: ");
    }

    void SdlGpuRenderer::DestroyLitTexturedResources()
    {
        for (auto& [key, pipeline] : litTexturedPipelines_)
            ReleaseGraphicsPipeline(pipeline);
        litTexturedPipelines_.clear();
        ReleaseShader(litTexturedFragmentShader_);
        ReleaseShader(litTexturedVertexShader_);
    }

    SDL_GPUGraphicsPipeline* SdlGpuRenderer::GetOrCreatePipelineLitTextured3D(
        SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
        SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, const RenderStateSnapshot& renderState)
    {
        const std::size_t key = PipelineCacheKey(
            topology, depthTest, depthWrite, depthFunc, colorFormat, colorTargetCount, sampleCount,
            depthStencilFormat, renderState);
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

        std::array<SDL_GPUColorTargetDescription, 4> colorTargets{};
        FillColorTargetDescriptions(colorTargets, colorTargetCount, colorFormat, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = litTexturedVertexShader_;
        pipelineInfo.fragment_shader = litTexturedFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = 3;
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(
            pipelineInfo.rasterizer_state, renderState,
            pipelineInfo.primitive_type);
        pipelineInfo.multisample_state.sample_count = sampleCount;
        FillDepthStencilState(pipelineInfo.depth_stencil_state, depthTest, depthWrite, depthFunc, renderState);
        pipelineInfo.target_info.color_target_descriptions = colorTargets.data();
        pipelineInfo.target_info.num_color_targets = static_cast<Uint32>(colorTargetCount);
        pipelineInfo.target_info.has_depth_stencil_target =
            (depthStencilFormat != SDL_GPU_TEXTUREFORMAT_INVALID);
        pipelineInfo.target_info.depth_stencil_format = depthStencilFormat;

        SDL_GPUGraphicsPipeline* pipeline =
            CreateGraphicsPipeline(
                pipelineInfo,
                "CNA SDL_GPU: failed to create lit_textured3d pipeline: ");
        return CacheGraphicsPipeline(litTexturedPipelines_, key, pipeline);
    }

    void SdlGpuRenderer::QueueColoredDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                                  PrimitiveType primitive, int primitiveCount,
                                                  const GpuDrawParams* params)
    {
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferRenderer&>(vb);
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
            const auto& sdlGpuIb = static_cast<const SdlGpuIndexBufferRenderer&>(*ib);
            command.indexed = true;
            command.index32 = sdlGpuIb.IsThirtyTwoBit();
            command.indexData = sdlGpuIb.ShadowData();
            // REMED-GFX-117: startIndex/baseVertex reach the native draw through the one
            // shared resolver, never a default-zero literal.
            ApplyIndexedRange(
                command, sdlGpuIb, sdlGpuVb, primitive, primitiveCount, params);
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

    void SdlGpuRenderer::QueueTexturedDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                                   PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferRenderer&>(vb);
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
        command.texture = ResolveSampledTextureEXT(params.texture0, "BasicEffect.Texture");
        // SDLGPU-21: real per-slot dynamic sampler state (GraphicsDevice.SamplerStates[0]).
        command.textureFilter = samplerSlots_[0].filter;
        command.addressU = samplerSlots_[0].addressU;
        command.addressV = samplerSlots_[0].addressV;
        command.maxAnisotropy = samplerSlots_[0].maxAnisotropy;  // REMED-GFX-170

        if (ib != nullptr)
        {
            const auto& sdlGpuIb = static_cast<const SdlGpuIndexBufferRenderer&>(*ib);
            command.indexed = true;
            command.index32 = sdlGpuIb.IsThirtyTwoBit();
            command.indexData = sdlGpuIb.ShadowData();
            ApplyIndexedRange(
                command, sdlGpuIb, sdlGpuVb, primitive, primitiveCount, &params);
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

    void SdlGpuRenderer::QueueLitTexturedDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                                      const Matrix& world, const Matrix& view, const Matrix& projection,
                                                      PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferRenderer&>(vb);
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
        command.texture = ResolveSampledTextureEXT(params.texture0, "BasicEffect.Texture (lit)");
        command.textureFilter = samplerSlots_[0].filter;
        command.addressU = samplerSlots_[0].addressU;
        command.addressV = samplerSlots_[0].addressV;
        command.maxAnisotropy = samplerSlots_[0].maxAnisotropy;  // REMED-GFX-170

        if (ib != nullptr)
        {
            const auto& sdlGpuIb = static_cast<const SdlGpuIndexBufferRenderer&>(*ib);
            command.indexed = true;
            command.index32 = sdlGpuIb.IsThirtyTwoBit();
            command.indexData = sdlGpuIb.ShadowData();
            ApplyIndexedRange(
                command, sdlGpuIb, sdlGpuVb, primitive, primitiveCount, &params);
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

    void SdlGpuRenderer::CreateAlphaTestResources(ConstructionResources& resources)
    {
        SDL_GPUShaderCreateInfo vsInfo{};
        vsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kAlphaTest3dVertSpv);
        vsInfo.code_size = Shaders::kAlphaTest3dVertSpv_size;
        vsInfo.entrypoint = "main";
        vsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        vsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        vsInfo.num_uniform_buffers = 2;  // + FogParams (REMED-GFX-009)
        resources.CreateShader(
            ConstructionShader::AlphaTestVertex,
            SdlGpuFailurePointEXT::AlphaTestVertexShaderCreation, vsInfo,
            "CNA SDL_GPU: failed to create alpha_test3d vertex shader: ");

        SDL_GPUShaderCreateInfo cvsInfo{};
        cvsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kAlphaTestColored3dVertSpv);
        cvsInfo.code_size = Shaders::kAlphaTestColored3dVertSpv_size;
        cvsInfo.entrypoint = "main";
        cvsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        cvsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        cvsInfo.num_uniform_buffers = 2;  // + FogParams (REMED-GFX-009)
        resources.CreateShader(
            ConstructionShader::AlphaTestColoredVertex,
            SdlGpuFailurePointEXT::AlphaTestColoredVertexShaderCreation, cvsInfo,
            "CNA SDL_GPU: failed to create alpha_test_colored3d vertex shader: ");

        SDL_GPUShaderCreateInfo fsInfo{};
        fsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kAlphaTest3dFragSpv);
        fsInfo.code_size = Shaders::kAlphaTest3dFragSpv_size;
        fsInfo.entrypoint = "main";
        fsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        fsInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        fsInfo.num_samplers = 1;
        fsInfo.num_uniform_buffers = 1;
        resources.CreateShader(
            ConstructionShader::AlphaTestFragment,
            SdlGpuFailurePointEXT::AlphaTestFragmentShaderCreation, fsInfo,
            "CNA SDL_GPU: failed to create alpha_test3d fragment shader: ");
    }

    void SdlGpuRenderer::DestroyAlphaTestResources()
    {
        for (auto& [key, pipeline] : alphaTestPipelines_)
            ReleaseGraphicsPipeline(pipeline);
        alphaTestPipelines_.clear();
        for (auto& [key, pipeline] : alphaTestColoredPipelines_)
            ReleaseGraphicsPipeline(pipeline);
        alphaTestColoredPipelines_.clear();
        ReleaseShader(alphaTestFragmentShader_);
        ReleaseShader(alphaTestColoredVertexShader_);
        ReleaseShader(alphaTestVertexShader_);
    }

    SDL_GPUGraphicsPipeline* SdlGpuRenderer::GetOrCreatePipelineAlphaTest3D(
        std::size_t stride, SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
        SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, const RenderStateSnapshot& renderState)
    {
        // stride 24 (vertex colour) uses its own dedicated map, keyed the same way every other
        // stride-specific map here is. strides 20/32 share alphaTestVertexShader_ but need
        // DIFFERENT vertex_input_state (different attribute offsets), so that map's key folds in
        // the stride explicitly.
        if (stride == 24)
        {
            const std::size_t key = PipelineCacheKey(
                topology, depthTest, depthWrite, depthFunc, colorFormat, colorTargetCount, sampleCount,
                depthStencilFormat, renderState);
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

            std::array<SDL_GPUColorTargetDescription, 4> colorTargets{};
            FillColorTargetDescriptions(colorTargets, colorTargetCount, colorFormat, renderState);

            SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.vertex_shader = alphaTestColoredVertexShader_;
            pipelineInfo.fragment_shader = alphaTestFragmentShader_;
            pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
            pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
            pipelineInfo.vertex_input_state.vertex_attributes = attrs;
            pipelineInfo.vertex_input_state.num_vertex_attributes = 3;
            pipelineInfo.primitive_type = topology;
            FillRasterizerState(
                pipelineInfo.rasterizer_state, renderState,
                pipelineInfo.primitive_type);
            pipelineInfo.multisample_state.sample_count = sampleCount;
            FillDepthStencilState(pipelineInfo.depth_stencil_state, depthTest, depthWrite, depthFunc, renderState);
            pipelineInfo.target_info.color_target_descriptions = colorTargets.data();
            pipelineInfo.target_info.num_color_targets = static_cast<Uint32>(colorTargetCount);
            pipelineInfo.target_info.has_depth_stencil_target =
                (depthStencilFormat != SDL_GPU_TEXTUREFORMAT_INVALID);
            pipelineInfo.target_info.depth_stencil_format = depthStencilFormat;

            SDL_GPUGraphicsPipeline* pipeline =
                CreateGraphicsPipeline(
                    pipelineInfo,
                    "CNA SDL_GPU: failed to create alpha_test_colored3d pipeline: ");
            return CacheGraphicsPipeline(alphaTestColoredPipelines_, key, pipeline);
        }

        const std::size_t key = HashCombine(static_cast<std::size_t>(stride),
            PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, colorFormat, colorTargetCount, sampleCount,
                             depthStencilFormat, renderState));
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

        std::array<SDL_GPUColorTargetDescription, 4> colorTargets{};
        FillColorTargetDescriptions(colorTargets, colorTargetCount, colorFormat, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = alphaTestVertexShader_;
        pipelineInfo.fragment_shader = alphaTestFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = 2;
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(
            pipelineInfo.rasterizer_state, renderState,
            pipelineInfo.primitive_type);
        pipelineInfo.multisample_state.sample_count = sampleCount;
        FillDepthStencilState(pipelineInfo.depth_stencil_state, depthTest, depthWrite, depthFunc, renderState);
        pipelineInfo.target_info.color_target_descriptions = colorTargets.data();
        pipelineInfo.target_info.num_color_targets = static_cast<Uint32>(colorTargetCount);
        pipelineInfo.target_info.has_depth_stencil_target =
            (depthStencilFormat != SDL_GPU_TEXTUREFORMAT_INVALID);
        pipelineInfo.target_info.depth_stencil_format = depthStencilFormat;

        SDL_GPUGraphicsPipeline* pipeline =
            CreateGraphicsPipeline(
                pipelineInfo,
                "CNA SDL_GPU: failed to create alpha_test3d pipeline: ");
        return CacheGraphicsPipeline(alphaTestPipelines_, key, pipeline);
    }

    void SdlGpuRenderer::CreateDualTextureResources(ConstructionResources& resources)
    {
        SDL_GPUShaderCreateInfo vsInfo{};
        vsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kDualTexture3dVertSpv);
        vsInfo.code_size = Shaders::kDualTexture3dVertSpv_size;
        vsInfo.entrypoint = "main";
        vsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        vsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        vsInfo.num_uniform_buffers = 2;  // + FogParams (REMED-GFX-009)
        resources.CreateShader(
            ConstructionShader::DualTextureVertex,
            SdlGpuFailurePointEXT::DualTextureVertexShaderCreation, vsInfo,
            "CNA SDL_GPU: failed to create dual_texture3d vertex shader: ");

        SDL_GPUShaderCreateInfo cvsInfo{};
        cvsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kDualTextureColored3dVertSpv);
        cvsInfo.code_size = Shaders::kDualTextureColored3dVertSpv_size;
        cvsInfo.entrypoint = "main";
        cvsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        cvsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        cvsInfo.num_uniform_buffers = 2;  // + FogParams (REMED-GFX-009)
        resources.CreateShader(
            ConstructionShader::DualTextureColoredVertex,
            SdlGpuFailurePointEXT::DualTextureColoredVertexShaderCreation, cvsInfo,
            "CNA SDL_GPU: failed to create dual_texture_colored3d vertex shader: ");

        SDL_GPUShaderCreateInfo fsInfo{};
        fsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kDualTexture3dFragSpv);
        fsInfo.code_size = Shaders::kDualTexture3dFragSpv_size;
        fsInfo.entrypoint = "main";
        fsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        fsInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        fsInfo.num_samplers = 2;
        resources.CreateShader(
            ConstructionShader::DualTextureFragment,
            SdlGpuFailurePointEXT::DualTextureFragmentShaderCreation, fsInfo,
            "CNA SDL_GPU: failed to create dual_texture3d fragment shader: ");
    }

    void SdlGpuRenderer::DestroyDualTextureResources()
    {
        for (auto& [key, pipeline] : dualTexturePipelines_)
            ReleaseGraphicsPipeline(pipeline);
        dualTexturePipelines_.clear();
        for (auto& [key, pipeline] : dualTextureColoredPipelines_)
            ReleaseGraphicsPipeline(pipeline);
        dualTextureColoredPipelines_.clear();
        ReleaseShader(dualTextureFragmentShader_);
        ReleaseShader(dualTextureColoredVertexShader_);
        ReleaseShader(dualTextureVertexShader_);
    }

    SDL_GPUGraphicsPipeline* SdlGpuRenderer::GetOrCreatePipelineDualTexture3D(
        std::size_t stride, SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
        SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, const RenderStateSnapshot& renderState)
    {
        auto& cache = (stride == 24) ? dualTextureColoredPipelines_ : dualTexturePipelines_;
        const std::size_t key = PipelineCacheKey(
            topology, depthTest, depthWrite, depthFunc, colorFormat, colorTargetCount, sampleCount,
            depthStencilFormat, renderState);
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

        std::array<SDL_GPUColorTargetDescription, 4> colorTargets{};
        FillColorTargetDescriptions(colorTargets, colorTargetCount, colorFormat, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = (stride == 24) ? dualTextureColoredVertexShader_ : dualTextureVertexShader_;
        pipelineInfo.fragment_shader = dualTextureFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = numAttrs;
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(
            pipelineInfo.rasterizer_state, renderState,
            pipelineInfo.primitive_type);
        pipelineInfo.multisample_state.sample_count = sampleCount;
        FillDepthStencilState(pipelineInfo.depth_stencil_state, depthTest, depthWrite, depthFunc, renderState);
        pipelineInfo.target_info.color_target_descriptions = colorTargets.data();
        pipelineInfo.target_info.num_color_targets = static_cast<Uint32>(colorTargetCount);
        pipelineInfo.target_info.has_depth_stencil_target =
            (depthStencilFormat != SDL_GPU_TEXTUREFORMAT_INVALID);
        pipelineInfo.target_info.depth_stencil_format = depthStencilFormat;

        SDL_GPUGraphicsPipeline* pipeline =
            CreateGraphicsPipeline(
                pipelineInfo,
                "CNA SDL_GPU: failed to create dual_texture3d pipeline: ");
        return CacheGraphicsPipeline(cache, key, pipeline);
    }

    void SdlGpuRenderer::CreateEnvMapResources(ConstructionResources& resources)
    {
        SDL_GPUShaderCreateInfo vsInfo{};
        vsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kEnvMap3dVertSpv);
        vsInfo.code_size = Shaders::kEnvMap3dVertSpv_size;
        vsInfo.entrypoint = "main";
        vsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        vsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        vsInfo.num_uniform_buffers = 3;  // + FogParams (REMED-GFX-009)
        resources.CreateShader(
            ConstructionShader::EnvMapVertex,
            SdlGpuFailurePointEXT::EnvMapVertexShaderCreation, vsInfo,
            "CNA SDL_GPU: failed to create env_map3d vertex shader: ");

        SDL_GPUShaderCreateInfo fsInfo{};
        fsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kEnvMap3dFragSpv);
        fsInfo.code_size = Shaders::kEnvMap3dFragSpv_size;
        fsInfo.entrypoint = "main";
        fsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        fsInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        fsInfo.num_samplers = 2;  // uTexture (2D) + uEnvMap (cube)
        fsInfo.num_uniform_buffers = 2;
        resources.CreateShader(
            ConstructionShader::EnvMapFragment,
            SdlGpuFailurePointEXT::EnvMapFragmentShaderCreation, fsInfo,
            "CNA SDL_GPU: failed to create env_map3d fragment shader: ");
    }

    void SdlGpuRenderer::DestroyEnvMapResources()
    {
        for (auto& [key, pipeline] : envMapPipelines_)
            ReleaseGraphicsPipeline(pipeline);
        envMapPipelines_.clear();
        ReleaseShader(envMapFragmentShader_);
        ReleaseShader(envMapVertexShader_);
    }

    SDL_GPUGraphicsPipeline* SdlGpuRenderer::GetOrCreatePipelineEnvMap3D(
        SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
        SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, const RenderStateSnapshot& renderState)
    {
        const std::size_t key = PipelineCacheKey(
            topology, depthTest, depthWrite, depthFunc, colorFormat, colorTargetCount, sampleCount,
            depthStencilFormat, renderState);
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

        std::array<SDL_GPUColorTargetDescription, 4> colorTargets{};
        FillColorTargetDescriptions(colorTargets, colorTargetCount, colorFormat, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = envMapVertexShader_;
        pipelineInfo.fragment_shader = envMapFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = 3;
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(
            pipelineInfo.rasterizer_state, renderState,
            pipelineInfo.primitive_type);
        pipelineInfo.multisample_state.sample_count = sampleCount;
        FillDepthStencilState(pipelineInfo.depth_stencil_state, depthTest, depthWrite, depthFunc, renderState);
        pipelineInfo.target_info.color_target_descriptions = colorTargets.data();
        pipelineInfo.target_info.num_color_targets = static_cast<Uint32>(colorTargetCount);
        pipelineInfo.target_info.has_depth_stencil_target =
            (depthStencilFormat != SDL_GPU_TEXTUREFORMAT_INVALID);
        pipelineInfo.target_info.depth_stencil_format = depthStencilFormat;

        SDL_GPUGraphicsPipeline* pipeline =
            CreateGraphicsPipeline(
                pipelineInfo,
                "CNA SDL_GPU: failed to create env_map3d pipeline: ");
        return CacheGraphicsPipeline(envMapPipelines_, key, pipeline);
    }

    void SdlGpuRenderer::CreateSkinnedResources(ConstructionResources& resources)
    {
        SDL_GPUShaderCreateInfo vsInfo{};
        vsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kSkinned3dVertSpv);
        vsInfo.code_size = Shaders::kSkinned3dVertSpv_size;
        vsInfo.entrypoint = "main";
        vsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        vsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        vsInfo.num_uniform_buffers = 3;  // PC, SkinnedLightParams + FogParams (REMED-GFX-009)
        vsInfo.num_storage_buffers = 1;  // BoneBlock (4608 bytes) -- see SkinnedDrawCommand's doc comment
        resources.CreateShader(
            ConstructionShader::SkinnedVertex,
            SdlGpuFailurePointEXT::SkinnedVertexShaderCreation, vsInfo,
            "CNA SDL_GPU: failed to create skinned3d vertex shader: ");

        SDL_GPUShaderCreateInfo colVsInfo{};
        colVsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kSkinnedColored3dVertSpv);
        colVsInfo.code_size = Shaders::kSkinnedColored3dVertSpv_size;
        colVsInfo.entrypoint = "main";
        colVsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        colVsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        colVsInfo.num_uniform_buffers = 3;  // PC, SkinnedLightParams + FogParams (REMED-GFX-009)
        colVsInfo.num_storage_buffers = 1;  // BoneBlock
        resources.CreateShader(
            ConstructionShader::SkinnedColoredVertex,
            SdlGpuFailurePointEXT::SkinnedColoredVertexShaderCreation, colVsInfo,
            "CNA SDL_GPU: failed to create skinned_colored3d vertex shader: ");

        SDL_GPUShaderCreateInfo colFsInfo{};
        colFsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kSkinnedColored3dFragSpv);
        colFsInfo.code_size = Shaders::kSkinnedColored3dFragSpv_size;
        colFsInfo.entrypoint = "main";
        colFsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        colFsInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        colFsInfo.num_samplers = 1;
        colFsInfo.num_uniform_buffers = 2;
        resources.CreateShader(
            ConstructionShader::SkinnedColoredFragment,
            SdlGpuFailurePointEXT::SkinnedColoredFragmentShaderCreation, colFsInfo,
            "CNA SDL_GPU: failed to create skinned_colored3d fragment shader: ");
    }

    void SdlGpuRenderer::DestroySkinnedResources()
    {
        for (auto& [key, pipeline] : skinnedPipelines_)
            ReleaseGraphicsPipeline(pipeline);
        skinnedPipelines_.clear();
        for (auto& [key, pipeline] : skinnedColoredPipelines_)
            ReleaseGraphicsPipeline(pipeline);
        skinnedColoredPipelines_.clear();
        ReleaseShader(skinnedColoredFragmentShader_);
        ReleaseShader(skinnedColoredVertexShader_);
        ReleaseShader(skinnedVertexShader_);
    }

    SDL_GPUGraphicsPipeline* SdlGpuRenderer::GetOrCreatePipelineSkinned3D(
        bool hasVertexColor, SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
        SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, const RenderStateSnapshot& renderState)
    {
        const std::size_t key = PipelineCacheKey(
            topology, depthTest, depthWrite, depthFunc, colorFormat, colorTargetCount, sampleCount,
            depthStencilFormat, renderState);
        auto& cache = hasVertexColor ? skinnedColoredPipelines_ : skinnedPipelines_;
        const auto it = cache.find(key);
        if (it != cache.end())
            return it->second;

        // Stride 52: VertexPositionNormalTextureSkinned -- pos(12) + normal(12) + uv(8) +
        // blendWeight(16) + blendIndices(4, UBYTE4, non-normalized -> uvec4 in the shader).
        // Stride 56 appends a normalized ubyte4 Color at offset 52 (location 5), matching
        // EasyGLRenderer::ApplyLayout's stride==56 case exactly.
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

        std::array<SDL_GPUColorTargetDescription, 4> colorTargets{};
        FillColorTargetDescriptions(colorTargets, colorTargetCount, colorFormat, renderState);

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
        FillRasterizerState(
            pipelineInfo.rasterizer_state, renderState,
            pipelineInfo.primitive_type);
        pipelineInfo.multisample_state.sample_count = sampleCount;
        FillDepthStencilState(pipelineInfo.depth_stencil_state, depthTest, depthWrite, depthFunc, renderState);
        pipelineInfo.target_info.color_target_descriptions = colorTargets.data();
        pipelineInfo.target_info.num_color_targets = static_cast<Uint32>(colorTargetCount);
        pipelineInfo.target_info.has_depth_stencil_target =
            (depthStencilFormat != SDL_GPU_TEXTUREFORMAT_INVALID);
        pipelineInfo.target_info.depth_stencil_format = depthStencilFormat;

        SDL_GPUGraphicsPipeline* pipeline =
            CreateGraphicsPipeline(
                pipelineInfo,
                "CNA SDL_GPU: failed to create skinned3d pipeline: ");
        return CacheGraphicsPipeline(cache, key, pipeline);
    }

    void SdlGpuRenderer::EnsureDefaultPbrTextures()
    {
        if (defaultWhiteTexture_ != nullptr && defaultFlatNormalTexture_ != nullptr)
            return;

        struct StagedDefaultTexture
        {
            std::unique_ptr<SdlGpuTextureRenderer> texture;
            SdlGpuTestHooksEXT hooks;

            ~StagedDefaultTexture()
            {
                if (texture != nullptr)
                {
                    texture.reset();
                    NotifyResource(hooks, SdlGpuResourceKindEXT::DefaultTexture,
                                   SdlGpuResourceEventEXT::Released);
                }
            }
        };

        StagedDefaultTexture whiteStage{{}, testHooks_};
        StagedDefaultTexture flatNormalStage{{}, testHooks_};

        if (defaultWhiteTexture_ == nullptr)
        {
            MaybeFailForTest(SdlGpuFailurePointEXT::DefaultWhiteTextureCreation);
            ImageData white{1, 1, {255, 255, 255, 255}, 1};
            whiteStage.texture = std::make_unique<SdlGpuTextureRenderer>(*this, white);
            NotifyResourceEvent(SdlGpuResourceKindEXT::DefaultTexture,
                                SdlGpuResourceEventEXT::Acquired);
        }
        if (defaultFlatNormalTexture_ == nullptr)
        {
            MaybeFailForTest(SdlGpuFailurePointEXT::DefaultFlatNormalTextureCreation);
            // (128,128,255,255) decodes (via the shader's rgb*2-1) to a tangent-space normal of
            // ~(0,0,1) -- the unperturbed geometric normal -- mirrors EasyGLRenderer::
            // EnsureDefaultFlatNormalTexture()'s identical encoding exactly.
            ImageData flatNormal{1, 1, {128, 128, 255, 255}, 1};
            flatNormalStage.texture =
                std::make_unique<SdlGpuTextureRenderer>(*this, flatNormal);
            NotifyResourceEvent(SdlGpuResourceKindEXT::DefaultTexture,
                                SdlGpuResourceEventEXT::Acquired);
        }

        if (whiteStage.texture != nullptr)
            defaultWhiteTexture_ = std::move(whiteStage.texture);
        if (flatNormalStage.texture != nullptr)
            defaultFlatNormalTexture_ = std::move(flatNormalStage.texture);
    }

    void SdlGpuRenderer::CreatePbrResources(ConstructionResources& resources)
    {
        SDL_GPUShaderCreateInfo vsInfo{};
        vsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kPbr3dVertSpv);
        vsInfo.code_size = Shaders::kPbr3dVertSpv_size;
        vsInfo.entrypoint = "main";
        vsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        vsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        vsInfo.num_uniform_buffers = 3;  // PC, LitLightParams + FogParams (REMED-GFX-009)
        resources.CreateShader(
            ConstructionShader::PbrVertex,
            SdlGpuFailurePointEXT::PbrVertexShaderCreation, vsInfo,
            "CNA SDL_GPU: failed to create pbr3d vertex shader: ");

        SDL_GPUShaderCreateInfo skinnedVsInfo{};
        skinnedVsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kPbrSkinned3dVertSpv);
        skinnedVsInfo.code_size = Shaders::kPbrSkinned3dVertSpv_size;
        skinnedVsInfo.entrypoint = "main";
        skinnedVsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        skinnedVsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        skinnedVsInfo.num_uniform_buffers = 3;  // PC, SkinnedLightParams + FogParams (REMED-GFX-009)
        skinnedVsInfo.num_storage_buffers = 1;  // BoneBlock
        resources.CreateShader(
            ConstructionShader::PbrSkinnedVertex,
            SdlGpuFailurePointEXT::PbrSkinnedVertexShaderCreation, skinnedVsInfo,
            "CNA SDL_GPU: failed to create pbr_skinned3d vertex shader: ");

        SDL_GPUShaderCreateInfo fsInfo{};
        fsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kPbr3dFragSpv);
        fsInfo.code_size = Shaders::kPbr3dFragSpv_size;
        fsInfo.entrypoint = "main";
        fsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        fsInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        fsInfo.num_samplers = 7;  // core five plus specular strength and colour
        fsInfo.num_uniform_buffers = 3;  // PC, LitLightParams, PbrParams
        resources.CreateShader(
            ConstructionShader::PbrFragment,
            SdlGpuFailurePointEXT::PbrFragmentShaderCreation, fsInfo,
            "CNA SDL_GPU: failed to create pbr3d fragment shader: ");
    }

    void SdlGpuRenderer::DestroyPbrResources()
    {
        for (auto& [key, pipeline] : pbrPipelines_)
            ReleaseGraphicsPipeline(pipeline);
        pbrPipelines_.clear();
        for (auto& [key, pipeline] : pbrSkinnedPipelines_)
            ReleaseGraphicsPipeline(pipeline);
        pbrSkinnedPipelines_.clear();
        ReleaseShader(pbrFragmentShader_);
        ReleaseShader(pbrSkinnedVertexShader_);
        ReleaseShader(pbrVertexShader_);
        // Destroyed here (not left to ~SdlGpuRenderer()'s generic pendingTextureReleases_
        // sweep) since these are owned SdlGpuTextureRenderer instances, not raw handles.
        if (defaultFlatNormalTexture_ != nullptr)
        {
            defaultFlatNormalTexture_.reset();
            NotifyResourceEvent(SdlGpuResourceKindEXT::DefaultTexture,
                                SdlGpuResourceEventEXT::Released);
        }
        if (defaultWhiteTexture_ != nullptr)
        {
            defaultWhiteTexture_.reset();
            NotifyResourceEvent(SdlGpuResourceKindEXT::DefaultTexture,
                                SdlGpuResourceEventEXT::Released);
        }
    }

    SDL_GPUGraphicsPipeline* SdlGpuRenderer::GetOrCreatePipelinePbr3D(
        bool skinned, SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
        SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, const RenderStateSnapshot& renderState)
    {
        const std::size_t key = PipelineCacheKey(
            topology, depthTest, depthWrite, depthFunc, colorFormat, colorTargetCount, sampleCount,
            depthStencilFormat, renderState);
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
        // blendIndices(4) after the same 48-byte prefix, matching EasyGLRenderer::
        // ApplyLayout's stride==48/68 cases exactly.
        SDL_GPUVertexAttribute attrs[6]{};
        attrs[0].location = 0; attrs[0].buffer_slot = 0; attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[0].offset = 0;
        attrs[1].location = 1; attrs[1].buffer_slot = 0; attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[1].offset = 12;
        attrs[2].location = 2; attrs[2].buffer_slot = 0; attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4; attrs[2].offset = 24;
        attrs[3].location = 3; attrs[3].buffer_slot = 0; attrs[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; attrs[3].offset = 40;
        attrs[4].location = 4; attrs[4].buffer_slot = 0; attrs[4].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4; attrs[4].offset = 48;
        attrs[5].location = 5; attrs[5].buffer_slot = 0; attrs[5].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4; attrs[5].offset = 64;

        std::array<SDL_GPUColorTargetDescription, 4> colorTargets{};
        FillColorTargetDescriptions(colorTargets, colorTargetCount, colorFormat, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = skinned ? pbrSkinnedVertexShader_ : pbrVertexShader_;
        pipelineInfo.fragment_shader = pbrFragmentShader_;  // shared unchanged by both variants
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = skinned ? 6 : 4;
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(
            pipelineInfo.rasterizer_state, renderState,
            pipelineInfo.primitive_type);
        pipelineInfo.multisample_state.sample_count = sampleCount;
        FillDepthStencilState(pipelineInfo.depth_stencil_state, depthTest, depthWrite, depthFunc, renderState);
        pipelineInfo.target_info.color_target_descriptions = colorTargets.data();
        pipelineInfo.target_info.num_color_targets = static_cast<Uint32>(colorTargetCount);
        pipelineInfo.target_info.has_depth_stencil_target =
            (depthStencilFormat != SDL_GPU_TEXTUREFORMAT_INVALID);
        pipelineInfo.target_info.depth_stencil_format = depthStencilFormat;

        SDL_GPUGraphicsPipeline* pipeline =
            CreateGraphicsPipeline(pipelineInfo,
                                   "CNA SDL_GPU: failed to create pbr3d pipeline: ");
        return CacheGraphicsPipeline(cache, key, pipeline);
    }

    void SdlGpuRenderer::QueueAlphaTestDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                                    const Matrix& world, const Matrix& view, const Matrix& projection,
                                                    PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferRenderer&>(vb);
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
        command.texture = ResolveSampledTextureEXT(params.texture0, "AlphaTestEffect.Texture");
        command.textureFilter = samplerSlots_[0].filter;
        command.addressU = samplerSlots_[0].addressU;
        command.addressV = samplerSlots_[0].addressV;
        command.maxAnisotropy = samplerSlots_[0].maxAnisotropy;  // REMED-GFX-170

        if (ib != nullptr)
        {
            const auto& sdlGpuIb = static_cast<const SdlGpuIndexBufferRenderer&>(*ib);
            command.indexed = true;
            command.index32 = sdlGpuIb.IsThirtyTwoBit();
            command.indexData = sdlGpuIb.ShadowData();
            ApplyIndexedRange(
                command, sdlGpuIb, sdlGpuVb, primitive, primitiveCount, &params);
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

    void SdlGpuRenderer::QueueDualTextureDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                                      const Matrix& world, const Matrix& view, const Matrix& projection,
                                                      PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferRenderer&>(vb);
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
        command.texture0 = ResolveSampledTextureEXT(params.texture0, "DualTextureEffect.Texture");
        command.texture1 = ResolveSampledTextureEXT(params.texture1, "DualTextureEffect.Texture2");
        // SDLGPU-21: texture0/texture1 are independent slots (GraphicsDevice.SamplerStates[0]/[1]
        // -- real XNA lets DualTextureEffect's two textures sample differently).
        command.texture0Filter = samplerSlots_[0].filter;
        command.texture0AddressU = samplerSlots_[0].addressU;
        command.texture0AddressV = samplerSlots_[0].addressV;
        command.texture0MaxAnisotropy = samplerSlots_[0].maxAnisotropy;  // REMED-GFX-170
        command.texture1Filter = samplerSlots_[1].filter;
        command.texture1AddressU = samplerSlots_[1].addressU;
        command.texture1AddressV = samplerSlots_[1].addressV;
        command.texture1MaxAnisotropy = samplerSlots_[1].maxAnisotropy;  // REMED-GFX-170

        if (ib != nullptr)
        {
            const auto& sdlGpuIb = static_cast<const SdlGpuIndexBufferRenderer&>(*ib);
            command.indexed = true;
            command.index32 = sdlGpuIb.IsThirtyTwoBit();
            command.indexData = sdlGpuIb.ShadowData();
            ApplyIndexedRange(
                command, sdlGpuIb, sdlGpuVb, primitive, primitiveCount, &params);
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

    void SdlGpuRenderer::QueueEnvMapDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                                const Matrix& world, const Matrix& view, const Matrix& projection,
                                                PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferRenderer&>(vb);
        if (sdlGpuVb.Stride() != 32)
            throw std::invalid_argument("CNA SDL_GPU: env_map3d requires a stride-32 "
                                        "(VertexPositionNormalTexture) vertex buffer");

        // params.envMap (ITextureCubeRenderer*) may be either a plain, uploaded TextureCube
        // (SdlGpuTextureCubeRenderer, SDLGPU-51) or a RenderTargetCube sampled after being rendered
        // into (SdlGpuRenderTargetCubeRenderer, SDLGPU-36) -- unrelated concrete classes, resolved
        // through the one shared resolver every binding route uses (REMED-GFX-152).
        const SdlGpuSampledTextureEXT envMapTexture =
            ResolveSampledCubeEXT(params.envMap, "EnvironmentMapEffect.EnvironmentMap");

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
        command.texture = ResolveSampledTextureEXT(params.texture0, "EnvironmentMapEffect.Texture");
        command.envMapTexture = envMapTexture;
        // SDLGPU-21 / REMED-GFX-173: EnvironmentMapEffect samples TWO resources, and each one takes
        // the sampler of its OWN public slot -- the base texture GraphicsDevice.SamplerStates[0],
        // the reflection cube SamplerStates[1]. Both are captured HERE, by value, so a later
        // ApplySamplerState cannot reach back into this already-queued draw.
        command.textureFilter = samplerSlots_[0].filter;
        command.addressU = samplerSlots_[0].addressU;
        command.addressV = samplerSlots_[0].addressV;
        command.maxAnisotropy = samplerSlots_[0].maxAnisotropy;  // REMED-GFX-170
        command.envMapFilter = samplerSlots_[1].filter;
        command.envMapAddressU = samplerSlots_[1].addressU;
        command.envMapAddressV = samplerSlots_[1].addressV;
        command.envMapMaxAnisotropy = samplerSlots_[1].maxAnisotropy;

        if (ib != nullptr)
        {
            const auto& sdlGpuIb = static_cast<const SdlGpuIndexBufferRenderer&>(*ib);
            command.indexed = true;
            command.index32 = sdlGpuIb.IsThirtyTwoBit();
            command.indexData = sdlGpuIb.ShadowData();
            ApplyIndexedRange(
                command, sdlGpuIb, sdlGpuVb, primitive, primitiveCount, &params);
            command.vertexCount = static_cast<Uint32>(sdlGpuVb.GetVertexCount()) - static_cast<Uint32>(vertexStart);
        }
        else
        {
            command.vertexCount = static_cast<Uint32>(PrimitiveVertexCount(primitive, primitiveCount));
        }

        command.target = CurrentDrawTarget();
        if (EnvMapTraceEnabled())
        {
            std::fprintf(stderr,
                         "[cna-sdlgpu-envmap] queue=%u family=EnvironmentMap3D tex2D=%p cube=%p | "
                         "public slot0 filter=%d(%s) addrU=%d addrV=%d aniso=%d | "
                         "public slot1 filter=%d(%s) addrU=%d addrV=%d aniso=%d | "
                         "captured slot0 filter=%d addrU=%d addrV=%d aniso=%d | "
                         "captured slot1 filter=%d addrU=%d addrV=%d aniso=%d | indexed=%d\n",
                         static_cast<unsigned>(envMapTraceQueueIndex_++),
                         static_cast<void*>(command.texture.texture),
                         static_cast<void*>(command.envMapTexture.texture),
                         samplerSlots_[0].filter, TextureFilterName(samplerSlots_[0].filter),
                         samplerSlots_[0].addressU, samplerSlots_[0].addressV, samplerSlots_[0].maxAnisotropy,
                         samplerSlots_[1].filter, TextureFilterName(samplerSlots_[1].filter),
                         samplerSlots_[1].addressU, samplerSlots_[1].addressV, samplerSlots_[1].maxAnisotropy,
                         command.textureFilter, command.addressU, command.addressV, command.maxAnisotropy,
                         command.envMapFilter, command.envMapAddressU, command.envMapAddressV,
                         command.envMapMaxAnisotropy,
                         command.indexed ? 1 : 0);
        }
        envMapDrawCommands_.push_back(std::move(command));
        PushDrawOrder(DrawKind::EnvMap, envMapDrawCommands_.size() - 1);
        framePending_ = true;
    }

    void SdlGpuRenderer::QueueSkinnedDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                                 const Matrix& world, const Matrix& view, const Matrix& projection,
                                                 PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferRenderer&>(vb);
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
        command.texture = ResolveSampledTextureEXT(params.texture0, "SkinnedEffect.Texture");
        command.textureFilter = samplerSlots_[0].filter;
        command.addressU = samplerSlots_[0].addressU;
        command.addressV = samplerSlots_[0].addressV;
        command.maxAnisotropy = samplerSlots_[0].maxAnisotropy;  // REMED-GFX-170

        if (ib != nullptr)
        {
            const auto& sdlGpuIb = static_cast<const SdlGpuIndexBufferRenderer&>(*ib);
            command.indexed = true;
            command.index32 = sdlGpuIb.IsThirtyTwoBit();
            command.indexData = sdlGpuIb.ShadowData();
            ApplyIndexedRange(
                command, sdlGpuIb, sdlGpuVb, primitive, primitiveCount, &params);
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

    void SdlGpuRenderer::QueuePbrDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                             const Matrix& world, const Matrix& view, const Matrix& projection,
                                             PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferRenderer&>(vb);
        const std::size_t stride = sdlGpuVb.Stride();
        // plan_gltf.md GLTF-465: strides 60 and 80 already fail the expected-stride check below, but
        // they fail it as a layout mismatch, which says nothing about the core semantic that is
        // missing. Name it first so this renderer's refusal is the shared one, with the
        // application's own opt-out spelled out.
        RequireVertexColourPbrSupportEXT(params, stride, "SDL_GPU");

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
        command.texture = ResolveSampledTextureEXT(params.texture0, "PbrEffect.Texture");
        command.normalMap = ResolveSampledTextureEXT(params.pbrNormalMap, "PbrEffect.NormalMap");
        command.metallicRoughnessMap = ResolveSampledTextureEXT(params.pbrMetallicRoughnessMap, "PbrEffect.MetallicRoughnessMap");
        command.emissiveMap = ResolveSampledTextureEXT(params.pbrEmissiveMap, "PbrEffect.EmissiveMap");
        command.occlusionMap = ResolveSampledTextureEXT(params.pbrOcclusionMap, "PbrEffect.OcclusionMap");
        command.specularMap = ResolveSampledTextureEXT(params.pbrSpecularMap, "PbrEffect.SpecularMapEXT");
        command.specularColorMap = ResolveSampledTextureEXT(
            params.pbrSpecularColorMap, "PbrEffect.SpecularColorMapEXT");
        command.textureFilter = samplerSlots_[0].filter;
        command.addressU = samplerSlots_[0].addressU;
        command.addressV = samplerSlots_[0].addressV;
        command.maxAnisotropy = samplerSlots_[0].maxAnisotropy;  // REMED-GFX-170
        command.specularSampler = samplerSlots_[5];
        command.specularColorSampler = samplerSlots_[6];

        if (ib != nullptr)
        {
            const auto& sdlGpuIb = static_cast<const SdlGpuIndexBufferRenderer&>(*ib);
            command.indexed = true;
            command.index32 = sdlGpuIb.IsThirtyTwoBit();
            command.indexData = sdlGpuIb.ShadowData();
            ApplyIndexedRange(
                command, sdlGpuIb, sdlGpuVb, primitive, primitiveCount, &params);
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

#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
    SDL_GPUGraphicsPipeline* SdlGpuRenderer::GetOrCreatePipelineCompiledEffect(
        const CompiledEffectBinding& binding, Uint32 vertexStride,
        SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        const RenderStateSnapshot& renderState,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
        SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount)
    {
        std::size_t key = PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, colorFormat,
                                           colorTargetCount, sampleCount, depthStencilFormat,
                                           renderState);
        // The base key above is exactly what every stock pipeline hashes; a compiled effect's
        // shader pair and vertex layout vary per pass/effect (unlike a stock family's fixed shader
        // fields, or SpriteBatch's own fixed SpriteVertex layout), so those are folded in here
        // instead of being implicit in which cache this key is looked up in. One cache and one key
        // scheme serves both the ordinary-draw and SpriteBatch routes.
        key = HashCombine(key, std::hash<const void*>{}(binding.vertexShader));
        key = HashCombine(key, std::hash<const void*>{}(binding.pixelShader));
        key = HashCombine(key, static_cast<std::size_t>(vertexStride));
        for (const SDL_GPUVertexAttribute& attribute : binding.vertexAttributes)
        {
            key = HashCombine(key, static_cast<std::size_t>(attribute.location));
            key = HashCombine(key, static_cast<std::size_t>(attribute.format));
            key = HashCombine(key, static_cast<std::size_t>(attribute.offset));
        }

        const auto it = compiledEffectPipelines_.find(key);
        if (it != compiledEffectPipelines_.end())
            return it->second;

        SDL_GPUVertexBufferDescription vbDesc{};
        vbDesc.slot = 0;
        vbDesc.pitch = vertexStride;
        vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        std::array<SDL_GPUColorTargetDescription, 4> colorTargets{};
        FillColorTargetDescriptions(colorTargets, colorTargetCount, colorFormat, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = binding.vertexShader;
        pipelineInfo.fragment_shader = binding.pixelShader;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = binding.vertexAttributes.empty() ? 0 : 1;
        pipelineInfo.vertex_input_state.vertex_attributes = binding.vertexAttributes.data();
        pipelineInfo.vertex_input_state.num_vertex_attributes =
            static_cast<Uint32>(binding.vertexAttributes.size());
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(pipelineInfo.rasterizer_state, renderState, pipelineInfo.primitive_type);
        pipelineInfo.multisample_state.sample_count = sampleCount;
        FillDepthStencilState(pipelineInfo.depth_stencil_state, depthTest, depthWrite,
                              depthFunc, renderState);
        pipelineInfo.target_info.color_target_descriptions = colorTargets.data();
        pipelineInfo.target_info.num_color_targets = static_cast<Uint32>(colorTargetCount);
        pipelineInfo.target_info.has_depth_stencil_target =
            (depthStencilFormat != SDL_GPU_TEXTUREFORMAT_INVALID);
        pipelineInfo.target_info.depth_stencil_format = depthStencilFormat;

        SDL_GPUGraphicsPipeline* pipeline =
            CreateGraphicsPipeline(pipelineInfo,
                                   "CNA SDL_GPU: failed to create compiled-effect pipeline: ");
        return CacheGraphicsPipeline(compiledEffectPipelines_, key, pipeline);
    }

    SdlGpuRenderer::CompiledEffectBinding SdlGpuRenderer::BuildCompiledEffectBindingEXT(
        CNA::Internal::Renderers::SdlGpu::SdlGpuCompiledEffect& effect,
        const std::vector<VertexElement>& declaredElements)
    {
        CompiledEffectBinding binding;
        binding.vertexAttributes =
            effect.LinkAndGetShadersEXT(declaredElements, binding.vertexShader, binding.pixelShader);

        MOJOSHADER_sdlShaderData* vertexShaderData = nullptr;
        MOJOSHADER_sdlShaderData* pixelShaderData = nullptr;
        effect.GetBoundShadersEXT(vertexShaderData, pixelShaderData);
        const MOJOSHADER_parseData* vertexParseData =
            MOJOSHADER_sdlGetShaderParseData(vertexShaderData);
        const MOJOSHADER_parseData* pixelParseData =
            MOJOSHADER_sdlGetShaderParseData(pixelShaderData);
        if (vertexParseData != nullptr && vertexParseData->sampler_count > 0)
        {
            throw System::NotSupportedException(
                "CNA SDL_GPU: this compiled effect's vertex shader samples a texture; vertex-stage "
                "sampling is not implemented by this renderer's compiled-effect draw route yet.");
        }

        effect.CaptureUniformSnapshotEXT(binding.vertexUniformBytes, binding.pixelUniformBytes);

        // MOJOSHADER_sdlCompileShader (mojoshader_sdlgpu.c) always reports at least one sampler
        // slot for the compiled SDL_GPU shader module -- its own maxSamplerIndex starts at 0, not
        // -1, so a shader with zero reflected samplers still gets num_samplers=1 -- and SDL_GPU's
        // own debug validation checks that declared count, not MojoShader's reflected usage. Every
        // slot in [0, GetSamplerSlots) therefore needs a real binding regardless of whether this
        // pass's reflection lists one for it; an unlisted slot gets this renderer's default white
        // texture, which the shader never actually samples.
        EnsureDefaultPbrTextures();  // lazily creates defaultWhiteTexture_, reused here
        const unsigned int pixelSamplerSlotCount = MOJOSHADER_sdlGetSamplerSlots(pixelShaderData);
        binding.pixelSamplers.resize(pixelSamplerSlotCount);
        for (unsigned int slot = 0; slot < pixelSamplerSlotCount; ++slot)
        {
            CompiledEffectSamplerBinding& samplerBinding = binding.pixelSamplers[slot];
            const MOJOSHADER_sampler* reflectedSampler = nullptr;
            if (pixelParseData != nullptr)
            {
                for (int i = 0; i < pixelParseData->sampler_count; ++i)
                {
                    if (static_cast<unsigned int>(pixelParseData->samplers[i].index) == slot)
                    {
                        reflectedSampler = &pixelParseData->samplers[i];
                        break;
                    }
                }
            }

            if (reflectedSampler == nullptr)
            {
                samplerBinding.texture = ResolveSampledTextureEXT(
                    defaultWhiteTexture_.get(), "CompiledEffect.UnreflectedSampler");
                continue;
            }

            Texture* boundTexture = nullptr;
            Microsoft::Xna::Framework::Graphics::SamplerState samplerState;
            effect.GetBoundSamplerEXT(slot, /*vertexStage=*/false, boundTexture, samplerState);
            if (boundTexture == nullptr)
            {
                const char* name = reflectedSampler->name != nullptr ? reflectedSampler->name
                                                                      : "<unnamed>";
                throw std::runtime_error(
                    std::string("CNA SDL_GPU: this compiled effect's pixel shader samples '") +
                    name + "', but no texture is bound to it.");
            }
            auto* texture2D = dynamic_cast<Microsoft::Xna::Framework::Graphics::Texture2D*>(boundTexture);
            if (texture2D == nullptr)
            {
                throw System::NotSupportedException(
                    "CNA SDL_GPU: this compiled effect binds a 3D or cube texture to a "
                    "sampler; this renderer's compiled-effect draw route supports 2D textures "
                    "only so far.");
            }

            samplerBinding.texture = ResolveSampledTextureEXT(&texture2D->GetRenderer(),
                                                              "CompiledEffect.Sampler");
            samplerBinding.filter = static_cast<int>(samplerState.getFilterProperty());
            samplerBinding.addressU = static_cast<int>(samplerState.getAddressUProperty());
            samplerBinding.addressV = static_cast<int>(samplerState.getAddressVProperty());
            samplerBinding.maxAnisotropy = samplerState.getMaxAnisotropyProperty();
        }

        binding.vertexDummySamplerCount = MOJOSHADER_sdlGetSamplerSlots(vertexShaderData);
        if (binding.vertexDummySamplerCount > 0)
        {
            binding.vertexDummyTexture = ResolveSampledTextureEXT(
                defaultWhiteTexture_.get(), "CompiledEffect.VertexDummySampler");
        }

        return binding;
    }

    void SdlGpuRenderer::QueueCompiledEffectDraw(const IVertexBufferRenderer& vb,
                                                 const IIndexBufferRenderer* ib,
                                                 PrimitiveType primitive, int primitiveCount,
                                                 const GpuDrawParams& params)
    {
        auto* sdlGpuEffect = dynamic_cast<CNA::Internal::Renderers::SdlGpu::SdlGpuCompiledEffect*>(
            params.compiledEffectRuntime);
        if (sdlGpuEffect == nullptr)
        {
            throw std::runtime_error(
                "CNA SDL_GPU: the applied compiled effect was not created by this renderer.");
        }

        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferRenderer&>(vb);
        const auto& declaration = sdlGpuVb.Declaration();
        if (declaration.IsEmpty())
        {
            throw System::NotSupportedException(
                "CNA SDL_GPU: a compiled-effect draw needs the vertex buffer's own "
                "VertexDeclaration; this renderer does not infer one from stride for this route.");
        }

        CompiledEffectDrawCommand command;
        command.vertexStride = static_cast<Uint32>(declaration.GetStride());
        command.binding = BuildCompiledEffectBindingEXT(*sdlGpuEffect, declaration.GetElements());

        const int vertexStart = params.vertexStart;
        const auto& shadow = sdlGpuVb.ShadowData();
        const std::size_t byteOffset =
            static_cast<std::size_t>(vertexStart) * command.vertexStride;
        if (byteOffset <= shadow.size())
        {
            command.vertexData.assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset),
                                      shadow.end());
        }
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        command.renderState = CaptureRenderState();

        if (ib != nullptr)
        {
            const auto& sdlGpuIb = static_cast<const SdlGpuIndexBufferRenderer&>(*ib);
            command.indexed = true;
            command.index32 = sdlGpuIb.IsThirtyTwoBit();
            command.indexData = sdlGpuIb.ShadowData();
            ApplyIndexedRange(command, sdlGpuIb, sdlGpuVb, primitive, primitiveCount, &params);
            command.vertexCount =
                static_cast<Uint32>(sdlGpuVb.GetVertexCount()) - static_cast<Uint32>(vertexStart);
        }
        else
        {
            command.vertexCount = static_cast<Uint32>(PrimitiveVertexCount(primitive, primitiveCount));
        }

        command.target = CurrentDrawTarget();
        compiledEffectDrawCommands_.push_back(std::move(command));
        PushDrawOrder(DrawKind::CompiledEffect, compiledEffectDrawCommands_.size() - 1);
        framePending_ = true;
    }

    void SdlGpuRenderer::BindCompiledEffectForDrawEXT(
        SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
        const CompiledEffectBinding& binding, Uint32 vertexStride,
        SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        const RenderStateSnapshot& renderState,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
        SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount,
        SDL_GPUGraphicsPipeline*& boundPipeline)
    {
        SDL_GPUGraphicsPipeline* pipeline = GetOrCreatePipelineCompiledEffect(
            binding, vertexStride, topology, depthTest, depthWrite, depthFunc, renderState,
            colorFormat, sampleCount, depthStencilFormat, colorTargetCount);
        if (pipeline != boundPipeline) { SDL_BindGPUGraphicsPipeline(pass, pipeline); boundPipeline = pipeline; }
        SDL_SetGPUStencilReference(pass, static_cast<Uint8>(renderState.stencilReference));

        if (!binding.vertexUniformBytes.empty())
        {
            SDL_PushGPUVertexUniformData(cmd, 0, binding.vertexUniformBytes.data(),
                                         static_cast<Uint32>(binding.vertexUniformBytes.size()));
        }
        if (!binding.pixelUniformBytes.empty())
        {
            SDL_PushGPUFragmentUniformData(cmd, 0, binding.pixelUniformBytes.data(),
                                           static_cast<Uint32>(binding.pixelUniformBytes.size()));
        }

        if (binding.vertexDummySamplerCount > 0)
        {
            SDL_GPUTextureSamplerBinding dummyBinding{};
            dummyBinding.texture = binding.vertexDummyTexture.texture;
            dummyBinding.sampler =
                GetOrCreateSampler(0, 0, 0, 4, "CompiledEffectVertexDummy");
            std::vector<SDL_GPUTextureSamplerBinding> dummyBindings(
                binding.vertexDummySamplerCount, dummyBinding);
            SDL_BindGPUVertexSamplers(pass, 0, dummyBindings.data(),
                                      static_cast<Uint32>(dummyBindings.size()));
        }

        if (!binding.pixelSamplers.empty())
        {
            std::vector<SDL_GPUTextureSamplerBinding> samplerBindings;
            samplerBindings.reserve(binding.pixelSamplers.size());
            for (const CompiledEffectSamplerBinding& samplerBinding : binding.pixelSamplers)
            {
                SDL_GPUTextureSamplerBinding gpuSamplerBinding{};
                gpuSamplerBinding.texture = samplerBinding.texture.texture;
                gpuSamplerBinding.sampler = GetOrCreateSampler(
                    samplerBinding.filter, samplerBinding.addressU, samplerBinding.addressV,
                    samplerBinding.maxAnisotropy, "CompiledEffect");
                samplerBindings.push_back(gpuSamplerBinding);
            }
            SDL_BindGPUFragmentSamplers(pass, 0, samplerBindings.data(),
                                        static_cast<Uint32>(samplerBindings.size()));
        }
    }

    void SdlGpuRenderer::IssueCompiledEffectDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                 const CompiledEffectDrawCommand& command,
                                                 SDL_GPUTextureFormat colorFormat,
                                                 SDL_GPUSampleCount sampleCount,
                                                 SDL_GPUTextureFormat depthStencilFormat,
                                                 int colorTargetCount,
                                                 SDL_GPUGraphicsPipeline*& boundPipeline)
    {
        BindCompiledEffectForDrawEXT(pass, cmd, command.binding, command.vertexStride,
                                     command.topology, command.depthTest, command.depthWrite,
                                     command.depthFunc, command.renderState, colorFormat,
                                     sampleCount, depthStencilFormat, colorTargetCount,
                                     boundPipeline);

        SDL_GPUBufferBinding vbBinding{};
        vbBinding.buffer = command.uploadedVertexBuffer;
        SDL_BindGPUVertexBuffers(pass, 0, &vbBinding, 1);

        if (command.indexed && command.uploadedIndexBuffer != nullptr)
        {
            SDL_GPUBufferBinding ibBinding{};
            ibBinding.buffer = command.uploadedIndexBuffer;
            SDL_BindGPUIndexBuffer(pass, &ibBinding,
                                   command.index32 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT);
            SDL_DrawGPUIndexedPrimitives(
                pass, command.indexCount, 1, command.firstIndex, command.vertexOffset, 0);
        }
        else
        {
            SDL_DrawGPUPrimitives(pass, command.vertexCount, 1, 0, 0);
        }
    }
#endif  // CNA_SDL_GPU_COMPILED_EFFECTS

    void SdlGpuRenderer::IssueAlphaTestDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                   const AlphaTestDrawCommand& command, SDL_GPUTextureFormat colorFormat,
                                                   SDL_GPUSampleCount sampleCount,
                                 SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount,
                                                   SDL_GPUGraphicsPipeline*& boundPipeline)
    {
        SDL_GPUGraphicsPipeline* pipeline = GetOrCreatePipelineAlphaTest3D(
            command.stride, command.topology, command.depthTest, command.depthWrite,
            command.depthFunc, colorFormat, sampleCount, depthStencilFormat, colorTargetCount, command.renderState);
        if (pipeline != boundPipeline) { SDL_BindGPUGraphicsPipeline(pass, pipeline); boundPipeline = pipeline; }
        SDL_SetGPUStencilReference(pass, static_cast<Uint8>(command.renderState.stencilReference));
        SDL_PushGPUVertexUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));
        SDL_PushGPUVertexUniformData(cmd, 1, command.fogUniforms.data(), sizeof(command.fogUniforms));  // REMED-GFX-009
        SDL_PushGPUFragmentUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));

        SDL_GPUBufferBinding vbBinding{};
        vbBinding.buffer = command.uploadedVertexBuffer;
        SDL_BindGPUVertexBuffers(pass, 0, &vbBinding, 1);

        SDL_GPUTextureSamplerBinding samplerBinding{};
        samplerBinding.texture = command.texture.texture;
        samplerBinding.sampler = GetOrCreateSampler(command.textureFilter, command.addressU,
                                                   command.addressV, command.maxAnisotropy,
                                                   "AlphaTest3D");
        SDL_BindGPUFragmentSamplers(pass, 0, &samplerBinding, 1);

        if (command.indexed && command.uploadedIndexBuffer != nullptr)
        {
            SDL_GPUBufferBinding ibBinding{};
            ibBinding.buffer = command.uploadedIndexBuffer;
            SDL_BindGPUIndexBuffer(pass, &ibBinding,
                                   command.index32 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT);
            SDL_DrawGPUIndexedPrimitives(
                pass, command.indexCount, 1, command.firstIndex, command.vertexOffset, 0);
        }
        else
        {
            SDL_DrawGPUPrimitives(pass, command.vertexCount, 1, 0, 0);
        }
    }

    void SdlGpuRenderer::IssueDualTextureDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                     const DualTextureDrawCommand& command, SDL_GPUTextureFormat colorFormat,
                                                     SDL_GPUSampleCount sampleCount,
                            SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount,
                                                     SDL_GPUGraphicsPipeline*& boundPipeline)
    {
        SDL_GPUGraphicsPipeline* pipeline = GetOrCreatePipelineDualTexture3D(
            command.hasVertexColor ? 24 : 20, command.topology, command.depthTest,
            command.depthWrite, command.depthFunc, colorFormat, sampleCount,
            depthStencilFormat, colorTargetCount, command.renderState);
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
        samplerBindings[0].texture = command.texture0.texture;
        samplerBindings[0].sampler = GetOrCreateSampler(command.texture0Filter, command.texture0AddressU,
                                                      command.texture0AddressV,
                                                      command.texture0MaxAnisotropy,
                                                      "DualTexture3D/slot0");
        samplerBindings[1].texture = command.texture1.texture;
        samplerBindings[1].sampler = GetOrCreateSampler(command.texture1Filter, command.texture1AddressU,
                                                      command.texture1AddressV,
                                                      command.texture1MaxAnisotropy,
                                                      "DualTexture3D/slot1");
        SDL_BindGPUFragmentSamplers(pass, 0, samplerBindings, 2);

        if (command.indexed && command.uploadedIndexBuffer != nullptr)
        {
            SDL_GPUBufferBinding ibBinding{};
            ibBinding.buffer = command.uploadedIndexBuffer;
            SDL_BindGPUIndexBuffer(pass, &ibBinding,
                                   command.index32 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT);
            SDL_DrawGPUIndexedPrimitives(
                pass, command.indexCount, 1, command.firstIndex, command.vertexOffset, 0);
        }
        else
        {
            SDL_DrawGPUPrimitives(pass, command.vertexCount, 1, 0, 0);
        }
    }

    void SdlGpuRenderer::IssueEnvMapDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                const EnvMapDrawCommand& command, SDL_GPUTextureFormat colorFormat,
                                                SDL_GPUSampleCount sampleCount,
                             SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount,
                                                SDL_GPUGraphicsPipeline*& boundPipeline)
    {
        SDL_GPUGraphicsPipeline* pipeline = GetOrCreatePipelineEnvMap3D(
            command.topology, command.depthTest, command.depthWrite, command.depthFunc,
            colorFormat, sampleCount, depthStencilFormat, colorTargetCount, command.renderState);
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

        // REMED-GFX-173: binding 0 (sampler2D uTexture) and binding 1 (samplerCube uEnvMap) are two
        // INDEPENDENT sampled resources, so each takes the sampler captured from its own public
        // slot -- the identical shape IssueDualTextureDraw already uses. This previously bound a
        // literal LinearClamp for the cube, described as "this project's other renderers' fixed
        // reflection-map sampling convention"; REMED-GFX-169 ended that convention by making
        // Vulkan honour GraphicsDevice.SamplerStates[1] for the same binding.
        SDL_GPUTextureSamplerBinding samplerBindings[2]{};
        samplerBindings[0].texture = command.texture.texture;
        samplerBindings[0].sampler = GetOrCreateSampler(command.textureFilter, command.addressU,
                                                      command.addressV, command.maxAnisotropy,
                                                      "EnvironmentMap3D");
        samplerBindings[1].texture = command.envMapTexture.texture;
        samplerBindings[1].sampler = GetOrCreateSampler(command.envMapFilter, command.envMapAddressU,
                                                       command.envMapAddressV,
                                                       command.envMapMaxAnisotropy,
                                                       "EnvironmentMap3D/cube");
        SDL_BindGPUFragmentSamplers(pass, 0, samplerBindings, 2);
        // REMED-GFX-173: the whole two-slot binding on one line -- both public slots, both native
        // samplers and both texture identities together, so "the cube got slot 0's sampler" or
        // "the cube got a constant" is readable directly instead of inferred from pixels.
        if (EnvMapTraceEnabled())
        {
            std::fprintf(stderr,
                         "[cna-sdlgpu-envmap] replay=%u family=EnvironmentMap3D "
                         "tex2D=%p cube=%p | slot0 filter=%d(%s) addrU=%d addrV=%d aniso=%d "
                         "sampler=%p | slot1 filter=%d(%s) addrU=%d addrV=%d aniso=%d sampler=%p | "
                         "indexed=%d\n",
                         static_cast<unsigned>(envMapTraceReplayIndex_++),
                         static_cast<void*>(command.texture.texture),
                         static_cast<void*>(command.envMapTexture.texture),
                         command.textureFilter, TextureFilterName(command.textureFilter),
                         command.addressU, command.addressV, command.maxAnisotropy,
                         static_cast<void*>(samplerBindings[0].sampler),
                         command.envMapFilter, TextureFilterName(command.envMapFilter),
                         command.envMapAddressU, command.envMapAddressV, command.envMapMaxAnisotropy,
                         static_cast<void*>(samplerBindings[1].sampler),
                         command.indexed ? 1 : 0);
        }

        if (command.indexed && command.uploadedIndexBuffer != nullptr)
        {
            SDL_GPUBufferBinding ibBinding{};
            ibBinding.buffer = command.uploadedIndexBuffer;
            SDL_BindGPUIndexBuffer(pass, &ibBinding,
                                   command.index32 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT);
            SDL_DrawGPUIndexedPrimitives(
                pass, command.indexCount, 1, command.firstIndex, command.vertexOffset, 0);
        }
        else
        {
            SDL_DrawGPUPrimitives(pass, command.vertexCount, 1, 0, 0);
        }
    }

    void SdlGpuRenderer::IssueSkinnedDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                 const SkinnedDrawCommand& command, SDL_GPUTextureFormat colorFormat,
                                                 SDL_GPUSampleCount sampleCount,
                         SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount,
                                                 SDL_GPUGraphicsPipeline*& boundPipeline)
    {
        SDL_GPUGraphicsPipeline* pipeline = GetOrCreatePipelineSkinned3D(
            command.hasVertexColor, command.topology, command.depthTest, command.depthWrite,
            command.depthFunc, colorFormat, sampleCount, depthStencilFormat, colorTargetCount, command.renderState);
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
        samplerBinding.texture = command.texture.texture;
        samplerBinding.sampler = GetOrCreateSampler(command.textureFilter, command.addressU,
                                                   command.addressV, command.maxAnisotropy,
                                                   "Skinned3D");
        SDL_BindGPUFragmentSamplers(pass, 0, &samplerBinding, 1);

        if (command.indexed && command.uploadedIndexBuffer != nullptr)
        {
            SDL_GPUBufferBinding ibBinding{};
            ibBinding.buffer = command.uploadedIndexBuffer;
            SDL_BindGPUIndexBuffer(pass, &ibBinding,
                                   command.index32 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT);
            SDL_DrawGPUIndexedPrimitives(
                pass, command.indexCount, 1, command.firstIndex, command.vertexOffset, 0);
        }
        else
        {
            SDL_DrawGPUPrimitives(pass, command.vertexCount, 1, 0, 0);
        }
    }

    void SdlGpuRenderer::IssuePbrDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                             const PbrDrawCommand& command, SDL_GPUTextureFormat colorFormat,
                                             SDL_GPUSampleCount sampleCount,
                             SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount,
                                             SDL_GPUGraphicsPipeline*& boundPipeline)
    {
        SDL_GPUGraphicsPipeline* pipeline = GetOrCreatePipelinePbr3D(
            command.skinned, command.topology, command.depthTest, command.depthWrite,
            command.depthFunc, colorFormat, sampleCount, depthStencilFormat, colorTargetCount, command.renderState);
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

        // 7 samplers: the core five plus KHR_materials_specular strength and colour. Optional maps
        // fall back to the lazily-created default textures (EnsureDefaultPbrTextures(), already
        // invoked at Queue-time) so "map absent" reads as the correct neutral value per semantic
        // (flat normal, factor-only, no emissive tint, fully lit) -- mirrors
        // EasyGLRenderer::BindDrawParams()'s identical fallback set. The original five retain the
        // base color texture's sampler state; extension maps use their imported slots 5 and 6.
        SDL_GPUTextureSamplerBinding samplerBindings[7]{};
        SDL_GPUSampler* sampler = GetOrCreateSampler(command.textureFilter, command.addressU,
                                                    command.addressV, command.maxAnisotropy,
                                                    "Pbr3D");
        samplerBindings[0].texture = command.texture.texture;
        samplerBindings[0].sampler = sampler;
        samplerBindings[1].texture = command.normalMap ? command.normalMap.texture : defaultFlatNormalTexture_->Texture();
        samplerBindings[1].sampler = sampler;
        samplerBindings[2].texture = command.metallicRoughnessMap ? command.metallicRoughnessMap.texture : defaultWhiteTexture_->Texture();
        samplerBindings[2].sampler = sampler;
        samplerBindings[3].texture = command.emissiveMap ? command.emissiveMap.texture : defaultWhiteTexture_->Texture();
        samplerBindings[3].sampler = sampler;
        samplerBindings[4].texture = command.occlusionMap ? command.occlusionMap.texture : defaultWhiteTexture_->Texture();
        samplerBindings[4].sampler = sampler;
        SDL_GPUSampler* specularSampler = GetOrCreateSampler(
            command.specularSampler.filter, command.specularSampler.addressU,
            command.specularSampler.addressV, command.specularSampler.maxAnisotropy,
            "Pbr3D.Specular");
        SDL_GPUSampler* specularColorSampler = GetOrCreateSampler(
            command.specularColorSampler.filter, command.specularColorSampler.addressU,
            command.specularColorSampler.addressV, command.specularColorSampler.maxAnisotropy,
            "Pbr3D.SpecularColor");
        samplerBindings[5].texture = command.specularMap
            ? command.specularMap.texture : defaultWhiteTexture_->Texture();
        samplerBindings[5].sampler = specularSampler;
        samplerBindings[6].texture = command.specularColorMap
            ? command.specularColorMap.texture : defaultWhiteTexture_->Texture();
        samplerBindings[6].sampler = specularColorSampler;
        SDL_BindGPUFragmentSamplers(pass, 0, samplerBindings, 7);

        if (command.indexed && command.uploadedIndexBuffer != nullptr)
        {
            SDL_GPUBufferBinding ibBinding{};
            ibBinding.buffer = command.uploadedIndexBuffer;
            SDL_BindGPUIndexBuffer(pass, &ibBinding,
                                   command.index32 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT);
            SDL_DrawGPUIndexedPrimitives(
                pass, command.indexCount, 1, command.firstIndex, command.vertexOffset, 0);
        }
        else
        {
            SDL_DrawGPUPrimitives(pass, command.vertexCount, 1, 0, 0);
        }
    }

    void SdlGpuRenderer::UploadSceneDrawData(SDL_GPUCommandBuffer* cmd)
    {
        if (coloredDrawCommands_.empty() && texturedDrawCommands_.empty() && litTexturedDrawCommands_.empty() &&
            alphaTestDrawCommands_.empty() && dualTextureDrawCommands_.empty() && envMapDrawCommands_.empty() &&
            skinnedDrawCommands_.empty() && pbrDrawCommands_.empty()
#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
            && compiledEffectDrawCommands_.empty()
#endif
            )
            return;

        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
        CopyPassOwner copyPassOwner(copyPass);

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
#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
        for (CompiledEffectDrawCommand& command : compiledEffectDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty())
                continue;
            command.uploadedVertexBuffer = uploadOne(command.vertexData, SDL_GPU_BUFFERUSAGE_VERTEX);
            if (command.indexed && !command.indexData.empty())
                command.uploadedIndexBuffer = uploadOne(command.indexData, SDL_GPU_BUFFERUSAGE_INDEX);
        }
#endif

        copyPassOwner.End();
    }

    void SdlGpuRenderer::IssueColoredDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                 const ColoredDrawCommand& command, SDL_GPUTextureFormat colorFormat,
                                                 SDL_GPUSampleCount sampleCount,
                              SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount,
                                                 SDL_GPUGraphicsPipeline*& boundPipeline)
    {
        SDL_GPUGraphicsPipeline* pipeline = GetOrCreatePipelineColored3D(command.topology, command.depthTest,
                                                                          command.depthWrite, command.depthFunc,
                                                                          colorFormat, sampleCount,
                                                                          depthStencilFormat, colorTargetCount, command.renderState);
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
            SDL_DrawGPUIndexedPrimitives(
                pass, command.indexCount, 1, command.firstIndex, command.vertexOffset, 0);
        }
        else
        {
            SDL_DrawGPUPrimitives(pass, command.vertexCount, 1, 0, 0);
        }
    }

    void SdlGpuRenderer::IssueTexturedDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                  const TexturedDrawCommand& command, SDL_GPUTextureFormat colorFormat,
                                                  SDL_GPUSampleCount sampleCount,
                                 SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount,
                                                  SDL_GPUGraphicsPipeline*& boundPipeline)
    {
        SDL_GPUGraphicsPipeline* pipeline = command.hasVertexColor
            ? GetOrCreatePipelineColoredTextured3D(
                command.topology, command.depthTest, command.depthWrite, command.depthFunc,
                colorFormat, sampleCount, depthStencilFormat, colorTargetCount, command.renderState)
            : GetOrCreatePipelineTextured3D(
                command.topology, command.depthTest, command.depthWrite, command.depthFunc,
                colorFormat, sampleCount, depthStencilFormat, colorTargetCount, command.renderState);
        if (pipeline != boundPipeline) { SDL_BindGPUGraphicsPipeline(pass, pipeline); boundPipeline = pipeline; }
        SDL_SetGPUStencilReference(pass, static_cast<Uint8>(command.renderState.stencilReference));
        SDL_PushGPUVertexUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));
        SDL_PushGPUVertexUniformData(cmd, 1, command.fogUniforms.data(), sizeof(command.fogUniforms));  // REMED-GFX-009
        SDL_PushGPUFragmentUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));

        SDL_GPUBufferBinding vbBinding{};
        vbBinding.buffer = command.uploadedVertexBuffer;
        SDL_BindGPUVertexBuffers(pass, 0, &vbBinding, 1);

        SDL_GPUTextureSamplerBinding samplerBinding{};
        samplerBinding.texture = command.texture.texture;
        samplerBinding.sampler = GetOrCreateSampler(command.textureFilter, command.addressU,
                                                   command.addressV, command.maxAnisotropy,
                                                   "Textured3D");
        SDL_BindGPUFragmentSamplers(pass, 0, &samplerBinding, 1);

        if (command.indexed && command.uploadedIndexBuffer != nullptr)
        {
            SDL_GPUBufferBinding ibBinding{};
            ibBinding.buffer = command.uploadedIndexBuffer;
            SDL_BindGPUIndexBuffer(pass, &ibBinding,
                                   command.index32 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT);
            SDL_DrawGPUIndexedPrimitives(
                pass, command.indexCount, 1, command.firstIndex, command.vertexOffset, 0);
        }
        else
        {
            SDL_DrawGPUPrimitives(pass, command.vertexCount, 1, 0, 0);
        }
    }

    void SdlGpuRenderer::IssueLitTexturedDraw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                     const LitTexturedDrawCommand& command, SDL_GPUTextureFormat colorFormat,
                                                     SDL_GPUSampleCount sampleCount,
                                                     SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount,
                                                     SDL_GPUGraphicsPipeline*& boundPipeline)
    {
        SDL_GPUGraphicsPipeline* pipeline = GetOrCreatePipelineLitTextured3D(
            command.topology, command.depthTest, command.depthWrite, command.depthFunc,
            colorFormat, sampleCount, depthStencilFormat, colorTargetCount, command.renderState);
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
        samplerBinding.texture = command.texture.texture;
        samplerBinding.sampler = GetOrCreateSampler(command.textureFilter, command.addressU,
                                                   command.addressV, command.maxAnisotropy,
                                                   "LitTextured3D");
        SDL_BindGPUFragmentSamplers(pass, 0, &samplerBinding, 1);

        if (command.indexed && command.uploadedIndexBuffer != nullptr)
        {
            SDL_GPUBufferBinding ibBinding{};
            ibBinding.buffer = command.uploadedIndexBuffer;
            SDL_BindGPUIndexBuffer(pass, &ibBinding,
                                   command.index32 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT);
            SDL_DrawGPUIndexedPrimitives(
                pass, command.indexCount, 1, command.firstIndex, command.vertexOffset, 0);
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
    void SdlGpuRenderer::RenderQueuedDraws(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                  const DrawTarget& target, SDL_GPUTextureFormat colorFormat,
                                                  SDL_GPUSampleCount sampleCount,
                                                  SDL_GPUTextureFormat depthStencilFormat,
                                                  int colorTargetCount,
                                                  std::uint64_t segment)
    {
        const float viewportSize[2] = {
            static_cast<float>(target.rt != nullptr ? target.rt->width
                              : target.cube != nullptr ? target.cube->size : physicalWidth_),
            static_cast<float>(target.rt != nullptr ? target.rt->height
                              : target.cube != nullptr ? target.cube->size : physicalHeight_)};

        SDL_GPUGraphicsPipeline* boundPipeline = nullptr;
        for (const QueuedDrawRef& ref : drawOrder_)
        {
            // REMED-GFX-145: a pass replays ONE bind cycle. Target identity alone would replay
            // every cycle of that target -- exactly the collapsing that fix removed. REMED-GFX-143:
            // this holds for the backbuffer too, so there is no longer any "replay every swapchain
            // draw whichever cycle issued it" escape.
            if (ref.segment != segment)
                continue;
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
                        IssueColoredDraw(pass, cmd, c, colorFormat, sampleCount,
                                         depthStencilFormat, colorTargetCount, boundPipeline);
                    break;
                }
                case DrawKind::Textured:
                {
                    const TexturedDrawCommand& c = texturedDrawCommands_[ref.index];
                    if (c.uploadedVertexBuffer != nullptr && c.texture && c.target == target)
                        IssueTexturedDraw(pass, cmd, c, colorFormat, sampleCount,
                                          depthStencilFormat, colorTargetCount, boundPipeline);
                    break;
                }
                case DrawKind::LitTextured:
                {
                    const LitTexturedDrawCommand& c = litTexturedDrawCommands_[ref.index];
                    if (c.uploadedVertexBuffer != nullptr && c.texture && c.target == target)
                        IssueLitTexturedDraw(pass, cmd, c, colorFormat, sampleCount,
                                             depthStencilFormat, colorTargetCount, boundPipeline);
                    break;
                }
                case DrawKind::AlphaTest:
                {
                    const AlphaTestDrawCommand& c = alphaTestDrawCommands_[ref.index];
                    if (c.uploadedVertexBuffer != nullptr && c.texture && c.target == target)
                        IssueAlphaTestDraw(pass, cmd, c, colorFormat, sampleCount,
                                           depthStencilFormat, colorTargetCount, boundPipeline);
                    break;
                }
                case DrawKind::DualTexture:
                {
                    const DualTextureDrawCommand& c = dualTextureDrawCommands_[ref.index];
                    if (c.uploadedVertexBuffer != nullptr && c.texture0 && c.texture1 && c.target == target)
                        IssueDualTextureDraw(pass, cmd, c, colorFormat, sampleCount,
                                             depthStencilFormat, colorTargetCount, boundPipeline);
                    break;
                }
                case DrawKind::EnvMap:
                {
                    const EnvMapDrawCommand& c = envMapDrawCommands_[ref.index];
                    if (c.uploadedVertexBuffer != nullptr && c.texture && c.envMapTexture && c.target == target)
                        IssueEnvMapDraw(pass, cmd, c, colorFormat, sampleCount,
                                        depthStencilFormat, colorTargetCount, boundPipeline);
                    break;
                }
                case DrawKind::Skinned:
                {
                    const SkinnedDrawCommand& c = skinnedDrawCommands_[ref.index];
                    if (c.uploadedVertexBuffer != nullptr && c.uploadedBoneBuffer != nullptr && c.texture && c.target == target)
                        IssueSkinnedDraw(pass, cmd, c, colorFormat, sampleCount,
                                         depthStencilFormat, colorTargetCount, boundPipeline);
                    break;
                }
                case DrawKind::Pbr:
                {
                    const PbrDrawCommand& c = pbrDrawCommands_[ref.index];
                    if (c.uploadedVertexBuffer != nullptr && c.texture
                        && (!c.skinned || c.uploadedBoneBuffer != nullptr) && c.target == target)
                        IssuePbrDraw(pass, cmd, c, colorFormat, sampleCount,
                                     depthStencilFormat, colorTargetCount, boundPipeline);
                    break;
                }
#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
                case DrawKind::CompiledEffect:
                {
                    const CompiledEffectDrawCommand& c = compiledEffectDrawCommands_[ref.index];
                    if (c.uploadedVertexBuffer != nullptr && c.target == target)
                        IssueCompiledEffectDraw(pass, cmd, c, colorFormat, sampleCount,
                                               depthStencilFormat, colorTargetCount, boundPipeline);
                    break;
                }
#endif
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
                        IssueSpriteDraw(pass, cmd, c, ref.index, spriteVpSize, colorFormat,
                                        sampleCount, depthStencilFormat, colorTargetCount,
                                        boundPipeline);
                    }
                    break;
                }
            }
        }
    }

    void SdlGpuRenderer::ReleaseSceneDrawBuffers(bool clearCommands)
    {
        auto release = [&](SDL_GPUBuffer*& buffer)
        {
            if (buffer != nullptr)
            {
                SDL_ReleaseGPUBuffer(device_, buffer);
                buffer = nullptr;
            }
        };

        for (ColoredDrawCommand& command : coloredDrawCommands_)
        {
            release(command.uploadedVertexBuffer);
            release(command.uploadedIndexBuffer);
        }
        if (clearCommands) coloredDrawCommands_.clear();
        for (TexturedDrawCommand& command : texturedDrawCommands_)
        {
            release(command.uploadedVertexBuffer);
            release(command.uploadedIndexBuffer);
        }
        if (clearCommands) texturedDrawCommands_.clear();
        for (LitTexturedDrawCommand& command : litTexturedDrawCommands_)
        {
            release(command.uploadedVertexBuffer);
            release(command.uploadedIndexBuffer);
        }
        if (clearCommands) litTexturedDrawCommands_.clear();
        for (AlphaTestDrawCommand& command : alphaTestDrawCommands_)
        {
            release(command.uploadedVertexBuffer);
            release(command.uploadedIndexBuffer);
        }
        if (clearCommands) alphaTestDrawCommands_.clear();
        for (DualTextureDrawCommand& command : dualTextureDrawCommands_)
        {
            release(command.uploadedVertexBuffer);
            release(command.uploadedIndexBuffer);
        }
        if (clearCommands) dualTextureDrawCommands_.clear();
        for (EnvMapDrawCommand& command : envMapDrawCommands_)
        {
            release(command.uploadedVertexBuffer);
            release(command.uploadedIndexBuffer);
        }
        if (clearCommands) envMapDrawCommands_.clear();
        for (SkinnedDrawCommand& command : skinnedDrawCommands_)
        {
            release(command.uploadedVertexBuffer);
            release(command.uploadedIndexBuffer);
            release(command.uploadedBoneBuffer);
        }
        if (clearCommands) skinnedDrawCommands_.clear();
        for (PbrDrawCommand& command : pbrDrawCommands_)
        {
            release(command.uploadedVertexBuffer);
            release(command.uploadedIndexBuffer);
            release(command.uploadedBoneBuffer);
        }
        if (clearCommands) pbrDrawCommands_.clear();
#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
        for (CompiledEffectDrawCommand& command : compiledEffectDrawCommands_)
        {
            release(command.uploadedVertexBuffer);
            release(command.uploadedIndexBuffer);
        }
        if (clearCommands) compiledEffectDrawCommands_.clear();
#endif
    }

    // REMED-GFX-DECL-GUARD: the declaration-fidelity boundary. This renderer selects its
    // SDL_GPUVertexAttribute set from the stride (REMED-GFX-217), so a declaration that set cannot
    // represent is refused before a pipeline is created or a command queued. An unlisted stride is
    // left to this renderer's own established rejection -- the unmatched-shape fall-through reaches
    // QueueColoredDraw, which refuses anything but a stride-16 buffer.
    static void RequireFaithfulDeclarationEXT(const IVertexBufferRenderer& vb, const char* route)
    {
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferRenderer&>(vb);
        CNA::Internal::Graphics::RequireFaithfulVertexDeclaration(
            sdlGpuVb.Declaration(), static_cast<int>(sdlGpuVb.Stride()),
            CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt,
            "SDL_GPU", route);
    }

    void SdlGpuRenderer::DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                                       const Matrix& world, const Matrix& view, const Matrix& projection,
                                                       PrimitiveType primitive, int primitiveCount)
    {
        QueueColoredDraw(vb, nullptr, world, view, projection, primitive, primitiveCount);
    }

    void SdlGpuRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                                              const IIndexBufferRenderer& ib,
                                                              const Matrix& world, const Matrix& view, const Matrix& projection,
                                                              PrimitiveType primitive, int primitiveCount)
    {
        QueueColoredDraw(vb, &ib, world, view, projection, primitive, primitiveCount);
    }

    void SdlGpuRenderer::DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                                  PrimitiveType primitive, int primitiveCount,
                                                  const GpuDrawParams& params)
    {
#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
        // plan_fx.md FX-071: a compiled effect's vertex layout is arbitrary and validated against
        // the applied pass's own shader reflection (BuildCompiledEffectVertexAttributes), not
        // against the fixed-stride table RequireFaithfulDeclarationEXT enforces below -- so this
        // dispatches before that guard runs, not after.
        if (params.compiledEffectRuntime != nullptr)
        {
            QueueCompiledEffectDraw(vb, nullptr, primitive, primitiveCount, params);
            return;
        }
#endif
        RequireFaithfulDeclarationEXT(vb, "ordinary-nonindexed");
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferRenderer&>(vb);
        const std::size_t stride = sdlGpuVb.Stride();
        // Matches VulkanRenderer/WebGPURenderer's own dispatch precedence: PBR keeps its shader
        // when MASK coverage is active, while the standalone alpha-test effect still wins over
        // the other non-PBR effect families. Env-map/PBR/skinned win over plain lit_textured3d
        // (env-map shares stride 32, skinned has its own stride 52/56, PBR its own stride 48/68).
        // needsPbr is checked (and excluded from needsSkinned) before needsSkinned since
        // SkinnedPbrEffect::FillGpuDrawParams() sets BOTH params.pbr and params.skinned -- the PBR
        // shader variant, not the plain SkinnedEffect one, must win for that combination.
        const bool needsPbr = params.pbr;
        const bool needsAlphaTest = !needsPbr &&
                                    (params.alphaTest[2] < 0.0f || params.alphaTest[3] < 0.0f);
        const bool needsDualTexture = !needsAlphaTest && params.dualTexture;
        const bool needsEnvMap = !needsAlphaTest && !needsDualTexture && params.envMapping;
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

    void SdlGpuRenderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                                         const Matrix& world, const Matrix& view, const Matrix& projection,
                                                         PrimitiveType primitive, int primitiveCount,
                                                         const GpuDrawParams& params)
    {
#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
        // plan_fx.md FX-071: see DrawPrimitivesEx's identical guard for why this dispatches before
        // RequireFaithfulDeclarationEXT rather than after.
        if (params.compiledEffectRuntime != nullptr)
        {
            QueueCompiledEffectDraw(vb, &ib, primitive, primitiveCount, params);
            return;
        }
#endif
        RequireFaithfulDeclarationEXT(vb, "ordinary-indexed");
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferRenderer&>(vb);
        const std::size_t stride = sdlGpuVb.Stride();
        const bool needsPbr = params.pbr;
        const bool needsAlphaTest = !needsPbr &&
                                    (params.alphaTest[2] < 0.0f || params.alphaTest[3] < 0.0f);
        const bool needsDualTexture = !needsAlphaTest && params.dualTexture;
        const bool needsEnvMap = !needsAlphaTest && !needsDualTexture && params.envMapping;
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

    // ---- SdlGpuSampledTextureState / texture resolution (REMED-GFX-152) ----

    SdlGpuSampledTextureState::~SdlGpuSampledTextureState()
    {
        // Deferred, not immediate -- a draw queued this frame is only issued at
        // EnsureFrameRendered(), so a public Texture2D/TextureCube destroyed in between must leave
        // its native handle valid until that command buffer has been submitted. Same contract (and
        // same drain point) as SdlGpuRenderTarget2DState; see QueueTextureRelease's own doc comment.
        //
        // Honest scope: an immediate SDL_ReleaseGPUTexture here was A/B-measured and produced
        // neither a wrong pixel nor an ASan report, because SDL_gpu defers the real destruction
        // internally. This is therefore hardening, not a defect that was observed failing -- but
        // the commands now store a raw native handle, and relying on another library's internal
        // deferral to keep it valid is exactly the kind of unstated assumption REMED-GFX-152
        // exists to remove. Ordinary textures and render targets follow ONE lifetime rule.
        if (owner != nullptr)
            owner->QueueTextureRelease(texture);
    }

    SdlGpuSampledTextureEXT ResolveSampledTextureEXT(const ITextureRenderer* texture, const char* usage)
    {
        if (texture == nullptr)
            return {};
        // dynamic_cast, never static_cast: a public Texture2D and a public RenderTarget2D arrive
        // here as the same static type but are unrelated sibling classes, so the object -- not the
        // call site -- has to answer which one this is (REMED-GFX-152).
        if (const auto* plain = dynamic_cast<const SdlGpuTextureRenderer*>(texture))
            return plain->Sampled();
        if (const auto* target = dynamic_cast<const SdlGpuRenderTargetRenderer*>(texture))
            return target->Sampled();
        throw std::invalid_argument(
            std::string("CNA SDL_GPU: ") + usage +
            " received a texture this renderer cannot sample -- it is neither an SDL_GPU Texture2D "
            "nor an SDL_GPU RenderTarget2D (a resource from another graphics renderer?)");
    }

    SdlGpuSampledTextureEXT ResolveSampledCubeEXT(const ITextureCubeRenderer* texture, const char* usage)
    {
        if (texture == nullptr)
            return {};
        if (const auto* plain = dynamic_cast<const SdlGpuTextureCubeRenderer*>(texture))
            return plain->Sampled();
        if (const auto* target = dynamic_cast<const SdlGpuRenderTargetCubeRenderer*>(texture))
            return target->Sampled();
        throw std::invalid_argument(
            std::string("CNA SDL_GPU: ") + usage +
            " received a cube texture this renderer cannot sample -- it is neither an SDL_GPU "
            "TextureCube nor an SDL_GPU RenderTargetCube (a resource from another graphics renderer?)");
    }

    // ---- SdlGpuTextureRenderer ----

    SdlGpuTextureRenderer::SdlGpuTextureRenderer(SdlGpuRenderer& owner, const ImageData& data)
        : owner_(&owner),
          state_(std::make_shared<SdlGpuSampledTextureState>()),
          width_(data.width), height_(data.height)
    {
        state_->owner = &owner;

        // REMED-GFX-176: a zero or negative extent has to be refused HERE, before SDL is asked for
        // a texture, or createInfo.width/height wrap into an enormous Uint32 and the failure is
        // reported as an out-of-memory device error rather than as the invalid request it is.
        if (width_ <= 0 || height_ <= 0)
            throw std::invalid_argument(
                "CNA SDL_GPU: Texture2D dimensions must be positive (got " +
                std::to_string(width_) + "x" + std::to_string(height_) + ")");

        // The declared chain, from the ONE authority on it: ImageData::mipLevels, which
        // Texture2D's own constructor fills from CalculateMipLevels(w, h) for mipMap=true and
        // leaves at 1 otherwise. This class used to ignore the field entirely and hardcode 1, so
        // NO Texture2D on this renderer ever had a second level to select between.
        const int maxLevels = CalculateMipLevels(width_, height_);
        const int requestedLevels = std::max(1, data.mipLevels);
        // Never silently capped: a request beyond what these dimensions can mathematically halve
        // down to is a caller error, and quietly shortening the chain would make the texture
        // sample correctly while reporting a LevelCount the resource does not have.
        if (requestedLevels > maxLevels)
            throw std::invalid_argument(
                "CNA SDL_GPU: Texture2D requested " + std::to_string(requestedLevels) +
                " mip levels but " + std::to_string(width_) + "x" + std::to_string(height_) +
                " has at most " + std::to_string(maxLevels));
        levelCount_ = requestedLevels;

        SDL_GPUTextureCreateInfo createInfo{};
        createInfo.type = SDL_GPU_TEXTURETYPE_2D;
        createInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        createInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        createInfo.width = static_cast<Uint32>(width_);
        createInfo.height = static_cast<Uint32>(height_);
        createInfo.layer_count_or_depth = 1;
        createInfo.num_levels = static_cast<Uint32>(levelCount_);
        createInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;

        state_->texture = SDL_CreateGPUTexture(owner_->Device(), &createInfo);
        if (state_->texture == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create Texture2D: ") + SDL_GetError());

        if (TextureTraceEnabled())
        {
            traceId_ = NextTextureTraceId();
            std::fprintf(stderr,
                         "[cna-sdlgpu-texture] id=%d created %dx%d format=R8G8B8A8_UNORM "
                         "usage=SAMPLER requestedLevels=%d maxLevels=%d nativeLevels=%u "
                         "texture=%p\n",
                         traceId_, width_, height_, requestedLevels, maxLevels,
                         static_cast<unsigned>(createInfo.num_levels),
                         static_cast<void*>(state_->texture));
            std::fflush(stderr);
        }

        // Only level 0 is written here, and only with what the caller already supplied. The
        // remaining declared levels are ALLOCATED, never generated -- SDL_GenerateMipmapsForGPU-
        // Texture is deliberately not called (XNA/FNA give Texture2D no implicit regeneration, and
        // REMED-GFX-175's contract makes level content the caller's).
        UpdatePixels(data.pixels.data(), width_ * 4);
        // A throwing UpdatePixels no longer needs its own cleanup: state_ is already constructed,
        // so unwinding destroys it and its destructor releases the texture through the same
        // deferred path every other exit uses.
    }

    SdlGpuTextureRenderer::~SdlGpuTextureRenderer()
    {
        if (TextureTraceEnabled() && traceId_ != 0)
        {
            std::fprintf(stderr, "[cna-sdlgpu-texture] id=%d destroyed levels=%d texture=%p\n",
                         traceId_, levelCount_, static_cast<void*>(state_->texture));
            std::fflush(stderr);
        }
    }

    void SdlGpuTextureRenderer::UpdatePixels(const uint8_t* rgba, int stride)
    {
        UploadLevel(0, rgba, width_, height_, stride);
    }

    void SdlGpuTextureRenderer::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        // Same convention as VulkanTextureRenderer::UpdatePixelsLevel: this interface method
        // returns void, so an out-of-range level cannot be reported and must not be guessed at
        // either -- reaching SDL_UploadToGPUTexture with a nonexistent subresource is exactly the
        // class of failure REMED-GFX-135 removed from the Texture3D path.
        if (rgba == nullptr || level < 0 || level >= levelCount_)
        {
            if (TextureTraceEnabled() && traceId_ != 0)
            {
                std::fprintf(stderr,
                             "[cna-sdlgpu-texture] id=%d level=%d IGNORED reason=%s levels=%d\n",
                             traceId_, level, rgba == nullptr ? "null-source" : "level-out-of-range",
                             levelCount_);
                std::fflush(stderr);
            }
            return;
        }
        UploadLevel(level, rgba, levelW, levelH, levelW * 4);
    }

    void SdlGpuTextureRenderer::UploadLevel(int level, const uint8_t* rgba, int levelW, int levelH,
                                            int stride)
    {
        // The destination extent is the LEVEL's own size, never the resource's. Uploading
        // width_ x height_ into level 1 is not a slightly-too-large copy -- SDL rejects it, and
        // before REMED-GFX-176 there was no other level to get it wrong for.
        const int expectedW = std::max(1, width_ >> level);
        const int expectedH = std::max(1, height_ >> level);
        if (levelW != expectedW || levelH != expectedH)
            throw std::invalid_argument(
                "CNA SDL_GPU: Texture2D level " + std::to_string(level) + " of a " +
                std::to_string(width_) + "x" + std::to_string(height_) + " texture is " +
                std::to_string(expectedW) + "x" + std::to_string(expectedH) + ", not " +
                std::to_string(levelW) + "x" + std::to_string(levelH));

        SDL_GPUDevice* device = owner_->Device();
        const Uint32 rowBytes = static_cast<Uint32>(levelW) * 4;
        const Uint32 sizeBytes = rowBytes * static_cast<Uint32>(levelH);

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
            for (int y = 0; y < levelH; ++y)
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
        source.pixels_per_row = static_cast<Uint32>(levelW);
        source.rows_per_layer = static_cast<Uint32>(levelH);
        SDL_GPUTextureRegion destination{};
        destination.texture = state_->texture;
        destination.mip_level = static_cast<Uint32>(level);
        destination.w = static_cast<Uint32>(levelW);
        destination.h = static_cast<Uint32>(levelH);
        destination.d = 1;
        // cycle: a ONE-level texture keeps the original cycle=true, where "swap to a fresh
        // resource rather than stall on an in-flight read" is free because the whole resource is
        // being replaced anyway. A CHAIN must not cycle: cycling discards every level this upload
        // does not write, so level 0 followed by level 1 would leave level 0 orphaned on an
        // abandoned resource. That is not a hypothetical -- it is the exact failure SDLGPU-40
        // measured on the cube path and REMED-GFX-135 on the volume path, and both landed on
        // cycle=false for the same reason.
        const bool cycle = levelCount_ == 1;
        SDL_UploadToGPUTexture(copyPass, &source, &destination, cycle);
        SDL_EndGPUCopyPass(copyPass);
        if (!SDL_SubmitGPUCommandBuffer(cmd))
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: SDL_SubmitGPUCommandBuffer (texture upload) failed: ") + SDL_GetError());
        }
        SDL_ReleaseGPUTransferBuffer(device, transferBuffer);

        if (TextureTraceEnabled() && traceId_ != 0)
        {
            std::fprintf(stderr,
                         "[cna-sdlgpu-texture] id=%d level=%d levelDims=%dx%d mip_level=%u "
                         "region=(0,0,%ux%u) transferBytes=%u rowPitch=%u pixelsPerRow=%u "
                         "srcStride=%d cycle=%s submit=ok\n",
                         traceId_, level, levelW, levelH,
                         static_cast<unsigned>(destination.mip_level),
                         static_cast<unsigned>(destination.w), static_cast<unsigned>(destination.h),
                         static_cast<unsigned>(sizeBytes), static_cast<unsigned>(rowBytes),
                         static_cast<unsigned>(source.pixels_per_row), stride,
                         cycle ? "yes" : "no");
            std::fflush(stderr);
        }
    }

    // ---- SdlGpuTexture3DRenderer (Phase SDLGPU-9, SDLGPU-40/SDLGPU-41) ----

    SdlGpuTexture3DRenderer::SdlGpuTexture3DRenderer(SdlGpuRenderer& owner, int width, int height,
                                                    int depth, bool mipMap)
        : owner_(&owner), width_(width), height_(height), depth_(depth)
        , levelCount_(mipMap ? CalculateMipLevels(width, height) : 1)
    {
        SDL_GPUTextureCreateInfo createInfo{};
        createInfo.type = SDL_GPU_TEXTURETYPE_3D;
        createInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        // Plain XNA/FNA Texture3D mip levels are explicitly authored with SetData; construction
        // never makes the resource a render target and a level-0 upload never regenerates the
        // chain. Keeping COLOR_TARGET out also avoids asking SDL 3.5.0's Vulkan renderer to build
        // illegal 2D depth-plane render-target views for shrunken 3D mip depths (REMED-GFX-099).
        createInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        createInfo.width = static_cast<Uint32>(width);
        createInfo.height = static_cast<Uint32>(height);
        createInfo.layer_count_or_depth = static_cast<Uint32>(depth);
        // Mirrors FNA's Texture3D constructor: LevelCount = mipMap ? CalculateMipLevels(width, height)
        // : 1 -- depth does not participate in the mip-level count.
        createInfo.num_levels = mipMap ? static_cast<Uint32>(CalculateMipLevels(width, height)) : 1;
        createInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;

        texture_ = SDL_CreateGPUTexture(owner_->Device(), &createInfo);
        if (texture_ == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create Texture3D: ") + SDL_GetError());
    }

    SdlGpuTexture3DRenderer::~SdlGpuTexture3DRenderer()
    {
        if (texture_ != nullptr)
            SDL_ReleaseGPUTexture(owner_->Device(), texture_);
    }

    bool SdlGpuTexture3DRenderer::SetData(int level, int x, int y, int z, int w, int h, int depth,
                                         const void* data, int dataLength)
    {
        // REMED-GFX-135: `w <= 0` used to be a silent `return` the shared layer read as a completed
        // upload, and neither the null source nor the level/box range was checked at all -- an
        // out-of-range level reached SDL_UploadToGPUTexture as a nonexistent subresource.
        if (data == nullptr || w <= 0 || h <= 0 || depth <= 0) return false;
        if (level < 0 || level >= levelCount_) return false;
        const int levelW = std::max(1, width_ >> level);
        const int levelH = std::max(1, height_ >> level);
        const int levelD = std::max(1, depth_ >> level);
        if (x < 0 || y < 0 || z < 0 || x + w > levelW || y + h > levelH || z + depth > levelD)
            return false;
        const Uint32 sizeBytes = static_cast<Uint32>(w) * static_cast<Uint32>(h) * static_cast<Uint32>(depth) * 4;
        if (dataLength < 0 || static_cast<Uint32>(dataLength) < sizeBytes) return false;

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
        // cycle=false: unlike SdlGpuTextureRenderer::UpdatePixels's single full-texture replace
        // (where cycle=true's "swap to a fresh resource" avoids stalling on an in-flight read),
        // Texture3D content is built up via multiple independent sub-volume/per-level SetData
        // calls that must all land on the SAME underlying resource -- cycle=true here silently
        // orphaned earlier partial writes onto an abandoned resource (found via a real byte-exact
        // round-trip test: an off-center sub-volume written first came back as zero/uninitialized
        // once a second sub-volume was written afterward).
        SDL_UploadToGPUTexture(copyPass, &source, &destination, false);
        SDL_EndGPUCopyPass(copyPass);

        if (!SDL_SubmitGPUCommandBuffer(cmd))
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: Texture3D::SetData: SDL_SubmitGPUCommandBuffer failed: ") + SDL_GetError());
        }
        SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
        return true;
    }

    bool SdlGpuTexture3DRenderer::GetData(int level, int x, int y, int z, int w, int h, int depth,
                                         void* data, int dataLength) const
    {
        // REMED-GFX-130: a silent `return` here was converted by the shared layer into a complete
        // transparent-black volume rather than a refusal.
        if (w <= 0 || h <= 0 || depth <= 0 || data == nullptr || level < 0)
            return false;
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
        return true;
    }

    // ---- SdlGpuTextureCubeRenderer (Phase SDLGPU-9, SDLGPU-51) ----

    SdlGpuTextureCubeRenderer::SdlGpuTextureCubeRenderer(SdlGpuRenderer& owner, int size, bool mipMap)
        : owner_(&owner)
        , state_(std::make_shared<SdlGpuSampledTextureState>())
        , size_(size), mipMap_(mipMap)
        , levelCount_(mipMap ? CalculateMipLevels(size, size) : 1)
    {
        state_->owner = &owner;

        SDL_GPUTextureCreateInfo createInfo{};
        createInfo.type = SDL_GPU_TEXTURETYPE_CUBE;
        createInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        // SDL_gpu's own debug validation requires COLOR_TARGET usage (in addition to SAMPLER) on
        // any texture SDL_GenerateMipmapsForGPUTexture is called on -- see
        // SdlGpuTexture3DRenderer's own identical constructor comment for the real finding this fix
        // came from. Only widened when mipMap is actually requested.
        createInfo.usage = mipMap_ ? (SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET)
                                    : SDL_GPU_TEXTUREUSAGE_SAMPLER;
        createInfo.width = static_cast<Uint32>(size);
        createInfo.height = static_cast<Uint32>(size);
        createInfo.layer_count_or_depth = 6;
        // Mirrors FNA's TextureCube constructor: LevelCount = mipMap ? CalculateMipLevels(size, size) : 1.
        createInfo.num_levels = mipMap_ ? static_cast<Uint32>(CalculateMipLevels(size, size)) : 1;
        createInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;

        state_->texture = SDL_CreateGPUTexture(owner_->Device(), &createInfo);
        if (state_->texture == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create TextureCube: ") + SDL_GetError());
    }

    SdlGpuTextureCubeRenderer::~SdlGpuTextureCubeRenderer() = default;

    bool SdlGpuTextureCubeRenderer::SetData(int face, int level, int x, int y, int w, int h,
                                           const void* data, int dataLength)
    {
        // REMED-GFX-135: see SdlGpuTexture3DRenderer::SetData -- silent returns looked like writes,
        // and neither the face nor the level was range-checked before reaching SDL.
        if (data == nullptr || w <= 0 || h <= 0) return false;
        if (face < 0 || face >= 6 || level < 0 || level >= levelCount_) return false;
        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize) return false;
        const Uint32 sizeBytes = static_cast<Uint32>(w) * static_cast<Uint32>(h) * 4;
        if (dataLength < 0 || static_cast<Uint32>(dataLength) < sizeBytes) return false;

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
        destination.texture = state_->texture;
        destination.mip_level = static_cast<Uint32>(level);
        destination.layer = static_cast<Uint32>(face);
        destination.x = static_cast<Uint32>(x);
        destination.y = static_cast<Uint32>(y);
        destination.w = static_cast<Uint32>(w);
        destination.h = static_cast<Uint32>(h);
        destination.d = 1;
        // cycle=false, same rationale as SdlGpuTexture3DRenderer::SetData: a cube map is built up via
        // multiple independent per-face SetData calls that must all land on the SAME resource --
        // cycle=true would silently orphan earlier faces' writes (the bug SDLGPU-40 found and fixed).
        SDL_UploadToGPUTexture(copyPass, &source, &destination, false);
        SDL_EndGPUCopyPass(copyPass);

        // "Generated case": a full level-0 upload of a given face with mips requested regenerates
        // the whole chain (all 6 faces) immediately -- matches SdlGpuRenderTargetCubeRenderer's own
        // "regenerates all faces" convention (SDL_gpu has no per-layer mip-regen control), and
        // real XNA/FNA has no explicit "regenerate mips" call for TextureCube either. Must run
        // outside any pass, per SDL_gpu.h.
        const bool isFullLevel0Upload = level == 0 && x == 0 && y == 0 && w == size_ && h == size_;
        if (mipMap_ && isFullLevel0Upload)
            SDL_GenerateMipmapsForGPUTexture(cmd, state_->texture);

        if (!SDL_SubmitGPUCommandBuffer(cmd))
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: TextureCube::SetData: SDL_SubmitGPUCommandBuffer failed: ") + SDL_GetError());
        }
        SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
        return true;
    }

    bool SdlGpuTextureCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                           void* data, int dataLength) const
    {
        // REMED-GFX-130: see SdlGpuTexture3DRenderer::GetData above.
        if (w <= 0 || h <= 0 || data == nullptr || level < 0 || face < 0 || face >= 6)
            return false;
        // REMED-GFX-134: a level this cube never allocated has no content to return, and
        // SDL_DownloadFromGPUTexture answers such a request anyway -- the shared layer would then
        // convert an untouched transfer buffer into a face. Same for a rectangle that leaves the
        // level. Refusing here is what makes the public call raise System::NotSupportedException
        // with the caller's destination untouched.
        if (level >= levelCount_) return false;
        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize) return false;
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
        region.texture = state_->texture;
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
        return true;
    }

    // ---- SdlGpuRenderTargetRenderer (Phase SDLGPU-8, SDLGPU-35) ----

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

    SdlGpuRenderTargetRenderer::SdlGpuRenderTargetRenderer(SdlGpuRenderer& owner, int width, int height,
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
        // REMED-GFX-186: mipMap and multiSampleCount are INDEPENDENT, and they always were --
        // they are properties of two DIFFERENT resources. The mip chain lives on the
        // single-sample colorTexture below; the multisample attachment created further down is a
        // separate COLOR_TARGET-only texture with num_levels = 1 that never owns a mip level at
        // all. Suppressing mipMap here left the public LevelCount (which XNA derives from mipMap
        // alone) describing five levels of a native resource that had one, and GetData then handed
        // SDL a mip index the texture did not own.
        //
        // This is exactly what FNA does: RenderTarget2D forwards mipMap to its Texture2D base
        // regardless of preferredMultiSampleCount, and FNA3D's own SDL_GPU driver allocates the
        // full levelCount on the single-sample texture while SDLGPU_GenColorRenderbuffer creates
        // the multisample attachment with one level. The two jobs are then ordered by
        // FNA3D_ResolveTarget at unbind -- resolve into level 0, then regenerate the chain from it
        // -- which is precisely what RenderToTarget already does here (the render pass' own
        // resolve_texture store action, then GenerateRenderTargetMipChain after the pass ends).
        mipMap_ = mipMap;
        state_->mipMap = mipMap_;

        SDL_GPUTextureCreateInfo colorInfo{};
        colorInfo.type = SDL_GPU_TEXTURETYPE_2D;
        colorInfo.format = kFormat;
        colorInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        colorInfo.width = static_cast<Uint32>(width);
        colorInfo.height = static_cast<Uint32>(height);
        colorInfo.layer_count_or_depth = 1;
        colorInfo.num_levels = mipMap_ ? static_cast<Uint32>(CalculateMipLevels(width, height)) : 1;
        // REMED-GFX-186: what SDL really allocated, so GetData can refuse a level with no storage.
        levelCount_ = static_cast<int>(colorInfo.num_levels);
        state_->levelCount = levelCount_;
        colorInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;  // the sampleable texture itself is always single-sample

        state_->colorTexture = SDL_CreateGPUTexture(device, &colorInfo);
        if (state_->colorTexture == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create RenderTarget2D color texture: ") + SDL_GetError());

        if (multiSampleCount_ > 0)
        {
            // The real multisampled render target, resolved into colorTexture automatically via
            // SDL_GPUColorTargetInfo.resolve_texture at render-pass end -- same mechanism as
            // SdlGpuRenderTargetCubeRenderer's own MSAA support.
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

    SdlGpuRenderTargetRenderer::~SdlGpuRenderTargetRenderer()
    {
        if (owner_->currentRenderTarget_ == this)
        {
            owner_->currentRenderTarget_ = nullptr;
            // Destroying the bound target ends its bind cycle exactly like an explicit unbind.
            owner_->EndRenderTargetSegment();
        }
        // state_ itself is NOT removed from the pending segments here -- if it's still referenced
        // there (or by a queued DrawCommand's DrawTarget), this shared_ptr going out of scope just
        // drops OUR reference; the state survives via those other references until
        // EnsureFrameRendered() finishes with it, so this target's own pending Clear()/draws still
        // render correctly even though this wrapper is gone. See SdlGpuRenderTarget2DState's own
        // doc comment.
    }

    void SdlGpuRenderTargetRenderer::BindAsRenderTarget()
    {
        owner_->currentRenderTarget_ = this;
        // Mutually exclusive with a bound RenderTargetCube face -- matches real XNA
        // single-current-target semantics (SetRenderTarget always replaces whatever was there).
        if (owner_->currentRenderTargetCube_ != nullptr)
        {
            owner_->currentRenderTargetCube_ = nullptr;
            owner_->currentActiveCubeFace_ = -1;
        }
        // REMED-GFX-145: unconditionally a NEW logical pass, including a rebind of the target that
        // is already current -- that rebind is a real boundary (its own load action, clear state,
        // viewport and scissor), and treating it as "no change" was this finding's whole subject.
        owner_->BeginRenderTargetSegment(state_);
    }

    void SdlGpuRenderTargetRenderer::AttachToCurrentSegment()
    {
        if (SdlGpuRenderer::PassSegment* segment = owner_->CurrentSegment())
            segment->extraAttachments.push_back(state_);
        owner_->framePending_ = true;
    }

    void SdlGpuRenderTargetRenderer::UnbindAsRenderTarget()
    {
        if (owner_->currentRenderTarget_ == this)
        {
            owner_->currentRenderTarget_ = nullptr;
            owner_->EndRenderTargetSegment();
        }
    }

    bool SdlGpuRenderTargetRenderer::GetData(int level, int x, int y, int w, int h,
                                            void* data, int dataLength) const
    {
        // REMED-GFX-127: nothing was written, so this must not be reported as a completed readback.
        if (w <= 0 || h <= 0)
            return false;
        // REMED-GFX-186: a level this resource does not own must be refused HERE, before any
        // native call. SDL_DownloadFromGPUTexture does not validate mip_level even with the
        // device's debug mode enabled -- its Vulkan driver indexes its own per-subresource array
        // with the value and dereferences the result, which was measured as a SIGSEGV (READ at
        // 0x40, the null page) inside VULKAN_DownloadFromTexture, on the caller's own thread. The
        // sibling routes learned this already: SdlGpuRenderTargetCubeRenderer (REMED-GFX-134),
        // SdlGpuTexture3DRenderer and SdlGpuTextureCubeRenderer (REMED-GFX-135) all range-check the
        // level against what SDL really allocated; this one never did, which is why an
        // out-of-range level was a signal here and a public exception everywhere else.
        if (level < 0 || level >= levelCount_)
            throw std::out_of_range(
                "CNA SDL_GPU: RenderTarget2D::GetData: mip level " + std::to_string(level) +
                " does not exist (this target has " + std::to_string(levelCount_) +
                (levelCount_ == 1 ? " level)" : " levels)"));
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
        // resolved-into by the time any frame's pass has run, even when this target is MSAA, and
        // (REMED-GFX-186) the only resource that owns this target's mip chain at all. The
        // multisample attachment is created with num_levels = 1 and is never a legal source here.
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
        if (TargetReadbackTraceEnabled())
        {
            std::fprintf(stderr,
                         "[cna-sdlgpu-target-readback] target=%dx%d appliedSamples=%d mipMap=%d "
                         "nativeLevels=%d requestedLevel=%d levelDims=%dx%d msaaTex=%p "
                         "resolvedTex=%p source=%s region=(%u,%u %ux%u) mip_level=%u layer=%u d=%u "
                         "pixels_per_row=%u rows_per_layer=%u transferBytes=%u\n",
                         state_->width, state_->height, multiSampleCount_, mipMap_ ? 1 : 0,
                         levelCount_, level,
                         std::max(1, state_->width >> level), std::max(1, state_->height >> level),
                         static_cast<void*>(state_->msaaTexture),
                         static_cast<void*>(state_->colorTexture),
                         region.texture == state_->colorTexture ? "resolved" : "OTHER",
                         region.x, region.y, region.w, region.h,
                         static_cast<unsigned>(region.mip_level),
                         static_cast<unsigned>(region.layer), static_cast<unsigned>(region.d),
                         dest.pixels_per_row, dest.rows_per_layer,
                         static_cast<unsigned>(sizeBytes));
            std::fflush(stderr);
        }
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
        return true;
    }

    // ---- SdlGpuRenderTargetCubeRenderer (Phase SDLGPU-8, SDLGPU-36) ----

    SdlGpuRenderTargetCubeState::~SdlGpuRenderTargetCubeState()
    {
        // Deferred -- see SdlGpuRenderTarget2DState's own destructor / QueueTextureRelease's doc
        // comment for why (some other still-pending draw may sample cubeTexture as an
        // EnvironmentMapEffect input, independent of the pending pass segments).
        owner->QueueTextureRelease(depthTexture);
        // REMED-GFX-141: six per-face multisample textures now, released exactly once each.
        for (SDL_GPUTexture* msaa : msaaTextures) owner->QueueTextureRelease(msaa);
        owner->QueueTextureRelease(cubeTexture);
    }

    SdlGpuRenderTargetCubeRenderer::SdlGpuRenderTargetCubeRenderer(SdlGpuRenderer& owner, int size,
                                                                  int depthFormat, bool preserveContents,
                                                                  bool mipMap, int multiSampleCount)
        : owner_(&owner), mipMap_(mipMap)
    {
        state_ = std::make_shared<SdlGpuRenderTargetCubeState>();
        state_->owner = owner_;
        state_->size = size;
        // REMED-GFX-141: only the multisampled path consults it -- see the field's own comment.
        state_->preserveContents = preserveContents;

        SDL_GPUDevice* device = owner_->Device();
        constexpr SDL_GPUTextureFormat kFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

        const SDL_GPUSampleCount sampleCount = ClampSampleCount(device, kFormat, multiSampleCount);
        multiSampleCount_ = SampleCountToInt(sampleCount);
        state_->sampleCount = sampleCount;
        // The resolved cube owns the public mip chain even when rendering uses a separate
        // level-zero-only multisample attachment (REMED-GFX-188).
        state_->mipMap = mipMap_;

        SDL_GPUTextureCreateInfo cubeInfo{};
        cubeInfo.type = SDL_GPU_TEXTURETYPE_CUBE;
        cubeInfo.format = kFormat;
        cubeInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        cubeInfo.width = static_cast<Uint32>(size);
        cubeInfo.height = static_cast<Uint32>(size);
        cubeInfo.layer_count_or_depth = 6;
        cubeInfo.num_levels = mipMap_ ? static_cast<Uint32>(CalculateMipLevels(size, size)) : 1;
        // REMED-GFX-188: one shared allocation fact drives finalization, readback and the native
        // diagnostic accessor; it must agree with TextureCube's public LevelCount.
        state_->levelCount = static_cast<int>(cubeInfo.num_levels);
        cubeInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;  // the cube texture itself is always single-sample
        state_->cubeTexture = SDL_CreateGPUTexture(device, &cubeInfo);
        if (state_->cubeTexture == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create RenderTargetCube color texture: ") + SDL_GetError());

        if (multiSampleCount_ > 0)
        {
            // SDL_GPU_TEXTURETYPE_CUBE has no multisampled variant, and SDL_gpu's own debug
            // validation forbids sample_count>1 on ANY array texture ("For array textures:
            // sample_count must be SDL_GPU_SAMPLECOUNT_1"), so a 6-layer multisampled array is not
            // a valid construction here either (found 2026-07-16 once SDLGPU-6 wired debug_mode to
            // a real CNA-side toggle -- previously a silent violation, then a genuine hang).
            //
            // REMED-GFX-141: SIX single-layer SDL_GPU_TEXTURETYPE_2D textures, one per face, where
            // this used to create ONE shared by whichever face was currently active. The shared one
            // could not be preserved by construction: its previous contents belonged to another
            // face, so it had to be cycled on every pass, and cycling is illegal together with
            // SDL_GPU_LOADOP_LOAD -- RenderToTargetCubeFace was forced to clear it every time.
            // Each face's texture resolves into that face's layer of cubeTexture via
            // SDL_GPUColorTargetInfo.resolve_texture/resolve_layer at render-pass end (see
            // RenderToTargetCubeFace) -- no manual ResolveSubresource-equivalent needed.
            SDL_GPUTextureCreateInfo msaaInfo{};
            msaaInfo.type = SDL_GPU_TEXTURETYPE_2D;
            msaaInfo.format = kFormat;
            msaaInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
            msaaInfo.width = static_cast<Uint32>(size);
            msaaInfo.height = static_cast<Uint32>(size);
            msaaInfo.layer_count_or_depth = 1;
            msaaInfo.num_levels = 1;
            state_->msaaLevelCount = static_cast<int>(msaaInfo.num_levels);
            msaaInfo.sample_count = sampleCount;
            for (SDL_GPUTexture*& face : state_->msaaTextures)
            {
                face = SDL_CreateGPUTexture(device, &msaaInfo);
                if (face == nullptr)
                {
                    const std::string what = SDL_GetError();
                    for (SDL_GPUTexture* made : state_->msaaTextures)
                        if (made != nullptr) SDL_ReleaseGPUTexture(device, made);
                    state_->msaaTextures.fill(nullptr);
                    SDL_ReleaseGPUTexture(device, state_->cubeTexture);
                    throw std::runtime_error(
                        "CNA SDL_GPU: failed to create RenderTargetCube MSAA texture: " + what);
                }
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
                const std::string what = SDL_GetError();
                // REMED-GFX-141: all six, not one.
                for (SDL_GPUTexture* msaa : state_->msaaTextures)
                    if (msaa != nullptr) SDL_ReleaseGPUTexture(device, msaa);
                state_->msaaTextures.fill(nullptr);
                SDL_ReleaseGPUTexture(device, state_->cubeTexture);
                throw std::runtime_error(
                    "CNA SDL_GPU: failed to create RenderTargetCube depth texture: " + what);
            }
        }
    }

    SdlGpuRenderTargetCubeRenderer::~SdlGpuRenderTargetCubeRenderer()
    {
        if (owner_->currentRenderTargetCube_ == this)
        {
            owner_->currentRenderTargetCube_ = nullptr;
            owner_->currentActiveCubeFace_ = -1;
            owner_->EndRenderTargetSegment();
        }
        // state_ is NOT removed from the pending segments here -- same rationale as
        // SdlGpuRenderTargetRenderer's own destructor (see SdlGpuRenderTarget2DState's doc comment).
    }

    void SdlGpuRenderTargetCubeRenderer::BindAsRenderTargetFace(int face)
    {
        owner_->currentRenderTargetCube_ = this;
        owner_->currentActiveCubeFace_ = face;
        // Mutually exclusive with a bound RenderTarget2D -- matches real XNA single-current-target
        // semantics (SetRenderTargetCubeFace always replaces whatever was there).
        owner_->currentRenderTarget_ = nullptr;
        // REMED-GFX-145: a new logical pass per bind, so face A -> face B -> face A is three
        // passes. The FACE rides on the segment, so a rebind of the SAME face is a boundary too.
        owner_->BeginCubeFaceSegment(state_, face);
    }

    void SdlGpuRenderTargetCubeRenderer::UnbindAsRenderTarget()
    {
        if (owner_->currentRenderTargetCube_ == this)
        {
            owner_->currentRenderTargetCube_ = nullptr;
            owner_->currentActiveCubeFace_ = -1;
            owner_->EndRenderTargetSegment();
        }
    }

    bool SdlGpuRenderTargetCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                                void* data, int dataLength) const
    {
        // REMED-GFX-130: see SdlGpuTexture3DRenderer::GetData above.
        if (w <= 0 || h <= 0 || data == nullptr || level < 0 || face < 0 || face >= 6)
            return false;
        // REMED-GFX-134/GFX-188: a level this target never allocated has no content to return, and
        // SDL_DownloadFromGPUTexture answers such a request anyway -- the shared layer would then
        // convert an untouched transfer buffer into a face. Same for a rectangle that leaves the
        // level. Refusing here is what makes the public call raise System::NotSupportedException
        // with the caller's destination untouched.
        if (level >= state_->levelCount) return false;
        const int levelSize = std::max(1, state_->size >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize) return false;
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
        if (TargetReadbackTraceEnabled())
        {
            std::fprintf(stderr,
                         "[cna-sdlgpu-target-readback] cube=%dx%d appliedSamples=%d mipMap=%d "
                         "nativeLevels=%d requestedFace=%d requestedLevel=%d levelDims=%dx%d "
                         "msaaTex=%p resolvedTex=%p source=%s region=(%u,%u %ux%u) "
                         "mip_level=%u layer=%u d=%u pixels_per_row=%u rows_per_layer=%u "
                         "transferBytes=%u\n",
                         state_->size, state_->size, multiSampleCount_, mipMap_ ? 1 : 0,
                         state_->levelCount, face, level,
                         std::max(1, state_->size >> level), std::max(1, state_->size >> level),
                         static_cast<void*>(state_->msaaTextures[static_cast<std::size_t>(face)]),
                         static_cast<void*>(state_->cubeTexture),
                         region.texture == state_->cubeTexture ? "resolved" : "OTHER",
                         region.x, region.y, region.w, region.h,
                         static_cast<unsigned>(region.mip_level),
                         static_cast<unsigned>(region.layer), static_cast<unsigned>(region.d),
                         dest.pixels_per_row, dest.rows_per_layer,
                         static_cast<unsigned>(sizeBytes));
            std::fflush(stderr);
        }
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
        return true;
    }

    // ---- SdlGpuVertexBufferRenderer ----

    SdlGpuVertexBufferRenderer::SdlGpuVertexBufferRenderer(SdlGpuRenderer& owner, int vertexCapacity)
        : owner_(&owner), vertexCapacity_(vertexCapacity)
    {
    }

    SdlGpuVertexBufferRenderer::~SdlGpuVertexBufferRenderer()
    {
        if (buffer_ != nullptr)
            SDL_ReleaseGPUBuffer(owner_->Device(), buffer_);
    }

    void SdlGpuVertexBufferRenderer::SetData(const void* data, int vertexCount, std::size_t strideInBytes)
    {
        // SetDataOptions::None/Discard both cycle -- see SetDataWithOptions's own doc comment.
        SetDataWithOptions(data, vertexCount, strideInBytes, SetDataOptions::Discard);
    }

    void SdlGpuVertexBufferRenderer::SetDataWithOptions(const void* data, int vertexCount,
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

    // ---- SdlGpuIndexBufferRenderer ----

    SdlGpuIndexBufferRenderer::SdlGpuIndexBufferRenderer(SdlGpuRenderer& owner, int indexCapacity, bool thirtyTwoBit)
        : owner_(&owner), indexCapacity_(indexCapacity), thirtyTwoBit_(thirtyTwoBit)
    {
    }

    SdlGpuIndexBufferRenderer::~SdlGpuIndexBufferRenderer()
    {
        if (buffer_ != nullptr)
            SDL_ReleaseGPUBuffer(owner_->Device(), buffer_);
    }

    // SetDataOptions::None/Discard both cycle -- see SdlGpuVertexBufferRenderer::SetDataWithOptions's
    // own doc comment for the real Discard/NoOverwrite rationale.
    void SdlGpuIndexBufferRenderer::SetData16(const void* data, int indexCount) { Upload(data, indexCount, false, true); }
    void SdlGpuIndexBufferRenderer::SetData32(const void* data, int indexCount) { Upload(data, indexCount, true, true); }
    void SdlGpuIndexBufferRenderer::SetData16WithOptions(const void* data, int indexCount, SetDataOptions options)
    {
        Upload(data, indexCount, false, options != SetDataOptions::NoOverwrite);
    }
    void SdlGpuIndexBufferRenderer::SetData32WithOptions(const void* data, int indexCount, SetDataOptions options)
    {
        Upload(data, indexCount, true, options != SetDataOptions::NoOverwrite);
    }

    void SdlGpuIndexBufferRenderer::Upload(const void* data, int indexCount, bool dataIsThirtyTwoBit, bool cycle)
    {
        SDL_GPUDevice* device = owner_->Device();
        const std::size_t elementSize = dataIsThirtyTwoBit ? sizeof(std::uint32_t) : sizeof(std::uint16_t);
        const Uint32 sizeBytes = static_cast<Uint32>(indexCount) * static_cast<Uint32>(elementSize);
        // See SdlGpuVertexBufferRenderer::SetDataWithOptions's own comment: SDL_gpu's Vulkan
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

    // ---- SdlGpuEffectRenderer (Phase SDLGPU-10, SDLGPU-42/43) ----

    SdlGpuEffectRenderer::SdlGpuEffectRenderer(SdlGpuRenderer& owner)
        : owner_(&owner)
    {
    }

    SdlGpuEffectRenderer::~SdlGpuEffectRenderer()
    {
        for (auto& [key, pipeline] : pipelines_)
            if (pipeline != nullptr) SDL_ReleaseGPUGraphicsPipeline(owner_->Device(), pipeline);
        if (fragmentShader_ != nullptr) SDL_ReleaseGPUShader(owner_->Device(), fragmentShader_);
        if (vertexShader_ != nullptr) SDL_ReleaseGPUShader(owner_->Device(), vertexShader_);
    }

    bool SdlGpuEffectRenderer::CompileProgram(const std::string& vertSrc, const std::string& fragSrc)
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

    SDL_GPUGraphicsPipeline* SdlGpuEffectRenderer::GetOrCreatePipeline(SDL_GPUTextureFormat colorFormat,
                                                                       SDL_GPUSampleCount sampleCount,
                                                                       SDL_GPUTextureFormat depthStencilFormat,
                                                                       int colorTargetCount,
                                                                       const std::array<int, 4>& colorWriteMasks,
                                                                       float depthBias,
                                                                       float slopeScaleDepthBias)
    {
        if (!valid_)
            return nullptr;
        // colorTargetCount (real MRT, SDLGPU-37 -- every RenderTarget2D in this renderer is
        // R8G8B8A8_UNORM, so all N simultaneous attachments always share one colorFormat) folded
        // into the cache key alongside sample count and active depth/stencil format. INVALID is a
        // real value meaning "no depth/stencil attachment", not a default format: REMED-GFX-097
        // proved that reusing a depth-backed pipeline in that pass violates Vulkan compatibility.
        std::size_t key = static_cast<std::size_t>(colorFormat);
        key = HashCombine(key, static_cast<std::size_t>(SampleCountToInt(sampleCount)));
        key = HashCombine(key, static_cast<std::size_t>(colorTargetCount));
        key = HashCombine(key, static_cast<std::size_t>(depthStencilFormat));
        // Per-slot write masks are static pipeline state. Keep custom-effect reuse aligned with
        // the same active attachment slots represented by this pipeline's target descriptions.
        for (int i = 0; i < colorTargetCount; ++i)
            key = HashCombine(key, static_cast<std::size_t>(colorWriteMasks[i] & 0xF));
        key = HashDepthBias(
            key, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
            depthBias, slopeScaleDepthBias);
        const auto it = pipelines_.find(key);
        if (it != pipelines_.end())
            return it->second;

        // Fixed SpriteVertex-shaped contract (x,y|u,v|r,g,b,a, 32 bytes), matching the stock
        // sprite pipeline's own vertex layout exactly (see GetOrCreateSpritePipeline) -- this is a
        // SpriteBatch-custom-shader facility, not a general arbitrary-vertex-format one.
        SDL_GPUVertexBufferDescription vbDesc{};
        vbDesc.slot = 0;
        vbDesc.pitch = sizeof(SdlGpuRenderer::SpriteVertex);
        vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        SDL_GPUVertexAttribute attrs[3]{};
        attrs[0].location = 0; attrs[0].buffer_slot = 0; attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attrs[0].offset = offsetof(SdlGpuRenderer::SpriteVertex, x);
        attrs[1].location = 1; attrs[1].buffer_slot = 0; attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attrs[1].offset = offsetof(SdlGpuRenderer::SpriteVertex, u);
        attrs[2].location = 2; attrs[2].buffer_slot = 0; attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attrs[2].offset = offsetof(SdlGpuRenderer::SpriteVertex, r);

        std::array<SDL_GPUColorTargetDescription, 4> colorTargets{};
        for (int i = 0; i < colorTargetCount; ++i)
        {
            SDL_GPUColorTargetDescription& colorTarget = colorTargets[i];
            colorTarget.format = colorFormat;
            colorTarget.blend_state.enable_blend = true;
            colorTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            colorTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            colorTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            colorTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            colorTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            colorTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            const Uint8 mask = static_cast<Uint8>(colorWriteMasks[i] & 0xF);
            if (mask != 0xF)
            {
                colorTarget.blend_state.enable_color_write_mask = true;
                colorTarget.blend_state.color_write_mask = mask;
            }
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
        // Preserve this custom SpriteBatch family's established hardcoded fill/cull behavior;
        // REMED-GFX-051 adds only the pipeline-static bias state common to every triangle family.
        FillDepthBiasState(
            pipelineInfo.rasterizer_state,
            pipelineInfo.primitive_type,
            depthBias, slopeScaleDepthBias);
        pipelineInfo.multisample_state.sample_count = sampleCount;
        pipelineInfo.depth_stencil_state.enable_depth_test = false;
        pipelineInfo.depth_stencil_state.enable_depth_write = false;
        pipelineInfo.depth_stencil_state.enable_stencil_test = false;
        pipelineInfo.target_info.color_target_descriptions = colorTargets.data();
        pipelineInfo.target_info.num_color_targets = static_cast<Uint32>(colorTargetCount);
        pipelineInfo.target_info.has_depth_stencil_target =
            (depthStencilFormat != SDL_GPU_TEXTUREFORMAT_INVALID);
        pipelineInfo.target_info.depth_stencil_format = depthStencilFormat;

        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(owner_->Device(), &pipelineInfo);
        if (pipeline == nullptr)
        {
            compileError_ = std::string("SDL_CreateGPUGraphicsPipeline failed: ") + SDL_GetError();
            return nullptr;
        }
        pipelines_[key] = pipeline;
        return pipeline;
    }

    // Fixed 128-byte layout mirroring D3D11EffectRenderer's own convention byte-for-byte: [0..15]=
    // vpSize (vec4, xy used), [16..79]=mat4 matrix, [80..95]=vec4 color, [96..99]=float/int slot 0.
    // `name` is deliberately ignored, matching every sibling EffectRenderer's own convention.
    void SdlGpuEffectRenderer::SetUniformMat4(const char* /*name*/, const float* matrix)
    {
        std::memcpy(pushConst_.data() + 4, matrix, 64);
    }

    void SdlGpuEffectRenderer::SetUniformVec4(const char* /*name*/, float x, float y, float z, float w)
    {
        pushConst_[20] = x; pushConst_[21] = y; pushConst_[22] = z; pushConst_[23] = w;
    }

    void SdlGpuEffectRenderer::SetUniformVec3(const char* /*name*/, float x, float y, float z)
    {
        pushConst_[20] = x; pushConst_[21] = y; pushConst_[22] = z;
    }

    void SdlGpuEffectRenderer::SetUniformVec2(const char* /*name*/, float x, float y)
    {
        pushConst_[20] = x; pushConst_[21] = y;
    }

    void SdlGpuEffectRenderer::SetUniformFloat(const char* /*name*/, float value)
    {
        pushConst_[24] = value;
    }

    void SdlGpuEffectRenderer::SetUniformInt(const char* /*name*/, int value)
    {
        pushConst_[24] = static_cast<float>(value);
    }

    void SdlGpuEffectRenderer::SetViewportSizeEXT(float width, float height)
    {
        pushConst_[0] = width;
        pushConst_[1] = height;
    }

    std::unique_ptr<IEffectRenderer> SdlGpuRenderer::CreateEffectRenderer(
        const std::string& vertSrc, const std::string& fragSrc)
    {
        auto renderer = std::make_unique<SdlGpuEffectRenderer>(*this);
        if (!vertSrc.empty() && !fragSrc.empty())
            renderer->CompileProgram(vertSrc, fragSrc);
        return renderer;
    }

    // ---- SdlGpuSpriteBatchRenderer ----

    SdlGpuSpriteBatchRenderer::SdlGpuSpriteBatchRenderer(SdlGpuRenderer& owner)
        : owner_(&owner)
    {
    }

    void SdlGpuSpriteBatchRenderer::Begin()
    {
        if (begun_)
            throw std::logic_error("CNA SDL_GPU SpriteBatch.Begin called twice without End");
        begun_ = true;
    }

    void SdlGpuSpriteBatchRenderer::End()
    {
        if (!begun_)
            throw std::logic_error("CNA SDL_GPU SpriteBatch.End called without Begin");
        begun_ = false;
    }

    void SdlGpuSpriteBatchRenderer::SetCustomEffect(Effect* effect)
    {
        customEffect_ = effect;
    }

    void SdlGpuSpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        const Rectangle source{0, 0, texture.GetWidth(), texture.GetHeight()};
        const Rectangle destination{static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight()};
        Draw(texture, destination, source, Color::White);
    }

    void SdlGpuSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                        const Rectangle& destinationRectangle,
                                        const Rectangle& sourceRectangle,
                                        const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
    }

    void SdlGpuSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
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
        // A drawn texture is either a plain Texture2D (SdlGpuTextureRenderer) or a RenderTarget2D
        // sampled after being rendered into (SdlGpuRenderTargetRenderer, Phase SDLGPU-8) -- unrelated
        // concrete classes, resolved through the one shared resolver the 3D effect paths also use,
        // so sprites and effects cannot drift apart on either type or lifetime (REMED-GFX-152).
        const SdlGpuSampledTextureEXT nativeTexture =
            ResolveSampledTextureEXT(&texture, "SpriteBatch.Draw");
        // SDLGPU-42/43: resolve the custom effect NOW (Draw()-call time), not once per Begin/End --
        // a game may reasonably change uniforms between individual Draw() calls within one
        // Begin/End cycle using the same custom effect object (see SpriteCommand's own doc comment).
        SdlGpuEffectRenderer* customEffectRenderer = customEffect_
            ? dynamic_cast<SdlGpuEffectRenderer*>(customEffect_->GetEffectRendererPtr())
            : nullptr;
        // plan_fx.md FX-071: customEffect_ is either ShaderEffect-derived (resolved above) or a
        // compiled effect (resolved here), never both -- Effect::GetCompiledRuntimePtr() returns
        // null for a ShaderEffect and GetEffectRendererPtr() returns null for a compiled effect.
        ICompiledEffectRuntime* compiledEffectRuntime =
            customEffect_ ? customEffect_->GetCompiledRuntimePtr() : nullptr;
        owner_->QueueSprite(texture, nativeTexture, destinationRectangle, sourceRectangle, color, rotation,
                            origin, effects, layerDepth, transform_, textureFilter_, addressU_, addressV_,
                            customEffectRenderer, compiledEffectRuntime);
    }

}

namespace CNA::Internal::Renderers
{
    // plan_runtimerenderer.md design decision 4: declared in this family's own
    // namespace so several renderer archives can link into one binary, then defined
    // below with a qualified name -- the body keeps its place unchanged.
    namespace SdlGpu { std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args); }

    std::unique_ptr<IGraphicsRenderer> SdlGpu::CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<SdlGpu::SdlGpuRenderer>(
            CNA::Platform::Detail::ResolveSdl3RendererWindow(args.surface.windowId),
            args.virtualWidth, args.virtualHeight, args.presentationMode, args.swapInterval);
    }
}
