#pragma once

#include <cstdint>
#include <string>

#include "Microsoft/Xna/Framework/DisplayOrientation.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentInterval.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// Describes the presentation settings used by a GraphicsDevice.
    class PresentationParameters : public System::Object
    {
    public:
        using IntPtr = std::uintptr_t;

        /// Creates presentation parameters with XNA-compatible defaults.
        PresentationParameters();

        /// Gets the back buffer format.
        [[nodiscard]] SurfaceFormat getBackBufferFormatProperty() const;

        /// Sets the back buffer format.
        void setBackBufferFormatProperty(SurfaceFormat value);

        /// Gets the back buffer height.
        [[nodiscard]] SharpRuntime::intcs getBackBufferHeightProperty() const;

        /// Sets the back buffer height.
        void setBackBufferHeightProperty(SharpRuntime::intcs value);

        /// Gets the back buffer width.
        [[nodiscard]] SharpRuntime::intcs getBackBufferWidthProperty() const;

        /// Sets the back buffer width.
        void setBackBufferWidthProperty(SharpRuntime::intcs value);

        /// Gets bounds matching the back buffer dimensions.
        [[nodiscard]] Microsoft::Xna::Framework::Rectangle getBoundsProperty() const;

        /// Gets the native device window handle.
        [[nodiscard]] IntPtr getDeviceWindowHandleProperty() const;

        /// Sets the native device window handle.
        void setDeviceWindowHandleProperty(IntPtr value);

        /// Gets the depth/stencil format.
        [[nodiscard]] DepthFormat getDepthStencilFormatProperty() const;

        /// Sets the depth/stencil format.
        void setDepthStencilFormatProperty(DepthFormat value);

        /// Gets whether presentation is fullscreen.
        [[nodiscard]] bool getIsFullScreenProperty() const;

        /// Sets whether presentation is fullscreen.
        void setIsFullScreenProperty(bool value);

        /// Gets the multisample count.
        [[nodiscard]] SharpRuntime::intcs getMultiSampleCountProperty() const;

        /// Sets the multisample count.
        void setMultiSampleCountProperty(SharpRuntime::intcs value);

        /// Gets the presentation interval.
        [[nodiscard]] PresentInterval getPresentationIntervalProperty() const;

        /// Sets the presentation interval.
        void setPresentationIntervalProperty(PresentInterval value);

        /// Gets the display orientation.
        [[nodiscard]] Microsoft::Xna::Framework::DisplayOrientation getDisplayOrientationProperty() const;

        /// Sets the display orientation.
        void setDisplayOrientationProperty(Microsoft::Xna::Framework::DisplayOrientation value);

        /// Gets render-target usage.
        [[nodiscard]] RenderTargetUsage getRenderTargetUsageProperty() const;

        /// Sets render-target usage.
        void setRenderTargetUsageProperty(RenderTargetUsage value);

        /// Creates a copy of this object.
        [[nodiscard]] PresentationParameters Clone() const;

        [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        SurfaceFormat backBufferFormat_;
        SharpRuntime::intcs backBufferHeight_;
        SharpRuntime::intcs backBufferWidth_;
        IntPtr deviceWindowHandle_;
        DepthFormat depthStencilFormat_;
        bool isFullScreen_;
        SharpRuntime::intcs multiSampleCount_;
        PresentInterval presentationInterval_;
        Microsoft::Xna::Framework::DisplayOrientation displayOrientation_;
        RenderTargetUsage renderTargetUsage_;
    };
}
