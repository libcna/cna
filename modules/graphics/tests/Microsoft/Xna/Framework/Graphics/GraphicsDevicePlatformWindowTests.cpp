// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/DefaultWindowTitle.hpp"
#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererRegistry.hpp"
#include "CNA/Platform/IPlatformSurfacePresenter.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "System/Environment.hpp"
#include "System/NotSupportedException.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Internal
{
    /**
     * @brief Reaches `GraphicsDevice`'s internal viewport refresh, the way `GameWindow` does.
     *
     * `UpdateViewportFromWindow()` is private and `GameWindow` is a friend, because that call is
     * the framework's own reaction to a resize rather than something a game invokes. The test
     * below reproduces exactly that reaction, so it needs the same access -- through a named test
     * peer `GraphicsDevice` befriends, rather than by widening the XNA-visible API.
     */
    class GraphicsDevicePlatformWindowTestPeer
    {
    public:
        /**
         * @brief Runs the viewport refresh a window resize triggers.
         *
         * @param device The device to refresh.
         */
        static void RefreshViewport(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device)
        {
            device.UpdateViewportFromWindow();
        }

        // Exercise the shared family refusal without constructing an FNA3D GPU device. The
        // renderer stays SOFTWARE; the scoped test below restores its real descriptor.
        static const CNA::Internal::Renderers::GraphicsRendererDescriptor* SetPresentIdentity(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
            const CNA::Internal::Renderers::GraphicsRendererDescriptor* descriptor)
        {
            const auto* previous = device.activeDescriptor_;
            device.activeDescriptor_ = descriptor;
            return previous;
        }
    };
}

namespace {

using CNA::Platform::IPlatformSurfacePresenter;
using CNA::Platform::IPlatformWindow;
using CNA::Platform::NativeWindowHandle;
using CNA::Platform::NativeWindowSystem;
using CNA::Platform::PlatformCapabilities;
using CNA::Platform::PlatformSubsystem;
using CNA::Platform::PresentFilter;
using CNA::Platform::PresentScaleMode;
using CNA::Platform::SurfaceFrame;
using CNA::Platform::WindowBounds;
using CNA::Platform::WindowDescription;
using CNA::Platform::WindowFullscreenMode;
using CNA::Platform::WindowId;
using CNA::Platform::WindowSize;
using CNA::Platform::Testing::PlatformTestDecorator;
using CNA::Platform::Testing::ScopedCurrentPlatform;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

struct WindowOwnershipTrace
{
    std::vector<std::string> events;
    WindowDescription description;
    std::uintptr_t adoptedHandle = 0;
    bool throwFromSync = false;
    bool throwFromPixelSize = false;
};

class TracedWindow final : public IPlatformWindow
{
public:
    explicit TracedWindow(WindowOwnershipTrace& trace, const std::uintptr_t handle = 0x6200u,
                          const std::uintptr_t native = 0, int width = 1, int height = 1)
        : trace_(trace), handle_(handle), native_(native), width_(width), height_(height)
    {
    }

    ~TracedWindow() override
    {
        trace_.events.emplace_back(native_ ? "window-destroyed-" + std::to_string(handle_)
                                          : "window-destroyed");
    }

