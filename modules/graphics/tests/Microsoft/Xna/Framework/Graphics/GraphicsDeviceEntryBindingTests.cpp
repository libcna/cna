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
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"

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

TEST(GraphicsDeviceEntryBindingTest, TooManyStreamsPreserveEveryExistingBinding)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD)) GTEST_SKIP();
    VertexBuffer vertex(device, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
    device.SetVertexBuffers({VertexBufferBinding(&vertex, 1, 0)});
    // Refresh SetVertexBuffers IL_0015-0039 is outside the cleanup finally.
    try { device.SetVertexBuffers(std::vector<VertexBufferBinding>(17)); ADD_FAILURE(); }
    catch (const System::NotSupportedException& e)
    {
        EXPECT_EQ(e.getMessageProperty(), "XNA Framework Reach profile supports a maximum of 16 simultaneous vertex buffers.");
    }
    ASSERT_EQ(device.GetVertexBuffers().size(), 1u);
    EXPECT_EQ(device.GetVertexBuffer(), &vertex);
    EXPECT_EQ(device.GetVertexBuffers()[0].getVertexBufferProperty(), &vertex);
    EXPECT_EQ(device.GetVertexBuffers()[0].getVertexOffsetProperty(), 1);
    EXPECT_EQ(device.GetVertexBuffers()[0].getInstanceFrequencyProperty(), 0);
    device.SetVertexBuffer(nullptr, std::numeric_limits<int>::min());
    EXPECT_EQ(device.GetVertexBuffer(), nullptr);
    EXPECT_TRUE(device.GetVertexBuffers().empty());
}
TEST(GraphicsDeviceEntryBindingTest, DisposedIndicesRefuseEvenSameIdentityAndPreserveOtherBinding)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD)) GTEST_SKIP();
    IndexBuffer live(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    IndexBuffer dead(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    device.setIndicesProperty(&live);
    dead.Dispose();
    // Refresh set_Indices IL_0015-002a checks disposal before cache equality IL_002b-0032.
    EXPECT_THROW(device.setIndicesProperty(&dead), System::ObjectDisposedException);
    EXPECT_EQ(device.getIndicesProperty(), &live);
    live.Dispose();
    EXPECT_THROW(device.setIndicesProperty(&live), System::ObjectDisposedException);
    device.setIndicesProperty(nullptr);
    EXPECT_EQ(device.getIndicesProperty(), nullptr);
}
TEST(GraphicsDeviceEntryBindingTest, TextureResourceChecksPrecedeIndexWhileSamplerIndexWins)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD)) GTEST_SKIP();
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    Texture2D live(device, 2, 2);
    Texture2D dead(device, 2, 2);
    dead.Dispose();
    device.getTexturesProperty()(0, &live);
    // TextureCollection::set_Item IL_0022-0085 before index IL_0086-0099.
    EXPECT_THROW(device.getTexturesProperty()(16, &dead), System::ObjectDisposedException);
    ExpectArgument<System::ArgumentOutOfRangeException>("index", "Specified argument was out of the range of valid values.", [&] {
        device.getTexturesProperty()(16, nullptr);
    });
    EXPECT_EQ(device.getTexturesProperty()[0], &live);
    SamplerState sampler;
    sampler.Dispose();
    // SamplerStateCollection::set_Item IL_0000-000e before value/Apply IL_0010-0038.
    EXPECT_THROW(device.getSamplerStatesProperty()(16, sampler), System::ArgumentOutOfRangeException);
    EXPECT_THROW(device.getVertexSamplerStatesProperty()(4, sampler), System::ArgumentOutOfRangeException);
    EXPECT_THROW(device.getSamplerStatesProperty()(0, sampler), System::ObjectDisposedException);
    EXPECT_EQ(device.getSamplerStatesProperty()[0].getFilterProperty(), TextureFilter::Linear);
}
TEST(GraphicsDeviceEntryBindingTest, UserArgumentRefusalPreservesBindingsButShaderRefusalClearsOnlyVertices)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD)) GTEST_SKIP();
    VertexBuffer vertex(device, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
    IndexBuffer index(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    const std::vector<VertexPositionColor> vertices(3);
    const std::vector<std::int16_t> indices{0, 1, 2};
    for (bool indexed : {false, true})
    {
        device.SetVertexBuffer(&vertex);
        device.setIndicesProperty(&index);
        auto draw = [&](int count) {
            if (indexed) device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, 0, 3, indices, 0, count);
            else device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, count);
        };
        EXPECT_THROW(draw(0), System::ArgumentOutOfRangeException);
        ASSERT_EQ(device.GetVertexBuffers().size(), 1u);
        EXPECT_EQ(device.GetVertexBuffer(), &vertex);
        EXPECT_EQ(device.getIndicesProperty(), &index);
        // BeginUserPrimitives IL_0000-005b precedes VerifyCanDraw. The indexed finally
        // clearing Indices is not entered on a VerifyCanDraw shader refusal (IL_0107 vs IL_0116).
        EXPECT_THROW(draw(1), System::InvalidOperationException);
        EXPECT_TRUE(device.GetVertexBuffers().empty());
        EXPECT_EQ(device.GetVertexBuffer(), nullptr);
        EXPECT_EQ(device.getIndicesProperty(), &index);
    }
}

TEST(GraphicsDeviceEntryBindingTest, InvalidOffsetPrecedesDeviceDisposalButValidAndNullBindingsCheckDevice)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD)) GTEST_SKIP();
    VertexBuffer vertex(device, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
    device.Dispose();
    // Refresh SetVertexBuffer(VertexBuffer,int) IL_0003-0010 constructs the binding before
    // shared SetVertexBuffers checks device disposal (IL_0000-0014). Its constructor reads
    // VertexCount and rejects offsets at IL_0013-001e/0037-0041 even on a disposed device.
    for (int offset : {-1, 3, std::numeric_limits<int>::max()})
        ExpectArgument<System::ArgumentOutOfRangeException>("vertexOffset",
            "Specified argument was out of the range of valid values.", [&] {
            device.SetVertexBuffer(&vertex, offset);
        });
    auto expectDeviceDisposed = [&](const VertexBuffer* buffer, int offset) {
        try { device.SetVertexBuffer(buffer, offset); ADD_FAILURE(); }
        catch (const System::ObjectDisposedException& e)
        {
            EXPECT_EQ(typeid(e), typeid(System::ObjectDisposedException));
            EXPECT_EQ(e.getObjectNameProperty(), "GraphicsDevice");
        }
    };
    // A valid offset reaches the core's device check before resource disposal. Null skips
    // the binding constructor entirely, so even a negative offset reaches that same check.
    expectDeviceDisposed(&vertex, 0);
    expectDeviceDisposed(nullptr, -1);
}
