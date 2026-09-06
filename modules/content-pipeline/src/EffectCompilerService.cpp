// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-A2: the build-time effect compiler backend.
//
// **What XNA 4.0's Effect XNB payload actually contains**, established from this repository's own
// evidence rather than from memory:
//
//   * The payload is a **legacy Direct3D 9 Effect Framework binary**, magic `0xFEFF0901`. That is
//     what `CNA::Content::Pipeline::IsCompiledEffectBinary` accepts, what CNA's runtime `Effect`
//     preflight parses, and what the six committed stock `.fxb` blobs begin with -- files FNA
//     ships and loads through the same code path. XNA 4.0's own Content Pipeline additionally
//     wraps it in a `0xBCF00BCF` header carrying the offset of the inner token, which the same
//     function accepts.
//   * The compiler that produces it is **`fxc` at profile `fx_2_0`**, and specifically the legacy
//     one: `modules/renderers/fna3d/effects/README.md` records the exact identity used to build
//     this repository's own conformance binary -- Microsoft (R) Direct3D Shader Compiler
//     9.29.952.3111 from the June 2010 DirectX SDK -- and records the reason a modern compiler is
//     not a substitute. `fxc` delegates the `fx_2_0` path to `d3dx9`, and without the d3dx9/
//     D3DCompiler redistributables beside it the compile fails with *"E5017: Aborting due to not
//     yet implemented feature: Write pass assignments"*. A standalone `d3dcompiler_47` therefore
//     cannot write a legacy effect at all. That is decisive: there is no modern or portable
//     compiler that produces this container, which is why this backend runs an external process
//     rather than linking a library.
//   * The shader model inside it comes from the source's own `compile vs_2_0 …` statements. CNA
//     does not rewrite them, so Reach versus HiDef is the author's decision expressed in the
//     source; see `EffectSourceProfile` for exactly what CNA's profile selection does and does
//     not claim.
//   * **Windows only.** The Xbox 360's shader bytecode is a different instruction set produced by
//     a different compiler, and no evidence in this repository describes a Windows Phone Effect
//     payload; the extended XNB ecosystem uses its own effect container entirely. The XNB Effect
//     writer refuses a non-Windows target rather than writing Direct3D 9 bytes under one.
//
// Nothing here is derived from Microsoft, MonoGame or FNA source. The command-line shape is the
// documented public interface of a compiler this repository already invokes (see
// `modules/renderers/directx9/src/shaders/compile_shaders_sm2.py` and that README's reproduction
// command), and no compiler binary is committed or redistributed.

#include "CNA/Content/Pipeline/EffectCompilerService.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <atomic>
#include <random>
#include <sstream>
#include <system_error>
#include <utility>

#include "CNA/Internal/HostProcess.hpp"

namespace CNA::Content::Pipeline
{
    namespace
    {
        /** @brief The profile the legacy Effect compiler must be asked for. */
        constexpr const char* kTargetProfile = "fx_2_0";

        [[nodiscard]] std::string EnvironmentValue(const char* name)
        {
            const char* value = std::getenv(name);
            return value == nullptr ? std::string{} : std::string(value);
        }

        /** @brief The compiler path CMake baked in, or an empty string. */
        [[nodiscard]] std::string ConfiguredExecutable()
        {
#if defined(CNA_FXC_EXECUTABLE)
            return CNA_FXC_EXECUTABLE;
#else
            return {};
#endif
        }

        /** @brief The launcher CMake baked in, or an empty string. */
        [[nodiscard]] std::string ConfiguredLauncher()
        {
#if defined(CNA_FXC_LAUNCHER)
            return CNA_FXC_LAUNCHER;
#else
            return {};
#endif
        }

        [[nodiscard]] std::string Trim(const std::string& text)
        {
            const auto begin = text.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos) { return {}; }
            const auto end = text.find_last_not_of(" \t\r\n");
            return text.substr(begin, end - begin + 1u);
        }

        /**
         * @brief Extracts a version from a compiler banner.
         *
         * `fxc` announces itself as `Microsoft (R) Direct3D Shader Compiler 9.29.952.3111`. Any
         * dotted numeric run of three or more components is taken as the version, which keeps this
         * working for a compiler that words its banner differently without pretending to know the
         * wording.
         */
        [[nodiscard]] std::string ExtractVersion(const std::string& banner)
        {
            std::size_t index = 0u;
            while (index < banner.size())
            {
                if (std::isdigit(static_cast<unsigned char>(banner[index])) == 0)
                {
                    ++index;
                    continue;
                }
                const std::size_t start = index;
                std::size_t dots = 0u;
                while (index < banner.size() &&
                       (std::isdigit(static_cast<unsigned char>(banner[index])) != 0 ||
                        banner[index] == '.'))
                {
                    if (banner[index] == '.') { ++dots; }
                    ++index;
                }
                if (dots >= 2u) { return banner.substr(start, index - start); }
            }
            return {};
        }

