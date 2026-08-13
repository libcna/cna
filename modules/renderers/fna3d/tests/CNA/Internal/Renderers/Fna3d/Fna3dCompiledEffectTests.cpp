// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "System/ArgumentException.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Matrix;

namespace
{
    std::vector<SharpRuntime::bytecs> LoadStockEffect(const char* name)
    {
        const std::filesystem::path path =
            std::filesystem::path(__FILE__).parent_path() / "../../../../../effects" / name;
        std::ifstream input(path, std::ios::binary);
        if (!input) return {};
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }
}

TEST(Fna3dCompiledEffectTest, ParsesAndAppliesEveryProvenanceTrackedStockFixture)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    constexpr const char* fixtures[] = {
        "AlphaTestEffect.fxb", "BasicEffect.fxb", "DualTextureEffect.fxb",
        "EnvironmentMapEffect.fxb", "SkinnedEffect.fxb", "SpriteEffect.fxb"
    };
    for (const char* fixture : fixtures)
    {
        SCOPED_TRACE(fixture);
        const auto bytes = LoadStockEffect(fixture);
        ASSERT_FALSE(bytes.empty());
        Effect effect(device, bytes);
        ASSERT_GT(effect.getParametersProperty().getCountProperty(), 0);
        ASSERT_NE(effect.getCurrentTechniqueProperty(), nullptr);
        ASSERT_GT(effect.getCurrentTechniqueProperty()->getPassesProperty().getCountProperty(), 0);
        EXPECT_NO_THROW(effect.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply());
    }
}

TEST(Fna3dCompiledEffectTest, ReflectsAndAppliesRealXnaEffectBytecode)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    const auto bytes = LoadStockEffect("BasicEffect.fxb");
    ASSERT_FALSE(bytes.empty());
    Effect effect(device, bytes);

    ASSERT_EQ(effect.getTechniquesProperty().getCountProperty(), 1);
    ASSERT_EQ(effect.getTechniquesProperty()[0].getPassesProperty().getCountProperty(), 1);
    EXPECT_GT(effect.getParametersProperty().getCountProperty(), 0);

    auto* worldViewProjection = effect.getParametersProperty()["WorldViewProj"];
    ASSERT_NE(worldViewProjection, nullptr);
    const Matrix value = Matrix::CreateTranslation(7.0f, 11.0f, 13.0f);
    worldViewProjection->SetValue(value);
    effect.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply();
    EXPECT_EQ(worldViewProjection->GetValueMatrix(), value);
}

TEST(Fna3dCompiledEffectTest, CloneHasIndependentParameterStorageAndNativeEffect)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    const auto bytes = LoadStockEffect("BasicEffect.fxb");
    ASSERT_FALSE(bytes.empty());
    Effect original(device, bytes);
    auto* originalParameter = original.getParametersProperty()["DiffuseColor"];
    ASSERT_NE(originalParameter, nullptr);
    originalParameter->SetValue(Microsoft::Xna::Framework::Vector3(0.25f, 0.5f, 0.75f));

    std::unique_ptr<Effect> clone(original.Clone());
    auto* cloneParameter = clone->getParametersProperty()["DiffuseColor"];
    ASSERT_NE(cloneParameter, nullptr);
    EXPECT_EQ(cloneParameter->GetValueVector3(), originalParameter->GetValueVector3());

    cloneParameter->SetValue(Microsoft::Xna::Framework::Vector3(1.0f, 0.0f, 0.0f));
    EXPECT_NE(cloneParameter->GetValueVector3(), originalParameter->GetValueVector3());
    EXPECT_NO_THROW(clone->getCurrentTechniqueProperty()->getPassesProperty()[0].Apply());
}

TEST(Fna3dCompiledEffectTest, InvalidBytecodeReturnsMojoShaderDiagnostics)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    const std::vector<SharpRuntime::bytecs> invalid{1, 2, 3, 4};
    try
    {
        Effect effect(device, invalid);
        FAIL() << "invalid bytecode must not construct an Effect";
    }
    catch (const System::ArgumentException& error)
    {
        EXPECT_NE(std::string(error.what()).find("effect bytecode"), std::string::npos);
    }
}

TEST(Fna3dCompiledEffectTest, TruncatedCredibleBytecodeFailsWithoutCleanupCrash)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    // Plausible top-level counts, but no technique body. This reaches the pinned MojoShader
    // revision's static "unexpected EOF" sentinel, whose callback context must never be freed.
    const std::vector<SharpRuntime::bytecs> bytes{
        0x01, 0x09, 0xFF, 0xFE, 0, 0, 0, 0,
        0, 0, 0, 0, 1, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0
    };
    EXPECT_ANY_THROW(Effect(device, bytes));
}

