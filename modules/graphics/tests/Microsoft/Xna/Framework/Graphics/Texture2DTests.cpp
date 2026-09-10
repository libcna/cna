// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

// Lets CNA_RENDERER_IS name identities bare, matching the compile-time guards it replaced.
using namespace CNA::Testing::Renderers;
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <typeinfo>
#include <utility>
#include <vector>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgr565.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra4444.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra5551.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "System/Environment.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/Environment.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::Texture;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using System::IO::MemoryStream;

namespace
{
    using CNA::Internal::Renderers::ITextureRenderer;

    template <typename TException, typename TCallable>
    void ExpectExactNamedException(TCallable&& callable, const char* parameterName)
    {
        try
        {
            callable();
            FAIL() << "expected " << typeid(TException).name();
        }
        catch (const TException& exception)
        {
            EXPECT_EQ(typeid(exception), typeid(TException));
            EXPECT_EQ(exception.getParamNameProperty(), parameterName);
        }
        catch (...)
        {
            FAIL() << "unexpected exception type; expected " << typeid(TException).name();
        }
    }

    template <typename TException, typename TCallable>
    void ExpectExactException(TCallable&& callable)
    {
        try
        {
            callable();
            FAIL() << "expected " << typeid(TException).name();
        }
        catch (const TException& exception)
        {
            EXPECT_EQ(typeid(exception), typeid(TException));
        }
        catch (...)
        {
            FAIL() << "unexpected exception type; expected " << typeid(TException).name();
        }
    }

    class RecordingMipTextureRenderer final : public ITextureRenderer
    {
    public:
        explicit RecordingMipTextureRenderer(int width, int height)
            : width_(width), height_(height)
        {
        }

        int GetWidth() const override { return width_; }
        int GetHeight() const override { return height_; }

        void UpdatePixels(const uint8_t*, int stride) override
        {
            ++levelZeroUpdates;
            levelZeroStride = stride;
        }

        void UpdatePixelsLevel(int level, const uint8_t*, int levelW, int levelH) override
        {
            levelUpdates.emplace_back(level, levelW, levelH);
        }

        bool GetData(int, int, int, int, int, void*, int) const override
        {
            ++getDataCalls;
            return false;
        }

        int levelZeroUpdates = 0;
        int levelZeroStride = 0;
        std::vector<std::tuple<int, int, int>> levelUpdates;
        mutable int getDataCalls = 0;

    private:
        int width_;
        int height_;
    };

    class NonSeekableImageStream final : public System::IO::Stream
    {
    public:
        int Read(System::IO::bytecs[], System::IO::intcs,
                 System::IO::intcs) override
        {
            ++readCalls;
            return 0;
        }

        void Close() override {}

        [[nodiscard]] System::IO::intcs getLengthProperty() const override
        {
            ++lengthCalls;
            throw System::NotSupportedException("Length");
        }

        [[nodiscard]] bool getCanSeekProperty() const override { return false; }

        mutable int lengthCalls = 0;
        int readCalls = 0;
    };

    int TestMipDimension(int base, int level)
    {
        return std::max(1, base >> level);
    }

    Color TestMipColor(int level, int index)
    {
        return Color(20 + level * 31 + index % 17,
                     40 + level * 23 + index % 19,
                     60 + level * 13 + index % 29,
                     255);
    }

    void ExpectExactColor(const Color& actual, const Color& expected)
    {
        EXPECT_EQ(actual.getRProperty(), expected.getRProperty());
        EXPECT_EQ(actual.getGProperty(), expected.getGProperty());
        EXPECT_EQ(actual.getBProperty(), expected.getBProperty());
        EXPECT_EQ(actual.getAProperty(), expected.getAProperty());
    }

    template <typename Packed>
    Packed Packed16Value(std::uint16_t value)
    {
        Packed result;
        result.setPackedValueProperty(value);
        return result;
    }

    template <typename Packed>
    void ExpectPacked16TransferContract(GraphicsDevice& device, SurfaceFormat format)
    {
        Texture2D texture(device, 4, 4, true, format);

        std::array<Packed, 16> base{};
        for (std::size_t index = 0; index < base.size(); ++index)
            base[index] = Packed16Value<Packed>(static_cast<std::uint16_t>(0x0123u + index * 0x0711u));
        texture.SetData(base.data(), static_cast<int>(base.size()));

        std::array<Packed, 16> fullReadback{};
        texture.GetData(fullReadback.data(), static_cast<int>(fullReadback.size()));
        for (std::size_t index = 0; index < base.size(); ++index)
        {
            EXPECT_EQ(fullReadback[index].getPackedValueProperty(),
                      base[index].getPackedValueProperty());
        }

        const Rectangle rectangle(1, 1, 2, 2);
        std::array<Packed, 6> patchSource{};
        for (int index = 0; index < 4; ++index)
        {
            patchSource[static_cast<std::size_t>(index + 1)] =
                Packed16Value<Packed>(static_cast<std::uint16_t>(0xF00Du - index * 0x1111u));
        }
        texture.SetData(0, &rectangle, patchSource.data(), 1, 4);

        std::array<Packed, 7> patchReadback{};
        texture.GetData(0, &rectangle, patchReadback.data(), 2, 4);
        for (int index = 0; index < 4; ++index)
        {
            EXPECT_EQ(patchReadback[static_cast<std::size_t>(index + 2)].getPackedValueProperty(),
                      patchSource[static_cast<std::size_t>(index + 1)].getPackedValueProperty());
        }

        std::array<Packed, 4> mip{{
            Packed16Value<Packed>(0x0000u), Packed16Value<Packed>(0xFFFFu),
            Packed16Value<Packed>(0x55AAu), Packed16Value<Packed>(0xAA55u),
        }};
        texture.SetData(1, nullptr, mip.data(), 0, static_cast<int>(mip.size()));

        std::array<Packed, 4> mipReadback{};
        texture.GetData(1, nullptr, mipReadback.data(), 0, static_cast<int>(mipReadback.size()));
        for (std::size_t index = 0; index < mip.size(); ++index)
        {
            EXPECT_EQ(mipReadback[index].getPackedValueProperty(),
                      mip[index].getPackedValueProperty());
        }
    }

    std::vector<std::uint8_t> DxtBytes(int blockCount, int blockBytes, std::uint8_t seed)
    {
        std::vector<std::uint8_t> result(
            static_cast<std::size_t>(blockCount) * static_cast<std::size_t>(blockBytes));
        for (std::size_t index = 0; index < result.size(); ++index)
            result[index] = static_cast<std::uint8_t>(seed + index * 29u);
        return result;
    }

    std::vector<std::uint8_t> SolidRedDxt1Dds()
    {
        std::vector<std::uint8_t> result(136u, 0u);
        const auto put32 = [&result](std::size_t offset, std::uint32_t value)
        {
            result[offset] = static_cast<std::uint8_t>(value);
            result[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
            result[offset + 2u] = static_cast<std::uint8_t>(value >> 16u);
            result[offset + 3u] = static_cast<std::uint8_t>(value >> 24u);
        };
        result[0] = 'D'; result[1] = 'D'; result[2] = 'S'; result[3] = ' ';
        put32(4u, 124u);
        put32(8u, 0x00081007u);
        put32(12u, 4u);
        put32(16u, 4u);
        put32(20u, 8u);
        put32(28u, 1u);
        put32(76u, 32u);
        put32(80u, 4u);
        result[84] = 'D'; result[85] = 'X'; result[86] = 'T'; result[87] = '1';
        put32(108u, 0x1000u);
        result[128] = 0x00u; result[129] = 0xF8u;
        result[130] = 0x00u; result[131] = 0xF8u;
        return result;
    }

    void ExpectDxtTransferContract(GraphicsDevice& device, SurfaceFormat format)
    {
        const int blockBytes = format == SurfaceFormat::Dxt1 ? 8 : 16;
        Texture2D texture(device, 8, 8, true, format);

        const std::vector<std::uint8_t> base = DxtBytes(4, blockBytes, 0x11u);
        texture.SetData(base.data(), static_cast<int>(base.size()));

        std::vector<std::uint8_t> fullReadback(base.size() + 5u, 0xCCu);
        texture.GetData(fullReadback.data(), 3, static_cast<int>(base.size()));
        EXPECT_TRUE(std::equal(base.begin(), base.end(), fullReadback.begin() + 3));

        const Rectangle rightColumn(4, 0, 4, 8);
        const std::vector<std::uint8_t> patch = DxtBytes(2, blockBytes, 0xA3u);
        std::vector<std::uint8_t> patchSource(patch.size() + 2u, 0x5Au);
        std::copy(patch.begin(), patch.end(), patchSource.begin() + 2);
        texture.SetData(0, &rightColumn, patchSource.data(), 2, static_cast<int>(patch.size()));

        std::vector<std::uint8_t> patchReadback(patch.size() + 4u, 0xC3u);
        texture.GetData(0, &rightColumn, patchReadback.data(), 1,
                        static_cast<int>(patch.size()));
        EXPECT_TRUE(std::equal(patch.begin(), patch.end(), patchReadback.begin() + 1));

        const std::vector<std::uint8_t> mip = DxtBytes(1, blockBytes, 0x47u);
        texture.SetData(1, nullptr, mip.data(), 0, static_cast<int>(mip.size()));
        std::vector<std::uint8_t> mipReadback(mip.size(), 0u);
        texture.GetData(1, nullptr, mipReadback.data(), 0,
                        static_cast<int>(mipReadback.size()));
        EXPECT_EQ(mipReadback, mip);

    }

    std::vector<std::vector<Color>> PopulateEveryMip(Texture2D& texture, int width, int height)
    {
        constexpr int kSourceStart = 3;
        std::vector<std::vector<Color>> expected;
        for (int level = 0; level < texture.getLevelCountProperty(); ++level)
        {
            const int levelW = TestMipDimension(width, level);
            const int levelH = TestMipDimension(height, level);
            const int count = levelW * levelH;
            std::vector<Color> source(static_cast<std::size_t>(kSourceStart + count + 2),
                                      Color(1, 2, 3, 4));
            expected.emplace_back();
            expected.back().reserve(static_cast<std::size_t>(count));
            for (int i = 0; i < count; ++i)
            {
                const Color value = TestMipColor(level, i);
                source[static_cast<std::size_t>(kSourceStart + i)] = value;
                expected.back().push_back(value);
            }
            const std::vector<Color> sourceBefore = source;
            texture.SetData(level, nullptr, source.data(), kSourceStart, count);
            EXPECT_EQ(source, sourceBefore) << "SetData modified its source at mip " << level;
        }
        return expected;
    }

    void ExpectEveryMipExact(Texture2D& texture, int width, int height,
                             const std::vector<std::vector<Color>>& expected)
    {
        constexpr int kDestinationStart = 4;
        constexpr int kExtraCapacity = 2;
        const Color sentinel(7, 3, 11, 199);
        ASSERT_EQ(expected.size(), static_cast<std::size_t>(texture.getLevelCountProperty()));
        for (int level = 0; level < texture.getLevelCountProperty(); ++level)
        {
            const int levelW = TestMipDimension(width, level);
            const int levelH = TestMipDimension(height, level);
            const int count = levelW * levelH;
            ASSERT_EQ(expected[static_cast<std::size_t>(level)].size(),
                      static_cast<std::size_t>(count));
            std::vector<Color> destination(
                static_cast<std::size_t>(kDestinationStart + count + kExtraCapacity + 3), sentinel);
            texture.GetData(level, nullptr, destination.data(), kDestinationStart,
                            count);
            for (int i = 0; i < count; ++i)
            {
                SCOPED_TRACE("mip=" + std::to_string(level) + " index=" + std::to_string(i));
                ExpectExactColor(destination[static_cast<std::size_t>(kDestinationStart + i)],
                                 expected[static_cast<std::size_t>(level)][static_cast<std::size_t>(i)]);
            }
            for (int i = 0; i < kDestinationStart; ++i)
                ExpectExactColor(destination[static_cast<std::size_t>(i)], sentinel);
            for (std::size_t i = static_cast<std::size_t>(kDestinationStart + count);
                 i < destination.size(); ++i)
                ExpectExactColor(destination[i], sentinel);
        }
    }
}

// -----------------------------------------------------------------------
// Default constructor — dimensions and base-class properties
// -----------------------------------------------------------------------

TEST(Texture2DTest, DefaultConstructorWidthIsZero)
{
    Texture2D tex;
    EXPECT_EQ(tex.getWidthProperty(), 0);
}

TEST(Texture2DTest, DefaultConstructorHeightIsZero)
{
    Texture2D tex;
    EXPECT_EQ(tex.getHeightProperty(), 0);
}

TEST(Texture2DTest, DefaultConstructorFormatIsColor)
{
    Texture2D tex;
    EXPECT_EQ(tex.getFormatProperty(), SurfaceFormat::Color);
}

TEST(Texture2DTest, DefaultConstructorLevelCountIsOne)
{
    Texture2D tex;
    EXPECT_EQ(tex.getLevelCountProperty(), 1);
}

// SOFTWARE-275: byte is an ordinary XNA SetData<T>/GetData<T> element type, not a CNA-only
// one-channel texture route. FNA measures elementCount in bytes here and applies startIndex to the
// caller's array, including for a four-byte Color texel.
TEST(Texture2DTest, ByteTransfersUseByteCountsAndCallerArrayWindowsForClassicFormats)
{
    GraphicsDevice device;
    Texture2D texture(device, 2, 1, false, SurfaceFormat::Color);
    const std::array<std::uint8_t, 12> source{
        0xEEu, 0xEEu, 0xEEu,
        0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u, 0x88u,
        0xEEu};
    texture.SetData(0, nullptr, source.data(), 3, 8);

    std::array<std::uint8_t, 14> destination{};
    destination.fill(0xCDu);
    texture.GetData(0, nullptr, destination.data(), 4, 8);
    EXPECT_TRUE(std::equal(source.begin() + 3, source.begin() + 11,
                           destination.begin() + 4));
    EXPECT_TRUE(std::all_of(destination.begin(), destination.begin() + 4,
                            [](std::uint8_t value) { return value == 0xCDu; }));
    EXPECT_TRUE(std::all_of(destination.begin() + 12, destination.end(),
                            [](std::uint8_t value) { return value == 0xCDu; }));
}

// -----------------------------------------------------------------------
// LevelCount — mipmapped vs non-mipmapped construction (Task 267)
//
// FNA's Texture.CalculateMipLevels formula: levels = 1 + the number of times
// max(width, height) can be halved (integer division) before reaching 1.
// Expected values below are computed by hand-tracing that formula.
// -----------------------------------------------------------------------

class LevelCountTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
};