    [[nodiscard]] WindowId GetId() const override { return native_ ? handle_ : 0x6200u; }
    [[nodiscard]] std::uintptr_t GetWindowHandle() const override { return handle_; }
    [[nodiscard]] NativeWindowHandle GetNativeHandle() const override
    {
        NativeWindowHandle handle;
        handle.system = native_ ? NativeWindowSystem::X11 : NativeWindowSystem::Headless;
        if (native_) {
            handle.display = reinterpret_cast<void*>(1);
            handle.windowId = native_;
        }
        return handle;
    }
    [[nodiscard]] std::string GetTitle() const override { return title_; }
    void SetTitle(const std::string& title) override { title_ = title; }
    [[nodiscard]] WindowBounds GetClientBounds() const override
    {
        return WindowBounds{0, 0, width_, height_};
    }
    [[nodiscard]] WindowSize GetPixelSize() const override
    {
        if (trace_.throwFromPixelSize)
        {
            throw std::runtime_error("synthetic drawable-size query failure");
        }
        return WindowSize{width_ * 2, height_ * 2};
    }
    void SetSize(const int width, const int height) override
    {
        trace_.events.emplace_back("size-requested");
        pendingWidth_ = width;
        pendingHeight_ = height;
    }
    [[nodiscard]] float GetDisplayScale() const override { return 2.0f; }
    [[nodiscard]] bool IsResizable() const override { return true; }
    void SetResizable(bool) override {}
    [[nodiscard]] bool IsBorderless() const override { return false; }
    void SetBorderless(bool) override {}
    void SetFullscreenMode(const WindowFullscreenMode mode) override
    {
        trace_.events.emplace_back("fullscreen-applied");
        mode_ = mode;
    }
    [[nodiscard]] WindowFullscreenMode GetFullscreenMode() const override { return mode_; }
    void Show() override {}
    void Hide() override {}
    void Minimize() override {}
    void Maximize() override {}
    void Restore() override {}
    void Sync() override
    {
        trace_.events.emplace_back("window-synced");
        if (trace_.throwFromSync)
        {
            throw std::runtime_error("synthetic window synchronization failure");
        }
        width_ = pendingWidth_;
        height_ = pendingHeight_;
    }
    [[nodiscard]] bool HasFocus() const override { return true; }
    [[nodiscard]] bool IsMinimized() const override { return false; }
    [[nodiscard]] std::string GetDisplayName() const override { return {}; }

private:
    WindowOwnershipTrace& trace_;
    std::uintptr_t handle_;
    std::uintptr_t native_ = 0;
    std::string title_ = "Game";
    int width_ = 1;
    int height_ = 1;
    int pendingWidth_ = 1;
    int pendingHeight_ = 1;
    WindowFullscreenMode mode_ = WindowFullscreenMode::Windowed;
};

class TracedPresenter final : public IPlatformSurfacePresenter
{
public:
    explicit TracedPresenter(WindowOwnershipTrace& trace)
        : trace_(trace)
    {
    }

    ~TracedPresenter() override { trace_.events.emplace_back("presenter-destroyed"); }

    void SetScaleMode(PresentScaleMode, PresentFilter) override {}
    bool SetVSync(bool) override { return true; }
    void Present(const SurfaceFrame&) override {}
    void GetTargetSize(int& width, int& height) const override
    {
        width = 0;
        height = 0;
    }

private:
    WindowOwnershipTrace& trace_;
};

/// Which display route `TracedPlatform` reports.
enum class TracedDisplay
{
    /// The wrapped platform's own capabilities.
    Inherited,
    /// A screen reachable only through a surface presenter: `SurfacePresentation` without a
    /// `NativeWindowHandle`, which is what TERMINAL reports on a TTY. `GraphicsDevice` then gives a
    /// CPU-raster renderer a platform window and a presenter -- a renderer that never looks at a
    /// native handle, so the fake windows below are enough for a real device to be built on them.
    PresenterOnly,
};

class TracedPlatform final : public PlatformTestDecorator
{
public:
    explicit TracedPlatform(WindowOwnershipTrace& trace,
                            const TracedDisplay display = TracedDisplay::Inherited)
        : trace_(trace), display_(display)
    {
    }

    [[nodiscard]] PlatformCapabilities GetCapabilities() const override
    {
        auto capabilities = PlatformTestDecorator::GetCapabilities();
        if (display_ == TracedDisplay::PresenterOnly)
        {
            capabilities.surfacePresentation = true;
            capabilities.nativeWindowHandle = false;
        }
        return capabilities;
    }

    [[nodiscard]] std::unique_ptr<IPlatformSurfacePresenter> CreateSurfacePresenter(
        IPlatformWindow& window) override
    {
        if (display_ != TracedDisplay::PresenterOnly)
            return PlatformTestDecorator::CreateSurfacePresenter(window);
        trace_.events.emplace_back("presenter-created");
        return std::make_unique<TracedPresenter>(trace_);
    }

    void AcquireSubsystem(const PlatformSubsystem subsystem) override
    {
        EXPECT_EQ(subsystem, PlatformSubsystem::Video);
        trace_.events.emplace_back("video-acquired");
        videoAcquired_ = true;
    }

    void ReleaseSubsystem(const PlatformSubsystem subsystem) override
    {
        EXPECT_EQ(subsystem, PlatformSubsystem::Video);
        trace_.events.emplace_back("video-released");
        videoAcquired_ = false;
    }

