// SPDX-License-Identifier: MS-PL

#include "X11DragAndDrop.hpp"

#include "X11Clipboard.hpp"
#include "X11Display.hpp"
#include "X11Error.hpp"
#include "X11Window.hpp"
#include "../Freedesktop/DropParsing.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <unistd.h>

namespace CNA::Platform::X11 {

    namespace {

        /// The XDND version this backend speaks: 5, the last one, which is what every current
        /// toolkit's source speaks too.
        constexpr long kXdndVersion = 5;

    } // namespace

    X11DragAndDrop::X11DragAndDrop(X11Connection& connection, X11Clipboard& clipboard)
        : connection_(connection), clipboard_(clipboard)
    {
    }

    void X11DragAndDrop::AttachWindow(const X11Window& window)
    {
        const long version = kXdndVersion;
        XChangeProperty(connection_.GetDisplay(), window.GetXWindow(),
                        connection_.GetAtoms().xdndAware, XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(&version), 1);
    }

    void X11DragAndDrop::ForgetWindow(const ::Window window)
    {
        drags_.erase(window);
    }

    std::string X11DragAndDrop::NameOf(const Atom atom) const
    {
        if (atom == kNone)
        {
            return {};
        }
        // A source's type list can name an atom that does not exist; the trap keeps that from
        // reaching the error log as though CNA had done something wrong.
        X11ErrorTrap trap(connection_.GetDisplay());
        std::string name;
        if (char* text = XGetAtomName(connection_.GetDisplay(), atom))
        {
            name = text;
            XFree(text);
        }
        trap.Sync();
        return name;
    }

    bool X11DragAndDrop::HandleClientMessage(const XClientMessageEvent& message, X11Window& window,
                                             std::vector<PlatformEvent>& destination)
    {
        const X11Atoms& atoms = connection_.GetAtoms();
        const ::Window xid = window.GetXWindow();
        if (message.message_type == atoms.xdndEnter)
        {
            Enter(message, window, destination);
            return true;
        }
        if (message.message_type == atoms.xdndPosition)
        {
            Position(message, window, destination);
            return true;
        }
        if (message.message_type == atoms.xdndLeave)
        {
            const auto found = drags_.find(xid);
            if (found != drags_.end())
            {
                if (found->second.begun)
                {
                    DropEvent complete;
                    complete.window = window.GetId();
                    complete.kind = DropEventKind::Complete;
                    destination.emplace_back(std::move(complete));
                }
                drags_.erase(found);
            }
            return true;
        }
        if (message.message_type == atoms.xdndDrop)
        {
            Drop(message, window, destination);
            return true;
        }
        return false;
    }

    void X11DragAndDrop::Enter(const XClientMessageEvent& message, X11Window& window,
                               std::vector<PlatformEvent>& destination)
    {
        const ::Window xid = window.GetXWindow();
        // A new drag while an old one never ended -- its source died between two messages.
        if (const auto stale = drags_.find(xid); stale != drags_.end())
        {
            if (stale->second.begun)
            {
                DropEvent complete;
                complete.window = window.GetId();
                complete.kind = DropEventKind::Complete;
                destination.emplace_back(std::move(complete));
            }
            drags_.erase(stale);
        }

        Drag drag;
        drag.source = static_cast<::Window>(message.data.l[0]);
        drag.version = static_cast<int>((static_cast<unsigned long>(message.data.l[1]) >> 24) & 0xFFu);

        std::vector<Atom> types;
        if ((message.data.l[1] & 1) != 0)
        {
            // More than three types: the whole list is a property on the source window.
            X11ErrorTrap trap(connection_.GetDisplay());
            std::vector<unsigned char> data;
            int format = 0;
            if (connection_.ReadProperty(drag.source, connection_.GetAtoms().xdndTypeList, XA_ATOM,
                                         format, data) &&
                format == 32)
            {
                const std::size_t count = data.size() / sizeof(long);
                for (std::size_t index = 0; index < count; ++index)
                {
                    long value = 0;
                    std::memcpy(&value, data.data() + index * sizeof(long), sizeof(long));
                    types.push_back(static_cast<Atom>(value));
                }
            }
            trap.Sync();
        }
        else
        {
            for (int index = 2; index <= 4; ++index)
            {
                if (message.data.l[index] != 0)
                {
                    types.push_back(static_cast<Atom>(message.data.l[index]));
                }
            }
        }

        std::vector<std::string> names;
        names.reserve(types.size());
        for (const Atom type : types)
        {
            names.push_back(NameOf(type));
        }
        if (const std::optional<Freedesktop::DropTarget> choice = Freedesktop::ChooseDropTarget(names))
        {
            drag.target = types[choice->index];
            drag.encoding = choice->encoding;
        }
        drags_[xid] = drag;
    }

    void X11DragAndDrop::Position(const XClientMessageEvent& message, X11Window& window,
                                  std::vector<PlatformEvent>& destination)
    {
        const ::Window xid = window.GetXWindow();
        auto found = drags_.find(xid);
        if (found == drags_.end())
        {
            // A position with no enter before it: answer "no" so the source is not left waiting.
            Drag stranger;
            stranger.source = static_cast<::Window>(message.data.l[0]);
            SendStatus(stranger, xid);
            return;
        }
        Drag& drag = found->second;
        if (drag.target != kNone)
        {
            const auto packed = static_cast<unsigned long>(message.data.l[2]);
            const int rootX = static_cast<int>((packed >> 16) & 0xFFFFu);
            const int rootY = static_cast<int>(packed & 0xFFFFu);
            int windowX = 0;
            int windowY = 0;
            ::Window child = kNone;
            XTranslateCoordinates(connection_.GetDisplay(), connection_.GetRoot(), xid, rootX, rootY,
                                  &windowX, &windowY, &child);
            const auto x = static_cast<float>(windowX);
            const auto y = static_cast<float>(windowY);
            const bool first = !drag.begun;
            if (first)
            {
                DropEvent begin;
                begin.window = window.GetId();
                begin.kind = DropEventKind::Begin;
                destination.emplace_back(std::move(begin));
                drag.begun = true;
            }
            if (first || x != drag.x || y != drag.y)
            {
                drag.x = x;
                drag.y = y;
                DropEvent moved;
                moved.window = window.GetId();
                moved.kind = DropEventKind::Position;
                moved.x = x;
                moved.y = y;
                destination.emplace_back(std::move(moved));
            }
        }
        SendStatus(drag, xid);
    }