        /** @brief A scratch directory that removes itself, for the compiler's output file. */
        class ScratchDirectory
        {
        public:
            ScratchDirectory()
            {
                std::error_code error;
                // The counter makes concurrent workers independent without a clock or a random
                // source, so the pipeline stays deterministic.
                static std::atomic<unsigned long long> counter{0u};
                path_ = std::filesystem::temp_directory_path(error) /
                        ("cna-fx-" + std::to_string(counter.fetch_add(1u)) + "-" +
                         std::to_string(reinterpret_cast<std::uintptr_t>(this)));
                std::filesystem::create_directories(path_, error);
            }

            ~ScratchDirectory()
            {
                std::error_code error;
                std::filesystem::remove_all(path_, error);
            }

            ScratchDirectory(const ScratchDirectory&) = delete;
            ScratchDirectory& operator=(const ScratchDirectory&) = delete;

            [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

        private:
            std::filesystem::path path_;
        };

        /** @brief What one pass of a compiled effect disturbs, as XNA's header records it. */
        struct EffectPassStateSummary
        {
            /** @brief Bit 0 blend, bit 1 depth-stencil, bit 2 rasterizer. */
            std::uint32_t stateGroups = 0u;
            /** @brief Bit per sampler register the pass's shaders bind. */
            std::uint32_t samplerRegisters = 0u;
        };

        /**
         * @brief Which of XNA's three device states a container render state belongs to.
         *
         * The Direct3D 9 effect container numbers the render states densely from `D3DRS_ZENABLE`,
         * and the three sets below are measured rather than derived: three fixtures each assign
         * every state of one group and nothing else, and the genuine build answers 1, 2 and 4 for
         * them (`effect/fx_state_blend_wide`, `_depth_wide`, `_raster_wide` in the differential
         * corpus, plans/plan_xnapipeline_parity.md XNAPP-191). 146 and 147 are the pass's own
         * vertex and pixel shader assignments and belong to no device state.
         *
         * A state outside every set is one this repository has not measured. It answers a group
         * of its own -- see @ref SummarizeEffectPasses, which then declines to describe the effect
         * at all rather than describe it wrongly.
         *
         * @param stateType The container's dense render-state number.
         * @return 1 blend, 2 depth-stencil, 4 rasterizer, 0 for a shader assignment, 8 for
         *         anything unmeasured.
         */
        [[nodiscard]] std::uint32_t EffectStateGroupOf(const std::uint32_t stateType)
        {
            switch (stateType)
            {
            // SrcBlend, DestBlend, AlphaBlendEnable, ColorWriteEnable, BlendOp,
            // SeparateAlphaBlendEnable, SrcBlendAlpha, DestBlendAlpha, BlendOpAlpha.
            case 6u: case 7u: case 13u: case 73u: case 75u:
            case 99u: case 100u: case 101u: case 102u:
                return 1u;
            // ZEnable, ZWriteEnable, ZFunc, StencilEnable, StencilFail, StencilZFail,
            // StencilPass, StencilFunc, StencilRef, StencilMask, StencilWriteMask.
            case 0u: case 3u: case 9u: case 22u: case 23u: case 24u:
            case 25u: case 26u: case 27u: case 28u: case 29u:
                return 2u;
            // FillMode, CullMode, ScissorTestEnable, DepthBias, SlopeScaleDepthBias,
            // MultiSampleAntialias.
            case 1u: case 8u: case 68u: case 78u: case 79u: case 98u:
                return 4u;
            // VertexShader, PixelShader.
            case 146u: case 147u:
                return 0u;
            default:
                return 8u;
            }
        }

