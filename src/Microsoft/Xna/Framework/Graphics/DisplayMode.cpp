// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/DisplayMode.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    DisplayMode::DisplayMode()
        : width_(0),
          height_(0),
          format_(SurfaceFormat::Color)
    {
    }

    DisplayMode::DisplayMode(SharpRuntime::intcs width, SharpRuntime::intcs height, SurfaceFormat format)
        : width_(width),
          height_(height),
          format_(format)
    {
    }

    SharpRuntime::intcs DisplayMode::getWidthProperty() const
    {
        return width_;
    }

    SharpRuntime::intcs DisplayMode::getHeightProperty() const
    {
        return height_;
    }

    float DisplayMode::getAspectRatioProperty() const
    {
        if (height_ == 0)
        {
            return 0.0f;
        }

        return static_cast<float>(width_) / static_cast<float>(height_);
    }

    SurfaceFormat DisplayMode::getFormatProperty() const
    {
        return format_;
    }

    bool DisplayMode::operator==(const DisplayMode& other) const
    {
        return width_ == other.width_ && height_ == other.height_ && format_ == other.format_;
    }

    bool DisplayMode::operator!=(const DisplayMode& other) const
    {
        return !(*this == other);
    }

    const std::string& DisplayMode::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Graphics.DisplayMode";
        return typeName;
    }
}
