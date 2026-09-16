// SPDX-License-Identifier: MS-PL
//
// plans/plan_win32_native_validation.md: the Win32 lifecycle stress and leak harness.
//
// Everything here is deliberately outside the unit-test suite. A GoogleTest case asserts a
// behaviour once; this asserts that ten thousand repetitions of that behaviour cost the process
// nothing -- which is a different question, needs a real Windows kernel to answer (Wine's USER and
// GDI object accounting is its own, not Windows'), and takes long enough that it does not belong
// in a suite people run on every build.
//
// What it measures, per phase, is the process's own resource accounting as Windows reports it:
//
//   GetGuiResources(GR_USEROBJECTS)  -- HWNDs, HCURSORs, HICONs, menus, hooks
//   GetGuiResources(GR_GDIOBJECTS)   -- DCs, brushes, bitmaps, regions, fonts
//   GetProcessHandleCount            -- every kernel handle the process holds
//   PROCESS_MEMORY_COUNTERS.PagefileUsage / WorkingSetSize
//   GetCurrentThreadCount            -- threads a backend started and forgot to join
//
// A leak is a count that ends a phase above where it started, after the phase has been given a
// chance to settle (pumped messages, a short idle). Windows itself caches: the first window of a
// process costs objects that stay for the process lifetime, so every phase warms up before it
// takes its baseline, and the verdict is on the steady state, never on the first iteration.
//
// Usage:
//   cna_win32_native_stress.exe [--iterations N] [--phase NAME]... [--json FILE] [--quiet]
//
// Exit codes: 0 all phases within tolerance; 1 a leak or a failure; 2 the platform could not be
// created at all (nothing proved, nothing claimed).

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/IPlatformWindow.hpp"
#include "CNA/Platform/NativeWindowHandle.hpp"
#include "CNA/Platform/PlatformEvent.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "CNA/Platform/WindowDescription.hpp"

// NOMINMAX before <windows.h>, or windef.h defines min and max as function-like macros and every
// std::max(...) below is parsed as one. MinGW-w64 happens not to define them for C++, so this is a
// defect only cl.exe can show -- the same class of thing modules/platform/src/Win32/Win32Common.hpp
// exists to centralise, and the reason docs/platform-win32.md tells hosts what <windows.h> does.
// (guarded: mingw-w64's libstdc++ already defines NOMINMAX in os_defines.h, which is exactly why
// the MinGW cross-build never saw this.)
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

// docs/platform-win32.md, "what a host must undo": <windows.h> defines CreateWindow, MessageBox,
// CreateDirectory and GetClassName as macros that rewrite any identically named identifier to its
// A/W variant. IPlatform::CreateWindow is one of those names, so without this every call below
// would be compiled as a call to a member function called CreateWindowA that does not exist. That
// is precisely the hazard the documentation tells hosts about, and this harness is a host.
#undef CreateWindow
#undef CreateDirectory
#undef MessageBox
#undef GetClassName

namespace
{
    using CNA::Platform::IPlatform;
    using CNA::Platform::IPlatformWindow;
    using CNA::Platform::PlatformEvent;
    using CNA::Platform::PlatformSubsystem;
    using CNA::Platform::WindowDescription;
    using CNA::Platform::WindowFullscreenMode;

    // ------------------------------------------------------------------ resource accounting

    struct Resources
    {
        std::uint32_t userObjects = 0;
        std::uint32_t gdiObjects = 0;
        std::uint32_t handles = 0;
        std::uint64_t pagefileKb = 0;
        std::uint64_t workingSetKb = 0;
        std::uint32_t threads = 0;
    };

