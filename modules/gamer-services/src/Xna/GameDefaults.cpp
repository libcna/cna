// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/GameDefaults.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    GameDefaults::GameDefaults() = default;

    GameDefaults GameDefaults::CreateInternal()
    {
        return GameDefaults();
    }

    GameDifficulty GameDefaults::getGameDifficultyProperty() const            { return gameDifficulty_; }
    ControllerSensitivity GameDefaults::getControllerSensitivityProperty() const { return controllerSensitivity_; }
    std::optional<Color> GameDefaults::getPrimaryColorProperty() const        { return primaryColor_; }
    std::optional<Color> GameDefaults::getSecondaryColorProperty() const      { return secondaryColor_; }
    bool GameDefaults::getAutoAimProperty() const                             { return autoAim_; }
    bool GameDefaults::getAutoCenterProperty() const                         { return autoCenter_; }
    bool GameDefaults::getMoveWithRightThumbStickProperty() const            { return moveWithRightThumbStick_; }
    bool GameDefaults::getInvertYAxisProperty() const                        { return invertYAxis_; }
    bool GameDefaults::getManualTransmissionProperty() const                 { return manualTransmission_; }
    RacingCameraAngle GameDefaults::getRacingCameraAngleProperty() const      { return racingCameraAngle_; }
    bool GameDefaults::getAccelerateWithButtonsProperty() const              { return accelerateWithButtons_; }
    bool GameDefaults::getBrakeWithButtonsProperty() const                   { return brakeWithButtons_; }
}
