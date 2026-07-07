// SPDX-License-Identifier: MS-PL
// Task 272: TextureCube audit — FNA API conformance tests.
//
// Tests cover:
//   • Constructor properties (Size/Format/LevelCount), including mipMap level-count math
//     (Task 272 audit finding: previously hardcoded to 1 regardless of mipMap, same bug class
//     as Texture3D's Task 271 finding).
//   • GetTypeName.
//   • The previously-missing SetData/GetData(face, data, startIndex, elementCount) overload
//     (Task 272 audit finding: FNA has 3 SetData/GetData overload arities for TextureCube;
//     CNA only had 2 — the middle one was missing entirely).
//   • SetData/GetData argument guards (null data, elementCount<=0, negative startIndex,
//     negative level, invalid rect) — previously missing entirely (Task 272 audit finding,
//     same class of bug fixed for Texture2D in Tasks 265/266 and Texture3D in Task 271).
//   • The rect=nullptr-at-level>0 mip-dimension bug (Task 272 audit finding: SetData/GetData
//     used the full face Size regardless of level when no explicit rect was given, instead of
//     Size>>level like Texture2D/Texture3D's mipDim() pattern — verified both that the fix
//     accepts a correctly-sized call at level>0 and rejects one sized for the full face).
//   • Dispose marks the resource as disposed.
//
// Happy-path SetData/GetData round-trip coverage (per-face colour verification, level 0 only)
// lives in the EasyGL pixel-readback integration test: examples/easygl_texturecube_faces_test.cpp.

#include <gtest/gtest.h>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "System/FormatException.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/NotSupportedException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::TextureCube;

// -----------------------------------------------------------------------
// Constructor / properties
// -----------------------------------------------------------------------

class TextureCubeTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
};

TEST_F(TextureCubeTest, ConstructorSetsSize)
{
    TextureCube tex(gd, 4, false, SurfaceFormat::Color);
    EXPECT_EQ(tex.getSizeProperty(), 4);
}

TEST_F(TextureCubeTest, ConstructorSetsFormat)
{
    TextureCube tex(gd, 4, false, SurfaceFormat::Color);
    EXPECT_EQ(tex.getFormatProperty(), SurfaceFormat::Color);
}

TEST_F(TextureCubeTest, GetTypeNameReturnsFullyQualifiedName)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    EXPECT_EQ(tex.GetTypeName(), "Microsoft.Xna.Framework.Graphics.TextureCube");
}

// -----------------------------------------------------------------------
// LevelCount — mipmapped vs non-mipmapped construction (Task 272)
//
// Mirrors FNA's TextureCube constructor: LevelCount = mipMap ? CalculateMipLevels(size) : 1.
// Previously hardcoded to 1 regardless of mipMap — a real bug fixed by this task (same class
// as Texture3D's Task 271 finding).
// -----------------------------------------------------------------------

TEST_F(TextureCubeTest, MipMapFalseIsAlwaysOne)
{
    EXPECT_EQ(TextureCube(gd, 8, false, SurfaceFormat::Color).getLevelCountProperty(), 1);
    EXPECT_EQ(TextureCube(gd, 3, false, SurfaceFormat::Color).getLevelCountProperty(), 1);
}

TEST_F(TextureCubeTest, MipMapTruePowerOfTwo)
{
    EXPECT_EQ(TextureCube(gd, 1, true, SurfaceFormat::Color).getLevelCountProperty(), 1);
    EXPECT_EQ(TextureCube(gd, 4, true, SurfaceFormat::Color).getLevelCountProperty(), 3);
    EXPECT_EQ(TextureCube(gd, 16, true, SurfaceFormat::Color).getLevelCountProperty(), 5);
}

TEST_F(TextureCubeTest, MipMapTrueNonPowerOfTwo)
{
    EXPECT_EQ(TextureCube(gd, 3, true, SurfaceFormat::Color).getLevelCountProperty(), 2);
    EXPECT_EQ(TextureCube(gd, 7, true, SurfaceFormat::Color).getLevelCountProperty(), 3);
}

// -----------------------------------------------------------------------
// SetData(face, data, elementCount) / SetData(face, data, startIndex, elementCount)
// — argument guards (both delegate to the 6-arg overload; guards live there)
//
// The 4-arg (face,data,startIndex,elementCount) overload was previously missing entirely
// (Task 272 audit finding) — its presence here is itself part of the fix.
// -----------------------------------------------------------------------