TEST_F(LevelCountTest, ConstructorsRejectNonPositiveDimensionsBeforeRendererAllocation)
{
    EXPECT_THROW((void)Texture2D(gd, 0, 1), System::ArgumentOutOfRangeException);
    EXPECT_THROW((void)Texture2D(gd, -1, 1), System::ArgumentOutOfRangeException);
    EXPECT_THROW((void)Texture2D(gd, 1, 0), System::ArgumentOutOfRangeException);
    EXPECT_THROW((void)Texture2D(gd, 1, -1), System::ArgumentOutOfRangeException);
    EXPECT_THROW((void)Texture2D(gd, 0, 1, true, SurfaceFormat::Color),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW((void)Texture2D(gd, 1, 0, true, SurfaceFormat::Color),
                 System::ArgumentOutOfRangeException);
}

TEST_F(LevelCountTest, SimpleTwoArgConstructorIsAlwaysOne)
{
    // Texture2D(device, w, h) always matches FNA's mipMap=false delegating overload.
    EXPECT_EQ(Texture2D(gd, 8, 8).getLevelCountProperty(), 1);
    EXPECT_EQ(Texture2D(gd, 3, 5).getLevelCountProperty(), 1);
    EXPECT_EQ(Texture2D(gd, 1, 1).getLevelCountProperty(), 1);
}

TEST_F(LevelCountTest, MipMapFalseIsAlwaysOneRegardlessOfSize)
{
    EXPECT_EQ(Texture2D(gd, 8, 8, false, SurfaceFormat::Color).getLevelCountProperty(), 1);
    EXPECT_EQ(Texture2D(gd, 100, 37, false, SurfaceFormat::Color).getLevelCountProperty(), 1);
    EXPECT_EQ(Texture2D(gd, 1, 1, false, SurfaceFormat::Color).getLevelCountProperty(), 1);
}

// TINYGL and NANOVG store and sample level 0 only, so any request that would actually produce a
// mip chain is refused at construction (their own mipLevels != 1 guards) rather than silently
// collapsed to one level -- NanoVG additionally has no per-level upload entry point at all, so
// accepting one would leave Texture2D reporting storage that does not exist. A single-level
// request is still an ordinary success, which is why the 1x1 case below stays an equality
// assertion on every renderer.
TEST_F(LevelCountTest, MipMapTrueSquarePowerOfTwo)
{
    EXPECT_EQ(Texture2D(gd, 1, 1, true, SurfaceFormat::Color).getLevelCountProperty(), 1);
#if defined(CNA_RENDERER_TINYGL) || defined(CNA_RENDERER_NANOVG)
    EXPECT_THROW(Texture2D(gd, 2, 2, true, SurfaceFormat::Color), System::NotSupportedException);
    EXPECT_THROW(Texture2D(gd, 4, 4, true, SurfaceFormat::Color), System::NotSupportedException);
    EXPECT_THROW(Texture2D(gd, 16, 16, true, SurfaceFormat::Color), System::NotSupportedException);
#else
    EXPECT_EQ(Texture2D(gd, 2, 2, true, SurfaceFormat::Color).getLevelCountProperty(), 2);
    EXPECT_EQ(Texture2D(gd, 4, 4, true, SurfaceFormat::Color).getLevelCountProperty(), 3);
    EXPECT_EQ(Texture2D(gd, 16, 16, true, SurfaceFormat::Color).getLevelCountProperty(), 5);
#endif
}

TEST_F(LevelCountTest, MipMapTrueNonSquarePowerOfTwo)
{
#if defined(CNA_RENDERER_TINYGL) || defined(CNA_RENDERER_NANOVG)
    EXPECT_THROW(Texture2D(gd, 8, 4, true, SurfaceFormat::Color), System::NotSupportedException);
    EXPECT_THROW(Texture2D(gd, 1, 8, true, SurfaceFormat::Color), System::NotSupportedException);
#else
    EXPECT_EQ(Texture2D(gd, 8, 4, true, SurfaceFormat::Color).getLevelCountProperty(), 4);
    EXPECT_EQ(Texture2D(gd, 1, 8, true, SurfaceFormat::Color).getLevelCountProperty(), 4);
#endif
}

TEST_F(LevelCountTest, MipMapTrueNonPowerOfTwo)
{
    gd.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
#if defined(CNA_RENDERER_TINYGL) || defined(CNA_RENDERER_NANOVG)
    EXPECT_THROW(Texture2D(gd, 3, 5, true, SurfaceFormat::Color), System::NotSupportedException);
    EXPECT_THROW(Texture2D(gd, 7, 11, true, SurfaceFormat::Color), System::NotSupportedException);
#else
    EXPECT_EQ(Texture2D(gd, 3, 5, true, SurfaceFormat::Color).getLevelCountProperty(), 3);
    EXPECT_EQ(Texture2D(gd, 7, 11, true, SurfaceFormat::Color).getLevelCountProperty(), 4);
#endif
}

TEST_F(LevelCountTest, NpotFullPartialRowsAndEveryMipRoundTripExactly)
{
    gd.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
#if defined(CNA_RENDERER_TINYGL) || defined(CNA_RENDERER_NANOVG)
    GTEST_SKIP() << "this renderer deliberately has no mipmapped Texture2D storage";
#else
    constexpr int width = 3;
    constexpr int height = 5;
    Texture2D texture(gd, width, height, true, SurfaceFormat::Color);
    ASSERT_EQ(texture.getLevelCountProperty(), 3);

    std::vector<Color> expected(static_cast<std::size_t>(width * height));
    for (int index = 0; index < width * height; ++index)
        expected[static_cast<std::size_t>(index)] = TestMipColor(0, index);
    std::vector<Color> baseSource(expected.size() + 3u, Color(1, 2, 3, 4));
    std::copy(expected.begin(), expected.end(), baseSource.begin() + 2);
    texture.SetData(0, nullptr, baseSource.data(), 2, width * height);

    const Rectangle patchRectangle(1, 1, 2, 3);
    std::array<Color, 8> patchSource{};
    for (int index = 0; index < 6; ++index)
    {
        patchSource[static_cast<std::size_t>(index + 1)] =
            Color(180 + index, 90 + index * 2, 30 + index * 3, 255);
        const int x = patchRectangle.X + index % patchRectangle.Width;
        const int y = patchRectangle.Y + index / patchRectangle.Width;
        expected[static_cast<std::size_t>(y * width + x)] =
            patchSource[static_cast<std::size_t>(index + 1)];
    }
    texture.SetData(0, &patchRectangle, patchSource.data(), 1, 6);

    const Color sentinel(7, 3, 11, 199);
    std::vector<Color> fullReadback(expected.size() + 5u, sentinel);
    texture.GetData(fullReadback.data(), 3, width * height);
    for (std::size_t index = 0; index < expected.size(); ++index)
        ExpectExactColor(fullReadback[index + 3u], expected[index]);
    for (std::size_t index = 0; index < 3u; ++index)
        ExpectExactColor(fullReadback[index], sentinel);
    for (std::size_t index = expected.size() + 3u; index < fullReadback.size(); ++index)
        ExpectExactColor(fullReadback[index], sentinel);

    std::array<Color, 9> patchReadback{};
    patchReadback.fill(sentinel);
    texture.GetData(0, &patchRectangle, patchReadback.data(), 2, 6);
    for (int index = 0; index < 6; ++index)
    {
        ExpectExactColor(patchReadback[static_cast<std::size_t>(index + 2)],
                         patchSource[static_cast<std::size_t>(index + 1)]);
    }
    ExpectExactColor(patchReadback[0], sentinel);
    ExpectExactColor(patchReadback[1], sentinel);
    ExpectExactColor(patchReadback[8], sentinel);

    const std::array<Color, 3> mipOneSource{{
        sentinel, Color(21, 43, 65, 87), Color(123, 145, 167, 189),
    }};
    texture.SetData(1, nullptr, mipOneSource.data(), 1, 2);
    std::array<Color, 4> mipOneReadback{};
    mipOneReadback.fill(sentinel);
    texture.GetData(1, nullptr, mipOneReadback.data(), 1, 2);
    ExpectExactColor(mipOneReadback[0], sentinel);
    ExpectExactColor(mipOneReadback[1], mipOneSource[1]);
    ExpectExactColor(mipOneReadback[2], mipOneSource[2]);
    ExpectExactColor(mipOneReadback[3], sentinel);

    const Color mipTwoSource(9, 19, 29, 39);
    texture.SetData(2, nullptr, &mipTwoSource, 0, 1);
    Color mipTwoReadback(0, 0, 0, 0);
    texture.GetData(2, nullptr, &mipTwoReadback, 0, 1);
    ExpectExactColor(mipTwoReadback, mipTwoSource);
#endif
}

// -----------------------------------------------------------------------
// REMED-GFX-192 -- one shared [0, LevelCount) validation path must run before mip dimensions,
// CPU shadows, allocation, or renderer dispatch. A real mipmapped Texture2D proves every valid
// level's dimensions and bytes, while the existing CPU-only recording-renderer factory proves that
// rejected SetData calls never dispatch even on the single-level boundary.
// -----------------------------------------------------------------------

TEST(Texture2DMipLevelValidationTest, EveryValidMipKeepsItsDimensionsContentsAndTransferWindow)
{
    constexpr int kWidth = 13;
    constexpr int kHeight = 7;
    GraphicsDevice gd;
    gd.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
#if defined(CNA_RENDERER_TINYGL) || defined(CNA_RENDERER_NANOVG)
    // These renderers own level 0 only, so the mipmapped texture this test needs cannot be
    // constructed at all -- the refusal itself is the contract worth asserting here (see
    // LevelCountTest above).
    EXPECT_THROW(Texture2D(gd, kWidth, kHeight, true, SurfaceFormat::Color),
                 System::NotSupportedException);
    GTEST_SKIP() << "this renderer stores level 0 only -- no mip chain exists to walk";
#else
    constexpr int kLevelCount = 4;
    Texture2D texture(gd, kWidth, kHeight, true, SurfaceFormat::Color);
    ASSERT_EQ(texture.getLevelCountProperty(), kLevelCount);

    const std::vector<std::vector<Color>> expected =
        PopulateEveryMip(texture, kWidth, kHeight);
    ExpectEveryMipExact(texture, kWidth, kHeight, expected);
#endif
}

TEST(Texture2DMipLevelValidationTest, RejectedSetDataLeavesEveryValidMipAndItsSourceUnchanged)
{
    constexpr int kWidth = 13;
    constexpr int kHeight = 7;
    constexpr int kLevelCount = 1;
    auto renderer = std::make_shared<RecordingMipTextureRenderer>(kWidth, kHeight);
    Texture2D texture = Texture2D::CreateWithRendererForTests(kWidth, kHeight, renderer);
    const std::vector<std::vector<Color>> expected =
        PopulateEveryMip(texture, kWidth, kHeight);

    const int levelZeroUpdatesBefore = renderer->levelZeroUpdates;
    const std::vector<std::tuple<int, int, int>> levelUpdatesBefore = renderer->levelUpdates;
    const std::array<int, 4> invalidLevels = {
        -1, kLevelCount, 1000, std::numeric_limits<int>::max()
    };
    for (int level : invalidLevels)
    {
        SCOPED_TRACE("level=" + std::to_string(level));
        std::vector<Color> source(6, Color(201, 111, 77, 255));
        const std::vector<Color> sourceBefore = source;
        ExpectExactException<System::InvalidOperationException>(
            [&] { texture.SetData(level, nullptr, source.data(), 2, 1); });
        EXPECT_EQ(source, sourceBefore);
        EXPECT_EQ(renderer->levelZeroUpdates, levelZeroUpdatesBefore);
        EXPECT_EQ(renderer->levelUpdates, levelUpdatesBefore);
    }

    ExpectEveryMipExact(texture, kWidth, kHeight, expected);
    EXPECT_EQ(renderer->levelZeroUpdates, levelZeroUpdatesBefore);
    EXPECT_EQ(renderer->levelUpdates, levelUpdatesBefore);
    EXPECT_EQ(renderer->getDataCalls, 0);
}

TEST(Texture2DMipLevelValidationTest, RejectedGetDataLeavesDestinationAndRendererUntouched)
{
    constexpr int kWidth = 13;
    constexpr int kHeight = 7;
    constexpr int kLevelCount = 1;
    auto renderer = std::make_shared<RecordingMipTextureRenderer>(kWidth, kHeight);
    Texture2D texture = Texture2D::CreateWithRendererForTests(kWidth, kHeight, renderer);
    const Color sentinel(7, 3, 11, 199);
    const Rectangle one(0, 0, 1, 1);
    const std::array<int, 4> invalidLevels = {
        -1, kLevelCount, 1000, std::numeric_limits<int>::max()
    };

    for (std::size_t request = 0; request < invalidLevels.size(); ++request)
    {
        const int level = invalidLevels[request];
        SCOPED_TRACE("level=" + std::to_string(level));
        std::vector<Color> destination(7, sentinel);
        const Rectangle* rect = (request % 2 == 0) ? &one : nullptr;
        ExpectExactException<System::InvalidOperationException>(
            [&] { texture.GetData(level, rect, destination.data(), 3, 1); });
        for (const Color& value : destination) ExpectExactColor(value, sentinel);
        EXPECT_EQ(renderer->getDataCalls, 0);
    }
}

TEST(Texture2DMipLevelValidationTest, NullPrecedesLevelButLevelPrecedesCopyWindowValidation)
{
    constexpr int kLevelCount = 1;
    auto renderer = std::make_shared<RecordingMipTextureRenderer>(13, 7);
    Texture2D texture = Texture2D::CreateWithRendererForTests(13, 7, renderer);
    Color value(1, 2, 3, 4);

    ExpectExactNamedException<System::ArgumentNullException>(
        [&] { texture.GetData(kLevelCount, nullptr, nullptr, -1, 0); }, "data");
    ExpectExactNamedException<System::ArgumentNullException>(
        [&] { texture.SetData(kLevelCount, nullptr, nullptr, -1, 0); }, "data");
    ExpectExactException<System::InvalidOperationException>(
        [&] { texture.GetData(kLevelCount, nullptr, &value, -1, 1); });
    ExpectExactException<System::InvalidOperationException>(
        [&] { texture.SetData(kLevelCount, nullptr, &value, -1, 1); });
    EXPECT_EQ(renderer->getDataCalls, 0);
    EXPECT_EQ(renderer->levelZeroUpdates, 0);
    EXPECT_TRUE(renderer->levelUpdates.empty());
}

TEST(Texture2DMipLevelValidationTest, CopyWindowPrecedesElementWidthAndRectangleValidation)
{
    struct EightByteValue final
    {
        std::uint64_t value;
    };

    auto renderer = std::make_shared<RecordingMipTextureRenderer>(2, 2);
    Texture2D texture = Texture2D::CreateWithRendererForTests(2, 2, renderer);
    EightByteValue value{};
    const Rectangle outside(2, 0, 1, 1);

    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { texture.SetData(0, &outside, &value, -1, 1); }, "dataIndex");
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { texture.GetData(0, &outside, &value, -1, 1); }, "dataIndex");
    ExpectExactNamedException<System::ArgumentException>(
        [&] { texture.SetData(0, &outside, &value, 0, 1); }, "");
    ExpectExactNamedException<System::ArgumentException>(
        [&] { texture.GetData(0, &outside, &value, 0, 1); }, "");
    EXPECT_EQ(renderer->getDataCalls, 0);
    EXPECT_EQ(renderer->levelZeroUpdates, 0);
    EXPECT_TRUE(renderer->levelUpdates.empty());
}

// -----------------------------------------------------------------------
// Unsupported SurfaceFormat construction — must throw clearly, never
// silently fall back to RGBA8 (Task 176 established the pattern; Task 286
// closes the gap for the two bump-map formats it left uncovered).
// -----------------------------------------------------------------------

class UnsupportedFormatConstructionTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
};

TEST_F(UnsupportedFormatConstructionTest, NormalizedByte2Throws)
{
    // The two signed-normalized byte formats differ only in channel count, and EasyGL stores
    // them through one branch -- EasyGL widens the two-channel form to RGBA8_SNORM so every
    // stock-effect shader sees XNA's missing-channel expansion, while Software retains signed
    // float samples. Both are real format implementations, so this list is deliberately the
    // same one NormalizedByte4Throws uses.
    if (CNA_RENDERER_IS(OpenGLES3, OpenGL33, WebGL2, Software))
    {
        EXPECT_NO_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::NormalizedByte2));
    }
    else
    {
        EXPECT_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::NormalizedByte2), std::runtime_error);
    }
}

