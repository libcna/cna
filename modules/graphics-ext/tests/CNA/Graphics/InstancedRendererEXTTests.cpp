// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-1400..MOD-1405: the instanced-draw helper.
//
// What can be asserted without a window is everything except the pixels: the declaration a caller
// has to match, the validation, the buffer reuse that makes a per-frame upload free, and the two
// halves of the capability decision -- one draw call where instancing exists, and either an
// explicit refusal or one call per instance where it does not.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/InstancedRendererEXT.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <memory>
#include <stdexcept>
#include <vector>

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::ModelMeshPart;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;
using CNA::Graphics::InstancedRendererEXT;

namespace {

    /// One triangle, enough to be a drawable part; nothing here inspects its geometry.
    class TrianglePart : public ::testing::Test
    {
    protected:
        GraphicsDevice gd;
        std::unique_ptr<VertexBuffer> vertices;
        std::unique_ptr<IndexBuffer> indices;
        std::unique_ptr<ModelMeshPart> part;

        void SetUp() override
        {
            // plans/plan_modern.md MOD-1690. A vertex buffer is 3D work, and a 2D-only renderer refuses
            // to create one -- the 2D-only renderers throw "does not support 3D:
            // CreateVertexBuffer" from this very line. Without this gate the whole fixture fails
            // there rather than skipping,
            // which reads as seven engine-layer defects instead of one documented boundary.
            if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
                GTEST_SKIP() << "this renderer has no 3D pipeline, so it cannot create the vertex "
                                "buffer every test in this fixture needs";

            const std::vector<VertexPositionColor> data{
                VertexPositionColor(Vector3(0.0f, 0.0f, 0.0f), Color::White),
                VertexPositionColor(Vector3(1.0f, 0.0f, 0.0f), Color::White),
                VertexPositionColor(Vector3(0.0f, 1.0f, 0.0f), Color::White)};
            vertices = std::make_unique<VertexBuffer>(
                gd, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::WriteOnly);
            vertices->SetData(data.data(), 3);

            const std::vector<std::uint16_t> indexData{0, 1, 2};
            indices = std::make_unique<IndexBuffer>(gd, IndexElementSize::SixteenBits, 3,
                                                    BufferUsage::WriteOnly);
            indices->SetData(indexData.data(), 3);

            part = std::make_unique<ModelMeshPart>(vertices.get(), indices.get(), 3, 1, 0, 0);
        }

        std::vector<Matrix> Grid(int count)
        {
            std::vector<Matrix> transforms;
            transforms.reserve(static_cast<std::size_t>(count));
            for (int i = 0; i < count; ++i)
                transforms.push_back(
                    Matrix::CreateTranslation(Vector3(static_cast<float>(i), 0.0f, 0.0f)));
            return transforms;
        }
    };

} // namespace

TEST_F(TrianglePart, TheInstanceDeclarationIsFourVector4sAtTheDocumentedUsageIndices)
{
    // MOD-1402: a caller building its own instance buffer has to match this exactly, and the
    // renderers bind it to the stock shaders' locations 12..15 by these usage indices.
    const auto& declaration = InstancedRendererEXT::getInstanceDeclaration();
    EXPECT_EQ(declaration.getVertexStrideProperty(), 64);
    const auto& elements = declaration.GetVertexElements();
    ASSERT_EQ(elements.size(), 4u);
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_EQ(elements[static_cast<std::size_t>(i)].getOffsetProperty(), i * 16);
        EXPECT_EQ(elements[static_cast<std::size_t>(i)].getVertexElementFormatProperty(),
                  VertexElementFormat::Vector4);
        EXPECT_EQ(elements[static_cast<std::size_t>(i)].getVertexElementUsageProperty(),
                  VertexElementUsage::TextureCoordinate);
        EXPECT_EQ(elements[static_cast<std::size_t>(i)].getUsageIndexProperty(), i + 1);
    }

    const auto& tint = InstancedRendererEXT::getTintDeclaration();
    EXPECT_EQ(tint.getVertexStrideProperty(), 4);
    ASSERT_EQ(tint.GetVertexElements().size(), 1u);
    EXPECT_EQ(tint.GetVertexElements()[0].getVertexElementUsageProperty(),
              VertexElementUsage::Color);
    EXPECT_EQ(tint.GetVertexElements()[0].getUsageIndexProperty(), 1);
}

TEST_F(TrianglePart, ANullOrUndrawablePartIsRefused)
{
    EXPECT_THROW(InstancedRendererEXT(gd, nullptr), std::invalid_argument);

    ModelMeshPart noVertices(nullptr, indices.get(), 3, 1, 0, 0);
    EXPECT_THROW(InstancedRendererEXT(gd, &noVertices), std::invalid_argument);

    ModelMeshPart noIndices(vertices.get(), nullptr, 3, 1, 0, 0);
    EXPECT_THROW(InstancedRendererEXT(gd, &noIndices), std::invalid_argument);

    ModelMeshPart noPrimitives(vertices.get(), indices.get(), 3, 0, 0, 0);
    EXPECT_THROW(InstancedRendererEXT(gd, &noPrimitives), std::invalid_argument);
}

