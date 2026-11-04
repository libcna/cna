// SPDX-License-Identifier: MS-PL
// TQ-021: what a user-primitive draw leaves bound on the device (XNA and FNA disagree; XNA wins).
//
// Oracle: Microsoft XNA 4.0 GraphicsDevice.
//  - BeginUserPrimitives IL_0000-005b, called by every DrawUserPrimitives /
//    DrawUserIndexedPrimitives before VerifyCanDraw, resets every VertexBufferBinding slot,
//    sets currentVertexBufferCount = 0 and instanceStreamMask = 0: afterwards GetVertexBuffers()
//    is empty and a buffered draw has no data (VerifyCanDraw IL_01fa-020c,
//    InvalidOperationException).
//  - DrawUserIndexedPrimitives<T> (the 16- and 32-bit cores) clear _currentIB in their finally
//    block (IL_018a-018c): Indices is null after an indexed user draw. DrawUserPrimitives leaves
//    Indices alone.
// FNA keeps both bindings (GraphicsDevice.cs PrepareUserVertexBuffer only marks the cached
// bindings dirty), so code written against FNA that draws buffered geometry after user primitives
// must rebind, as XNA code always had to.
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <typeinfo>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/InvalidOperationException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

namespace
{
    const std::array<VertexPositionColor, 3> kTriangle{
        VertexPositionColor(Vector3(-0.5f, -0.5f, 0.0f), Color(255, 0, 0, 255)),
        VertexPositionColor(Vector3(0.5f, -0.5f, 0.0f), Color(0, 255, 0, 255)),
        VertexPositionColor(Vector3(0.0f, 0.5f, 0.0f), Color(0, 0, 255, 255)),
    };
    const std::array<std::uint16_t, 3> kIndices{0, 1, 2};

    class UserPrimitiveBindingStateTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;

        void SetUp() override
        {
            device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
            if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD))
                GTEST_SKIP() << "renderer has no 3D pipeline";
        }
    };
}

TEST_F(UserPrimitiveBindingStateTest, UserDrawUnbindsVertexStreamsButKeepsIndices)
{
    VertexBuffer vertices(device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                          BufferUsage::None);
    vertices.SetData(kTriangle.data(), 3);
    IndexBuffer indices(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    indices.SetData(kIndices.data(), 3);
    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    effect.Apply();
    device.SetVertexBuffer(&vertices);
    device.SetIndexBuffer(&indices);

    device.DrawUserPrimitives(PrimitiveType::TriangleList, kTriangle.data(), 0, 1);

    EXPECT_TRUE(device.GetVertexBuffers().empty());
    EXPECT_EQ(device.getIndicesProperty(), &indices);
    try
    {
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        ADD_FAILURE() << "a buffered draw found the vertex buffer a user draw had unbound";
    }
    catch (const System::InvalidOperationException& exception)
    {
        EXPECT_EQ(typeid(exception), typeid(System::InvalidOperationException));
    }
}

TEST_F(UserPrimitiveBindingStateTest, IndexedUserDrawUnbindsVertexStreamsAndIndices)
{
    VertexBuffer vertices(device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                          BufferUsage::None);
    vertices.SetData(kTriangle.data(), 3);
    IndexBuffer indices(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    indices.SetData(kIndices.data(), 3);
    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    effect.Apply();
    device.SetVertexBuffer(&vertices);
    device.SetIndexBuffer(&indices);

    device.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, kTriangle.data(), 0, 3, kIndices.data(), 0, 1);

    EXPECT_TRUE(device.GetVertexBuffers().empty());
    EXPECT_EQ(device.getIndicesProperty(), nullptr);
}
