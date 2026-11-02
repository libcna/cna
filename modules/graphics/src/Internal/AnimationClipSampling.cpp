// SPDX-License-Identifier: MS-PL
#include "AnimationClipSampling.hpp"
#include "System/ArgumentException.hpp"

#include <cmath>
#include <cstdint>

namespace CNA::Internal
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    void ValidateAnimationClip(const AnimationClipEXT& clip, ClipTargetSpaceEXT expectedSpace)
    {
        if (clip.TargetSpace != expectedSpace)
        {
            throw System::ArgumentException("Animation clip targets the wrong index space.");
        }
        for (const auto& track : clip.Tracks)
        {
            for (std::size_t index = 0; index < track.Keys.size(); ++index)
            {
                const auto& key = track.Keys[index];
                if (index != 0 && key.Time < track.Keys[index - 1].Time)
                {
                    throw System::ArgumentException("Animation keyframe times must be nondecreasing.");
                }
                for (const float value : {key.Translation.X, key.Translation.Y, key.Translation.Z,
                         key.Rotation.X, key.Rotation.Y, key.Rotation.Z, key.Rotation.W,
                         key.Scale.X, key.Scale.Y, key.Scale.Z})
                {
                    if (!std::isfinite(value))
                    {
                        throw System::ArgumentException("Animation keyframe transforms must be finite.");
                    }
                }
            }
        }
    }

    Matrix SampleAnimationTrack(const BoneTrackEXT& track, System::TimeSpan pos)
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

        // Ordered ticks can span the whole signed range; unsigned differences preserve that
        // nonnegative distance without overflowing TimeSpan or losing low ticks to cancellation.
        const auto spanTicks = static_cast<std::uint64_t>(b.Time.getTicksProperty())
            - static_cast<std::uint64_t>(a.Time.getTicksProperty());
        const auto elapsedTicks = static_cast<std::uint64_t>(pos.getTicksProperty())
            - static_cast<std::uint64_t>(a.Time.getTicksProperty());
        const float amount = spanTicks != 0
            ? static_cast<float>(static_cast<double>(elapsedTicks) / static_cast<double>(spanTicks))
            : 0.0f;

        const Vector3 translation = Vector3::Lerp(a.Translation, b.Translation, amount);
        const Vector3 scale = Vector3::Lerp(a.Scale, b.Scale, amount);
        const Quaternion rotation = Quaternion::Slerp(a.Rotation, b.Rotation, amount);

        return Matrix::CreateScale(scale) * Matrix::CreateFromQuaternion(rotation)
             * Matrix::CreateTranslation(translation);
    }
}
