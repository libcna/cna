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
// The base-color texture uses whichever TEXCOORD set the material's own "texcoord" index selects,
// not always TEXCOORD_0. A primitive's COLOR_0 attribute (unskinned meshes only -- see below) is
// extracted too, reusing the real XNA VertexPositionColorTexture layout and BasicEffect's existing
// VertexColorEnabled shader path on every backend.
//
// Only nodes reachable from the file's default scene are imported (matches how the file's own
// author intended it to be used), and CUBICSPLINE animation channels are evaluated with the glTF
// spec's real cubic Hermite basis (in/out tangents included), not just the sampled value. An
// optional unitScale CLI argument corrects a source file not authored in meters, applied uniformly
// to positions, bone bind-pose translations, and animated translation keyframes/tangents alike.
// Draco-compressed primitives (KHR_draco_mesh_compression) are rejected with a clear error rather
// than silently read as garbage.
//
// CNB-72/73 (Phase 13E): an unskinned, uncolored primitive with both a base-color and an
// occlusion texture is imported through DualTextureEffect (Texture=base color, Texture2=
// occlusion) instead of BasicEffect, reusing that effect's already-working cross-backend shaders.
// This is a documented approximation, not a faithful PBR occlusion implementation: real XNA's
// DualTextureEffect blends as `(base*2) * texture2 * diffuseColor` (a "centered at 0.5 = neutral"
// baked-lightmap convention), while glTF occlusion maps use "1.0 = fully visible, 0.0 = fully
// occluded" -- so unoccluded surface areas (occlusion ~= 1.0) render roughly 2x brighter than the
// base color alone, rather than unchanged. Occlusion was still chosen over the other PBR maps
// because it is the only one that is a single multiplicative modulation map to begin with (normal/
// metallic-roughness/emissive don't fit a multiply-blend model at all). Fixing the brightness bias
// would require decoding and re-encoding the occlusion image (remapping each pixel by x0.5) rather
// than the byte-for-byte passthrough ExtractImage() does today -- left as a documented follow-up,
// not implemented here to avoid vendoring an image codec for a single-purpose remap.
//
// CNB-66/67/68 (Phase 13C): a primitive's COLOR_0 attribute is now imported for skinned meshes
// too, using a new stride-56 vertex layout (the stride-52 GPU-skinned layout with a per-vertex
// Color appended at the end) and SkinnedEffect's own NOXNA VertexColorEnabled addition (real XNA's
// SkinnedEffect has no such property -- see SkinnedEffect.hpp's own doc comment). DualTextureEffect
// (CNB-72/73 above) remains unskinned-only and mutually exclusive with vertex color, since both
// still need the mesh's one shared vertex layout and DualTextureEffect's own vertex shader has no
// room for a per-vertex Normal, BlendWeight/BlendIndices, or Color between its Position/
// TextureCoordinate attributes.
//
// Known, deliberate MVP scope cuts (see plan_cnj.md CNB-50/51's own notes for the full list): only
// the base-color and (per above) occlusion textures are extracted (no normal/metallic-roughness/
// emissive maps, no PBR factor values -- CNA's real-XNA-faithful BasicEffect/SkinnedEffect/
// DualTextureEffect have no shader path for any of these); morph targets (blend shapes) are not
// imported.

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
#include <unordered_set>
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

    // Uniformly scales just the translation part of an affine transform, leaving rotation and any
    // local scale factor untouched -- correct for a global unit-of-measure correction (e.g. a
    // source file authored in centimeters), not a shape-changing per-axis scale. Applying the same
    // factor to both a bone's local bind pose translation and its (separately glTF-authored)
    // inverse bind matrix translation is mathematically consistent: Inverse([R|t]) = [R^-1 |
    // -R^-1*t], so scaling t by k scales the inverse's own translation by exactly k too, not 1/k.
    Matrix ScaleTranslation(const Matrix& m, float scale)
    {
        Matrix result = m;
        result.M41 *= scale;
        result.M42 *= scale;
        result.M43 *= scale;
        return result;
    }

    SkeletonResult BuildSkeleton(const cgltf_skin* skin, float unitScale)
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
            bone.bindPoseLocal = ScaleTranslation(ConvertGltfMatrix(localMat), unitScale);

            if (!ibm.empty())
            {
                const float* m = ibm.data() + static_cast<std::size_t>(oldIdx) * 16;
                bone.inverseBindGlobal = ScaleTranslation(ConvertGltfMatrix(m), unitScale);
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

    // glTF's CUBICSPLINE Hermite basis, applied component-wise: given the bracketing keyframes
    // lo/lo+1 (each holding an [in-tangent, value, out-tangent] triplet in ch.values), the
    // normalized fraction s within the bracket, and the bracket's real time span deltaT (the
    // spec's Hermite formula scales tangents by the interval length, not just s), writes
    // ch.componentsPerValue interpolated components into out.
    void HermiteEvaluate(const SampledChannel& ch, std::size_t lo, float s, double deltaT, float* out)
    {
        const int n = ch.componentsPerValue;
        const std::size_t stride = static_cast<std::size_t>(n);
        const float* v0 = ch.values.data() + (lo * 3 + 1) * stride;       // value at lo
        const float* b0 = ch.values.data() + (lo * 3 + 2) * stride;       // out-tangent at lo
        const float* v1 = ch.values.data() + ((lo + 1) * 3 + 1) * stride; // value at lo+1
        const float* a1 = ch.values.data() + ((lo + 1) * 3 + 0) * stride; // in-tangent at lo+1

        const float s2 = s * s, s3 = s2 * s;
        const float h00 = 2.0f * s3 - 3.0f * s2 + 1.0f;
        const float h10 = s3 - 2.0f * s2 + s;
        const float h01 = -2.0f * s3 + 3.0f * s2;
        const float h11 = s3 - s2;
        const float dt = static_cast<float>(deltaT);

        for (int i = 0; i < n; ++i)
        {
            out[i] = h00 * v0[i] + dt * h10 * b0[i] + h01 * v1[i] + dt * h11 * a1[i];
        }
    }

    Vector3 EvaluateVec3Channel(const SampledChannel* ch, double t, Vector3 fallback)
    {
        if (ch == nullptr) { return fallback; }
        std::size_t lo = 0; float amount = 0.0f;
        FindBracket(ch->times, t, lo, amount);
        if (amount <= 0.0f || ch->stepInterpolation || lo + 1 >= ch->times.size())
        {
            return ReadVec3Sample(*ch, lo);
        }
        if (ch->cubicSpline)
        {
            float out[3];
            HermiteEvaluate(*ch, lo, amount, ch->times[lo + 1] - ch->times[lo], out);
            return Vector3(out[0], out[1], out[2]);
        }
        return Vector3::Lerp(ReadVec3Sample(*ch, lo), ReadVec3Sample(*ch, lo + 1), amount);
    }

    Quaternion EvaluateQuatChannel(const SampledChannel* ch, double t, Quaternion fallback)
    {
        if (ch == nullptr) { return fallback; }
        std::size_t lo = 0; float amount = 0.0f;
        FindBracket(ch->times, t, lo, amount);
        if (amount <= 0.0f || ch->stepInterpolation || lo + 1 >= ch->times.size())
        {
            return ReadQuatSample(*ch, lo);
        }
        if (ch->cubicSpline)
        {
            // Component-wise Hermite does not preserve unit length -- normalize afterward, the
            // standard treatment for glTF CUBICSPLINE rotation channels.
            float out[4];
            HermiteEvaluate(*ch, lo, amount, ch->times[lo + 1] - ch->times[lo], out);
            Quaternion q(out[0], out[1], out[2], out[3]);
            const float lenSq = q.X * q.X + q.Y * q.Y + q.Z * q.Z + q.W * q.W;
            if (lenSq > 1e-12f)
            {
                const float invLen = 1.0f / std::sqrt(lenSq);
                q = Quaternion(q.X * invLen, q.Y * invLen, q.Z * invLen, q.W * invLen);
            }
            return q;
        }
        return Quaternion::Slerp(ReadQuatSample(*ch, lo), ReadQuatSample(*ch, lo + 1), amount);
    }

    std::vector<ClipOut> ExtractClips(const cgltf_data* data, const SkeletonResult& skel,
                                       float unitScale, std::vector<std::string>& warnings)
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
            bool sawUnsupportedTarget = false;

            for (cgltf_size c = 0; c < anim.channels_count; ++c)
            {
                const cgltf_animation_channel& ch = anim.channels[c];
                auto it = skel.nodeToNewIndex.find(ch.target_node);
                if (it == skel.nodeToNewIndex.end()) { continue; } // targets a non-joint node -- skip
                const int boneIdx = it->second;

                if (ch.target_path == cgltf_animation_path_type_translation)
                {
                    byBone[boneIdx].translation = LoadChannel(ch, 3, "translation channel");
                    // Translation values (and, for CUBICSPLINE, their in/out tangents -- both are
                    // position-derived quantities) must track the same unit-scale correction
                    // already applied to the skeleton's own bind-pose translations, or an
                    // animated bone would jump back to unscaled-space offsets mid-clip.
                    for (float& component : byBone[boneIdx].translation->values) { component *= unitScale; }
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

    // CNB-72/73 (Phase 13E): returns the material's occlusion texture image, or nullptr. Among
    // glTF's own PBR texture slots, occlusion is the only one that is a single multiplicative
    // modulation map (not a normal/vector map, not an additive emissive map), making it the
    // closest available semantic match to DualTextureEffect's real-XNA "diffuse * 2 * lightmap"
    // Texture2 slot -- see the ConvertGroup docs below for the known brightness caveat.
    const cgltf_image* FindOcclusionImage(const cgltf_primitive& prim)
    {
        if (!prim.material || !prim.material->occlusion_texture.texture) { return nullptr; }
        return prim.material->occlusion_texture.texture->image;
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
        bool colored = false;
        bool useDualTexture = false;
        const cgltf_image* baseColorImage = nullptr;
        const cgltf_image* occlusionImage = nullptr;
    };

    std::uint8_t ToByteColorChannel(float v)
    {
        return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    }

    MeshOut ExtractMesh(const cgltf_primitive& prim, const std::string& name, const SkeletonResult* skel,
                         float unitScale)
    {
        if (prim.has_draco_mesh_compression)
        {
            throw std::runtime_error(
                "Primitive '" + name + "' uses Draco mesh compression (KHR_draco_mesh_compression), "
                "which this tool does not support.");
        }

        const cgltf_accessor* posAcc = cgltf_find_accessor(&prim, cgltf_attribute_type_position, 0);
        if (!posAcc) { throw std::runtime_error("Primitive '" + name + "' has no POSITION attribute."); }
        const cgltf_accessor* normAcc = cgltf_find_accessor(&prim, cgltf_attribute_type_normal, 0);

        // Use whichever TEXCOORD set the base-color texture actually references (glTF allows a
        // texture reference to select TEXCOORD_1/2/... via its own "texcoord" index, defaulting
        // to 0) -- a hardcoded TEXCOORD_0 would silently mismatch a texture authored against a
        // different UV set.
        int texcoordIndex = 0;
        if (prim.material && prim.material->has_pbr_metallic_roughness &&
            prim.material->pbr_metallic_roughness.base_color_texture.texture)
        {
            texcoordIndex = prim.material->pbr_metallic_roughness.base_color_texture.texcoord;
        }
        const cgltf_accessor* uvAcc = cgltf_find_accessor(&prim, cgltf_attribute_type_texcoord, texcoordIndex);

        const cgltf_accessor* colorAcc = cgltf_find_accessor(&prim, cgltf_attribute_type_color, 0);
        const cgltf_accessor* jointsAcc = skel ? cgltf_find_accessor(&prim, cgltf_attribute_type_joints, 0) : nullptr;
        const cgltf_accessor* weightsAcc = skel ? cgltf_find_accessor(&prim, cgltf_attribute_type_weights, 0) : nullptr;

        MeshOut out;
        out.name = name;
        out.skinned = (jointsAcc != nullptr) && (weightsAcc != nullptr);
        // CNB-68 (Phase 13C): vertex color (COLOR_0) is supported for both skinned and unskinned
        // meshes. A skinned+colored primitive uses a new stride-56 layout (the stride-52 GPU-
        // skinned layout with a per-vertex Color appended at the end) and SkinnedEffect's own
        // NOXNA VertexColorEnabled addition (see SkinnedEffect.hpp -- real XNA's SkinnedEffect has
        // no such property).
        out.colored = (colorAcc != nullptr);
        out.baseColorImage = FindBaseColorImage(prim);
        // CNB-73: an unskinned, uncolored primitive with both a base-color and an occlusion
        // texture is imported through DualTextureEffect (Texture=base color, Texture2=occlusion)
        // instead of BasicEffect -- real XNA's DualTextureEffect always samples both texture
        // slots (no TextureEnabled-style toggle) via a single shared UV set at vertex attribute
        // locations 0/1 with no Normal in between (see EasyGLGraphicsBackend::ApplyLayout's
        // stride==20 case), so this reuses the plain VertexPositionTexture layout rather than a
        // new Position+Normal+Texture+Texture2 vertex format. Skinned/colored meshes keep their
        // existing effect (SkinnedEffect has no Texture2 slot; the colored VertexPositionColor
        // Texture layout has no room for a second UV/texture either) -- a documented scope cut,
        // not an oversight.
        out.occlusionImage = (!out.skinned && !out.colored) ? FindOcclusionImage(prim) : nullptr;
        out.useDualTexture = (out.occlusionImage != nullptr) && (out.baseColorImage != nullptr);
        // Unskinned colored meshes reuse the real XNA VertexPositionColorTexture layout (stride
        // 24, Position+Color+TextureCoordinate, no Normal) -- already fully supported end-to-end
        // by ModelTypeReader and every graphics backend's existing VertexColorEnabled shader path.
        // Skinned colored meshes use the new stride-56 layout instead (Position+Normal+
        // TextureCoordinate+BlendWeight+BlendIndices+Color -- see CNB-67).
        out.stride = out.skinned ? (out.colored ? 56 : 52) : (out.colored ? 24 : (out.useDualTexture ? 20 : 32));

        const cgltf_size vertexCount = posAcc->count;
        out.vertexBytes.reserve(static_cast<std::size_t>(vertexCount) * static_cast<std::size_t>(out.stride));

        const std::vector<float> positions = UnpackAccessor(posAcc, 3, "POSITION");
        // Only the unskinned stride-24 (Position+Color+TextureCoordinate) and stride-20
        // (DualTextureEffect) layouts have no room for a per-vertex Normal.
        const std::vector<float> normals = (normAcc && out.stride != 24 && out.stride != 20)
            ? UnpackAccessor(normAcc, 3, "NORMAL") : std::vector<float>();
        const std::vector<float> uvs = uvAcc ? UnpackAccessor(uvAcc, 2, "TEXCOORD") : std::vector<float>();
        const std::vector<float> weights = out.skinned ? UnpackAccessor(weightsAcc, 4, "WEIGHTS_0") : std::vector<float>();
        const std::vector<float> joints = out.skinned ? UnpackAccessor(jointsAcc, 4, "JOINTS_0") : std::vector<float>();
        // COLOR_0 may be VEC3 (RGB) or VEC4 (RGBA) per the glTF spec; a missing alpha defaults to
        // fully opaque. cgltf_accessor_unpack_floats/UnpackAccessor also transparently normalizes
        // whichever component type (FLOAT/normalized UBYTE/normalized USHORT) the file actually uses.
        const int colorComponents = colorAcc ? static_cast<int>(cgltf_num_components(colorAcc->type)) : 0;
        const std::vector<float> colors = out.colored
            ? UnpackAccessor(colorAcc, static_cast<cgltf_size>(colorComponents), "COLOR_0")
            : std::vector<float>();

        for (cgltf_size i = 0; i < vertexCount; ++i)
        {
            const std::size_t i3 = static_cast<std::size_t>(i) * 3;
            const std::size_t i2 = static_cast<std::size_t>(i) * 2;
            const std::size_t i4 = static_cast<std::size_t>(i) * 4;

            const float px = positions[i3] * unitScale, py = positions[i3 + 1] * unitScale, pz = positions[i3 + 2] * unitScale;
            const float u = uvs.empty() ? 0.0f : uvs[i2];
            const float v = uvs.empty() ? 0.0f : uvs[i2 + 1];

            AppendFloat(out.vertexBytes, px); AppendFloat(out.vertexBytes, py); AppendFloat(out.vertexBytes, pz);

            const std::size_t co = static_cast<std::size_t>(i) * static_cast<std::size_t>(colorComponents);
            auto appendColor = [&]()
            {
                out.vertexBytes.push_back(ToByteColorChannel(colors[co]));
                out.vertexBytes.push_back(ToByteColorChannel(colors[co + 1]));
                out.vertexBytes.push_back(ToByteColorChannel(colors[co + 2]));
                out.vertexBytes.push_back(colorComponents >= 4 ? ToByteColorChannel(colors[co + 3]) : std::uint8_t{255});
            };

            if (out.colored && !out.skinned)
            {
                // stride 24: Position + Color + TextureCoordinate.
                appendColor();
                AppendFloat(out.vertexBytes, u); AppendFloat(out.vertexBytes, v);
                continue;
            }

            if (out.useDualTexture)
            {
                // stride 20: Position + TextureCoordinate.
                AppendFloat(out.vertexBytes, u); AppendFloat(out.vertexBytes, v);
                continue;
            }

            // stride 32/52/56: Position + Normal + TextureCoordinate [+ BlendWeight +
            // BlendIndices] [+ Color] -- Color, when present, is always appended last (CNB-67).
            const float nx = normals.empty() ? 0.0f : normals[i3];
            const float ny = normals.empty() ? 0.0f : normals[i3 + 1];
            const float nz = normals.empty() ? 1.0f : normals[i3 + 2];
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

                if (out.colored) { appendColor(); }
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

    // Nodes reachable from the file's default scene (data->scene, or the first scene if that's
    // unset but at least one scene exists), walked via each node's own children[] array. A file
    // with more than one scene may have nodes that exist only in a non-default scene, or that
    // aren't part of any scene at all (e.g. staging nodes an authoring tool left behind) -- those
    // must not be silently imported alongside the actual content.
    std::unordered_set<const cgltf_node*> CollectSceneReachableNodes(const cgltf_data* data)
    {
        std::unordered_set<const cgltf_node*> reachable;
        const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);
        if (!scene) { return reachable; } // no scenes at all -- caller falls back to "every node"

        std::vector<const cgltf_node*> stack(scene->nodes, scene->nodes + scene->nodes_count);
        while (!stack.empty())
        {
            const cgltf_node* node = stack.back();
            stack.pop_back();
            if (!reachable.insert(node).second) { continue; } // already visited
            for (cgltf_size c = 0; c < node->children_count; ++c) { stack.push_back(node->children[c]); }
        }
        return reachable;
    }

    std::vector<MeshGroup> CollectMeshGroups(const cgltf_data* data)
    {
        std::vector<MeshGroup> groups;
        std::unordered_map<const cgltf_skin*, std::size_t> indexOfSkin;

        const std::unordered_set<const cgltf_node*> reachable = CollectSceneReachableNodes(data);

        for (cgltf_size i = 0; i < data->nodes_count; ++i)
        {
            const cgltf_node& node = data->nodes[i];
            if (!node.mesh) { continue; }
            if (!reachable.empty() && reachable.find(&node) == reachable.end()) { continue; }

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
                       float unitScale, std::vector<std::string>& warnings)
    {
        const bool hasSkin = group.skin != nullptr;
        SkeletonResult skeleton;
        if (hasSkin) { skeleton = BuildSkeleton(group.skin, unitScale); }

        struct MeshEntry { std::string vertFile, idxFile, textureFile, texture2File; int stride; std::string effect; bool vertexColorEnabled; };
        std::vector<MeshEntry> meshEntries;

        int meshCounter = 0;
        for (const cgltf_mesh* mesh : group.meshes)
        {
            for (cgltf_size p = 0; p < mesh->primitives_count; ++p)
            {
                const std::string partName = mesh->name
                    ? (std::string(mesh->name) + (mesh->primitives_count > 1 ? "_" + std::to_string(p) : ""))
                    : ("mesh" + std::to_string(meshCounter));
                MeshOut meshOut = ExtractMesh(mesh->primitives[p], partName, hasSkin ? &skeleton : nullptr, unitScale);

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
                    auto cached = writtenTextures.find(meshOut.occlusionImage);
                    if (cached != writtenTextures.end())
                    {
                        texture2File = cached->second;
                    }
                    else if (auto img = ExtractImage(meshOut.occlusionImage, gltfDir))
                    {
                        texture2File = outName + "_tex" + std::to_string(writtenTextures.size()) + "." + img->extension;
                        WriteBinaryFile(outputDir / texture2File, img->bytes);
                        writtenTextures[meshOut.occlusionImage] = texture2File;
                    }
                }

                MeshEntry entry;
                entry.vertFile = vertFile;
                entry.idxFile = idxFile;
                entry.stride = meshOut.stride;
                entry.effect = meshOut.skinned ? "SkinnedEffect"
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
                entry.vertexColorEnabled = meshOut.colored;
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
            ConvertGroup(data, groups[g], outName, gltfDir, opts.outputDir, writtenTextures, opts.unitScale, warnings);
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
