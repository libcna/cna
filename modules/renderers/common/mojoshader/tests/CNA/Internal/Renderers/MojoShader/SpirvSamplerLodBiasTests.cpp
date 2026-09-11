// SPDX-License-Identifier: MS-PL
//
// plans/plan_webgpu.md WEBGPU-208: the SPIR-V half of compiled-effect LOD bias, measured on the
// committed compiled-effect corpus rather than on one hand-built module.
//
// The transformation this guards is small but easy to get subtly wrong in a way no pixel test
// notices, because a wrong bias index still renders A picture:
//
//   * a bias applied to "the" shader instead of to each D3D9 sampler register;
//   * an access chain whose array index is the BINDING (which the split doubled) rather than the
//     register;
//   * a module whose id bound no longer covers the ids the rewrite introduced;
//   * an operand appended without widening the instruction's own word count.
//
// So the assertions here are structural and per-sample: every implicit sample that names a split
// sampler must gain exactly one Bias operand, and the float that operand loads must come from the
// array element whose index is that sample's OWN register.
//
// This lives in the shared MojoShader half rather than in the WebGPU renderer for the same reason
// the entry-point suite does: the SPIR-V is shared, so a change to it should be checked wherever
// compiled effects are built, not only where they are currently consumed this way.

#if __has_include(<mojoshader.h>)

#include "CNA/Internal/Renderers/MojoShader/SpirvCombinedSamplerSplit.hpp"
#include "CNA/Internal/Renderers/MojoShader/SpirvSamplerLodBias.hpp"
#include "CNA/Internal/Renderers/MojoShader/SpirvToWgsl.hpp"
#include "CNA/TestSupport/TestPaths.hpp"

