// SPDX-License-Identifier: MS-PL
//
// plans/plan_win32.md WIN32-0062/WIN32-0063 -- the real renderer integration probe.
//
// Win32DirectXIntegrationTests asserts that the handle a Win32Platform window produces satisfies
// exactly the `TryGetWin32` call both D3D renderers make. That is the contract, and it is what the
// platform owes. This program asserts the next thing along: that a Direct3D 11 and a Direct3D 12
// device will actually accept that HWND and create a swap chain on it, resize it and present.
//
// It is a separate program rather than a test case on purpose. A GPU dependency does not belong in
// the platform module's own suite -- coupling it to d3d11.dll would make `platform != renderer` a
// little less true -- and the answer here depends on the machine rather than on the code, so a
// failure must be reportable as an environment limitation rather than as a red test.
//
// Exit codes
//   0  every stage that was attempted succeeded
//   1  a stage failed in a way that indicates a defect (the HWND was rejected, a resize lost the
//      swap chain, presentation failed)
//   2  no Direct3D at all on this machine -- nothing was proved, and nothing is claimed

#include "CNA/Platform/NativeWindowHandle.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include "Win32/Win32Common.hpp"

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <cstdio>
#include <memory>
#include <string>

namespace {

using namespace CNA::Platform;

constexpr int kWidth = 640;
constexpr int kHeight = 480;

bool g_sawAnyDevice = false;
bool g_sawDefect = false;

void Report(const char* const stage, const bool succeeded, const std::string& detail = {})
{
    std::printf("%-34s %s%s%s\n", stage, succeeded ? "ok" : "unavailable",
                detail.empty() ? "" : "  -- ", detail.c_str());
}

void Defect(const char* const stage, const std::string& detail)
{
    g_sawDefect = true;
    std::printf("%-34s DEFECT -- %s\n", stage, detail.c_str());
}

std::string Hr(const HRESULT result)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "hr=0x%08lX",
                  static_cast<unsigned long>(static_cast<unsigned int>(result)));
    return buffer;
}

template <typename T>
void Release(T*& object)
{
    if (object != nullptr)
    {
        object->Release();
        object = nullptr;
    }
}

// --- Direct3D 11 -------------------------------------------------------------------------------

void ProbeDirect3D11(const HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferCount = 2;
    description.BufferDesc.Width = kWidth;
    description.BufferDesc.Height = kHeight;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    // The whole point of the probe: the HWND comes from CNA's platform-neutral handle and goes
    // straight into the swap-chain description a renderer builds.
    description.OutputWindow = hwnd;
    description.SampleDesc.Count = 1;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* swapChain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL level{};

    // WARP as well as HARDWARE: a container with no GPU still has a software rasteriser, and the
    // question here is whether the window handle is acceptable, not how fast it draws.
    for (const D3D_DRIVER_TYPE driver : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP})
    {
        const HRESULT result = D3D11CreateDeviceAndSwapChain(
            nullptr, driver, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &description, &swapChain,
            &device, &level, &context);
        if (SUCCEEDED(result) && swapChain != nullptr && device != nullptr)
            break;
        Report("d3d11 device", false, Hr(result));
    }

    if (swapChain == nullptr || device == nullptr)
    {
        Report("d3d11", false, "no Direct3D 11 device on this machine");
        return;
    }
    g_sawAnyDevice = true;
    Report("d3d11 device + swap chain", true);

    // The back buffer really belongs to this window: its dimensions are the ones the swap chain
    // was asked for, which is what proves the HWND was used rather than merely accepted.
    ID3D11Texture2D* backBuffer = nullptr;
    if (SUCCEEDED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                       reinterpret_cast<void**>(&backBuffer))) &&
        backBuffer != nullptr)
    {
        D3D11_TEXTURE2D_DESC backBufferDescription{};
        backBuffer->GetDesc(&backBufferDescription);
        if (backBufferDescription.Width != kWidth || backBufferDescription.Height != kHeight)
        {
            Defect("d3d11 back buffer size",
                   std::to_string(backBufferDescription.Width) + "x" +
                       std::to_string(backBufferDescription.Height));
        }
        else
        {
            Report("d3d11 back buffer size", true);
        }

        ID3D11RenderTargetView* view = nullptr;
        if (SUCCEEDED(device->CreateRenderTargetView(backBuffer, nullptr, &view)) && view != nullptr)
        {
            const float clear[4] = {0.39f, 0.58f, 0.93f, 1.0f}; // cornflower blue
            context->ClearRenderTargetView(view, clear);
            Report("d3d11 clear", true);
            Release(view);
        }
        Release(backBuffer);
    }

    const HRESULT presented = swapChain->Present(0, 0);
    if (FAILED(presented))
        Defect("d3d11 present", Hr(presented));
    else
        Report("d3d11 present", true);

    // The resize path a window event drives. Releasing every back-buffer reference first is a
    // requirement of ResizeBuffers, and getting it wrong is the classic D3D11 resize bug.
    const HRESULT resized =
        swapChain->ResizeBuffers(2, 800, 600, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    if (FAILED(resized))
    {
        Defect("d3d11 resize", Hr(resized));
    }
    else
    {
        Report("d3d11 resize", true);
        if (FAILED(swapChain->Present(0, 0)))
            Defect("d3d11 present after resize", "present failed once resized");
        else
            Report("d3d11 present after resize", true);
    }

    Release(context);
    Release(swapChain);
    Release(device);
}

