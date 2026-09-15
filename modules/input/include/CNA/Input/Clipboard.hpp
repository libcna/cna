// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace CNA::Input
{
    /**
     * @brief CNAEXT — system clipboard text access.
     *
     * XNA 4.0 has no clipboard API; this is a CNA extension (whole class `CNAEXT`). It exposes the OS
     * clipboard's UTF-8 text so games can implement copy/paste in text fields. Platform notes: desktop
     * (Windows/Linux/macOS) and Android support this fully; on the web the browser clipboard is
     * permission- and user-gesture-gated, so `SetTextEXT` may be ignored and `GetTextEXT` may be empty.
     * A platform with no clipboard at all reports empty and ignores writes, rather than failing.
     */
    CNAEXT class Clipboard
    {
    public:
        /** @brief Static-only utility; not instantiable. */
        Clipboard() = delete;

        /**
         * @brief Returns the clipboard's current UTF-8 text.
         * @return The clipboard text, or an empty string if the clipboard is empty or unavailable.
         */
        CNAEXT [[nodiscard]] static std::string GetTextEXT();

        /**
         * @brief Sets the clipboard's text.
         * @param text The UTF-8 text to place on the clipboard.
         */
        CNAEXT static void SetTextEXT(const std::string& text);

        /**
         * @brief Returns whether the clipboard currently holds non-empty text.
         * @return True if the clipboard has non-empty text; false otherwise.
         */
        CNAEXT [[nodiscard]] static bool HasTextEXT();

        /**
         * @brief Returns the primary selection's current UTF-8 text.
         *
         * The primary selection is the text most recently selected on an X11 or Wayland desktop,
         * pasted there with the middle mouse button; it is separate from the clipboard. A game
         * pastes it on a middle click itself -- nothing does that for it.
         *
         * @return The selected text, or an empty string if there is none or the platform has no
         * primary selection.
         */
        CNAEXT [[nodiscard]] static std::string GetPrimarySelectionTextEXT();

        /**
         * @brief Offers text as the primary selection, as selecting it in a text field would.
         *
         * Ignored where the platform has no primary selection.
         *
         * @param text The UTF-8 text that was selected.
         */
        CNAEXT static void SetPrimarySelectionTextEXT(const std::string& text);

        /**
         * @brief Returns whether the primary selection currently holds non-empty text.
         * @return True if there is selected text to paste; false otherwise.
         */
        CNAEXT [[nodiscard]] static bool HasPrimarySelectionTextEXT();

        /**
         * @brief Returns the formats the clipboard offers now, as MIME types.
         *
         * `image/png`, `text/html`, `text/plain;charset=utf-8` and so on; on X11 an application's
         * older format names (`UTF8_STRING`, `STRING`) appear as they are named.
         *
         * @return The formats, most preferred first; empty when the clipboard is empty or carries
         * only text on this platform.
         */
        CNAEXT [[nodiscard]] static std::vector<std::string> GetMimeTypesEXT();

        /**
         * @brief Returns whether the clipboard offers one format.
         * @param mimeType The format, e.g. `image/png`.
         * @return True when it is offered.
         */
        CNAEXT [[nodiscard]] static bool HasDataEXT(const std::string& mimeType);

        /**
         * @brief Reads the clipboard in one format.
         * @param mimeType The format, e.g. `image/png`.
         * @return The bytes exactly as the application that copied them gave them; empty when the
         * format is not offered or the platform carries only text.
         */
        CNAEXT [[nodiscard]] static std::vector<std::uint8_t> GetDataEXT(const std::string& mimeType);

        /**
         * @brief Copies content in one format, replacing what the clipboard held.
         *
         * @param mimeType The format, e.g. `image/png`.
         * @param data The content.
         * @return True when the clipboard took it; false where the platform carries only text (use
         * SetTextEXT) or has no clipboard.
         */
        CNAEXT static bool SetDataEXT(const std::string& mimeType, const std::vector<std::uint8_t>& data);

        /**
         * @brief Copies content in several formats at once -- HTML and its plain text, say.
         *
         * A UTF-8 text format among them (`text/plain;charset=utf-8`) is also what a plain paste
         * and GetTextEXT receive.
         *
         * @param formats Each format's MIME type and content, most preferred first.
         * @return True when the clipboard took them; false where the platform carries only text or
         * has no clipboard.
         */
        CNAEXT static bool SetDataEXT(
            const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& formats);
    };
}
