// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-180: the three device-loss questions the recovery inventory could not
// answer from source. Standalone on purpose -- SDL3 for a window, wgpu-native v29.0.1.1 for
// everything else, and no CNA at all, so what it reports is the pinned library's behaviour rather
// than this project's wrapper around it.
//
// The questions, and what each probe does:
//
// Q1  OBJECT GRAPH. Can a lost WGPUDevice be replaced while the WGPUInstance, WGPUAdapter and
//     WGPUSurface stay alive -- does wgpuSurfaceConfigure accept a NEW device on an
//     already-configured surface? This decides whether recovery is renderer-internal or has to
//     reach back into CNA::Platform to rebuild the window's surface. Probed by configuring the
//     surface, acquiring a texture, destroying the device, requesting a second device from the SAME
//     adapter, re-configuring the SAME surface with it, and acquiring again.
//
// Q2  EVENT CLASSIFICATION. Which WGPUDeviceLostReason this pin actually delivers, under which
//     callback mode, and on which thread. The header pins the vocabulary but not the behaviour:
//     Unknown / Destroyed / CallbackCancelled / FailedCreation, with `Destroyed` documented as the
//     application's own wgpuDeviceDestroy and only the others as a real loss. What is unknown is
//     what wgpu-native emits when the device dies underneath the app, and whether the callback
//     arrives on the thread that pumped wgpuInstanceProcessEvents or on a driver thread -- which is
//     what decides whether CNA may raise RendererDeviceEvent straight from it.
//
// Q3  BROWSER SEMANTICS. Not probed here: it needs an Emscripten build and a browser, and this
//     program is native. What this file records instead is the native answer, so the browser row can
//     be decided against something.
//
// Build (from the repository root):
//   ccache g++ -std=c++23 -O1 -o spikes/webgpu-devicelost-spike/webgpu_devicelost_spike \
//     spikes/webgpu-devicelost-spike/webgpu_devicelost_spike.cpp \
//     -I$HOME/deps/wgpu-native-v29.0.1.1/include $(pkg-config --cflags --libs sdl3) \
//     -L$HOME/deps/wgpu-native-v29.0.1.1/lib -lwgpu_native -Wl,-rpath,$HOME/deps/wgpu-native-v29.0.1.1/lib
//
// Run it on the project's virtual display, like every other GPU program here:
//   DISPLAY=:131 SDL_VIDEODRIVER=x11 ./spikes/webgpu-devicelost-spike/webgpu_devicelost_spike

