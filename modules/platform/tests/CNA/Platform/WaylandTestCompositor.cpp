// SPDX-License-Identifier: MS-PL
//
// plans/plan_wayland.md WAYLAND-0111: the in-process test compositor. See the header for what it
// is for; this file is protocol plumbing. Every request of every interface the backend can send
// has an implementation (libwayland-server calls a null entry and crashes), and every rule the
// protocols make a protocol error is checked where the protocol says it is checked.

#include "WaylandTestCompositor.hpp"

#if defined(CNA_WAYLAND_HAVE_TEST_COMPOSITOR)

#include <wayland-server.h>

#include "xdg-shell-server-protocol.h"
#if defined(CNA_WAYLAND_HAVE_VIEWPORTER)
#  include "viewporter-server-protocol.h"
#endif
#if defined(CNA_WAYLAND_HAVE_FRACTIONAL_SCALE)
#  include "fractional-scale-v1-server-protocol.h"
#endif
#if defined(CNA_WAYLAND_HAVE_XDG_OUTPUT)
#  include "xdg-output-unstable-v1-server-protocol.h"
#endif
#if defined(CNA_WAYLAND_HAVE_RELATIVE_POINTER)
#  include "relative-pointer-unstable-v1-server-protocol.h"
#endif
#if defined(CNA_WAYLAND_HAVE_POINTER_CONSTRAINTS)
#  include "pointer-constraints-unstable-v1-server-protocol.h"
#endif
#if defined(CNA_WAYLAND_HAVE_TABLET)
#  include "tablet-v2-server-protocol.h"
#endif
#if defined(CNA_WAYLAND_HAVE_CURSOR_SHAPE)
// cursor-shape-v1 names zwp_tablet_tool_v2: tablet-v2's header comes first, as in the backend's
// own WaylandProtocols.hpp.
#  include "cursor-shape-v1-server-protocol.h"
#endif
#if defined(CNA_WAYLAND_HAVE_TEXT_INPUT_V3)
#  include "text-input-unstable-v3-server-protocol.h"
#endif
#if defined(CNA_WAYLAND_HAVE_PRIMARY_SELECTION)
#  include "primary-selection-unstable-v1-server-protocol.h"
#endif
#if defined(CNA_WAYLAND_HAVE_XDG_DECORATION)
#  include "xdg-decoration-unstable-v1-server-protocol.h"
#endif
#if defined(CNA_WAYLAND_HAVE_XDG_ACTIVATION)
#  include "xdg-activation-v1-server-protocol.h"
#endif
#if defined(CNA_WAYLAND_HAVE_IDLE_INHIBIT)
#  include "idle-inhibit-unstable-v1-server-protocol.h"
#endif
#if defined(CNA_WAYLAND_HAVE_XDG_FOREIGN)
#  include "xdg-foreign-unstable-v2-server-protocol.h"
#endif

#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <list>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

namespace CNA::Platform::Wayland::Testing {

    namespace {

        std::uint32_t NowMs()
        {
            timespec now{};
            clock_gettime(CLOCK_MONOTONIC, &now);
            return static_cast<std::uint32_t>(now.tv_sec * 1000 + now.tv_nsec / 1000000);
        }

        std::uint64_t NowUs()
        {
            timespec now{};
            clock_gettime(CLOCK_MONOTONIC, &now);
            return static_cast<std::uint64_t>(now.tv_sec) * 1000000u + static_cast<std::uint64_t>(now.tv_nsec) / 1000u;
        }

        template <typename T>
        T* Data(wl_resource* resource)
        {
            return static_cast<T*>(wl_resource_get_user_data(resource));
        }

        void DestroyRequest(wl_client*, wl_resource* resource)
        {
            wl_resource_destroy(resource);
        }

        /// A destroy listener with a C++ callback. The listener is the first member, so the
        /// notify function can find its owner without offsetof on a non-standard-layout type.
        struct DestroyWatch
        {
            wl_listener listener{};
            std::function<void()> onDestroy;
            bool armed = false;

            DestroyWatch() { wl_list_init(&listener.link); }
            DestroyWatch(const DestroyWatch&) = delete;
            DestroyWatch& operator=(const DestroyWatch&) = delete;
            ~DestroyWatch() { Unwatch(); }

            void Watch(wl_resource* resource, std::function<void()> callback)
            {
                Unwatch();
                if (resource == nullptr)
                {
                    return;
                }
                onDestroy = std::move(callback);
                listener.notify = [](wl_listener* raw, void*) {
                    auto* self = reinterpret_cast<DestroyWatch*>(raw);
                    // The signal has already unlinked the listener.
                    wl_list_init(&raw->link);
                    self->armed = false;
                    auto callback = std::move(self->onDestroy);
                    self->onDestroy = nullptr;
                    if (callback)
                    {
                        callback();
                    }
                };
                wl_resource_add_destroy_listener(resource, &listener);
                armed = true;
            }

            void Unwatch()
            {
                if (armed)
                {
                    wl_list_remove(&listener.link);
                    wl_list_init(&listener.link);
                    armed = false;
                }
                onDestroy = nullptr;
            }
        };

        /// A buffer reference that forgets the buffer when the client destroys it.
        struct BufferRef
        {
            wl_resource* buffer = nullptr;
            DestroyWatch watch;

            void Set(wl_resource* value)
            {
                watch.Unwatch();
                buffer = value;
                if (value != nullptr)
                {
                    watch.Watch(value, [this] { buffer = nullptr; });
                }
            }
        };

        using Payload = std::map<std::string, std::vector<std::uint8_t>>;

    } // namespace

    // =============================================================================================
    // State
    // =============================================================================================

    struct TestCompositor::State
    {
        struct Surface;
        struct XdgSurface;
        struct Toplevel;
        struct Subsurface;
        struct Viewport;
        struct Output;
        struct Constraint;
        struct TextInput;
        struct DataSource;
        struct Offer;
        struct Decoration;

        struct Region
        {
            bool nonEmpty = false;
        };

        struct Surface
        {
            State* server = nullptr;
            wl_resource* resource = nullptr;
            std::uint32_t id = 0;

            BufferRef pendingBuffer;
            bool pendingAttach = false;
            int pendingScale = 1;
            bool pendingScaleSet = false;
            bool pendingOpaqueSet = false;
            bool pendingOpaque = false;
            std::vector<wl_resource*> pendingFrames;

            BufferRef buffer;
            int bufferWidth = 0;
            int bufferHeight = 0;
            int scale = 1;
            bool opaque = false;
            std::vector<wl_resource*> frames;
            std::uint32_t centrePixel = 0;
            int commits = 0;
            int bufferCommits = 0;

            enum class Role { None, Toplevel, Subsurface, Cursor } role = Role::None;
            XdgSurface* xdg = nullptr;
            Subsurface* sub = nullptr;
            Viewport* viewport = nullptr;
            wl_resource* fractional = nullptr;
            std::vector<Subsurface*> children;

            [[nodiscard]] int SurfaceWidth() const;
            [[nodiscard]] int SurfaceHeight() const;
        };

        struct XdgSurface
        {
            State* server = nullptr;
            wl_resource* resource = nullptr;
            wl_resource* wmBase = nullptr;
            Surface* surface = nullptr;
            Toplevel* toplevel = nullptr;

            struct Sent
            {
                std::uint32_t serial = 0;
                int width = 0;
                int height = 0;
                bool maximized = false;
                bool fullscreen = false;
            };
            std::vector<Sent> sent;
            Sent acked;
            bool everAcked = false;
            bool initialCommitSeen = false;

            bool pendingGeometrySet = false;
            int px = 0, py = 0, pw = 0, ph = 0;
            bool geometrySet = false;
            int gx = 0, gy = 0, gw = 0, gh = 0;
        };

        struct Toplevel
        {
            State* server = nullptr;
            wl_resource* resource = nullptr;
            XdgSurface* xdg = nullptr;
            std::string title;
            std::string appId;
            int pendingMinW = 0, pendingMinH = 0, pendingMaxW = 0, pendingMaxH = 0;
            int minW = 0, minH = 0, maxW = 0, maxH = 0;
            bool maximized = false;
            bool fullscreen = false;
            bool maximizeRequested = false;
            bool fullscreenRequested = false;
            int minimizeRequests = 0;
            int configures = 0;
            int acks = 0;
            std::uint32_t lastConfigureSerial = 0;
            std::uint32_t lastAckedSerial = 0;
            bool mapped = false;
            int moveRequests = 0;
            int resizeRequests = 0;
            std::uint32_t lastResizeEdges = 0;
            int windowMenuRequests = 0;
            Decoration* decoration = nullptr;
            std::uint32_t requestedDecorationMode = 0;
        };

        struct Subsurface
        {
            State* server = nullptr;
            wl_resource* resource = nullptr;
            Surface* surface = nullptr;
            Surface* parent = nullptr;
            int pendingX = 0, pendingY = 0;
            int x = 0, y = 0;
            bool desync = false;
        };

        struct Viewport
        {
            State* server = nullptr;
            wl_resource* resource = nullptr;
            Surface* surface = nullptr;
            bool pendingSourceSet = false;
            double psx = 0, psy = 0, psw = 0, psh = 0;
            bool pendingDestinationSet = false;
            int pdw = 0, pdh = 0;
            bool sourceSet = false;
            double sx = 0, sy = 0, sw = 0, sh = 0;
            bool destinationSet = false;
            int dw = 0, dh = 0;
        };

        struct Output
        {
            State* server = nullptr;
            OutputSpec spec;
            wl_global* global = nullptr;
            std::vector<wl_resource*> resources;
            std::vector<wl_resource*> xdgOutputs;
            bool removed = false;
        };

        struct Constraint
        {
            State* server = nullptr;
            wl_resource* resource = nullptr;
            Surface* surface = nullptr;
            bool lock = true;
            std::uint32_t lifetime = 0;
            bool active = false;
            bool defunct = false;
            bool pendingHint = false;
            double phx = 0, phy = 0;
            bool hasHint = false;
            double hx = 0, hy = 0;
        };

        struct TextInput
        {
            State* server = nullptr;
            wl_resource* resource = nullptr;
            struct Values
            {
                bool enabled = false;
                std::uint32_t hint = 0;
                std::uint32_t purpose = 0;
                int cx = 0, cy = 0, cw = 0, ch = 0;
            } pending, current;
            std::uint32_t commits = 0;
        };

        struct DataSource
        {
            State* server = nullptr;
            wl_resource* resource = nullptr;
            bool primary = false;
            std::vector<std::string> mimeTypes;
            std::uint32_t actions = 0;
        };

        struct Offer
        {
            State* server = nullptr;
            wl_resource* resource = nullptr;
            bool primary = false;
            bool dnd = false;
            Payload payload;           // another program's data
            DataSource* source = nullptr;  // the client's own source, echoed back
            bool dropped = false;
        };

        struct Decoration
        {
            State* server = nullptr;
            wl_resource* resource = nullptr;
            Toplevel* toplevel = nullptr;
        };

        struct Token
        {
            State* server = nullptr;
            wl_resource* resource = nullptr;
            bool committed = false;
        };

        struct Task
        {
            std::function<void()> work;
            bool done = false;
            std::exception_ptr error;
        };

        // --- members --------------------------------------------------------------------------

        CompositorOptions options;
        wl_display* display = nullptr;
        wl_event_loop* loop = nullptr;
        wl_client* client = nullptr;
        bool clientDying = false;
        bool clientConnected = false;
        int clientFd = -1;
        bool exported = false;
        /// The client's destroy listener. Standard layout with the listener first, so the notify
        /// function reaches the state without offsetof on State itself.
        struct ClientWatch
        {
            wl_listener listener{};
            State* state = nullptr;
        } clientWatch;

        std::thread thread;
        std::thread::id threadId;
        int taskFd = -1;
        wl_event_source* taskSource = nullptr;
        std::mutex mutex;
        std::condition_variable finished;
        std::deque<Task*> tasks;
        bool stop = false;

        std::string postedError;
        std::vector<std::string> violations;

        std::map<std::string, wl_global*> singletons;
        std::vector<wl_global*> removedGlobals;
        std::vector<Output*> outputs;

        std::vector<Surface*> surfaces;
        std::vector<Toplevel*> toplevels;
        std::vector<Subsurface*> subsurfaces;
        std::vector<XdgSurface*> xdgSurfaces;
        std::list<BufferRef> held;
        int pongs = 0;

        std::vector<wl_resource*> seats;
        std::vector<wl_resource*> pointers;
        std::vector<wl_resource*> keyboards;
        std::vector<wl_resource*> touches;
        std::vector<wl_resource*> relativePointers;
        std::vector<Constraint*> constraints;
        std::vector<wl_resource*> shapeDevices;
        std::vector<wl_resource*> tabletSeats;
        std::vector<wl_resource*> tablets;
        std::vector<wl_resource*> tabletTools;
        std::vector<TextInput*> textInputs;
        std::vector<wl_resource*> dataDevices;
        std::vector<wl_resource*> primaryDevices;
        std::vector<Offer*> offers;
        std::vector<Decoration*> decorations;

        xkb_context* xkbContext = nullptr;
        xkb_keymap* keymap = nullptr;
        xkb_state* xkbState = nullptr;
        std::string keymapText;
        std::uint32_t lastDepressed = 0, lastLatched = 0, lastLocked = 0, lastGroup = 0;

        Surface* keyboardFocus = nullptr;
        Surface* pointerFocus = nullptr;
        Surface* touchFocus = nullptr;
        Surface* tabletFocus = nullptr;
        std::set<std::uint32_t> inputSerials;
        std::uint32_t lastInputSerial = 0;
        /// Serials of button presses (and touch downs) still held: the implicit grabs a move,
        /// resize or window menu may name, as Mutter checks.
        std::map<std::uint32_t, std::uint32_t> heldPresses;  // button -> serial
        std::uint32_t pointerEnterSerial = 0;

        PointerInfo pointerInfo;
        std::uint32_t lastCursorSurfaceId = 0;

        DataSource* clientSelection = nullptr;
        DataSource* clientPrimary = nullptr;
        std::optional<Payload> externalSelection;
        std::optional<Payload> externalPrimary;
        ClientSelectionInfo selectionInfo;
        ClientSelectionInfo primaryInfo;

        Offer* dragOffer = nullptr;
        wl_resource* dragDevice = nullptr;
        Surface* dragSurface = nullptr;
        DragInfo dragInfo;
        std::uint32_t dragSourceActions = 0;

        Surface* textInputFocus = nullptr;

        int idleInhibitors = 0;
        int activationTokens = 0;
        std::vector<std::string> activations;
        int exports = 0;

        std::vector<std::thread> writers;

        // --- helpers --------------------------------------------------------------------------

        void PostError(wl_resource* resource, const std::uint32_t code, const std::string& message)
        {
            if (clientDying)
            {
                return;
            }
            if (postedError.empty())
            {
                postedError = std::string(wl_resource_get_class(resource)) + "#" + std::to_string(code) + ": " + message;
            }
            wl_resource_post_error(resource, code, "%s", message.c_str());
        }

        void Violation(const std::string& what)
        {
            if (!clientDying)
            {
                violations.push_back(what);
            }
        }

        std::uint32_t NextSerial() { return wl_display_next_serial(display); }

        std::uint32_t InputSerial()
        {
            const std::uint32_t serial = NextSerial();
            inputSerials.insert(serial);
            lastInputSerial = serial;
            return serial;
        }

        void CheckGrabSerial(const std::uint32_t serial, const char* request)
        {
            for (const auto& [button, held] : heldPresses)
            {
                (void) button;
                if (held == serial)
                {
                    return;
                }
            }
            Violation(std::string(request) + " used serial " + std::to_string(serial) +
                      ", which is not a button press still held (the compositor would ignore it)");
        }

        void CheckInputSerial(const std::uint32_t serial, const char* request)
        {
            if (inputSerials.count(serial) == 0)
            {
                Violation(std::string(request) + " used serial " + std::to_string(serial) +
                          ", which no input event carried");
            }
        }

        Output* PrimaryOutput() const
        {
            for (Output* output : outputs)
            {
                if (!output->removed)
                {
                    return output;
                }
            }
            return nullptr;
        }

        void OutputLogical(const Output* output, int& width, int& height) const
        {
            if (output == nullptr)
            {
                width = 1280;
                height = 720;
                return;
            }
            const int scale = std::max(1, output->spec.scale);
            width = output->spec.logicalWidth > 0 ? output->spec.logicalWidth : output->spec.width / scale;
            height = output->spec.logicalHeight > 0 ? output->spec.logicalHeight : output->spec.height / scale;
        }

        Toplevel* ToplevelAt(const int index) const
        {
            if (index < 0 || static_cast<std::size_t>(index) >= toplevels.size())
            {
                throw std::out_of_range("the test compositor has no toplevel " + std::to_string(index));
            }
            return toplevels[static_cast<std::size_t>(index)];
        }

        Surface* FindSurface(const std::uint32_t id) const
        {
            for (Surface* surface : surfaces)
            {
                if (surface->id == id)
                {
                    return surface;
                }
            }
            return nullptr;
        }

        wl_resource* OutputResourceFor(const Output* output) const
        {
            return output != nullptr && !output->resources.empty() ? output->resources.front() : nullptr;
        }

        std::vector<std::uint32_t> StatesOf(const Toplevel* toplevel) const
        {
            std::vector<std::uint32_t> states;
            if (toplevel->maximized) { states.push_back(XDG_TOPLEVEL_STATE_MAXIMIZED); }
            if (toplevel->fullscreen) { states.push_back(XDG_TOPLEVEL_STATE_FULLSCREEN); }
            if (options.activated) { states.push_back(XDG_TOPLEVEL_STATE_ACTIVATED); }
            return states;
        }

        std::uint32_t SendConfigure(Toplevel* toplevel, const int width, const int height,
                                    const std::vector<std::uint32_t>& states)
        {
            wl_array array;
            wl_array_init(&array);
            bool maximized = false;
            bool fullscreen = false;
            for (const std::uint32_t state : states)
            {
                *static_cast<std::uint32_t*>(wl_array_add(&array, sizeof(std::uint32_t))) = state;
                maximized = maximized || state == XDG_TOPLEVEL_STATE_MAXIMIZED;
                fullscreen = fullscreen || state == XDG_TOPLEVEL_STATE_FULLSCREEN;
            }
            xdg_toplevel_send_configure(toplevel->resource, width, height, &array);
            wl_array_release(&array);
            const std::uint32_t serial = NextSerial();
            xdg_surface_send_configure(toplevel->xdg->resource, serial);
            toplevel->xdg->sent.push_back({serial, width, height, maximized, fullscreen});
            toplevel->maximized = maximized;
            toplevel->fullscreen = fullscreen;
            ++toplevel->configures;
            toplevel->lastConfigureSerial = serial;
            return serial;
        }

        void Reconfigure(Toplevel* toplevel)
        {
            int width = 0;
            int height = 0;
            if (toplevel->maximized || toplevel->fullscreen)
            {
                OutputLogical(PrimaryOutput(), width, height);
            }
            SendConfigure(toplevel, width, height, StatesOf(toplevel));
        }

        // --- keymap ---------------------------------------------------------------------------

