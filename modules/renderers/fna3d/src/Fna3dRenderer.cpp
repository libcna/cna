// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Fna3d/Fna3dRenderer.hpp"

#include "CNA/Internal/Renderers/Fna3d/Fna3dEnumMapping.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dCompiledEffect.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dSurfaceFormats.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dWindowFlags.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include "CNA/LogCategory.hpp"
#include "CNA/Logger.hpp"

#include "Fna3dStockEffectBlobs.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers::Fna3d
{
    namespace
    {
        void ForwardInfo(const char* message)
        {
            CNA::Logger::Info(std::string("FNA3D: ") + message, CNA::LogCategory::RENDER);
        }

        void ForwardWarn(const char* message)
        {
            CNA::Logger::Warn(std::string("FNA3D: ") + message, CNA::LogCategory::RENDER);
        }

        void ForwardError(const char* message)
        {
            CNA::Logger::Error(std::string("FNA3D: ") + message, CNA::LogCategory::RENDER);
        }

        /// FNA3D_HookLogFunctions stores the pointers unconditionally and its loggers call them
        /// without a null check, so muting means installing real no-ops, never nulls.
        void SwallowLog(const char* /*message*/) {}

        FNA3D_PresentInterval ToPresentInterval(int swapInterval)
        {
            switch (swapInterval)
            {
                case 0:  return FNA3D_PRESENTINTERVAL_IMMEDIATE;
                case 2:  return FNA3D_PRESENTINTERVAL_TWO;
                default: return FNA3D_PRESENTINTERVAL_ONE;
            }
        }
    }

    namespace Detail
    {
        std::uint64_t PrepareWindowFlags()
        {
            return static_cast<std::uint64_t>(FNA3D_PrepareWindowAttributes());
        }
    }

    // ---------------------------------------------------------------------------------------
    // Construction / teardown
    // ---------------------------------------------------------------------------------------

    Fna3dRenderer::Fna3dRenderer(const GraphicsRendererCreateArgs& args)
        : window_(args.window)
        , presentationMode_(args.presentationMode)
    {
        FNA3D_HookLogFunctions(ForwardInfo, ForwardWarn, ForwardError);

        // FNA3D is fetched and built from source at a pinned revision, so the linked library and
        // the headers CNA compiled against always agree -- but saying so is a one-call check, and
        // a mismatch (a system FNA3D picked up by an unusual link order) would otherwise surface
        // as unexplained struct-layout corruption rather than a clear message.
        const uint32_t linkedVersion = FNA3D_LinkedVersion();
        if (linkedVersion != FNA3D_COMPILED_VERSION)
        {
            throw std::runtime_error(
                "FNA3D renderer: the linked FNA3D reports version " +
                std::to_string(linkedVersion) + " but CNA was compiled against " +
                std::to_string(static_cast<uint32_t>(FNA3D_COMPILED_VERSION)) +
                ". The renderer refuses to run against a mismatched library, whose structure "
                "layouts it cannot assume.");
        }

        if (window_ == nullptr)
        {
            throw std::runtime_error(
                "FNA3D renderer: no SDL window was supplied. FNA3D presents into an OS window and "
                "has no headless device path.");
        }

        // GraphicsDevice already called FNA3D_PrepareWindowAttributes() before creating the
        // window (Detail::PrepareWindowFlags), which is also what selected the driver. Calling it
        // again here would re-run driver selection after the window's visual is fixed.
        int windowWidth = 0;
        int windowHeight = 0;
        SDL_GetWindowSizeInPixels(window_, &windowWidth, &windowHeight);

        int logicalWidth = 0;
        int logicalHeight = 0;
        ResolveLogicalSize(presentationMode_,
                           args.virtualWidth > 0 ? args.virtualWidth : windowWidth,
                           args.virtualHeight > 0 ? args.virtualHeight : windowHeight,
                           windowWidth, windowHeight, logicalWidth, logicalHeight);

        std::memset(&presentation_, 0, sizeof(presentation_));
        presentation_.backBufferWidth = logicalWidth;
        presentation_.backBufferHeight = logicalHeight;
        presentation_.backBufferFormat = ToFna3dSurfaceFormat(args.backBufferFormat);
        presentation_.multiSampleCount = args.multiSampleCount > 1 ? args.multiSampleCount : 0;
        presentation_.deviceWindowHandle = window_;
        presentation_.isFullScreen = args.isFullScreen ? 1 : 0;
        // A depth/stencil buffer that does not exist cannot be cleared, tested or written, and
        // XNA's own default PresentationParameters leave DepthFormat at None only for a device a
        // game then never draws 3D with. CNA's GraphicsDevice frequently constructs the renderer
        // before GraphicsDeviceManager.ApplyChanges() has propagated a preference, so a None here
        // would permanently deny depth to every 3D game that sets the preference late.
        // Depth24Stencil8 is the XNA HiDef default and what UpdatePresentationFormatEXT will
        // narrow if the game genuinely asks for less.
        presentation_.depthStencilFormat =
            args.depthStencilFormat == 0 ? FNA3D_DEPTHFORMAT_D24S8
                                         : ToFna3dDepthFormat(args.depthStencilFormat);
        presentation_.presentationInterval = ToPresentInterval(args.swapInterval);
        presentation_.displayOrientation = FNA3D_DISPLAYORIENTATION_DEFAULT;
        presentation_.renderTargetUsage = FNA3D_RENDERTARGETUSAGE_DISCARDCONTENTS;

        device_ = FNA3D_CreateDevice(&presentation_, 0);
        if (device_ == nullptr)
        {
            throw std::runtime_error(
                "FNA3D renderer: FNA3D_CreateDevice failed -- no FNA3D driver (SDL_GPU, Direct3D "
                "11 or OpenGL) could create a device for this window. Set the FNA3D_FORCE_DRIVER "
                "hint to pin a specific driver.");
        }

        RecomputeLayout();

        // XNA's device defaults: BlendState.Opaque, DepthStencilState.Default,
        // RasterizerState.CullCounterClockwise, SamplerState.LinearWrap. Applying them here means
        // the very first draw sees a defined pipeline even if the game never touches state.
        blendState_.colorSourceBlend = FNA3D_BLEND_ONE;
        blendState_.colorDestinationBlend = FNA3D_BLEND_ZERO;
        blendState_.colorBlendFunction = FNA3D_BLENDFUNCTION_ADD;
        blendState_.alphaSourceBlend = FNA3D_BLEND_ONE;
        blendState_.alphaDestinationBlend = FNA3D_BLEND_ZERO;
        blendState_.alphaBlendFunction = FNA3D_BLENDFUNCTION_ADD;
        blendState_.colorWriteEnable = FNA3D_COLORWRITECHANNELS_ALL;
        blendState_.colorWriteEnable1 = FNA3D_COLORWRITECHANNELS_ALL;
        blendState_.colorWriteEnable2 = FNA3D_COLORWRITECHANNELS_ALL;
        blendState_.colorWriteEnable3 = FNA3D_COLORWRITECHANNELS_ALL;
        blendState_.blendFactor = FNA3D_Color{ 255, 255, 255, 255 };
        blendState_.multiSampleMask = -1;

        depthStencilState_.depthBufferEnable = 1;
        depthStencilState_.depthBufferWriteEnable = 1;
        depthStencilState_.depthBufferFunction = FNA3D_COMPAREFUNCTION_LESSEQUAL;
        depthStencilState_.stencilEnable = 0;
        depthStencilState_.stencilMask = -1;
        depthStencilState_.stencilWriteMask = -1;
        depthStencilState_.twoSidedStencilMode = 0;
        depthStencilState_.stencilFail = FNA3D_STENCILOPERATION_KEEP;
        depthStencilState_.stencilDepthBufferFail = FNA3D_STENCILOPERATION_KEEP;
        depthStencilState_.stencilPass = FNA3D_STENCILOPERATION_KEEP;
        depthStencilState_.stencilFunction = FNA3D_COMPAREFUNCTION_ALWAYS;
        depthStencilState_.ccwStencilFail = FNA3D_STENCILOPERATION_KEEP;
        depthStencilState_.ccwStencilDepthBufferFail = FNA3D_STENCILOPERATION_KEEP;
        depthStencilState_.ccwStencilPass = FNA3D_STENCILOPERATION_KEEP;
        depthStencilState_.ccwStencilFunction = FNA3D_COMPAREFUNCTION_ALWAYS;
        depthStencilState_.referenceStencil = 0;

        rasterizerState_.fillMode = FNA3D_FILLMODE_SOLID;
        rasterizerState_.cullMode = FNA3D_CULLMODE_CULLCOUNTERCLOCKWISEFACE;
        rasterizerState_.depthBias = 0.0f;
        rasterizerState_.slopeScaleDepthBias = 0.0f;
        rasterizerState_.scissorTestEnable = 0;
        rasterizerState_.multiSampleAntiAlias = 1;

        for (FNA3D_SamplerState& sampler : samplerStates_)
        {
            sampler.filter = FNA3D_TEXTUREFILTER_LINEAR;
            sampler.addressU = FNA3D_TEXTUREADDRESSMODE_WRAP;
            sampler.addressV = FNA3D_TEXTUREADDRESSMODE_WRAP;
            sampler.addressW = FNA3D_TEXTUREADDRESSMODE_WRAP;
            sampler.mipMapLevelOfDetailBias = 0.0f;
            sampler.maxAnisotropy = 4;
            sampler.maxMipLevel = 0;
        }
        for (FNA3D_SamplerState& sampler : vertexSamplerStates_)
        {
            sampler.filter = FNA3D_TEXTUREFILTER_LINEAR;
            sampler.addressU = FNA3D_TEXTUREADDRESSMODE_WRAP;
            sampler.addressV = FNA3D_TEXTUREADDRESSMODE_WRAP;
            sampler.addressW = FNA3D_TEXTUREADDRESSMODE_WRAP;
            sampler.mipMapLevelOfDetailBias = 0.0f;
            sampler.maxAnisotropy = 4;
            sampler.maxMipLevel = 0;
        }

        FNA3D_SetBlendState(device_, &blendState_);
        FNA3D_SetDepthStencilState(device_, &depthStencilState_);
        FNA3D_ApplyRasterizerState(device_, &rasterizerState_);

        FNA3D_Viewport viewport{};
        viewport.x = 0;
        viewport.y = 0;
        viewport.w = presentation_.backBufferWidth;
        viewport.h = presentation_.backBufferHeight;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        FNA3D_SetViewport(device_, &viewport);

        using namespace StockEffectBlobs;
        effects_[static_cast<std::size_t>(StockEffectKind::Sprite)]
            .Load(device_, kSpriteEffectFxb, sizeof(kSpriteEffectFxb), "SpriteEffect");
        effects_[static_cast<std::size_t>(StockEffectKind::Basic)]
            .Load(device_, kBasicEffectFxb, sizeof(kBasicEffectFxb), "BasicEffect");
        effects_[static_cast<std::size_t>(StockEffectKind::AlphaTest)]
            .Load(device_, kAlphaTestEffectFxb, sizeof(kAlphaTestEffectFxb), "AlphaTestEffect");
        effects_[static_cast<std::size_t>(StockEffectKind::DualTexture)]
            .Load(device_, kDualTextureEffectFxb, sizeof(kDualTextureEffectFxb),
                  "DualTextureEffect");
        effects_[static_cast<std::size_t>(StockEffectKind::EnvironmentMap)]
            .Load(device_, kEnvironmentMapEffectFxb, sizeof(kEnvironmentMapEffectFxb),
                  "EnvironmentMapEffect");
        effects_[static_cast<std::size_t>(StockEffectKind::Skinned)]
            .Load(device_, kSkinnedEffectFxb, sizeof(kSkinnedEffectFxb), "SkinnedEffect");

        ProbeTexture3DReadbackSupport();
        QueryDriverLimits();
        ProbeCompressedReadbackSupport();

        IGraphicsRenderer::RegisterForWindow(window_, this);
    }

    void Fna3dRenderer::QueryDriverLimits()
    {
        // FNA3D answers all of these per running driver, and every one of them changes what this
        // renderer may honestly accept. Asking once at device creation keeps the answers out of
        // the per-resource path while still making them the driver's answers rather than CNA's
        // assumptions.
        supportsDxt1_ = FNA3D_SupportsDXT1(device_) != 0;
        supportsS3tc_ = FNA3D_SupportsS3TC(device_) != 0;
        supportsBc7_ = FNA3D_SupportsBC7(device_) != 0;
        supportsSrgbRenderTargets_ = FNA3D_SupportsSRGBRenderTargets(device_) != 0;

        int32_t textureSlots = 0;
        int32_t vertexTextureSlots = 0;
        FNA3D_GetMaxTextureSlots(device_, &textureSlots, &vertexTextureSlots);
        maxTextureSlots_ = static_cast<int>(textureSlots);
        maxVertexTextureSlots_ = static_cast<int>(vertexTextureSlots);

        CNA::Logger::Info(
            "FNA3D: driver limits -- DXT1 " + std::string(supportsDxt1_ ? "yes" : "no") +
                ", S3TC " + (supportsS3tc_ ? "yes" : "no") +
                ", BC7 " + (supportsBc7_ ? "yes" : "no") +
                ", sRGB render targets " + (supportsSrgbRenderTargets_ ? "yes" : "no") +
                ", texture slots " + std::to_string(maxTextureSlots_) +
                " (vertex " + std::to_string(maxVertexTextureSlots_) + ")",
            CNA::LogCategory::RENDER);
    }

    bool Fna3dRenderer::SupportsTextureFormatEXT(int surfaceFormat) const
    {
        using Xna = Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        switch (static_cast<Xna>(surfaceFormat))
        {
            case Xna::Dxt1:
                // FNA3D reports DXT1 separately because some drivers expose the DXT1-only subset.
                return supportsDxt1_ || supportsS3tc_;
            case Xna::Dxt3:
            case Xna::Dxt5:
            case Xna::Dxt5SrgbEXT:
                return supportsS3tc_;
            case Xna::Bc7EXT:
            case Xna::Bc7SrgbEXT:
                return supportsBc7_;
            default:
                // Every uncompressed format FNA3D's enumeration has is a format its drivers
                // create; only the compressed families are optional.
                return true;
        }
    }

    void Fna3dRenderer::RequireTextureFormatSupportedEXT(int surfaceFormat, const char* what) const
    {
        if (SupportsTextureFormatEXT(surfaceFormat))
        {
            return;
        }
        throw std::runtime_error(
            std::string("FNA3D renderer: the running driver reports no support for the ") +
            "block-compressed surface format ordinal " + std::to_string(surfaceFormat) +
            " requested for a " + what + ", so the texture is refused rather than created with "
            "content it cannot sample.");
    }

    void Fna3dRenderer::RequireRenderTargetFormatSupportedEXT(int surfaceFormat) const
    {
        using Xna = Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        const Xna format = static_cast<Xna>(surfaceFormat);
        const bool wantsSrgb = format == Xna::ColorSrgbEXT || format == Xna::Dxt5SrgbEXT ||
                               format == Xna::Bc7SrgbEXT;
        if (wantsSrgb && !supportsSrgbRenderTargets_)
        {
            throw std::runtime_error(
                "FNA3D renderer: the running driver reports no sRGB render-target support, so an "
                "sRGB render target is refused rather than created as a linear one that would "
                "silently render with the wrong gamma.");
        }
        // A render target is something the GPU writes; no driver renders into a block-compressed
        // surface, so such a request is refused here rather than handed to FNA3D.
        if (IsBlockCompressedFormat(surfaceFormat))
        {
            throw std::runtime_error(
                "FNA3D renderer: RenderTarget2D does not accept a block-compressed surface "
                "format -- no driver renders into compressed blocks.");
        }
        RequireTextureFormatSupportedEXT(surfaceFormat, "RenderTarget2D");
    }

    void Fna3dRenderer::ProbeTexture3DReadbackSupport()
    {
        // FNA3D's drivers disagree here and expose no query: the OpenGL driver implements no
        // GetTextureData3D at all (it logs "GetTextureData3D is unsupported!" and writes nothing),
        // while the SDL_GPU and Direct3D 11 drivers do. Rather than hard-code a driver name, this
        // measures it once with a 1x1x1 volume: upload a known value, read it back into a buffer
        // pre-filled with a different one, and see whether the readback actually happened.
        // Fna3dTexture3DRenderer mirrors uploads only when the answer is no.
        constexpr std::uint8_t kUploaded[4] = { 0x12, 0x34, 0x56, 0x78 };
        std::uint8_t readback[4] = { 0xAB, 0xAB, 0xAB, 0xAB };

        FNA3D_Texture* probe =
            FNA3D_CreateTexture3D(device_, FNA3D_SURFACEFORMAT_COLOR, 1, 1, 1, 1);
        if (probe == nullptr)
        {
            texture3DReadbackSupported_ = false;
            return;
        }

        FNA3D_SetTextureData3D(device_, probe, 0, 0, 0, 1, 1, 1, 0,
                               const_cast<std::uint8_t*>(kUploaded), sizeof(kUploaded));

        // A driver that refuses reports it through FNA3D's error log; that is expected here and
        // is not a CNA failure, so the hooks are muted for the duration of the probe and the
        // result is reported once, as information, below.
        FNA3D_HookLogFunctions(SwallowLog, SwallowLog, SwallowLog);
        FNA3D_GetTextureData3D(device_, probe, 0, 0, 0, 1, 1, 1, 0, readback, sizeof(readback));
        FNA3D_HookLogFunctions(ForwardInfo, ForwardWarn, ForwardError);

        FNA3D_AddDisposeTexture(device_, probe);

        texture3DReadbackSupported_ = std::memcmp(kUploaded, readback, sizeof(kUploaded)) == 0;
        CNA::Logger::Info(
            texture3DReadbackSupported_
                ? "FNA3D: the selected driver reads volume textures back; Texture3D::GetData is "
                  "served by the GPU."
                : "FNA3D: the selected driver implements no volume-texture readback; "
                  "Texture3D::GetData is served from this renderer's own upload mirror.",
            CNA::LogCategory::RENDER);
    }

    void Fna3dRenderer::ProbeCompressedReadbackSupport()
    {
        // Same shape as the volume-readback probe above, for the same reason: FNA3D's OpenGL and
        // Direct3D 11 drivers both refuse GetTextureData2D on a block-compressed texture (they log
        // "GetData with compressed textures unsupported!" and write nothing), while the SDL_GPU
        // driver forwards it, and FNA3D exposes no query to tell them apart. Reporting the region
        // as read when the driver wrote nothing would hand the caller a buffer of whatever it
        // happened to contain, which is exactly what ITextureRenderer::GetData forbids, so this is
        // measured rather than assumed. Unlike the volume case there is no mirror to fall back on:
        // the answer decides whether GetData refuses.
        compressedReadbackSupported_ = false;
        if (!supportsDxt1_ && !supportsS3tc_)
        {
            return;
        }

        // One 4x4 DXT1 block: c0 = 0xF800 (red), c1 = 0, all indices 0.
        constexpr std::uint8_t kUploaded[8] = { 0x00, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
        std::uint8_t readback[8] = { 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB };

        FNA3D_Texture* probe =
            FNA3D_CreateTexture2D(device_, FNA3D_SURFACEFORMAT_DXT1, 4, 4, 1, 0);
        if (probe == nullptr)
        {
            return;
        }

        FNA3D_SetTextureData2D(device_, probe, 0, 0, 4, 4, 0,
                               const_cast<std::uint8_t*>(kUploaded), sizeof(kUploaded));

        FNA3D_HookLogFunctions(SwallowLog, SwallowLog, SwallowLog);
        FNA3D_GetTextureData2D(device_, probe, 0, 0, 4, 4, 0, readback, sizeof(readback));
        FNA3D_HookLogFunctions(ForwardInfo, ForwardWarn, ForwardError);

        FNA3D_AddDisposeTexture(device_, probe);

        compressedReadbackSupported_ = std::memcmp(kUploaded, readback, sizeof(kUploaded)) == 0;
        CNA::Logger::Info(
            compressedReadbackSupported_
                ? "FNA3D: the selected driver reads block-compressed textures back."
                : "FNA3D: the selected driver implements no block-compressed readback; GetData on "
                  "a compressed texture reports that it read nothing.",
            CNA::LogCategory::RENDER);
    }

    Fna3dRenderer::~Fna3dRenderer()
    {
        if (window_ != nullptr)
        {
            IGraphicsRenderer::UnregisterForWindow(window_);
        }
        if (device_ == nullptr)
        {
            return;
        }

        spriteVertexBuffer_.reset();
        spriteIndexBuffer_.reset();
        for (Fna3dStockEffect& effect : effects_)
        {
            effect.Destroy(device_);
        }
        FNA3D_DestroyDevice(device_);
        device_ = nullptr;
    }

    // ---------------------------------------------------------------------------------------
    // Presentation
    // ---------------------------------------------------------------------------------------

    void Fna3dRenderer::RecomputeLayout()
    {
        int windowWidth = 0;
        int windowHeight = 0;
        if (window_ != nullptr)
        {
            SDL_GetWindowSizeInPixels(window_, &windowWidth, &windowHeight);
        }
        layout_ = ComputePresentationLayout(presentationMode_, presentation_.backBufferWidth,
                                            presentation_.backBufferHeight, windowWidth,
                                            windowHeight);
    }

    void Fna3dRenderer::ResetBackbuffer()
    {
        FNA3D_ResetBackbuffer(device_, &presentation_);
        RecomputeLayout();
    }

    void Fna3dRenderer::Present()
    {
        RecomputeLayout();

        FNA3D_Rect destination{};
        destination.x = layout_.destinationX;
        destination.y = layout_.destinationY;
        destination.w = layout_.destinationWidth;
        destination.h = layout_.destinationHeight;

        FNA3D_SwapBuffers(device_, nullptr, &destination, window_);
    }

    void Fna3dRenderer::GetViewportSize(int& width, int& height)
    {
        width = presentation_.backBufferWidth;
        height = presentation_.backBufferHeight;
    }

    void Fna3dRenderer::GetDefaultViewportRect(int& x, int& y, int& width, int& height)
    {
        // The GPU viewport lives in back-buffer space, not window space: FNA3D scales the whole
        // back buffer into the window at present time, so the default viewport is always the full
        // back buffer regardless of which presentation mode is active.
        x = 0;
        y = 0;
        width = presentation_.backBufferWidth;
        height = presentation_.backBufferHeight;
    }

    void Fna3dRenderer::SetVirtualResolution(int width, int height)
    {
        int windowWidth = 0;
        int windowHeight = 0;
        if (window_ != nullptr)
        {
            SDL_GetWindowSizeInPixels(window_, &windowWidth, &windowHeight);
        }

        int logicalWidth = 0;
        int logicalHeight = 0;
        ResolveLogicalSize(presentationMode_, width, height, windowWidth, windowHeight,
                           logicalWidth, logicalHeight);
        if (logicalWidth == presentation_.backBufferWidth &&
            logicalHeight == presentation_.backBufferHeight)
        {
            RecomputeLayout();
            return;
        }

        presentation_.backBufferWidth = logicalWidth;
        presentation_.backBufferHeight = logicalHeight;
        ResetBackbuffer();
    }

    void Fna3dRenderer::SetPresentationMode(int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
        RecomputeLayout();
    }

    void Fna3dRenderer::SetSwapInterval(int interval)
    {
        const FNA3D_PresentInterval resolved = ToPresentInterval(interval);
        if (resolved == presentation_.presentationInterval)
        {
            return;
        }
        presentation_.presentationInterval = resolved;
        ResetBackbuffer();
    }

    int Fna3dRenderer::ClampMultiSampleCount(int requested, int format) const
    {
        if (requested <= 1)
        {
            return 0;
        }
        const int deviceMax = FNA3D_GetMaxMultiSampleCount(
            device_, static_cast<FNA3D_SurfaceFormat>(format), requested);
        return deviceMax > 1 ? deviceMax : 0;
    }

    int Fna3dRenderer::ApplyMultiSampleCount(int requestedMultiSampleCount)
    {
        const int applied =
            ClampMultiSampleCount(requestedMultiSampleCount, presentation_.backBufferFormat);
        if (applied == presentation_.multiSampleCount)
        {
            return FNA3D_GetBackbufferMultiSampleCount(device_);
        }
        presentation_.multiSampleCount = applied;
        ResetBackbuffer();
        return FNA3D_GetBackbufferMultiSampleCount(device_);
    }

    void Fna3dRenderer::UpdatePresentationFormatEXT(int backBufferFormat, int depthStencilFormat,
                                                    bool isFullScreen)
    {
        const FNA3D_SurfaceFormat newColor = ToFna3dSurfaceFormat(backBufferFormat);
        const FNA3D_DepthFormat newDepth = ToFna3dDepthFormat(depthStencilFormat);
        const uint8_t newFullScreen = isFullScreen ? 1 : 0;

        if (newColor == presentation_.backBufferFormat &&
            newDepth == presentation_.depthStencilFormat &&
            newFullScreen == presentation_.isFullScreen)
        {
            return;
        }

        presentation_.backBufferFormat = newColor;
        presentation_.depthStencilFormat = newDepth;
        presentation_.isFullScreen = newFullScreen;
        ResetBackbuffer();
    }

    int Fna3dRenderer::GetAppliedBackBufferFormatEXT(int /*requestedFormat*/) const
    {
        return static_cast<int>(FNA3D_GetBackbufferSurfaceFormat(device_));
    }

    int Fna3dRenderer::GetAppliedDepthStencilFormatEXT(int /*requestedFormat*/) const
    {
        return static_cast<int>(FNA3D_GetBackbufferDepthFormat(device_));
    }

    int Fna3dRenderer::GetMultiSampleCount() const
    {
        return FNA3D_GetBackbufferMultiSampleCount(device_);
    }

    bool Fna3dRenderer::TransformWindowToLogical(float windowX, float windowY, float& logX,
                                                 float& logY) const
    {
        return WindowToLogical(layout_, windowX, windowY, logX, logY);
    }

    bool Fna3dRenderer::TransformLogicalToWindow(float logX, float logY, float& windowX,
                                                 float& windowY) const
    {
        return LogicalToWindow(layout_, logX, logY, windowX, windowY);
    }

    void Fna3dRenderer::ReadBackbuffer(int x, int y, int w, int h, std::uint8_t* pixels)
    {
        if (pixels == nullptr || w <= 0 || h <= 0)
        {
            return;
        }

        // FNA3D_ReadBackbuffer's sub-rectangle origin is NOT the same on every driver: the OpenGL
        // driver forwards (x, y) straight to glReadPixels, whose origin is the BOTTOM-left of the
        // backbuffer, and only afterwards flips the returned rows -- so a full-frame read comes
        // back correctly top-row-first while a sub-rectangle read comes back from the mirrored
        // row range. The D3D11 driver, which routes the read through GetTextureData2D, uses the
        // top-left origin instead. Measured, not assumed: fna3d-spike/fna3d_sprite_spike.c
        // renders a quad at rows 12..35 and a full-frame read finds it there, while the
        // equivalent sub-rectangle read returns the cleared colour.
        //
        // CNA's own contract is unambiguous ("x, y are top-left in game coordinates") and shared
        // pixel tests depend on it, so this reads the whole backbuffer -- which every driver
        // agrees is top-row-first -- and crops the requested rectangle out of it. Readback is
        // already a full CPU/GPU sync point that FNA3D documents as screenshot-only, so the extra
        // bandwidth buys driver-independent correctness at a cost nothing hot pays.
        const int backbufferWidth = presentation_.backBufferWidth;
        const int backbufferHeight = presentation_.backBufferHeight;
        if (backbufferWidth <= 0 || backbufferHeight <= 0)
        {
            return;
        }

        readbackScratch_.resize(static_cast<std::size_t>(backbufferWidth) * backbufferHeight * 4);
        FNA3D_ReadBackbuffer(device_, 0, 0, backbufferWidth, backbufferHeight,
                             readbackScratch_.data(),
                             static_cast<int32_t>(readbackScratch_.size()));

        for (int row = 0; row < h; ++row)
        {
            std::uint8_t* destination = pixels + static_cast<std::size_t>(row) * w * 4;
            const int sourceRow = y + row;
            if (sourceRow < 0 || sourceRow >= backbufferHeight)
            {
                std::memset(destination, 0, static_cast<std::size_t>(w) * 4);
                continue;
            }
            for (int column = 0; column < w; ++column)
            {
                const int sourceColumn = x + column;
                std::uint8_t* destinationPixel = destination + static_cast<std::size_t>(column) * 4;
                if (sourceColumn < 0 || sourceColumn >= backbufferWidth)
                {
                    std::memset(destinationPixel, 0, 4);
                    continue;
                }
                const std::size_t sourceOffset =
                    (static_cast<std::size_t>(sourceRow) * backbufferWidth + sourceColumn) * 4;
                std::memcpy(destinationPixel, readbackScratch_.data() + sourceOffset, 4);
            }
        }
    }

    // ---------------------------------------------------------------------------------------
    // Clears
    // ---------------------------------------------------------------------------------------

    void Fna3dRenderer::ClearInternal(FNA3D_ClearOptions options, const FNA3D_Vec4* color,
                                      float depth, int stencil)
    {
        FNA3D_Vec4 local{};
        if (color != nullptr)
        {
            local = *color;
        }
        FNA3D_Clear(device_, options, color != nullptr ? &local : nullptr, depth, stencil);
    }

    void Fna3dRenderer::Clear(float r, float g, float b, float a)
    {
        const FNA3D_Vec4 color{ r, g, b, a };
        ClearInternal(FNA3D_CLEAROPTIONS_TARGET, &color, 1.0f, 0);
    }

    void Fna3dRenderer::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        const FNA3D_Vec4 color{ r, g, b, a };
        ClearInternal(
            static_cast<FNA3D_ClearOptions>(FNA3D_CLEAROPTIONS_TARGET |
                                            FNA3D_CLEAROPTIONS_DEPTHBUFFER),
            &color, depth, 0);
    }

    void Fna3dRenderer::ClearDepth(float depth)
    {
        ClearInternal(FNA3D_CLEAROPTIONS_DEPTHBUFFER, nullptr, depth, 0);
    }

    void Fna3dRenderer::ClearStencil(int stencil)
    {
        ClearInternal(FNA3D_CLEAROPTIONS_STENCIL, nullptr, 1.0f, stencil);
    }

    void Fna3dRenderer::ClearDepthAndStencil(float depth, int stencil)
    {
        ClearInternal(
            static_cast<FNA3D_ClearOptions>(FNA3D_CLEAROPTIONS_DEPTHBUFFER |
                                            FNA3D_CLEAROPTIONS_STENCIL),
            nullptr, depth, stencil);
    }

    void Fna3dRenderer::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        const FNA3D_Vec4 color{ r, g, b, a };
        ClearInternal(
            static_cast<FNA3D_ClearOptions>(FNA3D_CLEAROPTIONS_TARGET | FNA3D_CLEAROPTIONS_STENCIL),
            &color, 1.0f, stencil);
    }

    void Fna3dRenderer::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth,
                                                  int stencil)
    {
        const FNA3D_Vec4 color{ r, g, b, a };
        ClearInternal(
            static_cast<FNA3D_ClearOptions>(FNA3D_CLEAROPTIONS_TARGET |
                                            FNA3D_CLEAROPTIONS_DEPTHBUFFER |
                                            FNA3D_CLEAROPTIONS_STENCIL),
            &color, depth, stencil);
    }

    // ---------------------------------------------------------------------------------------
    // Graphics state
    // ---------------------------------------------------------------------------------------

    void Fna3dRenderer::ApplyCurrentBlendState()
    {
        FNA3D_SetBlendState(device_, &blendState_);
    }

    void Fna3dRenderer::ApplyCurrentDepthStencilState()
    {
        FNA3D_SetDepthStencilState(device_, &depthStencilState_);
    }

    void Fna3dRenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend, int colorDstBlend,
                                        int alphaDstBlend, int colorBlendFunc, int alphaBlendFunc,
                                        const BlendWriteState& writeState)
    {
        blendState_.colorSourceBlend = ToFna3dBlend(colorSrcBlend);
        blendState_.colorDestinationBlend = ToFna3dBlend(colorDstBlend);
        blendState_.colorBlendFunction = ToFna3dBlendFunction(colorBlendFunc);
        blendState_.alphaSourceBlend = ToFna3dBlend(alphaSrcBlend);
        blendState_.alphaDestinationBlend = ToFna3dBlend(alphaDstBlend);
        blendState_.alphaBlendFunction = ToFna3dBlendFunction(alphaBlendFunc);
        blendState_.colorWriteEnable = ToFna3dColorWriteChannels(writeState.colorWriteChannels[0]);
        blendState_.colorWriteEnable1 = ToFna3dColorWriteChannels(writeState.colorWriteChannels[1]);
        blendState_.colorWriteEnable2 = ToFna3dColorWriteChannels(writeState.colorWriteChannels[2]);
        blendState_.colorWriteEnable3 = ToFna3dColorWriteChannels(writeState.colorWriteChannels[3]);
        blendState_.multiSampleMask = static_cast<int32_t>(writeState.multiSampleMask);
        ApplyCurrentBlendState();
    }

    void Fna3dRenderer::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                               int depthFunc, bool stencilEnable, int stencilFunc,
                                               int stencilPass, int stencilFail,
                                               int stencilDepthFail, int stencilMask,
                                               int stencilWriteMask, int referenceStencil,
                                               bool twoSidedStencilMode, int ccwStencilFunc,
                                               int ccwStencilPass, int ccwStencilFail,
                                               int ccwStencilDepthFail)
    {
        depthStencilState_.depthBufferEnable = depthEnable ? 1 : 0;
        depthStencilState_.depthBufferWriteEnable = depthWriteEnable ? 1 : 0;
        depthStencilState_.depthBufferFunction = ToFna3dCompareFunction(depthFunc);
        depthStencilState_.stencilEnable = stencilEnable ? 1 : 0;
        depthStencilState_.stencilMask = stencilMask;
        depthStencilState_.stencilWriteMask = stencilWriteMask;
        depthStencilState_.twoSidedStencilMode = twoSidedStencilMode ? 1 : 0;
        depthStencilState_.stencilFail = ToFna3dStencilOperation(stencilFail);
        depthStencilState_.stencilDepthBufferFail = ToFna3dStencilOperation(stencilDepthFail);
        depthStencilState_.stencilPass = ToFna3dStencilOperation(stencilPass);
        depthStencilState_.stencilFunction = ToFna3dCompareFunction(stencilFunc);
        depthStencilState_.ccwStencilFail = ToFna3dStencilOperation(ccwStencilFail);
        depthStencilState_.ccwStencilDepthBufferFail =
            ToFna3dStencilOperation(ccwStencilDepthFail);
        depthStencilState_.ccwStencilPass = ToFna3dStencilOperation(ccwStencilPass);
        depthStencilState_.ccwStencilFunction = ToFna3dCompareFunction(ccwStencilFunc);
        depthStencilState_.referenceStencil = referenceStencil;
        ApplyCurrentDepthStencilState();
    }

    void Fna3dRenderer::ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                             float depthBias, float slopeScaleDepthBias)
    {
        rasterizerState_.cullMode = ToFna3dCullMode(cullMode);
        rasterizerState_.fillMode = ToFna3dFillMode(fillMode);
        rasterizerState_.scissorTestEnable = scissorTestEnable ? 1 : 0;
        rasterizerState_.depthBias = depthBias;
        rasterizerState_.slopeScaleDepthBias = slopeScaleDepthBias;
        FNA3D_ApplyRasterizerState(device_, &rasterizerState_);
    }

    void Fna3dRenderer::ApplySamplerState(int slot, int filter, int addressU, int addressV,
                                          int maxAnisotropy)
    {
        if (slot < 0 || slot >= static_cast<int>(samplerStates_.size()))
        {
            return;
        }
        FNA3D_SamplerState& sampler = samplerStates_[static_cast<std::size_t>(slot)];
        sampler.filter = ToFna3dTextureFilter(filter);
        sampler.addressU = ToFna3dTextureAddressMode(addressU);
        sampler.addressV = ToFna3dTextureAddressMode(addressV);
        sampler.maxAnisotropy = maxAnisotropy > 0 ? maxAnisotropy : 1;
    }

    FNA3D_SamplerState Fna3dRenderer::GetSamplerStateEXT(int slot) const
    {
        if (slot < 0 || slot >= static_cast<int>(samplerStates_.size()))
        {
            return FNA3D_SamplerState{};
        }
        return samplerStates_[static_cast<std::size_t>(slot)];
    }

    void Fna3dRenderer::ApplySamplerAddressW(int slot, int addressW)
    {
        if (slot < 0 || slot >= static_cast<int>(samplerStates_.size()))
        {
            return;
        }
        samplerStates_[static_cast<std::size_t>(slot)].addressW =
            ToFna3dTextureAddressMode(addressW);
    }

    void Fna3dRenderer::ApplySamplerMipState(int slot, int maxMipLevel, float lodBias)
    {
        if (slot < 0 || slot >= static_cast<int>(samplerStates_.size()))
        {
            return;
        }
        FNA3D_SamplerState& sampler = samplerStates_[static_cast<std::size_t>(slot)];
        sampler.maxMipLevel = maxMipLevel;
        sampler.mipMapLevelOfDetailBias = lodBias;
    }

    void Fna3dRenderer::SetBlendFactor(float r, float g, float b, float a)
    {
        const auto quantize = [](float value) -> std::uint8_t {
            const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
            return static_cast<std::uint8_t>(clamped * 255.0f + 0.5f);
        };
        FNA3D_Color factor{ quantize(r), quantize(g), quantize(b), quantize(a) };
        blendState_.blendFactor = factor;
        FNA3D_SetBlendFactor(device_, &factor);
    }

    void Fna3dRenderer::SetReferenceStencil(int value)
    {
        depthStencilState_.referenceStencil = value;
        FNA3D_SetReferenceStencil(device_, value);
    }

    void Fna3dRenderer::SetScissorRect(int x, int y, int w, int h)
    {
        FNA3D_Rect rect{ x, y, w, h };
        FNA3D_SetScissorRect(device_, &rect);
    }

    void Fna3dRenderer::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        FNA3D_Viewport viewport{ x, y, w, h, minDepth, maxDepth };
        FNA3D_SetViewport(device_, &viewport);
    }

    void Fna3dRenderer::SetDepthTestEnabled(bool enabled)
    {
        depthStencilState_.depthBufferEnable = enabled ? 1 : 0;
        ApplyCurrentDepthStencilState();
    }

    void Fna3dRenderer::SetDepthWriteEnabled(bool enabled)
    {
        depthStencilState_.depthBufferWriteEnable = enabled ? 1 : 0;
        ApplyCurrentDepthStencilState();
    }

    void Fna3dRenderer::SetBlendEnabled(bool enabled)
    {
        // FNA3D, like XNA, has no standalone "blend enable" -- an opaque BlendState *is* the
        // disabled state (One/Zero/Add). Enabling restores XNA's AlphaBlend, which is what every
        // caller of this legacy toggle means by "blending on"; a caller that wants a different
        // equation goes through ApplyBlendState instead.
        if (enabled)
        {
            blendState_.colorSourceBlend = FNA3D_BLEND_ONE;
            blendState_.colorDestinationBlend = FNA3D_BLEND_INVERSESOURCEALPHA;
            blendState_.alphaSourceBlend = FNA3D_BLEND_ONE;
            blendState_.alphaDestinationBlend = FNA3D_BLEND_INVERSESOURCEALPHA;
        }
        else
        {
            blendState_.colorSourceBlend = FNA3D_BLEND_ONE;
            blendState_.colorDestinationBlend = FNA3D_BLEND_ZERO;
            blendState_.alphaSourceBlend = FNA3D_BLEND_ONE;
            blendState_.alphaDestinationBlend = FNA3D_BLEND_ZERO;
        }
        blendState_.colorBlendFunction = FNA3D_BLENDFUNCTION_ADD;
        blendState_.alphaBlendFunction = FNA3D_BLENDFUNCTION_ADD;
        ApplyCurrentBlendState();
    }

    // ---------------------------------------------------------------------------------------
    // Capabilities and diagnostics
    // ---------------------------------------------------------------------------------------

    bool Fna3dRenderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
            // FNA3D's only shader entry point is FNA3D_CreateEffect, which takes a *compiled*
            // Direct3D 9 Effect binary. CreateEffectRenderer receives GLSL/HLSL source strings,
            // which nothing in FNA3D can compile, so this is false rather than a promise the
            // renderer would have to break at the first custom Effect.
            case CNA::GraphicsCapability::CustomEffects:
                return false;

            case CNA::GraphicsCapability::CompiledEffects:
                return SupportsCompiledEffects();

            // Hardware instancing becomes usable with a compiled Effect that declares the
            // per-instance semantics. DrawInstancedPrimitivesEx still rejects the stock-effect
            // path because none of XNA's stock shaders consumes such a stream.
            case CNA::GraphicsCapability::Instancing:
                return FNA3D_SupportsHardwareInstancing(device_) != 0;

            // FNA3D_ApplyVertexBufferBindings takes an array of bindings, each carrying its own
            // declaration, vertex offset and instance frequency -- multi-stream input is the
            // library's native shape, not an emulation.
            case CNA::GraphicsCapability::MultiStreamVertexInput:
                return true;

            case CNA::GraphicsCapability::MultiSampleAntiAliasing:
                return FNA3D_GetMaxMultiSampleCount(device_, presentation_.backBufferFormat, 4) > 1;

            case CNA::GraphicsCapability::StencilBuffer:
                return SupportsStencilBuffer();

            default:
                return true;
        }
    }

    int Fna3dRenderer::GetMaxTextureDimension() const
    {
        // FNA3D exposes no maximum-texture-size query; XNA 4.0 HiDef guarantees 4096 and every
        // driver FNA3D can select (SDL_GPU, D3D11, desktop GL) guarantees at least the shared
        // interface default. Keeping the shared default is the honest answer -- inventing a
        // narrower number would reject textures this renderer can actually create.
        return IGraphicsRenderer::GetMaxTextureDimension();
    }

    void Fna3dRenderer::SetStringMarkerEXT(const char* marker)
    {
        if (marker != nullptr)
        {
            FNA3D_SetStringMarker(device_, marker);
        }
    }

    std::unique_ptr<IEffectRenderer> Fna3dRenderer::CreateEffectRenderer(
        const std::string& /*vertSrc*/, const std::string& /*fragSrc*/)
    {
        return nullptr;
    }

    std::unique_ptr<ICompiledEffectRuntime> Fna3dRenderer::CreateCompiledEffect(
        const std::uint8_t* effectCode, std::size_t effectCodeLength)
    {
        return std::make_unique<Fna3dCompiledEffect>(*this, effectCode, effectCodeLength);
    }
}

namespace CNA::Internal::Renderers
{
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(
        const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<Fna3d::Fna3dRenderer>(args);
    }
}
