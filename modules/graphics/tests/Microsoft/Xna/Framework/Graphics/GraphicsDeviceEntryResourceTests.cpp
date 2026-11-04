// SPDX-License-Identifier: MS-PL
// TQ-048: independent Refresh entry-point contracts.
#include <gtest/gtest.h>
#include <typeinfo>
#include <string>
#include <vector>
#include <limits>
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "CNA/RendererTestGate.hpp"

using namespace Microsoft::Xna::Framework::Graphics;
namespace
{
    // Only the diagnostic body is XNA-owned. sharp-runtime selects the Framework/Core
    // parameter suffix; remove that suffix, without manufacturing an expected CNA message.
    [[maybe_unused]] std::string DiagnosticBody(const System::Exception& error)
    {
        std::string message = error.getMessageProperty();
        auto pos = message.find("\nParameter name:");
        if (pos == std::string::npos) pos = message.find(" (Parameter '");
        return message.substr(0, pos);
    }
    template<class E, class F>
    void ExpectArgument(const char* parameter, const char* body, F action)
    {
        try { action(); ADD_FAILURE() << "call accepted: " << parameter; }
        catch (const E& e)
        {
            EXPECT_EQ(typeid(e), typeid(E));
            EXPECT_EQ(e.getParamNameProperty(), parameter);
            EXPECT_EQ(DiagnosticBody(e), body);
        }
    }
}

TEST(GraphicsDeviceEntryResourceTest, AllCollectionBoundsKeepTheIndexParameter)
{
    GraphicsDevice device;
    for (GraphicsProfile profile : {GraphicsProfile::Reach, GraphicsProfile::HiDef})
    {
        device.SetGraphicsProfileEXT(profile);
        auto textures = [&](auto& collection, int size) {
            const auto& readOnly = collection;
            for (int index : {-1, size, std::numeric_limits<int>::max()})
            {
                ExpectArgument<System::ArgumentOutOfRangeException>("index", "Specified argument was out of the range of valid values.", [&] { (void)readOnly[index]; });
                ExpectArgument<System::ArgumentOutOfRangeException>("index", "Specified argument was out of the range of valid values.", [&] { collection(index, nullptr); });
            }
            if (size > 0) { collection(size - 1, nullptr); EXPECT_EQ(readOnly[size - 1], nullptr); }
        };
        auto samplers = [&](auto& collection, int size) {
            const auto& readOnly = collection;
            for (int index : {-1, size, std::numeric_limits<int>::max()})
            {
                ExpectArgument<System::ArgumentOutOfRangeException>("index", "Specified argument was out of the range of valid values.", [&] { (void)collection[index]; });
                ExpectArgument<System::ArgumentOutOfRangeException>("index", "Specified argument was out of the range of valid values.", [&] { (void)readOnly[index]; });
                ExpectArgument<System::ArgumentOutOfRangeException>("index", "Specified argument was out of the range of valid values.", [&] { collection(index, SamplerState::PointClamp); });
            }
            if (size > 0) { collection(size - 1, SamplerState::PointClamp); EXPECT_EQ(readOnly[size - 1].getFilterProperty(), TextureFilter::Point); }
        };
        // Refresh TextureCollection::get_Item IL_001f-0031/set_Item IL_0086-0099;
        // SamplerStateCollection get_Item IL_0000-001e/set_Item IL_0000-000e.
        textures(device.getTexturesProperty(), 16);
        textures(device.getVertexTexturesProperty(), profile == GraphicsProfile::HiDef ? 4 : 0);
        samplers(device.getSamplerStatesProperty(), 16);
        samplers(device.getVertexSamplerStatesProperty(), profile == GraphicsProfile::HiDef ? 4 : 0);
    }
}
TEST(GraphicsDeviceEntryResourceTest, DisposedBindingNamesTheRuntimeTypeInsteadOfTheResourceName)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD)) GTEST_SKIP();
    Texture2D texture(device, 2, 2);
    VertexBuffer vertex(device, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
    IndexBuffer index(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    texture.setNameProperty("not-the-runtime-type");
    vertex.setNameProperty("not-the-runtime-type");
    index.setNameProperty("not-the-runtime-type");
    texture.Dispose(); vertex.Dispose(); index.Dispose();
    auto expectTypeName = [&](const char* name, auto action) {
        try { action(); ADD_FAILURE(); }
        catch (const System::ObjectDisposedException& e)
        {
            EXPECT_EQ(typeid(e), typeid(System::ObjectDisposedException));
            EXPECT_EQ(e.getObjectNameProperty(), name);
        }
    };
    // Framework Helpers::CheckDisposed IL_000d-001d uses GetType().Name;
    // graphics set_Indices IL_0018-0026, SetVertexBuffers IL_0067-007b,
    // TextureCollection set_Item IL_0025-0039 call it before touching a slot.
    expectTypeName("Texture2D", [&] { device.getTexturesProperty()(0, &texture); });
    expectTypeName("VertexBuffer", [&] { device.SetVertexBuffer(&vertex); });
    expectTypeName("IndexBuffer", [&] { device.setIndicesProperty(&index); });
    EXPECT_EQ(device.getTexturesProperty()[0], nullptr);
    EXPECT_TRUE(device.GetVertexBuffers().empty());
    EXPECT_EQ(device.getIndicesProperty(), nullptr);
}
TEST(GraphicsDeviceEntryResourceTest, ActiveTargetRefusalPrecedesIndexAndKeepsBothTextureStages)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Software);
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    Texture2D live(device, 2, 2, false, SurfaceFormat::Single);
    RenderTarget2D target(device, 2, 2, false, SurfaceFormat::Single, DepthFormat::None);
    device.getTexturesProperty()(0, &live);
    device.getVertexTexturesProperty()(0, &live);
    device.SetRenderTarget(&target);
    for (bool vertex : {false, true})
    {
        auto& collection = vertex ? device.getVertexTexturesProperty() : device.getTexturesProperty();
        // Refresh TextureCollection::set_Item IL_003e-0050 precedes format and index checks.
        try { collection(-1, &target); ADD_FAILURE(); }
        catch (const System::InvalidOperationException& e)
        {
            EXPECT_EQ(typeid(e), typeid(System::InvalidOperationException));
            EXPECT_EQ(e.getMessageProperty(), "The render target must not be set on the device when it is used as a texture.");
        }
        EXPECT_EQ(collection[0], &live);
    }
    device.SetRenderTarget(nullptr);
    device.getTexturesProperty()(0, &target);
    device.getVertexTexturesProperty()(0, &target);
    EXPECT_EQ(device.getTexturesProperty()[0], &target);
    EXPECT_EQ(device.getVertexTexturesProperty()[0], &target);
}
