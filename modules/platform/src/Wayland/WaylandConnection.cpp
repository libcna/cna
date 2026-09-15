// SPDX-License-Identifier: MS-PL

#include "WaylandConnection.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <poll.h>

namespace CNA::Platform::Wayland {

    namespace {

        /// The highest version of each interface this backend implements (plans/plan_wayland.md
        /// WAYLAND-0030). A newer compositor offering more is bound at these; an older one at its
        /// own.
        constexpr std::uint32_t kCompositorVersion = 6;     // v6: preferred_buffer_scale
        constexpr std::uint32_t kSubcompositorVersion = 1;
        constexpr std::uint32_t kShmVersion = 2;            // v2: release
        constexpr std::uint32_t kWmBaseVersion = 6;         // v6: suspended
        constexpr std::uint32_t kDataDeviceManagerVersion = 3;
        constexpr std::uint32_t kSeatVersion = 9;           // v8: axis_value120, v9: relative direction
        constexpr std::uint32_t kOutputVersion = 4;         // v4: name, description

        std::string DescribeError(wl_display* display, const int code)
        {
            if (code == EPROTO)
            {
                const wl_interface* interface = nullptr;
                std::uint32_t id = 0;
                const std::uint32_t protocolCode = wl_display_get_protocol_error(display, &interface, &id);
                return "the compositor raised protocol error " + std::to_string(protocolCode) + " on " +
                       (interface != nullptr ? std::string(interface->name) : std::string("an unknown object")) +
                       "@" + std::to_string(id);
            }
            return std::string("the connection to the compositor failed: ") + std::strerror(code);
        }

    } // namespace

    WaylandConnection::WaylandConnection(WaylandRegistryObserver& observer, const std::chrono::milliseconds timeout)
        : observer_(observer)
    {
        display_ = wl_display_connect(nullptr);
        if (display_ == nullptr)
        {
            const char* name = std::getenv("WAYLAND_DISPLAY");
            throw PlatformException("WaylandConnection",
                                    std::string("no Wayland compositor could be reached (") +
                                        (name != nullptr && *name != '\0' ? std::string("WAYLAND_DISPLAY=") + name
                                                                          : std::string("WAYLAND_DISPLAY is unset")) +
                                        ", " + std::strerror(errno) + ")");
        }

        static const wl_registry_listener listener = {
            .global = &WaylandConnection::OnGlobal,
            .global_remove = &WaylandConnection::OnGlobalRemove,
        };
        registry_ = wl_display_get_registry(display_);
        wl_registry_add_listener(registry_, &listener, this);

        // Two exchanges: the first delivers the globals and binds them; the second delivers what
        // the bound objects say about themselves at once (wl_shm's formats, a seat's
        // capabilities, an output's geometry), which the capability set is computed from.
        const bool announced = Roundtrip(timeout) && Roundtrip(timeout);
        if (!announced || !IsAlive())
        {
            const std::string why = !IsAlive() ? error_ : std::string("the compositor did not answer in time");
            observer_.OnDisconnecting();
            DestroySingletons();
            if (registry_ != nullptr) { wl_registry_destroy(registry_); }
            wl_display_disconnect(display_);
            display_ = nullptr;
            throw PlatformException("WaylandConnection", why);
        }
        if (globals_.compositor == nullptr || globals_.wmBase == nullptr)
        {
            observer_.OnDisconnecting();
            DestroySingletons();
            wl_registry_destroy(registry_);
            wl_display_disconnect(display_);
            display_ = nullptr;
            throw PlatformException("WaylandConnection",
                                    globals_.compositor == nullptr
                                        ? "the compositor offers no wl_compositor"
                                        : "the compositor offers no xdg_wm_base (xdg-shell), so it cannot show "
                                          "top-level windows");
        }
    }

    WaylandConnection::~WaylandConnection()
    {
        if (display_ == nullptr)
        {
            return;
        }
        observer_.OnDisconnecting();
        DestroySingletons();
        if (registry_ != nullptr)
        {
            wl_registry_destroy(registry_);
            registry_ = nullptr;
        }
        // Requests still buffered (destroy requests above) are sent before the socket closes, so
        // the compositor sees an orderly goodbye rather than an unexplained hang-up.
        wl_display_flush(display_);
        wl_display_disconnect(display_);
        display_ = nullptr;
    }

