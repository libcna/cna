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
// lives in the EasyGL pixel-readback integration test: modules/renderers/easygl/examples/easygl_texturecube_faces_test.cpp.

#include <gtest/gtest.h>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "System/FormatException.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture;
using Microsoft::Xna::Framework::Graphics::TextureCube;
using Microsoft::Xna::Framework::Graphics::TextureCollection;

// -----------------------------------------------------------------------
// REMED-GFX-130: does THIS build's renderer actually read a cube face back?
//
// TextureCube::GetData now has exactly two outcomes -- the resource's real content, or a
// deterministic System::NotSupportedException with the caller's destination untouched. Before that
// finding the shared layer converted its own zero-initialized scratch buffer for the caller
// regardless, so "GetData did not throw" and "GetData produced transparent black" were satisfied by
// a renderer that had read nothing at all. The tests below therefore assert the real outcome for
// this renderer rather than merely that the call returned.
//
// Native 2D, Canvas, DIRECTX3 and Blend2D keep IGraphicsRenderer::CreateTextureCube's
// nullptr default (no cube resource exists at all); Headless stores no pixel data by design.
// Every other renderer reads level 0 back exactly -- Software only at level 0, since it stores no
// cube mip levels.
// -----------------------------------------------------------------------
//
// REMED-GFX-135 adds the write-side half of the same question. `TextureCube::SetData` now has
// exactly two outcomes too -- the complete requested region is stored, or it throws -- so the
// "does not throw" tests below became false-positive-capable in the opposite direction: on a
// renderer with no cube storage they used to pass for a call that discarded the data. Storage
// support and readback support are the same set (a renderer either owns cube pixels or it does
// not), so one constant drives both.
//
// plan_sokol.md SOKOL-27: SokolTextureCubeRenderer stores every declared mip level's six faces in a
// real CPU shadow (SetData/GetData round-trip exactly, at every level -- not level-0-only like
// Software), even though nothing on this renderer samples a cube texture as a GPU resource yet.
// OPENVG keeps IGraphicsRenderer::CreateTextureCube's nullptr default under the default Throw
// policy (OpenVgRenderer::CreateTextureCube only ever returns a real object under the opt-in
// WarnAndStub policy, and even then a discard-everything null object, never real storage) --
// ShivaVG/OpenVG has no cube-map concept at all. Same "no cube resource exists" shape as
// Native-2D/Canvas/HTML_DOM/FreeDirect/GDI below.
//
// PORTABLEGL and TINYGL keep the same nullptr CreateTextureCube default for their own reasons
// (docs/portablegl-renderer.md, docs/tinygl-renderer.md): neither CPU OpenGL implementation has a
// cube-map resource at all. PORTABLEGL was previously written after this comment block with its
// `|| \` continuation inside a `//` line, which spliced the term into the comment and left it
// inert -- every term now sits in the directive itself so the guard means what it reads.
#if defined(CNA_RENDERER_SDL_RENDERER) || \
    defined(CNA_RENDERER_CANVAS) || defined(CNA_RENDERER_HTML_DOM) || \
    defined(CNA_RENDERER_FREEDIRECT) || defined(CNA_RENDERER_HEADLESS) || defined(CNA_RENDERER_GDI) || \
    defined(CNA_RENDERER_BLEND2D) || \
    defined(CNA_RENDERER_OPENVG) || \
    defined(CNA_RENDERER_PORTABLEGL) || defined(CNA_RENDERER_TINYGL)
constexpr bool kCubeLevel0ReadbackSupported = false;
constexpr bool kCubeStorageSupported        = false;
#else
constexpr bool kCubeLevel0ReadbackSupported = true;
constexpr bool kCubeStorageSupported        = true;
#endif