TEST(Fna3dCompiledEffectTest, CompiledBasicEffectRendersThroughDrawUserPrimitives)
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    Effect effect(device, LoadStockEffect("BasicEffect.fxb"));
    auto& parameters = effect.getParametersProperty();
    ASSERT_NE(parameters["WorldViewProj"], nullptr);
    ASSERT_NE(parameters["DiffuseColor"], nullptr);
    ASSERT_NE(parameters["ShaderIndex"], nullptr);
    parameters["WorldViewProj"]->SetValue(Matrix::getIdentityProperty());
    parameters["DiffuseColor"]->SetValue(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
    // No fog + vertex color + no texture + no lighting.
    parameters["ShaderIndex"]->SetValue(3);

    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.Clear(Color::Black);
    effect.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply();

    const VertexPositionColor vertices[6] = {
        { Vector3(-1.0f, 1.0f, 0.5f), Color(220, 30, 40, 255) },
        { Vector3(-1.0f, -1.0f, 0.5f), Color(220, 30, 40, 255) },
        { Vector3(1.0f, -1.0f, 0.5f), Color(220, 30, 40, 255) },
        { Vector3(-1.0f, 1.0f, 0.5f), Color(220, 30, 40, 255) },
        { Vector3(1.0f, -1.0f, 0.5f), Color(220, 30, 40, 255) },
        { Vector3(1.0f, 1.0f, 0.5f), Color(220, 30, 40, 255) },
    };
    EXPECT_NO_THROW(device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2));

    const auto& viewport = device.getViewportProperty();
    const Rectangle center(viewport.getWidthProperty() / 2,
                           viewport.getHeightProperty() / 2, 1, 1);
    Color pixel(0, 0, 0, 0);
    device.GetBackBufferData(&center, &pixel, 0, 1);
    EXPECT_NEAR(pixel.getRProperty(), 220, 2);
    EXPECT_NEAR(pixel.getGProperty(), 30, 2);
    EXPECT_NEAR(pixel.getBProperty(), 40, 2);
}

TEST(Fna3dCompiledEffectTest, CompiledSpriteEffectRendersThroughDrawUserPrimitives)
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    Effect effect(device, LoadStockEffect("SpriteEffect.fxb"));
    auto& parameters = effect.getParametersProperty();
    Texture2D white = Texture2D::CreateFromPixels(
        device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
    parameters["MatrixTransform"]->SetValue(Matrix::getIdentityProperty());
    parameters["Texture"]->SetValue(&white);
    effect.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply();
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.Clear(Color::Black);

    const VertexPositionColorTexture vertices[6] = {
        { Vector3(-1.0f, 1.0f, 0.0f), Color(20, 210, 60, 255), Vector2(0.0f, 0.0f) },
        { Vector3(-1.0f, -1.0f, 0.0f), Color(20, 210, 60, 255), Vector2(0.0f, 1.0f) },
        { Vector3(1.0f, -1.0f, 0.0f), Color(20, 210, 60, 255), Vector2(1.0f, 1.0f) },
        { Vector3(-1.0f, 1.0f, 0.0f), Color(20, 210, 60, 255), Vector2(0.0f, 0.0f) },
        { Vector3(1.0f, -1.0f, 0.0f), Color(20, 210, 60, 255), Vector2(1.0f, 1.0f) },
        { Vector3(1.0f, 1.0f, 0.0f), Color(20, 210, 60, 255), Vector2(1.0f, 0.0f) },
    };
    device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);

    const auto& viewport = device.getViewportProperty();
    const Rectangle center(viewport.getWidthProperty() / 2,
                           viewport.getHeightProperty() / 2, 1, 1);
    Color pixel(0, 0, 0, 0);
    device.GetBackBufferData(&center, &pixel, 0, 1);
    EXPECT_NEAR(pixel.getRProperty(), 20, 2);
    EXPECT_NEAR(pixel.getGProperty(), 210, 2);
    EXPECT_NEAR(pixel.getBProperty(), 60, 2);
}

TEST(Fna3dCompiledEffectTest, SpriteBatchExecutesCompiledEffectAndOverridesTextureSlotZero)
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    Effect effect(device, LoadStockEffect("SpriteEffect.fxb"));
    auto* matrixTransform = effect.getParametersProperty()["MatrixTransform"];
    ASSERT_NE(matrixTransform, nullptr);
    const auto& viewport = device.getViewportProperty();
    Matrix projection = Matrix::getIdentityProperty();
    projection.M11 = 2.0f / static_cast<float>(viewport.getWidthProperty());
    projection.M22 = -2.0f / static_cast<float>(viewport.getHeightProperty());
    projection.M33 = 1.0f;
    projection.M41 = -1.0f;
    projection.M42 = 1.0f;
    projection.M44 = 1.0f;
    matrixTransform->SetValue(projection);
    EXPECT_EQ(matrixTransform->getRowCountProperty(), 4);
    EXPECT_EQ(matrixTransform->getColumnCountProperty(), 4);
    EXPECT_EQ(matrixTransform->GetValueMatrix(), projection);

    Texture2D white = Texture2D::CreateFromPixels(
        device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
    SpriteBatch batch(device);
    SamplerState pointClamp = SamplerState::PointClamp;
    device.Clear(Color::Black);
    batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp,
                nullptr, nullptr, &effect);
    batch.Draw(white, Rectangle(8, 8, 24, 24), Color(20, 210, 60, 255));
    EXPECT_NO_THROW(batch.End());

    const Rectangle sample(16, 16, 1, 1);
    Color pixel(0, 0, 0, 0);
    device.GetBackBufferData(&sample, &pixel, 0, 1);
    EXPECT_NEAR(pixel.getRProperty(), 20, 2);
    EXPECT_NEAR(pixel.getGProperty(), 210, 2);
    EXPECT_NEAR(pixel.getBProperty(), 60, 2);
}
