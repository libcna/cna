// SPDX-License-Identifier: MS-PL
#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include "CNA/Content/Pipeline/ContentPipeline.hpp"

namespace CNA::Content::Pipeline
{
    /**
     * @brief Registers every built-in source pipeline shipped by CNA.
     *
     * User-owned content compilers can call this before adding their own importers, processors,
     * and writers. Registration remains explicit; this function performs no dynamic loading or
     * process-global registration.
     *
     * @param registry Mutable registry to configure before compilation begins.
     */
    void RegisterBuiltInContentPipeline(ContentPipelineRegistry& registry);

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
     * @param arguments Native command-line arguments excluding the executable name.
     * @param registry Non-null, fully configured registry retained for the complete invocation.
     * @return Zero on success, one when discovery or a build fails, or two for invalid syntax.
     * @throws std::invalid_argument if @p registry is null.
     */
    [[nodiscard]] int RunContentCompiler(
        const std::vector<std::filesystem::path>& arguments,
        std::shared_ptr<ContentPipelineRegistry> registry);
} // namespace CNA::Content::Pipeline
