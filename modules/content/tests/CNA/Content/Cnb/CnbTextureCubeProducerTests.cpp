// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-113: the TextureCube producer, and the shared DDS decoder it required.
//
// Correctness here is asserted against the DECODER'S CPU OUTPUT, not against GPU readback. Cube
// mip readback is not available on every renderer, so a test that only checked GetData() would be
// silently skipped exactly where a regression would hide. The GPU leg is still exercised, but it
// is the second check rather than the only one.

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbSourceImport.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Cnb/CnjToCnb.hpp"
#include "CNA/DdsCubeFixtureEXT.hpp"
#include "CNA/Internal/Graphics/DdsCubeDecoder.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "System/FormatException.hpp"
#include "System/NotSupportedException.hpp"

using CNA::Content::Cnb::CnbDocument;
using CNA::Content::Cnb::CompileCnjToCnb;
using CNA::TestSupport::BuildSolidColorCubeDds;
using CNA::TestSupport::DdsBlockFormat;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

namespace
{
    /// Six colours whose channels are each 0 or 255, so the round trip through DXT's RGB565 is
    /// exact and every assertion can name a precise value. All six differ, so a face reordering
    /// cannot pass.
    const Color* SixDistinctFaces()
    {
        static const Color faces[6] = {
            Color(255, 0, 0, 255),   Color(0, 255, 0, 255),   Color(0, 0, 255, 255),
            Color(255, 255, 0, 255), Color(255, 0, 255, 255), Color(0, 255, 255, 255),
        };
        return faces;
    }

    class Scratch
    {
    public:
        explicit Scratch(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_cnb_cube_" + tag + "_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_);
        }
        ~Scratch() { std::error_code e; std::filesystem::remove_all(path_, e); }
        Scratch(const Scratch&) = delete;
        Scratch& operator=(const Scratch&) = delete;
        [[nodiscard]] const std::filesystem::path& path() const { return path_; }
    private:
        std::filesystem::path path_;
    };

    void WriteBytes(const std::filesystem::path& p, const std::vector<std::uint8_t>& b)
    {
        std::ofstream o(p, std::ios::binary);
        o.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    }
    void WriteText(const std::filesystem::path& p, const std::string& t)
    {
        std::ofstream o(p); o << t;
    }

    /// Asserts every texel of one RGBA8 level is exactly `expected`.
    void ExpectSolid(const std::vector<std::uint8_t>& rgba, const Color& expected,
                     const char* what)
    {
        ASSERT_EQ(rgba.size() % 4u, 0u) << what;
        ASSERT_NE(rgba.size(), 0u) << what;
        for (std::size_t i = 0; i + 3u < rgba.size(); i += 4u)
        {
            ASSERT_EQ(rgba[i], expected.getRProperty()) << what << " texel " << (i / 4u);
            ASSERT_EQ(rgba[i + 1u], expected.getGProperty()) << what << " texel " << (i / 4u);
            ASSERT_EQ(rgba[i + 2u], expected.getBProperty()) << what << " texel " << (i / 4u);
            ASSERT_EQ(rgba[i + 3u], 255u) << what << " texel " << (i / 4u);
        }
    }
}

// --------------------------------------------------------------------------------------------
// The shared decoder itself -- pure CPU, no device anywhere in this section
// --------------------------------------------------------------------------------------------

TEST(CnbDdsCubeDecoderTest, EverySixFaceColourDecodesExactlyWithNoDeviceInSight)
{
    const std::vector<std::uint8_t> dds = BuildSolidColorCubeDds(4, SixDistinctFaces());
    const auto decoded = CNA::Internal::Graphics::DecodeDdsCube(dds.data(), dds.size(), "test");
    EXPECT_EQ(decoded.width, 4);
    EXPECT_EQ(decoded.mipCount, 1);
    for (int face = 0; face < 6; ++face)
    {
        ASSERT_EQ(decoded.faces[static_cast<std::size_t>(face)].size(), 1u) << "face " << face;
        ExpectSolid(decoded.faces[static_cast<std::size_t>(face)][0], SixDistinctFaces()[face],
                    "face");
    }
}

