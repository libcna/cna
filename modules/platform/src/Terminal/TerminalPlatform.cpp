// SPDX-License-Identifier: MS-PL

#include "TerminalPlatform.hpp"

#include "TerminalSurfacePresenter.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <unistd.h>

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
        TerminalWindow(std::shared_ptr<bool> slotTaken, const WindowId id,
                       const WindowDescription& description)
            : slotTaken_(std::move(slotTaken))
            , id_(id)
            , title_(description.title)
            , width_(description.width)
            , height_(description.height)
            , resizable_(description.resizable)
            , visible_(description.visible)
            , mode_(description.fullscreenMode)
        {
        }

        ~TerminalWindow() override { *slotTaken_ = false; }

        TerminalWindow(const TerminalWindow&) = delete;
        TerminalWindow& operator=(const TerminalWindow&) = delete;

        [[nodiscard]] WindowId GetId() const override { return id_; }

        [[nodiscard]] NativeWindowHandle GetNativeHandle() const override
        {
            // Every pointer null, so a renderer needing a real native window refuses at selection
            // instead of dereferencing something that was never there.
            NativeWindowHandle handle;
            handle.system = NativeWindowSystem::Headless;
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

        [[nodiscard]] bool HasFocus() const override { return visible_ && !minimized_; }
        [[nodiscard]] bool IsMinimized() const override { return minimized_; }
        [[nodiscard]] std::string GetDisplayName() const override { return {}; }

    private:
        std::shared_ptr<bool> slotTaken_;
        WindowId id_;
        std::string title_;
        int width_ = 0, height_ = 0;
        int pendingWidth_ = 0, pendingHeight_ = 0;
        bool resizable_ = true, visible_ = true, minimized_ = false;
        WindowFullscreenMode mode_ = WindowFullscreenMode::Windowed;
    };

    TerminalPlatform::TerminalPlatform()
        : createdAtNanoseconds_(NowNanoseconds())
        // Free, and changes no terminal state: no raw mode, no escape sequences, no waiting for a
        // reply. Full detection needs raw mode and belongs to the session (PLAT-131), not here --
        // constructing a platform must stay safe in a process whose terminal belongs to somebody
        // else, which is exactly what the conformance suite does.
        , attachedToTerminal_(CNA::Platform::Terminal::IsAttachedToTerminal())
        , windowSlotTaken_(std::make_shared<bool>(false))
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
        PlatformCapabilities capabilities;

        // True only where stdout really is a terminal. Reported honestly rather than
        // unconditionally, because the contract's rule is that a false capability refuses and a
        // true one works: advertising presentation into a pipe would be a promise this cannot
        // keep, and a caller branching on it would pick the path that draws nothing.
        capabilities.surfacePresentation = attachedToTerminal_;

        // Keyboard (PLAT-137/138) and mouse (PLAT-139) stay false until they land.
        return capabilities;
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
        if (*windowSlotTaken_)
        {
            // There is exactly one terminal. Handing back a second window would alias it onto the
            // first and leave two callers each believing they owned the screen.
            throw PlatformNotSupportedException(PlatformCapability::MultipleWindows, GetName());
        }
        *windowSlotTaken_ = true;
        return std::make_unique<TerminalWindow>(windowSlotTaken_, nextWindowId_++, description);
    }

    void TerminalPlatform::PollEvents(std::vector<PlatformEvent>& destination)
    {
        // Clearing first is the contract: the buffer belongs to the caller and is reused every
        // frame, so stale content from the previous frame must not survive into this one.
        destination.clear();
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

    IPlatformKeyboard* TerminalPlatform::GetKeyboard() { return nullptr; }
    IPlatformMouse* TerminalPlatform::GetMouse() { return nullptr; }
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

        // Detection is deferred to exactly this point: it needs raw mode and a round trip to the
        // terminal, which is far too costly and far too intrusive to do when a platform is merely
        // constructed. Here, something is about to draw, so asking is warranted.
        const TerminalCapabilities detected = DetectTerminalCapabilities();

        const WindowSize size = window.GetPixelSize();
        return std::make_unique<TerminalSurfacePresenter>(STDOUT_FILENO, STDIN_FILENO,
                                                          detected.colourDepth, size.width,
                                                          size.height);
    }

    bool TerminalPlatform::IsAttachedToTerminal() const { return attachedToTerminal_; }

} // namespace CNA::Platform::Terminal
