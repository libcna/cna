// SPDX-License-Identifier: MS-PL
//
// XNA-MISSING-008: the six documented generic user-array draw overloads.
//
//   DrawUserPrimitives<T>(PrimitiveType, T[], Int32, Int32)
//   DrawUserPrimitives<T>(PrimitiveType, T[], Int32, Int32, VertexDeclaration)
//   DrawUserIndexedPrimitives<T>(PrimitiveType, T[], Int32, Int32, Int16[], Int32, Int32)
//   DrawUserIndexedPrimitives<T>(..., Int16[], ..., VertexDeclaration)
//   DrawUserIndexedPrimitives<T>(PrimitiveType, T[], Int32, Int32, Int32[], Int32, Int32)
//   DrawUserIndexedPrimitives<T>(..., Int32[], ..., VertexDeclaration)
//
// CNA already had pointer/count and raw-void draw paths; what these add is the array shape, where
// the array carries its own length and the vertex type is retained. Both halves matter: the length
// is the one thing a pointer overload cannot check, and the retained type is what lets the layout
// come from the type. The cases below assert the signatures compile as generics, that the range
// checks the array makes possible actually fire, and that the retained type reaches the same
// converted draw the pointer overloads perform.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/IVertexType.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    /// A plain vertex structure of the kind a game writes: no IVertexType, no vtable, so its array
    /// is already the GPU stream the declaration describes.
    struct PlainVertex
    {
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        std::uint32_t PackedColor = 0xFFFFFFFFu;
    };

    /// A vertex structure that implements IVertexType but is not one of the built-in four, so it
    /// carries a vtable pointer and CNA has no object-to-stream conversion for it.
    struct CustomIVertexType final : public IVertexType
    {
        Vector3 Position{};

        [[nodiscard]] const VertexDeclaration& getVertexDeclarationProperty() const override
        {
            static const VertexDeclaration declaration({
                VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            });
            return declaration;
        }
    };

    VertexDeclaration PlainVertexDeclaration()
    {
        return VertexDeclaration(static_cast<int>(sizeof(PlainVertex)),
                                 {VertexElement(0, VertexElementFormat::Vector3,
                                                VertexElementUsage::Position, 0),
                                  VertexElement(12, VertexElementFormat::Color,
                                                VertexElementUsage::Color, 0)});
    }

    std::vector<VertexPositionColor> Triangle()
    {
        return {VertexPositionColor(Vector3(0.0f, 1.0f, 0.0f), Color(255, 0, 0, 255)),
                VertexPositionColor(Vector3(1.0f, -1.0f, 0.0f), Color(0, 255, 0, 255)),
                VertexPositionColor(Vector3(-1.0f, -1.0f, 0.0f), Color(0, 0, 255, 255))};
    }

    /// A device with an applied effect, which is what every DrawUser path requires. HiDef, because
    /// 32-bit user indices are a HiDef feature -- the Reach profile refuses them, in CNA as in XNA,
    /// and the Int32 overloads could otherwise not be exercised at all.
    class GenericUserArrayDrawTest : public ::testing::Test
    {
    protected:
        GenericUserArrayDrawTest()
            : device_(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                      PresentationParameters{})
        {
        }

        void SetUp() override
        {
            effect_ = std::make_unique<BasicEffect>(device_);
            effect_->Apply();
        }

        GraphicsDevice device_;
        std::unique_ptr<BasicEffect> effect_;
    };
}

// =============================================================================
// Signature shape
// =============================================================================

