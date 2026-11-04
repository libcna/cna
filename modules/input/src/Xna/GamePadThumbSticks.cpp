// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/GamePadThumbSticks.hpp"
#include <bit>
#include <algorithm>
#include <cmath>
#include <cstdint>

#include <string>
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"

namespace Microsoft::Xna::Framework::Input
{
    using Microsoft::Xna::Framework::Vector2;

    namespace
    {
        Vector2 ApplyStickDeadZone(const Vector2& value, GamePadDeadZone mode, float deadZone)
        {
            const auto sample = [](float value) {
                // XNA FillInternalState IL_030a-0315 et seq.: truncate the normalized axis * 32767.
                // Clamp before truncation; keep a floating representation to avoid NaN-to-int UB.
                return std::trunc(std::clamp(value, -1.0f, 1.0f) * 32767.0f);
            };
            const float x = sample(value.X);
            const float y = sample(value.Y);
            const auto linear = [](float value, float zone) {
                // XNA ApplyLinearDeadZone IL_0000-0033, including the output clamp.
                if (value < -zone) value += zone;
                else if (value > zone) value -= zone;
                else return 0.0f;
                return std::clamp(value / (32767.0f - zone), -1.0f, 1.0f);
            };
            // XNA ApplyStickDeadZone IL_0004-0029 / IL_009d-00c8: integer axes, max 32767.
            if (mode == GamePadDeadZone::IndependentAxes)
                return Vector2{linear(x, deadZone), linear(y, deadZone)};
            if (mode != GamePadDeadZone::Circular)
                return Vector2{linear(x, 0.0f), linear(y, 0.0f)};
            // XNA Circular IL_0037-0096: integer squared length -> double sqrt -> Single radius.
            // Products and sum are exact in double throughout the reconstructed short domain.
            const float radius = static_cast<float>(std::sqrt(double(x) * x + double(y) * y));
            const float length = linear(radius, deadZone);
            const float scale = length > 0.0f ? length / radius : 0.0f;
            return Vector2{std::clamp(x * scale, -1.0f, 1.0f),
                          std::clamp(y * scale, -1.0f, 1.0f)};
        }
    }

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
        : left_(ApplyStickDeadZone(leftPosition, deadZoneMode, 7849.0f)),
          right_(ApplyStickDeadZone(rightPosition, deadZoneMode, 8689.0f))
    {
    }

    const Vector2& GamePadThumbSticks::getLeftProperty()  const { return left_; }
    const Vector2& GamePadThumbSticks::getRightProperty() const { return right_; }

    void GamePadThumbSticks::ApplySquareClamp()
    {
        left_.X  = MathHelper::Clamp(left_.X,  -1.0f, 1.0f);
        left_.Y  = MathHelper::Clamp(left_.Y,  -1.0f, 1.0f);
        right_.X = MathHelper::Clamp(right_.X, -1.0f, 1.0f);
        right_.Y = MathHelper::Clamp(right_.Y, -1.0f, 1.0f);
    }

    std::string GamePadThumbSticks::ToString() const
    {
        return "{Left:" + getLeftProperty().ToString() + " Right:" + getRightProperty().ToString() + "}";
    }

    bool GamePadThumbSticks::Equals(const std::any& obj) const
    {
        const GamePadThumbSticks* other = std::any_cast<GamePadThumbSticks>(&obj);
        return other != nullptr && Equals(*other);
    }

    bool GamePadThumbSticks::Equals(const GamePadThumbSticks& other) const
    {
        return left_ == other.left_ && right_ == other.right_;
    }

    int GamePadThumbSticks::GetHashCode() const
    {
        // XNA GetHashCode IL_0000..0010 + Helpers.SmartGetHashCode IL_0028..0048:
        // Vector2 contributes both stored float words, not Vector2.GetHashCode().
        const uint32_t hash = std::bit_cast<uint32_t>(left_.X) ^ std::bit_cast<uint32_t>(left_.Y)
            ^ std::bit_cast<uint32_t>(right_.X) ^ std::bit_cast<uint32_t>(right_.Y);
        return hash == 0 ? 0x7fffffff : std::bit_cast<int32_t>(hash);
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
