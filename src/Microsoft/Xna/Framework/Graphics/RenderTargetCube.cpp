// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "System/InvalidOperationException.hpp"

#include <algorithm>

namespace Microsoft::Xna::Framework::Graphics
{
    using CNA::Internal::Backends::IRenderTargetCubeBackend;
    using CNA::Internal::Backends::ITextureCubeBackend;

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
                      // IRenderTargetCubeBackend : ITextureCubeBackend — pass single backend
                      // to TextureCube so sampling and rendering share the same GPU image.
                      std::unique_ptr<ITextureCubeBackend>(
                          device.backend_ ? device.backend_->CreateRenderTargetCube(
                                                 size, static_cast<int>(preferredDepthFormat), mipMap,
                                                 ClosestMSAAPower(preferredMultiSampleCount)).release()
                                          : nullptr),
                      mipMap ? CalculateMipLevels(size) : 1)
        , size_(size)
        , depthFormat_(preferredDepthFormat)
        , multiSampleCount_(preferredMultiSampleCount)
        , usage_(usage)
    {
        rtCubeBackend_ = static_cast<IRenderTargetCubeBackend*>(GetBackendRaw());
        // MultiSampleCount reflects the backend's real, device-clamped value (matching FNA's
        // FNA3D_GetMaxMultiSampleCount), not the raw constructor argument.
        if (rtCubeBackend_) multiSampleCount_ = rtCubeBackend_->GetMultiSampleCount();
    }

    IRenderTargetCubeBackend* RenderTargetCube::GetRenderTargetCubeBackend() const
    {
        return rtCubeBackend_;
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
        rtCubeBackend_ = nullptr;
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
