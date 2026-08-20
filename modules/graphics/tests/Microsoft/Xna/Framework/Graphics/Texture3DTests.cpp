// SPDX-License-Identifier: MS-PL
// Task 271: Texture3D audit — FNA API conformance tests.
//
// Tests cover:
//   • Constructor properties (Width/Height/Depth/Format/LevelCount), including mipMap
//     level-count math (Task 271 audit finding: previously hardcoded to 1 regardless of mipMap).
//   • GetTypeName.
//   • SetData/GetData argument guards (null data, elementCount<=0, negative startIndex,
//     negative level, invalid box) for all overloads — previously missing entirely
//     (Task 271 audit finding: null data caused a crash, negative startIndex caused an
//     out-of-bounds read/write, matching the class of bug fixed for Texture2D in Tasks 265/266).
//   • SetDataPointerEXT null-data guard.
//   • Dispose marks the resource as disposed.
//
// Happy-path SetData/GetData round-trip coverage (per-slice colour verification) lives in
// the EasyGL pixel-readback integration test: modules/renderers/easygl/examples/easygl_texture3d_slices_test.cpp.
//
// plans/plan_graphics.md Task 863: Texture3D now inherits Texture (matching FNA), instead of
// GraphicsResource directly, so it can be assigned into GraphicsDevice.Textures/VertexTextures
// (a TextureCollection, which stores Texture* slots) -- previously a compile-time impossibility.
// See the "TextureCollection assignment / Texture base class (Task 863)" section below.

#include <gtest/gtest.h>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/RendererTestGate.hpp"

// Lets CNA_RENDERER_IS name identities bare, matching the compile-time guards it replaced.
using namespace CNA::Testing::Renderers;
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture;
using Microsoft::Xna::Framework::Graphics::Texture3D;
using Microsoft::Xna::Framework::Graphics::TextureCollection;

namespace
{
/// Whether this renderer can fetch a volume texture's voxels back to the CPU.
///
/// Storage and readback were the same question for every renderer until IGL, exactly as they were
/// for cube faces (see TextureCubeTests.cpp's own split). IGL owns real volume pixels --
/// `IglTexture3DRenderer::SetData` uploads into an `igl::TextureType::ThreeD` resource and
/// `Igl_ShaderEffectTexture3D` proves they sample correctly through a real custom shader -- but IGL
/// v1.1.1 cannot attach a 3D texture to a framebuffer, which is the only readback it has.
/// `opengl::TextureBufferBase::attach` falls through to `glFramebufferTexture2D` for a volume
/// (because `getNumLayers()` counts ARRAY layers, of which a volume has one) and the driver answers
/// `GL_INVALID_OPERATION ... invalid textarget GL_TEXTURE_3D`; the Vulkan copy is 2D-only in the
/// same way. So `GetData` refuses rather than fabricating voxels, and the shared layer turns that
/// into a NotSupportedException.
///
/// Verified by attempting it, not assumed -- see plans/plan_igl.md IGL-17. Without this arm an IGL build
/// asserts a readback the renderer honestly cannot perform, in four tests at once.
[[nodiscard]] bool VolumeReadbackSupported()
{
    return !CNA_RENDERER_IS(Igl);
}
}

// -----------------------------------------------------------------------
// Constructor / properties
// -----------------------------------------------------------------------

// REMED-CONTENT-004: Texture3D is a documented, renderer-dependent capability -- Headless has no
// real GPU resource of any kind, and Software's Texture3D support is an explicit v1 scope boundary
// (plans/plan_software.md Boundaries). Every test below constructs a real Texture3D, so on a renderer that
// doesn't support it the constructor now throws System::NotSupportedException before any test body
// logic runs -- skip cleanly rather than fail, matching this project's own hardware-capability-skip
// convention (e.g. Accelerometer/Gyroscope). See Texture3DUnsupportedRendererTest below for the
// positive verification that the new throw behavior actually fires on such a renderer.
class Texture3DTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!gd.SupportsCapability(CNA::GraphicsCapability::Texture3D))
        {
            GTEST_SKIP() << "Texture3D is not supported on this renderer (REMED-CONTENT-004)";
        }
    }

    GraphicsDevice gd;
};

