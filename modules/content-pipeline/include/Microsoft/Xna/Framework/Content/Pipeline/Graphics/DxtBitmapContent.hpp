// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/BitmapContent.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    /**
     * @brief Provides methods and properties for managing compressed (DXT) textures. The pixel data
     *        is the sequence of 4x4 blocks, `blockSize` bytes each, covering the bitmap rounded up
     *        to whole blocks (`tests/reference/xna40/graphics/graphics-content-oracle.json`,
     *        `dxt/Dxt1/describe_5x3`).
     */
    class DxtBitmapContent : public BitmapContent
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.DxtBitmapContent";

        /**
         * @brief Gets the bitmap content as an array of encoded bytes.
         *
         * @return The compressed blocks.
         */
        [[nodiscard]] std::vector<SharpRuntime::bytecs> GetPixelData() const override;

        /**
         * @brief Sets the contents of the bitmap using an encoded byte array.
         *
         * @param sourceData Array containing the source data.
         * @throws System::ArgumentException when the array does not have the block data size.
         */
        void SetPixelData(const std::vector<SharpRuntime::bytecs>& sourceData) override;

        /**
         * @brief The number of bytes in one 4x4 block: 8 for DXT1, 16 for DXT3 and DXT5.
         *
         * @return The block size.
         */
        CNAEXT [[nodiscard]] SharpRuntime::intcs BlockSize() const noexcept;

    protected:
        /**
         * @brief Initializes a new instance of DxtBitmapContent with the specified compression.
         *
         * @param blockSize Size of the compression block, in bytes.
         */
        explicit DxtBitmapContent(SharpRuntime::intcs blockSize);

        /**
         * @brief Initializes a new instance of DxtBitmapContent with the specified size.
         *
         * @param blockSize Size of the compression block, in bytes.
         * @param width Width, in pixels, of the bitmap resource.
         * @param height Height, in pixels, of the bitmap resource.
         */
        DxtBitmapContent(SharpRuntime::intcs blockSize, SharpRuntime::intcs width, SharpRuntime::intcs height);

        /**
         * @brief Attempts to copy from a specified region to another: any source is converted to
         *        Color pixels, placed into the decoded image, and the whole bitmap is re-encoded with
         *        the canonical block compressor.
         *
         * @param sourceBitmap BitmapContent being copied.
         * @param sourceRegion Region of sourceBitmap.
         * @param destinationRegion Region of this bitmap.
         * @return True.
         */
        [[nodiscard]] bool TryCopyFrom(const std::shared_ptr<BitmapContent>& sourceBitmap, Rectangle sourceRegion,
                                       Rectangle destinationRegion) override;

        /**
         * @brief Attempts to copy the specified region to another: the blocks are decoded to Color
         *        pixels, which are then copied into the destination.
         *
         * @param destinationBitmap BitmapContent being overwritten.
         * @param sourceRegion Region of this bitmap.
         * @param destinationRegion Region of destinationBitmap.
         * @return True.
         */
        [[nodiscard]] bool TryCopyTo(const std::shared_ptr<BitmapContent>& destinationBitmap, Rectangle sourceRegion,
                                     Rectangle destinationRegion) override;

        /**
         * @brief Decodes the blocks into an RGBA Color bitmap of this bitmap's size.
         *
         * @return The decoded bitmap.
         */
        CNAEXT [[nodiscard]] virtual std::shared_ptr<BitmapContent> Decode() const = 0;

        /**
         * @brief Encodes an RGBA Color bitmap of this bitmap's size into the blocks.
         *
         * @param colorBitmap The pixels to compress.
         */
        CNAEXT virtual void Encode(const BitmapContent& colorBitmap) = 0;

    private:
        [[nodiscard]] std::size_t ExpectedByteCount() const noexcept;

        SharpRuntime::intcs blockSize_;
        std::vector<SharpRuntime::bytecs> blocks_;
    };

    /** @brief Provides methods and properties for managing compressed textures (DXT1). */
    class Dxt1BitmapContent final : public DxtBitmapContent
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.Dxt1BitmapContent";

        /**
         * @brief Initializes a new instance of Dxt1BitmapContent with the specified width and height.
         *
         * @param width Width, in pixels, of the bitmap resource.
         * @param height Height, in pixels, of the bitmap resource.
         */
        Dxt1BitmapContent(SharpRuntime::intcs width, SharpRuntime::intcs height);

        /**
         * @brief Attempts to get the GPU texture format of this bitmap type.
         *
         * @param format Receives `SurfaceFormat::Dxt1`.
         * @return True.
         */
        [[nodiscard]] bool TryGetFormat(Microsoft::Xna::Framework::Graphics::SurfaceFormat& format) const override;

        /** @brief `Dxt1BitmapContent`. */
        [[nodiscard]] std::string TypeDisplayName() const override;

        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    protected:
        [[nodiscard]] std::shared_ptr<BitmapContent> Decode() const override;
        void Encode(const BitmapContent& colorBitmap) override;
    };

    /** @brief Provides methods and properties for managing compressed textures (DXT3). */
    class Dxt3BitmapContent final : public DxtBitmapContent
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.Dxt3BitmapContent";

        /**
         * @brief Initializes a new instance of Dxt3BitmapContent with the specified width and height.
         *
         * @param width Width, in pixels, of the bitmap resource.
         * @param height Height, in pixels, of the bitmap resource.
         */
        Dxt3BitmapContent(SharpRuntime::intcs width, SharpRuntime::intcs height);

        /**
         * @brief Attempts to get the GPU texture format of this bitmap type.
         *
         * @param format Receives `SurfaceFormat::Dxt3`.
         * @return True.
         */
        [[nodiscard]] bool TryGetFormat(Microsoft::Xna::Framework::Graphics::SurfaceFormat& format) const override;

        /** @brief `Dxt3BitmapContent`. */
        [[nodiscard]] std::string TypeDisplayName() const override;

        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    protected:
        [[nodiscard]] std::shared_ptr<BitmapContent> Decode() const override;
        void Encode(const BitmapContent& colorBitmap) override;
    };

    /** @brief Provides methods and properties for managing compressed textures (DXT5). */
    class Dxt5BitmapContent final : public DxtBitmapContent
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.Dxt5BitmapContent";

        /**
         * @brief Initializes a new instance of Dxt5BitmapContent with the specified width and height.
         *
         * @param width Width, in pixels, of the bitmap resource.
         * @param height Height, in pixels, of the bitmap resource.
         */
        Dxt5BitmapContent(SharpRuntime::intcs width, SharpRuntime::intcs height);

        /**
         * @brief Attempts to get the GPU texture format of this bitmap type.
         *
         * @param format Receives `SurfaceFormat::Dxt5`.
         * @return True.
         */
        [[nodiscard]] bool TryGetFormat(Microsoft::Xna::Framework::Graphics::SurfaceFormat& format) const override;

        /** @brief `Dxt5BitmapContent`. */
        [[nodiscard]] std::string TypeDisplayName() const override;

        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    protected:
        [[nodiscard]] std::shared_ptr<BitmapContent> Decode() const override;
        void Encode(const BitmapContent& colorBitmap) override;
    };
}
