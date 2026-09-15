// SPDX-License-Identifier: MS-PL

#include "X11Clipboard.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "X11Display.hpp"
#include "X11Error.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#include <utility>

namespace CNA::Platform::X11 {

    namespace {

        /// How long a paste waits for the selection owner to answer.
        ///
        /// A selection transfer is a conversation with another process, and that process may be
        /// busy, swapped out, or -- the case this bound exists for -- may have died between
        /// taking ownership and being asked to convert. Without a timeout the paste would hang
        /// the game loop forever; with one it returns an empty string, which is what "the
        /// clipboard has nothing for you" already means.
        constexpr auto kSelectionTimeout = std::chrono::milliseconds(1000);

        /// How long each step of an INCR transfer waits for the next chunk.
        constexpr auto kIncrementalChunkTimeout = std::chrono::milliseconds(1000);

    } // namespace

    X11Clipboard::X11Clipboard(X11Connection& connection, const Atom selection,
                               std::string selectionName)
        : connection_(connection), selection_(selection), selectionName_(std::move(selectionName))
    {
    }

    X11Clipboard::~X11Clipboard()
    {
        Display* display = connection_.GetDisplay();
        if (ownsSelection_ && owner_ != 0)
        {
            // Releasing ownership explicitly rather than letting the window's destruction do it:
            // the difference is visible to other clients, which see the selection become
            // unowned immediately instead of at the next round trip.
            XSetSelectionOwner(display, selection_, kNone, kCurrentTime);
        }
        if (owner_ != 0)
        {
            XDestroyWindow(display, owner_);
            owner_ = 0;
        }
    }

    void X11Clipboard::EnsureOwnerWindow()
    {
        if (owner_ != 0)
        {
            return;
        }
        Display* display = connection_.GetDisplay();

        // An unmapped 1x1 InputOnly window. A selection owner must be a window, but it must not
        // be a window the user can see or interact with -- and using a real application window
        // would tie clipboard ownership to that window's lifetime, so closing the window the user
        // copied from would empty the clipboard.
        XSetWindowAttributes attributes{};
        attributes.override_redirect = True;
        owner_ = XCreateWindow(display, connection_.GetRoot(), -10, -10, 1, 1, 0, CopyFromParent,
                               InputOnly, CopyFromParent, CWOverrideRedirect, &attributes);
        XSelectInput(display, owner_, PropertyChangeMask);
    }

    std::size_t X11Clipboard::MaximumChunkBytes() const
    {
        // XMaxRequestSize is in 4-byte units and covers the whole request, header included.
        // Quartering it leaves generous room for the request header and keeps each chunk well
        // inside what the server will accept, which is what the INCR protocol expects.
        const long maximum = XMaxRequestSize(connection_.GetDisplay());
        const std::size_t bytes = static_cast<std::size_t>(std::max<long>(maximum, 4096)) * 4;
        return bytes / 4;
    }

    bool X11Clipboard::HasText() const
    {
        Display* display = connection_.GetDisplay();
        const ::Window current = XGetSelectionOwner(display, selection_);
        if (current == kNone)
        {
            return false;
        }
        if (current == owner_)
        {
            return !ownedText_.empty();
        }
        // Another client owns it. Asking for TARGETS is the only way to know whether it can give
        // us text without actually transferring the data -- which matters, because HasText() may
        // be called far more often than GetText().
        Atom actualType = kNone;
        std::vector<unsigned char> data;
        if (!ConvertAndWait(connection_.GetAtoms().targets, actualType, data))
        {
            return false;
        }
        const X11Atoms& atoms = connection_.GetAtoms();
        const std::size_t count = data.size() / sizeof(long);
        for (std::size_t index = 0; index < count; ++index)
        {
            long value = 0;
            std::memcpy(&value, data.data() + index * sizeof(long), sizeof(long));
            const auto target = static_cast<Atom>(value);
            if (target == atoms.utf8String || target == XA_STRING || target == atoms.text)
            {
                return true;
            }
        }
        return false;
    }