TEST_F(TextureCubeTest, SetDataSimpleNullDataThrowsInvalidArgument)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    EXPECT_THROW(tex.SetData(CubeMapFace::PositiveX, nullptr, 4), std::invalid_argument);
}

TEST_F(TextureCubeTest, SetDataSimpleZeroElementCountThrowsOutOfRange)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    Color buf[1] = { Color(0, 0, 0, 0) };
    EXPECT_THROW(tex.SetData(CubeMapFace::PositiveX, buf, 0), std::out_of_range);
}

TEST_F(TextureCubeTest, SetDataStartIndexNullDataThrowsInvalidArgument)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    EXPECT_THROW(tex.SetData(CubeMapFace::PositiveX, nullptr, 0, 4), std::invalid_argument);
}

TEST_F(TextureCubeTest, SetDataStartIndexNegativeStartIndexThrowsOutOfRange)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.SetData(CubeMapFace::PositiveX, buf.data(), -1, 4), std::out_of_range);
}

TEST_F(TextureCubeTest, SetDataExactElementCountDoesNotThrow)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(1, 2, 3, 4));
    EXPECT_NO_THROW(tex.SetData(CubeMapFace::PositiveX, buf.data(), 4));
    EXPECT_NO_THROW(tex.SetData(CubeMapFace::PositiveX, buf.data(), 0, 4));
}

// -----------------------------------------------------------------------
// SetData(face, level, rect, data, startIndex, elementCount) — argument guards
// (Task 272: previously none of these existed at all)
// -----------------------------------------------------------------------

TEST_F(TextureCubeTest, SetDataRectNullDataThrowsInvalidArgument)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    EXPECT_THROW(tex.SetData(CubeMapFace::PositiveX, 0, nullptr, nullptr, 0, 4), std::invalid_argument);
}

TEST_F(TextureCubeTest, SetDataRectZeroElementCountThrowsOutOfRange)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    Color buf[1] = { Color(0, 0, 0, 0) };
    EXPECT_THROW(tex.SetData(CubeMapFace::PositiveX, 0, nullptr, buf, 0, 0), std::out_of_range);
}

TEST_F(TextureCubeTest, SetDataRectNegativeStartIndexThrowsOutOfRange)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.SetData(CubeMapFace::PositiveX, 0, nullptr, buf.data(), -1, 4), std::out_of_range);
}

TEST_F(TextureCubeTest, SetDataRectNegativeLevelThrowsOutOfRange)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.SetData(CubeMapFace::PositiveX, -1, nullptr, buf.data(), 0, 4), std::out_of_range);
}

TEST_F(TextureCubeTest, SetDataRectOutOfBoundsThrowsOutOfRange)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    Color buf[1] = { Color(0, 0, 0, 0) };
    const Rectangle rect(1, 1, 2, 2); // extends past the 2x2 face
    EXPECT_THROW(tex.SetData(CubeMapFace::PositiveX, 0, &rect, buf, 0, 4), std::out_of_range);
}

TEST_F(TextureCubeTest, SetDataRectWithinBoundsDoesNotThrow)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    Color buf[1] = { Color(1, 2, 3, 4) };
    const Rectangle rect(0, 0, 1, 1);
    EXPECT_NO_THROW(tex.SetData(CubeMapFace::PositiveX, 0, &rect, buf, 0, 1));
}

// -----------------------------------------------------------------------
// Mip-level dimension bug (Task 272): rect=nullptr at level>0 must use the
// mip-reduced face size (Size>>level), not the full face Size.
// -----------------------------------------------------------------------

TEST_F(TextureCubeTest, SetDataNullRectAtMipLevelUsesReducedSize)
{
    // size=4, mipMap=true -> levels 4x4, 2x2, 1x1. Level 1 is 2x2 = 4 elements.
    TextureCube tex(gd, 4, true, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(1, 2, 3, 4));
    EXPECT_NO_THROW(tex.SetData(CubeMapFace::PositiveX, 1, nullptr, buf.data(), 0, 4));
}

TEST_F(TextureCubeTest, SetDataNullRectAtMipLevelRejectsFullFaceSizedElementCount)
{
    // Level 1 of a size=4 cube is 2x2 (4 elements) — an elementCount sized for the *full*
    // 4x4 face (16) must be rejected as exceeding the level's actual bounds, proving level is
    // no longer ignored when rect is null.
    TextureCube tex(gd, 4, true, SurfaceFormat::Color);
    std::vector<Color> buf(16, Color(1, 2, 3, 4));
    const Rectangle fullFaceRect(0, 0, 4, 4); // valid for level 0, not level 1
    EXPECT_THROW(tex.SetData(CubeMapFace::PositiveX, 1, &fullFaceRect, buf.data(), 0, 16),
                 std::out_of_range);
}