        /**
         * @brief The sampler registers one Direct3D 9 shader binds, as a bit per register.
         *
         * A shader declares each sampler with a `dcl` instruction whose destination names a
         * register of type `D3DSPR_SAMPLER`, and the register type is split across two fields of
         * that token, which is why it is reassembled rather than masked out in one go.
         *
         * @param bytes The whole container.
         * @param start Offset of the shader's version token.
         * @param end One past the shader blob's last byte.
         * @return The mask, or zero when the stream ends or stops making sense.
         */
        [[nodiscard]] std::uint32_t SamplerRegistersOf(const std::vector<std::uint8_t>& bytes,
                                                       const std::size_t start,
                                                       const std::size_t end)
        {
            const auto word = [&bytes](const std::size_t at)
            {
                return static_cast<std::uint32_t>(bytes[at]) |
                       (static_cast<std::uint32_t>(bytes[at + 1u]) << 8) |
                       (static_cast<std::uint32_t>(bytes[at + 2u]) << 16) |
                       (static_cast<std::uint32_t>(bytes[at + 3u]) << 24);
            };
            constexpr std::uint32_t kEndToken = 0x0000FFFFu;
            constexpr std::uint32_t kCommentOpcode = 0xFFFEu;
            constexpr std::uint32_t kDeclareOpcode = 0x001Fu;
            constexpr std::uint32_t kSamplerRegisterType = 10u;
            std::uint32_t mask = 0u;
            std::size_t at = start + 4u;
            while (at + 4u <= end)
            {
                const std::uint32_t token = word(at);
                if (token == kEndToken) { return mask; }
                const std::uint32_t opcode = token & 0xFFFFu;
                if (opcode == kCommentOpcode)
                {
                    at += 4u + 4u * static_cast<std::size_t>((token >> 16) & 0x7FFFu);
                    continue;
                }
                const std::uint32_t length = (token >> 24) & 0xFu;
                if (opcode == kDeclareOpcode && length >= 2u && at + 12u <= end)
                {
                    const std::uint32_t destination = word(at + 8u);
                    const std::uint32_t registerType =
                        ((destination >> 28) & 0x7u) | ((destination >> 8) & 0x18u);
                    if (registerType == kSamplerRegisterType)
                    {
                        const std::uint32_t index = destination & 0x7FFu;
                        if (index < 32u) { mask |= 1u << index; }
                    }
                }
                if (length == 0u) { return mask; }
                at += 4u * (static_cast<std::size_t>(length) + 1u);
            }
            return mask;
        }

