// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/Object.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class ModelMeshPart;

    /**
     * @brief One morph-weight animation keyframe: a full weight vector (one entry per morph
     * target) at a point in time.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. plan_cnj.md CNB-64 (Phase 13B).
     */
    struct MorphWeightKeyframeEXT
    {
        /** @brief Time of this keyframe, relative to the start of the clip. */
        System::TimeSpan Time;
        /** @brief Weight for each morph target at this keyframe, same order as MorphTargetDataEXT::PositionDeltas. */
        std::vector<float> Weights;
        /**
         * @brief CUBICSPLINE in-tangent, one per morph target (same order as Weights), or empty
         * when the source channel was not CUBICSPLINE-interpolated.
         */
        std::vector<float> InTangent;
        /**
         * @brief CUBICSPLINE out-tangent, one per morph target (same order as Weights), or empty
         * when the source channel was not CUBICSPLINE-interpolated.
         */
        std::vector<float> OutTangent;
    };

    /**
     * @brief A morph-weight animation track: a sequence of keyframed weight vectors driving one
     * mesh part's morph targets over time.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. Deliberately independent of AnimationPlayer/
     * SkinningData's own bone-track timeline: glTF's own "weights" animation channel targets a
     * mesh-instance node directly, not a skeleton joint, so it has no bone index to key against.
     * EvaluateMorphWeightsEXT supports LINEAR and STEP interpolation (real hold-last-value
     * behavior, not an approximation), and real CUBICSPLINE Hermite evaluation (component-wise,
     * matching GltfImportCore::HermiteEvaluate's own formula) when CubicSpline is true and each
     * keyframe's InTangent/OutTangent are populated.
     */
    struct MorphWeightTrackEXT
    {
        /** @brief Keyframes for this track, in ascending Time order. */
        std::vector<MorphWeightKeyframeEXT> Keys;
        /** @brief True if the source channel used STEP interpolation (hold last value, no lerp). */
        bool StepInterpolation = false;
        /** @brief True if the source channel used CUBICSPLINE interpolation (real Hermite tangents). */
        bool CubicSpline = false;
    };

    /**
     * @brief Per-vertex position/normal deltas for every morph target on a mesh part, the
     * current blend weights, and (optionally) a time-varying weight animation track.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. Mirrors SkinningData's own established
     * precedent (plan_cnj.md CNB-70/Task 941): attached to a loaded ModelMeshPart's own real
     * XNA `Tag` property (real XNA's ModelMeshPart has no dedicated morph-target property of its
     * own) -- game code retrieves it via `static_cast<MorphTargetDataEXT*>(part.getTagProperty())`.
     * Blending is CPU-side (re-blend + re-upload the vertex buffer via SetMorphWeightsEXT), not a
     * GPU vertex-shader technique: this works unchanged with every existing effect/shader on
     * every graphics renderer, at the cost of a full vertex-buffer re-upload whenever weights
     * change -- a deliberate simplicity-over-throughput tradeoff (see plan_cnj.md CNB-62's own
     * design-decision notes for the full reasoning).
     */
    CNAEXT struct MorphTargetDataEXT : public System::Object
    {
        /** @brief Returns the fully-qualified .NET-style type name of this object. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /** @brief The mesh part's base (all weights zero) vertex bytes, exactly as first uploaded. */
        std::vector<std::uint8_t> BaseVertexBytes;
        /** @brief Byte stride of one vertex in BaseVertexBytes -- any of CLAUDE.md's stride table. */
        int Stride = 32;
        /** @brief Per-target, per-vertex position deltas: PositionDeltas[target][vertex]. */
        std::vector<std::vector<Vector3>> PositionDeltas;
        /** @brief Per-target, per-vertex normal deltas, or an empty inner vector for a target with no NORMAL delta. */
        std::vector<std::vector<Vector3>> NormalDeltas;
        /**
         * @brief Per-target, per-vertex tangent deltas, or an empty inner vector for a target with
         * no TANGENT delta.
         *
         * @note CNAEXT — plan_gltf.md `GLTF-279`. `Vector3`, not `Vector4`: glTF morphs the tangent
         * **direction** only. The handedness `w` describes the UV winding and cannot be
         * interpolated — blending `+1` and `−1` passes through `0`, which is not a handedness — so
         * it stays on the base vertex and the blend never touches it. Only meaningful for strides
         * with a Tangent slot; a layout without one leaves these unused.
         */
        std::vector<std::vector<Vector3>> TangentDeltas;
        /** @brief Current blend weights, one per target (PositionDeltas.size() entries). */
        std::vector<float> Weights;
        /** @brief Optional time-varying weight animation track (glTF "weights" animation channel), or empty if none. */
        MorphWeightTrackEXT WeightTrack;
    };

    /**
     * @brief Computes a mesh part's blended vertex bytes from its base pose and morph target
     * deltas using the given weights -- the pure computation SetMorphWeightsEXT re-uploads.
     *
     * `finalPosition = basePosition + sum(weights[t] * PositionDeltas[t][v])`, and likewise for
     * normals (renormalized afterward, since a weighted sum of unit normals is not itself unit
     * length in general). Vertex attributes other than Position/Normal (TextureCoordinate,
     * BlendWeight, BlendIndices) are copied from the base pose unchanged.
     *
     * @param morph The base pose and per-target deltas to blend.
     * @param weights Weight vector; must have exactly as many entries as morph has targets.
     * @return The blended vertex bytes, same size/stride as morph.BaseVertexBytes.
     */
    CNAEXT [[nodiscard]] std::vector<std::uint8_t> BlendMorphTargetsEXT(
        const MorphTargetDataEXT& morph, const std::vector<float>& weights);

    /**
     * @brief Re-blends a mesh part's vertex buffer from its base pose and morph target deltas
     * using the given weights (via BlendMorphTargetsEXT), and re-uploads it.
     *
     * @param part The mesh part to re-blend; must have a MorphTargetDataEXT attached via its own Tag property.
     * @param weights New weight vector; must have exactly as many entries as the part has morph targets.
     */
    CNAEXT void SetMorphWeightsEXT(ModelMeshPart& part, const std::vector<float>& weights);

    /**
     * @brief Evaluates a morph-weight animation track at a point in time: real Hermite
     * interpolation between the bracketing keyframes when the track is CUBICSPLINE-interpolated,
     * LINEAR interpolation for a plain track (both clamped at both ends), or the bracket's lower
     * keyframe value unchanged when the track is STEP-interpolated.
     *
     * @param track The track to evaluate.
     * @param timeSeconds Time within the clip, in seconds.
     * @return The interpolated weight vector, or an empty vector if the track has no keyframes.
     */
    CNAEXT [[nodiscard]] std::vector<float> EvaluateMorphWeightsEXT(const MorphWeightTrackEXT& track, double timeSeconds);
}
