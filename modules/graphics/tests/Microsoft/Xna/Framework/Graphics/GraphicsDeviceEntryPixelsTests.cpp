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
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "CNA/RendererTestGate.hpp"
#include <array>
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

namespace
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;
    template<class V> V Vertex(float x, float y)
    {
        if constexpr (std::is_same_v<V, VertexPositionColor>) return V(Vector3(x,y,0), Color::White);
        else if constexpr (std::is_same_v<V, VertexPositionTexture>) return V(Vector3(x,y,0), Vector2::Zero);
        else if constexpr (std::is_same_v<V, VertexPositionColorTexture>) return V(Vector3(x,y,0), Color::White, Vector2::Zero);
        else return V(Vector3(x,y,0), Vector3(0,0,1), Vector2::Zero);
    }
    template<class F> void AssertRedDraw(GraphicsDevice& device, RenderTarget2D& target, F draw)
    {
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        draw();
        device.SetRenderTarget(nullptr);
        std::array<Color, 64> pixels{};
        target.GetData(pixels.data(), 64);
        // Hand-derived: (-1,-1),(3,-1),(-1,3) covers the viewport centre. An
        // unlit BasicEffect with diffuse=(1,0,0), alpha=1, Opaque writes RGBA=(255,0,0,255).
        EXPECT_EQ(pixels[4 * 8 + 4], Color(255, 0, 0, 255));
    }
    template<class V> void AssertEveryStockRoute(GraphicsDevice& device, RenderTarget2D& target)
    {
        const std::vector<V> vertices{Vertex<V>(-1,-1), Vertex<V>(-1,-1), Vertex<V>(3,-1), Vertex<V>(-1,3)};
        const auto& declaration = V::getVertexDeclarationStatic();
        const std::vector<std::int16_t> indices16{0, 2, 0, 1};
        const std::vector<std::int32_t> indices32{0, 2, 0, 1};
        // Distinct element offsets of one catch a byte-offset interpretation or a second offset.
        for (bool explicitDeclaration : {false, true})
        {
            for (bool array : {false, true})
            {
                SCOPED_TRACE(std::string(typeid(V).name()) + (array ? " vector" : " pointer") +
                    (explicitDeclaration ? " explicit declaration" : " inferred declaration"));
                AssertRedDraw(device, target, [&] {
                    if (array && explicitDeclaration) device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 1, 1, declaration);
                    else if (array) device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 1, 1);
                    else if (explicitDeclaration) device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 1, 1, declaration);
                    else device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 1, 1);
                });
                auto indexed = [&](const auto& indices) {
                    using I = typename std::decay_t<decltype(indices)>::value_type;
                    using U = std::make_unsigned_t<I>;
                    AssertRedDraw(device, target, [&] {
                        if (array && explicitDeclaration) device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, 1, 3, indices, 1, 1, declaration);
                        else if (array) device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, 1, 3, indices, 1, 1);
                        else if (explicitDeclaration) device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices.data(), 1, 3, reinterpret_cast<const U*>(indices.data()), 1, 1, declaration);
                        else device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices.data(), 1, 3, reinterpret_cast<const U*>(indices.data()), 1, 1);
                    });
                };
                indexed(indices16);
                indexed(indices32);
            }
        }
    }
}
TEST(GraphicsDeviceEntryPixelsTest, EveryStockPointerAndArrayOverloadPaintsWithElementOffsets)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Software);
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    RenderTarget2D target(device, 8, 8);
    BasicEffect effect(device);
    effect.setDiffuseColorProperty(Vector3(1,0,0));
    effect.Apply();
    device.setBlendStateProperty(BlendState::Opaque);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    AssertEveryStockRoute<VertexPositionColor>(device, target);
    AssertEveryStockRoute<VertexPositionTexture>(device, target);
    AssertEveryStockRoute<VertexPositionColorTexture>(device, target);
    AssertEveryStockRoute<VertexPositionNormalTexture>(device, target);
}
TEST(GraphicsDeviceEntryPixelsTest, RawDeclarationAndLegacyUntypedRoutesAlsoPaint)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Software);
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    RenderTarget2D target(device, 8, 8);
    BasicEffect effect(device);
    effect.setDiffuseColorProperty(Vector3(1,0,0));
    effect.Apply();
    device.setBlendStateProperty(BlendState::Opaque);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    // A raw position/color stream: 3*4+4 = 16 bytes; literal white ABGR.
    struct Packed { float x, y, z; std::uint32_t color; };
    const Packed stream[]{{-1,-1,0,0xffffffffu},{-1,-1,0,0xffffffffu},{3,-1,0,0xffffffffu},{-1,3,0,0xffffffffu}};
    static_assert(sizeof(Packed) == 16);
    const auto& declaration = VertexPositionColor::getVertexDeclarationStatic();
    const std::uint16_t i16[]{0,2,0,1};
    const std::uint32_t i32[]{0,2,0,1};
    AssertRedDraw(device, target, [&] { device.DrawUserPrimitives(PrimitiveType::TriangleList, static_cast<const void*>(stream), 1, 1, declaration); });
    AssertRedDraw(device, target, [&] { device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, static_cast<const void*>(stream), 1, 3, i16, 1, 1, declaration); });
    AssertRedDraw(device, target, [&] { device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, static_cast<const void*>(stream), 1, 3, i32, 1, 1, declaration); });
    // Legacy no-declaration void pointers mean native VPC objects and UInt16 indices in CNA.
    // This CNAEXT adapter promise is documented on GraphicsDevice.hpp, not a managed XNA overload.
    const VertexPositionColor objects[]{Vertex<VertexPositionColor>(-1,-1), Vertex<VertexPositionColor>(-1,-1), Vertex<VertexPositionColor>(3,-1), Vertex<VertexPositionColor>(-1,3)};
    AssertRedDraw(device, target, [&] { device.DrawUserPrimitives(PrimitiveType::TriangleList, static_cast<const void*>(objects), 1, 1); });
    AssertRedDraw(device, target, [&] { device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, static_cast<const void*>(objects), 1, 3, static_cast<const void*>(i16), 1, 1); });
}