TEST_F(UnsupportedFormatConstructionTest, NormalizedByte4Throws)
{
    if (CNA_RENDERER_IS(OpenGLES3, OpenGL33, WebGL2, Software))
    {
        EXPECT_NO_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::NormalizedByte4));
    }
    else
    {
        EXPECT_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::NormalizedByte4), std::runtime_error);
    }
}

TEST_F(UnsupportedFormatConstructionTest, Bgra5551Throws)
{
    // REMED-GFX-244 promoted the packed 16-bit formats on EasyGL's ES 3 generation too.
    if (CNA_RENDERER_IS(OpenGLES3, OpenGL33, WebGL2, Software))
    {
        EXPECT_NO_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::Bgra5551));
    }
    else
    {
        EXPECT_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::Bgra5551), std::runtime_error);
    }
}

TEST_F(UnsupportedFormatConstructionTest, Packed16FullPartialAndMipTransfersAreExact)
{
    if (!CNA_RENDERER_IS(OpenGLES3, OpenGL33, WebGL2, Software))
        GTEST_SKIP() << "The active renderer has not promoted packed-16 Texture2D storage";

    using namespace Microsoft::Xna::Framework::Graphics::PackedVector;
    ExpectPacked16TransferContract<Bgr565>(gd, SurfaceFormat::Bgr565);
    ExpectPacked16TransferContract<Bgra5551>(gd, SurfaceFormat::Bgra5551);
    ExpectPacked16TransferContract<Bgra4444>(gd, SurfaceFormat::Bgra4444);
}

TEST_F(UnsupportedFormatConstructionTest, DxtFullPartialAndMipTransfersAreExact)
{
    if (!gd.GetRenderer().IsCompressedTransferFormatEXT(static_cast<int>(SurfaceFormat::Dxt1)) ||
        !gd.GetRenderer().IsCompressedTransferFormatEXT(static_cast<int>(SurfaceFormat::Dxt3)) ||
        !gd.GetRenderer().IsCompressedTransferFormatEXT(static_cast<int>(SurfaceFormat::Dxt5)))
        GTEST_SKIP() << "The active renderer does not preserve DXT texture blocks";

    ExpectDxtTransferContract(gd, SurfaceFormat::Dxt1);
    ExpectDxtTransferContract(gd, SurfaceFormat::Dxt3);
    ExpectDxtTransferContract(gd, SurfaceFormat::Dxt5);
}

TEST_F(UnsupportedFormatConstructionTest, CompressedRegionEndpointOverflowThrowsCleanly)
{
    if (!gd.GetRenderer().IsCompressedTransferFormatEXT(
            static_cast<int>(SurfaceFormat::Dxt1)))
        GTEST_SKIP() << "The active renderer does not preserve DXT texture blocks";

    Texture2D texture(gd, 4, 4, false, SurfaceFormat::Dxt1);
    std::array<std::uint8_t, 8> bytes{};
    const Rectangle overflowingX(std::numeric_limits<int>::max(), 0, 4, 4);
    const Rectangle overflowingY(0, std::numeric_limits<int>::max(), 4, 4);

    ExpectExactNamedException<System::ArgumentException>(
        [&] { texture.SetData(0, &overflowingX, bytes.data(), 0, 8); }, "rect");
    ExpectExactNamedException<System::ArgumentException>(
        [&] { texture.SetData(0, &overflowingY, bytes.data(), 0, 8); }, "rect");
    ExpectExactNamedException<System::ArgumentException>(
        [&] { texture.GetData(0, &overflowingX, bytes.data(), 0, 8); }, "rect");
    ExpectExactNamedException<System::ArgumentException>(
        [&] { texture.GetData(0, &overflowingY, bytes.data(), 0, 8); }, "rect");
}

TEST_F(UnsupportedFormatConstructionTest, SingleThrows)
{
    // REMED-GFX-242: this fixture's device is GraphicsProfile.Reach, which excludes this
    // format -- measured on the real XNA 4.0 runtime, where Reach refuses it with
    // NotSupportedException and HiDef accepts it. The refusal is the PROFILE's and so is
    // unconditional here; whether a renderer could carry it is asked on HiDef below.
    EXPECT_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::Single),
                 System::NotSupportedException);
}

TEST_F(UnsupportedFormatConstructionTest, Vector2Throws)
{
    // REMED-GFX-242: this fixture's device is GraphicsProfile.Reach, which excludes this
    // format -- measured on the real XNA 4.0 runtime, where Reach refuses it with
    // NotSupportedException and HiDef accepts it. The refusal is the PROFILE's and so is
    // unconditional here; whether a renderer could carry it is asked on HiDef below.
    EXPECT_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::Vector2),
                 System::NotSupportedException);
}

TEST_F(UnsupportedFormatConstructionTest, Vector4Throws)
{
    // REMED-GFX-242: this fixture's device is GraphicsProfile.Reach, which excludes this
    // format -- measured on the real XNA 4.0 runtime, where Reach refuses it with
    // NotSupportedException and HiDef accepts it. The refusal is the PROFILE's and so is
    // unconditional here; whether a renderer could carry it is asked on HiDef below.
    EXPECT_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::Vector4),
                 System::NotSupportedException);
}

TEST_F(UnsupportedFormatConstructionTest, HalfSingleThrows)
{
    // REMED-GFX-242: this fixture's device is GraphicsProfile.Reach, which excludes this
    // format -- measured on the real XNA 4.0 runtime, where Reach refuses it with
    // NotSupportedException and HiDef accepts it. The refusal is the PROFILE's and so is
    // unconditional here; whether a renderer could carry it is asked on HiDef below.
    EXPECT_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::HalfSingle),
                 System::NotSupportedException);
}

TEST_F(UnsupportedFormatConstructionTest, HalfVector2Throws)
{
    // REMED-GFX-242: this fixture's device is GraphicsProfile.Reach, which excludes this
    // format -- measured on the real XNA 4.0 runtime, where Reach refuses it with
    // NotSupportedException and HiDef accepts it. The refusal is the PROFILE's and so is
    // unconditional here; whether a renderer could carry it is asked on HiDef below.
    EXPECT_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::HalfVector2),
                 System::NotSupportedException);
}

TEST_F(UnsupportedFormatConstructionTest, HalfVector4Throws)
{
    // REMED-GFX-242: this fixture's device is GraphicsProfile.Reach, which excludes this
    // format -- measured on the real XNA 4.0 runtime, where Reach refuses it with
    // NotSupportedException and HiDef accepts it. The refusal is the PROFILE's and so is
    // unconditional here; whether a renderer could carry it is asked on HiDef below.
    EXPECT_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::HalfVector4),
                 System::NotSupportedException);
}

TEST_F(UnsupportedFormatConstructionTest, HdrBlendableThrows)
{
    // REMED-GFX-242: this fixture's device is GraphicsProfile.Reach, which excludes this
    // format -- measured on the real XNA 4.0 runtime, where Reach refuses it with
    // NotSupportedException and HiDef accepts it. The refusal is the PROFILE's and so is
    // unconditional here; whether a renderer could carry it is asked on HiDef below.
    EXPECT_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::HdrBlendable),
                 System::NotSupportedException);
}

TEST_F(UnsupportedFormatConstructionTest, Rgba1010102Throws)
{
    // REMED-GFX-242: this fixture's device is GraphicsProfile.Reach, which excludes this
    // format -- measured on the real XNA 4.0 runtime, where Reach refuses it with
    // NotSupportedException and HiDef accepts it. The refusal is the PROFILE's and so is
    // unconditional here; whether a renderer could carry it is asked on HiDef below.
    EXPECT_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::Rgba1010102),
                 System::NotSupportedException);
}

TEST_F(UnsupportedFormatConstructionTest, Rgba64Throws)
{
    // REMED-GFX-242: this fixture's device is GraphicsProfile.Reach, which excludes this
    // format -- measured on the real XNA 4.0 runtime, where Reach refuses it with
    // NotSupportedException and HiDef accepts it. The refusal is the PROFILE's and so is
    // unconditional here; whether a renderer could carry it is asked on HiDef below.
    EXPECT_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::Rgba64),
                 System::NotSupportedException);
}


/// REMED-GFX-242: the same formats on a HiDef device, where the profile no longer refuses them and
/// the verdict belongs to the renderer alone. This is where a renderer's promoted set is exercised
/// -- moving it here is what keeps SKIA-138 and IGL-71 covered once Reach refuses these outright.
class HiDefFormatConstructionTest : public ::testing::Test
{
protected:
    GraphicsDevice gd{GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                      PresentationParameters()};

    void ExpectConstructionMatchesRenderer(SurfaceFormat format)
    {
        const auto verdict = gd.GetRenderer().ClassifySurfaceFormatEXT(static_cast<int>(format));
        if (verdict == CNA::Internal::Renderers::RendererFormatVerdict::Supported)
            EXPECT_NO_THROW(Texture2D(gd, 2, 2, false, format));
        else
            EXPECT_THROW(Texture2D(gd, 2, 2, false, format), std::runtime_error);
    }
};

TEST_F(HiDefFormatConstructionTest, TheProfileItselfRefusesNothing)
{
    // The new gate must be profile-sensitive rather than a second capability list: on HiDef every
    // one of the eleven passes the profile, so any refusal that remains is the renderer's own and
    // carries the renderer's exception type, not NotSupportedException.
    for (const SurfaceFormat fmt : {SurfaceFormat::Rgba1010102, SurfaceFormat::Rg32,
                                    SurfaceFormat::Rgba64,      SurfaceFormat::Alpha8,
                                    SurfaceFormat::Single,      SurfaceFormat::Vector2,
                                    SurfaceFormat::Vector4,     SurfaceFormat::HalfSingle,
                                    SurfaceFormat::HalfVector2, SurfaceFormat::HalfVector4,
                                    SurfaceFormat::HdrBlendable})
    {
        SCOPED_TRACE(static_cast<int>(fmt));
        EXPECT_TRUE(Texture::IsFormatAllowedByProfileEXT(GraphicsProfile::HiDef, fmt));
        try
        {
            Texture2D texture(gd, 2, 2, false, fmt);
        }
        catch (const System::NotSupportedException& e)
        {
            ADD_FAILURE() << "HiDef must not refuse on profile grounds: " << e.what();
        }
        catch (const std::exception&)
        {
            // The renderer cannot carry it. That is a legitimate answer and not this test's
            // business -- the per-format cases below say which renderers can.
        }
    }
}

TEST_F(HiDefFormatConstructionTest, SingleIsTheRenderersCallOnHiDef)
{
    ExpectConstructionMatchesRenderer(SurfaceFormat::Single);
}

TEST_F(HiDefFormatConstructionTest, Vector2IsTheRenderersCallOnHiDef)
{
    ExpectConstructionMatchesRenderer(SurfaceFormat::Vector2);
}

TEST_F(HiDefFormatConstructionTest, Vector4IsTheRenderersCallOnHiDef)
{
    ExpectConstructionMatchesRenderer(SurfaceFormat::Vector4);
}

TEST_F(HiDefFormatConstructionTest, HalfSingleIsTheRenderersCallOnHiDef)
{
    ExpectConstructionMatchesRenderer(SurfaceFormat::HalfSingle);
}

TEST_F(HiDefFormatConstructionTest, HalfVector2IsTheRenderersCallOnHiDef)
{
    ExpectConstructionMatchesRenderer(SurfaceFormat::HalfVector2);
}

TEST_F(HiDefFormatConstructionTest, HalfVector4IsTheRenderersCallOnHiDef)
{
    ExpectConstructionMatchesRenderer(SurfaceFormat::HalfVector4);
}

TEST_F(HiDefFormatConstructionTest, HdrBlendableIsTheRenderersCallOnHiDef)
{
    ExpectConstructionMatchesRenderer(SurfaceFormat::HdrBlendable);
}

TEST_F(HiDefFormatConstructionTest, Rgba1010102IsTheRenderersCallOnHiDef)
{
    ExpectConstructionMatchesRenderer(SurfaceFormat::Rgba1010102);
}

TEST_F(HiDefFormatConstructionTest, Rgba64IsTheRenderersCallOnHiDef)
{
    ExpectConstructionMatchesRenderer(SurfaceFormat::Rgba64);
}

TEST_F(HiDefFormatConstructionTest, Rg32IsTheRenderersCallOnHiDef)
{
    ExpectConstructionMatchesRenderer(SurfaceFormat::Rg32);
}

TEST_F(HiDefFormatConstructionTest, Alpha8IsTheRenderersCallOnHiDef)
{
    ExpectConstructionMatchesRenderer(SurfaceFormat::Alpha8);
}

TEST_F(HiDefFormatConstructionTest, GenericValueTypeRoundTripsAnExactSourceAndDestinationWindow)
{
    struct RawWord
    {
        std::uint16_t low;
        std::uint16_t high;
        bool operator==(const RawWord&) const = default;
    };
    static_assert(std::is_trivially_copyable_v<RawWord>);
    static_assert(sizeof(RawWord) == 4);

    Texture2D texture(gd, 2, 1, false, SurfaceFormat::Color);
    const std::array<RawWord, 4> source{{
        {0xEEEEu, 0xEEEEu}, {0x0123u, 0x4567u},
        {0x89ABu, 0xCDEFu}, {0xDDDDu, 0xDDDDu},
    }};
    texture.SetData(0, nullptr, source.data(), 1, 2);

    const RawWord sentinel{0xBEEFu, 0xCAFEu};
    std::array<RawWord, 4> destination{{sentinel, sentinel, sentinel, sentinel}};
    texture.GetData(0, nullptr, destination.data(), 1, 2);
    EXPECT_EQ(destination[0], sentinel);
    EXPECT_EQ(destination[1], source[1]);
    EXPECT_EQ(destination[2], source[2]);
    EXPECT_EQ(destination[3], sentinel);
}

TEST_F(HiDefFormatConstructionTest, ScalarFloatElementsSpanOneVector4Texel)
{
    if (gd.GetRenderer().ClassifySurfaceFormatEXT(static_cast<int>(SurfaceFormat::Vector4)) !=
        CNA::Internal::Renderers::RendererFormatVerdict::Supported)
    {
        GTEST_SKIP() << "The active renderer cannot allocate a Vector4 texture";
    }

    Texture2D texture(gd, 1, 1, false, SurfaceFormat::Vector4);
    const std::array<float, 4> source{{1.25f, -2.5f, 3.75f, -4.125f}};
    texture.SetData(source.data(), static_cast<int>(source.size()));
    std::array<float, 4> destination{};
    texture.GetData(destination.data(), static_cast<int>(destination.size()));
    EXPECT_EQ(destination, source);
}

TEST_F(HiDefFormatConstructionTest, TotalByteCountMustBeExactAndElementWidthMustDivideFormat)
{
    Texture2D texture(gd, 1, 1, false, SurfaceFormat::Color);
    const std::array<Color, 2> colors{{Color::Red, Color::Blue}};
    std::array<Color, 2> destination{};
    EXPECT_THROW(texture.SetData(colors.data(), 2), System::ArgumentException);
    EXPECT_THROW(texture.GetData(destination.data(), 2), System::ArgumentException);

    std::uint64_t tooWide = 0;
    EXPECT_THROW(texture.SetData(&tooWide, 1), System::ArgumentException);
    EXPECT_THROW(texture.GetData(&tooWide, 1), System::ArgumentException);
}