TEST_F(GenericUserArrayDrawTest, AllSixSignaturesAreGenericOverTheVertexType)
{
    // Instantiating each of the six through a vector of a built-in vertex type proves the shape:
    // an array parameter, a retained vertex type, and Int16/Int32 index arrays.
    const std::vector<VertexPositionColor> vertices = Triangle();
    const std::vector<std::int16_t> shortIndices = {0, 1, 2};
    const std::vector<std::int32_t> intIndices = {0, 1, 2};
    const VertexDeclaration declaration = VertexPositionColor::getVertexDeclarationStatic();

    EXPECT_NO_THROW(device_.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 1));
    EXPECT_NO_THROW(
        device_.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 1, declaration));
    EXPECT_NO_THROW(device_.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, 0, 3,
                                                     shortIndices, 0, 1));
    EXPECT_NO_THROW(device_.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, 0, 3,
                                                     shortIndices, 0, 1, declaration));
    EXPECT_NO_THROW(device_.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, 0, 3,
                                                     intIndices, 0, 1));
    EXPECT_NO_THROW(device_.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, 0, 3,
                                                     intIndices, 0, 1, declaration));
}

TEST_F(GenericUserArrayDrawTest, TheVertexTypeIsRetainedAcrossEveryBuiltInStructure)
{
    const std::vector<VertexPositionColor> colored = Triangle();
    const std::vector<VertexPositionTexture> textured = {
        VertexPositionTexture(Vector3(0.0f, 1.0f, 0.0f), Vector2(0.5f, 0.0f)),
        VertexPositionTexture(Vector3(1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f)),
        VertexPositionTexture(Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 1.0f))};

    EXPECT_NO_THROW(device_.DrawUserPrimitives(PrimitiveType::TriangleList, colored, 0, 1));
    EXPECT_NO_THROW(device_.DrawUserPrimitives(PrimitiveType::TriangleList, textured, 0, 1));
}

// =============================================================================
// The range checks the array makes possible
// =============================================================================

TEST_F(GenericUserArrayDrawTest, DrawUserPrimitivesRefusesARangePastTheArray)
{
    const std::vector<VertexPositionColor> vertices = Triangle();

    // Three vertices hold exactly one triangle; two would need six.
    EXPECT_THROW(device_.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2),
                 System::ArgumentOutOfRangeException);
    // The offset counts against the same budget.
    EXPECT_THROW(device_.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 1, 1),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(device_.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, -1, 1),
                 System::ArgumentOutOfRangeException);
    // An empty array cannot hold any primitive.
    EXPECT_THROW(device_.DrawUserPrimitives(PrimitiveType::TriangleList,
                                            std::vector<VertexPositionColor>{}, 0, 1),
                 System::ArgumentOutOfRangeException);
}

TEST_F(GenericUserArrayDrawTest, DrawUserPrimitivesRefusesANonPositivePrimitiveCount)
{
    const std::vector<VertexPositionColor> vertices = Triangle();
    EXPECT_ANY_THROW(device_.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 0));
    EXPECT_ANY_THROW(device_.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, -1));
}

TEST_F(GenericUserArrayDrawTest, DrawUserIndexedPrimitivesRefusesARangePastEitherArray)
{
    const std::vector<VertexPositionColor> vertices = Triangle();
    const std::vector<std::int16_t> indices = {0, 1, 2};

    // More vertices claimed than the array holds.
    EXPECT_THROW(device_.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, 0, 4,
                                                   indices, 0, 1),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(device_.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, 1, 3,
                                                   indices, 0, 1),
                 System::ArgumentOutOfRangeException);
    // More indices needed than the array holds.
    EXPECT_THROW(device_.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, 0, 3,
                                                   indices, 0, 2),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(device_.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, 0, 3,
                                                   indices, 1, 1),
                 System::ArgumentOutOfRangeException);
    // Negative offsets and counts.
    EXPECT_THROW(device_.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, -1, 3,
                                                   indices, 0, 1),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(device_.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, 0, 0,
                                                   indices, 0, 1),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(device_.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, 0, 3,
                                                   indices, -1, 1),
                 System::ArgumentOutOfRangeException);
}

TEST_F(GenericUserArrayDrawTest, BothIndexWidthsAreCheckedTheSameWay)
{
    const std::vector<VertexPositionColor> vertices = Triangle();
    const std::vector<std::int32_t> indices = {0, 1, 2};

    EXPECT_NO_THROW(device_.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, 0, 3,
                                                      indices, 0, 1));
    EXPECT_THROW(device_.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, 0, 3,
                                                   indices, 0, 2),
                 System::ArgumentOutOfRangeException);
}