TEST(CnbDdsCubeDecoderTest, Dxt1Dxt3AndDxt5AllDecodeToTheSameColours)
{
    // The three codecs differ only in how they carry alpha; the fixture's blocks are opaque in all
    // three, so a decoder that mixed up the alpha block and the colour block would show up here.
    for (const auto format : {DdsBlockFormat::Dxt1, DdsBlockFormat::Dxt3, DdsBlockFormat::Dxt5})
    {
        const std::vector<std::uint8_t> dds =
            BuildSolidColorCubeDds(4, SixDistinctFaces(), 1, format);
        const auto decoded =
            CNA::Internal::Graphics::DecodeDdsCube(dds.data(), dds.size(), "test");
        for (int face = 0; face < 6; ++face)
        {
            ExpectSolid(decoded.faces[static_cast<std::size_t>(face)][0],
                        SixDistinctFaces()[face], "face");
        }
    }
}

TEST(CnbDdsCubeDecoderTest, AMipChainDecodesEveryLevelAtItsOwnSize)
{
    // 8 -> 4 -> 2: three levels, and the 2x2 level is smaller than one DXT block, which is where
    // the block-rounding maths has to be right.
    const std::vector<std::uint8_t> dds = BuildSolidColorCubeDds(8, SixDistinctFaces(), 3);
    const auto decoded = CNA::Internal::Graphics::DecodeDdsCube(dds.data(), dds.size(), "test");
    EXPECT_EQ(decoded.width, 8);
    EXPECT_EQ(decoded.mipCount, 3);
    const std::array<std::size_t, 3> expectedBytes{8u * 8u * 4u, 4u * 4u * 4u, 2u * 2u * 4u};
    for (int face = 0; face < 6; ++face)
    {
        ASSERT_EQ(decoded.faces[static_cast<std::size_t>(face)].size(), 3u);
        for (int level = 0; level < 3; ++level)
        {
            EXPECT_EQ(decoded.faces[static_cast<std::size_t>(face)][static_cast<std::size_t>(level)]
                          .size(),
                      expectedBytes[static_cast<std::size_t>(level)])
                << "face " << face << " level " << level;
            ExpectSolid(
                decoded.faces[static_cast<std::size_t>(face)][static_cast<std::size_t>(level)],
                SixDistinctFaces()[face], "mip level");
        }
    }
}

TEST(CnbDdsCubeDecoderTest, MalformedAndOutOfScopeDdsAreRefusedAsBefore)
{
    const auto decode = [](const std::vector<std::uint8_t>& d)
    { return CNA::Internal::Graphics::DecodeDdsCube(d.data(), d.size(), "test"); };

    EXPECT_THROW(decode(std::vector<std::uint8_t>(128u, 0xABu)), System::NotSupportedException);
    EXPECT_THROW(decode(std::vector<std::uint8_t>(64u, 0u)), System::NotSupportedException);

    // Not a cube map.
    EXPECT_THROW(decode(BuildSolidColorCubeDds(4, SixDistinctFaces(), 1, DdsBlockFormat::Dxt1,
                                                /*asCubeMap=*/false)),
                 System::FormatException);
    // A FourCC outside the supported scope -- the move must not have widened it.
    EXPECT_THROW(decode(BuildSolidColorCubeDds(4, SixDistinctFaces(), 1,
                                                DdsBlockFormat::UnsupportedFourCc)),
                 System::NotSupportedException);
    EXPECT_THROW(decode(BuildSolidColorCubeDds(4, SixDistinctFaces(), 1,
                                                DdsBlockFormat::NoFourCc)),
                 System::NotSupportedException);

    // Truncated part-way through the face payload.
    std::vector<std::uint8_t> truncatedPayload = BuildSolidColorCubeDds(4, SixDistinctFaces());
    truncatedPayload.resize(truncatedPayload.size() - 4u);
    EXPECT_THROW(decode(truncatedPayload), System::FormatException);

    // Truncated inside the header itself.
    std::vector<std::uint8_t> truncatedHeader = BuildSolidColorCubeDds(4, SixDistinctFaces());
    truncatedHeader.resize(100u);
    EXPECT_THROW(decode(truncatedHeader), System::NotSupportedException);

    // Non-square faces: patch the height field so it disagrees with the width.
    std::vector<std::uint8_t> oblong = BuildSolidColorCubeDds(4, SixDistinctFaces());
    oblong[12] = 8u; // height = 8, width still 4
    EXPECT_THROW(decode(oblong), System::FormatException);
}

// --------------------------------------------------------------------------------------------
// Direct DDS -> TextureCube.cnb
// --------------------------------------------------------------------------------------------

