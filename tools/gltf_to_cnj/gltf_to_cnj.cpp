// SPDX-License-Identifier: MS-PL
//
// plan_cnj.md CNB-50/51/52 (Phase 12): offline glTF 2.0 -> .cnj Model/AnimationClip converter.
// Reads a .gltf/.glb file via cgltf and writes one Model .cnj per mesh group (meshes + optional
// "skeleton"/"animations" fields, Task 941's existing shape) plus vertex/index/skeleton binary
// sidecars, one standalone, shareable .cnj AnimationClip file per animation clip (CNB-48), and any
// referenced base-color textures. Targets CNA's general-purpose Model/AnimationClip path, not the
// separate Avatar-specific SkinnedModelEXT/.skinnedmodel.json system (see ../../gltf.md, which
// analyzes that different target).
//
// plan_cnj.md CNB-70 (Phase 13D): the actual glTF parsing/skeleton/animation/mesh-extraction core
// (topological bone reorder, sparse-accessor-safe reads, CUBICSPLINE Hermite evaluation, texture
// extraction, scene-scoped mesh grouping) now lives in the reusable
// CNA::Internal::GltfImport::GltfImportCore library (include/CNA/Internal/GltfImport/
// GltfImportCore.hpp -- see that header's own doc comments for the full behavior description),
// shared with the runtime Microsoft::Xna::Framework::Content::ContentManager::GltfModelTypeReader
// (ContentManager.cpp), which loads a .gltf/.glb file directly with no intermediate .cnj/binary
// sidecars. This file now keeps only what's genuinely CLI-specific: writing the extracted bytes
// out as .cnj JSON + binary sidecar files, and the command-line entry point.
//
// The original MVP material scope cuts named by CNB-50/51/55/67/73 are closed: the complete
// MaterialOut record (seven supported texture slots, factors/scalars, alpha state and samplers) survives
// the .cnj path, locked by plan_gltf.md GLTF-236/GLTF-237. Current representation limits are
// reported by GltfImportCore rather than copied into this CLI-specific file.
//
// plan_cnj.md CNB-82/83 (Phase 14C): morph target position/normal/tangent deltas (GltfImportCore::
// ExtractMesh always extracts them), the default blend weights, and an optional weight animation
// track are serialized to a per-primitive binary sidecar (BuildMorphBytes) plus "morphTargets"/
// "morphWeights"/"morphWeightTrack" mesh-entry JSON fields, read back by ModelTypeReader::Read()'s
// own .cnj JSON path in ContentManager.cpp (mirroring the runtime glTF path's own MorphTargetDataEXT
// wiring -- see MorphTargetEXT.hpp's own doc comments). Formerly a documented scope cut (CNB-64,
// Phase 13B); now fully serialized through both the offline CLI/.cnj path and the runtime glTF path.

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "CNA/Internal/CnjMorphSidecarEXT.hpp"
#include "GltfOracleEXT.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Microsoft/Xna/Framework/Matrix.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Graphics::GltfImportDiagnosticEXT;
using Microsoft::Xna::Framework::Graphics::GltfImportDiagnosticKindEXT;
using Microsoft::Xna::Framework::Graphics::GltfImportDiagnosticSeverityEXT;
using Microsoft::Xna::Framework::Graphics::GltfImportReportEXT;
using namespace CNA::Internal::GltfImport;

namespace
{
    namespace Oracle = CnaTest::GltfOracle;

    // ---------------------------------------------------------------------------
    // Small helpers: binary writers, JSON string escaping.
    // ---------------------------------------------------------------------------

    void AppendFloat(std::vector<std::uint8_t>& out, float v)
    {
        std::uint8_t bytes[4];
        std::memcpy(bytes, &v, 4);
        out.insert(out.end(), bytes, bytes + 4);
    }

    void AppendInt32(std::vector<std::uint8_t>& out, std::int32_t v)
    {
        std::uint8_t bytes[4];
        std::memcpy(bytes, &v, 4);
        out.insert(out.end(), bytes, bytes + 4);
    }

    // Byte order matches BinReaderEXT::ReadMatrix() in ContentManager.cpp: 16 sequential floats,
    // consumed directly as Matrix(m11,m12,...,m44) -- i.e. row-major, XNA's own field order.
    void AppendMatrix(std::vector<std::uint8_t>& out, const Matrix& m)
    {
        AppendFloat(out, m.M11); AppendFloat(out, m.M12); AppendFloat(out, m.M13); AppendFloat(out, m.M14);
        AppendFloat(out, m.M21); AppendFloat(out, m.M22); AppendFloat(out, m.M23); AppendFloat(out, m.M24);
        AppendFloat(out, m.M31); AppendFloat(out, m.M32); AppendFloat(out, m.M33); AppendFloat(out, m.M34);
        AppendFloat(out, m.M41); AppendFloat(out, m.M42); AppendFloat(out, m.M43); AppendFloat(out, m.M44);
    }

    // Morph target CLI/.cnj serialization: one binary sidecar per morphed primitive, format:
    //   int32 targetCount
    //   repeat targetCount times:
    //     int32 vertexCount
    //     vertexCount * float32[3]   (position deltas)
    //     int32 hasNormalDeltas (0 or 1)
    //     if hasNormalDeltas: vertexCount * float32[3]   (normal deltas)
    //   optional GLTF-289 trailer, present only when at least one target has tangent deltas:
    //     int32 magic = 'MTAN', int32 version = 1, int32 targetCount
    //     repeat targetCount times:
    //       int32 hasTangentDeltas (0 or 1)
    //       if hasTangentDeltas: that target's vertexCount * float32[3] (tangent xyz deltas)
    //
    // Keeping the original position/normal prefix unchanged means an older reader loads a new
    // sidecar exactly as it did before and ignores the trailer. Targets without POSITION use a
    // zero-filled position stream in that prefix: the old format tied every following semantic's
    // length to `vertexCount`, so writing zero there made a normal-only target structurally
    // unreadable even though glTF permits it.
    // Mirrors BinReaderEXT's own byte order (sequential little-endian floats/int32s, native
    // memcpy) already used by the .skeleton.bin/.clip.bin formats in ContentManager.cpp.
    std::vector<std::uint8_t> BuildMorphBytes(const MeshOut& meshOut)
    {
        std::vector<std::uint8_t> out;
        const auto targetCount = meshOut.morphPositionDeltas.size();
        const std::size_t meshVertexCount = meshOut.stride > 0
            ? meshOut.vertexBytes.size() / static_cast<std::size_t>(meshOut.stride) : 0u;
        bool hasAnyTangents = false;
        AppendInt32(out, static_cast<std::int32_t>(targetCount));
        for (std::size_t t = 0; t < targetCount; ++t)
        {
            const auto& positions = meshOut.morphPositionDeltas[t];
            const bool hasNormals = t < meshOut.morphNormalDeltas.size()
                                     && !meshOut.morphNormalDeltas[t].empty();
            const bool hasTangents = t < meshOut.morphTangentDeltas.size()
                                      && !meshOut.morphTangentDeltas[t].empty();
            const auto validSemanticSize = [meshVertexCount](std::size_t size)
            {
                return size == 0u || size == meshVertexCount;
            };
            if (!validSemanticSize(positions.size()) ||
                (hasNormals && !validSemanticSize(meshOut.morphNormalDeltas[t].size())) ||
                (hasTangents && !validSemanticSize(meshOut.morphTangentDeltas[t].size())))
            {
                throw std::runtime_error(
                    "Morph target " + std::to_string(t) +
                    " has a delta count that differs from the mesh vertex count " +
                    std::to_string(meshVertexCount) + ".");
            }

            const std::size_t vertexCount =
                (!positions.empty() || hasNormals || hasTangents) ? meshVertexCount : 0u;
            AppendInt32(out, static_cast<std::int32_t>(vertexCount));
            for (std::size_t v = 0; v < vertexCount; ++v)
            {
                const Vector3 p = positions.empty() ? Vector3::Zero : positions[v];
                AppendFloat(out, p.X); AppendFloat(out, p.Y); AppendFloat(out, p.Z);
            }

            AppendInt32(out, hasNormals ? 1 : 0);
            if (hasNormals)
            {
                for (const Vector3& n : meshOut.morphNormalDeltas[t])
                {
                    AppendFloat(out, n.X); AppendFloat(out, n.Y); AppendFloat(out, n.Z);
                }
            }
            hasAnyTangents = hasAnyTangents || hasTangents;
        }

        if (hasAnyTangents)
        {
            AppendInt32(out, CNA::Internal::CnjMorphTangentTrailerMagicEXT);
            AppendInt32(out, CNA::Internal::CnjMorphTangentTrailerVersionEXT);
            AppendInt32(out, static_cast<std::int32_t>(targetCount));
            for (std::size_t t = 0; t < targetCount; ++t)
            {
                const bool hasTangents = t < meshOut.morphTangentDeltas.size()
                                          && !meshOut.morphTangentDeltas[t].empty();
                AppendInt32(out, hasTangents ? 1 : 0);
                if (hasTangents)
                {
                    for (const Vector3& tangent : meshOut.morphTangentDeltas[t])
                    {
                        AppendFloat(out, tangent.X);
                        AppendFloat(out, tangent.Y);
                        AppendFloat(out, tangent.Z);
                    }
                }
            }
        }
        return out;
    }