    [[nodiscard]] bool IsSubsystemInitialized(const PlatformSubsystem subsystem) const override
    {
        return subsystem == PlatformSubsystem::Video && videoAcquired_;
    }

    [[nodiscard]] std::unique_ptr<IPlatformWindow> CreateWindow(
        const WindowDescription& description) override
    {
        trace_.events.emplace_back("window-created");
        trace_.description = description;
        return std::make_unique<TracedWindow>(trace_);
    }

    [[nodiscard]] std::unique_ptr<IPlatformWindow> AdoptWindowHandle(
        const std::uintptr_t handle) override
    {
        trace_.events.emplace_back("window-adopted");
        trace_.adoptedHandle = handle;
        return std::make_unique<TracedWindow>(trace_, handle);
    }

private:
    WindowOwnershipTrace& trace_;
    TracedDisplay display_;
    bool videoAcquired_ = false;
};

class GraphicsDeviceWindowDescriptionTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        CNA::GraphicsRendererSelection::ResetForTestingEXT();
        System::Environment::SetEnvironmentVariable(
            "CNA_DEBUG_FAIL_RENDERER_INIT", std::string{});

        std::vector<CNA::GraphicsRendererType> available;
        for (const auto& descriptor :
             CNA::Internal::Renderers::GraphicsRendererRegistry::All())
        {
            available.push_back(descriptor.type);
        }
        CNA::GraphicsRendererSelectionAccessEXT::PublishAvailable(
            available,
            CNA::Internal::Renderers::GraphicsRendererRegistry::Default().type);
    }

    void TearDown() override
    {
        System::Environment::SetEnvironmentVariable(
            "CNA_DEBUG_FAIL_RENDERER_INIT", std::string{});
        CNA::GraphicsRendererSelection::ResetForTestingEXT();
    }
};

TEST_F(GraphicsDeviceWindowDescriptionTest,
       XnaOwnedWindowIsNonResizableFromCreation)
{
    // The renderer the device will try, not the build default: a multi-renderer build may be run
    // with CNA_GRAPHICS_RENDERER naming a windowless one, and forcing the default's failure then
    // lets a device construct without any window to observe.
    const auto* selected = CNA::Internal::Renderers::GraphicsRendererRegistry::Find(
        CNA::GraphicsRendererSelection::GetSelected());
    ASSERT_NE(selected, nullptr);
    const auto& descriptor = *selected;
    if (!descriptor.needsWindow)
    {
        GTEST_SKIP() << descriptor.name << " does not create a platform window";
    }

    WindowOwnershipTrace trace;
    TracedPlatform platform(trace);
    ScopedCurrentPlatform current(platform);

    // Stop after the complete window-creation/application path but before the native renderer
    // can interpret TracedWindow's deliberately toolkit-neutral handle.
    System::Environment::SetEnvironmentVariable(
        "CNA_DEBUG_FAIL_RENDERER_INIT", std::string(descriptor.name));
    EXPECT_THROW((void) GraphicsDevice(), std::exception);

    ASSERT_NE(std::find(trace.events.begin(), trace.events.end(), "window-created"),
              trace.events.end());
    EXPECT_FALSE(trace.description.resizable)
        << "XNA GameWindow.AllowUserResizing defaults to false";
}

// L15. These cases need a REAL device built on a FAKE window, so that the window lifetime they assert
// is the one GraphicsDevice actually drives. Every renderer whose descriptor sets `needsWindow`
// interprets the native handle (the retired VULKAN renderer reached a real surface service holding
// a window id it had never seen), so none of them can. SOFTWARE can: on a platform that reports a
// surface presenter as its only display route, GraphicsDevice gives it a platform window and a
// presenter and never asks the window for a native handle. `TracedDisplay::PresenterOnly` is that
// platform, whatever CNA_PLATFORM the build selected, so the cases run in every build that contains
// SOFTWARE -- including SDL3 ones, where they used to skip unconditionally.
class GraphicsDevicePlatformWindowTests : public GraphicsDeviceWindowDescriptionTest
{
protected:
    void SetUp() override
    {
        GraphicsDeviceWindowDescriptionTest::SetUp();
        if (CNA::Internal::Renderers::GraphicsRendererRegistry::Find(
                CNA::GraphicsRendererType::Software) == nullptr)
        {
            GTEST_SKIP() << "needs the SOFTWARE renderer, the one family that builds a device on a "
                            "fake platform window";
        }
        CNA::GraphicsRendererSelection::SetPreferred(CNA::GraphicsRendererType::Software);
    }