        void BuildKeymap(const std::string& layout, const std::string& variant, const std::string& extraOptions)
        {
            if (xkbState != nullptr) { xkb_state_unref(xkbState); xkbState = nullptr; }
            if (keymap != nullptr) { xkb_keymap_unref(keymap); keymap = nullptr; }
            if (xkbContext == nullptr)
            {
                xkbContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
            }
            const xkb_rule_names names = {"evdev", "pc105", layout.c_str(), variant.c_str(), extraOptions.c_str()};
            keymap = xkbContext != nullptr ? xkb_keymap_new_from_names(xkbContext, &names, XKB_KEYMAP_COMPILE_NO_FLAGS)
                                           : nullptr;
            keymapText.clear();
            if (keymap != nullptr)
            {
                xkbState = xkb_state_new(keymap);
                char* text = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
                if (text != nullptr)
                {
                    keymapText = text;
                    std::free(text);
                }
            }
            lastDepressed = lastLatched = lastLocked = lastGroup = 0;
        }

        void SendKeymap(wl_resource* keyboard)
        {
            if (keymapText.empty())
            {
                const int fd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
                wl_keyboard_send_keymap(keyboard, WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP, fd, 0);
                ::close(fd);
                return;
            }
            const std::size_t size = keymapText.size() + 1;
            const int fd = memfd_create("cna-test-keymap", MFD_CLOEXEC);
            if (fd < 0)
            {
                return;
            }
            std::size_t written = 0;
            while (written < size)
            {
                const ssize_t result = ::write(fd, keymapText.c_str() + written, size - written);
                if (result <= 0)
                {
                    break;
                }
                written += static_cast<std::size_t>(result);
            }
            wl_keyboard_send_keymap(keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, static_cast<std::uint32_t>(size));
            ::close(fd);
        }

        void SendModifiersIfChanged(const bool force)
        {
            if (xkbState == nullptr)
            {
                return;
            }
            const std::uint32_t depressed = xkb_state_serialize_mods(xkbState, XKB_STATE_MODS_DEPRESSED);
            const std::uint32_t latched = xkb_state_serialize_mods(xkbState, XKB_STATE_MODS_LATCHED);
            const std::uint32_t locked = xkb_state_serialize_mods(xkbState, XKB_STATE_MODS_LOCKED);
            const std::uint32_t group = xkb_state_serialize_layout(xkbState, XKB_STATE_LAYOUT_EFFECTIVE);
            if (!force && depressed == lastDepressed && latched == lastLatched && locked == lastLocked && group == lastGroup)
            {
                return;
            }
            lastDepressed = depressed;
            lastLatched = latched;
            lastLocked = locked;
            lastGroup = group;
            if (keyboardFocus == nullptr)
            {
                return;
            }
            const std::uint32_t serial = NextSerial();
            for (wl_resource* keyboard : keyboards)
            {
                wl_keyboard_send_modifiers(keyboard, serial, depressed, latched, locked, group);
            }
        }

        // --- selection ------------------------------------------------------------------------

        Offer* CreateOffer(wl_resource* device, const bool primary, const bool dnd)
        {
            auto* offer = new Offer();
            offer->server = this;
            offer->primary = primary;
            offer->dnd = dnd;
            const wl_interface* interface = &wl_data_offer_interface;
#if defined(CNA_WAYLAND_HAVE_PRIMARY_SELECTION)
            if (primary)
            {
                interface = &zwp_primary_selection_offer_v1_interface;
            }
#endif
            offer->resource = wl_resource_create(client, interface, wl_resource_get_version(device), 0);
            offers.push_back(offer);
            return offer;
        }

        void SendSelectionTo(const bool primary)
        {
            if (client == nullptr || keyboardFocus == nullptr)
            {
                return;
            }
            const std::optional<Payload>& external = primary ? externalPrimary : externalSelection;
            DataSource* source = primary ? clientPrimary : clientSelection;
            const bool echo = source != nullptr && options.echoSelection;
            std::vector<std::string> mimeTypes;
            if (external)
            {
                for (const auto& entry : *external)
                {
                    mimeTypes.push_back(entry.first);
                }
            }
            else if (echo)
            {
                mimeTypes = source->mimeTypes;
            }
            const bool none = !external && !echo;
            for (wl_resource* device : primary ? primaryDevices : dataDevices)
            {
                if (none)
                {
                    if (!primary)
                    {
                        wl_data_device_send_selection(device, nullptr);
                    }
#if defined(CNA_WAYLAND_HAVE_PRIMARY_SELECTION)
                    else
                    {
                        zwp_primary_selection_device_v1_send_selection(device, nullptr);
                    }
#endif
                    continue;
                }
                Offer* offer = CreateOffer(device, primary, false);
                if (external)
                {
                    offer->payload = *external;
                }
                else
                {
                    offer->source = source;
                }
                if (!primary)
                {
                    wl_resource_set_implementation(offer->resource, &kDataOfferImplementation, offer, &DestroyOffer);
                    wl_data_device_send_data_offer(device, offer->resource);
                    for (const std::string& mime : mimeTypes)
                    {
                        wl_data_offer_send_offer(offer->resource, mime.c_str());
                    }
                    wl_data_device_send_selection(device, offer->resource);
                }
#if defined(CNA_WAYLAND_HAVE_PRIMARY_SELECTION)
                else
                {
                    wl_resource_set_implementation(offer->resource, &kPrimaryOfferImplementation, offer, &DestroyOffer);
                    zwp_primary_selection_device_v1_send_data_offer(device, offer->resource);
                    for (const std::string& mime : mimeTypes)
                    {
                        zwp_primary_selection_offer_v1_send_offer(offer->resource, mime.c_str());
                    }
                    zwp_primary_selection_device_v1_send_selection(device, offer->resource);
                }
#endif
            }
        }

        void WritePayload(const Payload& payload, const std::string& mime, const int fd)
        {
            const auto found = payload.find(mime);
            std::vector<std::uint8_t> bytes = found != payload.end() ? found->second : std::vector<std::uint8_t>{};
            writers.emplace_back([fd, bytes = std::move(bytes)] {
                // A reader that closes early must end this thread's write, not the test process.
                sigset_t pipeOnly;
                sigemptyset(&pipeOnly);
                sigaddset(&pipeOnly, SIGPIPE);
                pthread_sigmask(SIG_BLOCK, &pipeOnly, nullptr);
                const int flags = ::fcntl(fd, F_GETFL);
                ::fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
                std::size_t offset = 0;
                while (offset < bytes.size())
                {
                    const ssize_t written = ::write(fd, bytes.data() + offset, bytes.size() - offset);
                    if (written < 0 && errno == EINTR)
                    {
                        continue;
                    }
                    if (written <= 0)
                    {
                        break;
                    }
                    offset += static_cast<std::size_t>(written);
                }
                ::close(fd);
            });
        }

        void CancelSource(DataSource*& slot)
        {
            if (slot == nullptr)
            {
                return;
            }
            if (!slot->primary)
            {
                wl_data_source_send_cancelled(slot->resource);
            }
#if defined(CNA_WAYLAND_HAVE_PRIMARY_SELECTION)
            else
            {
                zwp_primary_selection_source_v1_send_cancelled(slot->resource);
            }
#endif
            slot = nullptr;
        }

        // --- request implementations (static tables below) ----------------------------------

        static const struct wl_compositor_interface kCompositorImplementation;
        static const struct wl_region_interface kRegionImplementation;
        static const struct wl_surface_interface kSurfaceImplementation;
        static const struct wl_subcompositor_interface kSubcompositorImplementation;
        static const struct wl_subsurface_interface kSubsurfaceImplementation;
        static const struct xdg_wm_base_interface kWmBaseImplementation;
        static const struct xdg_positioner_interface kPositionerImplementation;
        static const struct xdg_surface_interface kXdgSurfaceImplementation;
        static const struct xdg_toplevel_interface kToplevelImplementation;
        static const struct wl_seat_interface kSeatImplementation;
        static const struct wl_pointer_interface kPointerImplementation;
        static const struct wl_keyboard_interface kKeyboardImplementation;
        static const struct wl_touch_interface kTouchImplementation;
        static const struct wl_output_interface kOutputImplementation;
        static const struct wl_data_device_manager_interface kDataDeviceManagerImplementation;
        static const struct wl_data_source_interface kDataSourceImplementation;
        static const struct wl_data_device_interface kDataDeviceImplementation;
        static const struct wl_data_offer_interface kDataOfferImplementation;
#if defined(CNA_WAYLAND_HAVE_PRIMARY_SELECTION)
        static const struct zwp_primary_selection_device_manager_v1_interface kPrimaryManagerImplementation;
        static const struct zwp_primary_selection_source_v1_interface kPrimarySourceImplementation;
        static const struct zwp_primary_selection_device_v1_interface kPrimaryDeviceImplementation;
        static const struct zwp_primary_selection_offer_v1_interface kPrimaryOfferImplementation;
#endif

        static void DestroyOffer(wl_resource* resource)
        {
            Offer* offer = Data<Offer>(resource);
            State* server = offer->server;
            std::erase(server->offers, offer);
            if (server->dragOffer == offer)
            {
                server->dragOffer = nullptr;
                server->dragInfo.alive = false;
            }
            delete offer;
        }

        void CommitSurface(Surface* surface);
    };

    int TestCompositor::State::Surface::SurfaceWidth() const
    {
        if (viewport != nullptr && viewport->destinationSet)
        {
            return viewport->dw;
        }
        return scale > 0 ? bufferWidth / scale : bufferWidth;
    }

    int TestCompositor::State::Surface::SurfaceHeight() const
    {
        if (viewport != nullptr && viewport->destinationSet)
        {
            return viewport->dh;
        }
        return scale > 0 ? bufferHeight / scale : bufferHeight;
    }

    using S = TestCompositor::State;

    // =============================================================================================
    // wl_compositor, wl_region, wl_surface
    // =============================================================================================

    namespace {

        void DestroyRegion(wl_resource* resource)
        {
            delete Data<S::Region>(resource);
        }

        void DestroySurfaceResource(wl_resource* resource)
        {
            S::Surface* surface = Data<S::Surface>(resource);
            S* server = surface->server;
            if (surface->xdg != nullptr)
            {
                server->Violation("wl_surface destroyed before its xdg_surface");
                surface->xdg->surface = nullptr;
            }
            if (surface->sub != nullptr)
            {
                surface->sub->surface = nullptr;
            }
            for (S::Subsurface* child : surface->children)
            {
                child->parent = nullptr;
            }
            if (surface->viewport != nullptr)
            {
                surface->viewport->surface = nullptr;
            }
            if (surface->fractional != nullptr)
            {
                // Outlives the surface when a client is torn down (resources go in id order).
                wl_resource_set_user_data(surface->fractional, nullptr);
            }
            for (S::Constraint* constraint : server->constraints)
            {
                if (constraint->surface == surface)
                {
                    constraint->surface = nullptr;
                }
            }
            std::vector<wl_resource*> frames;
            frames.swap(surface->pendingFrames);
            frames.insert(frames.end(), surface->frames.begin(), surface->frames.end());
            surface->frames.clear();
            for (wl_resource* frame : frames)
            {
                wl_resource_set_user_data(frame, nullptr);
                wl_resource_destroy(frame);
            }
            if (server->keyboardFocus == surface) { server->keyboardFocus = nullptr; }
            if (server->pointerFocus == surface) { server->pointerFocus = nullptr; }
            if (server->touchFocus == surface) { server->touchFocus = nullptr; }
            if (server->tabletFocus == surface) { server->tabletFocus = nullptr; }
            if (server->dragSurface == surface) { server->dragSurface = nullptr; }
            if (server->textInputFocus == surface) { server->textInputFocus = nullptr; }
            std::erase(server->surfaces, surface);
            delete surface;
        }

        void DestroyFrameCallback(wl_resource* resource)
        {
            auto* surface = static_cast<S::Surface*>(wl_resource_get_user_data(resource));
            if (surface != nullptr)
            {
                std::erase(surface->pendingFrames, resource);
                std::erase(surface->frames, resource);
            }
        }

    } // namespace

    const struct wl_region_interface S::kRegionImplementation = {
        .destroy = DestroyRequest,
        .add = [](wl_client*, wl_resource* resource, std::int32_t, std::int32_t, const std::int32_t width,
                  const std::int32_t height) {
            if (width > 0 && height > 0)
            {
                Data<Region>(resource)->nonEmpty = true;
            }
        },
        .subtract = [](wl_client*, wl_resource*, std::int32_t, std::int32_t, std::int32_t, std::int32_t) {},
    };

    const struct wl_surface_interface S::kSurfaceImplementation = {
        .destroy = DestroyRequest,
        .attach = [](wl_client*, wl_resource* resource, wl_resource* buffer, const std::int32_t x, const std::int32_t y) {
            Surface* surface = Data<Surface>(resource);
            if ((x != 0 || y != 0) && wl_resource_get_version(resource) >= WL_SURFACE_OFFSET_SINCE_VERSION)
            {
                surface->server->PostError(resource, WL_SURFACE_ERROR_INVALID_OFFSET,
                                           "attach with a non-zero offset on wl_surface v5 or later");
                return;
            }
            surface->pendingBuffer.Set(buffer);
            surface->pendingAttach = true;
        },
        .damage = [](wl_client*, wl_resource*, std::int32_t, std::int32_t, std::int32_t, std::int32_t) {},
        .frame = [](wl_client* client, wl_resource* resource, const std::uint32_t id) {
            Surface* surface = Data<Surface>(resource);
            wl_resource* callback = wl_resource_create(client, &wl_callback_interface, 1, id);
            wl_resource_set_implementation(callback, nullptr, surface, &DestroyFrameCallback);
            surface->pendingFrames.push_back(callback);
        },
        .set_opaque_region = [](wl_client*, wl_resource* resource, wl_resource* region) {
            Surface* surface = Data<Surface>(resource);
            surface->pendingOpaqueSet = true;
            surface->pendingOpaque = region != nullptr && Data<Region>(region)->nonEmpty;
        },
        .set_input_region = [](wl_client*, wl_resource*, wl_resource*) {},
        .commit = [](wl_client*, wl_resource* resource) {
            Surface* surface = Data<Surface>(resource);
            surface->server->CommitSurface(surface);
        },
        .set_buffer_transform = [](wl_client*, wl_resource* resource, const std::int32_t transform) {
            if (transform < 0 || transform > 7)
            {
                Data<Surface>(resource)->server->PostError(resource, WL_SURFACE_ERROR_INVALID_TRANSFORM, "bad transform");
            }
        },
        .set_buffer_scale = [](wl_client*, wl_resource* resource, const std::int32_t scale) {
            Surface* surface = Data<Surface>(resource);
            if (scale < 1)
            {
                surface->server->PostError(resource, WL_SURFACE_ERROR_INVALID_SCALE, "buffer scale below 1");
                return;
            }
            surface->pendingScale = scale;
            surface->pendingScaleSet = true;
        },
        .damage_buffer = [](wl_client*, wl_resource*, std::int32_t, std::int32_t, std::int32_t, std::int32_t) {},
        .offset = [](wl_client*, wl_resource*, std::int32_t, std::int32_t) {},
    };

    const struct wl_compositor_interface S::kCompositorImplementation = {
        .create_surface = [](wl_client* client, wl_resource* resource, const std::uint32_t id) {
            State* server = static_cast<State*>(wl_resource_get_user_data(resource));
            auto* surface = new Surface();
            surface->server = server;
            surface->resource = wl_resource_create(client, &wl_surface_interface, wl_resource_get_version(resource), id);
            surface->id = id;
            wl_resource_set_implementation(surface->resource, &kSurfaceImplementation, surface, &DestroySurfaceResource);
            server->surfaces.push_back(surface);
        },
        .create_region = [](wl_client* client, wl_resource* resource, const std::uint32_t id) {
            wl_resource* region = wl_resource_create(client, &wl_region_interface, wl_resource_get_version(resource), id);
            wl_resource_set_implementation(region, &kRegionImplementation, new Region(), &DestroyRegion);
        },
    };

