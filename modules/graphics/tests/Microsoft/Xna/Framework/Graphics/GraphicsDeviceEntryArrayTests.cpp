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

// Refresh DrawUserPrimitives<T>(..., declaration) IL_003c-00a3; indexed core
// IL_0015-00fb and IL_0192-01d1; Int32 public overload IL_0000-0018.
namespace
{
    constexpr const char* countMessage = "When drawing, at least one primitive must be drawn.";
    constexpr const char* offsetMessage = "The offset must be within the valid range for this resource.";
    constexpr const char* indexMessage = "This parameter must be a valid index within the array.";
    constexpr const char* nullMessage = "This method does not accept null for this parameter.";
    template<class I> void CheckIndexedArrayOrder(GraphicsDevice& device, bool explicitDeclaration)
    {
        const std::vector<VertexPositionColor> vertices(3);
        const std::vector<I> indices{0, 1, 2};
        const auto& declaration = VertexPositionColor::getVertexDeclarationStatic();
        auto draw = [&](const auto& v, int vo, int nv, const auto& i, int io, int pc)
        {
            if (explicitDeclaration)
                device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, v, vo, nv, i, io, pc, declaration);
            else
                device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, v, vo, nv, i, io, pc);
        };
        const std::vector<VertexPositionColor> emptyVertices;
        const std::vector<I> emptyIndices;
        ExpectArgument<System::ArgumentNullException>("vertexData", nullMessage, [&] {
            draw(emptyVertices, -1, 0, emptyIndices, -1, 0);
        });
        ExpectArgument<System::ArgumentNullException>("indexData", nullMessage, [&] {
            draw(vertices, -1, 0, emptyIndices, -1, 0);
        });
        ExpectArgument<System::ArgumentOutOfRangeException>("numVertices",
            "When drawing indexed primitives, the number of vertices passed in must be greater than zero.", [&] {
            draw(vertices, -1, 0, indices, -1, 0);
        });
        ExpectArgument<System::ArgumentOutOfRangeException>("primitiveCount", countMessage, [&] {
            draw(vertices, -1, 3, indices, -1, 0);
        });
        for (int offset : {-1, 3, std::numeric_limits<int>::max()})
        {
            ExpectArgument<System::ArgumentOutOfRangeException>("vertexOffset", offsetMessage, [&] {
                draw(vertices, offset, 3, indices, -1, 1);
            });
            ExpectArgument<System::ArgumentOutOfRangeException>("indexOffset", offsetMessage, [&] {
                draw(vertices, 0, 4, indices, offset, 1);
            });
        }
        // Both extents are invalid: index extent wins (3*2 = 6 > 3).
        ExpectArgument<System::ArgumentOutOfRangeException>("primitiveCount", indexMessage, [&] {
            draw(vertices, 0, 4, indices, 0, 2);
        });
        // 1+3 = 4 > 3 is named vertexData, never numVertices.
        ExpectArgument<System::ArgumentOutOfRangeException>("vertexData", indexMessage, [&] {
            draw(vertices, 1, 3, indices, 0, 1);
        });
    }
}
TEST(GraphicsDeviceEntryArrayTest, NonIndexedCountOffsetAndExtentOrderInBothOverloads)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    const std::vector<VertexPositionColor> vertices(3);
    const auto& declaration = VertexPositionColor::getVertexDeclarationStatic();
    for (bool explicitDeclaration : {false, true})
    {
        auto draw = [&](const auto& v, int offset, int count) {
            if (explicitDeclaration)
                device.DrawUserPrimitives(PrimitiveType::TriangleList, v, offset, count, declaration);
            else device.DrawUserPrimitives(PrimitiveType::TriangleList, v, offset, count);
        };
        for (int count : {0, -1})
            ExpectArgument<System::ArgumentOutOfRangeException>("primitiveCount", countMessage, [&] {
                draw(vertices, -1, count);
            });
        for (int offset : {-1, 3, std::numeric_limits<int>::max()})
            ExpectArgument<System::ArgumentOutOfRangeException>("vertexOffset", offsetMessage, [&] {
                draw(vertices, offset, 1);
            });
        // Unlike the indexed core, an empty non-null vertex array reaches the offset guard.
        ExpectArgument<System::ArgumentOutOfRangeException>("vertexOffset", offsetMessage, [&] {
            draw(std::vector<VertexPositionColor>{}, 0, 1);
        });
        ExpectArgument<System::ArgumentOutOfRangeException>("primitiveCount", indexMessage, [&] {
            draw(vertices, 1, 1);
        });
    }
}
TEST(GraphicsDeviceEntryArrayTest, IndexedOrderInAllFourOverloads)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    for (bool explicitDeclaration : {false, true})
    {
        CheckIndexedArrayOrder<std::int16_t>(device, explicitDeclaration);
        CheckIndexedArrayOrder<std::int32_t>(device, explicitDeclaration);
    }
}
TEST(GraphicsDeviceEntryArrayTest, ReachWideIndicesPrecedeArrayAndDeviceChecks)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::Reach);
    const std::vector<VertexPositionColor> vertices;
    const std::vector<std::int32_t> indices;
    const auto& declaration = VertexPositionColor::getVertexDeclarationStatic();
    // The Int32 wrapper's profile gate is outside the disposed/shared-array core.
    device.Dispose();
    EXPECT_THROW(device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList,
        vertices, -1, 0, indices, -1, 0), System::NotSupportedException);
    EXPECT_THROW(device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList,
        vertices, -1, 0, indices, -1, 0, declaration), System::NotSupportedException);
}
TEST(GraphicsDeviceEntryArrayTest, DisposedDevicePrecedesArrayErrorsInCore)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    device.Dispose();
    const std::vector<VertexPositionColor> vertices;
    const std::vector<std::int16_t> indices;
    const auto& declaration = VertexPositionColor::getVertexDeclarationStatic();
    EXPECT_THROW(device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, -1, 0), System::ObjectDisposedException);
    EXPECT_THROW(device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, -1, 0, declaration), System::ObjectDisposedException);
    EXPECT_THROW(device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, -1, 0, indices, -1, 0), System::ObjectDisposedException);
    EXPECT_THROW(device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, -1, 0, indices, -1, 0, declaration), System::ObjectDisposedException);
}