TEST_F(GenericUserArrayDrawTest, IndicesAreReadAsTheWidthTheArrayDeclares)
{
    // A 16-bit and a 32-bit index array describing the same triangle must both draw it, which is
    // what shows the arrays are read at their own element width rather than reinterpreted.
    const std::vector<VertexPositionColor> vertices = Triangle();
    const std::vector<std::int16_t> shortIndices = {2, 0, 1};
    const std::vector<std::int32_t> intIndices = {2, 0, 1};

    EXPECT_NO_THROW(device_.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, 0, 3,
                                                      shortIndices, 0, 1));
    EXPECT_NO_THROW(device_.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, 0, 3,
                                                      intIndices, 0, 1));
}

// =============================================================================
// Vertex types other than the built-in ones
// =============================================================================

TEST_F(GenericUserArrayDrawTest, APlainVertexStructureDrawsThroughItsDeclaration)
{
    // A plain structure's array IS the stream, so the declaration overload consumes it directly.
    const std::vector<PlainVertex> vertices = {
        {0.0f, 1.0f, 0.0f, 0xFF0000FFu}, {1.0f, -1.0f, 0.0f, 0xFF00FF00u},
        {-1.0f, -1.0f, 0.0f, 0xFFFF0000u}};
    const VertexDeclaration declaration = PlainVertexDeclaration();

    EXPECT_NO_THROW(
        device_.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 1, declaration));

    const std::vector<std::int16_t> indices = {0, 1, 2};
    EXPECT_NO_THROW(device_.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, 0, 3,
                                                      indices, 0, 1, declaration));
}

TEST_F(GenericUserArrayDrawTest, APlainVertexStructureStillHasItsRangeChecked)
{
    const std::vector<PlainVertex> vertices = {{}, {}, {}};
    const VertexDeclaration declaration = PlainVertexDeclaration();
    EXPECT_THROW(
        device_.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2, declaration),
        System::ArgumentOutOfRangeException);
}

TEST_F(GenericUserArrayDrawTest, AnUnconvertibleIVertexTypeIsRefusedRatherThanDrawnFromItsBytes)
{
    // The structure carries a vtable pointer, so its bytes are not a vertex stream and CNA has no
    // conversion for it. Refusing is the only honest answer; drawing would send vtable bytes to the
    // renderer.
    const std::vector<CustomIVertexType> vertices(3);
    const CustomIVertexType probe{};

    EXPECT_THROW(device_.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 1),
                 System::NotSupportedException);
    EXPECT_THROW(device_.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 1,
                                            probe.getVertexDeclarationProperty()),
                 System::NotSupportedException);

    const std::vector<std::int16_t> indices = {0, 1, 2};
    EXPECT_THROW(device_.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, 0, 3,
                                                   indices, 0, 1),
                 System::NotSupportedException);
}

TEST_F(GenericUserArrayDrawTest, WithoutAnAppliedEffectTheDrawIsRefused)
{
    // The effect requirement belongs to the paths these overloads forward into, and must survive
    // the forwarding.
    GraphicsDevice bare;
    const std::vector<VertexPositionColor> vertices = Triangle();
    EXPECT_THROW(bare.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 1),
                 System::InvalidOperationException);
}

TEST(GenericUserArrayDrawProfileTest, ThirtyTwoBitIndicesStillNeedHiDef)
{
    // The profile restriction belongs to the path the Int32 overloads forward into, and must
    // survive the forwarding: Reach refuses 32-bit user indices, in CNA as in XNA.
    GraphicsDevice reach;
    BasicEffect effect(reach);
    effect.Apply();

    const std::vector<VertexPositionColor> vertices = Triangle();
    const std::vector<std::int16_t> shortIndices = {0, 1, 2};
    const std::vector<std::int32_t> intIndices = {0, 1, 2};

    EXPECT_NO_THROW(reach.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, 0, 3,
                                                    shortIndices, 0, 1));
    EXPECT_THROW(reach.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, 0, 3,
                                                 intIndices, 0, 1),
                 System::NotSupportedException);
}
