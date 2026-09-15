// SPDX-License-Identifier: MS-PL

#include "WaylandOutputs.hpp"

#include <algorithm>

namespace CNA::Platform::Wayland {

    namespace {

        /// Whether a wl_output transform turns the mode a quarter: 90, 270 and their flipped forms.
        bool TransformSwapsAxes(const int transform)
        {
            return transform == WL_OUTPUT_TRANSFORM_90 || transform == WL_OUTPUT_TRANSFORM_270 ||
                   transform == WL_OUTPUT_TRANSFORM_FLIPPED_90 || transform == WL_OUTPUT_TRANSFORM_FLIPPED_270;
        }

    } // namespace

    float OutputContentScale(const WaylandOutputState& state)
    {
        if (state.hasLogical && state.logicalWidth > 0 && state.modeWidth > 0)
        {
            const int pixels = TransformSwapsAxes(state.transform) ? state.modeHeight : state.modeWidth;
            const float scale = static_cast<float>(pixels) / static_cast<float>(state.logicalWidth);
            return std::clamp(scale, 0.25f, 16.0f);
        }
        return static_cast<float>(std::max(1, state.scale));
    }

    const wl_output_listener WaylandOutput::kListener = {
        .geometry =
            [](void* data, wl_output*, const std::int32_t x, const std::int32_t y, const std::int32_t physicalWidth,
               const std::int32_t physicalHeight, std::int32_t, const char* make, const char* model,
               const std::int32_t transform) {
                auto* self = static_cast<WaylandOutput*>(data);
                self->pending_.x = x;
                self->pending_.y = y;
                self->pending_.physicalWidth = physicalWidth;
                self->pending_.physicalHeight = physicalHeight;
                self->pending_.make = make != nullptr ? make : "";
                self->pending_.model = model != nullptr ? model : "";
                self->pending_.transform = transform;
            },
        .mode =
            [](void* data, wl_output*, const std::uint32_t flags, const std::int32_t width, const std::int32_t height,
               const std::int32_t refresh) {
                auto* self = static_cast<WaylandOutput*>(data);
                // A compositor re-sends the whole mode list when it sends any of it; one that
                // changes only the scale sends none, and the previous list stands.
                if (self->restartModes_)
                {
                    self->pending_.modes.clear();
                    self->restartModes_ = false;
                }
                DisplayMode mode;
                mode.width = width;
                mode.height = height;
                mode.refreshRate = static_cast<float>(refresh) / 1000.0f;
                const auto same = [&mode](const DisplayMode& other) {
                    return other.width == mode.width && other.height == mode.height &&
                           other.refreshRate == mode.refreshRate;
                };
                if (std::find_if(self->pending_.modes.begin(), self->pending_.modes.end(), same) ==
                    self->pending_.modes.end())
                {
                    self->pending_.modes.push_back(mode);
                }
                if ((flags & WL_OUTPUT_MODE_CURRENT) != 0)
                {
                    self->pending_.modeWidth = width;
                    self->pending_.modeHeight = height;
                    self->pending_.refreshMilliHz = refresh;
                }
            },
        .done = [](void* data, wl_output*) { static_cast<WaylandOutput*>(data)->Done(); },
        .scale = [](void* data, wl_output*, const std::int32_t factor) {
            static_cast<WaylandOutput*>(data)->pending_.scale = factor > 0 ? factor : 1;
        },
        .name = [](void* data, wl_output*, const char* name) {
            static_cast<WaylandOutput*>(data)->pending_.name = name != nullptr ? name : "";
        },
        .description = [](void* data, wl_output*, const char* description) {
            static_cast<WaylandOutput*>(data)->pending_.description = description != nullptr ? description : "";
        },
    };

#if defined(CNA_WAYLAND_HAVE_XDG_OUTPUT)
    const zxdg_output_v1_listener WaylandOutput::kXdgListener = {
        .logical_position = [](void* data, zxdg_output_v1*, const std::int32_t x, const std::int32_t y) {
            auto* self = static_cast<WaylandOutput*>(data);
            self->pending_.logicalX = x;
            self->pending_.logicalY = y;
            self->pending_.hasLogical = true;
        },
        .logical_size = [](void* data, zxdg_output_v1*, const std::int32_t width, const std::int32_t height) {
            auto* self = static_cast<WaylandOutput*>(data);
            self->pending_.logicalWidth = width;
            self->pending_.logicalHeight = height;
            self->pending_.hasLogical = true;
        },
        .done = [](void* data, zxdg_output_v1* xdg) {
            // From xdg-output v3 the logical geometry is applied by wl_output.done with everything
            // else, and this event is deprecated; before v3 it is the only signal that the logical
            // geometry changed, and the compositor may send no wl_output.done with it.
            if (zxdg_output_v1_get_version(xdg) < 3)
            {
                static_cast<WaylandOutput*>(data)->Done();
            }
        },
        .name = [](void* data, zxdg_output_v1*, const char* name) {
            auto* self = static_cast<WaylandOutput*>(data);
            if (self->pending_.name.empty() && name != nullptr) { self->pending_.name = name; }
        },
        .description = [](void* data, zxdg_output_v1*, const char* description) {
            auto* self = static_cast<WaylandOutput*>(data);
            if (self->pending_.description.empty() && description != nullptr)
            {
                self->pending_.description = description;
            }
        },
    };
#endif