// Task 290: exhaustive sweep over every SurfaceFormat value. This stays correct automatically if
// SurfaceFormat grows because every entry is listed explicitly rather than assumed.
TEST_F(UnsupportedFormatConstructionTest, EverySurfaceFormatEitherWorksOrThrowsClearly)
{
    static const SurfaceFormat kAllFormats[] = {
        SurfaceFormat::Color,
        SurfaceFormat::Bgr565,
        SurfaceFormat::Bgra5551,
        SurfaceFormat::Bgra4444,
        SurfaceFormat::Dxt1,
        SurfaceFormat::Dxt3,
        SurfaceFormat::Dxt5,
        SurfaceFormat::NormalizedByte2,
        SurfaceFormat::NormalizedByte4,
        SurfaceFormat::Rgba1010102,
        SurfaceFormat::Rg32,
        SurfaceFormat::Rgba64,
        SurfaceFormat::Alpha8,
        SurfaceFormat::Single,
        SurfaceFormat::Vector2,
        SurfaceFormat::Vector4,
        SurfaceFormat::HalfSingle,
        SurfaceFormat::HalfVector2,
        SurfaceFormat::HalfVector4,
        SurfaceFormat::HdrBlendable,
        SurfaceFormat::ColorBgraEXT,
        SurfaceFormat::ColorSrgbEXT,
        SurfaceFormat::Dxt5SrgbEXT,
        SurfaceFormat::Bc7EXT,
        SurfaceFormat::Bc7SrgbEXT,
        SurfaceFormat::ByteEXT,
        SurfaceFormat::UShortEXT,
    };

    for (SurfaceFormat format : kAllFormats)
    {
        // plans/plan_runtimerenderer.md RTR-P9-4: the Skia-promoted format list, evaluated at runtime so
        // this assertion describes the ACTIVE renderer rather than the build default.
        const bool skia = false;
        // plans/plan_igl.md IGL-71: IGL's promoted set is deliberately two formats wide, not a mirror of
        // everything it can store. A format is here only once the whole public path is verified end
        // to end on both its backends, and only if its texel is a multiple of four bytes -- the
        // framework's own transfer rule, which ByteEXT, UShortEXT and HalfSingle would break.
        const bool igl = CNA_RENDERER_IS(Igl);
        const bool easyGlSignedNormalized =
            CNA_RENDERER_IS(OpenGLES3, OpenGL33, WebGL2, Software);
        // REMED-GFX-244: the packed 16-bit formats Reach permits, promoted on the same ES 3
        // generation the signed-normalized pair needs and verified by a real sampled draw
        // (EasyGL_Packed16Format) rather than by a readback, which this renderer serves from a CPU
        // copy and which therefore cannot see a wrong channel order.
        const bool packed16Renderer = CNA_RENDERER_IS(OpenGLES3, OpenGL33, WebGL2, Software);
        // REMED-GFX-242: this fixture's device is Reach, and a format the profile excludes is
        // refused however capable the renderer is -- so the profile is a factor of "supported",
        // not an alternative to it.
        const bool profileAllows =
            Texture::IsFormatAllowedByProfileEXT(GraphicsProfile::Reach, format);
        const bool supported = profileAllows && (format == SurfaceFormat::Color
            || (easyGlSignedNormalized && (format == SurfaceFormat::NormalizedByte4
                                           || format == SurfaceFormat::NormalizedByte2))
            || (packed16Renderer && (format == SurfaceFormat::Bgr565
                                   || format == SurfaceFormat::Bgra5551
                                   || format == SurfaceFormat::Bgra4444))
            // REMED-GFX-244: block-compressed content is accepted on every EasyGL profile, since
            // the decode fallback needs no extension -- unlike the packed formats one line up,
            // whose sized storage is ES 3.
            || (CNA_RENDERER_IS(OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, Software)
                && (format == SurfaceFormat::Dxt1 || format == SurfaceFormat::Dxt3
                    || format == SurfaceFormat::Dxt5))
            || (igl && (format == SurfaceFormat::Rg32 || format == SurfaceFormat::Single))
            || (skia && (false
            || format == SurfaceFormat::Bgr565
            || format == SurfaceFormat::Bgra5551
            || format == SurfaceFormat::Bgra4444
            || format == SurfaceFormat::Rgba1010102
            || format == SurfaceFormat::Rg32
            || format == SurfaceFormat::Rgba64
            || format == SurfaceFormat::Alpha8
            || format == SurfaceFormat::ColorBgraEXT
            || format == SurfaceFormat::ColorSrgbEXT
            || format == SurfaceFormat::ByteEXT
            || format == SurfaceFormat::UShortEXT
            || format == SurfaceFormat::Single
            || format == SurfaceFormat::Vector2
            || format == SurfaceFormat::Vector4
            || format == SurfaceFormat::HalfSingle
            || format == SurfaceFormat::HalfVector2
            || format == SurfaceFormat::HalfVector4
            || format == SurfaceFormat::NormalizedByte2
            || format == SurfaceFormat::NormalizedByte4
            || format == SurfaceFormat::HdrBlendable
            // The renderer-selection audit found that the block-compressed formats belong here.
            // This list used to omit them while SkiaRenderer accepted them, so the two halves of
            // the contract contradicted each other and this loop failed on SKIA for as long as both
            // had their current contents.
            //
            // The implementation is the half that is right, and that was checked rather than
            // assumed: SkiaTextureRenderer.cpp carries IsCompressedTextureFormat, the correct block
            // sizes (8 bytes for Dxt1, 16 for the rest) and real decoders -- DxtUtil::DecompressDxt1
            // /Dxt3/Dxt5 and Bc7Util::DecompressBc7 -- decoding to RGBA for the CPU raster surface,
            // and it throws NotSupportedException for a format it has no decoder for. That is
            // genuine support, not silent acceptance, so a test demanding a throw was asserting the
            // opposite of what the renderer does.
            || format == SurfaceFormat::Dxt1
            || format == SurfaceFormat::Dxt3
            || format == SurfaceFormat::Dxt5
            || format == SurfaceFormat::Bc7EXT
            || format == SurfaceFormat::Bc7SrgbEXT
            )))
            ;
        if (supported)
        {
            EXPECT_NO_THROW(Texture2D(gd, 4, 4, false, format))
                << "supported SurfaceFormat ordinal " << static_cast<int>(format);
        }
        else if (!profileAllows)
        {
            // REMED-GFX-242: which of the two refused is the thing worth asserting. A format the
            // profile excludes must carry XNA's own exception type, so a caller can tell "not on
            // this profile" from "not on this renderer" and act on it -- the first is fixed by
            // asking for HiDef, the second is not.
            EXPECT_THROW(Texture2D(gd, 4, 4, false, format), System::NotSupportedException)
                << "SurfaceFormat ordinal " << static_cast<int>(format)
                << " is excluded by GraphicsProfile.Reach and must say so";
        }
        else
        {
            EXPECT_THROW(Texture2D(gd, 4, 4, false, format), std::runtime_error)
                << "SurfaceFormat ordinal " << static_cast<int>(format)
                << " must throw std::runtime_error, not silently succeed with the wrong GPU format";
        }
    }
}

// -----------------------------------------------------------------------
// REMED-CONTENT-001 -- dimension guard, defense in depth for any direct caller (not just the
// XNB content-reader path, which has its own equivalent ContentLoadException check)
// -----------------------------------------------------------------------

class DimensionGuardTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
};

TEST_F(DimensionGuardTest, WidthExceedingEffectiveTextureDimensionThrowsNotSupportedException)
{
    const int profileMax = gd.GetRenderer().GetMaxTextureSizeForProfileEXT(
        static_cast<int>(gd.getGraphicsProfileProperty()));
    const int overSize = std::min(gd.GetMaxTextureDimension(), profileMax) + 1;
    EXPECT_THROW(Texture2D(gd, overSize, 4), System::NotSupportedException);
}

TEST_F(DimensionGuardTest, HeightExceedingEffectiveTextureDimensionThrowsNotSupportedException)
{
    const int profileMax = gd.GetRenderer().GetMaxTextureSizeForProfileEXT(
        static_cast<int>(gd.getGraphicsProfileProperty()));
    const int overSize = std::min(gd.GetMaxTextureDimension(), profileMax) + 1;
    EXPECT_THROW(Texture2D(gd, 4, overSize), System::NotSupportedException);
}

TEST_F(DimensionGuardTest, WidthExceedingEffectiveTextureDimensionThrowsOnFormatConstructorToo)
{
    const int profileMax = gd.GetRenderer().GetMaxTextureSizeForProfileEXT(
        static_cast<int>(gd.getGraphicsProfileProperty()));
    const int overSize = std::min(gd.GetMaxTextureDimension(), profileMax) + 1;
    EXPECT_THROW(Texture2D(gd, overSize, 4, false, SurfaceFormat::Color), System::NotSupportedException);
}

TEST_F(DimensionGuardTest, DimensionAtTheLimitDoesNotThrow)
{
    const int profileMax = gd.GetRenderer().GetMaxTextureSizeForProfileEXT(
        static_cast<int>(gd.getGraphicsProfileProperty()));
    const int maxDim = std::min(gd.GetMaxTextureDimension(), profileMax);
    // A 1-pixel-tall texture at exactly the limit avoids allocating maxDim*maxDim*4 bytes of CPU
    // shadow storage for this test while still exercising the exact boundary value.
    EXPECT_NO_THROW(Texture2D(gd, maxDim, 1));
}

// -----------------------------------------------------------------------
// getBoundsProperty
// -----------------------------------------------------------------------

TEST(Texture2DTest, DefaultBoundsXIsZero)
{
    Texture2D tex;
    EXPECT_EQ(tex.getBoundsProperty().X, 0);
}

TEST(Texture2DTest, DefaultBoundsYIsZero)
{
    Texture2D tex;
    EXPECT_EQ(tex.getBoundsProperty().Y, 0);
}

TEST(Texture2DTest, DefaultBoundsWidthIsZero)
{
    Texture2D tex;
    EXPECT_EQ(tex.getBoundsProperty().Width, 0);
}

TEST(Texture2DTest, DefaultBoundsHeightIsZero)
{
    Texture2D tex;
    EXPECT_EQ(tex.getBoundsProperty().Height, 0);
}

// -----------------------------------------------------------------------
// Copy / move semantics
// -----------------------------------------------------------------------

TEST(Texture2DTest, CopyConstructorPreservesWidth)
{
    Texture2D src;
    Texture2D dst(src);
    EXPECT_EQ(dst.getWidthProperty(), src.getWidthProperty());
}

TEST(Texture2DTest, CopyConstructorPreservesHeight)
{
    Texture2D src;
    Texture2D dst(src);
    EXPECT_EQ(dst.getHeightProperty(), src.getHeightProperty());
}

TEST(Texture2DTest, MoveConstructorPreservesWidth)
{
    Texture2D src;
    Texture2D dst(std::move(src));
    EXPECT_EQ(dst.getWidthProperty(), 0);
}

TEST(Texture2DTest, CopyAssignmentPreservesFormat)
{
    Texture2D src;
    Texture2D dst;
    dst = src;
    EXPECT_EQ(dst.getFormatProperty(), SurfaceFormat::Color);
}

// -----------------------------------------------------------------------
// GetData(Color*, int startIndex, int elementCount) — error guards
// -----------------------------------------------------------------------

TEST(Texture2DTest, GetDataNullPtrThrowsNamedArgumentNullException)
{
    Texture2D tex;
    ExpectExactNamedException<System::ArgumentNullException>(
        [&] { tex.GetData(nullptr, 0, 1); }, "data");
}

TEST(Texture2DTest, GetDataZeroElementCountThrowsNamedArgumentOutOfRangeException)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { tex.GetData(buf, 0, 0); }, "elementCount");
}

TEST(Texture2DTest, GetDataNoCpuPixelsThrowsRuntimeError)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    EXPECT_THROW(tex.GetData(buf, 0, 1), std::runtime_error);
}

// Task 265: negative startIndex is rejected before it can compute a negative
// array index (px[(startIndex+i)*4]) and read out of bounds before the start
// of the internal cpuPixels_ buffer — mirrors the equivalent SetData guard.
TEST(Texture2DTest, GetDataNegativeStartIndexThrowsOutOfRange)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { tex.GetData(buf, -1, 1); }, "dataIndex");
}

// 2-param overload delegates to 3-param; same guards apply
TEST(Texture2DTest, GetData2ParamNullPtrThrowsNamedArgumentNullException)
{
    Texture2D tex;
    ExpectExactNamedException<System::ArgumentNullException>(
        [&] { tex.GetData(nullptr, 1); }, "data");
}

TEST(Texture2DTest, GetData2ParamNoCpuPixelsThrowsRuntimeError)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    EXPECT_THROW(tex.GetData(buf, 1), std::runtime_error);
}

// -----------------------------------------------------------------------
// GetData(int level, const Rectangle*, Color*, int, int) — error guards
// -----------------------------------------------------------------------

TEST(Texture2DTest, GetDataLevelNullDataThrowsNamedArgumentNullException)
{
    Texture2D tex;
    ExpectExactNamedException<System::ArgumentNullException>(
        [&] { tex.GetData(0, nullptr, nullptr, 0, 1); }, "data");
}

TEST(Texture2DTest, GetDataLevelZeroElementCountThrowsNamedArgumentOutOfRangeException)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { tex.GetData(0, nullptr, buf, 0, 0); }, "elementCount");
}

TEST(Texture2DTest, GetDataNegativeLevelThrowsExactInvalidOperationException)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    ExpectExactException<System::InvalidOperationException>(
        [&] { tex.GetData(-1, nullptr, buf, 0, 1); });
}

// Task 265: negative startIndex is rejected before it can compute a negative
// destination index (data[startIndex+row*w+col]) and write out of bounds
// before the start of the caller-supplied data array — mirrors the equivalent
// SetData(level,rect,...) guard (SetDataLevelNegativeStartIndexThrowsOutOfRange).
TEST(Texture2DTest, GetDataLevelNegativeStartIndexThrowsOutOfRange)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { tex.GetData(0, nullptr, buf, -1, 1); }, "dataIndex");
}

TEST(Texture2DTest, GetDataLevelNoCpuPixelsThrowsRuntimeError)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    // getMipBufferConst(0) returns nullptr when cpuPixels_ is empty
    EXPECT_THROW(tex.GetData(0, nullptr, buf, 0, 1), std::runtime_error);
}

// -----------------------------------------------------------------------
// SetData(const Color*, int) — the convenience overload retains the same
// public argument contract as the full level/rectangle overload.
// -----------------------------------------------------------------------

TEST(Texture2DTest, SetDataSimpleWithNullDataThrowsNamedArgumentNullException)
{
    GraphicsDevice device;
    Texture2D texture(device, 1, 1);
    ExpectExactNamedException<System::ArgumentNullException>(
        [&] { texture.SetData(nullptr, 1); }, "data");
}

TEST(Texture2DTest, SetDataSimpleWithZeroCountThrowsNamedArgumentOutOfRangeException)
{
    GraphicsDevice device;
    Texture2D texture(device, 1, 1);
    Color value(0, 0, 0, 0);
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { texture.SetData(&value, 0); }, "elementCount");
}