        /**
         * @brief Reads one summary per pass out of a compiled Direct3D 9 effect.
         *
         * The container's own graph answers both halves: a pass lists the render states it
         * assigns, and the large-object table records, for every shader blob, which technique and
         * pass it belongs to, which is the attribution the sampler mask needs when two passes bind
         * different samplers. Validated against fifteen effects built by the genuine pipeline: the
         * header this reproduces is byte-identical in every one
         * (plans/plan_xnapipeline_parity.md XNAPP-191).
         *
         * Every read is bounds-checked and every unexpected shape answers empty, because a
         * summary this build is not sure of must not be written: the caller then emits the single
         * zero pair, which is what it always emitted before.
         *
         * @param bytes The bare Effect Framework binary, beginning with `0xFEFF0901`.
         * @return One summary per pass in container order, or empty when it cannot be read.
         */
        [[nodiscard]] std::vector<EffectPassStateSummary> SummarizeEffectPasses(
            const std::vector<std::uint8_t>& bytes)
        {
            constexpr std::size_t kMaximumItems = 64u * 1024u;
            const std::vector<EffectPassStateSummary> unreadable;
            std::size_t cursor = 0u;
            bool failed = false;
            const auto read = [&bytes, &cursor, &failed]() -> std::uint32_t
            {
                if (failed || cursor + 4u > bytes.size()) { failed = true; return 0u; }
                const std::uint32_t value =
                    static_cast<std::uint32_t>(bytes[cursor]) |
                    (static_cast<std::uint32_t>(bytes[cursor + 1u]) << 8) |
                    (static_cast<std::uint32_t>(bytes[cursor + 2u]) << 16) |
                    (static_cast<std::uint32_t>(bytes[cursor + 3u]) << 24);
                cursor += 4u;
                return value;
            };
            const auto skip = [&bytes, &cursor, &failed](const std::size_t count)
            {
                if (failed || count > bytes.size() - std::min(cursor, bytes.size()))
                {
                    failed = true;
                    return;
                }
                cursor += count;
            };

            if (bytes.size() < 8u) { return unreadable; }
            cursor = 4u;
            const std::uint32_t structureOffset = read();
            const std::size_t base = 8u;
            if (failed || structureOffset > bytes.size() - base || (structureOffset & 3u) != 0u)
            {
                return unreadable;
            }
            cursor = base + structureOffset;

            const std::uint32_t parameterCount = read();
            const std::uint32_t techniqueCount = read();
            static_cast<void>(read());
            static_cast<void>(read());
            if (failed || parameterCount > kMaximumItems || techniqueCount == 0u ||
                techniqueCount > kMaximumItems)
            {
                return unreadable;
            }
            for (std::uint32_t index = 0u; index < parameterCount; ++index)
            {
                static_cast<void>(read());
                static_cast<void>(read());
                static_cast<void>(read());
                const std::uint32_t annotations = read();
                if (failed || annotations > kMaximumItems) { return unreadable; }
                skip(static_cast<std::size_t>(annotations) * 8u);
            }

            std::vector<EffectPassStateSummary> passes;
            std::map<std::pair<std::uint32_t, std::uint32_t>, std::size_t> located;
            for (std::uint32_t technique = 0u; technique < techniqueCount; ++technique)
            {
                static_cast<void>(read());
                const std::uint32_t annotations = read();
                const std::uint32_t passCount = read();
                if (failed || annotations > kMaximumItems || passCount == 0u ||
                    passCount > kMaximumItems)
                {
                    return unreadable;
                }
                skip(static_cast<std::size_t>(annotations) * 8u);
                for (std::uint32_t pass = 0u; pass < passCount; ++pass)
                {
                    static_cast<void>(read());
                    const std::uint32_t passAnnotations = read();
                    const std::uint32_t stateCount = read();
                    if (failed || passAnnotations > kMaximumItems || stateCount > kMaximumItems)
                    {
                        return unreadable;
                    }
                    skip(static_cast<std::size_t>(passAnnotations) * 8u);
                    EffectPassStateSummary summary;
                    for (std::uint32_t state = 0u; state < stateCount; ++state)
                    {
                        const std::uint32_t stateType = read();
                        static_cast<void>(read());
                        static_cast<void>(read());
                        static_cast<void>(read());
                        if (failed) { return unreadable; }
                        const std::uint32_t group = EffectStateGroupOf(stateType);
                        // An unmeasured state: this build does not know which device state it
                        // belongs to, and a header that guesses is worse than the zero one.
                        if (group == 8u) { return unreadable; }
                        summary.stateGroups |= group;
                    }
                    located[{technique, pass}] = passes.size();
                    passes.push_back(summary);
                }
            }

            const std::uint32_t smallObjects = read();
            const std::uint32_t largeObjects = read();
            if (failed || smallObjects > kMaximumItems || largeObjects > kMaximumItems)
            {
                return unreadable;
            }
            for (std::uint32_t index = 0u; index < smallObjects; ++index)
            {
                static_cast<void>(read());
                const std::uint32_t length = read();
                if (failed) { return unreadable; }
                skip((static_cast<std::size_t>(length) + 3u) & ~static_cast<std::size_t>(3u));
            }
            for (std::uint32_t index = 0u; index < largeObjects; ++index)
            {
                const std::uint32_t technique = read();
                const std::uint32_t pass = read();
                static_cast<void>(read());
                static_cast<void>(read());
                static_cast<void>(read());
                const std::uint32_t length = read();
                if (failed) { return unreadable; }
                const std::size_t blob = cursor;
                skip((static_cast<std::size_t>(length) + 3u) & ~static_cast<std::size_t>(3u));
                if (failed) { return unreadable; }
                if (length < 4u) { continue; }
                const std::uint32_t head =
                    static_cast<std::uint32_t>(bytes[blob]) |
                    (static_cast<std::uint32_t>(bytes[blob + 1u]) << 8) |
                    (static_cast<std::uint32_t>(bytes[blob + 2u]) << 16) |
                    (static_cast<std::uint32_t>(bytes[blob + 3u]) << 24);
                const std::uint32_t kind = head & 0xFFFF0000u;
                if (kind != 0xFFFE0000u && kind != 0xFFFF0000u) { continue; }
                const auto found = located.find({technique, pass});
                if (found == located.end()) { continue; }
                passes[found->second].samplerRegisters |=
                    SamplerRegistersOf(bytes, blob, blob + length);
            }
            return passes;
        }