    std::string X11Clipboard::GetText() const
    {
        Display* display = connection_.GetDisplay();
        const X11Atoms& atoms = connection_.GetAtoms();
        const ::Window current = XGetSelectionOwner(display, selection_);
        if (current == kNone)
        {
            return {};
        }
        if (current == owner_)
        {
            // Short-circuiting our own selection is not merely an optimisation: converting a
            // selection to ourselves would require the SelectionRequest to be answered from
            // inside this synchronous wait, which the single-threaded pump cannot do.
            return ownedText_;
        }

        // UTF8_STRING first because it is the only target with an unambiguous encoding. STRING is
        // Latin-1 by the ICCCM and is the fallback for an older application; TEXT lets the owner
        // choose, which is the last resort precisely because it can answer with anything.
        for (const Atom target : {atoms.utf8String, static_cast<Atom>(XA_STRING), atoms.text})
        {
            Atom actualType = kNone;
            std::vector<unsigned char> data;
            if (!ConvertAndWait(target, actualType, data) || data.empty())
            {
                continue;
            }
            if (actualType == atoms.utf8String)
            {
                return std::string(reinterpret_cast<const char*>(data.data()), data.size());
            }
            // Latin-1 to UTF-8. Returning the raw bytes would put an invalid sequence into a
            // std::string the rest of CNA treats as UTF-8.
            std::string text;
            text.reserve(data.size());
            for (const unsigned char byte : data)
            {
                if (byte < 0x80u)
                {
                    text.push_back(static_cast<char>(byte));
                }
                else
                {
                    text.push_back(static_cast<char>(0xC0u | (byte >> 6)));
                    text.push_back(static_cast<char>(0x80u | (byte & 0x3Fu)));
                }
            }
            return text;
        }
        return {};
    }

    bool X11Clipboard::ConvertAndWait(const Atom target, Atom& actualType,
                                      std::vector<unsigned char>& data) const
    {
        // const_cast rather than making the members mutable: this is a logically const read of
        // the clipboard that needs a window to receive the answer on, and lazily creating that
        // window is an implementation detail of the read rather than a state change a caller
        // could observe.
        auto& self = const_cast<X11Clipboard&>(*this);
        return self.ReadSelection(selection_, target, kCurrentTime, actualType, data);
    }

    Atom X11Clipboard::PropertyType(const Atom property) const
    {
        // ReadProperty deliberately does not report the type, so the type is read separately
        // with a zero-length request -- which returns the type and format without the data.
        Atom actual = kNone;
        int actualFormat = 0;
        unsigned long itemCount = 0;
        unsigned long remaining = 0;
        unsigned char* probe = nullptr;
        if (XGetWindowProperty(connection_.GetDisplay(), owner_, property, 0, 0, False,
                               AnyPropertyType, &actual, &actualFormat, &itemCount, &remaining,
                               &probe) != Success)
        {
            actual = kNone;
        }
        if (probe != nullptr) { XFree(probe); }
        return actual;
    }