// Deliberately NOT a Texture3DTest fixture (that fixture skips on an unsupported renderer) --
// this is the mirror-image check, verifying the new throw behavior on such a renderer.
TEST(Texture3DUnsupportedRendererTest, ConstructorThrowsNotSupportedExceptionWhenRendererLacksTexture3D)
{
    GraphicsDevice gd;
    if (gd.SupportsCapability(CNA::GraphicsCapability::Texture3D))
    {
        GTEST_SKIP() << "this renderer supports Texture3D; nothing to verify here";
    }
    EXPECT_THROW((Texture3D(gd, 2, 2, 2, false, SurfaceFormat::Color)), System::NotSupportedException);
}

TEST_F(Texture3DTest, ConstructorSetsWidth)
{
    Texture3D tex(gd, 2, 3, 4, false, SurfaceFormat::Color);
    EXPECT_EQ(tex.getWidthProperty(), 2);
}

TEST_F(Texture3DTest, ConstructorSetsHeight)
{
    Texture3D tex(gd, 2, 3, 4, false, SurfaceFormat::Color);
    EXPECT_EQ(tex.getHeightProperty(), 3);
}

TEST_F(Texture3DTest, ConstructorSetsDepth)
{
    Texture3D tex(gd, 2, 3, 4, false, SurfaceFormat::Color);
    EXPECT_EQ(tex.getDepthProperty(), 4);
}

TEST_F(Texture3DTest, ConstructorSetsFormat)
{
    Texture3D tex(gd, 2, 3, 4, false, SurfaceFormat::Color);
    EXPECT_EQ(tex.getFormatProperty(), SurfaceFormat::Color);
}

TEST_F(Texture3DTest, GetTypeNameReturnsFullyQualifiedName)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    EXPECT_EQ(tex.GetTypeName(), "Microsoft.Xna.Framework.Graphics.Texture3D");
}

// -----------------------------------------------------------------------
// LevelCount — mipmapped vs non-mipmapped construction (Task 271)
//
// Mirrors FNA's Texture3D constructor: LevelCount = mipMap ? CalculateMipLevels(width, height) : 1
// (depth does not participate in the mip-level count, matching FNA/Texture2D's formula).
// Previously hardcoded to 1 regardless of mipMap — a real bug fixed by this task.
// -----------------------------------------------------------------------

TEST_F(Texture3DTest, MipMapFalseIsAlwaysOne)
{
    EXPECT_EQ(Texture3D(gd, 8, 8, 4, false, SurfaceFormat::Color).getLevelCountProperty(), 1);
    EXPECT_EQ(Texture3D(gd, 3, 5, 2, false, SurfaceFormat::Color).getLevelCountProperty(), 1);
}

TEST_F(Texture3DTest, MipMapTrueSquarePowerOfTwo)
{
    EXPECT_EQ(Texture3D(gd, 1, 1, 1, true, SurfaceFormat::Color).getLevelCountProperty(), 1);
    EXPECT_EQ(Texture3D(gd, 4, 4, 1, true, SurfaceFormat::Color).getLevelCountProperty(), 3);
    EXPECT_EQ(Texture3D(gd, 16, 16, 4, true, SurfaceFormat::Color).getLevelCountProperty(), 5);
}

TEST_F(Texture3DTest, MipMapTrueNonPowerOfTwo)
{
    EXPECT_EQ(Texture3D(gd, 3, 5, 1, true, SurfaceFormat::Color).getLevelCountProperty(), 3);
    EXPECT_EQ(Texture3D(gd, 7, 11, 2, true, SurfaceFormat::Color).getLevelCountProperty(), 4);
}

// -----------------------------------------------------------------------
// SetData(Color*, int elementCount) / SetData(Color*, int, int) — argument guards
// (both delegate to the 10-arg overload; guards live there)
// -----------------------------------------------------------------------

