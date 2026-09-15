// SPDX-License-Identifier: MS-PL

#include "WaylandDataDevice.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "../Freedesktop/DropParsing.hpp"
#include "WaylandConnection.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

namespace CNA::Platform::Wayland {

    namespace {

        /// A read ends after this long without a byte from the owner (D-21): a dead or stuck owner
        /// costs the caller a bounded wait, never a hang.
        constexpr std::chrono::milliseconds kReadStallTimeout{2000};
        /// ...and at this size: a clipboard is not a file transfer, and an owner that never stops
        /// writing must not exhaust memory.
        constexpr std::size_t kReadLimit = 256u * 1024u * 1024u;
        /// An outgoing transfer a reader has taken nothing of for this long is abandoned.
        constexpr std::chrono::seconds kWriteStallTimeout{10};

        bool IsTextType(const std::string& type)
        {
            const std::vector<std::string>& text = TextMimeTypes();
            return std::find(text.begin(), text.end(), type) != text.end();
        }

        const wl_data_offer_listener kOfferListener = {
            .offer = [](void* data, wl_data_offer* offer, const char* type) {
                static_cast<WaylandSelection*>(data)->AddOfferType(offer, type);
            },
            .source_actions = [](void*, wl_data_offer*, std::uint32_t) {},
            .action = [](void*, wl_data_offer*, std::uint32_t) {},
        };

        const wl_data_source_listener kSourceListener = {
            .target = [](void*, wl_data_source*, const char*) {},
            .send = [](void* data, wl_data_source* source, const char* type, const std::int32_t descriptor) {
                static_cast<WaylandSelection*>(data)->OnSend(source, type, descriptor);
            },
            .cancelled = [](void* data, wl_data_source* source) {
                static_cast<WaylandSelection*>(data)->OnCancelled(source);
            },
            .dnd_drop_performed = [](void*, wl_data_source*) {},
            .dnd_finished = [](void*, wl_data_source*) {},
            .action = [](void*, wl_data_source*, std::uint32_t) {},
        };

#if defined(CNA_WAYLAND_HAVE_PRIMARY_SELECTION)
        const zwp_primary_selection_offer_v1_listener kPrimaryOfferListener = {
            .offer = [](void* data, zwp_primary_selection_offer_v1* offer, const char* type) {
                static_cast<WaylandSelection*>(data)->AddOfferType(offer, type);
            },
        };

        const zwp_primary_selection_source_v1_listener kPrimarySourceListener = {
            .send = [](void* data, zwp_primary_selection_source_v1* source, const char* type,
                       const std::int32_t descriptor) {
                static_cast<WaylandSelection*>(data)->OnSend(source, type, descriptor);
            },
            .cancelled = [](void* data, zwp_primary_selection_source_v1* source) {
                static_cast<WaylandSelection*>(data)->OnCancelled(source);
            },
        };
#endif

    } // namespace

    const std::vector<std::string>& TextMimeTypes()
    {
        static const std::vector<std::string> types = {"text/plain;charset=utf-8", "text/plain", "UTF8_STRING",
                                                       "TEXT", "STRING"};
        return types;
    }

    namespace {

        /// write() for a pipe whose reader may already have gone: a paste target that closes its
        /// end early must end one transfer with EPIPE, not the whole game with SIGPIPE. A library
        /// must not change the process's disposition, so SIGPIPE is blocked for this thread for
        /// the one call (a write raises it on the writing thread) and a SIGPIPE the write itself
        /// raised is consumed before the mask is restored.
        ssize_t WriteWithoutSigpipe(const int descriptor, const void* data, const std::size_t size)
        {
            sigset_t pipeOnly;
            sigemptyset(&pipeOnly);
            sigaddset(&pipeOnly, SIGPIPE);
            sigset_t pending;
            sigemptyset(&pending);
            sigpending(&pending);
            const bool alreadyPending = sigismember(&pending, SIGPIPE) == 1;
            sigset_t previous;
            pthread_sigmask(SIG_BLOCK, &pipeOnly, &previous);
            const ssize_t written = ::write(descriptor, data, size);
            const int error = errno;
            if (written < 0 && error == EPIPE && !alreadyPending)
            {
                const timespec zero{0, 0};
                while (sigtimedwait(&pipeOnly, nullptr, &zero) < 0 && errno == EINTR)
                {
                }
            }
            pthread_sigmask(SIG_SETMASK, &previous, nullptr);
            errno = error;
            return written;
        }

    } // namespace