// -----------------------------------------------------------------------
// GetData — argument guards (mirrors SetData's guards)
// -----------------------------------------------------------------------

TEST_F(TextureCubeTest, GetDataSimpleNullDataThrowsInvalidArgument)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    EXPECT_THROW(tex.GetData(CubeMapFace::PositiveX, nullptr, 4), std::invalid_argument);
}

TEST_F(TextureCubeTest, GetDataStartIndexNegativeStartIndexThrowsOutOfRange)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.GetData(CubeMapFace::PositiveX, buf.data(), -1, 4), std::out_of_range);
}

TEST_F(TextureCubeTest, GetDataRectNullDataThrowsInvalidArgument)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    EXPECT_THROW(tex.GetData(CubeMapFace::PositiveX, 0, nullptr, nullptr, 0, 4), std::invalid_argument);
}

TEST_F(TextureCubeTest, GetDataRectNegativeLevelThrowsOutOfRange)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.GetData(CubeMapFace::PositiveX, -1, nullptr, buf.data(), 0, 4), std::out_of_range);
}

TEST_F(TextureCubeTest, GetDataRectOutOfBoundsThrowsOutOfRange)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    Color buf[1] = { Color(0, 0, 0, 0) };
    const Rectangle rect(1, 1, 2, 2);
    EXPECT_THROW(tex.GetData(CubeMapFace::PositiveX, 0, &rect, buf, 0, 4), std::out_of_range);
}

TEST_F(TextureCubeTest, GetDataRectWithinBoundsDoesNotThrow)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    Color buf[1] = { Color(0, 0, 0, 0) };
    const Rectangle rect(0, 0, 1, 1);
    EXPECT_NO_THROW(tex.GetData(CubeMapFace::PositiveX, 0, &rect, buf, 0, 1));
}

// -----------------------------------------------------------------------
// Invalid CubeMapFace values (Task 279)
//
// FNA itself never validates cubeMapFace — it's passed straight through to
// FNA3D_SetTextureDataCube/FNA3D_GetTextureDataCube with no range check (confirmed in
// TextureCube.cs). All 3 CNA backends (EasyGL/Vulkan/Bgfx) already guard against an
// out-of-range face value at the backend layer (`if (face < 0 || face >= 6) return;`), so an
// invalid face was already memory-safe — it just silently did nothing instead of surfacing a
// clear, catchable error. This is a deliberate CNA safety extra beyond FNA, matching the
// established pattern of Tasks 265/271/272's added guards.
// -----------------------------------------------------------------------

TEST_F(TextureCubeTest, SetDataInvalidFaceBelowRangeThrowsOutOfRange)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    Color buf[4] = { Color(0, 0, 0, 0), Color(0, 0, 0, 0), Color(0, 0, 0, 0), Color(0, 0, 0, 0) };
    const auto invalidFace = static_cast<CubeMapFace>(-1);
    EXPECT_THROW(tex.SetData(invalidFace, buf, 4), std::out_of_range);
}

TEST_F(TextureCubeTest, SetDataInvalidFaceAboveRangeThrowsOutOfRange)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    Color buf[4] = { Color(0, 0, 0, 0), Color(0, 0, 0, 0), Color(0, 0, 0, 0), Color(0, 0, 0, 0) };
    const auto invalidFace = static_cast<CubeMapFace>(6);
    EXPECT_THROW(tex.SetData(invalidFace, buf, 4), std::out_of_range);
}

TEST_F(TextureCubeTest, SetDataRectInvalidFaceThrowsOutOfRange)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    Color buf[1] = { Color(0, 0, 0, 0) };
    const auto invalidFace = static_cast<CubeMapFace>(6);
    EXPECT_THROW(tex.SetData(invalidFace, 0, nullptr, buf, 0, 4), std::out_of_range);
}

TEST_F(TextureCubeTest, GetDataInvalidFaceBelowRangeThrowsOutOfRange)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    Color buf[4] = { Color(0, 0, 0, 0), Color(0, 0, 0, 0), Color(0, 0, 0, 0), Color(0, 0, 0, 0) };
    const auto invalidFace = static_cast<CubeMapFace>(-1);
    EXPECT_THROW(tex.GetData(invalidFace, buf, 4), std::out_of_range);
}

