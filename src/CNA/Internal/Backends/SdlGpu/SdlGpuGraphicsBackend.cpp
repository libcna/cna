// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.hpp"

#include "CNA/Logger.hpp"
#include "CNA/LogCategory.hpp"
#include "CNA/Internal/Backends/SdlGpu/shaders/spirv_shaders.hpp"

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

        // Packs (topology, depthTest, depthWrite, depthFunc, colorFormat) into one cache key --
        // mirrors WebGPUGraphicsBackend's own int-keyed pipeline cache convention. colorFormat was
        // added in Phase SDLGPU-8 (previously every pipeline only ever targeted the swapchain);
        // SDL_GPUTextureFormat's enum range comfortably fits under the *128 multiplier.
        [[nodiscard]] int PipelineCacheKey(SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
                                           SDL_GPUTextureFormat colorFormat)
        {
            const int base = (static_cast<int>(topology) * 2 + (depthTest ? 1 : 0)) * 2 * 16 +
                   (depthWrite ? 1 : 0) * 16 + std::clamp(depthFunc, 0, 15);
            return base * 128 + static_cast<int>(colorFormat);
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

        // Secondary UBO for lit_textured3d.glsl: DirectionalLight1/DirectionalLight2, EmissiveColor,
        // World (the vertex shader computes its own normal matrix via GLSL's built-in inverse(),
        // unlike WebGPUGraphicsBackend's WGSL-forced CPU-side precomputation -- no normal-matrix
        // slots needed here), EyePosition, per-light SpecularColor, material SpecularColor/Power.
        // Mirrors VulkanGraphicsBackend's LitLightParams UBO field-for-field (minus fog,
        // deliberately deferred like the other SDL_GPU 3D shaders).
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
        device_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, /*debug_mode=*/false, /*name=*/nullptr);
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

        int w = 0;
        int h = 0;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        physicalWidth_ = w;
        physicalHeight_ = h;

        IGraphicsBackend::RegisterForWindow(window_, this);
    }

    SdlGpuGraphicsBackend::~SdlGpuGraphicsBackend()
    {
        IGraphicsBackend::UnregisterForWindow(window_);
        ReleaseSceneDrawBuffers();
        DestroyDualTextureResources();
        DestroyAlphaTestResources();
        DestroyLitTexturedResources();
        DestroyTexturedResources();
        DestroyColoredResources();
        DestroySpriteResources();
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
        // (SDLGPU-35's own "bound in one pass, sampled in a later pass" contract).
        for (SdlGpuRenderTargetBackend* target : usedRenderTargetsThisFrame_)
            RenderToTarget(cmd, target);
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
        // 3D draws first, 2D SpriteBatch/UI on top -- matches typical XNA game draw order
        // (World.Draw() then a HUD SpriteBatch pass), both collapsed into this one deferred pass.
        const DrawTarget swapchainTarget{};
        RenderColoredDraws(pass, cmd, swapchainTarget, swapchainFormat);
        RenderTexturedDraws(pass, cmd, swapchainTarget, swapchainFormat);
        RenderLitTexturedDraws(pass, cmd, swapchainTarget, swapchainFormat);
        RenderAlphaTestDraws(pass, cmd, swapchainTarget, swapchainFormat);
        RenderDualTextureDraws(pass, cmd, swapchainTarget, swapchainFormat);
        RenderSprites(pass, cmd, swapchainTarget, swapchainFormat);
        SDL_EndGPURenderPass(pass);

        // Cube mip regen is real GPU work -- must happen on this command buffer BEFORE submission
        // (per SDL_gpu.h: SDL_GenerateMipmapsForGPUTexture must not be called inside any pass, but
        // is otherwise fine any time before submit). No per-layer control on this call -- it
        // regenerates all 6 faces' chains; harmless for a face untouched this frame (same
        // unchanged level-0 data produces an identical result).
        {
            std::vector<SdlGpuRenderTargetCubeBackend*> mipRegenerated;
            for (auto& [cube, face] : usedRenderTargetCubeFacesThisFrame_)
            {
                if (cube->WantsMipMap() && std::find(mipRegenerated.begin(), mipRegenerated.end(), cube) == mipRegenerated.end())
                {
                    SDL_GenerateMipmapsForGPUTexture(cmd, cube->CubeTexture());
                    mipRegenerated.push_back(cube);
                }
            }
        }

        if (!SDL_SubmitGPUCommandBuffer(cmd))
            throw std::runtime_error(std::string("CNA SDL_GPU: SDL_SubmitGPUCommandBuffer failed: ") + SDL_GetError());

        ReleaseSceneDrawBuffers();

        spriteCommands_.clear();
        clearColorPending_ = false;
        clearDepthPending_ = false;
        clearStencilPending_ = false;
        for (SdlGpuRenderTargetBackend* target : usedRenderTargetsThisFrame_)
            target->ResetPendingClears();
        usedRenderTargetsThisFrame_.clear();
        for (auto& [cube, face] : usedRenderTargetCubeFacesThisFrame_)
            cube->ResetPendingClears(face);
        usedRenderTargetCubeFacesThisFrame_.clear();
        framePending_ = false;
        return true;
    }

    void SdlGpuGraphicsBackend::RenderToTarget(SDL_GPUCommandBuffer* cmd, SdlGpuRenderTargetBackend* target)
    {
        constexpr SDL_GPUTextureFormat kRenderTargetFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

        SDL_GPUColorTargetInfo colorTarget{};
        colorTarget.texture = target->MsaaTexture() != nullptr ? target->MsaaTexture() : target->ColorTexture();
        colorTarget.clear_color = target->ClearColorValue();
        colorTarget.load_op = target->ClearColorPending() ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        if (target->MsaaTexture() != nullptr)
        {
            colorTarget.resolve_texture = target->ColorTexture();
            colorTarget.store_op = SDL_GPU_STOREOP_RESOLVE;
        }
        else
        {
            colorTarget.store_op = SDL_GPU_STOREOP_STORE;
        }

        SDL_GPUDepthStencilTargetInfo depthStencilTarget{};
        const bool hasDepth = target->DepthTexture() != nullptr;
        if (hasDepth)
        {
            depthStencilTarget.texture = target->DepthTexture();
            depthStencilTarget.clear_depth = target->ClearDepthValue();
            depthStencilTarget.load_op = target->ClearDepthPending() ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            depthStencilTarget.store_op = SDL_GPU_STOREOP_STORE;
            depthStencilTarget.clear_stencil = target->ClearStencilValue();
            depthStencilTarget.stencil_load_op = target->ClearStencilPending() ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            depthStencilTarget.stencil_store_op = SDL_GPU_STOREOP_STORE;
        }

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &colorTarget, 1, hasDepth ? &depthStencilTarget : nullptr);
        const DrawTarget dt{target, nullptr, -1};
        RenderColoredDraws(pass, cmd, dt, kRenderTargetFormat);
        RenderTexturedDraws(pass, cmd, dt, kRenderTargetFormat);
        RenderLitTexturedDraws(pass, cmd, dt, kRenderTargetFormat);
        RenderAlphaTestDraws(pass, cmd, dt, kRenderTargetFormat);
        RenderDualTextureDraws(pass, cmd, dt, kRenderTargetFormat);
        RenderSprites(pass, cmd, dt, kRenderTargetFormat);
        SDL_EndGPURenderPass(pass);

        // Per SDL_gpu.h: SDL_GenerateMipmapsForGPUTexture must not be called inside any pass --
        // matches FNA3D's OPENGL_ResolveTarget semantics (mip chain regenerated once this target's
        // contents are final for the frame).
        if (target->WantsMipMap())
            SDL_GenerateMipmapsForGPUTexture(cmd, target->ColorTexture());
    }

    void SdlGpuGraphicsBackend::RenderToTargetCubeFace(SDL_GPUCommandBuffer* cmd, SdlGpuRenderTargetCubeBackend* cube, int face)
    {
        constexpr SDL_GPUTextureFormat kRenderTargetFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

        SDL_GPUColorTargetInfo colorTarget{};
        colorTarget.texture = cube->MsaaTexture() != nullptr ? cube->MsaaTexture() : cube->CubeTexture();
        colorTarget.layer_or_depth_plane = static_cast<Uint32>(face);
        colorTarget.clear_color = cube->ClearColorValue(face);
        colorTarget.load_op = cube->ClearColorPending(face) ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        if (cube->MsaaTexture() != nullptr)
        {
            // Automatic render-pass-end resolve -- SDL_gpu has no multisampled cube texture type,
            // so the MSAA color target is a plain 6-layer 2D array resolved directly into the
            // active face of the real (single-sample) cube texture.
            colorTarget.resolve_texture = cube->CubeTexture();
            colorTarget.resolve_layer = static_cast<Uint32>(face);
            colorTarget.store_op = SDL_GPU_STOREOP_RESOLVE;
        }
        else
        {
            colorTarget.store_op = SDL_GPU_STOREOP_STORE;
        }

        SDL_GPUDepthStencilTargetInfo depthStencilTarget{};
        const bool hasDepth = cube->DepthTexture() != nullptr;
        if (hasDepth)
        {
            depthStencilTarget.texture = cube->DepthTexture();
            depthStencilTarget.clear_depth = cube->ClearDepthValue(face);
            depthStencilTarget.load_op = cube->ClearDepthPending(face) ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            depthStencilTarget.store_op = SDL_GPU_STOREOP_STORE;
            depthStencilTarget.clear_stencil = cube->ClearStencilValue(face);
            depthStencilTarget.stencil_load_op = cube->ClearStencilPending(face) ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            depthStencilTarget.stencil_store_op = SDL_GPU_STOREOP_STORE;
        }

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &colorTarget, 1, hasDepth ? &depthStencilTarget : nullptr);
        const DrawTarget dt{nullptr, cube, face};
        RenderColoredDraws(pass, cmd, dt, kRenderTargetFormat);
        RenderTexturedDraws(pass, cmd, dt, kRenderTargetFormat);
        RenderLitTexturedDraws(pass, cmd, dt, kRenderTargetFormat);
        RenderAlphaTestDraws(pass, cmd, dt, kRenderTargetFormat);
        RenderDualTextureDraws(pass, cmd, dt, kRenderTargetFormat);
        RenderSprites(pass, cmd, dt, kRenderTargetFormat);
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
            return DrawTarget{nullptr, currentRenderTargetCube_, currentActiveCubeFace_};
        return DrawTarget{currentRenderTarget_, nullptr, -1};
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
            static_cast<SdlGpuRenderTargetBackend*>(rt)->BindAsRenderTarget();
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

    void SdlGpuGraphicsBackend::SetRenderTargets(IRenderTargetBackend* const* rts, int count)
    {
        currentExtraMrtTargets_.clear();
        if (count <= 0 || rts == nullptr)
        {
            SetRenderTarget2D(nullptr);
            return;
        }

        // Draws remain single-target (rts[0] only) -- no shader in this codebase declares more
        // than one fragment output, the same honest scope boundary this project's D3D11/D3D12 MRT
        // support already established. Extra targets are bound (get their own real pass this
        // frame) and independently cleared (see Clear()/ClearDepth()/ClearStencil()) but never
        // become currentRenderTarget_.
        SetRenderTarget2D(rts[0]);
        for (int i = 1; i < count; ++i)
        {
            auto* extra = static_cast<SdlGpuRenderTargetBackend*>(rts[i]);
            extra->MarkUsedThisFrame();
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

    SDL_GPUGraphicsPipeline* SdlGpuGraphicsBackend::GetOrCreateSpritePipeline(SDL_GPUTextureFormat colorFormat)
    {
        const int key = static_cast<int>(colorFormat);
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

        // Standard (non-premultiplied) alpha blend: src*srcAlpha + dst*(1-srcAlpha).
        SDL_GPUColorTargetDescription colorTarget{};
        colorTarget.format = colorFormat;
        colorTarget.blend_state.enable_blend = true;
        colorTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        colorTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        colorTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        colorTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        colorTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        colorTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = spriteVertexShader_;
        pipelineInfo.fragment_shader = spriteFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = 3;
        pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pipelineInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        // No depth test/write for 2D sprites, but the pipeline must still declare a compatible
        // depth-stencil target format when one is present in the render passes it's used in
        // (this backend's render pass always includes one once depthStencilFormat_ is known --
        // see EnsureFrameRendered/EnsureDepthStencilTexture).
        pipelineInfo.depth_stencil_state.enable_depth_test = false;
        pipelineInfo.depth_stencil_state.enable_depth_write = false;
        pipelineInfo.depth_stencil_state.enable_stencil_test = false;
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

    void SdlGpuGraphicsBackend::RenderSprites(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                              const DrawTarget& target, SDL_GPUTextureFormat colorFormat)
    {
        if (spriteCommands_.empty())
            return;

        bool boundPipeline = false;
        const float viewportSize[2] = {
            static_cast<float>(target.rt != nullptr ? target.rt->GetWidth()
                              : target.cube != nullptr ? target.cube->GetSize() : physicalWidth_),
            static_cast<float>(target.rt != nullptr ? target.rt->GetHeight()
                              : target.cube != nullptr ? target.cube->GetSize() : physicalHeight_)};

        for (std::size_t i = 0; i < spriteCommands_.size(); ++i)
        {
            const SpriteCommand& command = spriteCommands_[i];
            if (command.target != target)
                continue;
            if (!boundPipeline)
            {
                SDL_BindGPUGraphicsPipeline(pass, GetOrCreateSpritePipeline(colorFormat));
                SDL_PushGPUVertexUniformData(cmd, 0, viewportSize, sizeof(viewportSize));
                boundPipeline = true;
            }
            SDL_GPUBufferBinding vbBinding{};
            vbBinding.buffer = spriteVertexBuffer_;
            vbBinding.offset = static_cast<Uint32>(i * sizeof(SpriteVertex) * 6);
            SDL_BindGPUVertexBuffers(pass, 0, &vbBinding, 1);

            SDL_GPUTextureSamplerBinding samplerBinding{};
            samplerBinding.texture = command.texture;
            samplerBinding.sampler = GetOrCreateSampler(command.textureFilter, command.addressU, command.addressV);
            SDL_BindGPUFragmentSamplers(pass, 0, &samplerBinding, 1);

            SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
        }
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
                                             int addressV)
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
        vsInfo.num_uniform_buffers = 1;
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
        SDL_GPUTextureFormat colorFormat)
    {
        const int key = PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, colorFormat);
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
        // Opaque -- BlendState mapping (plan_sdlgpu.md SDLGPU-18) is not needed for this milestone.

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = coloredVertexShader_;
        pipelineInfo.fragment_shader = coloredFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = 2;
        pipelineInfo.primitive_type = topology;
        pipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pipelineInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        pipelineInfo.depth_stencil_state.enable_depth_test = depthTest;
        pipelineInfo.depth_stencil_state.enable_depth_write = depthWrite;
        pipelineInfo.depth_stencil_state.compare_op = ToCompareOp(depthFunc);
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
        vsInfo.num_uniform_buffers = 1;
        texturedVertexShader_ = SDL_CreateGPUShader(device_, &vsInfo);
        if (texturedVertexShader_ == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create textured3d vertex shader: ") + SDL_GetError());

        SDL_GPUShaderCreateInfo cvsInfo{};
        cvsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kColoredTextured3dVertSpv);
        cvsInfo.code_size = Shaders::kColoredTextured3dVertSpv_size;
        cvsInfo.entrypoint = "main";
        cvsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        cvsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        cvsInfo.num_uniform_buffers = 1;
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
        SDL_GPUTextureFormat colorFormat)
    {
        const int key = PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, colorFormat);
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

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = texturedVertexShader_;
        pipelineInfo.fragment_shader = texturedFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = 2;
        pipelineInfo.primitive_type = topology;
        pipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pipelineInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        pipelineInfo.depth_stencil_state.enable_depth_test = depthTest;
        pipelineInfo.depth_stencil_state.enable_depth_write = depthWrite;
        pipelineInfo.depth_stencil_state.compare_op = ToCompareOp(depthFunc);
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
        SDL_GPUTextureFormat colorFormat)
    {
        const int key = PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, colorFormat);
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

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = coloredTexturedVertexShader_;
        pipelineInfo.fragment_shader = texturedFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = 3;
        pipelineInfo.primitive_type = topology;
        pipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pipelineInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        pipelineInfo.depth_stencil_state.enable_depth_test = depthTest;
        pipelineInfo.depth_stencil_state.enable_depth_write = depthWrite;
        pipelineInfo.depth_stencil_state.compare_op = ToCompareOp(depthFunc);
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
        vsInfo.num_uniform_buffers = 2;
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
        SDL_GPUTextureFormat colorFormat)
    {
        const int key = PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, colorFormat);
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

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = litTexturedVertexShader_;
        pipelineInfo.fragment_shader = litTexturedFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = 3;
        pipelineInfo.primitive_type = topology;
        pipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pipelineInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        pipelineInfo.depth_stencil_state.enable_depth_test = depthTest;
        pipelineInfo.depth_stencil_state.enable_depth_write = depthWrite;
        pipelineInfo.depth_stencil_state.compare_op = ToCompareOp(depthFunc);
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
        if (params != nullptr)
        {
            const Matrix wvp = world * view * projection;
            FillExtUniforms(command.uniforms, wvp, *params);
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
        const Matrix wvp = world * view * projection;
        FillExtUniforms(command.uniforms, wvp, params);
        command.texture = static_cast<const SdlGpuTextureBackend*>(params.texture0);
        // plan_sdlgpu.md SDLGPU-21: ApplySamplerState() (per-slot dynamic sampler state for 3D
        // draws) is not implemented yet -- always samples Linear+Clamp for now.
        command.textureFilter = 0;
        command.addressU = 1;
        command.addressV = 1;

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
        const Matrix wvp = world * view * projection;
        FillExtUniforms(command.uniforms, wvp, params);
        FillLitLightUniforms(command.lightUniforms, params);
        command.texture = static_cast<const SdlGpuTextureBackend*>(params.texture0);
        command.textureFilter = 0;
        command.addressU = 1;
        command.addressV = 1;

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
        vsInfo.num_uniform_buffers = 1;
        alphaTestVertexShader_ = SDL_CreateGPUShader(device_, &vsInfo);
        if (alphaTestVertexShader_ == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create alpha_test3d vertex shader: ") + SDL_GetError());

        SDL_GPUShaderCreateInfo cvsInfo{};
        cvsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kAlphaTestColored3dVertSpv);
        cvsInfo.code_size = Shaders::kAlphaTestColored3dVertSpv_size;
        cvsInfo.entrypoint = "main";
        cvsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        cvsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        cvsInfo.num_uniform_buffers = 1;
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
        SDL_GPUTextureFormat colorFormat)
    {
        // stride 24 (vertex colour) uses its own dedicated map, keyed the same way every other
        // stride-specific map here is. strides 20/32 share alphaTestVertexShader_ but need
        // DIFFERENT vertex_input_state (different attribute offsets), so that map's key folds in
        // the stride explicitly.
        if (stride == 24)
        {
            const int key = PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, colorFormat);
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

            SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.vertex_shader = alphaTestColoredVertexShader_;
            pipelineInfo.fragment_shader = alphaTestFragmentShader_;
            pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
            pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
            pipelineInfo.vertex_input_state.vertex_attributes = attrs;
            pipelineInfo.vertex_input_state.num_vertex_attributes = 3;
            pipelineInfo.primitive_type = topology;
            pipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            pipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
            pipelineInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
            pipelineInfo.depth_stencil_state.enable_depth_test = depthTest;
            pipelineInfo.depth_stencil_state.enable_depth_write = depthWrite;
            pipelineInfo.depth_stencil_state.compare_op = ToCompareOp(depthFunc);
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

        const int key = static_cast<int>(stride) * 1000000 + PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, colorFormat);
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

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = alphaTestVertexShader_;
        pipelineInfo.fragment_shader = alphaTestFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = 2;
        pipelineInfo.primitive_type = topology;
        pipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pipelineInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        pipelineInfo.depth_stencil_state.enable_depth_test = depthTest;
        pipelineInfo.depth_stencil_state.enable_depth_write = depthWrite;
        pipelineInfo.depth_stencil_state.compare_op = ToCompareOp(depthFunc);
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
        vsInfo.num_uniform_buffers = 1;
        dualTextureVertexShader_ = SDL_CreateGPUShader(device_, &vsInfo);
        if (dualTextureVertexShader_ == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create dual_texture3d vertex shader: ") + SDL_GetError());

        SDL_GPUShaderCreateInfo cvsInfo{};
        cvsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kDualTextureColored3dVertSpv);
        cvsInfo.code_size = Shaders::kDualTextureColored3dVertSpv_size;
        cvsInfo.entrypoint = "main";
        cvsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        cvsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        cvsInfo.num_uniform_buffers = 1;
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
        SDL_GPUTextureFormat colorFormat)
    {
        auto& cache = (stride == 24) ? dualTextureColoredPipelines_ : dualTexturePipelines_;
        const int key = PipelineCacheKey(topology, depthTest, depthWrite, depthFunc, colorFormat);
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

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = (stride == 24) ? dualTextureColoredVertexShader_ : dualTextureVertexShader_;
        pipelineInfo.fragment_shader = dualTextureFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = numAttrs;
        pipelineInfo.primitive_type = topology;
        pipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pipelineInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        pipelineInfo.depth_stencil_state.enable_depth_test = depthTest;
        pipelineInfo.depth_stencil_state.enable_depth_write = depthWrite;
        pipelineInfo.depth_stencil_state.compare_op = ToCompareOp(depthFunc);
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
        const Matrix wvp = world * view * projection;
        FillAlphaTestUniforms(command.uniforms, wvp, params);
        command.texture = static_cast<const SdlGpuTextureBackend*>(params.texture0);
        command.textureFilter = 0;
        command.addressU = 1;
        command.addressV = 1;

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
        const Matrix wvp = world * view * projection;
        FillExtUniforms(command.uniforms, wvp, params);
        command.texture0 = static_cast<const SdlGpuTextureBackend*>(params.texture0);
        command.texture1 = static_cast<const SdlGpuTextureBackend*>(params.texture1);
        command.textureFilter = 0;
        command.addressU = 1;
        command.addressV = 1;

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
        framePending_ = true;
    }

    void SdlGpuGraphicsBackend::RenderAlphaTestDraws(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                     const DrawTarget& target, SDL_GPUTextureFormat colorFormat)
    {
        for (const AlphaTestDrawCommand& command : alphaTestDrawCommands_)
        {
            if (command.uploadedVertexBuffer == nullptr || command.texture == nullptr || command.target != target)
                continue;

            SDL_GPUGraphicsPipeline* pipeline = GetOrCreatePipelineAlphaTest3D(
                command.stride, command.topology, command.depthTest, command.depthWrite, command.depthFunc, colorFormat);
            SDL_BindGPUGraphicsPipeline(pass, pipeline);
            SDL_PushGPUVertexUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));
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
    }

    void SdlGpuGraphicsBackend::RenderDualTextureDraws(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                       const DrawTarget& target, SDL_GPUTextureFormat colorFormat)
    {
        for (const DualTextureDrawCommand& command : dualTextureDrawCommands_)
        {
            if (command.uploadedVertexBuffer == nullptr || command.texture0 == nullptr || command.texture1 == nullptr
                || command.target != target)
                continue;

            SDL_GPUGraphicsPipeline* pipeline = GetOrCreatePipelineDualTexture3D(
                command.hasVertexColor ? 24 : 20, command.topology, command.depthTest, command.depthWrite, command.depthFunc, colorFormat);
            SDL_BindGPUGraphicsPipeline(pass, pipeline);
            SDL_PushGPUVertexUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));

            SDL_GPUBufferBinding vbBinding{};
            vbBinding.buffer = command.uploadedVertexBuffer;
            SDL_BindGPUVertexBuffers(pass, 0, &vbBinding, 1);

            SDL_GPUSampler* sampler = GetOrCreateSampler(command.textureFilter, command.addressU, command.addressV);
            SDL_GPUTextureSamplerBinding samplerBindings[2]{};
            samplerBindings[0].texture = command.texture0->Texture();
            samplerBindings[0].sampler = sampler;
            samplerBindings[1].texture = command.texture1->Texture();
            samplerBindings[1].sampler = sampler;
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
    }

    void SdlGpuGraphicsBackend::UploadSceneDrawData(SDL_GPUCommandBuffer* cmd)
    {
        if (coloredDrawCommands_.empty() && texturedDrawCommands_.empty() && litTexturedDrawCommands_.empty() &&
            alphaTestDrawCommands_.empty() && dualTextureDrawCommands_.empty())
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

        SDL_EndGPUCopyPass(copyPass);
    }

    void SdlGpuGraphicsBackend::RenderColoredDraws(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                   const DrawTarget& target, SDL_GPUTextureFormat colorFormat)
    {
        for (const ColoredDrawCommand& command : coloredDrawCommands_)
        {
            if (command.uploadedVertexBuffer == nullptr || command.target != target)
                continue;

            SDL_GPUGraphicsPipeline* pipeline = GetOrCreatePipelineColored3D(command.topology, command.depthTest,
                                                                              command.depthWrite, command.depthFunc, colorFormat);
            SDL_BindGPUGraphicsPipeline(pass, pipeline);
            SDL_PushGPUVertexUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));

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
    }

    void SdlGpuGraphicsBackend::RenderTexturedDraws(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                    const DrawTarget& target, SDL_GPUTextureFormat colorFormat)
    {
        for (const TexturedDrawCommand& command : texturedDrawCommands_)
        {
            if (command.uploadedVertexBuffer == nullptr || command.texture == nullptr || command.target != target)
                continue;

            SDL_GPUGraphicsPipeline* pipeline = command.hasVertexColor
                ? GetOrCreatePipelineColoredTextured3D(command.topology, command.depthTest, command.depthWrite, command.depthFunc, colorFormat)
                : GetOrCreatePipelineTextured3D(command.topology, command.depthTest, command.depthWrite, command.depthFunc, colorFormat);
            SDL_BindGPUGraphicsPipeline(pass, pipeline);
            SDL_PushGPUVertexUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));
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
    }

    void SdlGpuGraphicsBackend::RenderLitTexturedDraws(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                       const DrawTarget& target, SDL_GPUTextureFormat colorFormat)
    {
        for (const LitTexturedDrawCommand& command : litTexturedDrawCommands_)
        {
            if (command.uploadedVertexBuffer == nullptr || command.texture == nullptr || command.target != target)
                continue;

            SDL_GPUGraphicsPipeline* pipeline = GetOrCreatePipelineLitTextured3D(
                command.topology, command.depthTest, command.depthWrite, command.depthFunc, colorFormat);
            SDL_BindGPUGraphicsPipeline(pass, pipeline);
            SDL_PushGPUVertexUniformData(cmd, 0, command.uniforms.data(), sizeof(command.uniforms));
            SDL_PushGPUVertexUniformData(cmd, 1, command.lightUniforms.data(), sizeof(command.lightUniforms));
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
        // test wins over dual-texture (an AlphaTestEffect draw on a DualTextureEffect-shaped
        // buffer never reaches dual_texture3d); EnvironmentMapEffect/SkinnedEffect are not
        // implemented yet (plan_sdlgpu.md Phase SDLGPU-9/10) and fall through to plain BasicEffect.
        const bool needsAlphaTest = params.alphaTest[2] < 0.0f || params.alphaTest[3] < 0.0f;
        const bool needsDualTexture = !needsAlphaTest && params.dualTexture;
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

    // ---- SdlGpuRenderTargetBackend (Phase SDLGPU-8, SDLGPU-35) ----

    SdlGpuRenderTargetBackend::SdlGpuRenderTargetBackend(SdlGpuGraphicsBackend& owner, int width, int height,
                                                          int depthFormat, bool mipMap, int multiSampleCount)
        : owner_(&owner), width_(width), height_(height)
    {
        SDL_GPUDevice* device = owner_->Device();
        constexpr SDL_GPUTextureFormat kFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

        const SDL_GPUSampleCount sampleCount = ClampSampleCount(device, kFormat, multiSampleCount);
        multiSampleCount_ = SampleCountToInt(sampleCount);
        // MSAA and mip generation are mutually exclusive on the same attachment, same rationale
        // SdlGpuRenderTargetCubeBackend's own MSAA support already established.
        mipMap_ = mipMap && multiSampleCount_ == 0;

        SDL_GPUTextureCreateInfo colorInfo{};
        colorInfo.type = SDL_GPU_TEXTURETYPE_2D;
        colorInfo.format = kFormat;
        colorInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        colorInfo.width = static_cast<Uint32>(width);
        colorInfo.height = static_cast<Uint32>(height);
        colorInfo.layer_count_or_depth = 1;
        colorInfo.num_levels = mipMap_ ? static_cast<Uint32>(CalculateMipLevels(width, height)) : 1;
        colorInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;  // the sampleable texture itself is always single-sample

        colorTexture_ = SDL_CreateGPUTexture(device, &colorInfo);
        if (colorTexture_ == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create RenderTarget2D color texture: ") + SDL_GetError());

        if (multiSampleCount_ > 0)
        {
            // The real multisampled render target, resolved into colorTexture_ automatically via
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
            msaaTexture_ = SDL_CreateGPUTexture(device, &msaaInfo);
            if (msaaTexture_ == nullptr)
            {
                SDL_ReleaseGPUTexture(device, colorTexture_);
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
            depthTexture_ = SDL_CreateGPUTexture(device, &depthInfo);
            if (depthTexture_ == nullptr)
            {
                if (msaaTexture_ != nullptr) SDL_ReleaseGPUTexture(device, msaaTexture_);
                SDL_ReleaseGPUTexture(device, colorTexture_);
                throw std::runtime_error(std::string("CNA SDL_GPU: failed to create RenderTarget2D depth texture: ") + SDL_GetError());
            }
        }
    }

    SdlGpuRenderTargetBackend::~SdlGpuRenderTargetBackend()
    {
        if (owner_->currentRenderTarget_ == this)
            owner_->currentRenderTarget_ = nullptr;
        auto& used = owner_->usedRenderTargetsThisFrame_;
        used.erase(std::remove(used.begin(), used.end(), this), used.end());

        SDL_GPUDevice* device = owner_->Device();
        if (depthTexture_ != nullptr) SDL_ReleaseGPUTexture(device, depthTexture_);
        if (msaaTexture_ != nullptr) SDL_ReleaseGPUTexture(device, msaaTexture_);
        if (colorTexture_ != nullptr) SDL_ReleaseGPUTexture(device, colorTexture_);
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
        if (std::find(used.begin(), used.end(), this) == used.end())
            used.push_back(this);
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

        // Always downloads from colorTexture_ (the single-sample, sampleable texture) -- already
        // resolved-into by the time any frame's pass has run, even when this target is MSAA.
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureRegion region{};
        region.texture = colorTexture_;
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

    SdlGpuRenderTargetCubeBackend::SdlGpuRenderTargetCubeBackend(SdlGpuGraphicsBackend& owner, int size,
                                                                  int depthFormat, bool mipMap, int multiSampleCount)
        : owner_(&owner), size_(size), mipMap_(mipMap)
    {
        SDL_GPUDevice* device = owner_->Device();
        constexpr SDL_GPUTextureFormat kFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

        const SDL_GPUSampleCount sampleCount = ClampSampleCount(device, kFormat, multiSampleCount);
        multiSampleCount_ = SampleCountToInt(sampleCount);
        // MSAA and mip generation are mutually exclusive on the same attachment, same rationale
        // D3D12RenderTargetCubeBackend's own MSAA follow-up already established.
        mipMap_ = mipMap_ && multiSampleCount_ == 0;

        SDL_GPUTextureCreateInfo cubeInfo{};
        cubeInfo.type = SDL_GPU_TEXTURETYPE_CUBE;
        cubeInfo.format = kFormat;
        cubeInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        cubeInfo.width = static_cast<Uint32>(size);
        cubeInfo.height = static_cast<Uint32>(size);
        cubeInfo.layer_count_or_depth = 6;
        cubeInfo.num_levels = mipMap_ ? static_cast<Uint32>(CalculateMipLevels(size, size)) : 1;
        cubeInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;  // the cube texture itself is always single-sample
        cubeTexture_ = SDL_CreateGPUTexture(device, &cubeInfo);
        if (cubeTexture_ == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create RenderTargetCube color texture: ") + SDL_GetError());

        if (multiSampleCount_ > 0)
        {
            // SDL_GPU_TEXTURETYPE_CUBE has no multisampled variant -- the actual multisampled
            // render target is a plain 6-layer 2D array, resolved into cubeTexture_'s active face
            // automatically via SDL_GPUColorTargetInfo.resolve_texture/resolve_layer at render-pass
            // end (see RenderToTargetCubeFace) -- no manual ResolveSubresource-equivalent needed.
            SDL_GPUTextureCreateInfo msaaInfo{};
            msaaInfo.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
            msaaInfo.format = kFormat;
            msaaInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
            msaaInfo.width = static_cast<Uint32>(size);
            msaaInfo.height = static_cast<Uint32>(size);
            msaaInfo.layer_count_or_depth = 6;
            msaaInfo.num_levels = 1;
            msaaInfo.sample_count = sampleCount;
            msaaTexture_ = SDL_CreateGPUTexture(device, &msaaInfo);
            if (msaaTexture_ == nullptr)
            {
                SDL_ReleaseGPUTexture(device, cubeTexture_);
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
            depthTexture_ = SDL_CreateGPUTexture(device, &depthInfo);
            if (depthTexture_ == nullptr)
            {
                if (msaaTexture_ != nullptr) SDL_ReleaseGPUTexture(device, msaaTexture_);
                SDL_ReleaseGPUTexture(device, cubeTexture_);
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
        auto& used = owner_->usedRenderTargetCubeFacesThisFrame_;
        used.erase(std::remove_if(used.begin(), used.end(),
                                  [this](const auto& pair) { return pair.first == this; }),
                   used.end());

        SDL_GPUDevice* device = owner_->Device();
        if (depthTexture_ != nullptr) SDL_ReleaseGPUTexture(device, depthTexture_);
        if (msaaTexture_ != nullptr) SDL_ReleaseGPUTexture(device, msaaTexture_);
        if (cubeTexture_ != nullptr) SDL_ReleaseGPUTexture(device, cubeTexture_);
    }

    void SdlGpuRenderTargetCubeBackend::BindAsRenderTargetFace(int face)
    {
        owner_->currentRenderTargetCube_ = this;
        owner_->currentActiveCubeFace_ = face;
        // Mutually exclusive with a bound RenderTarget2D -- matches real XNA single-current-target
        // semantics (SetRenderTargetCubeFace always replaces whatever was there).
        owner_->currentRenderTarget_ = nullptr;
        auto& used = owner_->usedRenderTargetCubeFacesThisFrame_;
        const auto key = std::make_pair(this, face);
        if (std::find(used.begin(), used.end(), key) == used.end())
            used.push_back(key);
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
        region.texture = cubeTexture_;
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
        SDL_GPUDevice* device = owner_->Device();
        const Uint32 sizeBytes = static_cast<Uint32>(vertexCount) * static_cast<Uint32>(strideInBytes);
        if (buffer_ == nullptr || capacityBytes_ < sizeBytes)
        {
            if (buffer_ != nullptr)
                SDL_ReleaseGPUBuffer(device, buffer_);
            SDL_GPUBufferCreateInfo createInfo{};
            createInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
            createInfo.size = sizeBytes;
            buffer_ = SDL_CreateGPUBuffer(device, &createInfo);
            if (buffer_ == nullptr)
                throw std::runtime_error(std::string("CNA SDL_GPU: failed to create vertex buffer: ") + SDL_GetError());
            capacityBytes_ = sizeBytes;
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
        SDL_UploadToGPUBuffer(copyPass, &source, &destRegion, true);
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

    void SdlGpuIndexBufferBackend::SetData16(const void* data, int indexCount) { Upload(data, indexCount, false); }
    void SdlGpuIndexBufferBackend::SetData32(const void* data, int indexCount) { Upload(data, indexCount, true); }

    void SdlGpuIndexBufferBackend::Upload(const void* data, int indexCount, bool dataIsThirtyTwoBit)
    {
        SDL_GPUDevice* device = owner_->Device();
        const std::size_t elementSize = dataIsThirtyTwoBit ? sizeof(std::uint32_t) : sizeof(std::uint16_t);
        const Uint32 sizeBytes = static_cast<Uint32>(indexCount) * static_cast<Uint32>(elementSize);
        if (buffer_ == nullptr || capacityBytes_ < sizeBytes)
        {
            if (buffer_ != nullptr)
                SDL_ReleaseGPUBuffer(device, buffer_);
            SDL_GPUBufferCreateInfo createInfo{};
            createInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
            createInfo.size = sizeBytes;
            buffer_ = SDL_CreateGPUBuffer(device, &createInfo);
            if (buffer_ == nullptr)
                throw std::runtime_error(std::string("CNA SDL_GPU: failed to create index buffer: ") + SDL_GetError());
            capacityBytes_ = sizeBytes;
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
        SDL_UploadToGPUBuffer(copyPass, &source, &destRegion, true);
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
        if (effect != nullptr)
            throw std::runtime_error("CNA SDL_GPU: custom SpriteBatch effects are not implemented yet (see plan_sdlgpu.md)");
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
        owner_->QueueSprite(texture, nativeTexture, destinationRectangle, sourceRectangle, color, rotation,
                            origin, effects, layerDepth, transform_, textureFilter_, addressU_, addressV_);
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