// Readback above level 0 is a separate question from readback at level 0, and the two constants
// above deliberately only answer the latter -- their names say so. OpenGL ES 1.1 reads a cube face
// back by attaching it to a framebuffer, and GL_OES_framebuffer_object requires an attached
// texture's level to be 0, so no mip level above 0 can be read there however much storage exists.
#if defined(CNA_RENDERER_OPENGLES1)
constexpr bool kCubeMipReadbackSupported = false;
#else
constexpr bool kCubeMipReadbackSupported = kCubeLevel0ReadbackSupported;
#endif

/// Runs one TextureCube::SetData call and asserts REMED-GFX-135's contract for this renderer:
/// it either completes, or it refuses with System::NotSupportedException. Never both, never
/// neither.
template <typename Fn>
void ExpectUploadStoredOrRefused(Fn&& upload)
{
    if (kCubeStorageSupported)
        EXPECT_NO_THROW(upload());
    else
        EXPECT_THROW(upload(), System::NotSupportedException);
}

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

// REMED-GFX-135: both overloads used to be bare EXPECT_NO_THROWs, which a renderer that dropped
// the upload passed just as easily as one that stored it. They now assert the real outcome, and
// the readback proves the second (startIndex) overload really stored ITS OWN data rather than
// leaving the first call's behind.
TEST_F(TextureCubeTest, SetDataExactElementCountStoresOrRefusesDeterministically)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    std::vector<Color> first(4, Color(1, 2, 3, 4));
    std::vector<Color> second(4, Color(9, 8, 7, 6));
    ExpectUploadStoredOrRefused([&] { tex.SetData(CubeMapFace::PositiveX, first.data(), 4); });
    ExpectUploadStoredOrRefused([&] { tex.SetData(CubeMapFace::PositiveX, second.data(), 0, 4); });

    if (kCubeLevel0ReadbackSupported)
    {
        std::vector<Color> got(4, Color(0xCD, 0xCD, 0xCD, 0xCD));
        ASSERT_NO_THROW(tex.GetData(CubeMapFace::PositiveX, got.data(), 4));
        for (const Color& c : got)
            EXPECT_EQ(c.getPackedValueProperty(), second[0].getPackedValueProperty());
    }
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

// REMED-GFX-135: was a bare EXPECT_NO_THROW. It now asserts the outcome, and -- where the renderer
// can read back -- that the 1x1 write landed on exactly one texel and left the other three alone.
TEST_F(TextureCubeTest, SetDataRectWithinBoundsStoresOnlyThatTexel)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    std::vector<Color> whole(4, Color(40, 41, 42, 43));
    ExpectUploadStoredOrRefused([&] { tex.SetData(CubeMapFace::PositiveX, whole.data(), 4); });

    Color buf[1] = { Color(1, 2, 3, 4) };
    const Rectangle rect(0, 0, 1, 1);
    ExpectUploadStoredOrRefused([&] { tex.SetData(CubeMapFace::PositiveX, 0, &rect, buf, 0, 1); });

    if (kCubeLevel0ReadbackSupported)
    {
        std::vector<Color> got(4, Color(0xCD, 0xCD, 0xCD, 0xCD));
        ASSERT_NO_THROW(tex.GetData(CubeMapFace::PositiveX, got.data(), 4));
        EXPECT_EQ(got[0].getPackedValueProperty(), buf[0].getPackedValueProperty());
        for (std::size_t i = 1; i < got.size(); ++i)
            EXPECT_EQ(got[i].getPackedValueProperty(), whole[0].getPackedValueProperty());
    }
}

// Task 913: elementCount must cover the full requested region (w*h) — previously unvalidated,
// so a too-small elementCount caused the renderer to write/read past the caller-supplied buffer
// (confirmed via a live heap-corruption crash while building Task 663's DDS test fixture).
TEST_F(TextureCubeTest, SetDataRectElementCountLessThanRegionThrowsOutOfRange)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    Color buf[1] = { Color(1, 2, 3, 4) };
    const Rectangle rect(0, 0, 2, 2); // 4 texels requested, only 1 element provided
    EXPECT_THROW(tex.SetData(CubeMapFace::PositiveX, 0, &rect, buf, 0, 1), std::out_of_range);
}

