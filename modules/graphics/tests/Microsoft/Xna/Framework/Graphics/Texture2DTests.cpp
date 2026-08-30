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
#include <vector>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "System/Environment.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/Environment.hpp"
#include "System/NotSupportedException.hpp"
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
using System::IO::MemoryStream;

namespace
{
    using CNA::Internal::Renderers::ITextureRenderer;

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
                            count + kExtraCapacity);
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
#if 1
    EXPECT_EQ(Texture2D(gd, 2, 2, true, SurfaceFormat::Color).getLevelCountProperty(), 2);
    EXPECT_EQ(Texture2D(gd, 4, 4, true, SurfaceFormat::Color).getLevelCountProperty(), 3);
    EXPECT_EQ(Texture2D(gd, 16, 16, true, SurfaceFormat::Color).getLevelCountProperty(), 5);
#endif
}

TEST_F(LevelCountTest, MipMapTrueNonSquarePowerOfTwo)
{
#if 1
    EXPECT_EQ(Texture2D(gd, 8, 4, true, SurfaceFormat::Color).getLevelCountProperty(), 4);
    EXPECT_EQ(Texture2D(gd, 1, 8, true, SurfaceFormat::Color).getLevelCountProperty(), 4);
#endif
}

TEST_F(LevelCountTest, MipMapTrueNonPowerOfTwo)
{
#if 1
    EXPECT_EQ(Texture2D(gd, 3, 5, true, SurfaceFormat::Color).getLevelCountProperty(), 3);
    EXPECT_EQ(Texture2D(gd, 7, 11, true, SurfaceFormat::Color).getLevelCountProperty(), 4);
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
#if 1
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
        EXPECT_THROW(texture.SetData(level, nullptr, source.data(), 2, 1), std::out_of_range);
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
        EXPECT_THROW(texture.GetData(level, rect, destination.data(), 3, 1), std::out_of_range);
        for (const Color& value : destination) ExpectExactColor(value, sentinel);
        EXPECT_EQ(renderer->getDataCalls, 0);
    }
}

TEST(Texture2DMipLevelValidationTest, ExistingDataAndStartIndexValidationStillPrecedeLevelValidation)
{
    constexpr int kLevelCount = 1;
    auto renderer = std::make_shared<RecordingMipTextureRenderer>(13, 7);
    Texture2D texture = Texture2D::CreateWithRendererForTests(13, 7, renderer);
    Color value(1, 2, 3, 4);

    EXPECT_THROW(texture.GetData(kLevelCount, nullptr, nullptr, -1, 0), std::invalid_argument);
    EXPECT_THROW(texture.SetData(kLevelCount, nullptr, nullptr, -1, 0), std::invalid_argument);

    try
    {
        texture.GetData(kLevelCount, nullptr, &value, -1, 1);
        FAIL() << "GetData accepted a negative startIndex";
    }
    catch (const std::out_of_range& e)
    {
        EXPECT_NE(std::string(e.what()).find("startIndex"), std::string::npos);
    }
    try
    {
        texture.SetData(kLevelCount, nullptr, &value, -1, 1);
        FAIL() << "SetData accepted a negative startIndex";
    }
    catch (const std::out_of_range& e)
    {
        EXPECT_NE(std::string(e.what()).find("startIndex"), std::string::npos);
    }
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
    // them through one branch -- NormalizedByte2 as RG8_SNORM beside NormalizedByte4's
    // RGBA8_SNORM. Both need the ES 3 class of context that has SNORM at all, so this list
    // is deliberately the same one NormalizedByte4Throws uses.
    if (CNA_RENDERER_IS(OpenGLES3, OpenGL33, WebGL2))
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
    if (CNA_RENDERER_IS(OpenGLES3, OpenGL33, WebGL2))
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
    if (CNA_RENDERER_IS(OpenGLES3, OpenGL33, WebGL2))
    {
        EXPECT_NO_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::Bgra5551));
    }
    else
    {
        EXPECT_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::Bgra5551), std::runtime_error);
    }
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
    if (CNA_RENDERER_IS())
        EXPECT_NO_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::Single));
    else
        EXPECT_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::Single), std::runtime_error);
}

TEST_F(HiDefFormatConstructionTest, Vector2IsTheRenderersCallOnHiDef)
{
    if (CNA_RENDERER_IS())
        EXPECT_NO_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::Vector2));
    else
        EXPECT_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::Vector2), std::runtime_error);
}

TEST_F(HiDefFormatConstructionTest, Vector4IsTheRenderersCallOnHiDef)
{
    if (CNA_RENDERER_IS())
        EXPECT_NO_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::Vector4));
    else
        EXPECT_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::Vector4), std::runtime_error);
}

