// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimation.hpp"
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "CNA/Internal/GamerServices/AvatarSpace.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/OverflowException.hpp"
#include <cmath>
#include <stdexcept>

namespace Microsoft::Xna::Framework::GamerServices
{
    namespace
    {
        namespace Avatars = CNA::Internal::GamerServices::Avatars;

        const Avatars::AvatarClipLibrary& Library()
        {
            try
            {
                return Avatars::clipLibrary();
            }
            catch (const std::exception& error)
            {
                throw System::InvalidOperationException(std::string("The avatar animation assets are unavailable: ") + error.what());
            }
        }
    }

    AvatarAnimation::AvatarAnimation(AvatarAnimationPreset animationPreset)
        : avatarBones_(Avatars::BoneCount)
        , currentPosition_(System::TimeSpan::Zero)
        , length_(System::TimeSpan::Zero)
    {
        const auto& library = Library();
        const int preset = static_cast<int>(animationPreset);
        if (preset >= 0 && preset < static_cast<int>(library.clips.size()))
        {
            preset_ = preset;
            length_ = System::TimeSpan::FromTicks(
                static_cast<SharpRuntime::longcs>(std::llround(library.clips[preset].duration * 1.0e7)));
        }
        Update(System::TimeSpan::Zero, false);
    }

    System::Collections::ObjectModel::ReadOnlyCollection<Microsoft::Xna::Framework::Matrix>
    AvatarAnimation::getBoneTransformsProperty() const
    {
        return System::Collections::ObjectModel::ReadOnlyCollection<Microsoft::Xna::Framework::Matrix>(avatarBones_);
    }

    System::TimeSpan AvatarAnimation::getCurrentPositionProperty() const { return currentPosition_; }

    void AvatarAnimation::setCurrentPositionProperty(System::TimeSpan value)
    {
        currentPosition_ = value;
        Update(System::TimeSpan::Zero, false);
    }

    System::TimeSpan AvatarAnimation::getLengthProperty() const { return length_; }

    AvatarExpression AvatarAnimation::getExpressionProperty() const { return currentExpression_; }

    void AvatarAnimation::Update(System::TimeSpan elapsedAnimationTime, bool loop)
    {
        if (isDisposed_)
        {
            throw System::ObjectDisposedException("AvatarAnimation");
        }
        try
        {
            currentPosition_ = currentPosition_ + elapsedAnimationTime;
        }
        catch (const System::OverflowException&)
        {
            currentPosition_ = elapsedAnimationTime > System::TimeSpan::Zero ? length_ : System::TimeSpan::Zero;
        }
        // Wraps by the remainder of the length exactly as the reference does, including its
        // choice to keep a position equal to Length rather than wrapping it to zero.
        if (currentPosition_ > length_)
        {
            currentPosition_ = loop && length_ != System::TimeSpan::Zero
                ? System::TimeSpan::FromTicks(currentPosition_.getTicksProperty() % length_.getTicksProperty())
                : length_;
        }
        else if (currentPosition_ < System::TimeSpan::Zero)
        {
            currentPosition_ = loop && length_ != System::TimeSpan::Zero
                ? length_ + System::TimeSpan::FromTicks(currentPosition_.getTicksProperty() % length_.getTicksProperty())
                : System::TimeSpan::Zero;
        }
        Sample();
    }

    void AvatarAnimation::Sample()
    {
        const auto& library = Library();
        std::array<Microsoft::Xna::Framework::Quaternion, Avatars::BoneCount> rotations;
        Microsoft::Xna::Framework::Vector3 root;
        Avatars::AvatarExpressionKey expression{};
        if (preset_ >= 0)
        {
            const double seconds = static_cast<double>(currentPosition_.getTicksProperty()) / 1.0e7;
            expression = Avatars::sampleClip(library.clips[preset_], seconds, rotations, root);
        }
        else
        {
            rotations.fill(Microsoft::Xna::Framework::Quaternion::Identity);
        }
        for (int bone = 0; bone < Avatars::BoneCount; ++bone)
        {
            // Animation poses are deltas from BindPose, not a second copy of its offsets.
            const auto translation = bone == 0 ? root : Microsoft::Xna::Framework::Vector3::Zero;
            avatarBones_[bone] = Avatars::changeAvatarSpace(
                Microsoft::Xna::Framework::Matrix::CreateFromQuaternion(rotations[bone]) *
                Microsoft::Xna::Framework::Matrix::CreateTranslation(translation.X, translation.Y, translation.Z));
        }
        currentExpression_.setMouthProperty(static_cast<AvatarMouth>(expression.mouth));
        currentExpression_.setLeftEyeProperty(static_cast<AvatarEye>(expression.leftEye));
        currentExpression_.setRightEyeProperty(static_cast<AvatarEye>(expression.rightEye));
        currentExpression_.setLeftEyebrowProperty(static_cast<AvatarEyebrow>(expression.leftEyebrow));
        currentExpression_.setRightEyebrowProperty(static_cast<AvatarEyebrow>(expression.rightEyebrow));
    }

    bool AvatarAnimation::getIsDisposedProperty() const { return isDisposed_; }

    void AvatarAnimation::Dispose()
    {
        Dispose(true);
    }

    void AvatarAnimation::Dispose(bool /*disposing*/)
    {
        isDisposed_ = true;
    }
}
