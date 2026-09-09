// SPDX-License-Identifier: MS-PL
// SOFTWARE-206: XNA validates device-dependent VertexDeclaration limits when the declaration
// binds, rather than rejecting profile-neutral declarations in their constructors.

#include <cstdint>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

using namespace CNA::Testing::Renderers;

#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "System/ArgumentException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer;
using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

namespace
{
    GraphicsDevice MakeDevice(GraphicsProfile profile)
    {
        PresentationParameters parameters;
        return GraphicsDevice(
            GraphicsAdapter::getDefaultAdapterProperty(), profile, parameters);
    }

    VertexDeclaration SingleElement(VertexElementFormat format, int usageIndex = 0)
    {
        const int stride = format == VertexElementFormat::HalfVector4 ? 8 : 4;
        return VertexDeclaration(stride, {
            VertexElement(0, format, VertexElementUsage::Position, usageIndex),
        });
    }
}

TEST(VertexDeclarationProfileTest, ReachAcceptsEveryReachVertexFormat)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    auto device = MakeDevice(GraphicsProfile::Reach);

    for (int ordinal = static_cast<int>(VertexElementFormat::Single);
         ordinal <= static_cast<int>(VertexElementFormat::NormalizedShort4); ++ordinal)
    {
        const auto format = static_cast<VertexElementFormat>(ordinal);
        const int stride = format == VertexElementFormat::Vector2 ||
                           format == VertexElementFormat::Short4 ||
                           format == VertexElementFormat::NormalizedShort4 ? 8
                         : format == VertexElementFormat::Vector3 ? 12
                         : format == VertexElementFormat::Vector4 ? 16
                                                                  : 4;
        const VertexDeclaration declaration(stride, {
            VertexElement(0, format, VertexElementUsage::Position, 0),
        });
        EXPECT_NO_THROW((void)VertexBuffer(device, declaration, 1, BufferUsage::None))
            << "format ordinal " << ordinal;
    }
}

TEST(VertexDeclarationProfileTest, ReachRejectsHalfFormatsThatHiDefAccepts)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    auto reach = MakeDevice(GraphicsProfile::Reach);
    auto hiDef = MakeDevice(GraphicsProfile::HiDef);

    for (const VertexElementFormat format : {
             VertexElementFormat::HalfVector2, VertexElementFormat::HalfVector4})
    {
        const VertexDeclaration declaration = SingleElement(format);
        EXPECT_THROW((void)VertexBuffer(reach, declaration, 1, BufferUsage::None),
                     System::NotSupportedException);
        EXPECT_NO_THROW((void)VertexBuffer(hiDef, declaration, 1, BufferUsage::None));
    }
}

TEST(VertexDeclarationProfileTest, DynamicVertexBufferUsesTheSameProfileValidation)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    auto device = MakeDevice(GraphicsProfile::Reach);
    const VertexDeclaration declaration = SingleElement(VertexElementFormat::HalfVector2);

    EXPECT_THROW((void)DynamicVertexBuffer(device, declaration, 1, BufferUsage::None),
                 System::NotSupportedException);
}

TEST(VertexDeclarationProfileTest, BindRejectsStrideAboveTheXnaLimit)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    auto device = MakeDevice(GraphicsProfile::HiDef);
    const VertexDeclaration declaration(256, {
        VertexElement(0, VertexElementFormat::Single, VertexElementUsage::Position, 0),
    });

    EXPECT_THROW((void)VertexBuffer(device, declaration, 1, BufferUsage::None),
                 System::NotSupportedException);
}

TEST(VertexDeclarationProfileTest, BindRejectsMoreThanSixteenElements)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    auto device = MakeDevice(GraphicsProfile::HiDef);
    std::vector<VertexElement> elements;
    for (int index = 0; index < 16; ++index)
    {
        elements.emplace_back(index * 4, VertexElementFormat::Single,
                              VertexElementUsage::Position, index);
    }
    elements.emplace_back(64, VertexElementFormat::Single, VertexElementUsage::Color, 0);
    const VertexDeclaration declaration(68, std::move(elements));

    EXPECT_THROW((void)VertexBuffer(device, declaration, 1, BufferUsage::None),
                 System::NotSupportedException);
}

TEST(VertexDeclarationProfileTest, BindRejectsUsageIndicesOutsideZeroThroughFifteen)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    auto device = MakeDevice(GraphicsProfile::HiDef);

    for (const int usageIndex : {-1, 16})
    {
        const VertexDeclaration declaration =
            SingleElement(VertexElementFormat::Single, usageIndex);
        EXPECT_THROW((void)VertexBuffer(device, declaration, 1, BufferUsage::None),
                     System::ArgumentException)
            << "usage index " << usageIndex;
    }
}

TEST(VertexDeclarationProfileTest, BindRejectsUnknownFormatsAfterConstruction)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    auto device = MakeDevice(GraphicsProfile::HiDef);
    const VertexDeclaration declaration(4, {
        VertexElement(0, static_cast<VertexElementFormat>(12),
                      VertexElementUsage::Position, 0),
    });

    EXPECT_THROW((void)VertexBuffer(device, declaration, 1, BufferUsage::None),
                 System::NotSupportedException);
}

TEST(VertexDeclarationProfileTest, DisposedDeclarationCannotBindToVertexBuffers)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    auto device = MakeDevice(GraphicsProfile::HiDef);
    VertexDeclaration declaration = SingleElement(VertexElementFormat::Single);
    declaration.Dispose();

    EXPECT_THROW((void)VertexBuffer(device, declaration, 1, BufferUsage::None),
                 System::ObjectDisposedException);
    EXPECT_THROW((void)DynamicVertexBuffer(device, declaration, 1, BufferUsage::None),
                 System::ObjectDisposedException);
}

TEST(VertexDeclarationProfileTest, DrawUserValidatesProfileBeforeReadingVertexData)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    auto device = MakeDevice(GraphicsProfile::Reach);
    BasicEffect effect(device);
    effect.Apply();
    const VertexDeclaration declaration = SingleElement(VertexElementFormat::HalfVector2);

    EXPECT_THROW(device.DrawUserPrimitives(
                     PrimitiveType::TriangleList, static_cast<const void*>(nullptr), 0, 1,
                     declaration),
                 System::NotSupportedException);
}

TEST(VertexDeclarationProfileTest, DrawUserIndexedValidatesDisposedDeclarationBeforeReadingData)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    auto device = MakeDevice(GraphicsProfile::HiDef);
    BasicEffect effect(device);
    effect.Apply();
    VertexDeclaration declaration = SingleElement(VertexElementFormat::Single);
    declaration.Dispose();

    EXPECT_THROW(device.DrawUserIndexedPrimitives(
                     PrimitiveType::TriangleList, static_cast<const void*>(nullptr), 0, 3,
                     static_cast<const std::uint16_t*>(nullptr), 0, 1, declaration),
                 System::ObjectDisposedException);
}

TEST(VertexDeclarationProfileTest, CnaExtensionEmptyDeclarationStillSupportsLegacyBufferPath)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    auto device = MakeDevice(GraphicsProfile::Reach);

    EXPECT_NO_THROW((void)VertexBuffer(device, 1));
}
