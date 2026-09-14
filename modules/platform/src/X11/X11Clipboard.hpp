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
     * @brief The X11 clipboard, as a real selection owner.
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
         * @brief Builds the clipboard for one connection.
         *
         * @param connection The connection whose selections this owns and reads.
         */
        explicit X11Clipboard(X11Connection& connection);

        /** @brief Releases selection ownership and destroys the owner window. */
        ~X11Clipboard() override;

        X11Clipboard(const X11Clipboard&) = delete;
        X11Clipboard& operator=(const X11Clipboard&) = delete;

        /**
         * @brief Gets whether the clipboard currently holds text.
         *
         * @return True when some client owns `CLIPBOARD` and offers a text target.
         */
        [[nodiscard]] bool HasText() const override;

        /**
         * @brief Gets the clipboard's text.
         *
         * @return The text as UTF-8, or an empty string when the clipboard is empty or the owner
         * did not answer within the timeout.
         */
        [[nodiscard]] std::string GetText() const override;

        /**
         * @brief Takes ownership of the clipboard and offers @p text.
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

    private:
        struct IncrementalSend
        {
            ::Window requestor = 0;
            Atom property = 0;
            Atom type = 0;
            std::size_t offset = 0;
        };

        void EnsureOwnerWindow();
        [[nodiscard]] std::size_t MaximumChunkBytes() const;
        [[nodiscard]] bool ConvertAndWait(Atom target, Atom& actualType,
                                          std::vector<unsigned char>& data) const;
        void AnswerSelectionRequest(const XSelectionRequestEvent& request);
        void SendSelectionNotify(const XSelectionRequestEvent& request, Atom property) const;

        X11Connection& connection_;
        ::Window owner_ = 0;
        std::string ownedText_;
        bool ownsSelection_ = false;
        std::map<::Window, IncrementalSend> incrementalSends_;
    };

} // namespace CNA::Platform::X11