TEST_F(Texture3DTest, SetDataSimpleNullDataThrowsInvalidArgument)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    EXPECT_THROW(tex.SetData(nullptr, 8), std::invalid_argument);
}

TEST_F(Texture3DTest, SetDataSimpleZeroElementCountThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    Color buf[1] = { Color(0, 0, 0, 0) };
    EXPECT_THROW(tex.SetData(buf, 0), std::out_of_range);
}

TEST_F(Texture3DTest, SetDataStartIndexNullDataThrowsInvalidArgument)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    EXPECT_THROW(tex.SetData(nullptr, 0, 8), std::invalid_argument);
}

TEST_F(Texture3DTest, SetDataStartIndexNegativeStartIndexThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(8, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.SetData(buf.data(), -1, 8), std::out_of_range);
}

// REMED-GFX-135: both overloads used to be bare EXPECT_NO_THROWs, which a renderer that dropped the
// upload passed just as easily as one that stored it. Every renderer reaching this fixture has real
// volume storage (the SetUp above skips the rest), so the readback is the oracle: it proves the
// second (startIndex) overload stored ITS OWN data rather than leaving the first call's behind.
TEST_F(Texture3DTest, SetDataExactElementCountStoresTheWholeVolume)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    std::vector<Color> first(8, Color(1, 2, 3, 4));
    std::vector<Color> second(8, Color(9, 8, 7, 6));
    EXPECT_NO_THROW(tex.SetData(first.data(), 8));
    EXPECT_NO_THROW(tex.SetData(second.data(), 0, 8));

    std::vector<Color> got(8, Color(0xCD, 0xCD, 0xCD, 0xCD));
    // A renderer that owns volume pixels but cannot fetch them back must say so rather than
    // fabricate them; the upload above is still exercised on every renderer.
    if (!VolumeReadbackSupported())
    {
        EXPECT_THROW((void)tex.GetData(got.data(), 8), System::NotSupportedException);
        return;
    }
    ASSERT_NO_THROW(tex.GetData(got.data(), 8));
    for (const Color& c : got)
        EXPECT_EQ(c.getPackedValueProperty(), second[0].getPackedValueProperty());
}

// REMED-GFX-135: a source array padded on both sides of the uploaded window -- the padding must
// never reach the resource, which is what proves startIndex is applied exactly once.
TEST_F(Texture3DTest, SetDataNonZeroStartIndexUploadsOnlyItsOwnWindow)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    const Color poison(0x7E, 0x11, 0x33, 0x5C);
    std::vector<Color> src(15, poison);
    for (int i = 0; i < 8; ++i)
        src[static_cast<std::size_t>(4 + i)] =
            Color(static_cast<std::uint8_t>(20 + i * 11), static_cast<std::uint8_t>(130),
                  static_cast<std::uint8_t>(200 - i * 9), static_cast<std::uint8_t>(150 + i));
    EXPECT_NO_THROW(tex.SetData(src.data(), 4, 8));

    std::vector<Color> got(8, Color(0xCD, 0xCD, 0xCD, 0xCD));
    // A renderer that owns volume pixels but cannot fetch them back must say so rather than
    // fabricate them; the upload above is still exercised on every renderer.
    if (!VolumeReadbackSupported())
    {
        EXPECT_THROW((void)tex.GetData(got.data(), 8), System::NotSupportedException);
        return;
    }
    ASSERT_NO_THROW(tex.GetData(got.data(), 8));
    for (int i = 0; i < 8; ++i)
    {
        EXPECT_EQ(got[static_cast<std::size_t>(i)].getPackedValueProperty(),
                  src[static_cast<std::size_t>(4 + i)].getPackedValueProperty());
        EXPECT_NE(got[static_cast<std::size_t>(i)].getPackedValueProperty(),
                  poison.getPackedValueProperty());
    }
}

