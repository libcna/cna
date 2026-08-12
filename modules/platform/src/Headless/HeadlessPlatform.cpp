// SPDX-License-Identifier: MS-PL

#include "HeadlessPlatform.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <chrono>
#include <thread>

namespace CNA::Platform::Headless {

    namespace {

        std::uint64_t NowNanoseconds()
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
        }

        /// An in-memory window. Real enough that GameWindow and GraphicsDeviceManager behave as
        /// they would on any platform -- it can be resized, retitled and queried -- while exposing
        /// no native handle at all.
        class HeadlessWindow final : public IPlatformWindow
        {
        public:
            HeadlessWindow(const WindowId id, const WindowDescription& description)
                : id_(id)
                , title_(description.title)
                , width_(description.width)
                , height_(description.height)
                , x_(description.centered ? 0 : description.x)
                , y_(description.centered ? 0 : description.y)
                , resizable_(description.resizable)
                , borderless_(description.borderless)
                , visible_(description.visible)
                , mode_(description.fullscreenMode)
            {
            }

            [[nodiscard]] WindowId GetId() const override { return id_; }

            [[nodiscard]] NativeWindowHandle GetNativeHandle() const override
            {
                NativeWindowHandle handle;
                handle.system = NativeWindowSystem::Headless;
                return handle;
            }

            [[nodiscard]] std::string GetTitle() const override { return title_; }
            void SetTitle(const std::string& title) override { title_ = title; }

            [[nodiscard]] WindowBounds GetClientBounds() const override
            {
                return WindowBounds{x_, y_, width_, height_};
            }

            [[nodiscard]] WindowSize GetPixelSize() const override
            {
                // No display scaling, so pixel and logical sizes coincide. Answering both is what
                // keeps a renderer's swapchain sizing code identical across platforms.
                return WindowSize{width_, height_};
            }

            void SetSize(const int width, const int height) override
            {
                pendingWidth_ = width;
                pendingHeight_ = height;
            }

            [[nodiscard]] float GetDisplayScale() const override { return 1.0f; }
            void SetResizable(const bool resizable) override { resizable_ = resizable; }
            void SetBorderless(const bool borderless) override { borderless_ = borderless; }
            void SetFullscreenMode(const WindowFullscreenMode mode) override { mode_ = mode; }
            [[nodiscard]] WindowFullscreenMode GetFullscreenMode() const override { return mode_; }
            void Show() override { visible_ = true; }
            void Hide() override { visible_ = false; }
            void Minimize() override { minimized_ = true; }
            void Maximize() override { minimized_ = false; }
            void Restore() override { minimized_ = false; }

            void Sync() override
            {
                // Models the real asynchrony deliberately: a size request lands only here, the
                // same as on a windowing system. A headless platform that applied it immediately
                // would let a caller depend on behaviour SDL does not provide.
                width_ = pendingWidth_ > 0 ? pendingWidth_ : width_;
                height_ = pendingHeight_ > 0 ? pendingHeight_ : height_;
            }

            [[nodiscard]] bool HasFocus() const override { return visible_ && !minimized_; }
            [[nodiscard]] bool IsMinimized() const override { return minimized_; }
            [[nodiscard]] std::string GetDisplayName() const override { return {}; }

