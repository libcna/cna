// SPDX-License-Identifier: MS-PL
// Effect base class coverage for the stock-derived path plus format/capability checks shared by
// the compiled XNA Effect Framework path. Native bytecode integration lives with each backend.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectMaterial.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentException.hpp"
#include "System/InvalidCastException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "CNA/TestSupport/TestPaths.hpp"

using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::EffectPass;
using Microsoft::Xna::Framework::Graphics::EffectTechnique;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;

namespace
{
    std::uint32_t ReadUInt32LittleEndian(const std::vector<SharpRuntime::bytecs>& bytes,
                                         std::size_t offset)
    {
        return static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    }

    void WriteUInt32LittleEndian(std::vector<SharpRuntime::bytecs>& bytes,
                                 std::size_t offset, std::uint32_t value)
    {
        for (std::size_t i = 0; i < 4; ++i)
            bytes[offset + i] = static_cast<SharpRuntime::bytecs>(value >> (i * 8));
    }

    std::vector<SharpRuntime::bytecs> LoadValidCompiledEffectFixture()
    {
        const std::filesystem::path path =
            CNA::TestSupport::CompiledEffectDirectory() / "BasicEffect.fxb";
        std::ifstream input(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    std::vector<SharpRuntime::bytecs> LoadAuthenticRacingCompiledEffectFixture()
    {
        const std::filesystem::path path =
            CNA::TestSupport::CompiledEffectFixtureDirectory() /
            "racing-shadow-map-xna4.fxb";
        std::ifstream input(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    std::vector<SharpRuntime::bytecs> LoadConformanceCompiledEffectFixture()
    {
        const std::filesystem::path path =
            CNA::TestSupport::CompiledEffectDirectory() / "CnaConformanceEffect.fxb";
        std::ifstream input(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    std::size_t FindFirstPassRenderStateOffset(
        const std::vector<SharpRuntime::bytecs>& bytes)
    {
        if (bytes.size() < 24u) return bytes.size();

        std::size_t tokenOffset = 0;
        if (ReadUInt32LittleEndian(bytes, 0) == 0xBCF00BCFu)
            tokenOffset = ReadUInt32LittleEndian(bytes, 4);
        if (tokenOffset > bytes.size() - 8u) return bytes.size();

        const std::size_t base = tokenOffset + 8u;
        const std::size_t structure = base + ReadUInt32LittleEndian(bytes, tokenOffset + 4u);
        if (structure > bytes.size() - 16u) return bytes.size();

        const std::uint32_t parameterCount = ReadUInt32LittleEndian(bytes, structure);
        const std::uint32_t techniqueCount = ReadUInt32LittleEndian(bytes, structure + 4u);
        if (techniqueCount == 0) return bytes.size();

        std::size_t cursor = structure + 16u;
        for (std::uint32_t i = 0; i < parameterCount; ++i)
        {
            if (cursor > bytes.size() - 16u) return bytes.size();
            const std::uint32_t annotationCount = ReadUInt32LittleEndian(bytes, cursor + 12u);
            cursor += 16u;
            if (annotationCount > (bytes.size() - cursor) / 8u) return bytes.size();
            cursor += static_cast<std::size_t>(annotationCount) * 8u;
        }

        if (cursor > bytes.size() - 12u) return bytes.size();
        const std::uint32_t techniqueAnnotationCount =
            ReadUInt32LittleEndian(bytes, cursor + 4u);
        const std::uint32_t passCount = ReadUInt32LittleEndian(bytes, cursor + 8u);
        cursor += 12u;
        if (passCount == 0 || techniqueAnnotationCount > (bytes.size() - cursor) / 8u)
            return bytes.size();
        cursor += static_cast<std::size_t>(techniqueAnnotationCount) * 8u;

        if (cursor > bytes.size() - 12u) return bytes.size();
        const std::uint32_t passAnnotationCount = ReadUInt32LittleEndian(bytes, cursor + 4u);
        const std::uint32_t stateCount = ReadUInt32LittleEndian(bytes, cursor + 8u);
        cursor += 12u;
        if (stateCount == 0 || passAnnotationCount > (bytes.size() - cursor) / 8u)
            return bytes.size();
        cursor += static_cast<std::size_t>(passAnnotationCount) * 8u;
        return cursor <= bytes.size() - 4u ? cursor : bytes.size();
    }

    // Minimal derived Effect used to observe OnApply dispatch on the stock-style path.
    class TestEffect : public Effect
    {
    public:
        explicit TestEffect(GraphicsDevice& device) : Effect(device) {}
        TestEffect(GraphicsDevice& device, const std::vector<SharpRuntime::bytecs>& effectCode)
            : Effect(device, effectCode) {}
        explicit TestEffect(const TestEffect& src) : Effect(src) {}

        int applyCount = 0;

        Effect* Clone() override { return new TestEffect(*this); }

    protected:
        void OnApply() override { ++applyCount; }
    };
}

// -----------------------------------------------------------------------
// Construction
// -----------------------------------------------------------------------

TEST(EffectTest, ConstructorCreatesExactlyOneDefaultTechnique)
{
    GraphicsDevice gd;
    TestEffect fx(gd);

    ASSERT_EQ(fx.getTechniquesProperty().getCountProperty(), 1);
    EXPECT_EQ(fx.getTechniquesProperty()[0]->getNameProperty(), "Default");
}

TEST(EffectTest, ConstructorSelectsFirstTechniqueAsCurrent)
{
    GraphicsDevice gd;
    TestEffect fx(gd);

    ASSERT_NE(fx.getCurrentTechniqueProperty(), nullptr);
    EXPECT_EQ(fx.getCurrentTechniqueProperty(), fx.getTechniquesProperty()[0]);
}

TEST(EffectTest, ConstructorLeavesParametersEmpty)
{
    GraphicsDevice gd;
    TestEffect fx(gd);

    EXPECT_EQ(fx.getParametersProperty().getCountProperty(), 0);
    EXPECT_EQ(std::as_const(fx).getParametersProperty().getCountProperty(), 0);
}

TEST(EffectTest, GetTechniquesConstOverloadMatchesMutable)
{
    GraphicsDevice gd;
    TestEffect fx(gd);

    const Effect& constFx = fx;
    EXPECT_EQ(constFx.getTechniquesProperty()[0], fx.getTechniquesProperty()[0]);
}

TEST(EffectTest, GraphicsDeviceInternalReturnsOwningDevice)
{
    GraphicsDevice gd;
    TestEffect fx(gd);

    EXPECT_EQ(&fx.getGraphicsDeviceInternal(), &gd);
}

// -----------------------------------------------------------------------
// Compiled bytecode is an explicit renderer capability. A renderer without a
// D3D9 Effect Framework runtime must reject it before attempting to parse it.
// -----------------------------------------------------------------------

TEST(EffectTest, BytecodeConstructorUsesExplicitRendererCapability)
{
    GraphicsDevice gd;
    const std::vector<SharpRuntime::bytecs> fakeBytecode{ 1, 2, 3, 4 };

    EXPECT_THROW(TestEffect(gd, fakeBytecode), System::ArgumentException);
}

TEST(EffectTest, BytecodeConstructorFailureIsActionable)
{
    GraphicsDevice gd;
    const std::vector<SharpRuntime::bytecs> fakeBytecode{ 1, 2, 3, 4 };

    try
    {
        TestEffect fx(gd, fakeBytecode);
        FAIL() << "Expected invalid or unsupported bytecode to throw";
    }
    catch (const System::ArgumentException& e)
    {
        EXPECT_NE(std::string(e.what()).find("effect bytecode"), std::string::npos);
    }
}

TEST(EffectTest, MgfxIsRejectedAsADistinctUnsupportedFormat)
{
    GraphicsDevice gd;
    const std::vector<SharpRuntime::bytecs> mgfx{
        'M', 'G', 'F', 'X', 1, 0, 0, 0
    };

    EXPECT_THROW(TestEffect(gd, mgfx), System::NotSupportedException);
}

TEST(EffectTest, StructurallyValidFxReachesRendererCapabilityGate)
{
    GraphicsDevice gd;
    const auto validEffect = LoadValidCompiledEffectFixture();
    ASSERT_FALSE(validEffect.empty());

    if (!gd.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
    {
        EXPECT_THROW(TestEffect(gd, validEffect), System::NotSupportedException);
    }
    else
    {
        EXPECT_NO_THROW(TestEffect(gd, validEffect));
    }
}

TEST(EffectTest, CompiledTextureAccessorsValidateTheReflectedParameterType)
{
    GraphicsDevice gd;
    const auto typedBytes = LoadValidCompiledEffectFixture();
    const auto genericBytes = LoadConformanceCompiledEffectFixture();
    ASSERT_FALSE(typedBytes.empty());
    ASSERT_FALSE(genericBytes.empty());

    if (!gd.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
    {
        GTEST_SKIP() << "renderer does not execute compiled effects";
    }

    TestEffect typedEffect(gd, typedBytes);
    TestEffect genericEffect(gd, genericBytes);
    auto* scalar = typedEffect.getParametersProperty()["DiffuseColor"];
    auto* texture2D = typedEffect.getParametersProperty()["Texture"];
    auto* genericTexture = genericEffect.getParametersProperty()["FxTexture"];
    ASSERT_NE(scalar, nullptr);
    ASSERT_NE(texture2D, nullptr);
    ASSERT_NE(genericTexture, nullptr);

    EXPECT_THROW(static_cast<void>(scalar->GetValueTexture2D()), System::InvalidCastException);
    EXPECT_THROW(static_cast<void>(scalar->GetValueTexture3D()), System::InvalidCastException);
    EXPECT_THROW(static_cast<void>(scalar->GetValueTextureCube()), System::InvalidCastException);
    EXPECT_THROW(scalar->SetValue(static_cast<Microsoft::Xna::Framework::Graphics::Texture*>(nullptr)),
                 System::InvalidCastException);

    EXPECT_EQ(texture2D->GetValueTexture2D(), nullptr);
    EXPECT_THROW(static_cast<void>(texture2D->GetValueTexture3D()),
                 System::InvalidCastException);
    EXPECT_THROW(static_cast<void>(texture2D->GetValueTextureCube()),
                 System::InvalidCastException);
    EXPECT_NO_THROW(texture2D->SetValue(
        static_cast<Microsoft::Xna::Framework::Graphics::Texture2D*>(nullptr)));

    EXPECT_EQ(genericTexture->GetValueTexture2D(), nullptr);
    EXPECT_EQ(genericTexture->GetValueTexture3D(), nullptr);
    EXPECT_EQ(genericTexture->GetValueTextureCube(), nullptr);
}

TEST(EffectTest, CompiledTextureSetterRejectsDisposedAndActiveRenderTargetsBeforeType)
{
    GraphicsDevice gd;
    const auto bytes = LoadValidCompiledEffectFixture();
    ASSERT_FALSE(bytes.empty());

    if (!gd.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
    {
        GTEST_SKIP() << "renderer does not execute compiled effects";
    }

    TestEffect effect(gd, bytes);
    auto* scalar = effect.getParametersProperty()["DiffuseColor"];
    auto* texture2D = effect.getParametersProperty()["Texture"];
    ASSERT_NE(scalar, nullptr);
    ASSERT_NE(texture2D, nullptr);

    Microsoft::Xna::Framework::Graphics::Texture2D disposed(gd, 1, 1);
    disposed.Dispose();
    EXPECT_THROW(texture2D->SetValue(&disposed), System::ObjectDisposedException);
    EXPECT_THROW(scalar->SetValue(&disposed), System::ObjectDisposedException)
        << "texture lifetime validation precedes reflected parameter-type validation";

    Microsoft::Xna::Framework::Graphics::RenderTarget2D active(gd, 1, 1);
    gd.SetRenderTarget(&active);
    EXPECT_THROW(texture2D->SetValue(&active), System::InvalidOperationException);
    EXPECT_THROW(scalar->SetValue(&active), System::InvalidOperationException)
        << "active-target validation precedes reflected parameter-type validation";
    gd.SetRenderTarget(nullptr);

    EXPECT_NO_THROW(texture2D->SetValue(&active));
    EXPECT_EQ(texture2D->GetValueTexture2D(), &active);
}

TEST(EffectTest, CompiledTypedValueSettersValidateReflectedShape)
{
    GraphicsDevice gd;
    const auto bytes = LoadConformanceCompiledEffectFixture();
    ASSERT_FALSE(bytes.empty());

    if (!gd.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
    {
        GTEST_SKIP() << "renderer does not execute compiled effects";
    }

    Effect effect(gd, bytes);
    auto& parameters = effect.getParametersProperty();
    auto* gain = parameters["Gain"];
    auto* tint = parameters["Tint"];
    auto* transform = parameters["Transform"];
    auto* weights = parameters["Weights"];
    auto* lighting = parameters["Lighting"];
    ASSERT_NE(gain, nullptr);
    ASSERT_NE(tint, nullptr);
    ASSERT_NE(transform, nullptr);
    ASSERT_NE(weights, nullptr);
    ASSERT_NE(lighting, nullptr);
    auto* direction = lighting->getStructureMembersProperty()["Direction"];
    ASSERT_NE(direction, nullptr);

    EXPECT_THROW(gain->SetValue(Vector2::One), System::InvalidCastException);
    EXPECT_THROW(gain->SetValue(Vector4::One), System::InvalidCastException);
    EXPECT_THROW(tint->SetValue(Vector3::One), System::InvalidCastException);
    EXPECT_THROW(transform->SetValue(Vector4::One), System::InvalidCastException);
    EXPECT_THROW(tint->SetValue(Matrix::getIdentityProperty()), System::InvalidCastException);
    EXPECT_THROW(gain->SetValueTranspose(Matrix::getIdentityProperty()),
                 System::InvalidCastException);
    EXPECT_THROW(weights->SetValue(Vector4::One), System::InvalidCastException)
        << "a scalar overload cannot address an array parent";

    EXPECT_THROW(tint->SetValue(std::vector<Vector4>{Vector4::One}),
                 System::InvalidCastException);
    EXPECT_THROW(transform->SetValue(
                     std::vector<Matrix>{Matrix::getIdentityProperty()}),
                 System::InvalidCastException);
    EXPECT_THROW(weights->SetValue(std::vector<Vector4>{Vector4::One}),
                 System::InvalidCastException);

    EXPECT_NO_THROW(tint->SetValue(Quaternion(0.0f, 0.0f, 0.0f, 1.0f)));
    EXPECT_NO_THROW(tint->SetValue(Vector4::One));
    EXPECT_NO_THROW(direction->SetValue(Vector3::One));
    EXPECT_NO_THROW(transform->SetValue(Matrix::getIdentityProperty()));
    EXPECT_NO_THROW(weights->SetValue(std::vector<float>{0.25f, 0.75f}));

    const std::filesystem::path skinnedPath =
        CNA::TestSupport::CompiledEffectDirectory() / "SkinnedEffect.fxb";
    std::ifstream skinnedInput(skinnedPath, std::ios::binary);
    const std::vector<SharpRuntime::bytecs> skinnedBytes{
        std::istreambuf_iterator<char>(skinnedInput), std::istreambuf_iterator<char>()};
    ASSERT_FALSE(skinnedBytes.empty());
    Effect skinned(gd, skinnedBytes);
    auto* bones = skinned.getParametersProperty()["Bones"];
    ASSERT_NE(bones, nullptr);
    ASSERT_EQ(bones->getElementsProperty().getCountProperty(), 72);
    EXPECT_NO_THROW(bones->SetValue(
        std::vector<Matrix>(72, Matrix::getIdentityProperty())));
    EXPECT_THROW(bones->SetValue(
                     std::vector<Matrix>(73, Matrix::getIdentityProperty())),
                 System::InvalidCastException);
}

TEST(EffectTest, AuthenticXna4EffectAcceptsRepeatedAndAuxiliaryObjectRecords)
{
    GraphicsDevice gd;
    const auto racingEffect = LoadAuthenticRacingCompiledEffectFixture();
    ASSERT_EQ(racingEffect.size(), 17404u);

    if (!gd.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
    {
        EXPECT_THROW(TestEffect(gd, racingEffect), System::NotSupportedException);
        return;
    }

    TestEffect effect(gd, racingEffect);
    // MojoShader reports 19 raw parameters; XNA's public EffectParameterCollection
    // omits the three internal sampler objects and exposes 16.
    EXPECT_EQ(effect.getParametersProperty().getCountProperty(), 16);
    EXPECT_EQ(effect.getTechniquesProperty().getCountProperty(), 4);
    for (int techniqueIndex = 0;
         techniqueIndex < effect.getTechniquesProperty().getCountProperty();
         ++techniqueIndex)
    {
        EXPECT_EQ(effect.getTechniquesProperty()[techniqueIndex]
                      ->getPassesProperty().getCountProperty(),
                  1);
    }
}

TEST(EffectTest, AuthenticXna4ShaderStateIdentifiersSurvivePassApplication)
{
    GraphicsDevice gd;
    const auto racingEffect = LoadAuthenticRacingCompiledEffectFixture();
    ASSERT_FALSE(racingEffect.empty());

    if (!gd.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
    {
        GTEST_SKIP() << "renderer does not execute compiled effects";
    }

    TestEffect effect(gd, racingEffect);
    ASSERT_GE(effect.getTechniquesProperty().getCountProperty(), 2);
    EffectTechnique& technique = *effect.getTechniquesProperty()[1];
    ASSERT_EQ(technique.getPassesProperty().getCountProperty(), 1);
    effect.setCurrentTechniqueProperty(&technique);

    // The authentic XNA 4 payload stores VertexShader/PixelShader as MojoShader render-state
    // identifiers 146/147. If either identifier is incorrectly stripped to 18/19, applying this
    // pass reports unsupported FogStart/FogEnd before any draw can occur.
    EXPECT_NO_THROW(technique.getPassesProperty()[0]->Apply());
}

TEST(EffectTest, RejectsInvalidRenderStateIdentifierBeforeEnumConversion)
{
    GraphicsDevice gd;
    auto bytes = LoadAuthenticRacingCompiledEffectFixture();
    ASSERT_FALSE(bytes.empty());
    const std::size_t stateTypeOffset = FindFirstPassRenderStateOffset(bytes);
    ASSERT_LT(stateTypeOffset, bytes.size());
    ASSERT_EQ(ReadUInt32LittleEndian(bytes, stateTypeOffset), 8u);

    WriteUInt32LittleEndian(bytes, stateTypeOffset, 0xFFFFFFFFu);
    EXPECT_THROW(TestEffect(gd, bytes), System::ArgumentException);
}

TEST(EffectTest, RejectsUnsafeXna4WrapperOffsetBeforeNativeParser)
{
    GraphicsDevice gd;
    const std::vector<SharpRuntime::bytecs> malformedWrapper{
        0xCF, 0x0B, 0xF0, 0xBC, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0
    };
    EXPECT_THROW(TestEffect(gd, malformedWrapper), System::ArgumentException);
}

TEST(EffectTest, RejectsExcessiveTopLevelReflectionCountBeforeNativeParser)
{
    GraphicsDevice gd;
    auto bytes = LoadValidCompiledEffectFixture();
    ASSERT_GE(bytes.size(), 24u);
    const std::size_t structure = 8u + ReadUInt32LittleEndian(bytes, 4);
    ASSERT_LE(structure + 16u, bytes.size());
    WriteUInt32LittleEndian(bytes, structure, 0xFFFFFFFFu);

    EXPECT_THROW(TestEffect(gd, bytes), System::ArgumentException);
}

TEST(EffectTest, RejectsOutOfRangeValueOffsetsBeforeNativeParser)
{
    GraphicsDevice gd;
    auto bytes = LoadValidCompiledEffectFixture();
    ASSERT_GE(bytes.size(), 24u);
    const std::size_t structure = 8u + ReadUInt32LittleEndian(bytes, 4);
    ASSERT_LE(structure + 24u, bytes.size());

    auto invalidType = bytes;
    WriteUInt32LittleEndian(invalidType, structure + 16, 0xFFFFFFFFu);
    EXPECT_THROW(TestEffect(gd, invalidType), System::ArgumentException);

    auto invalidValue = bytes;
    WriteUInt32LittleEndian(invalidValue, structure + 20, 0xFFFFFFFFu);
    EXPECT_THROW(TestEffect(gd, invalidValue), System::ArgumentException);
}

TEST(EffectTest, RejectsUnterminatedReflectionStringsBeforeNativeParser)
{
    GraphicsDevice gd;
    auto bytes = LoadValidCompiledEffectFixture();
    ASSERT_GE(bytes.size(), 24u);
    const std::size_t base = 8;
    const std::size_t structure = base + ReadUInt32LittleEndian(bytes, 4);
    ASSERT_LE(structure + 20u, bytes.size());
    const std::size_t parameterType =
        base + ReadUInt32LittleEndian(bytes, structure + 16);
    ASSERT_LE(parameterType + 12u, bytes.size());
    const std::size_t parameterName =
        base + ReadUInt32LittleEndian(bytes, parameterType + 8);
    ASSERT_LE(parameterName + 4u, bytes.size());
    const std::uint32_t stringLength = ReadUInt32LittleEndian(bytes, parameterName);
    ASSERT_GT(stringLength, 0u);
    ASSERT_LE(parameterName + 4u + stringLength, bytes.size());
    bytes[parameterName + 4u + stringLength - 1] = 'X';

    EXPECT_THROW(TestEffect(gd, bytes), System::ArgumentException);
}

TEST(EffectTest, RejectsOutOfRangeObjectReferencesBeforeNativeParser)
{
    GraphicsDevice gd;
    auto bytes = LoadValidCompiledEffectFixture();
    ASSERT_GE(bytes.size(), 24u);
    const std::size_t base = 8;
    const std::size_t structure = base + ReadUInt32LittleEndian(bytes, 4);
    ASSERT_LE(structure + 24u, bytes.size());
    const std::size_t parameterValue =
        base + ReadUInt32LittleEndian(bytes, structure + 20);
    ASSERT_LE(parameterValue + 4u, bytes.size());
    WriteUInt32LittleEndian(bytes, parameterValue, 0xFFFFFFFFu);

    EXPECT_THROW(TestEffect(gd, bytes), System::ArgumentException);
}

// -----------------------------------------------------------------------
// CurrentTechnique — recovered Microsoft XNA validates the Effect lifetime,
// rejects null, and requires the technique to belong to the receiving Effect.
// -----------------------------------------------------------------------

TEST(EffectTest, SetCurrentTechniqueRejectsTechniqueOwnedByAnotherEffect)
{
    GraphicsDevice gd;
    TestEffect fx(gd);
    TestEffect other(gd);

    EXPECT_THROW(fx.setCurrentTechniqueProperty(other.getCurrentTechniqueProperty()),
                 System::InvalidOperationException);
    EXPECT_EQ(fx.getCurrentTechniqueProperty(), fx.getTechniquesProperty()[0]);
}

TEST(EffectTest, SetCurrentTechniqueRejectsNull)
{
    GraphicsDevice gd;
    TestEffect fx(gd);

    EXPECT_THROW(fx.setCurrentTechniqueProperty(nullptr), System::ArgumentNullException);
    EXPECT_EQ(fx.getCurrentTechniqueProperty(), fx.getTechniquesProperty()[0]);
}

TEST(EffectTest, SetCurrentTechniqueAfterDisposeThrowsEvenForCurrentValue)
{
    GraphicsDevice gd;
    TestEffect fx(gd);
    EffectTechnique* current = fx.getCurrentTechniqueProperty();
    fx.Dispose();

    EXPECT_THROW(fx.setCurrentTechniqueProperty(current), System::ObjectDisposedException);
    EXPECT_EQ(fx.getCurrentTechniqueProperty(), current);
}

// -----------------------------------------------------------------------
// Apply() — dispatches to OnApply() and makes the effect the device's
// current effect (verified indirectly: DrawPrimitives requires a
// previously-applied effect, matching GraphicsDevice::DrawPrimitives's
// "no effect has been applied" guard).
// -----------------------------------------------------------------------

class EffectApplyTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
    TestEffect fx{gd};

    VertexDeclaration MakeDecl()
    {
        return VertexDeclaration({
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0)
        });
    }

    void SetUp() override
    {
        // DrawPrimitives with a bound VertexBuffer is inherently a 3D-pipeline entry point -- a
        // renderer that honestly reports no 3D pipeline rejects VertexBuffer construction itself
        // before either test body reaches the missing-effect guard it exercises.
        if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
            GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    }
};

TEST_F(EffectApplyTest, ApplyInvokesOnApply)
{
    EXPECT_EQ(fx.applyCount, 0);
    fx.Apply();
    EXPECT_EQ(fx.applyCount, 1);
    fx.Apply();
    EXPECT_EQ(fx.applyCount, 2);
}

TEST_F(EffectApplyTest, DrawPrimitivesThrowsWithoutPriorApply)
{
    std::vector<VertexPositionColor> vpc {
        { Vector3(0.f, 0.f, 0.f), Color(255, 0, 0, 255) },
        { Vector3(1.f, 0.f, 0.f), Color(0, 255, 0, 255) },
        { Vector3(0.f, 1.f, 0.f), Color(0, 0, 255, 255) }
    };
    VertexBuffer vb(gd, MakeDecl(), 3, BufferUsage::None);
    vb.SetData(vpc.data(), 3);
    gd.SetVertexBuffer(&vb);

    EXPECT_THROW(
        gd.DrawPrimitives(PrimitiveType::TriangleList, 0, 1),
        System::InvalidOperationException);
}

TEST_F(EffectApplyTest, ApplyMakesEffectCurrentSoDrawPrimitivesNoLongerThrowsForMissingEffect)
{
    std::vector<VertexPositionColor> vpc {
        { Vector3(0.f, 0.f, 0.f), Color(255, 0, 0, 255) },
        { Vector3(1.f, 0.f, 0.f), Color(0, 255, 0, 255) },
        { Vector3(0.f, 1.f, 0.f), Color(0, 0, 255, 255) }
    };
    VertexBuffer vb(gd, MakeDecl(), 3, BufferUsage::None);
    vb.SetData(vpc.data(), 3);
    gd.SetVertexBuffer(&vb);

    fx.Apply();

    // "no effect has been applied" is exactly the guard fx.Apply() must clear;
    // any other exception (or none) means the guard was satisfied.
    try
    {
        gd.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
    }
    catch (const std::runtime_error& e)
    {
        EXPECT_STRNE(e.what(), std::string("GraphicsDevice::DrawPrimitives: no effect has been applied.").c_str());
    }
}

TEST_F(EffectApplyTest, ApplyAfterDisposeThrowsObjectDisposedException)
{
    fx.Dispose();
    EXPECT_THROW(fx.Apply(), System::ObjectDisposedException);
}

// -----------------------------------------------------------------------
// Task 360: effect lifecycle — dispose idempotency and apply-after-dispose
// coverage across every entry point. FNA's own Effect.Dispose(bool)/
// GraphicsResource.Dispose(bool) are both guarded by `if (!IsDisposed)`, so
// a second Dispose() call is always a safe no-op — CNA's
// GraphicsResource::Dispose(bool) carries the identical `if (isDisposed_)
// return;` guard. Note: FNA's own EffectPass.Apply()/Effect has NO
// IsDisposed check at all (verified by reading Effect.cs/EffectPass.cs) —
// calling Apply() on a disposed FNA effect hands a zeroed-out native handle
// straight to FNA3D_ApplyEffect, undefined behavior at the native layer.
// CNA's Effect::Apply() throwing ObjectDisposedException is a deliberate,
// confirmed, beneficial CNA-specific safety improvement over FNA's silent
// UB here, not a bug to match — same category of intentional divergence as
// GraphicsAdapter::DeviceId/VendorId (Task 345).
// -----------------------------------------------------------------------

TEST_F(EffectApplyTest, DisposeIsIdempotentAndDoesNotThrow)
{
    EXPECT_NO_THROW(fx.Dispose());
    EXPECT_NO_THROW(fx.Dispose());
    EXPECT_TRUE(fx.getIsDisposedProperty());
}

TEST_F(EffectApplyTest, ApplyOnPassThrowsObjectDisposedExceptionWhenOwnerEffectDisposed)
{
    EffectPass& p0 = *fx.getTechniquesProperty()[0]->getPassesProperty()[0];
    fx.Dispose();
    EXPECT_THROW(p0.Apply(), System::ObjectDisposedException);
}

TEST_F(EffectApplyTest, DisposedEffectWinsBeforeNonCurrentPassValidation)
{
    EffectPass& originalPass = *fx.getTechniquesProperty()[0]->getPassesProperty()[0];
    fx.getTechniquesProperty().Add(EffectTechnique(&fx, "Second"));
    fx.setCurrentTechniqueProperty(fx.getTechniquesProperty()[1]);
    fx.Dispose();

    EXPECT_THROW(originalPass.Apply(), System::ObjectDisposedException);
}

// -----------------------------------------------------------------------
// Task 355: EffectPass::Apply() must throw System::InvalidOperationException
// ("Applied a pass not in the current technique!") when applied while it is
// not part of the effect's CurrentTechnique, matching FNA's own
// EffectPass.Apply() guard (Graphics/Effect/EffectPass.cs). Uses a mock
// TestEffect with a manually-added second technique since no real CNA stock
// effect creates more than one technique today.
// -----------------------------------------------------------------------

TEST_F(EffectApplyTest, ApplyOnPassOfCurrentTechniqueSucceedsAndInvokesOnApply)
{
    EffectPass& p0 = *fx.getTechniquesProperty()[0]->getPassesProperty()[0];

    EXPECT_NO_THROW(p0.Apply());
    EXPECT_EQ(fx.applyCount, 1);
}

TEST_F(EffectApplyTest, ApplyOnPassNotInCurrentTechniqueThrowsInvalidOperationException)
{
    EffectPass& originalPass = *fx.getTechniquesProperty()[0]->getPassesProperty()[0];
    fx.getTechniquesProperty().Add(EffectTechnique(&fx, "Second"));

    fx.setCurrentTechniqueProperty(fx.getTechniquesProperty()[1]);

    EXPECT_THROW(originalPass.Apply(), System::InvalidOperationException);
    EXPECT_EQ(fx.applyCount, 0);
}

TEST_F(EffectApplyTest, ApplyIsConsistentAcrossInterleavedTechniqueSwitches)
{
    fx.getTechniquesProperty().Add(EffectTechnique(&fx, "Second"));
    EffectPass& firstPass = *fx.getTechniquesProperty()[0]->getPassesProperty()[0];
    EffectPass& secondPass = *fx.getTechniquesProperty()[1]->getPassesProperty()[0];

    // CurrentTechnique still points at [0] (set at construction) — applying [1]'s pass
    // must fail, and applying [0]'s pass must keep succeeding.
    EXPECT_THROW(secondPass.Apply(), System::InvalidOperationException);
    EXPECT_NO_THROW(firstPass.Apply());
    EXPECT_EQ(fx.applyCount, 1);

    // Switch CurrentTechnique to [1]: now [1]'s pass succeeds and [0]'s pass fails —
    // no stale state lingers from the previous technique being current.
    fx.setCurrentTechniqueProperty(fx.getTechniquesProperty()[1]);
    EXPECT_THROW(firstPass.Apply(), System::InvalidOperationException);
    EXPECT_NO_THROW(secondPass.Apply());
    EXPECT_EQ(fx.applyCount, 2);
}

// -----------------------------------------------------------------------
// Task 356: verify that switching CurrentTechnique genuinely changes which
// EffectPassCollection is "the applied one" — accessed *through*
// CurrentTechnique itself (getCurrentTechniqueProperty()->getPassesProperty()),
// not via a directly-held technique index as Task 355's test above does.
// The separate validation tests above cover Microsoft's null, ownership and
// disposal checks; this block covers successful owned-technique transitions.
// -----------------------------------------------------------------------

TEST_F(EffectApplyTest, CurrentTechniquePropertyPassCollectionTracksSelectedTechnique)
{
    fx.getTechniquesProperty().Add(EffectTechnique(&fx, "Second"));

    // Immediately after construction, CurrentTechnique's own Passes collection
    // must be technique [0]'s, not technique [1]'s.
    EXPECT_EQ(fx.getCurrentTechniqueProperty()->getPassesProperty()[0],
              fx.getTechniquesProperty()[0]->getPassesProperty()[0]);
    EXPECT_NO_THROW(fx.getCurrentTechniqueProperty()->getPassesProperty()[0]->Apply());

    fx.setCurrentTechniqueProperty(fx.getTechniquesProperty()[1]);

    // After switching, CurrentTechnique's own Passes collection must now be
    // technique [1]'s — a real toggle, not a one-directional/stale snapshot.
    EXPECT_EQ(fx.getCurrentTechniqueProperty()->getPassesProperty()[0],
              fx.getTechniquesProperty()[1]->getPassesProperty()[0]);
    EXPECT_NO_THROW(fx.getCurrentTechniqueProperty()->getPassesProperty()[0]->Apply());

    fx.setCurrentTechniqueProperty(fx.getTechniquesProperty()[0]);

    // Switching back restores [0]'s pass collection as current — bidirectional.
    EXPECT_EQ(fx.getCurrentTechniqueProperty()->getPassesProperty()[0],
              fx.getTechniquesProperty()[0]->getPassesProperty()[0]);
    EXPECT_NO_THROW(fx.getCurrentTechniqueProperty()->getPassesProperty()[0]->Apply());
}

// -----------------------------------------------------------------------
// GetTypeName() — must be the fully-qualified .NET name per CLAUDE.md,
// matching every other GraphicsResource subclass's convention
// (RenderTarget2D, Texture3D, BasicEffect, ...).
// -----------------------------------------------------------------------

TEST(EffectTest, GetTypeNameIsFullyQualified)
{
    GraphicsDevice gd;
    TestEffect fx(gd);

    EXPECT_EQ(fx.GetTypeName(), "Microsoft.Xna.Framework.Graphics.Effect");
}

// -----------------------------------------------------------------------
// Disposal
// -----------------------------------------------------------------------

TEST(EffectTest, DisposeSetsIsDisposed)
{
    GraphicsDevice gd;
    TestEffect fx(gd);

    EXPECT_FALSE(fx.getIsDisposedProperty());
    fx.Dispose();
    EXPECT_TRUE(fx.getIsDisposedProperty());
}

// -----------------------------------------------------------------------
// Task 883: Effect::Clone() — base-class virtual dispatch contract. The
// concrete deep-copy shape (which fields get copied) is exercised per stock
// effect in each of their own test files; this file only covers what the
// pure-virtual base contract itself guarantees for any subclass.
// -----------------------------------------------------------------------

TEST(EffectTest, CloneThroughBasePointerDispatchesToDerivedOverride)
{
    GraphicsDevice gd;
    TestEffect fx(gd);

    Effect& baseRef = fx;
    std::unique_ptr<Effect> clone(baseRef.Clone());

    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<TestEffect*>(clone.get()), nullptr);
    EXPECT_NE(static_cast<Effect*>(clone.get()), static_cast<Effect*>(&fx));
}

TEST(EffectTest, CloneGetsIndependentTechniqueNotAliasedToOriginal)
{
    GraphicsDevice gd;
    TestEffect fx(gd);
    std::unique_ptr<Effect> clone(fx.Clone());

    // The clone's own Default technique/pass must be distinct objects from the
    // original's, so EffectPass::Apply()'s owner_/techniqueId_ check on either
    // side can never accidentally resolve against the other effect's state.
    EXPECT_NE(clone->getCurrentTechniqueProperty(), fx.getCurrentTechniqueProperty());
    EXPECT_NE(clone->getTechniquesProperty()[0]->getPassesProperty()[0],
              fx.getTechniquesProperty()[0]->getPassesProperty()[0]);

    // Applying the clone's pass must not affect the original's apply count, and
    // vice versa — confirms there is no shared/aliased state between the two.
    auto& clonedTestFx = static_cast<TestEffect&>(*clone);
    clone->getTechniquesProperty()[0]->getPassesProperty()[0]->Apply();
    EXPECT_EQ(clonedTestFx.applyCount, 1);
    EXPECT_EQ(fx.applyCount, 0);
}

TEST(EffectTest, CloneAfterDisposeThrowsObjectDisposedException)
{
    GraphicsDevice gd;
    TestEffect fx(gd);
    fx.Dispose();

    EXPECT_THROW(std::unique_ptr<Effect>(fx.Clone()), System::ObjectDisposedException);
}

TEST(EffectTest, StockEffectClonesRejectDisposedSources)
{
    GraphicsDevice gd;
    Microsoft::Xna::Framework::Graphics::AlphaTestEffect alphaTest(gd);
    Microsoft::Xna::Framework::Graphics::BasicEffect basic(gd);
    Microsoft::Xna::Framework::Graphics::DualTextureEffect dualTexture(gd);
    Microsoft::Xna::Framework::Graphics::EnvironmentMapEffect environmentMap(gd);
    Microsoft::Xna::Framework::Graphics::SkinnedEffect skinned(gd);
    Microsoft::Xna::Framework::Graphics::SpriteEffect sprite(gd);
    TestEffect materialSource(gd);
    Microsoft::Xna::Framework::Graphics::EffectMaterial material(materialSource);

    alphaTest.Dispose();
    basic.Dispose();
    dualTexture.Dispose();
    environmentMap.Dispose();
    skinned.Dispose();
    sprite.Dispose();
    material.Dispose();

    EXPECT_THROW(std::unique_ptr<Effect>(alphaTest.Clone()), System::ObjectDisposedException);
    EXPECT_THROW(std::unique_ptr<Effect>(basic.Clone()), System::ObjectDisposedException);
    EXPECT_THROW(std::unique_ptr<Effect>(dualTexture.Clone()), System::ObjectDisposedException);
    EXPECT_THROW(std::unique_ptr<Effect>(environmentMap.Clone()), System::ObjectDisposedException);
    EXPECT_THROW(std::unique_ptr<Effect>(skinned.Clone()), System::ObjectDisposedException);
    EXPECT_THROW(std::unique_ptr<Effect>(sprite.Clone()), System::ObjectDisposedException);
    EXPECT_THROW(std::unique_ptr<Effect>(material.Clone()), System::ObjectDisposedException);
}
