// SPDX-License-Identifier: MS-PL
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace CNA::Tools::Gltf
{
    /** @brief Observable products and source inputs from one shared glTF-to-CNJ conversion. */
    struct GltfToCnjResult
    {
        /** @brief CNJ documents produced in deterministic order. */
        std::vector<std::filesystem::path> documents;

        /** @brief Extracted or derived texture images produced in deterministic order. */
        std::vector<std::filesystem::path> generatedTextures;

        /** @brief External buffer and image files read from the source glTF. */
        std::vector<std::filesystem::path> sourceDependencies;

        /** @brief Import warnings emitted by the shared glTF interpretation. */
        std::vector<std::string> warnings;
    };

    /**
     * @brief Runs the glTF -> `.cnj` conversion that `cna_tool_gltf_to_cnj` performs
     *        (plans/plan_cnb.md `CNBF-106`).
     *
     * Declared so a second front-end can reuse the **same orchestration**, in the same translation
     * unit, rather than growing a second interpretation of glTF. That is the whole architectural
     * point: the content library compiles this implementation and both CNB front ends call this
     * function, so they cannot disagree about what a glTF file means — there is only one source of
     * interpretation to disagree with.
     *
     * @param inputPath  The `.gltf` or `.glb` to read.
     * @param outputDir  Directory to write the `.cnj` document and its sidecar files into.
     * @param baseName   Stem for the produced files.
     * @param unitScale  Uniform multiplier applied to positions and bone translations.
     * @param emitMessages Whether to write legacy progress/warning lines to standard output.
     * @return Produced documents, generated textures, source dependencies and warnings.
     * @throws std::exception on any conversion failure; the message is caller-facing.
     */
    GltfToCnjResult ConvertGltfToCnj(const std::filesystem::path& inputPath,
                                     const std::filesystem::path& outputDir,
                                     const std::string& baseName, float unitScale,
                                     bool emitMessages = true);
}
