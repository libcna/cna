// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/GamePadThumbSticks.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"

namespace Microsoft::Xna::Framework::Input
{
    using Microsoft::Xna::Framework::Vector2;

    GamePadThumbSticks::GamePadThumbSticks()
        : left_(Vector2::Zero), right_(Vector2::Zero)
    {
    }

    GamePadThumbSticks::GamePadThumbSticks(const Vector2& leftPosition, const Vector2& rightPosition)
        : left_(leftPosition), right_(rightPosition)
    {
        ApplySquareClamp();
    }

    GamePadThumbSticks::GamePadThumbSticks(const Vector2& leftPosition, const Vector2& rightPosition,
                                           GamePadDeadZone deadZoneMode)
        : left_(leftPosition), right_(rightPosition)
    {
        ApplyDeadZone(deadZoneMode);
        if (deadZoneMode == GamePadDeadZone::Circular)
            ApplyCircularClamp();
        else
            ApplySquareClamp();
    }

    const Vector2& GamePadThumbSticks::getLeftProperty()  const { return left_; }
    const Vector2& GamePadThumbSticks::getRightProperty() const { return right_; }

    void GamePadThumbSticks::ApplyDeadZone(GamePadDeadZone dz)
    {
        switch (dz)
        {
        case GamePadDeadZone::None:
            break;
        case GamePadDeadZone::IndependentAxes:
            left_.X  = GamePad::ExcludeAxisDeadZone(left_.X,  GamePad::LeftDeadZone);
            left_.Y  = GamePad::ExcludeAxisDeadZone(left_.Y,  GamePad::LeftDeadZone);
            right_.X = GamePad::ExcludeAxisDeadZone(right_.X, GamePad::RightDeadZone);
            right_.Y = GamePad::ExcludeAxisDeadZone(right_.Y, GamePad::RightDeadZone);
            break;
        case GamePadDeadZone::Circular:
            left_  = ExcludeCircularDeadZone(left_,  GamePad::LeftDeadZone);
            right_ = ExcludeCircularDeadZone(right_, GamePad::RightDeadZone);
            break;
        }
    }

    void GamePadThumbSticks::ApplySquareClamp()
    {
        left_.X  = MathHelper::Clamp(left_.X,  -1.0f, 1.0f);
        left_.Y  = MathHelper::Clamp(left_.Y,  -1.0f, 1.0f);
        right_.X = MathHelper::Clamp(right_.X, -1.0f, 1.0f);
        right_.Y = MathHelper::Clamp(right_.Y, -1.0f, 1.0f);
    }

    void GamePadThumbSticks::ApplyCircularClamp()
    {
        if (left_.LengthSquared() > 1.0f)
            left_.Normalize();
        if (right_.LengthSquared() > 1.0f)
            right_.Normalize();
    }

    Vector2 GamePadThumbSticks::ExcludeCircularDeadZone(const Vector2& value, float deadZone)
    {
        float originalLength = value.Length();
        if (originalLength <= deadZone)
            return Vector2::Zero;
        float newLength = (originalLength - deadZone) / (1.0f - deadZone);
        return value * (newLength / originalLength);
    }

    bool GamePadThumbSticks::Equals(const GamePadThumbSticks& other) const
    {
        return left_ == other.left_ && right_ == other.right_;
    }

    int GamePadThumbSticks::GetHashCode() const
    {
        // Unsigned wraparound avoids signed-overflow UB (caught by UBSan, INPUT-BUILD-006) while
        // preserving FNA's unchecked `Left.GetHashCode() + 37 * Right.GetHashCode()` result.
        return static_cast<int>(static_cast<unsigned>(left_.GetHashCode())
                                + 37u * static_cast<unsigned>(right_.GetHashCode()));
    }

    bool operator==(const GamePadThumbSticks& left, const GamePadThumbSticks& right)
    {
        return left.Equals(right);
    }

    bool operator!=(const GamePadThumbSticks& left, const GamePadThumbSticks& right)
    {
        return !(left == right);
    }
}
