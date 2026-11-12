// SPDX-License-Identifier: MS-PL
// TQ-021: which draw route a set of vertex streams may use.
//
// Oracle: Microsoft XNA 4.0 GraphicsDevice.
//  - SetVertexBuffers(VertexBufferBinding*, int) IL_0150-0172 keeps instanceStreamMask: bit i is
//    set exactly when binding i has a non-zero InstanceFrequency; IL_0180-01df clears the bits of
//    slots no longer bound.
//  - DrawPrimitives IL_0055-0067 and DrawIndexedPrimitives IL_006c-007e: after VerifyCanDraw, a
//    non-zero instanceStreamMask throws InvalidOperationException (NonZeroInstanceFrequency).
//  - DrawInstancedPrimitives IL_00b0-00da: after VerifyCanDraw, instanceStreamMask == 0 (no
//    per-instance stream) or instanceStreamMask == (1 << count) - 1 (no per-vertex stream) throws
//    InvalidOperationException (InvalidInstanceStreams, IL_036f-0374).
// FNA performs none of these checks; XNA wins.
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <typeinfo>
#include <string>
#include <stdexcept>

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
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
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
using Microsoft::Xna::Framework::Graphics::VertexBufferBinding;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

namespace
{
    // Refresh FrameworkResources, selected by DrawPrimitives IL_005d-0067 and
    // DrawInstancedPrimitives IL_036f-0374.
    constexpr const char* NonZeroInstanceFrequency =
        "Non-instanced draw calls are not valid when a vertex buffer is bound with a non-zero "
        "instance frequency.";
    constexpr const char* InvalidInstanceStreams =
        "DrawInstancedPrimitives requires at least one vertex buffer to be bound with a non-zero "
        "instance frequency, and also at least one with a zero instance frequency.";

    struct MatrixRecord
    {
        float c0[4];
        float c1[4];
        float c2[4];
        float c3[4];
    };
    static_assert(sizeof(MatrixRecord) == 64);

    // The per-instance world-matrix layout CNA's stock instancing route reads.
    VertexDeclaration InstanceMatrixDeclaration()
    {
        return VertexDeclaration(
            64,
            {
                VertexElement(0, VertexElementFormat::Vector4,
                              VertexElementUsage::TextureCoordinate, 1),
                VertexElement(16, VertexElementFormat::Vector4,
                              VertexElementUsage::TextureCoordinate, 2),
                VertexElement(32, VertexElementFormat::Vector4,
                              VertexElementUsage::TextureCoordinate, 3),
                VertexElement(48, VertexElementFormat::Vector4,
                              VertexElementUsage::TextureCoordinate, 4),
            });
    }

    template<typename TCallable>
    void ExpectExactInvalidOperation(TCallable&& draw, const char* what, const char* message)
    {
        try
        {
            draw();
            ADD_FAILURE() << what << " was drawn";
        }
        catch (const System::InvalidOperationException& exception)
        {
            EXPECT_EQ(typeid(exception), typeid(System::InvalidOperationException)) << what;
            EXPECT_EQ(std::string(exception.what()), message) << what;
        }
        catch (...)
        {
            ADD_FAILURE() << what << " threw something other than InvalidOperationException";
        }
    }

    class InstanceStreamDrawRuleTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;
        std::unique_ptr<VertexBuffer> mesh;
        std::unique_ptr<VertexBuffer> instances;
        std::unique_ptr<IndexBuffer> indices;
        std::unique_ptr<BasicEffect> effect;

