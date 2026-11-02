// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "System/ArgumentException.hpp"

#include <cmath>
#include "../Internal/AnimationClipSampling.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    namespace
    {
        bool FiniteMatrix(const Matrix& matrix)
        {
            for (const float value : {matrix.M11, matrix.M12, matrix.M13, matrix.M14,
                     matrix.M21, matrix.M22, matrix.M23, matrix.M24,
                     matrix.M31, matrix.M32, matrix.M33, matrix.M34,
                     matrix.M41, matrix.M42, matrix.M43, matrix.M44})
            {
                if (!std::isfinite(value)) { return false; }
            }
            return true;
        }

        void ValidateSkeleton(const SkinnedModelEXT& model)
        {
            if (model.BoneCount < 0
                || model.ParentBoneIndices.size() != static_cast<std::size_t>(model.BoneCount)
                || model.BindPoseLocal.size() != static_cast<std::size_t>(model.BoneCount)
                || model.InverseBindPoseGlobal.size() != static_cast<std::size_t>(model.BoneCount))
            {
                throw System::ArgumentException("SkinnedModelEXT skeleton arrays must match a nonnegative BoneCount.");
            }
            for (int index = 0; index < model.BoneCount; ++index)
            {
                const auto slot = static_cast<std::size_t>(index);
                if (model.ParentBoneIndices[slot] >= index
                    || !FiniteMatrix(model.BindPoseLocal[slot])
                    || !FiniteMatrix(model.InverseBindPoseGlobal[slot]))
                {
                    throw System::ArgumentException("SkinnedModelEXT requires topologically ordered parents and finite bind poses.");
                }
            }
        }
    }

    SkinnedModelEXT::SkinnedModelEXT() = default;
    SkinnedModelEXT::~SkinnedModelEXT() = default;
    SkinnedModelEXT::SkinnedModelEXT(SkinnedModelEXT&&) noexcept = default;
    SkinnedModelEXT& SkinnedModelEXT::operator=(SkinnedModelEXT&&) noexcept = default;

    void SkinnedModelEXT::AddPartEXT(std::string name,
                                      std::unique_ptr<VertexBuffer> vertexBuffer,
                                      std::unique_ptr<IndexBuffer> indexBuffer,
                                      std::unique_ptr<ModelMeshPart> part,
                                      Texture2D texture)
    {
        Texture2D* texturePtr = nullptr;
        if (texture.HasRenderer())
        {
            textures_.push_back(std::make_unique<Texture2D>(std::move(texture)));
            texturePtr = textures_.back().get();
        }
        Parts.push_back(PartEXT{std::move(name), part.get(), texturePtr});
        vertexBuffers_.push_back(std::move(vertexBuffer));
        indexBuffers_.push_back(std::move(indexBuffer));
        ownedParts_.push_back(std::move(part));
    }

    void SkinnedModelEXT::RemovePartEXT(const std::string& name)
    {
        // Parts/vertexBuffers_/indexBuffers_/ownedParts_ are always parallel arrays (every
        // AddPartEXT call, and AttachPartEXT's own append loop below, push to all 4 in lockstep),
        // so a matching index i identifies the same part's entries in all 4 at once. textures_ is
        // NOT parallel (only populated when a part actually has a real texture), so its matching
        // entry (if any) is found by pointer instead.
        for (std::size_t i = 0; i < Parts.size(); )
        {
            if (Parts[i].Name != name)
            {
                ++i;
                continue;
            }

            if (Parts[i].Texture != nullptr)
            {
                const Texture2D* texturePtr = Parts[i].Texture;
                for (std::size_t t = 0; t < textures_.size(); ++t)
                {
                    if (textures_[t].get() == texturePtr)
                    {
                        textures_.erase(textures_.begin() + static_cast<std::ptrdiff_t>(t));
                        break;
                    }
                }
            }

            Parts.erase(Parts.begin() + static_cast<std::ptrdiff_t>(i));
            vertexBuffers_.erase(vertexBuffers_.begin() + static_cast<std::ptrdiff_t>(i));
            indexBuffers_.erase(indexBuffers_.begin() + static_cast<std::ptrdiff_t>(i));
            ownedParts_.erase(ownedParts_.begin() + static_cast<std::ptrdiff_t>(i));
            // Don't increment i - the next element has shifted into position i.
        }
    }

    void SkinnedModelEXT::AttachPartEXT(SkinnedModelEXT&& other)
    {
        if (this == &other)
        {
            throw System::ArgumentException("A SkinnedModelEXT cannot attach its own parts.", "other");
        }
        if (other.BoneCount != BoneCount)
        {
            throw System::ArgumentException(
                "AttachPartEXT: other.BoneCount (" + std::to_string(other.BoneCount)
                    + ") does not match this model's BoneCount (" + std::to_string(BoneCount) + ")",
                "other");
        }

        if (other.Parts.empty()) { return; }
        ValidateSkeleton(*this);
        ValidateSkeleton(other);
        if (ParentBoneIndices != other.ParentBoneIndices
            || BindPoseLocal != other.BindPoseLocal
            || InverseBindPoseGlobal != other.InverseBindPoseGlobal)
        {
            throw System::ArgumentException("Attached SkinnedModelEXT parts require identical indexed skeletons.", "other");
        }
        const GraphicsDevice* device = nullptr;
        const auto checkResource = [&](const GraphicsResource* resource) {
            if (resource == nullptr) { return; }
            const auto* candidate = resource->getGraphicsDeviceProperty();
            if (candidate == nullptr) { return; }
            if (device != nullptr && candidate != device)
            {
                throw System::ArgumentException("Attached SkinnedModelEXT resources must belong to one graphics device.", "other");
            }
            device = candidate;
        };
        for (const auto* model : {this, &other})
        {
            if (model->Parts.size() != model->vertexBuffers_.size()
                || model->Parts.size() != model->indexBuffers_.size()
                || model->Parts.size() != model->ownedParts_.size())
            {
                throw System::ArgumentException("SkinnedModelEXT public parts no longer match their owned resources.");
            }
            for (const auto& buffer : model->vertexBuffers_) { checkResource(buffer.get()); }
            for (const auto& buffer : model->indexBuffers_) { checkResource(buffer.get()); }
            for (const auto& texture : model->textures_) { checkResource(texture.get()); }
            for (const auto& part : model->Parts)
            {
                checkResource(part.Texture);
                if (part.Part != nullptr)
                {
                    checkResource(part.Part->getVertexBufferProperty());
                    checkResource(part.Part->getIndexBufferProperty());
                }
            }
        }

        // Task 11.4: replace-by-name - remove any part(s) this model already has under an
        // incoming part's name before appending, so re-attaching a same-named replacement (e.g.
        // swapping hairstyles at runtime) doesn't silently leave both the old and new part
        // rendered simultaneously.
        for (const auto& part : other.Parts)
        {
            RemovePartEXT(part.Name);
        }

        for (auto& part : other.Parts)
        {
            Parts.push_back(part);
        }
        other.Parts.clear();

        for (auto& vb : other.vertexBuffers_) { vertexBuffers_.push_back(std::move(vb)); }
        for (auto& ib : other.indexBuffers_) { indexBuffers_.push_back(std::move(ib)); }
        for (auto& p : other.ownedParts_) { ownedParts_.push_back(std::move(p)); }
        for (auto& t : other.textures_) { textures_.push_back(std::move(t)); }
        other.vertexBuffers_.clear();
        other.indexBuffers_.clear();
        other.ownedParts_.clear();
        other.textures_.clear();
    }

    void SkinnedModelEXT::ComputeBoneTransformsEXT(const std::string& clipName,
                                                    System::TimeSpan position,
                                                    bool loop,
                                                    std::vector<Matrix>& outWorldBones) const
    {
        const auto it = Clips.find(clipName);
        if (it == Clips.end())
        {
            throw System::ArgumentException("Unknown animation clip: " + clipName, "clipName");
        }
        const AnimationClipEXT& clip = it->second;

        ValidateSkeleton(*this);
        CNA::Internal::ValidateAnimationClip(clip, ClipTargetSpaceEXT::JointPalette);

        System::TimeSpan pos = position;
        if (clip.Duration > System::TimeSpan::Zero)
        {
            if (loop)
            {
                // Task 11.1: was an iterative subtract/add-one-Duration-at-a-time wraparound -
                // a real unbounded per-frame cost proportional to position/Duration for a
                // long-running session with a short clip. TimeSpan has no modulo operator, so
                // compute it via raw ticks instead: C++'s `%` truncates toward zero (sign follows
                // the dividend), so a single `+= durationTicks` normalizes a negative remainder
                // into `[0, durationTicks)` - equivalent to floor-mod, without ever looping.
                const auto durationTicks = clip.Duration.getTicksProperty();
                auto posTicks = pos.getTicksProperty() % durationTicks;
                if (posTicks < 0) { posTicks += durationTicks; }
                pos = System::TimeSpan::FromTicks(posTicks);
            }
            else
            {
                if (pos > clip.Duration) { pos = clip.Duration; }
                if (pos < System::TimeSpan::Zero) { pos = System::TimeSpan::Zero; }
            }
        }
        else
        {
            pos = System::TimeSpan::Zero;
        }

        std::vector<Matrix> localTransforms(BindPoseLocal);
        for (const auto& track : clip.Tracks)
        {
            if (!track.Keys.empty() && track.BoneIndex >= 0
                && track.BoneIndex < static_cast<int>(localTransforms.size()))
            {
                localTransforms[static_cast<std::size_t>(track.BoneIndex)] = CNA::Internal::SampleAnimationTrack(track, pos);
            }
        }

        // Bones are stored in topological (breadth-first) order, so each parent's world
        // transform is already finalized by the time a child bone is processed.
        std::vector<Matrix> worldTransforms(static_cast<std::size_t>(BoneCount));
        for (int i = 0; i < BoneCount; ++i)
        {
            const int parent = ParentBoneIndices[static_cast<std::size_t>(i)];
            worldTransforms[static_cast<std::size_t>(i)] = parent < 0
                ? localTransforms[static_cast<std::size_t>(i)]
                : localTransforms[static_cast<std::size_t>(i)] * worldTransforms[static_cast<std::size_t>(parent)];
        }

        outWorldBones.assign(static_cast<std::size_t>(BoneCount), Matrix::getIdentityProperty());
        for (int i = 0; i < BoneCount; ++i)
        {
            outWorldBones[static_cast<std::size_t>(i)] =
                InverseBindPoseGlobal[static_cast<std::size_t>(i)] * worldTransforms[static_cast<std::size_t>(i)];
        }
    }
}
