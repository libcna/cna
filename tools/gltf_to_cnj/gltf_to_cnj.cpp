// SPDX-License-Identifier: MS-PL
//
// plan_cnj.md CNB-50/51/52 (Phase 12): offline glTF 2.0 -> .cnj Model/AnimationClip converter.
// Reads a .gltf/.glb file via cgltf and writes a Model .cnj (meshes + optional "skeleton"/
// "animations" fields, Task 941's existing shape) plus vertex/index/skeleton binary sidecars and
// one standalone, shareable .cnj AnimationClip file per animation clip (CNB-48). Targets CNA's
// general-purpose Model/AnimationClip path, not the separate Avatar-specific SkinnedModelEXT/
// .skinnedmodel.json system (see ../../gltf.md, which analyzes that different target).
//
// Carries forward the two real bugs tools/avatar_asset_pipeline/convert_avatar.py already found
// fixing the same underlying problem for a different output format: (1) glTF's skin.joints order
// is not guaranteed topological (parent-before-child); SkinningData::SkeletonHierarchy and
// AnimationPlayer::ComputeBoneTransformsEXT both require it, so every joint-indexed value (bind
// pose, inverse bind pose, vertex JOINTS_0, animation channel targets) is remapped through a
// topological reorder before being written out. (2) glTF permits primitives with no "indices"
// accessor (implicit sequential order) -- Khronos's own "Fox" sample has one; handled by
// synthesizing a sequential index buffer instead of assuming indices are always present.
//
// Known, deliberate MVP scope cuts (see plan_cnj.md CNB-50's own notes for the full list):
// materials/textures are not extracted (meshes always use "BasicEffect"/"SkinnedEffect" with no
// texture); only the first skin in the file is used; sparse accessors are not supported (cgltf's
// own cgltf_accessor_read_float returns false for them, and this tool throws rather than silently
// reading zeros); CUBICSPLINE animation channels use only the value component of each keyframe
// triplet (in/out tangents are read but discarded), which is valid but not perfectly smooth.

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <algorithm>
#include <cmath>
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
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/TimeSpan.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector3;

namespace
{
    // ---------------------------------------------------------------------------
    // Small helpers: binary writers, JSON string escaping, glTF <-> XNA math.
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

    void AppendUint16(std::vector<std::uint8_t>& out, std::uint16_t v)
    {
        std::uint8_t bytes[2];
        std::memcpy(bytes, &v, 2);
        out.insert(out.end(), bytes, bytes + 2);
    }

    void AppendUint32(std::vector<std::uint8_t>& out, std::uint32_t v)
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

    // glTF matrices (raw node.matrix, inverseBindMatrices) are a flat 16-float array in
    // column-major order with column-vector convention (v' = M * v); XNA's Matrix is row-major
    // with row-vector convention (v' = v * M) -- numerically the transpose of the same transform.
    // Converting by copying basis vectors directly (rather than a generic transpose) since every
    // matrix this tool converts is a plain affine transform (no projective/shear component).
    Matrix ConvertGltfMatrix(const float g[16])
    {
        return Matrix(
            g[0], g[1], g[2], 0.0f,
            g[4], g[5], g[6], 0.0f,
            g[8], g[9], g[10], 0.0f,
            g[12], g[13], g[14], 1.0f);
    }

    void ReadFloatOrThrow(const cgltf_accessor* accessor, cgltf_size index, float* out,
                           cgltf_size elementSize, const char* context)
    {
        if (!cgltf_accessor_read_float(accessor, index, out, elementSize))
        {
            throw std::runtime_error(
                std::string("Failed to read accessor element (") + context +
                ") -- possibly a sparse accessor, which this tool does not support.");
        }
    }

    // ---------------------------------------------------------------------------
    // Skeleton: topologically reorder a skin's joints (glTF's own skin.joints order is not
    // guaranteed parent-before-child; SkinningData::SkeletonHierarchy and
    // AnimationPlayer::ComputeBoneTransformsEXT both require it).
    // ---------------------------------------------------------------------------

    struct BoneOut
    {
        std::string name;
        int parentIndex = -1;
        Matrix bindPoseLocal = Matrix::getIdentityProperty();
        Matrix inverseBindGlobal = Matrix::getIdentityProperty();
    };

    struct SkeletonResult
    {
        std::vector<BoneOut> bones; // in new (topological) order
        std::vector<int> oldToNew;  // indexed by original skin.joints[] index
        std::unordered_map<const cgltf_node*, int> nodeToNewIndex;
    };

