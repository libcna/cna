// SPDX-License-Identifier: MS-PL
#pragma once

// plans/plan_gltf.md GLTF-003/GLTF-004: loading side of the generated conformance corpus.
//
// TEST SCOPE ONLY -- see GltfOracleEXT.hpp for the scope rules.
//
// The corpus under tests/assets/gltf is generated and committed by tools/gltf_fixtures. This
// header opens an asset and its expectation manifest together, so a test can never accidentally
// compare one fixture's geometry against another's expectation.

#include <filesystem>
#include <string>
#include <vector>

#include "cgltf.h"

#include "CNA/Internal/Json.hpp"

namespace CnaTest::GltfOracle
{
    /**
     * @brief Locates the committed fixture corpus directory.
     *
     * Resolves `tests/assets/gltf` against the working directory and a few parents, so the suite
     * works whether it is run from the repository root (the CTest convention) or from a build tree.
     *
     * @return The corpus directory, or an empty path when it cannot be found.
     */
    [[nodiscard]] std::filesystem::path CorpusDirectory();

    /** @brief The parsed `manifest.json`, loaded once per process. Null-typed if unavailable. */
    [[nodiscard]] const CNA::Internal::JsonValue& CorpusManifest();

    /** @brief Every fixture id in the corpus, in manifest order. */
    [[nodiscard]] std::vector<std::string> CorpusFixtureIds();

    /**
     * @brief One fixture opened for testing: its parsed glTF and its expectation manifest.
     *
     * Owns the `cgltf_data` and frees it on destruction. Loading never throws; check @ref Ok
     * before using @ref Data.
     */
    class LoadedFixture
    {
    public:
        /**
         * @brief Parses `<id>.gltf` and `<id>.expected.json` from the corpus directory.
         *
         * @param id The fixture's canonical corpus id (e.g. "xf-shared-mesh").
         */
        explicit LoadedFixture(const std::string& id);

        /** @brief Frees the parsed glTF data. */
        ~LoadedFixture();

        LoadedFixture(const LoadedFixture&) = delete;
        LoadedFixture& operator=(const LoadedFixture&) = delete;

        /** @brief True when both the asset and its manifest loaded successfully. */
        [[nodiscard]] bool Ok() const { return ok_; }

        /** @brief Why loading failed; empty when @ref Ok is true. */
        [[nodiscard]] const std::string& Error() const { return error_; }

        /** @brief The fixture's canonical corpus id. */
        [[nodiscard]] const std::string& Id() const { return id_; }

        /** @brief The parsed glTF file, with buffers loaded. Only valid when @ref Ok is true. */
        [[nodiscard]] const cgltf_data& Data() const { return *data_; }

        /** @brief The parsed `<id>.expected.json` document. */
        [[nodiscard]] const CNA::Internal::JsonValue& Expected() const { return expected_; }

        /** @brief The path of the `.gltf` asset this fixture was loaded from. */
        [[nodiscard]] const std::filesystem::path& AssetPath() const { return assetPath_; }

    private:
        std::string id_;
        std::filesystem::path assetPath_;
        cgltf_data* data_ = nullptr;
        CNA::Internal::JsonValue expected_;
        bool ok_ = false;
        std::string error_;
    };

    // --- Manifest reading helpers -----------------------------------------------------------
    // Deliberately total: a missing member yields a null value rather than throwing, so a test
    // reports "expected <field> missing from the manifest" instead of dying mid-suite.

    /** @brief The null value returned for every absent lookup. */
    [[nodiscard]] const CNA::Internal::JsonValue& JsonNull();

    /** @brief Looks up an object member, returning @ref JsonNull when absent. */
    [[nodiscard]] const CNA::Internal::JsonValue& Member(const CNA::Internal::JsonValue& value,
                                                          const std::string& key);

    /** @brief Looks up a nested member by a dotted path (e.g. "l4.worldBounds.min"). */
    [[nodiscard]] const CNA::Internal::JsonValue& Path(const CNA::Internal::JsonValue& value,
                                                        const std::string& dottedPath);

    /** @brief Reads a JSON array of numbers, returning an empty vector for anything else. */
    [[nodiscard]] std::vector<double> Numbers(const CNA::Internal::JsonValue& value);

    /** @brief Reads a JSON array of strings, returning an empty vector for anything else. */
    [[nodiscard]] std::vector<std::string> Strings(const CNA::Internal::JsonValue& value);

    /** @brief Reads a number member, or @p fallback when absent or not a number. */
    [[nodiscard]] double NumberOr(const CNA::Internal::JsonValue& value, const std::string& key,
                                   double fallback);

    /** @brief Reads a string member, or @p fallback when absent or not a string. */
    [[nodiscard]] std::string StringOr(const CNA::Internal::JsonValue& value, const std::string& key,
                                        const std::string& fallback);

    /** @brief Reads a boolean member, or @p fallback when absent or not a boolean. */
    [[nodiscard]] bool BoolOr(const CNA::Internal::JsonValue& value, const std::string& key,
                               bool fallback);

    /**
     * @brief True when a fixture's expectation is that the importer refuses the file outright.
     *
     * Such a fixture has no L3 semantic mesh and no L4 world geometry, because a conforming reader
     * never gets that far -- the rejection itself is the expectation, and `GltfContainerValidation`
     * owns asserting it, including that the diagnostic names the cause. Skipping it in a
     * later-layer suite is not a suppression: nothing about it is left unchecked.
     *
     * @param expected The fixture's parsed expectation manifest.
     * @return True when the manifest carries a `rejection` block.
     */
    [[nodiscard]] bool IsRejectionFixture(const CNA::Internal::JsonValue& expected);

    /**
     * @brief True when this build intentionally lacks the decoder a Draco fixture requires.
     *
     * Structural L1/L2 tests still inspect these fixtures in a decoder-free build. Layers that
     * require semantic extraction skip only these records, while GltfContainerValidation asserts
     * their required-extension refusal explicitly. This keeps `CNA_ENABLE_DRACO=OFF` a complete,
     * green corpus configuration rather than deleting Draco assets from that configuration.
     */
    [[nodiscard]] bool RequiresUnavailableDraco(const CNA::Internal::JsonValue& expected);

    /**
     * @brief True when a defect record is still open (`known-failing` or `partially-remediated`).
     *
     * A `fixed` record stays in the corpus forever as the regression witness for the task that
     * closed it, but it no longer suppresses anything.
     *
     * @param defect One entry of a fixture's `defects[]` array.
     * @return True when the defect still breaks something.
     */
    [[nodiscard]] bool IsOpenDefect(const CNA::Internal::JsonValue& defect);

    /**
     * @brief True when @p fixture records a still-open defect that breaks @p field at @p layer.
     *
     * This is what keeps a known defect from suppressing more than it actually breaks: a defect
     * lists the exact fields it corrupts, per layer, so every other field of the same layer stays
     * under full conformance assertion.
     *
     * @param expected The fixture's parsed expectation manifest.
     * @param layer The oracle layer, e.g. "L3".
     * @param field The field name within that layer, e.g. "indices".
     * @return True when an open defect covers that field at that layer.
     */
    [[nodiscard]] bool IsKnownDefectField(const CNA::Internal::JsonValue& expected,
                                           const std::string& layer, const std::string& field);
}
