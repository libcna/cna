// SPDX-License-Identifier: MS-PL

#include "X11Clipboard.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "X11Display.hpp"
#include "X11Error.hpp"

#include <algorithm>
#include <cctype>
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

        /// The targets that are the protocol's own rather than a format: never reported as one.
        bool IsProtocolTarget(const std::string& name)
        {
            return name == "TARGETS" || name == "TIMESTAMP" || name == "MULTIPLE" ||
                   name == "SAVE_TARGETS" || name == "DELETE" || name == "INSERT_SELECTION" ||
                   name == "INSERT_PROPERTY" || name == "INCR";
        }

    } // namespace

    bool IsUtf8TextFormat(const std::string& name)
    {
        std::string normalised;
        for (const char character : name)
        {
            if (character != ' ' && character != '\t')
            {
                normalised.push_back(static_cast<char>(
                    std::tolower(static_cast<unsigned char>(character))));
            }
        }
        return normalised == "text/plain;charset=utf-8" || normalised == "text/plain" ||
               normalised == "utf8_string";
    }

    std::string Utf8ToLatin1(const std::string& utf8)
    {
        std::string latin1;
        latin1.reserve(utf8.size());
        for (std::size_t index = 0; index < utf8.size();)
        {
            const auto lead = static_cast<unsigned char>(utf8[index]);
            std::size_t length = 0;
            std::uint32_t codePoint = 0;
            if (lead < 0x80u) { length = 1; codePoint = lead; }
            else if ((lead & 0xE0u) == 0xC0u) { length = 2; codePoint = lead & 0x1Fu; }
            else if ((lead & 0xF0u) == 0xE0u) { length = 3; codePoint = lead & 0x0Fu; }
            else if ((lead & 0xF8u) == 0xF0u) { length = 4; codePoint = lead & 0x07u; }
            bool valid = length != 0 && index + length <= utf8.size();
            for (std::size_t next = 1; valid && next < length; ++next)
            {
                const auto continuation = static_cast<unsigned char>(utf8[index + next]);
                valid = (continuation & 0xC0u) == 0x80u;
                codePoint = (codePoint << 6) | (continuation & 0x3Fu);
            }
            if (!valid)
            {
                latin1.push_back('?');
                ++index;
                continue;
            }
            latin1.push_back(codePoint <= 0xFFu ? static_cast<char>(codePoint) : '?');
            index += length;
        }
        return latin1;
    }

    std::string Latin1ToUtf8(const std::string& latin1)
    {
        std::string text;
        text.reserve(latin1.size());
        for (const char character : latin1)
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
            const ClipboardOffer* text = OwnedText();
            return text != nullptr && !text->data.empty();
        }
        // Another client owns it. Asking for TARGETS is the only way to know whether it can give
        // us text without actually transferring the data -- which matters, because HasText() may
        // be called far more often than GetText().
        const X11Atoms& atoms = connection_.GetAtoms();
        for (const Atom target : OwnerTargets())
        {
            if (target == atoms.utf8String || target == XA_STRING || target == atoms.text ||
                target == atoms.textPlainUtf8 || target == atoms.textPlain)
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
            const ClipboardOffer* text = OwnedText();
            return text != nullptr ? std::string(text->data.begin(), text->data.end())
                                   : std::string();
        }

        // UTF8_STRING first because it is the only target with an unambiguous encoding every X
        // application knows; text/plain;charset=utf-8 is the same for one that names formats by
        // MIME type only. STRING is Latin-1 by the ICCCM and is the fallback for an older
        // application; TEXT lets the owner choose, and text/plain states no charset at all, which
        // is why they come last (X11-0158).
        for (const Atom target : {atoms.utf8String, atoms.textPlainUtf8,
                                  static_cast<Atom>(XA_STRING), atoms.text, atoms.textPlain})
        {
            Atom actualType = kNone;
            std::vector<unsigned char> data;
            if (!ConvertAndWait(target, actualType, data) || data.empty())
            {
                continue;
            }
            const std::string bytes(reinterpret_cast<const char*>(data.data()), data.size());
            if (actualType == atoms.utf8String || actualType == atoms.textPlainUtf8 ||
                actualType == atoms.textPlain)
            {
                return bytes;
            }
            // Latin-1 to UTF-8. Returning the raw bytes would put an invalid sequence into a
            // std::string the rest of CNA treats as UTF-8.
            return Latin1ToUtf8(bytes);
        }
        return {};
    }

    std::vector<Atom> X11Clipboard::OwnerTargets() const
    {
        Atom actualType = kNone;
        std::vector<unsigned char> data;
        if (!ConvertAndWait(connection_.GetAtoms().targets, actualType, data))
        {
            return {};
        }
        // Format-32 property data comes back from Xlib as longs, whatever their wire size.
        std::vector<Atom> targets;
        const std::size_t count = data.size() / sizeof(long);
        for (std::size_t index = 0; index < count; ++index)
        {
            long value = 0;
            std::memcpy(&value, data.data() + index * sizeof(long), sizeof(long));
            targets.push_back(static_cast<Atom>(value));
        }
        return targets;
    }

    std::vector<std::string> X11Clipboard::AtomNames(const std::vector<Atom>& atoms) const
    {
        std::vector<std::string> names(atoms.size());
        if (atoms.empty())
        {
            return names;
        }
        // One request for the lot; an atom another client made up and the server does not know
        // is an error, trapped, and leaves that name empty.
        std::vector<Atom> query = atoms;
        std::vector<char*> raw(atoms.size(), nullptr);
        X11ErrorTrap trap(connection_.GetDisplay());
        const XStatus status = XGetAtomNames(connection_.GetDisplay(), query.data(),
                                             static_cast<int>(query.size()), raw.data());
        trap.Sync();
        for (std::size_t index = 0; index < raw.size(); ++index)
        {
            if (raw[index] != nullptr)
            {
                if (status != 0) { names[index] = raw[index]; }
                XFree(raw[index]);
            }
        }
        return names;
    }

    const ClipboardOffer* X11Clipboard::OwnedText() const
    {
        for (const ClipboardOffer& offer : ownedOffers_)
        {
            if (IsUtf8TextFormat(offer.mimeType))
            {
                return &offer;
            }
        }
        return nullptr;
    }

    std::vector<Atom> X11Clipboard::OwnedTargets() const
    {
        // The offers in the application's order, then text under every other name it is asked
        // for by, each atom once.
        const X11Atoms& atoms = connection_.GetAtoms();
        std::vector<Atom> targets;
        const auto add = [&targets](const Atom atom) {
            if (std::find(targets.begin(), targets.end(), atom) == targets.end())
            {
                targets.push_back(atom);
            }
        };
        for (const Atom atom : ownedOfferAtoms_)
        {
            add(atom);
        }
        if (OwnedText() != nullptr)
        {
            for (const Atom atom : {atoms.utf8String, atoms.textPlainUtf8, atoms.text,
                                    static_cast<Atom>(XA_STRING), atoms.textPlain})
            {
                add(atom);
            }
        }
        return targets;
    }

    bool X11Clipboard::OwnedContent(const Atom target, Atom& type, std::string& bytes) const
    {
        for (std::size_t index = 0; index < ownedOffers_.size(); ++index)
        {
            if (ownedOfferAtoms_[index] == target)
            {
                type = target;
                bytes.assign(ownedOffers_[index].data.begin(), ownedOffers_[index].data.end());
                return true;
            }
        }
        const ClipboardOffer* text = OwnedText();
        if (text == nullptr)
        {
            return false;
        }
        const X11Atoms& atoms = connection_.GetAtoms();
        const std::string utf8(text->data.begin(), text->data.end());
        if (target == atoms.utf8String || target == atoms.textPlainUtf8 || target == atoms.textPlain)
        {
            type = target;
            bytes = utf8;
            return true;
        }
        if (target == atoms.text)
        {
            // TEXT lets the owner choose the encoding, and says which by the reply's type.
            type = atoms.utf8String;
            bytes = utf8;
            return true;
        }
        if (target == XA_STRING)
        {
            // STRING is Latin-1 by the ICCCM. Sending UTF-8 under it made every non-ASCII
            // character two wrong ones in an application that honours that (X11-0158, D-17).
            type = XA_STRING;
            bytes = Utf8ToLatin1(utf8);
            return true;
        }
        return false;
    }

    std::vector<std::string> X11Clipboard::GetMimeTypes() const
    {
        const ::Window current = XGetSelectionOwner(connection_.GetDisplay(), selection_);
        if (current == kNone)
        {
            return {};
        }
        const std::vector<std::string> names =
            AtomNames(current == owner_ ? OwnedTargets() : OwnerTargets());
        std::vector<std::string> formats;
        for (const std::string& name : names)
        {
            if (!name.empty() && !IsProtocolTarget(name) &&
                std::find(formats.begin(), formats.end(), name) == formats.end())
            {
                formats.push_back(name);
            }
        }
        return formats;
    }

    bool X11Clipboard::HasData(const std::string& mimeType) const
    {
        Display* display = connection_.GetDisplay();
        const ::Window current = XGetSelectionOwner(display, selection_);
        if (current == kNone || mimeType.empty() || IsProtocolTarget(mimeType))
        {
            return false;
        }
        // Compared as atoms: no names to fetch for a yes-or-no question.
        const Atom target = XInternAtom(display, mimeType.c_str(), False);
        const std::vector<Atom> targets = current == owner_ ? OwnedTargets() : OwnerTargets();
        return std::find(targets.begin(), targets.end(), target) != targets.end();
    }

    std::vector<std::uint8_t> X11Clipboard::GetData(const std::string& mimeType) const
    {
        Display* display = connection_.GetDisplay();
        const ::Window current = XGetSelectionOwner(display, selection_);
        if (current == kNone || mimeType.empty() || IsProtocolTarget(mimeType))
        {
            return {};
        }
        const Atom target = XInternAtom(display, mimeType.c_str(), False);
        if (current == owner_)
        {
            Atom type = kNone;
            std::string bytes;
            if (!OwnedContent(target, type, bytes))
            {
                return {};
            }
            return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
        }
        Atom actualType = kNone;
        std::vector<unsigned char> data;
        if (!ConvertAndWait(target, actualType, data))
        {
            return {};
        }
        return std::vector<std::uint8_t>(data.begin(), data.end());
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
        std::vector<ClipboardOffer> offers;
        if (!text.empty())
        {
            offers.push_back({"text/plain;charset=utf-8",
                              std::vector<std::uint8_t>(text.begin(), text.end())});
        }
        TakeOwnership(std::move(offers));
    }

    void X11Clipboard::SetData(const std::vector<ClipboardOffer>& offers)
    {
        TakeOwnership(offers);
    }

    void X11Clipboard::TakeOwnership(std::vector<ClipboardOffer> offers)
    {
        EnsureOwnerWindow();
        Display* display = connection_.GetDisplay();

        std::vector<Atom> offerAtoms;
        for (const ClipboardOffer& offer : offers)
        {
            offerAtoms.push_back(XInternAtom(display, offer.mimeType.c_str(), False));
        }
        ownedOffers_ = std::move(offers);
        ownedOfferAtoms_ = std::move(offerAtoms);
        XSetSelectionOwner(display, selection_, owner_, kCurrentTime);

        // The server is the authority on who owns a selection, and XSetSelectionOwner has no
        // return value -- a request that lost a race is only visible by asking afterwards.
        if (XGetSelectionOwner(display, selection_) != owner_)
        {
            ownedOffers_.clear();
            ownedOfferAtoms_.clear();
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
                // Another client took the clipboard. Dropping the content is not optional
                // bookkeeping: keeping it would make HasText()/GetText() answer from a stale copy
                // of what the user copied several applications ago.
                //
                // INCR transfers already under way are NOT dropped. The requestor asked while CNA
                // owned the selection and is waiting for the rest; abandoning it left it waiting
                // forever -- xclip has no timeout -- because a new owner does not take over a
                // transfer it never started (plans/plan_native_platform_validation.md NPV-0119).
                // Each transfer carries its own copy, so it finishes without ownedOffers_.
                ownsSelection_ = false;
                ownedOffers_.clear();
                ownedOfferAtoms_.clear();
                return true;
            }
            case DestroyNotify:
            {
                // A requestor went away in the middle of a transfer. The window is gone, so there
                // is no selection to undo -- only the bookkeeping. Never consumed: the same window
                // can be a requestor of the other selection too, or watched for another reason.
                bool erased = false;
                for (auto entry = incrementalSends_.begin(); entry != incrementalSends_.end();)
                {
                    if (entry->first.first == event.xdestroywindow.window)
                    {
                        entry = incrementalSends_.erase(entry);
                        erased = true;
                    }
                    else
                    {
                        ++entry;
                    }
                }
                if (erased)
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
                const auto found =
                    incrementalSends_.find({event.xproperty.window, event.xproperty.atom});
                if (found == incrementalSends_.end())
                {
                    return false;
                }

                IncrementalSend& send = found->second;
                const std::size_t chunkSize = MaximumChunkBytes();
                const std::size_t remaining = send.bytes.size() - std::min(send.offset,
                                                                           send.bytes.size());
                const std::size_t length = std::min(chunkSize, remaining);
                XChangeProperty(display, send.requestor, send.property, send.type, 8,
                                PropModeReplace,
                                reinterpret_cast<const unsigned char*>(send.bytes.data() +
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
                    if (!HasTransferTo(requestor))
                    {
                        connection_.UnwatchForeignWindow(requestor);
                    }
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

        if (request.target == XInternAtom(display, "MULTIPLE", False))
        {
            ConvertMultiple(request, property);
            return;
        }
        SendSelectionNotify(request, ConvertInto(request.requestor, request.target, property)
                                         ? property
                                         : kNone);
    }

    bool X11Clipboard::ConvertInto(const ::Window requestor, const Atom target, const Atom property)
    {
        Display* display = connection_.GetDisplay();
        const X11Atoms& atoms = connection_.GetAtoms();
        if (property == kNone)
        {
            return false;
        }

        if (target == atoms.targets)
        {
            // Listing TARGETS itself is required: a requestor asks "what can you give me" and
            // then picks. Omitting TARGETS from its own answer is a common bug that makes
            // well-behaved applications conclude we offer nothing. MULTIPLE is offered as the
            // ICCCM asks of an owner that answers it (X11-0164).
            std::vector<Atom> targets = {atoms.targets, atoms.timestamp,
                                         XInternAtom(display, "MULTIPLE", False)};
            for (const Atom offered : OwnedTargets())
            {
                targets.push_back(offered);
            }
            XChangeProperty(display, requestor, property, XA_ATOM, 32, PropModeReplace,
                            reinterpret_cast<const unsigned char*>(targets.data()),
                            static_cast<int>(targets.size()));
            return true;
        }

        if (target == atoms.timestamp)
        {
            const long when = static_cast<long>(kCurrentTime);
            XChangeProperty(display, requestor, property, XA_INTEGER, 32, PropModeReplace,
                            reinterpret_cast<const unsigned char*>(&when), 1);
            return true;
        }

        Atom type = kNone;
        std::string bytes;
        if (!OwnedContent(target, type, bytes))
        {
            // Refusing a target we cannot convert to, rather than sending something in the wrong
            // format. property = None is the ICCCM way to say no.
            return false;
        }

        if (bytes.size() <= MaximumChunkBytes())
        {
            XChangeProperty(display, requestor, property, type, 8, PropModeReplace,
                            reinterpret_cast<const unsigned char*>(bytes.data()),
                            static_cast<int>(bytes.size()));
            return true;
        }

        // Too large for one property: start an INCR transfer. The property value is the total
        // size, the type is INCR, and the chunks follow as the requestor deletes the property.
        const long total = static_cast<long>(bytes.size());
        // StructureNotify as well as PropertyChange: a requestor that is destroyed mid-transfer
        // will never delete the property again, and without its DestroyNotify the transfer -- and
        // its copy of the content -- would be kept for good (NPV-0127). Counted, because the same
        // window may be reading the other selection at the same time (X11-0157).
        if (!HasTransferTo(requestor))
        {
            connection_.WatchForeignWindow(requestor);
        }
        XChangeProperty(display, requestor, property, atoms.incr, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(&total), 1);
        IncrementalSend send;
        send.requestor = requestor;
        send.property = property;
        send.type = type;
        send.offset = 0;
        send.bytes = std::move(bytes);
        incrementalSends_[{requestor, property}] = std::move(send);
        return true;
    }

    void X11Clipboard::ConvertMultiple(const XSelectionRequestEvent& request, const Atom property)
    {
        // MULTIPLE: the requestor's property lists (target, property) pairs; each target is
        // converted into its property, and a pair whose target cannot be converted gets None as
        // its property -- written back, so the requestor knows which ones failed.
        Display* display = connection_.GetDisplay();
        const Atom atomPair = XInternAtom(display, "ATOM_PAIR", False);
        int format = 0;
        std::vector<unsigned char> data;
        if (request.property == kNone ||
            !connection_.ReadProperty(request.requestor, property, AnyPropertyType, format, data) ||
            format != 32)
        {
            SendSelectionNotify(request, kNone);
            return;
        }
        // Format-32 property data comes back from Xlib as longs, whatever their wire size.
        std::vector<long> pairs(data.size() / sizeof(long));
        std::memcpy(pairs.data(), data.data(), pairs.size() * sizeof(long));
        for (std::size_t index = 0; index + 1 < pairs.size(); index += 2)
        {
            const auto target = static_cast<Atom>(pairs[index]);
            const auto into = static_cast<Atom>(pairs[index + 1]);
            if (target == XInternAtom(display, "MULTIPLE", False) ||
                !ConvertInto(request.requestor, target, into))
            {
                pairs[index + 1] = static_cast<long>(kNone);
            }
        }
        XChangeProperty(display, request.requestor, property, atomPair, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(pairs.data()),
                        static_cast<int>(pairs.size()));
        SendSelectionNotify(request, property);
    }

    bool X11Clipboard::HasTransferTo(const ::Window requestor) const
    {
        return std::any_of(incrementalSends_.begin(), incrementalSends_.end(),
                           [requestor](const auto& entry) { return entry.first.first == requestor; });
    }

    bool X11Clipboard::HandOverToClipboardManager(const std::chrono::milliseconds budget)
    {
        Display* display = connection_.GetDisplay();
        if (!ownsSelection_ || ownedOffers_.empty() || owner_ == 0 ||
            XGetSelectionOwner(display, selection_) != owner_)
        {
            return false;
        }
        const Atom manager = XInternAtom(display, "CLIPBOARD_MANAGER", False);
        if (XGetSelectionOwner(display, manager) == kNone)
        {
            return false;
        }
        const Atom saveTargets = XInternAtom(display, "SAVE_TARGETS", False);
        const Atom request = XInternAtom(display, "CNA_SAVE_TARGETS", False);

        // The formats to keep, as the protocol has the owner name them.
        const std::vector<Atom> targets = OwnedTargets();
        XChangeProperty(display, owner_, request, XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(targets.data()),
                        static_cast<int>(targets.size()));
        XConvertSelection(display, manager, saveTargets, request, owner_, kCurrentTime);
        XFlush(display);

        // Only what the handover needs is taken off the queue -- this selection's requests and
        // ownership changes, the INCR traffic they start, and the manager's answer -- so nothing
        // of the application's own is lost to it.
        struct Filter
        {
            const X11Clipboard* self;
            Atom manager;

            static XBool Matches(Display*, XEvent* candidate, XPointer argument)
            {
                const auto* filter = reinterpret_cast<const Filter*>(argument);
                const X11Clipboard& clipboard = *filter->self;
                switch (candidate->type)
                {
                    case SelectionRequest:
                        return candidate->xselectionrequest.selection == clipboard.selection_ ? 1 : 0;
                    case SelectionClear:
                        return candidate->xselectionclear.selection == clipboard.selection_ ? 1 : 0;
                    case SelectionNotify:
                        return candidate->xselection.requestor == clipboard.owner_ &&
                                       candidate->xselection.selection == filter->manager
                                   ? 1
                                   : 0;
                    case PropertyNotify:
                        return candidate->xproperty.state == PropertyDelete &&
                                       clipboard.incrementalSends_.count(
                                           {candidate->xproperty.window, candidate->xproperty.atom}) != 0
                                   ? 1
                                   : 0;
                    case DestroyNotify:
                        return clipboard.HasTransferTo(candidate->xdestroywindow.window) ? 1 : 0;
                    default:
                        return 0;
                }
            }
        };
        Filter filter{this, manager};

        const auto deadline = std::chrono::steady_clock::now() + budget;
        while (std::chrono::steady_clock::now() < deadline)
        {
            XEvent event{};
            XFlush(display);
            if (XCheckIfEvent(display, &event, &Filter::Matches, reinterpret_cast<XPointer>(&filter)) == 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            if (event.type == SelectionNotify)
            {
                XDeleteProperty(display, owner_, request);
                XFlush(display);
                return event.xselection.property != kNone;
            }
            (void) HandleEvent(event);
        }
        XDeleteProperty(display, owner_, request);
        return false;
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