        /** @brief The external-process backend. */
        /**
         * @brief Puts the header XNA's own pipeline puts in front of a compiled effect.
         *
         * `fxc /T fx_2_0 /Fo` writes the bare Direct3D 9 Effect Framework binary, magic
         * `0xFEFF0901`. XNA's `EffectProcessor` answers the same binary behind a header -- magic
         * `0xBCF00BCF`, the offset of the inner token, then **one pair of dwords per pass** -- and
         * that is what reaches an `.xnb` and what a game loads.
         *
         * The pair was recorded here as two zeros, because the two effects
         * `effectprocessor/compile_simple_digest` and `effectprocessor/compile_second_digest`
         * measure the processor called *directly* and both answer zero. A build answers something
         * else: the pair says which device state that pass disturbs and which sampler registers it
         * binds, and its length therefore follows the pass count rather than being sixteen bytes
         * (plans/plan_xnapipeline_parity.md `XNAPP-191`, and @ref SummarizeEffectPasses for the
         * rule and the seventeen effects it was measured on).
         *
         * Applied here rather than in the processor because this is where an *fxc result*
         * exists: a caller that supplies its own compiler is handing over finished bytes, and
         * those still reach the container unchanged.
         *
         * @param bytecode The compiler's output, wrapped in place when it needs wrapping.
         */
        void WrapAsXnaEffect(std::vector<std::uint8_t>& bytecode)
        {
            constexpr std::uint32_t kEffectFrameworkToken = 0xFEFF0901u;
            constexpr std::uint32_t kXna4EffectWrapperToken = 0xBCF00BCFu;
            if (bytecode.size() < 4u) { return; }
            const std::uint32_t leading = static_cast<std::uint32_t>(bytecode[0]) |
                                          (static_cast<std::uint32_t>(bytecode[1]) << 8) |
                                          (static_cast<std::uint32_t>(bytecode[2]) << 16) |
                                          (static_cast<std::uint32_t>(bytecode[3]) << 24);
            if (leading != kEffectFrameworkToken) { return; }

            // One pair per pass, or the single zero pair when the container cannot be read. The
            // pair is the *pass's* own summary, so a container this build cannot walk is described
            // as disturbing nothing, which is what the header said before it was understood at all.
            std::vector<EffectPassStateSummary> passes = SummarizeEffectPasses(bytecode);
            if (passes.empty()) { passes.emplace_back(); }
            const std::size_t headerBytes = 8u + 8u * passes.size();

            std::vector<std::uint8_t> wrapped;
            wrapped.reserve(bytecode.size() + headerBytes);
            const auto word32 = [&wrapped](std::uint32_t value)
            {
                for (int shift = 0; shift < 32; shift += 8)
                {
                    wrapped.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
                }
            };
            word32(kXna4EffectWrapperToken);
            word32(static_cast<std::uint32_t>(headerBytes));
            for (const EffectPassStateSummary& pass : passes)
            {
                word32(pass.stateGroups | (pass.samplerRegisters << 3));
                word32(pass.samplerRegisters);
            }
            wrapped.insert(wrapped.end(), bytecode.begin(), bytecode.end());
            bytecode = std::move(wrapped);
        }


        class ExternalEffectCompiler final : public EffectCompilerService
        {
        public:
            explicit ExternalEffectCompiler(const ExternalEffectCompilerOptions& options)
            {
                Resolve(options);
            }

            [[nodiscard]] EffectCompilerIdentity Identity() const override { return identity_; }

            [[nodiscard]] bool Available() const override { return available_; }

            [[nodiscard]] std::string UnavailableReason() const override { return reason_; }

