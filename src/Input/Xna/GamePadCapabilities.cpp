// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/GamePadCapabilities.hpp"

namespace Microsoft::Xna::Framework::Input
{
    bool GamePadCapabilities::getIsConnectedProperty() const { return isConnected_; }
    void GamePadCapabilities::setIsConnectedProperty(bool value) { isConnected_ = value; }

    bool GamePadCapabilities::getHasAButtonProperty() const { return hasAButton_; }
    void GamePadCapabilities::setHasAButtonProperty(bool value) { hasAButton_ = value; }

    bool GamePadCapabilities::getHasBackButtonProperty() const { return hasBackButton_; }
    void GamePadCapabilities::setHasBackButtonProperty(bool value) { hasBackButton_ = value; }

    bool GamePadCapabilities::getHasBButtonProperty() const { return hasBButton_; }
    void GamePadCapabilities::setHasBButtonProperty(bool value) { hasBButton_ = value; }

    bool GamePadCapabilities::getHasDPadDownButtonProperty() const { return hasDPadDownButton_; }
    void GamePadCapabilities::setHasDPadDownButtonProperty(bool value) { hasDPadDownButton_ = value; }

    bool GamePadCapabilities::getHasDPadLeftButtonProperty() const { return hasDPadLeftButton_; }
    void GamePadCapabilities::setHasDPadLeftButtonProperty(bool value) { hasDPadLeftButton_ = value; }

    bool GamePadCapabilities::getHasDPadRightButtonProperty() const { return hasDPadRightButton_; }
    void GamePadCapabilities::setHasDPadRightButtonProperty(bool value) { hasDPadRightButton_ = value; }

    bool GamePadCapabilities::getHasDPadUpButtonProperty() const { return hasDPadUpButton_; }
    void GamePadCapabilities::setHasDPadUpButtonProperty(bool value) { hasDPadUpButton_ = value; }

    bool GamePadCapabilities::getHasLeftShoulderButtonProperty() const { return hasLeftShoulderButton_; }
    void GamePadCapabilities::setHasLeftShoulderButtonProperty(bool value) { hasLeftShoulderButton_ = value; }

    bool GamePadCapabilities::getHasLeftStickButtonProperty() const { return hasLeftStickButton_; }
    void GamePadCapabilities::setHasLeftStickButtonProperty(bool value) { hasLeftStickButton_ = value; }

    bool GamePadCapabilities::getHasRightShoulderButtonProperty() const { return hasRightShoulderButton_; }
    void GamePadCapabilities::setHasRightShoulderButtonProperty(bool value) { hasRightShoulderButton_ = value; }

    bool GamePadCapabilities::getHasRightStickButtonProperty() const { return hasRightStickButton_; }
    void GamePadCapabilities::setHasRightStickButtonProperty(bool value) { hasRightStickButton_ = value; }

    bool GamePadCapabilities::getHasStartButtonProperty() const { return hasStartButton_; }
    void GamePadCapabilities::setHasStartButtonProperty(bool value) { hasStartButton_ = value; }

    bool GamePadCapabilities::getHasXButtonProperty() const { return hasXButton_; }
    void GamePadCapabilities::setHasXButtonProperty(bool value) { hasXButton_ = value; }

    bool GamePadCapabilities::getHasYButtonProperty() const { return hasYButton_; }
    void GamePadCapabilities::setHasYButtonProperty(bool value) { hasYButton_ = value; }

    bool GamePadCapabilities::getHasBigButtonProperty() const { return hasBigButton_; }
    void GamePadCapabilities::setHasBigButtonProperty(bool value) { hasBigButton_ = value; }

    bool GamePadCapabilities::getHasLeftXThumbStickProperty() const { return hasLeftXThumbStick_; }
    void GamePadCapabilities::setHasLeftXThumbStickProperty(bool value) { hasLeftXThumbStick_ = value; }

    bool GamePadCapabilities::getHasLeftYThumbStickProperty() const { return hasLeftYThumbStick_; }
    void GamePadCapabilities::setHasLeftYThumbStickProperty(bool value) { hasLeftYThumbStick_ = value; }

