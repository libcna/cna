// SPDX-License-Identifier: MS-PL

#include "WaylandTablet.hpp"

#include <algorithm>

namespace CNA::Platform::Wayland {

#if defined(CNA_WAYLAND_HAVE_TABLET)

    struct WaylandTablet::Tool
    {
        WaylandTablet* owner = nullptr;
        zwp_tablet_tool_v2* proxy = nullptr;
        wl_seat* seat = nullptr;
        std::uint64_t id = 0;
        std::string name = "tablet tool";
        std::uint32_t type = 0;
        bool hasPressure = false;

        WindowId focus = 0;
        /// The tablet the current proximity named: the one whose unplugging ends this stroke.
        zwp_tablet_v2* tablet = nullptr;
        // The frame in progress: a tool reports position, pressure and tip state as separate
        // events and `frame` is when they take effect together.
        double x = 0.0;
        double y = 0.0;
        double pressure = 1.0;
        bool moved = false;
        bool pressureChanged = false;
        bool tipDown = false;
        bool tipChanged = false;

        // What the contract has been told.
        bool touching = false;
        double lastX = 0.0;
        double lastY = 0.0;
    };

    struct WaylandTablet::Seat
    {
        wl_seat* seat = nullptr;
        std::uint32_t name = 0;
        zwp_tablet_seat_v2* proxy = nullptr;
        std::vector<zwp_tablet_v2*> tablets;
    };

    namespace {

        /// The tool types the protocol names, for input-device enumeration.
        const char* ToolTypeName(const std::uint32_t type)
        {
            switch (type)
            {
                case ZWP_TABLET_TOOL_V2_TYPE_PEN: return "pen";
                case ZWP_TABLET_TOOL_V2_TYPE_ERASER: return "eraser";
                case ZWP_TABLET_TOOL_V2_TYPE_BRUSH: return "brush";
                case ZWP_TABLET_TOOL_V2_TYPE_PENCIL: return "pencil";
                case ZWP_TABLET_TOOL_V2_TYPE_AIRBRUSH: return "airbrush";
                case ZWP_TABLET_TOOL_V2_TYPE_FINGER: return "finger";
                case ZWP_TABLET_TOOL_V2_TYPE_MOUSE: return "tablet mouse";
                case ZWP_TABLET_TOOL_V2_TYPE_LENS: return "lens";
                default: return "tablet tool";
            }
        }

    } // namespace

    WaylandTablet::WaylandTablet(Host host) : host_(std::move(host)) {}

    WaylandTablet::~WaylandTablet()
    {
        while (!seats_.empty())
        {
            DetachSeat(seats_.back()->seat);
        }
    }

