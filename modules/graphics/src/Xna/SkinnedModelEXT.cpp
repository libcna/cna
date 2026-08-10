// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "System/ArgumentException.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    namespace
    {
        Matrix SampleTrack(const BoneTrackEXT& track, System::TimeSpan pos)
        {
            const auto& keys = track.Keys;
            if (keys.size() == 1 || pos <= keys.front().Time)
            {
                const auto& k = keys.front();
                return Matrix::CreateScale(k.Scale) * Matrix::CreateFromQuaternion(k.Rotation)
                     * Matrix::CreateTranslation(k.Translation);
            }
            if (pos >= keys.back().Time)
            {
                const auto& k = keys.back();
                return Matrix::CreateScale(k.Scale) * Matrix::CreateFromQuaternion(k.Rotation)
                     * Matrix::CreateTranslation(k.Translation);
            }

            std::size_t next = 1;
            while (next < keys.size() && keys[next].Time < pos)
            {
                ++next;
            }
            const auto& a = keys[next - 1];
            const auto& b = keys[next];

            const double spanSeconds = (b.Time - a.Time).getTotalSecondsProperty();
            const float amount = spanSeconds > 0.0
                ? static_cast<float>((pos - a.Time).getTotalSecondsProperty() / spanSeconds)
                : 0.0f;

            const Vector3 translation = Vector3::Lerp(a.Translation, b.Translation, amount);
            const Vector3 scale = Vector3::Lerp(a.Scale, b.Scale, amount);
            const Quaternion rotation = Quaternion::Slerp(a.Rotation, b.Rotation, amount);

            return Matrix::CreateScale(scale) * Matrix::CreateFromQuaternion(rotation)
                 * Matrix::CreateTranslation(translation);
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
        if (other.BoneCount != BoneCount)
        {
            throw System::ArgumentException(
                "AttachPartEXT: other.BoneCount (" + std::to_string(other.BoneCount)
                    + ") does not match this model's BoneCount (" + std::to_string(BoneCount) + ")",
                "other");
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

        // Task 11.3: ParentBoneIndices/BindPoseLocal/InverseBindPoseGlobal are all populated
        // straight from file content and then indexed up to BoneCount below with no check that
        // .size() == BoneCount - a corrupt/truncated .skeleton.bin (or a hand-constructed model,
        // e.g. in a test) previously produced real out-of-bounds std::vector::operator[] reads
        // instead of a clean, catchable error.
        if (static_cast<int>(ParentBoneIndices.size()) != BoneCount
            || static_cast<int>(BindPoseLocal.size()) != BoneCount
            || static_cast<int>(InverseBindPoseGlobal.size()) != BoneCount)
        {
            throw System::ArgumentException(
                "SkinnedModelEXT's BoneCount (" + std::to_string(BoneCount)
                    + ") does not match ParentBoneIndices.size() (" + std::to_string(ParentBoneIndices.size())
                    + "), BindPoseLocal.size() (" + std::to_string(BindPoseLocal.size())
                    + "), or InverseBindPoseGlobal.size() (" + std::to_string(InverseBindPoseGlobal.size()) + ")"
            );
        }

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
                localTransforms[static_cast<std::size_t>(track.BoneIndex)] = SampleTrack(track, pos);
            }
        }

        // Bones are stored in topological (breadth-first) order, so each parent's world
        // transform is already finalized by the time a child bone is processed.
        std::vector<Matrix> worldTransforms(static_cast<std::size_t>(BoneCount));
        for (int i = 0; i < BoneCount; ++i)
        {
            const int parent = ParentBoneIndices[static_cast<std::size_t>(i)];
            // Task 11.2: "topological order" above is a convention enforced only by the content
            // pipeline, never checked here - malformed/future content with parent[i] >= i (which
            // also transitively rules out any cycle: a cycle's maximum-index member would need a
            // parent with a strictly greater index than itself, violating this same check)
            // otherwise silently read a not-necessarily-finalized worldTransforms[parent] with no
            // error.
            if (parent >= i)
            {
                throw System::ArgumentException(
                    "ParentBoneIndices is not topologically ordered: bone " + std::to_string(i)
                        + " has parent index " + std::to_string(parent)
                        + ", which must be less than the bone's own index"
                );
            }
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
