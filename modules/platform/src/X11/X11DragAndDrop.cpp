// SPDX-License-Identifier: MS-PL

#include "X11DragAndDrop.hpp"

#include "X11Clipboard.hpp"
#include "X11Display.hpp"
#include "X11Error.hpp"
#include "X11Window.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <unistd.h>

namespace CNA::Platform::X11 {

    namespace {

        /// The XDND version this backend speaks: 5, the last one, which is what every current
        /// toolkit's source speaks too.
        constexpr long kXdndVersion = 5;

        bool EqualsIgnoringCase(const std::string_view a, const std::string_view b)
        {
            return a.size() == b.size() &&
                   std::equal(a.begin(), a.end(), b.begin(), [](const char x, const char y) {
                       return std::tolower(static_cast<unsigned char>(x)) ==
                              std::tolower(static_cast<unsigned char>(y));
                   });
        }

        int HexValue(const char digit)
        {
            if (digit >= '0' && digit <= '9') { return digit - '0'; }
            if (digit >= 'a' && digit <= 'f') { return digit - 'a' + 10; }
            if (digit >= 'A' && digit <= 'F') { return digit - 'A' + 10; }
            return -1;
        }

        /// Whether a byte string is valid UTF-8 -- structurally: lead bytes, continuation bytes,
        /// no overlong two-byte forms, nothing past U+10FFFF.
        bool IsUtf8(const std::string& text)
        {
            std::size_t index = 0;
            while (index < text.size())
            {
                const auto lead = static_cast<unsigned char>(text[index]);
                std::size_t length = 0;
                if (lead < 0x80u) { length = 1; }
                else if (lead >= 0xC2u && lead <= 0xDFu) { length = 2; }
                else if (lead >= 0xE0u && lead <= 0xEFu) { length = 3; }
                else if (lead >= 0xF0u && lead <= 0xF4u) { length = 4; }
                else { return false; }
                if (index + length > text.size()) { return false; }
                for (std::size_t next = 1; next < length; ++next)
                {
                    if ((static_cast<unsigned char>(text[index + next]) & 0xC0u) != 0x80u)
                    {
                        return false;
                    }
                }
                index += length;
            }
            return true;
        }

        std::string Latin1ToUtf8(const std::string& bytes)
        {
            std::string text;
            text.reserve(bytes.size());
            for (const char character : bytes)
            {
                const auto byte = static_cast<unsigned char>(character);
                if (byte < 0x80u)
                {
                    text.push_back(character);
                }
                else
                {
                    text.push_back(static_cast<char>(0xC0u | (byte >> 6)));
                    text.push_back(static_cast<char>(0x80u | (byte & 0x3Fu)));
                }
            }
            return text;
        }

        /// A drop's bytes as UTF-8 text. Sources commonly append a NUL; a claimed-UTF-8 source
        /// that sends something else is read as Latin-1 rather than passed on invalid.
        std::string DecodeText(const std::vector<unsigned char>& data, const X11DropEncoding encoding)
        {
            std::string bytes(data.begin(), data.end());
            while (!bytes.empty() && bytes.back() == '\0')
            {
                bytes.pop_back();
            }
            if (encoding == X11DropEncoding::Latin1 || !IsUtf8(bytes))
            {
                return Latin1ToUtf8(bytes);
            }
            return bytes;
        }

        std::string HostName()
        {
            char name[256] = {};
            if (gethostname(name, sizeof(name) - 1) != 0)
            {
                return {};
            }
            return name;
        }

    } // namespace

    std::optional<X11DropTarget> ChooseDropTarget(const std::vector<std::string>& offered)
    {
        std::optional<X11DropTarget> best;
        int bestRank = 0;
        for (std::size_t index = 0; index < offered.size(); ++index)
        {
            const std::string& name = offered[index];
            int rank = 0;
            X11DropEncoding encoding = X11DropEncoding::Utf8;
            if (EqualsIgnoringCase(name, "text/uri-list"))
            {
                rank = 1;
                encoding = X11DropEncoding::UriList;
            }
            else if (EqualsIgnoringCase(name, "text/plain;charset=utf-8") || name == "UTF8_STRING")
            {
                rank = 2;
            }
            else if (EqualsIgnoringCase(name, "text/plain"))
            {
                rank = 3;
            }
            else if (name == "TEXT")
            {
                rank = 4;
            }
            else if (name == "STRING")
            {
                rank = 5;
                encoding = X11DropEncoding::Latin1;
            }
            else
            {
                continue;
            }
            // The first offered of the best kind: a source lists its types best first.
            if (!best || rank < bestRank)
            {
                best = X11DropTarget{index, encoding};
                bestRank = rank;
            }
        }
        return best;
    }

    std::optional<std::string> FileUriToPath(const std::string_view uri,
                                             const std::string_view hostname)
    {
        if (uri.size() < 5 || !EqualsIgnoringCase(uri.substr(0, 5), "file:"))
        {
            return std::nullopt;
        }
        std::string_view rest = uri.substr(5);
        if (rest.starts_with("//"))
        {
            rest.remove_prefix(2);
            const std::size_t slash = rest.find('/');
            if (slash == std::string_view::npos)
            {
                return std::nullopt;
            }
            const std::string_view host = rest.substr(0, slash);
            // A file on another host is not a file this process can open.
            if (!host.empty() && !EqualsIgnoringCase(host, "localhost") &&
                (hostname.empty() || !EqualsIgnoringCase(host, hostname)))
            {
                return std::nullopt;
            }
            rest.remove_prefix(slash);
        }
        else if (!rest.starts_with("/"))
        {
            return std::nullopt;
        }

        std::string path;
        path.reserve(rest.size());
        for (std::size_t index = 0; index < rest.size(); ++index)
        {
            if (rest[index] != '%')
            {
                path.push_back(rest[index]);
                continue;
            }
            if (index + 2 >= rest.size())
            {
                return std::nullopt;
            }
            const int high = HexValue(rest[index + 1]);
            const int low = HexValue(rest[index + 2]);
            if (high < 0 || low < 0 || (high == 0 && low == 0))
            {
                // A malformed escape, or a NUL no path can contain.
                return std::nullopt;
            }
            path.push_back(static_cast<char>((high << 4) | low));
            index += 2;
        }
        return path;
    }

    std::vector<X11DroppedItem> ParseUriList(const std::string_view list,
                                             const std::string_view hostname)
    {
        std::vector<X11DroppedItem> items;
        std::size_t start = 0;
        while (start < list.size())
        {
            std::size_t end = list.find('\n', start);
            if (end == std::string_view::npos)
            {
                end = list.size();
            }
            std::string_view line = list.substr(start, end - start);
            start = end + 1;
            while (!line.empty() && (line.back() == '\r' || line.back() == '\0'))
            {
                line.remove_suffix(1);
            }
            if (line.empty() || line.front() == '#')
            {
                continue;
            }
            if (std::optional<std::string> path = FileUriToPath(line, hostname))
            {
                items.push_back(X11DroppedItem{true, std::move(*path)});
            }
            else
            {
                items.push_back(X11DroppedItem{false, std::string(line)});
            }
        }
        return items;
    }

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
        if (const std::optional<X11DropTarget> choice = ChooseDropTarget(names))
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
            if (drag.encoding == X11DropEncoding::UriList)
            {
                static const std::string hostname = HostName();
                const std::string list = DecodeText(data, X11DropEncoding::Utf8);
                for (X11DroppedItem& item : ParseUriList(list, hostname))
                {
                    emit(item.file, std::move(item.value));
                }
            }
            else
            {
                std::string text = DecodeText(data, drag.encoding);
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
