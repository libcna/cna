// SPDX-License-Identifier: MS-PL

#include "WaylandWindow.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "WaylandConnection.hpp"
#include "WaylandFrame.hpp"
#include "WaylandOutputs.hpp"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <fstream>

namespace CNA::Platform::Wayland {

    namespace {

        /// How long Show and CreateWindow wait for the compositor's first configure. A compositor
        /// that has not answered in this time is stuck, and a window no renderer can safely draw
        /// into is not a window.
        constexpr std::chrono::milliseconds kInitialConfigureTimeout{5000};

        /// How long Sync waits for the compositor to answer the requests made so far.
        constexpr std::chrono::milliseconds kSyncTimeout{1000};

        /// The application id a compositor matches a `.desktop` file and a window's icon by: the
        /// executable's name, which is what a desktop file's `StartupWMClass` names for a program
        /// that sets nothing else.
        std::string ApplicationId()
        {
            std::string name;
            std::ifstream comm("/proc/self/comm");
            std::getline(comm, name);
            return name.empty() ? std::string("cna") : name;
        }

    } // namespace

    const wl_surface_listener WaylandWindow::kSurfaceListener = {
        .enter = [](void* data, wl_surface*, wl_output* output) {
            auto* self = static_cast<WaylandWindow*>(data);
            if (std::find(self->enteredOutputs_.begin(), self->enteredOutputs_.end(), output) ==
                self->enteredOutputs_.end())
            {
                self->enteredOutputs_.push_back(output);
            }
            self->UpdateCurrentOutput();
            self->UpdateScale();
        },
        .leave = [](void* data, wl_surface*, wl_output* output) {
            auto* self = static_cast<WaylandWindow*>(data);
            std::erase(self->enteredOutputs_, output);
            self->UpdateCurrentOutput();
            self->UpdateScale();
        },
        .preferred_buffer_scale = [](void* data, wl_surface*, const std::int32_t factor) {
            // wl_compositor v6: the compositor's own integer scale for this surface, which beats
            // any guess from the outputs it is on.
            auto* self = static_cast<WaylandWindow*>(data);
            self->scaleInputs_.integer = factor > 0 ? factor : 1;
            self->preferredBufferScale_ = true;
            self->UpdateScale();
        },
        .preferred_buffer_transform = [](void*, wl_surface*, std::uint32_t) {
            // A renderer that pre-rotates would care; CNA's always render upright and let the
            // compositor rotate.
        },
    };

    const xdg_surface_listener WaylandWindow::kXdgSurfaceListener = {
        .configure = [](void* data, xdg_surface*, const std::uint32_t serial) {
            static_cast<WaylandWindow*>(data)->ApplyConfigure(serial);
        },
    };

    const xdg_toplevel_listener WaylandWindow::kToplevelListener = {
        .configure = [](void* data, xdg_toplevel*, const std::int32_t width, const std::int32_t height,
                        wl_array* states) {
            auto* self = static_cast<WaylandWindow*>(data);
            self->pendingToplevel_.width = width;
            self->pendingToplevel_.height = height;
            const auto* values = static_cast<const std::uint32_t*>(states->data);
            self->pendingToplevel_.states =
                ParseToplevelStates(std::span<const std::uint32_t>(values, states->size / sizeof(std::uint32_t)));
        },
        .close = [](void* data, xdg_toplevel*) { static_cast<WaylandWindow*>(data)->RequestClose(); },
        .configure_bounds = [](void* data, xdg_toplevel*, const std::int32_t width, const std::int32_t height) {
            // v4: the largest size the compositor would give a window now (a work area). Kept for
            // the built-in frame's maximize; never applied as a size.
            auto* self = static_cast<WaylandWindow*>(data);
            self->bounds_ = {width, height};
        },
        .wm_capabilities = [](void* data, xdg_toplevel*, wl_array* capabilities) {
            // v5: which of minimize, maximize, fullscreen and the window menu the compositor
            // offers; the built-in frame shows only the buttons that will work.
            auto* self = static_cast<WaylandWindow*>(data);
            self->wmCapabilities_ = 0;
            const auto* values = static_cast<const std::uint32_t*>(capabilities->data);
            for (std::size_t index = 0; index < capabilities->size / sizeof(std::uint32_t); ++index)
            {
                self->wmCapabilities_ |= 1u << values[index];
            }
            self->hasWmCapabilities_ = true;
        },
    };

