// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

namespace CNA::Input
{
    /**
     * @brief CNAEXT — a hint describing the kind of text being entered.
     *
     * XNA 4.0's text input has no notion of input type; this CNA extension mirrors SDL3's
     * `PlatformTextInputType`, letting a game hint the on-screen keyboard / IME layout (e.g. show a
     * numeric pad, hide password characters, or offer an email keyboard) via
     * `TextInputEXT::StartTextInputWithTypeEXT`.
     */
    CNAEXT enum class TextInputTypeEXT
    {
        /** @brief The input is plain text. */
        Text,
        /** @brief The input is a person's name. */
        TextName,
        /** @brief The input is an e-mail address. */
        TextEmail,
        /** @brief The input is a username. */
        TextUsername,
        /** @brief The input is a secure password that should be hidden. */
        TextPasswordHidden,
        /** @brief The input is a secure password that should be visible. */
        TextPasswordVisible,
        /** @brief The input is a number. */
        Number,
        /** @brief The input is a secure PIN that should be hidden. */
        NumberPasswordHidden,
        /** @brief The input is a secure PIN that should be visible. */
        NumberPasswordVisible
    };
}
