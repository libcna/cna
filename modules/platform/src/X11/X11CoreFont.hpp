// SPDX-License-Identifier: MS-PL
#pragma once

#include "X11Headers.hpp"

#include <string_view>
#include <vector>

namespace CNA::Platform::X11 {

    /**
     * @brief The font the backend's own windows draw text in: message boxes (X11-0167), the tray's
     * menu and tooltip (X11-0171).
     *
     * A Unicode core font indexed by UTF-16 code unit through `XDrawString16`, which needs nothing
     * of the process locale -- the 6x13 `fixed` in its `iso10646-1` edition first, then whatever
     * Unicode font the server has; the server's built-in `fixed`, Latin-1, where there is none.
     */
    class X11CoreFont
    {
    public:
        /**
         * @brief Loads the font on a connection.
         * @param display The connection.
         * @throws PlatformException When the server has no font at all.
         */
        explicit X11CoreFont(Display* display);

        /** @brief Frees the font. */
        ~X11CoreFont();

        X11CoreFont(const X11CoreFont&) = delete;
        X11CoreFont& operator=(const X11CoreFont&) = delete;

        /** @brief Measures UTF-8 text. @param text The text. @return Its width in pixels. */
        [[nodiscard]] int Measure(std::string_view text) const;

        /**
         * @brief Draws UTF-8 text in the context's foreground.
         * @param drawable Where.
         * @param gc The context; its font is set to this one.
         * @param x The left edge.
         * @param y The baseline.
         * @param text The text.
         */
        void Draw(Drawable drawable, GC gc, int x, int y, std::string_view text) const;

        /** @brief Gets the ascent. @return Pixels above the baseline. */
        [[nodiscard]] int Ascent() const { return font_->ascent; }
        /** @brief Gets the descent. @return Pixels below the baseline. */
        [[nodiscard]] int Descent() const { return font_->descent; }
        /** @brief Gets whether the font is a Unicode one. @return False for the Latin-1 fallback. */
        [[nodiscard]] bool IsUnicode() const { return unicode_; }

    private:
        [[nodiscard]] static std::vector<XChar2b> Characters(std::string_view text);

        Display* display_;
        XFontStruct* font_ = nullptr;
        bool unicode_ = false;
    };

} // namespace CNA::Platform::X11