    WaylandWindow::WaylandWindow(WaylandWindowHost& host, const WindowId id, const WindowDescription& description)
        : host_(&host), id_(id)
    {
        WaylandConnection& connection = host.GetConnection();
        const WaylandGlobals& globals = connection.GetGlobals();
        display_ = connection.GetDisplay();

        title_ = description.title;
        appId_ = ApplicationId();
        renderIntent_ = description.renderIntent;
        openGlFramebuffer_ = description.openGlFramebuffer;
        size_ = {std::max(1, description.width), std::max(1, description.height)};
        minimum_ = {std::max(0, description.minimumWidth), std::max(0, description.minimumHeight)};
        maximum_ = {std::max(0, description.maximumWidth), std::max(0, description.maximumHeight)};
        floatingSize_ = size_;
        resizable_ = description.resizable;
        borderless_ = description.borderless;
        highDpi_ = description.highDpi;
        requestedFullscreen_ = description.fullscreenMode;

        surface_ = wl_compositor_create_surface(globals.compositor);
        if (surface_ == nullptr)
        {
            throw PlatformException("WaylandWindow", "wl_compositor.create_surface failed");
        }
        wl_surface_add_listener(surface_, &kSurfaceListener, this);
        host.MapSurface(surface_, id_);

        // The whole surface is opaque, whatever buffer it gets: a back buffer's alpha channel is a
        // renderer's working data, not transparency, and saying so lets the compositor skip
        // blending the window. The region is clipped to the surface, so one large rectangle holds
        // for every size.
        if (wl_region* opaque = wl_compositor_create_region(globals.compositor))
        {
            wl_region_add(opaque, 0, 0, INT32_MAX, INT32_MAX);
            wl_surface_set_opaque_region(surface_, opaque);
            wl_region_destroy(opaque);
        }

#if defined(CNA_WAYLAND_HAVE_VIEWPORTER)
        if (globals.viewporter != nullptr)
        {
            viewport_ = wp_viewporter_get_viewport(globals.viewporter, surface_);
        }
#endif
#if defined(CNA_WAYLAND_HAVE_FRACTIONAL_SCALE)
        if (globals.fractionalScaleManager != nullptr)
        {
            static const wp_fractional_scale_v1_listener fractionalListener = {
                .preferred_scale = [](void* data, wp_fractional_scale_v1*, const std::uint32_t scale) {
                    auto* self = static_cast<WaylandWindow*>(data);
                    self->scaleInputs_.fractional120 = scale;
                    self->UpdateScale();
                },
            };
            fractionalScale_ = wp_fractional_scale_manager_v1_get_fractional_scale(globals.fractionalScaleManager, surface_);
            wp_fractional_scale_v1_add_listener(fractionalScale_, &fractionalListener, this);
        }
#endif

        // A title bar of CNA's own where the compositor draws none (D-22). Decided when the role
        // exists and the compositor has answered the decoration request; created here because
        // its subsurfaces belong to this surface for the window's whole life.
        frame_ = std::make_unique<WaylandFrame>(host, *this);

        scaleInputs_.highDpi = highDpi_;
#if defined(CNA_WAYLAND_HAVE_VIEWPORTER)
        scaleInputs_.viewporter = viewport_ != nullptr;
#endif
        scale_ = DecideScale(scaleInputs_);
        pixelSize_ = {ScaledExtent(size_.width, scale_.scale), ScaledExtent(size_.height, scale_.scale)};
        ApplySurfaceGeometry(false);

        // Shown by the platform once the window is registered (WaylandPlatform::CreateWindow):
        // showing waits for the first configure, and events dispatched while waiting -- a
        // keyboard's enter on the new surface -- must find the window already known.
    }

    WaylandWindow::~WaylandWindow()
    {
        // The platform first: it drops this window from every service, and the GL and Vulkan
        // services destroy what they made from this surface -- a wl_egl_window must go before its
        // wl_surface does.
        if (host_ != nullptr)
        {
            host_->OnWindowDestroyed(*this);
        }
        ReleaseNativeObjects();
    }