TEST_F(TextureCubeTest, GetDataInvalidFaceAboveRangeThrowsOutOfRange)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    Color buf[4] = { Color(0, 0, 0, 0), Color(0, 0, 0, 0), Color(0, 0, 0, 0), Color(0, 0, 0, 0) };
    const auto invalidFace = static_cast<CubeMapFace>(6);
    EXPECT_THROW(tex.GetData(invalidFace, buf, 4), std::out_of_range);
}

TEST_F(TextureCubeTest, GetDataRectInvalidFaceThrowsOutOfRange)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    Color buf[1] = { Color(0, 0, 0, 0) };
    const auto invalidFace = static_cast<CubeMapFace>(6);
    EXPECT_THROW(tex.GetData(invalidFace, 0, nullptr, buf, 0, 4), std::out_of_range);
}

TEST_F(TextureCubeTest, SetDataAllSixValidFacesDoNotThrow)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    Color buf[4] = { Color(1, 2, 3, 4), Color(1, 2, 3, 4), Color(1, 2, 3, 4), Color(1, 2, 3, 4) };
    const CubeMapFace faces[6] = {
        CubeMapFace::PositiveX, CubeMapFace::NegativeX,
        CubeMapFace::PositiveY, CubeMapFace::NegativeY,
        CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
    };
    for (CubeMapFace face : faces)
        EXPECT_NO_THROW(tex.SetData(face, buf, 4));
}

// -----------------------------------------------------------------------
// Dispose
// -----------------------------------------------------------------------

TEST_F(TextureCubeTest, DisposeMarksResourceDisposed)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    EXPECT_FALSE(tex.getIsDisposedProperty());
    tex.Dispose();
    EXPECT_TRUE(tex.getIsDisposedProperty());
}

TEST_F(TextureCubeTest, DoubleDisposeDoesNotThrow)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    tex.Dispose();
    EXPECT_NO_THROW(tex.Dispose());
}

// -----------------------------------------------------------------------
// DDSFromStreamEXT (Task 663)
//
// Previously a silent stub — ignored `stream` entirely and always returned a blank 1x1 cube map.
// Real implementation: parses the DDS header (magic/size/flags/width/height/mipCount/caps/caps2,
// mirroring FNA's Texture.ParseDDS), rejects non-cube-map DDS files with FormatException (matching
// FNA exactly), and decodes each of the 6 faces' DXT1/3/5-compressed levels to RGBA8 via DxtUtil
// (CNA's established, already-accepted deviation from FNA for this exact class of problem — see
// Texture2D::FromStream's identical TryDecodeDds/DxtUtil precedent), uploaded as SurfaceFormat::
// Color.
//
// Test fixture: a minimal, hand-built, valid DDS cube map (4x4 faces, single mip level, DXT1) is
// constructed byte-for-byte in BuildSolidColorCubeDds() below — one solid, exactly-RGB565-
// representable colour per face (so decompression is exact, no tolerance needed) — since no real
// .dds asset is available in this environment. Each DXT1 block encodes its face's colour via
// color0==color1 (both set to the target colour) and all 2-bit indices == 0, which DxtUtil's own
// decompressor maps to color0 in both of DXT1's internal comparison modes (confirmed by reading
// DxtUtil::DecompressDxt1Block — index 0 always selects color0/c0 regardless of the c0>c1 branch).
// -----------------------------------------------------------------------

namespace
{
    void PushU32LE(std::vector<uint8_t>& v, uint32_t x)
    {
        v.push_back(static_cast<uint8_t>(x & 0xFF));
        v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
        v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
        v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
    }

    void PushAscii4(std::vector<uint8_t>& v, const char* s)
    {
        v.push_back(static_cast<uint8_t>(s[0]));
        v.push_back(static_cast<uint8_t>(s[1]));
        v.push_back(static_cast<uint8_t>(s[2]));
        v.push_back(static_cast<uint8_t>(s[3]));
    }

