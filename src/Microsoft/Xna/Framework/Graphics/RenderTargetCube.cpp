// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    RenderTargetCube::RenderTargetCube(GraphicsDevice& device, int size,
                                       bool mipMap, SurfaceFormat preferredFormat,
                                       DepthFormat preferredDepthFormat,
                                       int preferredMultiSampleCount,
                                       RenderTargetUsage usage)
        : TextureCube(device, size, mipMap, preferredFormat)
        , size_(size)
        , depthFormat_(preferredDepthFormat)
        , multiSampleCount_(preferredMultiSampleCount)
        , usage_(usage)
    {
        if (device.backend_)
            backend_ = device.backend_->CreateRenderTargetCube(size);
    }

    CNA::Internal::Backends::IRenderTargetCubeBackend* RenderTargetCube::GetRenderTargetCubeBackend() const
    {
        return backend_.get();
    }

    int RenderTargetCube::getWidthProperty() const  { return size_; }
    int RenderTargetCube::getHeightProperty() const { return size_; }
    int RenderTargetCube::getLevelCountProperty() const { return TextureCube::getLevelCountProperty(); }
    DepthFormat RenderTargetCube::getDepthStencilFormatProperty() const { return depthFormat_; }
    int RenderTargetCube::getMultiSampleCountProperty() const { return multiSampleCount_; }
    RenderTargetUsage RenderTargetCube::getRenderTargetUsageProperty() const { return usage_; }
}
