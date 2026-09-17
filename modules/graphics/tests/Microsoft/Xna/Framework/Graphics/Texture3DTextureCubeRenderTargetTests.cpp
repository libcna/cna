// SPDX-License-Identifier: MS-PL
//
// Task 129: unit tests for the enums that govern Texture3D, TextureCube,
// RenderTarget2D, and RenderTargetCube.
//
// Texture3D / TextureCube / RenderTarget2D / RenderTargetCube all require a
// GraphicsDevice to construct and have no throw-before-access guards, so
// constructor / property / GetData / SetData tests are covered by the
// EasyGL and Vulkan integration tests (easygl_render_target_test, house3d_demo,
// etc.).  What CAN be verified without a GPU are the enum values that all four
// types depend on.

#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"

using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;

// -----------------------------------------------------------------------
// CubeMapFace — XNA 4.0 specifies explicit integer values 0-5
// -----------------------------------------------------------------------

TEST(CubeMapFaceTest, PositiveXIsZero)
{
    EXPECT_EQ(static_cast<int>(CubeMapFace::PositiveX), 0);
}

TEST(CubeMapFaceTest, NegativeXIsOne)
{
    EXPECT_EQ(static_cast<int>(CubeMapFace::NegativeX), 1);
}

TEST(CubeMapFaceTest, PositiveYIsTwo)
{
    EXPECT_EQ(static_cast<int>(CubeMapFace::PositiveY), 2);
}

TEST(CubeMapFaceTest, NegativeYIsThree)
{
    EXPECT_EQ(static_cast<int>(CubeMapFace::NegativeY), 3);
}

TEST(CubeMapFaceTest, PositiveZIsFour)
{
    EXPECT_EQ(static_cast<int>(CubeMapFace::PositiveZ), 4);
}

TEST(CubeMapFaceTest, NegativeZIsFive)
{
    EXPECT_EQ(static_cast<int>(CubeMapFace::NegativeZ), 5);
}

TEST(CubeMapFaceTest, AllSixFacesAreDistinct)
{
    EXPECT_NE(CubeMapFace::PositiveX, CubeMapFace::NegativeX);
    EXPECT_NE(CubeMapFace::PositiveX, CubeMapFace::PositiveY);
    EXPECT_NE(CubeMapFace::PositiveX, CubeMapFace::NegativeY);
    EXPECT_NE(CubeMapFace::PositiveX, CubeMapFace::PositiveZ);
    EXPECT_NE(CubeMapFace::PositiveX, CubeMapFace::NegativeZ);
    EXPECT_NE(CubeMapFace::NegativeX, CubeMapFace::PositiveY);
    EXPECT_NE(CubeMapFace::NegativeX, CubeMapFace::NegativeY);
    EXPECT_NE(CubeMapFace::NegativeX, CubeMapFace::PositiveZ);
    EXPECT_NE(CubeMapFace::NegativeX, CubeMapFace::NegativeZ);
    EXPECT_NE(CubeMapFace::PositiveY, CubeMapFace::NegativeY);
    EXPECT_NE(CubeMapFace::PositiveY, CubeMapFace::PositiveZ);
    EXPECT_NE(CubeMapFace::PositiveY, CubeMapFace::NegativeZ);
    EXPECT_NE(CubeMapFace::NegativeY, CubeMapFace::PositiveZ);
    EXPECT_NE(CubeMapFace::NegativeY, CubeMapFace::NegativeZ);
    EXPECT_NE(CubeMapFace::PositiveZ, CubeMapFace::NegativeZ);
}

TEST(CubeMapFaceTest, OppositeFacesDiffer)
{
    EXPECT_NE(CubeMapFace::PositiveX, CubeMapFace::NegativeX);
    EXPECT_NE(CubeMapFace::PositiveY, CubeMapFace::NegativeY);
    EXPECT_NE(CubeMapFace::PositiveZ, CubeMapFace::NegativeZ);
}

// -----------------------------------------------------------------------
// DepthFormat — XNA 4.0 specifies values: None=0, Depth16=1, Depth24=2,
//               Depth24Stencil8=3
// -----------------------------------------------------------------------

TEST(DepthFormatTest, NoneIsZero)
{
    EXPECT_EQ(static_cast<int>(DepthFormat::None), 0);
}