#include <mojoshader.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
    using namespace CNA::Internal::Renderers::MojoShaderEffect;

    // ------------------------------------------------------------------------------------------
    // The same minimal nine-function effect backend the entry-point suite uses; a compiled effect
    // does not translate at all without one.
    // ------------------------------------------------------------------------------------------
    struct ProbeShader
    {
        const MOJOSHADER_parseData* parseData = nullptr;
        int refcount = 1;
    };

    constexpr int kMaxFloat4Registers = 256;
    constexpr int kMaxInt4Registers = 16;
    constexpr int kMaxBoolRegisters = 16;

    struct ProbeContext
    {
        ProbeShader* boundVertex = nullptr;
        ProbeShader* boundPixel = nullptr;
        float vsRegF[kMaxFloat4Registers * 4]{};
        int vsRegI[kMaxInt4Registers * 4]{};
        unsigned char vsRegB[kMaxBoolRegisters]{};
        float psRegF[kMaxFloat4Registers * 4]{};
        int psRegI[kMaxInt4Registers * 4]{};
        unsigned char psRegB[kMaxBoolRegisters]{};
        std::string lastError;
    };

    void* MOJOSHADERCALL ProbeCompileShader(const void* contextData, const char* mainfn,
                                           const unsigned char* tokenbuf,
                                           const unsigned int bufsize,
                                           const MOJOSHADER_swizzle* swiz,
                                           const unsigned int swizcount,
                                           const MOJOSHADER_samplerMap* smap,
                                           const unsigned int smapcount)
    {
        auto* context = static_cast<ProbeContext*>(const_cast<void*>(contextData));
        const MOJOSHADER_parseData* data =
            MOJOSHADER_parse(MOJOSHADER_PROFILE_SPIRV, mainfn, tokenbuf, bufsize, swiz, swizcount,
                             smap, smapcount, nullptr, nullptr, nullptr);
        if (data->error_count > 0)
        {
            context->lastError =
                data->errors[0].error != nullptr ? data->errors[0].error : "<null>";
            MOJOSHADER_freeParseData(data);
            return nullptr;
        }
        auto* shader = new ProbeShader{};
        shader->parseData = data;
        return shader;
    }

    void MOJOSHADERCALL ProbeShaderAddRef(void* shaderData)
    {
        if (shaderData != nullptr) static_cast<ProbeShader*>(shaderData)->refcount++;
    }

    void MOJOSHADERCALL ProbeDeleteShader(const void* contextData, void* shaderData)
    {
        if (shaderData == nullptr) return;
        auto* shader = static_cast<ProbeShader*>(shaderData);
        if (--shader->refcount > 0) return;
        auto* context = static_cast<ProbeContext*>(const_cast<void*>(contextData));
        if (context->boundVertex == shader) context->boundVertex = nullptr;
        if (context->boundPixel == shader) context->boundPixel = nullptr;
        MOJOSHADER_freeParseData(shader->parseData);
        delete shader;
    }

    MOJOSHADER_parseData* MOJOSHADERCALL ProbeGetParseData(void* shaderData)
    {
        return shaderData != nullptr ? const_cast<MOJOSHADER_parseData*>(
                                           static_cast<ProbeShader*>(shaderData)->parseData)
                                     : nullptr;
    }

    void MOJOSHADERCALL ProbeBindShaders(const void* contextData, void* vertex, void* pixel)
    {
        auto* context = static_cast<ProbeContext*>(const_cast<void*>(contextData));
        context->boundVertex = static_cast<ProbeShader*>(vertex);
        context->boundPixel = static_cast<ProbeShader*>(pixel);
    }

    void MOJOSHADERCALL ProbeGetBoundShaders(const void* contextData, void** vertex, void** pixel)
    {
        const auto* context = static_cast<const ProbeContext*>(contextData);
        if (vertex != nullptr) *vertex = context->boundVertex;
        if (pixel != nullptr) *pixel = context->boundPixel;
    }

    void MOJOSHADERCALL ProbeMapUniformBufferMemory(const void* contextData, float** vsf,
                                                   int** vsi, unsigned char** vsb, float** psf,
                                                   int** psi, unsigned char** psb)
    {
        auto* context = static_cast<ProbeContext*>(const_cast<void*>(contextData));
        *vsf = context->vsRegF;
        *vsi = context->vsRegI;
        *vsb = context->vsRegB;
        *psf = context->psRegF;
        *psi = context->psRegI;
        *psb = context->psRegB;
    }

    void MOJOSHADERCALL ProbeUnmapUniformBufferMemory(const void*) {}

    const char* MOJOSHADERCALL ProbeGetError(const void* contextData)
    {
        return static_cast<const ProbeContext*>(contextData)->lastError.c_str();
    }

    MOJOSHADER_effectShaderContext MakeProbeBackend(ProbeContext* context)
    {
        MOJOSHADER_effectShaderContext backend{};
        backend.shaderContext = context;
        backend.compileShader = ProbeCompileShader;
        backend.shaderAddRef = ProbeShaderAddRef;
        backend.deleteShader = ProbeDeleteShader;
        backend.getParseData = ProbeGetParseData;
        backend.bindShaders = ProbeBindShaders;
        backend.getBoundShaders = ProbeGetBoundShaders;
        backend.mapUniformBufferMemory = ProbeMapUniformBufferMemory;
        backend.unmapUniformBufferMemory = ProbeUnmapUniformBufferMemory;
        backend.getError = ProbeGetError;
        return backend;
    }

    // ------------------------------------------------------------------------------------------
    // A reader that follows a biased sample back to the array index its bias came from. This is
    // the whole point of the suite, so it resolves the chain rather than pattern-matching words.
    // ------------------------------------------------------------------------------------------
    constexpr std::uint32_t kSpirvMagic = 0x07230203u;
    constexpr std::uint32_t kOpTypeInt = 21u;
    constexpr std::uint32_t kOpTypePointer = 32u;
    constexpr std::uint32_t kOpConstant = 43u;
    constexpr std::uint32_t kOpVariable = 59u;
    constexpr std::uint32_t kOpLoad = 61u;
    constexpr std::uint32_t kOpAccessChain = 65u;
    constexpr std::uint32_t kOpDecorate = 71u;
    constexpr std::uint32_t kOpSampledImage = 86u;
    constexpr std::uint32_t kOpImageSampleImplicitLod = 87u;
    constexpr std::uint32_t kImageOperandsBias = 0x1u;
    constexpr std::uint32_t kDecorationBinding = 33u;
    constexpr std::uint32_t kDecorationDescriptorSet = 34u;

    struct SampleFacts
    {
        /// Register the sample's texture came from, or UINT32_MAX when it could not be resolved.
        std::uint32_t samplerRegister = UINT32_MAX;
        /// Array index the Bias operand loaded from, or UINT32_MAX when the sample carries none.
        std::uint32_t biasIndex = UINT32_MAX;
        bool hasBias = false;
    };

    struct ModuleFacts
    {
        bool walkable = false;
        std::uint32_t bound = 0;
        std::uint32_t highestId = 0;
        std::vector<SampleFacts> samples;
        std::set<std::uint32_t> biasBlockVariables;
    };

    /// Walks @p words and resolves every implicit sample to its register and its bias index.
    ModuleFacts ReadModule(const std::vector<std::uint32_t>& words, std::uint32_t descriptorSet)
    {
        ModuleFacts facts;
        if (words.size() < 5 || words[0] != kSpirvMagic) return facts;
        facts.bound = words[3];

        std::map<std::uint32_t, std::uint32_t> constantValue;   // %const -> literal
        std::map<std::uint32_t, std::uint32_t> variableSlot;    // %var   -> register
        std::map<std::uint32_t, std::uint32_t> valueSlot;       // %id    -> register
        std::map<std::uint32_t, std::uint32_t> chainIndex;      // %ptr   -> array index
        std::map<std::uint32_t, std::uint32_t> loadedIndex;     // %float -> array index
        std::map<std::uint32_t, std::pair<std::uint32_t, std::uint32_t>> setBinding;
        std::set<std::uint32_t> uniformPointers;

        std::size_t i = 5;
        while (i < words.size())
        {
            const std::uint32_t length = words[i] >> 16;
            const std::uint32_t opcode = words[i] & 0xFFFFu;
            if (length == 0 || i + length > words.size()) return facts;
            const std::uint32_t* w = &words[i];

            switch (opcode)
            {
                case kOpDecorate:
                    if (length >= 4)
                    {
                        if (w[2] == kDecorationDescriptorSet) setBinding[w[1]].first = w[3];
                        else if (w[2] == kDecorationBinding) setBinding[w[1]].second = w[3];
                    }
                    break;
                case kOpConstant:
                    if (length >= 4) constantValue[w[2]] = w[3];
                    break;
                case kOpTypeInt:
                case kOpTypePointer: break;
                case kOpVariable:
                    if (length >= 4)
                    {
                        const auto decoration = setBinding.find(w[2]);
                        if (decoration != setBinding.end() &&
                            decoration->second.first == descriptorSet)
                        {
                            if (decoration->second.second ==
                                /* the WebGPU renderer's reserved bias binding */ 32u)
                            {
                                facts.biasBlockVariables.insert(w[2]);
                            }
                            else
                            {
                                variableSlot[w[2]] = decoration->second.second / 2u;
                            }
                        }
                    }
                    break;
                case kOpAccessChain:
                    // %p = OpAccessChain %type %base %member %index %component
                    facts.highestId = std::max(facts.highestId, w[2]);
                    if (length >= 7 && facts.biasBlockVariables.count(w[3]) != 0)
                    {
                        const auto index = constantValue.find(w[5]);
                        if (index != constantValue.end()) chainIndex[w[2]] = index->second;
                    }
                    break;
                case kOpLoad:
                    if (length >= 4)
                    {
                        facts.highestId = std::max(facts.highestId, w[2]);
                        const auto slot = variableSlot.find(w[3]);
                        if (slot != variableSlot.end()) valueSlot[w[2]] = slot->second;
                        const auto chain = chainIndex.find(w[3]);
                        if (chain != chainIndex.end()) loadedIndex[w[2]] = chain->second;
                    }
                    break;
                case kOpSampledImage:
                    if (length >= 5)
                    {
                        auto slot = valueSlot.find(w[3]);
                        if (slot == valueSlot.end()) slot = valueSlot.find(w[4]);
                        if (slot != valueSlot.end()) valueSlot[w[2]] = slot->second;
                    }
                    break;
                case kOpImageSampleImplicitLod:
                {
                    SampleFacts sample;
                    const auto slot = valueSlot.find(w[3]);
                    if (slot != valueSlot.end()) sample.samplerRegister = slot->second;
                    if (length > 5 && w[5] == kImageOperandsBias && length >= 7)
                    {
                        sample.hasBias = true;
                        const auto index = loadedIndex.find(w[6]);
                        if (index != loadedIndex.end()) sample.biasIndex = index->second;
                    }
                    facts.samples.push_back(sample);
                    break;
                }
                default: break;
            }
            i += length;
        }
        facts.walkable = true;
        return facts;
    }

    std::vector<std::uint8_t> ReadFixture(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) return {};
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    struct CorpusTally
    {
        int passes = 0;
        int biasedSamples = 0;
        int passesWithTwoOrMoreRegisters = 0;
        int vertexStagesLeftAlone = 0;
    };

    /// Splits and then injects on every pass of one fixture, asserting the per-sample invariants.
    void CheckEveryPass(const std::filesystem::path& path, CorpusTally& tally)
    {
        const std::vector<std::uint8_t> bytes = ReadFixture(path);
        ASSERT_FALSE(bytes.empty()) << path;

        ProbeContext context;
        MOJOSHADER_effectShaderContext backend = MakeProbeBackend(&context);
        MOJOSHADER_effect* effect = MOJOSHADER_compileEffect(
            bytes.data(), static_cast<unsigned int>(bytes.size()), nullptr, 0, nullptr, 0,
            &backend);
        ASSERT_NE(effect, nullptr) << path;
        ASSERT_EQ(effect->error_count, 0) << path;

        for (int t = 0; t < effect->technique_count; ++t)
        {
            const MOJOSHADER_effectTechnique& technique = effect->techniques[t];
            for (unsigned int p = 0; p < technique.pass_count; ++p)
            {
                MOJOSHADER_effectStateChanges changes{};
                unsigned int count = 0;
                MOJOSHADER_effectSetTechnique(effect, &technique);
                MOJOSHADER_effectBegin(effect, &count, 0, &changes);
                MOJOSHADER_effectBeginPass(effect, p);

                ProbeShader* vertex = context.boundVertex;
                ProbeShader* pixel = context.boundPixel;
                if (vertex != nullptr && pixel != nullptr)
                {
                    const MOJOSHADER_parseData* vertexData = vertex->parseData;
                    const MOJOSHADER_parseData* pixelData = pixel->parseData;
                    std::vector<MOJOSHADER_vertexAttribute> attributes;
                    for (int a = 0; a < vertexData->attribute_count; ++a)
                    {
                        MOJOSHADER_vertexAttribute attribute{};
                        attribute.usage = vertexData->attributes[a].usage;
                        attribute.usageIndex = vertexData->attributes[a].index;
                        attribute.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR4;
                        attributes.push_back(attribute);
                    }
                    const int patchTableSize = MOJOSHADER_linkSPIRVShaders(
                        vertexData, pixelData, attributes.data(),
                        static_cast<int>(attributes.size()));
                    ASSERT_GT(patchTableSize, 0) << path;

                    const std::string where = path.filename().string() + " technique " +
                                              std::to_string(t) + " pass " + std::to_string(p);

                    // --- the pixel stage: split, then inject ---------------------------------
                    const std::size_t pixelWords =
                        (static_cast<std::size_t>(pixelData->output_len) -
                         static_cast<std::size_t>(patchTableSize)) / sizeof(std::uint32_t);
                    SpirvSplitResult split = SplitCombinedImageSamplers(
                        reinterpret_cast<const std::uint32_t*>(pixelData->output), pixelWords);
                    ASSERT_TRUE(split.error.empty()) << where << ": " << split.error;

                    const ModuleFacts before = ReadModule(split.words, /*descriptorSet=*/2u);
                    ASSERT_TRUE(before.walkable) << where << ": the SPLIT module does not walk";
                    for (const SampleFacts& sample : before.samples)
                    {
                        EXPECT_FALSE(sample.hasBias)
                            << where << ": a split module already carried a Bias operand";
                    }

                    SpirvLodBiasResult biased = InjectSamplerLodBias(
                        split.words.data(), split.words.size(), /*descriptorSet=*/2u,
                        /*binding=*/32u);
                    ASSERT_TRUE(biased.error.empty()) << where << ": " << biased.error;

                    const ModuleFacts after = ReadModule(biased.words, /*descriptorSet=*/2u);
                    ASSERT_TRUE(after.walkable)
                        << where << ": the INJECTED module does not walk -- a rewritten "
                                    "instruction's word count is wrong";
                    // Only RESULT ids are compared: a literal operand word (a name, a float
                    // constant) is not an id, and an earlier version of this reader treated every
                    // operand as one and read back ASCII.
                    EXPECT_GT(after.bound, after.highestId)
                        << where << ": the id bound does not cover the result ids the rewrite "
                                    "introduced, so a consumer may reject the module";
                    EXPECT_GE(after.bound, before.bound)
                        << where << ": the rewrite lowered the id bound";

                    std::set<std::uint32_t> registers;
                    for (const SampleFacts& sample : after.samples)
                    {
                        if (sample.samplerRegister == UINT32_MAX)
                        {
                            // A sample this transformation could not attribute must be left
                            // untouched rather than given someone else's bias.
                            EXPECT_FALSE(sample.hasBias)
                                << where << ": an unattributable sample was given a bias";
                            continue;
                        }
                        registers.insert(sample.samplerRegister);
                        EXPECT_TRUE(sample.hasBias)
                            << where << ": the sample of s" << sample.samplerRegister
                            << " kept its unbiased form";
                        // THE invariant: the bias comes from this sample's OWN register.
                        EXPECT_EQ(sample.biasIndex, sample.samplerRegister)
                            << where << ": the sample of s" << sample.samplerRegister
                            << " reads the bias of index " << sample.biasIndex;
                        ++tally.biasedSamples;
                    }
                    if (registers.size() >= 2) ++tally.passesWithTwoOrMoreRegisters;
                    EXPECT_EQ(biased.changed, !after.samples.empty() && !registers.empty())
                        << where << ": `changed` disagrees with what was rewritten";

                    // --- the vertex stage samples nothing, so nothing may be injected --------
                    const std::size_t vertexWords =
                        (static_cast<std::size_t>(vertexData->output_len) -
                         static_cast<std::size_t>(patchTableSize)) / sizeof(std::uint32_t);
                    SpirvSplitResult vertexSplit = SplitCombinedImageSamplers(
                        reinterpret_cast<const std::uint32_t*>(vertexData->output), vertexWords);
                    ASSERT_TRUE(vertexSplit.error.empty()) << where;
                    SpirvLodBiasResult vertexBiased =
                        InjectSamplerLodBias(vertexSplit.words.data(), vertexSplit.words.size(),
                                             /*descriptorSet=*/0u, /*binding=*/32u);
                    ASSERT_TRUE(vertexBiased.error.empty()) << where;
                    EXPECT_FALSE(vertexBiased.changed)
                        << where << ": a stage that samples nothing gained a bias block";
                    EXPECT_EQ(vertexBiased.words, vertexSplit.words)
                        << where << ": a no-op injection still rewrote the module";
                    ++tally.vertexStagesLeftAlone;

                    ++tally.passes;
                }

                MOJOSHADER_effectEndPass(effect);
                MOJOSHADER_effectEnd(effect);
            }
        }
        MOJOSHADER_deleteEffect(effect);
    }

    void SweepCorpus(CorpusTally& tally)
    {
        for (const char* name : {"CnaConformanceEffect.fxb", "SpriteEffect.fxb", "BasicEffect.fxb",
                                 "AlphaTestEffect.fxb", "DualTextureEffect.fxb",
                                 "EnvironmentMapEffect.fxb", "SkinnedEffect.fxb"})
        {
            CheckEveryPass(CNA::TestSupport::CompiledEffectDirectory() / name, tally);
            if (::testing::Test::HasFatalFailure()) return;
        }
        for (const char* name : {"racing-shadow-map-xna4.fxb", "racing-normal-mapping-xna4.fxb"})
        {
            CheckEveryPass(CNA::TestSupport::CompiledEffectFixtureDirectory() / name, tally);
            if (::testing::Test::HasFatalFailure()) return;
        }
    }
}