    bool X11Clipboard::ReadSelection(const Atom selection, const Atom target, const Time time,
                                     Atom& actualType, std::vector<unsigned char>& data)
    {
        EnsureOwnerWindow();

        Display* display = connection_.GetDisplay();
        const X11Atoms& atoms = connection_.GetAtoms();

        XDeleteProperty(display, owner_, atoms.cnaSelection);
        XSync(display, False);
        // Property notifications left on our window by an earlier transfer (or by the delete just
        // made) would otherwise be mistaken for this transfer's chunks. Nothing else listens on
        // this window, so discarding them takes nothing from the application.
        XEvent stale{};
        while (XCheckTypedWindowEvent(display, owner_, PropertyNotify, &stale) == True)
        {
        }
        XConvertSelection(display, selection, target, atoms.cnaSelection, owner_, time);
        XFlush(display);

        // Waiting with XCheckTypedWindowEvent rather than XNextEvent: this must not consume the
        // application's own events out from under the pump. Only SelectionNotify for our owner
        // window is taken.
        const auto deadline = std::chrono::steady_clock::now() + kSelectionTimeout;
        XEvent event{};
        bool received = false;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (XCheckTypedWindowEvent(display, owner_, SelectionNotify, &event) == True)
            {
                received = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (!received || event.xselection.property == kNone)
        {
            // property == None is the owner saying "I cannot convert to that target", which is a
            // normal answer rather than a failure -- GetText tries the next target.
            return false;
        }

        int format = 0;
        if (!connection_.ReadProperty(owner_, atoms.cnaSelection, AnyPropertyType, format, data))
        {
            return false;
        }

        const Atom propertyType = PropertyType(atoms.cnaSelection);
        if (propertyType == atoms.incr)
        {
            // INCR: the property value was the total size, not the data. Deleting the property is
            // the signal to the owner to write the first chunk; each further delete asks for the
            // next, and a zero-length chunk ends the transfer.
            data.clear();
            actualType = kNone;
            XDeleteProperty(display, owner_, atoms.cnaSelection);
            XFlush(display);

            while (true)
            {
                const auto chunkDeadline =
                    std::chrono::steady_clock::now() + kIncrementalChunkTimeout;
                bool gotChunk = false;
                while (std::chrono::steady_clock::now() < chunkDeadline)
                {
                    if (XCheckTypedWindowEvent(display, owner_, PropertyNotify, &event) == True &&
                        event.xproperty.atom == atoms.cnaSelection &&
                        event.xproperty.state == PropertyNewValue)
                    {
                        gotChunk = true;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                if (!gotChunk)
                {
                    return !data.empty();
                }

                // The chunks carry the data's real type; the first one says what it is.
                if (actualType == kNone)
                {
                    actualType = PropertyType(atoms.cnaSelection);
                }
                std::vector<unsigned char> chunk;
                int chunkFormat = 0;
                const bool present = connection_.ReadProperty(owner_, atoms.cnaSelection,
                                                              AnyPropertyType, chunkFormat, chunk);
                if (!present)
                {
                    // A NewValue with no property behind it: a notification from before this
                    // chunk -- the INCR size announcement produces one -- whose property has
                    // since been deleted. The chunk itself is still on its way, so this is not
                    // the end of the transfer; only a zero-length chunk is (plans/
                    // plan_native_platform_validation.md NPV-0112).
                    continue;
                }
                XDeleteProperty(display, owner_, atoms.cnaSelection);
                XFlush(display);
                if (chunk.empty())
                {
                    break;
                }
                data.insert(data.end(), chunk.begin(), chunk.end());
            }
            if (actualType == kNone)
            {
                actualType = target;
            }
            return !data.empty();
        }

        actualType = propertyType;
        XDeleteProperty(display, owner_, atoms.cnaSelection);
        return true;
    }

    void X11Clipboard::SetText(const std::string& text)
    {
        EnsureOwnerWindow();
        Display* display = connection_.GetDisplay();
        const X11Atoms& atoms = connection_.GetAtoms();

        ownedText_ = text;
        XSetSelectionOwner(display, selection_, owner_, kCurrentTime);

        // The server is the authority on who owns a selection, and XSetSelectionOwner has no
        // return value -- a request that lost a race is only visible by asking afterwards.
        if (XGetSelectionOwner(display, selection_) != owner_)
        {
            ownedText_.clear();
            throw PlatformException("X11Clipboard::SetText",
                                    "the X server did not grant " + selectionName_ + " ownership");
        }
        ownsSelection_ = true;
        XFlush(display);
    }

    bool X11Clipboard::HandleEvent(const XEvent& event)
    {
        const X11Atoms& atoms = connection_.GetAtoms();
        Display* display = connection_.GetDisplay();

        switch (event.type)
        {
            case SelectionRequest:
            {
                if (event.xselectionrequest.selection != selection_)
                {
                    return false;
                }
                AnswerSelectionRequest(event.xselectionrequest);
                return true;
            }
            case SelectionClear:
            {
                if (event.xselectionclear.selection != selection_)
                {
                    return false;
                }
                // The server queues SelectionClear the moment another client takes the
                // clipboard, and an application that copies again before its next pump takes it
                // straight back -- so by the time this arrives it may describe a loss that has
                // already been undone. Acting on it then threw the new text away and every paste
                // came back empty (plans/plan_native_platform_validation.md NPV-0115). The server
                // is the authority on who owns the selection now.
                if (XGetSelectionOwner(display, selection_) == owner_)
                {
                    return true;
                }
                // Another client took the clipboard. Dropping the text is not optional
                // bookkeeping: keeping it would make HasText()/GetText() answer from a stale copy
                // of what the user copied several applications ago.
                //
                // INCR transfers already under way are NOT dropped. The requestor asked while CNA
                // owned the selection and is waiting for the rest; abandoning it left it waiting
                // forever -- xclip has no timeout -- because a new owner does not take over a
                // transfer it never started (plans/plan_native_platform_validation.md NPV-0119).
                // Each transfer carries its own copy, so it finishes without ownedText_.
                ownsSelection_ = false;
                ownedText_.clear();
                return true;
            }
            case DestroyNotify:
            {
                // A requestor went away in the middle of a transfer. The window is gone, so there
                // is no selection to undo -- only the bookkeeping. Never consumed: the same window
                // can be a requestor of the other selection too, or watched for another reason.
                if (incrementalSends_.erase(event.xdestroywindow.window) > 0)
                {
                    connection_.UnwatchForeignWindow(event.xdestroywindow.window);
                }
                return false;
            }
            case PropertyNotify:
            {
                // The requestor deleted the property, which under INCR means "send me the next
                // chunk". Anything else on our owner window is not ours to act on.
                if (event.xproperty.state != PropertyDelete)
                {
                    return false;
                }
                const auto found = incrementalSends_.find(event.xproperty.window);
                if (found == incrementalSends_.end() ||
                    found->second.property != event.xproperty.atom)
                {
                    return false;
                }

                IncrementalSend& send = found->second;
                const std::size_t chunkSize = MaximumChunkBytes();
                const std::size_t remaining = send.text.size() - std::min(send.offset,
                                                                          send.text.size());
                const std::size_t length = std::min(chunkSize, remaining);
                XChangeProperty(display, send.requestor, send.property, send.type, 8,
                                PropModeReplace,
                                reinterpret_cast<const unsigned char*>(send.text.data() +
                                                                       send.offset),
                                static_cast<int>(length));
                send.offset += length;
                XFlush(display);
                if (length == 0)
                {
                    // The zero-length write is the terminator. Ending the watch releases the
                    // requestor's window, which we do not own -- unless something else still
                    // watches it.
                    const ::Window requestor = send.requestor;
                    incrementalSends_.erase(found);
                    connection_.UnwatchForeignWindow(requestor);
                }
                return true;
            }
            default:
                return false;
        }
    }

    void X11Clipboard::AnswerSelectionRequest(const XSelectionRequestEvent& request)
    {
        Display* display = connection_.GetDisplay();
        const X11Atoms& atoms = connection_.GetAtoms();

        if (!ownsSelection_)
        {
            SendSelectionNotify(request, kNone);
            return;
        }

        // property == None is a pre-ICCCM requestor. The convention is to use the target atom as
        // the property name, which is what makes very old applications able to paste from us.
        const Atom property =
            request.property != kNone ? request.property : request.target;

        if (request.target == atoms.targets)
        {
            // Listing TARGETS itself is required: a requestor asks "what can you give me" and
            // then picks. Omitting TARGETS from its own answer is a common bug that makes
            // well-behaved applications conclude we offer nothing.
            const Atom targets[] = {atoms.targets, atoms.timestamp, atoms.utf8String,
                                    static_cast<Atom>(XA_STRING), atoms.text};
            XChangeProperty(display, request.requestor, property, XA_ATOM, 32, PropModeReplace,
                            reinterpret_cast<const unsigned char*>(targets),
                            static_cast<int>(sizeof(targets) / sizeof(targets[0])));
            SendSelectionNotify(request, property);
            return;
        }

        if (request.target == atoms.timestamp)
        {
            const long when = static_cast<long>(kCurrentTime);
            XChangeProperty(display, request.requestor, property, XA_INTEGER, 32, PropModeReplace,
                            reinterpret_cast<const unsigned char*>(&when), 1);
            SendSelectionNotify(request, property);
            return;
        }

        if (request.target != atoms.utf8String && request.target != XA_STRING &&
            request.target != atoms.text)
        {
            // Refusing a target we cannot convert to, rather than sending something in the wrong
            // encoding. property = None is the ICCCM way to say no.
            SendSelectionNotify(request, kNone);
            return;
        }

        const Atom type = request.target == atoms.text ? atoms.utf8String : request.target;

        if (ownedText_.size() <= MaximumChunkBytes())
        {
            XChangeProperty(display, request.requestor, property, type, 8, PropModeReplace,
                            reinterpret_cast<const unsigned char*>(ownedText_.data()),
                            static_cast<int>(ownedText_.size()));
            SendSelectionNotify(request, property);
            return;
        }

        // Too large for one property: start an INCR transfer. The property value is the total
        // size, the type is INCR, and the chunks follow as the requestor deletes the property.
        const long total = static_cast<long>(ownedText_.size());
        // StructureNotify as well as PropertyChange: a requestor that is destroyed mid-transfer
        // will never delete the property again, and without its DestroyNotify the transfer -- and
        // its copy of the text -- would be kept for good (NPV-0127). Counted, because the same
        // window may be reading the other selection at the same time (X11-0157).
        if (incrementalSends_.find(request.requestor) == incrementalSends_.end())
        {
            connection_.WatchForeignWindow(request.requestor);
        }
        XChangeProperty(display, request.requestor, property, atoms.incr, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(&total), 1);
        IncrementalSend send;
        send.requestor = request.requestor;
        send.property = property;
        send.type = type;
        send.offset = 0;
        send.text = ownedText_;
        incrementalSends_[request.requestor] = std::move(send);
        SendSelectionNotify(request, property);
    }

    void X11Clipboard::SendSelectionNotify(const XSelectionRequestEvent& request,
                                           const Atom property) const
    {
        XEvent notify{};
        notify.xselection.type = SelectionNotify;
        notify.xselection.requestor = request.requestor;
        notify.xselection.selection = request.selection;
        notify.xselection.target = request.target;
        notify.xselection.property = property;
        notify.xselection.time = request.time;
        XSendEvent(connection_.GetDisplay(), request.requestor, False, NoEventMask, &notify);
        XFlush(connection_.GetDisplay());
    }

} // namespace CNA::Platform::X11