// XNA BasicEffect.OnApply IL_0123-012e folds VertexColorEnabled into the ShaderIndex the pass
// applies; the vertex layout of the draw does not change that choice. The untyped CNAEXT routes
// read native VertexPositionColor objects like the typed overload and keep the applied selection
// too: green vertices under a white unlit effect draw white unless VertexColorEnabled is set.
TEST(GraphicsDeviceEntryPixelsTest, UntypedRoutesKeepTheAppliedVertexColorSelection)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Software);
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    RenderTarget2D target(device, 8, 8);
    BasicEffect effect(device);
    effect.setDiffuseColorProperty(Vector3(1, 1, 1));
    device.setBlendStateProperty(BlendState::Opaque);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    const Color green(0, 255, 0, 255);
    const VertexPositionColor objects[]{
        VertexPositionColor(Vector3(-1, -1, 0), green), VertexPositionColor(Vector3(-1, -1, 0), green),
        VertexPositionColor(Vector3(3, -1, 0), green), VertexPositionColor(Vector3(-1, 3, 0), green)};
    const std::uint16_t i16[]{0, 2, 0, 1};
    const auto centre = [&](auto draw)
    {
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        draw();
        device.SetRenderTarget(nullptr);
        std::array<Color, 64> pixels{};
        target.GetData(pixels.data(), 64);
        return pixels[4 * 8 + 4];
    };
    for (const bool vertexColor : {false, true})
    {
        SCOPED_TRACE(vertexColor ? "VertexColorEnabled" : "VertexColorEnabled=false");
        effect.VertexColorEnabled = vertexColor;
        effect.Apply();
        const Color expected = vertexColor ? green : Color(255, 255, 255, 255);
        EXPECT_EQ(centre([&] { device.DrawUserPrimitives(PrimitiveType::TriangleList, objects, 1, 1); }), expected)
            << "typed";
        EXPECT_EQ(centre([&] { device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                                         static_cast<const void*>(objects), 1, 1); }), expected)
            << "untyped";
        EXPECT_EQ(centre([&] { device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList,
                                                                static_cast<const void*>(objects), 1, 3,
                                                                static_cast<const void*>(i16), 1, 1); }), expected)
            << "untyped indexed";
    }
}