// -----------------------------------------------------------------------
// Mip-level dimension bug (Task 272): rect=nullptr at level>0 must use the
// mip-reduced face size (Size>>level), not the full face Size.
// -----------------------------------------------------------------------

TEST_F(TextureCubeTest, SetDataNullRectAtMipLevelUsesReducedSize)
{
    // size=4, mipMap=true -> levels 4x4, 2x2, 1x1. Level 1 is 2x2 = 4 elements.
    // REMED-GFX-135: every renderer that stores cube faces at all now stores the whole declared mip
    // chain (Software was the last one allocating level 0 only), so this level-1 write is either a
    // real store or a deterministic refusal -- never the silent drop it used to be.
    TextureCube tex(gd, 4, true, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(1, 2, 3, 4));
    ExpectUploadStoredOrRefused([&] { tex.SetData(CubeMapFace::PositiveX, 1, nullptr, buf.data(), 0, 4); });

    if (kCubeMipReadbackSupported)
    {
        std::vector<Color> got(4, Color(0xCD, 0xCD, 0xCD, 0xCD));
        ASSERT_NO_THROW(tex.GetData(CubeMapFace::PositiveX, 1, nullptr, got.data(), 0, 4));
        for (const Color& c : got)
            EXPECT_EQ(c.getPackedValueProperty(), buf[0].getPackedValueProperty());
    }
    else if (kCubeLevel0ReadbackSupported)
    {
        // The store above is still asserted; only reading a mip level back is unavailable, and it
        // must refuse rather than hand back undefined pixels.
        std::vector<Color> got(4, Color(0xCD, 0xCD, 0xCD, 0xCD));
        EXPECT_THROW(tex.GetData(CubeMapFace::PositiveX, 1, nullptr, got.data(), 0, 4),
                     System::NotSupportedException);
    }
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

// REMED-GFX-130 false-positive audit: this test used to be a bare EXPECT_NO_THROW, which asserted
// nothing about what GetData produced -- a renderer that read nothing and a renderer that read the
// texel correctly both passed it. It now asserts the real outcome: the uploaded texel on a renderer
// with cube readback, and a deterministic rejection that leaves the destination untouched on one
// without.
TEST_F(TextureCubeTest, GetDataRectWithinBoundsReturnsUploadedTexelOrRejectsDeterministically)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    const Color uploaded[4] = {
        Color(17, 200, 33, 199), Color(18, 201, 34, 198),
        Color(19, 202, 35, 197), Color(20, 203, 36, 196),
    };
    ExpectUploadStoredOrRefused([&] { tex.SetData(CubeMapFace::PositiveX, uploaded, 4); });

    const Color sentinel(0xCD, 0xCD, 0xCD, 0xCD);
    Color buf[1] = { sentinel };
    const Rectangle rect(0, 0, 1, 1);
    if (kCubeLevel0ReadbackSupported)
    {
        ASSERT_NO_THROW(tex.GetData(CubeMapFace::PositiveX, 0, &rect, buf, 0, 1));
        EXPECT_EQ(buf[0].getPackedValueProperty(), uploaded[0].getPackedValueProperty());
    }
    else
    {
        EXPECT_THROW(tex.GetData(CubeMapFace::PositiveX, 0, &rect, buf, 0, 1),
                     System::NotSupportedException);
        EXPECT_EQ(buf[0].getPackedValueProperty(), sentinel.getPackedValueProperty());
    }
}

// Task 913: see the identical SetData test above for the full rationale.
TEST_F(TextureCubeTest, GetDataRectElementCountLessThanRegionThrowsOutOfRange)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    Color buf[1] = { Color(0, 0, 0, 0) };
    const Rectangle rect(0, 0, 2, 2); // 4 texels requested, only 1 element provided
    EXPECT_THROW(tex.GetData(CubeMapFace::PositiveX, 0, &rect, buf, 0, 1), std::out_of_range);
}