TEST_F(HiDefFormatConstructionTest, HalfSingleIsTheRenderersCallOnHiDef)
{
    if (CNA_RENDERER_IS())
        EXPECT_NO_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::HalfSingle));
    else
        EXPECT_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::HalfSingle), std::runtime_error);
}

TEST_F(HiDefFormatConstructionTest, HalfVector2IsTheRenderersCallOnHiDef)
{
    if (CNA_RENDERER_IS())
        EXPECT_NO_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::HalfVector2));
    else
        EXPECT_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::HalfVector2), std::runtime_error);
}

TEST_F(HiDefFormatConstructionTest, HalfVector4IsTheRenderersCallOnHiDef)
{
    if (CNA_RENDERER_IS())
        EXPECT_NO_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::HalfVector4));
    else
        EXPECT_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::HalfVector4), std::runtime_error);
}

TEST_F(HiDefFormatConstructionTest, HdrBlendableIsTheRenderersCallOnHiDef)
{
    if (CNA_RENDERER_IS())
        EXPECT_NO_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::HdrBlendable));
    else
        EXPECT_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::HdrBlendable), std::runtime_error);
}

TEST_F(HiDefFormatConstructionTest, Rgba1010102IsTheRenderersCallOnHiDef)
{
    if (CNA_RENDERER_IS())
        EXPECT_NO_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::Rgba1010102));
    else
        EXPECT_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::Rgba1010102), std::runtime_error);
}

TEST_F(HiDefFormatConstructionTest, Rgba64IsTheRenderersCallOnHiDef)
{
    if (CNA_RENDERER_IS())
        EXPECT_NO_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::Rgba64));
    else
        EXPECT_THROW(Texture2D(gd, 2, 2, false, SurfaceFormat::Rgba64), std::runtime_error);
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
        const bool skia = CNA_RENDERER_IS();
        // plans/plan_igl.md IGL-71: IGL's promoted set is deliberately two formats wide, not a mirror of
        // everything it can store. A format is here only once the whole public path is verified end
        // to end on both its backends, and only if its texel is a multiple of four bytes -- the
        // framework's own transfer rule, which ByteEXT, UShortEXT and HalfSingle would break.
        const bool igl = CNA_RENDERER_IS();
        const bool easyGlSignedNormalized =
            CNA_RENDERER_IS(OpenGLES3, OpenGL33, WebGL2);
        // REMED-GFX-244: the packed 16-bit formats Reach permits, promoted on the same ES 3
        // generation the signed-normalized pair needs and verified by a real sampled draw
        // (EasyGL_Packed16Format) rather than by a readback, which this renderer serves from a CPU
        // copy and which therefore cannot see a wrong channel order.
        const bool easyGlPacked16 = CNA_RENDERER_IS(OpenGLES3, OpenGL33, WebGL2);
        // REMED-GFX-242: this fixture's device is Reach, and a format the profile excludes is
        // refused however capable the renderer is -- so the profile is a factor of "supported",
        // not an alternative to it.
        const bool profileAllows =
            Texture::IsFormatAllowedByProfileEXT(GraphicsProfile::Reach, format);
        const bool supported = profileAllows && (format == SurfaceFormat::Color
            || (easyGlSignedNormalized && (format == SurfaceFormat::NormalizedByte4
                                           || format == SurfaceFormat::NormalizedByte2))
            || (easyGlPacked16 && (format == SurfaceFormat::Bgr565
                                   || format == SurfaceFormat::Bgra5551
                                   || format == SurfaceFormat::Bgra4444))
            // REMED-GFX-244: block-compressed content is accepted on every EasyGL profile, since
            // the decode fallback needs no extension -- unlike the packed formats one line up,
            // whose sized storage is ES 3.
            || (CNA_RENDERER_IS(OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2)
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

TEST_F(DimensionGuardTest, WidthExceedingMaxTextureDimensionThrowsNotSupportedException)
{
    const int overSize = gd.GetMaxTextureDimension() + 1;
    EXPECT_THROW(Texture2D(gd, overSize, 4), System::NotSupportedException);
}

TEST_F(DimensionGuardTest, HeightExceedingMaxTextureDimensionThrowsNotSupportedException)
{
    const int overSize = gd.GetMaxTextureDimension() + 1;
    EXPECT_THROW(Texture2D(gd, 4, overSize), System::NotSupportedException);
}

TEST_F(DimensionGuardTest, WidthExceedingMaxTextureDimensionThrowsOnFormatConstructorToo)
{
    const int overSize = gd.GetMaxTextureDimension() + 1;
    EXPECT_THROW(Texture2D(gd, overSize, 4, false, SurfaceFormat::Color), System::NotSupportedException);
}

TEST_F(DimensionGuardTest, DimensionAtTheLimitDoesNotThrow)
{
    const int maxDim = gd.GetMaxTextureDimension();
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

TEST(Texture2DTest, GetDataNullPtrThrowsInvalidArgument)
{
    Texture2D tex;
    EXPECT_THROW(tex.GetData(nullptr, 0, 1), std::invalid_argument);
}

TEST(Texture2DTest, GetDataZeroElementCountThrowsInvalidArgument)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    EXPECT_THROW(tex.GetData(buf, 0, 0), std::invalid_argument);
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
    EXPECT_THROW(tex.GetData(buf, -1, 1), std::out_of_range);
}

// 2-param overload delegates to 3-param; same guards apply
TEST(Texture2DTest, GetData2ParamNullPtrThrowsInvalidArgument)
{
    Texture2D tex;
    EXPECT_THROW(tex.GetData(nullptr, 1), std::invalid_argument);
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

TEST(Texture2DTest, GetDataLevelNullDataThrowsInvalidArgument)
{
    Texture2D tex;
    EXPECT_THROW(tex.GetData(0, nullptr, nullptr, 0, 1), std::invalid_argument);
}

TEST(Texture2DTest, GetDataLevelZeroElementCountThrowsInvalidArgument)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    EXPECT_THROW(tex.GetData(0, nullptr, buf, 0, 0), std::invalid_argument);
}

TEST(Texture2DTest, GetDataNegativeLevelThrowsOutOfRange)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    EXPECT_THROW(tex.GetData(-1, nullptr, buf, 0, 1), std::out_of_range);
}

// Task 265: negative startIndex is rejected before it can compute a negative
// destination index (data[startIndex+row*w+col]) and write out of bounds
// before the start of the caller-supplied data array — mirrors the equivalent
// SetData(level,rect,...) guard (SetDataLevelNegativeStartIndexThrowsOutOfRange).
TEST(Texture2DTest, GetDataLevelNegativeStartIndexThrowsOutOfRange)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    EXPECT_THROW(tex.GetData(0, nullptr, buf, -1, 1), std::out_of_range);
}

