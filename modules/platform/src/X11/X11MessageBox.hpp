// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformSystemServices.hpp"
#include "X11Headers.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CNA::Platform::X11 {

    class X11Connection;

    /** @brief A rectangle of a message box, in its window's pixels. */
    struct X11MessageBoxRect
    {
        /** @brief Left edge. */
        int x = 0;
        /** @brief Top edge. */
        int y = 0;
        /** @brief Width. */
        int width = 0;
        /** @brief Height. */
        int height = 0;

        /** @brief Gets whether a point is inside. @param px X. @param py Y. @return The answer. */
        [[nodiscard]] bool Contains(int px, int py) const
        {
            return px >= x && py >= y && px < x + width && py < y + height;
        }
    };

    /** @brief Where everything in a message box goes. */
    struct X11MessageBoxLayout
    {
        /** @brief The window's width. */
        int width = 0;
        /** @brief The window's height. */
        int height = 0;
        /** @brief The coloured severity band down the left edge. */
        X11MessageBoxRect stripe;
        /** @brief Each line of the message: its left edge and its baseline. */
        std::vector<std::pair<int, int>> lines;
        /** @brief Each button, in the order given. */
        std::vector<X11MessageBoxRect> buttons;
    };

    /** @brief Measures a UTF-8 string's width in pixels, in the font a box draws with. */
    using X11TextMeasure = std::function<int(std::string_view)>;

    /**
     * @brief Decodes UTF-8 into the 16-bit characters a Unicode core font is indexed by.
     *
     * A core font has one plane: a character beyond U+FFFF becomes U+FFFD, and so does each
     * malformed sequence -- one replacement for its longest valid prefix, never a guess.
     *
     * @param utf8 The text.
     * @return Its UTF-16 code units, none of them a surrogate.
     */
    [[nodiscard]] std::u16string DecodeMessageBoxText(std::string_view utf8);

    /**
     * @brief Breaks a message into the lines a box shows.
     *
     * The message's own line breaks are kept; a line wider than @p maxWidth is broken at spaces,
     * and a word wider than the whole width at the last character that fits (never inside a UTF-8
     * sequence).
     *
     * @param text The message, UTF-8.
     * @param maxWidth The widest a line may be.
     * @param measure The font's measure.
     * @return The lines, at least one.
     */
    [[nodiscard]] std::vector<std::string> WrapMessageBoxText(std::string_view text, int maxWidth,
                                                              const X11TextMeasure& measure);

    /**
     * @brief Lays a message box out.
     *
     * Text on the left beside the severity band, buttons right-aligned along the bottom, each as
     * wide as its label needs and no narrower than a common minimum.
     *
     * @param lines The wrapped lines.
     * @param buttons The button labels.
     * @param measure The font's measure.
     * @param ascent The font's ascent.
     * @param descent The font's descent.
     * @return The layout.
     */
    [[nodiscard]] X11MessageBoxLayout LayoutMessageBox(const std::vector<std::string>& lines,
                                                       const std::vector<std::string>& buttons,
                                                       const X11TextMeasure& measure, int ascent, int descent);

    /**
     * @brief Finds the button under a point.
     * @param layout The layout.
     * @param x X in the window.
     * @param y Y in the window.
     * @return The button's index, or nothing.
     */
    [[nodiscard]] std::optional<int> MessageBoxButtonAt(const X11MessageBoxLayout& layout, int x, int y);

    /**
     * @brief Message boxes drawn with Xlib, and no file dialogs (plans/plan_x11.md X11-0167).
     *
     * X has no dialog service, so a box is a window of its own, on a connection of its own that
     * only the calling thread uses: nothing of the platform's connection is touched, so the game's
     * event queue is left as it was. It is marked as a dialog, transient for its parent and centred
     * on it (kept on the screen), and drawn in a Unicode core font through
     * `XDrawString16`. That needs nothing of the process locale, so a box changes none of it
     * (plans/plan_x11.md design decision 9); a server with no Unicode font gets its built-in
     * `fixed`, and Latin-1. It blocks until it is answered: a click on a button, Return or space on
     * the focused one (Tab and the arrow keys move the focus), Escape or the window manager's close
     * for "no choice". Nothing is started to show it -- no zenity, no helper process. File dialogs
     * would need a file browser, which X does not have and this backend does not draw; they refuse.
     */
    class X11Dialogs final : public IPlatformDialogs
    {
    public:
        /**
         * @brief Dialogs on the server one connection talks to.
         * @param connection The platform's connection, whose server the boxes appear on.
         */
        explicit X11Dialogs(X11Connection& connection);

        /**
         * @brief Shows a box with one OK button, and waits for it to be closed.
         * @param severity The band's colour.
         * @param title The window title.
         * @param message The message.
         * @param parent The window to centre on and be transient for, or null.
         * @throws PlatformException When the box cannot be shown.
         */
        void ShowMessageBox(MessageBoxSeverity severity, const std::string& title,
                            const std::string& message, IPlatformWindow* parent) override;

        /**
         * @brief Shows a box with buttons, and waits for the answer.
         * @param severity The band's colour.
         * @param title The window title.
         * @param message The message.
         * @param buttons The labels, in display order; not empty.
         * @param parent The window to centre on and be transient for, or null.
         * @return The chosen button's index, or -1 when the box was dismissed.
         * @throws PlatformException When the box cannot be shown.
         */
        [[nodiscard]] int ShowMessageBoxWithButtons(MessageBoxSeverity severity, const std::string& title,
                                                    const std::string& message,
                                                    const std::vector<std::string>& buttons,
                                                    IPlatformWindow* parent) override;

        /** @brief Refuses. @throws PlatformNotSupportedException Always, naming NativeFileDialog. */
        void ShowOpenFileDialog(FileDialogCallback onResult, const std::vector<FileDialogFilter>& filters,
                                const std::string& defaultLocation, bool allowMultiple,
                                IPlatformWindow* parent) override;
        /** @brief Refuses. @throws PlatformNotSupportedException Always, naming NativeFileDialog. */
        void ShowSaveFileDialog(FileDialogCallback onResult, const std::vector<FileDialogFilter>& filters,
                                const std::string& defaultLocation, IPlatformWindow* parent) override;
        /** @brief Refuses. @throws PlatformNotSupportedException Always, naming NativeFileDialog. */
        void ShowOpenFolderDialog(FileDialogCallback onResult, const std::string& defaultLocation,
                                  bool allowMultiple, IPlatformWindow* parent) override;

    private:
        X11Connection& connection_;
    };

} // namespace CNA::Platform::X11
