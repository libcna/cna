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
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include <type_traits>

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

TEST(GraphicsDeviceEntryDrawTest, BufferedNumericChecksNameTheFirstBadParameter)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD)) GTEST_SKIP();
    for (int value : {0, -1})
    {
        // Refresh DrawPrimitives IL_0013-0026; DrawIndexedPrimitives IL_0015-003e;
        // DrawInstancedPrimitives IL_0015-007d.
        ExpectArgument<System::ArgumentOutOfRangeException>("primitiveCount",
            "When drawing, at least one primitive must be drawn.", [&] {
            device.DrawPrimitives(PrimitiveType::TriangleList, -1, value);
        });
        ExpectArgument<System::ArgumentOutOfRangeException>("numVertices",
            "When drawing indexed primitives, the number of vertices passed in must be greater than zero.", [&] {
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, -1, -1, value, -1, 0);
        });
        ExpectArgument<System::ArgumentOutOfRangeException>("primitiveCount",
            "When drawing, at least one primitive must be drawn.", [&] {
            device.DrawInstancedPrimitives(PrimitiveType::TriangleList, -1, -1, 3, -1, value, 0);
        });
        ExpectArgument<System::ArgumentOutOfRangeException>("instanceCount",
            "When drawing, at least one primitive must be drawn.", [&] {
            device.DrawInstancedPrimitives(PrimitiveType::TriangleList, -1, -1, 3, -1, 1, value);
        });
    }
}
namespace
{
    template<class V> void CheckPointerErrors(GraphicsDevice& device)
    {
        const V* absent = nullptr;
        const auto& declaration = V::getVertexDeclarationStatic();
        const V data[3]{};
        // C++ object-to-stream adapter safety contract, GraphicsDevice.hpp explicit overloads.
        // PositionColor is 16 bytes on the GPU; PositionTexture is 20, then 24/32 for the others.
        const auto& wrongDeclaration = std::is_same_v<V, VertexPositionColor>
            ? VertexPositionTexture::getVertexDeclarationStatic() : VertexPositionColor::getVertexDeclarationStatic();
        ExpectArgument<System::ArgumentException>("vertexDeclaration",
            "The VertexDeclaration's VertexStride is not the GPU vertex stream stride of the supplied vertex type.", [&] {
            device.DrawUserPrimitives(PrimitiveType::TriangleList, data, 0, 1, wrongDeclaration);
        });
        const std::uint16_t indices16[3]{0, 1, 2};
        const std::uint32_t indices32[3]{0, 1, 2};
        for (bool explicitDeclaration : {false, true})
        {
            auto draw = [&] {
                if (explicitDeclaration)
                    device.DrawUserPrimitives(PrimitiveType::TriangleList, absent, -1, 0, declaration);
                else device.DrawUserPrimitives(PrimitiveType::TriangleList, absent, -1, 0);
            };
            ExpectArgument<System::ArgumentNullException>("vertexData",
                "This method does not accept null for this parameter.", draw);
            const char* offsetBody = "The offset must be within the valid range for this resource.";
            for (int offset : {-1, std::numeric_limits<int>::min()})
            {
                ExpectArgument<System::ArgumentOutOfRangeException>("vertexOffset", offsetBody, [&] {
                    if (explicitDeclaration) device.DrawUserPrimitives(PrimitiveType::TriangleList, data, offset, 1, declaration);
                    else device.DrawUserPrimitives(PrimitiveType::TriangleList, data, offset, 1);
                });
            }
            auto indexed = [&](const auto* vertices, const auto* indices) {
                if (explicitDeclaration)
                    device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, -1, 0, indices, -1, 0, declaration);
                else device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, -1, 0, indices, -1, 0);
            };
            auto checkRanges = [&](const auto* indices) {
                if (explicitDeclaration)
                    ExpectArgument<System::ArgumentException>("vertexDeclaration",
                        "The VertexDeclaration's VertexStride is not the GPU vertex stream stride of the supplied vertex type.", [&] {
                        device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, data, 0, 3, indices, 0, 1, wrongDeclaration);
                    });
                auto withOffsets = [&](int vo, int io) {
                    if (explicitDeclaration) device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, data, vo, 3, indices, io, 1, declaration);
                    else device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, data, vo, 3, indices, io, 1);
                };
                for (int offset : {-1, std::numeric_limits<int>::min()})
                {
                    ExpectArgument<System::ArgumentOutOfRangeException>("vertexOffset", offsetBody, [&] { withOffsets(offset, -1); });
                    ExpectArgument<System::ArgumentOutOfRangeException>("indexOffset", offsetBody, [&] { withOffsets(0, offset); });
                }
                // CNA pointer safety contract: INT_MAX element offset cannot fit its signed
                // byte-count channel, independent of actual pointer contents. No source read.
                ExpectArgument<System::ArgumentOutOfRangeException>("indexOffset",
                    "The requested source byte range is too large.", [&] { withOffsets(0, std::numeric_limits<int>::max()); });
            };
            checkRanges(indices16); checkRanges(indices32);
            // Refresh shared indexed core IL_0015-0035 and IL_01b2-01d1.
            ExpectArgument<System::ArgumentNullException>("vertexData",
                "This method does not accept null for this parameter.", [&] { indexed(absent, indices16); });
            ExpectArgument<System::ArgumentNullException>("vertexData",
                "This method does not accept null for this parameter.", [&] { indexed(absent, indices32); });
            ExpectArgument<System::ArgumentNullException>("indexData",
                "This method does not accept null for this parameter.", [&] { indexed(data, static_cast<const std::uint16_t*>(nullptr)); });
            ExpectArgument<System::ArgumentNullException>("indexData",
                "This method does not accept null for this parameter.", [&] { indexed(data, static_cast<const std::uint32_t*>(nullptr)); });
        }
    }
}
TEST(GraphicsDeviceEntryDrawTest, NullPrecedenceAndDiagnosticAcrossEveryStockPointerType)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD)) GTEST_SKIP();
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    CheckPointerErrors<VertexPositionColor>(device);
    CheckPointerErrors<VertexPositionTexture>(device);
    CheckPointerErrors<VertexPositionColorTexture>(device);
    CheckPointerErrors<VertexPositionNormalTexture>(device);
}
TEST(GraphicsDeviceEntryDrawTest, ProfileStateErrorsPrecedeMissingBuffers)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD)) GTEST_SKIP();
    device.SetGraphicsProfileEXT(GraphicsProfile::Reach);
    Texture2D texture(device, 3, 5);
    // Establish a compatible declaration, then remove data; XNA retains the last declaration
    // when SetVertexBuffers receives zero streams (IL_01e9-01fa).
    VertexBuffer vertex(device, VertexPositionTexture::getVertexDeclarationStatic(), 3, BufferUsage::None);
    device.SetVertexBuffer(&vertex);
    device.SetVertexBuffer(nullptr);
    BasicEffect effect(device);
    effect.setTextureEnabledProperty(true);
    effect.setTextureProperty(&texture);
    effect.Apply();
    const char* expected = "XNA Framework Reach profile requires TextureAddressMode to be Clamp when using texture sizes that are not powers of two.";
    // VerifyCanDraw IL_01ad-01df checks NPOT before buffer requirements IL_01e0-020c.
    for (int route = 0; route < 3; ++route)
    {
        SCOPED_TRACE(route);
        try
        {
            if (route == 0) device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
            if (route == 1) device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1);
            if (route == 2) device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1, 1);
            ADD_FAILURE() << "invalid state accepted";
        }
        catch (const System::NotSupportedException& e) { EXPECT_EQ(e.getMessageProperty(), expected); }
    }
}
TEST(GraphicsDeviceEntryDrawTest, BlendFormatPrecedesTextureFilterError)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD)) GTEST_SKIP();
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    Texture2D texture(device, 2, 2, false, SurfaceFormat::Single);
    RenderTarget2D target(device, 4, 4, false, SurfaceFormat::Single, DepthFormat::None);
    device.SetRenderTarget(&target);
    device.setBlendStateProperty(BlendState::AlphaBlend);
    BasicEffect effect(device);
    effect.setTextureEnabledProperty(true);
    effect.setTextureProperty(&texture);
    effect.Apply();
    const VertexPositionTexture data[3]{};
    // VerifyCanDraw IL_00f3-0121 (blend) before IL_0122-01ac (filter).
    try
    {
        device.DrawUserPrimitives(PrimitiveType::TriangleList, data, 0, 1);
        ADD_FAILURE() << "invalid blend and filter accepted";
    }
    catch (const System::NotSupportedException& e)
    {
        EXPECT_EQ(e.getMessageProperty(), "XNA Framework HiDef profile does not support alpha blending or ColorWriteChannels when using rendertarget format Single.");
    }
    EXPECT_TRUE(device.GetVertexBuffers().empty());
    device.SetRenderTarget(nullptr);
}
