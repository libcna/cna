// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-116: the shared DDS cube decoder against headers written to break it.
//
// This decoder sits under two callers -- TextureCube::DDSFromStreamEXT at run time and the CNB
// TextureCube producer at compile time -- and both of them are handed files by someone else. The
// positive path is covered where its consumers are (TextureCubeTests.cpp, CnbTextureCubeProducer-
// Tests.cpp); what is asserted here is that a hostile HEADER is refused before any arithmetic is
// performed on its numbers, rather than after.
//
// Every fixture is a real DDS built by the shared builder and then edited in exactly one field, so
// "the decoder refused it" can never be confused with "the fixture was never valid".

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/DdsCubeFixtureEXT.hpp"
#include "CNA/Internal/Graphics/DdsCubeDecoder.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "System/FormatException.hpp"
#include "System/NotSupportedException.hpp"

using CNA::Internal::Graphics::DecodeDdsCube;
using CNA::Internal::Graphics::DecodedDdsCube;
using Microsoft::Xna::Framework::Color;

namespace
{
    // DDS header field offsets, spelled out so a layout change has to break these explicitly.
    constexpr std::size_t kOffHeight = 12;
    constexpr std::size_t kOffWidth = 16;
    constexpr std::size_t kOffMipCount = 28;
    constexpr std::size_t kOffRgbBitCount = 88;
    constexpr std::size_t kOffRBitMask = 92;
    constexpr std::size_t kOffCaps = 108;
    constexpr std::size_t kOffCaps2 = 112;

    const Color kFaceColors[6] = {
        Color(255, 0, 0, 255),   Color(0, 255, 0, 255),   Color(0, 0, 255, 255),
        Color(255, 255, 0, 255), Color(255, 0, 255, 255), Color(0, 255, 255, 255)};

    std::vector<std::uint8_t> ValidCube(int size = 8, int mips = 1)
    {
        return CNA::TestSupport::BuildSolidColorCubeDds(size, kFaceColors, mips);
    }