            [[nodiscard]] EffectCompileResult Compile(
                const EffectCompileRequest& request) const override
            {
                EffectCompileResult result;
                if (!available_)
                {
                    EffectCompilerDiagnostic diagnostic;
                    diagnostic.file = request.source.string();
                    diagnostic.message = reason_;
                    result.diagnostics.push_back(diagnostic);
                    return result;
                }

                const ScratchDirectory scratch;
                const std::filesystem::path output = scratch.Path() / "effect.fxb";

                // Every path in one translation, in a fixed order: output, source, then the
                // include directories.
                std::vector<std::filesystem::path> paths{output, request.source};
                paths.insert(paths.end(), request.includeDirectories.begin(),
                             request.includeDirectories.end());
                const std::vector<std::string> spelled = SpellForLauncher(paths);

                std::vector<std::string> arguments;
                if (!launcher_.empty()) { arguments.push_back(executable_.string()); }
                arguments.push_back("/nologo");
                arguments.push_back("/T");
                arguments.push_back(kTargetProfile);
                arguments.push_back("/Fo");
                arguments.push_back(spelled[0]);
                arguments.push_back(request.debugInformation ? "/Zi" : "/Qstrip_debug");
                arguments.push_back("/D");
                arguments.push_back(request.profile == EffectSourceProfile::HiDef
                                        ? "CNA_HIDEF=1"
                                        : "CNA_REACH=1");
                for (const auto& [name, value] : request.defines)
                {
                    arguments.push_back("/D");
                    arguments.push_back(value.empty() ? name : name + "=" + value);
                }
                for (std::size_t at = 2u; at < spelled.size(); ++at)
                {
                    arguments.push_back("/I");
                    arguments.push_back(spelled[at]);
                }
                arguments.push_back(spelled[1]);

                const CNA::Internal::HostProcessResult process = CNA::Internal::RunHostProcess(
                    launcher_.empty() ? executable_ : launcher_, arguments);
                if (!process.started)
                {
                    EffectCompilerDiagnostic diagnostic;
                    diagnostic.file = request.source.string();
                    diagnostic.message = "the effect compiler could not be run: " +
                                         process.failure;
                    result.diagnostics.push_back(diagnostic);
                    return result;
                }

                result.diagnostics = ParseEffectCompilerDiagnostics(process.standardError);
                for (const EffectCompilerDiagnostic& diagnostic :
                     ParseEffectCompilerDiagnostics(process.standardOutput))
                {
                    result.diagnostics.push_back(diagnostic);
                }

                if (process.exitCode != 0)
                {
                    if (result.diagnostics.empty())
                    {
                        EffectCompilerDiagnostic diagnostic;
                        diagnostic.file = request.source.string();
                        diagnostic.message = "the effect compiler exited with status " +
                                             std::to_string(process.exitCode) +
                                             " and said nothing";
                        result.diagnostics.push_back(diagnostic);
                    }
                    return result;
                }

                std::ifstream stream(output, std::ios::binary);
                if (!stream)
                {
                    EffectCompilerDiagnostic diagnostic;
                    diagnostic.file = request.source.string();
                    diagnostic.message =
                        "the effect compiler reported success and wrote no output file";
                    result.diagnostics.push_back(diagnostic);
                    return result;
                }
                result.bytecode.assign(std::istreambuf_iterator<char>(stream),
                                       std::istreambuf_iterator<char>());
                WrapAsXnaEffect(result.bytecode);
                result.succeeded = !result.bytecode.empty();
                if (!result.succeeded)
                {
                    EffectCompilerDiagnostic diagnostic;
                    diagnostic.file = request.source.string();
                    diagnostic.message =
                        "the effect compiler reported success and wrote an empty output file";
                    result.diagnostics.push_back(diagnostic);
                }
                return result;
            }

        private:

            /**
             * @brief The launcher's own spelling of each path.
             *
             * A compiler run through a launcher is a foreign program: `fxc.exe` under Wine reads
             * `/tmp/build/effect.fxb` as an option, because a leading `/` is how a Windows command
             * line begins one, and answers `Unknown or invalid option`. Wine can spell a host path
             * the way the program it runs will read it, so ask it -- once per compile, for every
             * path at once, which is what `winepath` accepts.
             *
             * Any other launcher gets the paths unchanged: a translation that has not been
             * measured is a guess, and a guess here turns a working build into a puzzling one.
             *
             * @param paths The host paths, in order.
             * @return The spellings, in the same order; the inputs unchanged when no translation
             *         applies or the launcher could not answer.
             */
            [[nodiscard]] std::vector<std::string> SpellForLauncher(
                const std::vector<std::filesystem::path>& paths) const
            {
                std::vector<std::string> spelled;
                spelled.reserve(paths.size());
                for (const std::filesystem::path& path : paths) { spelled.push_back(path.string()); }
                if (!launcherTranslatesPaths_ || paths.empty()) { return spelled; }

                std::vector<std::string> arguments{"winepath", "-w"};
                for (const std::string& path : spelled) { arguments.push_back(path); }
                const CNA::Internal::HostProcessResult process =
                    CNA::Internal::RunHostProcess(launcher_, arguments);
                if (!process.started || process.exitCode != 0) { return spelled; }

                std::vector<std::string> lines;
                std::istringstream stream(process.standardOutput);
                std::string line;
                while (std::getline(stream, line))
                {
                    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
                    {
                        line.pop_back();
                    }
                    if (!line.empty()) { lines.push_back(line); }
                }
                // One line per path, or the answer is not the one that was asked for.
                return lines.size() == spelled.size() ? lines : spelled;
            }

