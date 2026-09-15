// SPDX-License-Identifier: MS-PL
#pragma once

#include "WaylandProtocols.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace CNA::Platform::Wayland {

    class WaylandShmBuffer;
    class WaylandWindow;
    class WaylandWindowHost;

    /** @brief What a point of the built-in frame does when pressed. */
    enum class FrameHit
    {
        /** @brief Nothing: outside every part. */
        None,
        /** @brief The title bar's empty part: drag moves, double click maximizes. */
        Title,
        /** @brief The close button. */
        Close,
        /** @brief The maximize/restore button. */
        Maximize,
        /** @brief The minimize button. */
        Minimize,
        /** @brief A resize edge or corner; ResizeEdgeAt says which. */
        Resize
    };

    /**
     * @brief Where a point of the title bar falls, and which buttons it has.
     *
     * Pure arithmetic, so the layout is testable without a compositor: the buttons are square
     * cells the title bar's height, from the right edge -- close, then maximize, then minimize --
     * each present only when the compositor offers the action (xdg_toplevel v5
     * `wm_capabilities`; a compositor before v5 is assumed to offer them all).
     *
     * @param x The point, logical, from the title bar's left.
     * @param width The title bar's width.
     * @param height The title bar's height (and each button's side).
     * @param maximizeOffered Whether the maximize button exists.
     * @param minimizeOffered Whether the minimize button exists.
     * @return What is there.
     */
    [[nodiscard]] FrameHit HitTitleBar(double x, int width, int height, bool maximizeOffered, bool minimizeOffered);

    /**
     * @brief The `xdg_toplevel_resize_edge` of a point on the resize border around the content.
     *
     * The border is `border` wide all round the window geometry (content plus title bar); a point
     * within `corner` of a corner resizes both ways.
     *
     * @param x The point, relative to the window geometry's top left (negative outside it).
     * @param y The point, likewise.
     * @param width The window geometry's width.
     * @param height The window geometry's height.
     * @param border The border's width.
     * @param corner How far from a corner still counts as the corner.
     * @return The edge, or 0 (`XDG_TOPLEVEL_RESIZE_EDGE_NONE`) for a point not on the border.
     */
    [[nodiscard]] std::uint32_t ResizeEdgeAt(double x, double y, int width, int height, int border, int corner);

    /**
     * @brief The title bar CNA draws itself where the compositor draws none (plans/plan_wayland.md
     * D-22, WAYLAND-0090).
     *
     * GNOME's compositor offers no `zxdg_decoration_manager_v1`: a window it shows has no title
     * bar, cannot be moved or closed with the mouse, and does not look like a window, unless the
     * client draws one. This is that, and no more: an opaque bar above the content with close,
     * maximize and minimize buttons (drawn as shapes -- there is no text renderer in the platform
     * module, so the title itself is not drawn; the compositor shows it in its task switcher), a
     * drag on it moves the window, a double click maximizes, a right click opens the compositor's
     * window menu, and an invisible border outside the window geometry resizes it -- the way GTK's
     * client-side decorations work.
     *
     * Everything is a `wl_subsurface` of the window's surface, in desynchronised mode so a hover
     * redraws without waiting for the game's next frame. The frame is hidden when the compositor
     * decorates the window itself, while the window is borderless or fullscreen, and while it has no
     * role.
     */
    class WaylandFrame
    {
    public:
        /** @brief The title bar's height, logical. */
        static constexpr int kTitleBarHeight = 32;
        /** @brief The invisible resize border's width, logical. */
        static constexpr int kBorder = 8;

        /**
         * @brief Creates the frame for a window; nothing is shown until SetEnabled.
         * @param host The platform.
         * @param window The window it frames.
         */
        WaylandFrame(WaylandWindowHost& host, WaylandWindow& window);

        /** @brief Destroys the frame's surfaces. */
        ~WaylandFrame();

        WaylandFrame(const WaylandFrame&) = delete;
        WaylandFrame& operator=(const WaylandFrame&) = delete;

        /**
         * @brief Gets the logical height the frame takes above the content.
         * @return The title bar's height while it is shown, else 0.
         */
        [[nodiscard]] int GetTitleBarHeight() const;

        /** @brief Sets the title (kept for when a text renderer exists). @param title The title. */
        void SetTitle(const std::string& title);

        /** @brief Shows or hides the frame (the window's own role exists only while shown). @param enabled Show it. */
        void SetEnabled(bool enabled);

        /** @brief Tells the frame the compositor decorates the window itself. @param serverDecorated True if it does. */
        void SetServerDecorated(bool serverDecorated);

        /**
         * @brief Tells the frame the window's state, which changes how it is drawn.
         * @param activated The compositor's active window.
         * @param maximized Maximized (no resize border).
         * @param fullscreen Fullscreen (the frame is hidden).
         */
        void SetState(bool activated, bool maximized, bool fullscreen);

        /**
         * @brief Lays the frame out for a content size and scale, and redraws it.
         * @param width Content width, logical.
         * @param height Content height, logical.
         * @param scale Pixels per logical unit.
         */
        void Layout(int width, int height, double scale);

        /**
         * @brief Handles a pointer event on one of the frame's surfaces.
         * @param surface The surface.
         * @param kind 0 enter, 1 leave, 2 motion, 3 button (WaylandMouse's frame event kinds).
         * @param x Surface-local x.
         * @param y Surface-local y.
         * @param button The `BTN_*` code, for buttons.
         * @param pressed Pressed or released, for buttons.
         * @param serial The event's serial (enter and button).
         * @param pointer The pointer.
         */
        void OnPointer(wl_surface* surface, int kind, double x, double y, std::uint32_t button, bool pressed,
                       std::uint32_t serial, wl_pointer* pointer);

        /** @brief Gets whether the frame is on screen (tests). @return True while shown. */
        [[nodiscard]] bool IsVisible() const { return visible_; }

    private:
        enum Part
        {
            kTitle = 0,
            kTop,
            kLeft,
            kRight,
            kBottom,
            kPartCount
        };

        struct Surface
        {
            wl_surface* surface = nullptr;
            wl_subsurface* subsurface = nullptr;
            void* viewport = nullptr;
            std::unique_ptr<WaylandShmBuffer> buffer;
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
            bool attached = false;
        };

        void Create();
        void Destroy();
        void UpdateVisibility();
        void Place(Part part, int x, int y, int width, int height);
        void DrawTitleBar();
        void DrawBorder(Part part);
        void Commit(Part part);
        [[nodiscard]] int PartOf(wl_surface* surface) const;

        WaylandWindowHost& host_;
        WaylandWindow& window_;
        std::array<Surface, kPartCount> parts_{};
        std::string title_;
        bool enabled_ = false;
        bool serverDecorated_ = false;
        bool fullscreen_ = false;
        bool maximized_ = false;
        bool activated_ = false;
        bool visible_ = false;
        int width_ = 0;
        int height_ = 0;
        double scale_ = 1.0;
        FrameHit hover_ = FrameHit::None;
        FrameHit pressed_ = FrameHit::None;
        std::chrono::steady_clock::time_point lastTitleClick_{};
        double lastTitleClickX_ = 0.0;
    };

} // namespace CNA::Platform::Wayland