TEST(Texture2DTest, GetDataLevelNoCpuPixelsThrowsRuntimeError)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    // getMipBufferConst(0) returns nullptr when cpuPixels_ is empty
    EXPECT_THROW(tex.GetData(0, nullptr, buf, 0, 1), std::runtime_error);
}

// -----------------------------------------------------------------------
// SetData(const Color*, int) — no renderer, returns early (no throw)
// -----------------------------------------------------------------------

TEST(Texture2DTest, SetDataSimpleWithNullDataDoesNotThrow)
{
    // graphicsDevice_ is null → early return, null data check skipped
    Texture2D tex;
    EXPECT_NO_THROW(tex.SetData(nullptr, 0));
}

TEST(Texture2DTest, SetDataSimpleWithZeroCountDoesNotThrow)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    EXPECT_NO_THROW(tex.SetData(buf, 0));
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

TEST(Texture2DTest, SetDataLevelNullDataThrowsInvalidArgument)
{
    Texture2D tex;
    EXPECT_THROW(tex.SetData(0, nullptr, nullptr, 0, 1), std::invalid_argument);
}

TEST(Texture2DTest, SetDataLevelZeroElementCountThrowsInvalidArgument)
{
    Texture2D tex;
    Color buf[4] = { Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0) };
    EXPECT_THROW(tex.SetData(0, nullptr, buf, 0, 0), std::invalid_argument);
}

TEST(Texture2DTest, SetDataLevelNegativeStartIndexThrowsOutOfRange)
{
    Texture2D tex;
    Color buf[4] = { Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0) };
    EXPECT_THROW(tex.SetData(0, nullptr, buf, -1, 1), std::out_of_range);
}

TEST(Texture2DTest, SetDataNegativeLevelThrowsOutOfRange)
{
    Texture2D tex;
    Color buf[4] = { Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0) };
    EXPECT_THROW(tex.SetData(-1, nullptr, buf, 0, 1), std::out_of_range);
}

TEST(Texture2DTest, SetDataLevelExtraElementsDoesNotThrow)
{
    // Default texture: mipDim(0,0)=1, effective region is 1×1 = 1 pixel.
    // Providing elementCount=2 (> region size) is allowed — XNA ignores extras.
    Texture2D tex;
    Color buf[2] = { Color(0,0,0,0), Color(0,0,0,0) };
    EXPECT_NO_THROW(tex.SetData(0, nullptr, buf, 0, 2));
}

