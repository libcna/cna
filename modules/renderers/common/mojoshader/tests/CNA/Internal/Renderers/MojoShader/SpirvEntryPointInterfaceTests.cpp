// SPDX-License-Identifier: MS-PL
//
// plans/plan_fx.md FX-129: the module-level invariants of MojoShader's SPIR-V profile, measured on
// the committed compiled-effect corpus.
//
// This suite belongs to the SHARED MojoShader half rather than to any one renderer because the
// SPIR-V profile and MOJOSHADER_linkSPIRVShaders() are shared: WebGPU, Vulkan and SDL_GPU all
// consume exactly these bytes. The defect it guards was found through WebGPU only because WebGPU
// is the backend whose consumer (naga) rejects the module instead of accepting it quietly.
//
// Two invariants, and neither is "the SPIR-V validates" -- both were violated by modules a
// validator was willing to load:
//
//  1. The module header must survive linking. MOJOSHADER_spirv_link_attributes() finishes the
//     dual-purpose gl_PointCoord-or-TEXCOORD0 input by writing through two patch-table offsets. A
//     Shader Model 1.x pixel shader's `t#` register claims the same attrib_offsets[TEXCOORD][0]
//     slot without creating that variable, so those offsets are 0 and the writes landed on word 1
//     of the module -- the SPIR-V version field.
//
//  2. OpEntryPoint must name each interface variable exactly once. A ps_1_x texture register is
//     reachable through two of MojoShader's own bookkeeping lists (ctx->attributes, since CNA's
//     legacy-texcoord-input patch registers it, and ctx->spirv.id_implicit_input[]), and the
//     interface used to be counted from one and emitted from both. A repeated <id> is two
//     arguments at one Location to a consumer.
//
// Both are fixed in cmake/patches/mojoshader-6333f74-spirv-pointcoord-existence.patch and
// mojoshader-6333f74-spirv-entry-interface-unique.patch. Before them, 6 of the 27 committed passes
// produced a fragment module WebGPU refused with "Multiple bindings at location 1 are present",
// and all 6 also carried a zeroed version word.

#if __has_include(<mojoshader.h>)

#include "CNA/TestSupport/TestPaths.hpp"