    bool WriteSome(const int descriptor, const std::vector<std::uint8_t>& data, std::size_t& offset)
    {
        while (offset < data.size())
        {
            const ssize_t written = WriteWithoutSigpipe(descriptor, data.data() + offset, data.size() - offset);
            if (written > 0)
            {
                offset += static_cast<std::size_t>(written);
                continue;
            }
            if (written < 0 && errno == EINTR)
            {
                continue;
            }
            if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            {
                return true;
            }
            return false;
        }
        return true;
    }

    // --- WaylandSelection ------------------------------------------------------------------------

    WaylandSelection::WaylandSelection(WaylandConnection& connection, const Kind kind,
                                       std::function<void(void*, std::uint32_t)> setSelection)
        : connection_(connection), kind_(kind), setSelection_(std::move(setSelection))
    {
    }

    WaylandSelection::~WaylandSelection()
    {
        for (Transfer& transfer : transfers_)
        {
            ::close(transfer.descriptor);
        }
        transfers_.clear();
        DestroySource();
        for (const auto& [offer, types] : offers_)
        {
            (void) types;
            DestroyOffer(offer);
        }
        offers_.clear();
        current_ = nullptr;
    }

    void WaylandSelection::DestroyOffer(void* offer) const
    {
        if (offer == nullptr)
        {
            return;
        }
        if (kind_ == Kind::Clipboard)
        {
            wl_data_offer_destroy(static_cast<wl_data_offer*>(offer));
        }
#if defined(CNA_WAYLAND_HAVE_PRIMARY_SELECTION)
        else
        {
            zwp_primary_selection_offer_v1_destroy(static_cast<zwp_primary_selection_offer_v1*>(offer));
        }
#endif
    }

    void WaylandSelection::DestroySource()
    {
        if (source_ == nullptr)
        {
            return;
        }
        if (kind_ == Kind::Clipboard)
        {
            wl_data_source_destroy(static_cast<wl_data_source*>(source_));
        }
#if defined(CNA_WAYLAND_HAVE_PRIMARY_SELECTION)
        else
        {
            zwp_primary_selection_source_v1_destroy(static_cast<zwp_primary_selection_source_v1*>(source_));
        }
#endif
        source_ = nullptr;
    }

    void WaylandSelection::AddOffer(void* offer)
    {
        offers_[offer] = {};
        if (kind_ == Kind::Clipboard)
        {
            wl_data_offer_add_listener(static_cast<wl_data_offer*>(offer), &kOfferListener, this);
        }
#if defined(CNA_WAYLAND_HAVE_PRIMARY_SELECTION)
        else
        {
            zwp_primary_selection_offer_v1_add_listener(static_cast<zwp_primary_selection_offer_v1*>(offer),
                                                        &kPrimaryOfferListener, this);
        }
#endif
    }

    void WaylandSelection::AddOfferType(void* offer, const char* mimeType)
    {
        const auto found = offers_.find(offer);
        if (found != offers_.end() && mimeType != nullptr)
        {
            found->second.emplace_back(mimeType);
        }
    }

    void WaylandSelection::SetCurrentOffer(void* offer)
    {
        // The previous selection's offer is of no further use: the protocol says a client
        // destroys it when a new selection arrives.
        if (current_ != nullptr && current_ != offer)
        {
            const auto found = offers_.find(current_);
            if (found != offers_.end())
            {
                DestroyOffer(current_);
                offers_.erase(found);
            }
        }
        current_ = offer;
    }

    std::vector<std::string> WaylandSelection::DetachOffer(void* offer)
    {
        const auto found = offers_.find(offer);
        if (found == offers_.end())
        {
            return {};
        }
        std::vector<std::string> types = std::move(found->second);
        offers_.erase(found);
        if (current_ == offer)
        {
            current_ = nullptr;
        }
        return types;
    }

    std::string WaylandSelection::PreferredTextType(const std::vector<std::string>& types) const
    {
        for (const std::string& wanted : TextMimeTypes())
        {
            if (std::find(types.begin(), types.end(), wanted) != types.end())
            {
                return wanted;
            }
        }
        return {};
    }

