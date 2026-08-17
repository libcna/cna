// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Json.hpp"
#include "CNA/TestSupport/CompiledEffectConformance.hpp"
#include "CNA/TestSupport/CompiledEffectFixtures.hpp"
#include "CNA/TestSupport/CompiledEffectFormat.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dApi.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "System/ArgumentException.hpp"
#include "System/Security/Cryptography/SHA256.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

#include <algorithm>
#include <filesystem>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using CNA::TestSupport::BuildSyntheticConformanceEffect;
using CNA::TestSupport::BuildSyntheticEffect;
using CNA::TestSupport::FloatBits;
using CNA::TestSupport::SyntheticEffectOptions;
using CNA::TestSupport::SyntheticRenderState;
using CNA::TestSupport::SyntheticSamplerState;
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

    struct DeterministicRng
    {
        std::uint64_t state;

        std::uint32_t Next()
        {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            return static_cast<std::uint32_t>(state >> 33);
        }

        std::uint32_t Below(std::uint32_t bound)
        {
            return bound == 0 ? 0 : Next() % bound;
        }
    };

    std::string MutateEffectBytes(DeterministicRng& random,
                                  std::vector<std::uint8_t>& bytes)
    {
        if (bytes.empty()) return "seed is empty";
        std::ostringstream description;
        const std::uint32_t mutationCount = 1 + random.Below(5);
        for (std::uint32_t mutation = 0; mutation < mutationCount; ++mutation)
        {
            if (mutation > 0) description << "; ";
            switch (random.Below(6))
            {
                case 0:
                {
                    const std::uint32_t offset =
                        random.Below(static_cast<std::uint32_t>(bytes.size()));
                    const std::uint32_t bit = random.Below(8);
                    bytes[offset] ^= static_cast<std::uint8_t>(1u << bit);
                    description << "flip byte " << offset << " bit " << bit;
                    break;
                }
                case 1:
                {
                    const std::uint32_t offset =
                        random.Below(static_cast<std::uint32_t>(bytes.size()));
                    const std::uint32_t value = random.Below(256);
                    bytes[offset] = static_cast<std::uint8_t>(value);
                    description << "replace byte " << offset << " with " << value;
                    break;
                }
                case 2:
                {
                    if (bytes.size() > 1)
                    {
                        const std::uint32_t length = 1 + random.Below(
                            static_cast<std::uint32_t>(bytes.size() - 1));
                        bytes.resize(length);
                        description << "truncate to " << length << " bytes";
                    }
                    else
                    {
                        description << "retain one-byte input";
                    }
                    break;
                }
                case 3:
                {
                    const std::uint32_t offset = random.Below(
                        static_cast<std::uint32_t>(bytes.size() + 1));
                    const std::uint32_t value = random.Below(256);
                    bytes.insert(bytes.begin() + offset, static_cast<std::uint8_t>(value));
                    description << "insert " << value << " at byte " << offset;
                    break;
                }
                case 4:
                {
                    const std::size_t offset = random.Below(
                        static_cast<std::uint32_t>(bytes.size()));
                    const std::size_t writable = std::min<std::size_t>(4, bytes.size() - offset);
                    std::fill_n(bytes.begin() + offset, writable, 0xFF);
                    description << "fill " << writable << " byte(s) with 255 at byte " << offset;
                    break;
                }
                case 5:
                {
                    // Whole aligned words are where the Effect Framework keeps its offsets and
                    // counts, so overwriting one reaches the bounds checks that byte flips
                    // almost never do. This is the mutation that found most of the parser
                    // crashes the managed MojoShader patch now fixes.
                    if (bytes.size() >= 4)
                    {
                        const std::uint32_t word = random.Below(
                            static_cast<std::uint32_t>(bytes.size() / 4));
                        const std::uint32_t value =
                            random.Below(4) == 0 ? 0xFFFFFFFFu : random.Next();
                        for (std::size_t byte = 0; byte < 4; ++byte)
                        {
                            bytes[word * 4 + byte] =
                                static_cast<std::uint8_t>(value >> (byte * 8));
                        }
                        description << "set word " << word << " to " << value;
                    }
                    else
                    {
                        description << "retain short input";
                    }
                    break;
                }
                default: break;
            }
        }
        return description.str();
    }

    /**
     * Walks every public surface a compiled effect exposes: reflection, annotations, array
     * elements, structure members, technique/pass selection, application and an independent
     * clone. Shared by the deterministic mutation corpus and mirrors what
     * tools/graphics/compiled_effect_fuzzer.cpp does for a coverage-guided campaign.
     */
    void ExerciseCompiledEffectSurface(Effect& effect)
    {
        using Microsoft::Xna::Framework::Graphics::EffectParameter;
        const auto walk = [](const EffectParameter& parameter, int depth, auto& self) -> void {
            if (depth > 8) return;
            (void) parameter.getNameProperty();
            (void) parameter.getSemanticProperty();
            (void) parameter.getRowCountProperty();
            (void) parameter.getColumnCountProperty();
            const auto& annotations = parameter.getAnnotationsProperty();
            for (int i = 0; i < annotations.getCountProperty(); ++i)
                (void) annotations[i].getNameProperty();
            const auto& elements = parameter.getElementsProperty();
            for (int i = 0; i < elements.getCountProperty(); ++i)
                self(elements[i], depth + 1, self);
            const auto& members = parameter.getStructureMembersProperty();
            for (int i = 0; i < members.getCountProperty(); ++i)
                self(members[i], depth + 1, self);
        };

        auto& parameters = effect.getParametersProperty();
        for (int i = 0; i < parameters.getCountProperty(); ++i) walk(parameters[i], 0, walk);

        auto& techniques = effect.getTechniquesProperty();
        for (int i = 0; i < techniques.getCountProperty(); ++i)
        {
            effect.setCurrentTechniqueProperty(&techniques[i]);
            auto& passes = techniques[i].getPassesProperty();
            for (int pass = 0; pass < passes.getCountProperty(); ++pass) passes[pass].Apply();
        }

        std::unique_ptr<Effect> clone(effect.Clone());
        if (clone && clone->getCurrentTechniqueProperty() != nullptr)
        {
            auto& passes = clone->getCurrentTechniqueProperty()->getPassesProperty();
            for (int pass = 0; pass < passes.getCountProperty(); ++pass) passes[pass].Apply();
        }
    }

    void RunEffectMutationCorpus(GraphicsDevice& device,
                                 const std::vector<std::uint8_t>& seed,
                                 std::uint64_t randomSeed, int iterations)
    {
        DeterministicRng random{randomSeed};
        int completed = 0;
        int cleanlyRejected = 0;
        for (int iteration = 0; iteration < iterations; ++iteration)
        {
            auto mutated = seed;
            const std::string mutation = MutateEffectBytes(random, mutated);
            std::ostringstream context;
            context << "seed=0x" << std::hex << randomSeed << std::dec
                    << " iteration=" << iteration << " size=" << mutated.size()
                    << " mutation=" << mutation;
            SCOPED_TRACE(context.str());
            try
            {
                Effect effect(device, mutated);
                // Constructing is only half the attack surface: reflection, clone, technique
                // selection and pass application all read the same parsed graph.
                ExerciseCompiledEffectSurface(effect);
                ++completed;
            }
            catch (const std::bad_alloc&)
            {
                ADD_FAILURE() << "allocation bomb escaped the compiled-effect limits";
            }
            catch (const std::exception&)
            {
                ++cleanlyRejected;
            }
            catch (...)
            {
                ADD_FAILURE() << "non-standard exception escaped malformed Effect parsing";
            }
        }
        EXPECT_EQ(completed + cleanlyRejected, iterations);
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

// plan_fx.md FX-060: the renderer-neutral half of the compiled-effect contract lives in
// tests/support/CNA/TestSupport/CompiledEffectConformance.hpp so that every backend claiming
// GraphicsCapability::CompiledEffects runs the identical assertions with nothing but its own
// device setup. FNA3D is the reference backend, so it runs the shared contract here and keeps its
// own backend-specific evidence -- fixture provenance, native parser diagnostics, golden pixels,
// SpriteBatch and Content Pipeline integration -- in the tests below.
namespace
{
    using CNA::Internal::JsonValue;

    /** @brief Reads FNA's checked-in reflection oracle (plan_fx.md FX-005). */
    JsonValue LoadFnaReflectionOracle()
    {
        const std::filesystem::path path = std::filesystem::path(__FILE__).parent_path() /
            "../../../../../../../../tests/fixtures/compiled-effects/fna-effect-reflection.json";
        std::ifstream input(path);
        if (!input) return {};
        std::ostringstream text;
        text << input.rdbuf();
        return CNA::Internal::ParseJson(text.str());
    }

    const JsonValue* FindOracleEffect(const JsonValue& document, const std::string& file)
    {
        const JsonValue* effects = document.FindMember("effects");
        if (effects == nullptr) return nullptr;
        for (const JsonValue& effect : effects->arrayValue)
        {
            const JsonValue* name = effect.FindMember("file");
            if (name != nullptr && name->stringValue == file) return &effect;
        }
        return nullptr;
    }

    std::string OracleString(const JsonValue& object, const char* key)
    {
        const JsonValue* member = object.FindMember(key);
        return member != nullptr && member->type == CNA::Internal::JsonType::String
            ? member->stringValue : std::string();
    }

    int OracleInt(const JsonValue& object, const char* key)
    {
        const JsonValue* member = object.FindMember(key);
        return member != nullptr ? static_cast<int>(member->numberValue) : 0;
    }

    /**
     * @brief Spells a CNA parameter class the way FNA's own enum ToString() spells it.
     *
     * The two enums carry the same XNA names, so this is a name mapping rather than a
     * translation: anything unmapped is reported as such so the comparison cannot pass by
     * accident.
     */
    std::string ToOracleClassName(
        Microsoft::Xna::Framework::Graphics::EffectParameterClass value)
    {
        using Class = Microsoft::Xna::Framework::Graphics::EffectParameterClass;
        switch (value)
        {
            case Class::Scalar: return "Scalar";
            case Class::Vector: return "Vector";
            case Class::Matrix: return "Matrix";
            case Class::Object: return "Object";
            case Class::Struct: return "Struct";
        }
        return "<unmapped class>";
    }

    /** @brief Spells a CNA parameter type the way FNA's own enum ToString() spells it. */
    std::string ToOracleTypeName(Microsoft::Xna::Framework::Graphics::EffectParameterType value)
    {
        using Type = Microsoft::Xna::Framework::Graphics::EffectParameterType;
        switch (value)
        {
            case Type::Void: return "Void";
            case Type::Bool: return "Bool";
            case Type::Int32: return "Int32";
            case Type::Single: return "Single";
            case Type::String: return "String";
            case Type::Texture: return "Texture";
            case Type::Texture1D: return "Texture1D";
            case Type::Texture2D: return "Texture2D";
            case Type::Texture3D: return "Texture3D";
            case Type::TextureCube: return "TextureCube";
        }
        return "<unmapped type>";
    }

    /** @brief Compares one CNA parameter subtree against FNA's own reflection of it. */
    void ExpectParameterMatchesOracle(
        const Microsoft::Xna::Framework::Graphics::EffectParameter& parameter,
        const JsonValue& oracle, const std::string& path, bool insideStructure = false)
    {
        using namespace Microsoft::Xna::Framework::Graphics;
        SCOPED_TRACE(path);
        EXPECT_EQ(parameter.getNameProperty(), OracleString(oracle, "name"));
        // FNA leaves a missing semantic as an empty string, exactly as CNA does.
        EXPECT_EQ(parameter.getSemanticProperty(), OracleString(oracle, "semantic"));
        EXPECT_EQ(ToOracleClassName(parameter.getParameterClassProperty()),
                  OracleString(oracle, "class"));
        EXPECT_EQ(ToOracleTypeName(parameter.getParameterTypeProperty()),
                  OracleString(oracle, "type"));
        EXPECT_EQ(parameter.getRowCountProperty(), OracleInt(oracle, "rowCount"));
        EXPECT_EQ(parameter.getColumnCountProperty(), OracleInt(oracle, "columnCount"));

        // The value comparison is what catches register padding: the Effect Framework stores a
        // float3 as a padded float4 register, so a getter that returned the padding, or dropped a
        // row, would look identical in the structural comparison above.
        // Struct members are deliberately excluded -- CNA and FNA genuinely disagree there, and
        // the disagreement is recorded in plan_fx.md rather than asserted either way here.
        const JsonValue* value = insideStructure ? nullptr : oracle.FindMember("value");
        if (value != nullptr && value->type == CNA::Internal::JsonType::Array &&
            parameter.getParameterTypeProperty() == EffectParameterType::Single)
        {
            const auto actual = parameter.GetValueSingleArray(
                static_cast<int>(value->arrayValue.size()));
            ASSERT_EQ(actual.size(), value->arrayValue.size());
            for (std::size_t i = 0; i < actual.size(); ++i)
            {
                EXPECT_FLOAT_EQ(actual[i],
                                static_cast<float>(value->arrayValue[i].numberValue))
                    << "float " << i << " of " << path;
            }
        }

        const JsonValue* annotations = oracle.FindMember("annotations");
        if (annotations != nullptr &&
            annotations->type == CNA::Internal::JsonType::Array)
        {
            ASSERT_EQ(parameter.getAnnotationsProperty().getCountProperty(),
                      static_cast<int>(annotations->arrayValue.size()));
            for (std::size_t i = 0; i < annotations->arrayValue.size(); ++i)
            {
                EXPECT_EQ(parameter.getAnnotationsProperty()[static_cast<int>(i)]
                              .getNameProperty(),
                          OracleString(annotations->arrayValue[i], "name"));
            }
        }

        const JsonValue* elements = oracle.FindMember("elements");
        if (elements != nullptr && elements->type == CNA::Internal::JsonType::Array)
        {
            ASSERT_EQ(parameter.getElementsProperty().getCountProperty(),
                      static_cast<int>(elements->arrayValue.size()));
            for (std::size_t i = 0; i < elements->arrayValue.size(); ++i)
            {
                ExpectParameterMatchesOracle(
                    parameter.getElementsProperty()[static_cast<int>(i)],
                    elements->arrayValue[i], path + "[" + std::to_string(i) + "]",
                    insideStructure);
            }
        }

        const JsonValue* members = oracle.FindMember("structureMembers");
        if (members != nullptr && members->type == CNA::Internal::JsonType::Array)
        {
            ASSERT_EQ(parameter.getStructureMembersProperty().getCountProperty(),
                      static_cast<int>(members->arrayValue.size()));
            for (std::size_t i = 0; i < members->arrayValue.size(); ++i)
            {
                ExpectParameterMatchesOracle(
                    parameter.getStructureMembersProperty()[static_cast<int>(i)],
                    members->arrayValue[i], path + "." + OracleString(
                        members->arrayValue[i], "name"), true);
            }
        }
    }
}

// plan_fx.md FX-005: the only check here that is not self-consistency. Every other reflection
// test compares CNA against the format or against CNA's own fixtures; this one compares it
// against reflection produced by *running* FNA over the same bytes, through the same pinned
// FNA3D/MojoShader. Regenerate the oracle with tools/fna-reference (see its README).
TEST(Fna3dCompiledEffectTest, StockFixtureReflectionMatchesTheFnaOracle)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    const JsonValue oracle = LoadFnaReflectionOracle();
    const JsonValue* effects = oracle.FindMember("effects");
    ASSERT_NE(effects, nullptr) << "the FNA reflection oracle is missing or unreadable";
    ASSERT_FALSE(effects->arrayValue.empty());

    for (const JsonValue& expected : effects->arrayValue)
    {
        const std::string file = OracleString(expected, "file");
        SCOPED_TRACE(file);
        const auto bytes = LoadStockEffect(file.c_str());
        ASSERT_FALSE(bytes.empty()) << "fixture " << file << " is missing";

        Effect effect(device, bytes);

        const JsonValue* parameters = expected.FindMember("parameters");
        ASSERT_NE(parameters, nullptr);
        ASSERT_EQ(effect.getParametersProperty().getCountProperty(),
                  static_cast<int>(parameters->arrayValue.size()))
            << "FNA and CNA disagree on how many parameters this effect exposes";
        for (std::size_t i = 0; i < parameters->arrayValue.size(); ++i)
        {
            ExpectParameterMatchesOracle(
                effect.getParametersProperty()[static_cast<int>(i)],
                parameters->arrayValue[i], OracleString(parameters->arrayValue[i], "name"));
        }

        const JsonValue* techniques = expected.FindMember("techniques");
        ASSERT_NE(techniques, nullptr);
        ASSERT_EQ(effect.getTechniquesProperty().getCountProperty(),
                  static_cast<int>(techniques->arrayValue.size()));
        for (std::size_t t = 0; t < techniques->arrayValue.size(); ++t)
        {
            const JsonValue& expectedTechnique = techniques->arrayValue[t];
            auto& technique = effect.getTechniquesProperty()[static_cast<int>(t)];
            EXPECT_EQ(technique.getNameProperty(), OracleString(expectedTechnique, "name"));

            const JsonValue* passes = expectedTechnique.FindMember("passes");
            ASSERT_NE(passes, nullptr);
            ASSERT_EQ(technique.getPassesProperty().getCountProperty(),
                      static_cast<int>(passes->arrayValue.size()));
            for (std::size_t p = 0; p < passes->arrayValue.size(); ++p)
            {
                EXPECT_EQ(technique.getPassesProperty()[static_cast<int>(p)].getNameProperty(),
                          OracleString(passes->arrayValue[p], "name"));
            }
        }
    }
}

