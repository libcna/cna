// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

#include <string>

namespace CNA::Input
{
    /**
     * @brief NOXNA — system clipboard text access, backed by SDL3.
     *
     * XNA 4.0 has no clipboard API; this is a CNA extension (whole class `NOXNA`). It exposes the OS
     * clipboard's UTF-8 text so games can implement copy/paste in text fields. Requires SDL's video
     * subsystem to be initialized (it is, once the game window exists). Platform notes: desktop
     * (Windows/Linux/macOS) and Android support this fully; on the web the browser clipboard is
     * permission- and user-gesture-gated, so `SetTextEXT` may be ignored and `GetTextEXT` may be empty.
     */
    NOXNA class Clipboard
    {
    public:
        /** @brief Static-only utility; not instantiable. */
        Clipboard() = delete;

        /**
         * @brief Returns the clipboard's current UTF-8 text.
         * @return The clipboard text, or an empty string if the clipboard is empty or unavailable.
         */
        NOXNA [[nodiscard]] static std::string GetTextEXT();

        /**
         * @brief Sets the clipboard's text.
         * @param text The UTF-8 text to place on the clipboard.
         */
        NOXNA static void SetTextEXT(const std::string& text);

        /**
         * @brief Returns whether the clipboard currently holds non-empty text.
         * @return True if the clipboard has non-empty text; false otherwise.
         */
        NOXNA [[nodiscard]] static bool HasTextEXT();
    };
}