    SkeletonResult BuildSkeleton(const cgltf_skin* skin)
    {
        SkeletonResult result;
        const std::size_t n = skin->joints_count;
        if (n == 0) { return result; }

        std::vector<const cgltf_node*> oldNodes(n);
        for (std::size_t i = 0; i < n; ++i) { oldNodes[i] = skin->joints[i]; }

        std::unordered_map<const cgltf_node*, int> nodeToOldIndex;
        nodeToOldIndex.reserve(n);
        for (std::size_t i = 0; i < n; ++i) { nodeToOldIndex[oldNodes[i]] = static_cast<int>(i); }

        std::vector<int> oldParentOfOld(n, -1);
        for (std::size_t i = 0; i < n; ++i)
        {
            const cgltf_node* p = oldNodes[i]->parent;
            auto it = nodeToOldIndex.find(p);
            oldParentOfOld[i] = (it != nodeToOldIndex.end()) ? it->second : -1;
        }

        std::vector<std::vector<int>> childrenOfOld(n);
        std::vector<int> queue;
        for (std::size_t i = 0; i < n; ++i)
        {
            if (oldParentOfOld[i] == -1) { queue.push_back(static_cast<int>(i)); }
            else { childrenOfOld[static_cast<std::size_t>(oldParentOfOld[i])].push_back(static_cast<int>(i)); }
        }

        std::vector<int> newOrderOldIndices;
        newOrderOldIndices.reserve(n);
        for (std::size_t qi = 0; qi < queue.size(); ++qi)
        {
            const int oldIdx = queue[qi];
            newOrderOldIndices.push_back(oldIdx);
            for (int c : childrenOfOld[static_cast<std::size_t>(oldIdx)]) { queue.push_back(c); }
        }
        if (newOrderOldIndices.size() != n)
        {
            throw std::runtime_error(
                "Skin joint hierarchy is inconsistent (a joint's parent chain does not resolve to "
                "a root within the skin's own joint set).");
        }

        result.oldToNew.assign(n, -1);
        for (std::size_t newIdx = 0; newIdx < n; ++newIdx)
        {
            result.oldToNew[static_cast<std::size_t>(newOrderOldIndices[newIdx])] = static_cast<int>(newIdx);
        }

        result.bones.resize(n);
        for (std::size_t newIdx = 0; newIdx < n; ++newIdx)
        {
            const int oldIdx = newOrderOldIndices[newIdx];
            const cgltf_node* node = oldNodes[static_cast<std::size_t>(oldIdx)];

            BoneOut bone;
            bone.name = node->name ? node->name : ("Bone" + std::to_string(newIdx));
            const int oldParent = oldParentOfOld[static_cast<std::size_t>(oldIdx)];
            bone.parentIndex = (oldParent == -1) ? -1 : result.oldToNew[static_cast<std::size_t>(oldParent)];

            float localMat[16];
            cgltf_node_transform_local(node, localMat);
            bone.bindPoseLocal = ConvertGltfMatrix(localMat);

            if (skin->inverse_bind_matrices)
            {
                float ibm[16];
                ReadFloatOrThrow(skin->inverse_bind_matrices, static_cast<cgltf_size>(oldIdx), ibm, 16,
                                  "inverseBindMatrices");
                bone.inverseBindGlobal = ConvertGltfMatrix(ibm);
            }

            result.bones[newIdx] = bone;
            result.nodeToNewIndex[node] = static_cast<int>(newIdx);
        }

        return result;
    }

    // ---------------------------------------------------------------------------
    // Animation: glTF stores translation/rotation/scale as separate channels, each with its own
    // keyframe times; CNA's KeyframeEXT bundles all three per keyframe. Resample: union the
    // distinct times across whichever of a bone's T/R/S channels exist, then interpolate each
    // channel at every such time (falling back to the bone's own bind-pose component for a
    // channel that doesn't exist at all, rather than an unrelated identity/zero default).
    // ---------------------------------------------------------------------------

    struct KeyframeOut
    {
        double time = 0.0;
        Vector3 translation;
        Quaternion rotation = Quaternion::Identity;
        Vector3 scale{1.0f, 1.0f, 1.0f};
    };

    struct TrackOut
    {
        int boneIndex = -1;
        std::vector<KeyframeOut> keys;
    };

    struct ClipOut
    {
        std::string name;
        double duration = 0.0;
        std::vector<TrackOut> tracks;
    };