TEST(DepthFormatTest, Depth16IsOne)
{
    EXPECT_EQ(static_cast<int>(DepthFormat::Depth16), 1);
}

TEST(DepthFormatTest, Depth24IsTwo)
{
    EXPECT_EQ(static_cast<int>(DepthFormat::Depth24), 2);
}

TEST(DepthFormatTest, Depth24Stencil8IsThree)
{
    EXPECT_EQ(static_cast<int>(DepthFormat::Depth24Stencil8), 3);
}

TEST(DepthFormatTest, AllFormatsAreDistinct)
{
    EXPECT_NE(DepthFormat::None,           DepthFormat::Depth16);
    EXPECT_NE(DepthFormat::None,           DepthFormat::Depth24);
    EXPECT_NE(DepthFormat::None,           DepthFormat::Depth24Stencil8);
    EXPECT_NE(DepthFormat::Depth16,        DepthFormat::Depth24);
    EXPECT_NE(DepthFormat::Depth16,        DepthFormat::Depth24Stencil8);
    EXPECT_NE(DepthFormat::Depth24,        DepthFormat::Depth24Stencil8);
}

TEST(DepthFormatTest, NoneIsDifferentFromAllDepthFormats)
{
    EXPECT_NE(DepthFormat::None, DepthFormat::Depth16);
    EXPECT_NE(DepthFormat::None, DepthFormat::Depth24);
    EXPECT_NE(DepthFormat::None, DepthFormat::Depth24Stencil8);
}

// -----------------------------------------------------------------------
// RenderTargetUsage — XNA 4.0 specifies:
//   DiscardContents=0, PreserveContents=1, PlatformContents=2
// -----------------------------------------------------------------------

TEST(RenderTargetUsageTest, DiscardContentsIsZero)
{
    EXPECT_EQ(static_cast<int>(RenderTargetUsage::DiscardContents), 0);
}

TEST(RenderTargetUsageTest, PreserveContentsIsOne)
{
    EXPECT_EQ(static_cast<int>(RenderTargetUsage::PreserveContents), 1);
}

TEST(RenderTargetUsageTest, PlatformContentsIsTwo)
{
    EXPECT_EQ(static_cast<int>(RenderTargetUsage::PlatformContents), 2);
}

TEST(RenderTargetUsageTest, AllUsagesAreDistinct)
{
    EXPECT_NE(RenderTargetUsage::DiscardContents,  RenderTargetUsage::PreserveContents);
    EXPECT_NE(RenderTargetUsage::DiscardContents,  RenderTargetUsage::PlatformContents);
    EXPECT_NE(RenderTargetUsage::PreserveContents, RenderTargetUsage::PlatformContents);
}

TEST(RenderTargetUsageTest, DefaultIsDiscardContents)
{
    // Matches the default parameter in RenderTarget2D and RenderTargetCube constructors.
    EXPECT_EQ(RenderTargetUsage::DiscardContents, static_cast<RenderTargetUsage>(0));
}

// -----------------------------------------------------------------------
// RenderTargetCube::SetData — REMED-GFX-135
//
// RenderTargetCube IS a TextureCube, so it inherits SetData. Every renderer except EasyGL leaves
// IRenderTargetCubeRenderer::SetData at its interface body, which used to be `{}` -- the same
// accept-and-discard this finding removes, reached through inheritance rather than a null renderer.
// EasyGL is the one renderer that overrides it with a real upload into the shared GL cube texture.
//
// Constructing a RenderTargetCube can legitimately fail on a renderer that has no cube-map render
// target at all (Software, native 2D, ASCII, Canvas, DIRECTX3 keep CreateRenderTargetCube's nullptr
// default); that is not what is under test, so it is skipped rather than asserted either way.
// -----------------------------------------------------------------------

#include <memory>
#include <vector>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/ObjectDisposedException.hpp"