    void WaylandWindow::ReleaseNativeObjects()
    {
        pixelSizeListener_ = nullptr;
        frame_.reset();
        // Role objects before the surface (xdg_surface.defunct_role_object otherwise), and the
        // decoration before the toplevel.
#if defined(CNA_WAYLAND_HAVE_XDG_DECORATION)
        if (decoration_ != nullptr)
        {
            zxdg_toplevel_decoration_v1_destroy(decoration_);
            decoration_ = nullptr;
        }
#endif
        if (toplevel_ != nullptr)
        {
            xdg_toplevel_destroy(toplevel_);
            toplevel_ = nullptr;
        }
        if (xdgSurface_ != nullptr)
        {
            xdg_surface_destroy(xdgSurface_);
            xdgSurface_ = nullptr;
        }
#if defined(CNA_WAYLAND_HAVE_FRACTIONAL_SCALE)
        if (fractionalScale_ != nullptr)
        {
            wp_fractional_scale_v1_destroy(fractionalScale_);
            fractionalScale_ = nullptr;
        }
#endif
#if defined(CNA_WAYLAND_HAVE_VIEWPORTER)
        if (viewport_ != nullptr)
        {
            wp_viewport_destroy(viewport_);
            viewport_ = nullptr;
        }
#endif
        if (surface_ != nullptr)
        {
            if (host_ != nullptr)
            {
                host_->MapSurface(surface_, 0);
            }
            wl_surface_destroy(surface_);
            surface_ = nullptr;
        }
        if (host_ != nullptr)
        {
            host_->GetConnection().Flush();
        }
        configureState_ = ConfigureState::NoRole;
    }

    void WaylandWindow::Abandon()
    {
        // The platform is going away before this window, which its contract does not allow; the
        // proxies are destroyed while the display still exists, and the wrapper stays behind
        // inert rather than touching a disconnected display from its destructor.
        ReleaseNativeObjects();
        host_ = nullptr;
    }

    NativeWindowHandle WaylandWindow::GetNativeHandle() const
    {
        NativeWindowHandle handle;
        handle.system = NativeWindowSystem::Wayland;
        handle.display = display_;
        handle.surface = surface_;
        return handle;
    }

    void WaylandWindow::SetTitle(const std::string& title)
    {
        title_ = title;
        if (toplevel_ != nullptr)
        {
            xdg_toplevel_set_title(toplevel_, title_.c_str());
        }
        if (frame_ != nullptr)
        {
            frame_->SetTitle(title_);
        }
        if (host_ != nullptr)
        {
            host_->GetConnection().Flush();
        }
    }

    WindowBounds WaylandWindow::GetClientBounds() const
    {
        // Position is the compositor's and is never told to a client (D-10).
        return {0, 0, size_.width, size_.height};
    }

    WindowSize WaylandWindow::GetPixelSize() const
    {
        return pixelSize_;
    }

    float WaylandWindow::GetDisplayScale() const
    {
        return static_cast<float>(scale_.scale);
    }

    int WaylandWindow::FrameHeight() const
    {
        return frame_ != nullptr ? frame_->GetTitleBarHeight() : 0;
    }

    void WaylandWindow::Post(const WindowEventKind kind, const int data1, const int data2)
    {
        if (host_ == nullptr)
        {
            return;
        }
        WindowEvent event;
        event.window = id_;
        event.kind = kind;
        event.data1 = data1;
        event.data2 = data2;
        host_->PostEvent(event);
    }

    void WaylandWindow::ApplySizeLimits()
    {
        if (toplevel_ == nullptr)
        {
            return;
        }
        // Min and max are window geometry, which includes a built-in title bar.
        const int frame = FrameHeight();
        if (!resizable_)
        {
            xdg_toplevel_set_min_size(toplevel_, size_.width, size_.height + frame);
            xdg_toplevel_set_max_size(toplevel_, size_.width, size_.height + frame);
            return;
        }
        xdg_toplevel_set_min_size(toplevel_, minimum_.width > 0 ? minimum_.width : 0,
                                  minimum_.height > 0 ? minimum_.height + frame : 0);
        xdg_toplevel_set_max_size(toplevel_, maximum_.width > 0 ? maximum_.width : 0,
                                  maximum_.height > 0 ? maximum_.height + frame : 0);
    }

    void WaylandWindow::ApplySurfaceGeometry(const bool notifyListener)
    {
        pixelSize_ = {ScaledExtent(size_.width, scale_.scale), ScaledExtent(size_.height, scale_.scale)};
#if defined(CNA_WAYLAND_HAVE_VIEWPORTER)
        if (viewport_ != nullptr)
        {
            // Always, scale 1 included: the surface's extent is then the logical size whatever
            // buffer a renderer committed last (see the class comment).
            wp_viewport_set_destination(viewport_, size_.width, size_.height);
        }
#endif
        if (host_ != nullptr && host_->GetConnection().GetGlobals().compositorVersion >= 3)
        {
            wl_surface_set_buffer_scale(surface_, scale_.bufferScale);
        }
        if (frame_ != nullptr)
        {
            frame_->Layout(size_.width, size_.height, scale_.scale);
        }
        if (xdgSurface_ != nullptr)
        {
            const int frame = FrameHeight();
            xdg_surface_set_window_geometry(xdgSurface_, 0, -frame, size_.width, size_.height + frame);
        }
        if (notifyListener && pixelSizeListener_)
        {
            pixelSizeListener_(pixelSize_.width, pixelSize_.height);
        }
    }