#include <SDL3/SDL.h>
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace
{
    WGPUInstance g_instance = nullptr;
    WGPUAdapter g_adapter = nullptr;

    std::atomic<int> g_lostCount{0};
    std::atomic<int> g_lastReason{0};
    std::thread::id g_mainThread;
    std::atomic<bool> g_lostOnMainThread{false};
    std::string g_lastMessage;

    [[nodiscard]] const char* ReasonName(WGPUDeviceLostReason reason)
    {
        switch (reason)
        {
        case WGPUDeviceLostReason_Unknown: return "Unknown";
        case WGPUDeviceLostReason_Destroyed: return "Destroyed";
        case WGPUDeviceLostReason_CallbackCancelled: return "CallbackCancelled";
        case WGPUDeviceLostReason_FailedCreation: return "FailedCreation";
        default: return "<not in the pinned enum>";
        }
    }

    [[nodiscard]] std::string ToString(WGPUStringView view)
    {
        if (view.data == nullptr) return {};
        return view.length == WGPU_STRLEN ? std::string(view.data)
                                          : std::string(view.data, view.length);
    }

    void OnDeviceLost(WGPUDevice const* device, WGPUDeviceLostReason reason, WGPUStringView message,
                      void*, void*)
    {
        g_lostCount.fetch_add(1);
        g_lastReason.store(static_cast<int>(reason));
        g_lostOnMainThread.store(std::this_thread::get_id() == g_mainThread);
        g_lastMessage = ToString(message);
        std::printf("    [callback] device-lost: reason=%s (0x%08X) devicePtrNull=%s thread=%s "
                    "message=\"%s\"\n",
                    ReasonName(reason), static_cast<unsigned>(reason),
                    (device == nullptr || *device == nullptr) ? "yes" : "no",
                    g_lostOnMainThread.load() ? "main" : "OTHER", g_lastMessage.c_str());
    }

    void OnUncapturedError(WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void*,
                           void*)
    {
        std::printf("    [callback] uncaptured error type=0x%08X message=\"%s\"\n",
                    static_cast<unsigned>(type), ToString(message).c_str());
    }

    struct AdapterRequest { WGPUAdapter adapter = nullptr; bool done = false; };

    void OnAdapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message,
                   void* userdata, void*)
    {
        auto* request = static_cast<AdapterRequest*>(userdata);
        request->adapter = status == WGPURequestAdapterStatus_Success ? adapter : nullptr;
        request->done = true;
        if (status != WGPURequestAdapterStatus_Success)
            std::printf("    adapter request failed: %s\n", ToString(message).c_str());
    }

    struct DeviceRequest { WGPUDevice device = nullptr; bool done = false; };

    void OnDevice(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message,
                  void* userdata, void*)
    {
        auto* request = static_cast<DeviceRequest*>(userdata);
        request->device = status == WGPURequestDeviceStatus_Success ? device : nullptr;
        request->done = true;
        if (status != WGPURequestDeviceStatus_Success)
            std::printf("    device request failed: %s\n", ToString(message).c_str());
    }

    void Pump(int iterations = 200)
    {
        for (int i = 0; i < iterations; ++i)
        {
            wgpuInstanceProcessEvents(g_instance);
            SDL_Delay(1);
        }
    }

    /// Requests a device from the shared adapter, with the loss and error callbacks attached.
    [[nodiscard]] WGPUDevice RequestDevice(const char* label,
                                           WGPUCallbackMode lostMode =
                                               WGPUCallbackMode_AllowProcessEvents)
    {
        WGPUDeviceDescriptor descriptor{};
        descriptor.label = WGPUStringView{label, WGPU_STRLEN};
        descriptor.deviceLostCallbackInfo.mode = lostMode;
        descriptor.deviceLostCallbackInfo.callback = OnDeviceLost;
        descriptor.uncapturedErrorCallbackInfo.callback = OnUncapturedError;

        DeviceRequest request;
        WGPURequestDeviceCallbackInfo info{};
        info.mode = WGPUCallbackMode_AllowProcessEvents;
        info.callback = OnDevice;
        info.userdata1 = &request;
        wgpuAdapterRequestDevice(g_adapter, &descriptor, info);
        for (int i = 0; i < 2000 && !request.done; ++i)
        {
            wgpuInstanceProcessEvents(g_instance);
            SDL_Delay(1);
        }
        return request.device;
    }

    [[nodiscard]] bool AcquireOnce(WGPUSurface surface, const char* what)
    {
        WGPUSurfaceTexture texture{};
        wgpuSurfaceGetCurrentTexture(surface, &texture);
        const bool ok = texture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal ||
                        texture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal;
        std::printf("    %s: status=0x%08X (%s)%s\n", what,
                    static_cast<unsigned>(texture.status), ok ? "success" : "NOT success",
                    texture.texture != nullptr ? "" : ", texture=null");
        if (texture.texture != nullptr) wgpuTextureRelease(texture.texture);
        return ok;
    }

    [[nodiscard]] bool ConfigureSurface(WGPUSurface surface, WGPUDevice device, int width,
                                        int height, WGPUTextureFormat format)
    {
        WGPUSurfaceConfiguration config{};
        config.device = device;
        config.format = format;
        config.usage = WGPUTextureUsage_RenderAttachment;
        config.width = static_cast<std::uint32_t>(width);
        config.height = static_cast<std::uint32_t>(height);
        config.presentMode = WGPUPresentMode_Fifo;
        config.alphaMode = WGPUCompositeAlphaMode_Auto;
        wgpuSurfaceConfigure(surface, &config);
        return true;
    }
}