// plan_fx.md FX-004: the same states, but declared in HLSL and compiled by the Effect compiler
// XNA itself used, rather than assembled byte by byte by CNA. This is the case a synthetic
// fixture cannot cover: it proves CNA reads what a real compiler writes, including the sampler
// states and pass render states it chooses to emit.
TEST(Fna3dCompiledEffectTest, CompilerProducedFixtureAppliesItsStatesAndSamplers)
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    const auto bytes = LoadStockEffect("CnaConformanceEffect.fxb");
    ASSERT_FALSE(bytes.empty()) << "the compiler-produced conformance fixture is missing";
    Effect effect(device, bytes);

    auto& parameters = effect.getParametersProperty();
    ASSERT_NE(parameters["FxTexture"], nullptr);
    Texture2D texture(device, 2, 2);
    const Color pixels[4] = {Color::Red, Color::Red, Color::Red, Color::Red};
    texture.SetData(pixels, 4);
    parameters["FxTexture"]->SetValue(&texture);

    auto* technique = effect.getTechniquesProperty()["FirstTechnique"];
    ASSERT_NE(technique, nullptr);
    ASSERT_EQ(technique->getPassesProperty().getCountProperty(), 2);
    effect.setCurrentTechniqueProperty(technique);

    // P0 binds the pixel shader that actually samples FxSampler, so this is the pass whose
    // sampler register the Effect Framework reports -- the states below travel with the shader
    // that uses the sampler, not with the effect as a whole.
    technique->getPassesProperty()[0].Apply();
    const SamplerState& sampler = device.getSamplerStatesProperty()[0];
    EXPECT_EQ(sampler.getFilterProperty(), TextureFilter::MinPointMagLinearMipPoint);
    EXPECT_EQ(sampler.getAddressUProperty(), TextureAddressMode::Mirror);
    EXPECT_EQ(sampler.getAddressVProperty(), TextureAddressMode::Clamp);
    EXPECT_EQ(sampler.getMaxAnisotropyProperty(), 8);
    EXPECT_EQ(device.getTexturesProperty()[0], &texture);

    technique->getPassesProperty()[1].Apply();

    // The render states the source declares on StatePass.
    const DepthStencilState& depth = device.getDepthStencilStateProperty();
    EXPECT_FALSE(depth.getDepthBufferEnableProperty());
    EXPECT_FALSE(depth.getDepthBufferWriteEnableProperty());
    EXPECT_EQ(device.getRasterizerStateProperty().getCullModeProperty(), CullMode::None);
    const BlendState& blend = device.getBlendStateProperty();
    EXPECT_EQ(blend.getColorSourceBlendProperty(), Blend::SourceAlpha);
    EXPECT_EQ(blend.getColorDestinationBlendProperty(), Blend::InverseSourceAlpha);
    EXPECT_EQ(blend.getColorBlendFunctionProperty(), BlendFunction::Add);
}

