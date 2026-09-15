// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformSystemServices.hpp"
#include "CNA/Platform/PlatformEvent.hpp"

#include "WaylandProtocols.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace CNA::Platform::Wayland {

    class WaylandConnection;

    /**
     * @brief The MIME types a UTF-8 text offer is also offered under, for readers that ask by an
     * older name: `text/plain;charset=utf-8` first, then `text/plain`, and X11's `UTF8_STRING`,
     * `TEXT` and `STRING`, which Xwayland clients paste by.
     * @return The names, most preferred first.
     */
    [[nodiscard]] const std::vector<std::string>& TextMimeTypes();

    /**
     * @brief Writes as much of a buffer as a non-blocking descriptor takes now.
     *
     * The step both an outgoing clipboard transfer and a test's reader repeat: `EINTR` is retried,
     * `EAGAIN` stops for now, anything else ends the transfer.
     *
     * @param descriptor The (non-blocking) descriptor.
     * @param data The bytes.
     * @param offset Where the next byte is; advanced by what was written.
     * @return False when the transfer failed and must be abandoned.
     */
    [[nodiscard]] bool WriteSome(int descriptor, const std::vector<std::uint8_t>& data, std::size_t& offset);

    /**
     * @brief One selection -- the clipboard or the primary selection -- over its protocol
     * (plans/plan_wayland.md D-21, WAYLAND-0070..0072).
     *
     * ### Reading
     *
     * What another client offers arrives as an offer with its MIME types; reading one is a pipe the
     * owner writes into. The read is a bounded wait: it ends at the owner's end of file, after two
     * seconds without a byte, or at 256 MiB, and it keeps dispatching this connection meanwhile --
     * the owner may be this very client, which then has a `send` request to answer.
     *
     * ### Writing
     *
     * CNA's own content is kept in memory and offered by MIME type; a text offer is also offered
     * under the older text names (TextMimeTypes). While CNA owns the selection, reads are answered
     * from memory. A request to send is written without blocking: whatever the reader does not
     * take at once is written from `Pump`, called every PollEvents, so a slow or stalled reader
     * never stalls the game. A transfer no reader has taken a byte of for ten seconds is dropped.
     */
    class WaylandSelection final : public IPlatformClipboard
    {
    public:
        /** @brief Which selection. */
        enum class Kind
        {
            /** @brief `wl_data_device`: the clipboard. */
            Clipboard,
            /** @brief `zwp_primary_selection_device_v1`: the middle-click selection. */
            Primary
        };

        /**
         * @brief Creates the selection.
         * @param connection The connection.
         * @param kind Which one.
         * @param setSelection Takes the selection with a source: (source, serial of recent input).
         */
        WaylandSelection(WaylandConnection& connection, Kind kind,
                         std::function<void(void* source, std::uint32_t serial)> setSelection);

        /** @brief Destroys the source, the offers and open transfers. */
        ~WaylandSelection() override;

        WaylandSelection(const WaylandSelection&) = delete;
        WaylandSelection& operator=(const WaylandSelection&) = delete;

        /** @brief Gets whether text is offered. @return True when a text type is. */
        [[nodiscard]] bool HasText() const override;
        /** @brief Reads the text. @return UTF-8 text, or empty. */
        [[nodiscard]] std::string GetText() const override;
        /** @brief Offers text. @param text UTF-8 text. */
        void SetText(const std::string& text) override;
        /** @brief Gets the offered types. @return The MIME types, in the owner's order. */
        [[nodiscard]] std::vector<std::string> GetMimeTypes() const override;
        /** @brief Gets whether a type is offered. @param mimeType The type. @return True if offered. */
        [[nodiscard]] bool HasData(const std::string& mimeType) const override;
        /** @brief Reads one type. @param mimeType The type. @return The bytes, or empty. */
        [[nodiscard]] std::vector<std::uint8_t> GetData(const std::string& mimeType) const override;
        /** @brief Offers content in several types. @param offers The formats. */
        void SetData(const std::vector<ClipboardOffer>& offers) override;

        /** @brief Continues outgoing transfers; never blocks. */
        void Pump();

        /**
         * @brief Records an offer the compositor created (`data_offer`), before any event names it.
         * @param offer The offer proxy.
         */
        void AddOffer(void* offer);

        /**
         * @brief Records one of an offer's MIME types.
         * @param offer The offer.
         * @param mimeType The type.
         */
        void AddOfferType(void* offer, const char* mimeType);

        /**
         * @brief The selection changed (`selection`): this offer, or none, is it now.
         * @param offer The offer, or null for an empty selection.
         */
        void SetCurrentOffer(void* offer);

        /**
         * @brief Takes an offer away from the selection's bookkeeping without destroying it (a
         * drag-and-drop offer: the drop target owns it).
         * @param offer The offer.
         * @return Its MIME types.
         */
        [[nodiscard]] std::vector<std::string> DetachOffer(void* offer);

        /**
         * @brief Reads one type of an offer, bounded (see the class comment).
         * @param offer The offer.
         * @param mimeType The type.
         * @return The bytes; empty when the owner refused or never answered.
         */
        [[nodiscard]] std::vector<std::uint8_t> Receive(void* offer, const std::string& mimeType) const;

        /** @brief Gets whether CNA owns the selection now. @return True until another client takes it. */
        [[nodiscard]] bool OwnsSelection() const { return source_ != nullptr; }

        /** @brief Gets how many outgoing transfers are open (tests). @return The count. */
        [[nodiscard]] std::size_t GetOpenTransfers() const { return transfers_.size(); }

        /**
         * @brief The owner of CNA's source was asked for a type (`send`).
         * @param source The source.
         * @param mimeType The type.
         * @param descriptor The descriptor to write into; owned from here on.
         */
        void OnSend(void* source, const char* mimeType, int descriptor);

        /**
         * @brief CNA's source was replaced (`cancelled`).
         * @param source The source.
         */
        void OnCancelled(void* source);

    private:
        struct Transfer
        {
            int descriptor = -1;
            std::shared_ptr<const std::vector<std::uint8_t>> data;
            std::size_t offset = 0;
            std::chrono::steady_clock::time_point lastProgress;
        };

        void DestroySource();
        void DestroyOffer(void* offer) const;
        [[nodiscard]] std::string PreferredTextType(const std::vector<std::string>& types) const;

        WaylandConnection& connection_;
        Kind kind_;
        std::function<void(void*, std::uint32_t)> setSelection_;
        std::map<void*, std::vector<std::string>> offers_;
        void* current_ = nullptr;
        void* source_ = nullptr;
        std::vector<std::string> ownTypes_;
        std::map<std::string, std::shared_ptr<const std::vector<std::uint8_t>>> ownData_;
        std::vector<Transfer> transfers_;
    };

    /**
     * @brief Each seat's `wl_data_device` and primary-selection device, the two selections, and
     * the drag-and-drop target (WAYLAND-0070..0073).
     *
     * ### Drag and drop, target side
     *
     * A drag entering a window is offered the way X11's XDND target chooses (src/Freedesktop/
     * DropParsing): files (`text/uri-list`) over text. A drag the window can take produces the
     * contract's `DropEvent` sequence -- `Begin`, `Position` while it moves, then on the drop one
     * `File` per file or one `Text`, and `Complete`; a drag that leaves ends with `Complete`
     * alone; one of nothing the window can take is refused and says nothing. Only the copy action
     * is accepted: a move would ask the source to delete its files.
     */
    class WaylandDataDevices
    {
    public:
        /** @brief What the data devices need from the platform. */
        struct Host
        {
            /** @brief Queues an event. */
            std::function<void(PlatformEvent)> post;
            /** @brief The window a surface belongs to, or 0. */
            std::function<WindowId(wl_surface*)> resolveSurface;
            /** @brief The latest input serial and its seat. */
            std::function<std::uint32_t(wl_seat*&)> latestSerial;
        };

        /**
         * @brief Creates the service.
         * @param connection The connection.
         * @param host The platform.
         */
        WaylandDataDevices(WaylandConnection& connection, Host host);

        /** @brief Releases every device and both selections. */
        ~WaylandDataDevices();

        WaylandDataDevices(const WaylandDataDevices&) = delete;
        WaylandDataDevices& operator=(const WaylandDataDevices&) = delete;

        /** @brief Gives a seat its devices. @param seat The seat. */
        void AttachSeat(wl_seat* seat);
        /** @brief Releases a seat's devices. @param seat The seat. */
        void DetachSeat(wl_seat* seat);
        /** @brief Ends a drag over a window that goes away. @param window The window. */
        void ForgetWindow(WindowId window);
        /**
         * @brief Records keyboard focus (selections are set by the focused client).
         * @param window The window.
         * @param focused Gained or lost.
         */
        void OnKeyboardFocus(WindowId window, bool focused);
        /** @brief Continues outgoing transfers. */
        void Pump();

        /** @brief Gets the clipboard. @return The service. */
        [[nodiscard]] IPlatformClipboard* GetClipboard() { return clipboard_.get(); }
        /** @brief Gets the primary selection. @return The service, or null without the protocol. */
        [[nodiscard]] IPlatformClipboard* GetPrimarySelection() { return primary_.get(); }
        /** @brief Gets whether the primary selection exists. @return True with the protocol. */
        [[nodiscard]] bool HasPrimarySelection() const { return primary_ != nullptr; }

    private:
        struct SeatDevices
        {
            wl_seat* seat = nullptr;
            wl_data_device* device = nullptr;
            void* primary = nullptr;
        };

        struct Drag
        {
            wl_data_offer* offer = nullptr;
            WindowId window = 0;
            std::vector<std::string> types;
            std::string chosen;
            bool accepted = false;
            bool uriList = false;
            bool dropped = false;
            float x = 0.0f;
            float y = 0.0f;
        };

        static const wl_data_device_listener kDeviceListener;

        void SetSelection(void* source, std::uint32_t serial, bool primary);
        void EndDrag(bool complete);
        void FinishDrop();

        WaylandConnection& connection_;
        Host host_;
        std::vector<SeatDevices> seats_;
        std::unique_ptr<WaylandSelection> clipboard_;
        std::unique_ptr<WaylandSelection> primary_;
        Drag drag_;
    };

} // namespace CNA::Platform::Wayland
