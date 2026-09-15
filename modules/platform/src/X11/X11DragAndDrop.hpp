// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/PlatformEvent.hpp"
#include "X11Headers.hpp"

#include "../Freedesktop/DropParsing.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CNA::Platform::X11 {

    class X11Clipboard;
    class X11Connection;
    class X11Window;

    /**
     * @brief Receives files and text dropped on this platform's windows: the XDND protocol,
     * version 5, target side.
     *
     * Every window CNA creates announces `XdndAware`. A drag the window can take -- a type
     * Freedesktop::ChooseDropTarget accepts -- produces `DropEvent`s in the contract's sequence: `Begin`
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
            Freedesktop::DropEncoding encoding = Freedesktop::DropEncoding::Utf8;
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