// plan_fx.md FX-051: inputs that once crashed the process, kept so they cannot again. Each was
// written by the coverage-guided fuzzer when it found a defect in the pinned MojoShader; the fixes
// live in a patch applied to a specific revision, so these are what notice if a future pin bump
// stops carrying them.
TEST(Fna3dCompiledEffectTest, CrashCorpusIsRejectedWithoutCrashing)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    const std::filesystem::path corpus = std::filesystem::path(__FILE__).parent_path() /
        "../../../../../../../../tests/fixtures/compiled-effects/crash-corpus";
    ASSERT_TRUE(std::filesystem::is_directory(corpus)) << "crash corpus is missing";

    int replayed = 0;
    for (const auto& entry : std::filesystem::directory_iterator(corpus))
    {
        if (entry.path().extension() != ".fxb") continue;
        SCOPED_TRACE(entry.path().filename().string());
        std::ifstream input(entry.path(), std::ios::binary);
        ASSERT_TRUE(input.is_open());
        const std::vector<SharpRuntime::bytecs> bytes(
            (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        ASSERT_FALSE(bytes.empty());

        try
        {
            Effect effect(device, bytes);
            // Constructing is allowed; what must not happen is a crash, here or in anything the
            // rest of the surface does with the parse tree.
            ExerciseCompiledEffectSurface(effect);
        }
        catch (const std::bad_alloc&)
        {
            ADD_FAILURE() << "allocation bomb escaped the compiled-effect limits";
        }
        catch (const std::exception&)
        {
            // Rejecting these is the contract.
        }
        ++replayed;
    }
    EXPECT_GT(replayed, 0) << "the crash corpus directory contains no .fxb inputs";
}

// plan_fx.md FX-084/FX-077: the shared draw matrix. Each of these renders the compiled effect's
// own Tint parameter into a render target and reads it back, so a draw that silently used a stock
// shader -- or bound an attribute from the wrong stream -- fails instead of passing quietly.

TEST(Fna3dCompiledEffectDrawTest, SharedDrawMatrixContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectDrawContract(device);
}

TEST(Fna3dCompiledEffectDrawTest, SharedMultiStreamDrawContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectMultiStreamDrawContract(device);
}

TEST(Fna3dCompiledEffectDrawTest, SharedInstancingDrawContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectInstancingDrawContract(device);
}

TEST(Fna3dCompiledEffectDrawTest, SharedSpriteBatchContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSpriteBatchContract(device);
}

TEST(Fna3dCompiledEffectDrawTest, SharedOrientationContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectOrientationContract(device);
}

TEST(Fna3dCompiledEffectDrawTest, SharedEffectSwitchingContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectSwitchingContract(device);
}

TEST(Fna3dCompiledEffectTest, SharedBackendConformanceContract)
{
    GraphicsDevice device;
    if (!CNA::TestSupport::SupportsCompiledEffects(device))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";
    CNA::TestSupport::RunCompiledEffectContract(device);
}

// The neutral format constants the shared fixtures are built from must keep matching the parser
// this backend actually uses; otherwise a fixture would silently stop meaning what it says.
static_assert(CNA::TestSupport::EffectFormat::RsZEnable == MOJOSHADER_RS_ZENABLE);
static_assert(CNA::TestSupport::EffectFormat::RsBlendFactor == MOJOSHADER_RS_BLENDFACTOR);
static_assert(CNA::TestSupport::EffectFormat::RsSeparateAlphaBlendEnable ==
              MOJOSHADER_RS_SEPARATEALPHABLENDENABLE);
static_assert(CNA::TestSupport::EffectFormat::RsPixelShader == MOJOSHADER_RS_PIXELSHADER);
static_assert(CNA::TestSupport::EffectFormat::SampTexture == MOJOSHADER_SAMP_TEXTURE);
static_assert(CNA::TestSupport::EffectFormat::SampMaxAnisotropy == MOJOSHADER_SAMP_MAXANISOTROPY);
static_assert(CNA::TestSupport::EffectFormat::AddressBorder == MOJOSHADER_TADDRESS_BORDER);
static_assert(CNA::TestSupport::EffectFormat::FilterAnisotropic ==
              MOJOSHADER_TEXTUREFILTER_ANISOTROPIC);
static_assert(CNA::TestSupport::EffectFormat::BlendInvBlendFactor == MOJOSHADER_BLEND_INVBLENDFACTOR);
static_assert(CNA::TestSupport::EffectFormat::CmpGreaterEqual == MOJOSHADER_CMP_GREATEREQUAL);
static_assert(CNA::TestSupport::EffectFormat::StencilDecr == MOJOSHADER_STENCILOP_DECR);
static_assert(CNA::TestSupport::EffectFormat::CullCcw == MOJOSHADER_CULL_CCW);
static_assert(CNA::TestSupport::EffectFormat::FillWireframe == MOJOSHADER_FILL_WIREFRAME);
static_assert(CNA::TestSupport::EffectFormat::TypeSampler2D == MOJOSHADER_SYMTYPE_SAMPLER2D);
static_assert(CNA::TestSupport::EffectFormat::ClassMatrixColumns ==
              MOJOSHADER_SYMCLASS_MATRIX_COLUMNS);

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

TEST(Fna3dCompiledEffectTest, SyntheticFixtureReflectsAnnotationsTechniquesAndExactPasses)
{
    using System::InvalidOperationException;

    GraphicsDevice device;
    Effect effect(device, BuildSyntheticConformanceEffect({}));

    ASSERT_EQ(effect.getParametersProperty().getCountProperty(), 5);
    auto* gain = effect.getParametersProperty()["Gain"];
    ASSERT_NE(gain, nullptr);
    EXPECT_EQ(gain->getSemanticProperty(), "SCALAR");
    EXPECT_FLOAT_EQ(gain->GetValueSingle(), 0.25f);
    const auto* visible = gain->getAnnotationsProperty()["Visible"];
    ASSERT_NE(visible, nullptr);
    EXPECT_TRUE(visible->GetValueBoolean());

    auto* tint = effect.getParametersProperty()["Tint"];
    ASSERT_NE(tint, nullptr);
    EXPECT_EQ(tint->getParameterClassProperty(),
              Microsoft::Xna::Framework::Graphics::EffectParameterClass::Vector);
    const auto tintValue = tint->GetValueVector4();
    EXPECT_FLOAT_EQ(tintValue.X, 0.1f);
    EXPECT_FLOAT_EQ(tintValue.Y, 0.2f);
    EXPECT_FLOAT_EQ(tintValue.Z, 0.3f);
    EXPECT_FLOAT_EQ(tintValue.W, 0.4f);

    auto* transform = effect.getParametersProperty()["Transform"];
    ASSERT_NE(transform, nullptr);
    EXPECT_EQ(transform->GetValueMatrix(), Matrix::getIdentityProperty());

    auto* weights = effect.getParametersProperty()["Weights"];
    ASSERT_NE(weights, nullptr);
    ASSERT_EQ(weights->getElementsProperty().getCountProperty(), 2);
    const auto weightValues = weights->GetValueSingleArray(2);
    ASSERT_EQ(weightValues.size(), 2u);
    EXPECT_FLOAT_EQ(weightValues[0], 0.2f);
    EXPECT_FLOAT_EQ(weightValues[1], 0.8f);
    weights->getElementsProperty()[1].SetValue(0.6f);
    EXPECT_FLOAT_EQ(weights->GetValueSingleArray(2)[1], 0.6f);

    auto* lighting = effect.getParametersProperty()["Lighting"];
    ASSERT_NE(lighting, nullptr);
    EXPECT_EQ(lighting->getParameterClassProperty(),
              Microsoft::Xna::Framework::Graphics::EffectParameterClass::Struct);
    EXPECT_EQ(lighting->getParameterTypeProperty(),
              Microsoft::Xna::Framework::Graphics::EffectParameterType::Void);
    EXPECT_EQ(lighting->getRowCountProperty(), 1);
    EXPECT_EQ(lighting->getColumnCountProperty(), 16);
    auto& lightingMembers = lighting->getStructureMembersProperty();
    ASSERT_EQ(lightingMembers.getCountProperty(), 3);
    EXPECT_FLOAT_EQ(lightingMembers["Intensity"]->GetValueSingle(), 0.5f);
    const auto direction = lightingMembers["Direction"]->GetValueVector3();
    EXPECT_FLOAT_EQ(direction.X, 0.6f);
    EXPECT_FLOAT_EQ(direction.Y, 0.7f);
    EXPECT_FLOAT_EQ(direction.Z, 0.8f);
    auto* thresholds = lightingMembers["Thresholds"];
    ASSERT_NE(thresholds, nullptr);
    ASSERT_EQ(thresholds->getElementsProperty().getCountProperty(), 2);
    const auto thresholdValues = thresholds->GetValueSingleArray(2);
    ASSERT_EQ(thresholdValues.size(), 2u);
    EXPECT_FLOAT_EQ(thresholdValues[0], 0.9f);
    EXPECT_FLOAT_EQ(thresholdValues[1], 1.0f);
    thresholds->getElementsProperty()[1].SetValue(1.25f);
    EXPECT_FLOAT_EQ(thresholds->GetValueSingleArray(2)[1], 1.25f);

    auto& techniques = effect.getTechniquesProperty();
    ASSERT_EQ(techniques.getCountProperty(), 2);
    EXPECT_EQ(techniques[0].getNameProperty(), "FirstTechnique");
    EXPECT_EQ(techniques[1].getNameProperty(), "SecondTechnique");
    const auto* quality = techniques[0].getAnnotationsProperty()["Quality"];
    ASSERT_NE(quality, nullptr);
    EXPECT_EQ(quality->GetValueInt32(), 7);
    ASSERT_EQ(techniques[0].getPassesProperty().getCountProperty(), 2);
    ASSERT_EQ(techniques[1].getPassesProperty().getCountProperty(), 1);
    EXPECT_EQ(techniques[0].getPassesProperty()[0].getNameProperty(), "P0");
    EXPECT_EQ(techniques[0].getPassesProperty()[1].getNameProperty(), "StatePass");
    const auto* tag = techniques[0].getPassesProperty()[1].getAnnotationsProperty()["PassTag"];
    ASSERT_NE(tag, nullptr);
    EXPECT_EQ(tag->GetValueInt32(), 3);

    EXPECT_NO_THROW(techniques[0].getPassesProperty()[0].Apply());
    EXPECT_NO_THROW(techniques[0].getPassesProperty()[1].Apply());
    EXPECT_THROW(techniques[1].getPassesProperty()[0].Apply(), InvalidOperationException);
    effect.setCurrentTechniqueProperty(&techniques[1]);
    EXPECT_NO_THROW(techniques[1].getPassesProperty()[0].Apply());

    std::unique_ptr<Effect> clone(effect.Clone());
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->getCurrentTechniqueProperty()->getNameProperty(), "SecondTechnique");
    clone->getParametersProperty()["Gain"]->SetValue(0.75f);
    EXPECT_FLOAT_EQ(clone->getParametersProperty()["Gain"]->GetValueSingle(), 0.75f);
    EXPECT_FLOAT_EQ(gain->GetValueSingle(), 0.25f);
}