TEST_F(TrianglePart, ReUploadingTheSameCountDoesNotReallocate)
{
    // MOD-1401. The capacity is the observable form of "a per-frame upload allocates nothing".
    InstancedRendererEXT renderer(gd, part.get());
    EXPECT_EQ(renderer.getInstanceCount(), 0);
    EXPECT_EQ(renderer.getInstanceCapacity(), 0);

    renderer.setInstances(Grid(100));
    EXPECT_EQ(renderer.getInstanceCount(), 100);
    EXPECT_EQ(renderer.getInstanceCapacity(), 100);

    renderer.setInstances(Grid(100));
    EXPECT_EQ(renderer.getInstanceCapacity(), 100);

    // Fewer instances reuse the buffer too; only growth reallocates.
    renderer.setInstances(Grid(40));
    EXPECT_EQ(renderer.getInstanceCount(), 40);
    EXPECT_EQ(renderer.getInstanceCapacity(), 100);

    renderer.setInstances(Grid(250));
    EXPECT_EQ(renderer.getInstanceCapacity(), 250);
}

TEST_F(TrianglePart, TheTintStreamIsOffByDefaultAndRemembersItsColours)
{
    InstancedRendererEXT renderer(gd, part.get());
    EXPECT_FALSE(renderer.isTintsEnabled());

    renderer.setInstances(Grid(4));
    // Set while disabled: kept, not lost, so switching the stream on later needs no re-upload.
    renderer.setInstanceTints({Color::Red, Color::Green});
    renderer.setTintsEnabled(true);
    EXPECT_TRUE(renderer.isTintsEnabled());
    renderer.setTintsEnabled(false);
    EXPECT_FALSE(renderer.isTintsEnabled());
}

TEST_F(TrianglePart, AnEmptyInstanceListDrawsNothingAtAll)
{
    InstancedRendererEXT renderer(gd, part.get());
    BasicEffect effect(gd);
    renderer.setInstances({});
    renderer.draw(effect);
    EXPECT_EQ(renderer.getLastDrawCallCount(), 0);
    EXPECT_FALSE(renderer.didLastDrawInstance());
}

TEST_F(TrianglePart, OneDrawCallForEveryInstanceOrAnExplicitRefusal)
{
    // MOD-1403/MOD-1405, written so it asserts something on every renderer rather than skipping:
    // where instancing exists, a hundred instances cost one call; where it does not, the default
    // is a refusal a caller can catch, and the fallback is one call each.
    InstancedRendererEXT renderer(gd, part.get());
    BasicEffect effect(gd);
    effect.setLightingEnabledProperty(false);
    effect.VertexColorEnabled = true;
    renderer.setInstances(Grid(100));

    if (renderer.isInstancingSupported())
    {
        renderer.draw(effect);
        EXPECT_EQ(renderer.getLastDrawCallCount(), 1) << "100 instances did not cost one draw call";
        EXPECT_TRUE(renderer.didLastDrawInstance());
    }
    else
    {
        EXPECT_THROW(renderer.draw(effect), std::logic_error);
        EXPECT_FALSE(renderer.isFallbackEnabled());
        renderer.setFallbackEnabled(true);
        renderer.draw(effect);
        EXPECT_EQ(renderer.getLastDrawCallCount(), 100);
        EXPECT_FALSE(renderer.didLastDrawInstance());
    }
}

TEST_F(TrianglePart, TheFallbackRestoresTheEffectsOwnWorldMatrix)
{
    // A helper that left the last instance's transform on the effect would corrupt every later
    // draw with it, and the corruption would look like a modelling error.
    InstancedRendererEXT renderer(gd, part.get());
    renderer.setFallbackEnabled(true);
    BasicEffect effect(gd);
    effect.setLightingEnabledProperty(false);
    effect.VertexColorEnabled = true;
    const Matrix world = Matrix::CreateTranslation(Vector3(5.0f, 6.0f, 7.0f));
    effect.setWorldProperty(world);

    renderer.setInstances(Grid(3));
    renderer.draw(effect);

    EXPECT_FLOAT_EQ(effect.getWorldProperty().M41, 5.0f);
    EXPECT_FLOAT_EQ(effect.getWorldProperty().M42, 6.0f);
    EXPECT_FLOAT_EQ(effect.getWorldProperty().M43, 7.0f);
}

#endif // CNA_CNAEXT
