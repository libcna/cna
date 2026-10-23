// SPDX-License-Identifier: MS-PL
//
// CNA-DEMOS-ISSUE-005: XNA's usual custom vertex embeds a Color. In CNA Color is a polymorphic
// 16-byte object, not XNA's 4-byte value, so such a structure's bytes are not the stream its
// VertexDeclaration describes. The raw declaration-driven user draws used to accept it and draw
// garbage; they now refuse it at compile time, while the packed-uint32 form keeps working.

#include <cstdint>
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTexture.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    struct XnaStyleVertex
    {
        Vector3 Position;
        float Glow;
        Color Tint;
    };

    struct PackedColorVertex
    {
        Vector3 Position;
        float Glow;
        std::uint32_t Tint;
    };

    template <typename T>
    concept RawDrawable = requires(GraphicsDevice& device, const T* vertices,
                                   const std::uint16_t* indices16, const std::uint32_t* indices32,
                                   const VertexDeclaration& declaration)
    {
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 1, declaration);
        device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, 0, 3, indices16, 0,
                                         1, declaration);
        device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, 0, 3, indices32, 0,
                                         1, declaration);
    };

    static_assert(!RawDrawable<XnaStyleVertex>,
                  "a vertex embedding the polymorphic Color must not reach the raw user draws");
    static_assert(!RawDrawable<VertexPositionNormalTangentTexture>,
                  "a built-in vertex without a typed overload carries a vtable too");
    static_assert(RawDrawable<PackedColorVertex>);
    static_assert(RawDrawable<float>);
}

TEST(UserVertexTypeRefusal, ThePackedFormHasXnasLayout)
{
    EXPECT_EQ(sizeof(PackedColorVertex), 20u);
    EXPECT_FALSE(IsUnreadableUserVertexTypeEXT<PackedColorVertex>);
    EXPECT_TRUE(IsUnreadableUserVertexTypeEXT<XnaStyleVertex>);
}