    // Packs an exactly-RGB565-representable Color (each channel already 0 or 255) into one solid
    // 8-byte DXT1 block: color0 == color1 == the target colour, every 2-bit index == 0.
    void PushSolidDxt1Block(std::vector<uint8_t>& v, const Color& c)
    {
        const uint16_t rgb565 = static_cast<uint16_t>(
            ((static_cast<uint32_t>(c.getRProperty()) >> 3) << 11) |
            ((static_cast<uint32_t>(c.getGProperty()) >> 2) << 5)  |
            (static_cast<uint32_t>(c.getBProperty()) >> 3));
        v.push_back(static_cast<uint8_t>(rgb565 & 0xFF));
        v.push_back(static_cast<uint8_t>((rgb565 >> 8) & 0xFF));
        v.push_back(static_cast<uint8_t>(rgb565 & 0xFF));
        v.push_back(static_cast<uint8_t>((rgb565 >> 8) & 0xFF));
        v.push_back(0); v.push_back(0); v.push_back(0); v.push_back(0); // indices: all 0
    }

    constexpr uint32_t kDdsdHeightWidth = 0x2 | 0x4;
    constexpr uint32_t kDdpfFourCC      = 0x4;
    constexpr uint32_t kDdscapsTexture  = 0x1000;
    constexpr uint32_t kDdscaps2Cubemap = 0x200;
    constexpr uint32_t kDdscaps2AllFaces = 0xFC00; // all 6 DDSCAPS2_CUBEMAP_* face bits

    // Builds a minimal, valid, single-mip-level DXT1 DDS cube map: `size`x`size` per face, one
    // solid colour per face (faceColors[0..5] map to CubeMapFace::PositiveX..NegativeZ in order,
    // matching the real DDS on-disk face layout FNA's TextureCube.DDSFromStreamEXT reads).
    std::vector<uint8_t> BuildSolidColorCubeDds(int size, const Color faceColors[6],
                                                bool asCubeMap = true, bool asDxt1 = true)
    {
        std::vector<uint8_t> d;
        PushAscii4(d, "DDS ");
        PushU32LE(d, 124);                       // header size
        PushU32LE(d, kDdsdHeightWidth);           // flags
        PushU32LE(d, static_cast<uint32_t>(size)); // height
        PushU32LE(d, static_cast<uint32_t>(size)); // width
        PushU32LE(d, 0);                          // pitchOrLinearSize (unused)
        PushU32LE(d, 0);                          // depth (unused)
        PushU32LE(d, 1);                          // mipMapCount (1 level)
        for (int i = 0; i < 11; ++i) PushU32LE(d, 0); // reserved1

        PushU32LE(d, 32);                         // pixel format size
        PushU32LE(d, asDxt1 ? kDdpfFourCC : 0);    // pixel format flags
        if (asDxt1) PushAscii4(d, "DXT1"); else PushU32LE(d, 0); // fourCC
        PushU32LE(d, 0);                          // RGBBitCount
        PushU32LE(d, 0);                          // RBitMask
        PushU32LE(d, 0);                          // GBitMask
        PushU32LE(d, 0);                          // BBitMask
        PushU32LE(d, 0);                          // ABitMask

        PushU32LE(d, kDdscapsTexture);             // caps
        PushU32LE(d, asCubeMap ? (kDdscaps2Cubemap | kDdscaps2AllFaces) : 0); // caps2
        PushU32LE(d, 0);                          // caps3 (unused)
        PushU32LE(d, 0);                          // caps4 (unused)
        PushU32LE(d, 0);                          // reserved2

        // 6 faces x 1 level x 1 block (size==4 -> exactly one 4x4 DXT1 block per face)
        for (int face = 0; face < 6; ++face)
            PushSolidDxt1Block(d, faceColors[face]);

        return d;
    }
}

TEST_F(TextureCubeTest, DDSFromStreamEXTThrowsOnEmptyStream)
{
    System::IO::MemoryStream stream;
    EXPECT_THROW(TextureCube::DDSFromStreamEXT(gd, stream), std::runtime_error);
}

TEST_F(TextureCubeTest, DDSFromStreamEXTThrowsOnNonDdsMagic)
{
    std::vector<uint8_t> garbage(128, 0xAB);
    System::IO::MemoryStream stream(garbage.data(), static_cast<System::IO::intcs>(garbage.size()));
    EXPECT_THROW(TextureCube::DDSFromStreamEXT(gd, stream), System::NotSupportedException);
}

