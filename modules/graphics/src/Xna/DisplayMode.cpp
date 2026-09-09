// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/DisplayMode.hpp"

#include <iomanip>
#include <sstream>

namespace Microsoft::Xna::Framework::Graphics
{
    namespace
    {
        [[nodiscard]] const char* SurfaceFormatName(const SurfaceFormat format)
        {
            switch (format)
            {
            case SurfaceFormat::Color:           return "Color";
            case SurfaceFormat::Bgr565:          return "Bgr565";
            case SurfaceFormat::Bgra5551:        return "Bgra5551";
            case SurfaceFormat::Bgra4444:        return "Bgra4444";
            case SurfaceFormat::Dxt1:            return "Dxt1";
            case SurfaceFormat::Dxt3:            return "Dxt3";
            case SurfaceFormat::Dxt5:            return "Dxt5";
            case SurfaceFormat::NormalizedByte2: return "NormalizedByte2";
            case SurfaceFormat::NormalizedByte4: return "NormalizedByte4";
            case SurfaceFormat::Rgba1010102:     return "Rgba1010102";
            case SurfaceFormat::Rg32:            return "Rg32";
            case SurfaceFormat::Rgba64:          return "Rgba64";
            case SurfaceFormat::Alpha8:          return "Alpha8";
            case SurfaceFormat::Single:          return "Single";
            case SurfaceFormat::Vector2:         return "Vector2";
            case SurfaceFormat::Vector4:         return "Vector4";
            case SurfaceFormat::HalfSingle:      return "HalfSingle";
            case SurfaceFormat::HalfVector2:     return "HalfVector2";
            case SurfaceFormat::HalfVector4:     return "HalfVector4";
            case SurfaceFormat::HdrBlendable:    return "HdrBlendable";
            case SurfaceFormat::ColorBgraEXT:    return "ColorBgraEXT";
            case SurfaceFormat::ColorSrgbEXT:    return "ColorSrgbEXT";
            case SurfaceFormat::Dxt5SrgbEXT:     return "Dxt5SrgbEXT";
            case SurfaceFormat::Bc7EXT:          return "Bc7EXT";
            case SurfaceFormat::Bc7SrgbEXT:      return "Bc7SrgbEXT";
            case SurfaceFormat::ByteEXT:         return "ByteEXT";
            case SurfaceFormat::UShortEXT:       return "UShortEXT";
            }
            return nullptr;
        }
    }

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

    Microsoft::Xna::Framework::Rectangle DisplayMode::getTitleSafeAreaProperty() const
    {
        return Microsoft::Xna::Framework::Rectangle(0, 0, width_, height_);
    }

    std::string DisplayMode::ToString() const
    {
        std::ostringstream result;
        result << std::setprecision(7)
               << "{Width:" << width_
               << " Height:" << height_
               << " Format:";
        if (const char* const name = SurfaceFormatName(format_); name != nullptr)
            result << name;
        else
            result << static_cast<int>(format_);
        result << " AspectRatio:" << getAspectRatioProperty() << '}';
        return result.str();
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
