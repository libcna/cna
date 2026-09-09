// SPDX-License-Identifier: MS-PL
// SOFTWARE-179: renderer-independent GraphicsProfile resource ceilings.

#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

using namespace CNA::Testing::Renderers;

#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ArgumentException.hpp"

using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DynamicIndexBuffer;
using Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetCube;
using Microsoft::Xna::Framework::Graphics::RenderTargetBinding;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::Texture3D;
using Microsoft::Xna::Framework::Graphics::TextureCube;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

TEST(GraphicsProfileResourceCeilingTest, ReachRejectsOversizedTexturesVolumeTexturesAndMrt)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    PresentationParameters parameters;
    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::Reach, parameters);

    EXPECT_THROW((void)Texture2D(device, 2049, 1, false, SurfaceFormat::Color),
                 System::NotSupportedException);
    EXPECT_THROW((void)TextureCube(device, 513, false, SurfaceFormat::Color),
                 System::NotSupportedException);
    EXPECT_THROW((void)Texture3D(device, 1, 1, 1, false, SurfaceFormat::Color),
                 System::NotSupportedException);

    RenderTarget2D first(device, 1, 1);
    RenderTarget2D second(device, 1, 1);
    EXPECT_THROW(
        device.SetRenderTargets({RenderTargetBinding(&first), RenderTargetBinding(&second)}),
        System::NotSupportedException);
}

TEST(GraphicsProfileResourceCeilingTest, HiDefPermitsVolumeAndMrtButEnforcesItsOwnLimits)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    PresentationParameters parameters;
    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, parameters);

    Texture2D texture(device, 1, 1, false, SurfaceFormat::Color);
    TextureCube cube(device, 1, false, SurfaceFormat::Color);
    Texture3D volume(device, 1, 1, 1, false, SurfaceFormat::Color);
    EXPECT_THROW((void)Texture2D(device, 4097, 1, false, SurfaceFormat::Color),
                 System::NotSupportedException);
    EXPECT_THROW((void)TextureCube(device, 4097, false, SurfaceFormat::Color),
                 System::NotSupportedException);
    EXPECT_THROW((void)Texture3D(device, 257, 1, 1, false, SurfaceFormat::Color),
                 System::NotSupportedException);

    RenderTarget2D first(device, 1, 1);
    RenderTarget2D second(device, 1, 1);
    EXPECT_NO_THROW(
        device.SetRenderTargets({RenderTargetBinding(&first), RenderTargetBinding(&second)}));
    device.SetRenderTargets({});
}

TEST(GraphicsProfileResourceCeilingTest, Texture2DEnforcesTheSharedMaximumAspectRatio)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    PresentationParameters parameters;
    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, parameters);

    EXPECT_NO_THROW((void)Texture2D(device, 4096, 2, false, SurfaceFormat::Color));
    EXPECT_NO_THROW((void)Texture2D(device, 2, 4096, false, SurfaceFormat::Color));
    EXPECT_THROW((void)Texture2D(device, 4096, 1, false, SurfaceFormat::Color),
                 System::NotSupportedException);
    EXPECT_THROW((void)Texture2D(device, 1, 4096, false, SurfaceFormat::Color),
                 System::NotSupportedException);
}

TEST(GraphicsProfileResourceCeilingTest, BackBufferReadbackIsHiDefOnly)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    PresentationParameters parameters;
    parameters.setBackBufferWidthProperty(2);
    parameters.setBackBufferHeightProperty(2);
    {
        GraphicsDevice reach(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::Reach, parameters);
        Color pixel;
        EXPECT_THROW(reach.GetBackBufferData(&pixel, 1), System::NotSupportedException);
        EXPECT_THROW(reach.GetBackBufferData(static_cast<Color*>(nullptr), 0),
                     System::NotSupportedException);
    }

    GraphicsDevice hiDef(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, parameters);
    Color pixel;
    hiDef.Clear(Color::CornflowerBlue);
    const Rectangle onePixel(0, 0, 1, 1);
    EXPECT_NO_THROW(hiDef.GetBackBufferData(&onePixel, &pixel, 0, 1));
    EXPECT_EQ(Color::CornflowerBlue, pixel);
}

