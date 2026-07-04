// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Describes the eye shape of an avatar's facial expression.
     */
    enum class AvatarEye
    {
        /** @brief A neutral eye shape. */
        Neutral,
        /** @brief A sad eye shape. */
        Sad,
        /** @brief An angry eye shape. */
        Angry,
        /** @brief A confused eye shape. */
        Confused,
        /** @brief A laughing eye shape. */
        Laughing,
        /** @brief A shocked eye shape. */
        Shocked,
        /** @brief A happy eye shape. */
        Happy,
        /** @brief A yawning eye shape. */
        Yawning,
        /** @brief A sleeping eye shape. */
        Sleeping,
        /** @brief Looking up. */
        LookUp,
        /** @brief Looking down. */
        LookDown,
        /** @brief Looking left. */
        LookLeft,
        /** @brief Looking right. */
        LookRight,
        /** @brief Blinking. */
        Blink
    };
}