    std::vector<double> ReadTimes(const cgltf_accessor* input, const char* context)
    {
        std::vector<double> times(input->count);
        for (cgltf_size i = 0; i < input->count; ++i)
        {
            float t = 0.0f;
            ReadFloatOrThrow(input, i, &t, 1, context);
            times[i] = static_cast<double>(t);
        }
        return times;
    }

    // Finds the bracketing pair [lo, lo+1] in a sorted, ascending time array such that
    // times[lo] <= t <= times[lo+1] (clamped at the ends), returning lo and the interpolation
    // fraction within that bracket.
    void FindBracket(const std::vector<double>& times, double t, std::size_t& lo, float& amount)
    {
        if (t <= times.front()) { lo = 0; amount = 0.0f; return; }
        if (t >= times.back()) { lo = times.size() - 1; amount = 0.0f; return; }
        for (std::size_t i = 0; i + 1 < times.size(); ++i)
        {
            if (t >= times[i] && t <= times[i + 1])
            {
                lo = i;
                const double span = times[i + 1] - times[i];
                amount = span > 0.0 ? static_cast<float>((t - times[i]) / span) : 0.0f;
                return;
            }
        }
        lo = times.size() - 1;
        amount = 0.0f;
    }

    Vector3 ReadVec3Sample(const cgltf_animation_channel& ch, std::size_t index)
    {
        float v[3] = {0, 0, 0};
        const cgltf_size outIndex = (ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline)
                                         ? static_cast<cgltf_size>(index) * 3 + 1
                                         : static_cast<cgltf_size>(index);
        ReadFloatOrThrow(ch.sampler->output, outIndex, v, 3, "vec3 channel output");
        return Vector3(v[0], v[1], v[2]);
    }

    Quaternion ReadQuatSample(const cgltf_animation_channel& ch, std::size_t index)
    {
        float v[4] = {0, 0, 0, 1};
        const cgltf_size outIndex = (ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline)
                                         ? static_cast<cgltf_size>(index) * 3 + 1
                                         : static_cast<cgltf_size>(index);
        ReadFloatOrThrow(ch.sampler->output, outIndex, v, 4, "quat channel output");
        return Quaternion(v[0], v[1], v[2], v[3]);
    }

    Vector3 EvaluateVec3Channel(const cgltf_animation_channel* ch, const std::vector<double>* times,
                                 double t, Vector3 fallback)
    {
        if (ch == nullptr) { return fallback; }
        std::size_t lo = 0; float amount = 0.0f;
        FindBracket(*times, t, lo, amount);
        if (amount <= 0.0f || ch->sampler->interpolation == cgltf_interpolation_type_step || lo + 1 >= times->size())
        {
            return ReadVec3Sample(*ch, lo);
        }
        return Vector3::Lerp(ReadVec3Sample(*ch, lo), ReadVec3Sample(*ch, lo + 1), amount);
    }

    Quaternion EvaluateQuatChannel(const cgltf_animation_channel* ch, const std::vector<double>* times,
                                    double t, Quaternion fallback)
    {
        if (ch == nullptr) { return fallback; }
        std::size_t lo = 0; float amount = 0.0f;
        FindBracket(*times, t, lo, amount);
        if (amount <= 0.0f || ch->sampler->interpolation == cgltf_interpolation_type_step || lo + 1 >= times->size())
        {
            return ReadQuatSample(*ch, lo);
        }
        return Quaternion::Slerp(ReadQuatSample(*ch, lo), ReadQuatSample(*ch, lo + 1), amount);
    }

