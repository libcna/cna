// SPDX-License-Identifier: MS-PL
#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace CNA::Content::Pipeline
{
    /**
     * @brief Which XNA graphics profile an effect is being compiled for
     *        (plans/plan_xnapipeline.md `XNAP-A2`).
     *
     * XNA 4.0 draws the line at Shader Model 2.0: a Reach game may use `vs_2_0`/`ps_2_0` and a
     * HiDef game may also use `vs_3_0`/`ps_3_0`. CNA does **not** rewrite the `compile` statements
     * in an `.fx` -- the shader target is whatever the source names -- so this selection does two
     * things and no more: it defines a preprocessor symbol the source can branch on, and it enters
     * the build fingerprint. It is not a claim to reproduce XNA's own profile validation, and the
     * documentation says so.
     */
    enum class EffectSourceProfile
    {
        /** @brief Reach -- Shader Model 2.0. Defines `CNA_REACH`. */
        Reach,
        /** @brief HiDef -- Shader Model 3.0 available. Defines `CNA_HIDEF`. */
        HiDef,
    };

    /** @brief Returns the stable lowercase spelling of a profile (`"reach"`, `"hidef"`). */
    [[nodiscard]] const char* EffectSourceProfileName(EffectSourceProfile profile) noexcept;

    /** @brief Parses `"reach"`/`"hidef"`; returns false for anything else. */
    [[nodiscard]] bool TryParseEffectSourceProfile(const std::string& name,
                                                   EffectSourceProfile& profile);

    /**
     * @brief One diagnostic an effect compiler reported, located in a source file.
     *
     * Held structured rather than as a line of text so the pipeline can re-emit it with the
     * authored source path a build actually mentions, instead of the temporary path the compiler
     * was handed.
     */
    struct EffectCompilerDiagnostic
    {
        /** @brief File the compiler blamed, exactly as it spelled it. */
        std::string file;

        /** @brief One-based line number, or zero when the compiler gave none. */
        int line = 0;

        /** @brief One-based column, or zero when the compiler gave none. */
        int column = 0;

        /** @brief Compiler error code, e.g. `X3000`, or empty. */
        std::string code;

        /** @brief The message text. */
        std::string message;

        /** @brief Whether this is an error rather than a warning. */
        bool isError = true;

        /** @brief Renders the diagnostic in the `file(line,column): code: message` form. */
        [[nodiscard]] std::string ToString() const;

        /** @brief Compares every field. */
        bool operator==(const EffectCompilerDiagnostic& other) const = default;
    };

    /**
     * @brief Parses one compiler output stream into structured diagnostics.
     *
     * Recognizes the form every Microsoft shader compiler emits --
     * `path(line,column): error X3000: message`, with `warning` in place of `error`, and with the
     * column optional -- and keeps any unrecognized line as a message-only diagnostic rather than
     * discarding it, because a compiler that says something unexpected is exactly when a build
     * needs to hear it.
     *
     * @param text The compiler's standard error (or output) stream.
     * @return One diagnostic per reported line, in order.
     */
    [[nodiscard]] std::vector<EffectCompilerDiagnostic> ParseEffectCompilerDiagnostics(
        const std::string& text);

    /** @brief Everything a compile depends on besides the source text itself. */
    struct EffectCompileRequest
    {
        /** @brief Absolute path of the root `.fx` file. */
        std::filesystem::path source;

        /** @brief Directories searched for `#include`, in order, after the source's own. */
        std::vector<std::filesystem::path> includeDirectories;

        /** @brief Preprocessor definitions, applied in key order so the command is deterministic. */
        std::map<std::string, std::string> defines;

        /** @brief Graphics profile selection. */
        EffectSourceProfile profile = EffectSourceProfile::Reach;

        /** @brief Whether to ask the compiler to embed debug information. */
        bool debugInformation = false;
    };

    /** @brief What a compile produced. */
    struct EffectCompileResult
    {
        /** @brief The compiled effect binary, empty when the compile failed. */
        std::vector<std::uint8_t> bytecode;

        /** @brief Every diagnostic the compiler reported, errors and warnings alike. */
        std::vector<EffectCompilerDiagnostic> diagnostics;

        /** @brief Whether the compile succeeded and @ref bytecode is usable. */
        bool succeeded = false;
    };

    /**
     * @brief Stable identity of one compiler backend, for the build fingerprint.
     *
     * Two different compilers, or two versions of one compiler, must not be treated as
     * interchangeable by an incremental build: the same source can legitimately compile to
     * different bytes. The processor folds this into its own component version, which the manifest
     * already fingerprints, so changing compilers rebuilds rather than reusing stale artifacts.
     */
    struct EffectCompilerIdentity
    {
        /** @brief Backend name, e.g. `"fxc"`. Empty when no compiler is available. */
        std::string backend;

        /** @brief Version the compiler reported about itself, or empty when it did not. */
        std::string version;

        /** @brief The shader/effect profile this backend targets, e.g. `"fx_2_0"`. */
        std::string targetProfile;

        /** @brief A single stable string combining the above, suitable for a fingerprint. */
        [[nodiscard]] std::string ToString() const;

        /** @brief Compares every field. */
        bool operator==(const EffectCompilerIdentity& other) const = default;
    };

    /**
     * @brief A build-time effect compiler.
     *
     * Deliberately an interface with a process boundary behind it rather than a linked library:
     * the only compiler known to produce the Effect Framework 9.1 binary an XNA 4.0 runtime's
     * `Effect` constructor expects is Microsoft's own legacy `fxc` at profile `fx_2_0`, which
     * cannot be vendored here and does not exist as a library. Keeping the seam at this interface
     * also means the pipeline's own paths -- routing, include tracking, fingerprinting,
     * diagnostics -- are testable without any compiler at all.
     */
    class EffectCompilerService
    {
    public:
        /** @brief Enables destruction through the interface. */
        virtual ~EffectCompilerService() = default;

        /** @brief Returns the fingerprint identity of this backend. */
        [[nodiscard]] virtual EffectCompilerIdentity Identity() const = 0;

        /**
         * @brief Whether this backend can compile right now.
         *
         * False means the executable was not found or did not answer a version probe. A build then
         * fails with @ref UnavailableReason rather than with a compiler error.
         */
        [[nodiscard]] virtual bool Available() const = 0;

        /** @brief A complete explanation of why @ref Available is false, or an empty string. */
        [[nodiscard]] virtual std::string UnavailableReason() const = 0;

        /**
         * @brief Compiles one effect source.
         *
         * @param request What to compile and how.
         * @return The compiled bytes, or a failed result carrying the compiler's diagnostics.
         */
        [[nodiscard]] virtual EffectCompileResult Compile(
            const EffectCompileRequest& request) const = 0;
    };

    /** @brief How to find and drive an external effect compiler. */
    struct ExternalEffectCompilerOptions
    {
        /**
         * @brief Explicit path to the compiler, or empty to discover one.
         *
         * Discovery order, first hit wins: this field; the `CNA_FXC` environment variable; the
         * path baked in by CMake's `CNA_FXC_EXECUTABLE`; then `fxc` on `PATH`.
         */
        std::filesystem::path executable;

        /**
         * @brief A program to run the compiler *through*, or empty to run it directly.
         *
         * `fxc.exe` is a Windows binary, and the ordinary way to run one on a Linux build machine
         * is `wine`. Discovery order matches @ref executable: this field, then `CNA_FXC_LAUNCHER`,
         * then CMake's `CNA_FXC_LAUNCHER`.
         */
        std::filesystem::path launcher;

        /** @brief Compares both fields. */
        bool operator==(const ExternalEffectCompilerOptions& other) const = default;
    };

    /**
     * @brief Creates the external-process effect compiler backend.
     *
     * Probing happens once, when this is called: the executable is resolved and asked for its
     * version. The returned service is then immutable and reports @ref
     * EffectCompilerService::Available accordingly, so a build with no compiler fails at the first
     * `.fx` with one complete explanation rather than once per asset.
     *
     * @param options Discovery overrides; every field may be empty.
     * @return A service that is never null, and may be unavailable.
     */
    [[nodiscard]] std::shared_ptr<const EffectCompilerService> MakeExternalEffectCompiler(
        const ExternalEffectCompilerOptions& options = {});
}
