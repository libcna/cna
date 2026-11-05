// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <typeinfo>
#include <vector>
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "System/ArgumentException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/InvalidOperationException.hpp"

using namespace Microsoft::Xna::Framework::Graphics;
using Microsoft::Xna::Framework::Rectangle;

namespace
{
    class RenderTargetBindingRefreshTest : public testing::Test
    {
    protected:
        void SetUp() override
        {
            CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Software);
        }
        static void Dirty(GraphicsDevice& device)
        {
            Viewport viewport(1, 2, 3, 4);
            viewport.setMinDepthProperty(0.25f);
            viewport.setMaxDepthProperty(0.75f);
            device.setViewportProperty(viewport);
            device.setScissorRectangleProperty(Rectangle(2, 1, 4, 3));
        }
        static void ExpectDirty(const GraphicsDevice& device)
        {
            const auto viewport = device.getViewportProperty();
            EXPECT_EQ(viewport.getXProperty(), 1);
            EXPECT_EQ(viewport.getYProperty(), 2);
            EXPECT_EQ(viewport.getWidthProperty(), 3);
            EXPECT_EQ(viewport.getHeightProperty(), 4);
            EXPECT_FLOAT_EQ(viewport.getMinDepthProperty(), 0.25f);
            EXPECT_FLOAT_EQ(viewport.getMaxDepthProperty(), 0.75f);
            EXPECT_EQ(device.getScissorRectangleProperty(), Rectangle(2, 1, 4, 3));
        }
        static void ExpectWhole(GraphicsDevice& device, int width, int height)
        {
            const auto viewport = device.getViewportProperty();
            EXPECT_EQ(viewport.getXProperty(), 0);
            EXPECT_EQ(viewport.getYProperty(), 0);
            EXPECT_EQ(viewport.getWidthProperty(), width);
            EXPECT_EQ(viewport.getHeightProperty(), height);
            EXPECT_FLOAT_EQ(viewport.getMinDepthProperty(), 0.0f);
            EXPECT_FLOAT_EQ(viewport.getMaxDepthProperty(), 1.0f);
            EXPECT_EQ(device.getScissorRectangleProperty(), Rectangle(0, 0, width, height));
        }
        template<class Exception, class Action>
        static void Refuses(Action&& action)
        {
            try { action(); ADD_FAILURE() << "binding accepted"; }
            catch (const Exception& error) { EXPECT_EQ(typeid(error), typeid(Exception)); }
        }
    };
}

TEST_F(RenderTargetBindingRefreshTest, CallerInputAndReturnedBindingsAreIndependentCopies)
{
    GraphicsDevice device;
    RenderTargetCube cube(device, 8, false, SurfaceFormat::Color, DepthFormat::None);
    // Refresh SetRenderTargets IL_02e5-02fb copies each binding; GetRenderTargets
    // IL_0007-0016 allocates and copies a new array. Resource and face both matter.
    std::vector<RenderTargetBinding> input{RenderTargetBinding(&cube, CubeMapFace::NegativeZ)};
    device.SetRenderTargets(input);
    input[0] = RenderTargetBinding(&cube, CubeMapFace::PositiveX);
    auto first = device.GetRenderTargets();
    ASSERT_EQ(first.size(), 1u);
    EXPECT_EQ(first[0].getRenderTargetProperty(), &cube);
    EXPECT_EQ(first[0].getCubeMapFaceProperty(), CubeMapFace::NegativeZ);
    first[0] = RenderTargetBinding();
    auto second = device.GetRenderTargets();
    ASSERT_EQ(second.size(), 1u);
    EXPECT_EQ(second[0].getRenderTargetProperty(), &cube);
    EXPECT_EQ(second[0].getCubeMapFaceProperty(), CubeMapFace::NegativeZ);
    device.SetRenderTargets({});
    ASSERT_EQ(second.size(), 1u);
    EXPECT_EQ(second[0].getRenderTargetProperty(), &cube);
    EXPECT_EQ(second[0].getCubeMapFaceProperty(), CubeMapFace::NegativeZ);
    EXPECT_TRUE(device.GetRenderTargets().empty());
}

