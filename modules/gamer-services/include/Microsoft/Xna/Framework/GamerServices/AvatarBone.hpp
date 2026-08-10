// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Identifies a bone in an avatar's skeleton.
     *
     * The underlying numeric values span 0-70 with gaps (unnamed bone slots in the real
     * XNA avatar skeleton); explicit values are preserved exactly as in the reference assembly.
     */
    enum class AvatarBone
    {
        /** @brief The root bone. */
        Root = 0,
        /** @brief The lower back bone. */
        BackLower = 1,
        /** @brief The left hip bone. */
        HipLeft = 2,
        /** @brief The right hip bone. */
        HipRight = 3,
        /** @brief The upper back bone. */
        BackUpper = 5,
        /** @brief The left knee bone. */
        KneeLeft = 6,
        /** @brief The right knee bone. */
        KneeRight = 8,
        /** @brief The left ankle bone. */
        AnkleLeft = 11,
        /** @brief The left collar bone. */
        CollarLeft = 12,
        /** @brief The neck bone. */
        Neck = 14,
        /** @brief The right ankle bone. */
        AnkleRight = 15,
        /** @brief The right collar bone. */
        CollarRight = 16,
        /** @brief The head bone. */
        Head = 19,
        /** @brief The left shoulder bone. */
        ShoulderLeft = 20,
        /** @brief The left toe bone. */
        ToeLeft = 21,
        /** @brief The right shoulder bone. */
        ShoulderRight = 22,
        /** @brief The right toe bone. */
        ToeRight = 23,
        /** @brief The left elbow bone. */
        ElbowLeft = 25,
        /** @brief The right elbow bone. */
        ElbowRight = 28,
        /** @brief The left wrist bone. */
        WristLeft = 33,
        /** @brief The right wrist bone. */
        WristRight = 36,
        /** @brief The left index finger bone. */
        FingerIndexLeft = 37,
        /** @brief The left middle finger bone. */
        FingerMiddleLeft = 38,
        /** @brief The left ring finger bone. */
        FingerRingLeft = 39,
        /** @brief The left small finger bone. */
        FingerSmallLeft = 40,
        /** @brief The left prop bone. */
        PropLeft = 41,
        /** @brief The left special bone. */
        SpecialLeft = 42,
        /** @brief The left thumb bone. */
        FingerThumbLeft = 43,
        /** @brief The right index finger bone. */
        FingerIndexRight = 44,
        /** @brief The right middle finger bone. */
        FingerMiddleRight = 45,
        /** @brief The right ring finger bone. */
        FingerRingRight = 46,
        /** @brief The right small finger bone. */
        FingerSmallRight = 47,
        /** @brief The right prop bone. */
        PropRight = 48,
        /** @brief The right special bone. */
        SpecialRight = 49,
        /** @brief The right thumb bone. */
        FingerThumbRight = 50,
        /** @brief The left index finger, second segment. */
        FingerIndex2Left = 51,
        /** @brief The left middle finger, second segment. */
        FingerMiddle2Left = 52,
        /** @brief The left ring finger, second segment. */
        FingerRing2Left = 53,
        /** @brief The left small finger, second segment. */
        FingerSmall2Left = 54,
        /** @brief The left thumb, second segment. */
        FingerThumb2Left = 55,
        /** @brief The right index finger, second segment. */
        FingerIndex2Right = 56,
        /** @brief The right middle finger, second segment. */
        FingerMiddle2Right = 57,
        /** @brief The right ring finger, second segment. */
        FingerRing2Right = 58,
        /** @brief The right small finger, second segment. */
        FingerSmall2Right = 59,
        /** @brief The right thumb, second segment. */
        FingerThumb2Right = 60,
        /** @brief The left index finger, third segment. */
        FingerIndex3Left = 61,
        /** @brief The left middle finger, third segment. */
        FingerMiddle3Left = 62,
        /** @brief The left ring finger, third segment. */
        FingerRing3Left = 63,
        /** @brief The left small finger, third segment. */
        FingerSmall3Left = 64,
        /** @brief The left thumb, third segment. */
        FingerThumb3Left = 65,
        /** @brief The right index finger, third segment. */
        FingerIndex3Right = 66,
        /** @brief The right middle finger, third segment. */
        FingerMiddle3Right = 67,
        /** @brief The right ring finger, third segment. */
        FingerRing3Right = 68,
        /** @brief The right small finger, third segment. */
        FingerSmall3Right = 69,
        /** @brief The right thumb, third segment. */
        FingerThumb3Right = 70
    };
}
