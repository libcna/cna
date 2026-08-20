// SPDX-License-Identifier: MS-PL
// plans/plan_wicked.md WICKED-80: Texture3D staged transfers corrupted dimension-dependent tail
// rows/slices. At the pinned Wicked revision, GraphicsDevice_Vulkan::CreateTexture allocated
// UPLOAD/READBACK staging buffers at the TIGHT texel size while the mapped layout it hands out
// and CopyTexture's buffer addressing consume row pitches aligned to
// optimalBufferCopyRowPitchAlignment (128 on the measured Mesa devices) -- so for any narrow
// subresource the copies addressed past the end of the buffer
// (VUID-vkCmdCopyBufferToImage-pRegions-00171 / VUID-vkCmdCopyImageToBuffer-pRegions-00183) and
// whether a given shape corrupted was decided by suballocation adjacency, not by its dimensions.
// Fixed by cmake/patches/wicked-staging-footprint.patch, which sizes those buffers with exactly
// the footprint CreateTextureSubresourceDatas lays out.
//
// The matrix below is the byte-exact discriminator the corpus lacked (its 13 transfer tests never
// hit an affected footprint deterministically): every texel's Color encodes its own linear index,
// so truncated rows, padded rows, wrong slice stepping and bytes-vs-texels confusions each decode
// to a DIFFERENT wrong value, and a full-volume compare sees every final row and final slice. The
// pre-fix implementation fails this matrix (preserved probe evidence:
// cmake-build-wicked/wicked-repro/probe_run{1,2}.log and raw_run1.log, 2026-08-06 -- e.g. 5x5x3
// corrupting from texel 50 with recycled pattern texel 0, and the raw control corrupting 5x4x7 at
// slice 6 from pattern texel 5); the fixed implementation passes it, and the WICKED-79 narrow
// Texture2D behaviour plus TextureCube face transfers are pinned by the same pattern here so this
// suite also carries their regression.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::Texture3D;
using Microsoft::Xna::Framework::Graphics::TextureCube;

namespace
{
    // Every texel's value identifies its linear index (R|G<<8|B<<16) with a checksum in A, so a
    // wrong byte quadruple decodes back to WHICH texel the stray value belongs to. `salt` keeps
    // two patterns in one test distinguishable from each other.
    std::uint8_t ChecksumFor(std::uint32_t index, std::uint8_t salt)
    {
        return static_cast<std::uint8_t>((index * 7u + 13u + salt) & 0xFFu);
    }

    Color EncodeIndex(std::uint32_t index, std::uint8_t salt = 0)
    {
        return Color(static_cast<int>(index & 0xFFu),
                     static_cast<int>((index >> 8) & 0xFFu),
                     static_cast<int>((index >> 16) & 0xFFu),
                     static_cast<int>(ChecksumFor(index, salt)));
    }

    std::vector<Color> MakePattern(std::uint32_t texels, std::uint8_t salt = 0)
    {
        std::vector<Color> pattern(texels, Color(0, 0, 0, 0));
        for (std::uint32_t i = 0; i < texels; ++i)
            pattern[i] = EncodeIndex(i, salt);
        return pattern;
    }

    bool SameColor(const Color& a, const Color& b)
    {
        // Channel getters, not raw object bytes: Color carries a vtable, so memcmp would compare
        // padding.
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }

    ::testing::AssertionResult VolumeMatches(const std::vector<Color>& expected,
                                             const std::vector<Color>& actual,
                                             int w, int h, int d)
    {
        if (expected.size() != actual.size())
            return ::testing::AssertionFailure()
                   << "size mismatch: " << expected.size() << " vs " << actual.size();
        for (std::uint32_t i = 0; i < expected.size(); ++i)
        {
            if (!SameColor(expected[i], actual[i]))
            {
                const int slice = static_cast<int>(i) / (w * h);
                const int row = (static_cast<int>(i) % (w * h)) / w;
                const int col = static_cast<int>(i) % w;
                return ::testing::AssertionFailure()
                       << w << "x" << h << "x" << d << ": first mismatch at texel " << i
                       << " (slice " << slice << ", row " << row << ", col " << col
                       << "): expected " << static_cast<int>(expected[i].getRProperty()) << ","
                       << static_cast<int>(expected[i].getGProperty()) << ","
                       << static_cast<int>(expected[i].getBProperty()) << ","
                       << static_cast<int>(expected[i].getAProperty()) << " actual "
                       << static_cast<int>(actual[i].getRProperty()) << ","
                       << static_cast<int>(actual[i].getGProperty()) << ","
                       << static_cast<int>(actual[i].getBProperty()) << ","
                       << static_cast<int>(actual[i].getAProperty());
            }
        }
        return ::testing::AssertionSuccess();
    }

