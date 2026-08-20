// SPDX-License-Identifier: MS-PL
//
// plans/plan_igl.md IGL-65: the IGL renderer's surface-format boundary and transfer byte arithmetic.
//
// Two real defects this suite exists to keep out.
//
// 1. `width * 4` as the row pitch of every texture. `ImageData::pixels` is packed in its own
//    SurfaceFormat, whose texel is 1, 2, 4, 8 or 16 bytes, so a narrower format was uploaded from
//    more bytes than it owns (an out-of-bounds read past the end of the vector) and a wider one
//    had every row sliced short. The checks below are stated per byte class rather than for
//    `Color` alone, because `Color` is the one case the broken arithmetic got right.
//
// 2. Substituting a different texel for an unrepresentable format. `Rgba64` (8 bytes,
//    R16G16B16A16 unsigned-normalized) was mapped to `igl::TextureFormat::RGBA_UInt32`, a 16-byte
//    integer-sampled texel -- twice the size, a different sampler type, and silently accepted.
//
// The reachable entry point is the renderer contract, not the XNA layer: `ImageData::surfaceFormat`
// carries an arbitrary ordinal into `CreateTexture`, while the shared `Texture::ValidateFormat`
// still admits only `SurfaceFormat::Color` for every renderer but Skia.
#include <gtest/gtest.h>

// plans/plan_runtimerenderer.md RTR-P9-9: PRESENT_, not only the identity macro -- this is device-free
// policy coverage, worth running whenever the family is compiled in.
#if defined(CNA_RENDERER_IGL) || defined(CNA_RENDERER_PRESENT_IGL)
#include "CNA/Internal/Renderers/Igl/IglSurfaceFormats.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"

#include <stdexcept>
#include <string>

namespace
{
    using namespace CNA::Internal::Renderers::Igl;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::Texture;

    constexpr int Ordinal(const SurfaceFormat format) { return static_cast<int>(format); }

    /// Every declared SurfaceFormat, derived from the enum rather than restated beside it.
    constexpr int kLastOrdinal = Ordinal(SurfaceFormat::UShortEXT);
}

// ---------------------------------------------------------------------------
// Byte arithmetic
// ---------------------------------------------------------------------------

TEST(IglSurfaceFormatTests, ATexelIsNotFourBytesForEveryFormat)
{
    // The whole point: one byte class per line, so an arithmetic that assumes 4 fails here rather
    // than in a driver.
    EXPECT_EQ(1, FormatUnitByteCount(Ordinal(SurfaceFormat::Alpha8)));
    EXPECT_EQ(1, FormatUnitByteCount(Ordinal(SurfaceFormat::ByteEXT)));
    EXPECT_EQ(2, FormatUnitByteCount(Ordinal(SurfaceFormat::Bgr565)));
    EXPECT_EQ(2, FormatUnitByteCount(Ordinal(SurfaceFormat::HalfSingle)));
    EXPECT_EQ(2, FormatUnitByteCount(Ordinal(SurfaceFormat::UShortEXT)));
    EXPECT_EQ(4, FormatUnitByteCount(Ordinal(SurfaceFormat::Color)));
    EXPECT_EQ(4, FormatUnitByteCount(Ordinal(SurfaceFormat::Rg32)));
    EXPECT_EQ(4, FormatUnitByteCount(Ordinal(SurfaceFormat::HalfVector2)));
    EXPECT_EQ(8, FormatUnitByteCount(Ordinal(SurfaceFormat::Rgba64)));
    EXPECT_EQ(8, FormatUnitByteCount(Ordinal(SurfaceFormat::Vector2)));
    EXPECT_EQ(8, FormatUnitByteCount(Ordinal(SurfaceFormat::HalfVector4)));
    EXPECT_EQ(16, FormatUnitByteCount(Ordinal(SurfaceFormat::Vector4)));
}