    void WaylandConnection::DestroySingletons()
    {
        for (auto it = bound_.rbegin(); it != bound_.rend(); ++it)
        {
            if (it->slot != nullptr && *it->slot != nullptr)
            {
                it->destroy(*it->slot);
                *it->slot = nullptr;
            }
        }
        bound_.clear();
    }

    void WaylandConnection::OnGlobal(void* data, wl_registry* registry, const std::uint32_t name,
                                     const char* interface, const std::uint32_t version)
    {
        (void) registry;
        static_cast<WaylandConnection*>(data)->Bind(name, interface, version);
    }

    void WaylandConnection::Bind(const std::uint32_t name, const char* interface, const std::uint32_t version)
    {
        const std::string_view which(interface);

        // Seats and outputs are objects with state of their own; the observer owns them.
        if (which == wl_seat_interface.name)
        {
            observer_.OnSeatAnnounced(registry_, name, NegotiateVersion(version, kSeatVersion,
                                                              static_cast<std::uint32_t>(wl_seat_interface.version)));
            return;
        }
        if (which == wl_output_interface.name)
        {
            observer_.OnOutputAnnounced(
                registry_, name, NegotiateVersion(version, kOutputVersion, static_cast<std::uint32_t>(wl_output_interface.version)));
            return;
        }

        // A singleton: bound once, into its field. A second announcement of an interface already
        // bound (a compositor that restarted a service) replaces nothing -- the first stays.
        const auto bindSingleton = [&](const wl_interface& type, std::uint32_t handled, void** slot, auto destroy,
                                       std::uint32_t* boundVersion = nullptr) -> void* {
            if (*slot != nullptr)
            {
                return nullptr;
            }
            const std::uint32_t chosen =
                NegotiateVersion(version, handled, static_cast<std::uint32_t>(type.version));
            *slot = wl_registry_bind(registry_, name, &type, chosen);
            if (boundVersion != nullptr) { *boundVersion = chosen; }
            bound_.push_back({name, slot, destroy});
            return *slot;
        };

        if (which == wl_compositor_interface.name)
        {
            bindSingleton(wl_compositor_interface, kCompositorVersion, reinterpret_cast<void**>(&globals_.compositor),
                          [](void* proxy) { wl_compositor_destroy(static_cast<wl_compositor*>(proxy)); },
                          &globals_.compositorVersion);
        }
        else if (which == wl_subcompositor_interface.name)
        {
            bindSingleton(wl_subcompositor_interface, kSubcompositorVersion,
                          reinterpret_cast<void**>(&globals_.subcompositor),
                          [](void* proxy) { wl_subcompositor_destroy(static_cast<wl_subcompositor*>(proxy)); });
        }
        else if (which == wl_shm_interface.name)
        {
            static const wl_shm_listener shmListener = {.format = &WaylandConnection::OnShmFormat};
            if (void* shm = bindSingleton(wl_shm_interface, kShmVersion, reinterpret_cast<void**>(&globals_.shm),
                                          [](void* proxy) {
                                              auto* shm = static_cast<wl_shm*>(proxy);
#if defined(WL_SHM_RELEASE_SINCE_VERSION)
                                              if (wl_shm_get_version(shm) >= WL_SHM_RELEASE_SINCE_VERSION)
                                              {
                                                  wl_shm_release(shm);
                                                  return;
                                              }
#endif
                                              wl_shm_destroy(shm);
                                          }))
            {
                wl_shm_add_listener(static_cast<wl_shm*>(shm), &shmListener, this);
            }
        }
        else if (which == xdg_wm_base_interface.name)
        {
            static const xdg_wm_base_listener wmBaseListener = {.ping = &WaylandConnection::OnPing};
            if (void* wmBase = bindSingleton(xdg_wm_base_interface, kWmBaseVersion,
                                             reinterpret_cast<void**>(&globals_.wmBase),
                                             [](void* proxy) { xdg_wm_base_destroy(static_cast<xdg_wm_base*>(proxy)); },
                                             &globals_.wmBaseVersion))
            {
                xdg_wm_base_add_listener(static_cast<xdg_wm_base*>(wmBase), &wmBaseListener, this);
            }
        }
        else if (which == wl_data_device_manager_interface.name)
        {
            bindSingleton(wl_data_device_manager_interface, kDataDeviceManagerVersion,
                          reinterpret_cast<void**>(&globals_.dataDeviceManager),
                          [](void* proxy) {
                              auto* manager = static_cast<wl_data_device_manager*>(proxy);
                              // v3 has no destructor request of its own; the proxy is dropped.
                              wl_data_device_manager_destroy(manager);
                          },
                          &globals_.dataDeviceManagerVersion);
        }
#if defined(CNA_WAYLAND_HAVE_XDG_OUTPUT)
        else if (which == zxdg_output_manager_v1_interface.name)
        {
            bindSingleton(zxdg_output_manager_v1_interface, 3, reinterpret_cast<void**>(&globals_.xdgOutputManager),
                          [](void* proxy) { zxdg_output_manager_v1_destroy(static_cast<zxdg_output_manager_v1*>(proxy)); },
                          &globals_.xdgOutputManagerVersion);
        }
#endif
#if defined(CNA_WAYLAND_HAVE_VIEWPORTER)
        else if (which == wp_viewporter_interface.name)
        {
            bindSingleton(wp_viewporter_interface, 1, reinterpret_cast<void**>(&globals_.viewporter),
                          [](void* proxy) { wp_viewporter_destroy(static_cast<wp_viewporter*>(proxy)); });
        }
#endif
#if defined(CNA_WAYLAND_HAVE_FRACTIONAL_SCALE)
        else if (which == wp_fractional_scale_manager_v1_interface.name)
        {
            bindSingleton(wp_fractional_scale_manager_v1_interface, 1,
                          reinterpret_cast<void**>(&globals_.fractionalScaleManager), [](void* proxy) {
                              wp_fractional_scale_manager_v1_destroy(static_cast<wp_fractional_scale_manager_v1*>(proxy));
                          });
        }
#endif
#if defined(CNA_WAYLAND_HAVE_RELATIVE_POINTER)
        else if (which == zwp_relative_pointer_manager_v1_interface.name)
        {
            bindSingleton(zwp_relative_pointer_manager_v1_interface, 1,
                          reinterpret_cast<void**>(&globals_.relativePointerManager), [](void* proxy) {
                              zwp_relative_pointer_manager_v1_destroy(static_cast<zwp_relative_pointer_manager_v1*>(proxy));
                          });
        }
#endif
#if defined(CNA_WAYLAND_HAVE_POINTER_CONSTRAINTS)
        else if (which == zwp_pointer_constraints_v1_interface.name)
        {
            bindSingleton(zwp_pointer_constraints_v1_interface, 1,
                          reinterpret_cast<void**>(&globals_.pointerConstraints), [](void* proxy) {
                              zwp_pointer_constraints_v1_destroy(static_cast<zwp_pointer_constraints_v1*>(proxy));
                          });
        }
#endif
#if defined(CNA_WAYLAND_HAVE_TEXT_INPUT_V3)
        else if (which == zwp_text_input_manager_v3_interface.name)
        {
            bindSingleton(zwp_text_input_manager_v3_interface, 1, reinterpret_cast<void**>(&globals_.textInputManager),
                          [](void* proxy) {
                              zwp_text_input_manager_v3_destroy(static_cast<zwp_text_input_manager_v3*>(proxy));
                          });
        }
#endif
#if defined(CNA_WAYLAND_HAVE_PRIMARY_SELECTION)
        else if (which == zwp_primary_selection_device_manager_v1_interface.name)
        {
            bindSingleton(zwp_primary_selection_device_manager_v1_interface, 1,
                          reinterpret_cast<void**>(&globals_.primarySelectionManager), [](void* proxy) {
                              zwp_primary_selection_device_manager_v1_destroy(
                                  static_cast<zwp_primary_selection_device_manager_v1*>(proxy));
                          });
        }
#endif
#if defined(CNA_WAYLAND_HAVE_XDG_DECORATION)
        else if (which == zxdg_decoration_manager_v1_interface.name)
        {
            bindSingleton(zxdg_decoration_manager_v1_interface, 1, reinterpret_cast<void**>(&globals_.decorationManager),
                          [](void* proxy) {
                              zxdg_decoration_manager_v1_destroy(static_cast<zxdg_decoration_manager_v1*>(proxy));
                          });
        }
#endif
#if defined(CNA_WAYLAND_HAVE_XDG_ACTIVATION)
        else if (which == xdg_activation_v1_interface.name)
        {
            bindSingleton(xdg_activation_v1_interface, 1, reinterpret_cast<void**>(&globals_.activation),
                          [](void* proxy) { xdg_activation_v1_destroy(static_cast<xdg_activation_v1*>(proxy)); });
        }
#endif
#if defined(CNA_WAYLAND_HAVE_IDLE_INHIBIT)
        else if (which == zwp_idle_inhibit_manager_v1_interface.name)
        {
            bindSingleton(zwp_idle_inhibit_manager_v1_interface, 1, reinterpret_cast<void**>(&globals_.idleInhibitManager),
                          [](void* proxy) {
                              zwp_idle_inhibit_manager_v1_destroy(static_cast<zwp_idle_inhibit_manager_v1*>(proxy));
                          });
        }
#endif
#if defined(CNA_WAYLAND_HAVE_PRESENTATION_TIME)
        else if (which == wp_presentation_interface.name)
        {
            bindSingleton(wp_presentation_interface, 1, reinterpret_cast<void**>(&globals_.presentation),
                          [](void* proxy) { wp_presentation_destroy(static_cast<wp_presentation*>(proxy)); });
        }
#endif
#if defined(CNA_WAYLAND_HAVE_XDG_FOREIGN)
        else if (which == zxdg_exporter_v2_interface.name)
        {
            bindSingleton(zxdg_exporter_v2_interface, 1, reinterpret_cast<void**>(&globals_.exporter),
                          [](void* proxy) { zxdg_exporter_v2_destroy(static_cast<zxdg_exporter_v2*>(proxy)); });
        }
#endif
#if defined(CNA_WAYLAND_HAVE_CURSOR_SHAPE)
        else if (which == wp_cursor_shape_manager_v1_interface.name)
        {
            bindSingleton(wp_cursor_shape_manager_v1_interface, 2, reinterpret_cast<void**>(&globals_.cursorShapeManager),
                          [](void* proxy) {
                              wp_cursor_shape_manager_v1_destroy(static_cast<wp_cursor_shape_manager_v1*>(proxy));
                          },
                          &globals_.cursorShapeManagerVersion);
        }
#endif
    }