// -----------------------------------------------------------------------
// Invalid CubeMapFace values (Task 279)
//
// FNA itself never validates cubeMapFace — it's passed straight through to
// FNA3D_SetTextureDataCube/FNA3D_GetTextureDataCube with no range check (confirmed in
// TextureCube.cs). All 3 CNA renderers (EasyGL/Vulkan/Bgfx) already guard against an
// out-of-range face value at the renderer layer (`if (face < 0 || face >= 6) return;`), so an
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

// REMED-GFX-135: uploading the SAME colour to all six faces could not distinguish them, so this
// used to pass on a renderer that stored one face, all six, or none. Each face now gets its own
// colour and is read back independently.
TEST_F(TextureCubeTest, SetDataAllSixValidFacesStoreIndependently)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    const CubeMapFace faces[6] = {
        CubeMapFace::PositiveX, CubeMapFace::NegativeX,
        CubeMapFace::PositiveY, CubeMapFace::NegativeY,
        CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
    };
    for (int f = 0; f < 6; ++f)
    {
        std::vector<Color> buf(4, Color(static_cast<std::uint8_t>(10 + f * 30),
                                        static_cast<std::uint8_t>(200 - f * 20),
                                        static_cast<std::uint8_t>(5 + f * 40),
                                        static_cast<std::uint8_t>(101 + f * 7)));
        ExpectUploadStoredOrRefused([&] { tex.SetData(faces[f], buf.data(), 4); });
    }

    if (kCubeLevel0ReadbackSupported)
    {
        for (int f = 0; f < 6; ++f)
        {
            const Color expected(static_cast<std::uint8_t>(10 + f * 30),
                                 static_cast<std::uint8_t>(200 - f * 20),
                                 static_cast<std::uint8_t>(5 + f * 40),
                                 static_cast<std::uint8_t>(101 + f * 7));
            std::vector<Color> got(4, Color(0xCD, 0xCD, 0xCD, 0xCD));
            ASSERT_NO_THROW(tex.GetData(faces[f], got.data(), 4));
            for (const Color& c : got)
                EXPECT_EQ(c.getPackedValueProperty(), expected.getPackedValueProperty())
                    << "face " << f;
        }
    }
}

// REMED-GFX-135: a source array with padding on both sides of the uploaded window. Whatever the
// renderer stores, the padding must never reach the resource -- that is what proves startIndex is
// applied exactly once and that the call reads only elementCount elements from it.
TEST_F(TextureCubeTest, SetDataNonZeroStartIndexUploadsOnlyItsOwnWindow)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    const Color poison(0x7E, 0x11, 0x33, 0x5C);
    std::vector<Color> src(11, poison);
    for (int i = 0; i < 4; ++i)
        src[static_cast<std::size_t>(3 + i)] =
            Color(static_cast<std::uint8_t>(60 + i * 9), static_cast<std::uint8_t>(140),
                  static_cast<std::uint8_t>(25), static_cast<std::uint8_t>(180 - i));
    ExpectUploadStoredOrRefused([&] { tex.SetData(CubeMapFace::NegativeY, src.data(), 3, 4); });

    if (kCubeLevel0ReadbackSupported)
    {
        std::vector<Color> got(4, Color(0xCD, 0xCD, 0xCD, 0xCD));
        ASSERT_NO_THROW(tex.GetData(CubeMapFace::NegativeY, got.data(), 4));
        for (int i = 0; i < 4; ++i)
        {
            EXPECT_EQ(got[static_cast<std::size_t>(i)].getPackedValueProperty(),
                      src[static_cast<std::size_t>(3 + i)].getPackedValueProperty());
            EXPECT_NE(got[static_cast<std::size_t>(i)].getPackedValueProperty(),
                      poison.getPackedValueProperty());
        }
    }
}

