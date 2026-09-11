// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "System/InvalidOperationException.hpp"

#include <algorithm>

namespace Microsoft::Xna::Framework::Graphics
{
    using CNA::Internal::Renderers::IRenderTargetCubeRenderer;
    using CNA::Internal::Renderers::ITextureCubeRenderer;

    // Mirrors TextureCube.cpp's CalculateMipLevels(size,size) — cube faces are square.
    static int CalculateMipLevels(int size)
    {
        int levels = 1;
        int s = size;
        while (s > 1) { s = std::max(1, s / 2); ++levels; }
        return levels;
    }

    // Mirrors FNA's MathHelper.ClosestMSAAPower (see RenderTarget2D.cpp for the identical helper).
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

    RenderTargetCube::RenderTargetCube(GraphicsDevice& device, int size,
                                       bool mipMap, SurfaceFormat preferredFormat,
                                       DepthFormat preferredDepthFormat,
                                       int preferredMultiSampleCount,
                                       RenderTargetUsage usage)
        : TextureCube(device, size, preferredFormat,
                      // IRenderTargetCubeRenderer : ITextureCubeRenderer — pass single renderer
                      // to TextureCube so sampling and rendering share the same GPU image.
                      std::shared_ptr<ITextureCubeRenderer>(
                          device.renderer_ ? device.renderer_->CreateRenderTargetCubeEXT(
                                                 size, static_cast<int>(preferredDepthFormat),
                                                 // REMED-GFX-136: `usage` used to stop here. The
                                                 // renderer had to invent its own answer, and both
                                                 // renderers that had to (Vulkan, WebGPU) invented
                                                 // "always discard".
                                                 RenderTargetUsagePreservesContentsEXT(usage), mipMap,
                                                 ClosestMSAAPower(preferredMultiSampleCount),
                                                 // plans/plan_modern.md MOD-107: the format stops being
                                                 // dropped here, the way RenderTarget2D's already
                                                 // is. TextureCube's own constructor checks it
                                                 // against the renderer's verdict.
                                                 static_cast<int>(preferredFormat)).release()
                                          : nullptr),
                      mipMap ? CalculateMipLevels(size) : 1)
        , size_(size)
        , depthFormat_(preferredDepthFormat)
        , multiSampleCount_(preferredMultiSampleCount)
        , usage_(usage)
    {
        rtCubeRenderer_ = static_cast<IRenderTargetCubeRenderer*>(GetRendererRaw());
        // MultiSampleCount reflects the renderer's real, device-clamped value (matching FNA's
        // FNA3D_GetMaxMultiSampleCount), not the raw constructor argument.
        if (rtCubeRenderer_)
        {
            multiSampleCount_ = rtCubeRenderer_->GetMultiSampleCount();
            // plans/plan_vulkan.md VULKAN-215: DepthStencilFormat reflects the renderer's real
            // choice, exactly as RenderTarget2D's already does. Before this it echoed the
            // constructor argument, so a renderer that substituted a depth format -- Vulkan gives
            // each cube its own VkFormat -- reported a format it had not used. The third time this
            // class has lagged its 2D twin in the same way; MOD-107 was the surface format, and
            // the multisample count above was the first.
            depthFormat_ = static_cast<DepthFormat>(
                rtCubeRenderer_->GetAppliedDepthStencilFormatEXT(
                    static_cast<int>(preferredDepthFormat)));
        }
    }

    IRenderTargetCubeRenderer* RenderTargetCube::GetRenderTargetCubeRenderer() const
    {
        return rtCubeRenderer_;
    }

    void RenderTargetCube::Dispose(bool disposing)
    {
        if (!isDisposed_ && graphicsDevice_ != nullptr)
        {
            for (const auto& binding : graphicsDevice_->GetRenderTargets())
            {
                if (binding.getRenderTargetProperty() == this)
                    throw System::InvalidOperationException("Disposing target that is still bound");
            }
        }
        TextureCube::Dispose(disposing);
        rtCubeRenderer_ = nullptr;
    }

    const std::string& RenderTargetCube::GetTypeName() const
    {
        static const std::string name = "Microsoft.Xna.Framework.Graphics.RenderTargetCube";
        return name;
    }

    int RenderTargetCube::getWidthProperty() const  { return size_; }
    int RenderTargetCube::getHeightProperty() const { return size_; }
    int RenderTargetCube::getLevelCountProperty() const { return TextureCube::getLevelCountProperty(); }
    DepthFormat RenderTargetCube::getDepthStencilFormatProperty() const { return depthFormat_; }
    int RenderTargetCube::getMultiSampleCountProperty() const { return multiSampleCount_; }
    RenderTargetUsage RenderTargetCube::getRenderTargetUsageProperty() const { return usage_; }
}