TEST_F(RenderTargetBindingRefreshTest, EqualBindingsAreNoOpsButChangingTheCubeFaceResetsState)
{
    GraphicsDevice device;
    RenderTargetCube cube(device, 8, false, SurfaceFormat::Color, DepthFormat::None);
    device.SetRenderTarget(&cube, CubeMapFace::NegativeX);
    Dirty(device);
    // Refresh SetRenderTargets IL_0015-0083 compares target identity AND cube face.
    device.SetRenderTargets({RenderTargetBinding(&cube, CubeMapFace::NegativeX)});
    ExpectDirty(device);
    device.SetRenderTarget(&cube, CubeMapFace::PositiveY);
    ExpectWhole(device, 8, 8);
    ASSERT_EQ(device.GetRenderTargets().size(), 1u);
    EXPECT_EQ(device.GetRenderTargets()[0].getRenderTargetProperty(), &cube);
    EXPECT_EQ(device.GetRenderTargets()[0].getCubeMapFaceProperty(), CubeMapFace::PositiveY);
    device.SetRenderTargets({});
}

TEST_F(RenderTargetBindingRefreshTest, NullCubeAndEmptyPluralRestoreBackbufferThenBecomeNoOps)
{
    GraphicsDevice device;
    RenderTarget2D target(device, 8, 4);
    device.SetRenderTarget(&target);
    // Refresh singular cube SetRenderTarget IL_0000-000b handles null before the face.
    device.SetRenderTarget(static_cast<RenderTargetCube*>(nullptr), static_cast<CubeMapFace>(99));
    EXPECT_TRUE(device.GetRenderTargets().empty());
    const auto& pp = device.getPresentationParametersProperty();
    ExpectWhole(device, pp.getBackBufferWidthProperty(), pp.getBackBufferHeightProperty());
    Dirty(device);
    device.SetRenderTargets({});
    ExpectDirty(device); // Refresh unchanged empty set returns before resetting rectangles.
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    ExpectDirty(device);
}

TEST_F(RenderTargetBindingRefreshTest, FourTargetsBindInOrderAndTooManyWinsBeforeNullValidation)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    RenderTarget2D a(device, 8, 8), b(device, 8, 8), c(device, 8, 8), d(device, 8, 8);
    const std::vector<RenderTargetBinding> four{&a, &b, &c, &d};
    device.SetRenderTargets(four);
    Dirty(device);
    // Refresh SetRenderTargets IL_0088-00ac: HiDef maximum 4, before per-slot checks.
    Refuses<System::NotSupportedException>([&] {
        device.SetRenderTargets(std::vector<RenderTargetBinding>(5));
    });
    const auto bound = device.GetRenderTargets();
    ASSERT_EQ(bound.size(), 4u);
    for (std::size_t i = 0; i != four.size(); ++i)
        EXPECT_EQ(bound[i].getRenderTargetProperty(), four[i].getRenderTargetProperty());
    ExpectDirty(device);
    device.SetRenderTargets({});
}

TEST_F(RenderTargetBindingRefreshTest, ReachCountRefusalPreservesAnExistingCubeFace)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::Reach);
    RenderTargetCube cube(device, 8, false, SurfaceFormat::Color, DepthFormat::None);
    device.SetRenderTarget(&cube, CubeMapFace::NegativeY);
    Dirty(device);
    // Refresh profile maximum 1, SetRenderTargets IL_0088-00ac. No null-slot error first.
    Refuses<System::NotSupportedException>([&] {
        device.SetRenderTargets(std::vector<RenderTargetBinding>(2));
    });
    const auto bound = device.GetRenderTargets();
    ASSERT_EQ(bound.size(), 1u);
    EXPECT_EQ(bound[0].getRenderTargetProperty(), &cube);
    EXPECT_EQ(bound[0].getCubeMapFaceProperty(), CubeMapFace::NegativeY);
    ExpectDirty(device);
    device.SetRenderTargets({});
}