    void WaylandWindow::UpdateScale()
    {
        if (!preferredBufferScale_ && host_ != nullptr)
        {
            // Without wl_surface.preferred_buffer_scale the integer scale is the largest of the
            // outputs the surface is on, as every compositor before wl_compositor v6 expected.
            int largest = 1;
            for (const wl_output* proxy : enteredOutputs_)
            {
                if (const WaylandOutput* output = host_->FindOutput(proxy))
                {
                    largest = std::max(largest, output->GetState().scale);
                }
            }
            scaleInputs_.integer = largest;
        }
        const ScaleDecision decision = DecideScale(scaleInputs_);
        if (decision.scale == scale_.scale && decision.method == scale_.method &&
            decision.bufferScale == scale_.bufferScale)
        {
            return;
        }
        const WindowSize before = pixelSize_;
        scale_ = decision;
        ApplySurfaceGeometry(true);
        Post(WindowEventKind::DisplayScaleChanged);
        if (pixelSize_.width != before.width || pixelSize_.height != before.height)
        {
            Post(WindowEventKind::PixelSizeChanged, pixelSize_.width, pixelSize_.height);
        }
        CommitState();
    }

    void WaylandWindow::UpdateCurrentOutput()
    {
        // The most recently entered output the surface is still on: where a window spanning two
        // monitors was last moved to.
        const WaylandOutput* now = nullptr;
        if (host_ != nullptr && !enteredOutputs_.empty())
        {
            now = host_->FindOutput(enteredOutputs_.back());
        }
        if (now == currentOutput_)
        {
            return;
        }
        const bool hadOne = currentOutput_ != nullptr;
        currentOutput_ = now;
        if (hadOne && now != nullptr)
        {
            Post(WindowEventKind::DisplayChanged);
        }
    }

    const WaylandOutput* WaylandWindow::GetCurrentOutput() const
    {
        // Re-resolved rather than trusted: an output withdrawn since the last enter is gone.
        if (host_ == nullptr || enteredOutputs_.empty())
        {
            return nullptr;
        }
        return host_->FindOutput(enteredOutputs_.back());
    }

    void WaylandWindow::ForgetOutput(const wl_output* output)
    {
        std::erase(enteredOutputs_, output);
        currentOutput_ = nullptr;
        UpdateCurrentOutput();
        UpdateScale();
    }

    void WaylandWindow::RefreshOutputs()
    {
        UpdateCurrentOutput();
        UpdateScale();
    }

    void WaylandWindow::ShowWindowMenu(const int x, const int y)
    {
        wl_seat* seat = nullptr;
        const std::uint32_t serial = host_ != nullptr ? host_->GetLatestInputSerial(seat) : 0;
        if (toplevel_ != nullptr && seat != nullptr && serial != 0)
        {
            xdg_toplevel_show_window_menu(toplevel_, seat, serial, x, y);
            host_->GetConnection().Flush();
        }
    }

    std::string WaylandWindow::GetDisplayName() const
    {
        const WaylandOutput* output = GetCurrentOutput();
        if (output == nullptr)
        {
            return {};
        }
        return !output->GetState().name.empty() ? output->GetState().name : output->ToDisplayInfo().name;
    }

