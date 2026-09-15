// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformSystemServices.hpp"
#include "X11Headers.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <string>
#include <vector>

namespace CNA::Platform::X11 {

    class X11Connection;

    /**
     * @brief One X selection as a clipboard service, owned for real: `CLIPBOARD` for the clipboard,
     * `PRIMARY` for the primary selection (plans/plan_x11.md X11-0157).
     *
     * The two are the same protocol on different selection atoms, and independent: owning one
     * says nothing about the other. Each instance owns its own selection through a window of its
     * own.
     *
     * ### There is no clipboard buffer in X11
     *
     * Every other system's clipboard is storage: copy puts bytes somewhere, paste takes them out,
     * and the source application can exit in between. X11 has no such storage. `CLIPBOARD` is a
     * *selection*, an ownership token; the owning client keeps the data in its own memory and
     * serves it on request. Copying is `XSetSelectionOwner`; pasting is asking the current owner
     * to convert its data into a type you named and write it onto a property of your window, then
     * waiting for the `SelectionNotify` that says it did.
     *
     * That is why this class needs the event pump: `SetText` is not a write, it is a promise to
     * answer `SelectionRequest` events for as long as this process owns the selection. A
     * clipboard implemented as a local `std::string` would appear to work perfectly between two
     * CNA windows and be invisible to every other application on the desktop.
     *
     * ### `INCR`
     *
     * A single X property transfer is bounded by the server's maximum request size. Anything
     * larger is sent in chunks under the `INCR` protocol: the owner sets an `INCR` property whose
     * value is the total size, then writes successive chunks each time the requestor deletes the
     * property, ending with a zero-length write. Both directions are implemented — a paste from
     * an application that sends a large document, and a copy of one this application owns.
     *
     * ### Formats
     *
     * A selection target is an atom, and for anything but text the atom's name is the format's
     * MIME type (`image/png`, `text/html`) -- which is what `SetData`/`GetData` name. Text is served
     * under every name X applications ask for it by: `UTF8_STRING`, `TEXT`, `STRING` (Latin-1, as
     * the ICCCM defines it), `text/plain;charset=utf-8` and `text/plain` (plans/plan_x11.md
     * X11-0158).
     */
    class X11Clipboard final : public IPlatformClipboard
    {
    public:
        /**
         * @brief Builds the service for one selection on one connection.
         *
         * @param connection The connection whose selection this owns and reads.
         * @param selection The selection: `CLIPBOARD` or `PRIMARY`.
         * @param selectionName The selection's name, for error messages.
         */
        X11Clipboard(X11Connection& connection, Atom selection, std::string selectionName);

        /** @brief Releases selection ownership and destroys the owner window. */
        ~X11Clipboard() override;

        X11Clipboard(const X11Clipboard&) = delete;
        X11Clipboard& operator=(const X11Clipboard&) = delete;

        /**
         * @brief Gets whether the selection currently holds text.
         *
         * @return True when some client owns the selection and offers a text target.
         */
        [[nodiscard]] bool HasText() const override;

        /**
         * @brief Gets the selection's text.
         *
         * @return The text as UTF-8, or an empty string when the selection is empty or the owner
         * did not answer within the timeout.
         */
        [[nodiscard]] std::string GetText() const override;

        /**
         * @brief Takes ownership of the selection and offers @p text.
         *
         * @param text The text to offer, UTF-8.
         * @throws PlatformException If the server refused to transfer ownership.
         */
        void SetText(const std::string& text) override;

        /**
         * @brief Gets the formats the selection offers.
         *
         * @return The owner's targets by name, without the protocol's own (`TARGETS`,
         * `TIMESTAMP`, `MULTIPLE`, ...); empty when nobody owns the selection.
         */
        [[nodiscard]] std::vector<std::string> GetMimeTypes() const override;

        /**
         * @brief Gets whether the selection offers one format.
         *
         * @param mimeType The format.
         * @return True when the owner lists it.
         */
        [[nodiscard]] bool HasData(const std::string& mimeType) const override;

        /**
         * @brief Reads the selection in one format.
         *
         * @param mimeType The format.
         * @return The owner's bytes, `INCR` included; empty when it refused or never answered.
         */
        [[nodiscard]] std::vector<std::uint8_t> GetData(const std::string& mimeType) const override;

        /**
         * @brief Takes ownership of the selection and offers content in several formats.
         *
         * @param offers The formats, most preferred first.
         * @throws PlatformException If the server refused to transfer ownership.
         */
        void SetData(const std::vector<ClipboardOffer>& offers) override;

        // --- driven by the event pump -----------------------------------------------------------

        /**
         * @brief Handles a selection-related event.
         *
         * @param event The event to consider.
         * @return True when the event belonged to the clipboard and was consumed.
         */
        bool HandleEvent(const XEvent& event);

        /** @brief Gets the window that holds selection ownership. @return The owner window XID. */
        [[nodiscard]] ::Window GetOwnerWindow() const { return owner_; }

        /** @brief Gets the selection this serves. @return The selection atom. */
        [[nodiscard]] Atom GetSelection() const { return selection_; }