    bool WaylandSelection::HasText() const
    {
        if (source_ != nullptr)
        {
            return std::any_of(ownTypes_.begin(), ownTypes_.end(), IsTextType);
        }
        const auto found = offers_.find(current_);
        return found != offers_.end() && !PreferredTextType(found->second).empty();
    }

    std::vector<std::string> WaylandSelection::GetMimeTypes() const
    {
        if (source_ != nullptr)
        {
            return ownTypes_;
        }
        const auto found = offers_.find(current_);
        return found != offers_.end() ? found->second : std::vector<std::string>();
    }

    bool WaylandSelection::HasData(const std::string& mimeType) const
    {
        const std::vector<std::string> types = GetMimeTypes();
        return std::find(types.begin(), types.end(), mimeType) != types.end();
    }

    std::vector<std::uint8_t> WaylandSelection::GetData(const std::string& mimeType) const
    {
        if (source_ != nullptr)
        {
            // CNA's own selection, answered from memory: asking the compositor would route the
            // request back to this very client.
            const auto found = ownData_.find(mimeType);
            return found != ownData_.end() && found->second != nullptr ? *found->second : std::vector<std::uint8_t>();
        }
        const auto found = offers_.find(current_);
        if (found == offers_.end() || std::find(found->second.begin(), found->second.end(), mimeType) == found->second.end())
        {
            return {};
        }
        return Receive(current_, mimeType);
    }

    std::string WaylandSelection::GetText() const
    {
        if (source_ != nullptr)
        {
            const std::string type = PreferredTextType(ownTypes_);
            const auto found = ownData_.find(type);
            if (found == ownData_.end() || found->second == nullptr)
            {
                return {};
            }
            return std::string(found->second->begin(), found->second->end());
        }
        const auto found = offers_.find(current_);
        if (found == offers_.end())
        {
            return {};
        }
        const std::string type = PreferredTextType(found->second);
        if (type.empty())
        {
            return {};
        }
        const std::vector<std::uint8_t> bytes = Receive(current_, type);
        std::vector<unsigned char> data(bytes.begin(), bytes.end());
        // STRING is Latin-1 by the ICCCM; everything else here is UTF-8, and a trailing NUL some
        // owners add is not text.
        return Freedesktop::DecodeDropText(data, type == "STRING" ? Freedesktop::DropEncoding::Latin1
                                                                  : Freedesktop::DropEncoding::Utf8);
    }

    std::vector<std::uint8_t> WaylandSelection::Receive(void* offer, const std::string& mimeType) const
    {
        std::vector<std::uint8_t> data;
        if (offer == nullptr || !connection_.IsAlive())
        {
            return data;
        }
        int ends[2] = {-1, -1};
        if (::pipe2(ends, O_CLOEXEC) != 0)
        {
            return data;
        }
        if (kind_ == Kind::Clipboard)
        {
            wl_data_offer_receive(static_cast<wl_data_offer*>(offer), mimeType.c_str(), ends[1]);
        }
#if defined(CNA_WAYLAND_HAVE_PRIMARY_SELECTION)
        else
        {
            zwp_primary_selection_offer_v1_receive(static_cast<zwp_primary_selection_offer_v1*>(offer),
                                                   mimeType.c_str(), ends[1]);
        }
#endif
        // Our copy of the write end goes: the owner has its own, and its close is our end of file.
        ::close(ends[1]);
        connection_.Flush();
        (void) fcntl(ends[0], F_SETFL, fcntl(ends[0], F_GETFL) | O_NONBLOCK);

        auto& connection = const_cast<WaylandConnection&>(connection_);
        auto lastProgress = std::chrono::steady_clock::now();
        std::uint8_t buffer[65536];
        while (data.size() < kReadLimit)
        {
            // The owner may be this client: its `send` is dispatched here, and its transfer
            // written by Pump, or the read would wait for a write that never comes.
            (void) connection.Pump();
            const_cast<WaylandSelection*>(this)->Pump();
            pollfd readable{ends[0], POLLIN, 0};
            const int ready = ::poll(&readable, 1, 10);
            if (ready < 0 && errno == EINTR)
            {
                continue;
            }
            if (ready > 0)
            {
                const ssize_t got = ::read(ends[0], buffer, sizeof(buffer));
                if (got > 0)
                {
                    data.insert(data.end(), buffer, buffer + got);
                    lastProgress = std::chrono::steady_clock::now();
                    continue;
                }
                if (got == 0)
                {
                    break;  // the owner closed its end: done
                }
                if (errno != EAGAIN && errno != EINTR)
                {
                    break;
                }
            }
            if (std::chrono::steady_clock::now() - lastProgress > kReadStallTimeout || !connection_.IsAlive())
            {
                break;
            }
        }
        ::close(ends[0]);
        return data;
    }