TEST(CnbTextureCubeProducerTest, ADdsCompilesToATextureCubeCnbWithEveryFaceIntact)
{
    Scratch root("direct");
    WriteBytes(root.path() / "sky.dds", BuildSolidColorCubeDds(4, SixDistinctFaces()));

    const auto cube =
        CNA::Content::Cnb::ImportDdsAsCnbTextureCube((root.path() / "sky.dds").string());
    EXPECT_EQ(cube.width, 4u);
    EXPECT_EQ(cube.height, 4u);
    EXPECT_EQ(cube.depth, 1u);
    EXPECT_EQ(cube.faceCount, 6u);
    EXPECT_EQ(cube.mipCount, 1u);
    ASSERT_EQ(cube.representations.size(), 1u);
    EXPECT_EQ(cube.representations[0].format, CNA::Content::Cnb::CnbTextureFormat::Rgba8);
    ASSERT_EQ(cube.representations[0].levels.size(), 6u);

    const std::vector<std::uint8_t> bytes =
        CNA::Content::Cnb::EncodeTextureCubeToCnb(cube, "sky");
    const CnbDocument document = CnbDocument::Parse(bytes, "sky.cnb");
    EXPECT_EQ(document.AssetTypeId(), CNA::Content::Cnb::CnbAssetTypeId::TextureCube);

    // Compared against the decoder's own CPU output, so this holds on every renderer rather than
    // only where cube readback happens to work.
    const auto decoded = CNA::Content::Cnb::DecodeTextureCubeFromCnb(document);
    for (int face = 0; face < 6; ++face)
    {
        ExpectSolid(decoded.representations[0].levels[static_cast<std::size_t>(face)],
                    SixDistinctFaces()[face], "compiled face");
    }
}

TEST(CnbTextureCubeProducerTest, AMippedDdsCompilesFaceMajorThenMip)
{
    Scratch root("mip");
    WriteBytes(root.path() / "m.dds", BuildSolidColorCubeDds(8, SixDistinctFaces(), 3));
    const auto cube =
        CNA::Content::Cnb::ImportDdsAsCnbTextureCube((root.path() / "m.dds").string());
    EXPECT_EQ(cube.mipCount, 3u);
    ASSERT_EQ(cube.representations[0].levels.size(), 18u);

    // Face-major then mip is what CnbTextureRepresentation documents; a transposed layout would
    // still have eighteen entries and be completely wrong.
    for (int face = 0; face < 6; ++face)
    {
        for (int level = 0; level < 3; ++level)
        {
            const std::size_t index = static_cast<std::size_t>(face) * 3u + level;
            ExpectSolid(cube.representations[0].levels[index], SixDistinctFaces()[face],
                        "face-major level");
        }
    }
    EXPECT_NO_THROW((void)CNA::Content::Cnb::EncodeTextureCubeToCnb(cube, "m"));
}

TEST(CnbTextureCubeProducerTest, ATextureCubeCnbLoadsThroughContentManager)
{
    Scratch root("load");
    WriteBytes(root.path() / "sky.cnb",
               CNA::Content::Cnb::EncodeTextureCubeToCnb(
                   CNA::Content::Cnb::DecodeDdsAsCnbTextureCube(
                       BuildSolidColorCubeDds(4, SixDistinctFaces()), "sky.dds"),
                   "sky"));

    GraphicsDevice device;
    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(device);
    auto cube = cm.Load<Microsoft::Xna::Framework::Graphics::TextureCube>("sky");
    EXPECT_EQ(cube.getSizeProperty(), 4);

    // The GPU leg, second and not sole: cube readback is not available on every renderer, so this
    // asserts what it can and the CPU comparisons above carry the correctness claim.
    for (int face = 0; face < 6; ++face)
    {
        std::vector<Color> texels(16);
        try
        {
            cube.GetData(static_cast<Microsoft::Xna::Framework::Graphics::CubeMapFace>(face),
                          texels.data(), 16);
        }
        catch (const System::NotSupportedException&)
        {
            GTEST_SKIP() << "this renderer cannot read cube faces back";
        }
        EXPECT_EQ(texels[0].getRProperty(), SixDistinctFaces()[face].getRProperty())
            << "face " << face;
        EXPECT_EQ(texels[0].getGProperty(), SixDistinctFaces()[face].getGProperty())
            << "face " << face;
        EXPECT_EQ(texels[0].getBProperty(), SixDistinctFaces()[face].getBProperty())
            << "face " << face;
    }
}

// --------------------------------------------------------------------------------------------
// TextureCube .cnj -> .cnb, and the invariant between the two routes
// --------------------------------------------------------------------------------------------

