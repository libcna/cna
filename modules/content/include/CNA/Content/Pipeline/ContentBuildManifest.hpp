// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "CNA/Content/Pipeline/ContentPipeline.hpp"

namespace CNA::Content::Pipeline
{
    /** @brief Current on-disk CNA Content Pipeline manifest format version. */
    inline constexpr std::uint32_t ContentBuildManifestVersion = 1u;

    /** @brief File name used for the inspectable manifest below a content output
     * root. */
    inline constexpr const char* ContentBuildManifestFileName = ".cna-content-manifest.json";

    /** @brief One deterministic, root-relative asset record in a build manifest. */
    struct ContentBuildManifestEntry
    {
        /** @brief Logical ContentManager asset name and manifest key. */
        std::string logicalName;

        /** @brief Primary source path relative to the source root, using `/`. */
        std::string source;

        /** @brief Published artifact path relative to the output root, using `/`. */
        std::string output;

        /** @brief Importer identity used to produce the artifact. */
        ContentComponentIdentity importer;

        /** @brief Processor identity used to produce the artifact. */
        ContentComponentIdentity processor;

        /** @brief Writer identity used to produce the artifact. */
        ContentComponentIdentity writer;

        /** @brief Effective, typed processor configuration. */
        ContentProcessorParameters parameters;

        /** @brief Root-relative categorized build inputs, sorted deterministically.
         */
        std::vector<ContentDependency> dependencies;

        /** @brief Runtime XREFs recorded separately from build inputs. */
        std::vector<RuntimeContentReference> runtimeReferences;

        /** @brief CNB asset type written by the selected writer. */
        std::uint32_t assetTypeId = 0u;

        /** @brief SHA-256 of all effective build inputs and component identities. */
        std::string fingerprint;

        /** @brief SHA-256 of the published CNB bytes, used to detect deletion or
         * tampering. */
        std::string outputSha256;

        /** @brief Compares all persisted fields. */
        bool operator==(const ContentBuildManifestEntry&) const = default;
    };

    /** @brief Deterministic, versioned set of incremental content-build records. */
    class ContentBuildManifest
    {
    public:
        /**
         * @brief Parses a complete version-1 manifest.
         *
         * @param json UTF-8 JSON document.
         * @return Parsed manifest with entries ordered by logical name.
         * @throws std::runtime_error when the document is malformed or incompatible.
         */
        [[nodiscard]] static ContentBuildManifest Parse(const std::string& json);

        /**
         * @brief Serializes this manifest deterministically.
         *
         * @return UTF-8 JSON ending with one newline.
         */
        [[nodiscard]] std::string Serialize() const;

        /**
         * @brief Finds a record by logical asset name.
         *
         * @param logicalName Logical asset name to find.
         * @return Pointer valid until the manifest is mutated, or null when absent.
         */
        [[nodiscard]] const ContentBuildManifestEntry* Find(const std::string& logicalName) const;

        /**
         * @brief Inserts or replaces one record.
         *
         * @param entry Fully populated record whose logical name is the key.
         * @throws std::invalid_argument when a required field is invalid.
         */
        void Set(ContentBuildManifestEntry entry);

        /** @brief Removes all records. */
        void Clear() noexcept;

        /**
         * @brief Returns every record in stable logical-name order.
         *
         * @return Read-only ordered record map.
         */
        [[nodiscard]] const std::map<std::string, ContentBuildManifestEntry>&
        Entries() const noexcept;

    private:
        std::map<std::string, ContentBuildManifestEntry> entries_;
    };

    /**
     * @brief Computes lowercase SHA-256 for an in-memory byte sequence.
     *
     * @param bytes Bytes to hash.
     * @return Sixty-four lowercase hexadecimal characters.
     */
    [[nodiscard]] std::string ContentSha256(const std::vector<std::uint8_t>& bytes);

    /**
     * @brief Reads and hashes one regular file.
     *
     * @param path Native path to hash.
     * @return Sixty-four lowercase hexadecimal characters.
     * @throws std::runtime_error if the file cannot be read completely.
     */
    [[nodiscard]] std::string ContentFileSha256(const std::filesystem::path& path);

    /**
     * @brief Computes the canonical effective-input fingerprint for one manifest
     * record.
     *
     * File dependencies are resolved beneath @p sourceRoot and hashed by bytes.
     * Content-build dependencies use the supplied effective fingerprints; absence
     * is an error rather than an unsafe skip. Runtime XREFs and native output paths
     * are outputs, not build inputs.
     *
     * @param entry Record containing current components, parameters and
     * dependencies.
     * @param sourceRoot Root under which every file dependency must resolve.
     * @param contentBuildFingerprints Effective fingerprints keyed by logical asset
     * name.
     * @return Lowercase SHA-256 of canonical length-prefixed fields.
     * @throws std::runtime_error for missing, unreadable or escaping dependencies.
     */
    [[nodiscard]] std::string ComputeContentBuildFingerprint(
        const ContentBuildManifestEntry& entry, const std::filesystem::path& sourceRoot,
        const std::map<std::string, std::string>& contentBuildFingerprints = {});

    /**
     * @brief Converts a successful in-memory result into a root-relative manifest
     * record.
     *
     * @param result Successful build result.
     * @param sourceRoot Canonical source root.
     * @param outputRoot Canonical output root.
     * @param outputPath Published artifact path.
     * @return Record with empty fingerprint and output digest fields for the caller
     * to fill.
     */
    [[nodiscard]] ContentBuildManifestEntry MakeContentBuildManifestEntry(
        const ContentBuildResult& result, const std::filesystem::path& sourceRoot,
        const std::filesystem::path& outputRoot, const std::filesystem::path& outputPath);
} // namespace CNA::Content::Pipeline
