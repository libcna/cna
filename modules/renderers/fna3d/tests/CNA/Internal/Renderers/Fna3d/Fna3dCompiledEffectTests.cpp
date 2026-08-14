// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
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

using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Matrix;

namespace
{
    struct SyntheticRenderState
    {
        std::uint32_t type;
        std::uint32_t valueBits;
        bool isFloat = false;
    };

    void AppendUInt32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
    {
        bytes.push_back(static_cast<std::uint8_t>(value));
        bytes.push_back(static_cast<std::uint8_t>(value >> 8));
        bytes.push_back(static_cast<std::uint8_t>(value >> 16));
        bytes.push_back(static_cast<std::uint8_t>(value >> 24));
    }

    void PatchUInt32(std::vector<std::uint8_t>& bytes, std::size_t offset,
                     std::uint32_t value)
    {
        ASSERT_LE(offset + 4, bytes.size());
        bytes[offset] = static_cast<std::uint8_t>(value);
        bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
        bytes[offset + 2] = static_cast<std::uint8_t>(value >> 16);
        bytes[offset + 3] = static_cast<std::uint8_t>(value >> 24);
    }

    std::uint32_t AppendEffectString(std::vector<std::uint8_t>& bytes,
                                     const std::string& value)
    {
        const auto offset = static_cast<std::uint32_t>(bytes.size() - 8);
        AppendUInt32(bytes, static_cast<std::uint32_t>(value.size() + 1));
        bytes.insert(bytes.end(), value.begin(), value.end());
        bytes.push_back(0);
        while ((bytes.size() & 3u) != 0) bytes.push_back(0);
        return offset;
    }

    std::uint32_t AppendNumericType(std::vector<std::uint8_t>& bytes,
                                    MOJOSHADER_symbolType type,
                                    MOJOSHADER_symbolClass parameterClass,
                                    std::uint32_t nameOffset,
                                    std::uint32_t semanticOffset,
                                    std::uint32_t elementCount,
                                    std::uint32_t columns,
                                    std::uint32_t rows)
    {
        const auto offset = static_cast<std::uint32_t>(bytes.size() - 8);
        AppendUInt32(bytes, static_cast<std::uint32_t>(type));
        AppendUInt32(bytes, static_cast<std::uint32_t>(parameterClass));
        AppendUInt32(bytes, nameOffset);
        AppendUInt32(bytes, semanticOffset);
        AppendUInt32(bytes, elementCount);
        AppendUInt32(bytes, columns);
        AppendUInt32(bytes, rows);
        return offset;
    }

    std::uint32_t AppendScalarType(std::vector<std::uint8_t>& bytes,
                                   MOJOSHADER_symbolType type,
                                   std::uint32_t nameOffset,
                                   std::uint32_t semanticOffset,
                                   std::uint32_t elementCount = 0)
    {
        return AppendNumericType(bytes, type, MOJOSHADER_SYMCLASS_SCALAR,
                                 nameOffset, semanticOffset, elementCount, 1, 1);
    }

    std::uint32_t FloatBits(float value);

