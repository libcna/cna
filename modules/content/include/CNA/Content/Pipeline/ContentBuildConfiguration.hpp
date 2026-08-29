// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

#include "CNA/Content/Pipeline/ContentPipeline.hpp"

namespace CNA::Content::Pipeline
{
    /** @brief Current CNA Content Pipeline configuration format version. */
    inline constexpr std::uint32_t ContentBuildConfigurationVersion = 1u;

    /** @brief Conventional optional configuration file name below a source root. */
    inline constexpr const char* ContentBuildConfigurationFileName = ".cna-content.json";

    /** @brief Effective overrides for one root-relative source asset. */
    struct ContentAssetBuildConfiguration
    {
        /** @brief Root-relative source path using `/`, and the configuration entry key. */
        std::string source;

        /** @brief Optional logical ContentManager name override. */
        std::string logicalName;

        /** @brief Optional stable importer-name override. */
        std::string importer;

        /** @brief Optional stable processor-name override. */
        std::string processor;

        /** @brief Optional stable writer-name override. */
        std::string writer;

        /** @brief Explicitly typed processor parameters. */
        ContentProcessorParameters parameters;

        /** @brief Compares the source identity and every effective override. */
        bool operator==(const ContentAssetBuildConfiguration&) const = default;
    };

    /** @brief Strict, deterministic optional per-asset build configuration. */
    class ContentBuildConfiguration
    {
    public:
        /**
         * @brief Parses and validates a complete version-1 configuration document.
         *
         * @param json UTF-8 JSON document.
         * @param sourceName Native configuration path used in diagnostics.
         * @return Configuration entries ordered by root-relative source path.
         * @throws std::runtime_error for malformed JSON, unknown fields, duplicate entries,
         *         unsafe paths, or invalid typed parameter values.
         */
        [[nodiscard]] static ContentBuildConfiguration Parse(
            const std::string& json, const std::filesystem::path& sourceName = {});

        /**
         * @brief Finds overrides for one root-relative source path.
         *
         * @param source Root-relative generic UTF-8 source path.
         * @return Pointer valid for this configuration's lifetime, or null when unconfigured.
         */
        [[nodiscard]] const ContentAssetBuildConfiguration* Find(
            const std::string& source) const;

        /**
         * @brief Returns every configured asset in deterministic source-path order.
         *
         * @return Read-only ordered entry map.
         */
        [[nodiscard]] const std::map<std::string, ContentAssetBuildConfiguration>& Entries()
            const noexcept;

    private:
        std::map<std::string, ContentAssetBuildConfiguration> entries_;
    };
} // namespace CNA::Content::Pipeline