    void WaylandConnection::OnGlobalRemove(void* data, wl_registry* registry, const std::uint32_t name)
    {
        (void) registry;
        auto* self = static_cast<WaylandConnection*>(data);
        for (auto it = self->bound_.begin(); it != self->bound_.end(); ++it)
        {
            if (it->name != name)
            {
                continue;
            }
            // A withdrawn singleton: its proxy stays valid until destroyed, and requests on it are
            // ignored. Destroying it now and nulling the field makes every later use see that the
            // compositor has taken it back.
            if (it->slot != nullptr && *it->slot != nullptr)
            {
                it->destroy(*it->slot);
                *it->slot = nullptr;
            }
            self->bound_.erase(it);
            return;
        }
        self->observer_.OnGlobalRemoved(name);
    }

    void WaylandConnection::OnPing(void* data, xdg_wm_base* wmBase, const std::uint32_t serial)
    {
        (void) data;
        // Answered from inside the dispatch, so a client that pumps once per frame answers within
        // a frame; a compositor marks a client that does not answer as not responding.
        xdg_wm_base_pong(wmBase, serial);
    }

    void WaylandConnection::OnShmFormat(void* data, wl_shm* shm, const std::uint32_t format)
    {
        (void) shm;
        if (format == WL_SHM_FORMAT_XRGB8888)
        {
            static_cast<WaylandConnection*>(data)->hasXrgb8888_ = true;
        }
    }

