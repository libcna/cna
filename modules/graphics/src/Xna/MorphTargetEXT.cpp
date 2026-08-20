// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/MorphTargetEXT.hpp"

#include <cmath>
#include <cstring>
#include <stdexcept>

#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    const std::string& MorphTargetDataEXT::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Graphics.MorphTargetDataEXT";
        return typeName;
    }

    std::vector<std::uint8_t> BlendMorphTargetsEXT(const MorphTargetDataEXT& morph, const std::vector<float>& weights)
    {
        if (weights.size() != morph.PositionDeltas.size())
        {
            throw std::runtime_error(
                "BlendMorphTargetsEXT: expected " + std::to_string(morph.PositionDeltas.size()) +
                " weight(s), got " + std::to_string(weights.size()) + ".");
        }

        const int stride = morph.Stride;
        // plans/plan_gltf.md GLTF-278. This used to be the literal list {32, 52, 56}, written when those
        // were the only strides a mesh with normals could have. GLTF-215 changed which effect a
        // metallic-roughness material selects, and with it the strides an ordinary glTF mesh gets
        // (48 unskinned, 68 skinned) -- both of which carry Normal at offset 12 and neither of
        // which was in the list, so every PBR morph target silently kept its base normals while
        // its positions moved. Restating an ABI is what let that happen, so the predicate is now a
        // query against the canonical stride table itself and cannot go stale again.
        const CNA::Internal::Graphics::InferredVertexLayout layout =
            CNA::Internal::Graphics::InferredLayoutForStride(
                stride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
        int normalOffset = -1;
        int tangentOffset = -1;
        for (std::size_t i = 0; layout.known && i < layout.count; ++i)
        {
            if (layout.elements[i].usage == VertexElementUsage::Normal &&
                layout.elements[i].usageIndex == 0 &&
                layout.elements[i].format == VertexElementFormat::Vector3)
            {
                normalOffset = layout.elements[i].offset;
            }
            // plans/plan_gltf.md GLTF-279. Vector4 in the layout, but only its xyz is morphed: `w` is the
            // handedness and stays exactly as the base vertex has it.
            else if (layout.elements[i].usage == VertexElementUsage::Tangent &&
                     layout.elements[i].usageIndex == 0 &&
                     layout.elements[i].format == VertexElementFormat::Vector4)
            {
                tangentOffset = layout.elements[i].offset;
            }
        }
        const bool hasNormalSlot = normalOffset >= 0;
        const bool hasTangentSlot = tangentOffset >= 0;
        const int numVertices = stride > 0
            ? static_cast<int>(morph.BaseVertexBytes.size()) / stride : 0;

        std::vector<std::uint8_t> blended = morph.BaseVertexBytes;

        for (int v = 0; v < numVertices; ++v)
        {
            Vector3 posDelta;
            Vector3 normDelta;
            Vector3 tangentDelta;
            bool anyNormalDelta = false;
            bool anyTangentDelta = false;

            for (std::size_t t = 0; t < weights.size(); ++t)
            {
                const float w = weights[t];
                if (w == 0.0f) { continue; }
                // Empty means "this target authors no POSITION delta", the same convention the
                // normal and tangent arrays already used (plans/plan_gltf.md GLTF-292). Before that,
                // such a target arrived zero-filled and this read was safe by accident.
                if (!morph.PositionDeltas[t].empty())
                {
                    posDelta = posDelta + w * morph.PositionDeltas[t][static_cast<std::size_t>(v)];
                }
                if (!morph.NormalDeltas[t].empty())
                {
                    normDelta = normDelta + w * morph.NormalDeltas[t][static_cast<std::size_t>(v)];
                    anyNormalDelta = true;
                }
                if (t < morph.TangentDeltas.size() && !morph.TangentDeltas[t].empty())
                {
                    tangentDelta =
                        tangentDelta + w * morph.TangentDeltas[t][static_cast<std::size_t>(v)];
                    anyTangentDelta = true;
                }
            }

            const std::size_t off = static_cast<std::size_t>(v) * static_cast<std::size_t>(stride);

            float basePos[3];
            std::memcpy(basePos, blended.data() + off, sizeof(basePos));
            const Vector3 newPos(basePos[0] + posDelta.X, basePos[1] + posDelta.Y, basePos[2] + posDelta.Z);
            std::memcpy(blended.data() + off, &newPos, sizeof(newPos));

            if (hasNormalSlot && anyNormalDelta)
            {
                const std::size_t normOff = off + static_cast<std::size_t>(normalOffset);
                float baseNorm[3];
                std::memcpy(baseNorm, blended.data() + normOff, sizeof(baseNorm));
                Vector3 newNorm(baseNorm[0] + normDelta.X, baseNorm[1] + normDelta.Y, baseNorm[2] + normDelta.Z);
                // A weighted sum of unit normals is not itself unit length in general --
                // renormalize, the same treatment GltfImportCore's own CUBICSPLINE rotation
                // evaluation already uses for an analogous reason.
                const float lenSq = newNorm.X * newNorm.X + newNorm.Y * newNorm.Y + newNorm.Z * newNorm.Z;
                if (lenSq > 1e-12f)
                {
                    const float invLen = 1.0f / std::sqrt(lenSq);
                    newNorm = Vector3(newNorm.X * invLen, newNorm.Y * invLen, newNorm.Z * invLen);
                }
                std::memcpy(blended.data() + normOff, &newNorm, sizeof(newNorm));
            }

            if (hasTangentSlot && anyTangentDelta)
            {
                const std::size_t tanOff = off + static_cast<std::size_t>(tangentOffset);
                float baseTangent[4];
                std::memcpy(baseTangent, blended.data() + tanOff, sizeof(baseTangent));
                Vector3 newTangent(baseTangent[0] + tangentDelta.X,
                                   baseTangent[1] + tangentDelta.Y,
                                   baseTangent[2] + tangentDelta.Z);
                const float lenSq = newTangent.X * newTangent.X + newTangent.Y * newTangent.Y +
                                    newTangent.Z * newTangent.Z;
                if (lenSq > 1e-12f)
                {
                    const float invLen = 1.0f / std::sqrt(lenSq);
                    newTangent = Vector3(newTangent.X * invLen, newTangent.Y * invLen,
                                         newTangent.Z * invLen);
                }
                // Only xyz is written. baseTangent[3] -- the handedness -- is deliberately left
                // exactly as it was: it is a property of the UV winding, not of the pose, and
                // interpolating it would pass through 0, which is not a handedness at all.
                std::memcpy(blended.data() + tanOff, &newTangent, sizeof(newTangent));
            }
        }

        // plans/plan_gltf.md GLTF-461. §3.7.2.2: "When the base mesh primitive does not specify normals,
        // client implementations MUST calculate flat normals for each morph target." Everything
        // above blends what the file authored; this recomputes what it did not.
        //
        // Why it has to be here rather than at import: a POSITION delta can rotate a face, so the
        // normal of a normal-less primitive is a function of the WEIGHTS, and the weights are a
        // runtime value an animation drives. Baking the rest pose left such a primitive lit as
        // though it had never deformed -- correct at weight 0 and wrong everywhere else -- and no
        // delta blend could have fixed it, because §3.7.2.2 makes an original attribute a
        // precondition for a target attribute and so forbids the NORMAL deltas outright.
        //
        // Exact rather than averaged because the importer splits every corner of such a primitive
        // into its own vertex: each face owns its three vertices, so writing the face normal to
        // them is flat shading, not a smoothing of neighbours.
        if (morph.RecomputeFlatNormalsEXT && hasNormalSlot && numVertices > 0)
        {
            std::vector<Vector3> accumulated(static_cast<std::size_t>(numVertices));
            const auto positionOf = [&](std::uint32_t index) {
                float p[3];
                std::memcpy(p, blended.data() +
                                  static_cast<std::size_t>(index) * static_cast<std::size_t>(stride),
                            sizeof(p));
                return Vector3(p[0], p[1], p[2]);
            };

            const std::vector<std::uint32_t>& indices = morph.TriangleIndicesEXT;
            for (std::size_t f = 0; f + 2 < indices.size(); f += 3)
            {
                const std::uint32_t i0 = indices[f], i1 = indices[f + 1], i2 = indices[f + 2];
                if (i0 >= static_cast<std::uint32_t>(numVertices) ||
                    i1 >= static_cast<std::uint32_t>(numVertices) ||
                    i2 >= static_cast<std::uint32_t>(numVertices))
                {
                    continue;
                }
                const Vector3 a = positionOf(i0), b = positionOf(i1), c = positionOf(i2);
                // Un-normalized: its length is twice the triangle's area, so a vertex a caller left
                // shared gets an area weighting rather than whichever face came last.
                const Vector3 weighted = Vector3::Cross(b - a, c - a);
                accumulated[i0] = accumulated[i0] + weighted;
                accumulated[i1] = accumulated[i1] + weighted;
                accumulated[i2] = accumulated[i2] + weighted;
            }

            for (int v = 0; v < numVertices; ++v)
            {
                const std::size_t off = static_cast<std::size_t>(v) * static_cast<std::size_t>(stride);
                const std::size_t normOff = off + static_cast<std::size_t>(normalOffset);
                Vector3 n = accumulated[static_cast<std::size_t>(v)];
                const float lenSq = n.X * n.X + n.Y * n.Y + n.Z * n.Z;
                if (lenSq <= 1e-24f)
                {
                    // A vertex no face reaches, or one whose faces all collapsed at this pose, has
                    // no computable normal -- leave whatever the base pose carried rather than
                    // inventing a direction the geometry does not state.
                    continue;
                }
                const float invLen = 1.0f / std::sqrt(lenSq);
                n = Vector3(n.X * invLen, n.Y * invLen, n.Z * invLen);
                std::memcpy(blended.data() + normOff, &n, sizeof(n));

                // §3.7.2.2 SHOULD-recompute tangents per morph target with MikkTSpace against the
                // updated positions, normals and UVs. CNA re-orthogonalizes the generated basis
                // against the new normal instead -- a documented approximation of that SHOULD (see
                // docs/gltf-limitations.md), and one that at least keeps T perpendicular to N, which
                // is the property tangent-space normal mapping actually depends on.
                if (!hasTangentSlot) { continue; }
                const std::size_t tanOff = off + static_cast<std::size_t>(tangentOffset);
                float baseTangent[4];
                std::memcpy(baseTangent, blended.data() + tanOff, sizeof(baseTangent));
                const Vector3 t(baseTangent[0], baseTangent[1], baseTangent[2]);
                const float projection = t.X * n.X + t.Y * n.Y + t.Z * n.Z;
                Vector3 ortho(t.X - n.X * projection, t.Y - n.Y * projection, t.Z - n.Z * projection);
                const float orthoLenSq = ortho.X * ortho.X + ortho.Y * ortho.Y + ortho.Z * ortho.Z;
                if (orthoLenSq <= 1e-24f) { continue; }
                const float invOrtho = 1.0f / std::sqrt(orthoLenSq);
                ortho = Vector3(ortho.X * invOrtho, ortho.Y * invOrtho, ortho.Z * invOrtho);
                std::memcpy(blended.data() + tanOff, &ortho, sizeof(ortho));
            }
        }

        return blended;
    }

    // The morph equation itself -- base + sum(weight_i * delta_i), applied on the CPU and
    // re-uploaded -- is stated in docs/gltf-conventions.md ("Morphing happens on the CPU"), along
    // with what that costs (docs/gltf-performance.md, GLTF-441/GLTF-442: the vertex data is held
    // twice) and why a GPU path is scoped rather than built.
    void SetMorphWeightsEXT(ModelMeshPart& part, const std::vector<float>& weights)
    {
        auto* morph = dynamic_cast<MorphTargetDataEXT*>(part.getTagProperty());
        if (!morph)
        {
            throw std::runtime_error(
                "SetMorphWeightsEXT: the given ModelMeshPart has no MorphTargetDataEXT attached "
                "via its own Tag property.");
        }

        const std::vector<std::uint8_t> blended = BlendMorphTargetsEXT(*morph, weights);
        morph->Weights = weights;

        const int numVertices = morph->Stride > 0
            ? static_cast<int>(morph->BaseVertexBytes.size()) / morph->Stride : 0;
        VertexBuffer* vb = part.getVertexBufferProperty();
        vb->SetDataRaw(blended.data(), numVertices, morph->Stride);
    }

    std::vector<float> EvaluateMorphWeightsEXT(const MorphWeightTrackEXT& track, double timeSeconds)
    {
        if (track.Keys.empty()) { return {}; }
        if (track.Keys.size() == 1) { return track.Keys.front().Weights; }

        if (timeSeconds <= track.Keys.front().Time.getTotalSecondsProperty())
        {
            return track.Keys.front().Weights;
        }
        if (timeSeconds >= track.Keys.back().Time.getTotalSecondsProperty())
        {
            return track.Keys.back().Weights;
        }

        for (std::size_t i = 0; i + 1 < track.Keys.size(); ++i)
        {
            const double t0 = track.Keys[i].Time.getTotalSecondsProperty();
            const double t1 = track.Keys[i + 1].Time.getTotalSecondsProperty();
            if (timeSeconds >= t0 && timeSeconds <= t1)
            {
                // plans/plan_gltf.md GLTF-301, the same half-open-interval rule as the bone channels'
                // own StepSampleIndex: §3.6 holds key i's value on [t0, t1), so at exactly t1 the
                // next key is already in force. Returning key i there makes every interior
                // keyframe of a STEP weight track play the previous key's value.
                if (track.StepInterpolation)
                {
                    return (timeSeconds >= t1) ? track.Keys[i + 1].Weights : track.Keys[i].Weights;
                }

                const double span = t1 - t0;
                const float amount = span > 0.0 ? static_cast<float>((timeSeconds - t0) / span) : 0.0f;
                const std::vector<float>& a = track.Keys[i].Weights;
                const std::vector<float>& b = track.Keys[i + 1].Weights;

                // Real glTF CUBICSPLINE Hermite basis, applied component-wise -- same formula as
                // GltfImportCore::HermiteEvaluate (the bone-channel equivalent), but evaluated
                // lazily here at playback time rather than baked/resampled at import time (see
                // MorphWeightTrackEXT's own doc comment for why).
                if (track.CubicSpline &&
                    !track.Keys[i].OutTangent.empty() && !track.Keys[i + 1].InTangent.empty())
                {
                    const std::vector<float>& outTangentA = track.Keys[i].OutTangent;
                    const std::vector<float>& inTangentB = track.Keys[i + 1].InTangent;
                    const float s = amount, s2 = s * s, s3 = s2 * s;
                    const float h00 = 2.0f * s3 - 3.0f * s2 + 1.0f;
                    const float h10 = s3 - 2.0f * s2 + s;
                    const float h01 = -2.0f * s3 + 3.0f * s2;
                    const float h11 = s3 - s2;
                    const float dt = static_cast<float>(span);

                    std::vector<float> result(a.size());
                    for (std::size_t k = 0; k < a.size(); ++k)
                    {
                        result[k] = h00 * a[k] + dt * h10 * outTangentA[k]
                                  + h01 * b[k] + dt * h11 * inTangentB[k];
                    }
                    return result;
                }

                std::vector<float> result(a.size());
                for (std::size_t k = 0; k < a.size(); ++k)
                {
                    result[k] = a[k] + (b[k] - a[k]) * amount;
                }
                return result;
            }
        }

        return track.Keys.back().Weights;
    }
}