    void WaylandTablet::AttachSeat(void* manager, wl_seat* seat, const std::uint32_t seatName)
    {
        if (manager == nullptr || seat == nullptr)
        {
            return;
        }
        auto owned = std::make_unique<Seat>();
        owned->seat = seat;
        owned->name = seatName;
        owned->proxy = zwp_tablet_manager_v2_get_tablet_seat(static_cast<zwp_tablet_manager_v2*>(manager), seat);
        Seat& added = *owned;
        seats_.push_back(std::move(owned));

        static const zwp_tablet_v2_listener tabletListener = {
            .name = [](void*, zwp_tablet_v2*, const char*) {},
            .id = [](void*, zwp_tablet_v2*, std::uint32_t, std::uint32_t) {},
            .path = [](void*, zwp_tablet_v2*, const char*) {},
            .done = [](void*, zwp_tablet_v2*) {},
            .removed = [](void* data, zwp_tablet_v2* tablet) {
                // A tablet unplugged. A stroke drawn on it ends as a cancellation rather than
                // staying down, even if the compositor sent no proximity_out first.
                auto* self = static_cast<WaylandTablet*>(data);
                for (const auto& tool : self->tools_)
                {
                    if (tool->tablet == tablet)
                    {
                        self->Cancel(*tool);
                        tool->tablet = nullptr;
                        tool->focus = 0;
                    }
                }
                for (const auto& seat : self->seats_)
                {
                    if (std::erase(seat->tablets, tablet) > 0)
                    {
                        zwp_tablet_v2_destroy(tablet);
                        return;
                    }
                }
            },
            .bustype = [](void*, zwp_tablet_v2*, std::uint32_t) {},
        };

        static const zwp_tablet_tool_v2_listener toolListener = {
            .type = [](void* data, zwp_tablet_tool_v2*, const std::uint32_t type) {
                auto* tool = static_cast<Tool*>(data);
                tool->type = type;
                tool->name = ToolTypeName(type);
            },
            .hardware_serial = [](void*, zwp_tablet_tool_v2*, std::uint32_t, std::uint32_t) {},
            .hardware_id_wacom = [](void*, zwp_tablet_tool_v2*, std::uint32_t, std::uint32_t) {},
            .capability = [](void* data, zwp_tablet_tool_v2*, const std::uint32_t capability) {
                if (capability == ZWP_TABLET_TOOL_V2_CAPABILITY_PRESSURE)
                {
                    static_cast<Tool*>(data)->hasPressure = true;
                }
            },
            .done = [](void*, zwp_tablet_tool_v2*) {},
            .removed = [](void* data, zwp_tablet_tool_v2*) {
                auto* tool = static_cast<Tool*>(data);
                WaylandTablet* owner = tool->owner;
                owner->Cancel(*tool);
                zwp_tablet_tool_v2_destroy(tool->proxy);
                std::erase_if(owner->tools_, [tool](const auto& candidate) { return candidate.get() == tool; });
            },
            .proximity_in = [](void* data, zwp_tablet_tool_v2*, const std::uint32_t serial, zwp_tablet_v2* tablet,
                               wl_surface* surface) {
                auto* tool = static_cast<Tool*>(data);
                WaylandTablet* owner = tool->owner;
                if (owner->host_.recordSerial) { owner->host_.recordSerial(tool->seat, serial); }
                tool->tablet = tablet;
                tool->focus = owner->host_.resolveSurface && surface != nullptr ? owner->host_.resolveSurface(surface) : 0;
            },
            .proximity_out = [](void* data, zwp_tablet_tool_v2*) {
                auto* tool = static_cast<Tool*>(data);
                // A tool taken away from the tablet mid-stroke: the contact is cancelled, never
                // left down (the same rule as a touchscreen's cancel).
                tool->owner->Cancel(*tool);
                tool->tablet = nullptr;
                tool->focus = 0;
            },
            .down = [](void* data, zwp_tablet_tool_v2*, const std::uint32_t serial) {
                auto* tool = static_cast<Tool*>(data);
                if (tool->owner->host_.recordSerial) { tool->owner->host_.recordSerial(tool->seat, serial); }
                tool->tipDown = true;
                tool->tipChanged = true;
            },
            .up = [](void* data, zwp_tablet_tool_v2*) {
                auto* tool = static_cast<Tool*>(data);
                tool->tipDown = false;
                tool->tipChanged = true;
            },
            .motion = [](void* data, zwp_tablet_tool_v2*, const wl_fixed_t x, const wl_fixed_t y) {
                auto* tool = static_cast<Tool*>(data);
                tool->x = wl_fixed_to_double(x);
                tool->y = wl_fixed_to_double(y);
                tool->moved = true;
            },
            .pressure = [](void* data, zwp_tablet_tool_v2*, const std::uint32_t pressure) {
                auto* tool = static_cast<Tool*>(data);
                // 0..65535 over the tool's range.
                tool->pressure = static_cast<double>(pressure) / 65535.0;
                tool->pressureChanged = true;
            },
            .distance = [](void*, zwp_tablet_tool_v2*, std::uint32_t) {},
            .tilt = [](void*, zwp_tablet_tool_v2*, wl_fixed_t, wl_fixed_t) {},
            .rotation = [](void*, zwp_tablet_tool_v2*, wl_fixed_t) {},
            .slider = [](void*, zwp_tablet_tool_v2*, std::int32_t) {},
            .wheel = [](void*, zwp_tablet_tool_v2*, wl_fixed_t, std::int32_t) {},
            .button = [](void*, zwp_tablet_tool_v2*, std::uint32_t, std::uint32_t, std::uint32_t) {},
            .frame = [](void* data, zwp_tablet_tool_v2*, std::uint32_t) {
                auto* tool = static_cast<Tool*>(data);
                WaylandTablet* owner = tool->owner;
                const bool moved = tool->moved;
                const bool pressureChanged = tool->pressureChanged;
                tool->tipChanged = false;
                tool->moved = false;
                tool->pressureChanged = false;
                if (tool->focus == 0 || !owner->host_.post)
                {
                    return;
                }
                // A hovering tool is nothing: only a tip that is down is a contact (X11-0155).
                if (!tool->tipDown && !tool->touching)
                {
                    tool->lastX = tool->x;
                    tool->lastY = tool->y;
                    return;
                }
                int width = 1;
                int height = 1;
                if (owner->host_.clientSize) { owner->host_.clientSize(tool->focus, width, height); }
                width = std::max(1, width);
                height = std::max(1, height);

                TouchEvent event;
                event.window = tool->focus;
                event.fingerId = tool->id;
                event.x = static_cast<float>(tool->x / width);
                event.y = static_cast<float>(tool->y / height);
                event.pressure = static_cast<float>(tool->hasPressure ? tool->pressure : 1.0);
                event.clientWidth = width;
                event.clientHeight = height;
                if (tool->tipDown && !tool->touching)
                {
                    tool->touching = true;
                    event.kind = TouchEventKind::Down;
                }
                else if (!tool->tipDown && tool->touching)
                {
                    tool->touching = false;
                    event.kind = TouchEventKind::Up;
                    event.pressure = 0.0f;
                }
                else if (moved || pressureChanged)
                {
                    // Pressure that changes while the tip stays put is a motion too: a drawing
                    // program reads the pressure of a stroke from the contact it already has.
                    event.kind = TouchEventKind::Motion;
                    event.deltaX = static_cast<float>((tool->x - tool->lastX) / width);
                    event.deltaY = static_cast<float>((tool->y - tool->lastY) / height);
                }
                else
                {
                    return;  // a frame that changed nothing the contract carries (tilt, a button)
                }
                tool->lastX = tool->x;
                tool->lastY = tool->y;
                owner->host_.post(event);
            },
        };

        static const zwp_tablet_seat_v2_listener seatListener = {
            .tablet_added = [](void* data, zwp_tablet_seat_v2* proxy, zwp_tablet_v2* tablet) {
                auto* self = static_cast<WaylandTablet*>(data);
                for (const auto& seat : self->seats_)
                {
                    if (seat->proxy == proxy)
                    {
                        seat->tablets.push_back(tablet);
                    }
                }
                zwp_tablet_v2_add_listener(tablet, &tabletListener, self);
            },
            .tool_added = [](void* data, zwp_tablet_seat_v2* proxy, zwp_tablet_tool_v2* proxyTool) {
                auto* self = static_cast<WaylandTablet*>(data);
                wl_seat* seat = nullptr;
                std::uint32_t seatName = 0;
                for (const auto& candidate : self->seats_)
                {
                    if (candidate->proxy == proxy)
                    {
                        seat = candidate->seat;
                        seatName = candidate->name;
                    }
                }
                auto tool = std::make_unique<Tool>();
                tool->owner = self;
                tool->proxy = proxyTool;
                tool->seat = seat;
                // The seat's global name in the upper half, a counter in the lower: two seats'
                // tablets never share a contact id, as touchscreens do not (WAYLAND-0058).
                tool->id = (static_cast<std::uint64_t>(seatName) << 40) | (0x20u << 32) |
                           static_cast<std::uint64_t>(self->tools_.size() + 1);
                Tool& added = *tool;
                self->tools_.push_back(std::move(tool));
                zwp_tablet_tool_v2_add_listener(proxyTool, &toolListener, &added);
            },
            .pad_added = [](void*, zwp_tablet_seat_v2*, zwp_tablet_pad_v2* pad) {
                // The pad's rings, strips and buttons carry nothing the contract has; the object
                // is destroyed at once so the compositor does not keep sending its events.
                zwp_tablet_pad_v2_destroy(pad);
            },
        };
        zwp_tablet_seat_v2_add_listener(added.proxy, &seatListener, this);
    }