    bool WaylandConnection::CheckError()
    {
        if (!error_.empty())
        {
            return false;
        }
        const int code = wl_display_get_error(display_);
        if (code == 0)
        {
            return true;
        }
        error_ = DescribeError(display_, code);
        return false;
    }

    void WaylandConnection::Flush()
    {
        if (!IsAlive())
        {
            return;
        }
        // EAGAIN means the socket buffer is full: the rest goes out on a later flush, when the
        // compositor has read what it already has. Blocking here instead would tie the game's
        // frame to how fast the compositor reads.
        const int result = wl_display_flush(display_);
        flushPending_ = result < 0 && errno == EAGAIN;
        if (result < 0 && errno != EAGAIN)
        {
            (void) CheckError();
        }
    }

    bool WaylandConnection::DispatchFor(const std::chrono::milliseconds timeout)
    {
        if (!IsAlive())
        {
            return false;
        }
        // Everything already queued is dispatched first: prepare_read refuses while the queue is
        // not empty, and that refusal is the protocol's way of saying "dispatch before you read".
        while (wl_display_prepare_read(display_) != 0)
        {
            if (wl_display_dispatch_pending(display_) < 0)
            {
                return CheckError();
            }
        }
        Flush();

        pollfd descriptor{wl_display_get_fd(display_), static_cast<short>(POLLIN | (flushPending_ ? POLLOUT : 0)), 0};
        int ready = 0;
        do
        {
            ready = ::poll(&descriptor, 1, static_cast<int>(timeout.count()));
        } while (ready < 0 && errno == EINTR);

        if (ready > 0 && (descriptor.revents & (POLLIN | POLLERR | POLLHUP)) != 0)
        {
            // read_events releases the read intent whatever it returns.
            if (wl_display_read_events(display_) < 0)
            {
                return CheckError();
            }
        }
        else
        {
            wl_display_cancel_read(display_);
        }
        if (ready > 0 && (descriptor.revents & POLLOUT) != 0)
        {
            Flush();
        }
        if (wl_display_dispatch_pending(display_) < 0)
        {
            return CheckError();
        }
        return CheckError();
    }