TEST_F(RenderTargetBindingRefreshTest, SampleAndPixelSizeMismatchPreserveAnExistingCubeFace)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    RenderTargetCube cube(device, 8, false, SurfaceFormat::Color, DepthFormat::None);
    RenderTarget2D plain(device, 8, 8), msaa(device, 8, 8, false, SurfaceFormat::Color,
        DepthFormat::None, 4, RenderTargetUsage::PreserveContents);
    RenderTarget2D wide(device, 8, 8, false, SurfaceFormat::Rgba64, DepthFormat::None);
    ASSERT_EQ(msaa.getMultiSampleCountProperty(), 4);
    ASSERT_EQ(wide.getFormatProperty(), SurfaceFormat::Rgba64);
    device.SetRenderTarget(&cube, CubeMapFace::NegativeZ);
    Dirty(device);
    // Refresh IsSameSize IL_0075-008f compares applied samples and byte size, not depth.
    for (auto* incompatible : {&msaa, &wide})
    {
        Refuses<System::ArgumentException>([&] {
            device.SetRenderTargets({RenderTargetBinding(&plain), RenderTargetBinding(incompatible)});
        });
        const auto bound = device.GetRenderTargets();
        ASSERT_EQ(bound.size(), 1u);
        EXPECT_EQ(bound[0].getRenderTargetProperty(), &cube);
        EXPECT_EQ(bound[0].getCubeMapFaceProperty(), CubeMapFace::NegativeZ);
        ExpectDirty(device);
    }
    device.SetRenderTargets({});
}

TEST_F(RenderTargetBindingRefreshTest, EqualPixelSizeAndDifferentDepthFormatsRemainCompatible)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    RenderTarget2D color(device, 8, 4, false, SurfaceFormat::Color, DepthFormat::None);
    RenderTarget2D single(device, 8, 4, false, SurfaceFormat::Single, DepthFormat::Depth24Stencil8);
    // Refresh IsSameSize IL_0059-0099 compares w/h/samples/pixelSize; depth is absent.
    ASSERT_EQ(single.getFormatProperty(), SurfaceFormat::Single);
    device.SetRenderTargets({RenderTargetBinding(&color), RenderTargetBinding(&single)});
    const auto bound = device.GetRenderTargets();
    ASSERT_EQ(bound.size(), 2u);
    EXPECT_EQ(bound[0].getRenderTargetProperty(), &color);
    EXPECT_EQ(bound[1].getRenderTargetProperty(), &single);
    ExpectWhole(device, 8, 4);
    device.SetRenderTargets({RenderTargetBinding(&single), RenderTargetBinding(&color)});
    const auto reversed = device.GetRenderTargets();
    ASSERT_EQ(reversed.size(), 2u);
    EXPECT_EQ(reversed[0].getRenderTargetProperty(), &single);
    EXPECT_EQ(reversed[1].getRenderTargetProperty(), &color);
    device.SetRenderTargets({});
}

TEST_F(RenderTargetBindingRefreshTest, PreviouslySampledTargetMayBindButActiveCubeRejectsTextureAssignment)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    RenderTargetCube cube(device, 8, false, SurfaceFormat::Color, DepthFormat::None);
    // Refresh SetRenderTargets IL_00b1-0172 has no texture-slot validation. The reverse
    // direction is refused by TextureCollection.set_Item IL_003e-0050 before slot mutation.
    device.getTexturesProperty()(0, &cube);
    device.SetRenderTarget(&cube, CubeMapFace::PositiveZ);
    EXPECT_THROW(device.getTexturesProperty()(1, &cube), System::InvalidOperationException);
    EXPECT_EQ(device.getTexturesProperty()[1], nullptr);
    ASSERT_EQ(device.GetRenderTargets().size(), 1u);
    EXPECT_EQ(device.GetRenderTargets()[0].getCubeMapFaceProperty(), CubeMapFace::PositiveZ);
    device.SetRenderTargets({});
    device.getTexturesProperty()(1, &cube);
    EXPECT_EQ(device.getTexturesProperty()[1], &cube);
    device.getTexturesProperty()(0, nullptr);
    device.getTexturesProperty()(1, nullptr);
}

TEST_F(RenderTargetBindingRefreshTest, UnsupportedArraySliceCannotHideBehindAnEqualXnaBinding)
{
    GraphicsDevice device;
    RenderTarget2D target(device, 8, 8);
    device.SetRenderTarget(&target);
    Dirty(device);
    // ArraySlice is CNAEXT (absent from Refresh RTBinding's two-field value type).
    // CNA's RT2D::DescribeRenderTargetBindingEXT explicitly refuses every nonzero slice.
    Refuses<System::NotSupportedException>([&] {
        device.SetRenderTargets({RenderTargetBinding(static_cast<Texture*>(&target), 1)});
    });
    const auto bound = device.GetRenderTargets();
    ASSERT_EQ(bound.size(), 1u);
    EXPECT_EQ(bound[0].getRenderTargetProperty(), &target);
    EXPECT_EQ(bound[0].getArraySliceProperty(), 0);
    ExpectDirty(device);
    device.SetRenderTargets({});
}