TEST(Fna3dCompiledEffectTest, SyntheticFixtureAcceptsEveryFnaRenderStateToken)
{
    const std::vector<SyntheticRenderState> states = {
        {MOJOSHADER_RS_ZENABLE, MOJOSHADER_ZB_TRUE},
        {MOJOSHADER_RS_FILLMODE, MOJOSHADER_FILL_SOLID},
        {MOJOSHADER_RS_ZWRITEENABLE, 1},
        {MOJOSHADER_RS_SRCBLEND, MOJOSHADER_BLEND_ONE},
        {MOJOSHADER_RS_DESTBLEND, MOJOSHADER_BLEND_ZERO},
        {MOJOSHADER_RS_CULLMODE, MOJOSHADER_CULL_NONE},
        {MOJOSHADER_RS_ZFUNC, MOJOSHADER_CMP_ALWAYS},
        {MOJOSHADER_RS_ALPHABLENDENABLE, 1},
        {MOJOSHADER_RS_STENCILENABLE, 0},
        {MOJOSHADER_RS_STENCILFAIL, MOJOSHADER_STENCILOP_KEEP},
        {MOJOSHADER_RS_STENCILZFAIL, MOJOSHADER_STENCILOP_KEEP},
        {MOJOSHADER_RS_STENCILPASS, MOJOSHADER_STENCILOP_KEEP},
        {MOJOSHADER_RS_STENCILFUNC, MOJOSHADER_CMP_ALWAYS},
        {MOJOSHADER_RS_STENCILREF, 0},
        {MOJOSHADER_RS_STENCILMASK, 0xFFFFFFFFu},
        {MOJOSHADER_RS_STENCILWRITEMASK, 0xFFFFFFFFu},
        {MOJOSHADER_RS_MULTISAMPLEANTIALIAS, 1},
        {MOJOSHADER_RS_MULTISAMPLEMASK, 0xFFFFFFFFu},
        {MOJOSHADER_RS_COLORWRITEENABLE, 15},
        {MOJOSHADER_RS_BLENDOP, MOJOSHADER_BLENDOP_ADD},
        {MOJOSHADER_RS_SCISSORTESTENABLE, 0},
        {MOJOSHADER_RS_SLOPESCALEDEPTHBIAS, FloatBits(0.0f), true},
        {MOJOSHADER_RS_TWOSIDEDSTENCILMODE, 0},
        {MOJOSHADER_RS_CCW_STENCILFAIL, MOJOSHADER_STENCILOP_KEEP},
        {MOJOSHADER_RS_CCW_STENCILZFAIL, MOJOSHADER_STENCILOP_KEEP},
        {MOJOSHADER_RS_CCW_STENCILPASS, MOJOSHADER_STENCILOP_KEEP},
        {MOJOSHADER_RS_CCW_STENCILFUNC, MOJOSHADER_CMP_ALWAYS},
        {MOJOSHADER_RS_COLORWRITEENABLE1, 15},
        {MOJOSHADER_RS_COLORWRITEENABLE2, 15},
        {MOJOSHADER_RS_COLORWRITEENABLE3, 15},
        {MOJOSHADER_RS_BLENDFACTOR, 0x10203040u},
        {MOJOSHADER_RS_DEPTHBIAS, FloatBits(0.0f), true},
        {MOJOSHADER_RS_SEPARATEALPHABLENDENABLE, 1},
        {MOJOSHADER_RS_SRCBLENDALPHA, MOJOSHADER_BLEND_ONE},
        {MOJOSHADER_RS_DESTBLENDALPHA, MOJOSHADER_BLEND_ZERO},
        {MOJOSHADER_RS_BLENDOPALPHA, MOJOSHADER_BLENDOP_ADD},
        {178u, 0u}, // Effect compiler's undocumented SetSampler metadata token.
    };

    GraphicsDevice device;
    Effect effect(device, BuildSyntheticConformanceEffect(states));
    ASSERT_EQ(effect.getTechniquesProperty()[0].getPassesProperty().getCountProperty(), 2);
    EXPECT_NO_THROW(effect.getTechniquesProperty()[0].getPassesProperty()[1].Apply());
}

