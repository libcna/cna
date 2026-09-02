// SPDX-License-Identifier: MS-PL
//
// plans/plan_fx.md FX-061/FX-062/FX-063/FX-065 existence gate: parse a compiled Effect Framework binary
// through the pinned MojoShader while linking *only* MojoShader -- no FNA3D, no CNA, no graphics
// device of any kind.
//
// Every backend after FNA3D needs the same thing from this dependency: the effect container
// decoded into parameters, techniques, passes, render states and per-pass shader objects. Whether
// that is available without also linking FNA3D decides whether those backends can be implemented
// at all, and the answer was not obvious -- MojoShader is FNA3D's git submodule, its include root
// is not part of FNA3D's install surface, and mojoshader.h hides every Effect Framework struct
// unless the right switches are defined. This probe is what makes the answer a fact rather than a
// reading of build files.
//
// It also doubles as a check on the managed robustness patch: it runs against the same committed
// binaries the renderer tests use, so a pin bump that drops the patch shows up here too.
//
// Usage: cna_mojoshader_effect_probe <file.fxb>...

#include "mojoshader.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    std::uint32_t ReadUInt32LittleEndian(
        const std::vector<unsigned char>& bytes, std::size_t offset)
    {
        return static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    }

    void WriteUInt32LittleEndian(
        std::vector<unsigned char>& bytes, std::size_t offset, std::uint32_t value)
    {
        for (std::size_t i = 0; i < 4; ++i)
            bytes[offset + i] = static_cast<unsigned char>(value >> (i * 8));
    }

    std::size_t FindFirstPassRenderStateOffset(const std::vector<unsigned char>& bytes)
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

    std::vector<unsigned char> ReadFile(const char* path)
    {
        std::FILE* file = std::fopen(path, "rb");
        if (file == nullptr) return {};
        std::fseek(file, 0, SEEK_END);
        const long size = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        std::vector<unsigned char> bytes(size > 0 ? static_cast<std::size_t>(size) : 0u);
        if (!bytes.empty() && std::fread(bytes.data(), 1, bytes.size(), file) != bytes.size())
        {
            bytes.clear();
        }
        std::fclose(file);
        return bytes;
    }

    /// Every shader object the parser meets, kept so the probe can report what it was handed.
    struct ProbeShader
    {
        MOJOSHADER_parseData* parseData = nullptr;
        unsigned int tokenBytes = 0;
        int refCount = 1;
    };

    unsigned int shadersSeen = 0;
    unsigned int shaderTokenBytes = 0;

    // The parser will not run without a backend: MOJOSHADER_compileEffect refuses a null context
    // outright, and it calls compileShader for every shader object in the container. So this is
    // both the existence gate and a statement of the surface a new renderer has to fill.
    //
    // Worth recording for plans/plan_fx.md FX-070: compileShader receives the raw D3D9 token buffer. A
    // Direct3D 9 backend's implementation is therefore CreateVertexShader/CreatePixelShader on
    // that buffer, with nothing translated. This probe instead runs MOJOSHADER_parse over the
    // GLSL profile, because reflection has to come from somewhere and the pin disables the
    // BYTECODE profile that would otherwise give it without translating.
    void* ProbeCompileShader(const void*, const char* mainfn,
                             const unsigned char* tokenbuf, const unsigned int bufsize,
                             const MOJOSHADER_swizzle* swiz, const unsigned int swizcount,
                             const MOJOSHADER_samplerMap* smap, const unsigned int smapcount)
    {
        ProbeShader* shader = new ProbeShader();
        shader->tokenBytes = bufsize;
        shader->parseData = const_cast<MOJOSHADER_parseData*>(
            MOJOSHADER_parse(MOJOSHADER_PROFILE_GLSL, mainfn, tokenbuf, bufsize,
                             swiz, swizcount, smap, smapcount, nullptr, nullptr, nullptr));
        ++shadersSeen;
        shaderTokenBytes += bufsize;
        return shader;
    }

    void ProbeShaderAddRef(void* shader)
    {
        if (shader != nullptr) ++static_cast<ProbeShader*>(shader)->refCount;
    }

    void ProbeDeleteShader(const void*, void* shader)
    {
        ProbeShader* probe = static_cast<ProbeShader*>(shader);
        if (probe == nullptr || --probe->refCount > 0) return;
        if (probe->parseData != nullptr) MOJOSHADER_freeParseData(probe->parseData);
        delete probe;
    }

    MOJOSHADER_parseData* ProbeGetParseData(void* shader)
    {
        const ProbeShader* probe = static_cast<const ProbeShader*>(shader);
        return probe != nullptr ? probe->parseData : nullptr;
    }

    void ProbeBindShaders(const void*, void*, void*) {}
    void ProbeGetBoundShaders(const void*, void** vertex, void** pixel)
    {
        if (vertex != nullptr) *vertex = nullptr;
        if (pixel != nullptr) *pixel = nullptr;
    }
    void ProbeMapUniformBufferMemory(const void*, float**, int**, unsigned char**,
                                     float**, int**, unsigned char**) {}
    void ProbeUnmapUniformBufferMemory(const void*) {}
    const char* ProbeGetError(const void*) { return ""; }

    MOJOSHADER_effectShaderContext MakeProbeBackend()
    {
        MOJOSHADER_effectShaderContext backend{};
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

    int VerifyInvalidRenderStateRejected(
        const char* path, const std::vector<unsigned char>& source,
        const MOJOSHADER_effectShaderContext& backend)
    {
        std::vector<unsigned char> malformed = source;
        const std::size_t stateTypeOffset = FindFirstPassRenderStateOffset(malformed);
        if (stateTypeOffset >= malformed.size())
        {
            std::printf("%s: could not locate a pass render state for mutation\n", path);
            return 1;
        }
        WriteUInt32LittleEndian(malformed, stateTypeOffset, 0xFFFFFFFFu);

        const MOJOSHADER_effect* effect = MOJOSHADER_compileEffect(
            malformed.data(), static_cast<unsigned int>(malformed.size()),
            nullptr, 0, nullptr, 0, &backend);
        const bool rejected = effect != nullptr && effect->error_count > 0;
        if (!rejected)
        {
            MOJOSHADER_deleteEffect(effect);
            std::printf("%s: invalid pass render-state identifier was accepted\n", path);
            return 1;
        }
        std::printf("%s: invalid pass render-state identifier rejected\n", path);
        return 0;
    }

    int VerifyLegacyTexcrdTranslationForProfile(
        const char* profile, std::string_view expectedAssignment)
    {
        // ps_1_1: texcoord t0; mov r0, t0; end. The official XNA 4 EffectProcessor
        // emits TEXCRD for legacy techniques still carried alongside their ps_2_0
        // counterparts. MojoShader used to reject the entire Effect before a game
        // could select the modern technique.
        constexpr std::array<unsigned char, 28> shader = {
            0x01, 0x01, 0xff, 0xff,
            0x40, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x0f, 0xb0,
            0x01, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x0f, 0x80,
            0x00, 0x00, 0xe4, 0xb0,
            0xff, 0xff, 0x00, 0x00,
        };
        const MOJOSHADER_parseData* parsed = MOJOSHADER_parse(
            profile, "LegacyTexcrdProbe", shader.data(),
            static_cast<unsigned int>(shader.size()), nullptr, 0, nullptr, 0,
            nullptr, nullptr, nullptr);
        if (parsed == nullptr)
        {
            std::printf("legacy TEXCRD probe: parse returned nothing\n");
            return 1;
        }

        int failure = 0;
        if (parsed->error_count > 0)
        {
            for (int i = 0; i < parsed->error_count; ++i)
            {
                std::printf("legacy TEXCRD probe: error: %s\n",
                            parsed->errors[i].error != nullptr
                                ? parsed->errors[i].error : "<null>");
            }
            failure = 1;
        }
        else
        {
            const std::string_view output(
                parsed->output != nullptr ? parsed->output : "",
                parsed->output_len > 0 ? static_cast<std::size_t>(parsed->output_len) : 0u);
            if (output.find(expectedAssignment) == std::string_view::npos)
            {
                std::printf(
                    "legacy TEXCRD probe (%s): translated GLSL does not reload "
                    "TEXCOORD0\n",
                    profile);
                failure = 1;
            }
        }
        MOJOSHADER_freeParseData(parsed);
        if (failure == 0)
            std::printf("legacy TEXCRD probe (%s): ps_1_1 TEXCOORD reload translated\n",
                        profile);
        return failure;
    }

    int VerifyLegacyTexcrdTranslation()
    {
        return VerifyLegacyTexcrdTranslationForProfile(
                   MOJOSHADER_PROFILE_GLSL120, "ps_t0 = gl_TexCoord[0]") +
               VerifyLegacyTexcrdTranslationForProfile(
                   MOJOSHADER_PROFILE_GLSLES, "ps_t0 = io_5_0");
    }

    /// Reports what the parser found, so a silently empty parse cannot read as success.
    int Describe(const char* path, const MOJOSHADER_effect* effect)
    {
        if (effect == nullptr)
        {
            std::printf("%s: parse returned nothing\n", path);
            return 1;
        }
        if (effect->error_count > 0)
        {
            for (int i = 0; i < effect->error_count; ++i)
            {
                std::printf("%s: error: %s\n", path,
                            effect->errors[i].error != nullptr ? effect->errors[i].error
                                                               : "<null>");
            }
            return 1;
        }

        int passCount = 0;
        for (int i = 0; i < effect->technique_count; ++i)
        {
            passCount += static_cast<int>(effect->techniques[i].pass_count);
        }
        std::printf("%s: %d parameters, %d techniques, %d passes, %d objects\n",
                    path, effect->param_count, effect->technique_count, passCount,
                    effect->object_count);

        // An effect with no techniques would satisfy every count above while proving nothing about
        // the container actually being understood.
        if (effect->technique_count <= 0 || passCount <= 0)
        {
            std::printf("%s: parsed, but carries no technique or no pass\n", path);
            return 1;
        }
        return 0;
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: cna_mojoshader_effect_probe <file.fxb>...\n");
        return 2;
    }

    int failures = VerifyLegacyTexcrdTranslation();
    for (int i = 1; i < argc; ++i)
    {
        const std::vector<unsigned char> bytes = ReadFile(argv[i]);
        if (bytes.empty())
        {
            std::fprintf(stderr, "cannot read %s\n", argv[i]);
            ++failures;
            continue;
        }

        MOJOSHADER_effectShaderContext backend = MakeProbeBackend();
        const MOJOSHADER_effect* effect = MOJOSHADER_compileEffect(
            bytes.data(), static_cast<unsigned int>(bytes.size()),
            nullptr, 0,              // no swizzles
            nullptr, 0,              // no sampler map
            &backend);
        failures += Describe(argv[i], effect);
        MOJOSHADER_deleteEffect(effect);
        failures += VerifyInvalidRenderStateRejected(argv[i], bytes, backend);
    }

    std::printf("%u shader objects, %u bytes of Shader Model bytecode handed to the backend\n",
                shadersSeen, shaderTokenBytes);
    std::printf("%s\n", failures == 0
        ? "MojoShader parses compiled effects with no FNA3D linked."
        : "one or more effects failed");
    return failures == 0 ? 0 : 1;
}