    std::vector<ClipOut> ExtractClips(const cgltf_data* data, const SkeletonResult& skel,
                                       std::vector<std::string>& warnings)
    {
        std::vector<ClipOut> clips;

        for (cgltf_size a = 0; a < data->animations_count; ++a)
        {
            const cgltf_animation& anim = data->animations[a];

            struct BoneChannels
            {
                const cgltf_animation_channel* translation = nullptr;
                const cgltf_animation_channel* rotation = nullptr;
                const cgltf_animation_channel* scale = nullptr;
            };
            std::unordered_map<int, BoneChannels> byBone;
            double maxTime = 0.0;
            bool sawCubicSpline = false;
            bool sawUnsupportedTarget = false;

            for (cgltf_size c = 0; c < anim.channels_count; ++c)
            {
                const cgltf_animation_channel& ch = anim.channels[c];
                auto it = skel.nodeToNewIndex.find(ch.target_node);
                if (it == skel.nodeToNewIndex.end()) { continue; } // targets a non-joint node -- skip
                const int boneIdx = it->second;

                if (ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline) { sawCubicSpline = true; }

                if (ch.target_path == cgltf_animation_path_type_translation) { byBone[boneIdx].translation = &ch; }
                else if (ch.target_path == cgltf_animation_path_type_rotation) { byBone[boneIdx].rotation = &ch; }
                else if (ch.target_path == cgltf_animation_path_type_scale) { byBone[boneIdx].scale = &ch; }
                else { sawUnsupportedTarget = true; continue; } // e.g. morph target weights

                if (ch.sampler->input->count > 0)
                {
                    float lastTime = 0.0f;
                    ReadFloatOrThrow(ch.sampler->input, ch.sampler->input->count - 1, &lastTime, 1, "sampler input");
                    maxTime = std::max(maxTime, static_cast<double>(lastTime));
                }
            }

            if (sawCubicSpline)
            {
                warnings.push_back(
                    "Clip '" + std::string(anim.name ? anim.name : "") +
                    "' uses CUBICSPLINE interpolation on at least one channel -- only the sampled "
                    "value is used (in/out tangents are discarded), which is valid but not "
                    "perfectly smooth.");
            }
            if (sawUnsupportedTarget)
            {
                warnings.push_back(
                    "Clip '" + std::string(anim.name ? anim.name : "") +
                    "' targets a channel path this tool does not import (e.g. morph target "
                    "weights) -- skipped.");
            }

            ClipOut clip;
            clip.name = anim.name ? anim.name : ("Clip" + std::to_string(a));
            clip.duration = maxTime;

            for (auto& [boneIdx, channels] : byBone)
            {
                std::optional<std::vector<double>> tTimes, rTimes, sTimes;
                if (channels.translation) { tTimes = ReadTimes(channels.translation->sampler->input, "translation"); }
                if (channels.rotation) { rTimes = ReadTimes(channels.rotation->sampler->input, "rotation"); }
                if (channels.scale) { sTimes = ReadTimes(channels.scale->sampler->input, "scale"); }

                std::vector<double> unionTimes;
                for (const auto* times : {&tTimes, &rTimes, &sTimes})
                {
                    if (*times) { unionTimes.insert(unionTimes.end(), times->value().begin(), times->value().end()); }
                }
                if (unionTimes.empty()) { continue; }
                std::sort(unionTimes.begin(), unionTimes.end());
                unionTimes.erase(
                    std::unique(unionTimes.begin(), unionTimes.end(),
                                [](double x, double y) { return std::fabs(x - y) < 1e-9; }),
                    unionTimes.end());

                Vector3 bindScale{1.0f, 1.0f, 1.0f};
                Quaternion bindRotation = Quaternion::Identity;
                Vector3 bindTranslation;
                (void)skel.bones[static_cast<std::size_t>(boneIdx)].bindPoseLocal.Decompose(
                    bindScale, bindRotation, bindTranslation);

                TrackOut track;
                track.boneIndex = boneIdx;
                track.keys.reserve(unionTimes.size());
                for (double t : unionTimes)
                {
                    KeyframeOut key;
                    key.time = t;
                    key.translation = EvaluateVec3Channel(channels.translation, tTimes ? &*tTimes : nullptr, t, bindTranslation);
                    key.rotation = EvaluateQuatChannel(channels.rotation, rTimes ? &*rTimes : nullptr, t, bindRotation);
                    key.scale = EvaluateVec3Channel(channels.scale, sTimes ? &*sTimes : nullptr, t, bindScale);
                    track.keys.push_back(key);
                }
                clip.tracks.push_back(std::move(track));
            }

            clips.push_back(std::move(clip));
        }

        return clips;
    }

    // ---------------------------------------------------------------------------
    // Mesh geometry: handles both indexed and non-indexed primitives (Khronos's own "Fox" sample
    // has the latter -- gltf.md's own already-found bug), and both the unskinned stride-32
    // (VertexPositionNormalTexture) and skinned stride-52 (VertexPositionNormalTextureSkinned,
    // pos+normal+uv+blendweight(4f)+blendindices(4x u8) = 52 bytes) layouts ModelTypeReader
    // already supports.
    // ---------------------------------------------------------------------------

    struct MeshOut
    {
        std::string name;
        std::vector<std::uint8_t> vertexBytes;
        int stride = 32;
        std::vector<std::uint8_t> indexBytes;
        bool use32BitIndices = false;
        bool skinned = false;
    };