namespace
{
    /** @brief Applies the sampler-bearing fixture's state pass and returns the device's slot. */
    Microsoft::Xna::Framework::Graphics::SamplerState ApplySyntheticSampler(
        GraphicsDevice& device, const std::vector<SyntheticSamplerState>& states,
        std::uint32_t samplerRegister = 0)
    {
        SyntheticEffectOptions options;
        options.includeSampler = true;
        options.samplerStates = states;
        options.samplerRegister = samplerRegister;
        Effect effect(device, BuildSyntheticEffect(options));
        effect.getTechniquesProperty()[0].getPassesProperty()[1].Apply();
        return device.getSamplerStatesProperty()[static_cast<int>(samplerRegister)];
    }
}

TEST(Fna3dCompiledEffectTest, PartialFilterAssignmentStartsFromTheSelectedSamplerState)
{
    using namespace Microsoft::Xna::Framework::Graphics;

    GraphicsDevice device;
    device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;

    // Only the magnification axis is assigned, so the other two keep the game's own selection.
    const SamplerState applied = ApplySyntheticSampler(device, {
        {MOJOSHADER_SAMP_MAGFILTER, MOJOSHADER_TEXTUREFILTER_LINEAR},
    });
    EXPECT_EQ(applied.getFilterProperty(), TextureFilter::MinPointMagLinearMipPoint);
    EXPECT_EQ(applied.getAddressUProperty(), TextureAddressMode::Clamp);
}