// REMED-GFX-135: a mipmapped cube declares LevelCount mip levels; every one of them must have real
// storage behind it. Software was the last renderer allocating level 0 only, so this walked the
// whole declared chain and passed on a resource that kept just the first level.
TEST_F(TextureCubeTest, EveryDeclaredMipLevelStoresItsOwnContent)
{
    TextureCube tex(gd, 8, true, SurfaceFormat::Color);
    const int levels = tex.getLevelCountProperty();
    ASSERT_EQ(levels, 4);   // 8 -> 4 -> 2 -> 1

    for (int level = 0; level < levels; ++level)
    {
        const int dim = 8 >> level;
        std::vector<Color> src(static_cast<std::size_t>(dim) * dim,
                               Color(static_cast<std::uint8_t>(30 + level * 50),
                                     static_cast<std::uint8_t>(200 - level * 40),
                                     static_cast<std::uint8_t>(7 + level * 25),
                                     static_cast<std::uint8_t>(90 + level * 30)));
        ExpectUploadStoredOrRefused(
            [&] { tex.SetData(CubeMapFace::PositiveZ, level, nullptr, src.data(), 0,
                              static_cast<int>(src.size())); });
    }

    if (!kCubeLevel0ReadbackSupported) return;

    // Read back in reverse, so a renderer that kept only the LAST write cannot pass either.
    for (int level = levels - 1; level >= 0; --level)
    {
        // A renderer that stores the whole chain but can only read the base level back still has
        // its level-0 content verified; the levels it cannot read are skipped rather than
        // asserted away.
        if (!kCubeMipReadbackSupported && level != 0) continue;
        const int dim = 8 >> level;
        const Color expected(static_cast<std::uint8_t>(30 + level * 50),
                             static_cast<std::uint8_t>(200 - level * 40),
                             static_cast<std::uint8_t>(7 + level * 25),
                             static_cast<std::uint8_t>(90 + level * 30));
        std::vector<Color> got(static_cast<std::size_t>(dim) * dim, Color(0xCD, 0xCD, 0xCD, 0xCD));
        ASSERT_NO_THROW(tex.GetData(CubeMapFace::PositiveZ, level, nullptr, got.data(), 0,
                                    static_cast<int>(got.size()))) << "level " << level;
        for (const Color& c : got)
            EXPECT_EQ(c.getPackedValueProperty(), expected.getPackedValueProperty())
                << "level " << level;
    }
}

// REMED-GFX-135: SetData after Dispose() used to be a silent no-op, while GetData already threw --
// so a caller could be told the resource was gone on read and never on write.
TEST_F(TextureCubeTest, SetDataAfterDisposeThrowsObjectDisposed)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(1, 2, 3, 4));
    tex.Dispose();
    EXPECT_THROW(tex.SetData(CubeMapFace::PositiveX, buf.data(), 4), System::ObjectDisposedException);
    EXPECT_THROW(tex.SetData(CubeMapFace::PositiveX, 0, nullptr, buf.data(), 0, 4),
                 System::ObjectDisposedException);
    tex.Dispose();   // repeated Dispose must not change the answer
    EXPECT_THROW(tex.SetData(CubeMapFace::PositiveX, buf.data(), 4), System::ObjectDisposedException);
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
// TextureCollection assignment / Texture base class (plan_graphics.md Task 863)
//
// Before Task 863, TextureCube inherited GraphicsResource directly (not Texture), so it could
// never be stored in a TextureCollection (std::vector<Texture*>) at all -- a TextureCube* could
// not be assigned into GraphicsDevice.Textures[slot], a real, previously-impossible operation
// that is the clearest possible proof this fix succeeded. Now TextureCube : Texture, matching FNA.
// -----------------------------------------------------------------------

TEST_F(TextureCubeTest, CanBeAssignedIntoTextureCollection)
{
    TextureCube tex(gd, 4, false, SurfaceFormat::Color);
    TextureCollection col;
    // Previously a compile error: TextureCube was not convertible to Texture*.
    EXPECT_NO_THROW(col(0, &tex));
    EXPECT_EQ(col[0], static_cast<Texture*>(&tex));
}

