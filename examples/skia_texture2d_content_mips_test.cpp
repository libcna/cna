// SPDX-License-Identifier: MS-PL
// SKIA-130: exact DDS/XNB Texture2D mip-chain content loading.

#include "CNA/Internal/Backends/Skia/SkiaGraphicsBackend.hpp"
#include "CNA/Internal/Xnb/Texture2DContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "System/IO/Stream.hpp"

#include <algorithm>
#include <any>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using CNA::Internal::Backends::Skia::SkiaGraphicsBackend;
using CNA::Internal::Backends::Skia::SkiaResourceStats;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Content;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    class VectorStream final : public System::IO::Stream
    {
    public:
        explicit VectorStream(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

        System::IO::intcs Read(System::IO::bytecs buffer[], System::IO::intcs offset,
                               System::IO::intcs count) override
        {
            const int remaining = static_cast<int>(bytes_.size()) - position_;
            const int read = std::min(count, remaining);
            if (read > 0)
            {
                std::memcpy(buffer + offset, bytes_.data() + position_,
                            static_cast<std::size_t>(read));
                position_ += read;
            }
            return read;
        }

        void Close() override {}

        [[nodiscard]] System::IO::intcs getLengthProperty() const override
        {
            return static_cast<System::IO::intcs>(bytes_.size());
        }

    private:
        const std::vector<std::uint8_t>& bytes_;
        int position_ = 0;
    };

    void WriteU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value)
    {
        bytes[offset + 0u] = static_cast<std::uint8_t>(value);
        bytes[offset + 1u] = static_cast<std::uint8_t>(value >> 8);
        bytes[offset + 2u] = static_cast<std::uint8_t>(value >> 16);
        bytes[offset + 3u] = static_cast<std::uint8_t>(value >> 24);
    }

    void AppendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
    {
        const std::size_t offset = bytes.size();
        bytes.resize(offset + 4u);
        WriteU32(bytes, offset, value);
    }

    std::vector<std::uint8_t> SolidDxtBlock(
        std::uint32_t fourCC, std::uint16_t rgb565, std::uint8_t alpha)
    {
        const bool dxt1 = fourCC == 0x31545844u;
        std::vector<std::uint8_t> block(dxt1 ? 8u : 16u, 0u);
        std::size_t colorOffset = 0u;
        if (fourCC == 0x33545844u)
        {
            const std::uint8_t nibble = static_cast<std::uint8_t>(alpha / 17u);
            std::fill_n(block.begin(), 8, static_cast<std::uint8_t>(nibble | (nibble << 4)));
            colorOffset = 8u;
        }
        else if (fourCC == 0x35545844u)
        {
            block[0] = alpha;
            block[1] = 0u;
            colorOffset = 8u;
        }
        block[colorOffset + 0u] = static_cast<std::uint8_t>(rgb565);
        block[colorOffset + 1u] = static_cast<std::uint8_t>(rgb565 >> 8);
        // c1 and lookup/index masks stay zero: every texel selects c0 (and alpha endpoint 0).
        return block;
    }

    constexpr std::uint16_t kLevelColors565[] = {
        0xF800u, // red
        0x07E0u, // green
        0x001Fu, // blue
        0xFFE0u, // yellow
    };

    const Color kLevelColors[] = {
        Color(255, 0, 0, 255),
        Color(0, 255, 0, 255),
        Color(0, 0, 255, 255),
        Color(255, 255, 0, 255),
    };

    std::vector<std::uint8_t> BuildDds(
        std::uint32_t fourCC, int mipCount, std::uint8_t alpha)
    {
        std::vector<std::uint8_t> bytes(128u, 0u);
        bytes[0] = 'D'; bytes[1] = 'D'; bytes[2] = 'S'; bytes[3] = ' ';
        WriteU32(bytes, 4u, 124u);
        WriteU32(bytes, 12u, 8u);
        WriteU32(bytes, 16u, 8u);
        WriteU32(bytes, 28u, static_cast<std::uint32_t>(mipCount));
        WriteU32(bytes, 76u, 32u);
        WriteU32(bytes, 80u, 4u);
        WriteU32(bytes, 84u, fourCC);

        int width = 8;
        int height = 8;
        for (int level = 0; level < mipCount; ++level)
        {
            const std::vector<std::uint8_t> block =
                SolidDxtBlock(fourCC, kLevelColors565[level], alpha);
            const int blockCount = ((width + 3) / 4) * ((height + 3) / 4);
            for (int i = 0; i < blockCount; ++i)
                bytes.insert(bytes.end(), block.begin(), block.end());
            width = std::max(1, width / 2);
            height = std::max(1, height / 2);
        }
        return bytes;
    }

    std::vector<std::uint8_t> BuildXnbColorBody(int mipCount)
    {
        std::vector<std::uint8_t> bytes;
        AppendU32(bytes, static_cast<std::uint32_t>(SurfaceFormat::Color));
        AppendU32(bytes, 8u);
        AppendU32(bytes, 8u);
        AppendU32(bytes, static_cast<std::uint32_t>(mipCount));

        int width = 8;
        int height = 8;
        for (int level = 0; level < mipCount; ++level)
        {
            const std::uint32_t pixelCount = static_cast<std::uint32_t>(width * height);
            AppendU32(bytes, pixelCount * 4u);
            for (std::uint32_t pixel = 0; pixel < pixelCount; ++pixel)
            {
                bytes.push_back(kLevelColors[level].getRProperty());
                bytes.push_back(kLevelColors[level].getGProperty());
                bytes.push_back(kLevelColors[level].getBProperty());
                bytes.push_back(kLevelColors[level].getAProperty());
            }
            width = std::max(1, width / 2);
            height = std::max(1, height / 2);
        }
        return bytes;
    }

    std::vector<std::uint8_t> BuildXnbDxt5Body()
    {
        std::vector<std::uint8_t> bytes;
        AppendU32(bytes, static_cast<std::uint32_t>(SurfaceFormat::Dxt5));
        AppendU32(bytes, 8u);
        AppendU32(bytes, 8u);
        AppendU32(bytes, 4u);

        int width = 8;
        int height = 8;
        for (int level = 0; level < 4; ++level)
        {
            const std::vector<std::uint8_t> block =
                SolidDxtBlock(0x35545844u, kLevelColors565[level], 91u);
            const int blockCount = ((width + 3) / 4) * ((height + 3) / 4);
            AppendU32(bytes, static_cast<std::uint32_t>(blockCount * block.size()));
            for (int i = 0; i < blockCount; ++i)
                bytes.insert(bytes.end(), block.begin(), block.end());
            width = std::max(1, width / 2);
            height = std::max(1, height / 2);
        }
        return bytes;
    }

    [[nodiscard]] bool SameAllocationStats(
        const SkiaResourceStats& left, const SkiaResourceStats& right)
    {
        return left.textureBackends == right.textureBackends
            && left.textureImageViews == right.textureImageViews
            && left.mipChains2D == right.mipChains2D
            && left.textureImageBytes == right.textureImageBytes
            && left.cpuTextureStorageBytes == right.cpuTextureStorageBytes
            && left.mipChain2DStorageBytes == right.mipChain2DStorageBytes;
    }

    template <typename F>
    [[nodiscard]] bool Throws(F&& operation)
    {
        try { operation(); }
        catch (const std::exception&) { return true; }
        return false;
    }
}