TEST(Fna3dCompiledEffectTest, ClonedSamplerEffectKeepsItsOwnTextureBinding)
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    GraphicsDevice device;
    SyntheticEffectOptions options;
    options.includeSampler = true;
    options.samplerStates = {{MOJOSHADER_SAMP_ADDRESSU, MOJOSHADER_TADDRESS_CLAMP}};
    Effect effect(device, BuildSyntheticEffect(options));

    Texture2D first(device, 2, 2);
    Texture2D second(device, 2, 2);
    const Color pixels[4] = {Color::White, Color::White, Color::White, Color::White};
    first.SetData(pixels, 4);
    second.SetData(pixels, 4);
    effect.getParametersProperty()["FxTexture"]->SetValue(&first);

    std::unique_ptr<Effect> clone(effect.Clone());
    clone->getParametersProperty()["FxTexture"]->SetValue(&second);

    effect.getTechniquesProperty()[0].getPassesProperty()[1].Apply();
    EXPECT_EQ(device.getTexturesProperty()[0], &first);
    clone->getTechniquesProperty()[0].getPassesProperty()[1].Apply();
    EXPECT_EQ(device.getTexturesProperty()[0], &second);
}

TEST(Fna3dCompiledEffectTest, SyntheticBlendFactorStateMatchesFnaByteOrderingInPixels)
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    GraphicsDevice device;
    Effect shader(device, LoadStockEffect("BasicEffect.fxb"));
    shader.getParametersProperty()["WorldViewProj"]->SetValue(Matrix::getIdentityProperty());
    shader.getParametersProperty()["DiffuseColor"]->SetValue(
        Vector4(1.0f, 1.0f, 1.0f, 1.0f));
    shader.getParametersProperty()["ShaderIndex"]->SetValue(3);
    shader.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply();

    const std::vector<SyntheticRenderState> states = {
        {MOJOSHADER_RS_ZENABLE, MOJOSHADER_ZB_FALSE},
        {MOJOSHADER_RS_CULLMODE, MOJOSHADER_CULL_NONE},
        {MOJOSHADER_RS_ALPHABLENDENABLE, 1},
        {MOJOSHADER_RS_BLENDFACTOR, 0x10203040u},
        {MOJOSHADER_RS_SRCBLEND, MOJOSHADER_BLEND_BLENDFACTOR},
        {MOJOSHADER_RS_DESTBLEND, MOJOSHADER_BLEND_ZERO},
        {MOJOSHADER_RS_BLENDOP, MOJOSHADER_BLENDOP_ADD},
    };
    Effect stateEffect(device, BuildSyntheticConformanceEffect(states));
    stateEffect.getTechniquesProperty()[0].getPassesProperty()[1].Apply();

    device.Clear(Color::Black);
    const VertexPositionColor vertices[6] = {
        { Vector3(-1.0f, 1.0f, 0.5f), Color::White },
        { Vector3(-1.0f, -1.0f, 0.5f), Color::White },
        { Vector3(1.0f, -1.0f, 0.5f), Color::White },
        { Vector3(-1.0f, 1.0f, 0.5f), Color::White },
        { Vector3(1.0f, -1.0f, 0.5f), Color::White },
        { Vector3(1.0f, 1.0f, 0.5f), Color::White },
    };
    device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);

    const auto& viewport = device.getViewportProperty();
    const Rectangle center(viewport.getWidthProperty() / 2,
                           viewport.getHeightProperty() / 2, 1, 1);
    Color pixel(0, 0, 0, 0);
    device.GetBackBufferData(&center, &pixel, 0, 1);
    EXPECT_NEAR(pixel.getRProperty(), 0x10, 2);
    EXPECT_NEAR(pixel.getGProperty(), 0x20, 2);
    EXPECT_NEAR(pixel.getBProperty(), 0x30, 2);
}