TEST(IglSurfaceFormatTests, TheRowPitchFollowsTheFormatRatherThanFourBytes)
{
    constexpr int width = 7;

    EXPECT_EQ(width * 1, FormatRowByteCount(Ordinal(SurfaceFormat::Alpha8), width));
    EXPECT_EQ(width * 2, FormatRowByteCount(Ordinal(SurfaceFormat::HalfSingle), width));
    EXPECT_EQ(width * 4, FormatRowByteCount(Ordinal(SurfaceFormat::Color), width));
    EXPECT_EQ(width * 8, FormatRowByteCount(Ordinal(SurfaceFormat::Vector2), width));
    EXPECT_EQ(width * 16, FormatRowByteCount(Ordinal(SurfaceFormat::Vector4), width));

    // Stated as an inequality too, because it is the exact shape of the bug: the same call that
    // returns width*4 for Color must not for anything else.
    for (int ordinal = 0; ordinal <= kLastOrdinal; ++ordinal)
    {
        if (FormatUnitByteCount(ordinal) == 4 || IsBlockCompressedFormat(ordinal))
            continue;
        EXPECT_NE(width * 4, FormatRowByteCount(ordinal, width))
            << "SurfaceFormat " << GetSurfaceFormatName(ordinal)
            << " must not be transferred with a Color-sized row pitch";
    }
}

TEST(IglSurfaceFormatTests, LinearRegionsAreWidthTimesHeightTimesTexelSize)
{
    EXPECT_EQ(8 * 8 * 4, FormatRegionByteCount(Ordinal(SurfaceFormat::Color), 8, 8));
    EXPECT_EQ(8 * 8 * 1, FormatRegionByteCount(Ordinal(SurfaceFormat::Alpha8), 8, 8));
    EXPECT_EQ(8 * 8 * 2, FormatRegionByteCount(Ordinal(SurfaceFormat::HalfSingle), 8, 8));
    EXPECT_EQ(4 * 4 * 8, FormatRegionByteCount(Ordinal(SurfaceFormat::Rgba64), 4, 4));
    EXPECT_EQ(4 * 4 * 16, FormatRegionByteCount(Ordinal(SurfaceFormat::Vector4), 4, 4));
}

TEST(IglSurfaceFormatTests, CompressedRegionsAreCountedInWholeBlocks)
{
    EXPECT_TRUE(IsBlockCompressedFormat(Ordinal(SurfaceFormat::Dxt1)));
    EXPECT_TRUE(IsBlockCompressedFormat(Ordinal(SurfaceFormat::Bc7EXT)));
    EXPECT_FALSE(IsBlockCompressedFormat(Ordinal(SurfaceFormat::Color)));
    EXPECT_FALSE(IsBlockCompressedFormat(Ordinal(SurfaceFormat::Vector4)));

    EXPECT_EQ(2 * 2 * 8, FormatRegionByteCount(Ordinal(SurfaceFormat::Dxt1), 8, 8));
    EXPECT_EQ(2 * 2 * 16, FormatRegionByteCount(Ordinal(SurfaceFormat::Dxt5), 8, 8));
    // A 5-texel-wide level still occupies two full block columns; shorting that partial tail is
    // exactly the under-read that corrupts the last block row.
    EXPECT_EQ(2 * 8, FormatRowByteCount(Ordinal(SurfaceFormat::Dxt1), 5));
    EXPECT_EQ(2 * 2 * 8, FormatRegionByteCount(Ordinal(SurfaceFormat::Dxt1), 5, 5));
}

TEST(IglSurfaceFormatTests, ABoxIsOneRegionPerSlice)
{
    EXPECT_EQ(4 * 4 * 4 * 4, FormatBoxByteCount(Ordinal(SurfaceFormat::Color), 4, 4, 4));
    EXPECT_EQ(2 * 3 * 5 * 16, FormatBoxByteCount(Ordinal(SurfaceFormat::Vector4), 2, 3, 5));
}

TEST(IglSurfaceFormatTests, NonPositiveExtentsAndUnknownOrdinalsCountZeroRatherThanThrow)
{
    // These feed guard expressions in the resource paths, which answer false rather than throwing.
    EXPECT_EQ(0, FormatRowByteCount(Ordinal(SurfaceFormat::Color), 0));
    EXPECT_EQ(0, FormatRegionByteCount(Ordinal(SurfaceFormat::Color), 4, -1));
    EXPECT_EQ(0, FormatBoxByteCount(Ordinal(SurfaceFormat::Color), 4, 4, 0));
    EXPECT_EQ(0, FormatUnitByteCount(-1));
    EXPECT_EQ(0, FormatUnitByteCount(kLastOrdinal + 1));
    EXPECT_EQ(0, FormatRegionByteCount(kLastOrdinal + 1, 4, 4));
}