        private:
            WindowId id_;
            std::string title_;
            int width_ = 0, height_ = 0;
            int pendingWidth_ = 0, pendingHeight_ = 0;
            int x_ = 0, y_ = 0;
            bool resizable_ = true, borderless_ = false, visible_ = true, minimized_ = false;
            WindowFullscreenMode mode_ = WindowFullscreenMode::Windowed;
        };

    } // namespace

    HeadlessPlatform::HeadlessPlatform()
        : createdAtNanoseconds_(NowNanoseconds())
        // Real path resolution and file loading rather than stubs. A headless platform still has
        // a filesystem, and a game's save/load path is exactly the kind of logic worth running in
        // CI. The preferences root is implementation-specific so two platforms live in one
        // process cannot collide on the same directory.
        , fileSystem_(std::make_unique<Common::StandardFileSystem>("cna-headless"))
        , systemInfo_(std::make_unique<Common::StandardSystemInfo>())
    {
    }

    HeadlessPlatform::~HeadlessPlatform() = default;

    const std::string& HeadlessPlatform::GetName() const
    {
        static const std::string name = "Headless";
        return name;
    }

    PlatformCapabilities HeadlessPlatform::GetCapabilities() const
    {
        // Everything false by default. Only what this platform genuinely does is turned on, which
        // is what makes it exercise every refusal path in the contract.
        return PlatformCapabilities{};
    }

    void HeadlessPlatform::AcquireSubsystem(const PlatformSubsystem subsystem)
    {
        // Every subsystem "starts", because there is nothing to start. Refusing would make the
        // platform useless for running game logic, which is its whole purpose.
        ++refCounts_[subsystem];
    }

    void HeadlessPlatform::ReleaseSubsystem(const PlatformSubsystem subsystem)
    {
        const auto it = refCounts_.find(subsystem);
        if (it == refCounts_.end() || it->second == 0)
        {
            return;  // unpaired release is a documented no-op
        }
        --it->second;
    }

    bool HeadlessPlatform::IsSubsystemInitialized(const PlatformSubsystem subsystem) const
    {
        const auto it = refCounts_.find(subsystem);
        return it != refCounts_.end() && it->second > 0;
    }

    std::unique_ptr<IPlatformWindow> HeadlessPlatform::CreateWindow(const WindowDescription& description)
    {
        return std::make_unique<HeadlessWindow>(nextWindowId_++, description);
    }

    void HeadlessPlatform::PollEvents(std::vector<PlatformEvent>& destination)
    {
        destination.clear();
        destination.insert(destination.end(), queued_.begin(), queued_.end());
        queued_.clear();
    }

    void HeadlessPlatform::InjectEvent(const PlatformEvent& event) { queued_.push_back(event); }

    std::uint64_t HeadlessPlatform::GetPerformanceCounter() const { return NowNanoseconds(); }

    std::uint64_t HeadlessPlatform::GetPerformanceFrequency() const { return 1000000000ull; }

    std::uint64_t HeadlessPlatform::GetTicksMilliseconds() const
    {
        return (NowNanoseconds() - createdAtNanoseconds_) / 1000000ull;
    }

    void HeadlessPlatform::Delay(const std::uint32_t milliseconds)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }

    IPlatformKeyboard* HeadlessPlatform::GetKeyboard() { return nullptr; }
    IPlatformMouse* HeadlessPlatform::GetMouse() { return nullptr; }
    IPlatformGamepad* HeadlessPlatform::GetGamepad() { return nullptr; }
    IPlatformTextInput* HeadlessPlatform::GetTextInput() { return nullptr; }
    IPlatformSensors* HeadlessPlatform::GetSensors() { return nullptr; }
    IPlatformHaptics* HeadlessPlatform::GetHaptics() { return nullptr; }
    IPlatformInputDevices* HeadlessPlatform::GetInputDevices() { return nullptr; }
    IPlatformClipboard* HeadlessPlatform::GetClipboard() { return nullptr; }
    IPlatformDisplays* HeadlessPlatform::GetDisplays() { return nullptr; }
    IPlatformDialogs* HeadlessPlatform::GetDialogs() { return nullptr; }
    IPlatformFileSystem* HeadlessPlatform::GetFileSystem() { return fileSystem_.get(); }
    IPlatformSystemInfo* HeadlessPlatform::GetSystemInfo() { return systemInfo_.get(); }
    IPlatformGlContext* HeadlessPlatform::GetGlContext() { return nullptr; }
    IPlatformVulkanSurface* HeadlessPlatform::GetVulkanSurface() { return nullptr; }

    std::unique_ptr<IPlatformSurfacePresenter> HeadlessPlatform::CreateSurfacePresenter(IPlatformWindow&)
    {
        // Refuses deterministically, naming the capability -- never a stub that accepts frames
        // and drops them, which would let a caller believe it was drawing.
        throw PlatformNotSupportedException(PlatformCapability::SurfacePresentation, GetName());
    }

} // namespace CNA::Platform::Headless
