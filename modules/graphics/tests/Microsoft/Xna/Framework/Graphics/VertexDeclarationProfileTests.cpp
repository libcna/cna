// SPDX-License-Identifier: MS-PL
// SOFTWARE-206: XNA validates device-dependent VertexDeclaration limits when the declaration
// binds, rather than rejecting profile-neutral declarations in their constructors.

#include <cstdint>
#include <memory>
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
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/ArgumentException.hpp"
#include "System/EventArgs.hpp"
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
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

namespace
{
    class TestTag final : public System::Object
    {
    public:
        [[nodiscard]] const std::string& GetTypeName() const override
        {
            static const std::string name = "TestTag";
            return name;
        }
    };

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

TEST(VertexDeclarationProfileTest, VertexBufferRetainsTheDeclarationResourceIdentity)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    auto device = MakeDevice(GraphicsProfile::HiDef);
    VertexDeclaration declaration = SingleElement(VertexElementFormat::Single);
    TestTag firstTag;
    TestTag secondTag;
    declaration.setNameProperty("before");
    declaration.setTagProperty(&firstTag);

    VertexBuffer buffer(device, declaration, 1, BufferUsage::None);
    VertexDeclaration& retained = buffer.getVertexDeclarationProperty();

    EXPECT_EQ(declaration.getGraphicsDeviceProperty(), &device);
    EXPECT_EQ(retained.getGraphicsDeviceProperty(), &device);
    EXPECT_EQ(retained.getNameProperty(), "before");
    EXPECT_EQ(retained.getTagProperty(), &firstTag);

    declaration.setNameProperty("after");
    declaration.setTagProperty(&secondTag);
    EXPECT_EQ(retained.getNameProperty(), "after");
    EXPECT_EQ(retained.getTagProperty(), &secondTag);

    int sourceDisposingCount = 0;
    int retainedDisposingCount = 0;
    System::Object* disposingSender = nullptr;
    declaration.Disposing += [&](System::Object* sender, const System::EventArgs&)
    {
        ++sourceDisposingCount;
        disposingSender = sender;
    };
    retained.Disposing += [&](System::Object* sender, const System::EventArgs&)
    {
        ++retainedDisposingCount;
        disposingSender = sender;
    };
    retained.Dispose();
    EXPECT_TRUE(declaration.getIsDisposedProperty());
    EXPECT_TRUE(retained.getIsDisposedProperty());
    EXPECT_EQ(sourceDisposingCount, 1);
    EXPECT_EQ(retainedDisposingCount, 1);
    EXPECT_EQ(disposingSender, &declaration);
}

TEST(VertexDeclarationProfileTest, RetainedDeclarationOutlivesTheSourceCppWrapper)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    auto device = MakeDevice(GraphicsProfile::HiDef);
    std::unique_ptr<VertexBuffer> buffer;
    {
        VertexDeclaration declaration = SingleElement(VertexElementFormat::Single);
        declaration.setNameProperty("temporary-scope");
        buffer = std::make_unique<VertexBuffer>(
            device, declaration, 1, BufferUsage::None);
    }

    const VertexDeclaration& retained = buffer->getVertexDeclarationProperty();
    EXPECT_EQ(retained.getGraphicsDeviceProperty(), &device);
    EXPECT_EQ(retained.getNameProperty(), "temporary-scope");
    EXPECT_FALSE(retained.getIsDisposedProperty());
    EXPECT_EQ(retained.getVertexStrideProperty(), 4);
    ASSERT_EQ(retained.GetVertexElements().size(), 1U);
    EXPECT_EQ(retained.GetVertexElements()[0].getVertexElementFormatProperty(),
              VertexElementFormat::Single);
}

TEST(VertexDeclarationProfileTest, DeclarationOwnershipRebindsAcrossDevices)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    auto firstDevice = MakeDevice(GraphicsProfile::HiDef);
    auto secondDevice = MakeDevice(GraphicsProfile::HiDef);
    VertexDeclaration declaration = SingleElement(VertexElementFormat::Single);

    VertexBuffer first(firstDevice, declaration, 1, BufferUsage::None);
    EXPECT_EQ(declaration.getGraphicsDeviceProperty(), &firstDevice);
    EXPECT_EQ(first.getVertexDeclarationProperty().getGraphicsDeviceProperty(), &firstDevice);

    {
        VertexBuffer second(secondDevice, declaration, 1, BufferUsage::None);
        EXPECT_EQ(declaration.getGraphicsDeviceProperty(), &secondDevice);
        EXPECT_EQ(first.getVertexDeclarationProperty().getGraphicsDeviceProperty(), &secondDevice);
        EXPECT_EQ(second.getVertexDeclarationProperty().getGraphicsDeviceProperty(), &secondDevice);
    }

    EXPECT_EQ(declaration.getGraphicsDeviceProperty(), &secondDevice);
    VertexBuffer rebound(firstDevice, declaration, 1, BufferUsage::None);
    EXPECT_EQ(declaration.getGraphicsDeviceProperty(), &firstDevice);
    EXPECT_EQ(first.getVertexDeclarationProperty().getGraphicsDeviceProperty(), &firstDevice);
    EXPECT_EQ(rebound.getVertexDeclarationProperty().getGraphicsDeviceProperty(), &firstDevice);
}

TEST(VertexDeclarationProfileTest, DeviceDisposalDoesNotDisposeDeclarationAliases)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    auto device = MakeDevice(GraphicsProfile::HiDef);
    VertexDeclaration declaration = SingleElement(VertexElementFormat::Single);
    VertexBuffer buffer(device, declaration, 1, BufferUsage::None);
    VertexDeclaration& retained = buffer.getVertexDeclarationProperty();
    int sourceDisposingCount = 0;
    int retainedDisposingCount = 0;
    declaration.Disposing += [&](System::Object*, const System::EventArgs&)
    {
        ++sourceDisposingCount;
    };
    retained.Disposing += [&](System::Object*, const System::EventArgs&)
    {
        ++retainedDisposingCount;
    };

    device.Dispose();

    EXPECT_FALSE(declaration.getIsDisposedProperty());
    EXPECT_FALSE(retained.getIsDisposedProperty());
    EXPECT_EQ(sourceDisposingCount, 0);
    EXPECT_EQ(retainedDisposingCount, 0);
}

TEST(VertexDeclarationProfileTest, StaticDeclarationsSurviveCompletedCppDeviceScopes)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    const VertexDeclaration& declaration = VertexPositionColor::getVertexDeclarationStatic();

    {
        auto device = MakeDevice(GraphicsProfile::HiDef);
        VertexBuffer buffer(device, declaration, 1, BufferUsage::None);
        EXPECT_EQ(declaration.getGraphicsDeviceProperty(), &device);
    }
    EXPECT_FALSE(declaration.getIsDisposedProperty());

    auto reboundDevice = MakeDevice(GraphicsProfile::HiDef);
    EXPECT_NO_THROW(VertexBuffer(reboundDevice, declaration, 1, BufferUsage::None));
    EXPECT_EQ(declaration.getGraphicsDeviceProperty(), &reboundDevice);
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
