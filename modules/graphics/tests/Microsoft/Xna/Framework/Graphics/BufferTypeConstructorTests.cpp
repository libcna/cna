// SPDX-License-Identifier: MS-PL
//
// XNA-MISSING-006: the documented (GraphicsDevice, Type, Int32, BufferUsage) constructors of
// IndexBuffer, DynamicIndexBuffer, VertexBuffer and DynamicVertexBuffer.
//
// These are not the same as the IndexElementSize and VertexDeclaration constructors CNA already
// had. XNA derives the index element width from Marshal.SizeOf(indexType) and reports SixteenBits
// only for a two-byte element, and it resolves the vertex layout with its internal
// VertexDeclaration.FromType, which instantiates the type and reads its IVertexType declaration
// (xna4-decomp/.../Microsoft.Xna.Framework.Graphics/{IndexBuffer,VertexBuffer,VertexDeclaration}.cs).
// C++ has no reflection, so the vertex route goes through
// CNA::Graphics::VertexTypeRegistryEXT, where a vertex structure records the same three facts at
// registration time.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "CNA/Graphics/VertexTypeRegistryEXT.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/IVertexType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/Type.hpp"

using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    /// A game's own vertex structure: registered here, which is what the registry exists for.
    struct GameVertex final : public IVertexType
    {
        Vector3 Position{};
        Vector2 TextureCoordinate{};

        [[nodiscard]] const VertexDeclaration& getVertexDeclarationProperty() const override
        {
            static const VertexDeclaration declaration({
                VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector2,
                              VertexElementUsage::TextureCoordinate, 0),
            });
            return declaration;
        }
    };

    /// A 32-bit index buffer needs HiDef; the Reach profile refuses one, in CNA as in XNA, so the
    /// Type constructor's 32-bit cases need a device that allows it.
    GraphicsDevice MakeHiDefDevice()
    {
        return GraphicsDevice(GraphicsAdapter::getDefaultAdapterProperty(),
                              GraphicsProfile::HiDef, PresentationParameters{});
    }

    /// Not a vertex type at all.
    struct NotAVertex
    {
        int value = 0;
    };
}

// =============================================================================
// IndexBuffer / DynamicIndexBuffer
// =============================================================================

TEST(BufferTypeConstructorTest, IndexBufferResolvesSixteenBitTypes)
{
    GraphicsDevice device;
    const IndexBuffer fromInt16(device, System::Type::From<std::int16_t>(), 8, BufferUsage::None);
    const IndexBuffer fromUInt16(device, System::Type::From<std::uint16_t>(), 8, BufferUsage::None);

    EXPECT_EQ(fromInt16.getIndexElementSizeProperty(), IndexElementSize::SixteenBits);
    EXPECT_EQ(fromUInt16.getIndexElementSizeProperty(), IndexElementSize::SixteenBits);
    EXPECT_EQ(fromInt16.getIndexCountProperty(), 8);
}

TEST(BufferTypeConstructorTest, IndexBufferResolvesThirtyTwoBitTypes)
{
    GraphicsDevice device = MakeHiDefDevice();
    const IndexBuffer fromInt32(device, System::Type::From<std::int32_t>(), 8, BufferUsage::None);
    const IndexBuffer fromUInt32(device, System::Type::From<std::uint32_t>(), 8, BufferUsage::None);

    EXPECT_EQ(fromInt32.getIndexElementSizeProperty(), IndexElementSize::ThirtyTwoBits);
    EXPECT_EQ(fromUInt32.getIndexElementSizeProperty(), IndexElementSize::ThirtyTwoBits);
}

TEST(BufferTypeConstructorTest, IndexBufferTypeConstructorMatchesTheElementSizeConstructor)
{
    // The two constructors are documented separately and must agree once the type is resolved.
    GraphicsDevice device;
    const IndexBuffer viaType(device, System::Type::From<std::uint16_t>(), 16, BufferUsage::None);
    const IndexBuffer viaEnum(device, IndexElementSize::SixteenBits, 16, BufferUsage::None);

    EXPECT_EQ(viaType.getIndexElementSizeProperty(), viaEnum.getIndexElementSizeProperty());
    EXPECT_EQ(viaType.getIndexCountProperty(), viaEnum.getIndexCountProperty());
}

