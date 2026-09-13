// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentItem.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/Type.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    /**
     * @brief Provides properties and methods for creating and maintaining a bitmap resource.
     *
     * The copy protocol is XNA's: `Copy` validates, then asks the destination to copy from the
     * source, then the source to copy to the destination, and finally routes through a
     * `PixelBitmapContent<Vector4>` that every bitmap can convert to and from. Resizing happens
     * inside that conversion (docs/xna-content-pipeline-compat-api.md §9).
     */
    class BitmapContent : public ContentItem
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent";

        /**
         * @brief Gets the height of the bitmap, in pixels.
         *
         * @return The height.
         */
        [[nodiscard]] SharpRuntime::intcs getHeightProperty() const noexcept;

        /**
         * @brief Gets the width of the bitmap, in pixels.
         *
         * @return The width.
         */
        [[nodiscard]] SharpRuntime::intcs getWidthProperty() const noexcept;

        /**
         * @brief Copies one bitmap into another. The destination bitmap can be in any format and
         *        size; the pixels are converted and resized as needed.
         *
         * @param sourceBitmap BitmapContent being copied.
         * @param destinationBitmap BitmapContent being overwritten.
         * @throws System::ArgumentNullException for a null bitmap.
         */
        static void Copy(const std::shared_ptr<BitmapContent>& sourceBitmap,
                         const std::shared_ptr<BitmapContent>& destinationBitmap);

        /**
         * @brief Copies one bitmap into another, converting and resizing the region as needed.
         *
         * @param sourceBitmap BitmapContent being copied.
         * @param sourceRegion Region of sourceBitmap.
         * @param destinationBitmap BitmapContent being overwritten.
         * @param destinationRegion Region of destinationBitmap.
         * @throws System::ArgumentNullException for a null bitmap.
         * @throws System::ArgumentOutOfRangeException for a region outside its bitmap.
         */
        static void Copy(const std::shared_ptr<BitmapContent>& sourceBitmap, Rectangle sourceRegion,
                         const std::shared_ptr<BitmapContent>& destinationBitmap, Rectangle destinationRegion);

        /**
         * @brief Reads encoded bitmap content.
         *
         * @return The encoded pixels: little-endian element values for a `PixelBitmapContent<T>`,
         *         the compressed blocks for a DXT bitmap.
         */
        [[nodiscard]] virtual std::vector<SharpRuntime::bytecs> GetPixelData() const = 0;

        /**
         * @brief Writes encoded bitmap content.
         *
         * @param sourceData Array containing the source data.
         * @throws System::ArgumentException when the array does not have the bitmap's data size.
         */
        virtual void SetPixelData(const std::vector<SharpRuntime::bytecs>& sourceData) = 0;

        /**
         * @brief Returns a string description of the bitmap resource: the type as XNA spells it,
         *        a comma, and the size (`PixelBitmapContent<Color>, 3x2`).
         *
         * @return The description.
         */
        [[nodiscard]] std::string ToString() const override;

        /**
         * @brief Gets the corresponding GPU texture format for the specified bitmap type.
         *
         * @param format Format of the bitmap, when it has one.
         * @return True when the bitmap type has a GPU format.
         */
        [[nodiscard]] virtual bool TryGetFormat(Microsoft::Xna::Framework::Graphics::SurfaceFormat& format) const = 0;

        /**
         * @brief The type name XNA's `ToString` and its validation messages use for this bitmap
         *        (`PixelBitmapContent<Color>`, `Dxt1BitmapContent`).
         *
         * @return The display name.
         */
        CNAEXT [[nodiscard]] virtual std::string TypeDisplayName() const = 0;

        /**
         * @brief Invokes the protected `TryCopyFrom` -- the way another bitmap's copy routine
         *        reaches it, as XNA's `Copy` does inside one assembly.
         *
         * @param sourceBitmap The source.
         * @param sourceRegion Region of the source.
         * @param destinationRegion Region of this bitmap.
         * @return True when this bitmap could copy the region.
         */
        CNAEXT bool InvokeTryCopyFrom(const std::shared_ptr<BitmapContent>& sourceBitmap, Rectangle sourceRegion,
                                      Rectangle destinationRegion)
        {
            return TryCopyFrom(sourceBitmap, sourceRegion, destinationRegion);
        }

        /**
         * @brief Invokes the protected `TryCopyTo`.
         *
         * @param destinationBitmap The destination.
         * @param sourceRegion Region of this bitmap.
         * @param destinationRegion Region of the destination.
         * @return True when this bitmap could write the region.
         */
        CNAEXT bool InvokeTryCopyTo(const std::shared_ptr<BitmapContent>& destinationBitmap, Rectangle sourceRegion,
                                    Rectangle destinationRegion)
        {
            return TryCopyTo(destinationBitmap, sourceRegion, destinationRegion);
        }

        /**
         * @brief Creates a bitmap of a registered type with the given size -- what XNA does by
         *        reflection for `TextureContent::ConvertBitmapType`.
         *
         * @param bitmapType The bitmap class.
         * @param width Width in pixels.
         * @param height Height in pixels.
         * @return The new bitmap.
         * @throws System::ArgumentException when the type is not a registered bitmap type, with
         *         XNA's `ConvertBitmapType` message.
         */
        CNAEXT [[nodiscard]] static std::shared_ptr<BitmapContent> CreateBitmap(System::Type bitmapType,
                                                                                SharpRuntime::intcs width,
                                                                                SharpRuntime::intcs height);

        /**
         * @brief Registers a concrete bitmap class so `CreateBitmap` and `ConvertBitmapType` can
         *        instantiate it; every `PixelBitmapContent<T>` and the DXT bitmaps are registered
         *        on first use.
         *
         * @tparam TBitmap A bitmap class constructible from `(intcs width, intcs height)`.
         * @param dotNetName The .NET full name used in messages.
         */
        template<typename TBitmap>
        CNAEXT static void RegisterBitmapType(std::string dotNetName)
        {
            RegisterBitmapType(System::Type::From<TBitmap>(), std::move(dotNetName),
                               [](SharpRuntime::intcs width, SharpRuntime::intcs height) -> std::shared_ptr<BitmapContent>
                               { return std::make_shared<TBitmap>(width, height); });
        }

        /**
         * @brief The .NET full name a registered bitmap type was given, or `getName` of the type.
         *
         * @param bitmapType The bitmap class.
         * @return The name.
         */
        CNAEXT [[nodiscard]] static std::string BitmapTypeName(System::Type bitmapType);

    protected:
        /** @brief Initializes a new instance of BitmapContent. */
        BitmapContent();

        /**
         * @brief Initializes a new instance of BitmapContent with the specified width or height.
         *
         * @param width Width, in pixels, of the bitmap resource.
         * @param height Height, in pixels, of the bitmap resource.
         * @throws System::ArgumentOutOfRangeException when a dimension is not positive
         *         (`Bitmap size must be greater than zero.`).
         */
        BitmapContent(SharpRuntime::intcs width, SharpRuntime::intcs height);

        /**
         * @brief Sets the height of the bitmap, in pixels.
         *
         * @param value The height.
         */
        void setHeightProperty(SharpRuntime::intcs value);

        /**
         * @brief Sets the width of the bitmap, in pixels.
         *
         * @param value The width.
         */
        void setWidthProperty(SharpRuntime::intcs value);

        /**
         * @brief Attempts to copy a region from a specified bitmap.
         *
         * @param sourceBitmap BitmapContent being copied.
         * @param sourceRegion Region of sourceBitmap.
         * @param destinationRegion Region of the destination (this bitmap).
         * @return True when this bitmap can accept the source's type.
         */
        [[nodiscard]] virtual bool TryCopyFrom(const std::shared_ptr<BitmapContent>& sourceBitmap, Rectangle sourceRegion,
                                               Rectangle destinationRegion) = 0;

        /**
         * @brief Attempts to copy a region of the specified bitmap onto another.
         *
         * @param destinationBitmap BitmapContent being overwritten.
         * @param sourceRegion Region of this bitmap.
         * @param destinationRegion Region of destinationBitmap.
         * @return True when this bitmap can write into the destination's type.
         */
        [[nodiscard]] virtual bool TryCopyTo(const std::shared_ptr<BitmapContent>& destinationBitmap, Rectangle sourceRegion,
                                             Rectangle destinationRegion) = 0;

        /**
         * @brief Validates the arguments to the Copy function.
         *
         * @param sourceBitmap BitmapContent being copied.
         * @param sourceRegion Location of sourceBitmap.
         * @param destinationBitmap BitmapContent being overwritten.
         * @param destinationRegion Region of destinationBitmap.
         * @throws System::ArgumentNullException for a null bitmap.
         * @throws System::ArgumentOutOfRangeException for a region that is not inside its bitmap.
         */
        static void ValidateCopyArguments(const std::shared_ptr<BitmapContent>& sourceBitmap, Rectangle sourceRegion,
                                          const std::shared_ptr<BitmapContent>& destinationBitmap,
                                          Rectangle destinationRegion);

    private:
        using BitmapFactory = std::function<std::shared_ptr<BitmapContent>(SharpRuntime::intcs, SharpRuntime::intcs)>;
        static void RegisterBitmapType(System::Type bitmapType, std::string dotNetName, BitmapFactory factory);

        SharpRuntime::intcs width_ = 0;
        SharpRuntime::intcs height_ = 0;
    };
}