TEST_F(TextureCubeTest, CanBeAssignedIntoRealGraphicsDeviceTexturesSlot)
{
    TextureCube tex(gd, 4, false, SurfaceFormat::Color);
    // Real GraphicsDevice.Textures[slot] = textureCube, matching FNA's own TextureCube : Texture
    // capability -- structurally impossible before Task 863.
    EXPECT_NO_THROW(gd.getTexturesProperty()(0, &tex));
    EXPECT_EQ(gd.getTexturesProperty()[0], static_cast<Texture*>(&tex));
}

// Texture::Dispose(bool) removes the texture from GraphicsDevice.Textures/VertexTextures on
// disposal (matches FNA's Texture.Dispose unbind behaviour). TextureCube::Dispose(bool)
// previously only released its own renderer handle without ever calling into
// Texture::Dispose(bool), so this unbind never applied to TextureCube. Now it does, since
// TextureCube::Dispose(bool) calls Texture::Dispose(disposing) after releasing its own renderer
// handle (same order as Texture2D::Dispose(bool)).
TEST_F(TextureCubeTest, DisposeUnbindsFromGraphicsDeviceTextures)
{
    auto tex = std::make_unique<TextureCube>(gd, 4, false, SurfaceFormat::Color);
    gd.getTexturesProperty()(0, tex.get());
    ASSERT_EQ(gd.getTexturesProperty()[0], static_cast<Texture*>(tex.get()));

    tex->Dispose();

    EXPECT_EQ(gd.getTexturesProperty()[0], nullptr);
}

TEST_F(TextureCubeTest, DisposeUnbindsFromGraphicsDeviceVertexTextures)
{
    auto tex = std::make_unique<TextureCube>(gd, 4, false, SurfaceFormat::Color);
    gd.getVertexTexturesProperty()(0, tex.get());
    ASSERT_EQ(gd.getVertexTexturesProperty()[0], static_cast<Texture*>(tex.get()));

    tex->Dispose();

    EXPECT_EQ(gd.getVertexTexturesProperty()[0], nullptr);
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

    // REMED-GFX-135: DDSFromStreamEXT decodes on the CPU and then uploads every face through
    // SetData, which on a renderer with no cube storage is now a deterministic refusal instead of a
    // silent discard -- so the whole load fails rather than returning a TextureCube holding nothing.
    if (!kCubeStorageSupported)
    {
        EXPECT_THROW((void)TextureCube::DDSFromStreamEXT(gd, stream), System::NotSupportedException);
        return;
    }

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

    // REMED-GFX-135: see the sibling test above -- a renderer that cannot store a cube face now
    // fails the load outright rather than handing back an empty resource.
    if (!kCubeStorageSupported)
    {
        EXPECT_THROW((void)TextureCube::DDSFromStreamEXT(gd, stream), System::NotSupportedException);
        return;
    }

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
        // REMED-GFX-130 false-positive audit: the readback used to be issued unconditionally and
        // only asserted on three named renderers, so every other renderer ran a call whose result was
        // never checked -- including the ones that answered with a fabricated transparent-black
        // face. Both halves are now asserted: the decoded colour where the renderer really reads a
        // cube face back, and the deterministic rejection (destination untouched) where it cannot.
        const Color sentinel(0xA5, 0xA5, 0xA5, 0xA5);
        std::vector<Color> got(16, sentinel);
        if (kCubeLevel0ReadbackSupported)
        {
            ASSERT_NO_THROW(tex.GetData(faces[i], got.data(), 16));
            EXPECT_EQ(got[0].getRProperty(), expected[i].getRProperty()) << "face " << i;
            EXPECT_EQ(got[0].getGProperty(), expected[i].getGProperty()) << "face " << i;
            EXPECT_EQ(got[0].getBProperty(), expected[i].getBProperty()) << "face " << i;
        }
        else
        {
            EXPECT_THROW(tex.GetData(faces[i], got.data(), 16), System::NotSupportedException)
                << "face " << i;
            EXPECT_EQ(got[0].getPackedValueProperty(), sentinel.getPackedValueProperty())
                << "face " << i;
        }
    }
}
