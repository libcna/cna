// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <bit>

namespace Microsoft::Xna::Framework::Graphics
{
    using CNA::Internal::Renderers::IRenderTargetCubeRenderer;
    using CNA::Internal::Renderers::IGraphicsRenderer;
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

    static std::unique_ptr<IRenderTargetCubeRenderer> CreateValidatedRenderTargetCubeRenderer(
        GraphicsDevice& device, int size, DepthFormat depthFormat, RenderTargetUsage usage,
        bool mipMap, int preferredMultiSampleCount, SurfaceFormat format)
    {
        // Constructor arguments are evaluated before TextureCube can validate its base state.
        // Keep invalid dimensions out of the renderer and preserve XNA's public exception type.
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(size, "size");
        const int profile = static_cast<int>(device.getGraphicsProfileProperty());
        const int maxSize = device.GetRenderer().GetMaxCubeSizeForProfileEXT(profile);
        if (size > maxSize)
        {
            throw System::NotSupportedException(
                "RenderTargetCube exceeds the active graphics profile's maximum cube size.");
        }
        if (device.getGraphicsProfileProperty() == GraphicsProfile::Reach &&
            !std::has_single_bit(static_cast<unsigned int>(size)))
        {
            throw System::NotSupportedException(
                "Non-power-of-two RenderTargetCube resources are not supported by the Reach "
                "graphics profile.");
        }
        return device.GetRenderer().CreateRenderTargetCubeEXT(
            size, static_cast<int>(depthFormat),
            RenderTargetUsagePreservesContentsEXT(usage), mipMap,
            ClosestMSAAPower(preferredMultiSampleCount), static_cast<int>(format));
    }

    RenderTargetCube::RenderTargetCube(GraphicsDevice& device, int size,
                                       bool mipMap, SurfaceFormat preferredFormat,
                                       DepthFormat preferredDepthFormat,
                                       int preferredMultiSampleCount,
                                       RenderTargetUsage usage)
        : TextureCube(device, size, SelectRenderTargetFormatEXT(device, preferredFormat),
                      // IRenderTargetCubeRenderer : ITextureCubeRenderer — pass single renderer
                      // to TextureCube so sampling and rendering share the same GPU image.
                      std::shared_ptr<ITextureCubeRenderer>(
                          CreateValidatedRenderTargetCubeRenderer(
                              device, size, preferredDepthFormat, usage, mipMap,
                              preferredMultiSampleCount,
                              SelectRenderTargetFormatEXT(device, preferredFormat)).release()),
                      mipMap ? CalculateMipLevels(size) : 1)
        , size_(size)
        , depthFormat_(preferredDepthFormat)
        , multiSampleCount_(preferredMultiSampleCount)
        , usage_(usage)
    {
        rtCubeRenderer_ = static_cast<IRenderTargetCubeRenderer*>(GetRendererRaw());
        // MultiSampleCount reflects the renderer's real, device-clamped value (matching FNA's
        // FNA3D_GetMaxMultiSampleCount), not the raw constructor argument.
        if (rtCubeRenderer_) multiSampleCount_ = rtCubeRenderer_->GetMultiSampleCount();
    }

    IRenderTargetCubeRenderer* RenderTargetCube::GetRenderTargetCubeRenderer() const
    {
        return rtCubeRenderer_;
    }

    void RenderTargetCube::Dispose(bool disposing)
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