    std::uint32_t AppendLightingStructType(std::vector<std::uint8_t>& bytes,
                                           std::uint32_t nameOffset,
                                           std::uint32_t intensityName,
                                           std::uint32_t directionName,
                                           std::uint32_t thresholdsName,
                                           std::uint32_t empty)
    {
        const auto offset = static_cast<std::uint32_t>(bytes.size() - 8);
        AppendUInt32(bytes, static_cast<std::uint32_t>(MOJOSHADER_SYMTYPE_VOID));
        AppendUInt32(bytes, static_cast<std::uint32_t>(MOJOSHADER_SYMCLASS_STRUCT));
        AppendUInt32(bytes, nameOffset);
        AppendUInt32(bytes, empty); // semantic
        AppendUInt32(bytes, 0); // elements
        AppendUInt32(bytes, 3); // members

        auto appendMember = [&](MOJOSHADER_symbolClass parameterClass,
                                std::uint32_t memberName, std::uint32_t elements,
                                std::uint32_t columns, std::uint32_t rows)
        {
            AppendUInt32(bytes, static_cast<std::uint32_t>(MOJOSHADER_SYMTYPE_FLOAT));
            AppendUInt32(bytes, static_cast<std::uint32_t>(parameterClass));
            AppendUInt32(bytes, memberName);
            AppendUInt32(bytes, empty); // semantic
            AppendUInt32(bytes, elements);
            AppendUInt32(bytes, columns);
            AppendUInt32(bytes, rows);
        };
        // The Effect Framework struct encoding stores the concrete element multiplicity for each
        // member, including one for non-array members (unlike top-level numeric declarations,
        // where zero means non-array).
        appendMember(MOJOSHADER_SYMCLASS_SCALAR, intensityName, 1, 1, 1);
        appendMember(MOJOSHADER_SYMCLASS_VECTOR, directionName, 1, 3, 1);
        appendMember(MOJOSHADER_SYMCLASS_SCALAR, thresholdsName, 2, 1, 1);

        // Struct defaults live immediately after the member metadata. MojoShader expands every
        // member row to a float4 register while parsing this tight compiler representation.
        for (const float value : {0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f})
            AppendUInt32(bytes, FloatBits(value));
        return offset;
    }

    std::uint32_t AppendValueBits(std::vector<std::uint8_t>& bytes, std::uint32_t value)
    {
        const auto offset = static_cast<std::uint32_t>(bytes.size() - 8);
        AppendUInt32(bytes, value);
        return offset;
    }

    std::uint32_t FloatBits(float value)
    {
        std::uint32_t result = 0;
        static_assert(sizeof(result) == sizeof(value));
        std::memcpy(&result, &value, sizeof(result));
        return result;
    }

    std::uint32_t AppendFloatValues(std::vector<std::uint8_t>& bytes,
                                    std::initializer_list<float> values)
    {
        const auto offset = static_cast<std::uint32_t>(bytes.size() - 8);
        for (const float value : values) AppendUInt32(bytes, FloatBits(value));
        return offset;
    }