    /**
     * @brief Settles the default adapter's own video reference before tracing starts.
     *
     * The adapter pins the video subsystem of whichever platform is current, once. That reference
     * is the adapter's, not the device's, so it is taken here and the trace starts after it.
     *
     * @param trace The trace to clear afterwards.
     */
    static void PrimeAdapter(WindowOwnershipTrace& trace)
    {
        (void)Microsoft::Xna::Framework::Graphics::GraphicsAdapter::getDefaultAdapterProperty();
        trace.events.clear();
    }
};

TEST_F(GraphicsDevicePlatformWindowTests,
       OwnsThePlatformWindowAndDestroysItAfterItsPresenter)
{
    WindowOwnershipTrace trace;
    TracedPlatform platform(trace, TracedDisplay::PresenterOnly);
    ScopedCurrentPlatform current(platform);
    PrimeAdapter(trace);

    {
        GraphicsDevice device;
        // The window title is not a literal: it is the declared assembly title, then the running
        // executable's own name, then "Game". This test binary supplies the middle one, so the
        // assertion is against the same derivation the product uses.
        EXPECT_EQ(trace.description.title, CNA::Internal::GetDefaultWindowTitle());
        EXPECT_EQ(trace.description.width, 800);
        EXPECT_EQ(trace.description.height, 480);
        // WindowDescription's CNA-native default is resizable, but GraphicsDevice creates an XNA
        // GameWindow, whose AllowUserResizing property defaults to false.
        EXPECT_FALSE(trace.description.resizable);
        // A CPU-raster family declares needsVideoSubsystem false, and the presenter-only route
        // must not take a video reference nothing asked for.
        EXPECT_EQ(trace.events, (std::vector<std::string>{
            "window-created", "fullscreen-applied", "size-requested", "window-synced",
            "presenter-created"}));
        // Without a native handle the platform token must not masquerade as one.
        EXPECT_EQ(device.getPresentationParametersProperty().getDeviceWindowHandleProperty(), 0u);
    }

    // The presenter was created against the window, so it goes first.
    EXPECT_EQ(trace.events, (std::vector<std::string>{
        "window-created", "fullscreen-applied", "size-requested", "window-synced",
        "presenter-created", "presenter-destroyed", "window-destroyed"}));
}

TEST_F(GraphicsDevicePlatformWindowTests,
       ConstructorFailureDestroysTheWindowItCreated)
{
    WindowOwnershipTrace trace;
    trace.throwFromSync = true;
    TracedPlatform platform(trace, TracedDisplay::PresenterOnly);
    ScopedCurrentPlatform current(platform);
    PrimeAdapter(trace);

    EXPECT_THROW((void)GraphicsDevice(), std::runtime_error);
    // The failure is in the window's first synchronization, before any renderer or presenter
    // exists; the window CNA created is still destroyed rather than leaked.
    EXPECT_EQ(trace.events, (std::vector<std::string>{
        "window-created", "fullscreen-applied", "size-requested", "window-synced",
        "window-destroyed"}));
}

TEST_F(GraphicsDevicePlatformWindowTests,
       ExternalWindowTokenIsInterpretedOnlyByThePlatform)
{
    constexpr std::uintptr_t token = 0xCAFE1234u;
    WindowOwnershipTrace trace;
    TracedPlatform platform(trace, TracedDisplay::PresenterOnly);
    ScopedCurrentPlatform current(platform);
    PrimeAdapter(trace);

    // XNA's PresentationParameters() is a 0x0 fullscreen back buffer, which no renderer can build;
    // this case is about the window token, so it states the windowed size GraphicsDevice() uses.
    Microsoft::Xna::Framework::Graphics::PresentationParameters parameters;
    parameters.setBackBufferWidthProperty(800);
    parameters.setBackBufferHeightProperty(480);
    parameters.setIsFullScreenProperty(false);
    parameters.setDeviceWindowHandleProperty(token);
    {
        GraphicsDevice device(
            Microsoft::Xna::Framework::Graphics::GraphicsAdapter::getDefaultAdapterProperty(),
            Microsoft::Xna::Framework::Graphics::GraphicsProfile::Reach,
            parameters);
        EXPECT_EQ(trace.adoptedHandle, token);
        EXPECT_EQ(
            device.getPresentationParametersProperty().getDeviceWindowHandleProperty(), token);
    }

    // An adopted window still belongs to the caller: GraphicsDevice applies its parameters and
    // presents into it, and releasing the platform's wrapper is the platform's business.
    EXPECT_EQ(trace.events, (std::vector<std::string>{
        "window-adopted", "fullscreen-applied", "size-requested", "window-synced",
        "presenter-created", "presenter-destroyed", "window-destroyed"}));
}