    void S::CommitSurface(Surface* surface)
    {
        ++surface->commits;
        XdgSurface* xdg = surface->xdg;

        const bool attaching = surface->pendingAttach;
        wl_resource* newBuffer = attaching ? surface->pendingBuffer.buffer : surface->buffer.buffer;

        if (xdg != nullptr && xdg->toplevel != nullptr && attaching && newBuffer != nullptr && !xdg->everAcked)
        {
            PostError(xdg->resource, XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER,
                      "a buffer was committed before the first configure was acknowledged");
            return;
        }
        if (xdg != nullptr && xdg->toplevel == nullptr && attaching && newBuffer != nullptr)
        {
            PostError(xdg->resource, XDG_SURFACE_ERROR_NOT_CONSTRUCTED, "a buffer was committed to an xdg_surface without a role");
            return;
        }

        const int scale = surface->pendingScaleSet ? surface->pendingScale : surface->scale;
        if (attaching)
        {
            int width = 0;
            int height = 0;
            std::uint32_t centre = 0;
            if (newBuffer != nullptr)
            {
                if (wl_shm_buffer* shm = wl_shm_buffer_get(newBuffer))
                {
                    width = wl_shm_buffer_get_width(shm);
                    height = wl_shm_buffer_get_height(shm);
                    wl_shm_buffer_begin_access(shm);
                    const auto* bytes = static_cast<const std::uint8_t*>(wl_shm_buffer_get_data(shm));
                    const int stride = wl_shm_buffer_get_stride(shm);
                    if (bytes != nullptr && width > 0 && height > 0)
                    {
                        std::memcpy(&centre, bytes + static_cast<std::size_t>(height / 2) * stride + (width / 2) * 4, 4);
                    }
                    wl_shm_buffer_end_access(shm);
                }
                else
                {
                    Violation("a non-shm buffer was committed; the test compositor cannot size it");
                }
            }
            if (newBuffer != nullptr && (width % scale != 0 || height % scale != 0))
            {
                PostError(surface->resource, WL_SURFACE_ERROR_INVALID_SIZE,
                          "buffer " + std::to_string(width) + "x" + std::to_string(height) +
                              " is not a multiple of buffer scale " + std::to_string(scale));
                return;
            }
            wl_resource* old = surface->buffer.buffer;
            if (old != nullptr && old != newBuffer)
            {
                if (options.releaseBuffers)
                {
                    wl_buffer_send_release(old);
                }
                else
                {
                    held.emplace_back();
                    held.back().Set(old);
                }
            }
            surface->buffer.Set(newBuffer);
            surface->bufferWidth = width;
            surface->bufferHeight = height;
            surface->centrePixel = centre;
            if (newBuffer != nullptr)
            {
                ++surface->bufferCommits;
            }
            surface->pendingBuffer.Set(nullptr);
            surface->pendingAttach = false;
        }
        surface->scale = scale;
        surface->pendingScaleSet = false;
        if (surface->pendingOpaqueSet)
        {
            surface->opaque = surface->pendingOpaque;
            surface->pendingOpaqueSet = false;
        }

        if (Viewport* viewport = surface->viewport)
        {
            if (viewport->pendingSourceSet)
            {
                viewport->sourceSet = viewport->psw >= 0;
                viewport->sx = viewport->psx;
                viewport->sy = viewport->psy;
                viewport->sw = viewport->psw;
                viewport->sh = viewport->psh;
                viewport->pendingSourceSet = false;
            }
            if (viewport->pendingDestinationSet)
            {
                viewport->destinationSet = viewport->pdw > 0;
                viewport->dw = viewport->pdw;
                viewport->dh = viewport->pdh;
                viewport->pendingDestinationSet = false;
            }
            if (viewport->sourceSet && surface->buffer.buffer != nullptr)
            {
                const double bufferW = static_cast<double>(surface->bufferWidth) / scale;
                const double bufferH = static_cast<double>(surface->bufferHeight) / scale;
                if (viewport->sx + viewport->sw > bufferW + 1e-6 || viewport->sy + viewport->sh > bufferH + 1e-6)
                {
                    PostError(viewport->resource, WP_VIEWPORT_ERROR_OUT_OF_BUFFER, "the source rectangle leaves the buffer");
                    return;
                }
            }
            if (viewport->sourceSet && !viewport->destinationSet &&
                (viewport->sw != std::floor(viewport->sw) || viewport->sh != std::floor(viewport->sh)))
            {
                PostError(viewport->resource, WP_VIEWPORT_ERROR_BAD_SIZE, "a fractional source without a destination");
                return;
            }
        }

        for (Subsurface* child : surface->children)
        {
            child->x = child->pendingX;
            child->y = child->pendingY;
        }

        for (Constraint* constraint : constraints)
        {
            if (constraint->surface == surface && constraint->pendingHint)
            {
                constraint->hasHint = true;
                constraint->hx = constraint->phx;
                constraint->hy = constraint->phy;
                constraint->pendingHint = false;
            }
        }

        if (xdg != nullptr)
        {
            if (xdg->pendingGeometrySet)
            {
                xdg->geometrySet = true;
                xdg->gx = xdg->px;
                xdg->gy = xdg->py;
                xdg->gw = xdg->pw;
                xdg->gh = xdg->ph;
                xdg->pendingGeometrySet = false;
            }
            if (Toplevel* toplevel = xdg->toplevel)
            {
                toplevel->minW = toplevel->pendingMinW;
                toplevel->minH = toplevel->pendingMinH;
                toplevel->maxW = toplevel->pendingMaxW;
                toplevel->maxH = toplevel->pendingMaxH;
                if (toplevel->minW > 0 && toplevel->maxW > 0 && toplevel->minW > toplevel->maxW)
                {
                    PostError(toplevel->resource, XDG_TOPLEVEL_ERROR_INVALID_SIZE, "min width above max width");
                    return;
                }
                if (toplevel->minH > 0 && toplevel->maxH > 0 && toplevel->minH > toplevel->maxH)
                {
                    PostError(toplevel->resource, XDG_TOPLEVEL_ERROR_INVALID_SIZE, "min height above max height");
                    return;
                }
                toplevel->mapped = xdg->everAcked && surface->buffer.buffer != nullptr;

                if (!xdg->initialCommitSeen)
                {
                    xdg->initialCommitSeen = true;
                    if (options.configureOnInitialCommit)
                    {
                        const int version = wl_resource_get_version(toplevel->resource);
                        if (version >= XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION)
                        {
                            int width = 0;
                            int height = 0;
                            OutputLogical(PrimaryOutput(), width, height);
                            xdg_toplevel_send_configure_bounds(toplevel->resource, width, height);
                        }
                        if (version >= XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION)
                        {
                            wl_array capabilities;
                            wl_array_init(&capabilities);
                            for (const std::uint32_t capability :
                                 {XDG_TOPLEVEL_WM_CAPABILITIES_WINDOW_MENU, XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE,
                                  XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN, XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE})
                            {
                                *static_cast<std::uint32_t*>(wl_array_add(&capabilities, sizeof(std::uint32_t))) = capability;
                            }
                            xdg_toplevel_send_wm_capabilities(toplevel->resource, &capabilities);
                            wl_array_release(&capabilities);
                        }
                        int width = options.initialWidth;
                        int height = options.initialHeight;
                        if (toplevel->maximized || toplevel->fullscreen)
                        {
                            OutputLogical(PrimaryOutput(), width, height);
                        }
                        SendConfigure(toplevel, width, height, StatesOf(toplevel));
                    }
                }
                else if (toplevel->mapped && options.strictConstrainedGeometry && (xdg->acked.maximized || xdg->acked.fullscreen) &&
                         xdg->acked.width > 0 && xdg->acked.height > 0)
                {
                    const int width = xdg->geometrySet ? xdg->gw : surface->SurfaceWidth();
                    const int height = xdg->geometrySet ? xdg->gh : surface->SurfaceHeight();
                    const bool bad = xdg->acked.maximized ? (width != xdg->acked.width || height != xdg->acked.height)
                                                          : (width > xdg->acked.width || height > xdg->acked.height);
                    if (bad)
                    {
                        PostError(xdg->wmBase, XDG_WM_BASE_ERROR_INVALID_SURFACE_STATE,
                                  "window geometry " + std::to_string(width) + "x" + std::to_string(height) +
                                      " does not match the " + (xdg->acked.maximized ? "maximized" : "fullscreen") +
                                      " configure " + std::to_string(xdg->acked.width) + "x" +
                                      std::to_string(xdg->acked.height));
                        return;
                    }
                }
            }
        }

        // Frame callbacks: the pending ones become this commit's.
        for (wl_resource* frame : surface->pendingFrames)
        {
            surface->frames.push_back(frame);
        }
        surface->pendingFrames.clear();
        if (options.autoFrameDone)
        {
            const std::uint32_t now = NowMs();
            std::vector<wl_resource*> frames;
            frames.swap(surface->frames);
            for (wl_resource* frame : frames)
            {
                wl_resource_set_user_data(frame, nullptr);
                wl_callback_send_done(frame, now);
                wl_resource_destroy(frame);
            }
        }
    }

    // =============================================================================================
    // wl_subcompositor
    // =============================================================================================

    namespace {

        void DestroySubsurface(wl_resource* resource)
        {
            S::Subsurface* sub = Data<S::Subsurface>(resource);
            S* server = sub->server;
            if (sub->surface != nullptr)
            {
                sub->surface->sub = nullptr;
            }
            if (sub->parent != nullptr)
            {
                std::erase(sub->parent->children, sub);
            }
            std::erase(server->subsurfaces, sub);
            delete sub;
        }

    } // namespace

    const struct wl_subsurface_interface S::kSubsurfaceImplementation = {
        .destroy = DestroyRequest,
        .set_position = [](wl_client*, wl_resource* resource, const std::int32_t x, const std::int32_t y) {
            Subsurface* sub = Data<Subsurface>(resource);
            sub->pendingX = x;
            sub->pendingY = y;
        },
        .place_above = [](wl_client*, wl_resource*, wl_resource*) {},
        .place_below = [](wl_client*, wl_resource*, wl_resource*) {},
        .set_sync = [](wl_client*, wl_resource* resource) { Data<Subsurface>(resource)->desync = false; },
        .set_desync = [](wl_client*, wl_resource* resource) { Data<Subsurface>(resource)->desync = true; },
    };

    const struct wl_subcompositor_interface S::kSubcompositorImplementation = {
        .destroy = DestroyRequest,
        .get_subsurface = [](wl_client* client, wl_resource* resource, const std::uint32_t id, wl_resource* surfaceResource,
                             wl_resource* parentResource) {
            State* server = static_cast<State*>(wl_resource_get_user_data(resource));
            Surface* surface = Data<Surface>(surfaceResource);
            Surface* parent = Data<Surface>(parentResource);
            if (surface == parent)
            {
                server->PostError(resource, WL_SUBCOMPOSITOR_ERROR_BAD_PARENT, "a surface cannot be its own parent");
                return;
            }
            if (surface->role != Surface::Role::None && surface->role != Surface::Role::Subsurface)
            {
                server->PostError(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE, "the surface already has another role");
                return;
            }
            if (surface->sub != nullptr)
            {
                server->PostError(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE, "the surface is already a subsurface");
                return;
            }
            auto* sub = new Subsurface();
            sub->server = server;
            sub->surface = surface;
            sub->parent = parent;
            sub->resource = wl_resource_create(client, &wl_subsurface_interface, wl_resource_get_version(resource), id);
            wl_resource_set_implementation(sub->resource, &kSubsurfaceImplementation, sub, &DestroySubsurface);
            surface->role = Surface::Role::Subsurface;
            surface->sub = sub;
            parent->children.push_back(sub);
            server->subsurfaces.push_back(sub);
        },
    };

    // =============================================================================================
    // xdg-shell
    // =============================================================================================

    namespace {

        void DestroyXdgSurface(wl_resource* resource)
        {
            S::XdgSurface* xdg = Data<S::XdgSurface>(resource);
            S* server = xdg->server;
            if (xdg->toplevel != nullptr)
            {
                server->PostError(resource, XDG_SURFACE_ERROR_DEFUNCT_ROLE_OBJECT,
                                  "xdg_surface destroyed before its xdg_toplevel");
                xdg->toplevel->xdg = nullptr;
            }
            if (xdg->surface != nullptr)
            {
                xdg->surface->xdg = nullptr;
            }
            std::erase(server->xdgSurfaces, xdg);
            delete xdg;
        }

        void DestroyToplevel(wl_resource* resource)
        {
            S::Toplevel* toplevel = Data<S::Toplevel>(resource);
            S* server = toplevel->server;
            if (toplevel->decoration != nullptr)
            {
                server->PostError(toplevel->decoration->resource, 2 /* orphaned */,
                                  "xdg_toplevel destroyed before its decoration object");
                toplevel->decoration->toplevel = nullptr;
            }
            if (toplevel->xdg != nullptr)
            {
                toplevel->xdg->toplevel = nullptr;
                // The role object is gone: the surface is unmapped, and a new xdg_surface on it
                // starts from nothing -- a new initial commit, a new first configure.
                toplevel->xdg->everAcked = false;
                toplevel->xdg->initialCommitSeen = false;
                toplevel->xdg->sent.clear();
            }
            std::erase(server->toplevels, toplevel);
            delete toplevel;
        }

        void DestroyPositioner(wl_resource*) {}

    } // namespace

    const struct xdg_positioner_interface S::kPositionerImplementation = {
        .destroy = DestroyRequest,
        .set_size = [](wl_client*, wl_resource*, std::int32_t, std::int32_t) {},
        .set_anchor_rect = [](wl_client*, wl_resource*, std::int32_t, std::int32_t, std::int32_t, std::int32_t) {},
        .set_anchor = [](wl_client*, wl_resource*, std::uint32_t) {},
        .set_gravity = [](wl_client*, wl_resource*, std::uint32_t) {},
        .set_constraint_adjustment = [](wl_client*, wl_resource*, std::uint32_t) {},
        .set_offset = [](wl_client*, wl_resource*, std::int32_t, std::int32_t) {},
        .set_reactive = [](wl_client*, wl_resource*) {},
        .set_parent_size = [](wl_client*, wl_resource*, std::int32_t, std::int32_t) {},
        .set_parent_configure = [](wl_client*, wl_resource*, std::uint32_t) {},
    };

    const struct xdg_toplevel_interface S::kToplevelImplementation = {
        .destroy = DestroyRequest,
        .set_parent = [](wl_client*, wl_resource*, wl_resource*) {},
        .set_title = [](wl_client*, wl_resource* resource, const char* title) {
            Data<Toplevel>(resource)->title = title != nullptr ? title : "";
        },
        .set_app_id = [](wl_client*, wl_resource* resource, const char* appId) {
            Data<Toplevel>(resource)->appId = appId != nullptr ? appId : "";
        },
        .show_window_menu = [](wl_client*, wl_resource* resource, wl_resource*, const std::uint32_t serial, std::int32_t,
                               std::int32_t) {
            Toplevel* toplevel = Data<Toplevel>(resource);
            toplevel->server->CheckGrabSerial(serial, "xdg_toplevel.show_window_menu");
            ++toplevel->windowMenuRequests;
        },
        .move = [](wl_client*, wl_resource* resource, wl_resource*, const std::uint32_t serial) {
            Toplevel* toplevel = Data<Toplevel>(resource);
            toplevel->server->CheckGrabSerial(serial, "xdg_toplevel.move");
            ++toplevel->moveRequests;
        },
        .resize = [](wl_client*, wl_resource* resource, wl_resource*, const std::uint32_t serial, const std::uint32_t edges) {
            Toplevel* toplevel = Data<Toplevel>(resource);
            const bool valid = edges == 1 || edges == 2 || edges == 4 || edges == 5 || edges == 6 || edges == 8 ||
                               edges == 9 || edges == 10;
            if (!valid)
            {
                toplevel->server->PostError(resource, XDG_TOPLEVEL_ERROR_INVALID_RESIZE_EDGE,
                                            "resize edge " + std::to_string(edges));
                return;
            }
            toplevel->server->CheckGrabSerial(serial, "xdg_toplevel.resize");
            ++toplevel->resizeRequests;
            toplevel->lastResizeEdges = edges;
        },
        .set_max_size = [](wl_client*, wl_resource* resource, const std::int32_t width, const std::int32_t height) {
            Toplevel* toplevel = Data<Toplevel>(resource);
            if (width < 0 || height < 0)
            {
                toplevel->server->PostError(resource, XDG_TOPLEVEL_ERROR_INVALID_SIZE, "negative max size");
                return;
            }
            toplevel->pendingMaxW = width;
            toplevel->pendingMaxH = height;
        },
        .set_min_size = [](wl_client*, wl_resource* resource, const std::int32_t width, const std::int32_t height) {
            Toplevel* toplevel = Data<Toplevel>(resource);
            if (width < 0 || height < 0)
            {
                toplevel->server->PostError(resource, XDG_TOPLEVEL_ERROR_INVALID_SIZE, "negative min size");
                return;
            }
            toplevel->pendingMinW = width;
            toplevel->pendingMinH = height;
        },
        .set_maximized = [](wl_client*, wl_resource* resource) {
            Toplevel* toplevel = Data<Toplevel>(resource);
            toplevel->maximizeRequested = true;
            if (toplevel->server->options.answerStateRequests && toplevel->xdg != nullptr && toplevel->xdg->initialCommitSeen)
            {
                toplevel->maximized = true;
                toplevel->server->Reconfigure(toplevel);
            }
            else
            {
                toplevel->maximized = toplevel->server->options.answerStateRequests;
            }
        },
        .unset_maximized = [](wl_client*, wl_resource* resource) {
            Toplevel* toplevel = Data<Toplevel>(resource);
            toplevel->maximizeRequested = false;
            if (toplevel->server->options.answerStateRequests && toplevel->xdg != nullptr && toplevel->xdg->initialCommitSeen)
            {
                toplevel->maximized = false;
                toplevel->server->Reconfigure(toplevel);
            }
        },
        .set_fullscreen = [](wl_client*, wl_resource* resource, wl_resource*) {
            Toplevel* toplevel = Data<Toplevel>(resource);
            toplevel->fullscreenRequested = true;
            if (toplevel->server->options.answerStateRequests && toplevel->xdg != nullptr && toplevel->xdg->initialCommitSeen)
            {
                toplevel->fullscreen = true;
                toplevel->server->Reconfigure(toplevel);
            }
            else
            {
                toplevel->fullscreen = toplevel->server->options.answerStateRequests;
            }
        },
        .unset_fullscreen = [](wl_client*, wl_resource* resource) {
            Toplevel* toplevel = Data<Toplevel>(resource);
            toplevel->fullscreenRequested = false;
            if (toplevel->server->options.answerStateRequests && toplevel->xdg != nullptr && toplevel->xdg->initialCommitSeen)
            {
                toplevel->fullscreen = false;
                toplevel->server->Reconfigure(toplevel);
            }
        },
        .set_minimized = [](wl_client*, wl_resource* resource) { ++Data<Toplevel>(resource)->minimizeRequests; },
    };

    const struct xdg_surface_interface S::kXdgSurfaceImplementation = {
        .destroy = DestroyRequest,
        .get_toplevel = [](wl_client* client, wl_resource* resource, const std::uint32_t id) {
            XdgSurface* xdg = Data<XdgSurface>(resource);
            State* server = xdg->server;
            if (xdg->toplevel != nullptr)
            {
                server->PostError(resource, XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED, "the xdg_surface already has a role");
                return;
            }
            auto* toplevel = new Toplevel();
            toplevel->server = server;
            toplevel->xdg = xdg;
            toplevel->resource = wl_resource_create(client, &xdg_toplevel_interface, wl_resource_get_version(resource), id);
            wl_resource_set_implementation(toplevel->resource, &kToplevelImplementation, toplevel, &DestroyToplevel);
            xdg->toplevel = toplevel;
            if (xdg->surface != nullptr)
            {
                xdg->surface->role = Surface::Role::Toplevel;
            }
            server->toplevels.push_back(toplevel);
        },
        .get_popup = [](wl_client* client, wl_resource*, std::uint32_t, wl_resource*, wl_resource*) {
            wl_client_post_implementation_error(client, "the test compositor has no popups");
        },
        .set_window_geometry = [](wl_client*, wl_resource* resource, const std::int32_t x, const std::int32_t y,
                                  const std::int32_t width, const std::int32_t height) {
            XdgSurface* xdg = Data<XdgSurface>(resource);
            if (width <= 0 || height <= 0)
            {
                xdg->server->PostError(resource, XDG_SURFACE_ERROR_INVALID_SIZE,
                                       "window geometry " + std::to_string(width) + "x" + std::to_string(height));
                return;
            }
            xdg->pendingGeometrySet = true;
            xdg->px = x;
            xdg->py = y;
            xdg->pw = width;
            xdg->ph = height;
        },
        .ack_configure = [](wl_client*, wl_resource* resource, const std::uint32_t serial) {
            XdgSurface* xdg = Data<XdgSurface>(resource);
            const auto found = std::find_if(xdg->sent.begin(), xdg->sent.end(),
                                            [serial](const XdgSurface::Sent& sent) { return sent.serial == serial; });
            if (found == xdg->sent.end())
            {
                xdg->server->PostError(resource, XDG_SURFACE_ERROR_INVALID_SERIAL,
                                       "ack_configure of serial " + std::to_string(serial) + ", which was never sent or was already superseded");
                return;
            }
            xdg->acked = *found;
            xdg->everAcked = true;
            xdg->sent.erase(xdg->sent.begin(), found + 1);
            if (xdg->toplevel != nullptr)
            {
                ++xdg->toplevel->acks;
                xdg->toplevel->lastAckedSerial = serial;
            }
        },
    };

    const struct xdg_wm_base_interface S::kWmBaseImplementation = {
        .destroy = [](wl_client*, wl_resource* resource) {
            State* server = static_cast<State*>(wl_resource_get_user_data(resource));
            for (XdgSurface* xdg : server->xdgSurfaces)
            {
                if (xdg->wmBase == resource)
                {
                    server->PostError(resource, XDG_WM_BASE_ERROR_DEFUNCT_SURFACES,
                                      "xdg_wm_base destroyed while an xdg_surface made from it exists");
                    return;
                }
            }
            wl_resource_destroy(resource);
        },
        .create_positioner = [](wl_client* client, wl_resource* resource, const std::uint32_t id) {
            wl_resource* positioner = wl_resource_create(client, &xdg_positioner_interface, wl_resource_get_version(resource), id);
            wl_resource_set_implementation(positioner, &kPositionerImplementation, nullptr, &DestroyPositioner);
        },
        .get_xdg_surface = [](wl_client* client, wl_resource* resource, const std::uint32_t id, wl_resource* surfaceResource) {
            State* server = static_cast<State*>(wl_resource_get_user_data(resource));
            Surface* surface = Data<Surface>(surfaceResource);
            auto* xdg = new XdgSurface();
            xdg->server = server;
            xdg->wmBase = resource;
            xdg->surface = surface;
            xdg->resource = wl_resource_create(client, &xdg_surface_interface, wl_resource_get_version(resource), id);
            wl_resource_set_implementation(xdg->resource, &kXdgSurfaceImplementation, xdg, &DestroyXdgSurface);
            server->xdgSurfaces.push_back(xdg);
            if (surface->role != Surface::Role::None && surface->role != Surface::Role::Toplevel)
            {
                xdg->surface = nullptr;
                server->PostError(resource, XDG_WM_BASE_ERROR_ROLE, "the surface already has another role");
                return;
            }
            if (surface->xdg != nullptr)
            {
                xdg->surface = nullptr;
                server->PostError(resource, XDG_WM_BASE_ERROR_ROLE, "the surface already has an xdg_surface");
                return;
            }
            if (surface->buffer.buffer != nullptr || (surface->pendingAttach && surface->pendingBuffer.buffer != nullptr))
            {
                surface->xdg = xdg;
                server->PostError(xdg->resource, XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER,
                                  "an xdg_surface was created from a surface with a buffer");
                return;
            }
            surface->xdg = xdg;
        },
        .pong = [](wl_client*, wl_resource* resource, std::uint32_t) {
            ++static_cast<State*>(wl_resource_get_user_data(resource))->pongs;
        },
    };