TEST(Fna3dCompiledEffectTest, SyntheticFixtureRejectsUnknownRenderStateTransactionally)
{
    GraphicsDevice device;
    Effect effect(device, BuildSyntheticConformanceEffect({{177u, 0u}}));
    try
    {
        effect.getTechniquesProperty()[0].getPassesProperty()[1].Apply();
        FAIL() << "an unknown Effect Framework render state must never be ignored";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_NE(std::string(error.what()).find("unsupported render state 177"),
                  std::string::npos);
    }
}

TEST(Fna3dCompiledEffectTest, DeterministicMutationCorpusCompletesOrFailsCleanly)
{
    GraphicsDevice device;
    // Iteration counts are chosen to keep this well under a second while still reaching the
    // parser paths the FX-051 campaign found crashes in; the campaign itself runs far longer
    // out of tree (docs/fx-bytecode-fuzzing.md).
    RunEffectMutationCorpus(device, BuildSyntheticConformanceEffect({}),
                            0x465853594E5448ULL, 1024);
    RunEffectMutationCorpus(device, LoadStockEffect("BasicEffect.fxb"),
                            0x46584241534943ULL, 192);
    // A shader-bearing fixture reaches the object table, the constant table and the preshader
    // selection path, none of which the state-only fixture can exercise.
    SyntheticEffectOptions withSampler;
    withSampler.includeSampler = true;
    RunEffectMutationCorpus(device, BuildSyntheticEffect(withSampler),
                            0x4658534D504C52ULL, 192);
}

TEST(Fna3dCompiledEffectTest, MissingShaderParameterSymbolIsRejectedNormally)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    auto bytes = LoadStockEffect("BasicEffect.fxb");
    constexpr std::size_t mutationOffset = 25018;
    ASSERT_GT(bytes.size(), mutationOffset);

    // This is BasicEffect corpus seed 0x46584241534943, iteration 121: changing this
    // shader-symbol byte makes it no longer match any reflected Effect parameter.
    bytes[mutationOffset] ^= static_cast<std::uint8_t>(1u << 4);
    try
    {
        Effect effect(device, bytes);
        FAIL() << "a shader symbol without a matching Effect parameter must be rejected";
    }
    catch (const std::bad_alloc&)
    {
        FAIL() << "the malformed Effect escaped the parser's allocation limits";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_NE(std::string(error.what()).find("Shader parameter not found in effect."),
                  std::string::npos);
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

TEST(Fna3dCompiledEffectTest, CompiledEffectSurvivesDeviceReset)
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    GraphicsDevice device;
    Effect effect(device, LoadStockEffect("BasicEffect.fxb"));
    effect.getParametersProperty()["WorldViewProj"]->SetValue(Matrix::getIdentityProperty());
    effect.getParametersProperty()["DiffuseColor"]->SetValue(Vector4(0.0f, 1.0f, 0.0f, 1.0f));
    effect.getParametersProperty()["ShaderIndex"]->SetValue(3);
    effect.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply();

    PresentationParameters parameters = device.getPresentationParametersProperty().Clone();
    parameters.setBackBufferWidthProperty(
        parameters.getBackBufferWidthProperty() / 2 > 0
            ? parameters.getBackBufferWidthProperty() / 2 : 64);
    parameters.setBackBufferHeightProperty(
        parameters.getBackBufferHeightProperty() / 2 > 0
            ? parameters.getBackBufferHeightProperty() / 2 : 64);
    ASSERT_NO_THROW(device.Reset(parameters));

    // The native effect belongs to the FNA3D device, which Reset() reconfigures rather than
    // recreates, so the same instance must still reflect, apply and draw afterwards.
    ASSERT_EQ(effect.getParametersProperty()["ShaderIndex"]->GetValueInt32(), 3);

    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.Clear(Color::Black);
    effect.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply();

    const VertexPositionColor vertices[6] = {
        { Vector3(-1.0f, 1.0f, 0.5f), Color::White },
        { Vector3(-1.0f, -1.0f, 0.5f), Color::White },
        { Vector3(1.0f, -1.0f, 0.5f), Color::White },
        { Vector3(-1.0f, 1.0f, 0.5f), Color::White },
        { Vector3(1.0f, -1.0f, 0.5f), Color::White },
        { Vector3(1.0f, 1.0f, 0.5f), Color::White },
    };
    device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);

    const auto& viewport = device.getViewportProperty();
    const Rectangle center(viewport.getWidthProperty() / 2,
                           viewport.getHeightProperty() / 2, 1, 1);
    Color pixel(0, 0, 0, 0);
    device.GetBackBufferData(&center, &pixel, 0, 1);
    EXPECT_NEAR(pixel.getGProperty(), 255, 2);
}

