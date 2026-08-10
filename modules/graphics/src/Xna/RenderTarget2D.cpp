// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>

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

#ifdef CNA_RENDERER_SKIA
    // SKIA-142: the formats FNA itself reports renderable (see the surface-format matrix's
    // "FNA/Skia RT decision" column) that this raster renderer can now genuinely render into --
    // Skia's raster SkSurface has no hardware format restriction, but promoting only these keeps
    // parity with real XNA/FNA renderability, not "whatever Skia happens to allow". Every other
    // format (packed 16-bit colours, all compressed formats, SNORM, Alpha8, ColorBgraEXT) is a
    // real non-renderable format on actual XNA/FNA hardware and stays refused regardless.
    static bool IsRenderableSkiaFormatEXT(SurfaceFormat format) noexcept
    {
        return format == SurfaceFormat::Color
            || format == SurfaceFormat::Rgba1010102
            || format == SurfaceFormat::Rg32
            || format == SurfaceFormat::Rgba64
            || format == SurfaceFormat::Single
            || format == SurfaceFormat::Vector2
            || format == SurfaceFormat::Vector4
            || format == SurfaceFormat::HalfSingle
            || format == SurfaceFormat::HalfVector2
            || format == SurfaceFormat::HalfVector4
            || format == SurfaceFormat::HdrBlendable
            || format == SurfaceFormat::ColorSrgbEXT
            || format == SurfaceFormat::ByteEXT
            || format == SurfaceFormat::UShortEXT;
    }
#endif

    static std::shared_ptr<IRenderTargetRenderer> CreateValidatedRenderTargetRenderer(
        GraphicsDevice& device, int width, int height, SurfaceFormat format,
        DepthFormat depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
#ifdef CNA_RENDERER_SKIA
        if (!IsRenderableSkiaFormatEXT(format))
        {
            throw System::NotSupportedException(
                "Skia RenderTarget2D: this SurfaceFormat is not renderable (matches FNA "
                "hardware renderability, not Skia's own raster capability).");
        }
#else
        Texture::ValidateFormat(format);
#endif
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
        : Texture2D(device, width, height, preferredFormat,
                    mipMap ? CalculateMipLevels(width, height) : 1,
                    CreateValidatedRenderTargetRenderer(
                            device, width, height, preferredFormat, preferredDepthFormat,
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
        if (!isDisposed_ && graphicsDevice_ != nullptr)
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