    void WriteBinaryFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream f(path, std::ios::binary);
        if (!f) { throw std::runtime_error("Cannot open for writing: " + path.string()); }
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    void WriteTextFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream f(path, std::ios::binary);
        if (!f) { throw std::runtime_error("Cannot open for writing: " + path.string()); }
        f << text;
    }

    std::string JsonEscape(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s)
        {
            switch (c)
            {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) { /* skip other control chars */ }
                    else { out += c; }
            }
        }
        return out;
    }

    std::string HexBytes(const std::vector<std::uint8_t>& bytes)
    {
        constexpr char digits[] = "0123456789abcdef";
        std::string result;
        result.reserve(bytes.size() * 2);
        for (const std::uint8_t byte : bytes)
        {
            result.push_back(digits[byte >> 4]);
            result.push_back(digits[byte & 0x0f]);
        }
        return result;
    }

    const char* DiagnosticSeverityNameEXT(GltfImportDiagnosticSeverityEXT severity)
    {
        return severity == GltfImportDiagnosticSeverityEXT::Warning ? "Warning" : "Information";
    }

    const char* DiagnosticKindNameEXT(GltfImportDiagnosticKindEXT kind)
    {
        switch (kind)
        {
            case GltfImportDiagnosticKindEXT::Information:        return "Information";
            case GltfImportDiagnosticKindEXT::GeneratedData:      return "GeneratedData";
            case GltfImportDiagnosticKindEXT::InvalidSourceData:  return "InvalidSourceData";
            case GltfImportDiagnosticKindEXT::Approximation:      return "Approximation";
            case GltfImportDiagnosticKindEXT::DroppedData:        return "DroppedData";
            case GltfImportDiagnosticKindEXT::UnsupportedFeature: return "UnsupportedFeature";
        }
        return "Information";
    }

    void WriteGltfImportReportEXT(std::ostringstream& json, const GltfImportReportEXT& report)
    {
        json << "  \"gltfImportReport\": {\n"
             << "    \"nodeCount\": " << report.NodeCount
             << ", \"meshInstanceCount\": " << report.MeshInstanceCount
             << ", \"distinctMeshCount\": " << report.DistinctMeshCount
             << ", \"sharedMeshCount\": " << report.SharedMeshCount
             << ", \"maxNodeDepth\": " << report.MaxNodeDepth << ",\n"
             << "    \"cameraNodeCount\": " << report.CameraNodeCount
             << ", \"lightNodeCount\": " << report.LightNodeCount
             << ", \"importedLightCount\": " << report.ImportedLightCount
             << ", \"primitiveCount\": " << report.PrimitiveCount
             << ", \"skinCount\": " << report.SkinCount
             << ", \"animationCount\": " << report.AnimationCount
             << ", \"clipCount\": " << report.ClipCount << ",\n"
             << "    \"diagnostics\": [\n";
        for (std::size_t i = 0; i < report.Diagnostics.size(); ++i)
        {
            const GltfImportDiagnosticEXT& diagnostic = report.Diagnostics[i];
            json << "      { \"code\": \"" << JsonEscape(diagnostic.Code)
                 << "\", \"severity\": \"" << DiagnosticSeverityNameEXT(diagnostic.Severity)
                 << "\", \"kind\": \"" << DiagnosticKindNameEXT(diagnostic.Kind)
                 << "\", \"subject\": \"" << JsonEscape(diagnostic.Subject)
                 << "\", \"count\": " << diagnostic.Count
                 << ", \"worstMagnitude\": " << diagnostic.WorstMagnitude
                 << ", \"details\": [";
            for (std::size_t d = 0; d < diagnostic.Details.size(); ++d)
            {
                json << "\"" << JsonEscape(diagnostic.Details[d]) << "\""
                     << (d + 1 < diagnostic.Details.size() ? ", " : "");
            }
            json << "], \"message\": \"" << JsonEscape(diagnostic.Message) << "\" }"
                 << (i + 1 < report.Diagnostics.size() ? "," : "") << "\n";
        }
        json << "    ]\n  },\n";
    }

    // Strips characters unsafe/awkward for a filename component (spaces, path separators, etc.)
    // down to alnum/underscore/hyphen, for names sourced from arbitrary glTF author-supplied
    // strings (skin/material names) rather than this tool's own fixed field names.
    std::string SanitizeForFilename(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s)
        {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') { out += c; }
            else { out += '_'; }
        }
        return out.empty() ? "unnamed" : out;
    }

    // Applies a unit-of-measure conversion to a transform's translation only. The importer's own
    // ScaleTranslation does the same thing for bind poses and inverse bind matrices; this is the
    // node-hierarchy half of the same rule (plan_gltf.md GLTF-121).
    Matrix ScaleLocalTranslation(const Matrix& m, float unitScale)
    {
        Matrix result = m;
        result.M41 *= unitScale;
        result.M42 *= unitScale;
        result.M43 *= unitScale;
        return result;
    }

    // ---------------------------------------------------------------------------
    // Per-group conversion + .cnj/binary output.
    // ---------------------------------------------------------------------------

    void ConvertGroup(const cgltf_data* data, const SceneGraphOut& sceneGraph, const MeshGroup& group,
                       const std::string& outName,
                       const std::filesystem::path& gltfDir, const std::filesystem::path& outputDir,
                       std::unordered_map<const cgltf_image*, std::string>& writtenTextures,
                       std::unordered_map<const cgltf_image*, std::string>& remappedOcclusionTextures,
                       float unitScale, const std::vector<std::string>& validationWarnings,
                       std::vector<std::string>& warnings)
    {
        const bool hasSkin = group.skin != nullptr;
        GltfImportReportEXT importReport;
        AppendGltfValidationWarningsEXT(importReport, validationWarnings);
        AppendGltfNodeGraphReportEXT(
            importReport, BuildNodeGraphReportEXT(sceneGraph, std::vector<MeshGroup>{group}));
        importReport.SkinCount = hasSkin ? 1u : 0u;
        SkeletonResult skeleton;
        if (hasSkin)
        {
            // plan_gltf.md GLTF-245/GLTF-247: same two coordinate spaces the runtime path resolves
            // -- the joints' full scene ancestry, and the skinned mesh node's own placement, which
            // glTF cancels rather than applies.
            Matrix meshNodeWorld = Matrix::getIdentityProperty();
            for (const MeshInstanceOut& placement : group.instances)
            {
                if (placement.skinned) { meshNodeWorld = placement.worldTransform; break; }
            }
            skeleton = BuildSkeleton(group.skin, sceneGraph, meshNodeWorld, unitScale);
        }

        struct MeshEntry {
            // plan_gltf.md GLTF-141: the primitive's own name, traceable back to the glTF mesh it
            // came from (and its primitive index when the mesh has several). The reader already
            // read a "name" field for hand-written .model.json assets; this is the offline path
            // finally writing one instead of leaving every imported mesh called "mesh".
            std::string name;
            std::string vertFile, idxFile, textureFile, texture2File;
            int stride; std::string effect; bool vertexColorEnabled; bool unlit = false;
            // plan_gltf.md GLTF-073: the topology the index buffer is in, by its specification
            // name. Absent from a .cnj written before this, which could only ever hold a triangle
            // list -- so the reader's default is TRIANGLES and an older asset is unaffected.
            std::string primitiveTopology = "TRIANGLES";
            // plan_gltf.md GLTF-236/GLTF-237: the same complete carrier the runtime loader
            // consumes. Keeping another loose copy here was exactly how four fields fell out of
            // the .cnj path while the direct path stayed correct.
            std::string normalMapFile, metallicRoughnessMapFile, emissiveMapFile,
                        pbrOcclusionMapFile, specularMapFile, specularColorMapFile;
            MaterialOut material;
            // Morph target CLI/.cnj serialization: morphFile is the binary sidecar path (empty =
            // no morph targets on this primitive), morphWeights are the default blend weights, and
            // morphWeightTrack (optional) is the "weights" animation channel, if any.
            std::string morphFile;
            std::vector<float> morphWeights;
            std::optional<MorphWeightTrackOut> morphWeightTrack;
            // plan_gltf.md GLTF-461: the primitive authored no NORMAL, so its flat normals are a
            // function of the morph weights and the runtime has to recompute them per pose. Carried
            // as a mesh-entry JSON field rather than a further binary trailer because the reader
            // already has the index buffer it needs; only the DECISION has to travel.
            bool morphFlatNormals = false;
            // plan_gltf.md GLTF-114/GLTF-129 (Phase 5): index into the emitted "bones" array of the
            // node that instantiates this primitive's mesh -- 0 (the identity root) for a skinned
            // instance, whose own node transform glTF requires to be ignored.
            int parentBone = 0;
            // plan_gltf.md GLTF-139: which ModelMesh this primitive is a part OF. The .cnj
            // "meshes" array is per primitive and XNA's shape is one ModelMesh per mesh with one
            // part per primitive, so consecutive entries sharing this value are one ModelMesh.
            // Left -1 -- and then omitted from the JSON entirely -- for a single-primitive
            // placement, whose entry is its own mesh either way; that keeps an ordinary asset's
            // .cnj byte-identical to what it was before this field existed.
            int partOfMesh = -1;
            // GLTF-341/342: a material-variant state is serialized as another full mesh-state
            // record in the same array, reusing the default entry's index data. The reader does
            // not expose it as another ModelMeshPart: variantOf names the preceding default entry
            // it overrides, and materialVariant indexes the root name table. Keeping the record
            // flat lets it travel through the exact same mature .cnj state reader as a default --
            // textures, samplers, morph carrier and all -- instead of inventing a smaller nested
            // material schema that would immediately lose one of those fields.
            int variantOf = -1;
            int materialVariant = -1;
        };
        std::vector<MeshEntry> meshEntries;

        int meshCounter = 0;
        int placementIndex = -1;
        for (const MeshInstanceOut& instance : group.instances)
        {
            const cgltf_mesh* mesh = instance.mesh;
            ++placementIndex;
            AppendGltfInstanceReportEXT(
                importReport, instance,
                instance.node != nullptr && instance.node->name != nullptr
                    ? instance.node->name : "<unnamed>");
            for (cgltf_size p = 0; p < mesh->primitives_count; ++p)
            {
                const std::string partName = mesh->name
                    ? (std::string(mesh->name) + (mesh->primitives_count > 1 ? "_" + std::to_string(p) : ""))
                    : ("mesh" + std::to_string(meshCounter));
                MeshOut meshOut = ExtractMesh(data, mesh->primitives[p], partName, hasSkin ? &skeleton : nullptr, unitScale);
                AppendGltfMeshReportEXT(importReport, meshOut, partName);

                // Morph target CLI/.cnj serialization: write the position/normal deltas to a
                // binary sidecar (BuildMorphBytes) plus the default weights and (if present) the
                // weight animation track, both threaded through MeshEntry into the JSON below.
                std::string morphFile;
                std::vector<float> morphWeights;
                std::optional<MorphWeightTrackOut> morphWeightTrack;
                const bool morphFlatNormals = meshOut.morphedFlatNormalsEXT;
                if (!meshOut.morphPositionDeltas.empty())
                {
                    morphFile = outName + "_mesh" + std::to_string(meshCounter) + "_morph.bin";
                    WriteBinaryFile(outputDir / morphFile, BuildMorphBytes(meshOut));

                    const std::size_t targetCount = meshOut.morphPositionDeltas.size();
                    // glTF gives an instancing node's `weights` precedence over `mesh.weights`.
                    // This converter emits one entry per placement, so using the mesh alone made
                    // two instances with distinct default poses round-trip as the same pose.
                    morphWeights = GetMeshDefaultWeights(mesh, targetCount, instance.node);
                    morphWeightTrack = ExtractMorphWeightTrack(data, mesh, targetCount);
                    AppendGltfMorphReportEXT(
                        importReport, BuildMorphReportEXT(meshOut, morphWeights),
                        partName, meshOut.usePbr);
                }

                // GLTF-188: two distinct sampled TEXCOORD sets are carried; name any map that
                // needs a third rather than silently remapping it.
                if (!meshOut.uvSetMismatchedMapsEXT.empty())
                {
                    // GLTF-188: named, so the warning says which map to go and look at.
                    std::string maps;
                    for (const std::string& map : meshOut.uvSetMismatchedMapsEXT)
                    {
                        if (!maps.empty()) { maps += ", "; }
                        maps += map;
                    }
                    warnings.push_back(
                        "Primitive '" + partName + "' needs a third distinct glTF TEXCOORD set for " +
                        maps + "; CNA carries the first two sampled sets, so these maps fall back "
                        "to packed channel 0.");
                }

                // plan_gltf.md GLTF-206: the converter copies glTF PNG/JPEG bytes and the runtime
                // decoder creates one texture level. Report authored mip filtering now, rather
                // than pretending a role-incorrect generic RGBA downsample would be fidelity.
                if (!meshOut.mipmappedSamplerMapsWithoutMipChainEXT.empty())
                {
                    std::string maps;
                    for (const std::string& map :
                         meshOut.mipmappedSamplerMapsWithoutMipChainEXT)
                    {
                        if (!maps.empty()) { maps += ", "; }
                        maps += map;
                    }
                    warnings.push_back(
                        "Primitive '" + partName + "': " + maps +
                        " declares a mipmapped minFilter, but CNA imports glTF PNG/JPEG images "
                        "with one texture level. Role-aware mip generation is deferred, so level "
                        "zero is used for every LOD and minification quality may be reduced "
                        "(GLTF-206).");
                }

                // plan_gltf.md GLTF-339: transmission approximated as alpha blending.
                if (meshOut.transmissionApproximatedEXT)
                {
                    warnings.push_back(
                        "Primitive '" + partName + "' declares KHR_materials_transmission (factor " +
                        std::to_string(meshOut.transmissionFactorEXT) +
                        "); CNA has no transmission pass, so it is approximated as alpha blending "
                        "with alpha = 1 - factor. This is NOT physical: no refraction, no "
                        "roughness blur, tinted glass darkens rather than tints what is behind it, "
                        "and specular fades with the alpha." +
                        std::string(meshOut.transmissionHasTextureEXT
                            ? " The transmission texture has nowhere to go in this approximation -- "
                              "the whole surface uses the single factor."
                            : ""));
                }

                // plan_gltf.md GLTF-461: the residue of the flat-normal computation, not the
                // computation itself -- the split is exact, so only what the tolerance merged and
                // what glTF required to be thrown away are worth a warning.
                if (meshOut.flatNormalMergedVertexCountEXT > 0)
                {
                    warnings.push_back(
                        "Primitive '" + partName + "' authors no NORMAL, so glTF 3.7.2.1's flat "
                        "normals were computed; " +
                        std::to_string(meshOut.flatNormalMergedVertexCountEXT) +
                        " vertex/vertices average faces that are parallel only within the split's "
                        "reproducibility tolerance rather than exactly.");
                }
                if (meshOut.ignoredTangentForGeneratedNormalsEXT)
                {
                    warnings.push_back(
                        "Primitive '" + partName + "' authors a TANGENT basis but no NORMAL. glTF "
                        "3.7.2.1 requires the provided tangents to be ignored in that case, so a "
                        "basis was generated from the computed normals instead.");
                }
                if (!meshOut.ignoredMorphAttributesEXT.empty())
                {
                    std::string names;
                    for (const std::string& semantic : meshOut.ignoredMorphAttributesEXT)
                    {
                        if (!names.empty()) { names += ", "; }
                        names += semantic;
                    }
                    warnings.push_back(
                        "Primitive '" + partName + "' has morph targets carrying " + names +
                        ", which CNA does not morph: glTF 3.7.2.2 makes morphed TEXCOORD_n and "
                        "COLOR_n optional and CNA carries only POSITION, NORMAL and TANGENT.");
                }
                if (meshOut.ignoredMorphNormalDeltasForGeneratedNormalsEXT)
                {
                    warnings.push_back(
                        "Primitive '" + partName + "' has a morph target declaring NORMAL deltas "
                        "while the base authors no NORMAL, which glTF 3.7.2.2 does not permit. The "
                        "deltas were dropped and each morphed pose's flat normals are computed "
                        "instead.");
                }

                // plan_gltf.md GLTF-095/GLTF-257: influence sets past the first are dropped,
                // because XNA's BlendIndices/BlendWeight carry exactly four.
                if (meshOut.extraInfluenceSetsEXT > 0)
                {
                    warnings.push_back(
                        "Primitive '" + partName + "' authors " +
                        std::to_string(meshOut.extraInfluenceSetsEXT + 1) +
                        " joint influence sets; only the first four influences are imported. Up to " +
                        std::to_string(meshOut.worstDroppedInfluenceEXT * 100.0f) +
                        "% of a vertex's influence was dropped. The retained weights are "
                        "renormalised, so the skin is coarser rather than collapsed.");
                }

                // plan_gltf.md GLTF-200/GLTF-350: a map whose pixels are in a format CNA has no
                // decoder for. A build pipeline is exactly where this needs to be loud -- the
                // conversion succeeds, and the texture is simply not in the output.
                for (const std::string& unsupported : meshOut.unsupportedTextureSourcesEXT)
                {
                    warnings.push_back(
                        "Primitive '" + partName + "' has a texture CNA cannot read -- " +
                        unsupported + ". That map is not applied; the primitive is converted as "
                        "though it had none.");
                }

                const std::string vertFile = outName + "_mesh" + std::to_string(meshCounter) + "_verts.bin";
                const std::string idxFile  = outName + "_mesh" + std::to_string(meshCounter) + "_idx.bin";
                WriteBinaryFile(outputDir / vertFile, meshOut.vertexBytes);
                WriteBinaryFile(outputDir / idxFile, meshOut.indexBytes);

                std::string textureFile;
                if (meshOut.material.baseColorImage)
                {
                    auto cached = writtenTextures.find(meshOut.material.baseColorImage);
                    if (cached != writtenTextures.end())
                    {
                        textureFile = cached->second;
                    }
                    else if (auto img = ExtractImage(meshOut.material.baseColorImage, gltfDir))
                    {
                        textureFile = outName + "_tex" + std::to_string(writtenTextures.size()) + "." + img->extension;
                        WriteBinaryFile(outputDir / textureFile, img->bytes);
                        writtenTextures[meshOut.material.baseColorImage] = textureFile;
                    }
                }

                std::string texture2File;
                if (meshOut.useDualTexture && meshOut.material.occlusionImage)
                {
                    // CNB-88 (Phase 14E): DualTextureEffect's own occlusion-as-lightmap blend
                    // expects "0.5 = neutral", not glTF's own real "1.0 = fully visible"
                    // occlusion convention (see RemapOcclusionImageForDualTextureEXT's own doc
                    // comment for the full derivation) -- decode/halve/re-encode before writing,
                    // fixing the ~2x-too-bright approximation. Cached separately from
                    // writtenTextures: the SAME image could in principle also be referenced,
                    // unmodified, as a different primitive's PbrEffect::OcclusionMap elsewhere in
                    // this same file, and that must not observe the remapped bytes (or vice versa).
                    auto cached = remappedOcclusionTextures.find(meshOut.material.occlusionImage);
                    if (cached != remappedOcclusionTextures.end())
                    {
                        texture2File = cached->second;
                    }
                    else if (auto img = ExtractImage(meshOut.material.occlusionImage, gltfDir))
                    {
                        auto remapped = RemapOcclusionImageForDualTextureEXT(*img);
                        if (!remapped)
                        {
                            warnings.push_back(
                                "Primitive '" + partName + "' has an occlusion texture that "
                                "could not be decoded for the DualTextureEffect brightness fix "
                                "-- written unmodified (still ~2x too bright where unoccluded).");
                            remapped = img;
                        }
                        // Distinct "_texocc" prefix + its own independent counter (rather than
                        // sharing writtenTextures' own "_tex"+N sequence): this cache is a
                        // genuinely separate namespace from writtenTextures (same cgltf_image*
                        // key can validly appear in both, with different byte content), so
                        // deriving a shared index from either map's .size() alone risks two
                        // unrelated entries computing the same N and colliding on disk.
                        texture2File = outName + "_texocc" + std::to_string(remappedOcclusionTextures.size())
                                     + "." + remapped->extension;
                        WriteBinaryFile(outputDir / texture2File, remapped->bytes);
                        remappedOcclusionTextures[meshOut.material.occlusionImage] = texture2File;
                    }
                }

                // plan_cnj.md CNB-59 (Phase 13A): PbrEffect's own 4 maps. A helper mirroring the
                // baseColor/occlusion extraction above -- cached by cgltf_image* like the others,
                // so a texture shared across primitives is only written once.
                auto extractCached = [&](const cgltf_image* image) -> std::string
                {
                    if (!image) { return {}; }
                    auto cached = writtenTextures.find(image);
                    if (cached != writtenTextures.end()) { return cached->second; }
                    if (auto img = ExtractImage(image, gltfDir))
                    {
                        std::string file = outName + "_tex" + std::to_string(writtenTextures.size()) + "." + img->extension;
                        WriteBinaryFile(outputDir / file, img->bytes);
                        writtenTextures[image] = file;
                        return file;
                    }
                    return {};
                };
                std::string normalMapFile, metallicRoughnessMapFile, emissiveMapFile,
                            pbrOcclusionMapFile, specularMapFile, specularColorMapFile;
                if (meshOut.usePbr)
                {
                    normalMapFile            = extractCached(meshOut.material.normalImage);
                    metallicRoughnessMapFile =
                        extractCached(meshOut.material.metallicRoughnessImage);
                    emissiveMapFile          = extractCached(meshOut.material.emissiveImage);
                    pbrOcclusionMapFile      = extractCached(meshOut.material.occlusionImage);
                    specularMapFile          = extractCached(meshOut.material.specularImageEXT);
                    specularColorMapFile     =
                        extractCached(meshOut.material.specularColorImageEXT);
                }

                MeshEntry entry;
                entry.vertFile = vertFile;
                entry.idxFile = idxFile;
                entry.stride = meshOut.stride;
                // meshOut.usePbr forces stride 48 (unskinned) / 68 (skinned) --
                // BasicEffect/DualTextureEffect/SkinnedEffect don't understand either layout at
                // all, so this branch must never fall through to any of them (unlike
                // useDualTexture's own "fall back to BasicEffect if Texture2 extraction failed"
                // case just above, where the vertex layout stays a BasicEffect-compatible
                // stride 20/24/32 either way).
                entry.effect = (meshOut.usePbr && meshOut.skinned) ? "SkinnedPbrEffect"
                              : meshOut.usePbr ? "PbrEffect"
                              : meshOut.skinned ? "SkinnedEffect"
                              : meshOut.useDualTexture ? "DualTextureEffect"
                              : "BasicEffect";
                entry.textureFile = textureFile;
                // A DualTextureEffect mesh needs both textures to render sensibly (the shader
                // always samples both slots) -- if occlusion image extraction failed for some
                // reason while useDualTexture was still decided from the material data, fall back
                // to BasicEffect rather than emitting a Texture2-less DualTextureEffect.
                if (meshOut.useDualTexture && texture2File.empty())
                {
                    entry.effect = "BasicEffect";
                }
                else
                {
                    entry.texture2File = texture2File;
                }
                entry.normalMapFile = normalMapFile;
                entry.metallicRoughnessMapFile = metallicRoughnessMapFile;
                entry.emissiveMapFile = emissiveMapFile;
                entry.pbrOcclusionMapFile = pbrOcclusionMapFile;
                entry.specularMapFile = specularMapFile;
                entry.specularColorMapFile = specularColorMapFile;
                entry.material = meshOut.material;
                entry.primitiveTopology = PrimitiveTopologyName(meshOut.topology);
                entry.vertexColorEnabled = meshOut.colored;
                // plan_gltf.md GLTF-337: KHR_materials_unlit. Carried through the .cnj so the two
                // loaders agree -- the runtime path turns lighting off from MeshOut directly,
                // and without this field the offline path would silently light the same file.
                entry.unlit = meshOut.unlitEXT;
                entry.morphFile = morphFile;
                entry.morphWeights = morphWeights;
                entry.morphFlatNormals = morphFlatNormals;
                entry.morphWeightTrack = morphWeightTrack;
                entry.parentBone = instance.skinned ? 0 : instance.sceneNodeIndex;
                // GLTF-139: only a multi-primitive placement needs the grouping key; a
                // single-primitive one is its own ModelMesh under either rule.
                entry.partOfMesh = mesh->primitives_count > 1 ? placementIndex : -1;
                entry.name = partName;
                const int defaultEntryIndex = static_cast<int>(meshEntries.size());
                meshEntries.push_back(entry);

                for (const MaterialVariantOutEXT& variant : ExtractMaterialVariantsEXT(
                         data, mesh->primitives[p], partName,
                         hasSkin ? &skeleton : nullptr, unitScale))
                {
                    const MeshOut& variantMesh = variant.mesh;
                    AppendGltfMeshReportEXT(
                        importReport, variantMesh,
                        partName + " variant " + std::to_string(variant.variantIndex), false);
                    MeshEntry variantEntry;
                    variantEntry.name = partName;
                    variantEntry.variantOf = defaultEntryIndex;
                    variantEntry.materialVariant = static_cast<int>(variant.variantIndex);
                    variantEntry.idxFile = idxFile; // topology and indices are material-independent
                    variantEntry.stride = variantMesh.stride;
                    variantEntry.primitiveTopology = PrimitiveTopologyName(variantMesh.topology);
                    variantEntry.vertexColorEnabled = variantMesh.colored;
                    variantEntry.unlit = variantMesh.unlitEXT;
                    variantEntry.material = variantMesh.material;
                    variantEntry.parentBone = entry.parentBone;

                    variantEntry.vertFile =
                        outName + "_mesh" + std::to_string(meshCounter) + "_variant" +
                        std::to_string(variant.variantIndex) + "_verts.bin";
                    WriteBinaryFile(outputDir / variantEntry.vertFile,
                                    variantMesh.vertexBytes);

                    variantEntry.textureFile = extractCached(
                        variantMesh.material.baseColorImage);
                    if (variantMesh.useDualTexture && variantMesh.material.occlusionImage)
                    {
                        auto cached = remappedOcclusionTextures.find(
                            variantMesh.material.occlusionImage);
                        if (cached != remappedOcclusionTextures.end())
                        {
                            variantEntry.texture2File = cached->second;
                        }
                        else if (auto img = ExtractImage(
                                     variantMesh.material.occlusionImage, gltfDir))
                        {
                            auto remapped = RemapOcclusionImageForDualTextureEXT(*img);
                            if (!remapped) { remapped = img; }
                            variantEntry.texture2File =
                                outName + "_texocc" +
                                std::to_string(remappedOcclusionTextures.size()) + "." +
                                remapped->extension;
                            WriteBinaryFile(outputDir / variantEntry.texture2File,
                                            remapped->bytes);
                            remappedOcclusionTextures[
                                variantMesh.material.occlusionImage] =
                                variantEntry.texture2File;
                        }
                    }
                    if (variantMesh.usePbr)
                    {
                        variantEntry.normalMapFile =
                            extractCached(variantMesh.material.normalImage);
                        variantEntry.metallicRoughnessMapFile =
                            extractCached(variantMesh.material.metallicRoughnessImage);
                        variantEntry.emissiveMapFile =
                            extractCached(variantMesh.material.emissiveImage);
                        variantEntry.pbrOcclusionMapFile =
                            extractCached(variantMesh.material.occlusionImage);
                        variantEntry.specularMapFile =
                            extractCached(variantMesh.material.specularImageEXT);
                        variantEntry.specularColorMapFile =
                            extractCached(variantMesh.material.specularColorImageEXT);
                    }

                    variantEntry.effect =
                        (variantMesh.usePbr && variantMesh.skinned) ? "SkinnedPbrEffect"
                        : variantMesh.usePbr ? "PbrEffect"
                        : variantMesh.skinned ? "SkinnedEffect"
                        : variantMesh.useDualTexture ? "DualTextureEffect"
                        : "BasicEffect";
                    if (variantMesh.useDualTexture && variantEntry.texture2File.empty())
                        variantEntry.effect = "BasicEffect";

                    if (!variantMesh.morphPositionDeltas.empty())
                    {
                        variantEntry.morphFile =
                            outName + "_mesh" + std::to_string(meshCounter) + "_variant" +
                            std::to_string(variant.variantIndex) + "_morph.bin";
                        WriteBinaryFile(outputDir / variantEntry.morphFile,
                                        BuildMorphBytes(variantMesh));
                        const std::size_t targetCount =
                            variantMesh.morphPositionDeltas.size();
                        variantEntry.morphWeights =
                            GetMeshDefaultWeights(mesh, targetCount, instance.node);
                        variantEntry.morphWeightTrack =
                            ExtractMorphWeightTrack(data, mesh, targetCount);
                        // plan_gltf.md GLTF-461, per variant: a variant chooses its own layout, so
                        // whether its normals are generated is its own MeshOut's answer.
                        variantEntry.morphFlatNormals = variantMesh.morphedFlatNormalsEXT;
                    }
                    meshEntries.push_back(std::move(variantEntry));
                }
                ++meshCounter;
            }
        }
        if (meshEntries.empty())
        {
            throw std::runtime_error("Mesh group '" + outName + "' contains no mesh primitives to import.");
        }

        std::string skeletonFile;
        if (hasSkin)
        {
            std::vector<std::uint8_t> skelBytes;
            AppendInt32(skelBytes, static_cast<std::int32_t>(skeleton.bones.size()));
            for (const BoneOut& b : skeleton.bones) { AppendInt32(skelBytes, b.parentIndex); }
            for (const BoneOut& b : skeleton.bones) { AppendMatrix(skelBytes, b.bindPoseLocal); }
            for (const BoneOut& b : skeleton.bones) { AppendMatrix(skelBytes, b.inverseBindGlobal); }
            // GLTF-245/GLTF-247: the per-root prefix, appended after the two existing matrix
            // blocks so a reader that stops early still sees the original layout unchanged.
            for (const BoneOut& b : skeleton.bones) { AppendMatrix(skelBytes, b.parentWorldPrefix); }

            skeletonFile = outName + ".skeleton.bin";
            WriteBinaryFile(outputDir / skeletonFile, skelBytes);
        }

        struct ClipEntry { std::string name, cnjFile; };
        std::vector<ClipEntry> clipEntries;

        // One writer for both clip kinds (plan_gltf.md GLTF-294). The joint-palette and scene-node
        // paths differ only in which index space the tracks are in, and that difference is a field
        // in the file rather than a second serialiser -- keeping two would let them drift, which is
        // the shape of mistake this whole track has been about.
        const auto writeClip = [&](const ClipOut& clip) -> std::string {
            std::ostringstream json;
            json << "{\n  \"cnjVersion\": 1,\n  \"type\": \"AnimationClip\",\n  \"duration\": "
                 << clip.duration;
            // Written only for a scene-node clip, so a joint-palette clip's .cnj is byte-identical
            // to what it was before -- and an older file, which could only ever have been one,
            // reads back as one.
            if (clip.targetSpace == ClipTargetSpace::SceneNode)
            {
                json << ",\n  \"targetSpace\": \"SceneNode\"";
            }
            json << ",\n  \"tracks\": [\n";
            for (std::size_t ti = 0; ti < clip.tracks.size(); ++ti)
            {
                const TrackOut& track = clip.tracks[ti];
                json << "    { \"boneIndex\": " << track.boneIndex << ", \"keys\": [\n";
                for (std::size_t ki = 0; ki < track.keys.size(); ++ki)
                {
                    const KeyframeOut& k = track.keys[ki];
                    json << "      { \"time\": " << k.time
                         << ", \"translation\": [" << k.translation.X << ", " << k.translation.Y << ", " << k.translation.Z << "]"
                         << ", \"rotation\": [" << k.rotation.X << ", " << k.rotation.Y << ", " << k.rotation.Z << ", " << k.rotation.W << "]"
                         << ", \"scale\": [" << k.scale.X << ", " << k.scale.Y << ", " << k.scale.Z << "] }"
                         << (ki + 1 < track.keys.size() ? "," : "") << "\n";
                }
                json << "    ] }" << (ti + 1 < clip.tracks.size() ? "," : "") << "\n";
            }
            json << "  ]\n}\n";

            const std::string cnjFile = outName + "_" + SanitizeForFilename(clip.name) + ".cnj";
            WriteTextFile(outputDir / cnjFile, json.str());
            return cnjFile;
        };

        if (hasSkin)
        {
            AnimationReportEXT animationReport;
            for (const ClipOut& clip :
                 ExtractClips(data, skeleton, unitScale, warnings, &animationReport))
            {
                clipEntries.push_back({clip.name, writeClip(clip)});
            }
            AppendGltfAnimationReportEXT(importReport, animationReport);
        }

        // plan_gltf.md GLTF-293: rigid (non-joint) node animation. Before this, a channel targeting
        // an ordinary mesh node matched nothing in the skin's joint set and was dropped in complete
        // silence -- and for an unskinned file ExtractClips was never called at all, so the .cnj
        // simply had no "animations" key and said nothing about why (defect D6).
        //
        // GLTF-294 added the .cnj clip schema's "targetSpace" field, so the clip is now SERIALISED
        // rather than only reported. That field is what makes it safe: a scene-node clip's
        // boneIndex is a sceneNodeIndex, and without a field naming which of the two index spaces
        // (§15.1.2) a track is in, a reader could apply a scene-node index as a joint-palette slot
        // -- a fresh silent corruption in place of the old one.
        //
        // GLTF-295: extraction is unconditional. A file with no skin at all still reaches this
        // loop, which is what the task's own acceptance asks for.
        AnimationReportEXT sceneAnimationReport;
        const std::vector<ClipOut> sceneNodeClips =
            ExtractSceneNodeClips(
                data, sceneGraph, unitScale, warnings, &sceneAnimationReport);
        if (hasSkin) { sceneAnimationReport.clipCount = 0; }
        AppendGltfAnimationReportEXT(importReport, sceneAnimationReport);
        for (const ClipOut& clip : sceneNodeClips)
        {
            // Model::Tag holds one object, and a skinned model already uses it for SkinningData.
            // A file with both a skin and rigid node animation therefore has nowhere to put these
            // (docs/gltf-api-change-review.md §1.5). Reported by name rather than dropped, which
            // is the property D6 was really about.
            if (hasSkin)
            {
                const std::size_t droppedTrackCount = CountGltfRigidAnimationDropsEXT(
                    clip, sceneGraph, {&skeleton});
                if (droppedTrackCount == 0) { continue; }
                warnings.push_back(
                    "Clip '" + clip.name + "' has " + std::to_string(droppedTrackCount) +
                    " scene-node track(s) not carried by this model's skin, but its Tag already "
                    "contains SkinningData, so those rigid tracks are not written (GLTF-295).");
                AppendGltfRigidAnimationDropEXT(importReport, clip.name, droppedTrackCount);
                continue;
            }
            clipEntries.push_back({clip.name, writeClip(clip)});
        }

        std::ostringstream json;
        json << "{\n  \"cnjVersion\": 2,\n  \"type\": \"Model\",\n";
        if (!skeletonFile.empty())
        {
            json << "  \"skeleton\": \"" << JsonEscape(skeletonFile) << "\",\n";
        }

        // plan_gltf.md GLTF-129 (Phase 5): the glTF node graph, one entry per BuildSceneGraph node,
        // parent-before-child, index 0 the synthetic identity root. Each mesh below names its own
        // "parentBone" index into this array, so the loader can rebuild the same ModelBone tree the
        // runtime .gltf path builds directly -- the two paths must place geometry identically
        // (GLTF-130). "transform" is the node-LOCAL transform in XNA row-major order; the loader
        // composes world transforms itself, exactly as Model::CopyAbsoluteBoneTransformsTo does.
        //
        // cnjVersion 2 adds this array. A version-1 Model .cnj has no "bones" (or a name-only one)
        // and still loads: the reader falls back to a single Root plus a child bone per mesh.
        json << "  \"bones\": [\n";
        for (std::size_t b = 0; b < sceneGraph.nodes.size(); ++b)
        {
            const SceneNodeOut& node = sceneGraph.nodes[b];
            // plan_gltf.md GLTF-121: unitScale converts the file's unit of measure, so it has to
            // reach the node translations as well as the vertex positions ExtractMesh already
            // scales. Scaling each node's LOCAL translation scales every composed world
            // translation by the same factor -- composition only ever adds a parent's already
            // scaled translation to a child's -- so a model converted from centimetres shrinks as
            // one object instead of collapsing its parts onto a full-size skeleton. Rotation and
            // any authored scale are untouched: this is a change of unit, not of shape.
            const Matrix m = ScaleLocalTranslation(node.localTransform, unitScale);
            json << "    { \"name\": \"" << JsonEscape(node.name) << "\", \"parent\": " << node.parentIndex
                 << ", \"transform\": ["
                 << m.M11 << ", " << m.M12 << ", " << m.M13 << ", " << m.M14 << ", "
                 << m.M21 << ", " << m.M22 << ", " << m.M23 << ", " << m.M24 << ", "
                 << m.M31 << ", " << m.M32 << ", " << m.M33 << ", " << m.M34 << ", "
                 << m.M41 << ", " << m.M42 << ", " << m.M43 << ", " << m.M44 << "] }"
                 << (b + 1 < sceneGraph.nodes.size() ? "," : "") << "\n";
        }
        json << "  ],\n";

        // CNB-97 (Phase 14H): KHR_lights_punctual, approximated as up to 3 directional lights
        // (see ExtractPunctualLightsEXT's own doc comment) -- scene-level, so the same extracted
        // lights are emitted into every mesh group's own .cnj output.
        LightReportEXT lightReport;
        const std::vector<LightOut> punctualLights = ExtractPunctualLightsEXT(data, lightReport);
        AppendGltfLightReportEXT(importReport, lightReport, punctualLights.size());
        // plan_gltf.md GLTF-326: what the three-directional-light approximation cost this file.
        if (lightReport.droppedLightCount > 0)
        {
            warnings.push_back(
                "The scene declares " +
                std::to_string(punctualLights.size() + lightReport.droppedLightCount) +
                " lights; XNA's stock effects bind three, so " +
                std::to_string(lightReport.droppedLightCount) + " were dropped.");
        }
        if (lightReport.approximatedPointLightCount > 0 ||
            lightReport.approximatedSpotLightCount > 0)
        {
            warnings.push_back(
                std::to_string(lightReport.approximatedPointLightCount) + " point and " +
                std::to_string(lightReport.approximatedSpotLightCount) +
                " spot light(s) were approximated as directional lights aimed at the scene origin; "
                "a spot's cone is lost entirely.");
        }
        if (lightReport.clampedIntensityLightCount > 0)
        {
            warnings.push_back(
                std::to_string(lightReport.clampedIntensityLightCount) +
                " light(s) had color * intensity above 1 and were clamped (worst channel " +
                std::to_string(lightReport.worstPreClampChannelEXT) +
                "); glTF intensity is photometric and unbounded, so a bright light imports as "
                "white.");
        }
        if (!punctualLights.empty())
        {
            json << "  \"lights\": [\n";
            for (std::size_t i = 0; i < punctualLights.size(); ++i)
            {
                const LightOut& l = punctualLights[i];
                json << "    { \"direction\": [" << l.direction.X << ", " << l.direction.Y << ", " << l.direction.Z
                     << "], \"diffuseColor\": [" << l.diffuseColor.X << ", " << l.diffuseColor.Y << ", " << l.diffuseColor.Z
                     << "] }" << (i + 1 < punctualLights.size() ? "," : "") << "\n";
            }
            json << "  ],\n";
        }
        if (!clipEntries.empty())
        {
            json << "  \"animations\": [\n";
            for (std::size_t i = 0; i < clipEntries.size(); ++i)
            {
                json << "    { \"name\": \"" << JsonEscape(clipEntries[i].name) << "\", \"clip\": \""
                     << JsonEscape(clipEntries[i].cnjFile) << "\" }" << (i + 1 < clipEntries.size() ? "," : "") << "\n";
            }
            json << "  ],\n";
        }
        if (data->variants_count > 0)
        {
            json << "  \"materialVariantNames\": [";
            for (cgltf_size i = 0; i < data->variants_count; ++i)
            {
                json << "\"" << JsonEscape(
                    data->variants[i].name != nullptr ? data->variants[i].name : "") << "\""
                     << (i + 1 < data->variants_count ? ", " : "");
            }
            json << "],\n";
        }
        WriteGltfImportReportEXT(json, importReport);
        json << "  \"meshes\": [\n";
        for (std::size_t i = 0; i < meshEntries.size(); ++i)
        {
            const MeshEntry& e = meshEntries[i];
            json << "    { \"name\": \"" << JsonEscape(e.name) << "\""
                 << ", \"vertices\": \"" << JsonEscape(e.vertFile) << "\", \"indices\": \"" << JsonEscape(e.idxFile)
                 << "\", \"vertexStride\": " << e.stride << ", \"effect\": \"" << e.effect << "\""
                 << ", \"parentBone\": " << e.parentBone;
            if (e.variantOf >= 0)
            {
                json << ", \"variantOf\": " << e.variantOf
                     << ", \"materialVariant\": " << e.materialVariant;
            }
            // GLTF-139: written only for a multi-primitive placement, so every other asset's .cnj
            // is byte-identical to what it was before the field existed.
            if (e.partOfMesh >= 0) { json << ", \"partOfMesh\": " << e.partOfMesh; }
            if (!e.textureFile.empty()) { json << ", \"texture\": \"" << JsonEscape(e.textureFile) << "\""; }
            if (!e.texture2File.empty()) { json << ", \"texture2\": \"" << JsonEscape(e.texture2File) << "\""; }
            if (e.vertexColorEnabled) { json << ", \"vertexColorEnabled\": true"; }
            // GLTF-337. The base colour travels with the flag on the non-PBR path: nothing else
            // there reads baseColorFactor, so an unlit material would otherwise import unlit AND
            // the wrong colour. The PBR branch below writes its own diffuseColor/alpha, and an
            // unlit material never reaches it.
            if (e.unlit)
            {
                json << ", \"unlit\": true"
                     << ", \"diffuseColor\": [" << e.material.baseColorFactor.X << ", "
                     << e.material.baseColorFactor.Y << ", " << e.material.baseColorFactor.Z
                     << "]"
                     << ", \"alpha\": " << e.material.baseColorFactor.W;
            }
            // Only written when it is not the default, so an ordinary triangle-list asset's .cnj
            // is byte-identical to what it was before GLTF-073.
            if (e.primitiveTopology != "TRIANGLES")
            {
                json << ", \"primitiveTopology\": \"" << JsonEscape(e.primitiveTopology) << "\"";
            }
            // plan_gltf.md GLTF-237: sampler state is part of the material as it reaches a draw,
            // even though glTF declares it on the texture object. The direct path already stores
            // all supported slots on ModelMeshPart; preserve non-default states in .cnj as well. An
            // explicit LinearWrap serialises to nothing because it is observationally identical
            // to the ModelMeshPart default.
            for (std::size_t slot = 0; slot < e.material.samplers.size(); ++slot)
            {
                const SamplerOut& sampler = e.material.samplers[slot];
                const std::string prefix = "sampler" + std::to_string(slot);
                if (sampler.filter != Microsoft::Xna::Framework::Graphics::TextureFilter::Linear)
                {
                    json << ", \"" << prefix << "Filter\": "
                         << static_cast<int>(sampler.filter);
                }
                if (sampler.addressU !=
                    Microsoft::Xna::Framework::Graphics::TextureAddressMode::Wrap)
                {
                    json << ", \"" << prefix << "AddressU\": "
                         << static_cast<int>(sampler.addressU);
                }
                if (sampler.addressV !=
                    Microsoft::Xna::Framework::Graphics::TextureAddressMode::Wrap)
                {
                    json << ", \"" << prefix << "AddressV\": "
                         << static_cast<int>(sampler.addressV);
                }
            }
            if (e.effect == "PbrEffect" || e.effect == "SkinnedPbrEffect")
            {
                if (!e.normalMapFile.empty()) { json << ", \"normalMap\": \"" << JsonEscape(e.normalMapFile) << "\""; }
                if (!e.metallicRoughnessMapFile.empty()) { json << ", \"metallicRoughnessMap\": \"" << JsonEscape(e.metallicRoughnessMapFile) << "\""; }
                if (!e.emissiveMapFile.empty()) { json << ", \"emissiveMap\": \"" << JsonEscape(e.emissiveMapFile) << "\""; }
                if (!e.pbrOcclusionMapFile.empty()) { json << ", \"occlusionMap\": \"" << JsonEscape(e.pbrOcclusionMapFile) << "\""; }
                if (!e.specularMapFile.empty()) { json << ", \"specularMap\": \"" << JsonEscape(e.specularMapFile) << "\""; }
                if (!e.specularColorMapFile.empty()) { json << ", \"specularColorMap\": \"" << JsonEscape(e.specularColorMapFile) << "\""; }
                json << ", \"metallicFactor\": " << e.material.metallicFactor
                     << ", \"roughnessFactor\": " << e.material.roughnessFactor
                     << ", \"emissiveFactor\": [" << e.material.emissiveFactor.X << ", "
                     << e.material.emissiveFactor.Y << ", " << e.material.emissiveFactor.Z << "]"
                     // GLTF-216/GLTF-237: these fields predated the complete material round-trip;
                     // GLTF-237 fixed the PBR reader that previously ignored them.
                     << ", \"diffuseColor\": [" << e.material.baseColorFactor.X << ", "
                     << e.material.baseColorFactor.Y << ", " << e.material.baseColorFactor.Z
                     << "]"
                     << ", \"alpha\": " << e.material.baseColorFactor.W;
                // GLTF-343/344: factor-only IOR/specular state survives the offline path. Defaults
                // are omitted so old/simple .cnj output remains byte-stable.
                if (e.material.iorEXT != 1.5f)
                    json << ", \"ior\": " << e.material.iorEXT;
                if (e.material.specularFactorEXT != 1.0f)
                    json << ", \"specularFactor\": " << e.material.specularFactorEXT;
                if (e.material.specularColorFactorEXT.X != 1.0f ||
                    e.material.specularColorFactorEXT.Y != 1.0f ||
                    e.material.specularColorFactorEXT.Z != 1.0f)
                {
                    json << ", \"specularColorFactor\": ["
                         << e.material.specularColorFactorEXT.X << ", "
                         << e.material.specularColorFactorEXT.Y << ", "
                         << e.material.specularColorFactorEXT.Z << "]";
                }
                // GLTF-224/225 were correct only on direct .gltf loads. These two schema fields
                // close the offline loss; omitted defaults keep older/simple .cnj byte-stable.
                if (e.material.normalScale != 1.0f)
                    json << ", \"normalScale\": " << e.material.normalScale;
                if (e.material.occlusionStrength != 1.0f)
                    json << ", \"occlusionStrength\": " << e.material.occlusionStrength;
                if (std::any_of(e.material.textureCoordinateSetsEXT.begin(),
                                e.material.textureCoordinateSetsEXT.begin() + 5,
                                [](std::uint8_t set) { return set != 0; }))
                {
                    json << ", \"textureCoordinateSets\": [";
                    for (std::size_t slot = 0;
                         slot < 5; ++slot)
                    {
                        if (slot != 0) json << ", ";
                        json << static_cast<int>(e.material.textureCoordinateSetsEXT[slot]);
                    }
                    json << "]";
                }
                if (e.material.textureCoordinateSetsEXT[5] != 0 ||
                    e.material.textureCoordinateSetsEXT[6] != 0)
                {
                    json << ", \"specularTextureCoordinateSets\": ["
                         << static_cast<int>(e.material.textureCoordinateSetsEXT[5]) << ", "
                         << static_cast<int>(e.material.textureCoordinateSetsEXT[6]) << "]";
                }
                const Microsoft::Xna::Framework::Graphics::TextureTransformEXT identityTransform;
                if (std::any_of(e.material.textureTransformsEXT.begin(),
                                e.material.textureTransformsEXT.begin() + 5,
                                [&](const auto& transform) {
                                    return transform != identityTransform;
                                }))
                {
                    // Flat five-values-per-slot form keeps the deliberately small .cnj reader
                    // unambiguous: offset.xy, scale.xy, rotation for each established PBR slot.
                    json << ", \"textureTransforms\": [";
                    for (std::size_t slot = 0; slot < 5; ++slot)
                    {
                        if (slot != 0) json << ", ";
                        const auto& transform = e.material.textureTransformsEXT[slot];
                        json << transform.Offset.X << ", " << transform.Offset.Y << ", "
                             << transform.Scale.X << ", " << transform.Scale.Y << ", "
                             << transform.Rotation;
                    }
                    json << "]";
                }
                if (e.material.textureTransformsEXT[5] != identityTransform ||
                    e.material.textureTransformsEXT[6] != identityTransform)
                {
                    json << ", \"specularTextureTransforms\": [";
                    for (std::size_t slot = 5; slot < 7; ++slot)
                    {
                        if (slot != 5) json << ", ";
                        const auto& transform = e.material.textureTransformsEXT[slot];
                        json << transform.Offset.X << ", " << transform.Offset.Y << ", "
                             << transform.Scale.X << ", " << transform.Scale.Y << ", "
                             << transform.Rotation;
                    }
                    json << "]";
                }
                // Written only when not the glTF default, so an ordinary opaque material's .cnj is
                // byte-identical to what it was before GLTF-228.
                const std::string alphaMode = AlphaModeEXTName(e.material.alphaMode);
                if (alphaMode != "OPAQUE") { json << ", \"alphaMode\": \"" << alphaMode << "\""; }
                if (e.material.alphaCutoff != 0.5f)
                    json << ", \"alphaCutoff\": " << e.material.alphaCutoff;
                if (e.material.doubleSided) { json << ", \"doubleSided\": true"; }
            }
            if (!e.morphFile.empty())
            {
                json << ", \"morphTargets\": \"" << JsonEscape(e.morphFile) << "\", \"morphWeights\": [";
                for (std::size_t w = 0; w < e.morphWeights.size(); ++w)
                {
                    json << e.morphWeights[w] << (w + 1 < e.morphWeights.size() ? ", " : "");
                }
                json << "]";
                if (e.morphFlatNormals) { json << ", \"morphFlatNormals\": true"; }
                if (e.morphWeightTrack)
                {
                    const MorphWeightTrackOut& track = *e.morphWeightTrack;
                    auto writeFloatArray = [&](const std::vector<float>& v)
                    {
                        json << "[";
                        for (std::size_t w = 0; w < v.size(); ++w)
                        {
                            json << v[w] << (w + 1 < v.size() ? ", " : "");
                        }
                        json << "]";
                    };
                    json << ", \"morphWeightTrack\": { \"stepInterpolation\": "
                         << (track.stepInterpolation ? "true" : "false")
                         << ", \"cubicSpline\": " << (track.cubicSpline ? "true" : "false")
                         << ", \"keys\": [\n";
                    for (std::size_t ki = 0; ki < track.keys.size(); ++ki)
                    {
                        const MorphWeightKeyframeOut& k = track.keys[ki];
                        json << "        { \"time\": " << k.time << ", \"weights\": ";
                        writeFloatArray(k.weights);
                        if (!k.inTangent.empty())
                        {
                            json << ", \"inTangent\": ";
                            writeFloatArray(k.inTangent);
                        }
                        if (!k.outTangent.empty())
                        {
                            json << ", \"outTangent\": ";
                            writeFloatArray(k.outTangent);
                        }
                        json << " }" << (ki + 1 < track.keys.size() ? "," : "") << "\n";
                    }
                    json << "      ] }";
                }
            }
            json << " }" << (i + 1 < meshEntries.size() ? "," : "") << "\n";
        }
        json << "  ]\n}\n";

        WriteTextFile(outputDir / (outName + ".cnj"), json.str());

        std::cout << "Wrote " << outputDir / (outName + ".cnj") << " ("
                  << meshCounter << " mesh part(s), "
                  << (hasSkin ? std::to_string(skeleton.bones.size()) + " bones, " : std::string("no skeleton, "))
                  << clipEntries.size() << " clip(s)).\n";
    }

    // ---------------------------------------------------------------------------
    // Top-level entry point.
    // ---------------------------------------------------------------------------

    struct ConvertOptions
    {
        std::filesystem::path inputPath;
        std::filesystem::path outputDir;
        std::string baseName;
        // glTF mandates meters; a source file that doesn't follow that convention (some exporters
        // don't) can be corrected with an explicit multiplier applied uniformly to every position
        // and bone translation (see gltf.md's own quality checklist, "Unit/coordinate convention").
        float unitScale = 1.0f;
    };

    void Convert(const ConvertOptions& opts)
    {
        cgltf_options parseOptions{};
        cgltf_data* data = nullptr;
        cgltf_result result = cgltf_parse_file(&parseOptions, opts.inputPath.string().c_str(), &data);
        if (result != cgltf_result_success)
        {
            throw std::runtime_error("cgltf_parse_file failed (code " + std::to_string(static_cast<int>(result)) +
                                      ") for: " + opts.inputPath.string());
        }
        struct DataGuard { cgltf_data* d; ~DataGuard() { cgltf_free(d); } } guard{data};

        // plan_gltf.md GLTF-032/GLTF-198: refuse a file naming something outside its own
        // directory, before cgltf_load_buffers resolves those URIs itself. The offline tool is if
        // anything the more exposed of the two entry points -- it is what a build pipeline points
        // at unattended assets with.
        ValidateExternalUriContainmentEXT(data, opts.inputPath.parent_path());

        result = cgltf_load_buffers(&parseOptions, data, opts.inputPath.string().c_str());
        if (result != cgltf_result_success)
        {
            throw std::runtime_error("cgltf_load_buffers failed (code " + std::to_string(static_cast<int>(result)) + ").");
        }

        if (data->asset.version && std::string(data->asset.version) != "2.0")
        {
            throw std::runtime_error("Unsupported glTF asset.version '" + std::string(data->asset.version) +
                                      "' -- only glTF 2.0 is supported.");
        }

        std::vector<std::string> warnings;

        // plan_gltf.md GLTF-021..GLTF-024: structural validation, extensionsRequired enforcement
        // and an ignored-extension report, before anything is decoded. Deliberately after
        // cgltf_load_buffers, because the sparse index-bound check needs buffer data to run at all.
        ValidateGltfEXT(data, opts.inputPath.string(), warnings);
        const std::vector<std::string> validationWarnings = warnings;

        // plan_gltf.md GLTF-113: build the node graph once and share it across every group, so
        // each group's emitted "bones" array indexes the same scene-node identity space.
        const SceneGraphOut sceneGraph = BuildSceneGraph(data);
        std::vector<MeshGroup> groups = CollectMeshGroups(data, sceneGraph);
        if (groups.empty())
        {
            throw std::runtime_error("File contains no mesh instances to import.");
        }

        std::filesystem::create_directories(opts.outputDir);
        const std::filesystem::path gltfDir = opts.inputPath.parent_path();
        std::unordered_map<const cgltf_image*, std::string> writtenTextures;
        std::unordered_map<const cgltf_image*, std::string> remappedOcclusionTextures;

        for (std::size_t g = 0; g < groups.size(); ++g)
        {
            std::string outName = opts.baseName;
            if (groups.size() > 1)
            {
                if (groups[g].skin == nullptr) { outName += "_static"; }
                else
                {
                    outName += "_" + (groups[g].skin->name ? SanitizeForFilename(groups[g].skin->name)
                                                             : ("skin" + std::to_string(g)));
                }
            }
            ConvertGroup(data, sceneGraph, groups[g], outName, gltfDir, opts.outputDir, writtenTextures,
                         remappedOcclusionTextures, opts.unitScale, validationWarnings, warnings);
        }

        if (groups.size() > 1)
        {
            std::cout << "File had " << groups.size() << " mesh groups (by skin) -- wrote "
                      << groups.size() << " separate Model .cnj files.\n";
        }
        for (const std::string& w : warnings) { std::cout << "warning: " << w << "\n"; }
    }

    void DumpOracle(const std::filesystem::path& inputPath,
                    const std::filesystem::path& outputDir)
    {
        cgltf_options parseOptions{};
        cgltf_data* data = nullptr;
        cgltf_result result =
            cgltf_parse_file(&parseOptions, inputPath.string().c_str(), &data);
        if (result != cgltf_result_success)
        {
            throw std::runtime_error(
                "cgltf_parse_file failed (code " +
                std::to_string(static_cast<int>(result)) + ") for: " + inputPath.string());
        }
        struct DataGuard { cgltf_data* d; ~DataGuard() { cgltf_free(d); } } guard{data};

        ValidateExternalUriContainmentEXT(data, inputPath.parent_path());
        result = cgltf_load_buffers(&parseOptions, data, inputPath.string().c_str());
        if (result != cgltf_result_success)
        {
            throw std::runtime_error(
                "cgltf_load_buffers failed (code " +
                std::to_string(static_cast<int>(result)) + ").");
        }
        if (data->asset.version != nullptr && std::string(data->asset.version) != "2.0")
        {
            throw std::runtime_error(
                "Unsupported glTF asset.version '" + std::string(data->asset.version) +
                "' -- only glTF 2.0 is supported.");
        }

        std::vector<std::string> validationWarnings;
        ValidateGltfEXT(data, inputPath.string(), validationWarnings);

        std::error_code error;
        const bool outputExists = std::filesystem::exists(outputDir, error);
        if (error)
        {
            throw std::runtime_error("Could not inspect oracle output directory: " +
                                     error.message());
        }
        if (outputExists)
        {
            const bool outputIsDirectory = std::filesystem::is_directory(outputDir, error);
            if (error)
            {
                throw std::runtime_error("Could not inspect oracle output directory: " +
                                         error.message());
            }
            if (!outputIsDirectory)
            {
                throw std::runtime_error("Oracle output exists but is not a directory: " +
                                         outputDir.string());
            }
            const bool outputIsEmpty = std::filesystem::is_empty(outputDir, error);
            if (error)
            {
                throw std::runtime_error("Could not inspect oracle output directory: " +
                                         error.message());
            }
            if (!outputIsEmpty)
            {
                throw std::runtime_error("Oracle output directory must be empty: " +
                                         outputDir.string());
            }
        }
        else
        {
            std::filesystem::create_directories(outputDir, error);
            if (error)
            {
                throw std::runtime_error("Could not create oracle output directory: " +
                                         error.message());
            }
        }

        const std::vector<Oracle::ExtractedPrimitive> primitives =
            Oracle::ExtractSceneMeshesEXT(*data);
        const Oracle::WorldPositions expectedWorld = Oracle::EvaluateWorldPositionsEXT(*data);
        const Oracle::WorldPositions cnaWorld = Oracle::EvaluateCnaWorldPositionsEXT(*data);

        std::ostringstream json;
        json << "{\n"
             << "  \"schema\": \"CNA-gltf-oracle-v1\",\n"
             << "  \"source\": \"" << JsonEscape(inputPath.filename().string()) << "\",\n"
             << "  \"validationWarnings\": [";
        for (std::size_t index = 0; index < validationWarnings.size(); ++index)
        {
            if (index != 0) { json << ','; }
            json << "\"" << JsonEscape(validationWarnings[index]) << "\"";
        }
        json << "],\n  \"l2\": [";
        for (std::size_t index = 0; index < data->accessors_count; ++index)
        {
            if (index != 0) { json << ','; }
            json << Oracle::ToJson(Oracle::DumpAccessorEXT(*data, index));
        }

        json << "],\n  \"l3\": [";
        for (std::size_t index = 0; index < primitives.size(); ++index)
        {
            const Oracle::ExtractedPrimitive& primitive = primitives[index];
            if (index != 0) { json << ','; }
            json << "{\"mesh\":" << primitive.mesh
                 << ",\"primitive\":" << primitive.primitive
                 << ",\"meshName\":\"" << JsonEscape(primitive.meshName)
                 << "\",\"extracted\":" << (primitive.extracted ? "true" : "false")
                 << ",\"error\":\"" << JsonEscape(primitive.error) << "\",\"dump\":"
                 << Oracle::ToJson(primitive.dump) << '}';
        }

        json << "],\n  \"l4\": {\"expected\":" << Oracle::ToJson(expectedWorld)
             << ",\"cna\":" << Oracle::ToJson(cnaWorld) << "},\n"
             << "  \"l5\": [";
        for (std::size_t index = 0; index < primitives.size(); ++index)
        {
            const Oracle::ExtractedPrimitive& primitive = primitives[index];
            if (index != 0) { json << ','; }
            json << "{\"mesh\":" << primitive.mesh
                 << ",\"primitive\":" << primitive.primitive
                 << ",\"extracted\":" << (primitive.extracted ? "true" : "false")
                 << ",\"stride\":" << primitive.dump.stride
                 << ",\"indexElementSize\":"
                 << (primitive.extracted ? (primitive.dump.use32BitIndices ? 4 : 2) : 0)
                 << ",\"vertexByteCount\":" << primitive.vertexBytes.size()
                 << ",\"indexByteCount\":" << primitive.indexBytes.size()
                 << ",\"vertexBytesHex\":\"" << HexBytes(primitive.vertexBytes)
                 << "\",\"indexBytesHex\":\"" << HexBytes(primitive.indexBytes) << "\"}";
        }
        json << "]\n}\n";

        const std::filesystem::path output = outputDir / "oracle.json";
        WriteTextFile(output, json.str());
        std::cout << "Wrote glTF L2-L5 oracle dump to " << output << ".\n";
    }
}

