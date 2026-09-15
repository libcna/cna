// SPDX-License-Identifier: MS-PL

#include "WaylandFrame.hpp"

#include "WaylandConnection.hpp"
#include "WaylandScaling.hpp"
#include "WaylandShm.hpp"
#include "WaylandWindow.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>

#include <linux/input-event-codes.h>

namespace CNA::Platform::Wayland {

    namespace {

        /// How the frame looks: GNOME's dark header bar, roughly, since GNOME is where it shows.
        constexpr std::uint32_t kBackgroundActive = 0xFF2B2B2Bu;
        constexpr std::uint32_t kBackgroundInactive = 0xFF3A3A3Au;
        constexpr std::uint32_t kSeparator = 0xFF161616u;
        constexpr std::uint32_t kButtonHover = 0xFF474747u;
        constexpr std::uint32_t kButtonPressed = 0xFF585858u;
        constexpr std::uint32_t kCloseHover = 0xFFC42B1Cu;
        constexpr std::uint32_t kIconActive = 0xFFE6E6E6u;
        constexpr std::uint32_t kIconInactive = 0xFF9A9A9Au;

        /// How close in time and space two clicks on the title count as a double click.
        constexpr auto kDoubleClick = std::chrono::milliseconds(400);
        constexpr double kDoubleClickSlop = 4.0;

        /// Resize by a corner within this distance of it.
        constexpr int kCorner = 16;

        struct Canvas
        {
            std::uint8_t* pixels;
            int stride;
            int width;
            int height;

            void Fill(const int x0, const int y0, const int x1, const int y1, const std::uint32_t color) const
            {
                for (int y = std::max(0, y0); y < std::min(height, y1); ++y)
                {
                    auto* row = reinterpret_cast<std::uint32_t*>(pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride));
                    for (int x = std::max(0, x0); x < std::min(width, x1); ++x)
                    {
                        row[x] = color;
                    }
                }
            }

