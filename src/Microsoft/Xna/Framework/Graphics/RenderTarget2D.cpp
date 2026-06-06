#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
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
        : GraphicsResource(&device)
        , width_(width)
        , height_(height)
        , levelCount_(mipMap ? 1 : 1)
        , format_(preferredFormat)
        , depthFormat_(preferredDepthFormat)
        , multiSampleCount_(preferredMultiSampleCount)
        , usage_(usage)
    {
    }

    int RenderTarget2D::getWidthProperty() const { return width_; }
    int RenderTarget2D::getHeightProperty() const { return height_; }
    int RenderTarget2D::getLevelCountProperty() const { return levelCount_; }
    RenderTargetUsage RenderTarget2D::getRenderTargetUsageProperty() const { return usage_; }
    DepthFormat RenderTarget2D::getDepthStencilFormatProperty() const { return depthFormat_; }
    int RenderTarget2D::getMultiSampleCountProperty() const { return multiSampleCount_; }
    SurfaceFormat RenderTarget2D::getFormatProperty() const { return format_; }
}