    // =============================================================================================
    // wl_seat and its devices
    // =============================================================================================

    namespace {

        void DestroyInList(wl_resource* resource, std::vector<wl_resource*> S::*list)
        {
            S* server = static_cast<S*>(wl_resource_get_user_data(resource));
            if (server != nullptr)
            {
                std::erase(server->*list, resource);
            }
        }

    } // namespace

    const struct wl_pointer_interface S::kPointerImplementation = {
        .set_cursor = [](wl_client*, wl_resource* resource, const std::uint32_t serial, wl_resource* surfaceResource,
                         const std::int32_t hotspotX, const std::int32_t hotspotY) {
            State* server = static_cast<State*>(wl_resource_get_user_data(resource));
            if (serial != server->pointerEnterSerial)
            {
                server->Violation("wl_pointer.set_cursor with serial " + std::to_string(serial) +
                                  ", not the current enter serial " + std::to_string(server->pointerEnterSerial));
            }
            server->pointerInfo.cursorSet = true;
            server->pointerInfo.hotspotX = hotspotX;
            server->pointerInfo.hotspotY = hotspotY;
            if (surfaceResource == nullptr)
            {
                server->pointerInfo.cursorSurface = 0;
                return;
            }
            Surface* surface = Data<Surface>(surfaceResource);
            if (surface->role != Surface::Role::None && surface->role != Surface::Role::Cursor)
            {
                server->PostError(resource, WL_POINTER_ERROR_ROLE, "the cursor surface has another role");
                return;
            }
            surface->role = Surface::Role::Cursor;
            server->pointerInfo.cursorSurface = surface->id;
        },
        .release = DestroyRequest,
    };

    const struct wl_keyboard_interface S::kKeyboardImplementation = {.release = DestroyRequest};
    const struct wl_touch_interface S::kTouchImplementation = {.release = DestroyRequest};

    const struct wl_seat_interface S::kSeatImplementation = {
        .get_pointer = [](wl_client* client, wl_resource* resource, const std::uint32_t id) {
            State* server = static_cast<State*>(wl_resource_get_user_data(resource));
            wl_resource* pointer = wl_resource_create(client, &wl_pointer_interface, wl_resource_get_version(resource), id);
            wl_resource_set_implementation(pointer, &kPointerImplementation, server,
                                           [](wl_resource* r) { DestroyInList(r, &State::pointers); });
            server->pointers.push_back(pointer);
            if (!server->options.pointer)
            {
                server->Violation("wl_seat.get_pointer on a seat without the pointer capability");
            }
        },
        .get_keyboard = [](wl_client* client, wl_resource* resource, const std::uint32_t id) {
            State* server = static_cast<State*>(wl_resource_get_user_data(resource));
            wl_resource* keyboard = wl_resource_create(client, &wl_keyboard_interface, wl_resource_get_version(resource), id);
            wl_resource_set_implementation(keyboard, &kKeyboardImplementation, server,
                                           [](wl_resource* r) { DestroyInList(r, &State::keyboards); });
            server->keyboards.push_back(keyboard);
            server->SendKeymap(keyboard);
            if (wl_resource_get_version(keyboard) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION)
            {
                wl_keyboard_send_repeat_info(keyboard, server->options.repeatRate, server->options.repeatDelay);
            }
        },
        .get_touch = [](wl_client* client, wl_resource* resource, const std::uint32_t id) {
            State* server = static_cast<State*>(wl_resource_get_user_data(resource));
            wl_resource* touch = wl_resource_create(client, &wl_touch_interface, wl_resource_get_version(resource), id);
            wl_resource_set_implementation(touch, &kTouchImplementation, server,
                                           [](wl_resource* r) { DestroyInList(r, &State::touches); });
            server->touches.push_back(touch);
        },
        .release = DestroyRequest,
    };

    // =============================================================================================
    // wl_output
    // =============================================================================================

    namespace {

        void SendOutputState(S::Output* output, wl_resource* resource)
        {
            const OutputSpec& spec = output->spec;
            wl_output_send_geometry(resource, spec.x, spec.y, spec.physicalWidth, spec.physicalHeight,
                                    WL_OUTPUT_SUBPIXEL_UNKNOWN, "CNA", "Test Output", WL_OUTPUT_TRANSFORM_NORMAL);
            wl_output_send_mode(resource, WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED, spec.width, spec.height, spec.refresh);
            const int version = wl_resource_get_version(resource);
            if (version >= WL_OUTPUT_SCALE_SINCE_VERSION)
            {
                wl_output_send_scale(resource, spec.scale);
            }
            if (version >= WL_OUTPUT_NAME_SINCE_VERSION)
            {
                wl_output_send_name(resource, spec.name.c_str());
                wl_output_send_description(resource, spec.description.c_str());
            }
            if (version >= WL_OUTPUT_DONE_SINCE_VERSION)
            {
                wl_output_send_done(resource);
            }
        }

        void DestroyOutputResource(wl_resource* resource)
        {
            auto* output = static_cast<S::Output*>(wl_resource_get_user_data(resource));
            if (output != nullptr)
            {
                std::erase(output->resources, resource);
            }
        }

    } // namespace

    const struct wl_output_interface S::kOutputImplementation = {.release = DestroyRequest};

    // =============================================================================================
    // wl_data_device_manager
    // =============================================================================================

    namespace {

        void DestroyDataSource(wl_resource* resource)
        {
            S::DataSource* source = Data<S::DataSource>(resource);
            S* server = source->server;
            if (server->clientSelection == source)
            {
                server->clientSelection = nullptr;
                server->selectionInfo.owned = false;
            }
            if (server->clientPrimary == source)
            {
                server->clientPrimary = nullptr;
                server->primaryInfo.owned = false;
            }
            for (S::Offer* offer : server->offers)
            {
                if (offer->source == source)
                {
                    offer->source = nullptr;
                }
            }
            delete source;
        }

        void Receive(S::Offer* offer, const char* mime, const std::int32_t fd)
        {
            S* server = offer->server;
            if (offer->dnd)
            {
                server->dragInfo.received.emplace_back(mime != nullptr ? mime : "");
            }
            if (offer->source != nullptr)
            {
                if (!offer->source->primary)
                {
                    wl_data_source_send_send(offer->source->resource, mime, fd);
                }
#if defined(CNA_WAYLAND_HAVE_PRIMARY_SELECTION)
                else
                {
                    zwp_primary_selection_source_v1_send_send(offer->source->resource, mime, fd);
                }
#endif
                ::close(fd);
                return;
            }
            server->WritePayload(offer->payload, mime != nullptr ? mime : "", fd);
        }

    } // namespace

    const struct wl_data_offer_interface S::kDataOfferImplementation = {
        .accept = [](wl_client*, wl_resource* resource, std::uint32_t, const char* mime) {
            Offer* offer = Data<Offer>(resource);
            if (offer->dnd)
            {
                offer->server->dragInfo.acceptCalled = true;
                offer->server->dragInfo.accepted = mime != nullptr ? mime : "";
            }
        },
        .receive = [](wl_client*, wl_resource* resource, const char* mime, const std::int32_t fd) {
            Receive(Data<Offer>(resource), mime, fd);
        },
        .destroy = DestroyRequest,
        .finish = [](wl_client*, wl_resource* resource) {
            Offer* offer = Data<Offer>(resource);
            if (!offer->dnd || !offer->dropped)
            {
                offer->server->PostError(resource, WL_DATA_OFFER_ERROR_INVALID_FINISH, "finish before a drop");
                return;
            }
            offer->server->dragInfo.finished = true;
        },
        .set_actions = [](wl_client*, wl_resource* resource, const std::uint32_t actions, const std::uint32_t preferred) {
            Offer* offer = Data<Offer>(resource);
            State* server = offer->server;
            if ((actions & ~7u) != 0)
            {
                server->PostError(resource, WL_DATA_OFFER_ERROR_INVALID_ACTION_MASK, "unknown actions");
                return;
            }
            if (preferred != 0 && (preferred & (preferred - 1)) != 0)
            {
                server->PostError(resource, WL_DATA_OFFER_ERROR_INVALID_ACTION, "more than one preferred action");
                return;
            }
            server->dragInfo.actions = actions;
            server->dragInfo.preferred = preferred;
            const std::uint32_t common = actions & server->dragSourceActions;
            std::uint32_t chosen = 0;
            if ((preferred & common) != 0)
            {
                chosen = preferred;
            }
            else if (common != 0)
            {
                chosen = common & (~common + 1);
            }
            if (wl_resource_get_version(resource) >= WL_DATA_OFFER_ACTION_SINCE_VERSION)
            {
                wl_data_offer_send_action(resource, chosen);
            }
        },
    };

    const struct wl_data_source_interface S::kDataSourceImplementation = {
        .offer = [](wl_client*, wl_resource* resource, const char* mime) {
            Data<DataSource>(resource)->mimeTypes.emplace_back(mime != nullptr ? mime : "");
        },
        .destroy = DestroyRequest,
        .set_actions = [](wl_client*, wl_resource* resource, const std::uint32_t actions) {
            Data<DataSource>(resource)->actions = actions;
        },
    };

    const struct wl_data_device_interface S::kDataDeviceImplementation = {
        .start_drag = [](wl_client* client, wl_resource*, wl_resource*, wl_resource*, wl_resource*, std::uint32_t) {
            wl_client_post_implementation_error(client, "the test compositor does not accept drags from the client");
        },
        .set_selection = [](wl_client*, wl_resource* resource, wl_resource* sourceResource, const std::uint32_t serial) {
            State* server = static_cast<State*>(wl_resource_get_user_data(resource));
            server->CheckInputSerial(serial, "wl_data_device.set_selection");
            ++server->selectionInfo.setRequests;
            DataSource* source = sourceResource != nullptr ? Data<DataSource>(sourceResource) : nullptr;
            if (server->clientSelection != nullptr && server->clientSelection != source)
            {
                server->CancelSource(server->clientSelection);
            }
            server->externalSelection.reset();
            server->clientSelection = source;
            server->selectionInfo.owned = source != nullptr;
            server->selectionInfo.mimeTypes = source != nullptr ? source->mimeTypes : std::vector<std::string>{};
            server->SendSelectionTo(false);
        },
        .release = DestroyRequest,
    };

    const struct wl_data_device_manager_interface S::kDataDeviceManagerImplementation = {
        .create_data_source = [](wl_client* client, wl_resource* resource, const std::uint32_t id) {
            State* server = static_cast<State*>(wl_resource_get_user_data(resource));
            auto* source = new DataSource();
            source->server = server;
            source->resource = wl_resource_create(client, &wl_data_source_interface, wl_resource_get_version(resource), id);
            wl_resource_set_implementation(source->resource, &kDataSourceImplementation, source, &DestroyDataSource);
        },
        .get_data_device = [](wl_client* client, wl_resource* resource, const std::uint32_t id, wl_resource*) {
            State* server = static_cast<State*>(wl_resource_get_user_data(resource));
            wl_resource* device = wl_resource_create(client, &wl_data_device_interface, wl_resource_get_version(resource), id);
            wl_resource_set_implementation(device, &kDataDeviceImplementation, server,
                                           [](wl_resource* r) { DestroyInList(r, &State::dataDevices); });
            server->dataDevices.push_back(device);
        },
    };

#if defined(CNA_WAYLAND_HAVE_PRIMARY_SELECTION)
    const struct zwp_primary_selection_offer_v1_interface S::kPrimaryOfferImplementation = {
        .receive = [](wl_client*, wl_resource* resource, const char* mime, const std::int32_t fd) {
            Receive(Data<Offer>(resource), mime, fd);
        },
        .destroy = DestroyRequest,
    };

    const struct zwp_primary_selection_source_v1_interface S::kPrimarySourceImplementation = {
        .offer = [](wl_client*, wl_resource* resource, const char* mime) {
            Data<DataSource>(resource)->mimeTypes.emplace_back(mime != nullptr ? mime : "");
        },
        .destroy = DestroyRequest,
    };

    const struct zwp_primary_selection_device_v1_interface S::kPrimaryDeviceImplementation = {
        .set_selection = [](wl_client*, wl_resource* resource, wl_resource* sourceResource, const std::uint32_t serial) {
            State* server = static_cast<State*>(wl_resource_get_user_data(resource));
            server->CheckInputSerial(serial, "zwp_primary_selection_device_v1.set_selection");
            ++server->primaryInfo.setRequests;
            DataSource* source = sourceResource != nullptr ? Data<DataSource>(sourceResource) : nullptr;
            if (server->clientPrimary != nullptr && server->clientPrimary != source)
            {
                server->CancelSource(server->clientPrimary);
            }
            server->externalPrimary.reset();
            server->clientPrimary = source;
            server->primaryInfo.owned = source != nullptr;
            server->primaryInfo.mimeTypes = source != nullptr ? source->mimeTypes : std::vector<std::string>{};
            server->SendSelectionTo(true);
        },
        .destroy = DestroyRequest,
    };

    const struct zwp_primary_selection_device_manager_v1_interface S::kPrimaryManagerImplementation = {
        .create_source = [](wl_client* client, wl_resource* resource, const std::uint32_t id) {
            State* server = static_cast<State*>(wl_resource_get_user_data(resource));
            auto* source = new DataSource();
            source->server = server;
            source->primary = true;
            source->resource =
                wl_resource_create(client, &zwp_primary_selection_source_v1_interface, wl_resource_get_version(resource), id);
            wl_resource_set_implementation(source->resource, &kPrimarySourceImplementation, source, &DestroyDataSource);
        },
        .get_device = [](wl_client* client, wl_resource* resource, const std::uint32_t id, wl_resource*) {
            State* server = static_cast<State*>(wl_resource_get_user_data(resource));
            wl_resource* device =
                wl_resource_create(client, &zwp_primary_selection_device_v1_interface, wl_resource_get_version(resource), id);
            wl_resource_set_implementation(device, &kPrimaryDeviceImplementation, server,
                                           [](wl_resource* r) { DestroyInList(r, &State::primaryDevices); });
            server->primaryDevices.push_back(device);
        },
        .destroy = DestroyRequest,
    };
#endif

    // =============================================================================================
    // Optional protocols
    // =============================================================================================

    namespace {

#if defined(CNA_WAYLAND_HAVE_VIEWPORTER)
        void DestroyViewport(wl_resource* resource)
        {
            S::Viewport* viewport = Data<S::Viewport>(resource);
            if (viewport->surface != nullptr)
            {
                viewport->surface->viewport = nullptr;
            }
            delete viewport;
        }

        const struct wp_viewport_interface kViewportImplementation = {
            .destroy = DestroyRequest,
            .set_source = [](wl_client*, wl_resource* resource, const wl_fixed_t x, const wl_fixed_t y, const wl_fixed_t width,
                             const wl_fixed_t height) {
                S::Viewport* viewport = Data<S::Viewport>(resource);
                if (viewport->surface == nullptr)
                {
                    viewport->server->PostError(resource, WP_VIEWPORT_ERROR_NO_SURFACE, "the surface is gone");
                    return;
                }
                const double dx = wl_fixed_to_double(x);
                const double dy = wl_fixed_to_double(y);
                const double dw = wl_fixed_to_double(width);
                const double dh = wl_fixed_to_double(height);
                const bool unset = dx == -1.0 && dy == -1.0 && dw == -1.0 && dh == -1.0;
                if (!unset && (dx < 0 || dy < 0 || dw <= 0 || dh <= 0))
                {
                    viewport->server->PostError(resource, WP_VIEWPORT_ERROR_BAD_VALUE, "bad source rectangle");
                    return;
                }
                viewport->pendingSourceSet = true;
                viewport->psx = dx;
                viewport->psy = dy;
                viewport->psw = unset ? -1 : dw;
                viewport->psh = unset ? -1 : dh;
            },
            .set_destination = [](wl_client*, wl_resource* resource, const std::int32_t width, const std::int32_t height) {
                S::Viewport* viewport = Data<S::Viewport>(resource);
                if (viewport->surface == nullptr)
                {
                    viewport->server->PostError(resource, WP_VIEWPORT_ERROR_NO_SURFACE, "the surface is gone");
                    return;
                }
                const bool unset = width == -1 && height == -1;
                if (!unset && (width <= 0 || height <= 0))
                {
                    viewport->server->PostError(resource, WP_VIEWPORT_ERROR_BAD_VALUE,
                                                "destination " + std::to_string(width) + "x" + std::to_string(height));
                    return;
                }
                viewport->pendingDestinationSet = true;
                viewport->pdw = unset ? -1 : width;
                viewport->pdh = unset ? -1 : height;
            },
        };

        const struct wp_viewporter_interface kViewporterImplementation = {
            .destroy = DestroyRequest,
            .get_viewport = [](wl_client* client, wl_resource* resource, const std::uint32_t id, wl_resource* surfaceResource) {
                S* server = static_cast<S*>(wl_resource_get_user_data(resource));
                S::Surface* surface = Data<S::Surface>(surfaceResource);
                if (surface->viewport != nullptr)
                {
                    server->PostError(resource, WP_VIEWPORTER_ERROR_VIEWPORT_EXISTS, "the surface already has a viewport");
                    return;
                }
                auto* viewport = new S::Viewport();
                viewport->server = server;
                viewport->surface = surface;
                viewport->resource = wl_resource_create(client, &wp_viewport_interface, wl_resource_get_version(resource), id);
                wl_resource_set_implementation(viewport->resource, &kViewportImplementation, viewport, &DestroyViewport);
                surface->viewport = viewport;
            },
        };
#endif

#if defined(CNA_WAYLAND_HAVE_FRACTIONAL_SCALE)
        const struct wp_fractional_scale_v1_interface kFractionalImplementation = {.destroy = DestroyRequest};

        const struct wp_fractional_scale_manager_v1_interface kFractionalManagerImplementation = {
            .destroy = DestroyRequest,
            .get_fractional_scale = [](wl_client* client, wl_resource* resource, const std::uint32_t id,
                                       wl_resource* surfaceResource) {
                S* server = static_cast<S*>(wl_resource_get_user_data(resource));
                S::Surface* surface = Data<S::Surface>(surfaceResource);
                if (surface->fractional != nullptr)
                {
                    server->PostError(resource, WP_FRACTIONAL_SCALE_MANAGER_V1_ERROR_FRACTIONAL_SCALE_EXISTS,
                                      "the surface already has a fractional scale object");
                    return;
                }
                wl_resource* fractional =
                    wl_resource_create(client, &wp_fractional_scale_v1_interface, wl_resource_get_version(resource), id);
                wl_resource_set_implementation(fractional, &kFractionalImplementation, surface, [](wl_resource* r) {
                    auto* owner = static_cast<S::Surface*>(wl_resource_get_user_data(r));
                    if (owner != nullptr && owner->fractional == r)
                    {
                        owner->fractional = nullptr;
                    }
                });
                surface->fractional = fractional;
            },
        };
#endif

#if defined(CNA_WAYLAND_HAVE_XDG_OUTPUT)
        const struct zxdg_output_v1_interface kXdgOutputImplementation = {.destroy = DestroyRequest};

