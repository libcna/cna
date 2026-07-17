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
// Known, deliberate MVP scope cuts (see GltfImportCore.hpp's own doc comments and plan_cnj.md
// CNB-50/51/55/67/73's notes for the full list): only the base-color and occlusion textures are
// extracted (no normal/metallic-roughness/emissive maps, no PBR factor values); vertex color and
// DualTextureEffect's Texture2 are each mutually exclusive with the other and, for Texture2, with
// skinning.
//
// plan_cnj.md CNB-82/83 (Phase 14C): morph target position/normal deltas (GltfImportCore::
// ExtractMesh always extracts them), the default blend weights, and an optional weight animation
// track are serialized to a per-primitive binary sidecar (BuildMorphBytes) plus "morphTargets"/
// "morphWeights"/"morphWeightTrack" mesh-entry JSON fields, read back by ModelTypeReader::Read()'s
// own .cnj JSON path in ContentManager.cpp (mirroring the runtime glTF path's own MorphTargetDataEXT
// wiring -- see MorphTargetEXT.hpp's own doc comments). Formerly a documented scope cut (CNB-64,
// Phase 13B); now fully serialized through both the offline CLI/.cnj path and the runtime glTF path.

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"

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
#include <unordered_map>
#include <vector>

#include "Microsoft/Xna/Framework/Matrix.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using namespace CNA::Internal::GltfImport;