TEST(Texture2DTest, TransfersAfterDisposeThrowObjectDisposedException)
{
    Texture2D tex;
    tex.Dispose();
    Color color(1, 2, 3, 4);
    std::uint8_t rgba[4] = {1, 2, 3, 4};

    EXPECT_THROW(tex.SetData(&color, 1), System::ObjectDisposedException);
    EXPECT_THROW(tex.SetData(0, nullptr, &color, 0, 1),
                 System::ObjectDisposedException);
    EXPECT_THROW(tex.SetDataRGBA(rgba, 1), System::ObjectDisposedException);
    EXPECT_THROW(tex.GetData(&color, 1), System::ObjectDisposedException);
    EXPECT_THROW(tex.GetData(0, nullptr, &color, 0, 1),
                 System::ObjectDisposedException);
}

// -----------------------------------------------------------------------
// SetData(int level, const Rectangle*, const Color*, int, int) — error guards
//
// These validations fire before touching the CPU pixel buffer, so they are
// safe to test even on a default-constructed (zero-sized) Texture2D.
// -----------------------------------------------------------------------

TEST(Texture2DTest, SetDataLevelNullDataThrowsNamedArgumentNullException)
{
    Texture2D tex;
    ExpectExactNamedException<System::ArgumentNullException>(
        [&] { tex.SetData(0, nullptr, nullptr, 0, 1); }, "data");
}

TEST(Texture2DTest, SetDataLevelZeroElementCountThrowsNamedArgumentOutOfRangeException)
{
    Texture2D tex;
    Color buf[4] = { Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0) };
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { tex.SetData(0, nullptr, buf, 0, 0); }, "elementCount");
}

TEST(Texture2DTest, SetDataLevelNegativeStartIndexThrowsOutOfRange)
{
    Texture2D tex;
    Color buf[4] = { Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0) };
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { tex.SetData(0, nullptr, buf, -1, 1); }, "dataIndex");
}

TEST(Texture2DTest, SetDataNegativeLevelThrowsExactInvalidOperationException)
{
    Texture2D tex;
    Color buf[4] = { Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0) };
    ExpectExactException<System::InvalidOperationException>(
        [&] { tex.SetData(-1, nullptr, buf, 0, 1); });
}

TEST(Texture2DTest, SetDataLevelExtraElementsThrowsArgumentException)
{
    // Default texture: mipDim(0,0)=1, effective region is 1×1 = 1 pixel.
    // Microsoft XNA's private ValidateTotalSize requires exact byte equality.
    Texture2D tex;
    Color buf[2] = { Color(0,0,0,0), Color(0,0,0,0) };
    EXPECT_THROW(tex.SetData(0, nullptr, buf, 0, 2), System::ArgumentException);
}

TEST(Texture2DTest, SetDataLevelInvalidRectangleThrowsNamedArgumentException)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    const Rectangle wide(0, 0, 2, 1);
    ExpectExactNamedException<System::ArgumentException>(
        [&] { tex.SetData(0, &wide, buf, 0, 1); }, "rect");
}

// -----------------------------------------------------------------------
// SetData(int level, const Rectangle*, ...) — rect-bounds guard (Task 266)
//
// Mirrors the equivalent GetData bounds check (rectangle out of texture bounds).
// Fixes a heap buffer overflow write: prior to this guard, a caller-supplied
// rect that exceeded the mip level's dimensions would write past the end of
// the CPU-side mip buffer (found in the Task 261 Texture2D audit).
// -----------------------------------------------------------------------

TEST(Texture2DTest, SetDataLevelRectXOutOfBoundsThrowsNamedArgumentException)
{
    // Default texture: levelW=levelH=1 (mipDim clamp). x+w=1+1=2 > levelW=1.
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    const Rectangle rect(1, 0, 1, 1);
    ExpectExactNamedException<System::ArgumentException>(
        [&] { tex.SetData(0, &rect, buf, 0, 1); }, "rect");
}

TEST(Texture2DTest, SetDataLevelRectYOutOfBoundsThrowsNamedArgumentException)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    const Rectangle rect(0, 1, 1, 1);
    ExpectExactNamedException<System::ArgumentException>(
        [&] { tex.SetData(0, &rect, buf, 0, 1); }, "rect");
}

TEST(Texture2DTest, SetDataLevelRectNegativeXThrowsNamedArgumentException)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    const Rectangle rect(-1, 0, 1, 1);
    ExpectExactNamedException<System::ArgumentException>(
        [&] { tex.SetData(0, &rect, buf, 0, 1); }, "rect");
}

TEST(Texture2DTest, SetDataLevelRectNegativeYThrowsNamedArgumentException)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    const Rectangle rect(0, -1, 1, 1);
    ExpectExactNamedException<System::ArgumentException>(
        [&] { tex.SetData(0, &rect, buf, 0, 1); }, "rect");
}

TEST(Texture2DTest, SetDataLevelRectZeroExtentThrowsNamedArgumentException)
{
    GraphicsDevice device;
    Texture2D texture(device, 2, 2);
    Color value(1, 2, 3, 4);
    const Rectangle zeroWidth(0, 0, 0, 1);
    const Rectangle zeroHeight(0, 0, 1, 0);

    ExpectExactNamedException<System::ArgumentException>(
        [&] { texture.SetData(0, &zeroWidth, &value, 0, 1); }, "rect");
    ExpectExactNamedException<System::ArgumentException>(
        [&] { texture.SetData(0, &zeroHeight, &value, 0, 1); }, "rect");
}

TEST(Texture2DTest, SetDataLevelRectNegativeExtentThrowsNamedArgumentException)
{
    GraphicsDevice device;
    Texture2D texture(device, 2, 2);
    Color value(1, 2, 3, 4);
    const Rectangle negativeExtents(1, 1, -1, -1);

    ExpectExactNamedException<System::ArgumentException>(
        [&] { texture.SetData(0, &negativeExtents, &value, 0, 1); }, "rect");
}

TEST(Texture2DTest, GetDataLevelRectNonPositiveExtentThrowsNamedArgumentException)
{
    GraphicsDevice device;
    Texture2D texture(device, 2, 2);
    std::array<Color, 4> source{Color::Red, Color::Green, Color::Blue, Color::White};
    texture.SetData(source.data(), static_cast<int>(source.size()));
    Color destination(9, 8, 7, 6);
    const Rectangle zeroWidth(0, 0, 0, 1);
    const Rectangle negativeExtents(1, 1, -1, -1);

    ExpectExactNamedException<System::ArgumentException>(
        [&] { texture.GetData(0, &zeroWidth, &destination, 0, 1); }, "rect");
    ExpectExactNamedException<System::ArgumentException>(
        [&] { texture.GetData(0, &negativeExtents, &destination, 0, 1); }, "rect");
    EXPECT_EQ(destination, Color(9, 8, 7, 6));
}

TEST(Texture2DTest, EveryElementRouteUsesTheClassicNamedCopyArgumentExceptions)
{
    GraphicsDevice device;
    Texture2D colorTexture(device, 1, 1, false, SurfaceFormat::Color);
    Texture2D packedTexture(device, 1, 1, false, SurfaceFormat::Bgr565);
    Microsoft::Xna::Framework::Graphics::PackedVector::Bgr565 packed;

    struct RawWord
    {
        std::uint32_t value;
    };
    static_assert(std::is_trivially_copyable_v<RawWord>);
    RawWord raw{};
    std::uint8_t byte = 0;

    ExpectExactNamedException<System::ArgumentNullException>(
        [&] { packedTexture.SetData(static_cast<const decltype(packed)*>(nullptr), 1); }, "data");
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { packedTexture.SetData(0, nullptr, &packed, -1, 1); }, "dataIndex");
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { packedTexture.GetData(&packed, 0, 0); }, "elementCount");

    ExpectExactNamedException<System::ArgumentNullException>(
        [&] { colorTexture.SetData(static_cast<const RawWord*>(nullptr), 1); }, "data");
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { colorTexture.SetData(0, nullptr, &raw, -1, 1); }, "dataIndex");
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { colorTexture.GetData(&raw, 0, 0); }, "elementCount");

    ExpectExactNamedException<System::ArgumentNullException>(
        [&] { colorTexture.SetData(static_cast<const std::uint8_t*>(nullptr), 4); }, "data");
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { colorTexture.SetData(0, nullptr, &byte, -1, 4); }, "dataIndex");
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { colorTexture.GetData(&byte, 0, 0); }, "elementCount");
}

TEST(Texture2DTest, NullDataPrecedesOtherInvalidCopyArgumentsAndRectangle)
{
    Texture2D texture;
    const Rectangle invalid(-1, -1, 0, 0);
    ExpectExactNamedException<System::ArgumentNullException>(
        [&] { texture.SetData(0, &invalid, static_cast<const std::uint16_t*>(nullptr), -1, 0); },
        "data");
    ExpectExactNamedException<System::ArgumentNullException>(
        [&] { texture.GetData(0, &invalid, static_cast<std::uint16_t*>(nullptr), -1, 0); },
        "data");
}

TEST(Texture2DTest, SetDataLevelRectWithinBoundsDoesNotThrow)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    const Rectangle rect(0, 0, 1, 1);
    EXPECT_NO_THROW(tex.SetData(0, &rect, buf, 0, 1));
}

// -----------------------------------------------------------------------
// SetData(const Color*, int elementCount) — undersized-buffer guard (Task 266)
//
// Fixes a heap buffer overflow read: prior to this guard, calling SetData
// with fewer elements than width*height built an ImageData that claimed the
// full texture dimensions over an undersized pixel buffer, which the EasyGL
// renderer's set_image_2d then over-read (found in the Task 261 audit).
// Requires a real GraphicsDevice + renderer, since the guard only runs when
// graphicsDevice_ is non-null.
// -----------------------------------------------------------------------

class SetDataSimpleGuardTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
};

TEST_F(SetDataSimpleGuardTest, InsufficientElementCountThrowsArgumentException)
{
    Texture2D tex(gd, 4, 4);
    Color buf[4] = { Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0) };
    EXPECT_THROW(tex.SetData(buf, 4), System::ArgumentException);
}

TEST_F(SetDataSimpleGuardTest, ExactElementCountDoesNotThrow)
{
    Texture2D tex(gd, 2, 2);
    Color buf[4] = { Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0) };
    EXPECT_NO_THROW(tex.SetData(buf, 4));
}

// -----------------------------------------------------------------------
// Context-recovery interaction with the CPU pixel shadow (Task 270)
//
// GraphicsDevice::SetContextRecoveryEnabled(false) is a CNAEXT optimization:
// Texture2D::MaybeFreeCpuPixels() frees the CPU-side pixel shadow
// (cpuPixels_) after every full upload to save ~1x texture RAM. CNA has no
// GPU pixel-readback path, so GetData() depends entirely on that shadow —
// once freed, GetData() throws instead of falling back to a GPU read
// (FNA's real GetData always reads back from the GPU). See AUDIT.md,
// "Texture2D CPU shadow storage" for the full write-up.
// -----------------------------------------------------------------------

class ContextRecoveryTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
};

TEST_F(ContextRecoveryTest, GetDataWorksAfterFullUploadWithRecoveryEnabledByDefault)
{
    Texture2D tex(gd, 2, 2);
    Color in[4] = { Color(1,2,3,4), Color(5,6,7,8), Color(9,10,11,12), Color(13,14,15,16) };
    tex.SetData(in, 4);

    Color out[4] = { Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0) };
    EXPECT_NO_THROW(tex.GetData(out, 4));
    EXPECT_EQ(out[0], in[0]);
    EXPECT_EQ(out[3], in[3]);
}

TEST_F(ContextRecoveryTest, GetDataThrowsAfterFullUploadWithRecoveryDisabled)
{
    gd.SetContextRecoveryEnabled(false);
    Texture2D tex(gd, 2, 2);
    Color in[4] = { Color(1,2,3,4), Color(5,6,7,8), Color(9,10,11,12), Color(13,14,15,16) };
    tex.SetData(in, 4);

    Color out[4] = { Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0) };
    EXPECT_THROW(tex.GetData(out, 4), std::runtime_error);
}

TEST_F(ContextRecoveryTest, PartialUpdateAfterShadowFreedThrowsInsteadOfCorruptingTexture)
{
    // Regression test: before the Task 270 fix, this sequence silently zeroed
    // out the 3 untouched pixels on the GPU, because getMipBuffer(0)
    // resurrected a fresh zero-filled shadow and SetData re-uploaded the
    // whole level over the real (5,5,5,5) GPU content. Now it fails loudly.
    gd.SetContextRecoveryEnabled(false);
    Texture2D tex(gd, 2, 2);
    Color in[4] = { Color(5,5,5,5), Color(5,5,5,5), Color(5,5,5,5), Color(5,5,5,5) };
    tex.SetData(in, 4); // shadow freed again immediately after this upload

    const Rectangle onePixel(0, 0, 1, 1);
    Color patch(9, 9, 9, 9);
    EXPECT_THROW(tex.SetData(0, &onePixel, &patch, 0, 1), std::runtime_error);
}

TEST_F(ContextRecoveryTest, PartialUpdateCoveringFullLevelDoesNotThrowEvenWithRecoveryDisabled)
{
    // A partial-update rect that happens to cover the whole level is safe:
    // every pixel gets overwritten, so the resurrected zero-filled shadow
    // never leaks stale content to the GPU.
    gd.SetContextRecoveryEnabled(false);
    Texture2D tex(gd, 2, 2);
    Color in[4] = { Color(5,5,5,5), Color(5,5,5,5), Color(5,5,5,5), Color(5,5,5,5) };
    tex.SetData(in, 4);

    const Rectangle fullLevel(0, 0, 2, 2);
    Color patch[4] = { Color(9,9,9,9), Color(9,9,9,9), Color(9,9,9,9), Color(9,9,9,9) };
    EXPECT_NO_THROW(tex.SetData(0, &fullLevel, patch, 0, 4));
}

TEST_F(ContextRecoveryTest, PartialUpdateNeverThrowsWithRecoveryEnabledByDefault)
{
    Texture2D tex(gd, 2, 2);
    Color in[4] = { Color(5,5,5,5), Color(5,5,5,5), Color(5,5,5,5), Color(5,5,5,5) };
    tex.SetData(in, 4);

    const Rectangle onePixel(0, 0, 1, 1);
    Color patch(9, 9, 9, 9);
    EXPECT_NO_THROW(tex.SetData(0, &onePixel, &patch, 0, 1));
}

// -----------------------------------------------------------------------
// FromStream — format support verification (Task 262)
//
// Round-trips the supported PNG/JPEG containers and pins the classic XNA
// container boundary separately from the broader internal stb decoder.
// -----------------------------------------------------------------------