// REMED-GFX-135: SetData after Dispose() used to be a silent no-op, while GetData already threw.
TEST_F(Texture3DTest, SetDataAfterDisposeThrowsObjectDisposed)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(8, Color(1, 2, 3, 4));
    tex.Dispose();
    EXPECT_THROW(tex.SetData(buf.data(), 8), System::ObjectDisposedException);
    EXPECT_THROW(tex.SetData(0, 0, 0, 2, 2, 0, 2, buf.data(), 0, 8), System::ObjectDisposedException);
    EXPECT_THROW(tex.SetDataPointerEXT(0, 0, 0, 2, 2, 0, 2, buf.data(), 32),
                 System::ObjectDisposedException);
    tex.Dispose();   // repeated Dispose must not change the answer
    EXPECT_THROW(tex.SetData(buf.data(), 8), System::ObjectDisposedException);
}

// -----------------------------------------------------------------------
// SetData(level, left, top, right, bottom, front, back, data, startIndex, elementCount)
// — argument guards (Task 271: previously none of these existed at all)
// -----------------------------------------------------------------------

TEST_F(Texture3DTest, SetDataBoxNullDataThrowsInvalidArgument)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    EXPECT_THROW(tex.SetData(0, 0, 0, 2, 2, 0, 2, nullptr, 0, 4), std::invalid_argument);
}

TEST_F(Texture3DTest, SetDataBoxZeroElementCountThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    Color buf[1] = { Color(0, 0, 0, 0) };
    EXPECT_THROW(tex.SetData(0, 0, 0, 2, 2, 0, 1, buf, 0, 0), std::out_of_range);
}

TEST_F(Texture3DTest, SetDataBoxNegativeStartIndexThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.SetData(0, 0, 0, 2, 2, 0, 1, buf.data(), -1, 4), std::out_of_range);
}

TEST_F(Texture3DTest, SetDataBoxNegativeLevelThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.SetData(-1, 0, 0, 2, 2, 0, 1, buf.data(), 0, 4), std::out_of_range);
}

TEST_F(Texture3DTest, SetDataBoxNegativeLeftThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.SetData(0, -1, 0, 2, 2, 0, 1, buf.data(), 0, 4), std::out_of_range);
}

TEST_F(Texture3DTest, SetDataBoxLeftNotLessThanRightThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.SetData(0, 2, 0, 2, 2, 0, 1, buf.data(), 0, 4), std::out_of_range);
}

TEST_F(Texture3DTest, SetDataBoxFrontNotLessThanBackThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.SetData(0, 0, 0, 2, 2, 1, 1, buf.data(), 0, 4), std::out_of_range);
}

// REMED-GFX-135: was a bare EXPECT_NO_THROW. It now proves the sub-box landed on slice 0 only and
// left slice 1 untouched -- a write that flattened the volume or duplicated the slice fails.
TEST_F(Texture3DTest, SetDataBoxWithinBoundsStoresOnlyThatSlice)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    std::vector<Color> whole(8, Color(40, 41, 42, 43));
    EXPECT_NO_THROW(tex.SetData(whole.data(), 8));

    std::vector<Color> buf(4, Color(1, 2, 3, 4));
    EXPECT_NO_THROW(tex.SetData(0, 0, 0, 2, 2, 0, 1, buf.data(), 0, 4));

    std::vector<Color> got(8, Color(0xCD, 0xCD, 0xCD, 0xCD));
    // A renderer that owns volume pixels but cannot fetch them back must say so rather than
    // fabricate them; the upload above is still exercised on every renderer.
    if (!VolumeReadbackSupported())
    {
        EXPECT_THROW((void)tex.GetData(got.data(), 8), System::NotSupportedException);
        return;
    }
    ASSERT_NO_THROW(tex.GetData(got.data(), 8));
    for (std::size_t i = 0; i < 4; ++i)
        EXPECT_EQ(got[i].getPackedValueProperty(), buf[0].getPackedValueProperty()) << "slice 0, i=" << i;
    for (std::size_t i = 4; i < 8; ++i)
        EXPECT_EQ(got[i].getPackedValueProperty(), whole[0].getPackedValueProperty()) << "slice 1, i=" << i;
}