    std::uint32_t CurrentThreadCount()
    {
        // Counting the process's threads through the snapshot API rather than a performance
        // counter keeps this dependency-free and exact.
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return 0;
        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        const DWORD self = GetCurrentProcessId();
        std::uint32_t count = 0;
        if (Thread32First(snapshot, &entry))
        {
            do
            {
                if (entry.th32OwnerProcessID == self)
                    ++count;
            } while (Thread32Next(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return count;
    }

    Resources Sample()
    {
        Resources r{};
        const HANDLE self = GetCurrentProcess();
        r.userObjects = GetGuiResources(self, GR_USEROBJECTS);
        r.gdiObjects = GetGuiResources(self, GR_GDIOBJECTS);
        DWORD handles = 0;
        if (GetProcessHandleCount(self, &handles))
            r.handles = handles;
        PROCESS_MEMORY_COUNTERS memory{};
        memory.cb = sizeof(memory);
        if (GetProcessMemoryInfo(self, &memory, sizeof(memory)))
        {
            r.pagefileKb = static_cast<std::uint64_t>(memory.PagefileUsage / 1024);
            r.workingSetKb = static_cast<std::uint64_t>(memory.WorkingSetSize / 1024);
        }
        r.threads = CurrentThreadCount();
        return r;
    }

    // ------------------------------------------------------------------ harness plumbing

    bool quiet = false;

    void Say(const std::string& line)
    {
        if (!quiet)
        {
            std::cout << line << '\n';
            std::cout.flush();
        }
    }

    // Drains the message queue the way a frame would, so that the DestroyWindow a phase just asked
    // for has actually dispatched WM_DESTROY/WM_NCDESTROY before anything is counted.
    void Settle(IPlatform& platform, std::vector<PlatformEvent>& events, int rounds = 8)
    {
        for (int i = 0; i < rounds; ++i)
        {
            events.clear();
            platform.PollEvents(events);
            ::Sleep(1);
        }
    }

    struct PhaseResult
    {
        std::string name;
        std::uint64_t operations = 0;
        Resources before{};
        Resources after{};
        bool ran = false;
        bool failed = false;
        std::string detail;
        double seconds = 0.0;
    };

    // Windows keeps per-process caches that fill on first use and never shrink; a phase is judged
    // on what it does after that, so these tolerances are for cache settling, not for leaks. A
    // real leak in a 10 000-iteration phase is 10 000 objects, not 8.
    constexpr std::uint32_t kUserTolerance = 8;
    constexpr std::uint32_t kGdiTolerance = 12;
    constexpr std::uint32_t kHandleTolerance = 24;
    constexpr std::uint32_t kThreadTolerance = 2;

    bool Leaked(const PhaseResult& phase, std::string& why)
    {
        std::ostringstream out;
        bool leaked = false;
        auto check = [&](const char* what, std::uint32_t before, std::uint32_t after,
                         std::uint32_t tolerance) {
            if (after > before + tolerance)
            {
                out << (leaked ? "; " : "") << what << ' ' << before << " -> " << after
                    << " (+" << (after - before) << ", tolerance " << tolerance << ')';
                leaked = true;
            }
        };
        check("USER objects", phase.before.userObjects, phase.after.userObjects, kUserTolerance);
        check("GDI objects", phase.before.gdiObjects, phase.after.gdiObjects, kGdiTolerance);
        check("handles", phase.before.handles, phase.after.handles, kHandleTolerance);
        check("threads", phase.before.threads, phase.after.threads, kThreadTolerance);
        why = out.str();
        return leaked;
    }

    WindowDescription Described(const char* title, int width = 320, int height = 240,
                                bool visible = true)
    {
        WindowDescription description;
        description.title = title;
        description.width = width;
        description.height = height;
        description.visible = visible;
        description.centered = false;
        description.x = 40;
        description.y = 40;
        return description;
    }

    // ------------------------------------------------------------------ the phases

    using PhaseBody = void (*)(IPlatform&, std::vector<PlatformEvent>&, int, PhaseResult&);

    void PhaseCreateDestroy(IPlatform& platform, std::vector<PlatformEvent>& events,
                            int iterations, PhaseResult& result)
    {
        for (int i = 0; i < iterations; ++i)
        {
            auto window = platform.CreateWindow(Described("stress create/destroy"));
            events.clear();
            platform.PollEvents(events);
            window.reset();
            ++result.operations;
            if ((i % 64) == 0)
                Settle(platform, events, 2);
        }
    }

    void PhaseResize(IPlatform& platform, std::vector<PlatformEvent>& events, int iterations,
                     PhaseResult& result)
    {
        auto window = platform.CreateWindow(Described("stress resize"));
        for (int i = 0; i < iterations; ++i)
        {
            const int width = 200 + (i % 400);
            const int height = 150 + (i % 300);
            window->SetSize(width, height);
            events.clear();
            platform.PollEvents(events);
            ++result.operations;
        }
    }

    void PhaseShowHide(IPlatform& platform, std::vector<PlatformEvent>& events, int iterations,
                       PhaseResult& result)
    {
        auto window = platform.CreateWindow(Described("stress show/hide"));
        for (int i = 0; i < iterations; ++i)
        {
            if ((i & 1) == 0) window->Hide(); else window->Show();
            events.clear();
            platform.PollEvents(events);
            ++result.operations;
        }
    }

    void PhaseMinimizeRestore(IPlatform& platform, std::vector<PlatformEvent>& events,
                              int iterations, PhaseResult& result)
    {
        auto window = platform.CreateWindow(Described("stress minimize/restore"));
        for (int i = 0; i < iterations; ++i)
        {
            switch (i & 3)
            {
                case 0: window->Minimize(); break;
                case 1: window->Restore(); break;
                case 2: window->Maximize(); break;
                default: window->Restore(); break;
            }
            events.clear();
            platform.PollEvents(events);
            ++result.operations;
        }
        window->Restore();
        Settle(platform, events);
    }

    void PhaseFullscreen(IPlatform& platform, std::vector<PlatformEvent>& events, int iterations,
                         PhaseResult& result)
    {
        auto window = platform.CreateWindow(Described("stress fullscreen"));
        for (int i = 0; i < iterations; ++i)
        {
            window->SetFullscreenMode((i & 1) == 0 ? WindowFullscreenMode::BorderlessFullscreen
                                                   : WindowFullscreenMode::Windowed);
            events.clear();
            platform.PollEvents(events);
            ++result.operations;
        }
        window->SetFullscreenMode(WindowFullscreenMode::Windowed);
        Settle(platform, events);
    }

    void PhaseMultipleWindows(IPlatform& platform, std::vector<PlatformEvent>& events,
                              int iterations, PhaseResult& result)
    {
        // Four live windows, destroyed in a rotating order so that no single ordering can be the
        // only one that unwinds cleanly.
        constexpr int kWindows = 4;
        std::vector<std::unique_ptr<IPlatformWindow>> windows(kWindows);
        for (int i = 0; i < iterations; ++i)
        {
            const int slot = i % kWindows;
            windows[static_cast<std::size_t>(slot)].reset();
            windows[static_cast<std::size_t>(slot)] =
                platform.CreateWindow(Described("stress multi"));
            events.clear();
            platform.PollEvents(events);
            ++result.operations;
        }
        // Destroy in reverse of creation, which is the order a naive registry gets wrong.
        for (int i = kWindows - 1; i >= 0; --i)
            windows[static_cast<std::size_t>(i)].reset();
        Settle(platform, events);
    }

    void PhaseTitleRoundTrip(IPlatform& platform, std::vector<PlatformEvent>& events,
                             int iterations, PhaseResult& result)
    {
        auto window = platform.CreateWindow(Described("stress title"));
        // Non-ASCII on purpose: every iteration is a UTF-8 -> UTF-16 -> UTF-8 round trip through
        // the real Windows title, and a conversion that allocates and forgets shows up here.
        // A plain narrow literal, not u8"": since C++20 that would be char8_t[] and would not
        // convert to std::string. The source is UTF-8 and both toolchains are told so, so these
        // bytes reach Windows as UTF-8 either way -- and the escapes keep the encoding of this
        // file from being the thing under test.
        const std::string base = "stress \xC4\x8D\xC3\xA1st \xE2\x80\x94 \xF0\x9F\x8C\x80 ";
        for (int i = 0; i < iterations; ++i)
        {
            const std::string title = base + std::to_string(i);
            window->SetTitle(title);
            if (window->GetTitle() != title)
            {
                result.failed = true;
                result.detail = "title round trip diverged at iteration " + std::to_string(i);
                return;
            }
            ++result.operations;
        }
        events.clear();
        platform.PollEvents(events);
    }

    void PhaseAdoptRelease(IPlatform& platform, std::vector<PlatformEvent>& events,
                           int iterations, PhaseResult& result)
    {
        // A host-owned HWND, adopted and released repeatedly. CNA must never destroy it, and must
        // never leave a registry entry behind when the wrapper goes away.
        WNDCLASSEXW cls{};
        cls.cbSize = sizeof(cls);
        cls.lpfnWndProc = DefWindowProcW;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"CnaStressHostWindow";
        const ATOM atom = RegisterClassExW(&cls);
        if (atom == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            result.failed = true;
            result.detail = "could not register the host window class";
            return;
        }
        const HWND host = CreateWindowExW(0, L"CnaStressHostWindow", L"host", WS_OVERLAPPEDWINDOW,
                                          10, 10, 200, 160, nullptr, nullptr, cls.hInstance,
                                          nullptr);
        if (host == nullptr)
        {
            result.failed = true;
            result.detail = "could not create the host window";
            return;
        }

        for (int i = 0; i < iterations; ++i)
        {
            auto adopted =
                platform.AdoptWindowHandle(reinterpret_cast<std::uintptr_t>(host));
            if (adopted == nullptr)
            {
                result.failed = true;
                result.detail = "AdoptWindowHandle returned null at iteration " + std::to_string(i);
                break;
            }
            adopted.reset();
            if (IsWindow(host) == FALSE)
            {
                result.failed = true;
                result.detail = "CNA destroyed a host-owned HWND at iteration " + std::to_string(i);
                break;
            }
            ++result.operations;
            events.clear();
            platform.PollEvents(events);
        }

        DestroyWindow(host);
        UnregisterClassW(L"CnaStressHostWindow", cls.hInstance);
        Settle(platform, events);
    }

    struct Phase
    {
        const char* name;
        PhaseBody body;
        int divisor;  // iterations = requested / divisor, for phases where each step is expensive
    };

    const Phase kPhases[] = {
        {"create-destroy", PhaseCreateDestroy, 4},
        {"resize", PhaseResize, 1},
        {"show-hide", PhaseShowHide, 1},
        {"minimize-restore", PhaseMinimizeRestore, 2},
        {"fullscreen", PhaseFullscreen, 8},
        {"multiple-windows", PhaseMultipleWindows, 4},
        {"title-round-trip", PhaseTitleRoundTrip, 1},
        {"adopt-release", PhaseAdoptRelease, 2},
    };

} // namespace

int main(int argc, char** argv)
{
    int iterations = 2000;
    std::string jsonPath;
    std::vector<std::string> selected;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--iterations" && i + 1 < argc) iterations = std::atoi(argv[++i]);
        else if (arg == "--json" && i + 1 < argc) jsonPath = argv[++i];
        else if (arg == "--phase" && i + 1 < argc) selected.emplace_back(argv[++i]);
        else if (arg == "--quiet") quiet = true;
        else if (arg == "--help")
        {
            std::cout << "usage: cna_win32_native_stress [--iterations N] [--phase NAME]..."
                         " [--json FILE] [--quiet]\n";
            return 0;
        }
    }
    iterations = std::max(iterations, 1);

    std::unique_ptr<IPlatform> platform;
    try
    {
        platform = CNA::Platform::PlatformFactory::Create("Win32");
    }
    catch (const std::exception& error)
    {
        std::cerr << "cna_win32_native_stress: the Win32 platform could not be created: "
                  << error.what() << '\n';
        return 2;
    }
    if (platform == nullptr)
    {
        std::cerr << "cna_win32_native_stress: no Win32 platform in this build\n";
        return 2;
    }

    platform->AcquireSubsystem(PlatformSubsystem::Video);
    std::vector<PlatformEvent> events;

    // Warm-up: the first window of a process costs USER and GDI objects that never come back, and
    // counting those as a leak would make every run fail for the wrong reason.
    {
        auto warm = platform->CreateWindow(Described("warm-up", 200, 160));
        Settle(*platform, events);
    }
    Settle(*platform, events, 32);

    std::vector<PhaseResult> results;
    bool anyLeak = false;
    bool anyFailure = false;

    for (const Phase& phase : kPhases)
    {
        if (!selected.empty() &&
            std::find(selected.begin(), selected.end(), phase.name) == selected.end())
            continue;

        PhaseResult result;
        result.name = phase.name;
        result.ran = true;
        const int count = std::max(iterations / phase.divisor, 1);

        Settle(*platform, events, 16);
        result.before = Sample();
        const auto started = std::chrono::steady_clock::now();
        try
        {
            phase.body(*platform, events, count, result);
        }
        catch (const std::exception& error)
        {
            result.failed = true;
            result.detail = std::string("threw: ") + error.what();
        }
        result.seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        Settle(*platform, events, 32);
        result.after = Sample();

        std::string why;
        const bool leaked = Leaked(result, why);
        anyLeak = anyLeak || leaked;
        anyFailure = anyFailure || result.failed;
        if (leaked && result.detail.empty())
            result.detail = why;

        std::ostringstream line;
        line << std::left << std::setw(18) << result.name
             << (result.failed ? " FAIL " : (leaked ? " LEAK " : " ok   "))
             << std::right << std::setw(7) << result.operations << " ops in "
             << std::fixed << std::setprecision(1) << std::setw(6) << result.seconds << " s"
             << "  USER " << result.before.userObjects << "->" << result.after.userObjects
             << "  GDI " << result.before.gdiObjects << "->" << result.after.gdiObjects
             << "  handles " << result.before.handles << "->" << result.after.handles
             << "  threads " << result.before.threads << "->" << result.after.threads
             << "  WS " << result.after.workingSetKb << " KB";
        if (!result.detail.empty())
            line << "\n                   " << result.detail;
        Say(line.str());
        results.push_back(result);
    }

    platform->ReleaseSubsystem(PlatformSubsystem::Video);

    if (!jsonPath.empty())
    {
        std::ofstream out(jsonPath);
        out << "[\n";
        for (std::size_t i = 0; i < results.size(); ++i)
        {
            const PhaseResult& r = results[i];
            std::string why;
            out << "  {\"phase\":\"" << r.name << "\",\"operations\":" << r.operations
                << ",\"seconds\":" << std::fixed << std::setprecision(3) << r.seconds
                << ",\"failed\":" << (r.failed ? "true" : "false")
                << ",\"leaked\":" << (Leaked(r, why) ? "true" : "false")
                << ",\"userBefore\":" << r.before.userObjects
                << ",\"userAfter\":" << r.after.userObjects
                << ",\"gdiBefore\":" << r.before.gdiObjects
                << ",\"gdiAfter\":" << r.after.gdiObjects
                << ",\"handlesBefore\":" << r.before.handles
                << ",\"handlesAfter\":" << r.after.handles
                << ",\"threadsBefore\":" << r.before.threads
                << ",\"threadsAfter\":" << r.after.threads
                << ",\"workingSetKbAfter\":" << r.after.workingSetKb
                << ",\"detail\":\"" << r.detail << "\"}" << (i + 1 < results.size() ? "," : "")
                << '\n';
        }
        out << "]\n";
    }

    if (anyFailure) { Say("RESULT: a phase failed"); return 1; }
    if (anyLeak)    { Say("RESULT: a resource count grew beyond tolerance"); return 1; }
    Say("RESULT: every phase stayed within tolerance");
    return 0;
}
