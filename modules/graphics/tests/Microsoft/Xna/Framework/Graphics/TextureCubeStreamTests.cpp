// SPDX-License-Identifier: MS-PL
// ISSUE-054: stream boundaries must not manufacture DDS payload bytes.
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>
#include <gtest/gtest.h>
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "System/ArgumentException.hpp"
#include "System/FormatException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/IO/IOException.hpp"
#include "System/IO/MemoryStream.hpp"

namespace
{
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace System::IO;

const std::array<Color, 6>& Colors()
{
    // Color's named constants have dynamic initialization in another translation unit.
    static const std::array<Color, 6> colors{Color::Red, Color::Cyan, Color::Blue,
                                           Color::Yellow, Color::Lime, Color::Magenta};
    return colors;
}

// The original demo's 506-byte RGB24 cube: distinct colors at all six faces'
// three mip levels; the last (blue) byte is nonzero so padding is observable.
std::vector<bytecs> CubeBytes(int prefix)
{
    std::vector<bytecs> bytes(128, 0);
    bytes[0] = 'D'; bytes[1] = 'D'; bytes[2] = 'S'; bytes[3] = ' ';
    const auto word = [&bytes](int offset, std::uint32_t value) {
        for (int i = 0; i < 4; ++i)
            bytes[offset + i] = static_cast<bytecs>(value >> (8 * i));
    };
    word(4, 124); word(8, 0x2100f); word(12, 4); word(16, 4); word(20, 12);
    word(28, 3); word(76, 32); word(80, 0x40); word(88, 24);
    word(92, 0xff); word(96, 0xff00); word(100, 0xff0000);
    word(108, 0x401008); word(112, 0xfe00);
    for (int face = 0; face < 6; ++face)
        for (int level = 0; level < 3; ++level)
            for (int i = 0; i < (4 >> level) * (4 >> level); ++i)
            {
                const auto& color = Colors()[(face + level) % 6];
                bytes.insert(bytes.end(), {color.getRProperty(), color.getGProperty(),
                                            color.getBProperty()});
            }
    bytes.insert(bytes.begin(), prefix, 0x5a);
    return bytes;
}

class BoundedStream : public MemoryStream
{
public:
    BoundedStream(const std::vector<bytecs>& bytes, int chunk, int missing = 0)
        : MemoryStream(bytes.data(), static_cast<intcs>(bytes.size()), false),
          chunk_(chunk), missing_(missing) {}
    intcs Read(bytecs buffer[], intcs offset, intcs count) override
    {
        ++calls;
        return MemoryStream::Read(buffer, offset, std::min<intcs>(count, chunk_));
    }
    intcs getLengthProperty() const override
    {
        // Model a source reaching EOF before its originally reported length.
        return MemoryStream::getLengthProperty() + missing_;
    }
    int calls = 0;
private:
    int chunk_;
    int missing_;
};

class TextureCubeStreamTest : public testing::Test
{
protected:
    GraphicsDevice device{GraphicsAdapter::getDefaultAdapterProperty(),
                          GraphicsProfile::HiDef, PresentationParameters{}};
    void SetUp() override
    {
        RecordProperty("renderer", std::string(device.GetGraphicsRendererName()));
    }
    void ExpectPixels(TextureCube& cube)
    {
        EXPECT_EQ(cube.getSizeProperty(), 4);
        EXPECT_EQ(cube.getLevelCountProperty(), 3);
        EXPECT_EQ(cube.getFormatProperty(), SurfaceFormat::Color);
        for (int face = 0; face < 6; ++face)
            for (int level = 0; level < 3; ++level)
            {
                SCOPED_TRACE(testing::Message() << "face=" << face << " mip=" << level);
                std::vector<Color> pixels((4 >> level) * (4 >> level), Color::Orange);
                cube.GetData(static_cast<CubeMapFace>(face), level, nullptr,
                             pixels.data(), 0, static_cast<int>(pixels.size()));
                for (const auto& pixel : pixels) EXPECT_EQ(pixel, Colors()[(face + level) % 6]);
            }
    }
};

class TextureCubeCompleteStreamTest : public TextureCubeStreamTest,
    public testing::WithParamInterface<std::tuple<int, int>> {};

TEST_P(TextureCubeCompleteStreamTest, ReadsCurrentPositionAndLeavesCallerOpen)
{
    const auto [prefix, chunk] = GetParam();
    const auto bytes = CubeBytes(prefix);
    ASSERT_EQ(bytes.size(), 506u + prefix);
    BoundedStream stream(bytes, chunk);
    stream.setPositionProperty(prefix);
    if (CNA::Testing::ActiveRendererIs(CNA::GraphicsRendererType::Stub))
    {
        EXPECT_THROW((void)TextureCube::DDSFromStreamEXT(device, stream), System::NotSupportedException);
    }
    else
    {
        auto cube = TextureCube::DDSFromStreamEXT(device, stream);
        ExpectPixels(cube);
    }
    EXPECT_TRUE(stream.getCanReadProperty());
    EXPECT_EQ(stream.getPositionProperty(), static_cast<intcs>(bytes.size()));
    if (chunk == 7) { EXPECT_GT(stream.calls, 1); }
}

INSTANTIATE_TEST_SUITE_P(Boundaries, TextureCubeCompleteStreamTest,
    testing::Combine(testing::Values(0, 16), testing::Values(1024, 7)));

class TextureCubeTruncatedStreamTest : public TextureCubeStreamTest,
    public testing::WithParamInterface<std::tuple<int, int, int>> {};

TEST_P(TextureCubeTruncatedStreamTest, RefusesMissingByteWithoutPaddingOrClosing)
{
    const auto [prefix, chunk, missing] = GetParam();
    auto bytes = CubeBytes(prefix);
    ASSERT_EQ(bytes.back(), 255);
    bytes.pop_back();
    BoundedStream stream(bytes, chunk, missing);
    stream.setPositionProperty(prefix);
    EXPECT_THROW((void)TextureCube::DDSFromStreamEXT(device, stream), System::FormatException);
    EXPECT_TRUE(stream.getCanReadProperty());
    EXPECT_EQ(stream.getPositionProperty(), static_cast<intcs>(bytes.size()));
    if (chunk == 7) { EXPECT_GT(stream.calls, 1); }
}

INSTANTIATE_TEST_SUITE_P(Boundaries, TextureCubeTruncatedStreamTest,
    testing::Combine(testing::Values(0, 16), testing::Values(1024, 7), testing::Values(0, 1)));

TEST_F(TextureCubeStreamTest, ReadFailurePropagatesWithoutClosingCaller)
{
    class ThrowingStream final : public MemoryStream
    {
    public:
        using MemoryStream::MemoryStream;
        intcs Read(bytecs[], intcs, intcs) override { throw IOException("read failure witness"); }
    };
    const auto bytes = CubeBytes(0);
    ThrowingStream stream(bytes.data(), static_cast<intcs>(bytes.size()), false);
    EXPECT_THROW((void)TextureCube::DDSFromStreamEXT(device, stream), IOException);
    EXPECT_TRUE(stream.getCanReadProperty());
    EXPECT_EQ(stream.getPositionProperty(), 0);
}

TEST_F(TextureCubeStreamTest, EmptyStreamKeepsExistingArgumentGuardAndOwnership)
{
    MemoryStream stream;
    EXPECT_THROW((void)TextureCube::DDSFromStreamEXT(device, stream), System::ArgumentException);
    EXPECT_TRUE(stream.getCanReadProperty());
    EXPECT_EQ(stream.getPositionProperty(), 0);
}
}