            /// A line `thickness` wide from (x0, y0) to (x1, y1), by distance to the segment --
            /// crude, and enough for a close button's cross at any scale.
            void Line(const double x0, const double y0, const double x1, const double y1, const double thickness,
                      const std::uint32_t color) const
            {
                const int minX = static_cast<int>(std::floor(std::min(x0, x1) - thickness));
                const int maxX = static_cast<int>(std::ceil(std::max(x0, x1) + thickness));
                const int minY = static_cast<int>(std::floor(std::min(y0, y1) - thickness));
                const int maxY = static_cast<int>(std::ceil(std::max(y0, y1) + thickness));
                const double dx = x1 - x0;
                const double dy = y1 - y0;
                const double length2 = dx * dx + dy * dy;
                for (int y = std::max(0, minY); y <= std::min(height - 1, maxY); ++y)
                {
                    for (int x = std::max(0, minX); x <= std::min(width - 1, maxX); ++x)
                    {
                        const double px = x + 0.5 - x0;
                        const double py = y + 0.5 - y0;
                        const double t = length2 > 0.0 ? std::clamp((px * dx + py * dy) / length2, 0.0, 1.0) : 0.0;
                        const double ex = px - t * dx;
                        const double ey = py - t * dy;
                        if (ex * ex + ey * ey <= thickness * thickness / 4.0)
                        {
                            reinterpret_cast<std::uint32_t*>(pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride))[x] = color;
                        }
                    }
                }
            }
        };

    } // namespace

    FrameHit HitTitleBar(const double x, const int width, const int height, const bool maximizeOffered,
                         const bool minimizeOffered)
    {
        if (x < 0.0 || x >= width)
        {
            return FrameHit::None;
        }
        const double fromRight = width - x;
        int cell = 0;
        if (fromRight <= height * (cell + 1))
        {
            return FrameHit::Close;
        }
        ++cell;
        if (maximizeOffered)
        {
            if (fromRight <= height * (cell + 1))
            {
                return FrameHit::Maximize;
            }
            ++cell;
        }
        if (minimizeOffered && fromRight <= height * (cell + 1))
        {
            return FrameHit::Minimize;
        }
        return FrameHit::Title;
    }

    std::uint32_t ResizeEdgeAt(const double x, const double y, const int width, const int height, const int border,
                               const int corner)
    {
        if (x < -border || y < -border || x >= width + border || y >= height + border)
        {
            return XDG_TOPLEVEL_RESIZE_EDGE_NONE;
        }
        bool left = x < 0;
        bool right = x >= width;
        bool top = y < 0;
        bool bottom = y >= height;
        if (!left && !right && !top && !bottom)
        {
            return XDG_TOPLEVEL_RESIZE_EDGE_NONE;  // inside the window: not the border's
        }
        // Near a corner, along either edge, is the corner.
        if (left || right)
        {
            top = top || y < corner;
            bottom = bottom || y >= height - corner;
        }
        if (top || bottom)
        {
            left = left || x < corner;
            right = right || x >= width - corner;
        }
        if (top && left) { return XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT; }
        if (top && right) { return XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT; }
        if (bottom && left) { return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT; }
        if (bottom && right) { return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT; }
        if (top) { return XDG_TOPLEVEL_RESIZE_EDGE_TOP; }
        if (bottom) { return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM; }
        if (left) { return XDG_TOPLEVEL_RESIZE_EDGE_LEFT; }
        return XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
    }

    WaylandFrame::WaylandFrame(WaylandWindowHost& host, WaylandWindow& window) : host_(host), window_(window) {}

    WaylandFrame::~WaylandFrame()
    {
        Destroy();
    }

    int WaylandFrame::GetTitleBarHeight() const
    {
        return enabled_ && !serverDecorated_ && !fullscreen_ ? kTitleBarHeight : 0;
    }

    void WaylandFrame::SetTitle(const std::string& title)
    {
        title_ = title;
    }

    void WaylandFrame::SetEnabled(const bool enabled)
    {
        enabled_ = enabled;
        UpdateVisibility();
    }

    void WaylandFrame::SetServerDecorated(const bool serverDecorated)
    {
        serverDecorated_ = serverDecorated;
        UpdateVisibility();
    }

    void WaylandFrame::SetState(const bool activated, const bool maximized, const bool fullscreen)
    {
        const bool redraw = activated != activated_ || maximized != maximized_;
        activated_ = activated;
        maximized_ = maximized;
        fullscreen_ = fullscreen;
        UpdateVisibility();
        if (redraw && visible_)
        {
            Layout(width_, height_, scale_);
        }
    }

    void WaylandFrame::Create()
    {
        if (parts_[kTitle].surface != nullptr)
        {
            return;
        }
        WaylandConnection& connection = host_.GetConnection();
        const WaylandGlobals& globals = connection.GetGlobals();
        if (globals.subcompositor == nullptr || globals.shm == nullptr)
        {
            return;
        }
        for (Surface& part : parts_)
        {
            part.surface = wl_compositor_create_surface(globals.compositor);
            part.subsurface = wl_subcompositor_get_subsurface(globals.subcompositor, part.surface, window_.GetSurface());
            // Desynchronised: a hover redraws now, not with the game's next frame.
            wl_subsurface_set_desync(part.subsurface);
#if defined(CNA_WAYLAND_HAVE_VIEWPORTER)
            if (globals.viewporter != nullptr)
            {
                part.viewport = wp_viewporter_get_viewport(globals.viewporter, part.surface);
            }
#endif
            host_.MapFrameSurface(part.surface, this);
        }
        if (wl_region* opaque = wl_compositor_create_region(globals.compositor))
        {
            wl_region_add(opaque, 0, 0, INT32_MAX, INT32_MAX);
            wl_surface_set_opaque_region(parts_[kTitle].surface, opaque);
            wl_region_destroy(opaque);
        }
    }

    void WaylandFrame::Destroy()
    {
        for (Surface& part : parts_)
        {
            if (part.surface == nullptr)
            {
                continue;
            }
            host_.MapFrameSurface(part.surface, nullptr);
#if defined(CNA_WAYLAND_HAVE_VIEWPORTER)
            if (part.viewport != nullptr)
            {
                wp_viewport_destroy(static_cast<wp_viewport*>(part.viewport));
            }
#endif
            // The subsurface role goes before its surface, and both before the parent's surface,
            // which the window destroys after this frame.
            wl_subsurface_destroy(part.subsurface);
            wl_surface_destroy(part.surface);
            part = Surface{};
        }
        visible_ = false;
    }

    void WaylandFrame::UpdateVisibility()
    {
        const bool wanted = GetTitleBarHeight() > 0 && window_.IsConfigured();
        if (wanted == visible_)
        {
            return;
        }
        if (wanted)
        {
            Create();
            if (parts_[kTitle].surface == nullptr)
            {
                return;
            }
            visible_ = true;
            Layout(width_, height_, scale_);
            return;
        }
        // Hidden: a null buffer unmaps a subsurface; the objects stay for the next time.
        visible_ = false;
        for (Surface& part : parts_)
        {
            if (part.surface != nullptr && part.attached)
            {
                wl_surface_attach(part.surface, nullptr, 0, 0);
                wl_surface_commit(part.surface);
                part.attached = false;
            }
        }
        host_.GetConnection().Flush();
    }

    void WaylandFrame::Place(const Part part, const int x, const int y, const int width, const int height)
    {
        Surface& surface = parts_[part];
        surface.x = x;
        surface.y = y;
        surface.width = std::max(1, width);
        surface.height = std::max(1, height);
        // Applied with the parent's next commit, like every subsurface position.
        wl_subsurface_set_position(surface.subsurface, x, y);
    }

    void WaylandFrame::Layout(const int width, const int height, const double scale)
    {
        width_ = width;
        height_ = height;
        scale_ = scale > 0.0 ? scale : 1.0;
        UpdateVisibility();
        if (!visible_ || width_ <= 0 || height_ <= 0)
        {
            return;
        }
        const int title = kTitleBarHeight;
        // The resize border lies outside the window geometry, as GTK's does; maximized, there is
        // nothing to resize and the border collapses to nothing a click can land on.
        const int border = maximized_ ? 0 : kBorder;
        Place(kTitle, 0, -title, width_, title);
        Place(kTop, -border, -title - border, width_ + 2 * border, border);
        Place(kLeft, -border, -title, border, height_ + title + border);
        Place(kRight, width_, -title, border, height_ + title + border);
        Place(kBottom, 0, height_, width_, border);
        DrawTitleBar();
        for (const Part part : {kTop, kLeft, kRight, kBottom})
        {
            DrawBorder(part);
        }
        host_.GetConnection().Flush();
    }

    void WaylandFrame::Commit(const Part part)
    {
        Surface& surface = parts_[part];
        if (surface.buffer == nullptr)
        {
            return;
        }
        wl_surface_attach(surface.surface, surface.buffer->GetBuffer(), 0, 0);
        wl_surface_damage(surface.surface, 0, 0, INT32_MAX, INT32_MAX);
        wl_surface_commit(surface.surface);
        surface.buffer->MarkAttached();
        surface.attached = true;
    }

    void WaylandFrame::DrawBorder(const Part part)
    {
        Surface& surface = parts_[part];
        WaylandConnection& connection = host_.GetConnection();
        // Invisible: a transparent buffer whose only job is to catch the pointer. With a viewport
        // one pixel stretched is enough; without, a buffer of the border's size.
        const bool stretched = surface.viewport != nullptr;
        const int bufferWidth = stretched ? 1 : surface.width;
        const int bufferHeight = stretched ? 1 : surface.height;
        if (surface.buffer == nullptr || surface.buffer->IsBusy() || surface.buffer->GetWidth() != bufferWidth ||
            surface.buffer->GetHeight() != bufferHeight)
        {
            surface.buffer = WaylandShmBuffer::Create(connection.GetGlobals().shm, bufferWidth, bufferHeight,
                                                      WL_SHM_FORMAT_ARGB8888);
            if (surface.buffer == nullptr)
            {
                return;
            }
            std::memset(surface.buffer->GetPixels(), 0,
                        static_cast<std::size_t>(surface.buffer->GetStride()) * static_cast<std::size_t>(bufferHeight));
        }
#if defined(CNA_WAYLAND_HAVE_VIEWPORTER)
        if (stretched)
        {
            wp_viewport_set_destination(static_cast<wp_viewport*>(surface.viewport), surface.width, surface.height);
        }
#endif
        Commit(part);
    }

    void WaylandFrame::DrawTitleBar()
    {
        Surface& surface = parts_[kTitle];
        WaylandConnection& connection = host_.GetConnection();
        const bool viewport = surface.viewport != nullptr;
        // With a viewport the bar is drawn at the window's own pixel density; without, at the
        // integer buffer scale.
        const double scale = viewport ? scale_ : std::max(1.0, std::ceil(scale_));
        const int pixelWidth = ScaledExtent(surface.width, scale);
        const int pixelHeight = ScaledExtent(surface.height, scale);
        if (surface.buffer == nullptr || surface.buffer->IsBusy() || surface.buffer->GetWidth() != pixelWidth ||
            surface.buffer->GetHeight() != pixelHeight)
        {
            surface.buffer = WaylandShmBuffer::Create(connection.GetGlobals().shm, pixelWidth, pixelHeight,
                                                      WL_SHM_FORMAT_XRGB8888);
            if (surface.buffer == nullptr)
            {
                return;
            }
        }
        const Canvas canvas{surface.buffer->GetPixels(), surface.buffer->GetStride(), pixelWidth, pixelHeight};
        canvas.Fill(0, 0, pixelWidth, pixelHeight, activated_ ? kBackgroundActive : kBackgroundInactive);
        canvas.Fill(0, pixelHeight - std::max(1, static_cast<int>(scale)), pixelWidth, pixelHeight, kSeparator);

        const bool maximizeOffered = !window_.HasWmCapabilities() ||
                                     (window_.GetWmCapabilities() & (1u << XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE)) != 0;
        const bool minimizeOffered = !window_.HasWmCapabilities() ||
                                     (window_.GetWmCapabilities() & (1u << XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE)) != 0;
        const std::uint32_t icon = activated_ ? kIconActive : kIconInactive;
        const double cell = kTitleBarHeight * scale;
        const double stroke = std::max(1.0, 1.5 * scale);
        const double half = 5.0 * scale;
        int index = 0;
        const auto button = [&](const FrameHit which) {
            const double right = pixelWidth - index * cell;
            const double left = right - cell;
            const double cx = (left + right) / 2.0;
            const double cy = pixelHeight / 2.0;
            if (hover_ == which || pressed_ == which)
            {
                const std::uint32_t hot = which == FrameHit::Close ? kCloseHover
                                          : pressed_ == which     ? kButtonPressed
                                                                  : kButtonHover;
                canvas.Fill(static_cast<int>(left), 0, static_cast<int>(right), pixelHeight - 1, hot);
            }
            const std::uint32_t ink = which == FrameHit::Close && hover_ == which ? 0xFFFFFFFFu : icon;
            switch (which)
            {
                case FrameHit::Close:
                    canvas.Line(cx - half, cy - half, cx + half, cy + half, stroke, ink);
                    canvas.Line(cx - half, cy + half, cx + half, cy - half, stroke, ink);
                    break;
                case FrameHit::Maximize:
                    if (maximized_)
                    {
                        // Restore: two overlapping squares.
                        const double offset = 2.0 * scale;
                        const double s = half - offset;
                        canvas.Line(cx - half + 2 * offset, cy - half, cx + half, cy - half, stroke, ink);
                        canvas.Line(cx + half, cy - half, cx + half, cy + half - 2 * offset, stroke, ink);
                        canvas.Line(cx - half, cy - s + offset, cx + s - offset, cy - s + offset, stroke, ink);
                        canvas.Line(cx + s - offset, cy - s + offset, cx + s - offset, cy + half, stroke, ink);
                        canvas.Line(cx + s - offset, cy + half, cx - half, cy + half, stroke, ink);
                        canvas.Line(cx - half, cy + half, cx - half, cy - s + offset, stroke, ink);
                    }
                    else
                    {
                        canvas.Line(cx - half, cy - half, cx + half, cy - half, stroke, ink);
                        canvas.Line(cx + half, cy - half, cx + half, cy + half, stroke, ink);
                        canvas.Line(cx + half, cy + half, cx - half, cy + half, stroke, ink);
                        canvas.Line(cx - half, cy + half, cx - half, cy - half, stroke, ink);
                    }
                    break;
                case FrameHit::Minimize:
                    canvas.Line(cx - half, cy + half, cx + half, cy + half, stroke, ink);
                    break;
                default:
                    break;
            }
            ++index;
        };
        button(FrameHit::Close);
        if (maximizeOffered) { button(FrameHit::Maximize); }
        if (minimizeOffered) { button(FrameHit::Minimize); }

        if (viewport)
        {
#if defined(CNA_WAYLAND_HAVE_VIEWPORTER)
            wp_viewport_set_destination(static_cast<wp_viewport*>(surface.viewport), surface.width, surface.height);
#endif
        }
        else if (connection.GetGlobals().compositorVersion >= 3)
        {
            wl_surface_set_buffer_scale(surface.surface, static_cast<int>(scale));
        }
        Commit(kTitle);
    }

    int WaylandFrame::PartOf(wl_surface* surface) const
    {
        for (int index = 0; index < kPartCount; ++index)
        {
            if (parts_[static_cast<std::size_t>(index)].surface == surface)
            {
                return index;
            }
        }
        return -1;
    }

    void WaylandFrame::OnPointer(wl_surface* surface, const int kind, const double x, const double y,
                                 const std::uint32_t button, const bool pressed, const std::uint32_t serial,
                                 wl_pointer* pointer)
    {
        const int part = PartOf(surface);
        if (part < 0)
        {
            return;
        }
        const Surface& area = parts_[static_cast<std::size_t>(part)];
        // Where the pointer is in the window geometry, which starts at the title bar's top left.
        const double gx = area.x + x;
        const double gy = area.y + y + kTitleBarHeight;
        const int geometryHeight = height_ + kTitleBarHeight;
        const bool maximizeOffered = !window_.HasWmCapabilities() ||
                                     (window_.GetWmCapabilities() & (1u << XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE)) != 0;
        const bool minimizeOffered = !window_.HasWmCapabilities() ||
                                     (window_.GetWmCapabilities() & (1u << XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE)) != 0;

        FrameHit hit = FrameHit::None;
        std::uint32_t edge = XDG_TOPLEVEL_RESIZE_EDGE_NONE;
        if (part == kTitle)
        {
            hit = HitTitleBar(x, width_, kTitleBarHeight, maximizeOffered, minimizeOffered);
        }
        else if (!maximized_)
        {
            edge = ResizeEdgeAt(gx, gy, width_, geometryHeight, kBorder, kCorner);
            hit = edge != XDG_TOPLEVEL_RESIZE_EDGE_NONE ? FrameHit::Resize : FrameHit::None;
        }

        if (kind == 0)  // enter
        {
            SystemCursor cursor = SystemCursor::Arrow;
            switch (edge)
            {
                case XDG_TOPLEVEL_RESIZE_EDGE_TOP:
                case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM: cursor = SystemCursor::NsResize; break;
                case XDG_TOPLEVEL_RESIZE_EDGE_LEFT:
                case XDG_TOPLEVEL_RESIZE_EDGE_RIGHT: cursor = SystemCursor::EwResize; break;
                case XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT:
                case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT: cursor = SystemCursor::NwseResize; break;
                case XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT:
                case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT: cursor = SystemCursor::NeswResize; break;
                default: break;
            }
            host_.SetFrameCursor(pointer, serial, cursor);
        }
        if (part == kTitle && (kind == 0 || kind == 2))
        {
            if (hit != hover_)
            {
                hover_ = hit;
                DrawTitleBar();
                host_.GetConnection().Flush();
            }
            return;
        }
        if (kind == 1)  // leave
        {
            if (hover_ != FrameHit::None || pressed_ != FrameHit::None)
            {
                hover_ = FrameHit::None;
                pressed_ = FrameHit::None;
                if (visible_) { DrawTitleBar(); }
                host_.GetConnection().Flush();
            }
            return;
        }
        if (kind != 3)
        {
            return;
        }

        // Buttons.
        if (button == BTN_RIGHT && pressed && part == kTitle && hit == FrameHit::Title)
        {
            window_.ShowWindowMenu(static_cast<int>(x), static_cast<int>(y) - kTitleBarHeight);
            return;
        }
        if (button != BTN_LEFT)
        {
            return;
        }
        if (pressed)
        {
            pressed_ = hit;
            switch (hit)
            {
                case FrameHit::Title:
                {
                    const auto now = std::chrono::steady_clock::now();
                    if (now - lastTitleClick_ <= kDoubleClick && std::abs(x - lastTitleClickX_) <= kDoubleClickSlop)
                    {
                        lastTitleClick_ = {};
                        pressed_ = FrameHit::None;
                        window_.ToggleMaximized();
                        return;
                    }
                    lastTitleClick_ = now;
                    lastTitleClickX_ = x;
                    pressed_ = FrameHit::None;
                    window_.BeginMove();
                    return;
                }
                case FrameHit::Resize:
                    pressed_ = FrameHit::None;
                    window_.BeginResize(edge);
                    return;
                case FrameHit::Close:
                case FrameHit::Maximize:
                case FrameHit::Minimize:
                    DrawTitleBar();
                    host_.GetConnection().Flush();
                    return;
                case FrameHit::None:
                    return;
            }
            return;
        }
        // Released: a button acts when pressed and released on it, as every toolkit's does.
        const FrameHit was = pressed_;
        pressed_ = FrameHit::None;
        if (visible_) { DrawTitleBar(); }
        if (was != hit)
        {
            host_.GetConnection().Flush();
            return;
        }
        switch (hit)
        {
            case FrameHit::Close: window_.RequestClose(); break;
            case FrameHit::Maximize: window_.ToggleMaximized(); break;
            case FrameHit::Minimize: window_.Minimize(); break;
            default: break;
        }
        host_.GetConnection().Flush();
    }

} // namespace CNA::Platform::Wayland
