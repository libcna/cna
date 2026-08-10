// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Describes a supported display mode including resolution and pixel format. */
    class DisplayMode : public System::Object
    {
    public:
        /** @brief Constructs a default DisplayMode with zero dimensions and Color format. */
        DisplayMode();

        /**
         * @brief Constructs a DisplayMode with the given dimensions and pixel format.
         * @param width  Display width in pixels.
         * @param height Display height in pixels.
         * @param format The surface format of the display mode.
         */
        DisplayMode(SharpRuntime::intcs width, SharpRuntime::intcs height, SurfaceFormat format);

        /** @brief Returns the display width in pixels. */
        [[nodiscard]] SharpRuntime::intcs getWidthProperty() const;

        /** @brief Returns the display height in pixels. */
        [[nodiscard]] SharpRuntime::intcs getHeightProperty() const;

        /** @brief Returns the display aspect ratio (width / height). */
        [[nodiscard]] float getAspectRatioProperty() const;

        /** @brief Returns the surface format of this display mode. */
        [[nodiscard]] SurfaceFormat getFormatProperty() const;

        /**
         * @brief Returns true if both display modes have the same width, height, and format.
         * @param other The display mode to compare with.
         * @return True if equal.
         */
        [[nodiscard]] bool operator==(const DisplayMode& other) const;

        /**
         * @brief Returns true if the display modes differ.
         * @param other The display mode to compare with.
         * @return True if not equal.
         */
        [[nodiscard]] bool operator!=(const DisplayMode& other) const;

        /** @brief Returns the fully qualified .NET type name of this class. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        SharpRuntime::intcs width_;
        SharpRuntime::intcs height_;
        SurfaceFormat format_;
    };
}
