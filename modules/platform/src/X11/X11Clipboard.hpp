// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformSystemServices.hpp"
#include "X11Headers.hpp"

#include <cstddef>
#include <map>
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

    private:
        struct IncrementalSend
        {
            ::Window requestor = 0;
            Atom property = 0;
            Atom type = 0;
            std::size_t offset = 0;
            // The transfer's own copy. A transfer that has started is finished with the text it
            // started with, whatever happens to the clipboard meanwhile (NPV-0119).
            std::string text;
        };

        void EnsureOwnerWindow();
        [[nodiscard]] std::size_t MaximumChunkBytes() const;
        [[nodiscard]] bool ConvertAndWait(Atom target, Atom& actualType,
                                          std::vector<unsigned char>& data) const;
        [[nodiscard]] Atom PropertyType(Atom property) const;
        void AnswerSelectionRequest(const XSelectionRequestEvent& request);
        void SendSelectionNotify(const XSelectionRequestEvent& request, Atom property) const;

        X11Connection& connection_;
        Atom selection_;
        std::string selectionName_;
        ::Window owner_ = 0;
        std::string ownedText_;
        bool ownsSelection_ = false;
        std::map<::Window, IncrementalSend> incrementalSends_;
    };

} // namespace CNA::Platform::X11
