// SPDX-License-Identifier: MS-PL

#include "TerminalPlatform.hpp"

#include "TerminalSurfacePresenter.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>

namespace CNA::Platform::Terminal {

    namespace {

        std::uint64_t NowNanoseconds()
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
        }

    } // namespace

    /// The drawing surface, sized in pixels rather than character cells.
    ///
    /// A game asks for the resolution it wants and gets it; turning that into glyphs is the
    /// presenter's job (PLAT-132). The alternative -- reporting the terminal's cell grid as the
    /// window size -- would hand every game a 80x24 "resolution" and break its layout arithmetic
    /// while describing the same screen less usefully.
    class TerminalPlatform::TerminalWindow final : public IPlatformWindow
    {
    public:
        TerminalWindow(std::shared_ptr<WindowSlot> slot, const WindowId id,
                       const WindowDescription& description)
            : slot_(std::move(slot))
            , id_(id)
            , title_(description.title)
            , width_(description.width)
            , height_(description.height)
            , resizable_(description.resizable)
            , visible_(description.visible)
            , mode_(description.fullscreenMode)
        {
        }

        ~TerminalWindow() override
        {
            slot_->taken = false;
            slot_->window = nullptr;
        }

        TerminalWindow(const TerminalWindow&) = delete;
        TerminalWindow& operator=(const TerminalWindow&) = delete;

        [[nodiscard]] WindowId GetId() const override { return id_; }

        [[nodiscard]] NativeWindowHandle GetNativeHandle() const override
        {
            // Every pointer null, so a renderer needing a real native window refuses at selection
            // instead of dereferencing something that was never there.
            NativeWindowHandle handle;
            handle.system = NativeWindowSystem::Terminal;
            return handle;
        }

        [[nodiscard]] std::string GetTitle() const override { return title_; }

        void SetTitle(const std::string& title) override
        {
            // Stored only. A terminal title is set with an OSC sequence, and writing one from
            // here would put escape bytes on standard output before any session owns it --
            // corrupting a redirected log, or a frame the presenter is midway through. The
            // session (PLAT-131) emits it once it has the terminal.
            title_ = title;
        }

        [[nodiscard]] WindowBounds GetClientBounds() const override
        {
            // Always at the origin: a terminal has no desktop to be positioned within, and
            // reporting a fabricated position would be a value a caller could act on.
            return WindowBounds{0, 0, width_, height_};
        }

        [[nodiscard]] WindowSize GetPixelSize() const override { return WindowSize{width_, height_}; }

        void SetSize(const int width, const int height) override
        {
            pendingWidth_ = width;
            pendingHeight_ = height;
        }

        [[nodiscard]] float GetDisplayScale() const override { return 1.0f; }
        void SetResizable(const bool resizable) override { resizable_ = resizable; }
        void SetBorderless(bool) override {}
        void SetFullscreenMode(const WindowFullscreenMode mode) override { mode_ = mode; }
        [[nodiscard]] WindowFullscreenMode GetFullscreenMode() const override { return mode_; }
        void Show() override { visible_ = true; }
        void Hide() override { visible_ = false; }
        void Minimize() override { minimized_ = true; }
        void Maximize() override { minimized_ = false; }
        void Restore() override { minimized_ = false; }

        void Sync() override
        {
            // Models the same asynchrony every other implementation does, so game code written
            // against one behaves identically here.
            width_ = pendingWidth_ > 0 ? pendingWidth_ : width_;
            height_ = pendingHeight_ > 0 ? pendingHeight_ : height_;
        }

        /// Applies a terminal resize. Distinct from SetSize(), which is a *request* a caller
        /// makes and which the contract says lands only on Sync(); this is the terminal telling
        /// the window what it now is, which is not negotiable and takes effect at once.
        void ApplyTerminalSize(const int width, const int height)
        {
            width_ = width;
            height_ = height;
            pendingWidth_ = 0;
            pendingHeight_ = 0;
        }

        [[nodiscard]] bool HasFocus() const override { return visible_ && !minimized_; }
        [[nodiscard]] bool IsMinimized() const override { return minimized_; }
        [[nodiscard]] std::string GetDisplayName() const override { return {}; }

    private:
        std::shared_ptr<WindowSlot> slot_;
        WindowId id_;
        std::string title_;
        int width_ = 0, height_ = 0;
        int pendingWidth_ = 0, pendingHeight_ = 0;
        bool resizable_ = true, visible_ = true, minimized_ = false;
        WindowFullscreenMode mode_ = WindowFullscreenMode::Windowed;
    };

    TerminalPlatform::TerminalPlatform() : TerminalPlatform(STDOUT_FILENO, STDIN_FILENO) {}

    TerminalPlatform::TerminalPlatform(const int outputDescriptor, const int inputDescriptor)
        : createdAtNanoseconds_(NowNanoseconds())
        // Free, and changes no terminal state: no raw mode, no escape sequences, no waiting for a
        // reply. Full detection needs raw mode and belongs to the session (PLAT-131), not here --
        // constructing a platform must stay safe in a process whose terminal belongs to somebody
        // else, which is exactly what the conformance suite does.
        , attachedToTerminal_(isatty(outputDescriptor) != 0)
        , outputDescriptor_(outputDescriptor)
        , inputDescriptor_(inputDescriptor)
        , windowSlot_(std::make_shared<WindowSlot>())
        , fileSystem_(std::make_unique<Common::StandardFileSystem>("cna-terminal"))
        , systemInfo_(std::make_unique<Common::StandardSystemInfo>())
    {
    }

    TerminalPlatform::~TerminalPlatform() = default;

    const std::string& TerminalPlatform::GetName() const
    {
        static const std::string name = "Terminal";
        return name;
    }

    PlatformCapabilities TerminalPlatform::GetCapabilities() const
    {
        EnsureCapabilitiesDetected();
        PlatformCapabilities capabilities;

        // True only where stdout really is a terminal. Reported honestly rather than
        // unconditionally, because the contract's rule is that a false capability refuses and a
        // true one works: advertising presentation into a pipe would be a promise this cannot
        // keep, and a caller branching on it would pick the path that draws nothing.
        capabilities.surfacePresentation = attachedToTerminal_;
        capabilities.exactKeyboardState = detectedCapabilities_.hasKittyKeyboard;

        // The non-Kitty keyboard fallback reports a service with its quality flag false. The
        // terminal mouse follows the same pattern: its service exists but coordinates are cells
        // expanded to nominal client units, so pixelAccurateMouse deliberately remains false.
        return capabilities;
    }

    void TerminalPlatform::EnsureCapabilitiesDetected() const
    {
        if (capabilitiesDetected_)
        {
            return;
        }

        const TerminalCapabilities detected =
            DetectTerminalCapabilities(outputDescriptor_, inputDescriptor_);
        auto sessions = std::make_shared<TerminalSessionController>(
            outputDescriptor_, inputDescriptor_, detected.hasKittyKeyboard);
        std::shared_ptr<TerminalInputDecoder> decoder;
        std::unique_ptr<TerminalKeyboard> keyboard;
        std::unique_ptr<TerminalMouse> mouse;
        if (detected.canQuery)
        {
            decoder = std::make_shared<TerminalInputDecoder>(sessions);
            keyboard = std::make_unique<TerminalKeyboard>(decoder);
            mouse = std::make_unique<TerminalMouse>(decoder);
        }

        // Publish the cache only after every allocation succeeded. A failed first call remains
        // retryable instead of exposing a true capability whose service was never constructed.
        detectedCapabilities_ = detected;
        sessions_ = std::move(sessions);
        inputDecoder_ = std::move(decoder);
        keyboard_ = std::move(keyboard);
        mouse_ = std::move(mouse);
        capabilitiesDetected_ = true;
    }

    void TerminalPlatform::AcquireSubsystem(const PlatformSubsystem subsystem)
    {
        // Refcounted bookkeeping only. Taking the terminal over -- raw mode, alternate screen,
        // hidden cursor -- is PLAT-131's session, which has to guarantee restoration on every
        // exit path including a signal, and so is deliberately not started from here.
        ++refCounts_[subsystem];
    }

    void TerminalPlatform::ReleaseSubsystem(const PlatformSubsystem subsystem)
    {
        const auto it = refCounts_.find(subsystem);
        if (it == refCounts_.end() || it->second == 0)
        {
            return;  // unpaired release is a documented no-op
        }
        --it->second;
    }

    bool TerminalPlatform::IsSubsystemInitialized(const PlatformSubsystem subsystem) const
    {
        const auto it = refCounts_.find(subsystem);
        return it != refCounts_.end() && it->second > 0;
    }

    std::unique_ptr<IPlatformWindow> TerminalPlatform::CreateWindow(const WindowDescription& description)
    {
        if (windowSlot_->taken)
        {
            // There is exactly one terminal. Handing back a second window would alias it onto the
            // first and leave two callers each believing they owned the screen.
            throw PlatformNotSupportedException(PlatformCapability::MultipleWindows, GetName());
        }
        windowSlot_->taken = true;

        // The requested size stands, exactly as it does on any other implementation: a game draws
        // at the resolution it asked for and the presenter quantises. The terminal becomes
        // authoritative only when it *changes* -- which is the same way a real windowing system
        // behaves when a window manager resizes a window after it was created.
        auto window = std::make_unique<TerminalWindow>(windowSlot_, nextWindowId_++, description);
        windowSlot_->window = window.get();

        if (attachedToTerminal_ && !TerminalResizeWatcher::IsWatching())
        {
            resizeWatcher_ = std::make_unique<TerminalResizeWatcher>();
        }
        return window;
    }

    void TerminalPlatform::CellGridToPixelSize(const int columns, const int rows, int& width,
                                               int& height)
    {
        // Nominal, not measured: no escape sequence reports a cell's pixel size, and it depends on
        // the font. 8x16 is the classic VGA text cell and carries the 1:2 ratio the presenter's
        // letterboxing assumes, which is the part that has to be right.
        constexpr int kNominalCellWidth = 8;
        constexpr int kNominalCellHeight = 16;
        width = std::max(1, columns) * kNominalCellWidth;
        height = std::max(1, rows) * kNominalCellHeight;
    }

    void TerminalPlatform::PollEvents(std::vector<PlatformEvent>& destination)
    {
        // Clearing first is the contract: the buffer belongs to the caller and is reused every
        // frame, so stale content from the previous frame must not survive into this one.
        destination.clear();

        if (windowSlot_->window != nullptr && TerminalResizeWatcher::TakePendingResize())
        {
            // SIGWINCH carries no size, so the size is read here rather than in the handler --
            // which is also why coalescing is free: what a caller wants is the current size, and
            // ten resizes between two frames have one current size.
            const TerminalSize grid = QueryTerminalSize(outputDescriptor_);
            int width = 0, height = 0;
            CellGridToPixelSize(grid.columns, grid.rows, width, height);
            windowSlot_->window->ApplyTerminalSize(width, height);

            // Both kinds, because a caller that resizes its backbuffer watches PixelSizeChanged
            // and one that lays out its UI watches Resized. There is no display scaling here, so
            // the two always agree -- and emitting only one would silently break whichever half
            // of a game listened for the other.
            WindowEvent resized;
            resized.window = windowSlot_->window->GetId();
            resized.kind = WindowEventKind::Resized;
            resized.data1 = width;
            resized.data2 = height;
            destination.emplace_back(resized);

            resized.kind = WindowEventKind::PixelSizeChanged;
            destination.emplace_back(resized);
        }

        EnsureCapabilitiesDetected();
        if (inputDecoder_ != nullptr)
        {
            inputDecoder_->PumpAll();
            const WindowId window = windowSlot_->window != nullptr
                                        ? windowSlot_->window->GetId()
                                        : WindowId{0};
            inputDecoder_->DrainEvents(destination, window);
        }

        destination.insert(destination.end(), queued_.begin(), queued_.end());
        queued_.clear();
    }

    std::uint64_t TerminalPlatform::GetPerformanceCounter() const { return NowNanoseconds(); }

    std::uint64_t TerminalPlatform::GetPerformanceFrequency() const { return 1000000000ull; }

    std::uint64_t TerminalPlatform::GetTicksMilliseconds() const
    {
        return (NowNanoseconds() - createdAtNanoseconds_) / 1000000ull;
    }

    void TerminalPlatform::Delay(const std::uint32_t milliseconds)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }

    IPlatformKeyboard* TerminalPlatform::GetKeyboard()
    {
        EnsureCapabilitiesDetected();
        return keyboard_.get();
    }
    IPlatformMouse* TerminalPlatform::GetMouse()
    {
        EnsureCapabilitiesDetected();
        return mouse_.get();
    }
    IPlatformGamepad* TerminalPlatform::GetGamepad() { return nullptr; }
    IPlatformTextInput* TerminalPlatform::GetTextInput() { return nullptr; }
    IPlatformSensors* TerminalPlatform::GetSensors() { return nullptr; }
    IPlatformHaptics* TerminalPlatform::GetHaptics() { return nullptr; }
    IPlatformInputDevices* TerminalPlatform::GetInputDevices() { return nullptr; }
    IPlatformClipboard* TerminalPlatform::GetClipboard() { return nullptr; }
    IPlatformDisplays* TerminalPlatform::GetDisplays() { return nullptr; }
    IPlatformDialogs* TerminalPlatform::GetDialogs() { return nullptr; }
    IPlatformFileSystem* TerminalPlatform::GetFileSystem() { return fileSystem_.get(); }
    IPlatformSystemInfo* TerminalPlatform::GetSystemInfo() { return systemInfo_.get(); }
    IPlatformGlContext* TerminalPlatform::GetGlContext() { return nullptr; }
    IPlatformVulkanSurface* TerminalPlatform::GetVulkanSurface() { return nullptr; }

    std::unique_ptr<IPlatformSurfacePresenter> TerminalPlatform::CreateSurfacePresenter(
        IPlatformWindow& window)
    {
        if (!attachedToTerminal_)
        {
            // Piped into a log, redirected to a file, captured by CI. Refusing here is what stops
            // a build log filling with escape sequences, and it is the same answer
            // GetCapabilities() already gave.
            throw PlatformNotSupportedException(PlatformCapability::SurfacePresentation, GetName());
        }

        EnsureCapabilitiesDetected();

        const WindowSize size = window.GetPixelSize();
        return std::make_unique<TerminalSurfacePresenter>(sessions_,
                                                          detectedCapabilities_.colourDepth,
                                                          size.width, size.height);
    }

    bool TerminalPlatform::IsAttachedToTerminal() const { return attachedToTerminal_; }

} // namespace CNA::Platform::Terminal
