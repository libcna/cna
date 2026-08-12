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
using Microsoft::Xna::Framework::Vector4;
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
                       float unitScale, std::vector<std::string>& warnings)
    {
        const bool hasSkin = group.skin != nullptr;
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
            int stride; std::string effect; bool vertexColorEnabled;
            // plan_gltf.md GLTF-073: the topology the index buffer is in, by its specification
            // name. Absent from a .cnj written before this, which could only ever hold a triangle
            // list -- so the reader's default is TRIANGLES and an older asset is unaffected.
            std::string primitiveTopology = "TRIANGLES";
            // CNB-59 (Phase 13A): PbrEffect's own 4 maps + factor values.
            std::string normalMapFile, metallicRoughnessMapFile, emissiveMapFile, pbrOcclusionMapFile;
            float metallicFactor = 1.0f, roughnessFactor = 1.0f;
            Vector3 emissiveFactor;
            // plan_gltf.md GLTF-216: baseColorFactor, carried as PbrEffect's own DiffuseColor
            // (RGB) and Alpha (A), which the .cnj reader already consumes for every effect.
            Vector4 baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
            // plan_gltf.md GLTF-228/GLTF-229/GLTF-231: the material's alpha and sidedness state.
            std::string alphaMode = "OPAQUE";
            float alphaCutoff = 0.5f;
            bool doubleSided = false;
            // Morph target CLI/.cnj serialization: morphFile is the binary sidecar path (empty =
            // no morph targets on this primitive), morphWeights are the default blend weights, and
            // morphWeightTrack (optional) is the "weights" animation channel, if any.
            std::string morphFile;
            std::vector<float> morphWeights;
            std::optional<MorphWeightTrackOut> morphWeightTrack;
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
        };
        std::vector<MeshEntry> meshEntries;

        int meshCounter = 0;
        int placementIndex = -1;
        for (const MeshInstanceOut& instance : group.instances)
        {
            const cgltf_mesh* mesh = instance.mesh;
            ++placementIndex;
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

                // plan_gltf.md GLTF-184/GLTF-336: one UV channel means one bakeable transform.
                if (!meshOut.unbakedTextureTransformsEXT.empty())
                {
                    std::string maps;
                    for (const std::string& map : meshOut.unbakedTextureTransformsEXT)
                    {
                        if (!maps.empty()) { maps += ", "; }
                        maps += map;
                    }
                    warnings.push_back(
                        "Primitive '" + partName + "' declares a KHR_texture_transform on " + maps +
                        " that differs from the base colour's; CNA bakes exactly one transform into "
                        "its single UV channel, so those maps are sampled with the base colour's "
                        "coordinates instead of their own.");
                }

                // plan_gltf.md GLTF-173: computed normals that had to be averaged rather than
                // truly flat, because a vertex is shared between faces of different orientation.
                if (meshOut.smoothedNormalVertexCountEXT > 0)
                {
                    warnings.push_back(
                        "Primitive '" + partName + "' authors no NORMAL, so normals were computed "
                        "per glTF 3.7.2.1; " +
                        std::to_string(meshOut.smoothedNormalVertexCountEXT) +
                        " vertex/vertices are shared between faces of different orientation and "
                        "received the area-weighted average instead of a true flat normal, so "
                        "those edges will look smooth rather than sharp.");
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
                entry.baseColorFactor = meshOut.baseColorFactor;
                entry.primitiveTopology = PrimitiveTopologyName(meshOut.topology);
                entry.alphaMode = AlphaModeEXTName(meshOut.alphaMode);
                entry.alphaCutoff = meshOut.alphaCutoff;
                entry.doubleSided = meshOut.doubleSided;
                entry.vertexColorEnabled = meshOut.colored;
                entry.morphFile = morphFile;
                entry.morphWeights = morphWeights;
                entry.morphWeightTrack = morphWeightTrack;
                entry.parentBone = instance.skinned ? 0 : instance.sceneNodeIndex;
                // GLTF-139: only a multi-primitive placement needs the grouping key; a
                // single-primitive one is its own ModelMesh under either rule.
                entry.partOfMesh = mesh->primitives_count > 1 ? placementIndex : -1;
                entry.name = partName;
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
            for (const ClipOut& clip : ExtractClips(data, skeleton, unitScale, warnings))
            {
                clipEntries.push_back({clip.name, writeClip(clip)});
            }
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
        for (const ClipOut& clip : ExtractSceneNodeClips(data, sceneGraph, unitScale, warnings))
        {
            // Model::Tag holds one object, and a skinned model already uses it for SkinningData.
            // A file with both a skin and rigid node animation therefore has nowhere to put these
            // (docs/gltf-api-change-review.md §1.5). Reported by name rather than dropped, which
            // is the property D6 was really about.
            if (hasSkin)
            {
                warnings.push_back(
                    "Clip '" + clip.name + "' animates " + std::to_string(clip.tracks.size()) +
                    " scene node(s), but this model is skinned and its Tag already carries the "
                    "skeleton, so the clip is not written (GLTF-295).");
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
        json << "  \"meshes\": [\n";
        for (std::size_t i = 0; i < meshEntries.size(); ++i)
        {
            const MeshEntry& e = meshEntries[i];
            json << "    { \"name\": \"" << JsonEscape(e.name) << "\""
                 << ", \"vertices\": \"" << JsonEscape(e.vertFile) << "\", \"indices\": \"" << JsonEscape(e.idxFile)
                 << "\", \"vertexStride\": " << e.stride << ", \"effect\": \"" << e.effect << "\""
                 << ", \"parentBone\": " << e.parentBone;
            // GLTF-139: written only for a multi-primitive placement, so every other asset's .cnj
            // is byte-identical to what it was before the field existed.
            if (e.partOfMesh >= 0) { json << ", \"partOfMesh\": " << e.partOfMesh; }
            if (!e.textureFile.empty()) { json << ", \"texture\": \"" << JsonEscape(e.textureFile) << "\""; }
            if (!e.texture2File.empty()) { json << ", \"texture2\": \"" << JsonEscape(e.texture2File) << "\""; }
            if (e.vertexColorEnabled) { json << ", \"vertexColorEnabled\": true"; }
            // Only written when it is not the default, so an ordinary triangle-list asset's .cnj
            // is byte-identical to what it was before GLTF-073.
            if (e.primitiveTopology != "TRIANGLES")
            {
                json << ", \"primitiveTopology\": \"" << JsonEscape(e.primitiveTopology) << "\"";
            }
            if (e.effect == "PbrEffect" || e.effect == "SkinnedPbrEffect")
            {
                if (!e.normalMapFile.empty()) { json << ", \"normalMap\": \"" << JsonEscape(e.normalMapFile) << "\""; }
                if (!e.metallicRoughnessMapFile.empty()) { json << ", \"metallicRoughnessMap\": \"" << JsonEscape(e.metallicRoughnessMapFile) << "\""; }
                if (!e.emissiveMapFile.empty()) { json << ", \"emissiveMap\": \"" << JsonEscape(e.emissiveMapFile) << "\""; }
                if (!e.pbrOcclusionMapFile.empty()) { json << ", \"occlusionMap\": \"" << JsonEscape(e.pbrOcclusionMapFile) << "\""; }
                json << ", \"metallicFactor\": " << e.metallicFactor
                     << ", \"roughnessFactor\": " << e.roughnessFactor
                     << ", \"emissiveFactor\": [" << e.emissiveFactor.X << ", " << e.emissiveFactor.Y << ", " << e.emissiveFactor.Z << "]"
                     // GLTF-216: the base colour reaches the shader through the fields the reader
                     // already understands, so no .cnj schema addition is needed for it.
                     << ", \"diffuseColor\": [" << e.baseColorFactor.X << ", " << e.baseColorFactor.Y
                     << ", " << e.baseColorFactor.Z << "]"
                     << ", \"alpha\": " << e.baseColorFactor.W;
                // Written only when not the glTF default, so an ordinary opaque material's .cnj is
                // byte-identical to what it was before GLTF-228.
                if (e.alphaMode != "OPAQUE") { json << ", \"alphaMode\": \"" << e.alphaMode << "\""; }
                if (e.alphaCutoff != 0.5f)   { json << ", \"alphaCutoff\": " << e.alphaCutoff; }
                if (e.doubleSided)           { json << ", \"doubleSided\": true"; }
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