    bool WaylandConnection::Pump()
    {
        return DispatchFor(std::chrono::milliseconds(0));
    }

    bool WaylandConnection::DispatchUntil(const std::function<bool()>& condition,
                                          const std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!condition())
        {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline || !IsAlive())
            {
                return condition();
            }
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
            if (!DispatchFor(remaining.count() > 0 ? remaining : std::chrono::milliseconds(1)))
            {
                return condition();
            }
        }
        return true;
    }

    bool WaylandConnection::Roundtrip(const std::chrono::milliseconds timeout)
    {
        if (!IsAlive())
        {
            return false;
        }
        // The done flag lives on this stack frame, so the callback proxy must never outlive the
        // call: destroyed by its own handler when the compositor answers, and here when it does
        // not answer in time.
        struct Sync
        {
            bool done = false;
            wl_callback* callback = nullptr;
        } sync;
        static const wl_callback_listener listener = {
            .done = [](void* data, wl_callback* callback, std::uint32_t) {
                auto* state = static_cast<Sync*>(data);
                state->done = true;
                wl_callback_destroy(callback);
                state->callback = nullptr;
            },
        };
        sync.callback = wl_display_sync(display_);
        wl_callback_add_listener(sync.callback, &listener, &sync);
        const bool answered = DispatchUntil([&sync] { return sync.done; }, timeout);
        if (sync.callback != nullptr)
        {
            wl_callback_destroy(sync.callback);
            sync.callback = nullptr;
        }
        return answered && IsAlive();
    }

} // namespace CNA::Platform::Wayland
