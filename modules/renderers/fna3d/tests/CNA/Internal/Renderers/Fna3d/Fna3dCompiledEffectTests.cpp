// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
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
#include "System/Security/Cryptography/SHA256.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
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

    std::string HexDigest(const std::vector<std::uint8_t>& bytes)
    {
        std::ostringstream out;
        out << std::hex << std::setfill('0');
        for (const std::uint8_t byte : bytes)
            out << std::setw(2) << static_cast<unsigned int>(byte);
        return out.str();
    }

    std::vector<std::uint8_t> BuildEffectXnb(const std::vector<std::uint8_t>& effectBytes)
    {
        System::IO::MemoryStream body;
        System::IO::BinaryWriter bodyWriter(&body, true);
        bodyWriter.Write7BitEncodedInt(1);
        bodyWriter.Write(std::string("Microsoft.Xna.Framework.Content.EffectReader"));
        bodyWriter.Write(static_cast<std::int32_t>(0));
        bodyWriter.Write7BitEncodedInt(0);
        bodyWriter.Write7BitEncodedInt(1);
        bodyWriter.Write(static_cast<std::int32_t>(effectBytes.size()));
        bodyWriter.Write(effectBytes.data(), 0,
                         static_cast<std::int32_t>(effectBytes.size()));
        bodyWriter.Flush();
        const auto bodyBytes = body.ToArray();

        System::IO::MemoryStream file;
        System::IO::BinaryWriter fileWriter(&file, true);
        fileWriter.Write(static_cast<std::uint8_t>('X'));
        fileWriter.Write(static_cast<std::uint8_t>('N'));
        fileWriter.Write(static_cast<std::uint8_t>('B'));
        fileWriter.Write(static_cast<std::uint8_t>('w'));
        fileWriter.Write(static_cast<std::uint8_t>(5));
        fileWriter.Write(static_cast<std::uint8_t>(0));
        fileWriter.Write(static_cast<std::int32_t>(10 + bodyBytes.size()));
        fileWriter.Write(bodyBytes.data(), 0, static_cast<std::int32_t>(bodyBytes.size()));
        fileWriter.Flush();
        const auto fileBytes = file.ToArray();
        return {fileBytes.begin(), fileBytes.end()};
    }

    class ScratchContentRoot
    {
    public:
        ScratchContentRoot()
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_compiled_fx_xnb_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_);
        }

        ~ScratchContentRoot()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        [[nodiscard]] const std::filesystem::path& GetPath() const { return path_; }

    private:
        std::filesystem::path path_;
    };

    class BuiltInReaderScope
    {
    public:
        BuiltInReaderScope()
        {
            Microsoft::Xna::Framework::Content::ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
        }

        ~BuiltInReaderScope()
        {
            Microsoft::Xna::Framework::Content::ContentTypeReaderManager::ClearTypeCreators();
        }
    };
}

TEST(Fna3dCompiledEffectTest, StockFixtureHashesMatchDocumentedFnaRevision)
{
    constexpr std::pair<const char*, const char*> fixtures[] = {
        {"AlphaTestEffect.fxb", "6db696511b0a5ae52be02cff9c902d28046adbcb7e6c156a09b7e3f630153a48"},
        {"BasicEffect.fxb", "b3cedbb929418ba6eb7408a973842fb214b598ad35c3670b3e5af58b7b5ec0b7"},
        {"DualTextureEffect.fxb", "e3c5814923f6c8bb0007a0dc45ad7ca4031ee2de0e3c261484b62a5639ed8025"},
        {"EnvironmentMapEffect.fxb", "3b66dc7858036e8f5b8bc87471ca0b88a9f8a2871baa16a21629e940945e8395"},
        {"SkinnedEffect.fxb", "933a315f3a1352c634fd4b023bd7a428ca55ee4dfe48e55115bd3658a8c94931"},
        {"SpriteEffect.fxb", "ebed64c8f19e79ebc31148ee3ac8c32dca309e8e0145f698f7348e3741dd8c56"},
    };
    for (const auto& [name, expected] : fixtures)
    {
        SCOPED_TRACE(name);
        const auto bytes = LoadStockEffect(name);
        ASSERT_FALSE(bytes.empty());
        System::Security::Cryptography::SHA256 sha;
        EXPECT_EQ(HexDigest(sha.ComputeHash(bytes)), expected);
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

TEST(Fna3dCompiledEffectTest, MatrixArrayElementsSharePaddedTopLevelStorage)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    Effect effect(device, LoadStockEffect("SkinnedEffect.fxb"));
    auto* bones = effect.getParametersProperty()["Bones"];
    ASSERT_NE(bones, nullptr);
    ASSERT_EQ(bones->getElementsProperty().getCountProperty(), 72);

    const Matrix first = Matrix::CreateTranslation(2.0f, 3.0f, 4.0f);
    const Matrix second = Matrix::CreateTranslation(5.0f, 6.0f, 7.0f);
    bones->SetValue(std::vector<Matrix>{first, second});
    auto values = bones->GetValueMatrixArray(2);
    ASSERT_EQ(values.size(), 2u);
    EXPECT_EQ(values[0], first);
    EXPECT_EQ(values[1], second);

    const Matrix replacement = Matrix::CreateTranslation(11.0f, 13.0f, 17.0f);
    bones->getElementsProperty()[1].SetValue(replacement);
    values = bones->GetValueMatrixArray(2);
    ASSERT_EQ(values.size(), 2u);
    EXPECT_EQ(values[0], first);
    EXPECT_EQ(values[1], replacement);
    EXPECT_NO_THROW(effect.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply());
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

TEST(Fna3dCompiledEffectTest, RepeatedClonesApplyAndDisposeIndependently)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    Effect original(device, LoadStockEffect("BasicEffect.fxb"));
    for (int i = 0; i < 32; ++i)
    {
        std::unique_ptr<Effect> clone(original.Clone());
        clone->getParametersProperty()["DiffuseColor"]->SetValue(
            Microsoft::Xna::Framework::Vector3(
                static_cast<float>(i) / 31.0f, 0.5f, 1.0f));
        EXPECT_NO_THROW(clone->getCurrentTechniqueProperty()->getPassesProperty()[0].Apply());
        clone->Dispose();
        EXPECT_TRUE(clone->getIsDisposedProperty());
    }
    EXPECT_NO_THROW(original.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply());
}

TEST(Fna3dCompiledEffectTest, DisposingSelectedEffectPreventsStaleDraw)
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    GraphicsDevice device;
    Effect effect(device, LoadStockEffect("BasicEffect.fxb"));
    effect.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply();
    effect.Dispose();

    const VertexPositionColor triangle[3] = {
        { Vector3(-0.5f, 0.5f, 0.0f), Color::White },
        { Vector3(-0.5f, -0.5f, 0.0f), Color::White },
        { Vector3(0.5f, -0.5f, 0.0f), Color::White },
    };
    try
    {
        device.DrawUserPrimitives(PrimitiveType::TriangleList, triangle, 0, 1);
        FAIL() << "disposing the selected effect must clear GraphicsDevice's raw selection";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_NE(std::string(error.what()).find("no effect"), std::string::npos);
    }
}