TEST_F(GraphicsDevicePlatformWindowTests,
       AViewportRefreshSurvivesAWindowThatRefusesItsDrawableSize)
{
    // UpdateViewportFromWindow() is what GameWindow.ClientSizeChanged runs, from inside the frame's
    // event pump, because the operating system or the browser delivered a resize. A window that
    // refuses a query there must cost the game one viewport refresh, not the whole game loop --
    // this reproduces a transient platform size-query refusal without coupling the test to SDL.
    WindowOwnershipTrace trace;
    TracedPlatform platform(trace, TracedDisplay::PresenterOnly);
    ScopedCurrentPlatform current(platform);
    PrimeAdapter(trace);

    GraphicsDevice device;
    ASSERT_NE(std::find(trace.events.begin(), trace.events.end(), "window-created"),
              trace.events.end());
    const auto widthBefore = device.getViewportProperty().getWidthProperty();
    const auto heightBefore = device.getViewportProperty().getHeightProperty();

    trace.throwFromPixelSize = true;
    EXPECT_NO_THROW(CNA::Internal::GraphicsDevicePlatformWindowTestPeer::RefreshViewport(device));

    // The refusal is absorbed, not acted on: the viewport keeps the value it already had rather
    // than collapsing to whatever a failed query left behind.
    EXPECT_EQ(device.getViewportProperty().getWidthProperty(), widthBefore);
    EXPECT_EQ(device.getViewportProperty().getHeightProperty(), heightBefore);
}

TEST_F(GraphicsDevicePlatformWindowTests,
       DeviceCreationStillFailsOnAWindowThatRefusesItsDrawableSize)
{
    // The other half of the contract, and the reason the tolerance above is scoped to one block
    // rather than to the query itself: creating a renderer for a surface whose size cannot be
    // determined is a genuine failure the caller asked for and can act on. Only the unsolicited,
    // event-driven refresh absorbs it.
    WindowOwnershipTrace trace;
    trace.throwFromPixelSize = true;
    TracedPlatform platform(trace, TracedDisplay::PresenterOnly);
    ScopedCurrentPlatform current(platform);
    PrimeAdapter(trace);

    EXPECT_ANY_THROW((void)GraphicsDevice());
    EXPECT_NE(std::find(trace.events.begin(), trace.events.end(), "window-destroyed"),
              trace.events.end());
}

} // namespace