    MeshOut ExtractMesh(const cgltf_primitive& prim, const std::string& name, const SkeletonResult* skel)
    {
        const cgltf_accessor* posAcc = cgltf_find_accessor(&prim, cgltf_attribute_type_position, 0);
        if (!posAcc) { throw std::runtime_error("Primitive '" + name + "' has no POSITION attribute."); }
        const cgltf_accessor* normAcc = cgltf_find_accessor(&prim, cgltf_attribute_type_normal, 0);
        const cgltf_accessor* uvAcc = cgltf_find_accessor(&prim, cgltf_attribute_type_texcoord, 0);
        const cgltf_accessor* jointsAcc = skel ? cgltf_find_accessor(&prim, cgltf_attribute_type_joints, 0) : nullptr;
        const cgltf_accessor* weightsAcc = skel ? cgltf_find_accessor(&prim, cgltf_attribute_type_weights, 0) : nullptr;

        MeshOut out;
        out.name = name;
        out.skinned = (jointsAcc != nullptr) && (weightsAcc != nullptr);
        out.stride = out.skinned ? 52 : 32;

        const cgltf_size vertexCount = posAcc->count;
        out.vertexBytes.reserve(static_cast<std::size_t>(vertexCount) * static_cast<std::size_t>(out.stride));

        for (cgltf_size i = 0; i < vertexCount; ++i)
        {
            float pos[3] = {0, 0, 0};
            ReadFloatOrThrow(posAcc, i, pos, 3, "POSITION");
            float norm[3] = {0, 0, 1};
            if (normAcc) { ReadFloatOrThrow(normAcc, i, norm, 3, "NORMAL"); }
            float uv[2] = {0, 0};
            if (uvAcc) { ReadFloatOrThrow(uvAcc, i, uv, 2, "TEXCOORD_0"); }

            AppendFloat(out.vertexBytes, pos[0]);  AppendFloat(out.vertexBytes, pos[1]);  AppendFloat(out.vertexBytes, pos[2]);
            AppendFloat(out.vertexBytes, norm[0]); AppendFloat(out.vertexBytes, norm[1]); AppendFloat(out.vertexBytes, norm[2]);
            AppendFloat(out.vertexBytes, uv[0]);   AppendFloat(out.vertexBytes, uv[1]);

            if (out.skinned)
            {
                float weights[4] = {0, 0, 0, 0};
                ReadFloatOrThrow(weightsAcc, i, weights, 4, "WEIGHTS_0");
                AppendFloat(out.vertexBytes, weights[0]); AppendFloat(out.vertexBytes, weights[1]);
                AppendFloat(out.vertexBytes, weights[2]); AppendFloat(out.vertexBytes, weights[3]);

                float jointsF[4] = {0, 0, 0, 0};
                ReadFloatOrThrow(jointsAcc, i, jointsF, 4, "JOINTS_0");
                for (int k = 0; k < 4; ++k)
                {
                    const int oldJointIdx = static_cast<int>(jointsF[k] + 0.5f);
                    int newJointIdx = 0;
                    if (oldJointIdx >= 0 && static_cast<std::size_t>(oldJointIdx) < skel->oldToNew.size())
                    {
                        newJointIdx = skel->oldToNew[static_cast<std::size_t>(oldJointIdx)];
                    }
                    out.vertexBytes.push_back(static_cast<std::uint8_t>(newJointIdx));
                }
            }
        }

        const cgltf_size indexCount = prim.indices ? prim.indices->count : vertexCount;
        out.use32BitIndices = vertexCount > 65535;
        out.indexBytes.reserve(static_cast<std::size_t>(indexCount) *
                                (out.use32BitIndices ? sizeof(std::uint32_t) : sizeof(std::uint16_t)));

        for (cgltf_size i = 0; i < indexCount; ++i)
        {
            // Non-indexed primitive (prim.indices == nullptr): implicit sequential vertex order
            // per the glTF spec -- Khronos's own "Fox" sample has exactly this shape.
            const cgltf_size v = prim.indices ? cgltf_accessor_read_index(prim.indices, i) : i;
            if (out.use32BitIndices) { AppendUint32(out.indexBytes, static_cast<std::uint32_t>(v)); }
            else { AppendUint16(out.indexBytes, static_cast<std::uint16_t>(v)); }
        }

        return out;
    }

    // ---------------------------------------------------------------------------
    // Top-level conversion + .cnj/binary output.
    // ---------------------------------------------------------------------------

    struct ConvertOptions
    {
        std::filesystem::path inputPath;
        std::filesystem::path outputDir;
        std::string baseName;
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