    /**
     * Builds a small Effect Framework 9.1 container directly from the documented layout consumed
     * by CNA's pinned MojoShader. It deliberately contains no shader objects: MojoShader permits
     * state-only passes, which makes this a deterministic conformance fixture for public
     * reflection, exact pass identity, and every render-state translation without redistributing
     * a proprietary compiler or compiler-produced program.
     */
    std::vector<std::uint8_t> BuildSyntheticConformanceEffect(
        const std::vector<SyntheticRenderState>& renderStates)
    {
        std::vector<std::uint8_t> bytes;
        AppendUInt32(bytes, 0xFEFF0901u);
        AppendUInt32(bytes, 0); // structure offset, patched below

        const std::uint32_t empty = static_cast<std::uint32_t>(bytes.size() - 8);
        AppendUInt32(bytes, 0);
        const std::uint32_t gainName = AppendEffectString(bytes, "Gain");
        const std::uint32_t gainSemantic = AppendEffectString(bytes, "SCALAR");
        const std::uint32_t tintName = AppendEffectString(bytes, "Tint");
        const std::uint32_t transformName = AppendEffectString(bytes, "Transform");
        const std::uint32_t weightsName = AppendEffectString(bytes, "Weights");
        const std::uint32_t lightingName = AppendEffectString(bytes, "Lighting");
        const std::uint32_t intensityName = AppendEffectString(bytes, "Intensity");
        const std::uint32_t directionName = AppendEffectString(bytes, "Direction");
        const std::uint32_t thresholdsName = AppendEffectString(bytes, "Thresholds");
        const std::uint32_t visibleName = AppendEffectString(bytes, "Visible");
        const std::uint32_t qualityName = AppendEffectString(bytes, "Quality");
        const std::uint32_t passTagName = AppendEffectString(bytes, "PassTag");
        const std::uint32_t firstTechnique = AppendEffectString(bytes, "FirstTechnique");
        const std::uint32_t secondTechnique = AppendEffectString(bytes, "SecondTechnique");
        const std::uint32_t firstPass = AppendEffectString(bytes, "P0");
        const std::uint32_t statePass = AppendEffectString(bytes, "StatePass");
        const std::uint32_t secondPass = AppendEffectString(bytes, "P1");

        const std::uint32_t unnamedIntType =
            AppendScalarType(bytes, MOJOSHADER_SYMTYPE_INT, empty, empty);
        const std::uint32_t unnamedFloatType =
            AppendScalarType(bytes, MOJOSHADER_SYMTYPE_FLOAT, empty, empty);
        const std::uint32_t gainType =
            AppendScalarType(bytes, MOJOSHADER_SYMTYPE_FLOAT, gainName, gainSemantic);
        const std::uint32_t tintType = AppendNumericType(
            bytes, MOJOSHADER_SYMTYPE_FLOAT, MOJOSHADER_SYMCLASS_VECTOR,
            tintName, empty, 0, 4, 1);
        const std::uint32_t transformType = AppendNumericType(
            bytes, MOJOSHADER_SYMTYPE_FLOAT, MOJOSHADER_SYMCLASS_MATRIX_COLUMNS,
            transformName, empty, 0, 4, 4);
        const std::uint32_t weightsType =
            AppendScalarType(bytes, MOJOSHADER_SYMTYPE_FLOAT, weightsName, empty, 2);
        const std::uint32_t lightingType = AppendLightingStructType(
            bytes, lightingName, intensityName, directionName, thresholdsName, empty);
        const std::uint32_t visibleType =
            AppendScalarType(bytes, MOJOSHADER_SYMTYPE_BOOL, visibleName, empty);
        const std::uint32_t qualityType =
            AppendScalarType(bytes, MOJOSHADER_SYMTYPE_INT, qualityName, empty);
        const std::uint32_t passTagType =
            AppendScalarType(bytes, MOJOSHADER_SYMTYPE_INT, passTagName, empty);

        const std::uint32_t gainValue = AppendValueBits(bytes, FloatBits(0.25f));
        const std::uint32_t tintValue =
            AppendFloatValues(bytes, {0.1f, 0.2f, 0.3f, 0.4f});
        const std::uint32_t transformValue = AppendFloatValues(bytes, {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        });
        const std::uint32_t weightsValue = AppendFloatValues(bytes, {0.2f, 0.8f});
        const std::uint32_t visibleValue = AppendValueBits(bytes, 1);
        const std::uint32_t qualityValue = AppendValueBits(bytes, 7);
        const std::uint32_t passTagValue = AppendValueBits(bytes, 3);
        std::vector<std::uint32_t> stateValueOffsets;
        stateValueOffsets.reserve(renderStates.size());
        for (const auto& state : renderStates)
            stateValueOffsets.push_back(AppendValueBits(bytes, state.valueBits));

        const auto structureOffset = static_cast<std::uint32_t>(bytes.size() - 8);
        PatchUInt32(bytes, 4, structureOffset);
        AppendUInt32(bytes, 5); // parameters
        AppendUInt32(bytes, 2); // techniques
        AppendUInt32(bytes, 0); // ignored legacy count
        AppendUInt32(bytes, 0); // objects

        AppendUInt32(bytes, gainType);
        AppendUInt32(bytes, gainValue);
        AppendUInt32(bytes, 0); // flags
        AppendUInt32(bytes, 1); // annotations
        AppendUInt32(bytes, visibleType);
        AppendUInt32(bytes, visibleValue);

        AppendUInt32(bytes, tintType);
        AppendUInt32(bytes, tintValue);
        AppendUInt32(bytes, 0); // flags
        AppendUInt32(bytes, 0); // annotations

        AppendUInt32(bytes, lightingType);
        AppendUInt32(bytes, empty); // ignored for struct values; defaults follow type metadata
        AppendUInt32(bytes, 0); // flags
        AppendUInt32(bytes, 0); // annotations

        AppendUInt32(bytes, transformType);
        AppendUInt32(bytes, transformValue);
        AppendUInt32(bytes, 0); // flags
        AppendUInt32(bytes, 0); // annotations

        AppendUInt32(bytes, weightsType);
        AppendUInt32(bytes, weightsValue);
        AppendUInt32(bytes, 0); // flags
        AppendUInt32(bytes, 0); // annotations

        AppendUInt32(bytes, firstTechnique);
        AppendUInt32(bytes, 1); // annotations
        AppendUInt32(bytes, 2); // passes
        AppendUInt32(bytes, qualityType);
        AppendUInt32(bytes, qualityValue);

        AppendUInt32(bytes, firstPass);
        AppendUInt32(bytes, 0); // annotations
        AppendUInt32(bytes, 0); // states

        AppendUInt32(bytes, statePass);
        AppendUInt32(bytes, 1); // annotations
        AppendUInt32(bytes, static_cast<std::uint32_t>(renderStates.size()));
        AppendUInt32(bytes, passTagType);
        AppendUInt32(bytes, passTagValue);
        for (std::size_t i = 0; i < renderStates.size(); ++i)
        {
            AppendUInt32(bytes, renderStates[i].type);
            AppendUInt32(bytes, 0); // ignored legacy field
            AppendUInt32(bytes, renderStates[i].isFloat
                                      ? unnamedFloatType : unnamedIntType);
            AppendUInt32(bytes, stateValueOffsets[i]);
        }

        AppendUInt32(bytes, secondTechnique);
        AppendUInt32(bytes, 0); // annotations
        AppendUInt32(bytes, 1); // passes
        AppendUInt32(bytes, secondPass);
        AppendUInt32(bytes, 0); // annotations
        AppendUInt32(bytes, 0); // states

        AppendUInt32(bytes, 0); // small objects
        AppendUInt32(bytes, 0); // large objects
        return bytes;
    }

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
            switch (random.Below(5))
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
                default: break;
            }
        }
        return description.str();
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