namespace {
// The approved native contract is exercised through canned platform services, without SDL,
// native-window creation, or an output device. Pixel qualification is a separate backend test.
struct PresentBindingsTrace
{
    WindowOwnershipTrace windows;
    int adoptions = 0;
    int presenters = 0;
    bool refuseAdoption = false;
    IPlatformWindow* borrowed = nullptr;
    struct Frame {
        WindowId target;
        bool ordinary;
        CNA::Platform::SurfaceDestination destination;
        std::vector<std::uint8_t> bytes;
    };
    std::vector<Frame> frames;
};
class BindingPresenter final : public IPlatformSurfacePresenter
{
public:
    BindingPresenter(PresentBindingsTrace& trace, IPlatformWindow& target)
        : trace_(trace), target_(target) { ++trace_.presenters; }
    ~BindingPresenter() override {
        trace_.windows.events.push_back("presenter-destroyed-"+std::to_string(target_.GetId()));
    }
    void SetScaleMode(PresentScaleMode,PresentFilter) override {}
    bool SetVSync(bool) override { return false; }
    void GetTargetSize(int& w,int& h) const override {
        const auto size=target_.GetPixelSize(); w=size.width; h=size.height;
    }
    void Present(const SurfaceFrame& frame) override { Record(frame,{},true); }
    bool PresentRegion(const SurfaceFrame& frame,const CNA::Platform::SurfaceDestination& dest) override {
        Record(frame,dest,false); return true;
    }
private:
    void Record(const SurfaceFrame& frame,const CNA::Platform::SurfaceDestination& dest,bool ordinary) {
        PresentBindingsTrace::Frame output{target_.GetId(),ordinary,dest,{}};
        const int stride=frame.strideBytes ? frame.strideBytes : frame.width*4;
        for(int y=0;y<frame.height;++y)
            output.bytes.insert(output.bytes.end(),frame.pixels+y*stride,frame.pixels+y*stride+frame.width*4);
        trace_.frames.push_back(std::move(output));
    }
    PresentBindingsTrace& trace_;
    IPlatformWindow& target_;
};
class PresentBindingsPlatform final : public PlatformTestDecorator
{
public:
    explicit PresentBindingsPlatform(PresentBindingsTrace& t)
        : PlatformTestDecorator(CNA::Platform::PlatformFactory::Create("Headless")),trace(t) {}
    PlatformCapabilities GetCapabilities() const override {
        auto caps=PlatformTestDecorator::GetCapabilities();
        caps.surfacePresentation=true; caps.nativeWindowHandle=true; caps.multipleWindows=true;
        return caps;
    }
    void AcquireSubsystem(PlatformSubsystem) override { acquired=true; }
    void ReleaseSubsystem(PlatformSubsystem) override { acquired=false; }
    bool IsSubsystemInitialized(PlatformSubsystem) const override { return acquired; }
    std::unique_ptr<IPlatformWindow> AdoptWindowHandle(std::uintptr_t token) override {
        EXPECT_EQ(token,0x1000u);
        return std::make_unique<TracedWindow>(trace.windows,token,0x9000u,8,8);
    }
    std::unique_ptr<IPlatformWindow> AdoptNativeWindowHandle(std::uintptr_t native) override {
        ++trace.adoptions;
        EXPECT_EQ(native,0xA000u); // Distinct native identity and legacy token catch pointer casts.
        if(trace.refuseAdoption) throw CNA::Platform::PlatformException("native test","refused");
        auto window=std::make_unique<TracedWindow>(trace.windows,0x2000u,native,12,10);
        trace.borrowed=window.get();
        return window;
    }
    std::unique_ptr<IPlatformSurfacePresenter> CreateSurfacePresenter(IPlatformWindow& window) override {
        return std::make_unique<BindingPresenter>(trace,window);
    }
    PresentBindingsTrace& trace;
    bool acquired=false;
};
class Gap08PresentBindingTest : public GraphicsDevicePlatformWindowTests
{
protected:
    static Microsoft::Xna::Framework::Graphics::PresentationParameters Parameters() {
        auto p=Microsoft::Xna::Framework::Graphics::PresentationParameters::CreateWindowedEXT(8,8);
        p.setDeviceWindowHandleProperty(0x1000);
        return p;
    }
    std::unique_ptr<GraphicsDevice> Device() {
        using namespace Microsoft::Xna::Framework::Graphics;
        return std::make_unique<GraphicsDevice>(GraphicsAdapter::getDefaultAdapterProperty(),
                                                GraphicsProfile::HiDef,Parameters());
    }
    static void ExpectRed(const std::vector<std::uint8_t>& bytes) {
        ASSERT_EQ(bytes.size(),256u);
        for(std::size_t i=0;i<bytes.size();i+=4) {
            EXPECT_EQ(bytes[i],255); EXPECT_EQ(bytes[i+1],0);
            EXPECT_EQ(bytes[i+2],0); EXPECT_EQ(bytes[i+3],255);
        }
    }
};
TEST_F(Gap08PresentBindingTest, ExplicitSoftwareAttachmentPresentsToTheRequestedWindow)
{
    PresentBindingsTrace trace;
    PresentBindingsPlatform platform(trace);
    ScopedCurrentPlatform current(platform);
    auto device=Device();
    device->Clear(Microsoft::Xna::Framework::Color::Red);
    device->Present();
    ASSERT_EQ(trace.frames.size(),1u);
    EXPECT_EQ(trace.frames[0].target,0x1000u);
    EXPECT_TRUE(trace.frames[0].ordinary);
    ExpectRed(trace.frames[0].bytes);
}
TEST_F(Gap08PresentBindingTest, NativeOverrideUsesItsOwnBoundsAndTheOriginalDestination)
{
    PresentBindingsTrace trace;
    PresentBindingsPlatform platform(trace);
    ScopedCurrentPlatform current(platform);
    auto device=Device();
    device->Clear(Microsoft::Xna::Framework::Color::Red);
    device->Present(std::nullopt,Microsoft::Xna::Framework::Rectangle(18,2,4,4),0xA000);
    ASSERT_EQ(trace.frames.size(),1u);
    const auto& frame=trace.frames[0];
    EXPECT_EQ(frame.target,0x2000u); EXPECT_FALSE(frame.ordinary);
    EXPECT_EQ(frame.destination.x,18); EXPECT_EQ(frame.destination.y,2);
    EXPECT_EQ(frame.destination.width,4); EXPECT_EQ(frame.destination.height,4);
    ExpectRed(frame.bytes);
    EXPECT_EQ(device->getPresentationParametersProperty().getDeviceWindowHandleProperty(),0x1000u);
}
TEST_F(Gap08PresentBindingTest, RepeatedOverridesReuseBindingsAndKeepOrdinaryWindow)
{
    PresentBindingsTrace trace;
    PresentBindingsPlatform platform(trace);
    ScopedCurrentPlatform current(platform);
    auto device=Device();
    device->Clear(Microsoft::Xna::Framework::Color::Red);
    device->Present(std::nullopt,std::nullopt,0xA000);
    device->Clear(Microsoft::Xna::Framework::Color::Blue);
    device->Present(std::nullopt,std::nullopt,0xA000);
    device->Present();
    EXPECT_EQ(trace.adoptions,1); EXPECT_EQ(trace.presenters,2);
    ASSERT_EQ(trace.frames.size(),3u);
    EXPECT_EQ(trace.frames[0].target,0x2000u); EXPECT_EQ(trace.frames[1].target,0x2000u);
    EXPECT_EQ(trace.frames[2].target,0x1000u); EXPECT_TRUE(trace.frames[2].ordinary);
    ExpectRed(trace.frames[0].bytes);
    EXPECT_EQ(trace.frames[1].bytes[0],0); EXPECT_EQ(trace.frames[1].bytes[2],255);
}
TEST_F(Gap08PresentBindingTest, NullDestinationUsesTheForeignPhysicalSizeAndTracksResize)
{
    PresentBindingsTrace trace;
    PresentBindingsPlatform platform(trace);
    ScopedCurrentPlatform current(platform);
    auto device=Device();
    device->Clear(Microsoft::Xna::Framework::Color::Red);
    device->Present(std::nullopt,std::nullopt,0xA000);
    ASSERT_NE(trace.borrowed,nullptr);
    trace.borrowed->SetSize(14,11); trace.borrowed->Sync();
    device->Present(std::nullopt,std::nullopt,0xA000);
    ASSERT_EQ(trace.frames.size(),2u);
    // Canned scale2: 12x10 logical ->24x20 physical, resized14x11 ->28x22 physical.
    EXPECT_EQ(trace.frames[0].destination.width,24); EXPECT_EQ(trace.frames[0].destination.height,20);
    EXPECT_EQ(trace.frames[1].destination.width,28); EXPECT_EQ(trace.frames[1].destination.height,22);
    EXPECT_EQ(trace.adoptions,1);
}
TEST_F(Gap08PresentBindingTest, ReleasesPresentersBeforeTheirBorrowedWindows)
{
    PresentBindingsTrace trace;
    PresentBindingsPlatform platform(trace);
    ScopedCurrentPlatform current(platform);
    {
        auto device=Device();
        device->Present(std::nullopt,std::nullopt,0xA000);
    }
    const auto& events=trace.windows.events;
    const auto presenter=std::find(events.begin(),events.end(),"presenter-destroyed-8192");
    const auto window=std::find(events.begin(),events.end(),"window-destroyed-8192");
    ASSERT_NE(presenter,events.end()); ASSERT_NE(window,events.end());
    EXPECT_LT(presenter,window);
    EXPECT_FALSE(platform.acquired);
}
TEST_F(Gap08PresentBindingTest, ARefusedAdoptionLeavesOrdinaryPresentationUsable)
{
    PresentBindingsTrace trace;
    PresentBindingsPlatform platform(trace);
    ScopedCurrentPlatform current(platform);
    auto device=Device();
    platform.trace.refuseAdoption=true;
    EXPECT_THROW(device->Present(std::nullopt,std::nullopt,0xA000),CNA::Platform::PlatformException);
    device->Clear(Microsoft::Xna::Framework::Color::Red);
    device->Present();
    ASSERT_EQ(trace.frames.size(),1u);
    EXPECT_EQ(trace.frames[0].target,0x1000u); ExpectRed(trace.frames[0].bytes);
}
} // namespace

