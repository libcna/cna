// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/GamePadTriggers.hpp"
#include <bit>
#include <cstdint>

#include "System/Single.hpp"
#include "System/Globalization/CultureInfo.hpp"
#include <string>
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"

#include <cmath>

namespace Microsoft::Xna::Framework::Input
{
    GamePadTriggers::GamePadTriggers()
        : left_(0.0f), right_(0.0f)
    {
    }

    GamePadTriggers::GamePadTriggers(float leftTrigger, float rightTrigger)
        // Refresh ctor IL_002b..0035/0057..0061 calls Framework Math.Max(value,+0).
        // BCL4 Math.Max(Single) IL_0002..0011 selects +0 for equal zeros; FNA Clamp keeps -0.
        : left_(MathHelper::Clamp(leftTrigger == 0.0f ? 0.0f : leftTrigger, 0.0f, 1.0f)),
          right_(MathHelper::Clamp(rightTrigger == 0.0f ? 0.0f : rightTrigger, 0.0f, 1.0f))
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

    std::string GamePadTriggers::ToString() const
    {
        // Refresh ToString IL_0000..0033 uses CurrentCulture + Framework Single default G7.
        // FNA uses the same format string; C++ iostream's default precision is only six.
        const auto format = [](float value) {
            auto text = System::Single::ToString(value == 0.0f ? 0.0f : value, "G7");
            if (const auto exponent = text.find('e'); exponent != std::string::npos)
                text[exponent] = 'E';
            return System::detail::NumberFormatText::LocalizeFormatted(
                text, &System::Globalization::CultureInfo::getCurrentCultureProperty());
        };
        return "{Left:" + format(getLeftProperty()) + " Right:" + format(getRightProperty()) + "}";
    }

    bool GamePadTriggers::Equals(const std::any& obj) const
    {
        const GamePadTriggers* other = std::any_cast<GamePadTriggers>(&obj);
        return other != nullptr && Equals(*other);
    }

    bool GamePadTriggers::Equals(const GamePadTriggers& other) const
    {
        // XNA GamePadTriggers.op_Equality IL_0000..0022 uses exact comparisons,
        // unlike FNA's WithinEpsilon. Equals(object) delegates to the same operator.
        return left_ == other.left_ && right_ == other.right_;
    }

    int GamePadTriggers::GetHashCode() const
    {
        // XNA GetHashCode IL_0000..0010 + Helpers.SmartGetHashCode IL_0028..0048:
        // XOR stored float words, including their sign/payload bits, then replace zero.
        const uint32_t hash = std::bit_cast<uint32_t>(left_) ^ std::bit_cast<uint32_t>(right_);
        return hash == 0 ? 0x7fffffff : std::bit_cast<int32_t>(hash);
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