TEST(BufferTypeConstructorTest, IndexBufferRefusesAnUnusableIndexType)
{
    GraphicsDevice device;
    EXPECT_THROW((void)IndexBuffer(device, System::Type::From<std::uint8_t>(), 8, BufferUsage::None),
                 System::ArgumentException);
    EXPECT_THROW((void)IndexBuffer(device, System::Type::From<float>(), 8, BufferUsage::None),
                 System::ArgumentException);
    EXPECT_THROW((void)IndexBuffer(device, System::Type::From<std::int64_t>(), 8, BufferUsage::None),
                 System::ArgumentException);
    EXPECT_THROW((void)IndexBuffer(device, System::Type::From<NotAVertex>(), 8, BufferUsage::None),
                 System::ArgumentException);
}

TEST(BufferTypeConstructorTest, IndexBufferTypeConstructorStillValidatesTheCount)
{
    GraphicsDevice device;
    EXPECT_THROW((void)IndexBuffer(device, System::Type::From<std::uint16_t>(), 0, BufferUsage::None),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW((void)IndexBuffer(device, System::Type::From<std::uint16_t>(), -1, BufferUsage::None),
                 System::ArgumentOutOfRangeException);
}

TEST(BufferTypeConstructorTest, DynamicIndexBufferResolvesTheSameTypes)
{
    GraphicsDevice device = MakeHiDefDevice();
    const DynamicIndexBuffer sixteen(device, System::Type::From<std::uint16_t>(), 8,
                                     BufferUsage::WriteOnly);
    const DynamicIndexBuffer thirtyTwo(device, System::Type::From<std::uint32_t>(), 8,
                                       BufferUsage::WriteOnly);

    EXPECT_EQ(sixteen.getIndexElementSizeProperty(), IndexElementSize::SixteenBits);
    EXPECT_EQ(thirtyTwo.getIndexElementSizeProperty(), IndexElementSize::ThirtyTwoBits);
    EXPECT_EQ(sixteen.getBufferUsageProperty(), BufferUsage::WriteOnly);

    EXPECT_THROW((void)DynamicIndexBuffer(device, System::Type::From<float>(), 8,
                                          BufferUsage::WriteOnly),
                 System::ArgumentException);
}

// =============================================================================
// VertexBuffer / DynamicVertexBuffer
// =============================================================================

TEST(BufferTypeConstructorTest, VertexBufferResolvesEveryStockVertexStructure)
{
    GraphicsDevice device;

    // Each buffer must carry the declaration the structure itself reports, which is the whole
    // contract: the type names the layout.
    const auto expectResolves = [&device](const auto& probe, const System::Type& type) {
        const VertexBuffer buffer(device, type, 4, BufferUsage::None);
        EXPECT_EQ(buffer.getVertexDeclarationProperty().getVertexStrideProperty(),
                  probe.getVertexDeclarationProperty().getVertexStrideProperty());
        EXPECT_EQ(buffer.getVertexDeclarationProperty().GetVertexElements().size(),
                  probe.getVertexDeclarationProperty().GetVertexElements().size());
    };

    expectResolves(VertexPositionColor{}, System::Type::From<VertexPositionColor>());
    expectResolves(VertexPositionTexture{}, System::Type::From<VertexPositionTexture>());
    expectResolves(VertexPositionNormalTexture{},
                   System::Type::From<VertexPositionNormalTexture>());
    expectResolves(VertexPositionColorTexture{},
                   System::Type::From<VertexPositionColorTexture>());
}

TEST(BufferTypeConstructorTest, VertexBufferTypeConstructorMatchesTheDeclarationConstructor)
{
    GraphicsDevice device;
    const VertexPositionColor probe{};
    const VertexBuffer viaType(device, System::Type::From<VertexPositionColor>(), 6,
                               BufferUsage::None);
    const VertexBuffer viaDeclaration(device, probe.getVertexDeclarationProperty(), 6,
                                      BufferUsage::None);

    EXPECT_EQ(viaType.getVertexDeclarationProperty().getVertexStrideProperty(),
              viaDeclaration.getVertexDeclarationProperty().getVertexStrideProperty());
    EXPECT_EQ(viaType.getVertexCountProperty(), viaDeclaration.getVertexCountProperty());
    EXPECT_EQ(viaType.getVertexDeclarationProperty().GetVertexElements().size(),
              viaDeclaration.getVertexDeclarationProperty().GetVertexElements().size());
}

TEST(BufferTypeConstructorTest, VertexBufferResolvesARegisteredGameVertexStructure)
{
    CNA::Graphics::VertexTypeRegistryEXT::Register<GameVertex>();

    GraphicsDevice device;
    const GameVertex probe{};
    const VertexBuffer buffer(device, System::Type::From<GameVertex>(), 4, BufferUsage::None);
    EXPECT_EQ(buffer.getVertexDeclarationProperty().getVertexStrideProperty(),
              probe.getVertexDeclarationProperty().getVertexStrideProperty());
    EXPECT_EQ(buffer.getVertexDeclarationProperty().GetVertexElements().size(), 2u);
}

TEST(BufferTypeConstructorTest, VertexBufferRefusesAnUnregisteredType)
{
    GraphicsDevice device;
    EXPECT_THROW((void)VertexBuffer(device, System::Type::From<NotAVertex>(), 4, BufferUsage::None),
                 System::ArgumentException);
    EXPECT_THROW((void)VertexBuffer(device, System::Type::From<float>(), 4, BufferUsage::None),
                 System::ArgumentException);
}

TEST(BufferTypeConstructorTest, TheCppSizeOfAVertexStructureIsNotItsStride)
{
    // XNA's FromType also checks Marshal.SizeOf(vertexType) against the declaration's stride. That
    // check has no counterpart here and is deliberately absent: CNA's vertex structures derive from
    // IVertexType, a polymorphic base, so each carries a vtable pointer and the C++ object is
    // larger than the vertex data. Asserting the inequality keeps the reason visible -- a future
    // reader who adds the check would make every correct vertex type fail.
    const VertexPositionColor probe{};
    const int stride = probe.getVertexDeclarationProperty().getVertexStrideProperty();
    EXPECT_EQ(stride, 16) << "position (12) plus a packed colour (4)";
    EXPECT_GT(sizeof(VertexPositionColor), static_cast<std::size_t>(stride))
        << "the vtable pointer is the difference";

    // And the buffer is created from the stride, not from the C++ size.
    GraphicsDevice device;
    const VertexBuffer buffer(device, System::Type::From<VertexPositionColor>(), 4,
                              BufferUsage::None);
    EXPECT_EQ(buffer.getVertexDeclarationProperty().getVertexStrideProperty(), stride);
}

TEST(BufferTypeConstructorTest, VertexBufferTypeConstructorStillValidatesTheCount)
{
    GraphicsDevice device;
    EXPECT_THROW(
        (void)VertexBuffer(device, System::Type::From<VertexPositionColor>(), 0, BufferUsage::None),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        (void)VertexBuffer(device, System::Type::From<VertexPositionColor>(), -3, BufferUsage::None),
        System::ArgumentOutOfRangeException);
}

TEST(BufferTypeConstructorTest, DynamicVertexBufferResolvesTheSameTypes)
{
    GraphicsDevice device;
    const VertexPositionColor probe{};
    const DynamicVertexBuffer buffer(device, System::Type::From<VertexPositionColor>(), 8,
                                     BufferUsage::WriteOnly);
    EXPECT_EQ(buffer.getVertexDeclarationProperty().getVertexStrideProperty(),
              probe.getVertexDeclarationProperty().getVertexStrideProperty());
    EXPECT_EQ(buffer.getVertexCountProperty(), 8);
    EXPECT_EQ(buffer.getBufferUsageProperty(), BufferUsage::WriteOnly);

    EXPECT_THROW((void)DynamicVertexBuffer(device, System::Type::From<NotAVertex>(), 8,
                                           BufferUsage::WriteOnly),
                 System::ArgumentException);
}

TEST(BufferTypeConstructorTest, TheRegistryAnswersDirectlyToo)
{
    // TryResolve is the non-throwing route the buffer constructors do not need but a caller may.
    EXPECT_NE(CNA::Graphics::VertexTypeRegistryEXT::TryResolve(
                  System::Type::From<VertexPositionColor>()),
              nullptr);
    EXPECT_EQ(CNA::Graphics::VertexTypeRegistryEXT::TryResolve(System::Type::From<NotAVertex>()),
              nullptr);
}