// Task 913: elementCount must cover the full requested region (right-left)*(bottom-top)*
// (back-front) — previously unvalidated, so a too-small elementCount caused the renderer to
// write/read past the caller-supplied buffer (confirmed via a live heap-corruption crash while
// building Task 663's DDS test fixture for the analogous TextureCube gap).
TEST_F(Texture3DTest, SetDataBoxElementCountLessThanRegionThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    Color buf[1] = { Color(1, 2, 3, 4) };
    // Region is 2x2x1=4 voxels; only 1 element provided.
    EXPECT_THROW(tex.SetData(0, 0, 0, 2, 2, 0, 1, buf, 0, 1), std::out_of_range);
}

// -----------------------------------------------------------------------
// SetDataPointerEXT — null-data guard
// -----------------------------------------------------------------------

TEST_F(Texture3DTest, SetDataPointerEXTNullDataThrowsInvalidArgument)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    EXPECT_THROW(tex.SetDataPointerEXT(0, 0, 0, 2, 2, 0, 1, nullptr, 16), std::invalid_argument);
}

// -----------------------------------------------------------------------
// GetData — argument guards (mirrors SetData's guards)
// -----------------------------------------------------------------------

TEST_F(Texture3DTest, GetDataSimpleNullDataThrowsInvalidArgument)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    EXPECT_THROW(tex.GetData(nullptr, 8), std::invalid_argument);
}

TEST_F(Texture3DTest, GetDataSimpleZeroElementCountThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    Color buf[1] = { Color(0, 0, 0, 0) };
    EXPECT_THROW(tex.GetData(buf, 0), std::out_of_range);
}

TEST_F(Texture3DTest, GetDataStartIndexNegativeStartIndexThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(8, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.GetData(buf.data(), -1, 8), std::out_of_range);
}

TEST_F(Texture3DTest, GetDataBoxNullDataThrowsInvalidArgument)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    EXPECT_THROW(tex.GetData(0, 0, 0, 2, 2, 0, 2, nullptr, 0, 4), std::invalid_argument);
}

TEST_F(Texture3DTest, GetDataBoxNegativeLevelThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.GetData(-1, 0, 0, 2, 2, 0, 1, buf.data(), 0, 4), std::out_of_range);
}

TEST_F(Texture3DTest, GetDataBoxLeftNotLessThanRightThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.GetData(0, 2, 0, 2, 2, 0, 1, buf.data(), 0, 4), std::out_of_range);
}

// REMED-GFX-130 false-positive audit: this test used to be a bare EXPECT_NO_THROW, which asserted
// nothing about what GetData produced -- a renderer that read nothing and a renderer that read the
// slice correctly both passed it. It now asserts the uploaded content itself.
TEST_F(Texture3DTest, GetDataBoxWithinBoundsReturnsUploadedSlice)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    const Color uploaded[8] = {
        Color(11, 111, 21, 191), Color(12, 112, 22, 192),
        Color(13, 113, 23, 193), Color(14, 114, 24, 194),
        Color(15, 115, 25, 195), Color(16, 116, 26, 196),
        Color(17, 117, 27, 197), Color(18, 118, 28, 198),
    };
    tex.SetData(uploaded, 8);

    // Every renderer that reaches this point reports GraphicsCapability::Texture3D (the fixture
    // skips the ones that do not), and REMED-GFX-130 made that report honest everywhere -- ASCII
    // used to answer true from IGraphicsRenderer::SupportsCapability's own default while creating
    // no volume resource at all. So real content is the only acceptable outcome here.
    const Color sentinel(0xCD, 0xCD, 0xCD, 0xCD);
    std::vector<Color> buf(4, sentinel);
    // A renderer that owns volume pixels but cannot fetch them back must say so rather than
    // fabricate them; the upload above is still exercised on every renderer.
    if (!VolumeReadbackSupported())
    {
        EXPECT_THROW((void)tex.GetData(0, 0, 0, 2, 2, 0, 1, buf.data(), 0, 4), System::NotSupportedException);
        return;
    }
    ASSERT_NO_THROW(tex.GetData(0, 0, 0, 2, 2, 0, 1, buf.data(), 0, 4));
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(buf[i].getPackedValueProperty(), uploaded[i].getPackedValueProperty()) << i;
}

