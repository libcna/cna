// SPDX-License-Identifier: MS-PL
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include "CNA/Content/Pipeline/ContentPipeline.hpp"
#include "CNA/Internal/Xnb/XnbFileOptions.hpp"

namespace CNA::Content::Pipeline
{
    /**
     * @brief Build-tool services that must be configured before the built-in routes are
     *        registered (plans/plan_xnapipeline.md `XNAP-A5`).
     *
     * A source route whose behaviour depends on an external program cannot be registered before
     * that program has been chosen, because the backend's identity enters the incremental build
     * fingerprint. This structure carries those choices from the command line -- or from an
     * embedding application -- to registration, which is what keeps the selection out of process
     * globals and out of the environment.
     *
     * The paths are spelled out here rather than reusing
     * CNA::Content::Pipeline::ExternalEffectCompilerOptions so that this header, which belongs to
     * the runtime-facing `cna_content` module, does not have to include a build-time-only one.
     */
    struct ContentCompilerOptions
    {
        /**
         * @brief Explicit effect-compiler executable, or empty to discover one.
         *
         * Discovery order, first hit wins: this field; the `CNA_FXC` environment variable; the
         * path baked in by CMake's `CNA_FXC_EXECUTABLE`; then `fxc` on `PATH`.
         */
        std::filesystem::path effectCompilerExecutable;

        /**
         * @brief Program to run the effect compiler *through* (`wine`), or empty to run it
         *        directly.
         *
         * Discovery order matches @ref effectCompilerExecutable: this field, then
         * `CNA_FXC_LAUNCHER`, then CMake's `CNA_FXC_LAUNCHER`.
         */
        std::filesystem::path effectCompilerLauncher;

        /**
         * @brief The container options this invocation selected.
         *
         * Informational, and the reason it exists is a third party's writers. A caller's registry
         * factory can register writers of its own -- `RegisterXnaXnbOutput` does exactly that for
         * an XNA-shaped route -- and those writers have to be bound to the same platform, version,
         * profile and compression the built-in ones are, or a `--xnb-platform windowsphone` build
         * silently produces a project where the game's own types went to a Windows container and
         * everything else did not. The coordinator fills this in before calling the factory
         * (plans/plan_xnapipeline_parity.md `XNAPP-260`).
         */
        CNA::Internal::Xnb::XnbFileOptions xnbContainer{};

        /**
         * @brief Compares the two build-time *services* a factory has to construct.
         *
         * Deliberately not the container options: those are something the coordinator applies to
         * its own writers whatever the factory did, so a registry that was built before this
         * command line existed is still usable with them. What such a registry cannot apply is an
         * effect compiler, and that is what the inequality is asked about.
         */
        bool operator==(const ContentCompilerOptions& other) const
        {
            return effectCompilerExecutable == other.effectCompilerExecutable &&
                   effectCompilerLauncher == other.effectCompilerLauncher;
        }
    };

    /**
     * @brief Registers every built-in source pipeline shipped by CNA.
     *
     * User-owned content compilers can call this before adding their own importers, processors,
     * and writers. Registration remains explicit; this function performs no dynamic loading or
     * process-global registration.
     *
     * Every source route is registered here, including the `.fx` route, which is registered even
     * when no effect compiler can be found so that a build tree containing one reports *why* it
     * cannot be compiled rather than reporting that nothing imports the extension. The XNB output
     * *writers* are the one exception: they are bound to container options the command line
     * selects, so RunContentCompiler() adds them once it has parsed them.
     *
     * @param registry Mutable registry to configure before compilation begins.
     * @param options Build-tool service selection; every field may be empty.
     */
    void RegisterBuiltInContentPipeline(ContentPipelineRegistry& registry,
                                        const ContentCompilerOptions& options = {});

    /**
     * @brief Builds the registry for one invocation from the build-tool options it parsed.
     *
     * Called exactly once per RunContentCompiler() call, after the command line has been parsed
     * and before any source is discovered. Returning a registry per call, rather than sharing
     * one, is what makes two invocations in a single process independent.
     */
    using ContentPipelineRegistryFactory =
        std::function<std::shared_ptr<ContentPipelineRegistry>(const ContentCompilerOptions&)>;

    /**
     * @brief Runs the standard `cna-content` command-line coordinator with a configured registry.
     *
     * The coordinator owns source discovery, configuration, fingerprints, incremental manifests,
     * diagnostics, atomic publication, and manifest-proven cleanup. A user-built compiler should
     * finish all registration before calling this function.
     *
     * The registry is taken mutably, and briefly stays mutable, for exactly one reason: when the
     * command line selects `--format xnb` the coordinator registers the XNB output writers bound
     * to the container options that same command line chose. Those options change the emitted
     * bytes, so they belong in each writer's own build version and therefore in the incremental
     * manifest. The registry is permanently frozen immediately afterwards, before source discovery
     * begins, and later registration through any retained alias fails. This C++ embedding surface
     * has the same experimental source/ABI status as the component API.
     *
     * A registry supplied this way was configured before the command line was read, so the
     * build-tool options that select an external service -- `--fx-compiler` and
     * `--fx-compiler-launcher` -- cannot reach it. Passing one of those together with a pre-built
     * registry is refused with a message naming the factory overload, rather than accepted and
     * silently ignored.
     *
     * @param arguments Native command-line arguments excluding the executable name.
     * @param registry Non-null, fully configured registry retained for the complete invocation.
     * @return Zero on success, one when discovery or a build fails, or two for invalid syntax.
     * @throws std::invalid_argument if @p registry is null.
     */
    [[nodiscard]] int RunContentCompiler(
        const std::vector<std::filesystem::path>& arguments,
        std::shared_ptr<ContentPipelineRegistry> registry);

    /**
     * @brief Runs the coordinator, building the registry from the options the command line chose.
     *
     * This is the overload `cna-content` itself uses, and the only one that can honour
     * `--fx-compiler` and `--fx-compiler-launcher`: the factory runs after parsing, so a route
     * whose backend the command line selects is registered with that selection rather than with a
     * default discovered earlier. Nothing is cached between calls, so two invocations in one
     * process with different compiler paths are independent.
     *
     * @param arguments Native command-line arguments excluding the executable name.
     * @param createRegistry Called once after parsing; must return a non-null mutable registry.
     * @return Zero on success, one when discovery or a build fails, or two for invalid syntax.
     * @throws std::invalid_argument if @p createRegistry is empty or returns null.
     */
    [[nodiscard]] int RunContentCompiler(
        const std::vector<std::filesystem::path>& arguments,
        const ContentPipelineRegistryFactory& createRegistry);
} // namespace CNA::Content::Pipeline
