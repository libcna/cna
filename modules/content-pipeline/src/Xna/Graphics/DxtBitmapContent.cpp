// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/DxtBitmapContent.hpp"

#include <cstring>

#include "CNA/Content/Pipeline/BlockCompression.hpp"
#include "CNA/Internal/Graphics/DxtUtil.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/PixelBitmapContent.hpp"
#include "System/ArgumentException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    namespace
    {
        std::size_t BlocksAcross(SharpRuntime::intcs pixels) { return (static_cast<std::size_t>(pixels) + 3u) / 4u; }

        std::vector<std::uint8_t> ToRgba(const BitmapContent& colorBitmap)
        {
            return colorBitmap.GetPixelData();
        }

        std::shared_ptr<BitmapContent> FromRgba(const std::vector<std::uint8_t>& rgba, SharpRuntime::intcs width,
                                                SharpRuntime::intcs height)
        {
            auto bitmap = std::make_shared<PixelBitmapContent<Color>>(width, height);
            bitmap->SetPixelData(rgba);
            return bitmap;
        }

        template<typename Register>
        void RegisterOnce(Register register_)
        {
            static const bool once = [&]
            {
                register_();
                return true;
            }();
            (void)once;
        }
    }

    DxtBitmapContent::DxtBitmapContent(SharpRuntime::intcs blockSize) : blockSize_(blockSize) {}

    DxtBitmapContent::DxtBitmapContent(SharpRuntime::intcs blockSize, SharpRuntime::intcs width, SharpRuntime::intcs height)
        : BitmapContent(width, height), blockSize_(blockSize), blocks_(ExpectedByteCount(), 0)
    {
    }

    SharpRuntime::intcs DxtBitmapContent::BlockSize() const noexcept { return blockSize_; }

    std::size_t DxtBitmapContent::ExpectedByteCount() const noexcept
    {
        return BlocksAcross(getWidthProperty()) * BlocksAcross(getHeightProperty()) * static_cast<std::size_t>(blockSize_);
    }

    std::vector<SharpRuntime::bytecs> DxtBitmapContent::GetPixelData() const { return blocks_; }

    void DxtBitmapContent::SetPixelData(const std::vector<SharpRuntime::bytecs>& sourceData)
    {
        const std::size_t expected = ExpectedByteCount();
        if (sourceData.size() != expected)
        {
            throw System::ArgumentException("The sourceData array has length " + std::to_string(sourceData.size()) +
                                            ", but bitmap pixel data size should be " + std::to_string(expected) + ".");
        }
        blocks_ = sourceData;
    }

    bool DxtBitmapContent::TryCopyFrom(const std::shared_ptr<BitmapContent>& sourceBitmap, Rectangle sourceRegion,
                                       Rectangle destinationRegion)
    {
        // Decode what is there, overwrite the region with the source converted to Color, re-encode.
        std::shared_ptr<BitmapContent> canvas = Decode();
        Copy(sourceBitmap, sourceRegion, canvas, destinationRegion);
        Encode(*canvas);
        return true;
    }

    bool DxtBitmapContent::TryCopyTo(const std::shared_ptr<BitmapContent>& destinationBitmap, Rectangle sourceRegion,
                                     Rectangle destinationRegion)
    {
        if (dynamic_cast<DxtBitmapContent*>(destinationBitmap.get()) != nullptr)
        {
            // Let the destination decode, place and re-encode.
            return false;
        }
        Copy(Decode(), sourceRegion, destinationBitmap, destinationRegion);
        return true;
    }

    // ---- DXT1 ---------------------------------------------------------------------------------
    Dxt1BitmapContent::Dxt1BitmapContent(SharpRuntime::intcs width, SharpRuntime::intcs height)
        : DxtBitmapContent(8, width, height)
    {
        RegisterOnce([] { BitmapContent::RegisterBitmapType<Dxt1BitmapContent>(std::string(XnaTypeName)); });
    }

    bool Dxt1BitmapContent::TryGetFormat(Microsoft::Xna::Framework::Graphics::SurfaceFormat& format) const
    {
        format = Microsoft::Xna::Framework::Graphics::SurfaceFormat::Dxt1;
        return true;
    }

    std::string Dxt1BitmapContent::TypeDisplayName() const { return "Dxt1BitmapContent"; }

    const std::string& Dxt1BitmapContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    std::shared_ptr<BitmapContent> Dxt1BitmapContent::Decode() const
    {
        const std::vector<SharpRuntime::bytecs> blocks = GetPixelData();
        return FromRgba(CNA::Internal::Graphics::DxtUtil::DecompressDxt1(blocks.data(), blocks.size(), getWidthProperty(),
                                                                          getHeightProperty()),
                        getWidthProperty(), getHeightProperty());
    }

    void Dxt1BitmapContent::Encode(const BitmapContent& colorBitmap)
    {
        SetPixelData(CNA::Content::Pipeline::EncodeBlockCompressedImage(
            CNA::Content::Pipeline::BlockCompressionFormat::Bc1, ToRgba(colorBitmap),
            static_cast<std::uint32_t>(getWidthProperty()), static_cast<std::uint32_t>(getHeightProperty())));
    }

    // ---- DXT3 ---------------------------------------------------------------------------------
    Dxt3BitmapContent::Dxt3BitmapContent(SharpRuntime::intcs width, SharpRuntime::intcs height)
        : DxtBitmapContent(16, width, height)
    {
        RegisterOnce([] { BitmapContent::RegisterBitmapType<Dxt3BitmapContent>(std::string(XnaTypeName)); });
    }

    bool Dxt3BitmapContent::TryGetFormat(Microsoft::Xna::Framework::Graphics::SurfaceFormat& format) const
    {
        format = Microsoft::Xna::Framework::Graphics::SurfaceFormat::Dxt3;
        return true;
    }

    std::string Dxt3BitmapContent::TypeDisplayName() const { return "Dxt3BitmapContent"; }

    const std::string& Dxt3BitmapContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    std::shared_ptr<BitmapContent> Dxt3BitmapContent::Decode() const
    {
        const std::vector<SharpRuntime::bytecs> blocks = GetPixelData();
        return FromRgba(CNA::Internal::Graphics::DxtUtil::DecompressDxt3(blocks.data(), blocks.size(), getWidthProperty(),
                                                                          getHeightProperty()),
                        getWidthProperty(), getHeightProperty());
    }

    void Dxt3BitmapContent::Encode(const BitmapContent& colorBitmap)
    {
        SetPixelData(CNA::Content::Pipeline::EncodeBlockCompressedImage(
            CNA::Content::Pipeline::BlockCompressionFormat::Bc2, ToRgba(colorBitmap),
            static_cast<std::uint32_t>(getWidthProperty()), static_cast<std::uint32_t>(getHeightProperty())));
    }

    // ---- DXT5 ---------------------------------------------------------------------------------
    Dxt5BitmapContent::Dxt5BitmapContent(SharpRuntime::intcs width, SharpRuntime::intcs height)
        : DxtBitmapContent(16, width, height)
    {
        RegisterOnce([] { BitmapContent::RegisterBitmapType<Dxt5BitmapContent>(std::string(XnaTypeName)); });
    }

    bool Dxt5BitmapContent::TryGetFormat(Microsoft::Xna::Framework::Graphics::SurfaceFormat& format) const
    {
        format = Microsoft::Xna::Framework::Graphics::SurfaceFormat::Dxt5;
        return true;
    }

    std::string Dxt5BitmapContent::TypeDisplayName() const { return "Dxt5BitmapContent"; }

    const std::string& Dxt5BitmapContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    std::shared_ptr<BitmapContent> Dxt5BitmapContent::Decode() const
    {
        const std::vector<SharpRuntime::bytecs> blocks = GetPixelData();
        return FromRgba(CNA::Internal::Graphics::DxtUtil::DecompressDxt5(blocks.data(), blocks.size(), getWidthProperty(),
                                                                          getHeightProperty()),
                        getWidthProperty(), getHeightProperty());
    }

    void Dxt5BitmapContent::Encode(const BitmapContent& colorBitmap)
    {
        SetPixelData(CNA::Content::Pipeline::EncodeBlockCompressedImage(
            CNA::Content::Pipeline::BlockCompressionFormat::Bc3, ToRgba(colorBitmap),
            static_cast<std::uint32_t>(getWidthProperty()), static_cast<std::uint32_t>(getHeightProperty())));
    }
}
