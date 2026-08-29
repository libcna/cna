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
     * diagnostics, and atomic publication. A user-built compiler should finish all registration
     * before calling this function and must not mutate another alias to @p registry while it runs.
     * This C++ embedding surface has the same experimental source/ABI status as the component API.
     *
     * @param arguments Native command-line arguments excluding the executable name.
     * @param registry Non-null, fully configured registry retained for the complete invocation.
     * @return Zero on success, one when discovery or a build fails, or two for invalid syntax.
     * @throws std::invalid_argument if @p registry is null.
     */
    [[nodiscard]] int RunContentCompiler(
        const std::vector<std::filesystem::path>& arguments,
        std::shared_ptr<const ContentPipelineRegistry> registry);
} // namespace CNA::Content::Pipeline
