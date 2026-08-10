// SPDX-License-Identifier: MS-PL
#pragma once
#include "Microsoft/Xna/Framework/GamerServices/AvatarEye.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarEyebrow.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarMouth.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Describes the facial expression of an avatar.
     */
    struct AvatarExpression
    {
        /**
         * @brief Gets the mouth shape.
         *
         * @return The mouth shape.
         */
        [[nodiscard]] AvatarMouth getMouthProperty() const;

        /**
         * @brief Sets the mouth shape.
         *
         * @param value The mouth shape.
         */
        void setMouthProperty(AvatarMouth value);

        /**
         * @brief Gets the left eye shape.
         *
         * @return The left eye shape.
         */
        [[nodiscard]] AvatarEye getLeftEyeProperty() const;

        /**
         * @brief Sets the left eye shape.
         *
         * @param value The left eye shape.
         */
        void setLeftEyeProperty(AvatarEye value);

        /**
         * @brief Gets the right eye shape.
         *
         * @return The right eye shape.
         */
        [[nodiscard]] AvatarEye getRightEyeProperty() const;

        /**
         * @brief Sets the right eye shape.
         *
         * @param value The right eye shape.
         */
        void setRightEyeProperty(AvatarEye value);

        /**
         * @brief Gets the left eyebrow shape.
         *
         * @return The left eyebrow shape.
         */
        [[nodiscard]] AvatarEyebrow getLeftEyebrowProperty() const;

        /**
         * @brief Sets the left eyebrow shape.
         *
         * @param value The left eyebrow shape.
         */
        void setLeftEyebrowProperty(AvatarEyebrow value);

        /**
         * @brief Gets the right eyebrow shape.
         *
         * @return The right eyebrow shape.
         */
        [[nodiscard]] AvatarEyebrow getRightEyebrowProperty() const;

        /**
         * @brief Sets the right eyebrow shape.
         *
         * @param value The right eyebrow shape.
         */
        void setRightEyebrowProperty(AvatarEyebrow value);

    private:
        AvatarMouth mouth_{AvatarMouth::Neutral};
        AvatarEye leftEye_{AvatarEye::Neutral};
        AvatarEye rightEye_{AvatarEye::Neutral};
        AvatarEyebrow leftEyebrow_{AvatarEyebrow::Neutral};
        AvatarEyebrow rightEyebrow_{AvatarEyebrow::Neutral};
    };
}
