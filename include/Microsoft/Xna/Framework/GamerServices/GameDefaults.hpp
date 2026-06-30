// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GamerServices/ControllerSensitivity.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GameDifficulty.hpp"
#include "Microsoft/Xna/Framework/GamerServices/RacingCameraAngle.hpp"
#include <optional>

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Stores the default game preferences for a signed-in gamer.
     */
    class GameDefaults
    {
    public:
        /**
         * @brief Gets the preferred game difficulty.
         *
         * @return The GameDifficulty setting.
         */
        [[nodiscard]] GameDifficulty getGameDifficultyProperty() const;

        /**
         * @brief Gets the preferred controller sensitivity.
         *
         * @return The ControllerSensitivity setting.
         */
        [[nodiscard]] ControllerSensitivity getControllerSensitivityProperty() const;

        /**
         * @brief Gets the preferred primary color, if set.
         *
         * @return An optional Color for the primary preference.
         */
        [[nodiscard]] std::optional<Color> getPrimaryColorProperty() const;

        /**
         * @brief Gets the preferred secondary color, if set.
         *
         * @return An optional Color for the secondary preference.
         */
        [[nodiscard]] std::optional<Color> getSecondaryColorProperty() const;

        /**
         * @brief Gets whether auto-aim is enabled.
         *
         * @return true if auto-aim is on.
         */
        [[nodiscard]] bool getAutoAimProperty() const;

        /**
         * @brief Gets whether auto-center is enabled.
         *
         * @return true if auto-center is on.
         */
        [[nodiscard]] bool getAutoCenterProperty() const;

        /**
         * @brief Gets whether movement is assigned to the right thumbstick.
         *
         * @return true if move-with-right-thumbstick is on.
         */
        [[nodiscard]] bool getMoveWithRightThumbStickProperty() const;

        /**
         * @brief Gets whether the Y-axis is inverted.
         *
         * @return true if Y-axis inversion is on.
         */
        [[nodiscard]] bool getInvertYAxisProperty() const;

        /**
         * @brief Gets whether manual transmission is preferred for racing games.
         *
         * @return true if manual transmission is on.
         */
        [[nodiscard]] bool getManualTransmissionProperty() const;

        /**
         * @brief Gets the preferred racing camera angle.
         *
         * @return The RacingCameraAngle setting.
         */
        [[nodiscard]] RacingCameraAngle getRacingCameraAngleProperty() const;

        /**
         * @brief Gets whether acceleration is assigned to face buttons.
         *
         * @return true if accelerate-with-buttons is on.
         */
        [[nodiscard]] bool getAccelerateWithButtonsProperty() const;

        /**
         * @brief Gets whether braking is assigned to face buttons.
         *
         * @return true if brake-with-buttons is on.
         */
        [[nodiscard]] bool getBrakeWithButtonsProperty() const;

        /** @brief Creates a GameDefaults instance for CNA internal use. */
        NOXNA static GameDefaults CreateInternal();

    private:
        GameDefaults();

        GameDifficulty gameDifficulty_{GameDifficulty::Normal};
        ControllerSensitivity controllerSensitivity_{ControllerSensitivity::Medium};
        std::optional<Color> primaryColor_;
        std::optional<Color> secondaryColor_;
        bool autoAim_{false};
        bool autoCenter_{false};
        bool moveWithRightThumbStick_{false};
        bool invertYAxis_{false};
        bool manualTransmission_{false};
        RacingCameraAngle racingCameraAngle_{RacingCameraAngle::Back};
        bool accelerateWithButtons_{false};
        bool brakeWithButtons_{false};
    };
}
