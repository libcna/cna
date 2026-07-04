// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimation.hpp"
#include "System/ObjectDisposedException.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    // The real XNA implementation never reads the animationPreset argument: every instance
    // ends up with 71 zero-valued bone transforms and a zero-length animation, regardless of
    // which preset was requested. Preserved exactly, not "fixed."
    AvatarAnimation::AvatarAnimation(AvatarAnimationPreset /*animationPreset*/)
        : avatarBones_(71)
        , currentPosition_(System::TimeSpan::Zero)
        , length_(System::TimeSpan::Zero)
    {
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

        currentPosition_ = currentPosition_ + elapsedAnimationTime;

        if (currentPosition_ > length_)
        {
            if (loop && length_ != System::TimeSpan::Zero)
            {
                while (currentPosition_ > length_)
                {
                    currentPosition_ = currentPosition_ - length_;
                }
            }
            else
            {
                currentPosition_ = length_;
            }
        }
        else if (currentPosition_ < System::TimeSpan::Zero)
        {
            if (loop && length_ != System::TimeSpan::Zero)
            {
                while (currentPosition_ < System::TimeSpan::Zero)
                {
                    currentPosition_ = currentPosition_ + length_;
                }
            }
            else
            {
                currentPosition_ = System::TimeSpan::Zero;
            }
        }
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
