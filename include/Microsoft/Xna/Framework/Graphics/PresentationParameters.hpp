// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>

#include "CNA/CNAHelper.hpp"
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
    /** @brief Describes the presentation settings used by a GraphicsDevice. */
    class PresentationParameters : public System::Object
    {
    public:
        using IntPtr = std::uintptr_t;

        /** @brief Creates presentation parameters with XNA-compatible defaults. */
        PresentationParameters();

        /**
         * @brief Gets the back buffer pixel format.
         * @return Current back buffer surface format.
         */
        [[nodiscard]] SurfaceFormat getBackBufferFormatProperty() const;

        /**
         * @brief Sets the back buffer pixel format.
         * @param value New back buffer surface format.
         */
        void setBackBufferFormatProperty(SurfaceFormat value);

        /**
         * @brief Gets the back buffer height in pixels.
         * @return Current back buffer height.
         */
        [[nodiscard]] SharpRuntime::intcs getBackBufferHeightProperty() const;

        /**
         * @brief Sets the back buffer height in pixels.
         * @param value New back buffer height.
         */
        void setBackBufferHeightProperty(SharpRuntime::intcs value);

        /**
         * @brief Gets the back buffer width in pixels.
         * @return Current back buffer width.
         */
        [[nodiscard]] SharpRuntime::intcs getBackBufferWidthProperty() const;

        /**
         * @brief Sets the back buffer width in pixels.
         * @param value New back buffer width.
         */
        void setBackBufferWidthProperty(SharpRuntime::intcs value);

        /**
         * @brief Gets a rectangle whose size matches the back buffer dimensions (origin at 0,0).
         * @return Rectangle with width and height of the back buffer.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Rectangle getBoundsProperty() const;

        /**
         * @brief Gets the native window handle associated with the device.
         * @return Current device window handle.
         */
        [[nodiscard]] IntPtr getDeviceWindowHandleProperty() const;

        /**
         * @brief Sets the native window handle associated with the device.
         * @param value New device window handle.
         */
        void setDeviceWindowHandleProperty(IntPtr value);

        /**
         * @brief Gets whether the device should be created without any real window (off-screen).
         *
         * CNA extension. When true, GraphicsDevice creates no SDL window and never initialises
         * SDL's video subsystem, so it can run with no display server at all -- the same thing the
         * Headless and Software backends do unconditionally, but as an opt-in for a backend that
         * normally does want a window.
         *
         * Only backends that can genuinely operate without a swap chain support this. Today that
         * means D3D12 (its backend already treats a null window as a real off-screen mode).
         * Backends whose device creation is inseparable from a window/swap chain -- notably D3D11,
         * whose constructor always creates a swap chain, and EasyGL, whose GL context is bound to a
         * window -- will throw if this is set.
         *
         * Present() is not meaningful without a swap chain; an off-screen device is for rendering
         * into render targets and reading the result back (tests, server-side rendering, thumbnail
         * generation), not for putting frames on screen.
         *
         * @return true if the device should be created off-screen, with no window.
         */
        NOXNA [[nodiscard]] bool getHeadlessEXTProperty() const;

        /**
         * @brief Sets whether the device should be created without any real window (off-screen).
         * @param value true to create the device off-screen, with no window. Defaults to false.
         */
        NOXNA void setHeadlessEXTProperty(bool value);

        /**
         * @brief Gets the depth/stencil buffer format.
         * @return Current depth-stencil format.
         */
        [[nodiscard]] DepthFormat getDepthStencilFormatProperty() const;

        /**
         * @brief Sets the depth/stencil buffer format.
         * @param value New depth-stencil format.
         */
        void setDepthStencilFormatProperty(DepthFormat value);

        /**
         * @brief Gets whether the device is presenting in fullscreen mode.
         * @return true if fullscreen mode is active.
         */
        [[nodiscard]] bool getIsFullScreenProperty() const;

        /**
         * @brief Sets whether the device should present in fullscreen mode.
         * @param value true to use fullscreen presentation.
         */
        void setIsFullScreenProperty(bool value);

        /**
         * @brief Gets the number of multisample anti-aliasing samples.
         * @return Current multisample count (0 = disabled).
         */
        [[nodiscard]] SharpRuntime::intcs getMultiSampleCountProperty() const;

        /**
         * @brief Sets the number of multisample anti-aliasing samples.
         * @param value New multisample count (0 = disabled).
         */
        void setMultiSampleCountProperty(SharpRuntime::intcs value);

        /**
         * @brief Gets the presentation interval controlling display refresh synchronisation.
         * @return Current presentation interval.
         */
        [[nodiscard]] PresentInterval getPresentationIntervalProperty() const;

        /**
         * @brief Sets the presentation interval controlling display refresh synchronisation.
         * @param value New presentation interval.
         */
        void setPresentationIntervalProperty(PresentInterval value);

        /**
         * @brief Gets the display orientation.
         * @return Current display orientation.
         */
        [[nodiscard]] Microsoft::Xna::Framework::DisplayOrientation getDisplayOrientationProperty() const;

        /**
         * @brief Sets the display orientation.
         * @param value New display orientation.
         */
        void setDisplayOrientationProperty(Microsoft::Xna::Framework::DisplayOrientation value);

        /**
         * @brief Gets the render-target usage policy for the back buffer.
         * @return Current render-target usage.
         */
        [[nodiscard]] RenderTargetUsage getRenderTargetUsageProperty() const;

        /**
         * @brief Sets the render-target usage policy for the back buffer.
         * @param value New render-target usage.
         */
        void setRenderTargetUsageProperty(RenderTargetUsage value);

        /**
         * @brief Creates a copy of this PresentationParameters.
         * @return A new PresentationParameters with all fields copied from this instance.
         */
        [[nodiscard]] PresentationParameters Clone() const;

        /** @brief Returns the fully-qualified .NET type name for this class. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        SurfaceFormat backBufferFormat_;
        SharpRuntime::intcs backBufferHeight_;
        SharpRuntime::intcs backBufferWidth_;
        IntPtr deviceWindowHandle_;
        NOXNA bool headlessEXT_ = false; ///< Off-screen (no-window) device -- see getHeadlessEXTProperty().
        DepthFormat depthStencilFormat_;
        bool isFullScreen_;
        SharpRuntime::intcs multiSampleCount_;
        PresentInterval presentationInterval_;
        Microsoft::Xna::Framework::DisplayOrientation displayOrientation_;
        RenderTargetUsage renderTargetUsage_;
    };
}