    ::testing::AssertionResult RoundTripVolume(GraphicsDevice& gd, int w, int h, int d)
    {
        const std::uint32_t n = static_cast<std::uint32_t>(w * h * d);
        const std::vector<Color> in = MakePattern(n);
        Texture3D tex(gd, w, h, d, false, SurfaceFormat::Color);
        tex.SetData(in.data(), static_cast<int>(n));
        std::vector<Color> out(n, Color(0, 0, 0, 0));
        tex.GetData(out.data(), static_cast<int>(n));
        return VolumeMatches(in, out, w, h, d);
    }
}

// REMED-CONTENT-004 convention: construct-and-skip on renderers without real Texture3D storage.
class WickedTexture3DStagedTransferTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!gd.SupportsCapability(CNA::GraphicsCapability::Texture3D))
            GTEST_SKIP() << "Texture3D is not supported on this renderer";
    }

    GraphicsDevice gd;
};

// The shapes the WICKED-80 evidence names (ThinkPad probe layouts A/B: 5x5x3, 4x5x3, 6x5x3;
// EliteBook reproductions added 3x3x3, 5x4x7, 7x3x4, 5x1x3), plus depth-1 and single-texel
// degenerate volumes. Two heights, depth > 1, final-row and final-slice coverage all come from
// the byte-exact full-volume compare.
TEST_F(WickedTexture3DStagedTransferTest, NarrowVolumesRoundTripByteExact)
{
    EXPECT_TRUE(RoundTripVolume(gd, 5, 5, 3));
    EXPECT_TRUE(RoundTripVolume(gd, 4, 5, 3));
    EXPECT_TRUE(RoundTripVolume(gd, 6, 5, 3));
    EXPECT_TRUE(RoundTripVolume(gd, 3, 3, 3));
    EXPECT_TRUE(RoundTripVolume(gd, 5, 4, 7));
    EXPECT_TRUE(RoundTripVolume(gd, 7, 3, 4));
    EXPECT_TRUE(RoundTripVolume(gd, 2, 2, 2));
    EXPECT_TRUE(RoundTripVolume(gd, 5, 1, 3));
    EXPECT_TRUE(RoundTripVolume(gd, 5, 5, 1));
    EXPECT_TRUE(RoundTripVolume(gd, 1, 1, 1));
}

// Row-pitch-aligned widths (128- and 256-byte rows on the measured devices) always passed and
// must keep passing: the pre-fix pass/fail line separated exactly here, which is the row-pitch
// padding discrimination.
TEST_F(WickedTexture3DStagedTransferTest, AlignedWidthVolumesRoundTripByteExact)
{
    EXPECT_TRUE(RoundTripVolume(gd, 32, 5, 3));
    EXPECT_TRUE(RoundTripVolume(gd, 64, 4, 2));
}

// One texel under and one over the alignment boundary bracket it. A bytes-versus-texels confusion
// anywhere in the pitch arithmetic (x4 factor) moves the boundary and one of the three fails.
TEST_F(WickedTexture3DStagedTransferTest, AlignmentBoundaryWidthsRoundTripByteExact)
{
    EXPECT_TRUE(RoundTripVolume(gd, 31, 5, 3));
    EXPECT_TRUE(RoundTripVolume(gd, 32, 5, 3));
    EXPECT_TRUE(RoundTripVolume(gd, 33, 5, 3));
}

// The WICKED-80 signature was a corrupt STORED volume: repeated readbacks disagreeing with the
// upload (identically on the ThinkPad, drifting on the EliteBook, where each readback's own
// under-allocated staging joined in). Four consecutive readbacks must each match the input.
TEST_F(WickedTexture3DStagedTransferTest, RepeatedReadbacksMatchUpload)
{
    const std::uint32_t n = 5 * 5 * 3;
    const std::vector<Color> in = MakePattern(n);
    Texture3D tex(gd, 5, 5, 3, false, SurfaceFormat::Color);
    tex.SetData(in.data(), static_cast<int>(n));
    for (int pass = 0; pass < 4; ++pass)
    {
        std::vector<Color> out(n, Color(0, 0, 0, 0));
        tex.GetData(out.data(), static_cast<int>(n));
        EXPECT_TRUE(VolumeMatches(in, out, 5, 5, 3)) << "readback pass " << pass;
    }
}