namespace {
TEST_F(Gap08PresentBindingTest, NonNativePlatformRefusesAnOverrideAsUnsupported)
{
    WindowOwnershipTrace trace;
    TracedPlatform platform(trace,TracedDisplay::PresenterOnly);
    ScopedCurrentPlatform current(platform);
    PrimeAdapter(trace);
    auto device=Device();
    // Terminal/presenter-only hosts have no native identity to import. Preserve the renderer's
    // explicit unsupported-request contract instead of leaking a platform adoption failure.
    EXPECT_THROW(device->Present(std::nullopt,std::nullopt,0xA000),System::NotSupportedException);
}
} // namespace

namespace {
TEST_F(Gap08PresentBindingTest, Fna3dRefusesAnOverrideWindowBeforeNativeAdoption)
{
    using namespace Microsoft::Xna::Framework;
    using Descriptor = CNA::Internal::Renderers::GraphicsRendererDescriptor;
    using Peer = CNA::Internal::GraphicsDevicePlatformWindowTestPeer;
    const auto* fna = CNA::Internal::Renderers::GraphicsRendererRegistry::Find(
        CNA::GraphicsRendererType::Fna3d);
    if (!fna) GTEST_SKIP() << "FNA3D identity is not compiled in";
    PresentBindingsTrace trace;
    PresentBindingsPlatform platform(trace);
    ScopedCurrentPlatform current(platform);
    auto device = Device();
    struct RestoreIdentity {
        GraphicsDevice& device;
        const Descriptor* previous;
        ~RestoreIdentity() { Peer::SetPresentIdentity(device, previous); }
    } restore{*device, Peer::SetPresentIdentity(*device, fna)};
    ASSERT_EQ(device->GetGraphicsRendererType(), CNA::GraphicsRendererType::Fna3d);

    // FNA3D-8 at fork 76a76cf: the OpenGL driver switches only to an SDL OpenGL window of its own
    // pixel format and reports a failed switch only in its log, and neither driver preserves the
    // pixels outside a destination. Refuse every override before adopting a target or submitting
    // pixels; this is a capability promise, not an IL claim.
    struct Request { std::optional<Rectangle> source, destination; };
    const Request requests[] = {
        {std::nullopt, std::nullopt},
        {Rectangle(0,0,4,4), std::nullopt},
        {std::nullopt, Rectangle(0,0,4,4)},
        {Rectangle(0,0,4,4), Rectangle(0,0,4,4)}};
    device->Clear(Color::Red);
    for (const auto& request : requests) {
        try {
            device->Present(request.source, request.destination, 0xA000);
            FAIL() << "FNA3D must refuse an override window";
        } catch (const System::NotSupportedException& error) {
            const std::string message = error.what();
            EXPECT_NE(message.find("FNA3D"), std::string::npos);
            EXPECT_NE(message.find("FNA3D-8"), std::string::npos);
            EXPECT_NE(message.find("dependency"), std::string::npos);
            EXPECT_NE(message.find("override window"), std::string::npos);
        }
    }
    EXPECT_EQ(trace.adoptions, 0);
    EXPECT_TRUE(trace.frames.empty());

    // Rectangles on the device's own window are the FNA3D renderer's per-driver decision
    // (Fna3dPresentPixels.*), so the device no longer refuses them itself.
    EXPECT_NO_THROW(device->Present(Rectangle(0,0,4,4), std::nullopt, 0));
    const std::size_t before = trace.frames.size();
    device->Present(std::nullopt, std::nullopt, 0);
    device->Present();
    ASSERT_EQ(trace.frames.size(), before + 2u);
    EXPECT_TRUE(trace.frames[before].ordinary);
    EXPECT_TRUE(trace.frames[before + 1].ordinary);
    EXPECT_EQ(trace.adoptions, 0);
}
} // namespace
