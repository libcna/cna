// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    using CNA::Internal::Backends::IRenderTargetCubeBackend;
    using CNA::Internal::Backends::ITextureCubeBackend;

    RenderTargetCube::RenderTargetCube(GraphicsDevice& device, int size,
                                       bool /*mipMap*/, SurfaceFormat preferredFormat,
                                       DepthFormat preferredDepthFormat,
                                       int preferredMultiSampleCount,
                                       RenderTargetUsage usage)
        : TextureCube(device, size, preferredFormat,
                      // IRenderTargetCubeBackend : ITextureCubeBackend — pass single backend
                      // to TextureCube so sampling and rendering share the same GPU image.
                      std::unique_ptr<ITextureCubeBackend>(
                          device.backend_ ? device.backend_->CreateRenderTargetCube(size).release()
                                          : nullptr))
        , size_(size)
        , depthFormat_(preferredDepthFormat)
        , multiSampleCount_(preferredMultiSampleCount)
        , usage_(usage)
    {
        rtCubeBackend_ = static_cast<IRenderTargetCubeBackend*>(GetBackendRaw());
    }

    IRenderTargetCubeBackend* RenderTargetCube::GetRenderTargetCubeBackend() const
    {
        return rtCubeBackend_;
    }

    int RenderTargetCube::getWidthProperty() const  { return size_; }
    int RenderTargetCube::getHeightProperty() const { return size_; }
    int RenderTargetCube::getLevelCountProperty() const { return TextureCube::getLevelCountProperty(); }
    DepthFormat RenderTargetCube::getDepthStencilFormatProperty() const { return depthFormat_; }
    int RenderTargetCube::getMultiSampleCountProperty() const { return multiSampleCount_; }
    RenderTargetUsage RenderTargetCube::getRenderTargetUsageProperty() const { return usage_; }
}
