// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/PlatformFactory.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include "Headless/HeadlessPlatform.hpp"

#if defined(CNA_PLATFORM_SDL3)
#  include "Sdl3/Sdl3Platform.hpp"
#endif
#if defined(CNA_PLATFORM_SDL2)
#  include "Sdl2/Sdl2Platform.hpp"
#endif
#if defined(CNA_PLATFORM_WIN32)
#  include "Win32/Win32Platform.hpp"
#endif
#if defined(CNA_PLATFORM_X11)
#  include "X11/X11Platform.hpp"
#endif
#if defined(CNA_PLATFORM_WAYLAND)
#  include "Wayland/WaylandPlatform.hpp"
#endif

// Compiled on every POSIX target regardless of the selection -- see the module's CMakeLists for
// why, and _WIN32 for why not there.
#if !defined(_WIN32)
#  include "Terminal/TerminalPlatform.hpp"
#endif

namespace CNA::Platform {

    namespace {

        // The build-time selection made by cmake/PlatformSelection.cmake. Resolved here rather
        // than at each call site so exactly one file knows which implementations exist.
#if defined(CNA_PLATFORM_HEADLESS)
        const std::string kDefaultName = "Headless";
#elif defined(CNA_PLATFORM_TERMINAL)
        const std::string kDefaultName = "Terminal";
#elif defined(CNA_PLATFORM_WIN32)
        const std::string kDefaultName = "Win32";
#elif defined(CNA_PLATFORM_SDL2)
        const std::string kDefaultName = "SDL2";
#elif defined(CNA_PLATFORM_X11)
        const std::string kDefaultName = "X11";
#elif defined(CNA_PLATFORM_WAYLAND)
        const std::string kDefaultName = "Wayland";
#else
        const std::string kDefaultName = "SDL3";
#endif

    } // namespace

    std::unique_ptr<IPlatform> PlatformFactory::Create()
    {
        return Create(kDefaultName);
    }

    std::unique_ptr<IPlatform> PlatformFactory::Create(const std::string& name)
    {
#if defined(CNA_PLATFORM_SDL3)
        if (name == "SDL3")
        {
            return std::make_unique<Sdl3::Sdl3Platform>();
        }
#endif
#if defined(CNA_PLATFORM_SDL2)
        if (name == "SDL2")
        {
            return std::make_unique<Sdl2::Sdl2Platform>();
        }
#endif
#if defined(CNA_PLATFORM_WIN32)
        if (name == "Win32")
        {
            return std::make_unique<Win32::Win32Platform>();
        }
#endif
#if defined(CNA_PLATFORM_X11)
        if (name == "X11")
        {
            return std::make_unique<X11::X11Platform>();
        }
#endif
#if defined(CNA_PLATFORM_WAYLAND)
        if (name == "Wayland")
        {
            return std::make_unique<Wayland::WaylandPlatform>();
        }
#endif

        // Always available -- see the module's CMakeLists for why it is not gated on
        // CNA_PLATFORM.
        if (name == "Headless")
        {
            return std::make_unique<Headless::HeadlessPlatform>();
        }

#if !defined(_WIN32)
        // Also always available on POSIX, and for the same reason as Headless: the conformance
        // suite is only worth something with more than one implementation live in one process.
        if (name == "Terminal")
        {
            return std::make_unique<Terminal::TerminalPlatform>();
        }
#endif

        // An unknown name refuses rather than returning null or a do-nothing stub, which would
        // let a caller believe it had a working platform.
        throw PlatformException(
            "PlatformFactory::Create(" + name + ")",
            "not compiled into this binary; available: " +
                [] {
                    std::string names;
                    for (const std::string& available : PlatformFactory::GetAvailable())
                    {
                        names += names.empty() ? available : ", " + available;
                    }
                    return names.empty() ? std::string("(none)") : names;
                }());
    }

    std::vector<std::string> PlatformFactory::GetAvailable()
    {
        std::vector<std::string> available;
#if defined(CNA_PLATFORM_SDL3)
        available.emplace_back("SDL3");
#endif
#if defined(CNA_PLATFORM_SDL2)
        available.emplace_back("SDL2");
#endif
#if defined(CNA_PLATFORM_WIN32)
        // Listing it here is also what enrols it in the implementation-neutral conformance suite:
        // PlatformConformanceTests is parameterised over exactly this list.
        available.emplace_back("Win32");
#endif
#if defined(CNA_PLATFORM_X11)
        available.emplace_back("X11");
#endif
#if defined(CNA_PLATFORM_WAYLAND)
        available.emplace_back("Wayland");
#endif
        available.emplace_back("Headless");
#if !defined(_WIN32)
        available.emplace_back("Terminal");
#endif
        return available;
    }

    const std::string& PlatformFactory::GetDefaultName()
    {
        return kDefaultName;
    }

} // namespace CNA::Platform