    void WaylandWindow::CreateRole()
    {
        WaylandConnection& connection = host_->GetConnection();
        const WaylandGlobals& globals = connection.GetGlobals();
        if (globals.wmBase == nullptr)
        {
            throw PlatformException("WaylandWindow::Show", "the compositor withdrew xdg_wm_base");
        }

        // A renderer may have committed buffers to the surface while it had no role (a window
        // created hidden, or one hidden and shown again), and xdg-shell forbids giving a role to
        // a surface with a buffer committed. A null commit clears it.
        wl_surface_attach(surface_, nullptr, 0, 0);
        wl_surface_commit(surface_);

        xdgSurface_ = xdg_wm_base_get_xdg_surface(globals.wmBase, surface_);
        xdg_surface_add_listener(xdgSurface_, &kXdgSurfaceListener, this);
        toplevel_ = xdg_surface_get_toplevel(xdgSurface_);
        xdg_toplevel_add_listener(toplevel_, &kToplevelListener, this);
        xdg_toplevel_set_title(toplevel_, title_.c_str());
        xdg_toplevel_set_app_id(toplevel_, appId_.c_str());

#if defined(CNA_WAYLAND_HAVE_XDG_DECORATION)
        if (globals.decorationManager != nullptr)
        {
            static const zxdg_toplevel_decoration_v1_listener decorationListener = {
                .configure = [](void* data, zxdg_toplevel_decoration_v1*, const std::uint32_t mode) {
                    auto* self = static_cast<WaylandWindow*>(data);
                    self->serverDecorated_ = mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
                    if (self->frame_ != nullptr)
                    {
                        self->frame_->SetServerDecorated(self->serverDecorated_);
                    }
                },
            };
            decoration_ = zxdg_decoration_manager_v1_get_toplevel_decoration(globals.decorationManager, toplevel_);
            zxdg_toplevel_decoration_v1_add_listener(decoration_, &decorationListener, this);
            zxdg_toplevel_decoration_v1_set_mode(decoration_, borderless_ ? ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
                                                                          : ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        }
#endif
        if (frame_ != nullptr)
        {
            frame_->SetEnabled(!borderless_);
        }
        ApplySizeLimits();
        if (maximizeRequested_)
        {
            xdg_toplevel_set_maximized(toplevel_);
        }
        if (requestedFullscreen_ != WindowFullscreenMode::Windowed)
        {
            const WaylandOutput* output = GetCurrentOutput();
            xdg_toplevel_set_fullscreen(toplevel_, output != nullptr ? output->GetProxy() : nullptr);
        }
        ApplySurfaceGeometry(false);

        configureState_ = ConfigureState::AwaitingInitialConfigure;
        // The initial commit: a role, a title, no buffer. The compositor answers it with the
        // first configure.
        wl_surface_commit(surface_);
        connection.Flush();
    }

    void WaylandWindow::DestroyRole()
    {
        if (frame_ != nullptr)
        {
            frame_->SetEnabled(false);
        }
#if defined(CNA_WAYLAND_HAVE_XDG_DECORATION)
        if (decoration_ != nullptr)
        {
            zxdg_toplevel_decoration_v1_destroy(decoration_);
            decoration_ = nullptr;
        }
        serverDecorated_ = false;
#endif
        if (toplevel_ != nullptr)
        {
            xdg_toplevel_destroy(toplevel_);
            toplevel_ = nullptr;
        }
        if (xdgSurface_ != nullptr)
        {
            xdg_surface_destroy(xdgSurface_);
            xdgSurface_ = nullptr;
        }
        if (surface_ != nullptr)
        {
            wl_surface_attach(surface_, nullptr, 0, 0);
            wl_surface_commit(surface_);
        }
        configureState_ = ConfigureState::NoRole;
        // The compositor's view of the window is gone with its role; what the application asked
        // for (maximizeRequested_, requestedFullscreen_) is kept and asked for again on Show.
        maximized_ = false;
        fullscreen_ = false;
        tiled_ = false;
        activated_ = false;
        suspended_ = false;
        pendingToplevel_ = {};
        supersededGeometries_.clear();
    }

    void WaylandWindow::Show()
    {
        if (host_ == nullptr)
        {
            return;
        }
        visible_ = true;
        if (configureState_ != ConfigureState::NoRole)
        {
            return;
        }
        CreateRole();
        WaylandConnection& connection = host_->GetConnection();
        const bool configured = connection.DispatchUntil(
            [this] { return configureState_ == ConfigureState::Configured; }, kInitialConfigureTimeout);
        if (!configured)
        {
            const std::string why = !connection.IsAlive()
                                        ? connection.GetError()
                                        : std::string("the compositor did not configure the window within 5 s");
            DestroyRole();
            visible_ = false;
            throw PlatformException("WaylandWindow::Show", why);
        }
        // A window being shown asks to be the one the user is typing into; the compositor
        // decides (xdg-activation, D-12).
        host_->RequestActivation(*this);
    }

    void WaylandWindow::Hide()
    {
        visible_ = false;
        if (configureState_ == ConfigureState::NoRole || host_ == nullptr)
        {
            return;
        }
        DestroyRole();
        host_->GetConnection().Flush();
    }

    void WaylandWindow::ApplyConfigure(const std::uint32_t serial)
    {
        if (xdgSurface_ == nullptr)
        {
            return;
        }
        const ToplevelStates states = pendingToplevel_.states;
        // The frame first: whether a title bar takes part of the configured height depends on the
        // state this configure brings (none while fullscreen).
        if (frame_ != nullptr)
        {
            frame_->SetState(states.activated, states.maximized, states.fullscreen);
        }
        const bool constrainedNow = states.maximized || states.fullscreen || states.tiled;
        const bool constrainedBefore = maximized_ || fullscreen_ || tiled_;
        // Leaving maximized or fullscreen the compositor usually sends 0x0 ("you choose"): the
        // floating size from before comes back.
        const LogicalSize base = !constrainedNow && constrainedBefore ? floatingSize_ : size_;
        int suggestedWidth = pendingToplevel_.width;
        int suggestedHeight = pendingToplevel_.height;
        if (!constrainedNow && !constrainedBefore && !states.resizing && suggestedWidth > 0 && suggestedHeight > 0)
        {
            // A floating window's configure size is a suggestion, and compositors (GNOME's among
            // them) put the window's current geometry in every configure -- a focus change, say.
            // One sent before the compositor saw a SetSize names the geometry the resize replaced;
            // taking it would silently undo the resize, so it is recognised and ignored. A size the
            // window never had is the compositor asking, and is taken; so is anything while the
            // user drags an edge.
            const int frame = FrameHeight();
            const bool stale = std::any_of(supersededGeometries_.begin(), supersededGeometries_.end(),
                                           [&](const LogicalSize& old) {
                                               return old.width == suggestedWidth && old.height == suggestedHeight;
                                           });
            if (stale)
            {
                suggestedWidth = 0;
                suggestedHeight = 0;
            }
            else if (suggestedWidth == size_.width && suggestedHeight == size_.height + frame)
            {
                supersededGeometries_.clear();  // the compositor has caught up
            }
            else
            {
                supersededGeometries_.clear();  // the compositor's own request wins
            }
        }
        else if (constrainedNow || states.resizing)
        {
            supersededGeometries_.clear();
        }
        LogicalSize resolved = ResolveConfigureSize(suggestedWidth, suggestedHeight, states, base,
                                                    FrameHeight(), minimum_, maximum_);
        if (!constrainedNow && !resizable_)
        {
            // A fixed-size window keeps its size whatever a floating configure suggests; the
            // compositor was told min = max = this size, and one that suggests another is ignored.
            resolved = base;
        }
        if (!constrainedNow)
        {
            floatingSize_ = resolved;
        }

        const bool wasMaximized = maximized_;
        const bool wasSuspended = suspended_;
        const bool focusBefore = HasFocus();
        maximized_ = states.maximized;
        fullscreen_ = states.fullscreen;
        tiled_ = states.tiled;
        activated_ = states.activated;
        suspended_ = host_ != nullptr && host_->GetConnection().GetGlobals().wmBaseVersion >= 6 && states.suspended;

        configuredGeometry_ = {std::max(0, pendingToplevel_.width), std::max(0, pendingToplevel_.height)};
        xdg_surface_ack_configure(xdgSurface_, serial);
        configureState_ = ConfigureState::Configured;
        ++configureCount_;

        const bool sizeChanged = resolved.width != size_.width || resolved.height != size_.height;
        const WindowSize before = pixelSize_;
        size_ = resolved;
        ApplySurfaceGeometry(sizeChanged);

        if (sizeChanged)
        {
            Post(WindowEventKind::Resized, size_.width, size_.height);
        }
        if (pixelSize_.width != before.width || pixelSize_.height != before.height)
        {
            Post(WindowEventKind::PixelSizeChanged, pixelSize_.width, pixelSize_.height);
        }
        if (maximized_ != wasMaximized)
        {
            Post(maximized_ ? WindowEventKind::Maximized : WindowEventKind::Restored);
        }
        if (suspended_ != wasSuspended)
        {
            Post(suspended_ ? WindowEventKind::Minimized : WindowEventKind::Restored);
        }
        // Without a keyboard the activated state is the focus (HasFocus), and it changes here.
        PostFocusChange(focusBefore);
        // Every configure asks for a frame at the state it describes.
        Post(WindowEventKind::Exposed);
        CommitState();
    }

    void WaylandWindow::CommitState()
    {
        // Only a configured window with something drawing into it: before the first configure a
        // commit would be premature, and a window no renderer draws has no state worth showing.
        if (configureState_ == ConfigureState::Configured && hasContent_ && surface_ != nullptr && host_ != nullptr)
        {
            wl_surface_commit(surface_);
            host_->GetConnection().Flush();
        }
    }

    void WaylandWindow::OnContentCommitted()
    {
        hasContent_ = true;
    }

    void WaylandWindow::SetPixelSizeListener(std::function<void(int, int)> listener)
    {
        pixelSizeListener_ = std::move(listener);
    }

    void WaylandWindow::SetSize(const int width, const int height)
    {
        LogicalSize wanted{std::max(1, width), std::max(1, height)};
        if (minimum_.width > 0) { wanted.width = std::max(wanted.width, minimum_.width); }
        if (minimum_.height > 0) { wanted.height = std::max(wanted.height, minimum_.height); }
        if (maximum_.width > 0) { wanted.width = std::min(wanted.width, maximum_.width); }
        if (maximum_.height > 0) { wanted.height = std::min(wanted.height, maximum_.height); }
        floatingSize_ = wanted;
        if (maximized_ || fullscreen_ || tiled_)
        {
            // The compositor's size stands while it constrains the window; this one comes back
            // when the window floats again (D-9).
            return;
        }
        if (wanted.width == size_.width && wanted.height == size_.height)
        {
            return;
        }
        const WindowSize before = pixelSize_;
        if (configureState_ == ConfigureState::Configured)
        {
            // The geometry being replaced, until the compositor shows it has seen the new one.
            supersededGeometries_.push_back({size_.width, size_.height + FrameHeight()});
            if (supersededGeometries_.size() > 8)
            {
                supersededGeometries_.erase(supersededGeometries_.begin());
            }
        }
        size_ = wanted;
        ApplySizeLimits();
        ApplySurfaceGeometry(true);
        Post(WindowEventKind::Resized, size_.width, size_.height);
        if (pixelSize_.width != before.width || pixelSize_.height != before.height)
        {
            Post(WindowEventKind::PixelSizeChanged, pixelSize_.width, pixelSize_.height);
        }
        CommitState();
    }

    void WaylandWindow::SetResizable(const bool resizable)
    {
        resizable_ = resizable;
        ApplySizeLimits();
        CommitState();
    }

    void WaylandWindow::SetBorderless(const bool borderless)
    {
        borderless_ = borderless;
#if defined(CNA_WAYLAND_HAVE_XDG_DECORATION)
        if (decoration_ != nullptr)
        {
            zxdg_toplevel_decoration_v1_set_mode(decoration_, borderless_ ? ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
                                                                          : ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        }
#endif
        if (frame_ != nullptr && configureState_ != ConfigureState::NoRole)
        {
            frame_->SetEnabled(!borderless_);
        }
        ApplySizeLimits();
        if ((maximized_ || fullscreen_ || tiled_) && configuredGeometry_.height > 0)
        {
            // The title bar is inside the window geometry, and a constrained window's geometry is
            // the compositor's: adding or removing the bar resizes the content, never the geometry
            // (a maximized window whose geometry moved by 32 units is invalid_surface_state).
            const LogicalSize before = size_;
            const WindowSize beforePixels = pixelSize_;
            if (configuredGeometry_.width > 0)
            {
                size_.width = configuredGeometry_.width;
            }
            size_.height = std::max(1, configuredGeometry_.height - FrameHeight());
            const bool resized = size_.width != before.width || size_.height != before.height;
            ApplySurfaceGeometry(resized);
            if (resized)
            {
                Post(WindowEventKind::Resized, size_.width, size_.height);
            }
            if (pixelSize_.width != beforePixels.width || pixelSize_.height != beforePixels.height)
            {
                Post(WindowEventKind::PixelSizeChanged, pixelSize_.width, pixelSize_.height);
            }
        }
        else
        {
            ApplySurfaceGeometry(false);
        }
        CommitState();
    }

    void WaylandWindow::SetFullscreenMode(const WindowFullscreenMode mode)
    {
        requestedFullscreen_ = mode;
        if (toplevel_ == nullptr)
        {
            return;
        }
        if (mode == WindowFullscreenMode::Windowed)
        {
            xdg_toplevel_unset_fullscreen(toplevel_);
        }
        else
        {
            // Exclusive is asked for exactly like borderless: an ordinary Wayland client cannot
            // take a display mode (D-11). The output is the one the window is on, so a game goes
            // fullscreen where it is rather than wherever the compositor prefers.
            const WaylandOutput* output = GetCurrentOutput();
            xdg_toplevel_set_fullscreen(toplevel_, output != nullptr ? output->GetProxy() : nullptr);
        }
        expectConfigure_ = true;
        host_->GetConnection().Flush();
    }

    WindowFullscreenMode WaylandWindow::GetFullscreenMode() const
    {
        // What the compositor confirmed, never what was asked: and never "exclusive" (D-11).
        return fullscreen_ ? WindowFullscreenMode::BorderlessFullscreen : WindowFullscreenMode::Windowed;
    }

    void WaylandWindow::Minimize()
    {
        if (toplevel_ != nullptr)
        {
            xdg_toplevel_set_minimized(toplevel_);
            host_->GetConnection().Flush();
        }
    }

    void WaylandWindow::Maximize()
    {
        maximizeRequested_ = true;
        if (toplevel_ != nullptr)
        {
            xdg_toplevel_set_maximized(toplevel_);
            expectConfigure_ = true;
            host_->GetConnection().Flush();
        }
    }

    void WaylandWindow::Restore()
    {
        maximizeRequested_ = false;
        if (toplevel_ == nullptr)
        {
            return;
        }
        if (maximized_)
        {
            xdg_toplevel_unset_maximized(toplevel_);
            expectConfigure_ = true;
        }
        if (fullscreen_)
        {
            requestedFullscreen_ = WindowFullscreenMode::Windowed;
            xdg_toplevel_unset_fullscreen(toplevel_);
            expectConfigure_ = true;
        }
        // A minimized window stays minimized: xdg-shell has no request that undoes
        // set_minimized, and the user brings it back through the compositor (D-12).
        host_->GetConnection().Flush();
    }

    void WaylandWindow::ToggleMaximized()
    {
        if (maximized_)
        {
            Restore();
        }
        else
        {
            Maximize();
        }
    }

    void WaylandWindow::BeginMove()
    {
        wl_seat* seat = nullptr;
        const std::uint32_t serial = host_ != nullptr ? host_->GetLatestInputSerial(seat) : 0;
        if (toplevel_ != nullptr && seat != nullptr && serial != 0)
        {
            xdg_toplevel_move(toplevel_, seat, serial);
            host_->GetConnection().Flush();
        }
    }

    void WaylandWindow::BeginResize(const std::uint32_t edges)
    {
        wl_seat* seat = nullptr;
        const std::uint32_t serial = host_ != nullptr ? host_->GetLatestInputSerial(seat) : 0;
        if (toplevel_ != nullptr && seat != nullptr && serial != 0 && resizable_)
        {
            xdg_toplevel_resize(toplevel_, seat, serial, edges);
            host_->GetConnection().Flush();
        }
    }

    void WaylandWindow::RequestClose()
    {
        Post(WindowEventKind::CloseRequested);
        // As X11 does: the window is not destroyed -- the application answers -- and a QuitEvent
        // follows only when this is the last window, so closing a secondary window does not end
        // the application.
        if (host_ != nullptr && host_->GetWindowCount() <= 1)
        {
            host_->PostEvent(QuitEvent{});
        }
    }

    void WaylandWindow::Sync()
    {
        if (host_ == nullptr)
        {
            return;
        }
        WaylandConnection& connection = host_->GetConnection();
        (void) connection.Roundtrip(kSyncTimeout);
        if (expectConfigure_)
        {
            // A state request (maximize, fullscreen) is answered by a configure that a compositor
            // may send a frame later than the roundtrip's reply; wait for it, bounded.
            const std::uint64_t before = configureCount_;
            (void) connection.DispatchUntil([this, before] { return configureCount_ != before; },
                                            std::chrono::milliseconds(250));
            expectConfigure_ = false;
        }
    }

    bool WaylandWindow::HasFocus() const
    {
        if (keyboardFocusCount_ > 0)
        {
            return true;
        }
        // Without any keyboard (a touch-only kiosk) the compositor's active window is the focus.
        return activated_ && host_ != nullptr && !host_->HasKeyboard();
    }

    void WaylandWindow::OnKeyboardFocus(const bool focused)
    {
        const bool before = HasFocus();
        keyboardFocusCount_ = std::max(0, keyboardFocusCount_ + (focused ? 1 : -1));
        PostFocusChange(before);
    }

    void WaylandWindow::PostFocusChange(const bool before)
    {
        // The events follow HasFocus exactly, so an application that tracks them never
        // disagrees with one that asks -- e.g. when the last keyboard is unplugged from a window
        // the compositor keeps active, which stays focused.
        const bool after = HasFocus();
        if (before != after)
        {
            Post(after ? WindowEventKind::FocusGained : WindowEventKind::FocusLost);
        }
    }

} // namespace CNA::Platform::Wayland