        const bool hasSkin = data->skins_count > 0;
        if (data->skins_count > 1)
        {
            warnings.push_back(
                "File has " + std::to_string(data->skins_count) + " skins; only the first is imported.");
        }

        SkeletonResult skeleton;
        if (hasSkin) { skeleton = BuildSkeleton(&data->skins[0]); }

        std::filesystem::create_directories(opts.outputDir);

        // --- Meshes ---
        struct MeshEntry { std::string vertFile, idxFile; int stride; std::string effect; };
        std::vector<MeshEntry> meshEntries;

        int meshCounter = 0;
        for (cgltf_size m = 0; m < data->meshes_count; ++m)
        {
            const cgltf_mesh& mesh = data->meshes[m];
            for (cgltf_size p = 0; p < mesh.primitives_count; ++p)
            {
                const std::string partName = mesh.name ? (std::string(mesh.name) + (mesh.primitives_count > 1 ? "_" + std::to_string(p) : ""))
                                                         : ("mesh" + std::to_string(meshCounter));
                MeshOut meshOut = ExtractMesh(mesh.primitives[p], partName, hasSkin ? &skeleton : nullptr);

                const std::string vertFile = opts.baseName + "_mesh" + std::to_string(meshCounter) + "_verts.bin";
                const std::string idxFile  = opts.baseName + "_mesh" + std::to_string(meshCounter) + "_idx.bin";
                WriteBinaryFile(opts.outputDir / vertFile, meshOut.vertexBytes);
                WriteBinaryFile(opts.outputDir / idxFile, meshOut.indexBytes);

                MeshEntry entry;
                entry.vertFile = vertFile;
                entry.idxFile = idxFile;
                entry.stride = meshOut.stride;
                entry.effect = meshOut.skinned ? "SkinnedEffect" : "BasicEffect";
                meshEntries.push_back(entry);
                ++meshCounter;
            }
        }
        if (meshEntries.empty())
        {
            throw std::runtime_error("File contains no mesh primitives to import.");
        }

        // --- Skeleton ---
        std::string skeletonFile;
        if (hasSkin)
        {
            std::vector<std::uint8_t> skelBytes;
            AppendInt32(skelBytes, static_cast<std::int32_t>(skeleton.bones.size()));
            for (const BoneOut& b : skeleton.bones) { AppendInt32(skelBytes, b.parentIndex); }
            for (const BoneOut& b : skeleton.bones) { AppendMatrix(skelBytes, b.bindPoseLocal); }
            for (const BoneOut& b : skeleton.bones) { AppendMatrix(skelBytes, b.inverseBindGlobal); }

            skeletonFile = opts.baseName + ".skeleton.bin";
            WriteBinaryFile(opts.outputDir / skeletonFile, skelBytes);
        }

        // --- Animation clips (standalone, shareable .cnj AnimationClip assets -- CNB-48) ---
        struct ClipEntry { std::string name, cnjFile; };
        std::vector<ClipEntry> clipEntries;
        if (hasSkin)
        {
            std::vector<ClipOut> clips = ExtractClips(data, skeleton, warnings);
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

                const std::string cnjFile = opts.baseName + "_" + JsonEscape(clip.name) + ".cnj";
                WriteTextFile(opts.outputDir / cnjFile, json.str());
                clipEntries.push_back({clip.name, cnjFile});
            }
        }

        // --- Top-level Model .cnj ---
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
                 << "\", \"vertexStride\": " << e.stride << ", \"effect\": \"" << e.effect << "\" }"
                 << (i + 1 < meshEntries.size() ? "," : "") << "\n";
        }
        json << "  ]\n}\n";

        WriteTextFile(opts.outputDir / (opts.baseName + ".cnj"), json.str());

        std::cout << "Wrote " << opts.outputDir / (opts.baseName + ".cnj") << " ("
                  << meshEntries.size() << " mesh part(s), "
                  << (hasSkin ? std::to_string(skeleton.bones.size()) + " bones, " : std::string("no skeleton, "))
                  << clipEntries.size() << " clip(s)).\n";
        for (const std::string& w : warnings) { std::cout << "warning: " << w << "\n"; }
    }
}

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        std::cerr << "Usage: gltf_to_cnj <input.gltf|input.glb> <outputDir> <baseName>\n";
        return 1;
    }

    ConvertOptions opts;
    opts.inputPath = argv[1];
    opts.outputDir = argv[2];
    opts.baseName = argv[3];

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