    void X11DragAndDrop::Drop(const XClientMessageEvent& message, X11Window& window,
                              std::vector<PlatformEvent>& destination)
    {
        const ::Window xid = window.GetXWindow();
        const auto found = drags_.find(xid);
        if (found == drags_.end())
        {
            Drag stranger;
            stranger.source = static_cast<::Window>(message.data.l[0]);
            SendFinished(stranger, xid, false);
            return;
        }
        Drag drag = found->second;
        drags_.erase(found);
        if (drag.target == kNone)
        {
            // Refused at every position, and refused now. Nothing reached the game, so nothing
            // has to be closed there either.
            SendFinished(drag, xid, false);
            return;
        }

        const WindowId id = window.GetId();
        if (!drag.begun)
        {
            // Dropped without a single position first -- legal in no version, but the game's
            // sequence stays Begin ... Complete regardless.
            DropEvent begin;
            begin.window = id;
            begin.kind = DropEventKind::Begin;
            destination.emplace_back(std::move(begin));
        }

        // Version 1 added the drop's timestamp, and the conversion must use it: the source may
        // already own the selection for a later drag.
        const Time time = drag.version >= 1 ? static_cast<Time>(message.data.l[2]) : kCurrentTime;
        Atom actualType = kNone;
        std::vector<unsigned char> data;
        const bool read = clipboard_.ReadSelection(connection_.GetAtoms().xdndSelection,
                                                   drag.target, time, actualType, data);
        bool delivered = false;
        if (read)
        {
            const auto emit = [&](const bool file, std::string value) {
                DropEvent item;
                item.window = id;
                item.kind = file ? DropEventKind::File : DropEventKind::Text;
                item.x = drag.x;
                item.y = drag.y;
                item.data = std::move(value);
                destination.emplace_back(std::move(item));
                delivered = true;
            };
            if (drag.encoding == Freedesktop::DropEncoding::UriList)
            {
                static const std::string hostname = Freedesktop::LocalHostName();
                const std::string list = Freedesktop::DecodeDropText(data, Freedesktop::DropEncoding::Utf8);
                for (Freedesktop::DroppedItem& item : Freedesktop::ParseUriList(list, hostname))
                {
                    emit(item.file, std::move(item.value));
                }
            }
            else
            {
                std::string text = Freedesktop::DecodeDropText(data, drag.encoding);
                if (!text.empty())
                {
                    emit(false, std::move(text));
                }
            }
        }

        DropEvent complete;
        complete.window = id;
        complete.kind = DropEventKind::Complete;
        destination.emplace_back(std::move(complete));
        SendFinished(drag, xid, delivered);
    }

    void X11DragAndDrop::SendStatus(const Drag& drag, const ::Window window) const
    {
        const X11Atoms& atoms = connection_.GetAtoms();
        const bool accepted = drag.target != kNone;
        XEvent reply{};
        reply.xclient.type = ClientMessage;
        reply.xclient.display = connection_.GetDisplay();
        reply.xclient.window = drag.source;
        reply.xclient.message_type = atoms.xdndStatus;
        reply.xclient.format = 32;
        reply.xclient.data.l[0] = static_cast<long>(window);
        // Bit 0: accepted. Bit 1: send a position on every move -- and the rectangle is empty,
        // so there is no area in which the source may stop.
        reply.xclient.data.l[1] = accepted ? 3 : 0;
        reply.xclient.data.l[2] = 0;
        reply.xclient.data.l[3] = 0;
        reply.xclient.data.l[4] = accepted ? static_cast<long>(atoms.xdndActionCopy) : 0;
        X11ErrorTrap trap(connection_.GetDisplay());
        XSendEvent(connection_.GetDisplay(), drag.source, kXFalse, NoEventMask, &reply);
        trap.Sync();
    }

    void X11DragAndDrop::SendFinished(const Drag& drag, const ::Window window,
                                      const bool accepted) const
    {
        const X11Atoms& atoms = connection_.GetAtoms();
        XEvent reply{};
        reply.xclient.type = ClientMessage;
        reply.xclient.display = connection_.GetDisplay();
        reply.xclient.window = drag.source;
        reply.xclient.message_type = atoms.xdndFinished;
        reply.xclient.format = 32;
        reply.xclient.data.l[0] = static_cast<long>(window);
        reply.xclient.data.l[1] = accepted ? 1 : 0;
        reply.xclient.data.l[2] = accepted ? static_cast<long>(atoms.xdndActionCopy) : 0;
        // A source that died mid-drop has no window to send to; the trap keeps that quiet.
        X11ErrorTrap trap(connection_.GetDisplay());
        XSendEvent(connection_.GetDisplay(), drag.source, kXFalse, NoEventMask, &reply);
        trap.Sync();
    }

} // namespace CNA::Platform::X11