// The load-bearing discriminator. Which shape corrupted was a suballocation-adjacency lottery,
// not a property of the shape -- a single narrow round trip on a fresh device usually survives
// (measured: every isolated single-shape probe run passed pre-fix). What failed on EVERY pre-fix
// run of the preserved probe was this exact choreography: the whole narrow shape list transferred
// in sequence on ONE device, four readbacks each, so the staging traffic itself supplies the
// adjacent allocations the out-of-bounds copies collide with (3/14 shapes corrupted per run on
// this host, different shapes per run). Kept as one test so the sequence shares a device and the
// churn is real.
TEST_F(WickedTexture3DStagedTransferTest, SequencedNarrowMatrixWithRepeatedReadbacks)
{
    struct Shape
    {
        int w, h, d;
    };
    static constexpr Shape kShapes[] = {
        {4, 5, 3}, {5, 5, 3}, {6, 5, 3}, {2, 2, 2},  {3, 3, 3},  {7, 3, 4},  {5, 4, 7},
        {5, 1, 3}, {5, 5, 1}, {1, 1, 1}, {32, 5, 3}, {64, 4, 2}, {31, 5, 3}, {33, 5, 3},
    };
    for (const Shape& s : kShapes)
    {
        const std::uint32_t n = static_cast<std::uint32_t>(s.w * s.h * s.d);
        const std::vector<Color> in = MakePattern(n);
        Texture3D tex(gd, s.w, s.h, s.d, false, SurfaceFormat::Color);
        tex.SetData(in.data(), static_cast<int>(n));
        for (int pass = 0; pass < 4; ++pass)
        {
            std::vector<Color> out(n, Color(0, 0, 0, 0));
            tex.GetData(out.data(), static_cast<int>(n));
            EXPECT_TRUE(VolumeMatches(in, out, s.w, s.h, s.d))
                << s.w << "x" << s.h << "x" << s.d << " readback pass " << pass;
        }
    }
}

// Sub-box upload: a salted second pattern lands inside a patterned 16x8x4 volume at
// (3,2,1)..(8,5,3). Every inside texel must decode to the sub-pattern at the right sub-index and
// every outside texel must stay the original -- which discriminates wrong x/y/z offsets, wrong
// row stepping and smear into neighbouring rows/slices in one assertion.
TEST_F(WickedTexture3DStagedTransferTest, SubBoxUploadLandsExactlyInsideTheBox)
{
    const int w = 16, h = 8, d = 4;
    const std::uint32_t n = static_cast<std::uint32_t>(w * h * d);
    const std::vector<Color> base = MakePattern(n);
    Texture3D tex(gd, w, h, d, false, SurfaceFormat::Color);
    tex.SetData(base.data(), static_cast<int>(n));

    const int left = 3, top = 2, front = 1;
    const int bw = 5, bh = 3, bd = 2;
    const std::vector<Color> box = MakePattern(static_cast<std::uint32_t>(bw * bh * bd), 0x40);
    tex.SetData(0, left, top, left + bw, top + bh, front, front + bd,
                box.data(), 0, bw * bh * bd);

    std::vector<Color> out(n, Color(0, 0, 0, 0));
    tex.GetData(out.data(), static_cast<int>(n));

    for (int z = 0; z < d; ++z)
    {
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                const std::uint32_t i = static_cast<std::uint32_t>((z * h + y) * w + x);
                const bool inside = x >= left && x < left + bw && y >= top && y < top + bh &&
                                    z >= front && z < front + bd;
                const Color& expected =
                    inside ? box[static_cast<std::uint32_t>(
                                 ((z - front) * bh + (y - top)) * bw + (x - left))]
                           : base[i];
                ASSERT_TRUE(SameColor(expected, out[i]))
                    << "texel (" << x << "," << y << "," << z << ") "
                    << (inside ? "inside" : "outside") << " the box is wrong";
            }
        }
    }
}

// Sub-box readback of the same geometry: the narrow readback staging is the mirrored half of the
// shared arithmetic (ReadbackTextureRegion), equally implicated by the WICKED-80 evidence.
TEST_F(WickedTexture3DStagedTransferTest, SubBoxReadbackSelectsExactlyTheBox)
{
    const int w = 16, h = 8, d = 4;
    const std::uint32_t n = static_cast<std::uint32_t>(w * h * d);
    const std::vector<Color> base = MakePattern(n);
    Texture3D tex(gd, w, h, d, false, SurfaceFormat::Color);
    tex.SetData(base.data(), static_cast<int>(n));

    const int left = 3, top = 2, front = 1;
    const int bw = 5, bh = 3, bd = 2;
    std::vector<Color> out(static_cast<std::uint32_t>(bw * bh * bd), Color(0, 0, 0, 0));
    tex.GetData(0, left, top, left + bw, top + bh, front, front + bd,
                out.data(), 0, bw * bh * bd);

    for (int z = 0; z < bd; ++z)
    {
        for (int y = 0; y < bh; ++y)
        {
            for (int x = 0; x < bw; ++x)
            {
                const Color& expected =
                    base[static_cast<std::uint32_t>(((front + z) * h + (top + y)) * w +
                                                    (left + x))];
                ASSERT_TRUE(SameColor(expected,
                                      out[static_cast<std::uint32_t>((z * bh + y) * bw + x)]))
                    << "box texel (" << x << "," << y << "," << z << ") is wrong";
            }
        }
    }
}