TEST(GraphicsProfileResourceCeilingTest, ReachAllowsOnlyConditionalNpotTexture2DResources)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    PresentationParameters parameters;
    {
        GraphicsDevice reach(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::Reach, parameters);
        EXPECT_NO_THROW((void)Texture2D(reach, 3, 5, false, SurfaceFormat::Color));
        EXPECT_THROW((void)Texture2D(reach, 3, 5, true, SurfaceFormat::Color),
                     System::NotSupportedException);
        EXPECT_THROW((void)Texture2D(reach, 7, 5, false, SurfaceFormat::Dxt1),
                     System::NotSupportedException);
    }

    GraphicsDevice hiDef(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, parameters);
    EXPECT_NO_THROW((void)Texture2D(hiDef, 3, 5, true, SurfaceFormat::Color));
    EXPECT_NO_THROW((void)Texture2D(hiDef, 8, 4, false, SurfaceFormat::Dxt1));
    EXPECT_THROW((void)Texture2D(hiDef, 7, 5, false, SurfaceFormat::Dxt1),
                 System::ArgumentException);
}

TEST(GraphicsProfileResourceCeilingTest, CubePowerOfTwoAndDxtAlignmentFollowXnaProfiles)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    PresentationParameters parameters;
    {
        GraphicsDevice reach(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::Reach, parameters);
        EXPECT_THROW((void)TextureCube(reach, 3, false, SurfaceFormat::Color),
                     System::NotSupportedException);
    }

    GraphicsDevice hiDef(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, parameters);
    EXPECT_NO_THROW((void)TextureCube(hiDef, 3, true, SurfaceFormat::Color));
    EXPECT_NO_THROW((void)TextureCube(hiDef, 4, false, SurfaceFormat::Dxt1));
    EXPECT_THROW((void)TextureCube(hiDef, 6, false, SurfaceFormat::Dxt1),
                 System::ArgumentException);
}

TEST(GraphicsProfileResourceCeilingTest, RenderTargetsReuseTextureProfileShapeLimits)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    PresentationParameters parameters;
    {
        GraphicsDevice reach(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::Reach, parameters);
        EXPECT_NO_THROW((void)RenderTarget2D(reach, 3, 5));
        EXPECT_THROW(
            (void)RenderTarget2D(reach, 3, 5, true, SurfaceFormat::Color, DepthFormat::None),
            System::NotSupportedException);
        EXPECT_THROW((void)RenderTarget2D(reach, 2049, 2), System::NotSupportedException);
        EXPECT_THROW(
            (void)RenderTargetCube(
                reach, 3, false, SurfaceFormat::Color, DepthFormat::None),
            System::NotSupportedException);
        EXPECT_THROW(
            (void)RenderTargetCube(
                reach, 513, false, SurfaceFormat::Color, DepthFormat::None),
            System::NotSupportedException);
    }

    GraphicsDevice hiDef(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, parameters);
    EXPECT_NO_THROW(
        (void)RenderTarget2D(hiDef, 3, 5, true, SurfaceFormat::Color, DepthFormat::None));
    EXPECT_THROW((void)RenderTarget2D(hiDef, 4096, 1), System::NotSupportedException);
    EXPECT_NO_THROW(
        (void)RenderTargetCube(
            hiDef, 3, true, SurfaceFormat::Color, DepthFormat::None));
}

TEST(GraphicsProfileResourceCeilingTest, RenderTargetsSubstituteProfileUnsupportedPreferredFormats)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    PresentationParameters parameters;
    {
        GraphicsDevice reach(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::Reach, parameters);
        RenderTarget2D target2D(
            reach, 2, 2, false, SurfaceFormat::Single, DepthFormat::None);
        RenderTargetCube targetCube(
            reach, 2, false, SurfaceFormat::Single, DepthFormat::None);
        EXPECT_EQ(target2D.getFormatProperty(), SurfaceFormat::Color);
        EXPECT_EQ(targetCube.getFormatProperty(), SurfaceFormat::Color);
        EXPECT_FALSE(reach.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Single));
    }

    GraphicsDevice hiDef(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, parameters);
    ASSERT_TRUE(hiDef.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Single));
    RenderTarget2D target2D(
        hiDef, 2, 2, false, SurfaceFormat::Single, DepthFormat::None);
    RenderTargetCube targetCube(
        hiDef, 2, false, SurfaceFormat::Single, DepthFormat::None);
    EXPECT_EQ(target2D.getFormatProperty(), SurfaceFormat::Single);
    EXPECT_EQ(targetCube.getFormatProperty(), SurfaceFormat::Single);
}