    void WaylandTablet::DetachSeat(wl_seat* seat)
    {
        const auto found = std::find_if(seats_.begin(), seats_.end(),
                                        [seat](const auto& candidate) { return candidate->seat == seat; });
        if (found == seats_.end())
        {
            return;
        }
        for (auto it = tools_.begin(); it != tools_.end();)
        {
            if ((*it)->seat == seat)
            {
                Cancel(**it);
                zwp_tablet_tool_v2_destroy((*it)->proxy);
                it = tools_.erase(it);
            }
            else
            {
                ++it;
            }
        }
        for (zwp_tablet_v2* tablet : (*found)->tablets)
        {
            zwp_tablet_v2_destroy(tablet);
        }
        if ((*found)->proxy != nullptr)
        {
            zwp_tablet_seat_v2_destroy((*found)->proxy);
        }
        seats_.erase(found);
    }

    void WaylandTablet::ForgetWindow(const WindowId window)
    {
        for (const auto& tool : tools_)
        {
            if (tool->focus == window)
            {
                Cancel(*tool);
                tool->focus = 0;
            }
        }
    }

    std::vector<std::string> WaylandTablet::GetToolNames() const
    {
        std::vector<std::string> names;
        names.reserve(tools_.size());
        for (const auto& tool : tools_)
        {
            names.push_back(tool->name);
        }
        return names;
    }