namespace
{
    // Minimal uncompressed 24bpp BMP, solid colour, no padding beyond the
    // mandatory 4-byte row alignment. width/height must keep row bytes a
    // multiple of 4 for this helper's simplicity (e.g. 2x2 uses 2-byte padding).
    std::vector<std::uint8_t> BuildSolidColorBmp(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b)
    {
        const int rowBytes = w * 3;
        const int rowPad = (4 - (rowBytes % 4)) % 4;
        const int rowStride = rowBytes + rowPad;
        const int pixelDataSize = rowStride * h;
        const int pixelDataOffset = 14 + 40;
        const int fileSize = pixelDataOffset + pixelDataSize;

        std::vector<std::uint8_t> buf(static_cast<std::size_t>(fileSize), 0);

        auto w32 = [&](int off, std::uint32_t v) {
            buf[off + 0] = static_cast<std::uint8_t>(v & 0xFF);
            buf[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
            buf[off + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
            buf[off + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
        };
        auto w16 = [&](int off, std::uint16_t v) {
            buf[off + 0] = static_cast<std::uint8_t>(v & 0xFF);
            buf[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
        };

        // BITMAPFILEHEADER (14 bytes)
        buf[0] = 'B'; buf[1] = 'M';
        w32(2, static_cast<std::uint32_t>(fileSize));
        w32(10, static_cast<std::uint32_t>(pixelDataOffset));

        // BITMAPINFOHEADER (40 bytes)
        w32(14, 40);
        w32(18, static_cast<std::uint32_t>(w));
        w32(22, static_cast<std::uint32_t>(h)); // positive height => bottom-up rows
        w16(26, 1);   // planes
        w16(28, 24);  // bitCount
        w32(30, 0);   // compression = BI_RGB

        for (int row = 0; row < h; ++row)
        {
            const int base = pixelDataOffset + row * rowStride;
            for (int col = 0; col < w; ++col)
            {
                buf[base + col * 3 + 0] = b;
                buf[base + col * 3 + 1] = g;
                buf[base + col * 3 + 2] = r;
            }
        }
        return buf;
    }

    std::vector<std::uint8_t> SolidGif()
    {
        return {
            0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x02, 0x00, 0x02, 0x00, 0xF1, 0x00,
            0x00, 0x00, 0x00, 0x00, 0xC8, 0xA7, 0x11, 0xD1, 0xAE, 0x10, 0x00, 0x00,
            0x00, 0x21, 0xF9, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x21, 0xFF, 0x0B,
            0x49, 0x6D, 0x61, 0x67, 0x65, 0x4D, 0x61, 0x67, 0x69, 0x63, 0x6B, 0x0D,
            0x67, 0x61, 0x6D, 0x6D, 0x61, 0x3D, 0x30, 0x2E, 0x34, 0x35, 0x34, 0x35,
            0x35, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00,
            0x02, 0x03, 0x04, 0x14, 0x05, 0x00, 0x3B
        };
    }

    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> DecoderOnlyImages()
    {
        std::vector<std::uint8_t> psd = {
            0x38, 0x42, 0x50, 0x53, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08,
            0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00
        };
        return {
            {"BMP", BuildSolidColorBmp(2, 2, 0, 0, 255)},
            {"TGA", {
                0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x02, 0x00, 0x02, 0x00, 0x20, 0x28, 0x4A, 0xA2, 0xB3, 0x71, 0x49, 0x9E,
                0xAD, 0x72, 0x10, 0xAE, 0xD1, 0xA6, 0x11, 0xA7, 0xC8, 0x92
            }},
            {"QOI", {
                0x71, 0x6F, 0x69, 0x66, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
                0x04, 0x00, 0xFF, 0xB3, 0xA2, 0x4A, 0x71, 0xFF, 0xAD, 0x9E, 0x49, 0x72,
                0xFF, 0xD1, 0xAE, 0x10, 0xA6, 0xFF, 0xC8, 0xA7, 0x11, 0x92, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x01
            }},
            {"PSD", std::move(psd)},
            {"HDR", {
                0x23, 0x3F, 0x52, 0x41, 0x44, 0x49, 0x41, 0x4E, 0x43, 0x45, 0x0A, 0x47,
                0x41, 0x4D, 0x4D, 0x41, 0x3D, 0x31, 0x0A, 0x50, 0x52, 0x49, 0x4D, 0x41,
                0x52, 0x49, 0x45, 0x53, 0x3D, 0x30, 0x20, 0x30, 0x20, 0x30, 0x20, 0x30,
                0x20, 0x30, 0x20, 0x30, 0x20, 0x30, 0x20, 0x30, 0x0A, 0x46, 0x4F, 0x52,
                0x4D, 0x41, 0x54, 0x3D, 0x33, 0x32, 0x2D, 0x62, 0x69, 0x74, 0x5F, 0x72,
                0x6C, 0x65, 0x5F, 0x72, 0x67, 0x62, 0x65, 0x0A, 0x0A, 0x2D, 0x59, 0x20,
                0x32, 0x20, 0x2B, 0x58, 0x20, 0x32, 0x0A, 0xE7, 0xB9, 0x22, 0x7F, 0xD5,
                0xAE, 0x22, 0x7F, 0xA3, 0x6C, 0x01, 0x80, 0x93, 0x62, 0x01, 0x80
            }},
            {"PPM", {
                0x50, 0x36, 0x0A, 0x32, 0x20, 0x32, 0x0A, 0x32, 0x35, 0x35, 0x0A, 0xB3,
                0xA2, 0x4A, 0xAD, 0x9E, 0x49, 0xD1, 0xAE, 0x10, 0xC8, 0xA7, 0x11
            }}
        };
    }
}

class Texture2DFromStreamFormatTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;

    static bool IsCloseTo(Color c, std::uint8_t r, std::uint8_t g, std::uint8_t b, int tolerance)
    {
        return std::abs(c.getRProperty() - r) <= tolerance &&
               std::abs(c.getGProperty() - g) <= tolerance &&
               std::abs(c.getBProperty() - b) <= tolerance;
    }
};

TEST_F(Texture2DFromStreamFormatTest, PngRoundTripDecodesCorrectSizeAndColor)
{
    Texture2D src(gd, 4, 4);
    std::vector<Color> red(16, Color(255, 0, 0, 255));
    src.SetData(red.data(), 16);

    MemoryStream writeStream;
    src.SaveAsPng(&writeStream, 4, 4);
    auto bytes = writeStream.GetBuffer();

    MemoryStream readStream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
    Texture2D loaded = Texture2D::FromStream(gd, readStream);

    EXPECT_EQ(loaded.getWidthProperty(), 4);
    EXPECT_EQ(loaded.getHeightProperty(), 4);
    // REMED-GFX-149: `elementCount` is the destination capacity for the WHOLE requested region --
    // the complete level 0 here -- so a one-element read of a 16-pixel texture is not a legal XNA
    // call and no longer silently returns a one-pixel partial frame. Reading the whole level also
    // makes this a real decode assertion: a decoder that got only the first pixel right used to
    // pass.
    std::vector<Color> px(16, Color(0, 0, 0, 0));
    loaded.GetData(px.data(), 0, 16);
    for (int i = 0; i < 16; ++i)
        EXPECT_TRUE(IsCloseTo(px[i], 255, 0, 0, 5)) << "pixel " << i; // PNG is lossless
}

TEST_F(Texture2DFromStreamFormatTest, JpegRoundTripDecodesCorrectSizeAndColor)
{
    Texture2D src(gd, 4, 4);
    std::vector<Color> green(16, Color(0, 255, 0, 255));
    src.SetData(green.data(), 16);

    MemoryStream writeStream;
    src.SaveAsJpeg(&writeStream, 4, 4);
    auto bytes = writeStream.GetBuffer();

    MemoryStream readStream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
    Texture2D loaded = Texture2D::FromStream(gd, readStream);

    EXPECT_EQ(loaded.getWidthProperty(), 4);
    EXPECT_EQ(loaded.getHeightProperty(), 4);
    // REMED-GFX-149: whole level, not one pixel -- see the PNG round trip above.
    std::vector<Color> px(16, Color(0, 0, 0, 0));
    loaded.GetData(px.data(), 0, 16);
    for (int i = 0; i < 16; ++i)
        EXPECT_TRUE(IsCloseTo(px[i], 0, 255, 0, 40)) << "pixel " << i; // JPEG is lossy
}

TEST_F(Texture2DFromStreamFormatTest, GifDecodesLikeMicrosoftXna)
{
    const auto bytes = SolidGif();
    MemoryStream readStream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
    Texture2D loaded = Texture2D::FromStream(gd, readStream);

    EXPECT_EQ(loaded.getWidthProperty(), 2);
    EXPECT_EQ(loaded.getHeightProperty(), 2);
}

TEST_F(Texture2DFromStreamFormatTest, DecoderOnlyContainersAreRejectedLikeMicrosoftXna)
{
    for (const auto& [name, bytes] : DecoderOnlyImages())
    {
        SCOPED_TRACE(name);
        const auto decoded = CNA::Internal::Graphics::ImageLoader::LoadFromMemory(
            bytes.data(), bytes.size());
        ASSERT_GT(decoded.width, 0);
        ASSERT_GT(decoded.height, 0);

        MemoryStream plain(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
        ExpectExactException<System::InvalidOperationException>(
            [&] { (void) Texture2D::FromStream(gd, plain); });

        MemoryStream resized(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
        ExpectExactException<System::InvalidOperationException>(
            [&] { (void) Texture2D::FromStream(gd, resized, 8, 8, false); });
    }
}

TEST_F(Texture2DFromStreamFormatTest, DdsIsRejectedLikeMicrosoftXna)
{
    const std::vector<std::uint8_t> dds = SolidRedDxt1Dds();

    MemoryStream plain(dds.data(), static_cast<System::IO::intcs>(dds.size()));
    ExpectExactException<System::InvalidOperationException>(
        [&] { (void) Texture2D::FromStream(gd, plain); });

    MemoryStream resized(dds.data(), static_cast<System::IO::intcs>(dds.size()));
    ExpectExactException<System::InvalidOperationException>(
        [&] { (void) Texture2D::FromStream(gd, resized, 8, 8, false); });
}

TEST_F(Texture2DFromStreamFormatTest, DdsRemainsAvailableOnlyThroughNamedExtension)
{
    const std::vector<std::uint8_t> dds = SolidRedDxt1Dds();

    MemoryStream nativeStream(dds.data(), static_cast<System::IO::intcs>(dds.size()));
    Texture2D native = Texture2D::DDSFromStreamEXT(gd, nativeStream);
    EXPECT_EQ(native.getWidthProperty(), 4);
    EXPECT_EQ(native.getHeightProperty(), 4);
    EXPECT_EQ(native.getFormatProperty(), SurfaceFormat::Dxt1);
    std::array<std::uint8_t, 8> block{};
    native.GetData(block.data(), static_cast<int>(block.size()));
    EXPECT_TRUE(std::equal(block.begin(), block.end(), dds.begin() + 128));

    MemoryStream resizedStream(dds.data(), static_cast<System::IO::intcs>(dds.size()));
    Texture2D resized = Texture2D::DDSFromStreamEXT(gd, resizedStream, 8, 8, false);
    EXPECT_EQ(resized.getWidthProperty(), 8);
    EXPECT_EQ(resized.getHeightProperty(), 8);
    EXPECT_EQ(resized.getFormatProperty(), SurfaceFormat::Color);
    std::array<Color, 64> pixels{};
    resized.GetData(pixels.data(), static_cast<int>(pixels.size()));
    EXPECT_TRUE(std::all_of(pixels.begin(), pixels.end(), [](const Color& pixel)
    {
        return pixel == Color::Red;
    }));
}

TEST_F(Texture2DFromStreamFormatTest, DecodesFromTheCurrentStreamPosition)
{
    constexpr System::IO::intcs prefixSize = 7;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(prefixSize), 0xA5);
    Texture2D source(gd, 2, 2);
    std::array<Color, 4> red{Color::Red, Color::Red, Color::Red, Color::Red};
    source.SetData(red.data(), static_cast<int>(red.size()));
    MemoryStream encoded;
    source.SaveAsPng(&encoded, 2, 2);
    const auto png = encoded.GetBuffer();
    bytes.insert(bytes.end(), png.begin(), png.end());

    MemoryStream stream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
    stream.setPositionProperty(prefixSize);
    Texture2D loaded = Texture2D::FromStream(gd, stream);

    std::vector<Color> pixels(4);
    loaded.GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
    EXPECT_TRUE(std::all_of(pixels.begin(), pixels.end(), [](const Color& pixel)
    {
        return pixel == Color(255, 0, 0, 255);
    }));
}

// -----------------------------------------------------------------------
// FromStream(device, stream, width, height, zoom) — resize/crop overload (Task 262)
//
// Source is an 8x4 (landscape) solid-colour PNG so the fit-vs-cover branch in
// the width/height computation is exercised (matches FNA3D_Image_Load's
// forceW/forceH/zoom logic — see Texture2D.cpp).
// -----------------------------------------------------------------------

class Texture2DFromStreamResizeTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
    std::vector<std::uint8_t> pngBytes;

    void SetUp() override
    {
        Texture2D src(gd, 8, 4);
        std::vector<Color> yellow(32, Color(255, 255, 0, 255));
        src.SetData(yellow.data(), 32);

        MemoryStream writeStream;
        src.SaveAsPng(&writeStream, 8, 4);
        pngBytes = writeStream.GetBuffer();
    }
};

TEST_F(Texture2DFromStreamResizeTest, FitPreservesAspectRatio)
{
    // scaleWidth = (8>4) = true; scale = 4/8 = 0.5 -> finalW=4, finalH=2.
    MemoryStream readStream(pngBytes.data(), static_cast<System::IO::intcs>(pngBytes.size()));
    Texture2D loaded = Texture2D::FromStream(gd, readStream, 4, 4, false);

    EXPECT_EQ(loaded.getWidthProperty(), 4);
    EXPECT_EQ(loaded.getHeightProperty(), 2);
}

TEST_F(Texture2DFromStreamResizeTest, ZoomFillsExactRequestedSize)
{
    MemoryStream readStream(pngBytes.data(), static_cast<System::IO::intcs>(pngBytes.size()));
    Texture2D loaded = Texture2D::FromStream(gd, readStream, 4, 4, true);

    EXPECT_EQ(loaded.getWidthProperty(), 4);
    EXPECT_EQ(loaded.getHeightProperty(), 4);
}

TEST_F(Texture2DFromStreamResizeTest, SeekableStreamAtEndRewindsBeforeDecode)
{
    MemoryStream stream(pngBytes.data(), static_cast<System::IO::intcs>(pngBytes.size()));
    stream.setPositionProperty(stream.getLengthProperty());

    Texture2D loaded = Texture2D::FromStream(gd, stream, 4, 4, false);

    EXPECT_EQ(loaded.getWidthProperty(), 4);
    EXPECT_EQ(loaded.getHeightProperty(), 2);
}

TEST_F(Texture2DFromStreamResizeTest,
       InvalidDimensionsUseNamedArgumentOutOfRangeBeforeDecoding)
{
    const auto expectDimensionFailure = [this](int width, int height, bool zoom,
                                               const char* parameterName)
    {
        MemoryStream stream(pngBytes.data(), static_cast<System::IO::intcs>(pngBytes.size()));
        ExpectExactNamedException<System::ArgumentOutOfRangeException>(
            [&] { (void) Texture2D::FromStream(gd, stream, width, height, zoom); },
            parameterName);
    };

    expectDimensionFailure(0, 4, false, "width");
    expectDimensionFailure(-1, 4, true, "width");
    expectDimensionFailure(4, 0, false, "height");
    expectDimensionFailure(4, -1, true, "height");
    expectDimensionFailure(-1, -1, false, "width");

    MemoryStream empty;
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { (void) Texture2D::FromStream(gd, empty, 0, 4, false); }, "width");
}

TEST_F(Texture2DFromStreamResizeTest, DecodeFailureUsesInvalidOperationException)
{
    MemoryStream plain;
    ExpectExactException<System::InvalidOperationException>(
        [&] { (void) Texture2D::FromStream(gd, plain); });

    MemoryStream resized;
    ExpectExactException<System::InvalidOperationException>(
        [&] { (void) Texture2D::FromStream(gd, resized, 4, 4, false); });

    const std::array<std::uint8_t, 7> garbage = {1, 2, 3, 4, 5, 6, 7};
    MemoryStream corruptPlain(garbage.data(), static_cast<System::IO::intcs>(garbage.size()));
    ExpectExactException<System::InvalidOperationException>(
        [&] { (void) Texture2D::FromStream(gd, corruptPlain); });

    MemoryStream corruptResized(garbage.data(), static_cast<System::IO::intcs>(garbage.size()));
    ExpectExactException<System::InvalidOperationException>(
        [&] { (void) Texture2D::FromStream(gd, corruptResized, 4, 4, true); });
}

TEST_F(Texture2DFromStreamResizeTest,
       NonSeekableStreamUsesNamedArgumentExceptionBeforeLengthOrRead)
{
    NonSeekableImageStream plain;
    ExpectExactNamedException<System::ArgumentException>(
        [&] { (void) Texture2D::FromStream(gd, plain); }, "stream");
    EXPECT_EQ(plain.lengthCalls, 0);
    EXPECT_EQ(plain.readCalls, 0);

    NonSeekableImageStream resized;
    ExpectExactNamedException<System::ArgumentException>(
        [&] { (void) Texture2D::FromStream(gd, resized, 4, 4, false); }, "stream");
    EXPECT_EQ(resized.lengthCalls, 0);
    EXPECT_EQ(resized.readCalls, 0);

    NonSeekableImageStream invalidDimensions;
    ExpectExactNamedException<System::ArgumentException>(
        [&] { (void) Texture2D::FromStream(gd, invalidDimensions, 0, 0, false); }, "stream");
    EXPECT_EQ(invalidDimensions.lengthCalls, 0);
    EXPECT_EQ(invalidDimensions.readCalls, 0);
}

TEST_F(Texture2DFromStreamResizeTest, ZoomCropsTheHorizontalCenterBeforeScaling)
{
    Texture2D striped(gd, 8, 4);
    std::vector<Color> pixels;
    pixels.reserve(32);
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            pixels.emplace_back(x < 2 ? Color(255, 0, 0, 255)
                                      : x < 6 ? Color(0, 255, 0, 255)
                                              : Color(0, 0, 255, 255));
        }
    }
    striped.SetData(pixels.data(), static_cast<int>(pixels.size()));

