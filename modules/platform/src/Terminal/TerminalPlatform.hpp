// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatform.hpp"

#include "../Common/StandardFileSystem.hpp"
#include "../Common/StandardSystemInfo.hpp"
#include "TerminalCapabilityProbe.hpp"
#include "TerminalKeyboard.hpp"
#include "TerminalMouse.hpp"
#include "TerminalResizeWatcher.hpp"
#include "TerminalSessionController.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace CNA::Platform::Terminal {

    /**
     * @brief A platform whose display is the text terminal the process is attached to.
     *
     * The third implementation of the contract, and the one that tests whether the contract is
     * genuinely implementation-neutral rather than SDL3 with the names changed. `HeadlessPlatform`
     * proves the *refusal* paths work by supporting almost nothing; this one has a real display,
     * real input and real resize events, none of which come from a windowing system — so an
     * interface that quietly assumed one would fail here rather than pass by saying no to
     * everything.
     *
     * ### What this task provides
     *
     * Selection, factory registration, timing, the subsystem lifecycle, a window, the services
     * every platform must have (PLAT-130), surface presentation (PLAT-132), exact keyboard state
     * where the terminal answers the Kitty protocol probe (PLAT-137), and a timed synthetic
     * keyboard fallback otherwise (PLAT-138), plus cell-quantised SGR mouse input (PLAT-139).
     *
     * ### The window is a pixel surface, not the character grid
     *
     * A game asks for the resolution it wants to draw at and gets it. The terminal's cell grid is
     * a property of the *presenter*, which quantises the RGBA frame down to glyphs. Handing a game
     * an 80×24 "window" instead would break every layout computation it has, to describe the same
     * thing less usefully.
     *
     * ### Constructing one touches nothing
     *
     * No raw mode, no escape sequences, no queries: construction asks only whether standard
     * output is a terminal, which is free and changes no state. That is what makes it safe for
     * the conformance suite to build one in a process whose terminal belongs to somebody else.
     * Taking the terminal over happens on the first keyboard update or when a presenter is
     * created, and not before.
     *
     * Compiled on every POSIX target regardless of `CNA_PLATFORM`, for the same reason
     * `HeadlessPlatform` is: the conformance suite needs the implementations live in one process,
     * and this one links no third-party library at all.
     */
    class TerminalPlatform final : public IPlatform
    {
    public:
        /** @brief Creates the platform without touching the terminal. */
        TerminalPlatform();

        /**
         * @brief Creates a platform over descriptors supplied by an embedding host or test.
         *
         * Construction still performs only `isatty()` and sends no query.
         *
         * @param outputDescriptor The terminal output descriptor.
         * @param inputDescriptor The terminal input descriptor.
         */
        TerminalPlatform(int outputDescriptor, int inputDescriptor);

        /** @brief Destroys the platform and any windows it still owns. */
        ~TerminalPlatform() override;

        TerminalPlatform(const TerminalPlatform&) = delete;
        TerminalPlatform& operator=(const TerminalPlatform&) = delete;

        /** @brief Gets this implementation's name. @return `"Terminal"`. */
        [[nodiscard]] const std::string& GetName() const override;

        /**
         * @brief Gets what this platform can do.
         *
         * `surfacePresentation` is true exactly when standard output is a terminal, so the answer
         * differs between a process run from a shell and the same process with its output
         * redirected. `exactKeyboardState` is true only after a successful Kitty-protocol probe;
         * `pixelAccurateMouse` stays false because SGR-1006 reports character cells.
         *
         * @return The capability set.
         */
        [[nodiscard]] PlatformCapabilities GetCapabilities() const override;

        /** @brief Acquires a subsystem. @param subsystem The subsystem to acquire. */
        void AcquireSubsystem(PlatformSubsystem subsystem) override;

        /** @brief Releases a subsystem. @param subsystem The subsystem to release. */
        void ReleaseSubsystem(PlatformSubsystem subsystem) override;

        /** @brief Gets whether a subsystem is up. @param subsystem The subsystem. @return True if acquired. */
        [[nodiscard]] bool IsSubsystemInitialized(PlatformSubsystem subsystem) const override;

        /**
         * @brief Creates the one window a terminal can have.
         *
         * @param description The window's creation parameters; its size is the resolution the
         *        game will draw at, not the terminal's cell grid.
         * @return The created window.
         * @throws PlatformNotSupportedException If a window already exists — there is exactly one
         *         terminal, so a second window is refused rather than silently aliased onto it.
         */
        [[nodiscard]] std::unique_ptr<IPlatformWindow> CreateWindow(
            const WindowDescription& description) override;

        /**
         * @brief Drains this frame's events.
         *
         * @param destination Receives this frame's events; always cleared first.
         */
        void PollEvents(std::vector<PlatformEvent>& destination) override;

        /** @brief Gets a monotonic counter. @return Nanoseconds from a steady clock. */
        [[nodiscard]] std::uint64_t GetPerformanceCounter() const override;

        /** @brief Gets the counter frequency. @return One billion; the counter is in nanoseconds. */
        [[nodiscard]] std::uint64_t GetPerformanceFrequency() const override;

        /** @brief Gets elapsed milliseconds since construction. @return The elapsed time. */
        [[nodiscard]] std::uint64_t GetTicksMilliseconds() const override;

        /** @brief Sleeps for the requested duration. @param milliseconds How long to sleep. */
        void Delay(std::uint32_t milliseconds) override;

        /**
         * @brief Gets the keyboard service.
         * @return Exact Kitty input, a timed fallback on a queryable TTY, or null without one.
         */
        [[nodiscard]] IPlatformKeyboard* GetKeyboard() override;
        /**
         * @brief Gets the cell-quantised mouse service.
         * @return The service on a queryable TTY, or null without one.
         */
        [[nodiscard]] IPlatformMouse* GetMouse() override;
        /** @brief Gets the gamepad service. @return Null; no gamepad channel exists through a TTY. */
        [[nodiscard]] IPlatformGamepad* GetGamepad() override;
        /** @brief Gets the joystick service. @return Null; no joystick channel exists through a TTY. */
        [[nodiscard]] IPlatformJoystick* GetJoystick() override;
        /** @brief Gets the text input service. @return Null; text input is not delivered separately. */
        [[nodiscard]] IPlatformTextInput* GetTextInput() override;
        /** @brief Gets the sensor service. @return Null; a terminal exposes no sensors. */
        [[nodiscard]] IPlatformSensors* GetSensors() override;
        /** @brief Gets the haptics service. @return Null; a terminal exposes no haptic devices. */
        [[nodiscard]] IPlatformHaptics* GetHaptics() override;
        /** @brief Gets the input device enumeration service. @return Null; no devices are enumerable. */
        [[nodiscard]] IPlatformInputDevices* GetInputDevices() override;
        /** @brief Gets the clipboard service. @return Null; there is no clipboard to reach. */
        [[nodiscard]] IPlatformClipboard* GetClipboard() override;
        /** @brief Gets the display service. @return Null; there is one terminal, not a display list. */
        [[nodiscard]] IPlatformDisplays* GetDisplays() override;
        /** @brief Gets the dialog service. @return Null; there is no native dialog in a terminal. */
        [[nodiscard]] IPlatformDialogs* GetDialogs() override;
        /** @brief Gets the tray service. @return Null; a terminal has no notification area. */
        [[nodiscard]] IPlatformTray* GetTray() override;
        /** @brief Gets the filesystem service. @return A real filesystem; never null. */
        [[nodiscard]] IPlatformFileSystem* GetFileSystem() override;
        /** @brief Gets the system information service. @return Real host information; never null. */
        [[nodiscard]] IPlatformSystemInfo* GetSystemInfo() override;
        /** @brief Gets the OpenGL context service. @return Null; a terminal has no GL context. */
        [[nodiscard]] IPlatformGlContext* GetGlContext() override;
        /** @brief Gets the Vulkan surface service. @return Null; a terminal has no Vulkan surface. */
        [[nodiscard]] IPlatformVulkanSurface* GetVulkanSurface() override;

        /**
         * @brief Creates a presenter that draws frames as characters.
         *
         * Presentation takes a shared terminal-session lease here: raw mode, the alternate screen
         * and a hidden cursor remain active until the last presenter/input lease is released.
         * Terminal capabilities are detected lazily here if no earlier service query did so;
         * probing costs a round trip and raw mode, neither of which belongs in a constructor.
         *
         * @param window The window whose pixel size sets the resolution callers rasterise at.
         * @return The presenter; destroying it gives the terminal back.
         * @throws PlatformNotSupportedException If standard output is not a terminal.
         * @throws PlatformException If a session is already active in this process.
         */
        [[nodiscard]] std::unique_ptr<IPlatformSurfacePresenter> CreateSurfacePresenter(
            IPlatformWindow& window) override;

        /**
         * @brief Converts the terminal's cell grid into the pixel size a window reports.
         *
         * A cell has no true pixel size — it depends on the user's font — so a nominal 8×16 is
         * assumed, chosen for its 1:2 ratio so a window sized from it has the same shape the
         * presenter's letterboxing assumes. Getting the absolute numbers wrong costs nothing;
         * getting the *ratio* wrong squashes every picture by half.
         *
         * @param columns The terminal's width in cells.
         * @param rows The terminal's height in cells.
         * @param width Receives the width in nominal pixels.
         * @param height Receives the height in nominal pixels.
         */
        static void CellGridToPixelSize(int columns, int rows, int& width, int& height);

        /**
         * @brief Reports whether the process is actually attached to a terminal.
         *
         * Determined once at construction, and free: no query is sent and no terminal state is
         * touched. False is a normal outcome — piped into a log, redirected to a file, captured
         * by CI — and is what lets presentation refuse cleanly instead of writing escape
         * sequences into somebody's build output.
         *
         * @return True when standard output is a terminal.
         */
        [[nodiscard]] bool IsAttachedToTerminal() const;

    private:
        void EnsureCapabilitiesDetected() const;

        class TerminalWindow;

        /// Shared between the platform and the one window it made, so that destroying either
        /// first is defined behaviour. A raw back-pointer would dangle in exactly one teardown
        /// order and in no other -- the kind of defect that passes every test until the one
        /// process that happens to tear down that way.
        struct WindowSlot
        {
            bool taken = false;
            TerminalWindow* window = nullptr;
        };

        std::map<PlatformSubsystem, int> refCounts_;
        std::vector<PlatformEvent> queued_;
        std::uint64_t createdAtNanoseconds_ = 0;
        WindowId nextWindowId_ = 1;
        bool attachedToTerminal_ = false;
        int outputDescriptor_ = -1;
        int inputDescriptor_ = -1;

        mutable bool capabilitiesDetected_ = false;
        mutable TerminalCapabilities detectedCapabilities_;
        mutable std::shared_ptr<TerminalSessionController> sessions_;
        mutable std::shared_ptr<TerminalInputDecoder> inputDecoder_;
        mutable std::unique_ptr<TerminalKeyboard> keyboard_;
        mutable std::unique_ptr<TerminalMouse> mouse_;

        /// The single window slot, held jointly with whatever window occupies it.
        std::shared_ptr<WindowSlot> windowSlot_;

        /// Installed while a window exists, because a resize is only meaningful when there is
        /// something to resize -- and because installing a process-global signal handler merely
        /// to construct a platform would be state a constructor has no business changing.
        std::unique_ptr<TerminalResizeWatcher> resizeWatcher_;

        std::unique_ptr<Common::StandardFileSystem> fileSystem_;
        std::unique_ptr<Common::StandardSystemInfo> systemInfo_;
    };

} // namespace CNA::Platform::Terminal
