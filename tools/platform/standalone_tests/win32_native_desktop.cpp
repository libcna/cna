// SPDX-License-Identifier: MS-PL
//
// plans/plan_win32_native_validation.md: the checks that need a real Windows desktop and a real
// Windows kernel, and that therefore could not be made before.
//
// The unit suite already covers the shape of these APIs, and passes natively. What it cannot cover
// from inside one process on one window station is:
//
//   host-ownership  what CNA does to PROCESS-GLOBAL state it does not own. docs/platform-win32.md
//                   promises the backend never sets the process DPI awareness, never calls
//                   timeBeginPeriod, never changes the current directory, and never takes the
//                   thread's COM apartment away from a host that already chose one. Those are
//                   claims about the process, so they are measured before and after.
//   dpi             the numbers Windows reports at a scaling factor other than 100 %. Under Wine
//                   there is one monitor at a fixed DPI and nothing to compare.
//   clipboard       the Windows clipboard is per window station and shared between processes.
//                   A round trip inside one process proves the code path; reading back what
//                   another program put there, and handing another program something to read,
//                   proves it is the system clipboard.
//   cursors         HCURSOR and HICON are USER objects. Whether repeated shape changes leak them
//                   is a question only Windows' own accounting answers.
//
// Usage:
//   cna_win32_native_desktop.exe --check <name> [--check <name>...] [--iterations N]
//   cna_win32_native_desktop.exe --list
//   cna_win32_native_desktop.exe --clipboard-put-file <path>   (file holds UTF-8 bytes)
//   cna_win32_native_desktop.exe --clipboard-get-file <path>   (file receives UTF-8 bytes)
//
// The clipboard modes take and give a FILE rather than an argument and stdout. Text handed to a
// native process on a PowerShell command line is re-encoded in the ANSI code page, and its stdout
// is decoded in the console code page, so a Czech or emoji sample measured through those was
// measuring PowerShell: the round trip lost characters that CNA had handled correctly. A file of
// UTF-8 bytes, written and read with an explicit encoding on both sides, measures CNA.
//
// Exit codes: 0 every selected check passed; 1 one failed; 2 the platform could not be created.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/IPlatformGlContext.hpp"
#include "CNA/Platform/IPlatformSystemServices.hpp"
#include "CNA/Platform/IPlatformWindow.hpp"
#include "CNA/Platform/Input/IPlatformKeyboard.hpp"
#include "CNA/Platform/Input/IPlatformMouse.hpp"
#include "CNA/Platform/Input/IPlatformTextInput.hpp"
#include "CNA/Platform/Input/KeyCode.hpp"
#include "CNA/Platform/Input/Scancode.hpp"
#include "CNA/Platform/NativeWindowHandle.hpp"
#include "CNA/Platform/PlatformEvent.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "CNA/Platform/WindowDescription.hpp"

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <GL/gl.h>

#undef CreateWindow
#undef CreateDirectory
#undef MessageBox
#undef GetClassName

namespace
{
    using namespace CNA::Platform;

    int failures = 0;
    int checksRun = 0;

    /// GetProcAddress returns FARPROC, and casting that to the real signature is what every
    /// dynamically resolved Windows entry point requires. Routed through one place so the
    /// -Wcast-function-type suppression is stated once rather than at each call site.
    template <typename Fn>
    Fn Resolve(HMODULE module, const char* name)
    {
        if (module == nullptr) return nullptr;
#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
        return reinterpret_cast<Fn>(GetProcAddress(module, name));
#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif
    }

    void Report(const std::string& check, bool ok, const std::string& detail)
    {
        std::cout << (ok ? "  ok   " : "  FAIL ") << std::left << std::setw(46) << check
                  << detail << '\n';
        std::cout.flush();
        if (!ok) ++failures;
    }

    void Info(const std::string& what, const std::string& detail)
    {
        std::cout << "  info " << std::left << std::setw(46) << what << detail << '\n';
    }

    // ---------------------------------------------------------------- process-global state

    struct HostState
    {
        std::string dpiAwareness;
        std::string comApartment;
        std::wstring currentDirectory;
        UINT errorMode = 0;
        ULONG timerResolution100ns = 0;
    };