        void SendXdgOutput(S::Output* output, wl_resource* xdgOutput, wl_resource* outputResource)
        {
            const OutputSpec& spec = output->spec;
            const int scale = std::max(1, spec.scale);
            zxdg_output_v1_send_logical_position(xdgOutput, spec.x, spec.y);
            zxdg_output_v1_send_logical_size(xdgOutput, spec.logicalWidth > 0 ? spec.logicalWidth : spec.width / scale,
                                             spec.logicalHeight > 0 ? spec.logicalHeight : spec.height / scale);
            const int version = wl_resource_get_version(xdgOutput);
            if (version >= ZXDG_OUTPUT_V1_NAME_SINCE_VERSION)
            {
                zxdg_output_v1_send_name(xdgOutput, spec.name.c_str());
                zxdg_output_v1_send_description(xdgOutput, spec.description.c_str());
            }
            if (version < 3)
            {
                zxdg_output_v1_send_done(xdgOutput);
            }
            else if (outputResource != nullptr && wl_resource_get_version(outputResource) >= WL_OUTPUT_DONE_SINCE_VERSION)
            {
                wl_output_send_done(outputResource);
            }
        }

        const struct zxdg_output_manager_v1_interface kXdgOutputManagerImplementation = {
            .destroy = DestroyRequest,
            .get_xdg_output = [](wl_client* client, wl_resource* resource, const std::uint32_t id, wl_resource* outputResource) {
                auto* output = static_cast<S::Output*>(wl_resource_get_user_data(outputResource));
                wl_resource* xdgOutput =
                    wl_resource_create(client, &zxdg_output_v1_interface, wl_resource_get_version(resource), id);
                wl_resource_set_implementation(xdgOutput, &kXdgOutputImplementation, output, [](wl_resource* r) {
                    auto* owner = static_cast<S::Output*>(wl_resource_get_user_data(r));
                    if (owner != nullptr)
                    {
                        std::erase(owner->xdgOutputs, r);
                    }
                });
                if (output == nullptr)
                {
                    return;  // an output already withdrawn
                }
                output->xdgOutputs.push_back(xdgOutput);
                SendXdgOutput(output, xdgOutput, outputResource);
            },
        };
#endif

#if defined(CNA_WAYLAND_HAVE_RELATIVE_POINTER)
        const struct zwp_relative_pointer_v1_interface kRelativePointerImplementation = {.destroy = DestroyRequest};

        const struct zwp_relative_pointer_manager_v1_interface kRelativeManagerImplementation = {
            .destroy = DestroyRequest,
            .get_relative_pointer = [](wl_client* client, wl_resource* resource, const std::uint32_t id, wl_resource*) {
                S* server = static_cast<S*>(wl_resource_get_user_data(resource));
                wl_resource* relative =
                    wl_resource_create(client, &zwp_relative_pointer_v1_interface, wl_resource_get_version(resource), id);
                wl_resource_set_implementation(relative, &kRelativePointerImplementation, server,
                                               [](wl_resource* r) { DestroyInList(r, &S::relativePointers); });
                server->relativePointers.push_back(relative);
            },
        };
#endif

#if defined(CNA_WAYLAND_HAVE_POINTER_CONSTRAINTS)
        void DestroyConstraint(wl_resource* resource)
        {
            S::Constraint* constraint = Data<S::Constraint>(resource);
            std::erase(constraint->server->constraints, constraint);
            delete constraint;
        }

        const struct zwp_locked_pointer_v1_interface kLockedImplementation = {
            .destroy = DestroyRequest,
            .set_cursor_position_hint = [](wl_client*, wl_resource* resource, const wl_fixed_t x, const wl_fixed_t y) {
                S::Constraint* constraint = Data<S::Constraint>(resource);
                constraint->pendingHint = true;
                constraint->phx = wl_fixed_to_double(x);
                constraint->phy = wl_fixed_to_double(y);
            },
            .set_region = [](wl_client*, wl_resource*, wl_resource*) {},
        };

        const struct zwp_confined_pointer_v1_interface kConfinedImplementation = {
            .destroy = DestroyRequest,
            .set_region = [](wl_client*, wl_resource*, wl_resource*) {},
        };

        void CreateConstraint(wl_client* client, wl_resource* resource, const std::uint32_t id, wl_resource* surfaceResource,
                              const std::uint32_t lifetime, const bool lock)
        {
            S* server = static_cast<S*>(wl_resource_get_user_data(resource));
            S::Surface* surface = Data<S::Surface>(surfaceResource);
            for (S::Constraint* existing : server->constraints)
            {
                if (existing->surface == surface && !existing->defunct)
                {
                    server->PostError(resource, ZWP_POINTER_CONSTRAINTS_V1_ERROR_ALREADY_CONSTRAINED,
                                      "the surface already has a pointer constraint for this seat");
                    return;
                }
            }
            auto* constraint = new S::Constraint();
            constraint->server = server;
            constraint->surface = surface;
            constraint->lock = lock;
            constraint->lifetime = lifetime;
            constraint->resource = wl_resource_create(
                client, lock ? &zwp_locked_pointer_v1_interface : &zwp_confined_pointer_v1_interface,
                wl_resource_get_version(resource), id);
            if (lock)
            {
                wl_resource_set_implementation(constraint->resource, &kLockedImplementation, constraint, &DestroyConstraint);
                ++server->pointerInfo.locksCreated;
                server->pointerInfo.lockLifetime = lifetime;
            }
            else
            {
                wl_resource_set_implementation(constraint->resource, &kConfinedImplementation, constraint, &DestroyConstraint);
            }
            server->constraints.push_back(constraint);
        }

        const struct zwp_pointer_constraints_v1_interface kConstraintsImplementation = {
            .destroy = DestroyRequest,
            .lock_pointer = [](wl_client* client, wl_resource* resource, const std::uint32_t id, wl_resource* surface,
                               wl_resource*, wl_resource*, const std::uint32_t lifetime) {
                CreateConstraint(client, resource, id, surface, lifetime, true);
            },
            .confine_pointer = [](wl_client* client, wl_resource* resource, const std::uint32_t id, wl_resource* surface,
                                  wl_resource*, wl_resource*, const std::uint32_t lifetime) {
                CreateConstraint(client, resource, id, surface, lifetime, false);
            },
        };
#endif

#if defined(CNA_WAYLAND_HAVE_CURSOR_SHAPE)
        const struct wp_cursor_shape_device_v1_interface kShapeDeviceImplementation = {
            .destroy = DestroyRequest,
            .set_shape = [](wl_client*, wl_resource* resource, const std::uint32_t serial, const std::uint32_t shape) {
                S* server = static_cast<S*>(wl_resource_get_user_data(resource));
                const std::uint32_t highest = wl_resource_get_version(resource) >= 2 ? 36u : 34u;
                if (shape < 1 || shape > highest)
                {
                    server->PostError(resource, WP_CURSOR_SHAPE_DEVICE_V1_ERROR_INVALID_SHAPE,
                                      "shape " + std::to_string(shape));
                    return;
                }
                if (serial != server->pointerEnterSerial)
                {
                    server->Violation("wp_cursor_shape_device_v1.set_shape with serial " + std::to_string(serial) +
                                      ", not the current enter serial");
                }
                server->pointerInfo.shape = shape;
                ++server->pointerInfo.shapeRequests;
            },
        };

        const struct wp_cursor_shape_manager_v1_interface kShapeManagerImplementation = {
            .destroy = DestroyRequest,
            .get_pointer = [](wl_client* client, wl_resource* resource, const std::uint32_t id, wl_resource*) {
                S* server = static_cast<S*>(wl_resource_get_user_data(resource));
                wl_resource* device =
                    wl_resource_create(client, &wp_cursor_shape_device_v1_interface, wl_resource_get_version(resource), id);
                wl_resource_set_implementation(device, &kShapeDeviceImplementation, server,
                                               [](wl_resource* r) { DestroyInList(r, &S::shapeDevices); });
                server->shapeDevices.push_back(device);
            },
            .get_tablet_tool_v2 = [](wl_client* client, wl_resource*, std::uint32_t, wl_resource*) {
                wl_client_post_implementation_error(client, "the cursor of a tablet tool is not a "
                                                            "shape the test compositor sets");
            },
        };
#endif

#if defined(CNA_WAYLAND_HAVE_TABLET)
        const struct zwp_tablet_v2_interface kTabletImplementation = {
            .destroy = DestroyRequest,
        };

        const struct zwp_tablet_tool_v2_interface kTabletToolImplementation = {
            .set_cursor = [](wl_client*, wl_resource*, std::uint32_t, wl_resource*, std::int32_t, std::int32_t) {},
            .destroy = DestroyRequest,
        };

        const struct zwp_tablet_seat_v2_interface kTabletSeatImplementation = {
            .destroy = DestroyRequest,
        };

        /// Announces one tablet with one tool on a tablet seat, as a compositor does when the
        /// client binds a seat that has one plugged in, or when one is plugged in later.
        void AnnounceTablet(S* server, wl_client* client, wl_resource* tabletSeat, const bool eraser,
                            const bool withPressure)
        {
            const int version = wl_resource_get_version(tabletSeat);
            wl_resource* tablet = wl_resource_create(client, &zwp_tablet_v2_interface, version, 0);
            wl_resource_set_implementation(tablet, &kTabletImplementation, server,
                                           [](wl_resource* r) { DestroyInList(r, &S::tablets); });
            server->tablets.push_back(tablet);
            zwp_tablet_seat_v2_send_tablet_added(tabletSeat, tablet);
            zwp_tablet_v2_send_name(tablet, "CNA test tablet");
            zwp_tablet_v2_send_id(tablet, 0x056a, 0x0357);
            zwp_tablet_v2_send_path(tablet, "/dev/input/event-cna-test");
            zwp_tablet_v2_send_done(tablet);

            wl_resource* tool = wl_resource_create(client, &zwp_tablet_tool_v2_interface, version, 0);
            wl_resource_set_implementation(tool, &kTabletToolImplementation, server,
                                           [](wl_resource* r) { DestroyInList(r, &S::tabletTools); });
            server->tabletTools.push_back(tool);
            zwp_tablet_seat_v2_send_tool_added(tabletSeat, tool);
            zwp_tablet_tool_v2_send_type(tool, eraser ? ZWP_TABLET_TOOL_V2_TYPE_ERASER
                                                      : ZWP_TABLET_TOOL_V2_TYPE_PEN);
            zwp_tablet_tool_v2_send_hardware_serial(tool, 0, 0x0C0A0001u);
            if (withPressure)
            {
                zwp_tablet_tool_v2_send_capability(tool, ZWP_TABLET_TOOL_V2_CAPABILITY_PRESSURE);
            }
            zwp_tablet_tool_v2_send_capability(tool, ZWP_TABLET_TOOL_V2_CAPABILITY_TILT);
            zwp_tablet_tool_v2_send_done(tool);
        }

        const struct zwp_tablet_manager_v2_interface kTabletManagerImplementation = {
            .get_tablet_seat = [](wl_client* client, wl_resource* resource, const std::uint32_t id,
                                  wl_resource* seat) {
                S* server = static_cast<S*>(wl_resource_get_user_data(resource));
                if (seat == nullptr)
                {
                    wl_client_post_implementation_error(client, "get_tablet_seat with no seat");
                    return;
                }
                wl_resource* tabletSeat =
                    wl_resource_create(client, &zwp_tablet_seat_v2_interface, wl_resource_get_version(resource), id);
                wl_resource_set_implementation(tabletSeat, &kTabletSeatImplementation, server,
                                               [](wl_resource* r) { DestroyInList(r, &S::tabletSeats); });
                server->tabletSeats.push_back(tabletSeat);
                if (server->options.tablet)
                {
                    AnnounceTablet(server, client, tabletSeat, false, server->options.tabletPressure);
                }
            },
            .destroy = DestroyRequest,
        };
#endif

#if defined(CNA_WAYLAND_HAVE_TEXT_INPUT_V3)
        void DestroyTextInput(wl_resource* resource)
        {
            S::TextInput* input = Data<S::TextInput>(resource);
            std::erase(input->server->textInputs, input);
            delete input;
        }

        const struct zwp_text_input_v3_interface kTextInputImplementation = {
            .destroy = DestroyRequest,
            .enable = [](wl_client*, wl_resource* resource) {
                S::TextInput* input = Data<S::TextInput>(resource);
                // enable resets the state to the initial one, then enables.
                input->pending = {};
                input->pending.enabled = true;
            },
            .disable = [](wl_client*, wl_resource* resource) { Data<S::TextInput>(resource)->pending.enabled = false; },
            .set_surrounding_text = [](wl_client*, wl_resource*, const char*, std::int32_t, std::int32_t) {},
            .set_text_change_cause = [](wl_client*, wl_resource*, std::uint32_t) {},
            .set_content_type = [](wl_client*, wl_resource* resource, const std::uint32_t hint, const std::uint32_t purpose) {
                S::TextInput* input = Data<S::TextInput>(resource);
                input->pending.hint = hint;
                input->pending.purpose = purpose;
            },
            .set_cursor_rectangle = [](wl_client*, wl_resource* resource, const std::int32_t x, const std::int32_t y,
                                       const std::int32_t width, const std::int32_t height) {
                S::TextInput* input = Data<S::TextInput>(resource);
                input->pending.cx = x;
                input->pending.cy = y;
                input->pending.cw = width;
                input->pending.ch = height;
            },
            .commit = [](wl_client*, wl_resource* resource) {
                S::TextInput* input = Data<S::TextInput>(resource);
                input->current = input->pending;
                ++input->commits;
            },
        };

        const struct zwp_text_input_manager_v3_interface kTextInputManagerImplementation = {
            .destroy = DestroyRequest,
            .get_text_input = [](wl_client* client, wl_resource* resource, const std::uint32_t id, wl_resource*) {
                S* server = static_cast<S*>(wl_resource_get_user_data(resource));
                auto* input = new S::TextInput();
                input->server = server;
                input->resource = wl_resource_create(client, &zwp_text_input_v3_interface, wl_resource_get_version(resource), id);
                wl_resource_set_implementation(input->resource, &kTextInputImplementation, input, &DestroyTextInput);
                server->textInputs.push_back(input);
            },
        };
#endif

#if defined(CNA_WAYLAND_HAVE_XDG_DECORATION)
        void DestroyDecoration(wl_resource* resource)
        {
            S::Decoration* decoration = Data<S::Decoration>(resource);
            if (decoration->toplevel != nullptr)
            {
                decoration->toplevel->decoration = nullptr;
            }
            std::erase(decoration->server->decorations, decoration);
            delete decoration;
        }

        void AnswerDecoration(S::Decoration* decoration)
        {
            S* server = decoration->server;
            zxdg_toplevel_decoration_v1_send_configure(decoration->resource,
                                                       server->options.serverSideDecorations
                                                           ? ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
                                                           : ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
            S::Toplevel* toplevel = decoration->toplevel;
            if (toplevel != nullptr && toplevel->xdg != nullptr && toplevel->xdg->initialCommitSeen)
            {
                server->Reconfigure(toplevel);
            }
        }

        const struct zxdg_toplevel_decoration_v1_interface kDecorationImplementation = {
            .destroy = DestroyRequest,
            .set_mode = [](wl_client*, wl_resource* resource, const std::uint32_t mode) {
                S::Decoration* decoration = Data<S::Decoration>(resource);
                if (mode != ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE && mode != ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE)
                {
                    decoration->server->PostError(resource, ZXDG_TOPLEVEL_DECORATION_V1_ERROR_INVALID_MODE, "bad mode");
                    return;
                }
                if (decoration->toplevel != nullptr)
                {
                    decoration->toplevel->requestedDecorationMode = mode;
                }
                AnswerDecoration(decoration);
            },
            .unset_mode = [](wl_client*, wl_resource* resource) { AnswerDecoration(Data<S::Decoration>(resource)); },
        };

        const struct zxdg_decoration_manager_v1_interface kDecorationManagerImplementation = {
            .destroy = DestroyRequest,
            .get_toplevel_decoration = [](wl_client* client, wl_resource* resource, const std::uint32_t id,
                                          wl_resource* toplevelResource) {
                S* server = static_cast<S*>(wl_resource_get_user_data(resource));
                S::Toplevel* toplevel = Data<S::Toplevel>(toplevelResource);
                auto* decoration = new S::Decoration();
                decoration->server = server;
                decoration->resource =
                    wl_resource_create(client, &zxdg_toplevel_decoration_v1_interface, wl_resource_get_version(resource), id);
                wl_resource_set_implementation(decoration->resource, &kDecorationImplementation, decoration, &DestroyDecoration);
                server->decorations.push_back(decoration);
                if (toplevel->decoration != nullptr)
                {
                    server->PostError(decoration->resource, ZXDG_TOPLEVEL_DECORATION_V1_ERROR_ALREADY_CONSTRUCTED,
                                      "the toplevel already has a decoration object");
                    return;
                }
                if (toplevel->xdg != nullptr && toplevel->xdg->surface != nullptr &&
                    toplevel->xdg->surface->buffer.buffer != nullptr)
                {
                    server->PostError(decoration->resource, ZXDG_TOPLEVEL_DECORATION_V1_ERROR_UNCONFIGURED_BUFFER,
                                      "decoration created for a toplevel with a buffer");
                    return;
                }
                decoration->toplevel = toplevel;
                toplevel->decoration = decoration;
            },
        };
#endif

#if defined(CNA_WAYLAND_HAVE_XDG_ACTIVATION)
        void DestroyToken(wl_resource* resource)
        {
            delete Data<S::Token>(resource);
        }

        const struct xdg_activation_token_v1_interface kTokenImplementation = {
            .set_serial = [](wl_client*, wl_resource* resource, const std::uint32_t serial, wl_resource*) {
                S::Token* token = Data<S::Token>(resource);
                token->server->CheckInputSerial(serial, "xdg_activation_token_v1.set_serial");
            },
            .set_app_id = [](wl_client*, wl_resource*, const char*) {},
            .set_surface = [](wl_client*, wl_resource*, wl_resource*) {},
            .commit = [](wl_client*, wl_resource* resource) {
                S::Token* token = Data<S::Token>(resource);
                if (token->committed)
                {
                    token->server->PostError(resource, XDG_ACTIVATION_TOKEN_V1_ERROR_ALREADY_USED, "committed twice");
                    return;
                }
                token->committed = true;
                ++token->server->activationTokens;
                const std::string value = "cna-test-token-" + std::to_string(token->server->activationTokens);
                xdg_activation_token_v1_send_done(resource, value.c_str());
            },
            .destroy = DestroyRequest,
        };

        const struct xdg_activation_v1_interface kActivationImplementation = {
            .destroy = DestroyRequest,
            .get_activation_token = [](wl_client* client, wl_resource* resource, const std::uint32_t id) {
                S* server = static_cast<S*>(wl_resource_get_user_data(resource));
                auto* token = new S::Token();
                token->server = server;
                token->resource =
                    wl_resource_create(client, &xdg_activation_token_v1_interface, wl_resource_get_version(resource), id);
                wl_resource_set_implementation(token->resource, &kTokenImplementation, token, &DestroyToken);
            },
            .activate = [](wl_client*, wl_resource* resource, const char* token, wl_resource*) {
                S* server = static_cast<S*>(wl_resource_get_user_data(resource));
                server->activations.emplace_back(token != nullptr ? token : "");
            },
        };
#endif

#if defined(CNA_WAYLAND_HAVE_IDLE_INHIBIT)
        const struct zwp_idle_inhibitor_v1_interface kInhibitorImplementation = {.destroy = DestroyRequest};

        const struct zwp_idle_inhibit_manager_v1_interface kIdleManagerImplementation = {
            .destroy = DestroyRequest,
            .create_inhibitor = [](wl_client* client, wl_resource* resource, const std::uint32_t id, wl_resource*) {
                S* server = static_cast<S*>(wl_resource_get_user_data(resource));
                wl_resource* inhibitor =
                    wl_resource_create(client, &zwp_idle_inhibitor_v1_interface, wl_resource_get_version(resource), id);
                wl_resource_set_implementation(inhibitor, &kInhibitorImplementation, server, [](wl_resource* r) {
                    --static_cast<S*>(wl_resource_get_user_data(r))->idleInhibitors;
                });
                ++server->idleInhibitors;
            },
        };
#endif

#if defined(CNA_WAYLAND_HAVE_XDG_FOREIGN)
        const struct zxdg_exported_v2_interface kExportedImplementation = {.destroy = DestroyRequest};

        const struct zxdg_exporter_v2_interface kExporterImplementation = {
            .destroy = DestroyRequest,
            .export_toplevel = [](wl_client* client, wl_resource* resource, const std::uint32_t id, wl_resource* surfaceResource) {
                S* server = static_cast<S*>(wl_resource_get_user_data(resource));
                S::Surface* surface = Data<S::Surface>(surfaceResource);
                wl_resource* exported =
                    wl_resource_create(client, &zxdg_exported_v2_interface, wl_resource_get_version(resource), id);
                wl_resource_set_implementation(exported, &kExportedImplementation, nullptr, nullptr);
                if (surface->xdg == nullptr || surface->xdg->toplevel == nullptr)
                {
                    server->PostError(resource, ZXDG_EXPORTER_V2_ERROR_INVALID_SURFACE, "not a toplevel");
                    return;
                }
                ++server->exports;
                const std::string handle = "cna-test-handle-" + std::to_string(server->exports);
                zxdg_exported_v2_send_handle(exported, handle.c_str());
            },
        };
#endif

        /// A global whose bind creates a resource with an implementation and the server as data.
        template <const wl_interface* Interface, auto Implementation>
        void BindSimple(wl_client* client, void* data, const std::uint32_t version, const std::uint32_t id)
        {
            wl_resource* resource = wl_resource_create(client, Interface, static_cast<int>(version), id);
            wl_resource_set_implementation(resource, Implementation, data, nullptr);
        }

    } // namespace