    MemoryStream encoded;
    striped.SaveAsPng(&encoded, 8, 4);
    const auto bytes = encoded.GetBuffer();
    MemoryStream source(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
    Texture2D loaded = Texture2D::FromStream(gd, source, 4, 4, true);

    std::vector<Color> result(16, Color(0, 0, 0, 0));
    loaded.GetData(result.data(), 0, static_cast<int>(result.size()));
    EXPECT_TRUE(std::all_of(result.begin(), result.end(), [](const Color& pixel) {
        return pixel == Color(0, 255, 0, 255);
    }));
}

// -----------------------------------------------------------------------
// SaveAsPng — round-trip verification (Task 263)
//
// Task 262's format tests already prove FromStream can decode a PNG produced
// by SaveAsPng, using a single solid colour. These tests go further: error
// guards, multi-pixel spatial correctness (catches row/column transposition
// bugs a solid-colour test can't), alpha preservation, non-square sizes, the
// save-time resize path, and the filename-based CNAEXT overload.
// -----------------------------------------------------------------------

class SaveAsPngTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
};

TEST_F(SaveAsPngTest, NullStreamThrowsArgumentNullException)
{
    Texture2D tex(gd, 1, 1);
    ExpectExactNamedException<System::ArgumentNullException>(
        [&] { tex.SaveAsPng(nullptr, 1, 1); }, "stream");
}

TEST_F(SaveAsPngTest, NoCpuPixelDataThrowsRuntimeError)
{
    Texture2D tex; // no SetData / renderer -> cpuPixels_ is empty
    MemoryStream stream;
    EXPECT_THROW(tex.SaveAsPng(&stream, 1, 1), std::runtime_error);
}

TEST_F(SaveAsPngTest, TargetDimensionsUseXnaArgumentExceptions)
{
    Texture2D tex(gd, 1, 1);
    MemoryStream stream;

    ExpectExactNamedException<System::ArgumentException>(
        [&] { tex.SaveAsPng(&stream, 0, 1); }, "");
    ExpectExactNamedException<System::ArgumentException>(
        [&] { tex.SaveAsPng(&stream, -1, 1); }, "targetWidth");
    ExpectExactNamedException<System::ArgumentException>(
        [&] { tex.SaveAsPng(&stream, 1, 0); }, "");
    ExpectExactNamedException<System::ArgumentException>(
        [&] { tex.SaveAsPng(&stream, 1, -1); }, "targetHeight");
}

TEST_F(SaveAsPngTest, DisposedTextureThrowsExactObjectDisposedException)
{
    Texture2D tex(gd, 1, 1);
    tex.Dispose();
    MemoryStream stream;

    try
    {
        tex.SaveAsPng(&stream, 1, 1);
        FAIL() << "expected ObjectDisposedException";
    }
    catch (const System::ObjectDisposedException& exception)
    {
        EXPECT_EQ(typeid(exception), typeid(System::ObjectDisposedException));
        EXPECT_EQ(exception.getObjectNameProperty(), "Texture2D");
    }
    catch (...)
    {
        FAIL() << "unexpected exception type; expected ObjectDisposedException";
    }
}

TEST_F(SaveAsPngTest, RoundTripPreservesDistinctPixelsAndAlpha)
{
    // 2x2, four distinct colours (including a semi-transparent one) in row-major order:
    // (0,0)=red, (1,0)=green, (0,1)=blue, (1,1)=translucent yellow.
    Texture2D src(gd, 2, 2);
    std::vector<Color> pixels = {
        Color(255, 0, 0, 255),
        Color(0, 255, 0, 255),
        Color(0, 0, 255, 255),
        Color(255, 255, 0, 128),
    };
    src.SetData(pixels.data(), 4);

    MemoryStream writeStream;
    src.SaveAsPng(&writeStream, 2, 2);
    auto bytes = writeStream.GetBuffer();

    MemoryStream readStream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
    Texture2D loaded = Texture2D::FromStream(gd, readStream);

    ASSERT_EQ(loaded.getWidthProperty(), 2);
    ASSERT_EQ(loaded.getHeightProperty(), 2);

    Color out[4] = { Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0) };
    loaded.GetData(out, 0, 4);
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_EQ(out[i].getRProperty(), pixels[i].getRProperty()) << "pixel " << i;
        EXPECT_EQ(out[i].getGProperty(), pixels[i].getGProperty()) << "pixel " << i;
        EXPECT_EQ(out[i].getBProperty(), pixels[i].getBProperty()) << "pixel " << i;
        EXPECT_EQ(out[i].getAProperty(), pixels[i].getAProperty()) << "pixel " << i;
    }
}

TEST_F(SaveAsPngTest, RoundTripNonSquareSizePreservesDimensions)
{
    Texture2D src(gd, 3, 5);
    std::vector<Color> magenta(15, Color(255, 0, 255, 255));
    src.SetData(magenta.data(), 15);

    MemoryStream writeStream;
    src.SaveAsPng(&writeStream, 3, 5);
    auto bytes = writeStream.GetBuffer();

    MemoryStream readStream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
    Texture2D loaded = Texture2D::FromStream(gd, readStream);

    EXPECT_EQ(loaded.getWidthProperty(), 3);
    EXPECT_EQ(loaded.getHeightProperty(), 5);
}

TEST_F(SaveAsPngTest, SaveWithDifferentTargetSizeResizesOutput)
{
    // Source is 2x2; ask SaveAsPng to encode it at 6x4 — the encoded PNG should be 6x4.
    Texture2D src(gd, 2, 2);
    std::vector<Color> cyan(4, Color(0, 255, 255, 255));
    src.SetData(cyan.data(), 4);

    MemoryStream writeStream;
    src.SaveAsPng(&writeStream, 6, 4);
    auto bytes = writeStream.GetBuffer();

    MemoryStream readStream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
    Texture2D loaded = Texture2D::FromStream(gd, readStream);

    EXPECT_EQ(loaded.getWidthProperty(), 6);
    EXPECT_EQ(loaded.getHeightProperty(), 4);
}

TEST_F(SaveAsPngTest, ResizeUsesXnaNearestNeighborTexelMapping)
{
    Texture2D source(gd, 4, 1);
    const std::array<Color, 4> pixels{{Color::Red, Color::Green, Color::Blue, Color::White}};
    source.SetData(pixels.data(), static_cast<int>(pixels.size()));

    MemoryStream downsampled;
    source.SaveAsPng(&downsampled, 2, 1);
    const auto downsampledBytes = downsampled.GetBuffer();
    MemoryStream downsampledStream(
        downsampledBytes.data(), static_cast<System::IO::intcs>(downsampledBytes.size()));
    Texture2D downsampledTexture = Texture2D::FromStream(gd, downsampledStream);
    std::array<Color, 2> downsampledPixels{};
    downsampledTexture.GetData(
        downsampledPixels.data(), static_cast<int>(downsampledPixels.size()));
    EXPECT_EQ(downsampledPixels[0], Color::Red);
    EXPECT_EQ(downsampledPixels[1], Color::Blue);

    Texture2D pair(gd, 2, 1);
    const std::array<Color, 2> pairPixels{{Color::Red, Color::Blue}};
    pair.SetData(pairPixels.data(), static_cast<int>(pairPixels.size()));
    MemoryStream upsampled;
    pair.SaveAsPng(&upsampled, 3, 1);
    const auto upsampledBytes = upsampled.GetBuffer();
    MemoryStream upsampledStream(
        upsampledBytes.data(), static_cast<System::IO::intcs>(upsampledBytes.size()));
    Texture2D upsampledTexture = Texture2D::FromStream(gd, upsampledStream);
    std::array<Color, 3> upsampledPixels{};
    upsampledTexture.GetData(upsampledPixels.data(), static_cast<int>(upsampledPixels.size()));
    EXPECT_EQ(upsampledPixels[0], Color::Red);
    EXPECT_EQ(upsampledPixels[1], Color::Red);
    EXPECT_EQ(upsampledPixels[2], Color::Blue);
}

TEST_F(SaveAsPngTest, ResolvedRenderTargetSavesItsLivePixels)
{
    RenderTarget2D target(gd, 2, 2);
    gd.SetRenderTarget(&target);
    gd.Clear(Color(17, 93, 201, 255));
    gd.SetRenderTarget(nullptr);

    MemoryStream encoded;
    target.SaveAsPng(&encoded, 2, 2);
    const auto bytes = encoded.GetBuffer();
    MemoryStream source(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
    Texture2D decoded = Texture2D::FromStream(gd, source);
    std::vector<Color> pixels(4);
    decoded.GetData(pixels.data(), 0, static_cast<int>(pixels.size()));

    EXPECT_TRUE(std::all_of(pixels.begin(), pixels.end(), [](const Color& pixel)
    {
        return pixel == Color(17, 93, 201, 255);
    }));
}

TEST_F(SaveAsPngTest, ConvertsEverySupportedClassicSurfaceFormatToRgba)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters());
    struct Case
    {
        SurfaceFormat format;
        std::vector<std::uint8_t> texel;
        Color expected;
    };
    const std::vector<Case> cases = {
        {SurfaceFormat::Bgr565,         {0x00, 0xF8}, Color::Red},
        {SurfaceFormat::Bgra5551,       {0x00, 0xFC}, Color::Red},
        {SurfaceFormat::Bgra4444,       {0x00, 0xFF}, Color::Red},
        {SurfaceFormat::NormalizedByte2,{0x40, 0x20}, Color(129, 64, 0, 255)},
        {SurfaceFormat::NormalizedByte4,{0x40, 0x20, 0x7F, 0x40}, Color(129, 64, 255, 129)},
        {SurfaceFormat::Rgba1010102,    {0xFF, 0x03, 0x00, 0xC0}, Color::Red},
        {SurfaceFormat::Rg32,           {0xFF, 0xFF, 0x00, 0x80}, Color(255, 128, 0, 255)},
        {SurfaceFormat::Rgba64,         {0xFF, 0xFF, 0x00, 0x80, 0, 0, 0xFF, 0xFF},
                                         Color(255, 128, 0, 255)},
        {SurfaceFormat::Alpha8,         {0x80}, Color(0, 0, 0, 128)},
        {SurfaceFormat::Single,         {0x00, 0x00, 0x00, 0x3F}, Color(128, 0, 0, 255)},
        {SurfaceFormat::Vector2,        {0x00, 0x00, 0x00, 0x3F,
                                         0x00, 0x00, 0x80, 0x3E}, Color(128, 64, 0, 255)},
        {SurfaceFormat::Vector4,        {0x00, 0x00, 0x00, 0x3F,
                                         0x00, 0x00, 0x80, 0x3E,
                                         0x00, 0x00, 0x80, 0x3F,
                                         0x00, 0x00, 0x00, 0x3F}, Color(128, 64, 255, 128)},
        {SurfaceFormat::HalfSingle,     {0x00, 0x38}, Color(128, 0, 0, 255)},
        {SurfaceFormat::HalfVector2,    {0x00, 0x38, 0x00, 0x34}, Color(128, 64, 0, 255)},
        {SurfaceFormat::HalfVector4,    {0x00, 0x38, 0x00, 0x34,
                                         0x00, 0x3C, 0x00, 0x38}, Color(128, 64, 255, 128)},
        {SurfaceFormat::HdrBlendable,   {0x00, 0x38, 0x00, 0x34,
                                         0x00, 0x3C, 0x00, 0x38}, Color(128, 64, 255, 128)},
    };

    int exercised = 0;
    for (const Case& value : cases)
    {
        if (device.GetRenderer().ClassifySurfaceFormatEXT(static_cast<int>(value.format)) !=
            CNA::Internal::Renderers::RendererFormatVerdict::Supported)
            continue;
        SCOPED_TRACE(static_cast<int>(value.format));
        Texture2D source(device, 1, 1, false, value.format);
        source.SetData(value.texel.data(), static_cast<int>(value.texel.size()));
        MemoryStream encoded;
        source.SaveAsPng(&encoded, 1, 1);
        const auto bytes = encoded.GetBuffer();
        MemoryStream stream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
        Texture2D decoded = Texture2D::FromStream(device, stream);
        Color actual;
        decoded.GetData(&actual, 1);
        EXPECT_EQ(actual, value.expected);
        ++exercised;
    }
    EXPECT_GT(exercised, 0);
}

TEST_F(SaveAsPngTest, DecompressesEveryClassicDxtFormatBeforeEncoding)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters());
    const std::array<std::pair<SurfaceFormat, std::vector<std::uint8_t>>, 3> cases{{
        {SurfaceFormat::Dxt1, {0x00, 0xF8, 0x00, 0xF8, 0, 0, 0, 0}},
        {SurfaceFormat::Dxt3, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                               0x00, 0xF8, 0x00, 0xF8, 0, 0, 0, 0}},
        {SurfaceFormat::Dxt5, {0xFF, 0xFF, 0, 0, 0, 0, 0, 0,
                               0x00, 0xF8, 0x00, 0xF8, 0, 0, 0, 0}},
    }};
    int exercised = 0;
    for (const auto& [format, blocks] : cases)
    {
        if (!device.GetRenderer().IsCompressedTransferFormatEXT(static_cast<int>(format)))
            continue;
        SCOPED_TRACE(static_cast<int>(format));
        Texture2D source(device, 4, 4, false, format);
        source.SetData(blocks.data(), static_cast<int>(blocks.size()));
        MemoryStream encoded;
        source.SaveAsPng(&encoded, 4, 4);
        const auto bytes = encoded.GetBuffer();
        MemoryStream stream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
        Texture2D decoded = Texture2D::FromStream(device, stream);
        std::array<Color, 16> actual{};
        decoded.GetData(actual.data(), static_cast<int>(actual.size()));
        EXPECT_TRUE(std::all_of(actual.begin(), actual.end(), [](const Color& pixel)
        {
            return pixel == Color::Red;
        }));
        ++exercised;
    }
    EXPECT_GT(exercised, 0);
}