    std::string DescribeDpiAwareness()
    {
        // Resolved dynamically, the same way the backend resolves them: these entry points do not
        // exist on every Windows the backend supports, and a static import would refuse to load.
        const HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32 == nullptr) return "user32 not loaded";
        using GetCtxFn = DPI_AWARENESS_CONTEXT(WINAPI*)();
        using AreEqualFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT, DPI_AWARENESS_CONTEXT);
        const auto getCtx = Resolve<GetCtxFn>(user32, "GetThreadDpiAwarenessContext");
        const auto areEqual = Resolve<AreEqualFn>(user32, "AreDpiAwarenessContextsEqual");
        if (getCtx == nullptr || areEqual == nullptr) return "pre-1607 Windows";
        const DPI_AWARENESS_CONTEXT ctx = getCtx();
        if (areEqual(ctx, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return "per-monitor-v2";
        if (areEqual(ctx, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE)) return "per-monitor";
        if (areEqual(ctx, DPI_AWARENESS_CONTEXT_SYSTEM_AWARE)) return "system";
        if (areEqual(ctx, DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED)) return "unaware-gdiscaled";
        if (areEqual(ctx, DPI_AWARENESS_CONTEXT_UNAWARE)) return "unaware";
        return "unrecognised";
    }

    std::string DescribeApartment()
    {
        APTTYPE type{};
        APTTYPEQUALIFIER qualifier{};
        const HRESULT hr = CoGetApartmentType(&type, &qualifier);
        if (hr == CO_E_NOTINITIALIZED) return "uninitialised";
        if (FAILED(hr)) return "query failed";
        switch (type)
        {
            case APTTYPE_STA: return "STA";
            case APTTYPE_MTA: return "MTA";
            case APTTYPE_NA: return "neutral";
            case APTTYPE_MAINSTA: return "main-STA";
            default: return "other";
        }
    }

    ULONG CurrentTimerResolution()
    {
        // NtQueryTimerResolution is the only way to see the process-visible timer resolution, and
        // it is exactly what a stray timeBeginPeriod would move.
        using QueryFn = LONG(WINAPI*)(PULONG, PULONG, PULONG);
        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll == nullptr) return 0;
        const auto query = Resolve<QueryFn>(ntdll, "NtQueryTimerResolution");
        if (query == nullptr) return 0;
        ULONG minimum = 0, maximum = 0, current = 0;
        if (query(&minimum, &maximum, &current) != 0) return 0;
        return current;
    }

    HostState CaptureHostState()
    {
        HostState state;
        state.dpiAwareness = DescribeDpiAwareness();
        state.comApartment = DescribeApartment();
        wchar_t directory[MAX_PATH]{};
        GetCurrentDirectoryW(MAX_PATH, directory);
        state.currentDirectory = directory;
        // GetErrorMode does not disturb what it reads, unlike the old SetErrorMode round trip.
        state.errorMode = GetErrorMode();
        state.timerResolution100ns = CurrentTimerResolution();
        return state;
    }

    // ---------------------------------------------------------------- helpers

    WindowDescription Described(const char* title, int width = 640, int height = 480)
    {
        WindowDescription description;
        description.title = title;
        description.width = width;
        description.height = height;
        description.visible = true;
        description.centered = true;
        return description;
    }

    HWND HandleOf(IPlatformWindow& window)
    {
        Win32NativeWindow native;
        if (!TryGetWin32(window.GetNativeHandle(), native)) return nullptr;
        return static_cast<HWND>(native.hwnd);
    }

    void Pump(IPlatform& platform, std::vector<PlatformEvent>& events, int rounds = 8)
    {
        for (int i = 0; i < rounds; ++i)
        {
            events.clear();
            platform.PollEvents(events);
            ::Sleep(2);
        }
    }

    std::string Utf8(const std::wstring& wide)
    {
        if (wide.empty()) return {};
        const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                             nullptr, 0, nullptr, nullptr);
        std::string out(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(),
                            size, nullptr, nullptr);
        return out;
    }

    std::wstring Wide(const std::string& utf8)
    {
        if (utf8.empty()) return {};
        const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                             nullptr, 0);
        std::wstring out(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), size);
        return out;
    }

    /// Reads CF_UNICODETEXT with the raw Win32 clipboard API -- deliberately not through CNA, so
    /// that agreement between the two means the backend really used the system clipboard.
    bool RawClipboardGet(std::string& text, std::string& why)
    {
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            if (OpenClipboard(nullptr) != FALSE) break;
            ::Sleep(50);
            if (attempt == 19) { why = "another process held the clipboard"; return false; }
        }
        bool ok = false;
        const HANDLE handle = GetClipboardData(CF_UNICODETEXT);
        if (handle == nullptr) { why = "no CF_UNICODETEXT on the clipboard"; }
        else
        {
            const auto* data = static_cast<const wchar_t*>(GlobalLock(handle));
            if (data == nullptr) { why = "GlobalLock failed"; }
            else { text = Utf8(std::wstring(data)); GlobalUnlock(handle); ok = true; }
        }
        CloseClipboard();
        return ok;
    }

    bool RawClipboardSet(const std::string& text, std::string& why)
    {
        const std::wstring wide = Wide(text);
        const std::size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
        const HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory == nullptr) { why = "GlobalAlloc failed"; return false; }
        auto* target = static_cast<wchar_t*>(GlobalLock(memory));
        std::memcpy(target, wide.c_str(), bytes);
        GlobalUnlock(memory);

        for (int attempt = 0; attempt < 20; ++attempt)
        {
            if (OpenClipboard(nullptr) != FALSE) break;
            ::Sleep(50);
            if (attempt == 19) { GlobalFree(memory); why = "could not open the clipboard"; return false; }
        }
        EmptyClipboard();
        const bool ok = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
        CloseClipboard();
        if (!ok) { GlobalFree(memory); why = "SetClipboardData failed"; }
        return ok;
    }

    // ---------------------------------------------------------------- the checks

    void CheckHostOwnership(IPlatform& platform, const HostState& before)
    {
        // docs/platform-win32.md, "the backend does not touch process-global policy". Everything
        // here is state the host owns; a framework that quietly changes any of it is a framework
        // that cannot be embedded.
        auto window = platform.CreateWindow(Described("host ownership"));
        std::vector<PlatformEvent> events;
        Pump(platform, events);

        const HostState after = CaptureHostState();

        Report("host.dpiAwarenessUnchanged", after.dpiAwareness == before.dpiAwareness,
               before.dpiAwareness + " -> " + after.dpiAwareness);
        Report("host.currentDirectoryUnchanged", after.currentDirectory == before.currentDirectory,
               Utf8(after.currentDirectory));
        Report("host.errorModeUnchanged", after.errorMode == before.errorMode,
               std::to_string(before.errorMode) + " -> " + std::to_string(after.errorMode));
        // A timeBeginPeriod anywhere in the backend would show as a smaller current resolution.
        Report("host.timerResolutionUnchanged",
               after.timerResolution100ns == before.timerResolution100ns,
               std::to_string(before.timerResolution100ns / 10) + " us -> " +
                   std::to_string(after.timerResolution100ns / 10) + " us");
        // COM is the one the backend does initialise -- but only to add a reference, never to take
        // an apartment a host already chose. An uninitialised thread may legitimately become STA.
        const bool comOk = before.comApartment == "uninitialised"
                               ? true
                               : after.comApartment == before.comApartment;
        Report("host.comApartmentRespected", comOk,
               before.comApartment + " -> " + after.comApartment);
        ++checksRun;
    }

    void CheckDpi(IPlatform& platform)
    {
        auto window = platform.CreateWindow(Described("dpi"));
        std::vector<PlatformEvent> events;
        Pump(platform, events);
        const HWND hwnd = HandleOf(*window);
        if (hwnd == nullptr) { Report("dpi.handle", false, "no HWND"); return; }

        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        const HMODULE user32 = GetModuleHandleW(L"user32.dll");
        const auto getDpiForWindow = Resolve<GetDpiForWindowFn>(user32, "GetDpiForWindow");

        UINT windowsDpi = 96;
        if (getDpiForWindow != nullptr) windowsDpi = getDpiForWindow(hwnd);
        else
        {
            const HDC dc = GetDC(nullptr);
            windowsDpi = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX));
            ReleaseDC(nullptr, dc);
        }

        const float scale = window->GetDisplayScale();
        const WindowBounds logical = window->GetClientBounds();
        const WindowSize pixels = window->GetPixelSize();

        Info("dpi.windowsReports", std::to_string(windowsDpi) + " dpi (" +
                                       std::to_string(windowsDpi * 100 / 96) + "% scaling)");
        Info("dpi.cnaReports", "scale=" + std::to_string(scale) + " client=" +
                                   std::to_string(logical.width) + "x" + std::to_string(logical.height) +
                                   " pixels=" + std::to_string(pixels.width) + "x" +
                                   std::to_string(pixels.height));

        // The contract: GetDisplayScale agrees with the window's own DPI, and the pixel size is
        // read from the window rather than derived by multiplying the logical one by the scale.
        const float expected = static_cast<float>(windowsDpi) / 96.0f;
        Report("dpi.scaleMatchesGetDpiForWindow", std::abs(scale - expected) < 0.01f,
               "cna " + std::to_string(scale) + " vs windows " + std::to_string(expected));

        RECT client{};
        GetClientRect(hwnd, &client);
        Report("dpi.pixelSizeIsTheClientRect",
               pixels.width == (client.right - client.left) &&
                   pixels.height == (client.bottom - client.top),
               std::to_string(client.right - client.left) + "x" +
                   std::to_string(client.bottom - client.top));
        ++checksRun;
    }

    void CheckDisplays(IPlatform& platform)
    {
        IPlatformDisplays* displays = platform.GetDisplays();
        if (displays == nullptr) { Report("displays.service", false, "no display service"); return; }
        const std::vector<DisplayInfo> reported = displays->GetDisplays();

        int monitorCount = 0;
        EnumDisplayMonitors(nullptr, nullptr,
                            [](HMONITOR, HDC, LPRECT, LPARAM user) -> BOOL {
                                ++*reinterpret_cast<int*>(user);
                                return TRUE;
                            },
                            reinterpret_cast<LPARAM>(&monitorCount));

        Report("displays.countMatchesEnumDisplayMonitors",
               static_cast<int>(reported.size()) == monitorCount,
               "cna " + std::to_string(reported.size()) + " vs windows " +
                   std::to_string(monitorCount));
        for (const DisplayInfo& display : reported)
        {
            Info("displays.entry", display.name + " " + std::to_string(display.width) + "x" +
                                       std::to_string(display.height) + " @" +
                                       std::to_string(display.x) + "," + std::to_string(display.y) +
                                       " scale=" + std::to_string(display.contentScale));
        }
        ++checksRun;
    }

    void CheckClipboard(IPlatform& platform)
    {
        IPlatformClipboard* clipboard = platform.GetClipboard();
        if (clipboard == nullptr) { Report("clipboard.service", false, "no clipboard service"); return; }

        struct Sample { const char* name; const char* text; };
        const Sample samples[] = {
            {"ascii", "the quick brown fox"},
            {"czech", "p\xC5\x99\xC3\xADli\xC5\xA1 \xC5\xBElu\xC5\xA5ou\xC4\x8Dk\xC3\xBD k\xC5\xAF\xC5\x88"},
            {"emoji", "clipboard \xF0\x9F\x93\x8B and \xF0\x9F\x8C\x80"},
            {"empty", ""},
        };

        for (const Sample& sample : samples)
        {
            clipboard->SetText(sample.text);
            std::string raw;
            std::string why;
            if (sample.text[0] == '\0')
            {
                // An empty string is still text, and must not be reported as "no text at all".
                Report(std::string("clipboard.roundTrip.") + sample.name,
                       clipboard->GetText().empty(), "empty text round-tripped");
                continue;
            }
            const bool gotRaw = RawClipboardGet(raw, why);
            Report(std::string("clipboard.systemClipboard.") + sample.name,
                   gotRaw && raw == sample.text,
                   gotRaw ? ("win32 read back " + std::to_string(raw.size()) + " bytes") : why);
            Report(std::string("clipboard.roundTrip.") + sample.name,
                   clipboard->GetText() == sample.text, "");
        }

        // A large payload: the backend allocates a moveable global for it, and a size bug here is
        // the classic clipboard defect.
        std::string large;
        large.reserve(600000);
        while (large.size() < 512 * 1024) large += "0123456789abcdef";
        clipboard->SetText(large);
        Report("clipboard.large", clipboard->GetText() == large,
               std::to_string(large.size()) + " bytes");

        // The other direction: something this process did NOT write through CNA.
        const std::string external = "written by the raw Win32 clipboard API";
        std::string why;
        if (RawClipboardSet(external, why))
        {
            Report("clipboard.readsWhatAnotherWriterPut", clipboard->GetText() == external,
                   clipboard->GetText());
            Report("clipboard.hasTextAgrees", clipboard->HasText(), "");
        }
        else { Report("clipboard.readsWhatAnotherWriterPut", false, why); }
        ++checksRun;
    }

    void CheckCursors(IPlatform& platform, int iterations)
    {
        IPlatformMouse* mouse = platform.GetMouse();
        if (mouse == nullptr) { Report("cursors.service", false, "no mouse service"); return; }
        auto window = platform.CreateWindow(Described("cursors"));
        std::vector<PlatformEvent> events;
        Pump(platform, events);

        const HANDLE self = GetCurrentProcess();
        const DWORD userBefore = GetGuiResources(self, GR_USEROBJECTS);

        const SystemCursor shapes[] = {
            SystemCursor::Arrow,      SystemCursor::IBeam,      SystemCursor::Wait,
            SystemCursor::Crosshair,  SystemCursor::Move,       SystemCursor::NotAllowed,
            SystemCursor::Pointer,    SystemCursor::Progress,   SystemCursor::NwseResize,
            SystemCursor::NeswResize, SystemCursor::EwResize,   SystemCursor::NsResize,
        };
        for (int i = 0; i < iterations; ++i)
        {
            mouse->SetCursor(shapes[static_cast<std::size_t>(i) % std::size(shapes)]);
            mouse->SetCursorVisible((i & 1) == 0);
            if ((i % 64) == 0) Pump(platform, events, 1);
        }
        mouse->SetCursorVisible(true);
        mouse->SetCursor(SystemCursor::Arrow);
        Pump(platform, events, 16);

        const DWORD userAfter = GetGuiResources(self, GR_USEROBJECTS);
        Report("cursors.noUserObjectLeak", userAfter <= userBefore + 4,
               std::to_string(iterations) + " shape changes, USER " + std::to_string(userBefore) +
                   " -> " + std::to_string(userAfter));
        ++checksRun;
    }

    void CheckNativeHandles(IPlatform& platform)
    {
        std::vector<PlatformEvent> events;
        auto first = platform.CreateWindow(Described("native handle 1", 400, 300));
        auto second = platform.CreateWindow(Described("native handle 2", 320, 240));
        Pump(platform, events);

        const HWND a = HandleOf(*first);
        const HWND b = HandleOf(*second);
        Report("handles.distinct", a != nullptr && b != nullptr && a != b, "");
        Report("handles.isWindow", IsWindow(a) != FALSE && IsWindow(b) != FALSE, "");

        DWORD processId = 0;
        GetWindowThreadProcessId(a, &processId);
        Report("handles.belongToThisProcess", processId == GetCurrentProcessId(),
               "pid " + std::to_string(processId));

        RECT client{};
        GetClientRect(a, &client);
        const WindowSize pixels = first->GetPixelSize();
        Report("handles.clientRectAgreesWithPixelSize",
               pixels.width == client.right - client.left &&
                   pixels.height == client.bottom - client.top,
               std::to_string(client.right - client.left) + "x" +
                   std::to_string(client.bottom - client.top));

        // Destroying out of creation order is the case a naive registry gets wrong.
        const HWND survivor = b;
        first.reset();
        Pump(platform, events);
        Report("handles.destroyingOneLeavesTheOther", IsWindow(survivor) != FALSE, "");
        Report("handles.destroyedHandleIsGone", IsWindow(a) == FALSE, "");
        second.reset();
        Pump(platform, events);
        Report("handles.bothGone", IsWindow(survivor) == FALSE, "");
        ++checksRun;
    }

    void CheckCloseIsARequest(IPlatform& platform)
    {
        // docs/platform-win32.md: WM_CLOSE is a request. DefWindowProcW would destroy the window;
        // the backend must suppress that and deliver an event instead.
        std::vector<PlatformEvent> events;
        auto window = platform.CreateWindow(Described("close request"));
        Pump(platform, events);
        const HWND hwnd = HandleOf(*window);

        SendMessageW(hwnd, WM_CLOSE, 0, 0);
        Pump(platform, events, 16);
        Report("close.windowSurvivesWmClose", IsWindow(hwnd) != FALSE,
               "WM_CLOSE must be a request, not a destruction");

        // And a second window must be entirely unaffected by the first one's close request.
        auto other = platform.CreateWindow(Described("close bystander"));
        Pump(platform, events);
        const HWND otherHwnd = HandleOf(*other);
        SendMessageW(hwnd, WM_CLOSE, 0, 0);
        Pump(platform, events, 16);
        Report("close.otherWindowsUnaffected", IsWindow(otherHwnd) != FALSE, "");
        ++checksRun;
    }


    void CheckWgl(IPlatform& platform, int iterations)
    {
        // WGL through the guest's OpenGL stack. On this VM that is Mesa's SVGA3D driver, so what
        // this proves is CNA's Windows WGL integration, not a GPU vendor's driver -- the context
        // it reports is recorded rather than asserted, precisely so the two are not confused.
        IPlatformGlContext* gl = platform.GetGlContext();
        if (gl == nullptr) { Report("wgl.service", false, "no GL service"); return; }

        WindowDescription description = Described("wgl");
        description.renderIntent = WindowRenderIntent::OpenGl;
        auto window = platform.CreateWindow(description);
        std::vector<PlatformEvent> events;
        Pump(platform, events);

        GlContextDescription wanted;
        wanted.majorVersion = 3;
        wanted.minorVersion = 3;
        wanted.profile = GlProfile::Core;

        GlContextHandle context = nullptr;
        try { context = gl->CreateContext(window->GetId(), wanted); }
        catch (const std::exception& error)
        {
            // An absent or unusable OpenGL driver is an environment limitation; it is reported as
            // one rather than as a CNA failure.
            Info("wgl.environment", std::string("no usable OpenGL driver: ") + error.what());
            ++checksRun;
            return;
        }
        Report("wgl.contextIsReal", context != nullptr, "");
        gl->MakeCurrent(window->GetId(), context);

        auto str = [](GLenum name) {
            const GLubyte* value = glGetString(name);
            return value != nullptr ? std::string(reinterpret_cast<const char*>(value)) : std::string("(null)");
        };
        Info("wgl.version", str(GL_VERSION));
        Info("wgl.renderer", str(GL_RENDERER));
        Info("wgl.vendor", str(GL_VENDOR));
        const GlContextDescription granted = gl->GetContextAttributes(context);
        Info("wgl.granted", std::to_string(granted.majorVersion) + "." +
                                std::to_string(granted.minorVersion));
        Report("wgl.glGetStringWorksOnTheCurrentContext", str(GL_VERSION) != "(null)", "");

        glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        gl->SwapBuffers(window->GetId());
        Report("wgl.clearAndSwap", glGetError() == GL_NO_ERROR, "");

        gl->MakeCurrent(window->GetId(), nullptr);
        gl->DestroyContext(context);
        Report("wgl.unbindAndDestroy", gl->GetCurrentBinding().context == nullptr, "");

        // Repeated create/destroy: a context or DC leaked per cycle is the classic WGL defect, and
        // only a real driver allocates anything to leak.
        const HANDLE self = GetCurrentProcess();
        const DWORD gdiBefore = GetGuiResources(self, GR_GDIOBJECTS);
        int cycles = 0;
        for (int i = 0; i < iterations / 20 + 1; ++i)
        {
            GlContextHandle each = nullptr;
            try { each = gl->CreateContext(window->GetId(), wanted); }
            catch (const std::exception&) { break; }
            gl->MakeCurrent(window->GetId(), each);
            gl->MakeCurrent(window->GetId(), nullptr);
            gl->DestroyContext(each);
            ++cycles;
        }
        Pump(platform, events, 16);
        const DWORD gdiAfter = GetGuiResources(self, GR_GDIOBJECTS);
        Report("wgl.noGdiLeakAcrossContexts", gdiAfter <= gdiBefore + 4,
               std::to_string(cycles) + " context cycles, GDI " + std::to_string(gdiBefore) +
                   " -> " + std::to_string(gdiAfter));
        ++checksRun;
    }


    // ---------------------------------------------------------------- synthetic input

    // SendInput is how Windows itself delivers a keystroke: it enters the same raw input stream a
    // physical keyboard does, below the message queue, so what arrives at the window is
    // indistinguishable from a real key. That makes it the only way to exercise the backend's
    // keyboard, text and mouse paths end to end without a person -- and it only works against the
    // FOREGROUND window, which is why none of this can run from an SSH session.

    bool ForegroundIsOurs(HWND hwnd) { return GetForegroundWindow() == hwnd; }

    bool BringToForeground(IPlatform& platform, IPlatformWindow& window,
                           std::vector<PlatformEvent>& events)
    {
        const HWND hwnd = HandleOf(window);
        if (hwnd == nullptr) return false;
        for (int attempt = 0; attempt < 40; ++attempt)
        {
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
            SetActiveWindow(hwnd);
            SetFocus(hwnd);
            Pump(platform, events, 4);
            if (ForegroundIsOurs(hwnd)) return true;
        }
        return false;
    }

    void SendKeyScan(WORD scan, bool extended, bool up)
    {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = scan;
        input.ki.dwFlags = KEYEVENTF_SCANCODE | (extended ? KEYEVENTF_EXTENDEDKEY : 0u) |
                           (up ? KEYEVENTF_KEYUP : 0u);
        SendInput(1, &input, sizeof(INPUT));
    }

    void SendUnitUtf16(wchar_t unit, bool up)
    {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = unit;
        input.ki.dwFlags = KEYEVENTF_UNICODE | (up ? KEYEVENTF_KEYUP : 0u);
        SendInput(1, &input, sizeof(INPUT));
    }

    void SendMouseFlags(DWORD flags, DWORD data = 0)
    {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = flags;
        input.mi.mouseData = data;
        SendInput(1, &input, sizeof(INPUT));
    }

    struct SeenKey
    {
        bool got = false;
        Scancode scancode = Scancode::Unknown;
        KeyCode keycode = KeyCode::None;
        bool pressed = false;
    };

    SeenKey PressAndCollect(IPlatform& platform, std::vector<PlatformEvent>& events,
                            WORD scan, bool extended)
    {
        SeenKey seen;
        SendKeyScan(scan, extended, false);
        for (int round = 0; round < 40 && !seen.got; ++round)
        {
            events.clear();
            platform.PollEvents(events);
            for (const PlatformEvent& event : events)
            {
                if (const auto* key = std::get_if<KeyEvent>(&event))
                {
                    if (!key->pressed) continue;
                    seen.got = true;
                    seen.scancode = key->scancode;
                    seen.keycode = key->keycode;
                    seen.pressed = key->pressed;
                    break;
                }
            }
            ::Sleep(5);
        }
        SendKeyScan(scan, extended, true);
        Pump(platform, events, 6);
        return seen;
    }

    std::string CollectText(IPlatform& platform, std::vector<PlatformEvent>& events, int rounds)
    {
        std::string text;
        for (int round = 0; round < rounds; ++round)
        {
            events.clear();
            platform.PollEvents(events);
            for (const PlatformEvent& event : events)
                if (const auto* input = std::get_if<TextInputEvent>(&event)) text += input->text;
            ::Sleep(5);
        }
        return text;
    }

    void CheckInput(IPlatform& platform)
    {
        std::vector<PlatformEvent> events;
        WindowDescription description = Described("synthetic input", 640, 480);
        auto window = platform.CreateWindow(description);
        Pump(platform, events, 12);
        if (!BringToForeground(platform, *window, events))
        {
            // Another window refusing to give up the foreground is an environment condition, not a
            // CNA defect, and is reported as one rather than as a failure.
            Info("input.environment", "this window could not take the foreground; input not exercised");
            ++checksRun;
            return;
        }
        Report("input.windowHasTheForeground", true, "");

        // --- keyboard: physical location and logical key are different things -------------------
        // set-1 scancodes, with the extended flag where the physical key needs one. The assertion
        // is on BOTH: Scancode must name the physical key and KeyCode the logical one, and a
        // backend that conflated them would pass one and fail the other.
        struct KeyCase { const char* name; WORD scan; bool extended; Scancode scancode; KeyCode keycode; };
        const KeyCase cases[] = {
            {"A",          0x1E, false, Scancode::A,            KeyCode::A},
            {"F1",         0x3B, false, Scancode::F1,           KeyCode::F1},
            {"CapsLock",   0x3A, false, Scancode::CapsLock,     KeyCode::CapsLock},
            {"LeftShift",  0x2A, false, Scancode::LeftShift,    KeyCode::LeftShift},
            {"RightShift", 0x36, false, Scancode::RightShift,   KeyCode::RightShift},
            {"LeftCtrl",   0x1D, false, Scancode::LeftControl,  KeyCode::LeftControl},
            {"RightCtrl",  0x1D, true,  Scancode::RightControl, KeyCode::RightControl},
            {"ArrowLeft",  0x4B, true,  Scancode::Left,         KeyCode::Left},
            {"Keypad5",    0x4C, false, Scancode::Keypad5,      KeyCode::None},
        };
        for (const KeyCase& c : cases)
        {
            const SeenKey seen = PressAndCollect(platform, events, c.scan, c.extended);
            if (!seen.got) { Report(std::string("input.key.") + c.name, false, "no key event arrived"); continue; }
            const bool scanOk = seen.scancode == c.scancode;
            // Keypad5's logical key depends on Num Lock, so only its physical identity is asserted.
            const bool codeOk = c.keycode == KeyCode::None || seen.keycode == c.keycode;
            Report(std::string("input.key.") + c.name, scanOk && codeOk,
                   std::string("scancode=") + ToString(seen.scancode) + " keycode=" +
                       ToString(seen.keycode));
        }

        // Left and right modifiers must be distinguishable, which is the whole point of sided
        // scancodes and the thing a naive VK_SHIFT mapping loses.
        const SeenKey left = PressAndCollect(platform, events, 0x2A, false);
        const SeenKey right = PressAndCollect(platform, events, 0x36, false);
        Report("input.key.shiftSidesAreDistinct",
               left.got && right.got && left.scancode != right.scancode &&
                   left.keycode != right.keycode,
               std::string(ToString(left.scancode)) + " vs " + ToString(right.scancode));

        // --- committed text, including a character no keyboard layout here can type -------------
        IPlatformTextInput* textInput = platform.GetTextInput();
        if (textInput != nullptr) textInput->Start(window->GetId(), TextInputType::Text);
        Pump(platform, events, 6);
        events.clear();
        platform.PollEvents(events);

        struct TextCase { const char* name; const wchar_t* utf16; const char* utf8; };
        const TextCase textCases[] = {
            {"ascii", L"A", "A"},
            {"czech", L"č", "\xC4\x8D"},
            {"euro",  L"€", "\xE2\x82\xAC"},
            // U+1F300: a surrogate PAIR. Sent as two UTF-16 units, exactly as Windows delivers it,
            // so the backend has to recombine them rather than emit two broken characters.
            {"surrogate-pair", L"\U0001F300", "\xF0\x9F\x8C\x80"},
        };
        for (const TextCase& c : textCases)
        {
            events.clear();
            platform.PollEvents(events);
            for (const wchar_t* unit = c.utf16; *unit != L'\0'; ++unit)
            {
                SendUnitUtf16(*unit, false);
                SendUnitUtf16(*unit, true);
            }
            const std::string got = CollectText(platform, events, 40);
            Report(std::string("input.text.") + c.name, got == c.utf8,
                   "got " + std::to_string(got.size()) + " bytes, expected " +
                       std::to_string(std::strlen(c.utf8)));
        }
        if (textInput != nullptr) textInput->Stop(window->GetId());

        // --- mouse buttons, including the two extra ones ---------------------------------------
        IPlatformMouse* mouse = platform.GetMouse();
        struct ButtonCase { const char* name; DWORD down; DWORD up; DWORD data; std::uint8_t button; };
        const ButtonCase buttons[] = {
            {"left",   MOUSEEVENTF_LEFTDOWN,   MOUSEEVENTF_LEFTUP,   0,        1},
            {"middle", MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP, 0,        2},
            {"right",  MOUSEEVENTF_RIGHTDOWN,  MOUSEEVENTF_RIGHTUP,  0,        3},
            {"x1",     MOUSEEVENTF_XDOWN,      MOUSEEVENTF_XUP,      XBUTTON1, 4},
            {"x2",     MOUSEEVENTF_XDOWN,      MOUSEEVENTF_XUP,      XBUTTON2, 5},
        };
        // The pointer has to be inside our client area for the click to reach us.
        RECT client{};
        GetClientRect(HandleOf(*window), &client);
        POINT centre{(client.right - client.left) / 2, (client.bottom - client.top) / 2};
        ClientToScreen(HandleOf(*window), &centre);
        SetCursorPos(centre.x, centre.y);
        Pump(platform, events, 8);

        for (const ButtonCase& b : buttons)
        {
            events.clear();
            platform.PollEvents(events);
            SendMouseFlags(b.down, b.data);
            SendMouseFlags(b.up, b.data);
            bool sawPress = false;
            std::uint8_t sawButton = 0;
            for (int round = 0; round < 40 && !sawPress; ++round)
            {
                events.clear();
                platform.PollEvents(events);
                for (const PlatformEvent& event : events)
                    if (const auto* click = std::get_if<MouseButtonEvent>(&event))
                        if (click->pressed) { sawPress = true; sawButton = click->button; break; }
                ::Sleep(5);
            }
            Report(std::string("input.mouse.") + b.name, sawPress && sawButton == b.button,
                   sawPress ? ("button " + std::to_string(sawButton)) : "no button event");
        }

        // --- wheel, both axes -------------------------------------------------------------------
        for (const auto& wheel : {std::pair<const char*, DWORD>{"vertical", MOUSEEVENTF_WHEEL},
                                  std::pair<const char*, DWORD>{"horizontal", MOUSEEVENTF_HWHEEL}})
        {
            events.clear();
            platform.PollEvents(events);
            SendMouseFlags(wheel.second, static_cast<DWORD>(WHEEL_DELTA));
            bool saw = false;
            float dx = 0.0f, dy = 0.0f;
            for (int round = 0; round < 40 && !saw; ++round)
            {
                events.clear();
                platform.PollEvents(events);
                for (const PlatformEvent& event : events)
                    if (const auto* w = std::get_if<MouseWheelEvent>(&event))
                    { saw = true; dx = w->x; dy = w->y; break; }
                ::Sleep(5);
            }
            Report(std::string("input.wheel.") + wheel.first, saw,
                   saw ? ("x=" + std::to_string(dx) + " y=" + std::to_string(dy)) : "no wheel event");
        }

        // --- relative mode: Raw Input, enabled and disabled repeatedly --------------------------
        if (mouse != nullptr && platform.GetCapabilities().relativeMouse)
        {
            bool recovered = true;
            for (int cycle = 0; cycle < 8; ++cycle)
            {
                mouse->SetRelativeMode(window->GetId(), true);
                Pump(platform, events, 4);
                if (!mouse->IsRelativeMode()) { recovered = false; break; }
                SendMouseFlags(MOUSEEVENTF_MOVE);
                Pump(platform, events, 4);
                mouse->SetRelativeMode(window->GetId(), false);
                Pump(platform, events, 4);
                if (mouse->IsRelativeMode()) { recovered = false; break; }
            }
            Report("input.relativeMode.togglesCleanly", recovered, "8 enable/disable cycles");

            // Left enabled, then the window is destroyed under it: the clip must be released and
            // the cursor must be no less visible than it was, or the desktop is left unusable.
            //
            // The cursor question is asked as "did CNA change it", not "is it showing". Cursor
            // visibility is session-global and moves for reasons that have nothing to do with this
            // process -- on this VM it reads hidden whenever the guest pointer is not over a window
            // of the session, which is the normal state for an unattended run. Asserting the
            // absolute state made this check fail for a reason that was not CNA's; asserting that
            // the state is unchanged is the property the backend actually owes.
            CURSORINFO cursorBefore{};
            cursorBefore.cbSize = sizeof(cursorBefore);
            GetCursorInfo(&cursorBefore);
            const bool shownBefore = (cursorBefore.flags & CURSOR_SHOWING) != 0;
            Info("input.relativeMode.cursorBefore", shownBefore ? "showing" : "hidden");

            mouse->SetRelativeMode(window->GetId(), true);
            Pump(platform, events, 6);
            window.reset();
            Pump(platform, events, 12);
            RECT clip{};
            GetClipCursor(&clip);
            RECT virtualScreen{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                               GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                               GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
            const bool released = clip.left <= virtualScreen.left && clip.top <= virtualScreen.top &&
                                  clip.right >= virtualScreen.right && clip.bottom >= virtualScreen.bottom;
            Report("input.relativeMode.cursorRecoveredAfterWindowDestroyed", released,
                   "clip " + std::to_string(clip.right - clip.left) + "x" +
                       std::to_string(clip.bottom - clip.top));
            CURSORINFO cursor{};
            cursor.cbSize = sizeof(cursor);
            GetCursorInfo(&cursor);
            const bool shownAfter = (cursor.flags & CURSOR_SHOWING) != 0;
            Report("input.relativeMode.cursorNoLessVisibleThanBefore", shownAfter || !shownBefore,
                   std::string(shownBefore ? "showing" : "hidden") + " -> " +
                       (shownAfter ? "showing" : "hidden"));
        }
        else { Info("input.relativeMode", "the platform does not claim relativeMouse"); }
        ++checksRun;
    }

} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> checks;
    int iterations = 500;
    std::string clipboardPutFile;
    std::string clipboardGetFile;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--check" && i + 1 < argc) checks.emplace_back(argv[++i]);
        else if (arg == "--iterations" && i + 1 < argc) iterations = std::atoi(argv[++i]);
        else if (arg == "--clipboard-put-file" && i + 1 < argc) clipboardPutFile = argv[++i];
        else if (arg == "--clipboard-get-file" && i + 1 < argc) clipboardGetFile = argv[++i];
        else if (arg == "--list")
        {
            std::cout << "host-ownership dpi displays clipboard cursors handles close wgl input\n";
            return 0;
        }
    }

    // The two clipboard modes exist so that a script can put this process on one side of a real
    // cross-process exchange with Notepad or a terminal, and check the other side itself.
    if (!clipboardGetFile.empty() || !clipboardPutFile.empty())
    {
        std::unique_ptr<IPlatform> platform;
        try { platform = PlatformFactory::Create("Win32"); }
        catch (const std::exception& error)
        {
            std::cerr << "cna_win32_native_desktop: " << error.what() << '\n';
            return 2;
        }
        platform->AcquireSubsystem(PlatformSubsystem::Video);
        IPlatformClipboard* clipboard = platform->GetClipboard();
        if (clipboard == nullptr) { std::cerr << "no clipboard service\n"; return 2; }
        if (!clipboardPutFile.empty())
        {
            std::ifstream in(clipboardPutFile, std::ios::binary);
            if (!in) { std::cerr << "cannot read " << clipboardPutFile << '\n'; return 2; }
            const std::string text((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
            clipboard->SetText(text);
            std::cout << "put " << text.size() << " bytes\n";
        }
        if (!clipboardGetFile.empty())
        {
            std::ofstream out(clipboardGetFile, std::ios::binary | std::ios::trunc);
            if (!out) { std::cerr << "cannot write " << clipboardGetFile << '\n'; return 2; }
            const std::string text = clipboard->GetText();
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            std::cout << "got " << text.size() << " bytes\n";
        }
        return 0;
    }

    if (checks.empty())
        checks = {"host-ownership", "dpi",     "displays", "clipboard", "cursors",
                  "handles",        "close",   "wgl",      "input"};

    // Captured BEFORE the platform exists: that is the whole point of the host-ownership check.
    const HostState before = CaptureHostState();
    std::cout << "process before the platform exists:\n";
    std::cout << "  dpi awareness   : " << before.dpiAwareness << '\n';
    std::cout << "  com apartment   : " << before.comApartment << '\n';
    std::cout << "  timer resolution: " << (before.timerResolution100ns / 10) << " us\n";
    std::cout << "  error mode      : " << before.errorMode << '\n';

    std::unique_ptr<IPlatform> platform;
    try { platform = PlatformFactory::Create("Win32"); }
    catch (const std::exception& error)
    {
        std::cerr << "cna_win32_native_desktop: the Win32 platform could not be created: "
                  << error.what() << '\n';
        return 2;
    }
    if (platform == nullptr) { std::cerr << "no Win32 platform in this build\n"; return 2; }
    platform->AcquireSubsystem(PlatformSubsystem::Video);

    for (const std::string& check : checks)
    {
        std::cout << check << ":\n";
        try
        {
            if (check == "host-ownership") CheckHostOwnership(*platform, before);
            else if (check == "dpi") CheckDpi(*platform);
            else if (check == "displays") CheckDisplays(*platform);
            else if (check == "clipboard") CheckClipboard(*platform);
            else if (check == "cursors") CheckCursors(*platform, iterations);
            else if (check == "handles") CheckNativeHandles(*platform);
            else if (check == "close") CheckCloseIsARequest(*platform);
            else if (check == "wgl") CheckWgl(*platform, iterations);
            else if (check == "input") CheckInput(*platform);
            else Report(check, false, "no such check (try --list)");
        }
        catch (const std::exception& error)
        {
            Report(check, false, std::string("threw: ") + error.what());
        }
    }

    platform->ReleaseSubsystem(PlatformSubsystem::Video);
    std::cout << '\n'
              << (failures == 0 ? "RESULT: every check passed" : "RESULT: a check failed")
              << " (" << checksRun << " groups run, " << failures << " failure(s))\n";
    return failures == 0 ? 0 : 1;
}
