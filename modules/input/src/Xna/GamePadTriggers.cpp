// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/GamePadTriggers.hpp"
#include <bit>
#include <cstdint>

#include "System/Single.hpp"
#include "System/Globalization/CultureInfo.hpp"
#include <string>
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"

#include <algorithm>
#include <cmath>

namespace Microsoft::Xna::Framework::Input
{
    namespace
    {
        // XNA builds a polled trigger from its XInput byte (GamePadState..ctor IL_01cf-0202):
        // ApplyTriggerDeadZone IL_0000-0026 -> ApplyLinearDeadZone(byte, 255, None ? 0 : 30), i.e.
        // (byte - deadZone) / (255 - deadZone) in Single arithmetic, zero at or below the dead zone.
        // The byte is the sample the button query reads, trunc(value * 255), which recovers every
        // byte b exactly from a platform value of b / 255.
        float PolledTrigger(float value, GamePadDeadZone deadZoneMode)
        {
            const float sample = std::isnan(value) ? 0.0f : std::trunc(std::clamp(value, 0.0f, 1.0f) * 255.0f);
            const float deadZone = deadZoneMode == GamePadDeadZone::None ? 0.0f : 30.0f;
            if (!(sample > deadZone)) return 0.0f;
            return MathHelper::Clamp((sample - deadZone) / (255.0f - deadZone), -1.0f, 1.0f);
        }
    }

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
        : left_(PolledTrigger(leftTrigger, deadZoneMode)),
          right_(PolledTrigger(rightTrigger, deadZoneMode))
    {
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