namespace
{
    using namespace CNA::Testing::Renderers;   // NOLINT(google-build-using-namespace)

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::RenderTargetCube;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    /// plans/plan_runtimerenderer.md RTR-P9-5: which renderers accept a RenderTargetCube SetData upload,
    /// asked of the ACTIVE renderer. Each arm's reasoning is preserved with its renderer:
    ///
    ///   EasyGL / Magnum -- SetData is a real glTexSubImage2D upload into the shared cube texture
    ///     (Magnum expresses the same upload through CubeMapTexture::setSubImage), so the
    ///     framebuffer's colour attachment IS an ordinary GL cube texture and an upload reaches it
    ///     directly. REMED-GFX-130 left RenderTargetCube READBACK unimplemented here (tracked as
    ///     REMED-GFX-134), so this suite can only assert that the upload is accepted -- the
    ///     byte-exact content assertion for the identical `set_sub_image_2d` path lives on the
    ///     plain cube in
    ///     modules/graphics/examples/texturecube_texture3d_setdata_contract_test.cpp.
    ///   OpenGL4 -- the same real upload shape, and unlike EasyGL it also implements the
    ///     per-face+level FBO readback, so the byte-exact round trip is asserted directly by its
    ///     own OpenGL4_RenderTargetCube CTest (plans/plan_opengl4.md GL4-15).
    ///   Wicked -- a real staged upload into the rendered cube's colour array (UploadTextureRegion,
    ///     plans/plan_wicked.md WICKED-55/79), with the per-face readback implemented, so the byte-exact
    ///     round trip is asserted by the shared TextureCube/RenderTargetCube suites. A
    ///     multisampled cube still refuses: resolving into one face needs a per-face resolve
    ///     subresource this renderer does not create.
    [[nodiscard]] inline bool RenderTargetCubeAcceptsSetData()
    {
        // plans/plan_igl.md IGL-21: IGL belongs here too. Its RenderTargetCube's colour attachment is an
        // ordinary sampleable `igl::TextureType::Cube` image, so `IglRenderTargetCubeRenderer::
        // SetData` seeds a face with a real upload rather than accepting and discarding one --
        // which is what this test forbids. Without the entry an IGL build asserted a refusal the
        // renderer deliberately does not make.
        // SOFTWARE-119 writes into the same CPU face/mip storage its rasterizer and GetData use;
        // the shared set-data contract asserts the exact round trip rather than mere acceptance.
        // WINCLOSE-0017: DirectX11's single-sample cube colour resource is an ordinary sampleable
        // TEXTURECUBE, and its RenderTargetCube renderer now uploads into it with the plain
        // TextureCube's own UpdateSubresource path; the exact round trip is asserted below.
        return CNA_RENDERER_IS(OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2,
                               Software, Magnum, OpenGL4, Wicked, Igl, Rlgl, DirectX11, DirectX12);
    }
}

TEST(RenderTargetDimensionValidationTest, RenderTarget2DRejectsNonPositiveDimensionsBeforeAllocation)
{
    GraphicsDevice gd;
    EXPECT_THROW((void)RenderTarget2D(gd, 0, 1), System::ArgumentOutOfRangeException);
    EXPECT_THROW((void)RenderTarget2D(gd, 1, 0), System::ArgumentOutOfRangeException);
    EXPECT_THROW((void)RenderTarget2D(
                     gd, -1, 1, true, SurfaceFormat::Color, DepthFormat::None, 4,
                     RenderTargetUsage::PreserveContents),
                 System::ArgumentOutOfRangeException);
}