    // =============================================================================================
    // TestCompositor
    // =============================================================================================

    TestCompositor::TestCompositor(CompositorOptions options) : state_(std::make_unique<State>())
    {
        State& s = *state_;
        s.options = std::move(options);
        s.display = wl_display_create();
        if (s.display == nullptr)
        {
            throw std::runtime_error("wl_display_create failed");
        }
        s.loop = wl_display_get_event_loop(s.display);
        if (wl_display_init_shm(s.display) != 0)
        {
            throw std::runtime_error("wl_display_init_shm failed");
        }

        s.BuildKeymap(s.options.layout, s.options.variant, s.options.options);

        State* server = &s;
        const auto global = [server](const char* name, const wl_interface* interface, const int version, wl_global_bind_func_t bind) {
            wl_global* created = wl_global_create(server->display, interface, version, server, bind);
            server->singletons[name] = created;
        };

        global("wl_compositor", &wl_compositor_interface, static_cast<int>(s.options.compositorVersion),
               [](wl_client* client, void* data, const std::uint32_t version, const std::uint32_t id) {
                   wl_resource* resource = wl_resource_create(client, &wl_compositor_interface, static_cast<int>(version), id);
                   wl_resource_set_implementation(resource, &State::kCompositorImplementation, data, nullptr);
               });
        if (s.options.subcompositor)
        {
            global("wl_subcompositor", &wl_subcompositor_interface, 1,
                   [](wl_client* client, void* data, const std::uint32_t version, const std::uint32_t id) {
                       wl_resource* resource = wl_resource_create(client, &wl_subcompositor_interface, static_cast<int>(version), id);
                       wl_resource_set_implementation(resource, &State::kSubcompositorImplementation, data, nullptr);
                   });
        }
        global("xdg_wm_base", &xdg_wm_base_interface, static_cast<int>(s.options.wmBaseVersion),
               [](wl_client* client, void* data, const std::uint32_t version, const std::uint32_t id) {
                   wl_resource* resource = wl_resource_create(client, &xdg_wm_base_interface, static_cast<int>(version), id);
                   wl_resource_set_implementation(resource, &State::kWmBaseImplementation, data, nullptr);
               });
        if (s.options.seat)
        {
            global("wl_seat", &wl_seat_interface, static_cast<int>(s.options.seatVersion),
                   [](wl_client* client, void* data, const std::uint32_t version, const std::uint32_t id) {
                       auto* server = static_cast<State*>(data);
                       wl_resource* resource = wl_resource_create(client, &wl_seat_interface, static_cast<int>(version), id);
                       wl_resource_set_implementation(resource, &State::kSeatImplementation, server,
                                                      [](wl_resource* r) { DestroyInList(r, &State::seats); });
                       server->seats.push_back(resource);
                       std::uint32_t capabilities = 0;
                       if (server->options.pointer) { capabilities |= WL_SEAT_CAPABILITY_POINTER; }
                       if (server->options.keyboard) { capabilities |= WL_SEAT_CAPABILITY_KEYBOARD; }
                       if (server->options.touch) { capabilities |= WL_SEAT_CAPABILITY_TOUCH; }
                       wl_seat_send_capabilities(resource, capabilities);
                       if (version >= WL_SEAT_NAME_SINCE_VERSION)
                       {
                           wl_seat_send_name(resource, "seat0");
                       }
                   });
        }
        if (s.options.dataDevice)
        {
            global("wl_data_device_manager", &wl_data_device_manager_interface, 3,
                   [](wl_client* client, void* data, const std::uint32_t version, const std::uint32_t id) {
                       wl_resource* resource =
                           wl_resource_create(client, &wl_data_device_manager_interface, static_cast<int>(version), id);
                       wl_resource_set_implementation(resource, &State::kDataDeviceManagerImplementation, data, nullptr);
                   });
        }
#if defined(CNA_WAYLAND_HAVE_PRIMARY_SELECTION)
        if (s.options.primarySelection)
        {
            global("zwp_primary_selection_device_manager_v1", &zwp_primary_selection_device_manager_v1_interface, 1,
                   [](wl_client* client, void* data, const std::uint32_t version, const std::uint32_t id) {
                       wl_resource* resource = wl_resource_create(client, &zwp_primary_selection_device_manager_v1_interface,
                                                                  static_cast<int>(version), id);
                       wl_resource_set_implementation(resource, &State::kPrimaryManagerImplementation, data, nullptr);
                   });
        }
#endif
#if defined(CNA_WAYLAND_HAVE_VIEWPORTER)
        if (s.options.viewporter)
        {
            global("wp_viewporter", &wp_viewporter_interface, 1,
                   &BindSimple<&wp_viewporter_interface, &kViewporterImplementation>);
        }
#endif
#if defined(CNA_WAYLAND_HAVE_FRACTIONAL_SCALE)
        if (s.options.fractionalScale)
        {
            global("wp_fractional_scale_manager_v1", &wp_fractional_scale_manager_v1_interface, 1,
                   &BindSimple<&wp_fractional_scale_manager_v1_interface, &kFractionalManagerImplementation>);
        }
#endif
#if defined(CNA_WAYLAND_HAVE_XDG_OUTPUT)
        if (s.options.xdgOutput)
        {
            global("zxdg_output_manager_v1", &zxdg_output_manager_v1_interface, 3,
                   &BindSimple<&zxdg_output_manager_v1_interface, &kXdgOutputManagerImplementation>);
        }
#endif
#if defined(CNA_WAYLAND_HAVE_RELATIVE_POINTER)
        if (s.options.relativePointer)
        {
            global("zwp_relative_pointer_manager_v1", &zwp_relative_pointer_manager_v1_interface, 1,
                   &BindSimple<&zwp_relative_pointer_manager_v1_interface, &kRelativeManagerImplementation>);
        }
#endif
#if defined(CNA_WAYLAND_HAVE_POINTER_CONSTRAINTS)
        if (s.options.pointerConstraints)
        {
            global("zwp_pointer_constraints_v1", &zwp_pointer_constraints_v1_interface, 1,
                   &BindSimple<&zwp_pointer_constraints_v1_interface, &kConstraintsImplementation>);
        }
#endif
#if defined(CNA_WAYLAND_HAVE_CURSOR_SHAPE)
        if (s.options.cursorShape)
        {
            global("wp_cursor_shape_manager_v1", &wp_cursor_shape_manager_v1_interface, 2,
                   &BindSimple<&wp_cursor_shape_manager_v1_interface, &kShapeManagerImplementation>);
        }
#endif
#if defined(CNA_WAYLAND_HAVE_TABLET)
        if (s.options.tabletManager || s.options.tablet)
        {
            global("zwp_tablet_manager_v2", &zwp_tablet_manager_v2_interface, 1,
                   &BindSimple<&zwp_tablet_manager_v2_interface, &kTabletManagerImplementation>);
        }
#endif
#if defined(CNA_WAYLAND_HAVE_TEXT_INPUT_V3)
        if (s.options.textInput)
        {
            global("zwp_text_input_manager_v3", &zwp_text_input_manager_v3_interface, 1,
                   &BindSimple<&zwp_text_input_manager_v3_interface, &kTextInputManagerImplementation>);
        }
#endif
#if defined(CNA_WAYLAND_HAVE_XDG_DECORATION)
        if (s.options.decorationManager)
        {
            global("zxdg_decoration_manager_v1", &zxdg_decoration_manager_v1_interface, 1,
                   &BindSimple<&zxdg_decoration_manager_v1_interface, &kDecorationManagerImplementation>);
        }
#endif
#if defined(CNA_WAYLAND_HAVE_XDG_ACTIVATION)
        if (s.options.activation)
        {
            global("xdg_activation_v1", &xdg_activation_v1_interface, 1,
                   &BindSimple<&xdg_activation_v1_interface, &kActivationImplementation>);
        }
#endif
#if defined(CNA_WAYLAND_HAVE_IDLE_INHIBIT)
        if (s.options.idleInhibit)
        {
            global("zwp_idle_inhibit_manager_v1", &zwp_idle_inhibit_manager_v1_interface, 1,
                   &BindSimple<&zwp_idle_inhibit_manager_v1_interface, &kIdleManagerImplementation>);
        }
#endif
#if defined(CNA_WAYLAND_HAVE_XDG_FOREIGN)
        if (s.options.exporter)
        {
            global("zxdg_exporter_v2", &zxdg_exporter_v2_interface, 1,
                   &BindSimple<&zxdg_exporter_v2_interface, &kExporterImplementation>);
        }
#endif

        OutputSpec first;
        first.width = s.options.outputWidth;
        first.height = s.options.outputHeight;
        first.scale = s.options.outputScale;
        first.refresh = s.options.outputRefresh;
        AddOutput(first);

        // The connection: a socketpair, the server end a wl_client now, the client end handed to
        // the backend by ExportSocket().
        int pair[2];
        if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) != 0)
        {
            throw std::runtime_error("socketpair failed");
        }
        s.client = wl_client_create(s.display, pair[0]);
        if (s.client == nullptr)
        {
            ::close(pair[0]);
            ::close(pair[1]);
            throw std::runtime_error("wl_client_create failed");
        }
        s.clientConnected = true;
        s.clientFd = pair[1];
        // Emitted before the client's resources are destroyed: from here on the destructors below
        // tear down a dead client's objects and must not be mistaken for protocol violations.
        s.clientWatch.state = &s;
        s.clientWatch.listener.notify = [](wl_listener* listener, void*) {
            State* owner = reinterpret_cast<State::ClientWatch*>(listener)->state;
            owner->clientDying = true;
            owner->client = nullptr;
            owner->clientConnected = false;
        };
        wl_client_add_destroy_listener(s.client, &s.clientWatch.listener);

        s.taskFd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        s.taskSource = wl_event_loop_add_fd(
            s.loop, s.taskFd, WL_EVENT_READABLE,
            [](int fd, std::uint32_t, void* data) -> int {
                auto* server = static_cast<State*>(data);
                std::uint64_t counter = 0;
                (void) ::read(fd, &counter, sizeof(counter));
                std::deque<State::Task*> batch;
                {
                    std::lock_guard<std::mutex> lock(server->mutex);
                    batch.swap(server->tasks);
                }
                for (State::Task* task : batch)
                {
                    try
                    {
                        task->work();
                    }
                    catch (...)
                    {
                        task->error = std::current_exception();
                    }
                    {
                        std::lock_guard<std::mutex> lock(server->mutex);
                        task->done = true;
                    }
                    server->finished.notify_all();
                }
                return 0;
            },
            &s);

