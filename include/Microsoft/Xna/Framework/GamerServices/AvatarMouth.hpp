// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Describes the mouth shape of an avatar's facial expression.
     */
    enum class AvatarMouth
    {
        /** @brief A neutral mouth shape. */
        Neutral,
        /** @brief A sad mouth shape. */
        Sad,
        /** @brief An angry mouth shape. */
        Angry,
        /** @brief A confused mouth shape. */
        Confused,
        /** @brief A laughing mouth shape. */
        Laughing,
        /** @brief A shocked mouth shape. */
        Shocked,
        /** @brief A happy mouth shape. */
        Happy,
        /** @brief The phonetic "o" mouth shape. */
        PhoneticO,
        /** @brief The phonetic "ai" mouth shape. */
        PhoneticAi,
        /** @brief The phonetic "ee" mouth shape. */
        PhoneticEe,
        /** @brief The phonetic "f"/"v" mouth shape. */
        PhoneticFv,
        /** @brief The phonetic "w" mouth shape. */
        PhoneticW,
        /** @brief The phonetic "l" mouth shape. */
        PhoneticL,
        /** @brief The phonetic "th" mouth shape. */
        PhoneticDth
    };
}