TEST_F(SaveAsPngTest, FullyTransparentPixelsLoseRgbLikeXna)
{
    Texture2D source(gd, 1, 1);
    const Color transparent(200, 50, 100, 0);
    source.SetData(&transparent, 1);
    MemoryStream encoded;
    source.SaveAsPng(&encoded, 1, 1);
    const auto bytes = encoded.GetBuffer();
    MemoryStream stream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
    Texture2D decoded = Texture2D::FromStream(gd, stream);
    Color actual;
    decoded.GetData(&actual, 1);
    EXPECT_EQ(actual, Color(0, 0, 0, 0));
}

TEST_F(SaveAsPngTest, FilenameOverloadWritesReadableFile)
{
    Texture2D src(gd, 2, 2);
    std::vector<Color> orange(4, Color(255, 128, 0, 255));
    src.SetData(orange.data(), 4);

    auto tmpDir = std::filesystem::temp_directory_path() / "cna_saveaspng_test";
    std::filesystem::create_directories(tmpDir);
    const std::string path = (tmpDir / "out.png").string();

    src.SaveAsPng(path);

    std::ifstream in(path, std::ios::binary);
    ASSERT_TRUE(in.good());
    std::vector<System::IO::bytecs> bytes(
        (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ASSERT_FALSE(bytes.empty());

    MemoryStream readStream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
    Texture2D loaded = Texture2D::FromStream(gd, readStream);

    EXPECT_EQ(loaded.getWidthProperty(), 2);
    EXPECT_EQ(loaded.getHeightProperty(), 2);
    // REMED-GFX-149: whole level, not one pixel -- see the PNG round trip above.
    std::vector<Color> px(4, Color(0, 0, 0, 0));
    loaded.GetData(px.data(), 0, 4);
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_EQ(px[i].getRProperty(), 255) << "pixel " << i;
        EXPECT_EQ(px[i].getGProperty(), 128) << "pixel " << i;
        EXPECT_EQ(px[i].getBProperty(), 0) << "pixel " << i;
    }
}

// -----------------------------------------------------------------------
// SaveAsJpeg — round-trip verification (Task 264)
//
// Mirrors the SaveAsPngTest coverage above, adapted for JPEG: lossy colour
// tolerance instead of exact match, and no alpha preservation (JPEG has no
// alpha channel — the reference image decoder round-trips it back as fully opaque).
// Also verifies FNA_GRAPHICS_JPEG_SAVE_QUALITY is honoured (Task 261 audit
// found CNA previously hardcoded quality=100, ignoring FNA's env var).
// -----------------------------------------------------------------------

class SaveAsJpegTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;

    static bool IsCloseTo(Color c, std::uint8_t r, std::uint8_t g, std::uint8_t b, int tolerance)
    {
        return std::abs(c.getRProperty() - r) <= tolerance &&
               std::abs(c.getGProperty() - g) <= tolerance &&
               std::abs(c.getBProperty() - b) <= tolerance;
    }
};

TEST_F(SaveAsJpegTest, NullStreamThrowsArgumentNullException)
{
    Texture2D tex(gd, 1, 1);
    ExpectExactNamedException<System::ArgumentNullException>(
        [&] { tex.SaveAsJpeg(nullptr, 1, 1); }, "stream");
}

TEST_F(SaveAsJpegTest, NoCpuPixelDataThrowsRuntimeError)
{
    Texture2D tex;
    MemoryStream stream;
    EXPECT_THROW(tex.SaveAsJpeg(&stream, 1, 1), std::runtime_error);
}

TEST_F(SaveAsJpegTest, TargetDimensionsUseXnaArgumentExceptions)
{
    Texture2D tex(gd, 1, 1);
    MemoryStream stream;

    ExpectExactNamedException<System::ArgumentException>(
        [&] { tex.SaveAsJpeg(&stream, 0, 1); }, "");
    ExpectExactNamedException<System::ArgumentException>(
        [&] { tex.SaveAsJpeg(&stream, -1, 1); }, "targetWidth");
    ExpectExactNamedException<System::ArgumentException>(
        [&] { tex.SaveAsJpeg(&stream, 1, 0); }, "");
    ExpectExactNamedException<System::ArgumentException>(
        [&] { tex.SaveAsJpeg(&stream, 1, -1); }, "targetHeight");
}

TEST_F(SaveAsJpegTest, DisposedTextureThrowsExactObjectDisposedException)
{
    Texture2D tex(gd, 1, 1);
    tex.Dispose();
    MemoryStream stream;

    try
    {
        tex.SaveAsJpeg(&stream, 1, 1);
        FAIL() << "expected ObjectDisposedException";
    }
    catch (const System::ObjectDisposedException& exception)
    {
        EXPECT_EQ(typeid(exception), typeid(System::ObjectDisposedException));
        EXPECT_EQ(exception.getObjectNameProperty(), "Texture2D");
    }
    catch (...)
    {
        FAIL() << "unexpected exception type; expected ObjectDisposedException";
    }
}

TEST_F(SaveAsJpegTest, RoundTripPreservesDistinctPixelsWithinTolerance)
{
    // 2x2, four distinct opaque colours in row-major order.
    Texture2D src(gd, 2, 2);
    std::vector<Color> pixels = {
        Color(255, 0, 0, 255),
        Color(0, 255, 0, 255),
        Color(0, 0, 255, 255),
        Color(255, 255, 0, 255),
    };
    src.SetData(pixels.data(), 4);

    MemoryStream writeStream;
    src.SaveAsJpeg(&writeStream, 2, 2);
    auto bytes = writeStream.GetBuffer();

    MemoryStream readStream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
    Texture2D loaded = Texture2D::FromStream(gd, readStream);

    ASSERT_EQ(loaded.getWidthProperty(), 2);
    ASSERT_EQ(loaded.getHeightProperty(), 2);

    Color out[4] = { Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0) };
    loaded.GetData(out, 0, 4);
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_TRUE(IsCloseTo(out[i], pixels[i].getRProperty(), pixels[i].getGProperty(),
                              pixels[i].getBProperty(), 40)) << "pixel " << i;
    }
}

TEST_F(SaveAsJpegTest, RoundTripDropsAlphaChannel)
{
    // JPEG has no alpha channel; a semi-transparent source must decode back fully opaque.
    Texture2D src(gd, 1, 1);
    Color translucent[1] = { Color(200, 100, 50, 100) };
    src.SetData(translucent, 1);

    MemoryStream writeStream;
    src.SaveAsJpeg(&writeStream, 1, 1);
    auto bytes = writeStream.GetBuffer();

    MemoryStream readStream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
    Texture2D loaded = Texture2D::FromStream(gd, readStream);

    Color px[1] = { Color(0, 0, 0, 0) };
    loaded.GetData(px, 0, 1);
    EXPECT_EQ(px[0].getAProperty(), 255);
}

TEST_F(SaveAsJpegTest, RoundTripNonSquareSizePreservesDimensions)
{
    Texture2D src(gd, 3, 5);
    std::vector<Color> magenta(15, Color(255, 0, 255, 255));
    src.SetData(magenta.data(), 15);

    MemoryStream writeStream;
    src.SaveAsJpeg(&writeStream, 3, 5);
    auto bytes = writeStream.GetBuffer();

    MemoryStream readStream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
    Texture2D loaded = Texture2D::FromStream(gd, readStream);

    EXPECT_EQ(loaded.getWidthProperty(), 3);
    EXPECT_EQ(loaded.getHeightProperty(), 5);
}

TEST_F(SaveAsJpegTest, SaveWithDifferentTargetSizeResizesOutput)
{
    Texture2D src(gd, 2, 2);
    std::vector<Color> cyan(4, Color(0, 255, 255, 255));
    src.SetData(cyan.data(), 4);

    MemoryStream writeStream;
    src.SaveAsJpeg(&writeStream, 6, 4);
    auto bytes = writeStream.GetBuffer();

    MemoryStream readStream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
    Texture2D loaded = Texture2D::FromStream(gd, readStream);

    EXPECT_EQ(loaded.getWidthProperty(), 6);
    EXPECT_EQ(loaded.getHeightProperty(), 4);
}

TEST_F(SaveAsJpegTest, ResizeUsesXnaNearestNeighborBeforeEncoding)
{
    Texture2D source(gd, 2, 1);
    const std::array<Color, 2> pixels{{Color::Red, Color::Blue}};
    source.SetData(pixels.data(), static_cast<int>(pixels.size()));
    MemoryStream encoded;
    source.SaveAsJpeg(&encoded, 16, 8);
    const auto bytes = encoded.GetBuffer();
    MemoryStream stream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
    Texture2D decoded = Texture2D::FromStream(gd, stream);
    std::array<Color, 128> actual{};
    decoded.GetData(actual.data(), static_cast<int>(actual.size()));

    const Color left = actual[4 * 16 + 6];
    const Color right = actual[4 * 16 + 9];
    EXPECT_GT(left.getRProperty(), 220);
    EXPECT_LT(left.getBProperty(), 40);
    EXPECT_LT(right.getRProperty(), 40);
    EXPECT_GT(right.getBProperty(), 220);
}

TEST_F(SaveAsJpegTest, ResolvedRenderTargetSavesItsLivePixels)
{
    RenderTarget2D target(gd, 4, 4);
    gd.SetRenderTarget(&target);
    gd.Clear(Color(30, 180, 70, 255));
    gd.SetRenderTarget(nullptr);

    MemoryStream encoded;
    target.SaveAsJpeg(&encoded, 4, 4);
    const auto bytes = encoded.GetBuffer();
    MemoryStream source(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
    Texture2D decoded = Texture2D::FromStream(gd, source);
    std::vector<Color> pixels(16);
    decoded.GetData(pixels.data(), 0, static_cast<int>(pixels.size()));

    EXPECT_TRUE(std::all_of(pixels.begin(), pixels.end(), [&](const Color& pixel)
    {
        return IsCloseTo(pixel, 30, 180, 70, 8);
    }));
}

TEST_F(SaveAsJpegTest, ConvertsPackedSurfaceFormatBeforeEncoding)
{
    Texture2D source(gd, 4, 4, false, SurfaceFormat::Bgr565);
    const std::array<std::uint8_t, 32> red = []
    {
        std::array<std::uint8_t, 32> result{};
        for (std::size_t offset = 0; offset < result.size(); offset += 2)
        {
            result[offset] = 0x00;
            result[offset + 1] = 0xF8;
        }
        return result;
    }();
    source.SetData(red.data(), static_cast<int>(red.size()));
    MemoryStream encoded;
    source.SaveAsJpeg(&encoded, 4, 4);
    const auto bytes = encoded.GetBuffer();
    MemoryStream stream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
    Texture2D decoded = Texture2D::FromStream(gd, stream);
    std::array<Color, 16> actual{};
    decoded.GetData(actual.data(), static_cast<int>(actual.size()));
    EXPECT_TRUE(std::all_of(actual.begin(), actual.end(), [](const Color& pixel)
    {
        return IsCloseTo(pixel, 255, 0, 0, 3);
    }));
}

TEST_F(SaveAsJpegTest, FullyTransparentPixelsLoseRgbLikeXna)
{
    Texture2D source(gd, 4, 4);
    const std::array<Color, 16> transparent = []
    {
        std::array<Color, 16> result{};
        result.fill(Color(200, 50, 100, 0));
        return result;
    }();
    source.SetData(transparent.data(), static_cast<int>(transparent.size()));
    MemoryStream encoded;
    source.SaveAsJpeg(&encoded, 4, 4);
    const auto bytes = encoded.GetBuffer();
    MemoryStream stream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
    Texture2D decoded = Texture2D::FromStream(gd, stream);
    std::array<Color, 16> actual{};
    decoded.GetData(actual.data(), static_cast<int>(actual.size()));
    EXPECT_TRUE(std::all_of(actual.begin(), actual.end(), [](const Color& pixel)
    {
        return IsCloseTo(pixel, 0, 0, 0, 2);
    }));
}

TEST_F(SaveAsJpegTest, FilenameOverloadWritesReadableFile)
{
    Texture2D src(gd, 2, 2);
    std::vector<Color> orange(4, Color(255, 128, 0, 255));
    src.SetData(orange.data(), 4);

    auto tmpDir = std::filesystem::temp_directory_path() / "cna_saveasjpeg_test";
    std::filesystem::create_directories(tmpDir);
    const std::string path = (tmpDir / "out.jpg").string();

    src.SaveAsJpeg(path);

    std::ifstream in(path, std::ios::binary);
    ASSERT_TRUE(in.good());
    std::vector<System::IO::bytecs> bytes(
        (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ASSERT_FALSE(bytes.empty());

    MemoryStream readStream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
    Texture2D loaded = Texture2D::FromStream(gd, readStream);

    EXPECT_EQ(loaded.getWidthProperty(), 2);
    EXPECT_EQ(loaded.getHeightProperty(), 2);
    // REMED-GFX-149: whole level, not one pixel -- see the PNG round trip above.
    std::vector<Color> px(4, Color(0, 0, 0, 0));
    loaded.GetData(px.data(), 0, 4);
    for (int i = 0; i < 4; ++i)
        EXPECT_TRUE(IsCloseTo(px[i], 255, 128, 0, 40)) << "pixel " << i;
}

TEST_F(SaveAsJpegTest, QualityEnvVarIsHonoredWithoutThrowing)
{
    // FNA_GRAPHICS_JPEG_SAVE_QUALITY: verify the env-var path (Task 264 fix for the Task 261
    // audit finding that quality was hardcoded to 100) parses and applies without throwing.
    System::Environment::SetEnvironmentVariable("FNA_GRAPHICS_JPEG_SAVE_QUALITY", "50");

    Texture2D src(gd, 2, 2);
    std::vector<Color> red(4, Color(255, 0, 0, 255));
    src.SetData(red.data(), 4);

    MemoryStream writeStream;
    EXPECT_NO_THROW(src.SaveAsJpeg(&writeStream, 2, 2));
    auto bytes = writeStream.GetBuffer();

    System::Environment::SetEnvironmentVariable("FNA_GRAPHICS_JPEG_SAVE_QUALITY", {});
    // sharp-runtime #2313 (downstream ticket #2366): the old comment here said "empty value
    // deletes it", which stopped being true -- "" now STORES an empty value and only a null value
    // removes. `{}` is used rather than `std::nullopt` because it means "remove" under both the
    // sharp-runtime on develop (parameter `const std::string&`) and the one on next
    // (`const std::optional<std::string>&`); `std::nullopt` does not compile against develop.

    ASSERT_FALSE(bytes.empty());
    MemoryStream readStream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
    Texture2D loaded = Texture2D::FromStream(gd, readStream);
    EXPECT_EQ(loaded.getWidthProperty(), 2);
    EXPECT_EQ(loaded.getHeightProperty(), 2);
}
