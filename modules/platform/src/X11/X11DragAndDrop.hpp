// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/PlatformEvent.hpp"
#include "X11Headers.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CNA::Platform::X11 {

    class X11Clipboard;
    class X11Connection;
    class X11Window;

    /** @brief How the data of the type a drop is read as turns into text. */
    enum class X11DropEncoding
    {
        /** @brief `text/uri-list`: one URI per line, files among them. */
        UriList,
        /** @brief UTF-8 text. */
        Utf8,
        /** @brief ISO 8859-1 text (`STRING`, by the ICCCM). */
        Latin1,
    };

    /** @brief The type chosen to read a drop as. */
    struct X11DropTarget
    {
        /** @brief Its position in the list the source offered. */
        std::size_t index = 0;
        /** @brief How its data decodes. */
        X11DropEncoding encoding = X11DropEncoding::Utf8;
    };

    /**
     * @brief Chooses the type to read a drop as, from the MIME types the source offers.
     *
     * `text/uri-list` first, so a file manager's drag is files rather than their names as text;
     * then UTF-8 text (`text/plain;charset=utf-8`, `UTF8_STRING`); then `text/plain`, `TEXT` and
     * `STRING`. SDL3's preference, extended by `STRING`.
     *
     * @param offered The source's types, by name; an empty name is a type this backend does not
     * know and never chooses.
     * @return The choice, or nothing when the window can take none of them.
     */
    [[nodiscard]] std::optional<X11DropTarget> ChooseDropTarget(
        const std::vector<std::string>& offered);

    /**
     * @brief Turns a `file:` URI into a local path.
     *
     * Accepts `file:///path`, `file://localhost/path`, `file://<this host>/path` and the older
     * `file:/path`, percent-decoded. A file on another host, a malformed escape or an embedded NUL
     * is not a local path.
     *
     * @param uri The URI.
     * @param hostname This machine's host name.
     * @return The path, or nothing when the URI does not name a local file.
     */
    [[nodiscard]] std::optional<std::string> FileUriToPath(std::string_view uri,
                                                           std::string_view hostname);

    /** @brief One item of a drop. */
    struct X11DroppedItem
    {
        /** @brief True for a local file, false for text. */
        bool file = false;
        /** @brief The path, or the text. */
        std::string value;
    };

    /**
     * @brief Splits a `text/uri-list` into what it names.
     *
     * One URI per line (CRLF by RFC 2483, a bare LF accepted), comment lines skipped. A local file
     * becomes a path; any other URI -- a link dragged out of a browser -- is text, the URI itself,
     * rather than nothing (SDL3 drops those silently).
     *
     * @param list The list.
     * @param hostname This machine's host name.
     * @return The items, in order.
     */
    [[nodiscard]] std::vector<X11DroppedItem> ParseUriList(std::string_view list,
                                                           std::string_view hostname);

    /**
     * @brief Receives files and text dropped on this platform's windows: the XDND protocol,
     * version 5, target side.
     *
     * Every window CNA creates announces `XdndAware`. A drag the window can take -- a type
     * @ref ChooseDropTarget accepts -- produces `DropEvent`s in the contract's sequence: `Begin`
     * at its first position, `Position` as it moves, and on the drop one `File` per file or one
     * `Text`, then `Complete`; a drag that leaves ends with `Complete` alone. A drag of nothing
     * the window can take is refused in the protocol and produces no event at all.
     *
     * The drop's data is read through the clipboard's selection reader, synchronously and within
     * its timeouts, so a source that dies mid-drop costs the event pump a bounded wait, not a
     * hang. Every drop is answered with `XdndFinished`, accepted or not: a source waits for it.
     * Only `XdndActionCopy` is performed -- a move would ask the source to delete its files.
     */
    class X11DragAndDrop
    {
    public:
        /**
         * @brief Creates the receiver for one connection.
         *
         * @param connection The connection.
         * @param clipboard The selection reader the drop's data is read with.
         */
        X11DragAndDrop(X11Connection& connection, X11Clipboard& clipboard);

        /**
         * @brief Makes a window a drop target.
         *
         * @param window A window this platform created.
         */
        void AttachWindow(const X11Window& window);

        /**
         * @brief Forgets a drag in progress over a window that is going away.
         *
         * @param window The window's XID.
         */
        void ForgetWindow(::Window window);

        /**
         * @brief Handles a client message if it is XDND's.
         *
         * @param message The message.
         * @param window The window it was sent to.
         * @param destination Receives the drop's events.
         * @return True when it was an XDND message.
         */
        bool HandleClientMessage(const XClientMessageEvent& message, X11Window& window,
                                 std::vector<PlatformEvent>& destination);

    private:
        struct Drag
        {
            ::Window source = 0;
            int version = 0;
            Atom target = 0;
            X11DropEncoding encoding = X11DropEncoding::Utf8;
            bool begun = false;
            float x = 0.0f;
            float y = 0.0f;
        };

        void Enter(const XClientMessageEvent& message, X11Window& window,
                   std::vector<PlatformEvent>& destination);
        void Position(const XClientMessageEvent& message, X11Window& window,
                      std::vector<PlatformEvent>& destination);
        void Drop(const XClientMessageEvent& message, X11Window& window,
                  std::vector<PlatformEvent>& destination);
        void SendStatus(const Drag& drag, ::Window window) const;
        void SendFinished(const Drag& drag, ::Window window, bool accepted) const;
        [[nodiscard]] std::string NameOf(Atom atom) const;

        X11Connection& connection_;
        X11Clipboard& clipboard_;
        std::map<::Window, Drag> drags_;
    };

} // namespace CNA::Platform::X11
