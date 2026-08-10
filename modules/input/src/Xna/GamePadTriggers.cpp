// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/GamePadTriggers.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "System/Single.hpp"

#include <cmath>

namespace Microsoft::Xna::Framework::Input
{
    GamePadTriggers::GamePadTriggers()
        : left_(0.0f), right_(0.0f)
    {
    }

    GamePadTriggers::GamePadTriggers(float leftTrigger, float rightTrigger)
        : left_(MathHelper::Clamp(leftTrigger, 0.0f, 1.0f)),
          right_(MathHelper::Clamp(rightTrigger, 0.0f, 1.0f))
    {
    }

    GamePadTriggers::GamePadTriggers(float leftTrigger, float rightTrigger,
                                     GamePadDeadZone deadZoneMode)
    {
        if (deadZoneMode == GamePadDeadZone::None)
        {
            left_  = MathHelper::Clamp(leftTrigger,  0.0f, 1.0f);
            right_ = MathHelper::Clamp(rightTrigger, 0.0f, 1.0f);
        }
        else
        {
            left_  = MathHelper::Clamp(
                GamePad::ExcludeAxisDeadZone(leftTrigger,  GamePad::TriggerThreshold),
                0.0f, 1.0f);
            right_ = MathHelper::Clamp(
                GamePad::ExcludeAxisDeadZone(rightTrigger, GamePad::TriggerThreshold),
                0.0f, 1.0f);
        }
    }

    float GamePadTriggers::getLeftProperty()  const { return left_; }
    float GamePadTriggers::getRightProperty() const { return right_; }

    bool GamePadTriggers::Equals(const GamePadTriggers& other) const
    {
        return MathHelper::WithinEpsilon(left_, other.left_) &&
               MathHelper::WithinEpsilon(right_, other.right_);
    }

    int GamePadTriggers::GetHashCode() const
    {
        // Unsigned wraparound avoids signed-overflow UB (INPUT-BUILD-006); result is unchanged.
        return static_cast<int>(static_cast<unsigned>(System::Single::GetHashCode(left_))
                                + static_cast<unsigned>(System::Single::GetHashCode(right_)));
    }

    bool operator==(const GamePadTriggers& left, const GamePadTriggers& right)
    {
        return left.Equals(right);
    }

    bool operator!=(const GamePadTriggers& left, const GamePadTriggers& right)
    {
        return !(left == right);
    }
}