    void PatchU32(std::vector<std::uint8_t>& dds, std::size_t offset, std::uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
        {
            dds[offset + static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
        }
    }

    DecodedDdsCube Decode(const std::vector<std::uint8_t>& dds)
    {
        return DecodeDdsCube(dds.data(), dds.size(), "DdsCubeDecoderTest");
    }
}

TEST(DdsCubeDecoderTest, TheUnmodifiedFixtureDecodes)
{
    // Stated first so every refusal below is a refusal OF THE EDIT, not of the builder.
    const std::vector<std::uint8_t> dds = ValidCube(8, 4);
    DecodedDdsCube cube = Decode(dds);
    EXPECT_EQ(cube.width, 8);
    EXPECT_EQ(cube.mipCount, 4);
    for (int face = 0; face < 6; ++face)
    {
        ASSERT_EQ(cube.faces[static_cast<std::size_t>(face)].size(), 4u);
        EXPECT_EQ(cube.faces[static_cast<std::size_t>(face)][0].size(), 8u * 8u * 4u);
    }
}

TEST(DdsCubeDecoderTest, Rgb24AndBgr24DecodeEveryFaceAndMipToOpaqueRgba)
{
    for (const auto format : {CNA::TestSupport::DdsBlockFormat::Rgb24,
                              CNA::TestSupport::DdsBlockFormat::Bgr24})
    {
        const std::vector<std::uint8_t> dds =
            CNA::TestSupport::BuildSolidColorCubeDds(5, kFaceColors, 3, format);
        const DecodedDdsCube cube = Decode(dds);
        ASSERT_EQ(cube.width, 5);
        ASSERT_EQ(cube.mipCount, 3);

        const std::array<std::size_t, 3> extents{5u, 2u, 1u};
        for (std::size_t face = 0; face < 6u; ++face)
        {
            ASSERT_EQ(cube.faces[face].size(), extents.size());
            for (std::size_t level = 0; level < extents.size(); ++level)
            {
                const std::vector<std::uint8_t>& rgba = cube.faces[face][level];
                ASSERT_EQ(rgba.size(), extents[level] * extents[level] * 4u);
                for (std::size_t texel = 0; texel < extents[level] * extents[level]; ++texel)
                {
                    EXPECT_EQ(rgba[texel * 4u], kFaceColors[face].getRProperty());
                    EXPECT_EQ(rgba[texel * 4u + 1u], kFaceColors[face].getGProperty());
                    EXPECT_EQ(rgba[texel * 4u + 2u], kFaceColors[face].getBProperty());
                    EXPECT_EQ(rgba[texel * 4u + 3u], 255u);
                }
            }
        }
    }
}

TEST(DdsCubeDecoderTest, OtherUncompressedLayoutsRemainHonestRefusals)
{
    std::vector<std::uint8_t> wrongBitCount = CNA::TestSupport::BuildSolidColorCubeDds(
        4, kFaceColors, 1, CNA::TestSupport::DdsBlockFormat::Bgr24);
    PatchU32(wrongBitCount, kOffRgbBitCount, 32u);
    EXPECT_THROW((void)Decode(wrongBitCount), System::NotSupportedException);

    std::vector<std::uint8_t> overlappingMasks = CNA::TestSupport::BuildSolidColorCubeDds(
        4, kFaceColors, 1, CNA::TestSupport::DdsBlockFormat::Bgr24);
    PatchU32(overlappingMasks, kOffRBitMask, 0x0000FF00u);
    EXPECT_THROW((void)Decode(overlappingMasks), System::NotSupportedException);
}

TEST(DdsCubeDecoderTest, DimensionsAtTheTopOfTheUnsignedRangeAreRefused)
{
    // 0xFFFFFFFF used to be cast to int as -1 and caught only by the `width <= 0` guard; the value
    // below it that survived, 0x7FFFFFFF, is the one that mattered -- it is positive, so it passed
    // that guard and reached `(width + 3) / 4`, which is signed overflow.
    for (const std::uint32_t hostile : {0xFFFFFFFFu, 0x80000000u, 0x7FFFFFFFu, 0x40000000u,
                                        16385u})
    {
        std::vector<std::uint8_t> dds = ValidCube();
        PatchU32(dds, kOffWidth, hostile);
        PatchU32(dds, kOffHeight, hostile);
        EXPECT_THROW((void)Decode(dds), System::FormatException)
            << "width/height " << hostile << " was accepted";
    }
}

TEST(DdsCubeDecoderTest, TheLargestRepresentableDimensionIsStillAccepted)
{
    // The ceiling must not be lower than it claims. A 16384-texel face is refused only for being
    // truncated -- the header itself is legal -- which proves the dimension check let it through.
    //
    // The budget has to be lifted for this one (plans/plan_cnb.md CNBF-122): six 16384-texel faces
    // decode to 6 GiB even at a single mip level, which the DEFAULT budget refuses before the
    // payload is looked at. Isolating the dimension check is exactly what an injected budget is
    // for -- and the two layers are asserted in order, one test each, rather than one standing in
    // for the other.
    std::vector<std::uint8_t> dds = ValidCube();
    PatchU32(dds, kOffWidth, 16384u);
    PatchU32(dds, kOffHeight, 16384u);
    try
    {
        (void)DecodeDdsCube(dds.data(), dds.size(), "DdsCubeDecoderTest",
                            /*maxDecodedBytes=*/16ull * 1024ull * 1024ull * 1024ull);
        FAIL() << "a 16384-texel cube with an 8-texel payload cannot decode";
    }
    catch (const System::FormatException& e)
    {
        EXPECT_NE(std::string(e.what()).find("truncated"), std::string::npos)
            << "expected the TRUNCATION refusal, not a dimension refusal: " << e.what();
    }

    // At the default budget the same header is refused earlier, and for the aggregate rather than
    // for the dimension -- which is the layering CNBF-122 added, stated where someone changing
    // either ceiling will see it.
    try
    {
        (void)Decode(dds);
        FAIL() << "the default budget accepted a 6 GiB decode";
    }
    catch (const System::FormatException& e)
    {
        EXPECT_NE(std::string(e.what()).find("decoded-output budget"), std::string::npos)
            << "expected the BUDGET refusal at the default: " << e.what();
    }
}

TEST(DdsCubeDecoderTest, AZeroDimensionIsRefusedRatherThanProducingSixEmptyFaces)
{
    std::vector<std::uint8_t> dds = ValidCube();
    PatchU32(dds, kOffWidth, 0u);
    PatchU32(dds, kOffHeight, 0u);
    EXPECT_THROW((void)Decode(dds), System::FormatException);
}

TEST(DdsCubeDecoderTest, AMipCountLongerThanThePhysicalChainIsRefusedBeforeAnyReserve)
{
    // Two billion levels used to be reserve()d for before a byte of the file was consulted. An
    // 8-texel face has exactly four levels; five is already impossible, and the refusal must not
    // depend on the file happening to run out of bytes.
    for (const std::uint32_t hostile : {0x7FFFFFFFu, 0xFFFFFFFEu, 1000u, 17u, 5u})
    {
        std::vector<std::uint8_t> dds = ValidCube(8, 4);
        PatchU32(dds, kOffMipCount, hostile);
        try
        {
            (void)Decode(dds);
            FAIL() << "mip count " << hostile << " was accepted";
        }
        catch (const System::FormatException& e)
        {
            EXPECT_NE(std::string(e.what()).find("mip levels"), std::string::npos)
                << "mip count " << hostile << " gave: " << e.what();
        }
    }
}

TEST(DdsCubeDecoderTest, TheExactPhysicalMipChainLengthIsAccepted)
{
    // An off-by-one in the chain-length rule would refuse legitimate content, which is the failure
    // a new bound is most likely to introduce.
    const std::vector<std::uint8_t> dds = ValidCube(16, 5);   // 16, 8, 4, 2, 1
    DecodedDdsCube cube = Decode(dds);
    EXPECT_EQ(cube.mipCount, 5);
    EXPECT_EQ(cube.faces[0].back().size(), 1u * 1u * 4u);
}

TEST(DdsCubeDecoderTest, AnUndeclaredCubeFaceIsRefused)
{
    // The decoder reads six faces unconditionally. A file declaring only some of them has fewer
    // faces' worth of bytes than will be taken out of it, so the payload after the declared faces
    // would be read as image data.
    for (const std::uint32_t missing : {0x400u, 0x800u, 0x1000u, 0x2000u, 0x4000u, 0x8000u})
    {
        std::vector<std::uint8_t> dds = ValidCube();
        PatchU32(dds, kOffCaps2,
                 CNA::TestSupport::kDdsCaps2Cubemap |
                     (CNA::TestSupport::kDdsCaps2AllFaces & ~missing));
        EXPECT_THROW((void)Decode(dds), System::FormatException)
            << "a cube map missing face bit " << missing << " was accepted";
    }

    // No face bits at all: still a "cube map", still no faces.
    std::vector<std::uint8_t> none = ValidCube();
    PatchU32(none, kOffCaps2, CNA::TestSupport::kDdsCaps2Cubemap);
    EXPECT_THROW((void)Decode(none), System::FormatException);
}

TEST(DdsCubeDecoderTest, ACaps2BitOutsideTheCubeSetIsRefused)
{
    // DDSCAPS2_VOLUME beside the cube bits describes a payload that is not six square faces.
    std::vector<std::uint8_t> dds = ValidCube();
    PatchU32(dds, kOffCaps2, CNA::TestSupport::kDdsCaps2Cubemap |
                                 CNA::TestSupport::kDdsCaps2AllFaces | 0x200000u);
    EXPECT_THROW((void)Decode(dds), System::NotSupportedException);
}

TEST(DdsCubeDecoderTest, AFileTruncatedPartWayThroughAFaceIsRefused)
{
    const std::vector<std::uint8_t> full = ValidCube(8, 4);
    // Every truncation point after the header: each must be refused, and none may read past the
    // end. Under a sanitizer this is where an out-of-bounds read would surface.
    for (std::size_t keep = 128u; keep < full.size(); keep += 8u)
    {
        const std::vector<std::uint8_t> cut(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(keep));
        EXPECT_THROW((void)Decode(cut), System::FormatException) << "kept " << keep << " bytes";
    }
    // And the complete file still decodes, so the loop above is not vacuous.
    EXPECT_NO_THROW((void)Decode(full));
}

TEST(DdsCubeDecoderTest, AHeaderOnlyFileIsRefusedRatherThanDecodingNothing)
{
    std::vector<std::uint8_t> dds = ValidCube();
    dds.resize(128u);
    EXPECT_THROW((void)Decode(dds), System::FormatException);
}

TEST(DdsCubeDecoderTest, NonCubeAndUnsupportedFormatsKeepTheirEstablishedRefusals)
{
    // CNBF-113 froze this scope; CNBF-116's hardening must not have widened or narrowed it.
    const std::vector<std::uint8_t> notCube = CNA::TestSupport::BuildSolidColorCubeDds(
        8, kFaceColors, 1, CNA::TestSupport::DdsBlockFormat::Dxt1, /*asCubeMap=*/false);
    EXPECT_THROW((void)Decode(notCube), System::FormatException);

    const std::vector<std::uint8_t> dx10 = CNA::TestSupport::BuildSolidColorCubeDds(
        8, kFaceColors, 1, CNA::TestSupport::DdsBlockFormat::UnsupportedFourCc);
    EXPECT_THROW((void)Decode(dx10), System::NotSupportedException);

    const std::vector<std::uint8_t> noFourCc = CNA::TestSupport::BuildSolidColorCubeDds(
        8, kFaceColors, 1, CNA::TestSupport::DdsBlockFormat::NoFourCc);
    EXPECT_THROW((void)Decode(noFourCc), System::NotSupportedException);

    // DXT3 and DXT5 still decode, so the refusals above are format-specific rather than blanket.
    for (const auto format : {CNA::TestSupport::DdsBlockFormat::Dxt3,
                              CNA::TestSupport::DdsBlockFormat::Dxt5})
    {
        EXPECT_NO_THROW(
            (void)Decode(CNA::TestSupport::BuildSolidColorCubeDds(8, kFaceColors, 1, format)));
    }
}

TEST(DdsCubeDecoderTest, ANonSquareOrNonTextureHeaderIsRefused)
{
    std::vector<std::uint8_t> oblong = ValidCube();
    PatchU32(oblong, kOffHeight, 16u);
    EXPECT_THROW((void)Decode(oblong), System::FormatException);

    std::vector<std::uint8_t> notTexture = ValidCube();
    PatchU32(notTexture, kOffCaps, 0u);
    EXPECT_THROW((void)Decode(notTexture), System::NotSupportedException);
}

TEST(DdsCubeDecoderTest, ANullOrShortBufferIsRefusedWithoutDereferencingIt)
{
    EXPECT_THROW((void)DecodeDdsCube(nullptr, 0u, "null"), System::NotSupportedException);
    EXPECT_THROW((void)DecodeDdsCube(nullptr, 4096u, "null with a size"),
                 System::NotSupportedException);
    const std::vector<std::uint8_t> tiny(16u, 0u);
    EXPECT_THROW((void)DecodeDdsCube(tiny.data(), tiny.size(), "tiny"),
                 System::NotSupportedException);
}

// --------------------------------------------------------------------------------------------
// CNBF-122 -- the aggregate DECODED size is budgeted, not just each level's
// --------------------------------------------------------------------------------------------

TEST(DdsCubeDecoderTest, TheAggregateDecodedSizeIsBudgetedBeforeAnythingIsAllocated)
{
    // The gap CNBF-116's dimension ceiling left. It bounds ONE level of ONE face; a cube map is six
    // faces each holding a whole mip chain, so the retained RGBA is up to eight times a single
    // level -- about 8.6 GiB at the 16384-texel maximum, every allocation of it individually
    // representable. The budget is therefore about the AGGREGATE and has to be computed from the
    // header before any output vector exists.
    //
    // Exercised with an injected small budget rather than a real large image: the arithmetic under
    // test is the same either way, and a genuine 8 GiB fixture is not a test.
    const std::vector<std::uint8_t> dds = ValidCube(8, 4);

    // 8x8 with four levels: 6 * 4 * (64 + 16 + 4 + 1) = 2040 bytes decoded.
    constexpr std::uint64_t kExact = 6ull * 4ull * (64ull + 16ull + 4ull + 1ull);

    EXPECT_NO_THROW((void)DecodeDdsCube(dds.data(), dds.size(), "exact", kExact))
        << "a cube map exactly at the budget must decode";
    EXPECT_THROW((void)DecodeDdsCube(dds.data(), dds.size(), "one byte over", kExact - 1u),
                 System::FormatException);

    // One mip LEVEL fewer is the other axis the budget has to see: the same face dimension, a
    // smaller total. A budget that admits the three-level cube must refuse the four-level one.
    const std::vector<std::uint8_t> threeLevels = ValidCube(8, 3);
    constexpr std::uint64_t kThreeLevels = 6ull * 4ull * (64ull + 16ull + 4ull);
    EXPECT_NO_THROW((void)DecodeDdsCube(threeLevels.data(), threeLevels.size(), "three levels",
                                        kThreeLevels));
    EXPECT_THROW((void)DecodeDdsCube(dds.data(), dds.size(), "one level over", kThreeLevels),
                 System::FormatException);

    // A budget of zero refuses everything, so the check cannot be short-circuited by an empty cube.
    EXPECT_THROW((void)DecodeDdsCube(dds.data(), dds.size(), "zero budget", 0u),
                 System::FormatException);
}

TEST(DdsCubeDecoderTest, TheDefaultBudgetAdmitsEveryCubeAGpuWouldAcceptAndRefusesTheLargest)
{
    // The default is a number with a reason, so the reason is asserted rather than described. A
    // fully mipped 8192-texel cube is the largest complete cube that fits; the 16384 one -- which
    // the dimension ceiling still permits -- is four times that and is refused.
    const auto fullyMippedCubeBytes = [](std::uint64_t extent)
    {
        std::uint64_t total = 0u;
        while (true)
        {
            total += extent * extent * 4u * 6u;
            if (extent == 1u) { break; }
            extent /= 2u;
        }
        return total;
    };

    EXPECT_LE(fullyMippedCubeBytes(8192u),
              CNA::Internal::Graphics::DefaultDdsCubeDecodedByteBudget)
        << "the default budget must admit a fully mipped 8192-texel cube map";
    EXPECT_GT(fullyMippedCubeBytes(16384u),
              CNA::Internal::Graphics::DefaultDdsCubeDecodedByteBudget)
        << "the default budget must refuse the ~8.6 GiB cube the dimension ceiling still permits";
    // Each step up in dimension is four times the previous chain plus its own extra 1x1 level,
    // which for six faces of RGBA is 24 bytes. Asserted so a future change to the ceiling has to
    // face the arithmetic rather than guess at it.
    EXPECT_EQ(fullyMippedCubeBytes(4096u) * 4u + 24u, fullyMippedCubeBytes(8192u))
        << "the arithmetic this bound rests on is wrong";
    EXPECT_EQ(fullyMippedCubeBytes(8192u), 2147483640ull);
}