#include <mojoshader.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace
{
    // ------------------------------------------------------------------------------------------
    // The nine-function MojoShader effect backend, in its smallest honest form: enough to make
    // MOJOSHADER_compileEffect translate every pass and hand back its parse data. Same shape as
    // spikes/webgpu-spirv-spike and tools/graphics/mojoshader_vulkan_probe.cpp.
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
    // The module reader. Deliberately not a validator: it reads back the two words the linker is
    // known to be able to damage, and nothing else.
    // ------------------------------------------------------------------------------------------
    constexpr std::uint32_t kSpirvMagic = 0x07230203u;
    constexpr std::uint32_t kSpirvVersion10 = 0x00010000u;
    constexpr std::uint32_t kOpName = 5u;
    constexpr std::uint32_t kOpEntryPoint = 15u;

    struct ModuleFacts
    {
        std::uint32_t magic = 0;
        std::uint32_t version = 0;
        /// The OpEntryPoint interface list, in module order.
        std::vector<std::uint32_t> entryInterface;
        std::map<std::uint32_t, std::string> names;
    };

    ModuleFacts ReadModule(const std::uint32_t* words, std::size_t wordCount)
    {
        ModuleFacts facts;
        if (wordCount < 5) return facts;
        facts.magic = words[0];
        facts.version = words[1];
        std::size_t i = 5;
        while (i < wordCount)
        {
            const std::uint32_t opcode = words[i] & 0xFFFFu;
            const std::uint32_t length = words[i] >> 16;
            if (length == 0 || i + length > wordCount) break;
            if (opcode == kOpName && length >= 3)
            {
                facts.names[words[i + 1]] =
                    std::string(reinterpret_cast<const char*>(&words[i + 2]));
            }
            else if (opcode == kOpEntryPoint && length >= 4)
            {
                // opcode | execution model | entry <id> | literal name... | interface <id>s
                const char* name = reinterpret_cast<const char*>(&words[i + 3]);
                const std::size_t nameWords = (std::strlen(name) + 1 + 3) / 4;
                for (std::size_t j = i + 3 + nameWords; j < i + length; ++j)
                    facts.entryInterface.push_back(words[j]);
            }
            i += length;
        }
        return facts;
    }

    std::string DescribeInterface(const ModuleFacts& facts)
    {
        std::string text;
        for (std::size_t i = 0; i < facts.entryInterface.size(); ++i)
        {
            const std::uint32_t id = facts.entryInterface[i];
            const auto name = facts.names.find(id);
            text += "\n  arg " + std::to_string(i) + " %" + std::to_string(id) + " " +
                    (name != facts.names.end() ? name->second : std::string("?"));
        }
        return text;
    }

    std::vector<std::uint8_t> ReadFixture(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) return {};
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    /// Every technique and every pass of one fixture, translated and linked the way a backend
    /// does it, with both invariants checked on both stages.
    void CheckEveryPass(const std::filesystem::path& path, int& passesChecked)
    {
        const std::vector<std::uint8_t> bytes = ReadFixture(path);
        ASSERT_FALSE(bytes.empty()) << path;

        ProbeContext context;
        MOJOSHADER_effectShaderContext backend = MakeProbeBackend(&context);
        MOJOSHADER_effect* effect = MOJOSHADER_compileEffect(
            bytes.data(), static_cast<unsigned int>(bytes.size()), nullptr, 0, nullptr, 0,
            &backend);
        ASSERT_NE(effect, nullptr) << path;
        ASSERT_EQ(effect->error_count, 0)
            << path << ": " << (effect->errors != nullptr ? effect->errors[0].error : "<null>");

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

                    // The link step patches vertex INPUT types, so it wants one attribute per
                    // declared shader input; the shapes here do not matter to either invariant.
                    std::vector<MOJOSHADER_vertexAttribute> attributes;
                    for (int a = 0; a < vertexData->attribute_count; ++a)
                    {
                        MOJOSHADER_vertexAttribute attribute{};
                        attribute.usage = vertexData->attributes[a].usage;
                        attribute.usageIndex = vertexData->attributes[a].index;
                        attribute.vertexElementFormat =
                            MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR4;
                        attributes.push_back(attribute);
                    }
                    const int patchTableSize = MOJOSHADER_linkSPIRVShaders(
                        vertexData, pixelData, attributes.data(),
                        static_cast<int>(attributes.size()));
                    ASSERT_GT(patchTableSize, 0)
                        << path << " technique " << t << " pass " << p;

                    const std::string where = path.filename().string() + " technique " +
                                              std::to_string(t) + " pass " + std::to_string(p);
                    for (const auto& [stage, data] :
                         {std::pair<const char*, const MOJOSHADER_parseData*>{"vertex",
                                                                              vertexData},
                          std::pair<const char*, const MOJOSHADER_parseData*>{"pixel", pixelData}})
                    {
                        const std::size_t wordCount =
                            (static_cast<std::size_t>(data->output_len) -
                             static_cast<std::size_t>(patchTableSize)) /
                            sizeof(std::uint32_t);
                        const ModuleFacts facts = ReadModule(
                            reinterpret_cast<const std::uint32_t*>(data->output), wordCount);

                        EXPECT_EQ(facts.magic, kSpirvMagic) << where << " " << stage;
                        // Invariant 1. A zero here means the linker wrote through a patch-table
                        // offset that was never filled in.
                        EXPECT_EQ(facts.version, kSpirvVersion10)
                            << where << " " << stage
                            << ": linking damaged the SPIR-V header version word";

                        // Invariant 2.
                        std::map<std::uint32_t, int> seen;
                        for (const std::uint32_t id : facts.entryInterface) ++seen[id];
                        for (const auto& [id, times] : seen)
                        {
                            const auto name = facts.names.find(id);
                            EXPECT_EQ(times, 1)
                                << where << " " << stage << ": OpEntryPoint names %" << id << " ("
                                << (name != facts.names.end() ? name->second : std::string("?"))
                                << ") " << times << " times" << DescribeInterface(facts);
                        }
                    }
                    ++passesChecked;
                }

                MOJOSHADER_effectEndPass(effect);
                MOJOSHADER_effectEnd(effect);
            }
        }
        MOJOSHADER_deleteEffect(effect);
    }
}

TEST(MojoShaderSpirvEntryPointInterfaceTest, EveryCommittedPassEmitsAWellFormedEntryPoint)
{
    // The whole committed compiled-effect corpus: Microsoft's stock binaries, CNA's own
    // conformance effect, and the two real XNA 4.0 game effects whose Shader Model 1.x pixel
    // shaders are the only content here that reaches the repaired path at all.
    int checked = 0;
    for (const char* name : {"CnaConformanceEffect.fxb", "SpriteEffect.fxb", "BasicEffect.fxb",
                             "AlphaTestEffect.fxb", "DualTextureEffect.fxb",
                             "EnvironmentMapEffect.fxb", "SkinnedEffect.fxb"})
    {
        CheckEveryPass(CNA::TestSupport::CompiledEffectDirectory() / name, checked);
        if (::testing::Test::HasFatalFailure()) return;
    }
    for (const char* name : {"racing-shadow-map-xna4.fxb", "racing-normal-mapping-xna4.fxb"})
    {
        CheckEveryPass(CNA::TestSupport::CompiledEffectFixtureDirectory() / name, checked);
        if (::testing::Test::HasFatalFailure()) return;
    }

    // A guard against the corpus silently shrinking to nothing: 27 passes is the whole committed
    // set, and 6 of them are the Shader Model 1.x ones this suite exists for.
    EXPECT_EQ(checked, 27);
}

#endif // __has_include(<mojoshader.h>)
