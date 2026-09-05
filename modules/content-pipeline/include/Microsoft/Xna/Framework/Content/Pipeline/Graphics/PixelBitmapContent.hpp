// SPDX-License-Identifier: MS-PL
#pragma once

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <span>
#include <utility>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/BitmapContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/detail/PixelTraits.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    /**
     * @brief The element-type-independent face of every `PixelBitmapContent<T>`: pixels as
     *        `Vector4`, which is how one pixel type converts to another and how resizing samples.
     */
    class CNAEXT PixelBitmapContentBase : public BitmapContent
    {
    public:
        /**
         * @brief Reads a pixel as a `Vector4` through the element type's conversion.
         *
         * @param x Column.
         * @param y Row.
         * @return The pixel.
         */
        [[nodiscard]] virtual Vector4 GetPixelVector4(SharpRuntime::intcs x, SharpRuntime::intcs y) const = 0;

        /**
         * @brief Writes a pixel from a `Vector4` through the element type's conversion.
         *
         * @param x Column.
         * @param y Row.
         * @param value The pixel.
         */
        virtual void SetPixelVector4(SharpRuntime::intcs x, SharpRuntime::intcs y, const Vector4& value) = 0;

        /**
         * @brief Copies a region between two pixel bitmaps of any element types, resampling when
         *        the regions differ in size: bilinear when enlarging, a box average when reducing.
         *
         * XNA resamples through D3DX, whose reduction kernel is not documented; the corpus test
         * records how far this box filter lands from it (docs/xna-content-pipeline-compat-api.md §9).
         *
         * @param source The source bitmap.
         * @param sourceRegion Region of the source.
         * @param destination The destination bitmap.
         * @param destinationRegion Region of the destination.
         */
        static void ConvertRegion(const PixelBitmapContentBase& source, Rectangle sourceRegion,
                                  PixelBitmapContentBase& destination, Rectangle destinationRegion);

    protected:
        using BitmapContent::BitmapContent;
    };

    /**
     * @brief Provides properties and methods for managing bitmap content of a specified pixel type.
     *
     * @tparam T Type of the pixel: `Single`, `Vector2`, `Vector3`, `Vector4`, `Color` or one of the
     *         `IPackedVector` types (docs/xna-content-pipeline-compat-api.md §9 lists the 22).
     */
    template<typename T>
    class PixelBitmapContent final : public PixelBitmapContentBase
    {
        static_assert(detail::ValidPixelType<T>,
                      "PixelBitmapContent<T>: T is not a valid PixelBitmapContent type. Supported types are Single, "
                      "Vector2, Vector3, Vector4, and value types that implement IPackedVector.");
        using Traits = detail::PixelTraits<T>;

    public:
        /** @brief .NET full name of this type. */
        CNAEXT static const std::string XnaTypeName;

        /**
         * @brief Initializes a new instance of PixelBitmapContent with the specified width or height.
         *
         * @param width Width, in pixels, of the bitmap resource.
         * @param height Height, in pixels, of the bitmap resource.
         * @throws System::ArgumentOutOfRangeException when a dimension is not positive.
         */
        PixelBitmapContent(SharpRuntime::intcs width, SharpRuntime::intcs height)
            : PixelBitmapContentBase(width, height),
              pixels_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), T{})
        {
            Register();
        }

        /**
         * @brief Gets the pixel at the specified location.
         *
         * @param x Column of the pixel.
         * @param y Row of the pixel.
         * @return The pixel.
         * @throws System::ArgumentOutOfRangeException for a location outside the bitmap.
         */
        [[nodiscard]] T GetPixel(SharpRuntime::intcs x, SharpRuntime::intcs y) const
        {
            CheckX(x);
            CheckY(y);
            return pixels_[Index(x, y)];
        }

        /**
         * @brief Reads encoded bitmap content: every pixel's element bytes, row by row.
         *
         * @return The bytes.
         */
        [[nodiscard]] std::vector<SharpRuntime::bytecs> GetPixelData() const override
        {
            std::vector<SharpRuntime::bytecs> data(pixels_.size() * Traits::Bytes);
            for (std::size_t i = 0; i < pixels_.size(); ++i)
            {
                Traits::Write(pixels_[i], data.data() + i * Traits::Bytes);
            }
            return data;
        }

        /**
         * @brief Gets the array of pixels in the specified row.
         *
         * The row is the bitmap's own storage, not a copy: writing through it changes the
         * bitmap, which is what the XNA runtime does (measured, tests/reference/xna40/graphics,
         * case color/get_row_is_live).
         *
         * @param y Row of the pixels.
         * @return The pixels of that row, aliasing the bitmap.
         * @throws System::ArgumentOutOfRangeException for a row outside the bitmap.
         */
        [[nodiscard]] std::span<T> GetRow(SharpRuntime::intcs y)
        {
            CheckY(y);
            return {pixels_.data() + static_cast<std::ptrdiff_t>(Index(0, y)),
                    static_cast<std::size_t>(getWidthProperty())};
        }

        /**
         * @brief Gets the array of pixels in the specified row of a constant bitmap.
         *
         * @param y Row of the pixels.
         * @return The pixels of that row, aliasing the bitmap.
         * @throws System::ArgumentOutOfRangeException for a row outside the bitmap.
         */
        [[nodiscard]] std::span<const T> GetRow(SharpRuntime::intcs y) const
        {
            CheckY(y);
            return {pixels_.data() + static_cast<std::ptrdiff_t>(Index(0, y)),
                    static_cast<std::size_t>(getWidthProperty())};
        }

        /**
         * @brief Replaces all pixels of the specified color with a new color.
         *
         * @param originalColor Color to be replaced.
         * @param newColor New color.
         */
        void ReplaceColor(const T& originalColor, const T& newColor)
        {
            for (T& pixel : pixels_)
            {
                if (Traits::Equal(pixel, originalColor))
                {
                    pixel = newColor;
                }
            }
        }

        /**
         * @brief Sets the pixel at the specified location.
         *
         * @param x Column of the pixel.
         * @param y Row of the pixel.
         * @param value Value of the pixel.
         * @throws System::ArgumentOutOfRangeException for a location outside the bitmap.
         */
        void SetPixel(SharpRuntime::intcs x, SharpRuntime::intcs y, const T& value)
        {
            CheckX(x);
            CheckY(y);
            pixels_[Index(x, y)] = value;
        }

        /**
         * @brief Writes encoded bitmap content.
         *
         * @param sourceData Array containing the source data.
         * @throws System::ArgumentException when the array does not have the bitmap's data size
         *         (`The sourceData array has length N, but bitmap pixel data size should be M.`).
         */
        void SetPixelData(const std::vector<SharpRuntime::bytecs>& sourceData) override
        {
            const std::size_t expected = pixels_.size() * Traits::Bytes;
            if (sourceData.size() != expected)
            {
                throw System::ArgumentException("The sourceData array has length " + std::to_string(sourceData.size()) +
                                                ", but bitmap pixel data size should be " + std::to_string(expected) + ".");
            }
            for (std::size_t i = 0; i < pixels_.size(); ++i)
            {
                pixels_[i] = Traits::Read(sourceData.data() + i * Traits::Bytes);
            }
        }

        /**
         * @brief Gets the corresponding GPU texture format for the specified bitmap type.
         *
         * @param format Format of the bitmap, when the element type has one.
         * @return True for the element types with a `SurfaceFormat`.
         */
        [[nodiscard]] bool TryGetFormat(Microsoft::Xna::Framework::Graphics::SurfaceFormat& format) const override
        {
            if (Traits::Surface.has_value())
            {
                format = *Traits::Surface;
                return true;
            }
            return false;
        }

        /** @brief `PixelBitmapContent<T>` with XNA's spelling of @p T (`Single` for float). */
        [[nodiscard]] std::string TypeDisplayName() const override
        {
            return "PixelBitmapContent<" + std::string(Traits::Name) + ">";
        }

        /** @brief Reads a pixel as a `Vector4`. */
        [[nodiscard]] Vector4 GetPixelVector4(SharpRuntime::intcs x, SharpRuntime::intcs y) const override
        {
            return Traits::ToVector4(pixels_[Index(x, y)]);
        }

        /** @brief Writes a pixel from a `Vector4`. */
        void SetPixelVector4(SharpRuntime::intcs x, SharpRuntime::intcs y, const Vector4& value) override
        {
            pixels_[Index(x, y)] = Traits::FromVector4(value);
        }

        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override { return XnaTypeName; }

    protected:
        /**
         * @brief Attempts to copy a region from a specified bitmap: any pixel bitmap converts in,
         *        through `Vector4`, with resampling when the regions differ.
         *
         * @param sourceBitmap BitmapContent being copied.
         * @param sourceRegion Region of sourceBitmap.
         * @param destinationRegion Region of this bitmap.
         * @return False when the source is not a pixel bitmap (a DXT bitmap decodes itself).
         */
        [[nodiscard]] bool TryCopyFrom(const std::shared_ptr<BitmapContent>& sourceBitmap, Rectangle sourceRegion,
                                       Rectangle destinationRegion) override
        {
            const auto* source = dynamic_cast<const PixelBitmapContentBase*>(sourceBitmap.get());
            if (source == nullptr)
            {
                return false;
            }
            if (const auto* same = dynamic_cast<const PixelBitmapContent<T>*>(source);
                same != nullptr && sourceRegion.Width == destinationRegion.Width &&
                sourceRegion.Height == destinationRegion.Height)
            {
                for (SharpRuntime::intcs y = 0; y < sourceRegion.Height; ++y)
                {
                    for (SharpRuntime::intcs x = 0; x < sourceRegion.Width; ++x)
                    {
                        pixels_[Index(destinationRegion.X + x, destinationRegion.Y + y)] =
                            same->pixels_[same->Index(sourceRegion.X + x, sourceRegion.Y + y)];
                    }
                }
                return true;
            }
            ConvertRegion(*source, sourceRegion, *this, destinationRegion);
            return true;
        }

        /**
         * @brief Attempts to copy a region of this bitmap onto another pixel bitmap.
         *
         * @param destinationBitmap BitmapContent being overwritten.
         * @param sourceRegion Region of this bitmap.
         * @param destinationRegion Region of destinationBitmap.
         * @return False when the destination is not a pixel bitmap.
         */
        [[nodiscard]] bool TryCopyTo(const std::shared_ptr<BitmapContent>& destinationBitmap, Rectangle sourceRegion,
                                     Rectangle destinationRegion) override
        {
            auto* destination = dynamic_cast<PixelBitmapContentBase*>(destinationBitmap.get());
            if (destination == nullptr)
            {
                return false;
            }
            ConvertRegion(*this, sourceRegion, *destination, destinationRegion);
            return true;
        }

    private:
        static void Register()
        {
            static const bool once = []
            {
                BitmapContent::RegisterBitmapType<PixelBitmapContent<T>>(XnaTypeName);
                return true;
            }();
            (void)once;
        }

        [[nodiscard]] std::size_t Index(SharpRuntime::intcs x, SharpRuntime::intcs y) const noexcept
        {
            return static_cast<std::size_t>(y) * static_cast<std::size_t>(getWidthProperty()) + static_cast<std::size_t>(x);
        }

        void CheckX(SharpRuntime::intcs x) const
        {
            if (x < 0 || x >= getWidthProperty())
            {
                throw System::ArgumentOutOfRangeException("x");
            }
        }

        void CheckY(SharpRuntime::intcs y) const
        {
            if (y < 0 || y >= getHeightProperty())
            {
                throw System::ArgumentOutOfRangeException("y");
            }
        }

        std::vector<T> pixels_;
    };

    template<typename T>
    const std::string PixelBitmapContent<T>::XnaTypeName =
        "Microsoft.Xna.Framework.Content.Pipeline.Graphics.PixelBitmapContent`1[[" +
        std::string(detail::PixelTraits<T>::DotNetName) + "]]";
}
