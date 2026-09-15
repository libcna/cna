// SPDX-License-Identifier: MS-PL
//
// plans/plan_wayland.md WAYLAND-0111: an in-process Wayland compositor for the backend's protocol
// tests.
//
// A real compositor is the wrong tool for most protocol-state tests: it decides by itself when to
// configure, when to focus, when to release a buffer and when to send a frame callback, it has no
// input devices on a headless machine, and it cannot be told to withdraw a global or to change an
// output's scale in the middle of a test. This one does exactly what the test says, when the test
// says it -- and it checks every request against the protocols' own rules, posting the protocol
// error a strict compositor would (Mutter's strictness is the model), so a backend that violates
// a rule fails here rather than on somebody's desktop.
//
// It is a libwayland-server program running on its own thread inside the test process; the
// backend reaches it through a socketpair handed over in WAYLAND_SOCKET, so no socket appears in
// XDG_RUNTIME_DIR and nothing on the machine can connect to it. It is linked into the test binary
// only (cmake/UnitTests.cmake); cna_platform never sees libwayland-server.
//
// Everything a test asks for is marshalled onto the compositor thread and waited for, so a test
// reads as straight-line code. The compositor never waits for the client, so a test may call it
// while the backend is blocked in a roundtrip of its own.
#pragma once

