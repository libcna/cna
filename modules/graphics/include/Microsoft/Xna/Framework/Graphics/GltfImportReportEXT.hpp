// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Severity of one glTF import diagnostic. */
    CNAEXT enum class GltfImportDiagnosticSeverityEXT
    {
        /** @brief Informational conversion or generated data; no authored result was lost. */
        Information,
        /** @brief The imported result differs from, or omits, authored data. */
        Warning
    };

    /** @brief The kind of change one glTF import diagnostic describes. */
    CNAEXT enum class GltfImportDiagnosticKindEXT
    {
        /** @brief An exact conversion or other useful import note. */
        Information,
        /** @brief CNA generated data that the source did not provide. */
        GeneratedData,
        /** @brief The source data is suspicious, but CNA could still import its actual values. */
        InvalidSourceData,
        /** @brief Authored data was represented approximately. */
        Approximation,
        /** @brief Authored data could not be carried and was discarded. */
        DroppedData,
        /** @brief CNA does not implement the named optional feature. */
        UnsupportedFeature
    };

    /**
     * @brief One programmatically reachable outcome of importing a glTF asset.
     *
     * @note CNAEXT — not part of the XNA 4.0 API (plans/plan_gltf.md `GLTF-034`/`GLTF-035`).
     * @ref Code is the stable machine-readable identity. @ref Message is deliberately for people
     * and may gain detail or wording fixes without becoming an API break. A diagnostic is normally
     * aggregated for one @ref Subject; @ref Count says how many occurrences it represents and
     * @ref Details names the individual maps, attributes or extensions when a count alone would
     * not be actionable.
     */
    CNAEXT struct GltfImportDiagnosticEXT
    {
        /** @brief Stable lower-case, hyphen-separated diagnostic identifier. */
        std::string Code;
        /** @brief Whether this is a note or an observable fidelity warning. */
        GltfImportDiagnosticSeverityEXT Severity =
            GltfImportDiagnosticSeverityEXT::Information;
        /** @brief Whether the outcome generated, approximated, dropped or did not support data. */
        GltfImportDiagnosticKindEXT Kind = GltfImportDiagnosticKindEXT::Information;
        /** @brief Primitive, node, clip or extension this entry concerns; may be empty. */
        std::string Subject;
        /** @brief Number of occurrences represented by this entry. */
        std::size_t Count = 0;
        /**
         * @brief Largest measured magnitude associated with the entry, or 0 when none applies.
         *
         * Examples are a dropped skin influence, a pre-normalisation weight-sum deviation and a
         * light channel before clamping. Its unit is defined by @ref Code.
         */
        double WorstMagnitude = 0.0;
        /** @brief Individual affected names, such as texture maps or custom attributes. */
        std::vector<std::string> Details;
        /** @brief Human-readable explanation suitable for a log or diagnostics overlay. */
        std::string Message;
    };

    /**
     * @brief Structured summary and diagnostics produced while importing one glTF model.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. Available from both a direct `.gltf`/`.glb`
     * load and a model loaded from `gltf_to_cnj` output. Models from other content paths, and old
     * `.cnj` documents that predate this object, expose the all-zero empty default.
     *
     * Counts describe the source scene represented by this Model. @ref Diagnostics is ordered by
     * discovery and contains only outcomes that occurred; consumers must branch on @ref
     * GltfImportDiagnosticEXT::Code rather than on its display message or vector position.
     */
    CNAEXT struct GltfImportReportEXT
    {
        /** @brief Nodes imported from the represented source scene. */
        std::size_t NodeCount = 0;
        /** @brief Mesh placements imported from source nodes. */
        std::size_t MeshInstanceCount = 0;
        /** @brief Distinct source meshes referenced by those placements. */
        std::size_t DistinctMeshCount = 0;
        /** @brief Distinct source meshes referenced by more than one placement. */
        std::size_t SharedMeshCount = 0;
        /** @brief Longest imported root-to-leaf node chain. */
        std::size_t MaxNodeDepth = 0;
        /** @brief Imported scene nodes that reference a camera. */
        std::size_t CameraNodeCount = 0;
        /** @brief Imported scene nodes that reference a punctual light. */
        std::size_t LightNodeCount = 0;
        /** @brief Punctual lights that reached a CNA effect light slot. */
        std::size_t ImportedLightCount = 0;
        /** @brief Source primitives represented by this Model, excluding material variants. */
        std::size_t PrimitiveCount = 0;
        /** @brief Independent skins represented by this Model. */
        std::size_t SkinCount = 0;
        /** @brief Source animations inspected while producing this Model. */
        std::size_t AnimationCount = 0;
        /** @brief Animation clips actually retained by this Model. */
        std::size_t ClipCount = 0;
        /** @brief Ordered, non-empty import outcomes discovered for this Model. */
        std::vector<GltfImportDiagnosticEXT> Diagnostics;

        /**
         * @brief Gets the number of warning diagnostics.
         * @return Warning entries, not the sum of their occurrence counts.
         */
        [[nodiscard]] std::size_t getWarningCountProperty() const;
        /**
         * @brief Gets the number of authored occurrences explicitly reported as dropped.
         * @return The sum of dropped-data and unsupported-feature occurrence counts.
         */
        [[nodiscard]] std::size_t getDroppedFeatureCountProperty() const;
        /**
         * @brief Gets the number of authored occurrences explicitly reported as approximated.
         * @return The sum of approximation occurrence counts.
         */
        [[nodiscard]] std::size_t getApproximationCountProperty() const;
        /**
         * @brief Tests whether at least one warning diagnostic is present.
         * @return True when the imported result may differ from the source; otherwise false.
         */
        [[nodiscard]] bool AnythingLost() const;
    };
}