        /**
         * @brief Gets how many INCR transfers to other clients are still in progress.
         *
         * @return The number of requestors still being served.
         */
        [[nodiscard]] std::size_t GetIncrementalTransferCount() const
        {
            return incrementalSends_.size();
        }

        /**
         * @brief Converts any selection to a target and waits for the owner's answer.
         *
         * The clipboard's own reader, for the other selections this backend reads -- a drop's
         * `XdndSelection` (plans/plan_x11.md X11-0154). Synchronous, bounded by a timeout per
         * step, and INCR-aware; it takes only the answer's own events off the queue.
         *
         * @param selection The selection to convert.
         * @param target The target type to ask for.
         * @param time The timestamp the conversion is for; a drop's must be the drop's own.
         * @param actualType Receives the type the owner answered with.
         * @param data Receives the bytes.
         * @return True when the owner answered with data, false when it refused, never answered
         * or died.
         */
        [[nodiscard]] bool ReadSelection(Atom selection, Atom target, Time time, Atom& actualType,
                                         std::vector<unsigned char>& data);

        /**
         * @brief Hands what this client owns to a clipboard manager, when there is one -- at
         * exit, so a copy outlives the game that made it (plans/plan_x11.md X11-0164).
         *
         * The freedesktop clipboard-manager protocol: the owner converts `CLIPBOARD_MANAGER` to
         * `SAVE_TARGETS`, naming the formats it offers, and keeps answering the manager's
         * requests -- `MULTIPLE` among them -- until the manager says it has them. Only this
         * selection's traffic and the manager's answer are taken off the queue.
         *
         * @param budget How long to serve the manager before giving up on it.
         * @return True when a manager confirmed it took the content; false when there was nothing
         * to hand over, no manager, or it did not answer in time.
         */
        bool HandOverToClipboardManager(std::chrono::milliseconds budget = std::chrono::milliseconds(2000));

    private:
        struct IncrementalSend
        {
            ::Window requestor = 0;
            Atom property = 0;
            Atom type = 0;
            std::size_t offset = 0;
            // The transfer's own copy. A transfer that has started is finished with the content it
            // started with, whatever happens to the selection meanwhile (NPV-0119).
            std::string bytes;
        };

        void EnsureOwnerWindow();
        void TakeOwnership(std::vector<ClipboardOffer> offers);
        [[nodiscard]] std::size_t MaximumChunkBytes() const;
        [[nodiscard]] bool ConvertAndWait(Atom target, Atom& actualType,
                                          std::vector<unsigned char>& data) const;
        [[nodiscard]] Atom PropertyType(Atom property) const;
        [[nodiscard]] std::vector<Atom> OwnerTargets() const;
        [[nodiscard]] std::vector<Atom> OwnedTargets() const;
        [[nodiscard]] const ClipboardOffer* OwnedText() const;
        [[nodiscard]] bool OwnedContent(Atom target, Atom& type, std::string& bytes) const;
        [[nodiscard]] std::vector<std::string> AtomNames(const std::vector<Atom>& atoms) const;
        void AnswerSelectionRequest(const XSelectionRequestEvent& request);
        [[nodiscard]] bool ConvertInto(::Window requestor, Atom target, Atom property);
        void ConvertMultiple(const XSelectionRequestEvent& request, Atom property);
        [[nodiscard]] bool HasTransferTo(::Window requestor) const;
        void SendSelectionNotify(const XSelectionRequestEvent& request, Atom property) const;

        X11Connection& connection_;
        Atom selection_;
        std::string selectionName_;
        ::Window owner_ = 0;
        // What this client offers while it owns the selection, most preferred first; SetText is
        // one UTF-8 text offer.
        std::vector<ClipboardOffer> ownedOffers_;
        std::vector<Atom> ownedOfferAtoms_;
        bool ownsSelection_ = false;
        // INCR transfers under way, by requestor and property: one requestor can be receiving
        // several at once, the targets of a MULTIPLE request (X11-0164).
        std::map<std::pair<::Window, Atom>, IncrementalSend> incrementalSends_;
    };

    /**
     * @brief Gets whether a format name is UTF-8 text: `text/plain;charset=utf-8` in any case and
     * spacing, `text/plain`, or X11's `UTF8_STRING`.
     *
     * @param name The MIME type or target name.
     * @return True for UTF-8 text.
     */
    [[nodiscard]] bool IsUtf8TextFormat(const std::string& name);

    /**
     * @brief Encodes UTF-8 text as the ICCCM's `STRING`, Latin-1.
     *
     * @param utf8 The text.
     * @return Latin-1 bytes; a character Latin-1 lacks becomes `?`, and a malformed sequence one
     * `?` per byte.
     */
    [[nodiscard]] std::string Utf8ToLatin1(const std::string& utf8);

    /**
     * @brief Decodes the ICCCM's `STRING`, Latin-1, as UTF-8.
     *
     * @param latin1 The bytes.
     * @return The text as UTF-8.
     */
    [[nodiscard]] std::string Latin1ToUtf8(const std::string& latin1);

} // namespace CNA::Platform::X11
