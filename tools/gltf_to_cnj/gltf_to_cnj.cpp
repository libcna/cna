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
// Carries forward the two real bugs tools/avatar_asset_pipeline/convert_avatar.py already found
// fixing the same underlying problem for a different output format: (1) glTF's skin.joints order
// is not guaranteed topological (parent-before-child); SkinningData::SkeletonHierarchy and
// AnimationPlayer::ComputeBoneTransformsEXT both require it, so every joint-indexed value (bind
// pose, inverse bind pose, vertex JOINTS_0, animation channel targets) is remapped through a
// topological reorder before being written out. (2) glTF permits primitives with no "indices"
// accessor (implicit sequential order) -- Khronos's own "Fox" sample has one; handled by
// synthesizing a sequential index buffer instead of assuming indices are always present.
//
// All accessor reads go through cgltf_accessor_unpack_floats (never the per-element
// cgltf_accessor_read_float), which resolves sparse accessors (base values + sparse overrides)
// correctly -- cgltf_accessor_read_float rejects sparse accessors outright.
//
// Meshes are grouped by which skin (if any) their scene node references -- a file with multiple
// skins produces multiple Model .cnj outputs (<baseName>_<skinName>.cnj), not just the first skin;
// unskinned mesh instances form their own "<baseName>_static" group when any skinned group also
// exists, or plain "<baseName>.cnj" when there is exactly one group in the whole file (preserves
// the original single-model naming for the common case).
//
// Each primitive's material base-color texture (embedded bufferView, external file, or base64
// data: URI) is extracted and written out as a real image file, wired into the mesh's own "texture"
// field -- reuses Texture2DTypeReader's existing native-extension loading, no CNA core change.
//
// Known, deliberate MVP scope cuts (see plan_cnj.md CNB-50/51's own notes for the full list): only
// the base-color texture is extracted (no normal/metallic-roughness/emissive/occlusion maps, no
// PBR factor values); CUBICSPLINE animation channels use only the value component of each keyframe
// triplet (in/out tangents are read but discarded), which is valid but not perfectly smooth.

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
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

    // Unpacks an entire accessor to floats in one call. Unlike per-element
    // cgltf_accessor_read_float, cgltf_accessor_unpack_floats correctly resolves sparse accessors
    // (base values overlaid with sparse overrides) -- read_float rejects sparse accessors outright.
    std::vector<float> UnpackAccessor(const cgltf_accessor* accessor, cgltf_size expectedComponents,
                                       const char* context)
    {
        const cgltf_size actualComponents = cgltf_num_components(accessor->type);
        if (actualComponents != expectedComponents)
        {
            throw std::runtime_error(
                std::string("Accessor '") + context + "' has " + std::to_string(actualComponents) +
                " components per element, expected " + std::to_string(expectedComponents) + ".");
        }
        std::vector<float> out(static_cast<std::size_t>(accessor->count) *
                                static_cast<std::size_t>(expectedComponents));
        const cgltf_size unpacked = cgltf_accessor_unpack_floats(accessor, out.data(), out.size());
        if (unpacked != out.size())
        {
            throw std::runtime_error(
                std::string("Failed to unpack accessor '") + context +
                "' (malformed data or an unsupported layout).");
        }
        return out;
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

        const std::vector<float> ibm = skin->inverse_bind_matrices
            ? UnpackAccessor(skin->inverse_bind_matrices, 16, "inverseBindMatrices")
            : std::vector<float>();

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

            if (!ibm.empty())
            {
                const float* m = ibm.data() + static_cast<std::size_t>(oldIdx) * 16;
                bone.inverseBindGlobal = ConvertGltfMatrix(m);
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

    // A fully unpacked (and therefore sparse-accessor-safe) animation channel: sample times plus
    // the flattened value array (componentsPerValue floats per sample; 3x samples for
    // CUBICSPLINE, only the middle "value" third of each triplet is ever read).
    struct SampledChannel
    {
        std::vector<double> times;
        std::vector<float> values;
        int componentsPerValue = 0;
        bool cubicSpline = false;
        bool stepInterpolation = false;
    };

    SampledChannel LoadChannel(const cgltf_animation_channel& ch, cgltf_size componentsPerValue,
                                const char* context)
    {
        SampledChannel result;
        result.componentsPerValue = static_cast<int>(componentsPerValue);
        result.cubicSpline = ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline;
        result.stepInterpolation = ch.sampler->interpolation == cgltf_interpolation_type_step;

        const std::vector<float> times = UnpackAccessor(ch.sampler->input, 1, context);
        result.times.assign(times.begin(), times.end());
        result.values = UnpackAccessor(ch.sampler->output, componentsPerValue, context);
        return result;
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

    Vector3 ReadVec3Sample(const SampledChannel& ch, std::size_t sampleIndex)
    {
        const std::size_t idx = ch.cubicSpline ? sampleIndex * 3 + 1 : sampleIndex;
        const std::size_t o = idx * static_cast<std::size_t>(ch.componentsPerValue);
        return Vector3(ch.values[o], ch.values[o + 1], ch.values[o + 2]);
    }

    Quaternion ReadQuatSample(const SampledChannel& ch, std::size_t sampleIndex)
    {
        const std::size_t idx = ch.cubicSpline ? sampleIndex * 3 + 1 : sampleIndex;
        const std::size_t o = idx * static_cast<std::size_t>(ch.componentsPerValue);
        return Quaternion(ch.values[o], ch.values[o + 1], ch.values[o + 2], ch.values[o + 3]);
    }

    Vector3 EvaluateVec3Channel(const SampledChannel* ch, double t, Vector3 fallback)
    {
        if (ch == nullptr) { return fallback; }
        std::size_t lo = 0; float amount = 0.0f;
        FindBracket(ch->times, t, lo, amount);
        if (amount <= 0.0f || ch->cubicSpline || ch->stepInterpolation || lo + 1 >= ch->times.size())
        {
            return ReadVec3Sample(*ch, lo);
        }
        return Vector3::Lerp(ReadVec3Sample(*ch, lo), ReadVec3Sample(*ch, lo + 1), amount);
    }

    Quaternion EvaluateQuatChannel(const SampledChannel* ch, double t, Quaternion fallback)
    {
        if (ch == nullptr) { return fallback; }
        std::size_t lo = 0; float amount = 0.0f;
        FindBracket(ch->times, t, lo, amount);
        if (amount <= 0.0f || ch->cubicSpline || ch->stepInterpolation || lo + 1 >= ch->times.size())
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
                std::optional<SampledChannel> translation, rotation, scale;
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

                if (ch.target_path == cgltf_animation_path_type_translation)
                {
                    byBone[boneIdx].translation = LoadChannel(ch, 3, "translation channel");
                }
                else if (ch.target_path == cgltf_animation_path_type_rotation)
                {
                    byBone[boneIdx].rotation = LoadChannel(ch, 4, "rotation channel");
                }
                else if (ch.target_path == cgltf_animation_path_type_scale)
                {
                    byBone[boneIdx].scale = LoadChannel(ch, 3, "scale channel");
                }
                else { sawUnsupportedTarget = true; continue; } // e.g. morph target weights

                if (ch.sampler->input->count > 0)
                {
                    const std::vector<float> t = UnpackAccessor(ch.sampler->input, 1, "sampler input");
                    maxTime = std::max(maxTime, static_cast<double>(t.back()));
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
                std::vector<double> unionTimes;
                for (const auto* ch : {&channels.translation, &channels.rotation, &channels.scale})
                {
                    if (*ch) { unionTimes.insert(unionTimes.end(), ch->value().times.begin(), ch->value().times.end()); }
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
                    key.translation = EvaluateVec3Channel(channels.translation ? &*channels.translation : nullptr, t, bindTranslation);
                    key.rotation = EvaluateQuatChannel(channels.rotation ? &*channels.rotation : nullptr, t, bindRotation);
                    key.scale = EvaluateVec3Channel(channels.scale ? &*channels.scale : nullptr, t, bindScale);
                    track.keys.push_back(key);
                }
                clip.tracks.push_back(std::move(track));
            }

            clips.push_back(std::move(clip));
        }

        return clips;
    }

    // ---------------------------------------------------------------------------
    // Textures: extracts a primitive's material base-color texture (embedded bufferView,
    // external file, or base64 data: URI) as real image bytes, cached by cgltf_image* so a
    // texture shared by several primitives is only written to disk once.
    // ---------------------------------------------------------------------------

    struct ExtractedImage
    {
        std::vector<std::uint8_t> bytes;
        std::string extension;
    };

    std::optional<ExtractedImage> ExtractImage(const cgltf_image* image, const std::filesystem::path& gltfDir)
    {
        if (!image) { return std::nullopt; }

        std::string ext;
        if (image->mime_type)
        {
            const std::string mt = image->mime_type;
            if (mt == "image/jpeg") { ext = "jpg"; }
            else if (mt == "image/png") { ext = "png"; }
        }

        if (image->buffer_view)
        {
            const std::uint8_t* data = cgltf_buffer_view_data(image->buffer_view);
            if (!data) { throw std::runtime_error("Failed to read embedded image bufferView."); }
            ExtractedImage result;
            result.bytes.assign(data, data + image->buffer_view->size);
            result.extension = ext.empty() ? "png" : ext;
            return result;
        }

        if (image->uri)
        {
            const std::string uri = image->uri;

            if (uri.rfind("data:", 0) == 0)
            {
                const auto comma = uri.find(',');
                if (comma == std::string::npos) { throw std::runtime_error("Malformed data: URI for image."); }
                if (ext.empty()) { ext = (uri.find("image/jpeg") != std::string::npos) ? "jpg" : "png"; }

                const std::string b64 = uri.substr(comma + 1);
                std::size_t padding = 0;
                if (!b64.empty() && b64.back() == '=') { ++padding; }
                if (b64.size() > 1 && b64[b64.size() - 2] == '=') { ++padding; }
                const std::size_t decodedSize = b64.size() >= 4 ? (b64.size() / 4) * 3 - padding : 0;

                cgltf_options opts{};
                void* decoded = nullptr;
                const cgltf_result r = cgltf_load_buffer_base64(&opts, decodedSize, b64.c_str(), &decoded);
                if (r != cgltf_result_success || !decoded)
                {
                    throw std::runtime_error("Failed to decode base64 image data: URI.");
                }
                ExtractedImage result;
                result.bytes.assign(static_cast<std::uint8_t*>(decoded), static_cast<std::uint8_t*>(decoded) + decodedSize);
                result.extension = ext;
                std::free(decoded);
                return result;
            }

            std::vector<char> uriBuf(uri.begin(), uri.end());
            uriBuf.push_back('\0');
            cgltf_decode_uri(uriBuf.data());
            const std::filesystem::path imgPath = gltfDir / uriBuf.data();
            std::ifstream f(imgPath, std::ios::binary);
            if (!f) { throw std::runtime_error("Cannot open external image file: " + imgPath.string()); }
            ExtractedImage result;
            result.bytes.assign(std::istreambuf_iterator<char>(f), {});
            if (ext.empty())
            {
                std::string realExt = imgPath.extension().string();
                if (!realExt.empty() && realExt.front() == '.') { realExt = realExt.substr(1); }
                ext = realExt.empty() ? "png" : realExt;
            }
            result.extension = ext;
            return result;
        }

        return std::nullopt;
    }

    // Returns the base-color texture's cgltf_image, or nullptr if the primitive has no material,
    // no PBR metallic-roughness block, or no base color texture set.
    const cgltf_image* FindBaseColorImage(const cgltf_primitive& prim)
    {
        if (!prim.material || !prim.material->has_pbr_metallic_roughness) { return nullptr; }
        const cgltf_texture_view& view = prim.material->pbr_metallic_roughness.base_color_texture;
        if (!view.texture) { return nullptr; }
        return view.texture->image;
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
        const cgltf_image* baseColorImage = nullptr;
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
        out.baseColorImage = FindBaseColorImage(prim);

        const cgltf_size vertexCount = posAcc->count;
        out.vertexBytes.reserve(static_cast<std::size_t>(vertexCount) * static_cast<std::size_t>(out.stride));

        const std::vector<float> positions = UnpackAccessor(posAcc, 3, "POSITION");
        const std::vector<float> normals = normAcc ? UnpackAccessor(normAcc, 3, "NORMAL") : std::vector<float>();
        const std::vector<float> uvs = uvAcc ? UnpackAccessor(uvAcc, 2, "TEXCOORD_0") : std::vector<float>();
        const std::vector<float> weights = out.skinned ? UnpackAccessor(weightsAcc, 4, "WEIGHTS_0") : std::vector<float>();
        const std::vector<float> joints = out.skinned ? UnpackAccessor(jointsAcc, 4, "JOINTS_0") : std::vector<float>();

        for (cgltf_size i = 0; i < vertexCount; ++i)
        {
            const std::size_t i3 = static_cast<std::size_t>(i) * 3;
            const std::size_t i2 = static_cast<std::size_t>(i) * 2;
            const std::size_t i4 = static_cast<std::size_t>(i) * 4;

            const float px = positions[i3], py = positions[i3 + 1], pz = positions[i3 + 2];
            const float nx = normals.empty() ? 0.0f : normals[i3];
            const float ny = normals.empty() ? 0.0f : normals[i3 + 1];
            const float nz = normals.empty() ? 1.0f : normals[i3 + 2];
            const float u = uvs.empty() ? 0.0f : uvs[i2];
            const float v = uvs.empty() ? 0.0f : uvs[i2 + 1];

            AppendFloat(out.vertexBytes, px); AppendFloat(out.vertexBytes, py); AppendFloat(out.vertexBytes, pz);
            AppendFloat(out.vertexBytes, nx); AppendFloat(out.vertexBytes, ny); AppendFloat(out.vertexBytes, nz);
            AppendFloat(out.vertexBytes, u);  AppendFloat(out.vertexBytes, v);

            if (out.skinned)
            {
                AppendFloat(out.vertexBytes, weights[i4]);     AppendFloat(out.vertexBytes, weights[i4 + 1]);
                AppendFloat(out.vertexBytes, weights[i4 + 2]); AppendFloat(out.vertexBytes, weights[i4 + 3]);

                for (int k = 0; k < 4; ++k)
                {
                    const int oldJointIdx = static_cast<int>(joints[i4 + static_cast<std::size_t>(k)] + 0.5f);
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
    // Mesh grouping: a glTF file may combine multiple independent skinned characters (or a mix
    // of skinned + static scenery) in one scene graph. Group mesh-bearing scene nodes by which
    // skin (if any) they reference, so each group becomes its own Model .cnj -- this tool no
    // longer silently imports only the first skin in the file.
    // ---------------------------------------------------------------------------

    struct MeshGroup
    {
        const cgltf_skin* skin = nullptr; // nullptr = unskinned (static) group
        std::vector<const cgltf_mesh*> meshes;
    };

    std::vector<MeshGroup> CollectMeshGroups(const cgltf_data* data)
    {
        std::vector<MeshGroup> groups;
        std::unordered_map<const cgltf_skin*, std::size_t> indexOfSkin;

        for (cgltf_size i = 0; i < data->nodes_count; ++i)
        {
            const cgltf_node& node = data->nodes[i];
            if (!node.mesh) { continue; }

            auto it = indexOfSkin.find(node.skin);
            if (it == indexOfSkin.end())
            {
                indexOfSkin[node.skin] = groups.size();
                groups.push_back(MeshGroup{node.skin, {node.mesh}});
            }
            else
            {
                groups[it->second].meshes.push_back(node.mesh);
            }
        }

        // Fallback for files where meshes exist but no scene node references them (unusual but
        // not invalid) -- treat every mesh in the file as one unskinned group, matching this
        // tool's original node-graph-independent behavior.
        if (groups.empty())
        {
            MeshGroup g;
            for (cgltf_size i = 0; i < data->meshes_count; ++i) { g.meshes.push_back(&data->meshes[i]); }
            if (!g.meshes.empty()) { groups.push_back(std::move(g)); }
        }

        return groups;
    }

    // ---------------------------------------------------------------------------
    // Per-group conversion + .cnj/binary output.
    // ---------------------------------------------------------------------------

    void ConvertGroup(const cgltf_data* data, const MeshGroup& group, const std::string& outName,
                       const std::filesystem::path& gltfDir, const std::filesystem::path& outputDir,
                       std::unordered_map<const cgltf_image*, std::string>& writtenTextures,
                       std::vector<std::string>& warnings)
    {
        const bool hasSkin = group.skin != nullptr;
        SkeletonResult skeleton;
        if (hasSkin) { skeleton = BuildSkeleton(group.skin); }

        struct MeshEntry { std::string vertFile, idxFile, textureFile; int stride; std::string effect; };
        std::vector<MeshEntry> meshEntries;

        int meshCounter = 0;
        for (const cgltf_mesh* mesh : group.meshes)
        {
            for (cgltf_size p = 0; p < mesh->primitives_count; ++p)
            {
                const std::string partName = mesh->name
                    ? (std::string(mesh->name) + (mesh->primitives_count > 1 ? "_" + std::to_string(p) : ""))
                    : ("mesh" + std::to_string(meshCounter));
                MeshOut meshOut = ExtractMesh(mesh->primitives[p], partName, hasSkin ? &skeleton : nullptr);

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

                MeshEntry entry;
                entry.vertFile = vertFile;
                entry.idxFile = idxFile;
                entry.stride = meshOut.stride;
                entry.effect = meshOut.skinned ? "SkinnedEffect" : "BasicEffect";
                entry.textureFile = textureFile;
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
            ConvertGroup(data, groups[g], outName, gltfDir, opts.outputDir, writtenTextures, warnings);
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
