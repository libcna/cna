// SPDX-License-Identifier: MS-PL
#pragma once

#include <filesystem>
#include <string>

namespace CNA::Tools::Gltf
{
    /**
     * @brief Runs the glTF -> `.cnj` conversion that `cna_tool_gltf_to_cnj` performs
     *        (plans/plan_cnb.md `CNBF-106`).
     *
     * Declared so a second front-end can reuse the **same orchestration**, in the same translation
     * unit, rather than growing a second interpretation of glTF. That is the whole architectural
     * point: `cna_tool_gltf_to_cnb` links this file and calls this function, so the two tools
     * cannot disagree about what a glTF file means — there is only one implementation to disagree
     * with.
     *
     * @param inputPath  The `.gltf` or `.glb` to read.
     * @param outputDir  Directory to write the `.cnj` document and its sidecar files into.
     * @param baseName   Stem for the produced files.
     * @param unitScale  Uniform multiplier applied to positions and bone translations.
     * @throws std::exception on any conversion failure; the message is caller-facing.
     */
    void ConvertGltfToCnj(const std::filesystem::path& inputPath,
                          const std::filesystem::path& outputDir, const std::string& baseName,
                          float unitScale);
}