            void Resolve(const ExternalEffectCompilerOptions& options)
            {
                identity_.targetProfile = kTargetProfile;

                launcher_ = options.launcher;
                if (launcher_.empty())
                {
                    const std::string fromEnvironment = EnvironmentValue("CNA_FXC_LAUNCHER");
                    launcher_ = fromEnvironment.empty()
                                    ? std::filesystem::path(ConfiguredLauncher())
                                    : std::filesystem::path(fromEnvironment);
                }

                // Wine is the launcher this project documents, and the only one whose path
                // translation has been measured here.
                const std::string launcherName = launcher_.filename().string();
                launcherTranslatesPaths_ = launcherName.rfind("wine", 0) == 0;

                executable_ = options.executable;
                if (executable_.empty())
                {
                    const std::string fromEnvironment = EnvironmentValue("CNA_FXC");
                    executable_ = fromEnvironment.empty()
                                      ? std::filesystem::path(ConfiguredExecutable())
                                      : std::filesystem::path(fromEnvironment);
                }
                if (executable_.empty())
                {
                    // No configured path: try the ordinary name and let the launcher, or PATH,
                    // resolve it.
                    executable_ = launcher_.empty() ? "fxc" : "fxc.exe";
                }

                // A version probe doubles as an availability probe: it proves the executable can
                // be started, and it is what the fingerprint records.
                std::vector<std::string> arguments;
                if (!launcher_.empty()) { arguments.push_back(executable_.string()); }
                arguments.push_back("/?");
                const CNA::Internal::HostProcessResult process = CNA::Internal::RunHostProcess(
                    launcher_.empty() ? executable_ : launcher_, arguments);
                if (!process.started)
                {
                    reason_ = Explain("it could not be started (" + process.failure + ")");
                    return;
                }

                const std::string banner =
                    process.standardOutput.empty() ? process.standardError
                                                   : process.standardOutput;
                identity_.backend = "fxc";
                identity_.version = ExtractVersion(banner);
                if (identity_.version.empty())
                {
                    // A tool that answers but does not announce a version is still usable; record
                    // the trimmed first line so the fingerprint changes when the tool does.
                    const std::size_t newline = banner.find('\n');
                    identity_.version =
                        Trim(newline == std::string::npos ? banner : banner.substr(0u, newline));
                }
                if (identity_.version.empty()) { identity_.version = "unknown"; }
                available_ = true;
            }

            /** @brief Builds the complete "no compiler" diagnostic, once, in one place. */
            [[nodiscard]] std::string Explain(const std::string& what) const
            {
                std::ostringstream text;
                text << "no usable effect compiler: "
                     << (launcher_.empty() ? executable_.string()
                                           : launcher_.string() + " " + executable_.string())
                     << " -- " << what << ".\n"
                     << "Compiling .fx source to an XNA 4.0 Effect needs Microsoft's legacy "
                        "'fxc' at profile "
                     << kTargetProfile
                     << ", which is the compiler XNA's own Content Pipeline used. It is not "
                        "vendored here and no portable substitute exists: a standalone modern "
                        "d3dcompiler cannot write a legacy effect at all (it fails with 'E5017: "
                        "Aborting due to not yet implemented feature: Write pass assignments'), "
                        "because fxc delegates the "
                     << kTargetProfile
                     << " path to d3dx9.\n"
                     << "Point CNA at one of:\n"
                     << "  --fx-compiler <path>            explicit path, highest precedence\n"
                     << "  CNA_FXC=<path>                  environment variable\n"
                     << "  -DCNA_FXC_EXECUTABLE=<path>     baked in at configure time\n"
                     << "  fxc / fxc.exe on PATH           the fallback\n"
                     << "On a non-Windows build machine add a launcher, which runs the compiler "
                        "through another program:\n"
                     << "  --fx-compiler-launcher wine     (or CNA_FXC_LAUNCHER, or "
                        "-DCNA_FXC_LAUNCHER)\n"
                     << "The DirectX SDK (June 2010) fxc needs d3dx9_43.dll and "
                        "D3DCompiler_43.dll beside it; see "
                        "modules/renderers/fna3d/effects/README.md for the exact extraction "
                        "commands and the recorded compiler identity.\n"
                     << "Already have compiled effect bytes? Build the .fxb directly: that route "
                        "needs no compiler at all.";
                return text.str();
            }

