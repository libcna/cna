// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"
#include "CNA/Platform/Detail/Sdl3RendererInterop.hpp"

#include "CNA/Logger.hpp"
#include "CNA/LogCategory.hpp"
#include "CNA/Internal/Graphics/DxtUtil.hpp"
#include "shaders/spirv_shaders.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
#include "CNA/Internal/Renderers/SdlGpu/SdlGpuCompiledEffect.hpp"
#include "CNA/Internal/Renderers/SdlGpu/SdlGpuCompiledEffectVertexLayout.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
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
    // plans/plan_fx.md FX-091: the sampler cache's key. REMED-GFX-170 established that the key must be
    // the COMPLETE sampler description; FX-083 then added the LOD clamp and bias to it by hand-
    // packing them into a uint64, which silently discarded the top eight bits of the 32-bit bias
    // -- a float's sign and most of its exponent -- and so aliased whole families of distinct
    // biases onto one cached sampler. A struct with defaulted member-wise equality cannot lose a
    // field that way, and a new field is a compile-time addition here rather than a bit-layout
    // puzzle.
    SamplerCacheKeyEXT SamplerCacheKeyEXT::Make(int filter, int u, int v, int w,
                                                int anisotropy, int mipLevel, float lodBias)
    {
        SamplerCacheKeyEXT key;
        key.textureFilter = filter;
        key.addressU = u;
        key.addressV = v;
        key.addressW = w;
        key.maxAnisotropy = anisotropy;
        key.maxMipLevel = mipLevel;
        static_assert(sizeof(key.mipLodBiasBits) == sizeof(lodBias));
        std::memcpy(&key.mipLodBiasBits, &lodBias, sizeof(key.mipLodBiasBits));
        return key;
    }

    std::size_t SamplerCacheKeyHashEXT::operator()(const SamplerCacheKeyEXT& key) const noexcept
    {
        // FNV-1a over the six integer fields and the bias bit pattern. Every member participates;
        // an equal-hash collision only costs a bucket walk, whereas a member left out of the key
        // itself would hand back the wrong sampler, which is what FX-091 fixes.
        std::size_t hash = 1469598103934665603ull;
        const auto mix = [&hash](std::uint32_t value) {
            for (int byte = 0; byte < 4; ++byte)
            {
                hash ^= static_cast<std::size_t>((value >> (byte * 8)) & 0xFFu);
                hash *= 1099511628211ull;
            }
        };
        mix(static_cast<std::uint32_t>(key.textureFilter));
        mix(static_cast<std::uint32_t>(key.addressU));
        mix(static_cast<std::uint32_t>(key.addressV));
        mix(static_cast<std::uint32_t>(key.addressW));
        mix(static_cast<std::uint32_t>(key.maxAnisotropy));
        mix(static_cast<std::uint32_t>(key.maxMipLevel));
        mix(key.mipLodBiasBits);
        return hash;
    }

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
            InstancedVertex,
            SkinnedVertex,
            SkinnedColoredVertex,
            SkinnedColoredFragment,
            PbrVertex,
            PbrSkinnedVertex,
            // plans/plan_gltf.md GLTF-462/GLTF-463: stride-60 and stride-80 twins that declare COLOR_0.
            PbrColorVertex,
            PbrSkinnedColorVertex,
            PbrFragment,
            Count
        };

        static_assert(static_cast<std::size_t>(ConstructionShader::Count) ==
                      SdlGpuConstructionShaderCountEXT);

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
                case SdlGpuFailurePointEXT::InstancedVertexShaderCreation: return "instanced vertex shader creation";
                case SdlGpuFailurePointEXT::SkinnedVertexShaderCreation: return "skinned vertex shader creation";
                case SdlGpuFailurePointEXT::SkinnedColoredVertexShaderCreation: return "skinned-colored vertex shader creation";
                case SdlGpuFailurePointEXT::SkinnedColoredFragmentShaderCreation: return "skinned-colored fragment shader creation";
                case SdlGpuFailurePointEXT::PbrVertexShaderCreation: return "PBR vertex shader creation";
                case SdlGpuFailurePointEXT::PbrSkinnedVertexShaderCreation: return "skinned PBR vertex shader creation";
                case SdlGpuFailurePointEXT::PbrColorVertexShaderCreation: return "PBR vertex-color shader creation";
                case SdlGpuFailurePointEXT::PbrSkinnedColorVertexShaderCreation: return "skinned PBR vertex-color shader creation";
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
            createInfo.min_lod = 0.0f;
            createInfo.mip_lod_bias = 0.0f;
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

        void UploadTargetRegion(SDL_GPUDevice* device, SDL_GPUTexture* texture,
                                SDL_GPUTextureFormat format, Uint32 bytesPerPixel,
                                int layer, int level, int x, int y, int w, int h,
                                const std::uint8_t* pixels, int stride,
                                const char* diagnostic)
        {
            if (pixels == nullptr)
                throw std::invalid_argument(std::string(diagnostic) + ": upload source is null");
            const int rowBytes = w * static_cast<int>(bytesPerPixel);
            if (stride < rowBytes)
                throw std::invalid_argument(std::string(diagnostic) + ": upload stride is too small");

            std::vector<std::uint8_t> tight;
            const std::uint8_t* upload = pixels;
            if (stride != rowBytes)
            {
                tight.resize(static_cast<std::size_t>(rowBytes) * h);
                for (int row = 0; row < h; ++row)
                    std::memcpy(tight.data() + static_cast<std::size_t>(row) * rowBytes,
                                pixels + static_cast<std::size_t>(row) * stride,
                                static_cast<std::size_t>(rowBytes));
                upload = tight.data();
            }

            const Uint32 sizeBytes = SDL_CalculateGPUTextureFormatSize(
                format, static_cast<Uint32>(w), static_cast<Uint32>(h), 1);
            if (sizeBytes != static_cast<Uint32>(rowBytes * h))
                throw std::runtime_error(std::string(diagnostic) +
                                         ": native upload size disagrees with texel size");

            SDL_GPUTransferBufferCreateInfo transferInfo{};
            transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            transferInfo.size = sizeBytes;
            SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
            if (transfer == nullptr)
                throw std::runtime_error(std::string(diagnostic) +
                                         ": failed to create transfer buffer: " + SDL_GetError());

            void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
            if (mapped == nullptr)
            {
                SDL_ReleaseGPUTransferBuffer(device, transfer);
                throw std::runtime_error(std::string(diagnostic) +
                                         ": failed to map transfer buffer: " + SDL_GetError());
            }
            std::memcpy(mapped, upload, sizeBytes);
            SDL_UnmapGPUTransferBuffer(device, transfer);

            SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(device);
            if (command == nullptr)
            {
                SDL_ReleaseGPUTransferBuffer(device, transfer);
                throw std::runtime_error(std::string(diagnostic) +
                                         ": failed to acquire command buffer: " + SDL_GetError());
            }
            SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(command);
            if (pass == nullptr)
            {
                (void)SDL_CancelGPUCommandBuffer(command);
                SDL_ReleaseGPUTransferBuffer(device, transfer);
                throw std::runtime_error(std::string(diagnostic) +
                                         ": failed to begin copy pass: " + SDL_GetError());
            }

            SDL_GPUTextureTransferInfo source{};
            source.transfer_buffer = transfer;
            SDL_GPUTextureRegion destination{};
            destination.texture = texture;
            destination.mip_level = static_cast<Uint32>(level);
            destination.layer = static_cast<Uint32>(layer);
            destination.x = static_cast<Uint32>(x);
            destination.y = static_cast<Uint32>(y);
            destination.w = static_cast<Uint32>(w);
            destination.h = static_cast<Uint32>(h);
            destination.d = 1;
            // Never cycle a render-target texture: every queued draw and public target wrapper
            // retains this exact handle, and cycling would replace untouched faces/mip levels.
            SDL_UploadToGPUTexture(pass, &source, &destination, false);
            SDL_EndGPUCopyPass(pass);
            if (!SDL_SubmitGPUCommandBuffer(command))
            {
                SDL_ReleaseGPUTransferBuffer(device, transfer);
                throw std::runtime_error(std::string(diagnostic) +
                                         ": failed to submit upload: " + SDL_GetError());
            }
            SDL_ReleaseGPUTransferBuffer(device, transfer);
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

        [[nodiscard]] SDL_GPUVertexElementFormat ToSdlGpuStockVertexFormat(
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
                "CNA SDL_GPU: unrecognized stock VertexElementFormat ordinal " +
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

        void BuildSdlGpuStockVertexStateEXT(
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

        [[nodiscard]] std::size_t StockPipelineKey(
            std::size_t pipelineKey,
            const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& layout)
        {
            return HashCombine(
                pipelineKey,
                static_cast<std::size_t>(
                    CNA::Internal::Graphics::HashResolvedStockVertexLayoutEXT(layout)));
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

        // SDL_gpu exposes the native D3D/Vulkan/Metal constant-factor convention, whereas XNA's
        // RasterizerState.DepthBias is a normalized depth offset. This is the same per-format
        // conversion used by FNA3D's SDL_gpu driver. D32 float has 23 mantissa bits; fixed-point
        // formats use all of their depth bits. A depthless pass follows FNA3D's D16 fallback --
        // the value is inert there, but keeping it deterministic preserves cache identity.
        [[nodiscard]] float XnaToSdlDepthBiasScale(SDL_GPUTextureFormat depthStencilFormat)
        {
            switch (depthStencilFormat)
            {
                case SDL_GPU_TEXTUREFORMAT_D32_FLOAT:
                case SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT:
                    return static_cast<float>((1u << 23) - 1u);
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
        [[nodiscard]] PipelineDepthBias NormalizeDepthBias(
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

        [[nodiscard]] std::size_t HashDepthBias(
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

        void FillDepthBiasState(
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
        // they're set to, so different "irrelevant" values don't create duplicate pipelines --
        // mirrors VulkanRenderer::PackBlendBits's identical "collapse to 0 when disabled" rule.
        // sampleCount (SDLGPU-38's MSAA fix) is a REQUIRED dimension, not an optional one to
        // collapse -- a pipeline created with the wrong sample_count for its render pass's actual
        // attachments is exactly finding #1 of the adversarial review this fixes.
        [[nodiscard]] std::size_t PipelineCacheKey(SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
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
            // SDL_GPU/Vulkan render-pass compatibility is slot-aligned. SDLGPU-72 made several
            // native RT formats reachable, so slot 0 alone is insufficient: {Color,Color} and
            // {Color,HalfVector4} need different immutable pipelines even though their primary
            // attachment and every public render state are identical.
            key = HashCombine(key, static_cast<std::size_t>(colorTargetCount));
            for (int i = 0; i < colorTargetCount; ++i)
                key = HashCombine(key, static_cast<std::size_t>(colorFormats[i]));
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

        // Mirrors VulkanRenderer's CalculateVulkanRTMipLevels / Texture2D.cpp's
        // CalculateMipLevels -- each renderer keeps its own copy of this small helper.
        [[nodiscard]] int CalculateMipLevels(int w, int h)
        {
            int levels = 1;
            while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++levels; }
            return levels;
        }

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
                case SurfaceFormat::Bgr565:
                case SurfaceFormat::Bgra5551:
                case SurfaceFormat::Bgra4444:
                case SurfaceFormat::NormalizedByte2:
                    return 2;
                case SurfaceFormat::Dxt1:
                    return 8;
                case SurfaceFormat::Dxt3:
                case SurfaceFormat::Dxt5:
                    return 16;
                case SurfaceFormat::Color:
                case SurfaceFormat::NormalizedByte4:
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
                default:                             return SDL_GPU_TEXTUREFORMAT_INVALID;
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

        struct DepthTargetFormatInfo
        {
            SDL_GPUTextureFormat nativeFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
            int appliedDepthFormat = 0;
            int depthBits = 0;
            bool hasStencil = false;
        };

        [[nodiscard]] bool TryGetDepthTargetFormatInfo(
            SDL_GPUDevice* device,
            Microsoft::Xna::Framework::Graphics::DepthFormat format,
            DepthTargetFormatInfo& out) noexcept
        {
            using Microsoft::Xna::Framework::Graphics::DepthFormat;
            const auto supported = [device](SDL_GPUTextureFormat candidate) {
                return SDL_GPUTextureSupportsFormat(
                    device, candidate, SDL_GPU_TEXTURETYPE_2D,
                    SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
            };
            switch (format)
            {
                case DepthFormat::None:
                    out = {SDL_GPU_TEXTUREFORMAT_INVALID,
                           static_cast<int>(DepthFormat::None), 0, false};
                    return true;
                case DepthFormat::Depth16:
                    if (supported(SDL_GPU_TEXTUREFORMAT_D16_UNORM))
                    {
                        out = {SDL_GPU_TEXTUREFORMAT_D16_UNORM,
                               static_cast<int>(DepthFormat::Depth16), 16, false};
                        return true;
                    }
                    if (supported(SDL_GPU_TEXTUREFORMAT_D32_FLOAT))
                    {
                        out = {SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
                               static_cast<int>(DepthFormat::Depth16), 23, false};
                        return true;
                    }
                    return false;
                case DepthFormat::Depth24:
                    if (supported(SDL_GPU_TEXTUREFORMAT_D24_UNORM))
                    {
                        out = {SDL_GPU_TEXTUREFORMAT_D24_UNORM,
                               static_cast<int>(DepthFormat::Depth24), 24, false};
                        return true;
                    }
                    if (supported(SDL_GPU_TEXTUREFORMAT_D32_FLOAT))
                    {
                        out = {SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
                               static_cast<int>(DepthFormat::Depth24), 23, false};
                        return true;
                    }
                    return false;
                case DepthFormat::Depth24Stencil8:
                    if (supported(SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT))
                    {
                        out = {SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
                               static_cast<int>(DepthFormat::Depth24Stencil8), 24, true};
                        return true;
                    }
                    if (supported(SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT))
                    {
                        out = {SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
                               static_cast<int>(DepthFormat::Depth24Stencil8), 23, true};
                        return true;
                    }
                    return false;
            }
            return false;
        }

        [[nodiscard]] std::array<float, 8> SpriteChannelExpansion(int surfaceFormat)
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

        [[nodiscard]] bool ForceDxtFallbackForTest()
        {
            const char* value = std::getenv("CNA_SDLGPU_FORCE_DXT_FALLBACK");
            return value != nullptr && std::strcmp(value, "0") != 0;
        }

        [[nodiscard]] bool ForcePackedFallbackForTest()
        {
            const char* value = std::getenv("CNA_SDLGPU_FORCE_PACKED_FALLBACK");
            return value != nullptr && std::strcmp(value, "0") != 0;
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

        [[nodiscard]] std::vector<std::uint8_t> ConvertTextureLevelForFallback(
            SurfaceFormat format, const std::uint8_t* source, std::size_t sourceBytes,
            int width, int height)
        {
            if (source == nullptr)
                throw std::invalid_argument("CNA SDL_GPU: texture conversion source cannot be null");

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
                        "CNA SDL_GPU: no renderer-side conversion for SurfaceFormat ordinal " +
                        std::to_string(static_cast<int>(format)));
                }
                result[i * 4u + 0u] = r;
                result[i * 4u + 1u] = g;
                result[i * 4u + 2u] = b;
                result[i * 4u + 3u] = a;
            }
            return result;
        }

        // SDLGPU-36: clamps an XNA multiSampleCount request down to the largest SDL_gpu sample
        // count this device/format actually supports, mirroring D3D12RenderTargetCubeRenderer's own
        // ClampMultiSampleCount() convention (XNA's RenderTargetCube.MultiSampleCount is documented
        // to reflect the real clamped value, not the raw constructor request).
        [[nodiscard]] SDL_GPUSampleCount ClampSampleCount(
            SDL_GPUDevice* device, SDL_GPUTextureFormat colorFormat,
            SDL_GPUTextureFormat depthFormat, int requested)
        {
            if (requested <= 1)
                return SDL_GPU_SAMPLECOUNT_1;
            SDL_GPUSampleCount candidate = requested >= 8 ? SDL_GPU_SAMPLECOUNT_8
                                          : requested >= 4 ? SDL_GPU_SAMPLECOUNT_4
                                                            : SDL_GPU_SAMPLECOUNT_2;
            while (candidate != SDL_GPU_SAMPLECOUNT_1 &&
                   (!SDL_GPUTextureSupportsSampleCount(device, colorFormat, candidate) ||
                    (depthFormat != SDL_GPU_TEXTUREFORMAT_INVALID &&
                     !SDL_GPUTextureSupportsSampleCount(device, depthFormat, candidate))))
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
        void FillColoredUniforms(std::array<float, 32>& out, const Matrix& wvp)
        {
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
            // The fourth lane was padding. It now carries the public lighting-family selector to
            // both stages without changing this established 56-float block: zero is XNA's default
            // per-vertex/Gouraud path, one requests the per-pixel path.
            out[16] = p.emissiveColor[0]; out[17] = p.emissiveColor[1]; out[18] = p.emissiveColor[2];
            out[19] = p.preferPerPixelLighting ? 1.0f : 0.0f;
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
        int appliedSwapInterval = 1;
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
            owner.appliedSwapInterval_ = appliedSwapInterval;
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
            owner.instancedVertexShader_ =
                shaders[static_cast<std::size_t>(ConstructionShader::InstancedVertex)];
            owner.skinnedVertexShader_ = shaders[static_cast<std::size_t>(ConstructionShader::SkinnedVertex)];
            owner.skinnedColoredVertexShader_ =
                shaders[static_cast<std::size_t>(ConstructionShader::SkinnedColoredVertex)];
            owner.skinnedColoredFragmentShader_ =
                shaders[static_cast<std::size_t>(ConstructionShader::SkinnedColoredFragment)];
            owner.pbrVertexShader_ = shaders[static_cast<std::size_t>(ConstructionShader::PbrVertex)];
            owner.pbrColorVertexShader_ =
                shaders[static_cast<std::size_t>(ConstructionShader::PbrColorVertex)];
            owner.pbrSkinnedColorVertexShader_ =
                shaders[static_cast<std::size_t>(ConstructionShader::PbrSkinnedColorVertex)];
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

        // plans/plan_sdlgpu.md SDLGPU-6: request SPIR-V first -- the only shader format this device's
        // vendored SDL3 compiles a driver for on Linux (Vulkan). DXBC/DXIL/MSL support (Windows/
        // macOS drivers) is deferred to plans/plan_sdlgpu.md's Phase SDLGPU-13.
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
        resources.appliedSwapInterval =
            ConfigureSwapchain(resources.device, window_, resources.swapInterval);
        if (resources.appliedSwapInterval < 0)
            resources.appliedSwapInterval = 1;

        resources.FailAt(SdlGpuFailurePointEXT::DepthStencilFormatQuery);
        resources.depthStencilFormat = QueryDepthStencilFormat(resources.device);

        CreateSpriteResources(resources);
        CreateColoredResources(resources);
        CreateTexturedResources(resources);
        CreateLitTexturedResources(resources);
        CreateAlphaTestResources(resources);
        CreateDualTextureResources(resources);
        CreateEnvMapResources(resources);
        CreateInstancedResources(resources);
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
        // plans/plan_fx.md FX-061: released before the device it was created against.
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
        DestroyInstancedResources();
        DestroyEnvMapResources();
        DestroyDualTextureResources();
        DestroyAlphaTestResources();
        DestroyLitTexturedResources();
        DestroyTexturedResources();
        DestroyColoredResources();
        DestroySpriteResources();
#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
        // plans/plan_fx.md FX-071: unlike every stock family's own Destroy*Resources, there are no fixed
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

    bool SdlGpuRenderer::SupportsCapability(const CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
            case CNA::GraphicsCapability::ThreeD:
            case CNA::GraphicsCapability::AnisotropicFiltering:
            case CNA::GraphicsCapability::CustomEffects:
            case CNA::GraphicsCapability::Texture3D:
            case CNA::GraphicsCapability::AdditiveBlending:
            case CNA::GraphicsCapability::MultiStreamVertexInput:
            case CNA::GraphicsCapability::Instancing:
            case CNA::GraphicsCapability::MultipleRenderTargets:
                return true;
            case CNA::GraphicsCapability::DepthStencilBuffer:
            case CNA::GraphicsCapability::StencilBuffer:
                return depthStencilFormat_ != SDL_GPU_TEXTUREFORMAT_INVALID;
            case CNA::GraphicsCapability::MultiSampleAntiAliasing:
                return device_ != nullptr &&
                       SDL_GPUTextureSupportsSampleCount(
                           device_, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                           SDL_GPU_SAMPLECOUNT_2) &&
                       (depthStencilFormat_ == SDL_GPU_TEXTUREFORMAT_INVALID ||
                        SDL_GPUTextureSupportsSampleCount(
                            device_, depthStencilFormat_, SDL_GPU_SAMPLECOUNT_2));
            case CNA::GraphicsCapability::CompiledEffects:
                return SupportsCompiledEffects();
            case CNA::GraphicsCapability::WireFrame:
                return true;
            case CNA::GraphicsCapability::OcclusionQuery:
            case CNA::GraphicsCapability::FloatRenderTargets:
            case CNA::GraphicsCapability::HalfFloatRenderTargets:
            case CNA::GraphicsCapability::HalfFloatTextureLinearFiltering:
            case CNA::GraphicsCapability::ComputeShaders:
            case CNA::GraphicsCapability::IndirectDraw:
                return false;
        }
        return false;
    }

    std::string_view SdlGpuRenderer::GetAdditionalLimitationsTextEXT() const
    {
        return "SDL GPU supports eight stock-effect vertex streams and four independently "
               "writable, mixed-format color targets. CNAEXT ShaderEffect instancing is not "
               "implemented. OcclusionQuery is unavailable: vendored SDL_gpu 3.5.0 exposes no "
               "occlusion-query or query-pool commands; GPU fences report only command-buffer "
               "completion and cannot count samples that pass depth/stencil.";
    }

    SDL_GPUTextureFormat SdlGpuRenderer::QueryDepthStencilFormat(SDL_GPUDevice* device)
    {
        // plans/plan_sdlgpu.md: SDL_gpu guarantees at most one of D24_UNORM_S8_UINT/D32_FLOAT_S8_UINT
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

            if (forceNextNullSwapchainTextureForTest_)
            {
                forceNextNullSwapchainTextureForTest_ = false;
                swapchainTexture = nullptr;
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
                out.store_op = (state.preserveContents || colorLoadedLater)
                    ? SDL_GPU_STOREOP_RESOLVE_AND_STORE : SDL_GPU_STOREOP_RESOLVE;
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
        activeColorTargetFormats_.fill(SDL_GPU_TEXTUREFORMAT_INVALID);
        activeColorTargetFormats_[0] = swapchainFormat;
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
        activeColorTargetFormats_.fill(SDL_GPU_TEXTUREFORMAT_INVALID);
        activeColorTargetFormats_[0] = target->colorFormat;
        for (std::size_t i = 0; i < segment.extraAttachments.size(); ++i)
            activeColorTargetFormats_[i + 1] = segment.extraAttachments[i]->colorFormat;

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
            depthStencilTarget.stencil_load_op = target->hasStencil
                ? ((segment.clearStencilRequested || target->clearStencilPending)
                       ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD)
                : SDL_GPU_LOADOP_DONT_CARE;
            depthStencilTarget.stencil_store_op = target->hasStencil
                ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
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
        RenderQueuedDraws(pass, cmd, dt, target->colorFormat, target->sampleCount,
                          hasDepth ? target->depthFormat : SDL_GPU_TEXTUREFORMAT_INVALID,
                          colorTargetCount, segment.id);
        passOwner.End();

        // Whatever this pass just stored is this resource's content from here on: a LATER segment
        // of the same resource must LOAD it, not clear it again.
        target->clearColorPending = false;
        target->clearDepthPending = false;
        target->clearStencilPending = false;
        if (!target->definedMipLevels.empty())
            target->definedMipLevels[0] = true;
        for (const auto& extra : segment.extraAttachments)
        {
            extra->clearColorPending = false;
            extra->clearDepthPending = false;
            extra->clearStencilPending = false;
            if (!extra->definedMipLevels.empty())
                extra->definedMipLevels[0] = true;
        }

        // REMED-GFX-187: regenerate through clamped per-level GPU blits outside the pass. This
        // matches FNA3D's OPENGL_ResolveTarget semantics (the chain is regenerated once this
        // target's contents are final for the segment) and keeps the existing resolve-before-mips
        // ordering. Per segment, since each segment's result is what a LATER segment or the
        // swapchain pass may sample.
        if (target->mipMap && target->levelCount > 1)
        {
            GenerateRenderTargetMipChain(cmd, target->colorTexture, target->width,
                                         target->height, target->levelCount);
            std::fill(target->definedMipLevels.begin(), target->definedMipLevels.end(), true);
        }
        for (const auto& extra : segment.extraAttachments)
        {
            if (extra->mipMap && extra->levelCount > 1)
            {
                GenerateRenderTargetMipChain(cmd, extra->colorTexture, extra->width,
                                             extra->height, extra->levelCount);
                std::fill(extra->definedMipLevels.begin(), extra->definedMipLevels.end(), true);
            }
        }
    }

    void SdlGpuRenderer::RenderToTargetCubeFace(SDL_GPUCommandBuffer* cmd,
                                                       const PassSegment& segment,
                                                       bool colorLoadedLater)
    {
        const std::shared_ptr<SdlGpuRenderTargetCubeState>& cube = segment.cube;
        const int face = segment.face;
        activeColorTargetFormats_.fill(SDL_GPU_TEXTUREFORMAT_INVALID);
        activeColorTargetFormats_[0] = cube->colorFormat;

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
            depthStencilTarget.stencil_load_op = cube->hasStencil
                ? ((segment.clearStencilRequested || cube->clearStencilPending[face])
                       ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD)
                : SDL_GPU_LOADOP_DONT_CARE;
            depthStencilTarget.stencil_store_op = cube->hasStencil
                ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
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
        RenderQueuedDraws(pass, cmd, dt, cube->colorFormat, cube->sampleCount,
                          hasDepth ? cube->depthFormat : SDL_GPU_TEXTUREFORMAT_INVALID, 1,
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

    void SdlGpuRenderer::OnSurfaceChanged(const RendererSurfaceInfo& surface)
    {
        if (surface.windowId != SDL_GetWindowID(window_))
        {
            throw std::invalid_argument(
                "CNA SDL_GPU: a renderer's platform window identity cannot change");
        }
        physicalWidth_ = std::max(0, surface.drawableSize.width);
        physicalHeight_ = std::max(0, surface.drawableSize.height);
        displayScale_ = std::isfinite(surface.displayScale) && surface.displayScale > 0.0f
            ? surface.displayScale
            : 1.0f;
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

    void SdlGpuRenderer::GetDefaultViewportRect(int& x, int& y, int& width, int& height)
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        x = static_cast<int>(std::lround(viewport.x));
        y = static_cast<int>(std::lround(viewport.y));
        width = static_cast<int>(std::lround(viewport.width));
        height = static_cast<int>(std::lround(viewport.height));
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

    int SdlGpuRenderer::ConfigureSwapchain(SDL_GPUDevice* device, SDL_Window* window,
                                           int interval)
    {
        // plans/plan_sdlgpu.md: SDL_gpu has no "half-rate" present mode -- XNA's PresentInterval.Two
        // (swapInterval==2) falls back to plain VSYNC, same as swapInterval==1.
        SDL_GPUPresentMode presentMode = SDL_GPU_PRESENTMODE_VSYNC;
        int appliedInterval = 1;
        if (interval == 0)
        {
            if (SDL_WindowSupportsGPUPresentMode(device, window, SDL_GPU_PRESENTMODE_IMMEDIATE))
            {
                presentMode = SDL_GPU_PRESENTMODE_IMMEDIATE;
                appliedInterval = 0;
            }
            else if (SDL_WindowSupportsGPUPresentMode(device, window, SDL_GPU_PRESENTMODE_MAILBOX))
            {
                presentMode = SDL_GPU_PRESENTMODE_MAILBOX;
                // Mailbox is synchronized to vblank even though it does not queue old frames.
                appliedInterval = 1;
            }
        }

        if (!SDL_SetGPUSwapchainParameters(
                device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, presentMode))
        {
            // Genuine per-device/driver capability gap, not a "not implemented yet" stub.
            CNA::Logger::Warn(
                std::string("CNA SDL_GPU: SDL_SetGPUSwapchainParameters failed: ") + SDL_GetError(),
                CNA::LogCategory::GPU);
            return -1;
        }
        return appliedInterval;
    }

    void SdlGpuRenderer::SetSwapInterval(int interval)
    {
        interval = std::max(0, interval);
        const int appliedInterval = ConfigureSwapchain(device_, window_, interval);
        swapInterval_ = interval;
        if (appliedInterval >= 0)
            appliedSwapInterval_ = appliedInterval;
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
        const float inverseDisplayScale = 1.0f / displayScale_;
        const float clientX = viewport.x * inverseDisplayScale;
        const float clientY = viewport.y * inverseDisplayScale;
        const float clientWidth = viewport.width * inverseDisplayScale;
        const float clientHeight = viewport.height * inverseDisplayScale;
        logicalX = (windowX - clientX) * viewport.logicalWidth / clientWidth;
        logicalY = (windowY - clientY) * viewport.logicalHeight / clientHeight;
        return windowX >= clientX && windowX < clientX + clientWidth &&
               windowY >= clientY && windowY < clientY + clientHeight;
    }

    bool SdlGpuRenderer::TransformLogicalToWindow(float logicalX, float logicalY, float& windowX, float& windowY) const
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.logicalWidth == 0.0f || viewport.logicalHeight == 0.0f)
            return false;
        const float inverseDisplayScale = 1.0f / displayScale_;
        windowX = (viewport.x + logicalX * viewport.width / viewport.logicalWidth) * inverseDisplayScale;
        windowY = (viewport.y + logicalY * viewport.height / viewport.logicalHeight) * inverseDisplayScale;
        return true;
    }

    RendererFormatVerdict SdlGpuRenderer::ClassifySurfaceFormatEXT(int surfaceFormat) const
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
            // SDLGPU-69: this is exactly EasyGL's ordinary Texture2D set. Color and both SNORM
            // formats have universally supported SDL sampler storage. Packed formats use their
            // exact SDL representation when present and a lossless logical RGBA expansion when
            // absent. DXT uses BC1/2/3 when present and the shared CPU decoder otherwise.
            case SurfaceFormat::Color:
            case SurfaceFormat::Bgr565:
            case SurfaceFormat::Bgra5551:
            case SurfaceFormat::Bgra4444:
            case SurfaceFormat::Dxt1:
            case SurfaceFormat::Dxt3:
            case SurfaceFormat::Dxt5:
            case SurfaceFormat::NormalizedByte2:
            case SurfaceFormat::NormalizedByte4:
                return RendererFormatVerdict::Supported;

            // Truthful current boundary for the remaining classic XNA 4.0 Texture2D formats.
            // SDL_gpu has candidate storage for many of these, but SDL GPU has not yet supplied
            // their channel-expansion/filtering/sample verification. Advertising them before that
            // would repeat the old RGBA8 substitution bug rather than improve parity.
            case SurfaceFormat::Rgba1010102:
            case SurfaceFormat::Rg32:
            case SurfaceFormat::Rgba64:
            case SurfaceFormat::Alpha8:
            case SurfaceFormat::Single:
            case SurfaceFormat::Vector2:
            case SurfaceFormat::Vector4:
            case SurfaceFormat::HalfSingle:
            case SurfaceFormat::HalfVector2:
            case SurfaceFormat::HalfVector4:
            case SurfaceFormat::HdrBlendable:
                return RendererFormatVerdict::Unsupported;

            // Newer CNAEXT formats are owned by the modern plan. Preserve the common gate.
            default:
                return RendererFormatVerdict::Defer;
        }
    }

    RendererFormatVerdict SdlGpuRenderer::ClassifyColorTransferFormatEXT(int surfaceFormat) const
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
            case SurfaceFormat::Color:
                return RendererFormatVerdict::Supported;
            case SurfaceFormat::Bgr565:
            case SurfaceFormat::Bgra5551:
            case SurfaceFormat::Bgra4444:
            case SurfaceFormat::Dxt1:
            case SurfaceFormat::Dxt3:
            case SurfaceFormat::Dxt5:
            case SurfaceFormat::NormalizedByte2:
            case SurfaceFormat::NormalizedByte4:
            case SurfaceFormat::Rgba1010102:
            case SurfaceFormat::Rg32:
            case SurfaceFormat::Rgba64:
            case SurfaceFormat::Alpha8:
            case SurfaceFormat::Single:
            case SurfaceFormat::Vector2:
            case SurfaceFormat::Vector4:
            case SurfaceFormat::HalfSingle:
            case SurfaceFormat::HalfVector2:
            case SurfaceFormat::HalfVector4:
            case SurfaceFormat::HdrBlendable:
                return RendererFormatVerdict::Unsupported;
            default:
                return RendererFormatVerdict::Defer;
        }
    }

    RendererFormatVerdict SdlGpuRenderer::ClassifyTextureCubeFormatEXT(int surfaceFormat) const
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
            // A plain TextureCube's public Color overload is RGBA8-shaped. DXT1/3/5 have the
            // separate exact-block overload and are either stored as native BC or decoded into a
            // renderer-owned RGBA8 cube when this SDL_gpu driver cannot sample the BC format.
            case SurfaceFormat::Color:
            case SurfaceFormat::Dxt1:
            case SurfaceFormat::Dxt3:
            case SurfaceFormat::Dxt5:
                return RendererFormatVerdict::Supported;

            // Unlike Texture2D, TextureCube exposes no typed packed/float transfer overload. In
            // particular EasyGL's inherited 2D classifier accepts the three packed formats even
            // though its cube implementation always allocates RGBA8 and can only consume Color;
            // copying that false capability would reproduce an EasyGL defect, not XNA behavior.
            case SurfaceFormat::Bgr565:
            case SurfaceFormat::Bgra5551:
            case SurfaceFormat::Bgra4444:
            case SurfaceFormat::NormalizedByte2:
            case SurfaceFormat::NormalizedByte4:
            case SurfaceFormat::Rgba1010102:
            case SurfaceFormat::Rg32:
            case SurfaceFormat::Rgba64:
            case SurfaceFormat::Alpha8:
            case SurfaceFormat::Single:
            case SurfaceFormat::Vector2:
            case SurfaceFormat::Vector4:
            case SurfaceFormat::HalfSingle:
            case SurfaceFormat::HalfVector2:
            case SurfaceFormat::HalfVector4:
            case SurfaceFormat::HdrBlendable:
                return RendererFormatVerdict::Unsupported;
            default:
                return RendererFormatVerdict::Defer;
        }
    }

    RendererFormatVerdict SdlGpuRenderer::ClassifyTexture3DFormatEXT(int surfaceFormat) const
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
            // The ordinary CNA Texture3D surface currently exposes Color-shaped transfers only,
            // and both EasyGL's real storage path and this one allocate RGBA8 volumes.
            case SurfaceFormat::Color:
                return RendererFormatVerdict::Supported;

            // Do not inherit Texture2D's packed/BC/SNORM verdict: a format with no corresponding
            // public volume transfer and no verified 3D storage path must be refused at creation,
            // never accepted and silently represented as RGBA8.
            case SurfaceFormat::Bgr565:
            case SurfaceFormat::Bgra5551:
            case SurfaceFormat::Bgra4444:
            case SurfaceFormat::Dxt1:
            case SurfaceFormat::Dxt3:
            case SurfaceFormat::Dxt5:
            case SurfaceFormat::NormalizedByte2:
            case SurfaceFormat::NormalizedByte4:
            case SurfaceFormat::Rgba1010102:
            case SurfaceFormat::Rg32:
            case SurfaceFormat::Rgba64:
            case SurfaceFormat::Alpha8:
            case SurfaceFormat::Single:
            case SurfaceFormat::Vector2:
            case SurfaceFormat::Vector4:
            case SurfaceFormat::HalfSingle:
            case SurfaceFormat::HalfVector2:
            case SurfaceFormat::HalfVector4:
            case SurfaceFormat::HdrBlendable:
                return RendererFormatVerdict::Unsupported;
            default:
                return RendererFormatVerdict::Defer;
        }
    }

    RendererFormatVerdict SdlGpuRenderer::ClassifyRenderTargetFormatEXT(int surfaceFormat) const
    {
        const SurfaceFormat format = static_cast<SurfaceFormat>(surfaceFormat);
        RenderTargetFormatInfo info{};
        if (TryGetRenderTargetFormatInfo(format, info))
        {
            return SDL_GPUTextureSupportsFormat(
                       device_, info.nativeFormat, SDL_GPU_TEXTURETYPE_2D,
                       SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)
                ? RendererFormatVerdict::Supported
                : RendererFormatVerdict::Unsupported;
        }

        switch (format)
        {
            case SurfaceFormat::Bgr565:
            case SurfaceFormat::Bgra5551:
            case SurfaceFormat::Bgra4444:
            case SurfaceFormat::Dxt1:
            case SurfaceFormat::Dxt3:
            case SurfaceFormat::Dxt5:
            case SurfaceFormat::NormalizedByte2:
            case SurfaceFormat::NormalizedByte4:
            case SurfaceFormat::Rgba1010102:
            case SurfaceFormat::Rg32:
            case SurfaceFormat::Alpha8:
                return RendererFormatVerdict::Unsupported;
            default:
                return RendererFormatVerdict::Defer;
        }
    }

    RendererFormatVerdict SdlGpuRenderer::ClassifyRenderTargetCubeFormatEXT(
        int surfaceFormat) const
    {
        const SurfaceFormat format = static_cast<SurfaceFormat>(surfaceFormat);
        RenderTargetFormatInfo info{};
        if (TryGetRenderTargetFormatInfo(format, info))
        {
            return SDL_GPUTextureSupportsFormat(
                       device_, info.nativeFormat, SDL_GPU_TEXTURETYPE_CUBE,
                       SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)
                ? RendererFormatVerdict::Supported
                : RendererFormatVerdict::Unsupported;
        }

        const RendererFormatVerdict twoDimensional =
            ClassifyRenderTargetFormatEXT(surfaceFormat);
        return twoDimensional == RendererFormatVerdict::Defer
            ? RendererFormatVerdict::Defer
            : RendererFormatVerdict::Unsupported;
    }

    bool SdlGpuRenderer::IsCompressedTransferFormatEXT(int surfaceFormat) const
    {
        return IsClassicDxtFormat(
            static_cast<Microsoft::Xna::Framework::Graphics::SurfaceFormat>(surfaceFormat));
    }

    bool SdlGpuRenderer::IsCompressedCubeTransferFormatEXT(int surfaceFormat) const
    {
        return IsClassicDxtFormat(
            static_cast<Microsoft::Xna::Framework::Graphics::SurfaceFormat>(surfaceFormat));
    }

    bool SdlGpuRenderer::LoadsCompressedContentNativelyEXT() const
    {
        if (ForceDxtFallbackForTest())
            return false;
        constexpr std::array formats{
            SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM,
            SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM,
            SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM,
        };
        return std::all_of(formats.begin(), formats.end(), [this](SDL_GPUTextureFormat format) {
            // This process-wide content-reader policy is shared by Texture2D and TextureCube. It
            // may retain source blocks only when BOTH resource kinds can keep all classic DXT
            // variants compressed; otherwise the reader's established Color decode is the one
            // truthful representation for every resource it may construct.
            return SDL_GPUTextureSupportsFormat(
                       device_, format, SDL_GPU_TEXTURETYPE_2D,
                       SDL_GPU_TEXTUREUSAGE_SAMPLER) &&
                   SDL_GPUTextureSupportsFormat(
                       device_, format, SDL_GPU_TEXTURETYPE_CUBE,
                       SDL_GPU_TEXTUREUSAGE_SAMPLER);
        });
    }

    std::unique_ptr<ITextureRenderer> SdlGpuRenderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<SdlGpuTextureRenderer>(*this, data);
    }

    std::unique_ptr<ISpriteBatchRenderer> SdlGpuRenderer::CreateSpriteBatch()
    {
        return std::make_unique<SdlGpuSpriteBatchRenderer>(*this);
    }

    std::unique_ptr<IOcclusionQueryRenderer> SdlGpuRenderer::CreateOcclusionQuery()
    {
        throw System::NotSupportedException(
            "CNA SDL_GPU: OcclusionQuery is unavailable because vendored SDL_gpu 3.5.0 exposes "
            "no occlusion-query or query-pool commands; GPU fences report only command-buffer "
            "completion and cannot count samples that pass depth/stencil.");
    }

    void SdlGpuRenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                                int colorDstBlend, int alphaDstBlend,
                                                int colorBlendFunc, int alphaBlendFunc,
                                                const BlendWriteState& writeState)
    {
        // Blend::One=0, Blend::Zero=1 -> Opaque preset: src=One, dst=Zero -> no blending. Matches
        // VulkanRenderer::ApplyBlendState's own derivation exactly.
        // One/Zero is a copy only under Add. Subtract/ReverseSubtract/Min/Max remain observable
        // with those same factors, so collapsing them to SDL's disabled-blend path would both
        // change the equation and make function-only A -> B -> A transitions reuse Opaque.
        blendEnabled_ = !(colorSrcBlend == 0 && colorDstBlend == 1 &&
                          alphaSrcBlend == 0 && alphaDstBlend == 1 &&
                          colorBlendFunc == 0 && alphaBlendFunc == 0);
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

    void SdlGpuRenderer::ApplySamplerMipState(int slot, int maxMipLevel, float lodBias)
    {
        if (slot < 0 || slot >= static_cast<int>(samplerSlots_.size()))
            return;
        samplerSlots_[slot].maxMipLevel = maxMipLevel;
        samplerSlots_[slot].lodBias = lodBias;
    }

    void SdlGpuRenderer::ApplySamplerAddressW(int slot, int addressW)
    {
        if (slot < 0 || slot >= static_cast<int>(samplerSlots_.size()))
            return;
        samplerSlots_[slot].addressW = addressW;
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

    Matrix SdlGpuRenderer::ApplyXnaPixelCenter(const Matrix& wvp) const
    {
        bool multisampledDestination = false;
        if (currentRenderTarget_ != nullptr)
            multisampledDestination =
                currentRenderTarget_->State()->sampleCount != SDL_GPU_SAMPLECOUNT_1;
        else if (currentRenderTargetCube_ != nullptr)
            multisampledDestination =
                currentRenderTargetCube_->State()->sampleCount != SDL_GPU_SAMPLECOUNT_1;

        const int width = viewportSet_ ? viewportW_
            : currentRenderTarget_ != nullptr ? currentRenderTarget_->GetWidth()
            : currentRenderTargetCube_ != nullptr ? currentRenderTargetCube_->GetSize()
            : physicalWidth_;
        const int height = viewportSet_ ? viewportH_
            : currentRenderTarget_ != nullptr ? currentRenderTarget_->GetHeight()
            : currentRenderTargetCube_ != nullptr ? currentRenderTargetCube_->GetSize()
            : physicalHeight_;
        if (multisampledDestination || width <= 0 || height <= 0)
            return wvp;

        // XNA 4.0 inherits Direct3D 9's integer window-pixel centres. SDL_gpu's Vulkan path uses
        // the modern half-integer convention, so move geometry just under half a pixel right and
        // down. The 63/64 margin is EasyGL's measured Wine/MonoGame-compatible correction: it
        // avoids placing an edge exactly on the opposite tie while preserving XNA's top-left rule.
        constexpr float kPixelCenterScale = 63.0f / 64.0f;
        return wvp * Matrix::CreateTranslation(
            kPixelCenterScale / static_cast<float>(width),
            -kPixelCenterScale / static_cast<float>(height), 0.0f);
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
        int w, int h, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        return CreateRenderTarget2DEXT(
            w, h, depthFormat, preserveContents, mipMap, multiSampleCount,
            static_cast<int>(SurfaceFormat::Color));
    }

    std::unique_ptr<IRenderTargetRenderer> SdlGpuRenderer::CreateRenderTarget2DEXT(
        int w, int h, int depthFormat, bool preserveContents, bool mipMap,
        int multiSampleCount, int surfaceFormat)
    {
        if (ClassifyRenderTargetFormatEXT(surfaceFormat) != RendererFormatVerdict::Supported)
        {
            throw std::runtime_error(
                "CNA SDL_GPU: SurfaceFormat ordinal " + std::to_string(surfaceFormat) +
                " is not supported as an exact RenderTarget2D color attachment on this device");
        }
        return std::make_unique<SdlGpuRenderTargetRenderer>(
            *this, w, h, depthFormat, preserveContents, mipMap,
            multiSampleCount, surfaceFormat);
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
        return CreateRenderTargetCubeEXT(
            size, depthFormat, preserveContents, mipMap, multiSampleCount,
            static_cast<int>(SurfaceFormat::Color));
    }

    std::unique_ptr<IRenderTargetCubeRenderer> SdlGpuRenderer::CreateRenderTargetCubeEXT(
        int size, int depthFormat, bool preserveContents, bool mipMap,
        int multiSampleCount, int surfaceFormat)
    {
        // REMED-GFX-136 threaded `preserveContents` here and left it unused: RenderToTargetCubeFace()
        // already picks SDL_GPU_LOADOP_LOAD unless a real Clear() is pending for that face
        // (clearColorPending[face]), and the only clear a cube target gets without the game asking
        // is the one GraphicsDevice::SetRenderTargets issues for a DiscardContents target -- so a
        // single-sample face is preserved by construction. REMED-GFX-141 gives it a real consumer:
        // a multisampled face now owns its own multisample texture, and this flag decides whether
        // that texture's store op keeps the samples (RESOLVE_AND_STORE) or drops them (RESOLVE).
        if (ClassifyRenderTargetCubeFormatEXT(surfaceFormat) !=
            RendererFormatVerdict::Supported)
        {
            throw std::runtime_error(
                "CNA SDL_GPU: SurfaceFormat ordinal " + std::to_string(surfaceFormat) +
                " is not supported as an exact RenderTargetCube color attachment on this device");
        }
        return std::make_unique<SdlGpuRenderTargetCubeRenderer>(
            *this, size, depthFormat, preserveContents, mipMap,
            multiSampleCount, surfaceFormat);
    }

    std::unique_ptr<ITexture3DRenderer> SdlGpuRenderer::CreateTexture3D(
        int w, int h, int depth, bool mipMap, int surfaceFormat)
    {
        return std::make_unique<SdlGpuTexture3DRenderer>(
            *this, w, h, depth, mipMap, surfaceFormat);
    }

    std::unique_ptr<ITextureCubeRenderer> SdlGpuRenderer::CreateTextureCube(
        int size, bool mipMap, int surfaceFormat)
    {
        return std::make_unique<SdlGpuTextureCubeRenderer>(*this, size, mipMap, surfaceFormat);
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

        // Stock shaders declare only output 0, as on the sibling renderers. A source ShaderEffect
        // or ordinary compiled XNA Effect may declare outputs 1..3 and then writes all `count`
        // targets in this one real pass. Pipeline descriptions use every attachment's native
        // format (SDLGPU-75), so mixed-format sets are genuine rather than slot-0 aliases.
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
        fsInfo.num_uniform_buffers = 1;
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
                                                  activeColorTargetFormats_, colorTargetCount,
                                                  sampleCount, depthStencilFormat, renderState);
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
        attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
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
        FillColorTargetDescriptions(colorTargets, colorTargetCount,
                                    activeColorTargetFormats_, renderState);

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
            pipelineInfo.primitive_type, depthStencilFormat);
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
                                                              const char* family,
                                                              int maxMipLevel, float lodBias,
                                                              int addressW)
    {
        const int clampedAniso = std::clamp(maxAnisotropy, 1, 16);
        // plans/plan_fx.md FX-083: XNA's MaxMipLevel is the most detailed level the sampler may use,
        // i.e. a lower bound on the computed level of detail -- SDL_GPU's min_lod. That is the
        // same translation FNA3D's own SDL_GPU driver makes.
        const float minLod = static_cast<float>(std::max(maxMipLevel, 0));
        const SamplerCacheKeyEXT key = SamplerCacheKeyEXT::Make(
            textureFilter, addressU, addressV, addressW, clampedAniso, maxMipLevel, lodBias);
        const auto it = samplerCache_.find(key);
        const bool hit = it != samplerCache_.end();

        SDL_GPUSamplerCreateInfo createInfo{};
        FillSdlGpuSamplerCreateInfo(createInfo, textureFilter, addressU, addressV, clampedAniso);
        createInfo.address_mode_w = ToAddressMode(addressW);
        createInfo.min_lod = minLod;
        createInfo.max_lod = std::max(createInfo.max_lod, minLod);
        createInfo.mip_lod_bias = lodBias;

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
                         "aniso=%s/%.1f addrU=%s addrV=%s addrW=%d minLod=%.1f bias=%.9g "
                         "keyhash=0x%016llx sampler=%p %s\n",
                         family, textureFilter, TextureFilterName(textureFilter),
                         SdlFilterName(createInfo.mag_filter),
                         SdlFilterName(createInfo.min_filter),
                         SdlMipmapModeName(createInfo.mipmap_mode),
                         createInfo.enable_anisotropy ? "on" : "off",
                         static_cast<double>(createInfo.max_anisotropy),
                         SdlAddressModeName(createInfo.address_mode_u),
                         SdlAddressModeName(createInfo.address_mode_v),
                         key.addressW,
                         static_cast<double>(createInfo.min_lod),
                         static_cast<double>(createInfo.mip_lod_bias),
                         static_cast<unsigned long long>(SamplerCacheKeyHashEXT{}(key)),
                         static_cast<void*>(sampler),
                         hit ? "HIT" : "CREATE");
        }
        return sampler;
    }

    void SdlGpuRenderer::SeedMultisampleTargetFromResolved(
        SDL_GPUTexture* resolvedTexture, int resolvedLayer,
        SDL_GPUTexture* multisampleTexture, SDL_GPUTextureFormat colorFormat,
        SDL_GPUSampleCount sampleCount, int surfaceFormat,
        int width, int height, const char* diagnostic)
    {
        activeColorTargetFormats_.fill(SDL_GPU_TEXTUREFORMAT_INVALID);
        activeColorTargetFormats_[0] = colorFormat;
        RenderStateSnapshot seedState{};
        SDL_GPUGraphicsPipeline* pipeline = GetOrCreateSpritePipeline(
            colorFormat, sampleCount, SDL_GPU_TEXTUREFORMAT_INVALID, 1,
            false, false, 3, seedState);
        SDL_GPUSampler* sampler = GetOrCreateSampler(
            1, 1, 1, 1, "RenderTargetSetData/MSAASeed");

        const float right = static_cast<float>(width);
        const float bottom = static_cast<float>(height);
        const std::array<SpriteVertex, 6> vertices{{
            {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
            {right, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
            {0.0f, bottom, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
            {0.0f, bottom, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
            {right, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
            {right, bottom, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        }};

        SDL_GPUBufferCreateInfo bufferInfo{};
        bufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bufferInfo.size = sizeof(vertices);
        SDL_GPUBuffer* vertexBuffer = SDL_CreateGPUBuffer(device_, &bufferInfo);
        if (vertexBuffer == nullptr)
            throw std::runtime_error(std::string(diagnostic) +
                                     ": failed to create MSAA seed vertex buffer: " +
                                     SDL_GetError());

        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = sizeof(vertices);
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &transferInfo);
        SDL_GPUTexture* sampleTexture = resolvedTexture;
        SDL_GPUTexture* faceTexture = nullptr;
        try
        {
            if (transfer == nullptr)
                throw std::runtime_error(std::string(diagnostic) +
                                         ": failed to create MSAA seed transfer buffer: " +
                                         SDL_GetError());
            void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false);
            if (mapped == nullptr)
                throw std::runtime_error(std::string(diagnostic) +
                                         ": failed to map MSAA seed transfer buffer: " +
                                         SDL_GetError());
            std::memcpy(mapped, vertices.data(), sizeof(vertices));
            SDL_UnmapGPUTransferBuffer(device_, transfer);

            // The stock sprite shader samples a 2D texture. A cube face is first copied, on the
            // same command-buffer timeline, into a temporary 2D view-compatible texture rather
            // than binding a cube resource to an incompatible sampler2D declaration.
            if (resolvedLayer >= 0)
            {
                SDL_GPUTextureCreateInfo faceInfo{};
                faceInfo.type = SDL_GPU_TEXTURETYPE_2D;
                faceInfo.format = colorFormat;
                faceInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
                faceInfo.width = static_cast<Uint32>(width);
                faceInfo.height = static_cast<Uint32>(height);
                faceInfo.layer_count_or_depth = 1;
                faceInfo.num_levels = 1;
                faceInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
                faceTexture = SDL_CreateGPUTexture(device_, &faceInfo);
                if (faceTexture == nullptr)
                    throw std::runtime_error(std::string(diagnostic) +
                                             ": failed to create cube-face seed texture: " +
                                             SDL_GetError());
                sampleTexture = faceTexture;
            }

            SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(device_);
            if (command == nullptr)
                throw std::runtime_error(std::string(diagnostic) +
                                         ": failed to acquire MSAA seed command buffer: " +
                                         SDL_GetError());
            FrameCommandBufferOwner commandOwner(command, testHooks_);
            {
                SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(command);
                if (copyPass == nullptr)
                    throw std::runtime_error(std::string(diagnostic) +
                                             ": failed to begin MSAA seed copy pass: " +
                                             SDL_GetError());
                CopyPassOwner copyOwner(copyPass);
                SDL_GPUTransferBufferLocation vertexSource{};
                vertexSource.transfer_buffer = transfer;
                SDL_GPUBufferRegion vertexDestination{};
                vertexDestination.buffer = vertexBuffer;
                vertexDestination.size = sizeof(vertices);
                SDL_UploadToGPUBuffer(
                    copyPass, &vertexSource, &vertexDestination, false);

                if (faceTexture != nullptr)
                {
                    SDL_GPUTextureLocation faceSource{};
                    faceSource.texture = resolvedTexture;
                    faceSource.layer = static_cast<Uint32>(resolvedLayer);
                    SDL_GPUTextureLocation faceDestination{};
                    faceDestination.texture = faceTexture;
                    SDL_CopyGPUTextureToTexture(
                        copyPass, &faceSource, &faceDestination,
                        static_cast<Uint32>(width), static_cast<Uint32>(height), 1, false);
                }
            }
            SDL_ReleaseGPUTransferBuffer(device_, transfer);
            transfer = nullptr;

            SDL_GPUColorTargetInfo colorTarget{};
            colorTarget.texture = multisampleTexture;
            colorTarget.load_op = SDL_GPU_LOADOP_DONT_CARE;
            colorTarget.store_op = SDL_GPU_STOREOP_STORE;
            colorTarget.cycle = false;
            SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(command, &colorTarget, 1, nullptr);
            if (pass == nullptr)
                throw std::runtime_error(std::string(diagnostic) +
                                         ": failed to begin MSAA seed render pass: " +
                                         SDL_GetError());
            {
                RenderPassOwner passOwner(pass);
                SDL_BindGPUGraphicsPipeline(pass, pipeline);
                const float viewportSize[2] = {right, bottom};
                SDL_PushGPUVertexUniformData(command, 0, viewportSize, sizeof(viewportSize));
                const std::array<float, 8> expansion = SpriteChannelExpansion(surfaceFormat);
                SDL_PushGPUFragmentUniformData(
                    command, 0, expansion.data(), sizeof(expansion));

                SDL_GPUViewport viewport{};
                viewport.w = right;
                viewport.h = bottom;
                viewport.min_depth = 0.0f;
                viewport.max_depth = 1.0f;
                SDL_SetGPUViewport(pass, &viewport);
                SDL_Rect scissor{0, 0, width, height};
                SDL_SetGPUScissor(pass, &scissor);

                SDL_GPUBufferBinding vertexBinding{};
                vertexBinding.buffer = vertexBuffer;
                SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
                SDL_GPUTextureSamplerBinding textureBinding{};
                textureBinding.texture = sampleTexture;
                textureBinding.sampler = sampler;
                SDL_BindGPUFragmentSamplers(pass, 0, &textureBinding, 1);
                SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
            }

            if (!commandOwner.Submit())
                throw std::runtime_error(std::string(diagnostic) +
                                         ": failed to submit MSAA seed render: " + SDL_GetError());
        }
        catch (...)
        {
            if (transfer != nullptr) SDL_ReleaseGPUTransferBuffer(device_, transfer);
            if (faceTexture != nullptr) SDL_ReleaseGPUTexture(device_, faceTexture);
            SDL_ReleaseGPUBuffer(device_, vertexBuffer);
            throw;
        }

        if (faceTexture != nullptr) SDL_ReleaseGPUTexture(device_, faceTexture);
        SDL_ReleaseGPUBuffer(device_, vertexBuffer);
    }

    void SdlGpuRenderer::InitializeSpritePipelineAndSamplerForTestEXT()
    {
        const SDL_GPUTextureFormat colorFormat =
            SDL_GetGPUSwapchainTextureFormat(device_, window_);
        activeColorTargetFormats_.fill(SDL_GPU_TEXTUREFORMAT_INVALID);
        activeColorTargetFormats_[0] = colorFormat;
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
                activeColorTargetFormats_, sampleCount, depthStencilFormat, colorTargetCount,
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
                pass, cmd, command.compiledEffect,
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
            const std::array<float, 8> channelExpansion =
                SpriteChannelExpansion(command.surfaceFormat);
            SDL_PushGPUFragmentUniformData(
                cmd, 0, channelExpansion.data(), sizeof(channelExpansion));
        }

        SDL_GPUBufferBinding vbBinding{};
        vbBinding.buffer = spriteVertexBuffer_;
        vbBinding.offset = static_cast<Uint32>(index * sizeof(SpriteVertex) * 6);
        SDL_BindGPUVertexBuffers(pass, 0, &vbBinding, 1);

        SDL_GPUTextureSamplerBinding samplerBinding{};
        samplerBinding.texture = command.texture.texture;
        samplerBinding.sampler = GetOrCreateSampler(command.textureFilter, command.addressU,
                                                   command.addressV, command.maxAnisotropy,
                                                   "SpriteBatch", command.maxMipLevel,
                                                   command.lodBias, command.addressW);
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
                                             float layerDepth,
                                             const Matrix& transform,
                                             int textureFilter,
                                             int addressU,
                                             int addressV,
                                             int addressW,
                                             int maxAnisotropy,
                                             int maxMipLevel,
                                             float lodBias,
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

        // REMED-GFX-072 / SDLGPU-68: SpriteBatch positions are viewport-local logical pixels.
        // GraphicsDevice has already mapped the public logical Viewport to the renderer's physical
        // presentation rectangle, so recover that logical extent for the shader projection. This
        // applies to the default letterbox/overscan viewport as well as a custom sub-viewport: the
        // native viewport supplies the physical offset/scale while the vertices stay local.
        float spriteProjectionWidth = 0.0f;
        float spriteProjectionHeight = 0.0f;
        if (viewportSet_ && viewportW_ > 0 && viewportH_ > 0)
        {
            spriteProjectionWidth = static_cast<float>(viewportW_);
            spriteProjectionHeight = static_cast<float>(viewportH_);
            if (currentRenderTarget_ == nullptr && viewport.width > 0.0f && viewport.height > 0.0f)
            {
                spriteProjectionWidth *= viewport.logicalWidth / viewport.width;
                spriteProjectionHeight *= viewport.logicalHeight / viewport.height;
            }
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
        std::array<float, 4> projectedDepths{};
        for (Vector2& point : points)
        {
            const float rotatedX = point.X * c - point.Y * s + static_cast<float>(destination.X);
            const float rotatedY = point.X * s + point.Y * c + static_cast<float>(destination.Y);
            const std::size_t corner = static_cast<std::size_t>(&point - points.data());
            const float transformedZ =
                rotatedX * transform.M13 + rotatedY * transform.M23
                + layerDepth * transform.M33 + transform.M43;
            const float transformedW =
                rotatedX * transform.M14 + rotatedY * transform.M24
                + layerDepth * transform.M34 + transform.M44;
            // FNA applies transform * CreateOrthographicOffCenter(..., -1, 1). Preserve that
            // projection's Z/W result even though this renderer CPU-expands the 2D X/Y transform.
            projectedDepths[corner] = transformedW != 0.0f
                ? (0.5f * transformedW - 0.5f * transformedZ) / transformedW
                : 0.0f;
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
        command.surfaceFormat = texture.GetSurfaceFormatEXT();
        command.projectionWidth = spriteProjectionWidth;
        command.projectionHeight = spriteProjectionHeight;
        command.textureFilter = textureFilter;
        command.addressU = addressU;
        command.addressV = addressV;
        command.addressW = addressW;
        command.maxAnisotropy = maxAnisotropy;
        command.maxMipLevel = maxMipLevel;
        command.lodBias = lodBias;
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
        // plans/plan_fx.md FX-071: the compiled-effect counterpart of the customEffect snapshot above --
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
                VertexElement(offsetof(SpriteVertex, x), VertexElementFormat::Vector3,
                             VertexElementUsage::Position, 0),
                VertexElement(offsetof(SpriteVertex, u), VertexElementFormat::Vector2,
                             VertexElementUsage::TextureCoordinate, 0),
                VertexElement(offsetof(SpriteVertex, r), VertexElementFormat::Vector4,
                             VertexElementUsage::Color, 0),
            };
            const std::vector<SdlGpuCompiledEffectVertexStreamEXT> streams{{
                &kSpriteVertexDeclaration, static_cast<Uint32>(sizeof(SpriteVertex)),
                SDL_GPU_VERTEXINPUTRATE_VERTEX}};
            command.compiledEffect = BuildCompiledEffectBindingEXT(
                *sdlGpuEffect, streams, &nativeTexture);
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
            // The old hardcoded shader Z=0 placed every sprite on the near plane, where native
            // floating-depth constant bias loses the precision XNA's normalized offset expects.
            vertex.z = projectedDepths[static_cast<std::size_t>(corner)];
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
        const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& vertexLayout,
        SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
        SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, const RenderStateSnapshot& renderState)
    {
        const std::size_t key = StockPipelineKey(
            PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, activeColorTargetFormats_,
                             colorTargetCount, sampleCount, depthStencilFormat, renderState),
            vertexLayout);
        const auto it = coloredPipelines_.find(key);
        if (it != coloredPipelines_.end())
            return it->second;

        SdlGpuStockVertexStateEXT vertexState;
        BuildSdlGpuStockVertexStateEXT(vertexLayout, vertexState);

        std::array<SDL_GPUColorTargetDescription, 4> colorTargets{};
        FillColorTargetDescriptions(colorTargets, colorTargetCount,
                                    activeColorTargetFormats_, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = coloredVertexShader_;
        pipelineInfo.fragment_shader = coloredFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = vertexState.buffers.data();
        pipelineInfo.vertex_input_state.num_vertex_buffers = vertexState.bufferCount;
        pipelineInfo.vertex_input_state.vertex_attributes = vertexState.attributes.data();
        pipelineInfo.vertex_input_state.num_vertex_attributes = vertexState.attributeCount;
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(
            pipelineInfo.rasterizer_state, renderState,
            pipelineInfo.primitive_type, depthStencilFormat);  // SDLGPU-20 / REMED-GFX-051
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
        const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& vertexLayout,
        SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
        SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, const RenderStateSnapshot& renderState)
    {
        const std::size_t key = StockPipelineKey(
            PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, activeColorTargetFormats_,
                             colorTargetCount, sampleCount, depthStencilFormat, renderState),
            vertexLayout);
        const auto it = texturedPipelines_.find(key);
        if (it != texturedPipelines_.end())
            return it->second;

        SdlGpuStockVertexStateEXT vertexState;
        BuildSdlGpuStockVertexStateEXT(vertexLayout, vertexState);

        std::array<SDL_GPUColorTargetDescription, 4> colorTargets{};
        FillColorTargetDescriptions(colorTargets, colorTargetCount,
                                    activeColorTargetFormats_, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = texturedVertexShader_;
        pipelineInfo.fragment_shader = texturedFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = vertexState.buffers.data();
        pipelineInfo.vertex_input_state.num_vertex_buffers = vertexState.bufferCount;
        pipelineInfo.vertex_input_state.vertex_attributes = vertexState.attributes.data();
        pipelineInfo.vertex_input_state.num_vertex_attributes = vertexState.attributeCount;
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(
            pipelineInfo.rasterizer_state, renderState,
            pipelineInfo.primitive_type, depthStencilFormat);
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
        const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& vertexLayout,
        SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
        SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, const RenderStateSnapshot& renderState)
    {
        const std::size_t key = StockPipelineKey(
            PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, activeColorTargetFormats_,
                             colorTargetCount, sampleCount, depthStencilFormat, renderState),
            vertexLayout);
        const auto it = coloredTexturedPipelines_.find(key);
        if (it != coloredTexturedPipelines_.end())
            return it->second;

        SdlGpuStockVertexStateEXT vertexState;
        BuildSdlGpuStockVertexStateEXT(vertexLayout, vertexState);

        std::array<SDL_GPUColorTargetDescription, 4> colorTargets{};
        FillColorTargetDescriptions(colorTargets, colorTargetCount,
                                    activeColorTargetFormats_, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = coloredTexturedVertexShader_;
        pipelineInfo.fragment_shader = texturedFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = vertexState.buffers.data();
        pipelineInfo.vertex_input_state.num_vertex_buffers = vertexState.bufferCount;
        pipelineInfo.vertex_input_state.vertex_attributes = vertexState.attributes.data();
        pipelineInfo.vertex_input_state.num_vertex_attributes = vertexState.attributeCount;
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(
            pipelineInfo.rasterizer_state, renderState,
            pipelineInfo.primitive_type, depthStencilFormat);
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
        const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& vertexLayout,
        SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
        SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, const RenderStateSnapshot& renderState)
    {
        const std::size_t key = StockPipelineKey(
            PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, activeColorTargetFormats_,
                             colorTargetCount, sampleCount, depthStencilFormat, renderState),
            vertexLayout);
        const auto it = litTexturedPipelines_.find(key);
        if (it != litTexturedPipelines_.end())
            return it->second;

        SdlGpuStockVertexStateEXT vertexState;
        BuildSdlGpuStockVertexStateEXT(vertexLayout, vertexState);

        std::array<SDL_GPUColorTargetDescription, 4> colorTargets{};
        FillColorTargetDescriptions(colorTargets, colorTargetCount,
                                    activeColorTargetFormats_, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = litTexturedVertexShader_;
        pipelineInfo.fragment_shader = litTexturedFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = vertexState.buffers.data();
        pipelineInfo.vertex_input_state.num_vertex_buffers = vertexState.bufferCount;
        pipelineInfo.vertex_input_state.vertex_attributes = vertexState.attributes.data();
        pipelineInfo.vertex_input_state.num_vertex_attributes = vertexState.attributeCount;
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(
            pipelineInfo.rasterizer_state, renderState,
            pipelineInfo.primitive_type, depthStencilFormat);
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
                                                  const GpuDrawParams* params,
                                                  const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& vertexLayout)
    {
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferRenderer&>(vb);

        ColoredDrawCommand command;
        command.vertexLayout = vertexLayout;
        const int vertexStart = params != nullptr ? params->vertexStart : 0;
        StockDrawVertexStreamsEXT streams;
        CollectStockVertexStreamsEXT(sdlGpuVb, params, streams);
        CaptureStockVertexStreamsEXT(vertexLayout, streams, vertexStart,
                                     command.vertexData, command.extraVertexStreams);
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        command.renderState = CaptureRenderState();
        if (params != nullptr)
        {
            const Matrix wvp = ApplyXnaPixelCenter(world * view * projection);
            FillExtUniforms(command.uniforms, wvp, *params);
            FillFogUniforms(command.fogUniforms, *params);  // REMED-GFX-009
        }
        else
        {
            FillColoredUniforms(
                command.uniforms, ApplyXnaPixelCenter(world * view * projection));
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
                                                   PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params,
                                                   bool hasVertexColor,
                                                   const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& vertexLayout)
    {
        // plans/plan_gltf.md GLTF-474: the replay binds neutral white when no base-colour map is
        // bound, so the 1x1 texture has to exist by then. Creating it here rather than in the
        // replay keeps every allocation on the queueing side, where a failure still has a
        // caller to report to.
        EnsureDefaultPbrTextures();
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferRenderer&>(vb);
        TexturedDrawCommand command;
        command.hasVertexColor = hasVertexColor;
        command.vertexLayout = vertexLayout;
        const int vertexStart = params.vertexStart;
        StockDrawVertexStreamsEXT streams;
        CollectStockVertexStreamsEXT(sdlGpuVb, &params, streams);
        CaptureStockVertexStreamsEXT(vertexLayout, streams, vertexStart,
                                     command.vertexData, command.extraVertexStreams);
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        command.renderState = CaptureRenderState();
        const Matrix wvp = ApplyXnaPixelCenter(world * view * projection);
        FillExtUniforms(command.uniforms, wvp, params);
        FillFogUniforms(command.fogUniforms, params);  // REMED-GFX-009
        command.texture = ResolveSampledTextureEXT(params.texture0, "BasicEffect.Texture");
        // SDLGPU-21: real per-slot dynamic sampler state (GraphicsDevice.SamplerStates[0]).
        command.textureFilter = samplerSlots_[0].filter;
        command.addressU = samplerSlots_[0].addressU;
        command.addressV = samplerSlots_[0].addressV;
        command.maxAnisotropy = samplerSlots_[0].maxAnisotropy;  // REMED-GFX-170
        command.maxMipLevel = samplerSlots_[0].maxMipLevel;
        command.lodBias = samplerSlots_[0].lodBias;
        command.addressW = samplerSlots_[0].addressW;

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
                                                      PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params,
                                                      const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& vertexLayout)
    {
        // plans/plan_gltf.md GLTF-474: the replay binds neutral white when no base-colour map is
        // bound, so the 1x1 texture has to exist by then. Creating it here rather than in the
        // replay keeps every allocation on the queueing side, where a failure still has a
        // caller to report to.
        EnsureDefaultPbrTextures();
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferRenderer&>(vb);
        LitTexturedDrawCommand command;
        command.vertexLayout = vertexLayout;
        const int vertexStart = params.vertexStart;
        StockDrawVertexStreamsEXT streams;
        CollectStockVertexStreamsEXT(sdlGpuVb, &params, streams);
        CaptureStockVertexStreamsEXT(vertexLayout, streams, vertexStart,
                                     command.vertexData, command.extraVertexStreams);
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        command.renderState = CaptureRenderState();
        const Matrix wvp = ApplyXnaPixelCenter(world * view * projection);
        FillExtUniforms(command.uniforms, wvp, params);
        FillFogUniforms(command.fogUniforms, params);  // REMED-GFX-009
        FillLitLightUniforms(command.lightUniforms, params);
        command.texture = ResolveSampledTextureEXT(params.texture0, "BasicEffect.Texture (lit)");
        command.textureFilter = samplerSlots_[0].filter;
        command.addressU = samplerSlots_[0].addressU;
        command.addressV = samplerSlots_[0].addressV;
        command.maxAnisotropy = samplerSlots_[0].maxAnisotropy;  // REMED-GFX-170
        command.maxMipLevel = samplerSlots_[0].maxMipLevel;
        command.lodBias = samplerSlots_[0].lodBias;
        command.addressW = samplerSlots_[0].addressW;

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
        bool hasVertexColor,
        const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& vertexLayout,
        SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
        SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, const RenderStateSnapshot& renderState)
    {
        if (hasVertexColor)
        {
            const std::size_t key = StockPipelineKey(
                PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, activeColorTargetFormats_,
                                 colorTargetCount, sampleCount, depthStencilFormat, renderState),
                vertexLayout);
            const auto it = alphaTestColoredPipelines_.find(key);
            if (it != alphaTestColoredPipelines_.end())
                return it->second;

            SdlGpuStockVertexStateEXT vertexState;
            BuildSdlGpuStockVertexStateEXT(vertexLayout, vertexState);

            std::array<SDL_GPUColorTargetDescription, 4> colorTargets{};
            FillColorTargetDescriptions(colorTargets, colorTargetCount,
                                        activeColorTargetFormats_, renderState);

            SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.vertex_shader = alphaTestColoredVertexShader_;
            pipelineInfo.fragment_shader = alphaTestFragmentShader_;
            pipelineInfo.vertex_input_state.vertex_buffer_descriptions = vertexState.buffers.data();
            pipelineInfo.vertex_input_state.num_vertex_buffers = vertexState.bufferCount;
            pipelineInfo.vertex_input_state.vertex_attributes = vertexState.attributes.data();
            pipelineInfo.vertex_input_state.num_vertex_attributes = vertexState.attributeCount;
            pipelineInfo.primitive_type = topology;
            FillRasterizerState(
                pipelineInfo.rasterizer_state, renderState,
                pipelineInfo.primitive_type, depthStencilFormat);
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

        const std::size_t key = StockPipelineKey(
            PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, activeColorTargetFormats_,
                             colorTargetCount, sampleCount, depthStencilFormat, renderState),
            vertexLayout);
        const auto it = alphaTestPipelines_.find(key);
        if (it != alphaTestPipelines_.end())
            return it->second;

        SdlGpuStockVertexStateEXT vertexState;
        BuildSdlGpuStockVertexStateEXT(vertexLayout, vertexState);

        std::array<SDL_GPUColorTargetDescription, 4> colorTargets{};
        FillColorTargetDescriptions(colorTargets, colorTargetCount,
                                    activeColorTargetFormats_, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = alphaTestVertexShader_;
        pipelineInfo.fragment_shader = alphaTestFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = vertexState.buffers.data();
        pipelineInfo.vertex_input_state.num_vertex_buffers = vertexState.bufferCount;
        pipelineInfo.vertex_input_state.vertex_attributes = vertexState.attributes.data();
        pipelineInfo.vertex_input_state.num_vertex_attributes = vertexState.attributeCount;
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(
            pipelineInfo.rasterizer_state, renderState,
            pipelineInfo.primitive_type, depthStencilFormat);
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
        bool hasVertexColor,
        const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& vertexLayout,
        SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
        SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, const RenderStateSnapshot& renderState)
    {
        auto& cache = hasVertexColor ? dualTextureColoredPipelines_ : dualTexturePipelines_;
        const std::size_t key = StockPipelineKey(
            PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, activeColorTargetFormats_,
                             colorTargetCount, sampleCount, depthStencilFormat, renderState),
            vertexLayout);
        const auto it = cache.find(key);
        if (it != cache.end())
            return it->second;

        SdlGpuStockVertexStateEXT vertexState;
        BuildSdlGpuStockVertexStateEXT(vertexLayout, vertexState);

        std::array<SDL_GPUColorTargetDescription, 4> colorTargets{};
        FillColorTargetDescriptions(colorTargets, colorTargetCount,
                                    activeColorTargetFormats_, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = hasVertexColor ? dualTextureColoredVertexShader_ : dualTextureVertexShader_;
        pipelineInfo.fragment_shader = dualTextureFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = vertexState.buffers.data();
        pipelineInfo.vertex_input_state.num_vertex_buffers = vertexState.bufferCount;
        pipelineInfo.vertex_input_state.vertex_attributes = vertexState.attributes.data();
        pipelineInfo.vertex_input_state.num_vertex_attributes = vertexState.attributeCount;
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(
            pipelineInfo.rasterizer_state, renderState,
            pipelineInfo.primitive_type, depthStencilFormat);
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

    void SdlGpuRenderer::CreateInstancedResources(ConstructionResources& resources)
    {
        SDL_GPUShaderCreateInfo vsInfo{};
        vsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kInstanced3dVertSpv);
        vsInfo.code_size = Shaders::kInstanced3dVertSpv_size;
        vsInfo.entrypoint = "main";
        vsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        vsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        vsInfo.num_uniform_buffers = 2;
        resources.CreateShader(
            ConstructionShader::InstancedVertex,
            SdlGpuFailurePointEXT::InstancedVertexShaderCreation, vsInfo,
            "CNA SDL_GPU: failed to create instanced3d vertex shader: ");
    }

    void SdlGpuRenderer::DestroyInstancedResources()
    {
        for (auto& [key, pipeline] : instancedPipelines_)
            ReleaseGraphicsPipeline(pipeline);
        instancedPipelines_.clear();
        ReleaseShader(instancedVertexShader_);
    }

    SDL_GPUGraphicsPipeline* SdlGpuRenderer::GetOrCreatePipelineInstanced3D(
        const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& vertexLayout,
        SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
        SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount,
        const RenderStateSnapshot& renderState)
    {
        const std::size_t key = StockPipelineKey(
            PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, activeColorTargetFormats_,
                             colorTargetCount, sampleCount, depthStencilFormat, renderState),
            vertexLayout);
        if (const auto it = instancedPipelines_.find(key); it != instancedPipelines_.end())
            return it->second;

        SdlGpuStockVertexStateEXT vertexState;
        BuildSdlGpuStockVertexStateEXT(vertexLayout, vertexState);
        std::array<SDL_GPUColorTargetDescription, 4> colorTargets{};
        FillColorTargetDescriptions(colorTargets, colorTargetCount,
                                    activeColorTargetFormats_, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = instancedVertexShader_;
        pipelineInfo.fragment_shader = coloredFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = vertexState.buffers.data();
        pipelineInfo.vertex_input_state.num_vertex_buffers = vertexState.bufferCount;
        pipelineInfo.vertex_input_state.vertex_attributes = vertexState.attributes.data();
        pipelineInfo.vertex_input_state.num_vertex_attributes = vertexState.attributeCount;
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(pipelineInfo.rasterizer_state, renderState,
                            topology, depthStencilFormat);
        pipelineInfo.multisample_state.sample_count = sampleCount;
        FillDepthStencilState(pipelineInfo.depth_stencil_state, depthTest, depthWrite,
                              depthFunc, renderState);
        pipelineInfo.target_info.color_target_descriptions = colorTargets.data();
        pipelineInfo.target_info.num_color_targets = static_cast<Uint32>(colorTargetCount);
        pipelineInfo.target_info.has_depth_stencil_target =
            depthStencilFormat != SDL_GPU_TEXTUREFORMAT_INVALID;
        pipelineInfo.target_info.depth_stencil_format = depthStencilFormat;

        SDL_GPUGraphicsPipeline* pipeline = CreateGraphicsPipeline(
            pipelineInfo, "CNA SDL_GPU: failed to create instanced3d pipeline: ");
        return CacheGraphicsPipeline(instancedPipelines_, key, pipeline);
    }

    SDL_GPUGraphicsPipeline* SdlGpuRenderer::GetOrCreatePipelineEnvMap3D(
        const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& vertexLayout,
        SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
        SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, const RenderStateSnapshot& renderState)
    {
        const std::size_t key = StockPipelineKey(
            PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, activeColorTargetFormats_,
                             colorTargetCount, sampleCount, depthStencilFormat, renderState),
            vertexLayout);
        const auto it = envMapPipelines_.find(key);
        if (it != envMapPipelines_.end())
            return it->second;

        SdlGpuStockVertexStateEXT vertexState;
        BuildSdlGpuStockVertexStateEXT(vertexLayout, vertexState);

        std::array<SDL_GPUColorTargetDescription, 4> colorTargets{};
        FillColorTargetDescriptions(colorTargets, colorTargetCount,
                                    activeColorTargetFormats_, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = envMapVertexShader_;
        pipelineInfo.fragment_shader = envMapFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = vertexState.buffers.data();
        pipelineInfo.vertex_input_state.num_vertex_buffers = vertexState.bufferCount;
        pipelineInfo.vertex_input_state.vertex_attributes = vertexState.attributes.data();
        pipelineInfo.vertex_input_state.num_vertex_attributes = vertexState.attributeCount;
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(
            pipelineInfo.rasterizer_state, renderState,
            pipelineInfo.primitive_type, depthStencilFormat);
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
            topology, depthTest, depthWrite, depthFunc, activeColorTargetFormats_, colorTargetCount,
            sampleCount,
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
        FillColorTargetDescriptions(colorTargets, colorTargetCount,
                                    activeColorTargetFormats_, renderState);

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
            pipelineInfo.primitive_type, depthStencilFormat);
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

        // plans/plan_gltf.md GLTF-462/GLTF-465: the stride-60 and stride-80 twins. Same uniform/sampler
        // shape as the two above -- only the vertex input set differs -- so they are created the same
        // way and cost nothing until a colour-carrying draw actually selects one.
        SDL_GPUShaderCreateInfo colorVsInfo = vsInfo;
        colorVsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kPbr3dColorVertSpv);
        colorVsInfo.code_size = Shaders::kPbr3dColorVertSpv_size;
        resources.CreateShader(
            ConstructionShader::PbrColorVertex,
            SdlGpuFailurePointEXT::PbrColorVertexShaderCreation, colorVsInfo,
            "CNA SDL_GPU: failed to create pbr3d (vertex colour) vertex shader: ");

        SDL_GPUShaderCreateInfo skinnedColorVsInfo = skinnedVsInfo;
        skinnedColorVsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kPbrSkinned3dColorVertSpv);
        skinnedColorVsInfo.code_size = Shaders::kPbrSkinned3dColorVertSpv_size;
        resources.CreateShader(
            ConstructionShader::PbrSkinnedColorVertex,
            SdlGpuFailurePointEXT::PbrSkinnedColorVertexShaderCreation, skinnedColorVsInfo,
            "CNA SDL_GPU: failed to create pbr_skinned3d (vertex colour) vertex shader: ");

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
        // plans/plan_gltf.md GLTF-465: the two colour-carrying caches and their shaders, released the same
        // way -- a pipeline cache nobody frees is exactly the leak this function exists to prevent.
        for (auto& [key, pipeline] : pbrColorPipelines_)
            ReleaseGraphicsPipeline(pipeline);
        pbrColorPipelines_.clear();
        for (auto& [key, pipeline] : pbrSkinnedColorPipelines_)
            ReleaseGraphicsPipeline(pipeline);
        pbrSkinnedColorPipelines_.clear();
        ReleaseShader(pbrColorVertexShader_);
        ReleaseShader(pbrSkinnedColorVertexShader_);
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
        bool skinned, bool colored, SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
        SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount, const RenderStateSnapshot& renderState)
    {
        const std::size_t key = PipelineCacheKey(
            topology, depthTest, depthWrite, depthFunc, activeColorTargetFormats_, colorTargetCount,
            sampleCount,
            depthStencilFormat, renderState);
        auto& cache = skinned ? (colored ? pbrSkinnedColorPipelines_ : pbrSkinnedPipelines_)
                              : (colored ? pbrColorPipelines_ : pbrPipelines_);
        const auto it = cache.find(key);
        if (it != cache.end())
            return it->second;

        SDL_GPUVertexBufferDescription vbDesc{};
        vbDesc.slot = 0;
        // plans/plan_gltf.md GLTF-462/GLTF-463: strides 60 and 80 are the same records with TEXCOORD_1 and
        // a packed COLOR_0 appended. This renderer's PBR shaders sample one UV set, so the second one
        // stays unbound; the colour does not, because glTF 3.9.2 makes it a term in base colour.
        vbDesc.pitch = skinned ? (colored ? 80 : 68) : (colored ? 60 : 48);
        vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        // Stride 48 (VertexPositionNormalTangentTexture): pos(12) + normal(12) + tangent(16) +
        // uv(8). Stride 68 (VertexPositionNormalTangentTextureSkinned) appends blendWeight(16) +
        // blendIndices(4) after the same 48-byte prefix, matching EasyGLRenderer::
        // ApplyLayout's stride==48/68 cases exactly.
        SDL_GPUVertexAttribute attrs[7]{};
        attrs[0].location = 0; attrs[0].buffer_slot = 0; attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[0].offset = 0;
        attrs[1].location = 1; attrs[1].buffer_slot = 0; attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attrs[1].offset = 12;
        attrs[2].location = 2; attrs[2].buffer_slot = 0; attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4; attrs[2].offset = 24;
        attrs[3].location = 3; attrs[3].buffer_slot = 0; attrs[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; attrs[3].offset = 40;
        attrs[4].location = 4; attrs[4].buffer_slot = 0; attrs[4].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4; attrs[4].offset = 48;
        attrs[5].location = 5; attrs[5].buffer_slot = 0; attrs[5].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4; attrs[5].offset = 64;
        // The colour lands at location 4 on the rigid record (nothing else uses it there) and at 6 on
        // the skinned one, matching the two shader variants' own declarations.
        if (colored)
        {
            const int slot = skinned ? 6 : 4;
            attrs[slot].location = static_cast<Uint32>(slot);
            attrs[slot].buffer_slot = 0;
            attrs[slot].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
            attrs[slot].offset = skinned ? 76 : 56;
        }

        std::array<SDL_GPUColorTargetDescription, 4> colorTargets{};
        FillColorTargetDescriptions(colorTargets, colorTargetCount,
                                    activeColorTargetFormats_, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = skinned
            ? (colored ? pbrSkinnedColorVertexShader_ : pbrSkinnedVertexShader_)
            : (colored ? pbrColorVertexShader_ : pbrVertexShader_);
        pipelineInfo.fragment_shader = pbrFragmentShader_;  // shared unchanged by both variants
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes =
            skinned ? (colored ? 7u : 6u) : (colored ? 5u : 4u);
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(
            pipelineInfo.rasterizer_state, renderState,
            pipelineInfo.primitive_type, depthStencilFormat);
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
                                                    PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params,
                                                    bool hasVertexColor,
                                                    const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& vertexLayout)
    {
        // plans/plan_gltf.md GLTF-474: the replay binds neutral white when no base-colour map is
        // bound, so the 1x1 texture has to exist by then. Creating it here rather than in the
        // replay keeps every allocation on the queueing side, where a failure still has a
        // caller to report to.
        EnsureDefaultPbrTextures();
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferRenderer&>(vb);
        AlphaTestDrawCommand command;
        command.hasVertexColor = hasVertexColor;
        command.vertexLayout = vertexLayout;
        const int vertexStart = params.vertexStart;
        StockDrawVertexStreamsEXT streams;
        CollectStockVertexStreamsEXT(sdlGpuVb, &params, streams);
        CaptureStockVertexStreamsEXT(vertexLayout, streams, vertexStart,
                                     command.vertexData, command.extraVertexStreams);
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        command.renderState = CaptureRenderState();
        const Matrix wvp = ApplyXnaPixelCenter(world * view * projection);
        FillAlphaTestUniforms(command.uniforms, wvp, params);
        FillFogUniforms(command.fogUniforms, params);  // REMED-GFX-009
        command.texture = ResolveSampledTextureEXT(params.texture0, "AlphaTestEffect.Texture");
        command.textureFilter = samplerSlots_[0].filter;
        command.addressU = samplerSlots_[0].addressU;
        command.addressV = samplerSlots_[0].addressV;
        command.maxAnisotropy = samplerSlots_[0].maxAnisotropy;  // REMED-GFX-170
        command.maxMipLevel = samplerSlots_[0].maxMipLevel;
        command.lodBias = samplerSlots_[0].lodBias;
        command.addressW = samplerSlots_[0].addressW;

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
                                                      PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params,
                                                      bool hasVertexColor,
                                                      const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& vertexLayout)
    {
        // DualTextureEffect permits either texture property to be null. The shader still samples
        // both slots, so an absent one is the multiplicative identity rather than a skipped draw.
        // Create the same neutral-white texture the other stock families already use before the
        // deferred command is queued.
        EnsureDefaultPbrTextures();
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferRenderer&>(vb);
        DualTextureDrawCommand command;
        command.hasVertexColor = hasVertexColor;
        command.vertexLayout = vertexLayout;
        const int vertexStart = params.vertexStart;
        StockDrawVertexStreamsEXT streams;
        CollectStockVertexStreamsEXT(sdlGpuVb, &params, streams);
        CaptureStockVertexStreamsEXT(vertexLayout, streams, vertexStart,
                                     command.vertexData, command.extraVertexStreams);
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        command.renderState = CaptureRenderState();
        const Matrix wvp = ApplyXnaPixelCenter(world * view * projection);
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
        command.texture0MaxMipLevel = samplerSlots_[0].maxMipLevel;
        command.texture0LodBias = samplerSlots_[0].lodBias;
        command.texture0AddressW = samplerSlots_[0].addressW;
        command.texture1Filter = samplerSlots_[1].filter;
        command.texture1AddressU = samplerSlots_[1].addressU;
        command.texture1AddressV = samplerSlots_[1].addressV;
        command.texture1MaxAnisotropy = samplerSlots_[1].maxAnisotropy;  // REMED-GFX-170
        command.texture1MaxMipLevel = samplerSlots_[1].maxMipLevel;
        command.texture1LodBias = samplerSlots_[1].lodBias;
        command.texture1AddressW = samplerSlots_[1].addressW;

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
                                                PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params,
                                                const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& vertexLayout)
    {
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferRenderer&>(vb);
        // params.envMap (ITextureCubeRenderer*) may be either a plain, uploaded TextureCube
        // (SdlGpuTextureCubeRenderer, SDLGPU-51) or a RenderTargetCube sampled after being rendered
        // into (SdlGpuRenderTargetCubeRenderer, SDLGPU-36) -- unrelated concrete classes, resolved
        // through the one shared resolver every binding route uses (REMED-GFX-152).
        const SdlGpuSampledTextureEXT envMapTexture =
            ResolveSampledCubeEXT(params.envMap, "EnvironmentMapEffect.EnvironmentMap");

        EnvMapDrawCommand command;
        command.vertexLayout = vertexLayout;
        const int vertexStart = params.vertexStart;
        StockDrawVertexStreamsEXT streams;
        CollectStockVertexStreamsEXT(sdlGpuVb, &params, streams);
        CaptureStockVertexStreamsEXT(vertexLayout, streams, vertexStart,
                                     command.vertexData, command.extraVertexStreams);
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        command.renderState = CaptureRenderState();
        const Matrix wvp = ApplyXnaPixelCenter(world * view * projection);
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
        command.maxMipLevel = samplerSlots_[0].maxMipLevel;
        command.lodBias = samplerSlots_[0].lodBias;
        command.addressW = samplerSlots_[0].addressW;
        command.envMapFilter = samplerSlots_[1].filter;
        command.envMapAddressU = samplerSlots_[1].addressU;
        command.envMapAddressV = samplerSlots_[1].addressV;
        command.envMapMaxAnisotropy = samplerSlots_[1].maxAnisotropy;
        command.envMapMaxMipLevel = samplerSlots_[1].maxMipLevel;
        command.envMapLodBias = samplerSlots_[1].lodBias;
        command.envMapAddressW = samplerSlots_[1].addressW;

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

    // Rewrite any semantically compatible SkinnedEffect declaration into the renderer's canonical
    // stride-52/56 record. SDL_gpu's UBYTE4 input feeds the shader's uvec4 directly, so a legal
    // Vector4 BLENDINDICES declaration cannot share the native pipeline unchanged. Capturing and
    // converting it here keeps one shader/pipeline family while preserving XNA's semantic (rather
    // than stride) vertex binding rule.
    [[nodiscard]] static bool NormalizeSkinnedStreamEXT(
        const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& declaredElements,
        const std::vector<std::uint8_t>& source, std::size_t sourceStride,
        std::vector<std::uint8_t>& out, std::size_t& outStride)
    {
        using CNA::Internal::Graphics::FindDeclaredSemanticEXT;
        using Microsoft::Xna::Framework::Graphics::VertexElement;
        using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

        if (sourceStride == 0 || declaredElements.empty())
            return false;

        const VertexElement* position =
            FindDeclaredSemanticEXT(declaredElements, VertexElementUsage::Position, 0);
        const VertexElement* normal =
            FindDeclaredSemanticEXT(declaredElements, VertexElementUsage::Normal, 0);
        const VertexElement* uv =
            FindDeclaredSemanticEXT(declaredElements, VertexElementUsage::TextureCoordinate, 0);
        const VertexElement* weights =
            FindDeclaredSemanticEXT(declaredElements, VertexElementUsage::BlendWeight, 0);
        const VertexElement* indices =
            FindDeclaredSemanticEXT(declaredElements, VertexElementUsage::BlendIndices, 0);
        const VertexElement* color =
            FindDeclaredSemanticEXT(declaredElements, VertexElementUsage::Color, 0);
        if (position == nullptr || normal == nullptr || uv == nullptr || weights == nullptr ||
            indices == nullptr)
            return false;
        if (position->getVertexElementFormatProperty() != VertexElementFormat::Vector3 ||
            normal->getVertexElementFormatProperty() != VertexElementFormat::Vector3 ||
            uv->getVertexElementFormatProperty() != VertexElementFormat::Vector2 ||
            weights->getVertexElementFormatProperty() != VertexElementFormat::Vector4)
            return false;

        const VertexElementFormat indexFormat = indices->getVertexElementFormatProperty();
        if (indexFormat != VertexElementFormat::Byte4 && indexFormat != VertexElementFormat::Vector4)
            return false;
        const bool packedColor = color != nullptr &&
            color->getVertexElementFormatProperty() == VertexElementFormat::Color;
        const bool vectorColor = color != nullptr &&
            color->getVertexElementFormatProperty() == VertexElementFormat::Vector4;
        if (color != nullptr && !packedColor && !vectorColor)
            return false;

        const auto fits = [sourceStride](const VertexElement* element, std::size_t bytes) {
            const int offset = element->getOffsetProperty();
            return offset >= 0 && static_cast<std::size_t>(offset) <= sourceStride &&
                   bytes <= sourceStride - static_cast<std::size_t>(offset);
        };
        if (!fits(position, 12) || !fits(normal, 12) || !fits(uv, 8) || !fits(weights, 16) ||
            !fits(indices, indexFormat == VertexElementFormat::Byte4 ? 4 : 16) ||
            (color != nullptr && !fits(color, packedColor ? 4 : 16)))
            return false;

        outStride = color != nullptr ? 56u : 52u;
        const std::size_t vertexCount = source.size() / sourceStride;
        out.assign(vertexCount * outStride, std::uint8_t{0});
        for (std::size_t vertex = 0; vertex < vertexCount; ++vertex)
        {
            const std::size_t src = vertex * sourceStride;
            std::uint8_t* dst = out.data() + vertex * outStride;
            std::memcpy(dst, source.data() + src + position->getOffsetProperty(), 12);
            std::memcpy(dst + 12, source.data() + src + normal->getOffsetProperty(), 12);
            std::memcpy(dst + 24, source.data() + src + uv->getOffsetProperty(), 8);
            std::memcpy(dst + 32, source.data() + src + weights->getOffsetProperty(), 16);
            if (indexFormat == VertexElementFormat::Byte4)
            {
                std::memcpy(dst + 48, source.data() + src + indices->getOffsetProperty(), 4);
            }
            else
            {
                float declared[4]{};
                std::memcpy(declared, source.data() + src + indices->getOffsetProperty(),
                            sizeof(declared));
                for (int lane = 0; lane < 4; ++lane)
                {
                    const float clamped = std::clamp(declared[lane], 0.0f, 255.0f);
                    dst[48 + lane] = static_cast<std::uint8_t>(clamped + 0.5f);
                }
            }
            if (packedColor)
            {
                std::memcpy(dst + 52, source.data() + src + color->getOffsetProperty(), 4);
            }
            else if (vectorColor)
            {
                float declared[4]{};
                std::memcpy(declared, source.data() + src + color->getOffsetProperty(),
                            sizeof(declared));
                for (int lane = 0; lane < 4; ++lane)
                {
                    const float normalized = std::clamp(declared[lane], 0.0f, 1.0f);
                    dst[52 + lane] = static_cast<std::uint8_t>(normalized * 255.0f + 0.5f);
                }
            }
        }
        return true;
    }

    void SdlGpuRenderer::QueueSkinnedDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                                 const Matrix& world, const Matrix& view, const Matrix& projection,
                                                 PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        // plans/plan_gltf.md GLTF-474: the replay binds neutral white when no base-colour map is
        // bound, so the 1x1 texture has to exist by then. Creating it here rather than in the
        // replay keeps every allocation on the queueing side, where a failure still has a
        // caller to report to.
        EnsureDefaultPbrTextures();
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferRenderer&>(vb);
        const std::size_t stride = sdlGpuVb.Stride();

        SkinnedDrawCommand command;
        std::vector<std::uint8_t> normalized;
        std::size_t normalizedStride = 0;
        const std::vector<std::uint8_t>* stream = &sdlGpuVb.ShadowData();
        std::size_t sourceStride = stride;
        const auto& declaredElements = sdlGpuVb.Declaration().GetElements();
        if (!declaredElements.empty())
        {
            if (!NormalizeSkinnedStreamEXT(declaredElements, sdlGpuVb.ShadowData(), stride,
                                           normalized, normalizedStride))
                throw std::invalid_argument(
                    "CNA SDL_GPU: SkinnedEffect needs POSITION0 (Vector3), NORMAL0 (Vector3), "
                    "TEXCOORD0 (Vector2), BLENDWEIGHT0 (Vector4), and BLENDINDICES0 "
                    "(Byte4 or Vector4), with an optional COLOR0 (Color or Vector4)");
            stream = &normalized;
            sourceStride = normalizedStride;
        }
        else if (stride != 52 && stride != 56)
        {
            throw std::invalid_argument(
                "CNA SDL_GPU: SkinnedEffect received no VertexDeclaration and its stride is "
                "neither the canonical 52 nor 56 bytes");
        }

        command.hasVertexColor = (sourceStride == 56);
        const int vertexStart = params.vertexStart;
        const auto& shadow = *stream;
        const std::size_t byteOffset = static_cast<std::size_t>(vertexStart) * sourceStride;
        if (byteOffset <= shadow.size())
            command.vertexData.assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset), shadow.end());
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        command.renderState = CaptureRenderState();
        const Matrix wvp = ApplyXnaPixelCenter(world * view * projection);
        FillExtUniforms(command.uniforms, wvp, params);
        FillFogUniforms(command.fogUniforms, params);  // REMED-GFX-009
        FillSkinnedBoneUniforms(command.boneUniforms, params);
        FillSkinnedLightUniforms(command.lightUniforms, params);
        command.texture = ResolveSampledTextureEXT(params.texture0, "SkinnedEffect.Texture");
        command.textureFilter = samplerSlots_[0].filter;
        command.addressU = samplerSlots_[0].addressU;
        command.addressV = samplerSlots_[0].addressV;
        command.maxAnisotropy = samplerSlots_[0].maxAnisotropy;  // REMED-GFX-170
        command.maxMipLevel = samplerSlots_[0].maxMipLevel;
        command.lodBias = samplerSlots_[0].lodBias;
        command.addressW = samplerSlots_[0].addressW;

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
        const bool skinned = params.skinned;
        // plans/plan_gltf.md GLTF-462/GLTF-463/GLTF-465: strides 60 and 80 are the same two records with
        // TEXCOORD_1 and a packed COLOR_0 appended, and glTF 3.9.2 makes that colour a multiplier on
        // base colour. They select the colour-carrying shader variants; the second UV set stays
        // unbound, which is this renderer's own separate capability gap.
        const bool colored = skinned ? (stride == 80u) : (stride == 60u);
        const bool acceptable = skinned ? (stride == 68u || stride == 80u)
                                        : (stride == 48u || stride == 60u);
        if (!acceptable)
            throw std::invalid_argument(skinned
                ? "CNA SDL_GPU: pbr_skinned3d requires a stride-68 or stride-80 "
                  "(VertexPositionNormalTangentTextureSkinned, optionally with COLOR_0) vertex buffer"
                : "CNA SDL_GPU: pbr3d requires a stride-48 or stride-60 "
                  "(VertexPositionNormalTangentTexture, optionally with COLOR_0) vertex buffer");

        EnsureDefaultPbrTextures();

        PbrDrawCommand command;
        command.skinned = skinned;
        command.colored = colored;
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
        const Matrix wvp = ApplyXnaPixelCenter(world * view * projection);
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
        command.maxMipLevel = samplerSlots_[0].maxMipLevel;
        command.lodBias = samplerSlots_[0].lodBias;
        command.addressW = samplerSlots_[0].addressW;
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
        const CompiledEffectBinding& binding,
        SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        const RenderStateSnapshot& renderState,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
        SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount)
    {
        std::size_t key = PipelineCacheKey(topology, depthTest, depthWrite, depthFunc,
                                           activeColorTargetFormats_,
                                           colorTargetCount, sampleCount, depthStencilFormat,
                                           renderState);
        // The base key above is exactly what every stock pipeline hashes; a compiled effect's
        // shader pair and vertex layout vary per pass/effect (unlike a stock family's fixed shader
        // fields, or SpriteBatch's own fixed SpriteVertex layout), so those are folded in here
        // instead of being implicit in which cache this key is looked up in. One cache and one key
        // scheme serves both the ordinary-draw and SpriteBatch routes.
        key = HashCombine(key, std::hash<const void*>{}(binding.vertexShader));
        key = HashCombine(key, std::hash<const void*>{}(binding.pixelShader));
        key = HashCombine(key, binding.vertexBuffers.size());
        for (const SDL_GPUVertexBufferDescription& buffer : binding.vertexBuffers)
        {
            key = HashCombine(key, static_cast<std::size_t>(buffer.slot));
            key = HashCombine(key, static_cast<std::size_t>(buffer.pitch));
            key = HashCombine(key, static_cast<std::size_t>(buffer.input_rate));
        }
        key = HashCombine(key, binding.vertexAttributes.size());
        for (const SDL_GPUVertexAttribute& attribute : binding.vertexAttributes)
        {
            key = HashCombine(key, static_cast<std::size_t>(attribute.location));
            key = HashCombine(key, static_cast<std::size_t>(attribute.buffer_slot));
            key = HashCombine(key, static_cast<std::size_t>(attribute.format));
            key = HashCombine(key, static_cast<std::size_t>(attribute.offset));
        }

        const auto it = compiledEffectPipelines_.find(key);
        if (it != compiledEffectPipelines_.end())
            return it->second;

        std::array<SDL_GPUColorTargetDescription, 4> colorTargets{};
        FillColorTargetDescriptions(colorTargets, colorTargetCount,
                                    activeColorTargetFormats_, renderState);

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = binding.vertexShader;
        pipelineInfo.fragment_shader = binding.pixelShader;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = binding.vertexBuffers.data();
        pipelineInfo.vertex_input_state.num_vertex_buffers =
            static_cast<Uint32>(binding.vertexBuffers.size());
        pipelineInfo.vertex_input_state.vertex_attributes = binding.vertexAttributes.data();
        pipelineInfo.vertex_input_state.num_vertex_attributes =
            static_cast<Uint32>(binding.vertexAttributes.size());
        pipelineInfo.primitive_type = topology;
        FillRasterizerState(
            pipelineInfo.rasterizer_state, renderState,
            pipelineInfo.primitive_type, depthStencilFormat);
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
        const std::vector<SdlGpuCompiledEffectVertexStreamEXT>& streams,
        const SdlGpuSampledTextureEXT* spriteTextureOverride)
    {
        CompiledEffectBinding binding;
        SdlGpuCompiledEffectVertexLayoutEXT vertexLayout =
            effect.LinkAndGetShadersMultiEXT(streams, binding.vertexShader, binding.pixelShader);
        binding.vertexAttributes = std::move(vertexLayout.attributes);
        binding.vertexBuffers = std::move(vertexLayout.buffers);
        binding.vertexStreamSourceIndices = std::move(vertexLayout.sourceIndices);

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
            bool samplerAssigned = false;
            effect.GetBoundSamplerEXT(slot, /*vertexStage=*/false, boundTexture, samplerState,
                                      &samplerAssigned);
            if (boundTexture == nullptr)
            {
                if (slot == 0 && spriteTextureOverride != nullptr && *spriteTextureOverride)
                {
                    samplerBinding.texture = *spriteTextureOverride;
                    if (slot < samplerSlots_.size())
                    {
                        const SamplerSlotState& deviceSlot = samplerSlots_[slot];
                        samplerBinding.filter = deviceSlot.filter;
                        samplerBinding.addressU = deviceSlot.addressU;
                        samplerBinding.addressV = deviceSlot.addressV;
                        samplerBinding.maxAnisotropy = deviceSlot.maxAnisotropy;
                        samplerBinding.maxMipLevel = deviceSlot.maxMipLevel;
                        samplerBinding.lodBias = deviceSlot.lodBias;
                        samplerBinding.addressW = deviceSlot.addressW;
                    }
                    continue;
                }
                const char* name = reflectedSampler->name != nullptr ? reflectedSampler->name
                                                                      : "<unnamed>";
                throw std::runtime_error(
                    std::string("CNA SDL_GPU: this compiled effect's pixel shader samples '") +
                    name + "', but no texture is bound to it.");
            }
            // plans/plan_fx.md FX-110: the shader's declared sampler dimension decides which resolver
            // the bound texture has to go through, and the two must agree. SDL_GPU binds a texture
            // by handle rather than by target, so a cube bound where the shader declared sampler2D
            // is a validation error at best and a wrongly-sampled image at worst -- named here
            // instead of either.
            using Microsoft::Xna::Framework::Graphics::Texture2D;
            using Microsoft::Xna::Framework::Graphics::Texture3D;
            using Microsoft::Xna::Framework::Graphics::TextureCube;
            auto* texture2D = dynamic_cast<Texture2D*>(boundTexture);
            auto* textureCube = dynamic_cast<TextureCube*>(boundTexture);
            auto* texture3D = dynamic_cast<Texture3D*>(boundTexture);
            const MOJOSHADER_samplerType boundKind =
                textureCube != nullptr ? MOJOSHADER_SAMPLER_CUBE
                : texture3D != nullptr ? MOJOSHADER_SAMPLER_VOLUME
                                       : MOJOSHADER_SAMPLER_2D;
            const auto kindName = [](MOJOSHADER_samplerType type) {
                switch (type)
                {
                    case MOJOSHADER_SAMPLER_CUBE:   return "samplerCUBE (TextureCube)";
                    case MOJOSHADER_SAMPLER_VOLUME: return "sampler3D (Texture3D)";
                    default:                        return "sampler2D (Texture2D)";
                }
            };
            if (reflectedSampler->type != boundKind)
            {
                throw System::NotSupportedException(
                    std::string("CNA SDL_GPU: this compiled effect's pixel shader declares ") +
                    kindName(reflectedSampler->type) + " at slot " + std::to_string(slot) +
                    ", but the texture bound there is a " + kindName(boundKind) +
                    ". The dimensions must match.");
            }
            samplerBinding.texture =
                texture3D != nullptr
                    ? ResolveSampledVolumeEXT(&texture3D->GetRenderer(),
                                              "CompiledEffect.VolumeSampler")
                : textureCube != nullptr
                    ? ResolveSampledCubeEXT(&textureCube->GetRenderer(),
                                            "CompiledEffect.CubeSampler")
                    : ResolveSampledTextureEXT(&texture2D->GetRenderer(),
                                               "CompiledEffect.Sampler");
            if (samplerAssigned)
            {
                // plans/plan_fx.md FX-083: the pass's own sampler_state block, LOD clamp and bias
                // included -- SDL_GPU expresses both exactly.
                samplerBinding.filter = static_cast<int>(samplerState.getFilterProperty());
                samplerBinding.addressU = static_cast<int>(samplerState.getAddressUProperty());
                samplerBinding.addressV = static_cast<int>(samplerState.getAddressVProperty());
                samplerBinding.maxAnisotropy = samplerState.getMaxAnisotropyProperty();
                samplerBinding.maxMipLevel = samplerState.getMaxMipLevelProperty();
                samplerBinding.lodBias = samplerState.getMipMapLevelOfDetailBiasProperty();
                samplerBinding.addressW = static_cast<int>(samplerState.getAddressWProperty());
            }
            else if (slot < samplerSlots_.size())
            {
                // No pass has assigned this slot, so what the game selected on the device stands --
                // GraphicsDevice.SamplerStates[slot], recorded here by ApplySamplerState /
                // ApplySamplerMipState. Filling in defaults instead would silently override it.
                const SamplerSlotState& deviceSlot = samplerSlots_[slot];
                samplerBinding.filter = deviceSlot.filter;
                samplerBinding.addressU = deviceSlot.addressU;
                samplerBinding.addressV = deviceSlot.addressV;
                samplerBinding.maxAnisotropy = deviceSlot.maxAnisotropy;
                samplerBinding.maxMipLevel = deviceSlot.maxMipLevel;
                samplerBinding.lodBias = deviceSlot.lodBias;
                samplerBinding.addressW = deviceSlot.addressW;
            }
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
                                                 const GpuDrawParams& params, int instanceCount)
    {
        auto* sdlGpuEffect = dynamic_cast<CNA::Internal::Renderers::SdlGpu::SdlGpuCompiledEffect*>(
            params.compiledEffectRuntime);
        if (sdlGpuEffect == nullptr)
        {
            throw std::runtime_error(
                "CNA SDL_GPU: the applied compiled effect was not created by this renderer.");
        }

        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferRenderer&>(vb);
        StockDrawVertexStreamsEXT streams;
        CollectStockVertexStreamsEXT(sdlGpuVb, &params, streams, /*includePerInstance=*/true);

        std::vector<SdlGpuCompiledEffectVertexStreamEXT> compiledStreams;
        compiledStreams.reserve(streams.count);
        for (std::size_t i = 0; i < streams.count; ++i)
        {
            const auto* elements = streams.declarations[i].elements;
            if (elements == nullptr || elements->empty())
            {
                throw System::NotSupportedException(
                    "CNA SDL_GPU: a compiled-effect draw needs every consumed vertex buffer's "
                    "own VertexDeclaration; this renderer does not infer one from stride for "
                    "this route.");
            }
            compiledStreams.push_back(SdlGpuCompiledEffectVertexStreamEXT{
                elements,
                static_cast<Uint32>(streams.declarations[i].stride),
                streams.declarations[i].instanceFrequency > 0
                    ? SDL_GPU_VERTEXINPUTRATE_INSTANCE : SDL_GPU_VERTEXINPUTRATE_VERTEX});
        }

        CompiledEffectDrawCommand command;
        command.binding = BuildCompiledEffectBindingEXT(*sdlGpuEffect, compiledStreams);
        command.instanceCount = static_cast<Uint32>(std::max(1, instanceCount));
        command.vertexStride = command.binding.vertexBuffers.empty()
            ? 0u : command.binding.vertexBuffers.front().pitch;

        for (std::size_t nativeSlot = 0;
             nativeSlot < command.binding.vertexStreamSourceIndices.size(); ++nativeSlot)
        {
            const std::size_t sourceIndex =
                command.binding.vertexStreamSourceIndices[nativeSlot];
            if (sourceIndex >= streams.count || streams.sources[sourceIndex].buffer == nullptr)
                throw std::runtime_error(
                    "CNA SDL_GPU: a compiled-effect draw lost a consumed vertex stream");

            const StockVertexStreamSourceEXT& source = streams.sources[sourceIndex];
            const auto& shadow = source.buffer->ShadowData();
            const std::size_t stride = static_cast<std::size_t>(std::max(1, source.stride));
            std::vector<std::uint8_t>* destination = &command.vertexData;
            if (nativeSlot > 0)
            {
                command.extraVertexStreams.emplace_back();
                destination = &command.extraVertexStreams.back().data;
            }

            if (source.instanceFrequency > 0)
            {
                const int frequency = std::max(1, source.instanceFrequency);
                const int lastRecord = source.vertexOffset +
                    (static_cast<int>(command.instanceCount) - 1) / frequency;
                if (source.vertexOffset < 0 || lastRecord >= source.vertexCount)
                {
                    throw System::ArgumentOutOfRangeException(
                        "instanceCount", std::to_string(instanceCount),
                        "CNA SDL_GPU: a compiled-effect per-instance stream does not contain "
                        "the requested record.");
                }
                destination->resize(static_cast<std::size_t>(command.instanceCount) * stride);
                for (Uint32 instance = 0; instance < command.instanceCount; ++instance)
                {
                    const std::size_t record = static_cast<std::size_t>(source.vertexOffset) +
                        static_cast<std::size_t>(instance / static_cast<Uint32>(frequency));
                    std::memcpy(destination->data() + static_cast<std::size_t>(instance) * stride,
                                shadow.data() + record * stride, stride);
                }
                continue;
            }

            const int firstVertex = source.vertexOffset + (ib == nullptr ? params.vertexStart : 0);
            if (firstVertex < 0 || firstVertex > source.vertexCount ||
                (ib != nullptr && params.baseVertex > source.vertexCount - firstVertex))
            {
                throw System::ArgumentOutOfRangeException(
                    "baseVertex", std::to_string(params.baseVertex),
                    "CNA SDL_GPU: a compiled-effect per-vertex stream offset leaves its buffer.");
            }
            const std::size_t byteOffset = static_cast<std::size_t>(firstVertex) * stride;
            if (byteOffset <= shadow.size())
                destination->assign(
                    shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset), shadow.end());
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
            command.vertexCount = static_cast<Uint32>(PrimitiveVertexCount(primitive, primitiveCount));
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
        const CompiledEffectBinding& binding,
        SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
        const RenderStateSnapshot& renderState,
        SDL_GPUTextureFormat colorFormat, SDL_GPUSampleCount sampleCount,
        SDL_GPUTextureFormat depthStencilFormat, int colorTargetCount,
        SDL_GPUGraphicsPipeline*& boundPipeline)
    {
        SDL_GPUGraphicsPipeline* pipeline = GetOrCreatePipelineCompiledEffect(
            binding, topology, depthTest, depthWrite, depthFunc, renderState,
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
                    samplerBinding.maxAnisotropy, "CompiledEffect",
                    samplerBinding.maxMipLevel, samplerBinding.lodBias, samplerBinding.addressW);
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
        BindCompiledEffectForDrawEXT(pass, cmd, command.binding,
                                     command.topology, command.depthTest, command.depthWrite,
                                     command.depthFunc, command.renderState, colorFormat,
                                     sampleCount, depthStencilFormat, colorTargetCount,
                                     boundPipeline);

        std::vector<SDL_GPUBufferBinding> vertexBindings(
            1 + command.extraVertexStreams.size());
        vertexBindings[0].buffer = command.uploadedVertexBuffer;
        for (std::size_t i = 0; i < command.extraVertexStreams.size(); ++i)
            vertexBindings[i + 1].buffer = command.extraVertexStreams[i].uploadedBuffer;
        SDL_BindGPUVertexBuffers(
            pass, 0, vertexBindings.data(), static_cast<Uint32>(vertexBindings.size()));

        if (command.indexed && command.uploadedIndexBuffer != nullptr)
        {
            SDL_GPUBufferBinding ibBinding{};
            ibBinding.buffer = command.uploadedIndexBuffer;
            SDL_BindGPUIndexBuffer(pass, &ibBinding,
                                   command.index32 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT);
            SDL_DrawGPUIndexedPrimitives(
                pass, command.indexCount, command.instanceCount,
                command.firstIndex, command.vertexOffset, 0);
        }
        else
        {
            SDL_DrawGPUPrimitives(pass, command.vertexCount, command.instanceCount, 0, 0);
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
            command.hasVertexColor, command.vertexLayout,
            command.topology, command.depthTest, command.depthWrite,
            command.depthFunc, colorFormat, sampleCount, depthStencilFormat, colorTargetCount, command.renderState);
        if (pipeline != boundPipeline) { SDL_BindGPUGraphicsPipeline(pass, pipeline); boundPipeline = pipeline; }
        SDL_SetGPUStencilReference(pass, static_cast<Uint8>(command.renderState.stencilReference));
        SDL_PushGPUVertexUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));
        SDL_PushGPUVertexUniformData(cmd, 1, command.fogUniforms.data(), sizeof(command.fogUniforms));  // REMED-GFX-009
        SDL_PushGPUFragmentUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));

        BindStockVertexBuffersEXT(pass, command.uploadedVertexBuffer,
                                  command.extraVertexStreams,
                                  command.uploadedNeutralVertexBuffer, command.vertexLayout);

        SDL_GPUTextureSamplerBinding samplerBinding{};
        // plans/plan_gltf.md GLTF-474: a stock effect's base-colour map is optional -- XNA lets
        // BasicEffect/SkinnedEffect/AlphaTestEffect run untextured, and glTF's own default
        // material has no baseColorTexture at all. Binding neutral white makes `tex * colour`
        // collapse to the colour, which is what EasyGL and Vulkan already do; without it this
        // renderer had to refuse the draw upstream instead.
        samplerBinding.texture = command.texture ? command.texture.texture
                                                 : defaultWhiteTexture_->Texture();
        samplerBinding.sampler = GetOrCreateSampler(command.textureFilter, command.addressU,
                                                   command.addressV, command.maxAnisotropy,
                                                   "AlphaTest3D", command.maxMipLevel,
                                                   command.lodBias, command.addressW);
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
            command.hasVertexColor, command.vertexLayout,
            command.topology, command.depthTest,
            command.depthWrite, command.depthFunc, colorFormat, sampleCount,
            depthStencilFormat, colorTargetCount, command.renderState);
        if (pipeline != boundPipeline) { SDL_BindGPUGraphicsPipeline(pass, pipeline); boundPipeline = pipeline; }
        SDL_SetGPUStencilReference(pass, static_cast<Uint8>(command.renderState.stencilReference));
        SDL_PushGPUVertexUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));
        SDL_PushGPUVertexUniformData(cmd, 1, command.fogUniforms.data(), sizeof(command.fogUniforms));  // REMED-GFX-009

        BindStockVertexBuffersEXT(pass, command.uploadedVertexBuffer,
                                  command.extraVertexStreams,
                                  command.uploadedNeutralVertexBuffer, command.vertexLayout);

        // SDLGPU-21: texture0/texture1 are independent GraphicsDevice.SamplerStates[0]/[1]
        // slots in real XNA -- each gets its own sampler object, not a shared one.
        SDL_GPUTextureSamplerBinding samplerBindings[2]{};
        samplerBindings[0].texture = command.texture0 ? command.texture0.texture
                                                      : defaultWhiteTexture_->Texture();
        samplerBindings[0].sampler = GetOrCreateSampler(command.texture0Filter, command.texture0AddressU,
                                                      command.texture0AddressV,
                                                      command.texture0MaxAnisotropy,
                                                      "DualTexture3D/slot0",
                                                      command.texture0MaxMipLevel,
                                                      command.texture0LodBias,
                                                      command.texture0AddressW);
        samplerBindings[1].texture = command.texture1 ? command.texture1.texture
                                                      : defaultWhiteTexture_->Texture();
        samplerBindings[1].sampler = GetOrCreateSampler(command.texture1Filter, command.texture1AddressU,
                                                      command.texture1AddressV,
                                                      command.texture1MaxAnisotropy,
                                                      "DualTexture3D/slot1",
                                                      command.texture1MaxMipLevel,
                                                      command.texture1LodBias,
                                                      command.texture1AddressW);
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
            command.vertexLayout,
            command.topology, command.depthTest, command.depthWrite, command.depthFunc,
            colorFormat, sampleCount, depthStencilFormat, colorTargetCount, command.renderState);
        if (pipeline != boundPipeline) { SDL_BindGPUGraphicsPipeline(pass, pipeline); boundPipeline = pipeline; }
        SDL_SetGPUStencilReference(pass, static_cast<Uint8>(command.renderState.stencilReference));
        SDL_PushGPUVertexUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));
        SDL_PushGPUVertexUniformData(cmd, 1, command.envMapUniforms.data(), sizeof(command.envMapUniforms));
        SDL_PushGPUVertexUniformData(cmd, 2, command.fogUniforms.data(), sizeof(command.fogUniforms));  // REMED-GFX-009
        SDL_PushGPUFragmentUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));
        SDL_PushGPUFragmentUniformData(cmd, 1, command.envMapUniforms.data(), sizeof(command.envMapUniforms));

        BindStockVertexBuffersEXT(pass, command.uploadedVertexBuffer,
                                  command.extraVertexStreams,
                                  command.uploadedNeutralVertexBuffer, command.vertexLayout);

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
                                                      "EnvironmentMap3D", command.maxMipLevel,
                                                      command.lodBias, command.addressW);
        samplerBindings[1].texture = command.envMapTexture.texture;
        samplerBindings[1].sampler = GetOrCreateSampler(command.envMapFilter, command.envMapAddressU,
                                                       command.envMapAddressV,
                                                       command.envMapMaxAnisotropy,
                                                       "EnvironmentMap3D/cube",
                                                       command.envMapMaxMipLevel,
                                                       command.envMapLodBias,
                                                       command.envMapAddressW);
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
        // plans/plan_gltf.md GLTF-474: a stock effect's base-colour map is optional -- XNA lets
        // BasicEffect/SkinnedEffect/AlphaTestEffect run untextured, and glTF's own default
        // material has no baseColorTexture at all. Binding neutral white makes `tex * colour`
        // collapse to the colour, which is what EasyGL and Vulkan already do; without it this
        // renderer had to refuse the draw upstream instead.
        samplerBinding.texture = command.texture ? command.texture.texture
                                                 : defaultWhiteTexture_->Texture();
        samplerBinding.sampler = GetOrCreateSampler(command.textureFilter, command.addressU,
                                                   command.addressV, command.maxAnisotropy,
                                                   "Skinned3D", command.maxMipLevel,
                                                   command.lodBias, command.addressW);
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
            command.skinned, command.colored, command.topology, command.depthTest, command.depthWrite,
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
                                                    "Pbr3D", command.maxMipLevel,
                                                    command.lodBias, command.addressW);
        // plans/plan_gltf.md GLTF-465: slot 0 was the one PBR map with no fallback, so a material
        // with only a baseColorFactor -- glTF's own default material, and what both COLOR_0
        // corpus fixtures author -- had to be refused upstream instead of multiplying the
        // factor by white. Same neutral-white contract as slots 1..6, and as every other
        // renderer's PBR base-colour bind.
        samplerBindings[0].texture = command.texture ? command.texture.texture
                                                    : defaultWhiteTexture_->Texture();
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
            "Pbr3D.Specular", command.specularSampler.maxMipLevel,
            command.specularSampler.lodBias, command.specularSampler.addressW);
        SDL_GPUSampler* specularColorSampler = GetOrCreateSampler(
            command.specularColorSampler.filter, command.specularColorSampler.addressU,
            command.specularColorSampler.addressV, command.specularColorSampler.maxAnisotropy,
            "Pbr3D.SpecularColor", command.specularColorSampler.maxMipLevel,
            command.specularColorSampler.lodBias, command.specularColorSampler.addressW);
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

    void SdlGpuRenderer::BindStockVertexBuffersEXT(
        SDL_GPURenderPass* pass, SDL_GPUBuffer* vertexBuffer,
        const std::vector<CapturedStockVertexStreamEXT>& extraVertexStreams,
        SDL_GPUBuffer* neutralVertexBuffer,
        const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& layout)
    {
        std::array<SDL_GPUBufferBinding,
                   CNA::Internal::Graphics::kMaxStockVertexStreamsEXT + 1> bindings{};
        bindings[0].buffer = vertexBuffer;
        Uint32 count = 1;
        for (const CapturedStockVertexStreamEXT& stream : extraVertexStreams)
        {
            if (count >= layout.streamCount || stream.uploadedBuffer == nullptr)
                break;
            bindings[count++].buffer = stream.uploadedBuffer;
        }
        if (count != layout.streamCount)
        {
            throw std::runtime_error(
                "CNA SDL_GPU: a stock draw is missing one of its captured vertex streams");
        }
        if (layout.usesNeutralRecord)
        {
            if (neutralVertexBuffer == nullptr)
                throw std::runtime_error(
                    "CNA SDL_GPU: a stock draw needs the neutral vertex record, but it was not uploaded");
            bindings[count++].buffer = neutralVertexBuffer;
        }
        SDL_BindGPUVertexBuffers(pass, 0, bindings.data(), count);
    }

    void SdlGpuRenderer::IssueInstancedDraw(
        SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
        const InstancedDrawCommand& command, SDL_GPUTextureFormat colorFormat,
        SDL_GPUSampleCount sampleCount, SDL_GPUTextureFormat depthStencilFormat,
        int colorTargetCount, SDL_GPUGraphicsPipeline*& boundPipeline)
    {
        SDL_GPUGraphicsPipeline* pipeline = GetOrCreatePipelineInstanced3D(
            command.vertexLayout, command.topology, command.depthTest, command.depthWrite,
            command.depthFunc, colorFormat, sampleCount, depthStencilFormat,
            colorTargetCount, command.renderState);
        if (pipeline != boundPipeline)
        {
            SDL_BindGPUGraphicsPipeline(pass, pipeline);
            boundPipeline = pipeline;
        }
        SDL_SetGPUStencilReference(
            pass, static_cast<Uint8>(command.renderState.stencilReference));
        SDL_PushGPUVertexUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));
        SDL_PushGPUVertexUniformData(
            cmd, 1, command.fogUniforms.data(), sizeof(command.fogUniforms));
        BindStockVertexBuffersEXT(pass, command.uploadedVertexBuffer,
                                  command.extraVertexStreams,
                                  command.uploadedNeutralVertexBuffer,
                                  command.vertexLayout);

        SDL_GPUBufferBinding indexBinding{};
        indexBinding.buffer = command.uploadedIndexBuffer;
        SDL_BindGPUIndexBuffer(
            pass, &indexBinding,
            command.index32 ? SDL_GPU_INDEXELEMENTSIZE_32BIT
                            : SDL_GPU_INDEXELEMENTSIZE_16BIT);
        SDL_DrawGPUIndexedPrimitives(
            pass, command.indexCount, command.instanceCount, command.firstIndex,
            command.vertexOffset, 0);
    }

    void SdlGpuRenderer::UploadSceneDrawData(SDL_GPUCommandBuffer* cmd)
    {
        if (coloredDrawCommands_.empty() && texturedDrawCommands_.empty() && litTexturedDrawCommands_.empty() &&
            alphaTestDrawCommands_.empty() && dualTextureDrawCommands_.empty() && envMapDrawCommands_.empty() &&
            instancedDrawCommands_.empty() && skinnedDrawCommands_.empty() && pbrDrawCommands_.empty()
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

        const auto* neutralBegin = reinterpret_cast<const std::uint8_t*>(
            CNA::Internal::Graphics::kNeutralVertexRecordEXT.data());
        const std::vector<std::uint8_t> neutralData(
            neutralBegin,
            neutralBegin + sizeof(CNA::Internal::Graphics::kNeutralVertexRecordEXT));
        const auto uploadNeutralIfNeeded = [&](auto& command)
        {
            if (command.vertexLayout.usesNeutralRecord)
                command.uploadedNeutralVertexBuffer =
                    uploadOne(neutralData, SDL_GPU_BUFFERUSAGE_VERTEX);
        };
        const auto uploadExtraStreams = [&](auto& command)
        {
            for (CapturedStockVertexStreamEXT& stream : command.extraVertexStreams)
                stream.uploadedBuffer = uploadOne(stream.data, SDL_GPU_BUFFERUSAGE_VERTEX);
        };

        for (ColoredDrawCommand& command : coloredDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty())
                continue;
            command.uploadedVertexBuffer = uploadOne(command.vertexData, SDL_GPU_BUFFERUSAGE_VERTEX);
            uploadExtraStreams(command);
            uploadNeutralIfNeeded(command);
            if (command.indexed && !command.indexData.empty())
                command.uploadedIndexBuffer = uploadOne(command.indexData, SDL_GPU_BUFFERUSAGE_INDEX);
        }
        for (TexturedDrawCommand& command : texturedDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty())
                continue;
            command.uploadedVertexBuffer = uploadOne(command.vertexData, SDL_GPU_BUFFERUSAGE_VERTEX);
            uploadExtraStreams(command);
            uploadNeutralIfNeeded(command);
            if (command.indexed && !command.indexData.empty())
                command.uploadedIndexBuffer = uploadOne(command.indexData, SDL_GPU_BUFFERUSAGE_INDEX);
        }
        for (LitTexturedDrawCommand& command : litTexturedDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty())
                continue;
            command.uploadedVertexBuffer = uploadOne(command.vertexData, SDL_GPU_BUFFERUSAGE_VERTEX);
            uploadExtraStreams(command);
            uploadNeutralIfNeeded(command);
            if (command.indexed && !command.indexData.empty())
                command.uploadedIndexBuffer = uploadOne(command.indexData, SDL_GPU_BUFFERUSAGE_INDEX);
        }
        for (AlphaTestDrawCommand& command : alphaTestDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty())
                continue;
            command.uploadedVertexBuffer = uploadOne(command.vertexData, SDL_GPU_BUFFERUSAGE_VERTEX);
            uploadExtraStreams(command);
            uploadNeutralIfNeeded(command);
            if (command.indexed && !command.indexData.empty())
                command.uploadedIndexBuffer = uploadOne(command.indexData, SDL_GPU_BUFFERUSAGE_INDEX);
        }
        for (DualTextureDrawCommand& command : dualTextureDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty())
                continue;
            command.uploadedVertexBuffer = uploadOne(command.vertexData, SDL_GPU_BUFFERUSAGE_VERTEX);
            uploadExtraStreams(command);
            uploadNeutralIfNeeded(command);
            if (command.indexed && !command.indexData.empty())
                command.uploadedIndexBuffer = uploadOne(command.indexData, SDL_GPU_BUFFERUSAGE_INDEX);
        }
        for (EnvMapDrawCommand& command : envMapDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty())
                continue;
            command.uploadedVertexBuffer = uploadOne(command.vertexData, SDL_GPU_BUFFERUSAGE_VERTEX);
            uploadExtraStreams(command);
            uploadNeutralIfNeeded(command);
            if (command.indexed && !command.indexData.empty())
                command.uploadedIndexBuffer = uploadOne(command.indexData, SDL_GPU_BUFFERUSAGE_INDEX);
        }
        for (InstancedDrawCommand& command : instancedDrawCommands_)
        {
            if (command.vertexData.empty() || command.indexData.empty())
                continue;
            command.uploadedVertexBuffer = uploadOne(
                command.vertexData, SDL_GPU_BUFFERUSAGE_VERTEX);
            uploadExtraStreams(command);
            uploadNeutralIfNeeded(command);
            command.uploadedIndexBuffer = uploadOne(
                command.indexData, SDL_GPU_BUFFERUSAGE_INDEX);
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
            uploadExtraStreams(command);
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
        SDL_GPUGraphicsPipeline* pipeline = GetOrCreatePipelineColored3D(command.vertexLayout,
                                                                          command.topology, command.depthTest,
                                                                          command.depthWrite, command.depthFunc,
                                                                          colorFormat, sampleCount,
                                                                          depthStencilFormat, colorTargetCount, command.renderState);
        if (pipeline != boundPipeline) { SDL_BindGPUGraphicsPipeline(pass, pipeline); boundPipeline = pipeline; }
        SDL_SetGPUStencilReference(pass, static_cast<Uint8>(command.renderState.stencilReference));
        SDL_PushGPUVertexUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));
        SDL_PushGPUVertexUniformData(cmd, 1, command.fogUniforms.data(), sizeof(command.fogUniforms));  // REMED-GFX-009

        BindStockVertexBuffersEXT(pass, command.uploadedVertexBuffer,
                                  command.extraVertexStreams,
                                  command.uploadedNeutralVertexBuffer, command.vertexLayout);

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
                command.vertexLayout,
                command.topology, command.depthTest, command.depthWrite, command.depthFunc,
                colorFormat, sampleCount, depthStencilFormat, colorTargetCount, command.renderState)
            : GetOrCreatePipelineTextured3D(
                command.vertexLayout,
                command.topology, command.depthTest, command.depthWrite, command.depthFunc,
                colorFormat, sampleCount, depthStencilFormat, colorTargetCount, command.renderState);
        if (pipeline != boundPipeline) { SDL_BindGPUGraphicsPipeline(pass, pipeline); boundPipeline = pipeline; }
        SDL_SetGPUStencilReference(pass, static_cast<Uint8>(command.renderState.stencilReference));
        SDL_PushGPUVertexUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));
        SDL_PushGPUVertexUniformData(cmd, 1, command.fogUniforms.data(), sizeof(command.fogUniforms));  // REMED-GFX-009
        SDL_PushGPUFragmentUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));

        BindStockVertexBuffersEXT(pass, command.uploadedVertexBuffer,
                                  command.extraVertexStreams,
                                  command.uploadedNeutralVertexBuffer, command.vertexLayout);

        SDL_GPUTextureSamplerBinding samplerBinding{};
        // plans/plan_gltf.md GLTF-474: a stock effect's base-colour map is optional -- XNA lets
        // BasicEffect/SkinnedEffect/AlphaTestEffect run untextured, and glTF's own default
        // material has no baseColorTexture at all. Binding neutral white makes `tex * colour`
        // collapse to the colour, which is what EasyGL and Vulkan already do; without it this
        // renderer had to refuse the draw upstream instead.
        samplerBinding.texture = command.texture ? command.texture.texture
                                                 : defaultWhiteTexture_->Texture();
        samplerBinding.sampler = GetOrCreateSampler(command.textureFilter, command.addressU,
                                                   command.addressV, command.maxAnisotropy,
                                                   "Textured3D", command.maxMipLevel,
                                                   command.lodBias, command.addressW);
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
            command.vertexLayout,
            command.topology, command.depthTest, command.depthWrite, command.depthFunc,
            colorFormat, sampleCount, depthStencilFormat, colorTargetCount, command.renderState);
        if (pipeline != boundPipeline) { SDL_BindGPUGraphicsPipeline(pass, pipeline); boundPipeline = pipeline; }
        SDL_SetGPUStencilReference(pass, static_cast<Uint8>(command.renderState.stencilReference));
        SDL_PushGPUVertexUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));
        SDL_PushGPUVertexUniformData(cmd, 1, command.lightUniforms.data(), sizeof(command.lightUniforms));
        SDL_PushGPUVertexUniformData(cmd, 2, command.fogUniforms.data(), sizeof(command.fogUniforms));  // REMED-GFX-009
        SDL_PushGPUFragmentUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));
        SDL_PushGPUFragmentUniformData(cmd, 1, command.lightUniforms.data(), sizeof(command.lightUniforms));

        BindStockVertexBuffersEXT(pass, command.uploadedVertexBuffer,
                                  command.extraVertexStreams,
                                  command.uploadedNeutralVertexBuffer, command.vertexLayout);

        SDL_GPUTextureSamplerBinding samplerBinding{};
        // plans/plan_gltf.md GLTF-474: a stock effect's base-colour map is optional -- XNA lets
        // BasicEffect/SkinnedEffect/AlphaTestEffect run untextured, and glTF's own default
        // material has no baseColorTexture at all. Binding neutral white makes `tex * colour`
        // collapse to the colour, which is what EasyGL and Vulkan already do; without it this
        // renderer had to refuse the draw upstream instead.
        samplerBinding.texture = command.texture ? command.texture.texture
                                                 : defaultWhiteTexture_->Texture();
        samplerBinding.sampler = GetOrCreateSampler(command.textureFilter, command.addressU,
                                                   command.addressV, command.maxAnisotropy,
                                                   "LitTextured3D", command.maxMipLevel,
                                                   command.lodBias, command.addressW);
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
                    if (c.uploadedVertexBuffer != nullptr && c.target == target)
                        IssueTexturedDraw(pass, cmd, c, colorFormat, sampleCount,
                                          depthStencilFormat, colorTargetCount, boundPipeline);
                    break;
                }
                case DrawKind::LitTextured:
                {
                    const LitTexturedDrawCommand& c = litTexturedDrawCommands_[ref.index];
                    if (c.uploadedVertexBuffer != nullptr && c.target == target)
                        IssueLitTexturedDraw(pass, cmd, c, colorFormat, sampleCount,
                                             depthStencilFormat, colorTargetCount, boundPipeline);
                    break;
                }
                case DrawKind::AlphaTest:
                {
                    const AlphaTestDrawCommand& c = alphaTestDrawCommands_[ref.index];
                    if (c.uploadedVertexBuffer != nullptr && c.target == target)
                        IssueAlphaTestDraw(pass, cmd, c, colorFormat, sampleCount,
                                           depthStencilFormat, colorTargetCount, boundPipeline);
                    break;
                }
                case DrawKind::DualTexture:
                {
                    const DualTextureDrawCommand& c = dualTextureDrawCommands_[ref.index];
                    if (c.uploadedVertexBuffer != nullptr && c.target == target)
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
                case DrawKind::Instanced:
                {
                    const InstancedDrawCommand& c = instancedDrawCommands_[ref.index];
                    if (c.uploadedVertexBuffer != nullptr &&
                        c.uploadedIndexBuffer != nullptr && c.target == target)
                    {
                        IssueInstancedDraw(pass, cmd, c, colorFormat, sampleCount,
                                           depthStencilFormat, colorTargetCount,
                                           boundPipeline);
                    }
                    break;
                }
                case DrawKind::Skinned:
                {
                    const SkinnedDrawCommand& c = skinnedDrawCommands_[ref.index];
                    if (c.uploadedVertexBuffer != nullptr && c.uploadedBoneBuffer != nullptr && c.target == target)
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
                        // QueueSprite snapshots the logical projection extent because both the
                        // window's presentation scale and the public viewport may change before
                        // this deferred command is replayed. Zero preserves the legacy no-viewport
                        // path and projects over the live render-target extent.
                        float spriteVpSize[2] = {
                            c.projectionWidth > 0.0f ? c.projectionWidth : viewportSize[0],
                            c.projectionHeight > 0.0f ? c.projectionHeight : viewportSize[1]
                        };
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
        const auto releaseExtraStreams = [&](auto& command)
        {
            for (CapturedStockVertexStreamEXT& stream : command.extraVertexStreams)
                release(stream.uploadedBuffer);
        };

        for (ColoredDrawCommand& command : coloredDrawCommands_)
        {
            release(command.uploadedVertexBuffer);
            releaseExtraStreams(command);
            release(command.uploadedNeutralVertexBuffer);
            release(command.uploadedIndexBuffer);
        }
        if (clearCommands) coloredDrawCommands_.clear();
        for (TexturedDrawCommand& command : texturedDrawCommands_)
        {
            release(command.uploadedVertexBuffer);
            releaseExtraStreams(command);
            release(command.uploadedNeutralVertexBuffer);
            release(command.uploadedIndexBuffer);
        }
        if (clearCommands) texturedDrawCommands_.clear();
        for (LitTexturedDrawCommand& command : litTexturedDrawCommands_)
        {
            release(command.uploadedVertexBuffer);
            releaseExtraStreams(command);
            release(command.uploadedNeutralVertexBuffer);
            release(command.uploadedIndexBuffer);
        }
        if (clearCommands) litTexturedDrawCommands_.clear();
        for (AlphaTestDrawCommand& command : alphaTestDrawCommands_)
        {
            release(command.uploadedVertexBuffer);
            releaseExtraStreams(command);
            release(command.uploadedNeutralVertexBuffer);
            release(command.uploadedIndexBuffer);
        }
        if (clearCommands) alphaTestDrawCommands_.clear();
        for (DualTextureDrawCommand& command : dualTextureDrawCommands_)
        {
            release(command.uploadedVertexBuffer);
            releaseExtraStreams(command);
            release(command.uploadedNeutralVertexBuffer);
            release(command.uploadedIndexBuffer);
        }
        if (clearCommands) dualTextureDrawCommands_.clear();
        for (EnvMapDrawCommand& command : envMapDrawCommands_)
        {
            release(command.uploadedVertexBuffer);
            releaseExtraStreams(command);
            release(command.uploadedNeutralVertexBuffer);
            release(command.uploadedIndexBuffer);
        }
        if (clearCommands) envMapDrawCommands_.clear();
        for (InstancedDrawCommand& command : instancedDrawCommands_)
        {
            release(command.uploadedVertexBuffer);
            releaseExtraStreams(command);
            release(command.uploadedNeutralVertexBuffer);
            release(command.uploadedIndexBuffer);
        }
        if (clearCommands) instancedDrawCommands_.clear();
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
            releaseExtraStreams(command);
            release(command.uploadedIndexBuffer);
        }
        if (clearCommands) compiledEffectDrawCommands_.clear();
#endif
    }

    void SdlGpuRenderer::CollectStockVertexStreamsEXT(
        const SdlGpuVertexBufferRenderer& vb, const GpuDrawParams* params,
        StockDrawVertexStreamsEXT& streams, bool includePerInstance)
    {
        streams = StockDrawVertexStreamsEXT{};
        if (params != nullptr)
        {
            for (int i = 0; i < params->vertexStreamCount; ++i)
            {
                const GpuVertexStreamBinding& binding = params->vertexStreams[i];
                if (binding.instanceFrequency != 0 && !includePerInstance)
                    continue;
                const auto* buffer =
                    static_cast<const SdlGpuVertexBufferRenderer*>(binding.buffer);
                if (buffer == nullptr)
                    continue;
                if (streams.count >= streams.declarations.size())
                {
                    throw System::NotSupportedException(
                        "CNA SDL_GPU: this stock draw uses more than eight vertex streams");
                }

                auto& declaration = streams.declarations[streams.count];
                auto& source = streams.sources[streams.count];
                declaration.elements = &buffer->Declaration().GetElements();
                declaration.stride = binding.strideInBytes > 0
                    ? binding.strideInBytes : static_cast<int>(buffer->Stride());
                declaration.instanceFrequency = binding.instanceFrequency;
                source.buffer = buffer;
                source.stride = declaration.stride;
                source.vertexOffset = binding.vertexOffset;
                source.slot = binding.slot;
                source.instanceFrequency = binding.instanceFrequency;
                source.vertexCount = binding.vertexCount > 0
                    ? binding.vertexCount : buffer->GetVertexCount();
                ++streams.count;
            }
        }
        if (streams.count == 0)
        {
            streams.declarations[0].elements = &vb.Declaration().GetElements();
            streams.declarations[0].stride = static_cast<int>(vb.Stride());
            streams.sources[0].buffer = &vb;
            streams.sources[0].stride = static_cast<int>(vb.Stride());
            streams.sources[0].vertexCount = vb.GetVertexCount();
            streams.count = 1;
        }
    }

    void SdlGpuRenderer::SynthesizeMissingStreamDeclarationsEXT(
        StockDrawVertexStreamsEXT& streams)
    {
        using namespace CNA::Internal::Graphics;
        using Microsoft::Xna::Framework::Graphics::VertexElement;
        using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

        for (std::size_t i = 0; i < streams.count; ++i)
        {
            auto& declaration = streams.declarations[i];
            if (declaration.elements != nullptr && !declaration.elements->empty())
                continue;

            auto& elements = streams.synthesized[i];
            if (streams.sources[i].instanceFrequency > 0)
            {
                const int stride = declaration.stride > 0 ? declaration.stride : 64;
                for (int column = 0; column < 4 && (column + 1) * 16 <= stride; ++column)
                {
                    elements.emplace_back(column * 16, VertexElementFormat::Vector4,
                                          VertexElementUsage::Position, column + 1);
                }
            }
            else
            {
                const InferredVertexLayout inferred = InferredLayoutForStride(
                    declaration.stride, UnlistedStrideLayout::PositionOnlyFallback);
                for (std::size_t e = 0; e < inferred.count; ++e)
                {
                    const auto& element = inferred.elements[e];
                    elements.emplace_back(element.offset, element.format, element.usage,
                                          element.usageIndex);
                }
            }
            declaration.elements = &elements;
        }
    }

    void SdlGpuRenderer::CaptureStockVertexStreamsEXT(
        const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& layout,
        const StockDrawVertexStreamsEXT& streams, int vertexStart,
        std::vector<std::uint8_t>& stream0Data,
        std::vector<CapturedStockVertexStreamEXT>& extra, int instanceCount)
    {
        stream0Data.clear();
        extra.clear();
        const std::size_t resolvedCount = layout.streamCount > 0 ? layout.streamCount : 1u;
        const int instances = std::max(1, instanceCount);

        for (std::size_t i = 0; i < resolvedCount; ++i)
        {
            const std::size_t sourceIndex = i < layout.streamCount
                ? static_cast<std::size_t>(layout.streams[i].sourceIndex) : 0u;
            if (sourceIndex >= streams.count)
                continue;
            const StockVertexStreamSourceEXT& source = streams.sources[sourceIndex];
            if (source.buffer == nullptr)
                continue;

            const int stride = source.stride > 0 ? source.stride : 1;
            const auto& shadow = source.buffer->ShadowData();
            std::vector<std::uint8_t>* destination = &stream0Data;
            if (i > 0)
            {
                extra.emplace_back();
                destination = &extra.back().data;
            }

            if (source.instanceFrequency > 0)
            {
                const std::size_t recordBytes = static_cast<std::size_t>(stride);
                const std::size_t base =
                    static_cast<std::size_t>(std::max(0, source.vertexOffset)) * recordBytes;
                destination->assign(static_cast<std::size_t>(instances) * recordBytes, 0u);
                for (int instance = 0; instance < instances; ++instance)
                {
                    const std::size_t sourceOffset = base +
                        static_cast<std::size_t>(instance / source.instanceFrequency) * recordBytes;
                    if (sourceOffset + recordBytes > shadow.size())
                        break;
                    std::memcpy(destination->data() +
                                    static_cast<std::size_t>(instance) * recordBytes,
                                shadow.data() + sourceOffset, recordBytes);
                }
                continue;
            }

            const std::size_t firstRecord =
                static_cast<std::size_t>(std::max(0, vertexStart)) +
                static_cast<std::size_t>(std::max(0, source.vertexOffset));
            const std::size_t byteOffset = firstRecord * static_cast<std::size_t>(stride);
            if (byteOffset <= shadow.size())
            {
                destination->assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset),
                                    shadow.end());
            }
        }
    }

    // Kept for the stock families SDLGPU-59 deliberately does not translate (Skinned/PBR). The
    // classic Basic/AlphaTest/DualTexture/EnvironmentMap paths below resolve their inputs directly
    // from the declaration instead.
    static void RequireFaithfulDeclarationEXT(const IVertexBufferRenderer& vb, const char* route)
    {
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferRenderer&>(vb);
        CNA::Internal::Graphics::RequireFaithfulVertexDeclaration(
            sdlGpuVb.Declaration(), static_cast<int>(sdlGpuVb.Stride()),
            CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt,
            "SDL_GPU", route);
    }

    SdlGpuRenderer::StockVertexShapeEXT SdlGpuRenderer::SelectStockVertexShapeEXT(
        const CNA::Internal::Graphics::StockVertexStreamEXT* streams,
        std::size_t streamCount, std::size_t stride, const GpuDrawParams& params)
    {
        using namespace CNA::Internal::Graphics;
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

        const bool haveDeclaration = AnyStreamDeclaresEXT(streams, streamCount);
        if (!haveDeclaration &&
            !InferredLayoutForStride(static_cast<int>(stride),
                                     UnlistedStrideLayout::RendererRefusesIt).known)
            return StockVertexShapeEXT::StrideDerived;

        if (haveDeclaration &&
            !AnyStreamNamesSemanticEXT(streams, streamCount, VertexElementUsage::Position, 0))
        {
            throw System::NotSupportedException(
                "CNA SDL_GPU: stock effects require POSITION0, but this VertexDeclaration does not declare it");
        }

        const bool hasNormal = haveDeclaration
            ? AnyStreamNamesUsageEXT(streams, streamCount, VertexElementUsage::Normal)
            : stride == 32;
        const bool hasColor = haveDeclaration
            ? AnyStreamNamesSemanticEXT(streams, streamCount, VertexElementUsage::Color, 0)
            : stride == 16 || stride == 24;
        const bool hasUv = haveDeclaration
            ? AnyStreamNamesSemanticEXT(
                streams, streamCount, VertexElementUsage::TextureCoordinate, 0)
            : stride == 20 || stride == 24 || stride == 32;

        const bool needsAlphaTest = !params.pbr &&
                                    (params.alphaTest[2] < 0.0f || params.alphaTest[3] < 0.0f);
        if (needsAlphaTest && hasUv)
            return hasColor ? StockVertexShapeEXT::AlphaTestColored
                            : StockVertexShapeEXT::AlphaTest;

        const bool needsDualTexture = !needsAlphaTest && params.dualTexture;
        if (needsDualTexture && hasUv)
            return hasColor ? StockVertexShapeEXT::DualTexturedColored
                            : StockVertexShapeEXT::DualTextured;

        const bool needsEnvMap = !needsAlphaTest && !needsDualTexture && params.envMapping;
        if (needsEnvMap && hasNormal && hasUv)
            return StockVertexShapeEXT::EnvMapped;

        if (needsAlphaTest || needsDualTexture || needsEnvMap || params.skinned || params.pbr)
            return StockVertexShapeEXT::StrideDerived;
        if (hasNormal)
            return StockVertexShapeEXT::Lit;
        if (hasUv)
            return hasColor ? StockVertexShapeEXT::ColoredTextured
                            : StockVertexShapeEXT::Textured;
        return StockVertexShapeEXT::Colored;
    }

    void SdlGpuRenderer::StockVertexInputsForShapeEXT(
        StockVertexShapeEXT shape,
        const CNA::Internal::Graphics::StockProgramInput*& inputs,
        std::size_t& count, const char*& programName)
    {
        using CNA::Internal::Graphics::StockProgramInput;
        namespace In = CNA::Internal::Graphics::StockVertexInputsEXT;

        static constexpr StockProgramInput kColored[] = {In::kPos, In::kColor};
        static constexpr StockProgramInput kTextured[] = {In::kPos, In::kUv};
        static constexpr StockProgramInput kColoredTextured[] = {In::kPos, In::kColor, In::kUv};
        static constexpr StockProgramInput kLit[] = {In::kPos, In::kNormal, In::kUv, In::kColor};
        static constexpr StockProgramInput kDualTextured[] = {In::kPos, In::kUv, In::kUv1};
        static constexpr StockProgramInput kDualTexturedColored[] = {
            In::kPos, In::kColor, In::kUv, In::kUv1};
        static constexpr StockProgramInput kEnvMapped[] = {In::kPos, In::kNormal, In::kUv};

        switch (shape)
        {
            case StockVertexShapeEXT::Colored:
                inputs = kColored; count = std::size(kColored); programName = "colored3d"; return;
            case StockVertexShapeEXT::Textured:
                inputs = kTextured; count = std::size(kTextured); programName = "textured3d"; return;
            case StockVertexShapeEXT::ColoredTextured:
                inputs = kColoredTextured; count = std::size(kColoredTextured);
                programName = "colored_textured3d"; return;
            case StockVertexShapeEXT::Lit:
                inputs = kLit; count = std::size(kLit); programName = "lit_textured3d"; return;
            case StockVertexShapeEXT::AlphaTest:
                inputs = kTextured; count = std::size(kTextured); programName = "alpha_test3d"; return;
            case StockVertexShapeEXT::AlphaTestColored:
                inputs = kColoredTextured; count = std::size(kColoredTextured);
                programName = "alpha_test_colored3d"; return;
            case StockVertexShapeEXT::DualTextured:
                inputs = kDualTextured; count = std::size(kDualTextured);
                programName = "dual_texture3d"; return;
            case StockVertexShapeEXT::DualTexturedColored:
                inputs = kDualTexturedColored; count = std::size(kDualTexturedColored);
                programName = "dual_texture_colored3d"; return;
            case StockVertexShapeEXT::EnvMapped:
                inputs = kEnvMapped; count = std::size(kEnvMapped); programName = "env_map3d"; return;
            case StockVertexShapeEXT::StrideDerived:
                break;
        }
        inputs = nullptr;
        count = 0;
        programName = "stride-derived";
    }

    CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT
    SdlGpuRenderer::ResolveStockVertexLayoutForDrawEXT(
        const SdlGpuVertexBufferRenderer& vb,
        const CNA::Internal::Graphics::StockVertexStreamEXT* streams,
        std::size_t streamCount, StockVertexShapeEXT shape) const
    {
        using namespace CNA::Internal::Graphics;
        const StockProgramInput* inputs = nullptr;
        std::size_t inputCount = 0;
        const char* programName = nullptr;
        StockVertexInputsForShapeEXT(shape, inputs, inputCount, programName);

        if (AnyStreamDeclaresEXT(streams, streamCount))
        {
            using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
            if (!AnyStreamNamesSemanticEXT(
                    streams, streamCount, VertexElementUsage::Position, 0))
            {
                throw System::NotSupportedException(
                    "CNA SDL_GPU: stock effects require POSITION0, but this VertexDeclaration does not declare it");
            }
            for (std::size_t i = 0; i < streamCount; ++i)
            {
                if (streams[i].elements != nullptr)
                {
                    RequireDeclarationMatchesStockProgram(
                        *streams[i].elements, inputs, inputCount, "SDL_GPU", programName);
                }
            }
            return ResolveStockVertexLayoutAcrossStreamsEXT(
                streams, streamCount, inputs, inputCount);
        }

        const InferredVertexLayout inferred = InferredLayoutForStride(
            static_cast<int>(vb.Stride()), UnlistedStrideLayout::RendererRefusesIt);
        if (!inferred.known)
            throw System::NotSupportedException(
                "CNA SDL_GPU: no VertexDeclaration was propagated and this vertex stride has no canonical stock layout");
        std::vector<Microsoft::Xna::Framework::Graphics::VertexElement> synthesized;
        synthesized.reserve(inferred.count);
        for (std::size_t i = 0; i < inferred.count; ++i)
        {
            const InferredVertexElement& element = inferred.elements[i];
            synthesized.emplace_back(
                element.offset, element.format, element.usage, element.usageIndex);
        }
        ResolvedStockVertexLayoutEXT resolved = ResolveStockVertexLayoutEXT(
            synthesized, static_cast<int>(vb.Stride()), inputs, inputCount);
        resolved.fromDeclaration = false;
        return resolved;
    }

    CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT
    SdlGpuRenderer::ResolveInstancedVertexLayoutEXT(
        const StockDrawVertexStreamsEXT& streams, bool hasVertexColor)
    {
        using namespace CNA::Internal::Graphics;
        using Microsoft::Xna::Framework::Graphics::VertexElement;
        using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        namespace In = CNA::Internal::Graphics::StockVertexInputsEXT;

        ResolvedStockVertexLayoutEXT resolved;
        resolved.stride = streams.declarations[0].stride;
        resolved.fromDeclaration = AnyStreamDeclaresEXT(
            streams.declarations.data(), streams.count);

        std::array<int, kMaxStockVertexStreamsEXT> resolvedIndexOfSource;
        resolvedIndexOfSource.fill(-1);
        const auto keep = [&](std::size_t source) {
            if (resolvedIndexOfSource[source] < 0)
            {
                ResolvedStockStreamEXT& kept = resolved.streams[resolved.streamCount];
                kept.sourceIndex = static_cast<int>(source);
                kept.stride = streams.declarations[source].stride;
                kept.instanceFrequency = streams.declarations[source].instanceFrequency;
                resolvedIndexOfSource[source] = static_cast<int>(resolved.streamCount++);
            }
            return resolvedIndexOfSource[source];
        };

        const StockProgramInput perVertexInputs[2] = {In::kPos, In::kColor};
        for (std::size_t location = 0; location < 2; ++location)
        {
            const StockProgramInput& input = perVertexInputs[location];
            auto& attribute = resolved.attributes[resolved.count++];
            attribute.usage = input.usage;
            attribute.usageIndex = input.usageIndex;
            attribute.shaderLocation = static_cast<int>(location);

            const VertexElement* element = nullptr;
            std::size_t source = 0;
            for (; source < streams.count; ++source)
            {
                if (streams.sources[source].instanceFrequency > 0 ||
                    streams.declarations[source].elements == nullptr)
                    continue;
                element = FindDeclaredSemanticEXT(*streams.declarations[source].elements,
                                                  input.usage, input.usageIndex);
                if (element != nullptr)
                    break;
            }
            if (element != nullptr && (location == 0 || hasVertexColor))
            {
                if (element->getVertexElementFormatProperty() != input.format &&
                    element->getVertexElementFormatProperty() != input.alternateFormat)
                {
                    throw System::NotSupportedException(
                        "CNA SDL_GPU: the instanced stock program received an incompatible per-vertex format");
                }
                attribute.format = element->getVertexElementFormatProperty();
                attribute.offset = element->getOffsetProperty();
                attribute.streamIndex = keep(source);
            }
            else
            {
                if (location == 0)
                {
                    throw System::NotSupportedException(
                        "CNA SDL_GPU: instanced stock effects require POSITION0 in a per-vertex stream");
                }
                attribute.format = NeutralFormatForStockInputEXT(input.format);
                attribute.defaulted = true;
                resolved.usesNeutralRecord = true;
            }
        }

        int column = 0;
        for (std::size_t source = 0; source < streams.count; ++source)
        {
            if (streams.sources[source].instanceFrequency <= 0 ||
                streams.declarations[source].elements == nullptr)
                continue;
            for (const VertexElement& element : *streams.declarations[source].elements)
            {
                if (column >= 4)
                {
                    throw System::NotSupportedException(
                        "CNA SDL_GPU: instanced stock effects require exactly four per-instance Vector4 matrix columns");
                }
                if (element.getVertexElementFormatProperty() != VertexElementFormat::Vector4)
                {
                    throw System::NotSupportedException(
                        "CNA SDL_GPU: instanced stock world-matrix columns must use Vector4 format");
                }
                auto& attribute = resolved.attributes[resolved.count++];
                attribute.usage = element.getVertexElementUsageProperty();
                attribute.usageIndex = element.getUsageIndexProperty();
                attribute.format = element.getVertexElementFormatProperty();
                attribute.offset = element.getOffsetProperty();
                attribute.shaderLocation = 4 + column;
                attribute.streamIndex = keep(source);
                ++column;
            }
        }
        if (column != 4)
        {
            throw System::NotSupportedException(
                "CNA SDL_GPU: instanced stock effects require exactly four per-instance Vector4 matrix columns");
        }
        return resolved;
    }

    void SdlGpuRenderer::DispatchStockDrawEXT(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferRenderer&>(vb);
        StockDrawVertexStreamsEXT streams;
        CollectStockVertexStreamsEXT(sdlGpuVb, &params, streams);
        const StockVertexShapeEXT shape = SelectStockVertexShapeEXT(
            streams.declarations.data(), streams.count, sdlGpuVb.Stride(), params);
        if (shape == StockVertexShapeEXT::StrideDerived)
        {
            // A SkinnedEffect draw is selected by effect state, not by one magic byte stride.
            // QueueSkinnedDraw validates and normalizes the declaration, including the legal
            // Vector4 BLENDINDICES spelling whose record is 64 bytes instead of 52.
            if (params.skinned && !params.pbr)
            {
                QueueSkinnedDraw(vb, ib, world, view, projection,
                                 primitive, primitiveCount, params);
                return;
            }
            RequireFaithfulDeclarationEXT(vb, ib == nullptr ? "ordinary-nonindexed" : "ordinary-indexed");
            const std::size_t stride = sdlGpuVb.Stride();
            if (params.pbr &&
                ((params.skinned && (stride == 68 || stride == 80)) ||
                 (!params.skinned && (stride == 48 || stride == 60))))
            {
                QueuePbrDraw(vb, ib, world, view, projection, primitive, primitiveCount, params);
                return;
            }
            throw System::NotSupportedException(
                "CNA SDL_GPU: the selected stock effect cannot be matched to this VertexDeclaration");
        }

        const auto layout = ResolveStockVertexLayoutForDrawEXT(
            sdlGpuVb, streams.declarations.data(), streams.count, shape);
        switch (shape)
        {
            case StockVertexShapeEXT::Colored:
                QueueColoredDraw(vb, ib, world, view, projection, primitive, primitiveCount,
                                 &params, layout); return;
            case StockVertexShapeEXT::Textured:
                QueueTexturedDraw(vb, ib, world, view, projection, primitive, primitiveCount,
                                  params, false, layout); return;
            case StockVertexShapeEXT::ColoredTextured:
                QueueTexturedDraw(vb, ib, world, view, projection, primitive, primitiveCount,
                                  params, true, layout); return;
            case StockVertexShapeEXT::Lit:
                QueueLitTexturedDraw(vb, ib, world, view, projection, primitive, primitiveCount,
                                     params, layout); return;
            case StockVertexShapeEXT::AlphaTest:
            case StockVertexShapeEXT::AlphaTestColored:
                QueueAlphaTestDraw(
                    vb, ib, world, view, projection, primitive, primitiveCount, params,
                    shape == StockVertexShapeEXT::AlphaTestColored, layout); return;
            case StockVertexShapeEXT::DualTextured:
            case StockVertexShapeEXT::DualTexturedColored:
                QueueDualTextureDraw(
                    vb, ib, world, view, projection, primitive, primitiveCount, params,
                    shape == StockVertexShapeEXT::DualTexturedColored, layout); return;
            case StockVertexShapeEXT::EnvMapped:
                QueueEnvMapDraw(vb, ib, world, view, projection, primitive, primitiveCount,
                                params, layout); return;
            case StockVertexShapeEXT::StrideDerived:
                break;
        }
    }

    void SdlGpuRenderer::DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                                       const Matrix& world, const Matrix& view, const Matrix& projection,
                                                       PrimitiveType primitive, int primitiveCount)
    {
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferRenderer&>(vb);
        StockDrawVertexStreamsEXT streams;
        CollectStockVertexStreamsEXT(sdlGpuVb, nullptr, streams);
        const auto layout = ResolveStockVertexLayoutForDrawEXT(
            sdlGpuVb, streams.declarations.data(), streams.count,
            StockVertexShapeEXT::Colored);
        QueueColoredDraw(vb, nullptr, world, view, projection, primitive, primitiveCount,
                         nullptr, layout);
    }

    void SdlGpuRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                                              const IIndexBufferRenderer& ib,
                                                              const Matrix& world, const Matrix& view, const Matrix& projection,
                                                              PrimitiveType primitive, int primitiveCount)
    {
        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferRenderer&>(vb);
        StockDrawVertexStreamsEXT streams;
        CollectStockVertexStreamsEXT(sdlGpuVb, nullptr, streams);
        const auto layout = ResolveStockVertexLayoutForDrawEXT(
            sdlGpuVb, streams.declarations.data(), streams.count,
            StockVertexShapeEXT::Colored);
        QueueColoredDraw(vb, &ib, world, view, projection, primitive, primitiveCount,
                         nullptr, layout);
    }

    void SdlGpuRenderer::DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                                  PrimitiveType primitive, int primitiveCount,
                                                  const GpuDrawParams& params)
    {
#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
        // plans/plan_fx.md FX-071: a compiled effect's vertex layout is arbitrary and validated against
        // the applied pass's own shader reflection (BuildCompiledEffectVertexAttributes), not
        // against the fixed-stride table RequireFaithfulDeclarationEXT enforces below -- so this
        // dispatches before that guard runs, not after.
        if (params.compiledEffectRuntime != nullptr)
        {
            QueueCompiledEffectDraw(vb, nullptr, primitive, primitiveCount, params);
            return;
        }
#endif
        DispatchStockDrawEXT(
            vb, nullptr, world, view, projection, primitive, primitiveCount, params);
    }

    void SdlGpuRenderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                                         const Matrix& world, const Matrix& view, const Matrix& projection,
                                                         PrimitiveType primitive, int primitiveCount,
                                                         const GpuDrawParams& params)
    {
#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
        // plans/plan_fx.md FX-071: see DrawPrimitivesEx's identical guard for why this dispatches before
        // RequireFaithfulDeclarationEXT rather than after.
        if (params.compiledEffectRuntime != nullptr)
        {
            QueueCompiledEffectDraw(vb, &ib, primitive, primitiveCount, params);
            return;
        }
#endif
        DispatchStockDrawEXT(
            vb, &ib, world, view, projection, primitive, primitiveCount, params);
    }

    void SdlGpuRenderer::DrawInstancedPrimitivesEx(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, int instanceCount,
        const GpuDrawParams& params)
    {
#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
        if (params.compiledEffectRuntime != nullptr)
        {
            QueueCompiledEffectDraw(
                vb, &ib, primitive, primitiveCount, params, instanceCount);
            return;
        }
#endif
        if (params.customEffectRenderer != nullptr)
        {
            throw System::NotSupportedException(
                "CNA SDL_GPU: custom-effect instancing is not implemented");
        }
        if (FirstInstanceStream(params) == nullptr)
        {
            DrawIndexedPrimitivesEx(vb, ib, world, view, projection,
                                    primitive, primitiveCount, params);
            return;
        }

        const auto& sdlGpuVb = static_cast<const SdlGpuVertexBufferRenderer&>(vb);
        const auto& sdlGpuIb = static_cast<const SdlGpuIndexBufferRenderer&>(ib);
        StockDrawVertexStreamsEXT streams;
        CollectStockVertexStreamsEXT(sdlGpuVb, &params, streams, true);
        SynthesizeMissingStreamDeclarationsEXT(streams);

        bool hasVertexColor = false;
        for (std::size_t i = 0; i < streams.count && !hasVertexColor; ++i)
        {
            if (streams.sources[i].instanceFrequency > 0 ||
                streams.declarations[i].elements == nullptr)
                continue;
            hasVertexColor = CNA::Internal::Graphics::FindDeclaredSemanticEXT(
                *streams.declarations[i].elements,
                Microsoft::Xna::Framework::Graphics::VertexElementUsage::Color, 0) != nullptr;
        }

        const int clampedInstanceCount = std::max(1, instanceCount);
        for (std::size_t i = 0; i < streams.count; ++i)
        {
            const StockVertexStreamSourceEXT& source = streams.sources[i];
            if (source.instanceFrequency > 0)
            {
                const int lastRecord = source.vertexOffset +
                    (clampedInstanceCount - 1) / source.instanceFrequency;
                if (source.vertexOffset < 0 || lastRecord >= source.vertexCount)
                {
                    throw System::ArgumentOutOfRangeException(
                        "instanceCount", std::to_string(instanceCount),
                        "CNA SDL_GPU: the per-instance stream bound to slot " +
                        std::to_string(source.slot) + " does not contain the requested record.");
                }
            }
            else if (source.vertexOffset < 0 || source.vertexOffset > source.vertexCount ||
                     params.baseVertex > source.vertexCount - source.vertexOffset)
            {
                throw System::ArgumentOutOfRangeException(
                    "baseVertex", std::to_string(params.baseVertex),
                    "CNA SDL_GPU: a per-vertex binding offset leaves its buffer.");
            }
        }

        InstancedDrawCommand command;
        command.vertexLayout = ResolveInstancedVertexLayoutEXT(streams, hasVertexColor);
        CaptureStockVertexStreamsEXT(command.vertexLayout, streams, 0,
                                     command.vertexData, command.extraVertexStreams,
                                     clampedInstanceCount);
        command.indexData = sdlGpuIb.ShadowData();
        command.index32 = sdlGpuIb.IsThirtyTwoBit();
        command.instanceCount = static_cast<Uint32>(clampedInstanceCount);
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthWrite = depthWriteEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.renderState = CaptureRenderState();
        const NativeIndexedRange range = ResolveIndexedRange(
            sdlGpuIb, sdlGpuVb, primitive, primitiveCount, &params);
        command.indexCount = range.indexCount;
        command.firstIndex = range.firstIndex;
        command.vertexOffset = range.vertexOffset;
        FillExtUniforms(
            command.uniforms, ApplyXnaPixelCenter(world * view * projection), params);
        FillFogUniforms(command.fogUniforms, params);
        command.target = CurrentDrawTarget();

        instancedDrawCommands_.push_back(std::move(command));
        PushDrawOrder(DrawKind::Instanced, instancedDrawCommands_.size() - 1);
        framePending_ = true;
    }

    // ---- SdlGpuSampledTextureState / texture resolution (REMED-GFX-152) ----

    SdlGpuSampledTextureState::~SdlGpuSampledTextureState()
    {
        // Deferred, not immediate -- a draw queued this frame is only issued at
        // EnsureFrameRendered(), so a public Texture2D/Texture3D/TextureCube destroyed in between must leave
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

    SdlGpuSampledTextureEXT ResolveSampledVolumeEXT(
        const ITexture3DRenderer* texture, const char* usage)
    {
        if (texture == nullptr)
            return {};
        if (const auto* plain = dynamic_cast<const SdlGpuTexture3DRenderer*>(texture))
            return plain->Sampled();
        throw std::invalid_argument(
            std::string("CNA SDL_GPU: ") + usage +
            " received a volume texture this renderer cannot sample (a resource from another "
            "graphics renderer?)");
    }

    // ---- SdlGpuTextureRenderer ----

    SdlGpuTextureRenderer::SdlGpuTextureRenderer(SdlGpuRenderer& owner, const ImageData& data)
        : owner_(&owner),
          state_(std::make_shared<SdlGpuSampledTextureState>()),
          width_(data.width), height_(data.height), surfaceFormat_(data.surfaceFormat)
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

        const SurfaceFormat format = static_cast<SurfaceFormat>(surfaceFormat_);
        logicalBlockBytes_ = LogicalTextureBlockBytes(format);
        if (logicalBlockBytes_ == 0)
            throw std::invalid_argument(
                "CNA SDL_GPU: Texture2D received unsupported SurfaceFormat ordinal " +
                std::to_string(surfaceFormat_));
        compressed_ = IsClassicDxtFormat(format);
        if (compressed_)
            compressedLevels_.resize(static_cast<std::size_t>(levelCount_));

        const SDL_GPUTextureFormat preferredFormat = PreferredTextureFormat(format);
        const bool forcedDxtFallback = compressed_ && ForceDxtFallbackForTest();
        const bool packedFormat = format == SurfaceFormat::Bgr565 ||
            format == SurfaceFormat::Bgra5551 || format == SurfaceFormat::Bgra4444;
        const bool forcedPackedFallback = packedFormat && ForcePackedFallbackForTest();
        const bool preferredSupported = !forcedDxtFallback && !forcedPackedFallback &&
            SDL_GPUTextureSupportsFormat(
            owner_->Device(), preferredFormat, SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_SAMPLER);
        // NormalizedByte2 deliberately uses RGBA8_SNORM even though RG8_SNORM exists: D3D9/XNA
        // sampling fills the missing B/A channels with one, and materializing those values at
        // upload makes the rule hold for every shader route without format-specific shader state.
        // Packed and DXT storage fall back to RGBA8 only when the concrete driver lacks their
        // exact native representation.
        nativeFormat_ = preferredSupported
            ? preferredFormat
            : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        compressedNative_ = compressed_ && preferredSupported;

        const std::size_t logicalLevelZeroBytes = compressed_
            ? static_cast<std::size_t>((width_ + 3) / 4) *
                  static_cast<std::size_t>((height_ + 3) / 4) *
                  static_cast<std::size_t>(logicalBlockBytes_)
            : static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) *
                  static_cast<std::size_t>(logicalBlockBytes_);
        if (data.pixels.size() < logicalLevelZeroBytes)
            throw std::invalid_argument(
                "CNA SDL_GPU: Texture2D pixel buffer is smaller than the requested format's "
                "level-zero storage");

        SDL_GPUTextureCreateInfo createInfo{};
        createInfo.type = SDL_GPU_TEXTURETYPE_2D;
        createInfo.format = nativeFormat_;
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
                         "[cna-sdlgpu-texture] id=%d created %dx%d surfaceFormat=%d "
                         "nativeFormat=%d logicalBlockBytes=%d compressed=%s nativeCompressed=%s "
                         "usage=SAMPLER requestedLevels=%d maxLevels=%d nativeLevels=%u texture=%p\n",
                         traceId_, width_, height_, surfaceFormat_, static_cast<int>(nativeFormat_),
                         logicalBlockBytes_, compressed_ ? "yes" : "no",
                         compressedNative_ ? "yes" : "no", requestedLevels, maxLevels,
                         static_cast<unsigned>(createInfo.num_levels),
                         static_cast<void*>(state_->texture));
            std::fflush(stderr);
        }

        // Only level 0 is written here, and only with what the caller already supplied. The
        // remaining declared levels are ALLOCATED, never generated -- SDL_GenerateMipmapsForGPU-
        // Texture is deliberately not called (XNA/FNA give Texture2D no implicit regeneration, and
        // REMED-GFX-175's contract makes level content the caller's).
        if (compressed_)
            UpdatePixelsLevel(0, data.pixels.data(), width_, height_);
        else
            UpdatePixels(data.pixels.data(), width_ * logicalBlockBytes_);
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

    void SdlGpuTextureRenderer::UpdatePixels(const uint8_t* pixels, int stride)
    {
        UploadLevel(0, pixels, width_, height_, stride);
    }

    void SdlGpuTextureRenderer::UpdatePixelsLevel(int level, const uint8_t* pixels,
                                                  int levelW, int levelH)
    {
        // Same convention as VulkanTextureRenderer::UpdatePixelsLevel: this interface method
        // returns void, so an out-of-range level cannot be reported and must not be guessed at
        // either -- reaching SDL_UploadToGPUTexture with a nonexistent subresource is exactly the
        // class of failure REMED-GFX-135 removed from the Texture3D path.
        if (pixels == nullptr || level < 0 || level >= levelCount_)
        {
            if (TextureTraceEnabled() && traceId_ != 0)
            {
                std::fprintf(stderr,
                             "[cna-sdlgpu-texture] id=%d level=%d IGNORED reason=%s levels=%d\n",
                             traceId_, level, pixels == nullptr ? "null-source" : "level-out-of-range",
                             levelCount_);
                std::fflush(stderr);
            }
            return;
        }
        const int stride = compressed_
            ? ((levelW + 3) / 4) * logicalBlockBytes_
            : levelW * logicalBlockBytes_;
        UploadLevel(level, pixels, levelW, levelH, stride);
    }

    void SdlGpuTextureRenderer::UploadLevel(int level, const uint8_t* pixels, int levelW, int levelH,
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

        if (pixels == nullptr)
            throw std::invalid_argument("CNA SDL_GPU: Texture2D upload source cannot be null");

        const int logicalRows = compressed_ ? (levelH + 3) / 4 : levelH;
        const int logicalRowBytes = compressed_
            ? ((levelW + 3) / 4) * logicalBlockBytes_
            : levelW * logicalBlockBytes_;
        if (stride < logicalRowBytes)
            throw std::invalid_argument("CNA SDL_GPU: Texture2D upload stride is too small");

        std::vector<std::uint8_t> tightLogical;
        const std::uint8_t* logicalPixels = pixels;
        if (stride != logicalRowBytes)
        {
            tightLogical.resize(static_cast<std::size_t>(logicalRowBytes) * logicalRows);
            for (int row = 0; row < logicalRows; ++row)
            {
                std::memcpy(tightLogical.data() + static_cast<std::size_t>(row) * logicalRowBytes,
                            pixels + static_cast<std::size_t>(row) * stride,
                            static_cast<std::size_t>(logicalRowBytes));
            }
            logicalPixels = tightLogical.data();
        }
        const std::size_t logicalBytes =
            static_cast<std::size_t>(logicalRowBytes) * logicalRows;
        if (compressed_)
        {
            compressedLevels_[static_cast<std::size_t>(level)].assign(
                logicalPixels, logicalPixels + logicalBytes);
        }

        const SurfaceFormat format = static_cast<SurfaceFormat>(surfaceFormat_);
        const bool packedFallback =
            (format == SurfaceFormat::Bgr565 || format == SurfaceFormat::Bgra5551 ||
             format == SurfaceFormat::Bgra4444) &&
            nativeFormat_ == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        const bool conversionNeeded = (!compressedNative_ && compressed_) ||
            format == SurfaceFormat::NormalizedByte2 || packedFallback;
        std::vector<std::uint8_t> converted;
        const std::uint8_t* uploadPixels = logicalPixels;
        std::size_t uploadBytes = logicalBytes;
        if (conversionNeeded)
        {
            converted = ConvertTextureLevelForFallback(
                format, logicalPixels, logicalBytes, levelW, levelH);
            uploadPixels = converted.data();
            uploadBytes = converted.size();
        }

        const Uint32 expectedUploadBytes = SDL_CalculateGPUTextureFormatSize(
            nativeFormat_, static_cast<Uint32>(levelW), static_cast<Uint32>(levelH), 1);
        if (uploadBytes != expectedUploadBytes)
            throw std::runtime_error(
                "CNA SDL_GPU: Texture2D converted upload size does not match SDL_gpu's format size");

        SDL_GPUDevice* device = owner_->Device();
        const Uint32 sizeBytes = expectedUploadBytes;

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
        std::memcpy(mapped, uploadPixels, sizeBytes);
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
        // Zero selects SDL's tightly-packed, format-aware defaults. This is important for BC
        // formats, where one transfer row is a row of 4x4 blocks rather than levelW texels.
        source.pixels_per_row = 0;
        source.rows_per_layer = 0;
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
                         "region=(0,0,%ux%u) transferBytes=%u logicalRowPitch=%d pixelsPerRow=%u "
                         "srcStride=%d conversion=%s cycle=%s submit=ok\n",
                         traceId_, level, levelW, levelH,
                         static_cast<unsigned>(destination.mip_level),
                         static_cast<unsigned>(destination.w), static_cast<unsigned>(destination.h),
                         static_cast<unsigned>(sizeBytes), logicalRowBytes,
                         static_cast<unsigned>(source.pixels_per_row), stride,
                         conversionNeeded ? "yes" : "no",
                         cycle ? "yes" : "no");
            std::fflush(stderr);
        }
    }

    bool SdlGpuTextureRenderer::GetData(int level, int x, int y, int w, int h,
                                        void* data, int dataLength) const
    {
        if (!compressed_ || data == nullptr || level < 0 || level >= levelCount_ ||
            w <= 0 || h <= 0)
            return false;

        const int levelW = std::max(1, width_ >> level);
        const int levelH = std::max(1, height_ >> level);
        if (x < 0 || y < 0 || x + w > levelW || y + h > levelH ||
            (x % 4) != 0 || (y % 4) != 0 ||
            ((w % 4) != 0 && x + w != levelW) ||
            ((h % 4) != 0 && y + h != levelH))
            return false;

        const int levelBlockCols = (levelW + 3) / 4;
        const int rectBlockCols = (w + 3) / 4;
        const int rectBlockRows = (h + 3) / 4;
        const std::size_t required = static_cast<std::size_t>(rectBlockCols) * rectBlockRows *
            static_cast<std::size_t>(logicalBlockBytes_);
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required)
            return false;

        const auto& levelBytes = compressedLevels_[static_cast<std::size_t>(level)];
        if (levelBytes.empty())
            return false;
        const std::size_t levelRowBytes =
            static_cast<std::size_t>(levelBlockCols) * logicalBlockBytes_;
        const std::size_t rectRowBytes =
            static_cast<std::size_t>(rectBlockCols) * logicalBlockBytes_;
        auto* destination = static_cast<std::uint8_t*>(data);
        for (int row = 0; row < rectBlockRows; ++row)
        {
            const std::size_t sourceOffset =
                static_cast<std::size_t>(y / 4 + row) * levelRowBytes +
                static_cast<std::size_t>(x / 4) * logicalBlockBytes_;
            if (sourceOffset + rectRowBytes > levelBytes.size())
                return false;
            std::memcpy(destination + static_cast<std::size_t>(row) * rectRowBytes,
                        levelBytes.data() + sourceOffset, rectRowBytes);
        }
        return true;
    }

    // ---- SdlGpuTexture3DRenderer (Phase SDLGPU-9, SDLGPU-40/SDLGPU-41) ----

    SdlGpuTexture3DRenderer::SdlGpuTexture3DRenderer(
        SdlGpuRenderer& owner, int width, int height, int depth,
        bool mipMap, int surfaceFormat)
        : owner_(&owner), state_(std::make_shared<SdlGpuSampledTextureState>())
        , width_(width), height_(height), depth_(depth)
        , levelCount_(mipMap ? CalculateMipLevels(width, height) : 1)
        , surfaceFormat_(surfaceFormat)
    {
        state_->owner = &owner;
        if (static_cast<SurfaceFormat>(surfaceFormat_) != SurfaceFormat::Color)
        {
            throw std::invalid_argument(
                "CNA SDL_GPU: Texture3D received unsupported SurfaceFormat ordinal " +
                std::to_string(surfaceFormat_));
        }

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

        state_->texture = SDL_CreateGPUTexture(owner_->Device(), &createInfo);
        if (state_->texture == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create Texture3D: ") + SDL_GetError());
    }

    SdlGpuTexture3DRenderer::~SdlGpuTexture3DRenderer() = default;

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
        destination.texture = state_->texture;
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
        region.texture = state_->texture;
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

    // ---- SdlGpuTextureCubeRenderer (Phase SDLGPU-9, SDLGPU-51, SDLGPU-70) ----

    SdlGpuTextureCubeRenderer::SdlGpuTextureCubeRenderer(
        SdlGpuRenderer& owner, int size, bool mipMap, int surfaceFormat)
        : owner_(&owner)
        , state_(std::make_shared<SdlGpuSampledTextureState>())
        , size_(size), mipMap_(mipMap)
        , levelCount_(mipMap ? CalculateMipLevels(size, size) : 1)
        , surfaceFormat_(surfaceFormat)
    {
        state_->owner = &owner;

        const SurfaceFormat format = static_cast<SurfaceFormat>(surfaceFormat_);
        compressed_ = IsClassicDxtFormat(format);
        blockBytes_ = LogicalTextureBlockBytes(format);
        if ((!compressed_ && format != SurfaceFormat::Color) || blockBytes_ == 0)
        {
            throw std::invalid_argument(
                "CNA SDL_GPU: TextureCube received unsupported SurfaceFormat ordinal " +
                std::to_string(surfaceFormat_));
        }

        const SDL_GPUTextureFormat preferredFormat = PreferredTextureFormat(format);
        compressedNative_ = compressed_ && !ForceDxtFallbackForTest() &&
            SDL_GPUTextureSupportsFormat(
                owner_->Device(), preferredFormat, SDL_GPU_TEXTURETYPE_CUBE,
                SDL_GPU_TEXTUREUSAGE_SAMPLER);
        nativeFormat_ = compressedNative_
            ? preferredFormat
            : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

        if (compressed_)
        {
            compressedLevels_.resize(static_cast<std::size_t>(6 * levelCount_));
            for (int face = 0; face < 6; ++face)
            {
                for (int level = 0; level < levelCount_; ++level)
                {
                    const int levelSize = std::max(1, size_ >> level);
                    compressedLevels_[static_cast<std::size_t>(face * levelCount_ + level)]
                        .resize(static_cast<std::size_t>((levelSize + 3) / 4) *
                                static_cast<std::size_t>((levelSize + 3) / 4) *
                                static_cast<std::size_t>(blockBytes_), 0u);
                }
            }
        }

        SDL_GPUTextureCreateInfo createInfo{};
        createInfo.type = SDL_GPU_TEXTURETYPE_CUBE;
        createInfo.format = nativeFormat_;
        // Plain XNA TextureCube mip levels are explicitly authored with SetData, exactly like
        // Texture2D/Texture3D. This resource is never a render target and therefore needs neither
        // COLOR_TARGET usage nor SDL's whole-cube mip generator.
        createInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
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
        if (compressed_ || data == nullptr || w <= 0 || h <= 0) return false;
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

        if (!SDL_SubmitGPUCommandBuffer(cmd))
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: TextureCube::SetData: SDL_SubmitGPUCommandBuffer failed: ") + SDL_GetError());
        }
        SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
        return true;
    }

    bool SdlGpuTextureCubeRenderer::UploadCompressedLevel(
        int face, int level, const std::vector<std::uint8_t>& blocks)
    {
        const int levelSize = std::max(1, size_ >> level);
        const SurfaceFormat format = static_cast<SurfaceFormat>(surfaceFormat_);

        std::vector<std::uint8_t> converted;
        const std::uint8_t* uploadBytes = blocks.data();
        std::size_t uploadByteCount = blocks.size();
        if (!compressedNative_)
        {
            converted = ConvertTextureLevelForFallback(
                format, blocks.data(), blocks.size(), levelSize, levelSize);
            uploadBytes = converted.data();
            uploadByteCount = converted.size();
        }

        const Uint32 expectedBytes = SDL_CalculateGPUTextureFormatSize(
            nativeFormat_, static_cast<Uint32>(levelSize),
            static_cast<Uint32>(levelSize), 1);
        if (uploadByteCount != expectedBytes)
        {
            throw std::runtime_error(
                "CNA SDL_GPU: TextureCube converted upload size does not match SDL_gpu's "
                "format size");
        }

        SDL_GPUDevice* device = owner_->Device();
        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = expectedBytes;
        SDL_GPUTransferBuffer* transferBuffer =
            SDL_CreateGPUTransferBuffer(device, &transferInfo);
        if (transferBuffer == nullptr)
        {
            throw std::runtime_error(
                std::string("CNA SDL_GPU: TextureCube compressed upload transfer allocation "
                            "failed: ") + SDL_GetError());
        }

        void* mapped = SDL_MapGPUTransferBuffer(device, transferBuffer, false);
        if (mapped == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(
                std::string("CNA SDL_GPU: TextureCube compressed upload map failed: ") +
                SDL_GetError());
        }
        std::memcpy(mapped, uploadBytes, expectedBytes);
        SDL_UnmapGPUTransferBuffer(device, transferBuffer);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        if (cmd == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(
                std::string("CNA SDL_GPU: TextureCube compressed upload command buffer failed: ") +
                SDL_GetError());
        }

        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transferBuffer;
        // SDL's zero values select the tight, format-aware pitch. For BC formats that is a row
        // of 4x4 blocks; for the decode fallback it is a row of RGBA8 texels.
        source.pixels_per_row = 0;
        source.rows_per_layer = 0;
        SDL_GPUTextureRegion destination{};
        destination.texture = state_->texture;
        destination.mip_level = static_cast<Uint32>(level);
        destination.layer = static_cast<Uint32>(face);
        destination.w = static_cast<Uint32>(levelSize);
        destination.h = static_cast<Uint32>(levelSize);
        destination.d = 1;
        // A cube is assembled over six faces and possibly several authored mip levels. Cycling
        // any single subresource would orphan every previously uploaded face/level.
        SDL_UploadToGPUTexture(copyPass, &source, &destination, false);
        SDL_EndGPUCopyPass(copyPass);

        if (!SDL_SubmitGPUCommandBuffer(cmd))
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(
                std::string("CNA SDL_GPU: TextureCube compressed upload submit failed: ") +
                SDL_GetError());
        }
        SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
        return true;
    }

    bool SdlGpuTextureCubeRenderer::SetCompressedDataEXT(
        int face, int level, int x, int y, int w, int h,
        const void* data, int dataLength)
    {
        if (!compressed_ || data == nullptr || w <= 0 || h <= 0)
            return false;
        if (face < 0 || face >= 6 || level < 0 || level >= levelCount_)
            return false;

        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize ||
            (x % 4) != 0 || (y % 4) != 0 ||
            ((w % 4) != 0 && x + w != levelSize) ||
            ((h % 4) != 0 && y + h != levelSize))
        {
            return false;
        }

        const int levelBlockCols = (levelSize + 3) / 4;
        const int rectBlockCols = (w + 3) / 4;
        const int rectBlockRows = (h + 3) / 4;
        const std::size_t requiredBytes =
            static_cast<std::size_t>(rectBlockCols) * rectBlockRows * blockBytes_;
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < requiredBytes)
            return false;

        const std::size_t index = static_cast<std::size_t>(face * levelCount_ + level);
        std::vector<std::uint8_t> replacement = compressedLevels_[index];
        const std::size_t levelRowBytes =
            static_cast<std::size_t>(levelBlockCols) * blockBytes_;
        const std::size_t rectRowBytes =
            static_cast<std::size_t>(rectBlockCols) * blockBytes_;
        const auto* source = static_cast<const std::uint8_t*>(data);
        for (int row = 0; row < rectBlockRows; ++row)
        {
            const std::size_t destinationOffset =
                static_cast<std::size_t>(y / 4 + row) * levelRowBytes +
                static_cast<std::size_t>(x / 4) * blockBytes_;
            std::memcpy(replacement.data() + destinationOffset,
                        source + static_cast<std::size_t>(row) * rectRowBytes,
                        rectRowBytes);
        }

        // Upload the reconstructed complete level. SDL_gpu can express native partial BC copies,
        // but the complete-level upload gives native BC and decoded RGBA fallback the same exact
        // preservation semantics for untouched blocks, including NPOT edge blocks.
        if (!UploadCompressedLevel(face, level, replacement))
            return false;
        compressedLevels_[index] = std::move(replacement);
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

        if (compressed_)
        {
            const auto& blocks = compressedLevels_[
                static_cast<std::size_t>(face * levelCount_ + level)];
            const SurfaceFormat format = static_cast<SurfaceFormat>(surfaceFormat_);
            const std::vector<std::uint8_t> rgba = ConvertTextureLevelForFallback(
                format, blocks.data(), blocks.size(), levelSize, levelSize);
            if (rgba.size() < static_cast<std::size_t>(levelSize) * levelSize * 4u)
                return false;
            auto* destination = static_cast<std::uint8_t*>(data);
            for (int row = 0; row < h; ++row)
            {
                std::memcpy(
                    destination + static_cast<std::size_t>(row) * w * 4u,
                    rgba.data() +
                        (static_cast<std::size_t>(y + row) * levelSize + x) * 4u,
                    static_cast<std::size_t>(w) * 4u);
            }
            return true;
        }

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

    SdlGpuRenderTargetRenderer::SdlGpuRenderTargetRenderer(
        SdlGpuRenderer& owner, int width, int height, int depthFormat,
        bool preserveContents, bool mipMap, int multiSampleCount, int surfaceFormat)
        : owner_(&owner)
    {
        state_ = std::make_shared<SdlGpuRenderTarget2DState>();
        state_->owner = owner_;
        state_->width = width;
        state_->height = height;
        state_->surfaceFormat = surfaceFormat;
        state_->preserveContents = preserveContents;

        SDL_GPUDevice* device = owner_->Device();
        RenderTargetFormatInfo formatInfo{};
        if (!TryGetRenderTargetFormatInfo(
                static_cast<SurfaceFormat>(surfaceFormat), formatInfo) ||
            !SDL_GPUTextureSupportsFormat(
                device, formatInfo.nativeFormat, SDL_GPU_TEXTURETYPE_2D,
                SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER))
        {
            throw std::invalid_argument(
                "CNA SDL_GPU: RenderTarget2D received unsupported SurfaceFormat ordinal " +
                std::to_string(surfaceFormat));
        }
        state_->colorFormat = formatInfo.nativeFormat;
        state_->colorBytesPerPixel = formatInfo.bytesPerPixel;

        DepthTargetFormatInfo depthInfo{};
        if (!TryGetDepthTargetFormatInfo(device,
                static_cast<Microsoft::Xna::Framework::Graphics::DepthFormat>(depthFormat),
                depthInfo))
        {
            throw std::invalid_argument(
                "CNA SDL_GPU: RenderTarget2D received unsupported exact DepthFormat ordinal " +
                std::to_string(depthFormat));
        }
        state_->depthFormat = depthInfo.nativeFormat;
        state_->appliedDepthFormat = depthInfo.appliedDepthFormat;
        state_->depthBits = depthInfo.depthBits;
        state_->hasStencil = depthInfo.hasStencil;

        const SDL_GPUSampleCount sampleCount =
            ClampSampleCount(
                device, state_->colorFormat, state_->depthFormat, multiSampleCount);
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
        colorInfo.format = state_->colorFormat;
        colorInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        colorInfo.width = static_cast<Uint32>(width);
        colorInfo.height = static_cast<Uint32>(height);
        colorInfo.layer_count_or_depth = 1;
        colorInfo.num_levels = mipMap_ ? static_cast<Uint32>(CalculateMipLevels(width, height)) : 1;
        // REMED-GFX-186: what SDL really allocated, so GetData can refuse a level with no storage.
        levelCount_ = static_cast<int>(colorInfo.num_levels);
        state_->levelCount = levelCount_;
        state_->definedMipLevels.assign(static_cast<std::size_t>(levelCount_), false);
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
            msaaInfo.format = state_->colorFormat;
            msaaInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
            msaaInfo.width = static_cast<Uint32>(width);
            msaaInfo.height = static_cast<Uint32>(height);
            msaaInfo.layer_count_or_depth = 1;
            msaaInfo.num_levels = 1;
            msaaInfo.sample_count = sampleCount;
            state_->msaaTexture = SDL_CreateGPUTexture(device, &msaaInfo);
            if (state_->msaaTexture == nullptr)
            {
                throw std::runtime_error(std::string("CNA SDL_GPU: failed to create RenderTarget2D MSAA texture: ") + SDL_GetError());
            }
        }

        // DepthFormat::None requests no attachment. Every other value uses its exact native
        // D16/D24/D24S8 storage, never the swapchain's unrelated combined-format choice.
        if (state_->depthFormat != SDL_GPU_TEXTUREFORMAT_INVALID)
        {
            SDL_GPUTextureCreateInfo depthInfo{};
            depthInfo.type = SDL_GPU_TEXTURETYPE_2D;
            depthInfo.format = state_->depthFormat;
            depthInfo.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
            depthInfo.width = static_cast<Uint32>(width);
            depthInfo.height = static_cast<Uint32>(height);
            depthInfo.layer_count_or_depth = 1;
            depthInfo.num_levels = 1;
            depthInfo.sample_count = sampleCount;  // MSAA depth matches MSAA color
            state_->depthTexture = SDL_CreateGPUTexture(device, &depthInfo);
            if (state_->depthTexture == nullptr)
            {
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

    void SdlGpuRenderTargetRenderer::UpdatePixels(const uint8_t* pixels, int stride)
    {
        UploadLevel(0, pixels, state_->width, state_->height, stride);
    }

    void SdlGpuRenderTargetRenderer::UpdatePixelsLevel(
        int level, const uint8_t* pixels, int levelW, int levelH)
    {
        if (level < 0 || level >= levelCount_ || pixels == nullptr)
            return;
        const int expectedW = std::max(1, state_->width >> level);
        const int expectedH = std::max(1, state_->height >> level);
        if (levelW != expectedW || levelH != expectedH)
            throw std::invalid_argument(
                "CNA SDL_GPU: RenderTarget2D::SetData supplied incorrect mip dimensions");
        UploadLevel(level, pixels, levelW, levelH,
                    levelW * static_cast<int>(state_->colorBytesPerPixel));
    }

    void SdlGpuRenderTargetRenderer::UploadLevel(
        int level, const uint8_t* pixels, int levelW, int levelH, int stride)
    {
        if (level < 0 || level >= levelCount_)
            throw std::out_of_range(
                "CNA SDL_GPU: RenderTarget2D::SetData mip level is outside the native chain");

        // Public SetData is chronological with queued draws. Flush every earlier segment before
        // the copy so a subsequent upload cannot be overwritten by work recorded before it.
        owner_->EnsureFrameRendered();
        UploadTargetRegion(owner_->Device(), state_->colorTexture, state_->colorFormat,
                           state_->colorBytesPerPixel, 0, level, 0, 0, levelW, levelH,
                           pixels, stride, "CNA SDL_GPU: RenderTarget2D::SetData");
        if (level == 0 && state_->msaaTexture != nullptr)
        {
            owner_->SeedMultisampleTargetFromResolved(
                state_->colorTexture, -1, state_->msaaTexture,
                state_->colorFormat, state_->sampleCount, state_->surfaceFormat,
                levelW, levelH, "CNA SDL_GPU: RenderTarget2D::SetData");
        }
        state_->definedMipLevels[static_cast<std::size_t>(level)] = true;
        if (level == 0)
        {
            // A first PreserveContents bind must LOAD the bytes authored through SetData instead
            // of applying the target's uninitialized-storage safety clear over them.
            state_->clearColorPending = false;
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
        const Uint32 sizeBytes = static_cast<Uint32>(w) * static_cast<Uint32>(h) *
                                 state_->colorBytesPerPixel;
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

    SdlGpuRenderTargetCubeRenderer::SdlGpuRenderTargetCubeRenderer(
        SdlGpuRenderer& owner, int size, int depthFormat, bool preserveContents,
        bool mipMap, int multiSampleCount, int surfaceFormat)
        : owner_(&owner), mipMap_(mipMap)
    {
        state_ = std::make_shared<SdlGpuRenderTargetCubeState>();
        state_->owner = owner_;
        state_->size = size;
        state_->surfaceFormat = surfaceFormat;
        // REMED-GFX-141: only the multisampled path consults it -- see the field's own comment.
        state_->preserveContents = preserveContents;

        SDL_GPUDevice* device = owner_->Device();
        RenderTargetFormatInfo formatInfo{};
        if (!TryGetRenderTargetFormatInfo(
                static_cast<SurfaceFormat>(surfaceFormat), formatInfo) ||
            !SDL_GPUTextureSupportsFormat(
                device, formatInfo.nativeFormat, SDL_GPU_TEXTURETYPE_CUBE,
                SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER))
        {
            throw std::invalid_argument(
                "CNA SDL_GPU: RenderTargetCube received unsupported SurfaceFormat ordinal " +
                std::to_string(surfaceFormat));
        }
        state_->colorFormat = formatInfo.nativeFormat;
        state_->colorBytesPerPixel = formatInfo.bytesPerPixel;

        DepthTargetFormatInfo depthInfo{};
        if (!TryGetDepthTargetFormatInfo(device,
                static_cast<Microsoft::Xna::Framework::Graphics::DepthFormat>(depthFormat),
                depthInfo))
        {
            throw std::invalid_argument(
                "CNA SDL_GPU: RenderTargetCube received unsupported exact DepthFormat ordinal " +
                std::to_string(depthFormat));
        }
        state_->depthFormat = depthInfo.nativeFormat;
        state_->appliedDepthFormat = depthInfo.appliedDepthFormat;
        state_->depthBits = depthInfo.depthBits;
        state_->hasStencil = depthInfo.hasStencil;

        const SDL_GPUSampleCount sampleCount = ClampSampleCount(
            device, state_->colorFormat, state_->depthFormat, multiSampleCount);
        multiSampleCount_ = SampleCountToInt(sampleCount);
        state_->sampleCount = sampleCount;
        // The resolved cube owns the public mip chain even when rendering uses a separate
        // level-zero-only multisample attachment (REMED-GFX-188).
        state_->mipMap = mipMap_;

        SDL_GPUTextureCreateInfo cubeInfo{};
        cubeInfo.type = SDL_GPU_TEXTURETYPE_CUBE;
        cubeInfo.format = state_->colorFormat;
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
            msaaInfo.format = state_->colorFormat;
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
                    throw std::runtime_error(
                        "CNA SDL_GPU: failed to create RenderTargetCube MSAA texture: " + what);
                }
            }
        }

        if (state_->depthFormat != SDL_GPU_TEXTUREFORMAT_INVALID)
        {
            SDL_GPUTextureCreateInfo depthInfo{};
            depthInfo.type = SDL_GPU_TEXTURETYPE_2D;
            depthInfo.format = state_->depthFormat;
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

    bool SdlGpuRenderTargetCubeRenderer::SetData(
        int face, int level, int x, int y, int w, int h,
        const void* data, int dataLength)
    {
        // TextureCube's classic transfer overload supplies unpacked RGBA8 Color texels. Formats
        // with another native element shape have no typed cube transfer surface in CNA yet, so do
        // not reinterpret those four-byte values as half/float/packed target storage.
        if (state_->surfaceFormat != static_cast<int>(SurfaceFormat::Color) || data == nullptr ||
            face < 0 || face >= 6 || level < 0 || level >= state_->levelCount ||
            w <= 0 || h <= 0)
        {
            return false;
        }
        const int levelSize = std::max(1, state_->size >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize)
            return false;
        const Uint32 sizeBytes = static_cast<Uint32>(w) * static_cast<Uint32>(h) * 4;
        if (dataLength < 0 || static_cast<Uint32>(dataLength) < sizeBytes)
            return false;

        // Match the 2D path's chronology: every draw queued before SetData reaches the target
        // first, then this face/level copy becomes the content observed by later work.
        owner_->EnsureFrameRendered();
        UploadTargetRegion(owner_->Device(), state_->cubeTexture, state_->colorFormat,
                           4, face, level, x, y, w, h,
                           static_cast<const std::uint8_t*>(data), w * 4,
                           "CNA SDL_GPU: RenderTargetCube::SetData");
        if (level == 0 && state_->msaaTextures[static_cast<std::size_t>(face)] != nullptr)
        {
            owner_->SeedMultisampleTargetFromResolved(
                state_->cubeTexture, face,
                state_->msaaTextures[static_cast<std::size_t>(face)],
                state_->colorFormat, state_->sampleCount, state_->surfaceFormat,
                state_->size, state_->size, "CNA SDL_GPU: RenderTargetCube::SetData");
        }
        if (level == 0)
        {
            // PreserveContents must not erase an authored face on its first bind.
            state_->clearColorPending[static_cast<std::size_t>(face)] = false;
        }
        return true;
    }

    bool SdlGpuRenderTargetCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                                void* data, int dataLength) const
    {
        // ITextureCubeRenderer's public transfer contract is tightly packed RGBA8. CNA's current
        // TextureCube surface has no typed float/half/Rgba64 overload, so never expose native bytes
        // through that Color-shaped virtual.
        if (state_->surfaceFormat != static_cast<int>(SurfaceFormat::Color)) return false;
        return GetNativeDataEXT(face, level, x, y, w, h, data, dataLength);
    }

    bool SdlGpuRenderTargetCubeRenderer::GetNativeDataEXT(
        int face, int level, int x, int y, int w, int h,
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
        const Uint32 sizeBytes = static_cast<Uint32>(w) * static_cast<Uint32>(h) *
                                 state_->colorBytesPerPixel;
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

    SDL_GPUGraphicsPipeline* SdlGpuEffectRenderer::GetOrCreatePipeline(
                                                                       const SdlGpuColorTargetFormatsEXT& colorFormats,
                                                                       SDL_GPUSampleCount sampleCount,
                                                                       SDL_GPUTextureFormat depthStencilFormat,
                                                                       int colorTargetCount,
                                                                       const std::array<int, 4>& colorWriteMasks,
                                                                       float depthBias,
                                                                       float slopeScaleDepthBias)
    {
        if (!valid_)
            return nullptr;
        // SDLGPU-75: count plus every slot's format are immutable compatibility state. INVALID is a
        // real value meaning "no depth/stencil attachment", not a default format: REMED-GFX-097
        // proved that reusing a depth-backed pipeline in that pass violates Vulkan compatibility.
        std::size_t key = 0;
        key = HashCombine(key, static_cast<std::size_t>(SampleCountToInt(sampleCount)));
        key = HashCombine(key, static_cast<std::size_t>(colorTargetCount));
        for (int i = 0; i < colorTargetCount; ++i)
            key = HashCombine(key, static_cast<std::size_t>(colorFormats[i]));
        key = HashCombine(key, static_cast<std::size_t>(depthStencilFormat));
        // Per-slot write masks are static pipeline state. Keep custom-effect reuse aligned with
        // the same active attachment slots represented by this pipeline's target descriptions.
        for (int i = 0; i < colorTargetCount; ++i)
            key = HashCombine(key, static_cast<std::size_t>(colorWriteMasks[i] & 0xF));
        key = HashDepthBias(
            key, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
            depthStencilFormat,
            depthBias, slopeScaleDepthBias);
        const auto it = pipelines_.find(key);
        if (it != pipelines_.end())
            return it->second;

        // Fixed SpriteVertex-shaped contract (x,y,z|u,v|r,g,b,a, 36 bytes), matching the stock
        // sprite pipeline's own vertex layout exactly (see GetOrCreateSpritePipeline) -- this is a
        // SpriteBatch-custom-shader facility, not a general arbitrary-vertex-format one.
        SDL_GPUVertexBufferDescription vbDesc{};
        vbDesc.slot = 0;
        vbDesc.pitch = sizeof(SdlGpuRenderer::SpriteVertex);
        vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        SDL_GPUVertexAttribute attrs[3]{};
        attrs[0].location = 0; attrs[0].buffer_slot = 0; attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        attrs[0].offset = offsetof(SdlGpuRenderer::SpriteVertex, x);
        attrs[1].location = 1; attrs[1].buffer_slot = 0; attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attrs[1].offset = offsetof(SdlGpuRenderer::SpriteVertex, u);
        attrs[2].location = 2; attrs[2].buffer_slot = 0; attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attrs[2].offset = offsetof(SdlGpuRenderer::SpriteVertex, r);

        std::array<SDL_GPUColorTargetDescription, 4> colorTargets{};
        for (int i = 0; i < colorTargetCount; ++i)
        {
            SDL_GPUColorTargetDescription& colorTarget = colorTargets[i];
            colorTarget.format = colorFormats[i];
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
            depthStencilFormat,
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

    void SdlGpuSpriteBatchRenderer::SetSamplerState(int textureFilter, int addressU, int addressV,
                                                     int addressW, int maxAnisotropy,
                                                     int maxMipLevel, float lodBias)
    {
        textureFilter_ = textureFilter;
        addressU_ = addressU;
        addressV_ = addressV;
        addressW_ = addressW;
        maxAnisotropy_ = maxAnisotropy;
        maxMipLevel_ = maxMipLevel;
        lodBias_ = lodBias;
    }

    void SdlGpuSpriteBatchRenderer::Begin()
    {
        if (begun_)
            throw std::logic_error("CNA SDL_GPU SpriteBatch.Begin called twice without End");
#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
        // plans/plan_fx.md FX-102: a batch starts empty even if the previous one threw out of its flush.
        // Nothing else clears this, and a leftover sprite would be replayed into an unrelated batch
        // with that batch's transform and sampler.
        pendingSprites_.clear();
#endif
        begun_ = true;
    }

    void SdlGpuSpriteBatchRenderer::End()
    {
        if (!begun_)
            throw std::logic_error("CNA SDL_GPU SpriteBatch.End called without Begin");
#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
        FlushPendingCompiledSpritesEXT();
#endif
        begun_ = false;
    }

#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
    void SdlGpuSpriteBatchRenderer::FlushPendingCompiledSpritesEXT()
    {
        if (pendingSprites_.empty()) return;

        // plans/plan_fx.md FX-102: XNA's own batching granularity, reproduced exactly.
        //
        // FNA's SpriteBatch.FlushBatch walks the (already sorted) sprites and splits them into
        // CONTIGUOUS RUNS OF ONE TEXTURE; DrawPrimitives then draws each run once per pass of the
        // effect's current technique -- `foreach (pass) { pass.Apply(); Textures[0] = texture;
        // Draw(run); }`. So the order is run-major, then pass-major, then sprite: pass 0 covers the
        // whole run before pass 1 begins.
        //
        // This route used to apply the passes inside Draw() and queue the sprite once per pass, so
        // two sprites and two passes came out sprite-major -- s0p0, s0p1, s1p0, s1p1 instead of
        // s0p0, s1p0, s0p1, s1p1. Under any order-dependent blend that is a different image, and it
        // is not a shape a game can work around: XNA guarantees the batch order.
        //
        // Immediate mode is the exception and is NOT routed here at all: XNA flushes per Draw()
        // there, so a run is one sprite and the per-sprite pass loop in Draw() is already correct.
        Microsoft::Xna::Framework::Graphics::EffectTechnique* technique =
            customEffect_ != nullptr ? customEffect_->getCurrentTechniqueProperty() : nullptr;
        const int passCount =
            technique != nullptr ? technique->getPassesProperty().getCountProperty() : 0;
        if (passCount == 0)
        {
            pendingSprites_.clear();
            throw std::logic_error(
                "CNA SDL_GPU: a compiled Effect used with SpriteBatch must have a current "
                "technique with at least one pass.");
        }

        ICompiledEffectRuntime* runtime = customEffect_->GetCompiledRuntimePtr();
        std::size_t runStart = 0;
        while (runStart < pendingSprites_.size())
        {
            std::size_t runEnd = runStart + 1;
            while (runEnd < pendingSprites_.size() &&
                   pendingSprites_[runEnd].texture == pendingSprites_[runStart].texture)
            {
                ++runEnd;
            }
            for (int pass = 0; pass < passCount; ++pass)
            {
                technique->getPassesProperty()[pass].Apply();
                for (std::size_t i = runStart; i < runEnd; ++i)
                {
                    const PendingSpriteEXT& sprite = pendingSprites_[i];
                    owner_->QueueSprite(*sprite.texture, sprite.nativeTexture, sprite.destination,
                                        sprite.source, sprite.color, sprite.rotation, sprite.origin,
                                        sprite.effects, sprite.layerDepth, transform_,
                                        textureFilter_, addressU_, addressV_, addressW_,
                                        maxAnisotropy_, maxMipLevel_, lodBias_, nullptr, runtime);
                }
            }
            runStart = runEnd;
        }
        pendingSprites_.clear();
    }
#endif

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
        // plans/plan_fx.md FX-071: customEffect_ is either ShaderEffect-derived (resolved above) or a
        // compiled effect (resolved here), never both -- Effect::GetCompiledRuntimePtr() returns
        // null for a ShaderEffect and GetEffectRendererPtr() returns null for a compiled effect.
        ICompiledEffectRuntime* compiledEffectRuntime =
            customEffect_ ? customEffect_->GetCompiledRuntimePtr() : nullptr;
#if defined(CNA_SDL_GPU_COMPILED_EFFECTS)
        if (compiledEffectRuntime != nullptr)
        {
            // plans/plan_fx.md FX-080: SpriteBatch applies the effect's passes itself, exactly as FNA's
            // SpriteBatch.DrawPrimitives does -- once per pass of the current technique, drawing
            // the sprite again for each. Before this the route silently relied on the caller
            // having applied a pass already, so a game using only the public
            // SpriteBatch.Begin(..., effect) API queued a sprite whose binding was captured from
            // whatever pass happened to be applied last, or from none at all.
            Microsoft::Xna::Framework::Graphics::EffectTechnique* technique =
                customEffect_->getCurrentTechniqueProperty();
            const int passCount =
                technique != nullptr ? technique->getPassesProperty().getCountProperty() : 0;
            if (passCount == 0)
            {
                throw std::logic_error(
                    "CNA SDL_GPU: a compiled Effect used with SpriteBatch must have a current "
                    "technique with at least one pass.");
            }
            if (!immediateMode_)
            {
                // Deferred and every sorted mode: XNA applies the passes when the batch FLUSHES,
                // over whole texture runs, so the sprite is only recorded here. See
                // FlushPendingCompiledSpritesEXT.
                pendingSprites_.push_back(PendingSpriteEXT{
                    &texture, nativeTexture, destinationRectangle, sourceRectangle, color,
                    rotation, origin, effects, layerDepth});
                return;
            }
            // Immediate: XNA's SpriteBatch flushes this one sprite now, so its run IS this sprite
            // and applying the passes around it here is the same order XNA produces.
            for (int pass = 0; pass < passCount; ++pass)
            {
                technique->getPassesProperty()[pass].Apply();
                owner_->QueueSprite(texture, nativeTexture, destinationRectangle, sourceRectangle,
                                    color, rotation, origin, effects, layerDepth, transform_,
                                    textureFilter_, addressU_, addressV_, addressW_, maxAnisotropy_,
                                    maxMipLevel_, lodBias_, customEffectRenderer, compiledEffectRuntime);
            }
            return;
        }
#endif
        owner_->QueueSprite(texture, nativeTexture, destinationRectangle, sourceRectangle, color, rotation,
                            origin, effects, layerDepth, transform_, textureFilter_, addressU_, addressV_,
                            addressW_, maxAnisotropy_, maxMipLevel_, lodBias_, customEffectRenderer,
                            compiledEffectRuntime);
    }

}

namespace CNA::Internal::Renderers
{
    // plans/plan_runtimerenderer.md design decision 4: declared in this family's own
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