TEST(RenderTargetDimensionValidationTest, RenderTargetCubeRejectsNonPositiveSizeBeforeAllocation)
{
    GraphicsDevice gd;
    EXPECT_THROW((void)RenderTargetCube(
                     gd, 0, false, SurfaceFormat::Color, DepthFormat::None, 0,
                     RenderTargetUsage::DiscardContents),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW((void)RenderTargetCube(
                     gd, -1, true, SurfaceFormat::Color, DepthFormat::None, 4,
                     RenderTargetUsage::PreserveContents),
                 System::ArgumentOutOfRangeException);
}

TEST(RenderTargetCubeSetDataContractTest, StoresTheFaceOrRefusesButNeverSilentlyDiscardsIt)
{
    GraphicsDevice gd;
    std::unique_ptr<RenderTargetCube> rt;
    try
    {
        rt = std::make_unique<RenderTargetCube>(gd, 4, false, SurfaceFormat::Color,
                                                DepthFormat::None, 0,
                                                RenderTargetUsage::DiscardContents);
    }
    catch (const std::exception&)
    {
        GTEST_SKIP() << "this renderer creates no cube-map render target, so the inherited SetData "
                        "is unreachable";
    }

    std::vector<Color> face(16, Color(64, 128, 192, 255));
    if (RenderTargetCubeAcceptsSetData())
        EXPECT_NO_THROW(rt->SetData(CubeMapFace::PositiveX, face.data(), 16));
    else
        EXPECT_THROW(rt->SetData(CubeMapFace::PositiveX, face.data(), 16),
                     System::NotSupportedException);
}

// WINCLOSE-0017: "does not throw" is not "stored". A seeded face and a seeded sub-rectangle of a
// second face must read back exactly, and writing one face must not touch another. Measured on
// the renderers this closure could run; the others in RenderTargetCubeAcceptsSetData() assert
// their round trip in their own suites.
TEST(RenderTargetCubeSetDataContractTest, SeededFacesAndRegionsReadBackExactly)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, DirectX11, DirectX12);

    GraphicsDevice gd;
    RenderTargetCube rt(gd, 4, false, SurfaceFormat::Color, DepthFormat::None, 0,
                        RenderTargetUsage::PreserveContents);

    std::vector<Color> positiveX(16);
    std::vector<Color> negativeY(16);
    for (int i = 0; i < 16; ++i)
    {
        positiveX[static_cast<std::size_t>(i)] =
            Color(i * 13, 255 - i * 7, 40 + i, 200 + i % 3);
        negativeY[static_cast<std::size_t>(i)] =
            Color(90 + i, i * 11, 17, 255);
    }
    rt.SetData(CubeMapFace::PositiveX, positiveX.data(), 16);
    rt.SetData(CubeMapFace::NegativeY, negativeY.data(), 16);

    const std::vector<Color> region{Color(1, 2, 3, 4), Color(5, 6, 7, 8),
                                    Color(9, 10, 11, 12), Color(13, 14, 15, 16)};
    const Microsoft::Xna::Framework::Rectangle rect(1, 2, 2, 2);
    rt.SetData(CubeMapFace::NegativeY, 0, &rect, region.data(), 0, 4);

    std::vector<Color> gotX(16);
    rt.GetData(CubeMapFace::PositiveX, gotX.data(), 16);
    for (int i = 0; i < 16; ++i)
        EXPECT_EQ(gotX[static_cast<std::size_t>(i)], positiveX[static_cast<std::size_t>(i)])
            << "+X texel " << i;

    std::vector<Color> gotRegion(4);
    rt.GetData(CubeMapFace::NegativeY, 0, &rect, gotRegion.data(), 0, 4);
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(gotRegion[static_cast<std::size_t>(i)], region[static_cast<std::size_t>(i)])
            << "-Y region texel " << i;

    std::vector<Color> gotY(16);
    rt.GetData(CubeMapFace::NegativeY, gotY.data(), 16);
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            const bool inRegion = x >= 1 && x < 3 && y >= 2 && y < 4;
            const Color expected = inRegion
                ? region[static_cast<std::size_t>((y - 2) * 2 + (x - 1))]
                : negativeY[static_cast<std::size_t>(y * 4 + x)];
            EXPECT_EQ(gotY[static_cast<std::size_t>(y * 4 + x)], expected)
                << "-Y texel (" << x << ',' << y << ')';
        }
    }
}

TEST(RenderTargetCubeSetDataContractTest, SetDataAfterDisposeThrowsObjectDisposed)
{
    GraphicsDevice gd;
    std::unique_ptr<RenderTargetCube> rt;
    try
    {
        rt = std::make_unique<RenderTargetCube>(gd, 4, false, SurfaceFormat::Color,
                                                DepthFormat::None, 0,
                                                RenderTargetUsage::DiscardContents);
    }
    catch (const std::exception&)
    {
        GTEST_SKIP() << "this renderer creates no cube-map render target";
    }

    std::vector<Color> face(16, Color(64, 128, 192, 255));
    rt->Dispose();
    EXPECT_THROW(rt->SetData(CubeMapFace::PositiveX, face.data(), 16),
                 System::ObjectDisposedException);
}