            std::filesystem::path executable_;
            std::filesystem::path launcher_;
            /** @brief Whether the launcher can spell a host path for the program it runs. */
            bool launcherTranslatesPaths_ = false;
            EffectCompilerIdentity identity_;
            bool available_ = false;
            std::string reason_;
        };
    }

    const char* EffectSourceProfileName(const EffectSourceProfile profile) noexcept
    {
        return profile == EffectSourceProfile::HiDef ? "hidef" : "reach";
    }

    bool TryParseEffectSourceProfile(const std::string& name, EffectSourceProfile& profile)
    {
        if (name == "reach") { profile = EffectSourceProfile::Reach; return true; }
        if (name == "hidef") { profile = EffectSourceProfile::HiDef; return true; }
        return false;
    }

    std::string EffectCompilerDiagnostic::ToString() const
    {
        std::ostringstream text;
        if (!file.empty())
        {
            text << file;
            if (line > 0)
            {
                text << '(' << line;
                if (column > 0) { text << ',' << column; }
                text << ')';
            }
            text << ": ";
        }
        text << (isError ? "error" : "warning");
        if (!code.empty()) { text << ' ' << code; }
        text << ": " << message;
        return text.str();
    }

    std::string EffectCompilerIdentity::ToString() const
    {
        if (backend.empty()) { return "none"; }
        return backend + "-" + version + "-" + targetProfile;
    }

    std::vector<EffectCompilerDiagnostic> ParseEffectCompilerDiagnostics(const std::string& text)
    {
        std::vector<EffectCompilerDiagnostic> diagnostics;
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r') { line.pop_back(); }
            const std::string trimmed = Trim(line);
            if (trimmed.empty()) { continue; }

            EffectCompilerDiagnostic diagnostic;
            // `path(line,column): severity CODE: message`. The path may itself contain a colon
            // (a Windows drive letter) and parentheses are the only reliable anchor, so the scan
            // is for the *last* `(` before the first `):` rather than the first colon.
            const std::size_t close = trimmed.find("): ");
            const std::size_t open = close == std::string::npos
                                         ? std::string::npos
                                         : trimmed.rfind('(', close);
            std::string remainder = trimmed;
            if (open != std::string::npos && close != std::string::npos && open < close)
            {
                const std::string location = trimmed.substr(open + 1u, close - open - 1u);
                const std::size_t comma = location.find(',');
                const std::string lineText =
                    comma == std::string::npos ? location : location.substr(0u, comma);
                const std::string columnText =
                    comma == std::string::npos ? std::string{} : location.substr(comma + 1u);
                const auto isNumber = [](const std::string& candidate)
                {
                    return !candidate.empty() &&
                           std::all_of(candidate.begin(), candidate.end(), [](const char value)
                                       { return std::isdigit(static_cast<unsigned char>(value)) != 0; });
                };
                if (isNumber(lineText) && (columnText.empty() || isNumber(columnText)))
                {
                    diagnostic.file = trimmed.substr(0u, open);
                    diagnostic.line = std::stoi(lineText);
                    diagnostic.column = columnText.empty() ? 0 : std::stoi(columnText);
                    remainder = trimmed.substr(close + 3u);
                }
            }

            // `severity CODE: message`, where the code is optional.
            const std::size_t severityEnd = remainder.find(' ');
            const std::string severity =
                severityEnd == std::string::npos ? remainder : remainder.substr(0u, severityEnd);
            if (severity == "error" || severity == "warning" || severity == "fatal")
            {
                diagnostic.isError = severity != "warning";
                std::string tail = Trim(remainder.substr(severity.size()));
                const std::size_t codeEnd = tail.find(':');
                if (codeEnd != std::string::npos && tail.find(' ') > codeEnd)
                {
                    diagnostic.code = Trim(tail.substr(0u, codeEnd));
                    tail = Trim(tail.substr(codeEnd + 1u));
                }
                diagnostic.message = tail;
            }
            else
            {
                // Something the compiler said that does not fit the pattern. Kept rather than
                // dropped: a tool that talks about a missing DLL says it exactly here.
                diagnostic.message = trimmed;
                diagnostic.file.clear();
            }
            diagnostics.push_back(diagnostic);
        }
        return diagnostics;
    }

    std::shared_ptr<const EffectCompilerService> MakeExternalEffectCompiler(
        const ExternalEffectCompilerOptions& options)
    {
        return std::make_shared<const ExternalEffectCompiler>(options);
    }
}