// plans/plan_webgpu.md WEBGPU-208. The whole committed corpus, every pass, both stages.
TEST(MojoShaderSpirvSamplerLodBiasTest, EverySampledRegisterReadsItsOwnBiasAcrossTheCorpus)
{
    CorpusTally tally;
    SweepCorpus(tally);
    if (::testing::Test::HasFatalFailure()) return;

    // The same 27 passes the entry-point suite counts; a corpus that silently shrank would make
    // every per-sample assertion above vacuous.
    EXPECT_EQ(tally.passes, 27);
    EXPECT_EQ(tally.vertexStagesLeftAlone, 27);
    EXPECT_GT(tally.biasedSamples, 0) << "no pass in the corpus samples a texture at all";

    // Two samplers in ONE shader is the case that separates a per-register implementation from a
    // per-shader one, so the corpus has to contain it for the assertion above to mean anything.
    EXPECT_GT(tally.passesWithTwoOrMoreRegisters, 0)
        << "no committed pass samples two different registers, so the corpus cannot show that two "
           "biases stay apart -- add such a fixture rather than deleting this expectation";
}

// plans/plan_webgpu.md WEBGPU-208. The translator's three cases, built from real modules rather
// than from hand-written SPIR-V, so the shapes are the ones this route actually produces.
TEST(MojoShaderSpirvSamplerLodBiasTest, TheTranslatorSpellsEachImageOperandCaseDistinctly)
{
    const std::vector<std::uint8_t> bytes =
        ReadFixture(CNA::TestSupport::CompiledEffectDirectory() / "CnaConformanceEffect.fxb");
    ASSERT_FALSE(bytes.empty());

    ProbeContext context;
    MOJOSHADER_effectShaderContext backend = MakeProbeBackend(&context);
    MOJOSHADER_effect* effect = MOJOSHADER_compileEffect(
        bytes.data(), static_cast<unsigned int>(bytes.size()), nullptr, 0, nullptr, 0, &backend);
    ASSERT_NE(effect, nullptr);

    bool checked = false;
    for (int t = 0; t < effect->technique_count && !checked; ++t)
    {
        const MOJOSHADER_effectTechnique& technique = effect->techniques[t];
        for (unsigned int p = 0; p < technique.pass_count && !checked; ++p)
        {
            MOJOSHADER_effectStateChanges changes{};
            unsigned int count = 0;
            MOJOSHADER_effectSetTechnique(effect, &technique);
            MOJOSHADER_effectBegin(effect, &count, 0, &changes);
            MOJOSHADER_effectBeginPass(effect, p);

            ProbeShader* vertex = context.boundVertex;
            ProbeShader* pixel = context.boundPixel;
            if (vertex != nullptr && pixel != nullptr)
            {
                const MOJOSHADER_parseData* vertexData = vertex->parseData;
                const MOJOSHADER_parseData* pixelData = pixel->parseData;
                std::vector<MOJOSHADER_vertexAttribute> attributes;
                for (int a = 0; a < vertexData->attribute_count; ++a)
                {
                    MOJOSHADER_vertexAttribute attribute{};
                    attribute.usage = vertexData->attributes[a].usage;
                    attribute.usageIndex = vertexData->attributes[a].index;
                    attribute.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR4;
                    attributes.push_back(attribute);
                }
                const int patchTableSize = MOJOSHADER_linkSPIRVShaders(
                    vertexData, pixelData, attributes.data(),
                    static_cast<int>(attributes.size()));
                ASSERT_GT(patchTableSize, 0);

                const std::size_t pixelWords =
                    (static_cast<std::size_t>(pixelData->output_len) -
                     static_cast<std::size_t>(patchTableSize)) / sizeof(std::uint32_t);
                SpirvSplitResult split = SplitCombinedImageSamplers(
                    reinterpret_cast<const std::uint32_t*>(pixelData->output), pixelWords);
                ASSERT_TRUE(split.error.empty());
                if (split.samplers.empty())
                {
                    MOJOSHADER_effectEndPass(effect);
                    MOJOSHADER_effectEnd(effect);
                    continue;
                }

                // Case 1 -- no image operands at all: an ordinary implicit sample.
                SpirvToWgslResult plain =
                    TranslateSpirvToWgsl(split.words.data(), split.words.size());
                ASSERT_TRUE(plain.error.empty()) << plain.error;
                EXPECT_NE(plain.wgsl.find("textureSample("), std::string::npos)
                    << "an unbiased sample must stay an unbiased sample";
                EXPECT_EQ(plain.wgsl.find("textureSampleBias("), std::string::npos)
                    << "nothing asked for a bias here";

                // Case 2 -- a Bias operand, which is the only one this subset accepts.
                SpirvLodBiasResult biased = InjectSamplerLodBias(
                    split.words.data(), split.words.size(), 2u, 32u);
                ASSERT_TRUE(biased.error.empty()) << biased.error;
                ASSERT_TRUE(biased.changed);
                SpirvToWgslResult withBias =
                    TranslateSpirvToWgsl(biased.words.data(), biased.words.size());
                ASSERT_TRUE(withBias.error.empty()) << withBias.error;
                EXPECT_NE(withBias.wgsl.find("textureSampleBias("), std::string::npos)
                    << withBias.wgsl;

                // Case 3 -- any OTHER image operand still refuses by name. Built by flipping the
                // injected module's operand mask from Bias to Lod, which is the smallest possible
                // difference from a module the translator accepts.
                std::vector<std::uint32_t> lodded = biased.words;
                bool flipped = false;
                for (std::size_t i = 5; i < lodded.size();)
                {
                    const std::uint32_t length = lodded[i] >> 16;
                    const std::uint32_t opcode = lodded[i] & 0xFFFFu;
                    if (length == 0) break;
                    if (opcode == kOpImageSampleImplicitLod && length >= 7 &&
                        lodded[i + 5] == kImageOperandsBias)
                    {
                        lodded[i + 5] = 0x2u;  // ImageOperandsLod
                        flipped = true;
                        break;
                    }
                    i += length;
                }
                ASSERT_TRUE(flipped);
                SpirvToWgslResult refused =
                    TranslateSpirvToWgsl(lodded.data(), lodded.size());
                EXPECT_FALSE(refused.error.empty())
                    << "an image operand outside the subset must be refused BY NAME, not "
                       "approximated";
                EXPECT_NE(refused.error.find("image operands"), std::string::npos)
                    << refused.error;
                checked = true;
            }
            MOJOSHADER_effectEndPass(effect);
            MOJOSHADER_effectEnd(effect);
        }
    }
    MOJOSHADER_deleteEffect(effect);
    EXPECT_TRUE(checked) << "no pass of the conformance fixture declared a sampler";
}

#endif // __has_include(<mojoshader.h>)
