// SPDX-License-Identifier: MS-PL

#include "HeadlessPlatform.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
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

    /// Real path resolution and file loading, implemented on the standard library rather than
    /// stubbed out. A headless platform still has a filesystem, and a game's save/load path is
    /// exactly the kind of logic worth running in CI.
    class HeadlessPlatform::HeadlessFileSystem final : public IPlatformFileSystem
    {
    public:
        [[nodiscard]] std::string GetBasePath() const override
        {
            return std::filesystem::current_path().string() + "/";
        }

        [[nodiscard]] std::string GetPreferencesPath(const std::string& organization,
                                                     const std::string& application) const override
        {
            const std::filesystem::path path =
                std::filesystem::temp_directory_path() / "cna-headless" / organization / application;
            std::error_code code;
            std::filesystem::create_directories(path, code);
            if (code)
            {
                throw PlatformException("FileSystem::GetPreferencesPath", code.message());
            }
            return path.string() + "/";
        }

        [[nodiscard]] bool TryLoadFile(const std::string& path,
                                       std::vector<std::uint8_t>& data) const override
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input.good())
            {
                return false;
            }
            const std::streamsize size = input.tellg();
            input.seekg(0);

            std::vector<std::uint8_t> contents(static_cast<std::size_t>(size));
            if (size > 0 && !input.read(reinterpret_cast<char*>(contents.data()), size))
            {
                return false;
            }
            data = std::move(contents);
            return true;
        }

        void CreateDirectory(const std::string& path) override
        {
            std::error_code code;
            std::filesystem::create_directories(path, code);
            if (code)
            {
                throw PlatformException("FileSystem::CreateDirectory(" + path + ")", code.message());
            }
        }
    };

    /// Host facts the standard library can answer. Everything it cannot is reported as unknown
    /// rather than guessed -- a fabricated core count is worse than an honest zero.
    class HeadlessPlatform::HeadlessSystemInfo final : public IPlatformSystemInfo
    {
    public:
        [[nodiscard]] std::string GetPlatformName() const override
        {
#if defined(_WIN32)
            return "Windows";
#elif defined(__APPLE__)
            return "macOS";
#elif defined(__ANDROID__)
            return "Android";
#elif defined(__linux__)
            return "Linux";
#else
            return "Unknown";
#endif
        }

        [[nodiscard]] int GetSystemMemoryMegabytes() const override { return 0; }

        [[nodiscard]] int GetLogicalCoreCount() const override
        {
            return static_cast<int>(std::thread::hardware_concurrency());
        }

        [[nodiscard]] std::vector<PlatformLocale> GetPreferredLocales() const override { return {}; }

        [[nodiscard]] PowerInfo GetPowerInfo() const override
        {
            // Unknown, not "no battery": this platform cannot tell, and claiming otherwise would
            // be a fabricated answer a caller could display.
            return PowerInfo{};
        }

        bool OpenUrl(const std::string&) override { return false; }
    };

    HeadlessPlatform::HeadlessPlatform()
        : createdAtNanoseconds_(NowNanoseconds())
        , fileSystem_(std::make_unique<HeadlessFileSystem>())
        , systemInfo_(std::make_unique<HeadlessSystemInfo>())
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