TEST(Fna3dCompiledEffectTest, DeviceDisposalReleasesCompiledEffectBeforeRenderer)
{
    GraphicsDevice device;
    auto effect = std::make_unique<Effect>(device, LoadStockEffect("BasicEffect.fxb"));
    effect->getCurrentTechniqueProperty()->getPassesProperty()[0].Apply();

    EXPECT_NO_THROW(device.Dispose());
    EXPECT_TRUE(effect->getIsDisposedProperty());
    EXPECT_NO_THROW(effect.reset());
}

TEST(Fna3dCompiledEffectTest, ContentManagerLoadsXnbEffectAndRendersIt)
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Content;
    using namespace Microsoft::Xna::Framework::Graphics;

    GraphicsDevice device;
    BuiltInReaderScope readers;
    ScratchContentRoot contentRoot;
    const auto xnb = BuildEffectXnb(LoadStockEffect("BasicEffect.fxb"));
    const auto assetPath = contentRoot.GetPath() / "custom.xnb";
    std::ofstream output(assetPath, std::ios::binary);
    output.write(reinterpret_cast<const char*>(xnb.data()),
                 static_cast<std::streamsize>(xnb.size()));
    output.close();

    ContentManager content(nullptr, contentRoot.GetPath().string());
    content.setGraphicsDevice(device);
    const auto effect = content.Load<std::shared_ptr<Effect>>("custom");
    ASSERT_NE(effect, nullptr);
    EXPECT_EQ(effect->getNameProperty(), "custom");
    effect->getParametersProperty()["WorldViewProj"]->SetValue(Matrix::getIdentityProperty());
    effect->getParametersProperty()["DiffuseColor"]->SetValue(
        Vector4(1.0f, 1.0f, 1.0f, 1.0f));
    effect->getParametersProperty()["ShaderIndex"]->SetValue(3);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.Clear(Color::Black);
    effect->getCurrentTechniqueProperty()->getPassesProperty()[0].Apply();

    const VertexPositionColor vertices[6] = {
        { Vector3(-1.0f, 1.0f, 0.5f), Color(40, 80, 230, 255) },
        { Vector3(-1.0f, -1.0f, 0.5f), Color(40, 80, 230, 255) },
        { Vector3(1.0f, -1.0f, 0.5f), Color(40, 80, 230, 255) },
        { Vector3(-1.0f, 1.0f, 0.5f), Color(40, 80, 230, 255) },
        { Vector3(1.0f, -1.0f, 0.5f), Color(40, 80, 230, 255) },
        { Vector3(1.0f, 1.0f, 0.5f), Color(40, 80, 230, 255) },
    };
    device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);

    const auto& viewport = device.getViewportProperty();
    const Rectangle center(viewport.getWidthProperty() / 2,
                           viewport.getHeightProperty() / 2, 1, 1);
    Color pixel(0, 0, 0, 0);
    device.GetBackBufferData(&center, &pixel, 0, 1);
    EXPECT_NEAR(pixel.getRProperty(), 40, 2);
    EXPECT_NEAR(pixel.getGProperty(), 80, 2);
    EXPECT_NEAR(pixel.getBProperty(), 230, 2);
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