        void SetUp() override
        {
            device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
            if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD) ||
                !device.SupportsCapability(CNA::GraphicsCapability::Instancing))
            {
                GTEST_SKIP() << "renderer has no instanced 3D pipeline";
            }

            const std::array<VertexPositionColor, 3> triangle{
                VertexPositionColor(Vector3(-0.5f, -0.5f, 0.0f), Color(255, 0, 0, 255)),
                VertexPositionColor(Vector3(0.5f, -0.5f, 0.0f), Color(0, 255, 0, 255)),
                VertexPositionColor(Vector3(0.0f, 0.5f, 0.0f), Color(0, 0, 255, 255)),
            };
            mesh = std::make_unique<VertexBuffer>(
                device, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
            mesh->SetData(triangle.data(), 3);

            MatrixRecord identity{};
            identity.c0[0] = 1.0f;
            identity.c1[1] = 1.0f;
            identity.c2[2] = 1.0f;
            identity.c3[3] = 1.0f;
            instances = std::make_unique<VertexBuffer>(
                device, InstanceMatrixDeclaration(), 1, BufferUsage::None);
            instances->SetDataRaw(&identity, 1, 64);

            const std::array<std::uint16_t, 3> triangleIndices{0, 1, 2};
            indices = std::make_unique<IndexBuffer>(
                device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
            indices->SetData(triangleIndices.data(), 3);
            device.SetIndexBuffer(indices.get());

            effect = std::make_unique<BasicEffect>(device);
            effect->VertexColorEnabled = true;
            effect->Apply();
        }

        void DrawOrdinary()
        {
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        }

        void DrawIndexed()
        {
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1);
        }

        void DrawInstanced()
        {
            device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1, 1);
        }

        // The legal instanced shape passes XNA's stream validation. FNA3D then refuses
        // stock-effect instancing itself (std::runtime_error, Fna3dDraw.cpp: no stock effect
        // declares instance semantics), a renderer capability boundary below the XNA rule; every
        // other renderer draws it.
        void ExpectLegalInstancedShapePassesValidation()
        {
            try
            {
                DrawInstanced();
            }
            catch (const System::InvalidOperationException&)
            {
                ADD_FAILURE() << "XNA validation refused the legal instanced shape";
            }
            catch (const std::runtime_error& refusal)
            {
                if (std::string(device.GetGraphicsRendererName()) != "FNA3D")
                    ADD_FAILURE() << "the legal instanced shape was refused: " << refusal.what();
            }
        }
    };
}

TEST_F(InstanceStreamDrawRuleTest, OrdinaryDrawsRefuseABoundPerInstanceStream)
{
    // Control: the same buffers draw on both ordinary routes with only a per-vertex stream.
    device.SetVertexBuffer(mesh.get());
    ASSERT_NO_THROW(DrawOrdinary());
    ASSERT_NO_THROW(DrawIndexed());

    device.SetVertexBuffers({VertexBufferBinding(mesh.get(), 0, 0),
                             VertexBufferBinding(instances.get(), 0, 1)});
    ExpectExactInvalidOperation([&] { DrawOrdinary(); }, "DrawPrimitives with an instance stream",
                                NonZeroInstanceFrequency);
    ExpectExactInvalidOperation([&] { DrawIndexed(); },
                                "DrawIndexedPrimitives with an instance stream",
                                NonZeroInstanceFrequency);

    // Rebinding a single per-vertex stream clears the instance slot again.
    device.SetVertexBuffer(mesh.get());
    EXPECT_NO_THROW(DrawOrdinary());
    EXPECT_NO_THROW(DrawIndexed());
}

TEST_F(InstanceStreamDrawRuleTest, InstancedDrawsNeedBothAPerVertexAndAPerInstanceStream)
{
    // Control: one stream of each rate is the legal instanced shape.
    device.SetVertexBuffers({VertexBufferBinding(mesh.get(), 0, 0),
                             VertexBufferBinding(instances.get(), 0, 1)});
    ExpectLegalInstancedShapePassesValidation();

    device.SetVertexBuffers({VertexBufferBinding(mesh.get(), 0, 0)});
    ExpectExactInvalidOperation([&] { DrawInstanced(); }, "instanced draw without instance data",
                                InvalidInstanceStreams);

    device.SetVertexBuffers({VertexBufferBinding(instances.get(), 0, 1)});
    ExpectExactInvalidOperation([&] { DrawInstanced(); }, "instanced draw without vertex data",
                                InvalidInstanceStreams);

    device.SetVertexBuffers({VertexBufferBinding(mesh.get(), 0, 0),
                             VertexBufferBinding(instances.get(), 0, 1)});
    ExpectLegalInstancedShapePassesValidation();
}