// Task 913: see the identical SetData test above for the full rationale.
TEST_F(Texture3DTest, GetDataBoxElementCountLessThanRegionThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    Color buf[1] = { Color(0, 0, 0, 0) };
    // Region is 2x2x1=4 voxels; only 1 element provided.
    EXPECT_THROW(tex.GetData(0, 0, 0, 2, 2, 0, 1, buf, 0, 1), std::out_of_range);
}

// -----------------------------------------------------------------------
// Dispose
// -----------------------------------------------------------------------

TEST_F(Texture3DTest, DisposeMarksResourceDisposed)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    EXPECT_FALSE(tex.getIsDisposedProperty());
    tex.Dispose();
    EXPECT_TRUE(tex.getIsDisposedProperty());
}

TEST_F(Texture3DTest, DoubleDisposeDoesNotThrow)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    tex.Dispose();
    EXPECT_NO_THROW(tex.Dispose());
}

// -----------------------------------------------------------------------
// TextureCollection assignment / Texture base class (plans/plan_graphics.md Task 863)
//
// Before Task 863, Texture3D inherited GraphicsResource directly (not Texture), so it could
// never be stored in a TextureCollection (std::vector<Texture*>) at all -- a Texture3D* could
// not be assigned into GraphicsDevice.Textures[slot], a real, previously-impossible operation
// that is the clearest possible proof this fix succeeded. Now Texture3D : Texture, matching FNA.
// -----------------------------------------------------------------------

TEST_F(Texture3DTest, CanBeAssignedIntoTextureCollection)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    TextureCollection col;
    // Previously a compile error: Texture3D was not convertible to Texture*.
    EXPECT_NO_THROW(col(0, &tex));
    EXPECT_EQ(col[0], static_cast<Texture*>(&tex));
}

TEST_F(Texture3DTest, CanBeAssignedIntoRealGraphicsDeviceTexturesSlot)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    // Real GraphicsDevice.Textures[slot] = texture3D, matching FNA's own Texture3D : Texture
    // capability -- structurally impossible before Task 863.
    EXPECT_NO_THROW(gd.getTexturesProperty()(0, &tex));
    EXPECT_EQ(gd.getTexturesProperty()[0], static_cast<Texture*>(&tex));
}

// Texture::Dispose(bool) removes the texture from GraphicsDevice.Textures/VertexTextures on
// disposal (matches FNA's Texture.Dispose unbind behaviour). Texture3D::Dispose(bool) previously
// only released its own renderer handle without ever calling into Texture::Dispose(bool), so this
// unbind never applied to Texture3D. Now it does, since Texture3D::Dispose(bool) calls
// Texture::Dispose(disposing) after releasing its own renderer handle (same order as
// Texture2D::Dispose(bool)).
TEST_F(Texture3DTest, DisposeUnbindsFromGraphicsDeviceTextures)
{
    auto tex = std::make_unique<Texture3D>(gd, 2, 2, 2, false, SurfaceFormat::Color);
    gd.getTexturesProperty()(0, tex.get());
    ASSERT_EQ(gd.getTexturesProperty()[0], static_cast<Texture*>(tex.get()));

    tex->Dispose();

    EXPECT_EQ(gd.getTexturesProperty()[0], nullptr);
}

TEST_F(Texture3DTest, DisposeUnbindsFromGraphicsDeviceVertexTextures)
{
    auto tex = std::make_unique<Texture3D>(gd, 2, 2, 2, false, SurfaceFormat::Color);
    gd.getVertexTexturesProperty()(0, tex.get());
    ASSERT_EQ(gd.getVertexTexturesProperty()[0], static_cast<Texture*>(tex.get()));

    tex->Dispose();

    EXPECT_EQ(gd.getVertexTexturesProperty()[0], nullptr);
}