int main(int argc, char** argv)
{
    if (argc == 4 && std::string_view(argv[1]) == "--dump-oracle")
    {
        try
        {
            DumpOracle(argv[2], argv[3]);
        }
        catch (const std::exception& e)
        {
            std::cerr << "error: " << e.what() << "\n";
            return 1;
        }
        return 0;
    }

    if (argc != 4 && argc != 5)
    {
        std::cerr << "Usage: gltf_to_cnj <input.gltf|input.glb> <outputDir> <baseName> [unitScale]\n"
                      "  unitScale: optional multiplier applied to all positions/bone translations\n"
                      "             (default 1.0; glTF mandates meters -- use e.g. 0.01 for a source\n"
                      "             file authored in centimeters).\n"
                      "       gltf_to_cnj --dump-oracle <input.gltf|input.glb> <emptyOutputDir>\n";
        return 1;
    }

    ConvertOptions opts;
    opts.inputPath = argv[1];
    opts.outputDir = argv[2];
    opts.baseName = argv[3];
    if (argc == 5)
    {
        try
        {
            opts.unitScale = std::stof(argv[4]);
        }
        catch (const std::exception&)
        {
            std::cerr << "error: unitScale must be a number, got '" << argv[4] << "'\n";
            return 1;
        }
        if (!(opts.unitScale > 0.0f))
        {
            std::cerr << "error: unitScale must be a positive number.\n";
            return 1;
        }
    }

    try
    {
        Convert(opts);
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
