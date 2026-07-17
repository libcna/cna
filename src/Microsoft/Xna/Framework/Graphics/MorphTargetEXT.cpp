// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/MorphTargetEXT.hpp"

#include <cmath>
#include <cstring>
#include <stdexcept>

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
        // Normal deltas only apply to strides with a Normal at a fixed offset 12 (Position is
        // always at offset 0, for every stride this project uses).
        const bool hasNormalSlot = (stride == 32 || stride == 52 || stride == 56);
        const int numVertices = stride > 0
            ? static_cast<int>(morph.BaseVertexBytes.size()) / stride : 0;

        std::vector<std::uint8_t> blended = morph.BaseVertexBytes;

        for (int v = 0; v < numVertices; ++v)
        {
            Vector3 posDelta;
            Vector3 normDelta;
            bool anyNormalDelta = false;

            for (std::size_t t = 0; t < weights.size(); ++t)
            {
                const float w = weights[t];
                if (w == 0.0f) { continue; }
                posDelta = posDelta + w * morph.PositionDeltas[t][static_cast<std::size_t>(v)];
                if (!morph.NormalDeltas[t].empty())
                {
                    normDelta = normDelta + w * morph.NormalDeltas[t][static_cast<std::size_t>(v)];
                    anyNormalDelta = true;
                }
            }

            const std::size_t off = static_cast<std::size_t>(v) * static_cast<std::size_t>(stride);

            float basePos[3];
            std::memcpy(basePos, blended.data() + off, sizeof(basePos));
            const Vector3 newPos(basePos[0] + posDelta.X, basePos[1] + posDelta.Y, basePos[2] + posDelta.Z);
            std::memcpy(blended.data() + off, &newPos, sizeof(newPos));

            if (hasNormalSlot && anyNormalDelta)
            {
                float baseNorm[3];
                std::memcpy(baseNorm, blended.data() + off + 12, sizeof(baseNorm));
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
                std::memcpy(blended.data() + off + 12, &newNorm, sizeof(newNorm));
            }
        }

        return blended;
    }

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
                if (track.StepInterpolation) { return track.Keys[i].Weights; }

                const double span = t1 - t0;
                const float amount = span > 0.0 ? static_cast<float>((timeSeconds - t0) / span) : 0.0f;
                const std::vector<float>& a = track.Keys[i].Weights;
                const std::vector<float>& b = track.Keys[i + 1].Weights;
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