// WICKED-79 control: narrow Texture2D transfers ride the same UploadTextureRegion /
// ReadbackTextureRegion staging arithmetic and the same upstream allocation site, and their
// pre-WICKED-79 smear is pinned green here at the same widths the WICKED-80 matrix uses.
TEST_F(WickedTexture3DStagedTransferTest, NarrowTexture2DRoundTripsStayByteExact)
{
    for (const auto [w, h] : {std::pair{5, 5}, {3, 3}, {33, 5}, {1, 1}})
    {
        const std::uint32_t n = static_cast<std::uint32_t>(w * h);
        const std::vector<Color> in = MakePattern(n);
        Texture2D tex(gd, w, h, false, SurfaceFormat::Color);
        tex.SetData(in.data(), static_cast<int>(n));
        std::vector<Color> out(n, Color(0, 0, 0, 0));
        tex.GetData(out.data(), static_cast<int>(n));
        EXPECT_TRUE(VolumeMatches(in, out, w, h, 1)) << w << "x" << h << " Texture2D";
    }
}

// TextureCube control: cube faces are array slices of the same staged transfer helpers. Two faces
// carry differently salted patterns so a face-index mix-up cannot cancel out.
TEST_F(WickedTexture3DStagedTransferTest, NarrowTextureCubeFaceRoundTripsStayByteExact)
{
    const int size = 5;
    const std::uint32_t n = static_cast<std::uint32_t>(size * size);
    const std::vector<Color> posX = MakePattern(n, 0x10);
    const std::vector<Color> negZ = MakePattern(n, 0x20);

    TextureCube tex(gd, size, false, SurfaceFormat::Color);
    tex.SetData(CubeMapFace::PositiveX, posX.data(), static_cast<int>(n));
    tex.SetData(CubeMapFace::NegativeZ, negZ.data(), static_cast<int>(n));

    std::vector<Color> out(n, Color(0, 0, 0, 0));
    tex.GetData(CubeMapFace::PositiveX, out.data(), static_cast<int>(n));
    EXPECT_TRUE(VolumeMatches(posX, out, size, size, 1)) << "PositiveX";
    tex.GetData(CubeMapFace::NegativeZ, out.data(), static_cast<int>(n));
    EXPECT_TRUE(VolumeMatches(negZ, out, size, size, 1)) << "NegativeZ";
}

// Small mip levels of an ordinary texture are narrow subresources too (a 4x4 or 1x1 level's rows
// are far below the alignment), which the ownership audit flagged as the same staging class.
TEST_F(WickedTexture3DStagedTransferTest, SmallMipLevelRegionRoundTripsStayByteExact)
{
    Texture2D tex(gd, 16, 16, true, SurfaceFormat::Color);

    const std::vector<Color> level2 = MakePattern(4 * 4, 0x30);
    Rectangle rect2(0, 0, 4, 4);
    tex.SetData(2, &rect2, level2.data(), 0, 4 * 4);
    std::vector<Color> out2(4 * 4, Color(0, 0, 0, 0));
    tex.GetData(2, &rect2, out2.data(), 0, 4 * 4);
    EXPECT_TRUE(VolumeMatches(level2, out2, 4, 4, 1)) << "level 2";

    const std::vector<Color> level4 = MakePattern(1, 0x50);
    Rectangle rect4(0, 0, 1, 1);
    tex.SetData(4, &rect4, level4.data(), 0, 1);
    std::vector<Color> out4(1, Color(0, 0, 0, 0));
    tex.GetData(4, &rect4, out4.data(), 0, 1);
    EXPECT_TRUE(VolumeMatches(level4, out4, 1, 1, 1)) << "level 4";
}

// The device must stay usable after the whole matrix: one more full narrow round trip on a fresh
// resource is the cheapest visible proof that no transfer left the device wedged.
TEST_F(WickedTexture3DStagedTransferTest, DeviceRemainsUsableAfterTheMatrix)
{
    EXPECT_TRUE(RoundTripVolume(gd, 5, 5, 3));
}