class SkiaTexture2DContentMipsTest final : public Game
{
public:
    SkiaTexture2DContentMipsTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(1);
        graphics_->setPreferredBackBufferHeightProperty(1);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        auto* skia = dynamic_cast<SkiaGraphicsBackend*>(&device.GetBackend());
        Check(skia != nullptr, "fixture uses the Skia backend");
        if (!skia)
        {
            Exit();
            return;
        }

        const SkiaResourceStats baseline = skia->GetResourceStatsEXT();
        VerifyDdsFormat(device, 0x31545844u, 255u, "DDS DXT1");
        VerifyDdsFormat(device, 0x33545844u, 136u, "DDS DXT3");
        VerifyDdsFormat(device, 0x35545844u, 91u, "DDS DXT5");
        Check(SameAllocationStats(skia->GetResourceStatsEXT(), baseline),
              "successful DDS chains release all Skia allocations");

        {
            const auto single = BuildDds(0x31545844u, 1, 255u);
            VectorStream stream(single);
            Texture2D texture = Texture2D::FromStream(device, stream);
            Check(texture.getLevelCountProperty() == 1,
                  "single-level DDS remains single-level without fabricated mips");
        }

        auto incompleteDds = BuildDds(0x31545844u, 3, 255u);
        Check(Throws([&] {
                  VectorStream stream(incompleteDds);
                  (void)Texture2D::FromStream(device, stream);
              }),
              "incomplete DDS mip prefix rejects instead of generating an absent level");
        auto truncatedDds = BuildDds(0x35545844u, 4, 91u);
        truncatedDds.pop_back();
        Check(Throws([&] {
                  VectorStream stream(truncatedDds);
                  (void)Texture2D::FromStream(device, stream);
              }),
              "truncated DDS mip payload rejects");
        Check(SameAllocationStats(skia->GetResourceStatsEXT(), baseline),
              "rejected DDS chains publish no lasting Skia allocation");