TEST(Fna3dCompiledEffectTest, CompiledPassPublishesRenderStatesThroughGraphicsDevice)
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    const std::vector<SyntheticRenderState> states = {
        {MOJOSHADER_RS_ZENABLE, MOJOSHADER_ZB_FALSE},
        {MOJOSHADER_RS_ZWRITEENABLE, 0},
        {MOJOSHADER_RS_ZFUNC, MOJOSHADER_CMP_GREATER},
        {MOJOSHADER_RS_STENCILREF, 7},
        {MOJOSHADER_RS_STENCILMASK, 0x0Fu},
        {MOJOSHADER_RS_CULLMODE, MOJOSHADER_CULL_CW},
        {MOJOSHADER_RS_FILLMODE, MOJOSHADER_FILL_WIREFRAME},
        {MOJOSHADER_RS_SLOPESCALEDEPTHBIAS, FloatBits(0.5f), true},
        {MOJOSHADER_RS_DEPTHBIAS, FloatBits(0.25f), true},
        {MOJOSHADER_RS_SRCBLEND, MOJOSHADER_BLEND_SRCALPHA},
        {MOJOSHADER_RS_DESTBLEND, MOJOSHADER_BLEND_INVSRCALPHA},
        {MOJOSHADER_RS_BLENDOP, MOJOSHADER_BLENDOP_REVSUBTRACT},
        {MOJOSHADER_RS_COLORWRITEENABLE, 5},
        {MOJOSHADER_RS_MULTISAMPLEMASK, 0x0F0F0F0Fu},
        {MOJOSHADER_RS_BLENDFACTOR, 0x10203040u},
    };

    GraphicsDevice device;
    Effect effect(device, BuildSyntheticConformanceEffect(states));
    effect.getTechniquesProperty()[0].getPassesProperty()[1].Apply();

    // XNA and FNA both publish a compiled pass's render states through the device's own state
    // objects, so a game sees exactly the state the effect selected.
    const DepthStencilState& depth = device.getDepthStencilStateProperty();
    EXPECT_FALSE(depth.getDepthBufferEnableProperty());
    EXPECT_FALSE(depth.getDepthBufferWriteEnableProperty());
    EXPECT_EQ(depth.getDepthBufferFunctionProperty(), CompareFunction::Greater);
    EXPECT_EQ(depth.getReferenceStencilProperty(), 7);
    EXPECT_EQ(depth.getStencilMaskProperty(), 0x0F);

    const RasterizerState& rasterizer = device.getRasterizerStateProperty();
    EXPECT_EQ(rasterizer.getCullModeProperty(), CullMode::CullClockwiseFace);
    EXPECT_EQ(rasterizer.getFillModeProperty(), FillMode::WireFrame);
    EXPECT_FLOAT_EQ(rasterizer.getSlopeScaleDepthBiasProperty(), 0.5f);
    EXPECT_FLOAT_EQ(rasterizer.getDepthBiasProperty(), 0.25f);

    const BlendState& blend = device.getBlendStateProperty();
    EXPECT_EQ(blend.getColorSourceBlendProperty(), Blend::SourceAlpha);
    EXPECT_EQ(blend.getColorDestinationBlendProperty(), Blend::InverseSourceAlpha);
    // Without SEPARATEALPHABLENDENABLE the alpha factors follow the colour ones, but BLENDOP
    // alone never changes the alpha blend function -- exactly as FNA's Effect.cs behaves.
    EXPECT_EQ(blend.getAlphaSourceBlendProperty(), Blend::SourceAlpha);
    EXPECT_EQ(blend.getAlphaDestinationBlendProperty(), Blend::InverseSourceAlpha);
    EXPECT_EQ(blend.getColorBlendFunctionProperty(), BlendFunction::ReverseSubtract);
    EXPECT_EQ(blend.getAlphaBlendFunctionProperty(), BlendFunction::Add);
    EXPECT_EQ(static_cast<int>(blend.getColorWriteChannelsProperty()), 5);
    EXPECT_EQ(blend.getMultiSampleMaskProperty(), 0x0F0F0F0F);
    EXPECT_EQ(blend.getBlendFactorProperty(), Color(0x10, 0x20, 0x30, 0x40));
}

