#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "CNA/Internal/Input/InputManager.hpp"

#include <cmath>

namespace Microsoft::Xna::Framework::Input
{
    float GamePad::ExcludeAxisDeadZone(float value, float deadZone)
    {
        if (value < -deadZone)
            value += deadZone;
        else if (value > deadZone)
            value -= deadZone;
        else
            return 0.0f;
        return value / (1.0f - deadZone);
    }

    GamePadCapabilities GamePad::GetCapabilities(PlayerIndex playerIndex)
    {
        const auto raw = CNA::Internal::Input::InputManager::GetRawGamePadState(playerIndex);
        GamePadCapabilities caps;
        caps.IsConnected = raw.isConnected;
        if (!raw.isConnected)
            return caps;

        caps.HasAButton             = true;
        caps.HasBButton             = true;
        caps.HasXButton             = true;
        caps.HasYButton             = true;
        caps.HasBackButton          = true;
        caps.HasStartButton         = true;
        caps.HasLeftShoulderButton  = true;
        caps.HasRightShoulderButton = true;
        caps.HasLeftStickButton     = true;
        caps.HasRightStickButton    = true;
        caps.HasDPadUpButton        = true;
        caps.HasDPadDownButton      = true;
        caps.HasDPadLeftButton      = true;
        caps.HasDPadRightButton     = true;
        caps.HasLeftXThumbStick     = true;
        caps.HasLeftYThumbStick     = true;
        caps.HasRightXThumbStick    = true;
        caps.HasRightYThumbStick    = true;
        caps.HasLeftTrigger         = true;
        caps.HasRightTrigger        = true;
        caps.GamePadType_           = GamePadType::GamePad;
        return caps;
    }

    GamePadState GamePad::GetState(PlayerIndex playerIndex)
    {
        return GetState(playerIndex, GamePadDeadZone::IndependentAxes);
    }

    GamePadState GamePad::GetState(PlayerIndex playerIndex, GamePadDeadZone deadZoneMode)
    {
        const auto raw = CNA::Internal::Input::InputManager::GetRawGamePadState(playerIndex);
        if (!raw.isConnected)
            return GamePadState();

        const GamePadThumbSticks thumbSticks(
            Microsoft::Xna::Framework::Vector2(raw.leftX, raw.leftY),
            Microsoft::Xna::Framework::Vector2(raw.rightX, raw.rightY),
            deadZoneMode);

        const GamePadTriggers triggers(raw.leftTrigger, raw.rightTrigger, deadZoneMode);
        const GamePadButtons  buttons(raw.buttons);
        const GamePadDPad     dpad = GamePadDPad::FromButtons(raw.buttons);

        return GamePadState(thumbSticks, triggers, buttons, dpad);
    }

    bool GamePad::SetVibration(PlayerIndex playerIndex, float leftMotor, float rightMotor)
    {
        (void)playerIndex; (void)leftMotor; (void)rightMotor;
        return false;
    }

    std::string GamePad::GetGUIDEXT(PlayerIndex playerIndex)
    {
        (void)playerIndex;
        return "";
    }

    void GamePad::SetLightBarEXT(PlayerIndex playerIndex, const Microsoft::Xna::Framework::Color& color)
    {
        (void)playerIndex; (void)color;
    }

    bool GamePad::SetTriggerVibrationEXT(PlayerIndex playerIndex, float leftTrigger, float rightTrigger)
    {
        (void)playerIndex; (void)leftTrigger; (void)rightTrigger;
        return false;
    }

    bool GamePad::GetGyroEXT(PlayerIndex playerIndex, Microsoft::Xna::Framework::Vector3& gyro)
    {
        (void)playerIndex;
        gyro = Microsoft::Xna::Framework::Vector3::Zero;
        return false;
    }

    bool GamePad::GetAccelerometerEXT(PlayerIndex playerIndex, Microsoft::Xna::Framework::Vector3& accel)
    {
        (void)playerIndex;
        accel = Microsoft::Xna::Framework::Vector3::Zero;
        return false;
    }
}
