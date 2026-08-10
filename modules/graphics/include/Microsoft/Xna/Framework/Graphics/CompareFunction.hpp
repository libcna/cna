// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief The comparison function used for depth, stencil, and alpha tests. */
    enum class CompareFunction
    {
        /** @brief Always passes the test. */
        Always,
        /** @brief Never passes the test. */
        Never,
        /** @brief Passes the test when the new pixel value is less than the current pixel value. */
        Less,
        /** @brief Passes the test when the new pixel value is less than or equal to the current pixel value. */
        LessEqual,
        /** @brief Passes the test when the new pixel value is equal to the current pixel value. */
        Equal,
        /** @brief Passes the test when the new pixel value is greater than or equal to the current pixel value. */
        GreaterEqual,
        /** @brief Passes the test when the new pixel value is greater than the current pixel value. */
        Greater,
        /** @brief Passes the test when the new pixel value does not equal the current pixel value. */
        NotEqual,
    };
}