    WaylandOutput::WaylandOutput(wl_registry* registry, const std::uint32_t name, const std::uint32_t version,
                                 std::function<void()> onChanged)
        : globalName_(name), version_(version), onChanged_(std::move(onChanged))
    {
        output_ = static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface, version));
        wl_output_add_listener(output_, &kListener, this);
    }

    WaylandOutput::~WaylandOutput()
    {
#if defined(CNA_WAYLAND_HAVE_XDG_OUTPUT)
        if (xdgOutput_ != nullptr)
        {
            zxdg_output_v1_destroy(static_cast<zxdg_output_v1*>(xdgOutput_));
            xdgOutput_ = nullptr;
        }
#endif
        if (output_ != nullptr)
        {
            // v3 has a destructor request; before it the proxy is only dropped.
            if (version_ >= WL_OUTPUT_RELEASE_SINCE_VERSION)
            {
                wl_output_release(output_);
            }
            else
            {
                wl_output_destroy(output_);
            }
            output_ = nullptr;
        }
    }

    void WaylandOutput::AttachXdgOutput(void* manager)
    {
#if defined(CNA_WAYLAND_HAVE_XDG_OUTPUT)
        if (manager == nullptr || xdgOutput_ != nullptr)
        {
            return;
        }
        auto* xdg = zxdg_output_manager_v1_get_xdg_output(static_cast<zxdg_output_manager_v1*>(manager), output_);
        zxdg_output_v1_add_listener(xdg, &kXdgListener, this);
        xdgOutput_ = xdg;
#else
        (void) manager;
#endif
    }

    void WaylandOutput::Done()
    {
        // `done` is the atomic point: geometry, mode, scale and the xdg-output logical geometry
        // that arrived before it describe one consistent state.
        const bool changed = !described_ || pending_.modeWidth != state_.modeWidth ||
                             pending_.modeHeight != state_.modeHeight || pending_.scale != state_.scale ||
                             pending_.logicalX != state_.logicalX || pending_.logicalY != state_.logicalY ||
                             pending_.logicalWidth != state_.logicalWidth ||
                             pending_.logicalHeight != state_.logicalHeight || pending_.transform != state_.transform ||
                             pending_.name != state_.name;
        state_ = pending_;
        restartModes_ = true;
        described_ = true;
        if (changed && onChanged_)
        {
            onChanged_();
        }
    }

    DisplayInfo WaylandOutput::ToDisplayInfo() const
    {
        DisplayInfo info;
        info.id = globalName_;
        info.name = !state_.description.empty() ? state_.description
                    : !state_.name.empty()      ? state_.name
                                                : state_.make + " " + state_.model;
        const float scale = OutputContentScale(state_);
        if (state_.hasLogical)
        {
            info.x = state_.logicalX;
            info.y = state_.logicalY;
            info.width = state_.logicalWidth;
            info.height = state_.logicalHeight;
        }
        else
        {
            const bool swap = TransformSwapsAxes(state_.transform);
            info.x = state_.x;
            info.y = state_.y;
            info.width = static_cast<int>((swap ? state_.modeHeight : state_.modeWidth) / std::max(1, state_.scale));
            info.height = static_cast<int>((swap ? state_.modeWidth : state_.modeHeight) / std::max(1, state_.scale));
        }
        info.contentScale = scale;
        info.desktopMode.width = state_.modeWidth;
        info.desktopMode.height = state_.modeHeight;
        info.desktopMode.refreshRate = static_cast<float>(state_.refreshMilliHz) / 1000.0f;
        return info;
    }

    std::vector<DisplayInfo> WaylandDisplays::GetDisplays() const
    {
        std::vector<DisplayInfo> displays;
        for (const WaylandOutput* output : source_.outputs())
        {
            displays.push_back(output->ToDisplayInfo());
        }
        return displays;
    }

    bool WaylandDisplays::TryGetDisplayForWindow(const IPlatformWindow& window, DisplayInfo& display) const
    {
        const WaylandOutput* output = source_.outputForWindow ? source_.outputForWindow(window) : nullptr;
        if (output == nullptr)
        {
            return false;
        }
        display = output->ToDisplayInfo();
        return true;
    }

    bool WaylandDisplays::TryGetSafeAreaForWindow(const IPlatformWindow& window, WindowBounds& safeArea) const
    {
        const WindowBounds bounds = window.GetClientBounds();
        safeArea = {0, 0, bounds.width, bounds.height};
        return true;
    }

    std::vector<DisplayMode> WaylandDisplays::GetDisplayModes(const std::uint32_t displayId) const
    {
        for (const WaylandOutput* output : source_.outputs())
        {
            if (output->GetGlobalName() == displayId)
            {
                return output->GetState().modes;
            }
        }
        return {};
    }

    bool WaylandDisplays::TryGetCurrentDisplayMode(const std::uint32_t displayId, DisplayMode& mode) const
    {
        for (const WaylandOutput* output : source_.outputs())
        {
            if (output->GetGlobalName() == displayId)
            {
                mode = output->ToDisplayInfo().desktopMode;
                return true;
            }
        }
        return false;
    }

    bool WaylandDisplays::IsScreenSaverEnabled() const
    {
        return source_.screenSaverEnabled ? source_.screenSaverEnabled() : true;
    }

    void WaylandDisplays::SetScreenSaverEnabled(const bool enabled)
    {
        if (source_.setScreenSaverEnabled)
        {
            source_.setScreenSaverEnabled(enabled);
        }
    }

} // namespace CNA::Platform::Wayland