        ContentTypeReaderManager::ClearTypeCreators();
        CNA::Internal::Xnb::RegisterTexture2DXnbReader();
        {
            auto body = BuildXnbColorBody(4);
            Texture2D texture = LoadXnbBody(device, body);
            VerifyChain(texture, 255u, "XNB Color");
        }
        {
            auto body = BuildXnbDxt5Body();
            Texture2D texture = LoadXnbBody(device, body);
            VerifyChain(texture, 91u, "XNB DXT5");
            Check(texture.getFormatProperty() == SurfaceFormat::Color,
                  "compressed XNB chain reports its canonical decoded Color storage");
        }
        {
            auto body = BuildXnbColorBody(1);
            Texture2D texture = LoadXnbBody(device, body);
            Check(texture.getLevelCountProperty() == 1,
                  "single-level XNB remains single-level without fabricated mips");
        }

        auto incompleteXnb = BuildXnbColorBody(3);
        Check(Throws([&] { (void)LoadXnbBody(device, incompleteXnb); }),
              "incomplete XNB mip prefix rejects instead of generating an absent level");
        auto truncatedXnb = BuildXnbColorBody(4);
        truncatedXnb.pop_back();
        Check(Throws([&] { (void)LoadXnbBody(device, truncatedXnb); }),
              "truncated XNB mip payload rejects");
        auto oversizedCompressedLevel = BuildXnbDxt5Body();
        // First serialized level's byte count follows the 16-byte common header.
        WriteU32(oversizedCompressedLevel, 16u, 65u);
        oversizedCompressedLevel.insert(oversizedCompressedLevel.begin() + 20 + 64, 0u);
        Check(Throws([&] { (void)LoadXnbBody(device, oversizedCompressedLevel); }),
              "XNB compressed level rejects bytes crossing its declared level boundary");
        ContentTypeReaderManager::ClearTypeCreators();

        Check(SameAllocationStats(skia->GetResourceStatsEXT(), baseline),
              "successful and rejected XNB chains return Skia allocations to baseline");
        Exit();
    }

    void Draw(const GameTime&) override {}

private:
    Texture2D LoadXnbBody(GraphicsDevice& device, const std::vector<std::uint8_t>& body)
    {
        ContentManager manager;
        manager.setGraphicsDevice(device);
        VectorStream stream(body);
        ContentReader reader(&manager, &stream, "skia-content-mips", 5, 'w');
        auto typeReader = ContentTypeReaderManager::CreateReader(
            "Microsoft.Xna.Framework.Content.Texture2DReader");
        if (!typeReader)
            throw std::runtime_error("Texture2DReader registration disappeared");
        return std::any_cast<Texture2D>(typeReader->ReadUntyped(reader, std::any{}));
    }

    void VerifyDdsFormat(GraphicsDevice& device, std::uint32_t fourCC,
                         std::uint8_t expectedAlpha, const char* label)
    {
        const auto bytes = BuildDds(fourCC, 4, expectedAlpha);
        VectorStream stream(bytes);
        Texture2D texture = Texture2D::FromStream(device, stream);
        VerifyChain(texture, expectedAlpha, label);
    }

    void VerifyChain(Texture2D& texture, std::uint8_t expectedAlpha, const char* label)
    {
        Check(texture.getWidthProperty() == 8 && texture.getHeightProperty() == 8
                  && texture.getLevelCountProperty() == 4,
              (std::string(label) + " preserves the declared 8x8 four-level boundary").c_str());
        int width = 8;
        int height = 8;
        for (int level = 0; level < 4; ++level)
        {
            std::vector<Color> pixels(
                static_cast<std::size_t>(width * height), Color(0, 0, 0, 0));
            texture.GetData(level, nullptr, pixels.data(), 0, width * height);
            const Color& expected = kLevelColors[level];
            const bool exact = std::all_of(pixels.begin(), pixels.end(), [&](const Color& pixel) {
                return pixel.getRProperty() == expected.getRProperty()
                    && pixel.getGProperty() == expected.getGProperty()
                    && pixel.getBProperty() == expected.getBProperty()
                    && pixel.getAProperty() == expectedAlpha;
            });
            Check(exact, (std::string(label) + " level " + std::to_string(level)
                          + " round-trips its authored texels").c_str());
            width = std::max(1, width / 2);
            height = std::max(1, height / 2);
        }
    }

    void Check(bool pass, const char* label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label);
        if (!pass) ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    int failures_ = 0;
};

int main()
{
    SkiaTexture2DContentMipsTest game;
    game.Run();
    return game.Result();
}