        s.thread = std::thread([server] {
            server->threadId = std::this_thread::get_id();
            while (true)
            {
                {
                    std::lock_guard<std::mutex> lock(server->mutex);
                    if (server->stop)
                    {
                        break;
                    }
                }
                wl_event_loop_dispatch(server->loop, -1);
                wl_display_flush_clients(server->display);
            }
        });
    }

    TestCompositor::~TestCompositor()
    {
        State& s = *state_;
        Run([&s] {
            if (s.client != nullptr)
            {
                wl_client_destroy(s.client);
            }
        });
        {
            std::lock_guard<std::mutex> lock(s.mutex);
            s.stop = true;
        }
        const std::uint64_t one = 1;
        (void) ::write(s.taskFd, &one, sizeof(one));
        if (s.thread.joinable())
        {
            s.thread.join();
        }
        for (std::thread& writer : s.writers)
        {
            if (writer.joinable())
            {
                writer.join();
            }
        }
        if (s.clientFd >= 0 && !s.exported)
        {
            ::close(s.clientFd);
        }
        s.held.clear();
        if (s.taskSource != nullptr)
        {
            wl_event_source_remove(s.taskSource);
        }
        wl_display_destroy(s.display);
        for (State::Output* output : s.outputs)
        {
            delete output;
        }
        if (s.xkbState != nullptr) { xkb_state_unref(s.xkbState); }
        if (s.keymap != nullptr) { xkb_keymap_unref(s.keymap); }
        if (s.xkbContext != nullptr) { xkb_context_unref(s.xkbContext); }
        if (s.taskFd >= 0)
        {
            ::close(s.taskFd);
        }
    }

    void TestCompositor::ExportSocket()
    {
        State& s = *state_;
        if (s.exported)
        {
            return;
        }
        // wl_display_connect() takes WAYLAND_SOCKET over, sets FD_CLOEXEC on it and unsets the
        // variable, so the next connection in the process -- the backend's -- is this one.
        ::setenv("WAYLAND_SOCKET", std::to_string(s.clientFd).c_str(), 1);
        s.exported = true;
    }

    void TestCompositor::Run(const std::function<void()>& task)
    {
        State& s = *state_;
        if (std::this_thread::get_id() == s.threadId)
        {
            task();
            return;
        }
        State::Task item;
        item.work = task;
        {
            std::lock_guard<std::mutex> lock(s.mutex);
            s.tasks.push_back(&item);
        }
        const std::uint64_t one = 1;
        (void) ::write(s.taskFd, &one, sizeof(one));
        std::unique_lock<std::mutex> lock(s.mutex);
        s.finished.wait(lock, [&item] { return item.done; });
        lock.unlock();
        if (item.error)
        {
            std::rethrow_exception(item.error);
        }
    }

    // --- the client ------------------------------------------------------------------------------

    bool TestCompositor::IsClientConnected()
    {
        bool connected = false;
        Run([&] { connected = state_->client != nullptr; });
        return connected;
    }

    std::string TestCompositor::GetPostedError()
    {
        std::string error;
        Run([&] { error = state_->postedError; });
        return error;
    }

    std::vector<std::string> TestCompositor::GetViolations()
    {
        std::vector<std::string> violations;
        Run([&] { violations = state_->violations; });
        return violations;
    }

    void TestCompositor::DisconnectClient()
    {
        Run([this] {
            State& s = *state_;
            if (s.client != nullptr)
            {
                wl_client_destroy(s.client);
            }
        });
    }

    std::uint32_t TestCompositor::Ping()
    {
        std::uint32_t serial = 0;
        Run([&] {
            State& s = *state_;
            if (s.client == nullptr)
            {
                return;
            }
            serial = s.NextSerial();
            // Every xdg_wm_base the client bound: found through the xdg_surfaces is not enough (a
            // client with no windows still answers pings), so ask the client's object map.
            wl_client_for_each_resource(
                s.client,
                [](wl_resource* resource, void* data) -> wl_iterator_result {
                    if (std::strcmp(wl_resource_get_class(resource), xdg_wm_base_interface.name) == 0)
                    {
                        xdg_wm_base_send_ping(resource, *static_cast<std::uint32_t*>(data));
                    }
                    return WL_ITERATOR_CONTINUE;
                },
                &serial);
        });
        return serial;
    }

    int TestCompositor::GetPongCount()
    {
        int pongs = 0;
        Run([&] { pongs = state_->pongs; });
        return pongs;
    }

    // --- windows ---------------------------------------------------------------------------------

    namespace {

        ToplevelInfo Describe(const S::Toplevel* toplevel, const int index)
        {
            ToplevelInfo info;
            info.index = index;
            info.title = toplevel->title;
            info.appId = toplevel->appId;
            info.minWidth = toplevel->minW;
            info.minHeight = toplevel->minH;
            info.maxWidth = toplevel->maxW;
            info.maxHeight = toplevel->maxH;
            info.maximizeRequested = toplevel->maximizeRequested;
            info.fullscreenRequested = toplevel->fullscreenRequested;
            info.minimizeRequests = toplevel->minimizeRequests;
            info.configures = toplevel->configures;
            info.acks = toplevel->acks;
            info.lastConfigureSerial = toplevel->lastConfigureSerial;
            info.lastAckedSerial = toplevel->lastAckedSerial;
            info.mapped = toplevel->mapped;
            info.moveRequests = toplevel->moveRequests;
            info.resizeRequests = toplevel->resizeRequests;
            info.lastResizeEdges = toplevel->lastResizeEdges;
            info.windowMenuRequests = toplevel->windowMenuRequests;
            info.hasDecoration = toplevel->decoration != nullptr;
            info.requestedDecorationMode = toplevel->requestedDecorationMode;
            if (const S::XdgSurface* xdg = toplevel->xdg)
            {
                info.hasGeometry = xdg->geometrySet;
                info.geometryX = xdg->gx;
                info.geometryY = xdg->gy;
                info.geometryWidth = xdg->gw;
                info.geometryHeight = xdg->gh;
                if (const S::Surface* surface = xdg->surface)
                {
                    info.surfaceId = surface->id;
                    info.bufferWidth = surface->buffer.buffer != nullptr ? surface->bufferWidth : 0;
                    info.bufferHeight = surface->buffer.buffer != nullptr ? surface->bufferHeight : 0;
                    info.bufferScale = surface->scale;
                    if (surface->viewport != nullptr && surface->viewport->destinationSet)
                    {
                        info.viewportWidth = surface->viewport->dw;
                        info.viewportHeight = surface->viewport->dh;
                    }
                    info.commits = surface->commits;
                    info.bufferCommits = surface->bufferCommits;
                    info.subsurfaces = static_cast<int>(surface->children.size());
                    info.opaqueRegion = surface->opaque;
                    info.centrePixel = surface->centrePixel;
                }
            }
            return info;
        }

    } // namespace

    std::vector<ToplevelInfo> TestCompositor::GetToplevels()
    {
        std::vector<ToplevelInfo> result;
        Run([&] {
            int index = 0;
            for (const State::Toplevel* toplevel : state_->toplevels)
            {
                result.push_back(Describe(toplevel, index++));
            }
        });
        return result;
    }

    std::optional<ToplevelInfo> TestCompositor::GetToplevel(const int index)
    {
        std::vector<ToplevelInfo> all = GetToplevels();
        if (index < 0 || static_cast<std::size_t>(index) >= all.size())
        {
            return std::nullopt;
        }
        return all[static_cast<std::size_t>(index)];
    }

    std::vector<SubsurfaceInfo> TestCompositor::GetSubsurfaces()
    {
        std::vector<SubsurfaceInfo> result;
        Run([&] {
            for (const State::Subsurface* sub : state_->subsurfaces)
            {
                SubsurfaceInfo info;
                info.surfaceId = sub->surface != nullptr ? sub->surface->id : 0;
                info.parentId = sub->parent != nullptr ? sub->parent->id : 0;
                info.x = sub->x;
                info.y = sub->y;
                info.desync = sub->desync;
                if (sub->surface != nullptr)
                {
                    info.bufferWidth = sub->surface->buffer.buffer != nullptr ? sub->surface->bufferWidth : 0;
                    info.bufferHeight = sub->surface->buffer.buffer != nullptr ? sub->surface->bufferHeight : 0;
                    if (sub->surface->viewport != nullptr && sub->surface->viewport->destinationSet)
                    {
                        info.viewportWidth = sub->surface->viewport->dw;
                        info.viewportHeight = sub->surface->viewport->dh;
                    }
                }
                result.push_back(info);
            }
        });
        return result;
    }

    int TestCompositor::GetSurfaceCount()
    {
        int count = 0;
        Run([&] { count = static_cast<int>(state_->surfaces.size()); });
        return count;
    }

    std::uint32_t TestCompositor::Configure(const int index, const int width, const int height,
                                            const std::vector<std::uint32_t>& states)
    {
        std::uint32_t serial = 0;
        Run([&] { serial = state_->SendConfigure(state_->ToplevelAt(index), width, height, states); });
        return serial;
    }

    void TestCompositor::Close(const int index)
    {
        Run([&] { xdg_toplevel_send_close(state_->ToplevelAt(index)->resource); });
    }

    int TestCompositor::FrameDone()
    {
        int count = 0;
        Run([&] {
            const std::uint32_t now = NowMs();
            for (State::Surface* surface : state_->surfaces)
            {
                std::vector<wl_resource*> frames;
                frames.swap(surface->frames);
                for (wl_resource* frame : frames)
                {
                    wl_resource_set_user_data(frame, nullptr);
                    wl_callback_send_done(frame, now);
                    wl_resource_destroy(frame);
                    ++count;
                }
            }
        });
        return count;
    }

    int TestCompositor::GetPendingFrameCallbacks()
    {
        int count = 0;
        Run([&] {
            for (const State::Surface* surface : state_->surfaces)
            {
                count += static_cast<int>(surface->frames.size());
            }
        });
        return count;
    }

    int TestCompositor::GetHeldBuffers()
    {
        int count = 0;
        Run([&] {
            for (const BufferRef& ref : state_->held)
            {
                count += ref.buffer != nullptr ? 1 : 0;
            }
        });
        return count;
    }

    void TestCompositor::ReleaseHeldBuffers()
    {
        Run([&] {
            for (BufferRef& ref : state_->held)
            {
                if (ref.buffer != nullptr)
                {
                    wl_buffer_send_release(ref.buffer);
                }
            }
            state_->held.clear();
        });
    }

    void TestCompositor::SendPreferredBufferScale(const int index, const int scale)
    {
        Run([&] {
            State::Toplevel* toplevel = state_->ToplevelAt(index);
            if (toplevel->xdg != nullptr && toplevel->xdg->surface != nullptr &&
                wl_resource_get_version(toplevel->xdg->surface->resource) >= WL_SURFACE_PREFERRED_BUFFER_SCALE_SINCE_VERSION)
            {
                wl_surface_send_preferred_buffer_scale(toplevel->xdg->surface->resource, scale);
            }
        });
    }

    void TestCompositor::SendPreferredScale(const int index, const std::uint32_t scale120)
    {
#if defined(CNA_WAYLAND_HAVE_FRACTIONAL_SCALE)
        Run([&] {
            State::Toplevel* toplevel = state_->ToplevelAt(index);
            if (toplevel->xdg != nullptr && toplevel->xdg->surface != nullptr && toplevel->xdg->surface->fractional != nullptr)
            {
                wp_fractional_scale_v1_send_preferred_scale(toplevel->xdg->surface->fractional, scale120);
            }
        });
#else
        (void) index;
        (void) scale120;
#endif
    }

    // --- outputs ---------------------------------------------------------------------------------

    int TestCompositor::AddOutput(const OutputSpec& spec)
    {
        int index = 0;
        const auto add = [&] {
            State& s = *state_;
            auto* output = new State::Output();
            output->server = &s;
            output->spec = spec;
            output->global = wl_global_create(
                s.display, &wl_output_interface, static_cast<int>(s.options.outputVersion), output,
                [](wl_client* client, void* data, const std::uint32_t version, const std::uint32_t id) {
                    auto* owner = static_cast<State::Output*>(data);
                    wl_resource* resource = wl_resource_create(client, &wl_output_interface, static_cast<int>(version), id);
                    wl_resource_set_implementation(resource, &State::kOutputImplementation, owner, &DestroyOutputResource);
                    owner->resources.push_back(resource);
                    SendOutputState(owner, resource);
                });
            s.outputs.push_back(output);
            index = static_cast<int>(s.outputs.size()) - 1;
        };
        if (state_->thread.joinable())
        {
            Run(add);
        }
        else
        {
            add();
        }
        return index;
    }

    void TestCompositor::RemoveOutput(const int index)
    {
        Run([&] {
            State& s = *state_;
            State::Output* output = s.outputs.at(static_cast<std::size_t>(index));
            if (output->removed)
            {
                return;
            }
            output->removed = true;
            // The resources stay valid until the client releases them; their events stop.
            for (wl_resource* resource : output->resources)
            {
                wl_resource_set_user_data(resource, nullptr);
            }
            output->resources.clear();
            for (wl_resource* xdgOutput : output->xdgOutputs)
            {
                wl_resource_set_user_data(xdgOutput, nullptr);
            }
            output->xdgOutputs.clear();
            wl_global_remove(output->global);
            s.removedGlobals.push_back(output->global);
        });
    }

    void TestCompositor::SetOutputScale(const int index, const int scale, const int logicalWidth, const int logicalHeight)
    {
        Run([&] {
            State::Output* output = state_->outputs.at(static_cast<std::size_t>(index));
            output->spec.scale = scale;
            output->spec.logicalWidth = logicalWidth;
            output->spec.logicalHeight = logicalHeight;
            for (wl_resource* resource : output->resources)
            {
                if (wl_resource_get_version(resource) >= WL_OUTPUT_SCALE_SINCE_VERSION)
                {
                    wl_output_send_scale(resource, scale);
                }
            }
#if defined(CNA_WAYLAND_HAVE_XDG_OUTPUT)
            const int lw = logicalWidth > 0 ? logicalWidth : output->spec.width / std::max(1, scale);
            const int lh = logicalHeight > 0 ? logicalHeight : output->spec.height / std::max(1, scale);
            for (wl_resource* xdgOutput : output->xdgOutputs)
            {
                zxdg_output_v1_send_logical_size(xdgOutput, lw, lh);
                if (wl_resource_get_version(xdgOutput) < 3)
                {
                    zxdg_output_v1_send_done(xdgOutput);
                }
            }
#endif
            for (wl_resource* resource : output->resources)
            {
                if (wl_resource_get_version(resource) >= WL_OUTPUT_DONE_SINCE_VERSION)
                {
                    wl_output_send_done(resource);
                }
            }
        });
    }

    void TestCompositor::EnterOutput(const int toplevel, const int output)
    {
        Run([&] {
            State::Toplevel* top = state_->ToplevelAt(toplevel);
            wl_resource* resource = state_->OutputResourceFor(state_->outputs.at(static_cast<std::size_t>(output)));
            if (resource != nullptr && top->xdg != nullptr && top->xdg->surface != nullptr)
            {
                wl_surface_send_enter(top->xdg->surface->resource, resource);
            }
        });
    }

    void TestCompositor::LeaveOutput(const int toplevel, const int output)
    {
        Run([&] {
            State::Toplevel* top = state_->ToplevelAt(toplevel);
            wl_resource* resource = state_->OutputResourceFor(state_->outputs.at(static_cast<std::size_t>(output)));
            if (resource != nullptr && top->xdg != nullptr && top->xdg->surface != nullptr)
            {
                wl_surface_send_leave(top->xdg->surface->resource, resource);
            }
        });
    }

    void TestCompositor::RemoveGlobal(const std::string& interfaceName)
    {
        Run([&] {
            State& s = *state_;
            const auto found = s.singletons.find(interfaceName);
            if (found == s.singletons.end() || found->second == nullptr)
            {
                throw std::invalid_argument("the test compositor offers no " + interfaceName);
            }
            wl_global_remove(found->second);
            s.removedGlobals.push_back(found->second);
            found->second = nullptr;
        });
    }

    // --- keyboard --------------------------------------------------------------------------------

    void TestCompositor::KeyboardEnter(const int toplevel, const std::vector<std::uint32_t>& heldKeys)
    {
        Run([&] {
            State& s = *state_;
            State::Toplevel* top = s.ToplevelAt(toplevel);
            if (top->xdg == nullptr || top->xdg->surface == nullptr)
            {
                return;
            }
            State::Surface* surface = top->xdg->surface;
            if (s.keyboardFocus != nullptr && s.keyboardFocus != surface)
            {
                const std::uint32_t serial = s.InputSerial();
                for (wl_resource* keyboard : s.keyboards)
                {
                    wl_keyboard_send_leave(keyboard, serial, s.keyboardFocus->resource);
                }
            }
            s.keyboardFocus = surface;
            // The selection first: a client learns what it can paste before it gets focus.
            s.SendSelectionTo(false);
            s.SendSelectionTo(true);
            if (s.xkbState != nullptr)
            {
                for (const std::uint32_t key : heldKeys)
                {
                    xkb_state_update_key(s.xkbState, key + 8, XKB_KEY_DOWN);
                }
            }
            wl_array keys;
            wl_array_init(&keys);
            for (const std::uint32_t key : heldKeys)
            {
                *static_cast<std::uint32_t*>(wl_array_add(&keys, sizeof(std::uint32_t))) = key;
            }
            const std::uint32_t serial = s.InputSerial();
            for (wl_resource* keyboard : s.keyboards)
            {
                wl_keyboard_send_enter(keyboard, serial, surface->resource, &keys);
            }
            wl_array_release(&keys);
            s.SendModifiersIfChanged(true);
        });
    }

    void TestCompositor::KeyboardLeave()
    {
        Run([&] {
            State& s = *state_;
            if (s.keyboardFocus == nullptr)
            {
                return;
            }
            const std::uint32_t serial = s.InputSerial();
            for (wl_resource* keyboard : s.keyboards)
            {
                wl_keyboard_send_leave(keyboard, serial, s.keyboardFocus->resource);
            }
            s.keyboardFocus = nullptr;
        });
    }

    void TestCompositor::Key(const std::uint32_t evdevKey, const bool pressed)
    {
        Run([&] {
            State& s = *state_;
            if (s.keyboardFocus == nullptr)
            {
                throw std::logic_error("Key() without keyboard focus: a compositor sends keys only to the focused surface");
            }
            const std::uint32_t serial = s.InputSerial();
            const std::uint32_t time = NowMs();
            for (wl_resource* keyboard : s.keyboards)
            {
                wl_keyboard_send_key(keyboard, serial, time, evdevKey,
                                     pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
            }
            if (s.xkbState != nullptr)
            {
                xkb_state_update_key(s.xkbState, evdevKey + 8, pressed ? XKB_KEY_DOWN : XKB_KEY_UP);
            }
            s.SendModifiersIfChanged(false);
        });
    }

    void TestCompositor::ReplaceKeymap(const std::string& layout, const std::string& variant)
    {
        Run([&] {
            State& s = *state_;
            s.BuildKeymap(layout, variant, s.options.options);
            for (wl_resource* keyboard : s.keyboards)
            {
                s.SendKeymap(keyboard);
            }
            s.SendModifiersIfChanged(true);
        });
    }

    void TestCompositor::SetLayoutGroup(const std::uint32_t group)
    {
        Run([&] {
            State& s = *state_;
            if (s.xkbState == nullptr)
            {
                return;
            }
            xkb_state_update_mask(s.xkbState, xkb_state_serialize_mods(s.xkbState, XKB_STATE_MODS_DEPRESSED),
                                  xkb_state_serialize_mods(s.xkbState, XKB_STATE_MODS_LATCHED),
                                  xkb_state_serialize_mods(s.xkbState, XKB_STATE_MODS_LOCKED), 0, 0, group);
            s.SendModifiersIfChanged(true);
        });
    }

    void TestCompositor::SetSeatCapabilities(const bool pointer, const bool keyboard, const bool touch)
    {
        Run([&] {
            State& s = *state_;
            s.options.pointer = pointer;
            s.options.keyboard = keyboard;
            s.options.touch = touch;
            if (!keyboard) { s.keyboardFocus = nullptr; }
            if (!pointer) { s.pointerFocus = nullptr; }
            if (!touch) { s.touchFocus = nullptr; }
            std::uint32_t capabilities = 0;
            if (pointer) { capabilities |= WL_SEAT_CAPABILITY_POINTER; }
            if (keyboard) { capabilities |= WL_SEAT_CAPABILITY_KEYBOARD; }
            if (touch) { capabilities |= WL_SEAT_CAPABILITY_TOUCH; }
            for (wl_resource* seat : s.seats)
            {
                wl_seat_send_capabilities(seat, capabilities);
            }
        });
    }

    int TestCompositor::GetTouchCount()
    {
        int count = 0;
        Run([&] { count = static_cast<int>(state_->touches.size()); });
        return count;
    }

    int TestCompositor::GetKeyboardCount()
    {
        int count = 0;
        Run([&] { count = static_cast<int>(state_->keyboards.size()); });
        return count;
    }

    std::uint32_t TestCompositor::GetLastInputSerial()
    {
        std::uint32_t serial = 0;
        Run([&] { serial = state_->lastInputSerial; });
        return serial;
    }

    // --- pointer ---------------------------------------------------------------------------------

    namespace {

        void PointerFrame(S& s)
        {
            for (wl_resource* pointer : s.pointers)
            {
                if (wl_resource_get_version(pointer) >= WL_POINTER_FRAME_SINCE_VERSION)
                {
                    wl_pointer_send_frame(pointer);
                }
            }
        }

        void EnterSurface(S& s, S::Surface* surface, const double x, const double y)
        {
            if (s.pointerFocus != nullptr && s.pointerFocus != surface)
            {
                const std::uint32_t serial = s.InputSerial();
                for (wl_resource* pointer : s.pointers)
                {
                    wl_pointer_send_leave(pointer, serial, s.pointerFocus->resource);
                }
                PointerFrame(s);
            }
            s.pointerFocus = surface;
            const std::uint32_t serial = s.InputSerial();
            s.pointerEnterSerial = serial;
            for (wl_resource* pointer : s.pointers)
            {
                wl_pointer_send_enter(pointer, serial, surface->resource, wl_fixed_from_double(x), wl_fixed_from_double(y));
            }
            PointerFrame(s);
        }

    } // namespace

    void TestCompositor::PointerEnter(const int toplevel, const double x, const double y)
    {
        Run([&] {
            State& s = *state_;
            State::Toplevel* top = s.ToplevelAt(toplevel);
            if (top->xdg != nullptr && top->xdg->surface != nullptr)
            {
                EnterSurface(s, top->xdg->surface, x, y);
            }
        });
    }

    void TestCompositor::PointerEnterSurface(const std::uint32_t surfaceId, const double x, const double y)
    {
        Run([&] {
            State& s = *state_;
            State::Surface* surface = s.FindSurface(surfaceId);
            if (surface == nullptr)
            {
                throw std::invalid_argument("no surface " + std::to_string(surfaceId));
            }
            EnterSurface(s, surface, x, y);
        });
    }

    void TestCompositor::PointerMotion(const double x, const double y)
    {
        Run([&] {
            State& s = *state_;
            if (s.pointerFocus == nullptr)
            {
                return;
            }
            const std::uint32_t time = NowMs();
            for (wl_resource* pointer : s.pointers)
            {
                wl_pointer_send_motion(pointer, time, wl_fixed_from_double(x), wl_fixed_from_double(y));
            }
            PointerFrame(s);
        });
    }

    void TestCompositor::PointerButton(const std::uint32_t button, const bool pressed)
    {
        Run([&] {
            State& s = *state_;
            if (s.pointerFocus == nullptr)
            {
                return;
            }
            const std::uint32_t serial = s.InputSerial();
            const std::uint32_t time = NowMs();
            if (pressed)
            {
                s.heldPresses[button] = serial;
            }
            else
            {
                s.heldPresses.erase(button);
            }
            for (wl_resource* pointer : s.pointers)
            {
                wl_pointer_send_button(pointer, serial, time, button,
                                       pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
            }
            PointerFrame(s);
        });
    }

    void TestCompositor::PointerAxis(const std::uint32_t axis, const double value, const std::int32_t value120,
                                     const std::int32_t discrete, const std::int32_t source)
    {
        Run([&] {
            State& s = *state_;
            if (s.pointerFocus == nullptr)
            {
                return;
            }
            const std::uint32_t time = NowMs();
            for (wl_resource* pointer : s.pointers)
            {
                const int version = wl_resource_get_version(pointer);
                if (source >= 0 && version >= WL_POINTER_AXIS_SOURCE_SINCE_VERSION)
                {
                    wl_pointer_send_axis_source(pointer, static_cast<std::uint32_t>(source));
                }
                if (value120 != 0 && version >= WL_POINTER_AXIS_VALUE120_SINCE_VERSION)
                {
                    wl_pointer_send_axis_value120(pointer, axis, value120);
                }
                else if (discrete != 0 && version >= WL_POINTER_AXIS_DISCRETE_SINCE_VERSION &&
                         version < WL_POINTER_AXIS_VALUE120_SINCE_VERSION)
                {
                    wl_pointer_send_axis_discrete(pointer, axis, discrete);
                }
                wl_pointer_send_axis(pointer, time, axis, wl_fixed_from_double(value));
            }
            PointerFrame(s);
        });
    }

    void TestCompositor::PointerLeave()
    {
        Run([&] {
            State& s = *state_;
            if (s.pointerFocus == nullptr)
            {
                return;
            }
            const std::uint32_t serial = s.InputSerial();
            for (wl_resource* pointer : s.pointers)
            {
                wl_pointer_send_leave(pointer, serial, s.pointerFocus->resource);
            }
            PointerFrame(s);
            s.pointerFocus = nullptr;
        });
    }

    void TestCompositor::RelativeMotion(const double dx, const double dy, const double dxUnaccelerated,
                                        const double dyUnaccelerated)
    {
#if defined(CNA_WAYLAND_HAVE_RELATIVE_POINTER)
        Run([&] {
            State& s = *state_;
            const std::uint64_t now = NowUs();
            for (wl_resource* relative : s.relativePointers)
            {
                zwp_relative_pointer_v1_send_relative_motion(relative, static_cast<std::uint32_t>(now >> 32),
                                                             static_cast<std::uint32_t>(now & 0xffffffffu),
                                                             wl_fixed_from_double(dx), wl_fixed_from_double(dy),
                                                             wl_fixed_from_double(dxUnaccelerated),
                                                             wl_fixed_from_double(dyUnaccelerated));
            }
            PointerFrame(s);
        });
#else
        (void) dx;
        (void) dy;
        (void) dxUnaccelerated;
        (void) dyUnaccelerated;
#endif
    }

    bool TestCompositor::ActivateLock()
    {
        bool activated = false;
#if defined(CNA_WAYLAND_HAVE_POINTER_CONSTRAINTS)
        Run([&] {
            for (State::Constraint* constraint : state_->constraints)
            {
                if (constraint->lock && !constraint->defunct && !constraint->active)
                {
                    zwp_locked_pointer_v1_send_locked(constraint->resource);
                    constraint->active = true;
                    activated = true;
                    return;
                }
            }
        });
#endif
        return activated;
    }

    void TestCompositor::DeactivateLock()
    {
#if defined(CNA_WAYLAND_HAVE_POINTER_CONSTRAINTS)
        Run([&] {
            for (State::Constraint* constraint : state_->constraints)
            {
                if (constraint->lock && constraint->active)
                {
                    zwp_locked_pointer_v1_send_unlocked(constraint->resource);
                    constraint->active = false;
                    if (constraint->lifetime == ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT)
                    {
                        constraint->defunct = true;
                    }
                }
            }
        });
#endif
    }

    PointerInfo TestCompositor::GetPointerInfo()
    {
        PointerInfo info;
        Run([&] {
            State& s = *state_;
            info = s.pointerInfo;
            info.pointers = static_cast<int>(s.pointers.size());
            info.relativePointers = static_cast<int>(s.relativePointers.size());
            info.locks = 0;
            info.confinements = 0;
            info.lockActive = false;
            info.hasHint = false;
            for (const State::Constraint* constraint : s.constraints)
            {
                if (constraint->lock)
                {
                    ++info.locks;
                    info.lockActive = info.lockActive || constraint->active;
                    if (constraint->hasHint)
                    {
                        info.hasHint = true;
                        info.hintX = constraint->hx;
                        info.hintY = constraint->hy;
                    }
                }
                else
                {
                    ++info.confinements;
                }
            }
        });
        return info;
    }

    // --- touch -----------------------------------------------------------------------------------

    void TestCompositor::TouchDown(const int toplevel, const std::int32_t id, const double x, const double y)
    {
        Run([&] {
            State& s = *state_;
            State::Toplevel* top = s.ToplevelAt(toplevel);
            if (top->xdg == nullptr || top->xdg->surface == nullptr)
            {
                return;
            }
            s.touchFocus = top->xdg->surface;
            const std::uint32_t serial = s.InputSerial();
            const std::uint32_t time = NowMs();
            for (wl_resource* touch : s.touches)
            {
                wl_touch_send_down(touch, serial, time, s.touchFocus->resource, id, wl_fixed_from_double(x),
                                   wl_fixed_from_double(y));
            }
        });
    }

    void TestCompositor::TouchMotion(const std::int32_t id, const double x, const double y)
    {
        Run([&] {
            const std::uint32_t time = NowMs();
            for (wl_resource* touch : state_->touches)
            {
                wl_touch_send_motion(touch, time, id, wl_fixed_from_double(x), wl_fixed_from_double(y));
            }
        });
    }

    void TestCompositor::TouchUp(const std::int32_t id)
    {
        Run([&] {
            State& s = *state_;
            const std::uint32_t serial = s.InputSerial();
            const std::uint32_t time = NowMs();
            for (wl_resource* touch : s.touches)
            {
                wl_touch_send_up(touch, serial, time, id);
            }
        });
    }

    void TestCompositor::TouchFrame()
    {
        Run([&] {
            for (wl_resource* touch : state_->touches)
            {
                wl_touch_send_frame(touch);
            }
        });
    }

    void TestCompositor::TouchCancel()
    {
        Run([&] {
            for (wl_resource* touch : state_->touches)
            {
                wl_touch_send_cancel(touch);
            }
        });
    }

    // --- tablets ---------------------------------------------------------------------------------

#if defined(CNA_WAYLAND_HAVE_TABLET)

    namespace {

        /// Every tool the client holds. A test drives one tool; more than one would need the
        /// caller to name it, which no test has asked for.
        void ForEachTool(TestCompositor::State& s, const std::function<void(wl_resource*)>& send)
        {
            for (wl_resource* tool : s.tabletTools)
            {
                send(tool);
            }
        }

    } // namespace

    void TestCompositor::AddTabletTool(const bool eraser, const bool withPressure)
    {
        Run([&] {
            State& s = *state_;
            if (s.tabletSeats.empty())
            {
                throw std::runtime_error("the client has bound no zwp_tablet_seat_v2");
            }
            for (wl_resource* tabletSeat : s.tabletSeats)
            {
                AnnounceTablet(&s, wl_resource_get_client(tabletSeat), tabletSeat, eraser, withPressure);
            }
        });
    }

    void TestCompositor::RemoveTabletTool()
    {
        Run([&] { ForEachTool(*state_, [](wl_resource* tool) { zwp_tablet_tool_v2_send_removed(tool); }); });
    }

    void TestCompositor::RemoveTablet()
    {
        Run([&] {
            for (wl_resource* tablet : state_->tablets)
            {
                zwp_tablet_v2_send_removed(tablet);
            }
        });
    }

    void TestCompositor::TabletProximityIn(const int toplevel)
    {
        Run([&] {
            State& s = *state_;
            State::Toplevel* top = s.ToplevelAt(toplevel);
            if (top->xdg == nullptr || top->xdg->surface == nullptr || s.tablets.empty())
            {
                return;
            }
            s.tabletFocus = top->xdg->surface;
            const std::uint32_t serial = s.InputSerial();
            wl_resource* tablet = s.tablets.front();
            ForEachTool(s, [&](wl_resource* tool) {
                zwp_tablet_tool_v2_send_proximity_in(tool, serial, tablet, s.tabletFocus->resource);
            });
        });
    }

    void TestCompositor::TabletProximityOut()
    {
        Run([&] {
            state_->tabletFocus = nullptr;
            ForEachTool(*state_, [](wl_resource* tool) { zwp_tablet_tool_v2_send_proximity_out(tool); });
        });
    }

    void TestCompositor::TabletDown()
    {
        Run([&] {
            const std::uint32_t serial = state_->InputSerial();
            ForEachTool(*state_, [serial](wl_resource* tool) { zwp_tablet_tool_v2_send_down(tool, serial); });
        });
    }

    void TestCompositor::TabletUp()
    {
        Run([&] { ForEachTool(*state_, [](wl_resource* tool) { zwp_tablet_tool_v2_send_up(tool); }); });
    }

    void TestCompositor::TabletMotion(const double x, const double y)
    {
        Run([&] {
            ForEachTool(*state_, [x, y](wl_resource* tool) {
                zwp_tablet_tool_v2_send_motion(tool, wl_fixed_from_double(x), wl_fixed_from_double(y));
            });
        });
    }

    void TestCompositor::TabletPressure(const double pressure)
    {
        Run([&] {
            const double clamped = pressure < 0.0 ? 0.0 : (pressure > 1.0 ? 1.0 : pressure);
            const auto raw = static_cast<std::uint32_t>(clamped * 65535.0 + 0.5);
            ForEachTool(*state_, [raw](wl_resource* tool) { zwp_tablet_tool_v2_send_pressure(tool, raw); });
        });
    }

    void TestCompositor::TabletTilt(const double x, const double y)
    {
        Run([&] {
            ForEachTool(*state_, [x, y](wl_resource* tool) {
                zwp_tablet_tool_v2_send_tilt(tool, wl_fixed_from_double(x), wl_fixed_from_double(y));
            });
        });
    }

    void TestCompositor::TabletButton(const std::uint32_t button, const bool pressed)
    {
        Run([&] {
            const std::uint32_t serial = state_->InputSerial();
            ForEachTool(*state_, [&](wl_resource* tool) {
                zwp_tablet_tool_v2_send_button(tool, serial, button,
                                               pressed ? ZWP_TABLET_TOOL_V2_BUTTON_STATE_PRESSED
                                                       : ZWP_TABLET_TOOL_V2_BUTTON_STATE_RELEASED);
            });
        });
    }

    void TestCompositor::TabletFrame()
    {
        Run([&] {
            const std::uint32_t time = NowMs();
            ForEachTool(*state_, [time](wl_resource* tool) { zwp_tablet_tool_v2_send_frame(tool, time); });
        });
    }

    int TestCompositor::GetTabletToolCount()
    {
        int count = 0;
        Run([&] { count = static_cast<int>(state_->tabletTools.size()); });
        return count;
    }

    int TestCompositor::GetTabletSeatCount()
    {
        int count = 0;
        Run([&] { count = static_cast<int>(state_->tabletSeats.size()); });
        return count;
    }

#else

    void TestCompositor::AddTabletTool(bool, bool) {}
    void TestCompositor::RemoveTabletTool() {}
    void TestCompositor::RemoveTablet() {}
    void TestCompositor::TabletProximityIn(int) {}
    void TestCompositor::TabletProximityOut() {}
    void TestCompositor::TabletDown() {}
    void TestCompositor::TabletUp() {}
    void TestCompositor::TabletMotion(double, double) {}
    void TestCompositor::TabletPressure(double) {}
    void TestCompositor::TabletTilt(double, double) {}
    void TestCompositor::TabletButton(std::uint32_t, bool) {}
    void TestCompositor::TabletFrame() {}
    int TestCompositor::GetTabletToolCount() { return 0; }
    int TestCompositor::GetTabletSeatCount() { return 0; }

#endif

    // --- selections ------------------------------------------------------------------------------

    void TestCompositor::OfferSelection(const std::map<std::string, std::vector<std::uint8_t>>& data)
    {
        Run([&] {
            State& s = *state_;
            s.CancelSource(s.clientSelection);
            s.selectionInfo.owned = false;
            s.externalSelection = data;
            s.SendSelectionTo(false);
        });
    }

    void TestCompositor::ClearSelection()
    {
        Run([&] {
            State& s = *state_;
            s.CancelSource(s.clientSelection);
            s.selectionInfo.owned = false;
            s.externalSelection.reset();
            s.SendSelectionTo(false);
        });
    }

    ClientSelectionInfo TestCompositor::GetClientSelection()
    {
        ClientSelectionInfo info;
        Run([&] {
            info = state_->selectionInfo;
            if (state_->clientSelection != nullptr)
            {
                info.mimeTypes = state_->clientSelection->mimeTypes;
            }
        });
        return info;
    }

    int TestCompositor::RequestClientSelection(const std::string& mimeType)
    {
        int readEnd = -1;
        Run([&] {
            State& s = *state_;
            if (s.clientSelection == nullptr)
            {
                return;
            }
            int pipes[2];
            if (::pipe2(pipes, O_CLOEXEC) != 0)
            {
                return;
            }
            wl_data_source_send_send(s.clientSelection->resource, mimeType.c_str(), pipes[1]);
            ::close(pipes[1]);
            readEnd = pipes[0];
        });
        return readEnd;
    }

    void TestCompositor::OfferPrimarySelection(const std::map<std::string, std::vector<std::uint8_t>>& data)
    {
        Run([&] {
            State& s = *state_;
            s.CancelSource(s.clientPrimary);
            s.primaryInfo.owned = false;
            s.externalPrimary = data;
            s.SendSelectionTo(true);
        });
    }

    ClientSelectionInfo TestCompositor::GetClientPrimarySelection()
    {
        ClientSelectionInfo info;
        Run([&] {
            info = state_->primaryInfo;
            if (state_->clientPrimary != nullptr)
            {
                info.mimeTypes = state_->clientPrimary->mimeTypes;
            }
        });
        return info;
    }

    int TestCompositor::RequestClientPrimarySelection(const std::string& mimeType)
    {
        int readEnd = -1;
#if defined(CNA_WAYLAND_HAVE_PRIMARY_SELECTION)
        Run([&] {
            State& s = *state_;
            if (s.clientPrimary == nullptr)
            {
                return;
            }
            int pipes[2];
            if (::pipe2(pipes, O_CLOEXEC) != 0)
            {
                return;
            }
            zwp_primary_selection_source_v1_send_send(s.clientPrimary->resource, mimeType.c_str(), pipes[1]);
            ::close(pipes[1]);
            readEnd = pipes[0];
        });
#else
        (void) mimeType;
#endif
        return readEnd;
    }

    void TestCompositor::DragEnter(const int toplevel, const double x, const double y,
                                   const std::map<std::string, std::vector<std::uint8_t>>& data,
                                   const std::uint32_t sourceActions)
    {
        Run([&] {
            State& s = *state_;
            State::Toplevel* top = s.ToplevelAt(toplevel);
            if (s.dataDevices.empty() || top->xdg == nullptr || top->xdg->surface == nullptr)
            {
                throw std::logic_error("DragEnter without a data device or a surface");
            }
            wl_resource* device = s.dataDevices.front();
            State::Offer* offer = s.CreateOffer(device, false, true);
            offer->payload = data;
            wl_resource_set_implementation(offer->resource, &State::kDataOfferImplementation, offer, &State::DestroyOffer);
            s.dragOffer = offer;
            s.dragDevice = device;
            s.dragSurface = top->xdg->surface;
            s.dragSourceActions = sourceActions;
            s.dragInfo = {};
            s.dragInfo.alive = true;
            wl_data_device_send_data_offer(device, offer->resource);
            for (const auto& entry : data)
            {
                wl_data_offer_send_offer(offer->resource, entry.first.c_str());
            }
            if (wl_resource_get_version(offer->resource) >= WL_DATA_OFFER_SOURCE_ACTIONS_SINCE_VERSION)
            {
                wl_data_offer_send_source_actions(offer->resource, sourceActions);
            }
            wl_data_device_send_enter(device, s.NextSerial(), s.dragSurface->resource, wl_fixed_from_double(x),
                                      wl_fixed_from_double(y), offer->resource);
        });
    }

    void TestCompositor::DragMotion(const double x, const double y)
    {
        Run([&] {
            State& s = *state_;
            if (s.dragDevice != nullptr)
            {
                wl_data_device_send_motion(s.dragDevice, NowMs(), wl_fixed_from_double(x), wl_fixed_from_double(y));
            }
        });
    }

    void TestCompositor::Drop()
    {
        Run([&] {
            State& s = *state_;
            if (s.dragDevice != nullptr && s.dragOffer != nullptr)
            {
                s.dragOffer->dropped = true;
                wl_data_device_send_drop(s.dragDevice);
            }
        });
    }

    void TestCompositor::DragLeave()
    {
        Run([&] {
            State& s = *state_;
            if (s.dragDevice != nullptr)
            {
                wl_data_device_send_leave(s.dragDevice);
            }
        });
    }

    DragInfo TestCompositor::GetDragInfo()
    {
        DragInfo info;
        Run([&] { info = state_->dragInfo; });
        return info;
    }

    // --- text input ------------------------------------------------------------------------------

    void TestCompositor::TextInputEnter(const int toplevel)
    {
#if defined(CNA_WAYLAND_HAVE_TEXT_INPUT_V3)
        Run([&] {
            State& s = *state_;
            State::Toplevel* top = s.ToplevelAt(toplevel);
            if (top->xdg == nullptr || top->xdg->surface == nullptr)
            {
                return;
            }
            s.textInputFocus = top->xdg->surface;
            for (State::TextInput* input : s.textInputs)
            {
                zwp_text_input_v3_send_enter(input->resource, s.textInputFocus->resource);
            }
        });
#else
        (void) toplevel;
#endif
    }

    void TestCompositor::TextInputLeave()
    {
#if defined(CNA_WAYLAND_HAVE_TEXT_INPUT_V3)
        Run([&] {
            State& s = *state_;
            if (s.textInputFocus == nullptr)
            {
                return;
            }
            for (State::TextInput* input : s.textInputs)
            {
                zwp_text_input_v3_send_leave(input->resource, s.textInputFocus->resource);
            }
            s.textInputFocus = nullptr;
        });
#endif
    }

    void TestCompositor::TextInputPreedit(const std::string& text, const std::int32_t cursorBegin, const std::int32_t cursorEnd)
    {
#if defined(CNA_WAYLAND_HAVE_TEXT_INPUT_V3)
        Run([&] {
            for (State::TextInput* input : state_->textInputs)
            {
                zwp_text_input_v3_send_preedit_string(input->resource, text.empty() ? nullptr : text.c_str(), cursorBegin,
                                                      cursorEnd);
            }
        });
#else
        (void) text;
        (void) cursorBegin;
        (void) cursorEnd;
#endif
    }

    void TestCompositor::TextInputCommit(const std::string& text)
    {
#if defined(CNA_WAYLAND_HAVE_TEXT_INPUT_V3)
        Run([&] {
            for (State::TextInput* input : state_->textInputs)
            {
                zwp_text_input_v3_send_commit_string(input->resource, text.c_str());
            }
        });
#else
        (void) text;
#endif
    }

    void TestCompositor::TextInputDone()
    {
#if defined(CNA_WAYLAND_HAVE_TEXT_INPUT_V3)
        Run([&] {
            for (State::TextInput* input : state_->textInputs)
            {
                zwp_text_input_v3_send_done(input->resource, input->commits);
            }
        });
#endif
    }

    TextInputInfo TestCompositor::GetTextInputInfo()
    {
        TextInputInfo info;
        Run([&] {
            State& s = *state_;
            info.objects = static_cast<int>(s.textInputs.size());
            if (!s.textInputs.empty())
            {
                const State::TextInput* input = s.textInputs.front();
                info.enabled = input->current.enabled;
                info.commits = input->commits;
                info.hint = input->current.hint;
                info.purpose = input->current.purpose;
                info.cursorX = input->current.cx;
                info.cursorY = input->current.cy;
                info.cursorWidth = input->current.cw;
                info.cursorHeight = input->current.ch;
            }
        });
        return info;
    }

    // --- desktop integration -----------------------------------------------------------------------

    int TestCompositor::GetIdleInhibitorCount()
    {
        int count = 0;
        Run([&] { count = state_->idleInhibitors; });
        return count;
    }

    int TestCompositor::GetActivationTokenCount()
    {
        int count = 0;
        Run([&] { count = state_->activationTokens; });
        return count;
    }

    std::vector<std::string> TestCompositor::GetActivations()
    {
        std::vector<std::string> result;
        Run([&] { result = state_->activations; });
        return result;
    }

    int TestCompositor::GetExportCount()
    {
        int count = 0;
        Run([&] { count = state_->exports; });
        return count;
    }

} // namespace CNA::Platform::Wayland::Testing

#endif