TEST(IglSurfaceFormatTests, TheByteCountsDelegateToTheSharedFormatMetadata)
{
    // Not a second format table: whatever CNA declares a texel to be, this renderer transfers.
    for (int ordinal = 0; ordinal <= kLastOrdinal; ++ordinal)
    {
        EXPECT_EQ(Texture::GetFormatSizeEXT(static_cast<SurfaceFormat>(ordinal)),
                  FormatUnitByteCount(ordinal))
            << "SurfaceFormat " << GetSurfaceFormatName(ordinal);
    }
}

// ---------------------------------------------------------------------------
// The IGL format boundary
// ---------------------------------------------------------------------------

TEST(IglSurfaceFormatTests, Rgba64IsRefusedRatherThanMappedToAWiderIntegerTexel)
{
    // The regression: Rgba64 -> igl::TextureFormat::RGBA_UInt32. CNA's Rgba64 is an 8-byte
    // R16G16B16A16 unsigned-normalized texel (PackedVector::Rgba64 packs r | g<<16 | b<<32 |
    // a<<48); RGBA_UInt32 is a 16-byte R32G32B32A32 integer-sampled one. IGL v1.1.1 has no
    // 16-bit-per-channel RGBA format at all, so the honest answer is a refusal.
    EXPECT_FALSE(IsSupportedSurfaceFormat(Ordinal(SurfaceFormat::Rgba64)));
    EXPECT_EQ(igl::TextureFormat::Invalid,
              ToIglSurfaceFormatOrInvalid(Ordinal(SurfaceFormat::Rgba64)));
    EXPECT_NE(igl::TextureFormat::RGBA_UInt32,
              ToIglSurfaceFormatOrInvalid(Ordinal(SurfaceFormat::Rgba64)));
    EXPECT_THROW((void)ToIglSurfaceFormat(Ordinal(SurfaceFormat::Rgba64)), std::runtime_error);

    try
    {
        (void)ToIglSurfaceFormat(Ordinal(SurfaceFormat::Rgba64));
        FAIL() << "Rgba64 must be refused by name";
    }
    catch (const std::runtime_error& e)
    {
        const std::string message = e.what();
        EXPECT_NE(std::string::npos, message.find("Rgba64")) << message;
    }
}

TEST(IglSurfaceFormatTests, EverySupportedFormatMapsToAnIglFormatOfTheSameTexelSize)
{
    // What "supported" has to mean before a format may be created: IGL's texel is the same number
    // of bytes CNA thinks it is. A mapping that changed the size silently would corrupt every
    // upload and readback of that format.
    struct Expectation
    {
        SurfaceFormat format;
        igl::TextureFormat igl;
        int byteCount;
    };
    constexpr Expectation expectations[] = {
        {SurfaceFormat::Color, igl::TextureFormat::RGBA_UNorm8, 4},
        {SurfaceFormat::ColorBgraEXT, igl::TextureFormat::BGRA_UNorm8, 4},
        {SurfaceFormat::ColorSrgbEXT, igl::TextureFormat::RGBA_SRGB, 4},
        {SurfaceFormat::ByteEXT, igl::TextureFormat::R_UNorm8, 1},
        {SurfaceFormat::UShortEXT, igl::TextureFormat::R_UNorm16, 2},
        {SurfaceFormat::Rg32, igl::TextureFormat::RG_UNorm16, 4},
        {SurfaceFormat::Single, igl::TextureFormat::R_F32, 4},
        {SurfaceFormat::Vector2, igl::TextureFormat::RG_F32, 8},
        {SurfaceFormat::Vector4, igl::TextureFormat::RGBA_F32, 16},
        {SurfaceFormat::HalfSingle, igl::TextureFormat::R_F16, 2},
        {SurfaceFormat::HalfVector2, igl::TextureFormat::RG_F16, 4},
        {SurfaceFormat::HalfVector4, igl::TextureFormat::RGBA_F16, 8},
        {SurfaceFormat::HdrBlendable, igl::TextureFormat::RGBA_F16, 8},
    };

    for (const Expectation& expectation : expectations)
    {
        const int ordinal = Ordinal(expectation.format);
        EXPECT_TRUE(IsSupportedSurfaceFormat(ordinal)) << GetSurfaceFormatName(ordinal);
        EXPECT_EQ(expectation.igl, ToIglSurfaceFormatOrInvalid(ordinal))
            << GetSurfaceFormatName(ordinal);
        EXPECT_EQ(expectation.byteCount, FormatUnitByteCount(ordinal))
            << GetSurfaceFormatName(ordinal);
        EXPECT_NO_THROW((void)ToIglSurfaceFormat(ordinal));
    }
}

