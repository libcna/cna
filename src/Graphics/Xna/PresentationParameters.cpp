// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    PresentationParameters::PresentationParameters()
        : backBufferFormat_(SurfaceFormat::Color),
          backBufferHeight_(480),  // matches GraphicsDeviceManager::DefaultBackBufferHeight
          backBufferWidth_(800),   // matches GraphicsDeviceManager::DefaultBackBufferWidth
          deviceWindowHandle_(0),
          depthStencilFormat_(DepthFormat::None),
          isFullScreen_(false),
          multiSampleCount_(0),
          presentationInterval_(PresentInterval::Default),
          displayOrientation_(Microsoft::Xna::Framework::DisplayOrientation::Default),
          renderTargetUsage_(RenderTargetUsage::DiscardContents)
    {
    }

    SurfaceFormat PresentationParameters::getBackBufferFormatProperty() const
    {
        return backBufferFormat_;
    }

    void PresentationParameters::setBackBufferFormatProperty(SurfaceFormat value)
    {
        backBufferFormat_ = value;
    }

    SharpRuntime::intcs PresentationParameters::getBackBufferHeightProperty() const
    {
        return backBufferHeight_;
    }

    void PresentationParameters::setBackBufferHeightProperty(SharpRuntime::intcs value)
    {
        backBufferHeight_ = value;
    }

    SharpRuntime::intcs PresentationParameters::getBackBufferWidthProperty() const
    {
        return backBufferWidth_;
    }

    void PresentationParameters::setBackBufferWidthProperty(SharpRuntime::intcs value)
    {
        backBufferWidth_ = value;
    }

    Microsoft::Xna::Framework::Rectangle PresentationParameters::getBoundsProperty() const
    {
        return Microsoft::Xna::Framework::Rectangle(0, 0, backBufferWidth_, backBufferHeight_);
    }

    PresentationParameters::IntPtr PresentationParameters::getDeviceWindowHandleProperty() const
    {
        return deviceWindowHandle_;
    }

    void PresentationParameters::setDeviceWindowHandleProperty(IntPtr value)
    {
        deviceWindowHandle_ = value;
    }

    bool PresentationParameters::getHeadlessEXTProperty() const
    {
        return headlessEXT_;
    }

    void PresentationParameters::setHeadlessEXTProperty(bool value)
    {
        headlessEXT_ = value;
    }

    DepthFormat PresentationParameters::getDepthStencilFormatProperty() const
    {
        return depthStencilFormat_;
    }

    void PresentationParameters::setDepthStencilFormatProperty(DepthFormat value)
    {
        depthStencilFormat_ = value;
    }

    bool PresentationParameters::getIsFullScreenProperty() const
    {
        return isFullScreen_;
    }

    void PresentationParameters::setIsFullScreenProperty(bool value)
    {
        isFullScreen_ = value;
    }

    SharpRuntime::intcs PresentationParameters::getMultiSampleCountProperty() const
    {
        return multiSampleCount_;
    }

    void PresentationParameters::setMultiSampleCountProperty(SharpRuntime::intcs value)
    {
        multiSampleCount_ = value;
    }

    PresentInterval PresentationParameters::getPresentationIntervalProperty() const
    {
        return presentationInterval_;
    }

    void PresentationParameters::setPresentationIntervalProperty(PresentInterval value)
    {
        presentationInterval_ = value;
    }

    Microsoft::Xna::Framework::DisplayOrientation PresentationParameters::getDisplayOrientationProperty() const
    {
        return displayOrientation_;
    }

    void PresentationParameters::setDisplayOrientationProperty(Microsoft::Xna::Framework::DisplayOrientation value)
    {
        displayOrientation_ = value;
    }

    RenderTargetUsage PresentationParameters::getRenderTargetUsageProperty() const
    {
        return renderTargetUsage_;
    }

    void PresentationParameters::setRenderTargetUsageProperty(RenderTargetUsage value)
    {
        renderTargetUsage_ = value;
    }

    PresentationParameters PresentationParameters::Clone() const
    {
        PresentationParameters clone;
        clone.backBufferFormat_ = backBufferFormat_;
        clone.backBufferHeight_ = backBufferHeight_;
        clone.backBufferWidth_ = backBufferWidth_;
        clone.deviceWindowHandle_ = deviceWindowHandle_;
        clone.depthStencilFormat_ = depthStencilFormat_;
        clone.isFullScreen_ = isFullScreen_;
        clone.multiSampleCount_ = multiSampleCount_;
        clone.presentationInterval_ = presentationInterval_;
        clone.displayOrientation_ = displayOrientation_;
        clone.renderTargetUsage_ = renderTargetUsage_;
        return clone;
    }

    const std::string& PresentationParameters::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Graphics.PresentationParameters";
        return typeName;
    }
}