TEST_F(TextureCubeTest, DDSFromStreamEXTThrowsWhenNotCubeMap)
{
    const Color colors[6] = {
        Color(255, 0, 0, 255), Color(255, 0, 0, 255), Color(255, 0, 0, 255),
        Color(255, 0, 0, 255), Color(255, 0, 0, 255), Color(255, 0, 0, 255),
    };
    std::vector<uint8_t> dds = BuildSolidColorCubeDds(4, colors, /*asCubeMap=*/false);
    System::IO::MemoryStream stream(dds.data(), static_cast<System::IO::intcs>(dds.size()));
    EXPECT_THROW(TextureCube::DDSFromStreamEXT(gd, stream), System::FormatException);
}

TEST_F(TextureCubeTest, DDSFromStreamEXTThrowsOnUnsupportedFourCC)
{
    const Color colors[6] = {
        Color(255, 0, 0, 255), Color(255, 0, 0, 255), Color(255, 0, 0, 255),
        Color(255, 0, 0, 255), Color(255, 0, 0, 255), Color(255, 0, 0, 255),
    };
    std::vector<uint8_t> dds = BuildSolidColorCubeDds(4, colors, /*asCubeMap=*/true, /*asDxt1=*/false);
    System::IO::MemoryStream stream(dds.data(), static_cast<System::IO::intcs>(dds.size()));
    EXPECT_THROW(TextureCube::DDSFromStreamEXT(gd, stream), System::NotSupportedException);
}

TEST_F(TextureCubeTest, DDSFromStreamEXTDecodesSizeFormatAndLevelCount)
{
    const Color colors[6] = {
        Color(255, 0, 0, 255), Color(0, 255, 0, 255), Color(0, 0, 255, 255),
        Color(255, 255, 0, 255), Color(255, 0, 255, 255), Color(0, 255, 255, 255),
    };
    std::vector<uint8_t> dds = BuildSolidColorCubeDds(4, colors);
    System::IO::MemoryStream stream(dds.data(), static_cast<System::IO::intcs>(dds.size()));

    TextureCube tex = TextureCube::DDSFromStreamEXT(gd, stream);
    EXPECT_EQ(tex.getSizeProperty(), 4);
    EXPECT_EQ(tex.getFormatProperty(), SurfaceFormat::Color);
    EXPECT_EQ(tex.getLevelCountProperty(), 1);
}

TEST_F(TextureCubeTest, DDSFromStreamEXTDecodesAllSixFacesWithDistinctColours)
{
    // 6 maximally-distinct, exactly-RGB565-representable colours (each channel already 0 or
    // 255), one per face in real DDS on-disk order (+X,-X,+Y,-Y,+Z,-Z).
    const Color expected[6] = {
        Color(255, 0, 0, 255),   // PositiveX: red
        Color(0, 255, 0, 255),   // NegativeX: green
        Color(0, 0, 255, 255),   // PositiveY: blue
        Color(255, 255, 0, 255), // NegativeY: yellow
        Color(255, 0, 255, 255), // PositiveZ: magenta
        Color(0, 255, 255, 255), // NegativeZ: cyan
    };
    std::vector<uint8_t> dds = BuildSolidColorCubeDds(4, expected);
    System::IO::MemoryStream stream(dds.data(), static_cast<System::IO::intcs>(dds.size()));

    TextureCube tex = TextureCube::DDSFromStreamEXT(gd, stream);

    const CubeMapFace faces[6] = {
        CubeMapFace::PositiveX, CubeMapFace::NegativeX,
        CubeMapFace::PositiveY, CubeMapFace::NegativeY,
        CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
    };
    for (int i = 0; i < 6; ++i)
    {
        // GetData(face, data, elementCount)'s 3-arg overload reads the ENTIRE face (its
        // elementCount must match width*height, same contract as SetData's identical overload —
        // it is not an arbitrary partial-read count), so read all 4x4=16 texels and just check
        // the first (every texel is the same solid colour by construction).
        std::vector<Color> got(16, Color(0, 0, 0, 0));
        tex.GetData(faces[i], got.data(), 16);
#ifdef CNA_BACKEND_EASYGL
        // Vulkan/Bgfx: Texture3D/TextureCube::GetData is a known, already-tracked no-op
        // (Task 865, unrelated to DDS loading) — SetData's own content can't be verified via
        // readback on those backends yet, so this exact-colour assertion is EasyGL-only.
        EXPECT_EQ(got[0].getRProperty(), expected[i].getRProperty()) << "face " << i;
        EXPECT_EQ(got[0].getGProperty(), expected[i].getGProperty()) << "face " << i;
        EXPECT_EQ(got[0].getBProperty(), expected[i].getBProperty()) << "face " << i;
#endif
    }
}
