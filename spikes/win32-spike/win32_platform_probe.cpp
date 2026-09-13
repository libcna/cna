// SPDX-License-Identifier: MS-PL
//
// plans/plan_win32.md WIN32-0000 -- existence gate for the native Win32 platform backend.
//
// Proves, before any CNA code is written, that the four Win32 facilities the backend is built on
// actually work in the environment the work is done in (Linux + mingw-w64 + Wine + Xvfb):
//
//   1. window-class registration and CreateWindowExW with a real WndProc;
//   2. the PeekMessageW / TranslateMessage / DispatchMessageW pump;
//   3. WM_CLOSE reaching the window procedure (so "close is a request" is implementable);
//   4. QueryPerformanceFrequency and GetDpiForWindow answering.
//
// Build and run:
//   x86_64-w64-mingw32-g++ -std=c++23 -static -o win32_platform_probe.exe \
//       win32_platform_probe.cpp -luser32 -lgdi32
//   Xvfb :97 -screen 0 1280x1024x24 & DISPLAY=:97 wine64 ./win32_platform_probe.exe

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>

namespace {

    bool g_sawClose = false;

    LRESULT CALLBACK ProbeWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_CLOSE)
        {
            // The whole point: returning 0 here suppresses DefWindowProcW's destroy, which is
            // what lets CNA report CloseRequested and leave the decision to the application.
            g_sawClose = true;
            return 0;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

} // namespace

int main()
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC | CS_DBLCLKS;
    windowClass.lpfnWndProc = ProbeWndProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = L"CnaWin32Probe";
    if (RegisterClassExW(&windowClass) == 0)
    {
        std::printf("FAIL RegisterClassExW %lu\n", GetLastError());
        return 1;
    }

    // CreateWindowExW takes the OUTER size; ask for a 640x480 CLIENT area the way CNA does.
    RECT desired{0, 0, 640, 480};
    AdjustWindowRectEx(&desired, WS_OVERLAPPEDWINDOW, FALSE, 0);
    const HWND window = CreateWindowExW(
        0, L"CnaWin32Probe", L"CNA Win32 probe", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        desired.right - desired.left, desired.bottom - desired.top, nullptr, nullptr, instance,
        nullptr);
    if (window == nullptr)
    {
        std::printf("FAIL CreateWindowExW %lu\n", GetLastError());
        return 1;
    }
    ShowWindow(window, SW_SHOW);

    RECT client{};
    GetClientRect(window, &client);
    std::printf("client=%ldx%ld\n", client.right - client.left, client.bottom - client.top);

    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    std::printf("qpf=%lld\n", static_cast<long long>(frequency.QuadPart));
    std::printf("dpi=%u\n", GetDpiForWindow(window));

    PostMessageW(window, WM_CLOSE, 0, 0);
    MSG message;
    int pumped = 0;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
        ++pumped;
    }
    std::printf("pumped=%d close=%d\n", pumped, g_sawClose ? 1 : 0);

    DestroyWindow(window);
    UnregisterClassW(L"CnaWin32Probe", instance);
    return (g_sawClose && client.right - client.left == 640) ? 0 : 1;
}