TEST(CnbTextureCubeProducerTest, ATextureCubeCnjCompilesAndAbsorbsItsDds)
{
    Scratch root("cnj");
    WriteBytes(root.path() / "sky.dds", BuildSolidColorCubeDds(4, SixDistinctFaces()));
    WriteText(root.path() / "sky.cnj",
              R"({"cnjVersion":1,"type":"TextureCube","sourceFile":"sky.dds"})");

    const auto compiled = CompileCnjToCnb((root.path() / "sky.cnj").string());
    EXPECT_EQ(compiled.assetTypeId, CNA::Content::Cnb::CnbAssetTypeId::TextureCube);
    EXPECT_NE(std::find(compiled.absorbedFiles.begin(), compiled.absorbedFiles.end(), "sky.dds"),
              compiled.absorbedFiles.end())
        << "the .dds must be absorbed, not referenced";

    const auto decoded = CNA::Content::Cnb::DecodeTextureCubeFromCnb(
        CnbDocument::Parse(compiled.bytes, "sky.cnb"));
    EXPECT_EQ(decoded.faceCount, 6u);
    for (int face = 0; face < 6; ++face)
    {
        ExpectSolid(decoded.representations[0].levels[static_cast<std::size_t>(face)],
                    SixDistinctFaces()[face], "face");
    }
}

TEST(CnbTextureCubeProducerTest, TheCnjAndDirectRoutesProduceIdenticalBytes)
{
    // Both routes run the same decoder over the same file and encode with the same writer, so the
    // semantics are intentionally identical -- which makes byte equality the right assertion, and
    // the one that would notice either route quietly growing its own processing.
    Scratch root("same");
    WriteBytes(root.path() / "sky.dds", BuildSolidColorCubeDds(4, SixDistinctFaces()));
    WriteText(root.path() / "sky.cnj",
              R"({"cnjVersion":1,"type":"TextureCube","sourceFile":"sky.dds"})");

    const auto viaCnj = CompileCnjToCnb((root.path() / "sky.cnj").string());
    const auto viaDirect = CNA::Content::Cnb::EncodeTextureCubeToCnb(
        CNA::Content::Cnb::ImportDdsAsCnbTextureCube((root.path() / "sky.dds").string()), "sky");
    EXPECT_EQ(viaCnj.bytes, viaDirect);
}

TEST(CnbTextureCubeProducerTest, CompilingTheSameCubeTwiceGivesIdenticalBytes)
{
    Scratch root("det");
    WriteBytes(root.path() / "sky.dds", BuildSolidColorCubeDds(8, SixDistinctFaces(), 3));
    WriteText(root.path() / "sky.cnj",
              R"({"cnjVersion":1,"type":"TextureCube","sourceFile":"sky.dds"})");

    const auto once = CNA::Content::Cnb::EncodeTextureCubeToCnb(
        CNA::Content::Cnb::ImportDdsAsCnbTextureCube((root.path() / "sky.dds").string()), "sky");
    const auto twice = CNA::Content::Cnb::EncodeTextureCubeToCnb(
        CNA::Content::Cnb::ImportDdsAsCnbTextureCube((root.path() / "sky.dds").string()), "sky");
    EXPECT_EQ(once, twice);
    EXPECT_EQ(CompileCnjToCnb((root.path() / "sky.cnj").string()).bytes,
              CompileCnjToCnb((root.path() / "sky.cnj").string()).bytes);
}

TEST(CnbTextureCubeProducerTest, AMalformedDdsIsRefusedByTheCompilerToo)
{
    Scratch root("bad");
    WriteBytes(root.path() / "bad.dds", std::vector<std::uint8_t>(200u, 0x11u));
    WriteText(root.path() / "bad.cnj",
              R"({"cnjVersion":1,"type":"TextureCube","sourceFile":"bad.dds"})");
    EXPECT_THROW((void)CompileCnjToCnb((root.path() / "bad.cnj").string()), std::exception);

    // A non-cube DDS must not become a six-faced .cnb by accident.
    WriteBytes(root.path() / "flat.dds",
               BuildSolidColorCubeDds(4, SixDistinctFaces(), 1, DdsBlockFormat::Dxt1, false));
    WriteText(root.path() / "flat.cnj",
              R"({"cnjVersion":1,"type":"TextureCube","sourceFile":"flat.dds"})");
    EXPECT_THROW((void)CompileCnjToCnb((root.path() / "flat.cnj").string()), std::exception);
}
