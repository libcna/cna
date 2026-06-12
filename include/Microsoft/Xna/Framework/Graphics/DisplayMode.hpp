// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// Describes a supported display mode.
    class DisplayMode : public System::Object
    {
    public:
        DisplayMode();

        /// Creates a display mode with width, height and pixel format.
        DisplayMode(SharpRuntime::intcs width, SharpRuntime::intcs height, SurfaceFormat format);

        /// Gets the display width in pixels.
        [[nodiscard]] SharpRuntime::intcs getWidthProperty() const;

        /// Gets the display height in pixels.
        [[nodiscard]] SharpRuntime::intcs getHeightProperty() const;

        /// Gets the display aspect ratio.
        [[nodiscard]] float getAspectRatioProperty() const;

        /// Gets the display mode surface format.
        [[nodiscard]] SurfaceFormat getFormatProperty() const;

        [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        SharpRuntime::intcs width_;
        SharpRuntime::intcs height_;
        SurfaceFormat format_;
    };
}