    bool GamePadCapabilities::getHasRightXThumbStickProperty() const { return hasRightXThumbStick_; }
    void GamePadCapabilities::setHasRightXThumbStickProperty(bool value) { hasRightXThumbStick_ = value; }

    bool GamePadCapabilities::getHasRightYThumbStickProperty() const { return hasRightYThumbStick_; }
    void GamePadCapabilities::setHasRightYThumbStickProperty(bool value) { hasRightYThumbStick_ = value; }

    bool GamePadCapabilities::getHasLeftTriggerProperty() const { return hasLeftTrigger_; }
    void GamePadCapabilities::setHasLeftTriggerProperty(bool value) { hasLeftTrigger_ = value; }

    bool GamePadCapabilities::getHasRightTriggerProperty() const { return hasRightTrigger_; }
    void GamePadCapabilities::setHasRightTriggerProperty(bool value) { hasRightTrigger_ = value; }

    bool GamePadCapabilities::getHasLeftVibrationMotorProperty() const { return hasLeftVibrationMotor_; }
    void GamePadCapabilities::setHasLeftVibrationMotorProperty(bool value) { hasLeftVibrationMotor_ = value; }

    bool GamePadCapabilities::getHasRightVibrationMotorProperty() const { return hasRightVibrationMotor_; }
    void GamePadCapabilities::setHasRightVibrationMotorProperty(bool value) { hasRightVibrationMotor_ = value; }

    bool GamePadCapabilities::getHasVoiceSupportProperty() const { return hasVoiceSupport_; }
    void GamePadCapabilities::setHasVoiceSupportProperty(bool value) { hasVoiceSupport_ = value; }

    GamePadType GamePadCapabilities::getGamePadTypeProperty() const { return gamePadType_; }
    void GamePadCapabilities::setGamePadTypeProperty(GamePadType value) { gamePadType_ = value; }

    bool GamePadCapabilities::getHasLightBarEXTProperty() const { return hasLightBarEXT_; }
    void GamePadCapabilities::setHasLightBarEXTProperty(bool value) { hasLightBarEXT_ = value; }

    bool GamePadCapabilities::getHasTriggerVibrationMotorsEXTProperty() const { return hasTriggerVibrationMotorsEXT_; }
    void GamePadCapabilities::setHasTriggerVibrationMotorsEXTProperty(bool value) { hasTriggerVibrationMotorsEXT_ = value; }

    bool GamePadCapabilities::getHasMisc1EXTProperty() const { return hasMisc1EXT_; }
    void GamePadCapabilities::setHasMisc1EXTProperty(bool value) { hasMisc1EXT_ = value; }

    bool GamePadCapabilities::getHasPaddle1EXTProperty() const { return hasPaddle1EXT_; }
    void GamePadCapabilities::setHasPaddle1EXTProperty(bool value) { hasPaddle1EXT_ = value; }

    bool GamePadCapabilities::getHasPaddle2EXTProperty() const { return hasPaddle2EXT_; }
    void GamePadCapabilities::setHasPaddle2EXTProperty(bool value) { hasPaddle2EXT_ = value; }

    bool GamePadCapabilities::getHasPaddle3EXTProperty() const { return hasPaddle3EXT_; }
    void GamePadCapabilities::setHasPaddle3EXTProperty(bool value) { hasPaddle3EXT_ = value; }

    bool GamePadCapabilities::getHasPaddle4EXTProperty() const { return hasPaddle4EXT_; }
    void GamePadCapabilities::setHasPaddle4EXTProperty(bool value) { hasPaddle4EXT_ = value; }

    bool GamePadCapabilities::getHasTouchPadEXTProperty() const { return hasTouchPadEXT_; }
    void GamePadCapabilities::setHasTouchPadEXTProperty(bool value) { hasTouchPadEXT_ = value; }

    bool GamePadCapabilities::getHasGyroEXTProperty() const { return hasGyroEXT_; }
    void GamePadCapabilities::setHasGyroEXTProperty(bool value) { hasGyroEXT_ = value; }

    bool GamePadCapabilities::getHasAccelerometerEXTProperty() const { return hasAccelerometerEXT_; }
    void GamePadCapabilities::setHasAccelerometerEXTProperty(bool value) { hasAccelerometerEXT_ = value; }
}