TEST(IglSurfaceFormatTests, FormatsIglCannotRepresentAreRefusedByNameRatherThanSubstituted)
{
    // Each of these used to fall through to RGBA_UNorm8 (or to a format with a different bit
    // order), so a caller asking for one silently got a texture of another layout.
    constexpr SurfaceFormat unsupported[] = {
        SurfaceFormat::Bgr565,          // XNA packs R5G6B5; IGL's B5G6R5 is reversed, and its
                                        // OpenGL backend refuses the format outright
        SurfaceFormat::Bgra5551,        // XNA packs A1R5G5B5; IGL has only B5G5R5A1 / R5G5B5A1
        SurfaceFormat::Bgra4444,        // XNA packs A4R4G4B4; IGL's two backends disagree
        SurfaceFormat::Dxt1,
        SurfaceFormat::Dxt3,
        SurfaceFormat::Dxt5,
        SurfaceFormat::Dxt5SrgbEXT,
        SurfaceFormat::Bc7EXT,          // IGL has the format; this renderer has no block-upload path
        SurfaceFormat::Bc7SrgbEXT,
        SurfaceFormat::NormalizedByte2, // IGL v1.1.1 has no SNorm texture format
        SurfaceFormat::NormalizedByte4,
        SurfaceFormat::Rgba1010102,     // backend-dependent channel order
        SurfaceFormat::Rgba64,
        SurfaceFormat::Alpha8,          // VK_FORMAT_UNDEFINED on Vulkan; GL_ALPHA is not core
    };

    for (const SurfaceFormat format : unsupported)
    {
        const int ordinal = Ordinal(format);
        EXPECT_FALSE(IsSupportedSurfaceFormat(ordinal)) << GetSurfaceFormatName(ordinal);
        EXPECT_EQ(igl::TextureFormat::Invalid, ToIglSurfaceFormatOrInvalid(ordinal))
            << GetSurfaceFormatName(ordinal);
        EXPECT_THROW((void)ToIglSurfaceFormat(ordinal), std::runtime_error)
            << GetSurfaceFormatName(ordinal);
    }
}

TEST(IglSurfaceFormatTests, EveryDeclaredFormatHasADecidedAnswerAndAName)
{
    // Derived from the enum, so a SurfaceFormat added to CNA fails here until this renderer has
    // actually decided what to do with it -- rather than inheriting a silent RGBA8 substitution.
    for (int ordinal = 0; ordinal <= kLastOrdinal; ++ordinal)
    {
        EXPECT_STRNE("<unknown>", GetSurfaceFormatName(ordinal)) << "ordinal " << ordinal;
        EXPECT_GT(FormatUnitByteCount(ordinal), 0) << GetSurfaceFormatName(ordinal);
        if (!IsSupportedSurfaceFormat(ordinal))
            EXPECT_THROW((void)ToIglSurfaceFormat(ordinal), std::runtime_error);
    }
    EXPECT_STREQ("<unknown>", GetSurfaceFormatName(kLastOrdinal + 1));
    EXPECT_STREQ("<unknown>", GetSurfaceFormatName(-1));
}

TEST(IglSurfaceFormatTests, AnOrdinalThatNamesNoFormatIsRefusedRatherThanDefaulted)
{
    EXPECT_FALSE(IsSupportedSurfaceFormat(-1));
    EXPECT_FALSE(IsSupportedSurfaceFormat(kLastOrdinal + 1));
    EXPECT_THROW((void)ToIglSurfaceFormat(kLastOrdinal + 1), std::runtime_error);
}
#endif // CNA_RENDERER_IGL || CNA_RENDERER_PRESENT_IGL
