// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>

namespace Microsoft::Xna::Framework::Graphics
{
    using CNA::Internal::Renderers::IRenderTargetRenderer;

    // Mirrors Texture2D.cpp's/TextureCube.cpp's CalculateMipLevels.
    static int CalculateMipLevels(int w, int h)
    {
        int levels = 1;
        while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++levels; }
        return levels;
    }

    // Mirrors FNA's MathHelper.ClosestMSAAPower: rounds down to the nearest power of two;
    // 1 is not a valid MSAA sample count and becomes 0 (no multisampling).
    static int ClosestMSAAPower(int value)
    {
        if (value == 1) return 0;
        if (value <= 0) return 0;
        unsigned int result = static_cast<unsigned int>(value) - 1;
        result |= result >> 1;
        result |= result >> 2;
        result |= result >> 4;
        result |= result >> 8;
        result |= result >> 16;
        result += 1;
        if (static_cast<int>(result) == value) return static_cast<int>(result);
        return static_cast<int>(result >> 1);
    }

    static SurfaceFormat SelectRenderTargetFormatEXT(
        GraphicsDevice& device, SurfaceFormat preferredFormat)
    {
        if (!Texture::IsRenderTargetFormatAllowedByProfileEXT(
                device.getGraphicsProfileProperty(), preferredFormat))
            return SurfaceFormat::Color;

        switch (device.GetRenderer().ClassifyRenderTargetFormatEXT(
            static_cast<int>(preferredFormat)))
        {
            case CNA::Internal::Renderers::RendererFormatVerdict::Supported:
                return preferredFormat;
            case CNA::Internal::Renderers::RendererFormatVerdict::Unsupported:
            case CNA::Internal::Renderers::RendererFormatVerdict::Defer:
                return SurfaceFormat::Color;
        }
        return SurfaceFormat::Color;
    }

    static std::shared_ptr<IRenderTargetRenderer> CreateValidatedRenderTargetRenderer(
        GraphicsDevice& device, int width, int height, SurfaceFormat format,
        DepthFormat depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        // This helper is evaluated before Texture2D's base constructor. Validate here as well so
        // an invalid render target cannot reach a native allocation or replace XNA's argument
        // exception with a renderer-specific failure.
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(width, "width");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(height, "height");
        const int profile = static_cast<int>(device.getGraphicsProfileProperty());
        const int maxSize = device.GetRenderer().GetMaxTextureSizeForProfileEXT(profile);
        if (width > maxSize || height > maxSize)
        {
            throw System::NotSupportedException(
                "RenderTarget2D exceeds the active graphics profile's maximum texture size.");
        }
        constexpr std::int64_t maximumAspectRatio = 2048;
        const std::int64_t larger = std::max(width, height);
        const std::int64_t smaller = std::min(width, height);
        if (larger > smaller * maximumAspectRatio)
        {
            throw System::NotSupportedException(
                "RenderTarget2D's aspect ratio exceeds the active graphics profile limit of "
                "2048:1.");
        }
        if (device.getGraphicsProfileProperty() == GraphicsProfile::Reach && mipMap &&
            (!std::has_single_bit(static_cast<unsigned int>(width)) ||
             !std::has_single_bit(static_cast<unsigned int>(height))))
        {
            throw System::NotSupportedException(
                "Mipmapped non-power-of-two RenderTarget2D resources are not supported by the "
                "Reach graphics profile.");
        }
        // plans/plan_runtimerenderer.md design decision 9: renderability is the renderer's own question.
        // A renderer that answers Defer accepts the framework's rule, which is what every renderer
        // except SKIA did when this was an #ifdef block.
        switch (device.GetRenderer().ClassifyRenderTargetFormatEXT(static_cast<int>(format)))
        {
            case CNA::Internal::Renderers::RendererFormatVerdict::Supported:
                break;
            case CNA::Internal::Renderers::RendererFormatVerdict::Unsupported:
                throw System::NotSupportedException(
                    "RenderTarget2D: this SurfaceFormat is not renderable on the active renderer "
                    "(matches real XNA/FNA hardware renderability, not the backing library's own "
                    "raster capability).");
            case CNA::Internal::Renderers::RendererFormatVerdict::Defer:
                Texture::ValidateFormat(format);
                break;
        }
        // A renderer with no real RenderTarget2D storage (or that refused this particular
        // request, e.g. OpenGL1 without GL_ARB_framebuffer_object) returns nullptr here.
        // Construction is deliberately allowed to succeed anyway -- Texture3D/TextureCube
        // establish the same "null-object" convention elsewhere in this file's sibling classes
        // (REMED-CONTENT-004, Task 774): every operation that actually needs real storage
        // (Texture2D::SetData/GetData, and GraphicsDevice::SetRenderTarget's bind path) checks
        // for a null renderer at its own point of use and throws NotSupportedException there,
        // rather than construction eagerly refusing an object that may never be bound or sampled.
        return std::shared_ptr<IRenderTargetRenderer>(
            device.GetRenderer().CreateRenderTarget2DEXT(
                width, height, static_cast<int>(depthFormat), preserveContents, mipMap,
                ClosestMSAAPower(multiSampleCount), static_cast<int>(format)));
    }

    RenderTarget2D::RenderTarget2D(GraphicsDevice& device, int width, int height)
        : RenderTarget2D(device, width, height, false, SurfaceFormat::Color, DepthFormat::None)
    {
    }

    RenderTarget2D::RenderTarget2D(GraphicsDevice& device,
                                   int width,
                                   int height,
                                   bool mipMap,
                                   SurfaceFormat preferredFormat,
                                   DepthFormat preferredDepthFormat,
                                   int preferredMultiSampleCount,
                                   RenderTargetUsage usage)
        : Texture2D(device, width, height, SelectRenderTargetFormatEXT(device, preferredFormat),
                    mipMap ? CalculateMipLevels(width, height) : 1,
                    CreateValidatedRenderTargetRenderer(
                            device, width, height,
                            SelectRenderTargetFormatEXT(device, preferredFormat),
                            preferredDepthFormat,
                            // REMED-GFX-136: one shared mapping for both public render targets.
                            // The literal `usage == PreserveContents` this replaces contradicted
                            // GraphicsDevice::SetRenderTargets, which only ever clears a
                            // DiscardContents target, so PlatformContents was preserved by the
                            // shared layer and discarded by the renderer at the same time.
                            RenderTargetUsagePreservesContentsEXT(usage), mipMap,
                            preferredMultiSampleCount))
        , depthFormat_(preferredDepthFormat)
        , multiSampleCount_(preferredMultiSampleCount)
        , usage_(usage)
    {
        rtRenderer_ = static_cast<IRenderTargetRenderer*>(GetRendererRaw());
        // GDI-058: the public property describes the attachment that was actually created, not a
        // request the renderer normalized away. The interface default is identity for renderers that
        // honor the requested format; GDI's shared CPU target reports DepthFormat::None.
        if (rtRenderer_)
        {
            depthFormat_ = static_cast<DepthFormat>(
                rtRenderer_->GetAppliedDepthStencilFormatEXT(
                    static_cast<int>(preferredDepthFormat)));
        }
        // MultiSampleCount reflects the renderer's real, device-clamped value (matching FNA's
        // FNA3D_GetMaxMultiSampleCount), not the raw constructor argument.
        if (rtRenderer_) multiSampleCount_ = rtRenderer_->GetMultiSampleCount();
    }

    RenderTarget2D::RenderTarget2D(RenderTarget2D&& other) noexcept
        : Texture2D(std::move(other))
        , ContentLost(std::move(other.ContentLost))
        , depthFormat_(other.depthFormat_)
        , multiSampleCount_(other.multiSampleCount_)
        , usage_(other.usage_)
        , rtRenderer_(other.rtRenderer_)
        , contentLost_(other.contentLost_)
    {
        other.rtRenderer_ = nullptr;
        other.multiSampleCount_ = 0;
        other.contentLost_ = false;
    }

    RenderTarget2D& RenderTarget2D::operator=(RenderTarget2D&& other)
    {
        if (this != &other)
        {
            if (graphicsDevice_ != nullptr && !graphicsDeviceLifetime_.expired())
            {
                for (const RenderTargetBinding& binding : graphicsDevice_->GetRenderTargets())
                {
                    if (binding.getRenderTargetProperty() == this)
                    {
                        throw System::InvalidOperationException(
                            "Moving over a render target that is still bound");
                    }
                }
            }
            Texture2D::operator=(std::move(other));
            depthFormat_ = other.depthFormat_;
            multiSampleCount_ = other.multiSampleCount_;
            usage_ = other.usage_;
            rtRenderer_ = other.rtRenderer_;
            contentLost_ = other.contentLost_;
            ContentLost = std::move(other.ContentLost);
            other.rtRenderer_ = nullptr;
            other.multiSampleCount_ = 0;
            other.contentLost_ = false;
        }
        return *this;
    }

    RenderTargetUsage RenderTarget2D::getRenderTargetUsageProperty() const { return usage_; }
    DepthFormat RenderTarget2D::getDepthStencilFormatProperty() const { return depthFormat_; }
    int RenderTarget2D::getMultiSampleCountProperty() const { return multiSampleCount_; }

    IRenderTargetRenderer* RenderTarget2D::GetRenderTargetRenderer() const
    {
        return rtRenderer_;
    }

    const std::string& RenderTarget2D::GetTypeName() const
    {
        static const std::string name = "Microsoft.Xna.Framework.Graphics.RenderTarget2D";
        return name;
    }

    void RenderTarget2D::Dispose(bool disposing)
    {
        if (!isDisposed_ && graphicsDevice_ != nullptr &&
            !graphicsDevice_->getIsDisposedProperty())
        {
            for (const auto& binding : graphicsDevice_->GetRenderTargets())
            {
                if (binding.getRenderTargetProperty() == this)
                    throw System::InvalidOperationException("Disposing target that is still bound");
            }
        }
        Texture2D::Dispose(disposing);
        // Task 717 finding: rtRenderer_ is a raw, non-owning pointer cached at construction time
        // into the object owned by Texture2D::renderer_ (a shared_ptr, just reset() above) --
        // left uncleared, GetRenderTargetRenderer() would return a dangling pointer after
        // disposal, a use-after-free the instant any caller dereferenced it.
        rtRenderer_ = nullptr;
    }
}
