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
    inline constexpr std::uint32_t ContentBuildManifestVersion = 8u;

    /** @brief File name used for the inspectable manifest below a content output
     * root. */
    inline constexpr const char* ContentBuildManifestFileName = ".cna-content-manifest.json";

    /** @brief One deterministic output owned by a build node. */
    struct ContentBuildManifestOutput
    {
        /** @brief Logical ContentManager name and stable output identity. */
        std::string logicalName;

        /** @brief Published artifact path relative to the output root, using `/`. */
        std::string path;

        /** @brief CNB asset type written by the selected writer. */
        std::uint32_t assetTypeId = 0u;

        /** @brief CNB asset schema version declared by the selected writer. */
        std::uint32_t assetSchemaVersion = 0u;

        /** @brief Canonical runtime type name declared by the selected writer. */
        std::string assetTypeName;

        /** @brief SHA-256 of the published CNB bytes, used to detect deletion or tampering. */
        std::string sha256;

        /** @brief Compares every stable output field. */
        bool operator==(const ContentBuildManifestOutput&) const = default;
    };

    /** @brief One non-CNB deployment file owned by a build node. */
    struct ContentBuildManifestDeploymentFile
    {
        /** @brief External source-root alias, or empty for the primary source root. */
        std::string sourceRoot;

        /** @brief Source path relative to the source root, using `/`. */
        std::string source;

        /** @brief Published path relative to the output root, using `/`. */
        std::string path;

        /** @brief SHA-256 of the deployed bytes for skip checks and ownership-safe collection. */
        std::string sha256;

        /** @brief Compares every stable deployment-file field. */
        bool operator==(const ContentBuildManifestDeploymentFile&) const = default;
    };

    /** @brief Canonical persisted fingerprint domains used to explain incremental decisions. */
    struct ContentBuildFingerprintState
    {
        /** @brief SHA-256 of the primary source bytes. */
        std::string primarySourceBytes;

        /** @brief Canonical fingerprint of non-primary file-dependency identities. */
        std::string sourceDependencySet;

        /** @brief Canonical fingerprint of non-primary file-dependency identities and bytes. */
        std::string sourceDependencyBytes;

        /** @brief Canonical fingerprint of content-build dependency identities. */
        std::string contentDependencySet;

        /** @brief Canonical fingerprint of content-build identities and effective fingerprints. */
        std::string contentDependencyFingerprints;

        /** @brief Canonical fingerprint of typed processor parameters. */
        std::string processorParameters;

        /** @brief Canonical fingerprint of writer asset/schema/codec declarations. */
        std::string writerSchemas;

        /** @brief Canonical fingerprint of compiled output and runtime-XREF definitions. */
        std::string outputDefinitions;

        /** @brief Canonical fingerprint of deployment source/output definitions. */
        std::string deploymentDefinitions;

        /** @brief Compares every persisted fingerprint domain. */
        bool operator==(const ContentBuildFingerprintState&) const = default;
    };

    /** @brief One deterministic, root-relative build-node record in a manifest. */
    struct ContentBuildManifestEntry
    {
        /** @brief Stable logical build-node identity and manifest key. */
        std::string nodeId;

        /** @brief Primary source path relative to the source root, using `/`. */
        std::string source;

        /** @brief Importer identity used to produce the artifact. */
        ContentComponentIdentity importer;

        /** @brief Processor identity used to produce the artifact. */
        ContentComponentIdentity processor;

        /** @brief Writer identity used to produce the artifact. */
        ContentComponentIdentity writer;

        /** @brief Stable writer asset/schema/codec declarations used by the skip path. */
        std::vector<ContentWriterSchemaIdentity> writerSchemas;

        /** @brief Effective, typed processor configuration. */
        ContentProcessorParameters parameters;

        /** @brief Root-relative categorized build inputs, sorted deterministically.
         */
        std::vector<ContentDependency> dependencies;

        /** @brief Runtime XREFs recorded separately from build inputs. */
        std::vector<RuntimeContentReference> runtimeReferences;

        /** @brief Outputs owned by this node, including exactly one named @ref nodeId. */
        std::vector<ContentBuildManifestOutput> outputs;

        /** @brief Non-CNB support files atomically deployed and owned by this node. */
        std::vector<ContentBuildManifestDeploymentFile> deploymentFiles;

        /** @brief Stable decomposition of the inputs collapsed into the aggregate fingerprints. */
        ContentBuildFingerprintState fingerprintState;

        /** @brief SHA-256 of direct inputs and dependency-edge identities. */
        std::string directFingerprint;

        /** @brief SHA-256 of all effective build inputs and component identities. */
        std::string fingerprint;

        /** @brief Compares all persisted fields. */
        bool operator==(const ContentBuildManifestEntry&) const = default;
    };

    /** @brief Deterministic, versioned set of incremental content-build records. */
    class ContentBuildManifest
    {
    public:
        /**
         * @brief Parses a complete current-version manifest.
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
         * @brief Finds a record by stable build-node identity.
         *
         * @param nodeId Build-node identity to find.
         * @return Pointer valid until the manifest is mutated, or null when absent.
         */
        [[nodiscard]] const ContentBuildManifestEntry* Find(const std::string& nodeId) const;

        /**
         * @brief Inserts or replaces one record.
         *
         * @param entry Fully populated record whose node ID is the key.
         * @throws std::invalid_argument when a required field is invalid.
         */
        void Set(ContentBuildManifestEntry entry);

        /** @brief Removes all records. */
        void Clear() noexcept;

        /**
         * @brief Returns every record in stable logical-name order.
         *
         * @return Read-only map ordered by build-node identity.
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
     * @brief Streams and hashes one regular file with bounded memory.
     *
     * @param path Native path to hash.
     * @return Sixty-four lowercase hexadecimal characters.
     * @throws std::runtime_error if the file cannot be opened or read completely.
     */
    [[nodiscard]] std::string ContentFileSha256(const std::filesystem::path& path);

    /**
     * @brief Computes the direct-input and graph-topology fingerprint for one node.
     *
     * Primary/generated files are resolved below @p sourceRoot. Source-file dependencies may
     * instead select one alias in @p externalSourceRoots. Content-build dependency identities
     * participate, but their effective fingerprints do not. This allows a previous edge set to be
     * reused only while every input that could have changed dependency discovery is unchanged.
     *
     * @param entry Record containing current components, parameters and dependencies.
     * @param sourceRoot Root under which every file dependency must resolve.
     * @param externalSourceRoots Explicit alias-to-native-root mappings.
     * @return Lowercase SHA-256 of canonical length-prefixed fields.
     * @throws std::runtime_error for missing, unreadable or escaping file dependencies.
     */
    [[nodiscard]] std::string ComputeContentBuildDirectFingerprint(
        const ContentBuildManifestEntry& entry, const std::filesystem::path& sourceRoot,
        const ContentSourceRootCapabilities& externalSourceRoots = {});

    /**
     * @brief Refreshes the persisted direct-input domains and aggregate direct fingerprint.
     *
     * The effective content-build dependency domain remains unresolved until
     * @ref RefreshContentBuildEffectiveFingerprint is called.
     *
     * @param entry Record to update in place.
     * @param sourceRoot Root under which every file dependency must resolve.
     * @param externalSourceRoots Explicit alias-to-native-root mappings.
     * @throws std::runtime_error for missing, unreadable or escaping file dependencies.
     */
    void RefreshContentBuildDirectFingerprint(
        ContentBuildManifestEntry& entry, const std::filesystem::path& sourceRoot,
        const ContentSourceRootCapabilities& externalSourceRoots = {});

    /**
     * @brief Combines a node's current direct fingerprint with effective dependency results.
     *
     * @param entry Record whose @ref ContentBuildManifestEntry::directFingerprint is current.
     * @param contentBuildFingerprints Effective fingerprints keyed by dependency node ID.
     * @return Lowercase SHA-256 of the direct fingerprint and sorted content-build edges.
     * @throws std::runtime_error when the direct fingerprint or a dependency fingerprint is
     * invalid or absent.
     */
    [[nodiscard]] std::string ComputeContentBuildEffectiveFingerprint(
        const ContentBuildManifestEntry& entry,
        const std::map<std::string, std::string>& contentBuildFingerprints = {});

    /**
     * @brief Refreshes the effective content-dependency domain and aggregate fingerprint.
     *
     * @param entry Record whose direct state and fingerprint are current.
     * @param contentBuildFingerprints Effective fingerprints keyed by dependency node ID.
     * @throws std::runtime_error when a direct or dependency fingerprint is absent.
     */
    void RefreshContentBuildEffectiveFingerprint(
        ContentBuildManifestEntry& entry,
        const std::map<std::string, std::string>& contentBuildFingerprints = {});

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
     * @param externalSourceRoots Explicit alias-to-native-root mappings.
     * @return Lowercase SHA-256 of canonical length-prefixed fields.
     * @throws std::runtime_error for missing, unreadable or escaping dependencies.
     */
    [[nodiscard]] std::string ComputeContentBuildFingerprint(
        const ContentBuildManifestEntry& entry, const std::filesystem::path& sourceRoot,
        const std::map<std::string, std::string>& contentBuildFingerprints = {},
        const ContentSourceRootCapabilities& externalSourceRoots = {});

    /**
     * @brief Converts a successful in-memory result into a root-relative manifest
     * record.
     *
     * @param result Successful build result.
     * @param sourceRoot Canonical source root.
     * @param outputRoot Canonical output root.
     * @param outputPath Published artifact path.
     * @param externalSourceRoots Explicit alias-to-native-root mappings.
     * Additional outputs use their logical names below @p outputRoot with the extension of the
     * result's own output format. The
     * primary output keeps the caller-selected path, which matters for single-file builds.
     *
     * @return Record with normalized output identities/digests and empty direct/effective
     *         fingerprints for the caller to fill.
     */
    [[nodiscard]] ContentBuildManifestEntry MakeContentBuildManifestEntry(
        const ContentBuildResult& result, const std::filesystem::path& sourceRoot,
        const std::filesystem::path& outputRoot, const std::filesystem::path& outputPath,
        const ContentSourceRootCapabilities& externalSourceRoots = {});
} // namespace CNA::Content::Pipeline