namespace
{
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
    // Mirrors BinReaderEXT's own byte order (sequential little-endian floats/int32s, native
    // memcpy) already used by the .skeleton.bin/.clip.bin formats in ContentManager.cpp.
    std::vector<std::uint8_t> BuildMorphBytes(const MeshOut& meshOut)
    {
        std::vector<std::uint8_t> out;
        const auto targetCount = meshOut.morphPositionDeltas.size();
        AppendInt32(out, static_cast<std::int32_t>(targetCount));
        for (std::size_t t = 0; t < targetCount; ++t)
        {
            const auto& positions = meshOut.morphPositionDeltas[t];
            AppendInt32(out, static_cast<std::int32_t>(positions.size()));
            for (const Vector3& p : positions)
            {
                AppendFloat(out, p.X); AppendFloat(out, p.Y); AppendFloat(out, p.Z);
            }

            const bool hasNormals = t < meshOut.morphNormalDeltas.size()
                                     && !meshOut.morphNormalDeltas[t].empty();
            AppendInt32(out, hasNormals ? 1 : 0);
            if (hasNormals)
            {
                for (const Vector3& n : meshOut.morphNormalDeltas[t])
                {
                    AppendFloat(out, n.X); AppendFloat(out, n.Y); AppendFloat(out, n.Z);
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

    // ---------------------------------------------------------------------------
    // Per-group conversion + .cnj/binary output.
    // ---------------------------------------------------------------------------

    void ConvertGroup(const cgltf_data* data, const MeshGroup& group, const std::string& outName,
                       const std::filesystem::path& gltfDir, const std::filesystem::path& outputDir,
                       std::unordered_map<const cgltf_image*, std::string>& writtenTextures,
                       std::unordered_map<const cgltf_image*, std::string>& remappedOcclusionTextures,
                       float unitScale, std::vector<std::string>& warnings)
    {
        const bool hasSkin = group.skin != nullptr;
        SkeletonResult skeleton;
        if (hasSkin) { skeleton = BuildSkeleton(group.skin, unitScale); }

        struct MeshEntry {
            std::string vertFile, idxFile, textureFile, texture2File;
            int stride; std::string effect; bool vertexColorEnabled;
            // CNB-59 (Phase 13A): PbrEffect's own 4 maps + factor values.
            std::string normalMapFile, metallicRoughnessMapFile, emissiveMapFile, pbrOcclusionMapFile;
            float metallicFactor = 1.0f, roughnessFactor = 1.0f;
            Vector3 emissiveFactor;
            // Morph target CLI/.cnj serialization: morphFile is the binary sidecar path (empty =
            // no morph targets on this primitive), morphWeights are the default blend weights, and
            // morphWeightTrack (optional) is the "weights" animation channel, if any.
            std::string morphFile;
            std::vector<float> morphWeights;
            std::optional<MorphWeightTrackOut> morphWeightTrack;
        };
        std::vector<MeshEntry> meshEntries;

        int meshCounter = 0;
        for (const cgltf_mesh* mesh : group.meshes)
        {
            for (cgltf_size p = 0; p < mesh->primitives_count; ++p)
            {
                const std::string partName = mesh->name
                    ? (std::string(mesh->name) + (mesh->primitives_count > 1 ? "_" + std::to_string(p) : ""))
                    : ("mesh" + std::to_string(meshCounter));
                MeshOut meshOut = ExtractMesh(data, mesh->primitives[p], partName, hasSkin ? &skeleton : nullptr, unitScale);

                // Morph target CLI/.cnj serialization: write the position/normal deltas to a
                // binary sidecar (BuildMorphBytes) plus the default weights and (if present) the
                // weight animation track, both threaded through MeshEntry into the JSON below.
                std::string morphFile;
                std::vector<float> morphWeights;
                std::optional<MorphWeightTrackOut> morphWeightTrack;
                if (!meshOut.morphPositionDeltas.empty())
                {
                    morphFile = outName + "_mesh" + std::to_string(meshCounter) + "_morph.bin";
                    WriteBinaryFile(outputDir / morphFile, BuildMorphBytes(meshOut));

                    const std::size_t targetCount = meshOut.morphPositionDeltas.size();
                    morphWeights = GetMeshDefaultWeights(mesh, targetCount);
                    morphWeightTrack = ExtractMorphWeightTrack(data, mesh, targetCount);
                }

                // Per-map UV set selection: PbrEffect/SkinnedPbrEffect sample every map from a
                // single shared UV channel (the base-color texture's own TEXCOORD set); warn
                // rather than silently mis-rendering when another map references a different one.
                if (meshOut.pbrUv2Mismatch)
                {
                    warnings.push_back(
                        "Primitive '" + partName + "' has a PBR map (normal/metallic-roughness/"
                        "emissive/occlusion) that references a different glTF TEXCOORD set than "
                        "the base-color texture -- that map will be sampled with the wrong UV "
                        "data (CNA currently samples every PBR map from one shared UV channel).");
                }

                const std::string vertFile = outName + "_mesh" + std::to_string(meshCounter) + "_verts.bin";
                const std::string idxFile  = outName + "_mesh" + std::to_string(meshCounter) + "_idx.bin";
                WriteBinaryFile(outputDir / vertFile, meshOut.vertexBytes);
                WriteBinaryFile(outputDir / idxFile, meshOut.indexBytes);

                std::string textureFile;
                if (meshOut.baseColorImage)
                {
                    auto cached = writtenTextures.find(meshOut.baseColorImage);
                    if (cached != writtenTextures.end())
                    {
                        textureFile = cached->second;
                    }
                    else if (auto img = ExtractImage(meshOut.baseColorImage, gltfDir))
                    {
                        textureFile = outName + "_tex" + std::to_string(writtenTextures.size()) + "." + img->extension;
                        WriteBinaryFile(outputDir / textureFile, img->bytes);
                        writtenTextures[meshOut.baseColorImage] = textureFile;
                    }
                }

                std::string texture2File;
                if (meshOut.useDualTexture && meshOut.occlusionImage)
                {
                    // CNB-88 (Phase 14E): DualTextureEffect's own occlusion-as-lightmap blend
                    // expects "0.5 = neutral", not glTF's own real "1.0 = fully visible"
                    // occlusion convention (see RemapOcclusionImageForDualTextureEXT's own doc
                    // comment for the full derivation) -- decode/halve/re-encode before writing,
                    // fixing the ~2x-too-bright approximation. Cached separately from
                    // writtenTextures: the SAME image could in principle also be referenced,
                    // unmodified, as a different primitive's PbrEffect::OcclusionMap elsewhere in
                    // this same file, and that must not observe the remapped bytes (or vice versa).
                    auto cached = remappedOcclusionTextures.find(meshOut.occlusionImage);
                    if (cached != remappedOcclusionTextures.end())
                    {
                        texture2File = cached->second;
                    }
                    else if (auto img = ExtractImage(meshOut.occlusionImage, gltfDir))
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
                        remappedOcclusionTextures[meshOut.occlusionImage] = texture2File;
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
                std::string normalMapFile, metallicRoughnessMapFile, emissiveMapFile, pbrOcclusionMapFile;
                if (meshOut.usePbr)
                {
                    normalMapFile            = extractCached(meshOut.normalImage);
                    metallicRoughnessMapFile = extractCached(meshOut.metallicRoughnessImage);
                    emissiveMapFile          = extractCached(meshOut.emissiveImage);
                    pbrOcclusionMapFile      = extractCached(meshOut.occlusionImage);
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
                entry.metallicFactor = meshOut.metallicFactor;
                entry.roughnessFactor = meshOut.roughnessFactor;
                entry.emissiveFactor = meshOut.emissiveFactor;
                entry.vertexColorEnabled = meshOut.colored;
                entry.morphFile = morphFile;
                entry.morphWeights = morphWeights;
                entry.morphWeightTrack = morphWeightTrack;
                meshEntries.push_back(entry);
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

            skeletonFile = outName + ".skeleton.bin";
            WriteBinaryFile(outputDir / skeletonFile, skelBytes);
        }

        struct ClipEntry { std::string name, cnjFile; };
        std::vector<ClipEntry> clipEntries;
        if (hasSkin)
        {
            std::vector<ClipOut> clips = ExtractClips(data, skeleton, unitScale, warnings);
            for (const ClipOut& clip : clips)
            {
                std::ostringstream json;
                json << "{\n  \"cnjVersion\": 1,\n  \"type\": \"AnimationClip\",\n  \"duration\": "
                     << clip.duration << ",\n  \"tracks\": [\n";
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
                clipEntries.push_back({clip.name, cnjFile});
            }
        }

        std::ostringstream json;
        json << "{\n  \"cnjVersion\": 1,\n  \"type\": \"Model\",\n";
        if (!skeletonFile.empty())
        {
            json << "  \"skeleton\": \"" << JsonEscape(skeletonFile) << "\",\n";
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
        json << "  \"meshes\": [\n";
        for (std::size_t i = 0; i < meshEntries.size(); ++i)
        {
            const MeshEntry& e = meshEntries[i];
            json << "    { \"vertices\": \"" << JsonEscape(e.vertFile) << "\", \"indices\": \"" << JsonEscape(e.idxFile)
                 << "\", \"vertexStride\": " << e.stride << ", \"effect\": \"" << e.effect << "\"";
            if (!e.textureFile.empty()) { json << ", \"texture\": \"" << JsonEscape(e.textureFile) << "\""; }
            if (!e.texture2File.empty()) { json << ", \"texture2\": \"" << JsonEscape(e.texture2File) << "\""; }
            if (e.vertexColorEnabled) { json << ", \"vertexColorEnabled\": true"; }
            if (e.effect == "PbrEffect" || e.effect == "SkinnedPbrEffect")
            {
                if (!e.normalMapFile.empty()) { json << ", \"normalMap\": \"" << JsonEscape(e.normalMapFile) << "\""; }
                if (!e.metallicRoughnessMapFile.empty()) { json << ", \"metallicRoughnessMap\": \"" << JsonEscape(e.metallicRoughnessMapFile) << "\""; }
                if (!e.emissiveMapFile.empty()) { json << ", \"emissiveMap\": \"" << JsonEscape(e.emissiveMapFile) << "\""; }
                if (!e.pbrOcclusionMapFile.empty()) { json << ", \"occlusionMap\": \"" << JsonEscape(e.pbrOcclusionMapFile) << "\""; }
                json << ", \"metallicFactor\": " << e.metallicFactor
                     << ", \"roughnessFactor\": " << e.roughnessFactor
                     << ", \"emissiveFactor\": [" << e.emissiveFactor.X << ", " << e.emissiveFactor.Y << ", " << e.emissiveFactor.Z << "]";
            }
            if (!e.morphFile.empty())
            {
                json << ", \"morphTargets\": \"" << JsonEscape(e.morphFile) << "\", \"morphWeights\": [";
                for (std::size_t w = 0; w < e.morphWeights.size(); ++w)
                {
                    json << e.morphWeights[w] << (w + 1 < e.morphWeights.size() ? ", " : "");
                }
                json << "]";
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
                  << meshEntries.size() << " mesh part(s), "
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
        std::vector<MeshGroup> groups = CollectMeshGroups(data);
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
            ConvertGroup(data, groups[g], outName, gltfDir, opts.outputDir, writtenTextures,
                         remappedOcclusionTextures, opts.unitScale, warnings);
        }

        if (groups.size() > 1)
        {
            std::cout << "File had " << groups.size() << " mesh groups (by skin) -- wrote "
                      << groups.size() << " separate Model .cnj files.\n";
        }
        for (const std::string& w : warnings) { std::cout << "warning: " << w << "\n"; }
    }
}

int main(int argc, char** argv)
{
    if (argc != 4 && argc != 5)
    {
        std::cerr << "Usage: gltf_to_cnj <input.gltf|input.glb> <outputDir> <baseName> [unitScale]\n"
                      "  unitScale: optional multiplier applied to all positions/bone translations\n"
                      "             (default 1.0; glTF mandates meters -- use e.g. 0.01 for a source\n"
                      "             file authored in centimeters).\n";
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