    void WaylandSelection::SetText(const std::string& text)
    {
        ClipboardOffer offer;
        offer.mimeType = "text/plain;charset=utf-8";
        offer.data.assign(text.begin(), text.end());
        SetData({offer});
    }

    void WaylandSelection::SetData(const std::vector<ClipboardOffer>& offers)
    {
        const WaylandGlobals& globals = connection_.GetGlobals();
        std::vector<std::string> types;
        std::map<std::string, std::shared_ptr<const std::vector<std::uint8_t>>> data;
        std::shared_ptr<const std::vector<std::uint8_t>> text;
        for (const ClipboardOffer& offer : offers)
        {
            if (offer.mimeType.empty() || data.count(offer.mimeType) != 0)
            {
                continue;
            }
            auto bytes = std::make_shared<const std::vector<std::uint8_t>>(offer.data);
            types.push_back(offer.mimeType);
            data[offer.mimeType] = bytes;
            if (text == nullptr && (offer.mimeType == "text/plain;charset=utf-8" || offer.mimeType == "text/plain"))
            {
                text = bytes;
            }
        }
        // UTF-8 text is also offered under every older name a reader may ask by.
        if (text != nullptr)
        {
            for (const std::string& alias : TextMimeTypes())
            {
                if (data.count(alias) == 0)
                {
                    types.push_back(alias);
                    data[alias] = text;
                }
            }
        }

        void* source = nullptr;
        if (kind_ == Kind::Clipboard)
        {
            if (globals.dataDeviceManager == nullptr)
            {
                throw PlatformNotSupportedException(PlatformCapability::Clipboard,
                                                    "Wayland (the compositor withdrew wl_data_device_manager)");
            }
            auto* created = wl_data_device_manager_create_data_source(globals.dataDeviceManager);
            wl_data_source_add_listener(created, &kSourceListener, this);
            for (const std::string& type : types)
            {
                wl_data_source_offer(created, type.c_str());
            }
            source = created;
        }
#if defined(CNA_WAYLAND_HAVE_PRIMARY_SELECTION)
        else
        {
            if (globals.primarySelectionManager == nullptr)
            {
                throw PlatformNotSupportedException(PlatformCapability::PrimarySelection,
                                                    "Wayland (the compositor withdrew the primary selection)");
            }
            auto* created = zwp_primary_selection_device_manager_v1_create_source(globals.primarySelectionManager);
            zwp_primary_selection_source_v1_add_listener(created, &kPrimarySourceListener, this);
            for (const std::string& type : types)
            {
                zwp_primary_selection_source_v1_offer(created, type.c_str());
            }
            source = created;
        }
#endif
        DestroySource();
        source_ = source;
        ownTypes_ = std::move(types);
        ownData_ = std::move(data);
        if (setSelection_)
        {
            setSelection_(source_, 0);
        }
        connection_.Flush();
    }

    void WaylandSelection::OnSend(void* source, const char* mimeType, const int descriptor)
    {
        const auto found = mimeType != nullptr ? ownData_.find(mimeType) : ownData_.end();
        if (source != source_ || found == ownData_.end() || found->second == nullptr)
        {
            ::close(descriptor);
            return;
        }
        // Never blocking: what the reader does not take now is written from Pump.
        (void) fcntl(descriptor, F_SETFL, fcntl(descriptor, F_GETFL) | O_NONBLOCK);
        Transfer transfer;
        transfer.descriptor = descriptor;
        transfer.data = found->second;
        transfer.lastProgress = std::chrono::steady_clock::now();
        if (!WriteSome(transfer.descriptor, *transfer.data, transfer.offset) || transfer.offset == transfer.data->size())
        {
            ::close(transfer.descriptor);
            return;
        }
        transfers_.push_back(std::move(transfer));
    }