TEST(Texture2DTest, SetDataLevelInsufficientElementsThrowsOutOfRange)
{
    // Default texture: mipDim(0,0)=1, effective region is 1×1 = 1 pixel.
    // Providing elementCount=0 is rejected by the elementCount <= 0 guard above,
    // but that already throws invalid_argument. Rectangle(0,0,2,1) also exceeds
    // levelW=1 (x+w=2>1), so the rect-bounds guard fires first here — both guards
    // throw std::out_of_range, so this still exercises the same failure mode.
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    const Rectangle wide(0, 0, 2, 1);
    EXPECT_THROW(tex.SetData(0, &wide, buf, 0, 1), std::out_of_range);
}

// -----------------------------------------------------------------------
// SetData(int level, const Rectangle*, ...) — rect-bounds guard (Task 266)
//
// Mirrors the equivalent GetData bounds check (rectangle out of texture bounds).
// Fixes a heap buffer overflow write: prior to this guard, a caller-supplied
// rect that exceeded the mip level's dimensions would write past the end of
// the CPU-side mip buffer (found in the Task 261 Texture2D audit).
// -----------------------------------------------------------------------

TEST(Texture2DTest, SetDataLevelRectXOutOfBoundsThrowsOutOfRange)
{
    // Default texture: levelW=levelH=1 (mipDim clamp). x+w=1+1=2 > levelW=1.
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    const Rectangle rect(1, 0, 1, 1);
    EXPECT_THROW(tex.SetData(0, &rect, buf, 0, 1), std::out_of_range);
}

TEST(Texture2DTest, SetDataLevelRectYOutOfBoundsThrowsOutOfRange)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    const Rectangle rect(0, 1, 1, 1);
    EXPECT_THROW(tex.SetData(0, &rect, buf, 0, 1), std::out_of_range);
}

TEST(Texture2DTest, SetDataLevelRectNegativeXThrowsOutOfRange)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    const Rectangle rect(-1, 0, 1, 1);
    EXPECT_THROW(tex.SetData(0, &rect, buf, 0, 1), std::out_of_range);
}

TEST(Texture2DTest, SetDataLevelRectNegativeYThrowsOutOfRange)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    const Rectangle rect(0, -1, 1, 1);
    EXPECT_THROW(tex.SetData(0, &rect, buf, 0, 1), std::out_of_range);
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

TEST_F(SetDataSimpleGuardTest, InsufficientElementCountThrowsOutOfRange)
{
    Texture2D tex(gd, 4, 4);
    Color buf[4] = { Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0) };
    EXPECT_THROW(tex.SetData(buf, 4), std::out_of_range);
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
// Round-trips through Texture2D::SaveAsPng/SaveAsJpeg (PNG/JPEG) and a
// hand-built minimal file (BMP) to empirically confirm which encoded
// formats Texture2D::FromStream can decode via the vendored stb image backend.
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

TEST_F(Texture2DFromStreamFormatTest, BmpDecodesCorrectSizeAndColor)
{
    auto bytes = BuildSolidColorBmp(2, 2, 0, 0, 255); // solid blue
    MemoryStream readStream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
    Texture2D loaded = Texture2D::FromStream(gd, readStream);

    EXPECT_EQ(loaded.getWidthProperty(), 2);
    EXPECT_EQ(loaded.getHeightProperty(), 2);
    // REMED-GFX-149: whole level, not one pixel -- see the PNG round trip above.
    std::vector<Color> px(4, Color(0, 0, 0, 0));
    loaded.GetData(px.data(), 0, 4);
    for (int i = 0; i < 4; ++i)
        EXPECT_TRUE(IsCloseTo(px[i], 0, 0, 255, 0)) << "pixel " << i; // BMP is uncompressed
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

TEST_F(SaveAsPngTest, NullStreamThrowsInvalidArgument)
{
    Texture2D tex; // default-constructed; null-stream guard fires before the CPU-pixels guard
    EXPECT_THROW(tex.SaveAsPng(nullptr, 0, 0), std::invalid_argument);
}

TEST_F(SaveAsPngTest, NoCpuPixelDataThrowsRuntimeError)
{
    Texture2D tex; // no SetData / renderer -> cpuPixels_ is empty
    MemoryStream stream;
    EXPECT_THROW(tex.SaveAsPng(&stream, 0, 0), std::runtime_error);
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

TEST_F(SaveAsJpegTest, NullStreamThrowsInvalidArgument)
{
    Texture2D tex;
    EXPECT_THROW(tex.SaveAsJpeg(nullptr, 0, 0), std::invalid_argument);
}

TEST_F(SaveAsJpegTest, NoCpuPixelDataThrowsRuntimeError)
{
    Texture2D tex;
    MemoryStream stream;
    EXPECT_THROW(tex.SaveAsJpeg(&stream, 0, 0), std::runtime_error);
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