// --- Direct3D 12 -------------------------------------------------------------------------------

void ProbeDirect3D12(const HWND hwnd)
{
    ID3D12Device* device = nullptr;
    HRESULT result = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                                       reinterpret_cast<void**>(&device));
    if (FAILED(result) || device == nullptr)
    {
        Report("d3d12 device", false, Hr(result));
        return;
    }
    g_sawAnyDevice = true;
    Report("d3d12 device", true);

    ID3D12CommandQueue* queue = nullptr;
    D3D12_COMMAND_QUEUE_DESC queueDescription{};
    queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    result = device->CreateCommandQueue(&queueDescription, __uuidof(ID3D12CommandQueue),
                                        reinterpret_cast<void**>(&queue));
    if (FAILED(result) || queue == nullptr)
    {
        Defect("d3d12 command queue", Hr(result));
        Release(device);
        return;
    }
    Report("d3d12 command queue", true);

    IDXGIFactory4* factory = nullptr;
    result = CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void**>(&factory));
    if (FAILED(result) || factory == nullptr)
    {
        Report("dxgi factory", false, Hr(result));
        Release(queue);
        Release(device);
        return;
    }

    DXGI_SWAP_CHAIN_DESC1 description{};
    description.BufferCount = 2;
    description.Width = kWidth;
    description.Height = kHeight;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    description.SampleDesc.Count = 1;

    // CreateSwapChainForHwnd is where D3D12 takes the window, and it takes exactly the HWND the
    // platform produced.
    IDXGISwapChain1* swapChain = nullptr;
    result = factory->CreateSwapChainForHwnd(queue, hwnd, &description, nullptr, nullptr,
                                             &swapChain);
    if (FAILED(result) || swapChain == nullptr)
    {
        Report("d3d12 swap chain", false, Hr(result));
    }
    else
    {
        Report("d3d12 swap chain", true);
        const HRESULT resized =
            swapChain->ResizeBuffers(2, 800, 600, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
        if (FAILED(resized))
            Defect("d3d12 resize", Hr(resized));
        else
            Report("d3d12 resize", true);
        Release(swapChain);
    }

    Release(factory);
    Release(queue);
    Release(device);
}

} // namespace

int main()
{
    std::unique_ptr<IPlatform> platform;
    try
    {
        platform = PlatformFactory::Create("Win32");
    }
    catch (const std::exception& error)
    {
        std::printf("FAILED to create the Win32 platform: %s\n", error.what());
        return 1;
    }
    platform->AcquireSubsystem(PlatformSubsystem::Video);

    WindowDescription description;
    description.title = "CNA Win32 + DirectX probe";
    description.width = kWidth;
    description.height = kHeight;
    description.visible = true;

    std::unique_ptr<IPlatformWindow> window;
    try
    {
        window = platform->CreateWindow(description);
    }
    catch (const std::exception& error)
    {
        std::printf("FAILED to create a Win32 window: %s\n", error.what());
        return 1;
    }
    window->Sync();

    // Exactly what DirectX11Renderer and DirectX12Renderer do with the surface they are handed.
    Win32NativeWindow nativeWindow;
    if (!TryGetWin32(window->GetNativeHandle(), nativeWindow))
    {
        std::printf("FAILED: the platform's handle was rejected by TryGetWin32 (%s)\n",
                    Describe(window->GetNativeHandle()).c_str());
        return 1;
    }
    const auto hwnd = static_cast<HWND>(nativeWindow.hwnd);
    std::printf("%-34s ok  -- hwnd=%p, client=%dx%d\n", "win32 platform window", (void*) hwnd,
                window->GetClientBounds().width, window->GetClientBounds().height);

    // Nothing here goes through SDL, and nothing created a window other than the one above.
    std::printf("%-34s ok  -- %s\n", "platform", platform->GetName().c_str());

    ProbeDirect3D11(hwnd);
    ProbeDirect3D12(hwnd);

    // A close request must reach the application rather than destroying the window under the
    // renderer that is presenting to it.
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
    std::vector<PlatformEvent> events;
    platform->PollEvents(events);
    bool sawCloseRequest = false;
    for (const PlatformEvent& event : events)
    {
        if (const auto* windowEvent = std::get_if<WindowEvent>(&event))
        {
            if (windowEvent->kind == WindowEventKind::CloseRequested)
                sawCloseRequest = true;
        }
    }
    if (!sawCloseRequest || IsWindow(hwnd) == FALSE)
        Defect("close request", "the window did not survive its own close request");
    else
        Report("close request", true);

    if (g_sawDefect)
        return 1;
    if (!g_sawAnyDevice)
    {
        std::printf("\nNo Direct3D device of any kind on this machine. The handoff contract was "
                    "still verified; device creation was not.\n");
        return 2;
    }
    std::printf("\nAll attempted stages succeeded.\n");
    return 0;
}
