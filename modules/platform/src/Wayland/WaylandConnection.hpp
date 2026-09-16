// SPDX-License-Identifier: MS-PL
#pragma once

#include "WaylandProtocols.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace CNA::Platform::Wayland {

    /**
     * @brief The compositor-wide singletons the backend binds, or null where the compositor does
     * not offer one.
     *
     * Every field is read at the point of use rather than cached by a service, because a
     * compositor may withdraw a global at any time (`wl_registry.global_remove`); the connection
     * then destroys the proxy and nulls the field, and the next use sees that.
     */
    struct WaylandGlobals
    {
        /** @brief `wl_compositor`. Mandatory. */
        wl_compositor* compositor = nullptr;
        /** @brief Bound version of `wl_compositor`. */
        std::uint32_t compositorVersion = 0;
        /** @brief `wl_subcompositor`: client-side decorations. */
        wl_subcompositor* subcompositor = nullptr;
        /** @brief `wl_shm`: software presentation, cursors, decorations. */
        wl_shm* shm = nullptr;
        /** @brief `xdg_wm_base`. Mandatory. */
        xdg_wm_base* wmBase = nullptr;
        /** @brief Bound version of `xdg_wm_base` (v6 carries `suspended`). */
        std::uint32_t wmBaseVersion = 0;
        /** @brief `wl_data_device_manager`: clipboard and drag and drop. */
        wl_data_device_manager* dataDeviceManager = nullptr;
        /** @brief Bound version of `wl_data_device_manager`. */
        std::uint32_t dataDeviceManagerVersion = 0;
#if defined(CNA_WAYLAND_HAVE_XDG_OUTPUT)
        /** @brief `zxdg_output_manager_v1`: logical output geometry. */
        zxdg_output_manager_v1* xdgOutputManager = nullptr;
        /** @brief Bound version of `zxdg_output_manager_v1`. */
        std::uint32_t xdgOutputManagerVersion = 0;
#endif
#if defined(CNA_WAYLAND_HAVE_VIEWPORTER)
        /** @brief `wp_viewporter`: the logical size of a fractionally scaled buffer. */
        wp_viewporter* viewporter = nullptr;
#endif
#if defined(CNA_WAYLAND_HAVE_FRACTIONAL_SCALE)
        /** @brief `wp_fractional_scale_manager_v1`. */
        wp_fractional_scale_manager_v1* fractionalScaleManager = nullptr;
#endif
#if defined(CNA_WAYLAND_HAVE_RELATIVE_POINTER)
        /** @brief `zwp_relative_pointer_manager_v1`. */
        zwp_relative_pointer_manager_v1* relativePointerManager = nullptr;
#endif
#if defined(CNA_WAYLAND_HAVE_POINTER_CONSTRAINTS)
        /** @brief `zwp_pointer_constraints_v1`. */
        zwp_pointer_constraints_v1* pointerConstraints = nullptr;
#endif
#if defined(CNA_WAYLAND_HAVE_TEXT_INPUT_V3)
        /** @brief `zwp_text_input_manager_v3`: input-method composition. */
        zwp_text_input_manager_v3* textInputManager = nullptr;
#endif
#if defined(CNA_WAYLAND_HAVE_PRIMARY_SELECTION)
        /** @brief `zwp_primary_selection_device_manager_v1`. */
        zwp_primary_selection_device_manager_v1* primarySelectionManager = nullptr;
#endif
#if defined(CNA_WAYLAND_HAVE_XDG_DECORATION)
        /** @brief `zxdg_decoration_manager_v1`: server-side decorations. */
        zxdg_decoration_manager_v1* decorationManager = nullptr;
#endif
#if defined(CNA_WAYLAND_HAVE_XDG_ACTIVATION)
        /** @brief `xdg_activation_v1`. */
        xdg_activation_v1* activation = nullptr;
#endif
#if defined(CNA_WAYLAND_HAVE_IDLE_INHIBIT)
        /** @brief `zwp_idle_inhibit_manager_v1`. */
        zwp_idle_inhibit_manager_v1* idleInhibitManager = nullptr;
#endif
#if defined(CNA_WAYLAND_HAVE_XDG_FOREIGN)
        /** @brief `zxdg_exporter_v2`: a window handle another process can name. */
        zxdg_exporter_v2* exporter = nullptr;
#endif
#if defined(CNA_WAYLAND_HAVE_TABLET)
        /** @brief `zwp_tablet_manager_v2`: the graphics tablets of a seat. */
        zwp_tablet_manager_v2* tabletManager = nullptr;
#endif
#if defined(CNA_WAYLAND_HAVE_CURSOR_SHAPE)
        /** @brief `wp_cursor_shape_manager_v1`. */
        wp_cursor_shape_manager_v1* cursorShapeManager = nullptr;
        /** @brief Bound version of `wp_cursor_shape_manager_v1`. */
        std::uint32_t cursorShapeManagerVersion = 0;
#endif
    };

    /**
     * @brief Receives the globals that can come and go in numbers: seats and outputs.
     *
     * The connection binds the singletons itself; a seat or an output is an object with state of
     * its own, owned by whoever implements this.
     */
    class WaylandRegistryObserver
    {
    public:
        /** @brief Destroys the observer. */
        virtual ~WaylandRegistryObserver() = default;

        /**
         * @brief A `wl_seat` was announced.
         *
         * Called during the connection's own construction for the seats present at startup, so an
         * implementation must bind through @p registry rather than through the connection.
         *
         * @param registry The registry to bind it through.
         * @param name The global's name.
         * @param version The version to bind (already negotiated).
         */
        virtual void OnSeatAnnounced(wl_registry* registry, std::uint32_t name, std::uint32_t version) = 0;

        /**
         * @brief A `wl_output` was announced.
         * @param registry The registry to bind it through.
         * @param name The global's name.
         * @param version The version to bind (already negotiated).
         */
        virtual void OnOutputAnnounced(wl_registry* registry, std::uint32_t name, std::uint32_t version) = 0;

        /**
         * @brief A global that is not one of the connection's singletons was withdrawn.
         * @param name The global's name.
         */
        virtual void OnGlobalRemoved(std::uint32_t name) = 0;

        /**
         * @brief The connection is about to close: every proxy made from it must be destroyed now,
         * while the display is still there to destroy it on.
         */
        virtual void OnDisconnecting() = 0;
    };

    /**
     * @brief Negotiates the version to bind (plans/plan_wayland.md WAYLAND-0030).
     *
     * Never above what the compositor advertises, never above what this backend handles, and
     * never above what the libwayland headers it was compiled against describe: an event of a
     * version the listener struct does not have a slot for would be a call through garbage.
     *
     * @param advertised The version in `wl_registry.global`.
     * @param handled The highest version this backend implements.
     * @param headers The interface's version in the compiled headers.
     * @return The version to bind.
     */
    [[nodiscard]] constexpr std::uint32_t NegotiateVersion(const std::uint32_t advertised,
                                                           const std::uint32_t handled,
                                                           const std::uint32_t headers)
    {
        std::uint32_t version = advertised < handled ? advertised : handled;
        return version < headers ? version : headers;
    }

    /**
     * @brief One connection to a Wayland compositor, owned by one platform instance (D-4).
     *
     * Opens the display, binds the singletons the backend uses at the highest version both sides
     * handle, and dispatches events without ever blocking unboundedly (D-5): `Pump` never waits
     * at all, and `Roundtrip`/`DispatchFor` wait at most as long as they are told.
     *
     * A protocol or connection error ends the connection for good -- libwayland refuses every
     * later request on it. The first error is recorded with the interface, object and code the
     * compositor named, so the one line a user reports is the one that explains it.
     */
    class WaylandConnection
    {
    public:
        /**
         * @brief Connects to the compositor `WAYLAND_DISPLAY` (or `WAYLAND_SOCKET`) names and
         * binds the singletons, waiting at most `timeout` for the compositor to announce them.
         * @param observer Told about seats and outputs, now and later.
         * @param timeout How long the startup exchange may take.
         * @throws PlatformException If no compositor could be reached or it lacks
         * `wl_compositor` or `xdg_wm_base`.
         */
        WaylandConnection(WaylandRegistryObserver& observer, std::chrono::milliseconds timeout);

        /** @brief Destroys every singleton proxy and disconnects. */
        ~WaylandConnection();

        WaylandConnection(const WaylandConnection&) = delete;
        WaylandConnection& operator=(const WaylandConnection&) = delete;

        /** @brief Gets the display. @return The `wl_display`. */
        [[nodiscard]] wl_display* GetDisplay() const { return display_; }

        /** @brief Gets the registry. @return The `wl_registry`. */
        [[nodiscard]] wl_registry* GetRegistry() const { return registry_; }

        /** @brief Gets the bound singletons. @return The globals. */
        [[nodiscard]] const WaylandGlobals& GetGlobals() const { return globals_; }

        /** @brief Gets whether the connection still works. @return False after an error. */
        [[nodiscard]] bool IsAlive() const { return error_.empty(); }

        /** @brief Gets what ended the connection. @return The description, or empty. */
        [[nodiscard]] const std::string& GetError() const { return error_; }

        /** @brief Gets whether `wl_shm` offered XRGB8888 (every compositor must). */
        [[nodiscard]] bool HasXrgb8888() const { return hasXrgb8888_; }

        /**
         * @brief Dispatches whatever has arrived, without waiting for anything (D-5).
         * @return False when the connection has failed (see GetError).
         */
        bool Pump();

        /**
         * @brief Waits up to `timeout` for events and dispatches them, once.
         * @param timeout How long to wait for the socket to become readable.
         * @return False when the connection has failed.
         */
        bool DispatchFor(std::chrono::milliseconds timeout);

        /**
         * @brief Sends a `wl_display.sync` and dispatches until the compositor answers it or the
         * time is up -- a roundtrip that cannot hang on a stuck compositor.
         * @param timeout The longest wait.
         * @return True when the compositor answered in time.
         */
        bool Roundtrip(std::chrono::milliseconds timeout);

        /**
         * @brief Dispatches until a condition holds or the time is up.
         * @param condition Checked after every dispatch.
         * @param timeout The longest wait.
         * @return True when the condition held in time.
         */
        bool DispatchUntil(const std::function<bool()>& condition, std::chrono::milliseconds timeout);

        /** @brief Sends buffered requests; data the socket cannot take now stays queued. */
        void Flush();

        /**
         * @brief Reads the connection's error state after a libwayland call reported failure.
         * @return False when the connection has failed.
         */
        bool CheckError();

    private:
        static void OnGlobal(void* data, wl_registry* registry, std::uint32_t name, const char* interface,
                             std::uint32_t version);
        static void OnGlobalRemove(void* data, wl_registry* registry, std::uint32_t name);
        static void OnPing(void* data, xdg_wm_base* wmBase, std::uint32_t serial);
        static void OnShmFormat(void* data, wl_shm* shm, std::uint32_t format);

        void Bind(std::uint32_t name, const char* interface, std::uint32_t version);
        void DestroySingletons();

        struct BoundGlobal
        {
            std::uint32_t name = 0;
            void** slot = nullptr;
            void (*destroy)(void*) = nullptr;
        };

        WaylandRegistryObserver& observer_;
        wl_display* display_ = nullptr;
        wl_registry* registry_ = nullptr;
        WaylandGlobals globals_;
        std::vector<BoundGlobal> bound_;
        std::string error_;
        bool hasXrgb8888_ = false;
        bool flushPending_ = false;
    };

} // namespace CNA::Platform::Wayland