TEST(GraphicsProfileResourceCeilingTest, VertexBuffersRespectTheSharedSixtyFourMiBMinusOneLimit)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    PresentationParameters parameters;
    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, parameters);
    const VertexDeclaration declaration(4, {
        VertexElement(0, VertexElementFormat::Single, VertexElementUsage::Position, 0),
    });
    constexpr int largestFourByteVertexCount = 16'777'215;

    EXPECT_NO_THROW((void)VertexBuffer(
        device, declaration, largestFourByteVertexCount, BufferUsage::None));
    EXPECT_THROW((void)VertexBuffer(
                     device, declaration, largestFourByteVertexCount + 1, BufferUsage::None),
                 System::NotSupportedException);
    EXPECT_THROW((void)DynamicVertexBuffer(
                     device, declaration, largestFourByteVertexCount + 1, BufferUsage::None),
                 System::NotSupportedException);
}

TEST(GraphicsProfileResourceCeilingTest, IndexBuffersRespectTheSharedSixtyFourMiBMinusOneLimit)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    PresentationParameters parameters;
    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, parameters);
    constexpr int largestSixteenBitIndexCount = 33'554'431;
    constexpr int largestThirtyTwoBitIndexCount = 16'777'215;

    EXPECT_NO_THROW((void)IndexBuffer(
        device, IndexElementSize::SixteenBits, largestSixteenBitIndexCount, BufferUsage::None));
    EXPECT_THROW((void)IndexBuffer(
                     device, IndexElementSize::SixteenBits,
                     largestSixteenBitIndexCount + 1, BufferUsage::None),
                 System::NotSupportedException);
    EXPECT_NO_THROW((void)IndexBuffer(
        device, IndexElementSize::ThirtyTwoBits,
        largestThirtyTwoBitIndexCount, BufferUsage::None));
    EXPECT_THROW((void)DynamicIndexBuffer(
                     device, IndexElementSize::ThirtyTwoBits,
                     largestThirtyTwoBitIndexCount + 1, BufferUsage::None),
                 System::NotSupportedException);
}

TEST(GraphicsProfileResourceCeilingTest, ThirtyTwoBitIndicesAreHiDefOnly)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    PresentationParameters parameters;
    GraphicsDevice reach(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::Reach, parameters);
    GraphicsDevice hiDef(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, parameters);

    EXPECT_THROW((void)IndexBuffer(
                     reach, IndexElementSize::ThirtyTwoBits, 1, BufferUsage::None),
                 System::NotSupportedException);
    EXPECT_THROW((void)DynamicIndexBuffer(
                     reach, IndexElementSize::ThirtyTwoBits, 1, BufferUsage::None),
                 System::NotSupportedException);
    EXPECT_NO_THROW((void)IndexBuffer(
        hiDef, IndexElementSize::ThirtyTwoBits, 1, BufferUsage::None));
}

TEST(GraphicsProfileResourceCeilingTest, IndexElementSizeEnumConstructorCanonicalizesNonSixteenValues)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    PresentationParameters parameters;
    GraphicsDevice reach(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::Reach, parameters);
    GraphicsDevice hiDef(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, parameters);
    constexpr auto invalidSize = static_cast<IndexElementSize>(99);

    IndexBuffer staticBuffer(hiDef, invalidSize, 1, BufferUsage::None);
    DynamicIndexBuffer dynamicBuffer(hiDef, invalidSize, 1, BufferUsage::None);
    EXPECT_EQ(IndexElementSize::ThirtyTwoBits,
              staticBuffer.getIndexElementSizeProperty());
    EXPECT_EQ(IndexElementSize::ThirtyTwoBits,
              dynamicBuffer.getIndexElementSizeProperty());

    EXPECT_THROW((void)IndexBuffer(reach, invalidSize, 1, BufferUsage::None),
                 System::NotSupportedException);
    EXPECT_THROW((void)DynamicIndexBuffer(reach, invalidSize, 1, BufferUsage::None),
                 System::NotSupportedException);
}