    void WaylandSelection::OnCancelled(void* source)
    {
        if (source == source_)
        {
            // Another client owns the selection now; its offer arrives with the selection event.
            DestroySource();
            ownTypes_.clear();
            ownData_.clear();
        }
        else if (source != nullptr)
        {
            if (kind_ == Kind::Clipboard)
            {
                wl_data_source_destroy(static_cast<wl_data_source*>(source));
            }
#if defined(CNA_WAYLAND_HAVE_PRIMARY_SELECTION)
            else
            {
                zwp_primary_selection_source_v1_destroy(static_cast<zwp_primary_selection_source_v1*>(source));
            }
#endif
        }
    }

    void WaylandSelection::Pump()
    {
        const auto now = std::chrono::steady_clock::now();
        for (auto it = transfers_.begin(); it != transfers_.end();)
        {
            const std::size_t before = it->offset;
            const bool alive = WriteSome(it->descriptor, *it->data, it->offset);
            if (it->offset != before)
            {
                it->lastProgress = now;
            }
            if (!alive || it->offset == it->data->size() || now - it->lastProgress > kWriteStallTimeout)
            {
                ::close(it->descriptor);
                it = transfers_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    // --- WaylandDataDevices ------------------------------------------------------------------------

    const wl_data_device_listener WaylandDataDevices::kDeviceListener = {
        .data_offer = [](void* data, wl_data_device*, wl_data_offer* offer) {
            // Before the event that says what it is for -- selection or drag -- names it.
            static_cast<WaylandDataDevices*>(data)->clipboard_->AddOffer(offer);
        },
        .enter = [](void* data, wl_data_device*, const std::uint32_t serial, wl_surface* surface, const wl_fixed_t x,
                    const wl_fixed_t y, wl_data_offer* offer) {
            auto* self = static_cast<WaylandDataDevices*>(data);
            self->EndDrag(false);
            Drag& drag = self->drag_;
            drag.window = self->host_.resolveSurface && surface != nullptr ? self->host_.resolveSurface(surface) : 0;
            drag.x = static_cast<float>(wl_fixed_to_double(x));
            drag.y = static_cast<float>(wl_fixed_to_double(y));
            if (offer == nullptr)
            {
                return;
            }
            drag.offer = offer;
            // The drop target owns a drag's offer; the selection's bookkeeping lets it go.
            drag.types = self->clipboard_->DetachOffer(offer);
            const std::optional<Freedesktop::DropTarget> choice = Freedesktop::ChooseDropTarget(drag.types);
            if (drag.window == 0 || !choice.has_value())
            {
                // Refused in the protocol, and nothing said to the application.
                wl_data_offer_accept(offer, serial, nullptr);
                if (wl_data_offer_get_version(offer) >= WL_DATA_OFFER_SET_ACTIONS_SINCE_VERSION)
                {
                    wl_data_offer_set_actions(offer, WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE,
                                              WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE);
                }
                return;
            }
            drag.chosen = drag.types[choice->index];
            drag.uriList = choice->encoding == Freedesktop::DropEncoding::UriList;
            drag.accepted = true;
            wl_data_offer_accept(offer, serial, drag.chosen.c_str());
            if (wl_data_offer_get_version(offer) >= WL_DATA_OFFER_SET_ACTIONS_SINCE_VERSION)
            {
                // Copy only: a move would ask the source to delete what it dragged.
                wl_data_offer_set_actions(offer, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY,
                                          WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
            }
            DropEvent begin;
            begin.window = drag.window;
            begin.kind = DropEventKind::Begin;
            if (self->host_.post) { self->host_.post(begin); }
        },
        .leave = [](void* data, wl_data_device*) {
            auto* self = static_cast<WaylandDataDevices*>(data);
            // After a drop the session ends with the drop's own Complete, from Pump.
            if (!self->drag_.dropped)
            {
                self->EndDrag(true);
            }
        },
        .motion = [](void* data, wl_data_device*, std::uint32_t, const wl_fixed_t x, const wl_fixed_t y) {
            auto* self = static_cast<WaylandDataDevices*>(data);
            Drag& drag = self->drag_;
            drag.x = static_cast<float>(wl_fixed_to_double(x));
            drag.y = static_cast<float>(wl_fixed_to_double(y));
            if (!drag.accepted || !self->host_.post)
            {
                return;
            }
            DropEvent position;
            position.window = drag.window;
            position.kind = DropEventKind::Position;
            position.x = drag.x;
            position.y = drag.y;
            self->host_.post(position);
        },
        .drop = [](void* data, wl_data_device*) {
            // Read from Pump, after the dispatch: reading here would dispatch again from inside
            // this handler, and the leave that may follow the drop would free the offer mid-read.
            auto* self = static_cast<WaylandDataDevices*>(data);
            if (!self->drag_.accepted || self->drag_.offer == nullptr)
            {
                self->EndDrag(true);
                return;
            }
            self->drag_.dropped = true;
        },
        .selection = [](void* data, wl_data_device*, wl_data_offer* offer) {
            static_cast<WaylandDataDevices*>(data)->clipboard_->SetCurrentOffer(offer);
        },
    };

    WaylandDataDevices::WaylandDataDevices(WaylandConnection& connection, Host host)
        : connection_(connection), host_(std::move(host))
    {
        clipboard_ = std::make_unique<WaylandSelection>(
            connection_, WaylandSelection::Kind::Clipboard,
            [this](void* source, std::uint32_t) { SetSelection(source, 0, false); });
#if defined(CNA_WAYLAND_HAVE_PRIMARY_SELECTION)
        if (connection_.GetGlobals().primarySelectionManager != nullptr)
        {
            primary_ = std::make_unique<WaylandSelection>(
                connection_, WaylandSelection::Kind::Primary,
                [this](void* source, std::uint32_t) { SetSelection(source, 0, true); });
        }
#endif
    }

    WaylandDataDevices::~WaylandDataDevices()
    {
        EndDrag(false);
        while (!seats_.empty())
        {
            DetachSeat(seats_.back().seat);
        }
        primary_.reset();
        clipboard_.reset();
    }

    void WaylandDataDevices::AttachSeat(wl_seat* seat)
    {
        const WaylandGlobals& globals = connection_.GetGlobals();
        SeatDevices devices;
        devices.seat = seat;
        if (globals.dataDeviceManager != nullptr)
        {
            devices.device = wl_data_device_manager_get_data_device(globals.dataDeviceManager, seat);
            wl_data_device_add_listener(devices.device, &kDeviceListener, this);
        }
#if defined(CNA_WAYLAND_HAVE_PRIMARY_SELECTION)
        if (primary_ != nullptr && globals.primarySelectionManager != nullptr)
        {
            static const zwp_primary_selection_device_v1_listener primaryListener = {
                .data_offer = [](void* data, zwp_primary_selection_device_v1*, zwp_primary_selection_offer_v1* offer) {
                    static_cast<WaylandDataDevices*>(data)->primary_->AddOffer(offer);
                },
                .selection = [](void* data, zwp_primary_selection_device_v1*, zwp_primary_selection_offer_v1* offer) {
                    static_cast<WaylandDataDevices*>(data)->primary_->SetCurrentOffer(offer);
                },
            };
            auto* device = zwp_primary_selection_device_manager_v1_get_device(globals.primarySelectionManager, seat);
            zwp_primary_selection_device_v1_add_listener(device, &primaryListener, this);
            devices.primary = device;
        }
#endif
        seats_.push_back(devices);
    }

    void WaylandDataDevices::DetachSeat(wl_seat* seat)
    {
        const auto found = std::find_if(seats_.begin(), seats_.end(),
                                        [seat](const SeatDevices& devices) { return devices.seat == seat; });
        if (found == seats_.end())
        {
            return;
        }
        if (found->device != nullptr)
        {
            if (wl_data_device_get_version(found->device) >= WL_DATA_DEVICE_RELEASE_SINCE_VERSION)
            {
                wl_data_device_release(found->device);
            }
            else
            {
                wl_data_device_destroy(found->device);
            }
        }
#if defined(CNA_WAYLAND_HAVE_PRIMARY_SELECTION)
        if (found->primary != nullptr)
        {
            zwp_primary_selection_device_v1_destroy(static_cast<zwp_primary_selection_device_v1*>(found->primary));
        }
#endif
        seats_.erase(found);
    }

    void WaylandDataDevices::SetSelection(void* source, const std::uint32_t serial, const bool primary)
    {
        (void) serial;
        // The serial of this client's latest input: a compositor ignores a selection set with a
        // serial older than the current selection's, and one set by a client that has had no
        // input at all.
        wl_seat* inputSeat = nullptr;
        const std::uint32_t latest = host_.latestSerial ? host_.latestSerial(inputSeat) : 0;
        for (const SeatDevices& devices : seats_)
        {
            if (inputSeat != nullptr && devices.seat != inputSeat && seats_.size() > 1)
            {
                continue;
            }
            if (!primary && devices.device != nullptr)
            {
                wl_data_device_set_selection(devices.device, static_cast<wl_data_source*>(source), latest);
            }
#if defined(CNA_WAYLAND_HAVE_PRIMARY_SELECTION)
            if (primary && devices.primary != nullptr)
            {
                zwp_primary_selection_device_v1_set_selection(
                    static_cast<zwp_primary_selection_device_v1*>(devices.primary),
                    static_cast<zwp_primary_selection_source_v1*>(source), latest);
            }
#endif
        }
        connection_.Flush();
    }

    void WaylandDataDevices::EndDrag(const bool complete)
    {
        if (drag_.offer != nullptr)
        {
            wl_data_offer_destroy(drag_.offer);
        }
        if (complete && drag_.accepted && host_.post)
        {
            DropEvent done;
            done.window = drag_.window;
            done.kind = DropEventKind::Complete;
            host_.post(done);
        }
        drag_ = Drag{};
    }

    void WaylandDataDevices::ForgetWindow(const WindowId window)
    {
        if (drag_.window == window)
        {
            EndDrag(false);
        }
    }

    void WaylandDataDevices::OnKeyboardFocus(const WindowId window, const bool focused)
    {
        (void) window;
        (void) focused;
    }

    void WaylandDataDevices::Pump()
    {
        clipboard_->Pump();
        if (primary_ != nullptr)
        {
            primary_->Pump();
        }
        if (drag_.dropped)
        {
            FinishDrop();
        }
    }

    void WaylandDataDevices::FinishDrop()
    {
        // Everything the read needs is copied first: the read dispatches, and the session may end
        // under it.
        const Drag drag = drag_;
        drag_.dropped = false;
        // Read now, bounded, as the X11 target reads its drop: a source that dies mid-drop costs
        // a bounded wait, not a hang.
        const std::vector<std::uint8_t> bytes = clipboard_->Receive(drag.offer, drag.chosen);
        const std::vector<unsigned char> raw(bytes.begin(), bytes.end());
        const auto emit = [this, &drag](const bool file, std::string value) {
            DropEvent item;
            item.window = drag.window;
            item.kind = file ? DropEventKind::File : DropEventKind::Text;
            item.x = drag.x;
            item.y = drag.y;
            item.data = std::move(value);
            if (host_.post) { host_.post(std::move(item)); }
        };
        if (drag.uriList)
        {
            static const std::string hostname = Freedesktop::LocalHostName();
            for (Freedesktop::DroppedItem& item :
                 Freedesktop::ParseUriList(Freedesktop::DecodeDropText(raw, Freedesktop::DropEncoding::Utf8), hostname))
            {
                emit(item.file, std::move(item.value));
            }
        }
        else
        {
            std::string text = Freedesktop::DecodeDropText(
                raw, drag.chosen == "STRING" ? Freedesktop::DropEncoding::Latin1 : Freedesktop::DropEncoding::Utf8);
            if (!text.empty())
            {
                emit(false, std::move(text));
            }
        }
        if (drag_.offer == drag.offer && drag.offer != nullptr &&
            wl_data_offer_get_version(drag.offer) >= WL_DATA_OFFER_FINISH_SINCE_VERSION)
        {
            wl_data_offer_finish(drag.offer);
        }
        if (drag_.offer == drag.offer)
        {
            EndDrag(true);
        }
        connection_.Flush();
    }

} // namespace CNA::Platform::Wayland