TEST(Fna3dCompiledEffectTest, CloneChainsStayIndependentInAnyDisposalOrder)
{
    GraphicsDevice device;
    auto root = std::make_unique<Effect>(device, LoadStockEffect("BasicEffect.fxb"));
    root->getParametersProperty()["ShaderIndex"]->SetValue(1);

    std::vector<std::unique_ptr<Effect>> chain;
    Effect* previous = root.get();
    for (int generation = 0; generation < 4; ++generation)
    {
        std::unique_ptr<Effect> next(previous->Clone());
        next->getParametersProperty()["ShaderIndex"]->SetValue(generation + 2);
        previous = next.get();
        chain.push_back(std::move(next));
    }

    // Each generation copies the values it was cloned from and then diverges independently.
    EXPECT_EQ(root->getParametersProperty()["ShaderIndex"]->GetValueInt32(), 1);
    for (std::size_t generation = 0; generation < chain.size(); ++generation)
    {
        EXPECT_EQ(chain[generation]->getParametersProperty()["ShaderIndex"]->GetValueInt32(),
                  static_cast<int>(generation) + 2);
        EXPECT_NO_THROW(chain[generation]->getCurrentTechniqueProperty()
                            ->getPassesProperty()[0].Apply());
    }

    // Disposing the middle of the chain must not disturb either end.
    chain[1].reset();
    EXPECT_NO_THROW(root->getCurrentTechniqueProperty()->getPassesProperty()[0].Apply());
    EXPECT_NO_THROW(chain[3]->getCurrentTechniqueProperty()->getPassesProperty()[0].Apply());
    root.reset();
    EXPECT_NO_THROW(chain[3]->getCurrentTechniqueProperty()->getPassesProperty()[0].Apply());
    EXPECT_EQ(chain[3]->getParametersProperty()["ShaderIndex"]->GetValueInt32(), 5);
}

TEST(Fna3dCompiledEffectTest, DisposedCompiledEffectRejectsEveryLaterApply)
{
    GraphicsDevice device;
    Effect effect(device, LoadStockEffect("BasicEffect.fxb"));
    effect.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply();
    effect.Dispose();

    EXPECT_THROW(effect.Apply(), System::ObjectDisposedException);
    EXPECT_THROW(effect.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply(),
                 System::ObjectDisposedException);
    // Disposal is idempotent, and the device stays usable for a fresh effect.
    EXPECT_NO_THROW(effect.Dispose());
    Effect replacement(device, LoadStockEffect("BasicEffect.fxb"));
    EXPECT_NO_THROW(replacement.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply());
}

TEST(Fna3dCompiledEffectTest, RepeatedNativeConstructionFailureLeavesTheDeviceUsable)
{
    GraphicsDevice device;
    SyntheticEffectOptions broken;
    broken.includeSampler = true;
    broken.breakShaderSymbolBinding = true;
    const auto brokenBytes = BuildSyntheticEffect(broken);

    // MojoShader fails after it has already allocated the parse tree and compiled part of the
    // object table, which is the point where a leak or a double free would show up.
    for (int attempt = 0; attempt < 32; ++attempt)
    {
        SCOPED_TRACE("attempt " + std::to_string(attempt));
        try
        {
            Effect effect(device, brokenBytes);
            FAIL() << "a shader symbol with no matching effect parameter must be rejected";
        }
        catch (const std::exception& error)
        {
            EXPECT_NE(std::string(error.what()).find("parameter"), std::string::npos);
        }
    }

    SyntheticEffectOptions valid;
    valid.includeSampler = true;
    Effect effect(device, BuildSyntheticEffect(valid));
    EXPECT_NO_THROW(effect.getTechniquesProperty()[0].getPassesProperty()[1].Apply());
}

TEST(Fna3dCompiledEffectTest, RepeatedCreateApplyDisposeCyclesStayStable)
{
    GraphicsDevice device;
    const auto bytes = LoadStockEffect("SpriteEffect.fxb");
    for (int cycle = 0; cycle < 24; ++cycle)
    {
        SCOPED_TRACE("cycle " + std::to_string(cycle));
        Effect effect(device, bytes);
        effect.getParametersProperty()["MatrixTransform"]->SetValue(
            Matrix::getIdentityProperty());
        effect.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply();
        std::unique_ptr<Effect> clone(effect.Clone());
        clone->getCurrentTechniqueProperty()->getPassesProperty()[0].Apply();
        // The clone is destroyed first on even cycles and last on odd ones.
        if ((cycle & 1) == 0) clone.reset();
        effect.Dispose();
    }
    Effect survivor(device, bytes);
    EXPECT_NO_THROW(survivor.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply());
}

TEST(Fna3dCompiledEffectTest, SelectedCompiledEffectUnlocksInstancedDrawing)
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    GraphicsDevice device;
    const VertexPositionColor vertices[3] = {
        { Vector3(-1.0f, 1.0f, 0.5f), Color::White },
        { Vector3(-1.0f, -1.0f, 0.5f), Color::White },
        { Vector3(1.0f, -1.0f, 0.5f), Color::White },
    };
    const std::uint16_t indices[3] = {0, 1, 2};

    VertexBuffer vertexBuffer(device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                              BufferUsage::None);
    vertexBuffer.SetData(vertices, 0, 3);
    IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    indexBuffer.SetData(indices, 0, 3);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.Clear(Color::Black);

    // FNA3D refuses an instanced draw while a stock effect is selected, because no stock effect
    // declares a per-instance vertex input. A compiled Effect is precisely the case real XNA
    // requires for hardware instancing, so selecting one has to reach the native instanced draw.
    BasicEffect stock(device);
    stock.setWorldProperty(Matrix::getIdentityProperty());
    stock.setViewProperty(Matrix::getIdentityProperty());
    stock.setProjectionProperty(Matrix::getIdentityProperty());
    stock.Apply();
    EXPECT_THROW(device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1, 2),
                 std::runtime_error);

    Effect compiled(device, LoadStockEffect("BasicEffect.fxb"));
    compiled.getParametersProperty()["WorldViewProj"]->SetValue(Matrix::getIdentityProperty());
    compiled.getParametersProperty()["DiffuseColor"]->SetValue(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
    compiled.getParametersProperty()["ShaderIndex"]->SetValue(3);
    compiled.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply();
    EXPECT_NO_THROW(
        device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1, 2));
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