    void WaylandTablet::Cancel(Tool& tool)
    {
        if (!tool.touching)
        {
            return;
        }
        tool.touching = false;
        tool.tipDown = false;
        if (tool.focus == 0 || !host_.post)
        {
            return;
        }
        int width = 1;
        int height = 1;
        if (host_.clientSize) { host_.clientSize(tool.focus, width, height); }
        TouchEvent event;
        event.window = tool.focus;
        event.fingerId = tool.id;
        event.kind = TouchEventKind::Cancelled;
        event.x = static_cast<float>(tool.x / std::max(1, width));
        event.y = static_cast<float>(tool.y / std::max(1, height));
        event.clientWidth = std::max(1, width);
        event.clientHeight = std::max(1, height);
        host_.post(event);
    }

#else  // no tablet-v2 in this build's wayland-protocols

    struct WaylandTablet::Tool
    {
    };
    struct WaylandTablet::Seat
    {
    };

    WaylandTablet::WaylandTablet(Host host) : host_(std::move(host)) {}
    WaylandTablet::~WaylandTablet() = default;

    void WaylandTablet::AttachSeat(void* manager, wl_seat* seat, const std::uint32_t seatName)
    {
        (void) manager;
        (void) seat;
        (void) seatName;
    }

    void WaylandTablet::DetachSeat(wl_seat* seat) { (void) seat; }
    void WaylandTablet::ForgetWindow(const WindowId window) { (void) window; }
    std::vector<std::string> WaylandTablet::GetToolNames() const { return {}; }
    void WaylandTablet::Cancel(Tool& tool) { (void) tool; }

#endif

} // namespace CNA::Platform::Wayland
