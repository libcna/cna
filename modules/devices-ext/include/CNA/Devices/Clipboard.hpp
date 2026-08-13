// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include <string>

namespace CNA::Devices
{
    /**
     * @brief Reads and writes the system clipboard's UTF-8 text content.
     *
     * CNA extension — no XNA/WP7 equivalent exists. Backed by SDL3's
     * `IPlatformClipboard` text operations.
     *
     * All members are static: this class has no instance state of its own.
     *
     * @note SDL3 documents every member here as main-thread-only. On Web/Emscripten,
     * browser clipboard access is additionally permission-gated and commonly requires
     * an active user-gesture context (e.g. a click handler) — calling these methods
     * outside such a context, or without a video subsystem initialized at all, is
     * expected to fail gracefully (returning false/empty), never crash.
     */
    class Clipboard
    {
    public:
        /**
         * @brief Sets the clipboard's text content.
         *
         * @param text The UTF-8 text to store in the clipboard.
         * @return true on success; false on failure (including when no clipboard is
         * available, e.g. no video subsystem initialized).
         */
        static bool setTextProperty(const std::string& text);

        /**
         * @brief Gets the clipboard's current text content.
         *
         * @return The clipboard text, or an empty string on failure or if the
         * clipboard has no text.
         */
        [[nodiscard]] static std::string getTextProperty();

        /**
         * @brief Gets whether the clipboard currently contains non-empty text.
         *
         * @return true if the clipboard has text; otherwise false.
         */
        [[nodiscard]] static bool getHasTextProperty();

    private:
        /** @brief Not instantiable — every member is static. */
        Clipboard() = delete;
    };
} // namespace CNA::Devices

#endif // CNA_DEVICES