TEST(Fna3dCompiledEffectTest, CompiledPassLeavesUnassignedStateGroupsSelected)
{
    using namespace Microsoft::Xna::Framework::Graphics;

    GraphicsDevice device;
    device.setBlendStateProperty(BlendState::NonPremultiplied);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setRasterizerStateProperty(RasterizerState::CullNone);

    // A pass that assigns only a rasterizer token must not rebuild the other two groups.
    Effect effect(device, BuildSyntheticConformanceEffect(
        {{MOJOSHADER_RS_FILLMODE, MOJOSHADER_FILL_WIREFRAME}}));
    effect.getTechniquesProperty()[0].getPassesProperty()[1].Apply();

    EXPECT_EQ(device.getBlendStateProperty().getColorSourceBlendProperty(),
              BlendState::NonPremultiplied.getColorSourceBlendProperty());
    EXPECT_EQ(device.getDepthStencilStateProperty().getDepthBufferEnableProperty(),
              DepthStencilState::None.getDepthBufferEnableProperty());
    EXPECT_EQ(device.getRasterizerStateProperty().getFillModeProperty(), FillMode::WireFrame);
    // The untouched rasterizer fields keep the values the game selected.
    EXPECT_EQ(device.getRasterizerStateProperty().getCullModeProperty(), CullMode::None);
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
    RunEffectMutationCorpus(device, BuildSyntheticConformanceEffect({}),
                            0x465853594E5448ULL, 512);
    RunEffectMutationCorpus(device, LoadStockEffect("BasicEffect.fxb"),
                            0x46584241534943ULL, 128);
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