int main()
{
    g_mainThread = std::this_thread::get_id();
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::printf("SDL_Init failed: %s\n", SDL_GetError());
        return 2;
    }
    SDL_Window* window = SDL_CreateWindow("webgpu device-lost spike", 256, 192, 0);
    if (window == nullptr)
    {
        std::printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 2;
    }

    WGPUInstanceDescriptor instanceDescriptor{};
    g_instance = wgpuCreateInstance(&instanceDescriptor);
    if (g_instance == nullptr)
    {
        std::printf("wgpuCreateInstance failed\n");
        return 2;
    }

    // An X11 surface from the SDL window. Xlib only: this spike runs on the project's Xvfb display.
    void* display = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                           SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
    const Sint64 xwindow = SDL_GetNumberProperty(SDL_GetWindowProperties(window),
                                                 SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    if (display == nullptr || xwindow == 0)
    {
        std::printf("no X11 window (run under DISPLAY=:131 SDL_VIDEODRIVER=x11)\n");
        return 2;
    }
    WGPUSurfaceSourceXlibWindow x11{};
    x11.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
    x11.display = display;
    x11.window = static_cast<std::uint64_t>(xwindow);
    WGPUSurfaceDescriptor surfaceDescriptor{};
    surfaceDescriptor.nextInChain = &x11.chain;
    WGPUSurface surface = wgpuInstanceCreateSurface(g_instance, &surfaceDescriptor);
    if (surface == nullptr)
    {
        std::printf("wgpuInstanceCreateSurface failed\n");
        return 2;
    }

    AdapterRequest adapterRequest;
    WGPURequestAdapterOptions adapterOptions{};
    adapterOptions.compatibleSurface = surface;
    WGPURequestAdapterCallbackInfo adapterInfo{};
    adapterInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    adapterInfo.callback = OnAdapter;
    adapterInfo.userdata1 = &adapterRequest;
    wgpuInstanceRequestAdapter(g_instance, &adapterOptions, adapterInfo);
    for (int i = 0; i < 2000 && !adapterRequest.done; ++i)
    {
        wgpuInstanceProcessEvents(g_instance);
        SDL_Delay(1);
    }
    g_adapter = adapterRequest.adapter;
    if (g_adapter == nullptr)
    {
        std::printf("no adapter\n");
        return 2;
    }

    WGPUSurfaceCapabilities capabilities{};
    wgpuSurfaceGetCapabilities(surface, g_adapter, &capabilities);
    const WGPUTextureFormat format =
        capabilities.formatCount > 0 ? capabilities.formats[0] : WGPUTextureFormat_BGRA8Unorm;
    wgpuSurfaceCapabilitiesFreeMembers(capabilities);

    int failures = 0;
    std::printf("=== Q1: can a NEW device configure the SAME, already-configured surface? ===\n");

    WGPUDevice first = RequestDevice("spike device 1");
    if (first == nullptr) { std::printf("no device\n"); return 2; }
    std::printf("  device 1 created\n");
    (void)ConfigureSurface(surface, first, 256, 192, format);
    const bool firstAcquire = AcquireOnce(surface, "acquire on device 1");
    if (!firstAcquire) ++failures;

    std::printf("  destroying device 1 (wgpuDeviceDestroy)...\n");
    wgpuDeviceDestroy(first);
    Pump();
    std::printf("  device-lost callbacks after destroy + %d ProcessEvents pumps: %d\n", 200,
                g_lostCount.load());
    // MEASURED, and the first thing this spike found: calling wgpuSurfaceGetCurrentTexture on a
    // surface whose device has been destroyed does NOT return an error status in this pin. It
    // panics inside wgpu-native --
    //     thread '<unnamed>' panicked at src/lib.rs:605:5:
    //     Error in wgpuSurfaceGetCurrentTexture: Validation Error
    //     Caused by: Parent device is lost
    // -- and, because the panic cannot unwind across the C ABI, aborts the process. So the probe
    // that would have read that status is deliberately absent: it cannot be performed, and its
    // absence is the answer. CNA must treat "the device is lost" as a gate BEFORE the acquire, not
    // as a status the acquire returns (WEBGPU-182).
    std::printf("  acquire after destroy, before reconfigure: NOT PROBED -- it aborts the process "
                "in this pin (wgpu-native panics: \"Parent device is lost\")\n");

    WGPUDevice second = RequestDevice("spike device 2");
    if (second == nullptr)
    {
        std::printf("  Q1 ANSWER: a second device could NOT be requested from the surviving "
                    "adapter -- recovery must rebuild the adapter too\n");
        ++failures;
    }
    else
    {
        std::printf("  device 2 created from the SAME adapter\n");
        (void)ConfigureSurface(surface, second, 256, 192, format);
        Pump(50);
        const bool secondAcquire = AcquireOnce(surface, "acquire on device 2, same surface");
        std::printf("  Q1 ANSWER: reconfiguring the SAME surface with a NEW device %s\n",
                    secondAcquire ? "WORKS -- instance, adapter and surface all survive a device "
                                    "replace, so recovery is renderer-internal"
                                  : "FAILS -- recovery must reach back into the platform layer and "
                                    "rebuild the surface");
        if (!secondAcquire) ++failures;
    }

    std::printf("=== Q2: does the device-lost callback fire at all, and where ===\n");
    // Four escalating chances for the callback, because "it did not fire" is only a finding once
    // the obvious reasons it might not have are ruled out: the wrong callback mode, an unpumped
    // instance, an unpolled device, and a device still holding a reference.
    const auto report = [](const char* what, int before) {
        const int now = g_lostCount.load();
        if (now > before)
            std::printf("  %-46s -> FIRED, reason %s, on the %s thread\n", what,
                        ReasonName(static_cast<WGPUDeviceLostReason>(g_lastReason.load())),
                        g_lostOnMainThread.load() ? "MAIN" : "a DIFFERENT");
        else
            std::printf("  %-46s -> no callback\n", what);
        return now;
    };
    int seen = report("wgpuDeviceDestroy + wgpuInstanceProcessEvents", 0);

    if (second != nullptr)
    {
        std::printf("  destroying device 2, then polling the DEVICE as well...\n");
        wgpuDeviceDestroy(second);
        for (int i = 0; i < 50; ++i)
        {
            wgpuDevicePoll(second, 0, nullptr);
            wgpuInstanceProcessEvents(g_instance);
            SDL_Delay(1);
        }
        seen = report("wgpuDeviceDestroy + wgpuDevicePoll", seen);
        std::printf("  releasing device 2's last reference...\n");
        wgpuDeviceRelease(second);
        second = nullptr;
        Pump();
        seen = report("wgpuDeviceRelease (last reference)", seen);
    }

    // And once more on a device whose loss callback asked for spontaneous delivery, which is the
    // mode the browser path uses.
    {
        WGPUDevice third = RequestDevice("spike device 3", WGPUCallbackMode_AllowSpontaneous);
        if (third != nullptr)
        {
            const int before = g_lostCount.load();
            wgpuDeviceDestroy(third);
            Pump();
            (void)report("AllowSpontaneous + wgpuDeviceDestroy", before);
            wgpuDeviceRelease(third);
            Pump(50);
        }
    }

    std::printf("  TOTAL device-lost callbacks observed across every attempt: %d\n",
                g_lostCount.load());
    std::printf("  Q2 ANSWER: %s\n",
                g_lostCount.load() == 0
                    ? "this pin delivers NO device-lost callback for an application-initiated "
                      "destroy, under any of the four attempts above -- so WEBGPU-182 cannot make "
                      "the debug simulate/restore path depend on it, and the driver-reported path "
                      "needs its own external verification (WEBGPU-196)"
                    : "the callback does fire -- see the lines above for the reason and thread");

    std::printf("=== Q3: browser semantics are NOT probed here -- this program is native ===\n");

    if (second != nullptr) wgpuDeviceRelease(second);
    wgpuDeviceRelease(first);
    wgpuSurfaceRelease(surface);
    wgpuAdapterRelease(g_adapter);
    wgpuInstanceRelease(g_instance);
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("=== %s ===\n", failures == 0 ? "all probes reported" : "some probes could not run");
    return failures == 0 ? 0 : 1;
}