#if defined(CNA_WAYLAND_HAVE_TEST_COMPOSITOR)

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace CNA::Platform::Wayland::Testing {

    /** @brief What the compositor advertises and how it behaves. */
    struct CompositorOptions
    {
        // --- globals --------------------------------------------------------------------------
        /** @brief wl_compositor version (6: preferred_buffer_scale). */
        std::uint32_t compositorVersion = 6;
        /** @brief xdg_wm_base version (6: suspended; 5: wm_capabilities; 4: configure_bounds). */
        std::uint32_t wmBaseVersion = 6;
        /** @brief wl_seat version (8: axis_value120; 5: frames). */
        std::uint32_t seatVersion = 9;
        /** @brief wl_output version (4: name and description). */
        std::uint32_t outputVersion = 4;
        /** @brief Offer wl_subcompositor. */
        bool subcompositor = true;
        /** @brief Offer wp_viewporter. */
        bool viewporter = true;
        /** @brief Offer wp_fractional_scale_manager_v1. */
        bool fractionalScale = true;
        /** @brief Offer zxdg_output_manager_v1. */
        bool xdgOutput = true;
        /** @brief Offer zwp_relative_pointer_manager_v1. */
        bool relativePointer = true;
        /** @brief Offer zwp_pointer_constraints_v1. */
        bool pointerConstraints = true;
        /** @brief Offer wp_cursor_shape_manager_v1. */
        bool cursorShape = true;
        /** @brief Offer wl_data_device_manager. */
        bool dataDevice = true;
        /** @brief Offer zwp_primary_selection_device_manager_v1. */
        bool primarySelection = true;
        /** @brief Offer zwp_text_input_manager_v3. */
        bool textInput = true;
        /** @brief Offer zxdg_decoration_manager_v1 (GNOME does not; KDE and wlroots do). */
        bool decorationManager = false;
        /** @brief The mode the decoration manager answers every request with, when offered. */
        bool serverSideDecorations = true;
        /** @brief Offer xdg_activation_v1. */
        bool activation = true;
        /** @brief Offer zwp_idle_inhibit_manager_v1. */
        bool idleInhibit = true;
        /** @brief Offer zxdg_exporter_v2. */
        bool exporter = true;
        /** @brief Offer a seat at all. */
        bool seat = true;
        /** @brief The seat has a keyboard. */
        bool keyboard = true;
        /** @brief The seat has a pointer. */
        bool pointer = true;
        /** @brief The seat has a touchscreen. */
        bool touch = false;

        // --- the seat -------------------------------------------------------------------------
        /** @brief XKB layout(s) of the keymap sent, e.g. "us" or "cz,us". */
        std::string layout = "us";
        /** @brief XKB variant(s). */
        std::string variant;
        /** @brief XKB options. */
        std::string options;
        /** @brief Key repeat rate sent in repeat_info, per second (0 disables repeat). */
        std::int32_t repeatRate = 25;
        /** @brief Key repeat delay sent in repeat_info, milliseconds. */
        std::int32_t repeatDelay = 600;

        // --- the first output ------------------------------------------------------------------
        /** @brief Mode width. */
        int outputWidth = 1920;
        /** @brief Mode height. */
        int outputHeight = 1080;
        /** @brief wl_output.scale. */
        int outputScale = 1;
        /** @brief Refresh in mHz. */
        int outputRefresh = 60000;

        // --- behaviour -------------------------------------------------------------------------
        /** @brief Answer the initial commit with a configure, as every compositor does. */
        bool configureOnInitialCommit = true;
        /** @brief Width of the initial configure (0: the client chooses). */
        int initialWidth = 0;
        /** @brief Height of the initial configure. */
        int initialHeight = 0;
        /** @brief Include `activated` in configures, as for the focused window. */
        bool activated = true;
        /** @brief Answer set_maximized/set_fullscreen and their unsets with configures. */
        bool answerStateRequests = true;
        /** @brief Release a buffer as soon as a newer one is committed over it. */
        bool releaseBuffers = true;
        /** @brief Answer frame callbacks at once on commit; otherwise only FrameDone() does. */
        bool autoFrameDone = true;
        /**
         * @brief Treat a committed window geometry that differs from a maximized or fullscreen
         * configure's size as the protocol error xdg-shell makes it
         * (`xdg_wm_base.invalid_surface_state`), as Mutter does.
         */
        bool strictConstrainedGeometry = true;
        /** @brief Echo a client's own selection back to it as an offer, as Mutter does. */
        bool echoSelection = true;
    };

    /** @brief One output the compositor exposes. */
    struct OutputSpec
    {
        /** @brief Connector name (wl_output v4, xdg-output v2). */
        std::string name = "CNA-1";
        /** @brief Description. */
        std::string description = "CNA test output";
        /** @brief Position in the compositor space (logical). */
        int x = 0;
        /** @brief Position in the compositor space (logical). */
        int y = 0;
        /** @brief Mode width in pixels. */
        int width = 1920;
        /** @brief Mode height in pixels. */
        int height = 1080;
        /** @brief Refresh in mHz. */
        int refresh = 60000;
        /** @brief wl_output.scale. */
        int scale = 1;
        /** @brief Logical width reported by xdg-output (0: width / scale). */
        int logicalWidth = 0;
        /** @brief Logical height reported by xdg-output (0: height / scale). */
        int logicalHeight = 0;
        /** @brief Physical width in mm. */
        int physicalWidth = 530;
        /** @brief Physical height in mm. */
        int physicalHeight = 300;
    };

    /** @brief What the compositor knows about one xdg_toplevel. */
    struct ToplevelInfo
    {
        /** @brief Index among live toplevels, in creation order. */
        int index = -1;
        /** @brief The wl_surface's protocol id. */
        std::uint32_t surfaceId = 0;
        /** @brief xdg_toplevel.set_title. */
        std::string title;
        /** @brief xdg_toplevel.set_app_id. */
        std::string appId;
        /** @brief set_min_size (window geometry). */
        int minWidth = 0;
        /** @brief set_min_size. */
        int minHeight = 0;
        /** @brief set_max_size. */
        int maxWidth = 0;
        /** @brief set_max_size. */
        int maxHeight = 0;
        /** @brief Whether set_window_geometry was ever committed. */
        bool hasGeometry = false;
        /** @brief Committed window geometry. */
        int geometryX = 0;
        /** @brief Committed window geometry. */
        int geometryY = 0;
        /** @brief Committed window geometry. */
        int geometryWidth = 0;
        /** @brief Committed window geometry. */
        int geometryHeight = 0;
        /** @brief The last set_maximized/unset_maximized. */
        bool maximizeRequested = false;
        /** @brief The last set_fullscreen/unset_fullscreen. */
        bool fullscreenRequested = false;
        /** @brief set_minimized requests received. */
        int minimizeRequests = 0;
        /** @brief Configures sent. */
        int configures = 0;
        /** @brief ack_configure received. */
        int acks = 0;
        /** @brief The last configure serial sent. */
        std::uint32_t lastConfigureSerial = 0;
        /** @brief The last serial acked. */
        std::uint32_t lastAckedSerial = 0;
        /** @brief A buffer is committed after an ack: the window is on screen. */
        bool mapped = false;
        /** @brief Committed buffer size (0 without a buffer). */
        int bufferWidth = 0;
        /** @brief Committed buffer size. */
        int bufferHeight = 0;
        /** @brief Committed wl_surface.set_buffer_scale. */
        int bufferScale = 1;
        /** @brief Committed viewport destination (0: none). */
        int viewportWidth = 0;
        /** @brief Committed viewport destination (0: none). */
        int viewportHeight = 0;
        /** @brief Commits on the surface. */
        int commits = 0;
        /** @brief Commits that carried a new buffer. */
        int bufferCommits = 0;
        /** @brief xdg_toplevel.move requests. */
        int moveRequests = 0;
        /** @brief xdg_toplevel.resize requests. */
        int resizeRequests = 0;
        /** @brief The last resize request's edges. */
        std::uint32_t lastResizeEdges = 0;
        /** @brief show_window_menu requests. */
        int windowMenuRequests = 0;
        /** @brief Whether a zxdg_toplevel_decoration_v1 exists for it. */
        bool hasDecoration = false;
        /** @brief The last decoration mode the client asked for (0 none). */
        std::uint32_t requestedDecorationMode = 0;
        /** @brief Subsurfaces whose parent is this toplevel's surface. */
        int subsurfaces = 0;
        /** @brief Whether an opaque region was committed. */
        bool opaqueRegion = false;
        /** @brief A 0xAARRGGBB sample of the committed shm buffer's centre (0 when not shm). */
        std::uint32_t centrePixel = 0;
    };

    /** @brief One subsurface. */
    struct SubsurfaceInfo
    {
        /** @brief The subsurface's wl_surface id. */
        std::uint32_t surfaceId = 0;
        /** @brief Its parent's wl_surface id. */
        std::uint32_t parentId = 0;
        /** @brief Committed position relative to the parent. */
        int x = 0;
        /** @brief Committed position relative to the parent. */
        int y = 0;
        /** @brief Desynchronized. */
        bool desync = false;
        /** @brief Committed buffer size. */
        int bufferWidth = 0;
        /** @brief Committed buffer size. */
        int bufferHeight = 0;
        /** @brief Committed viewport destination. */
        int viewportWidth = 0;
        /** @brief Committed viewport destination. */
        int viewportHeight = 0;
    };

    /** @brief The pointer's state as the client left it. */
    struct PointerInfo
    {
        /** @brief wl_pointer resources the client holds. */
        int pointers = 0;
        /** @brief The last wl_pointer.set_cursor: surface id, 0 for hidden. */
        std::uint32_t cursorSurface = 0;
        /** @brief Whether set_cursor was ever called. */
        bool cursorSet = false;
        /** @brief set_cursor hotspot. */
        int hotspotX = 0;
        /** @brief set_cursor hotspot. */
        int hotspotY = 0;
        /** @brief The last wp_cursor_shape_device_v1.set_shape (0 none). */
        std::uint32_t shape = 0;
        /** @brief set_shape requests. */
        int shapeRequests = 0;
        /** @brief Live locked-pointer objects. */
        int locks = 0;
        /** @brief Live confined-pointer objects. */
        int confinements = 0;
        /** @brief Whether the live lock was activated (locked sent, unlocked not). */
        bool lockActive = false;
        /** @brief Locks ever created. */
        int locksCreated = 0;
        /** @brief The lock's lifetime (1 oneshot, 2 persistent). */
        std::uint32_t lockLifetime = 0;
        /** @brief Whether a cursor position hint was committed. */
        bool hasHint = false;
        /** @brief The hint, surface-local. */
        double hintX = 0.0;
        /** @brief The hint, surface-local. */
        double hintY = 0.0;
        /** @brief Relative-pointer objects alive. */
        int relativePointers = 0;
    };

    /** @brief The text-input-v3 state the client committed. */
    struct TextInputInfo
    {
        /** @brief Objects alive. */
        int objects = 0;
        /** @brief Committed enabled state. */
        bool enabled = false;
        /** @brief Commits received. */
        std::uint32_t commits = 0;
        /** @brief Committed content hint. */
        std::uint32_t hint = 0;
        /** @brief Committed content purpose. */
        std::uint32_t purpose = 0;
        /** @brief Committed cursor rectangle. */
        int cursorX = 0;
        /** @brief Committed cursor rectangle. */
        int cursorY = 0;
        /** @brief Committed cursor rectangle. */
        int cursorWidth = 0;
        /** @brief Committed cursor rectangle. */
        int cursorHeight = 0;
    };

    /** @brief A drag the compositor is performing into the client. */
    struct DragInfo
    {
        /** @brief The last mime type the client accepted (empty: rejected). */
        std::string accepted;
        /** @brief Whether accept was ever called. */
        bool acceptCalled = false;
        /** @brief The last set_actions. */
        std::uint32_t actions = 0;
        /** @brief The last preferred action. */
        std::uint32_t preferred = 0;
        /** @brief Whether finish was called. */
        bool finished = false;
        /** @brief receive requests, by mime type. */
        std::vector<std::string> received;
        /** @brief Whether the offer object still exists. */
        bool alive = false;
    };

    /** @brief The selection a client set. */
    struct ClientSelectionInfo
    {
        /** @brief Whether the client currently owns the selection. */
        bool owned = false;
        /** @brief The mime types its source offers. */
        std::vector<std::string> mimeTypes;
        /** @brief set_selection requests. */
        int setRequests = 0;
    };

    /**
     * @brief The test compositor.
     *
     * One instance serves one client connection. The client end of it is placed in
     * WAYLAND_SOCKET by ExportSocket(), so the next wl_display_connect(nullptr) in the process
     * -- the backend's -- connects here.
     */
    class TestCompositor
    {
    public:
        /**
         * @brief Starts the compositor thread and advertises the globals the options name.
         * @param options What to offer and how to behave.
         */
        explicit TestCompositor(CompositorOptions options = {});

        /** @brief Stops the thread, disconnecting the client if it is still connected. */
        ~TestCompositor();

        TestCompositor(const TestCompositor&) = delete;
        TestCompositor& operator=(const TestCompositor&) = delete;

        /** @brief Puts the client end of the connection in WAYLAND_SOCKET (once). */
        void ExportSocket();

        /** @brief Runs @p task on the compositor thread and waits for it. @param task The task. */
        void Run(const std::function<void()>& task);

        // --- the client ------------------------------------------------------------------------

        /** @brief Whether the client is still connected. */
        [[nodiscard]] bool IsClientConnected();
        /** @brief The first protocol error this compositor posted, or empty. */
        [[nodiscard]] std::string GetPostedError();
        /** @brief Rules the client broke that are not fatal (a serial never sent, and so on). */
        [[nodiscard]] std::vector<std::string> GetViolations();
        /** @brief Disconnects the client, as a compositor that crashed or restarted does. */
        void DisconnectClient();
        /** @brief Sends xdg_wm_base.ping. @return The serial. */
        std::uint32_t Ping();
        /** @brief pong requests received. */
        [[nodiscard]] int GetPongCount();

        // --- windows ---------------------------------------------------------------------------

        /** @brief Every live toplevel, in creation order. */
        [[nodiscard]] std::vector<ToplevelInfo> GetToplevels();
        /** @brief One toplevel by index, if it exists. */
        [[nodiscard]] std::optional<ToplevelInfo> GetToplevel(int index);
        /** @brief Every live subsurface. */
        [[nodiscard]] std::vector<SubsurfaceInfo> GetSubsurfaces();
        /** @brief Live wl_surface objects. */
        [[nodiscard]] int GetSurfaceCount();
        /**
         * @brief Sends a configure to a toplevel.
         * @param index The toplevel.
         * @param width Width (0: client chooses).
         * @param height Height.
         * @param states xdg_toplevel_state values; `activated` is NOT added automatically.
         * @return The xdg_surface.configure serial.
         */
        std::uint32_t Configure(int index, int width, int height, const std::vector<std::uint32_t>& states);
        /** @brief Sends xdg_toplevel.close. @param index The toplevel. */
        void Close(int index);
        /** @brief Answers every pending frame callback. @return How many. */
        int FrameDone();
        /** @brief Frame callbacks the client requested and has not been answered. */
        [[nodiscard]] int GetPendingFrameCallbacks();
        /** @brief Buffers committed and not yet released, across surfaces. */
        [[nodiscard]] int GetHeldBuffers();
        /** @brief Releases every buffer the compositor still holds (other than the current ones). */
        void ReleaseHeldBuffers();
        /** @brief Sends wl_surface.preferred_buffer_scale. */
        void SendPreferredBufferScale(int index, int scale);
        /** @brief Sends wp_fractional_scale_v1.preferred_scale (120ths). */
        void SendPreferredScale(int index, std::uint32_t scale120);

        // --- outputs ---------------------------------------------------------------------------

        /** @brief Adds an output global. @return Its index. */
        int AddOutput(const OutputSpec& spec);
        /** @brief Withdraws an output global (global_remove). */
        void RemoveOutput(int index);
        /** @brief Changes an output's scale, sending scale and done to every binding. */
        void SetOutputScale(int index, int scale, int logicalWidth = 0, int logicalHeight = 0);
        /** @brief Sends wl_surface.enter for a toplevel's surface. */
        void EnterOutput(int toplevel, int output);
        /** @brief Sends wl_surface.leave. */
        void LeaveOutput(int toplevel, int output);
        /** @brief Withdraws a singleton global by interface name (global_remove). */
        void RemoveGlobal(const std::string& interfaceName);

        // --- keyboard --------------------------------------------------------------------------

        /** @brief Focuses a toplevel's surface, with keys already held (evdev codes). */
        void KeyboardEnter(int toplevel, const std::vector<std::uint32_t>& heldKeys = {});
        /** @brief Removes keyboard focus. */
        void KeyboardLeave();
        /** @brief Presses or releases an evdev key; modifiers follow as a compositor sends them. */
        void Key(std::uint32_t evdevKey, bool pressed);
        /** @brief Sends a new keymap to every keyboard (a layout switch in the desktop settings). */
        void ReplaceKeymap(const std::string& layout, const std::string& variant = {});
        /** @brief Sends a modifiers event with an explicit layout group (a layout switch key). */
        void SetLayoutGroup(std::uint32_t group);
        /** @brief wl_keyboard resources the client holds. */
        [[nodiscard]] int GetKeyboardCount();
        /** @brief The serial of the last input event sent. */
        [[nodiscard]] std::uint32_t GetLastInputSerial();

        // --- pointer ---------------------------------------------------------------------------

        /** @brief Enters a toplevel's content surface. */
        void PointerEnter(int toplevel, double x, double y);
        /** @brief Enters any surface by id (a subsurface of the built-in frame). */
        void PointerEnterSurface(std::uint32_t surfaceId, double x, double y);
        /** @brief Moves the pointer on the entered surface. */
        void PointerMotion(double x, double y);
        /** @brief Presses or releases a button (BTN_* code). */
        void PointerButton(std::uint32_t button, bool pressed);
        /**
         * @brief Sends one scroll frame.
         * @param axis 0 vertical, 1 horizontal.
         * @param value Continuous value.
         * @param value120 axis_value120 (v8), 0 for none.
         * @param discrete axis_discrete (v5-v7), 0 for none.
         * @param source wl_pointer.axis_source, or -1 for none.
         */
        void PointerAxis(std::uint32_t axis, double value, std::int32_t value120, std::int32_t discrete,
                         std::int32_t source = 0);
        /** @brief Leaves the entered surface. */
        void PointerLeave();
        /** @brief Sends relative motion to every relative-pointer object. */
        void RelativeMotion(double dx, double dy, double dxUnaccelerated, double dyUnaccelerated);
        /** @brief Activates the live lock (sends locked). @return False when there is none. */
        bool ActivateLock();
        /** @brief Deactivates the live lock (sends unlocked). */
        void DeactivateLock();
        /** @brief The pointer state. */
        [[nodiscard]] PointerInfo GetPointerInfo();

        // --- touch -----------------------------------------------------------------------------

        /** @brief A finger goes down on a toplevel. */
        void TouchDown(int toplevel, std::int32_t id, double x, double y);
        /** @brief A finger moves. */
        void TouchMotion(std::int32_t id, double x, double y);
        /** @brief A finger lifts. */
        void TouchUp(std::int32_t id);
        /** @brief Ends a touch frame. */
        void TouchFrame();
        /** @brief Cancels every touch. */
        void TouchCancel();

        // --- selections and drag and drop -----------------------------------------------------

        /**
         * @brief Makes another program the clipboard owner, offering @p data; the client gets an
         * offer when it has keyboard focus (now, or at its next enter).
         */
        void OfferSelection(const std::map<std::string, std::vector<std::uint8_t>>& data);
        /** @brief Clears the clipboard (no owner). */
        void ClearSelection();
        /** @brief The client's own clipboard source. */
        [[nodiscard]] ClientSelectionInfo GetClientSelection();
        /**
         * @brief Asks the client's clipboard source for @p mimeType, as a paste in another program
         * does. @return The read end of the pipe, which the test reads while pumping the client;
         * -1 when the client owns no selection.
         */
        int RequestClientSelection(const std::string& mimeType);
        /** @brief Like OfferSelection, for the primary selection. */
        void OfferPrimarySelection(const std::map<std::string, std::vector<std::uint8_t>>& data);
        /** @brief Like GetClientSelection, for the primary selection. */
        [[nodiscard]] ClientSelectionInfo GetClientPrimarySelection();
        /** @brief Like RequestClientSelection, for the primary selection. */
        int RequestClientPrimarySelection(const std::string& mimeType);
        /** @brief Starts a drag from another program over a toplevel. */
        void DragEnter(int toplevel, double x, double y, const std::map<std::string, std::vector<std::uint8_t>>& data,
                       std::uint32_t sourceActions = 1);
        /** @brief Moves the drag. */
        void DragMotion(double x, double y);
        /** @brief Drops. */
        void Drop();
        /** @brief Leaves without a drop. */
        void DragLeave();
        /** @brief What the client did with the drag. */
        [[nodiscard]] DragInfo GetDragInfo();

        // --- text input ------------------------------------------------------------------------

        /** @brief Sends text-input enter for a toplevel. */
        void TextInputEnter(int toplevel);
        /** @brief Sends text-input leave. */
        void TextInputLeave();
        /** @brief Sends preedit_string. */
        void TextInputPreedit(const std::string& text, std::int32_t cursorBegin, std::int32_t cursorEnd);
        /** @brief Sends commit_string. */
        void TextInputCommit(const std::string& text);
        /** @brief Sends done with the client's commit count as the serial. */
        void TextInputDone();
        /** @brief The committed text-input state. */
        [[nodiscard]] TextInputInfo GetTextInputInfo();

        // --- desktop integration ------------------------------------------------------------------

        /** @brief Live idle inhibitors. */
        [[nodiscard]] int GetIdleInhibitorCount();
        /** @brief Activation tokens committed. */
        [[nodiscard]] int GetActivationTokenCount();
        /** @brief xdg_activation_v1.activate requests, with the token each named. */
        [[nodiscard]] std::vector<std::string> GetActivations();
        /** @brief Exported toplevel handles handed out. */
        [[nodiscard]] int GetExportCount();

        /** @brief The compositor's own state (public only so its protocol tables can name it). */
        struct State;

    private:
        std::unique_ptr<State> state_;
    };

} // namespace CNA::Platform::Wayland::Testing

#endif
