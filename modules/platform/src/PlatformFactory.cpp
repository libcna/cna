// SPDX-License-Identifier: MS-PL

// PlatformFactory::Create(name) and GetAvailable(): every implementation compiled into this
// binary, by name -- what the platform conformance suite runs against.
//
// Link boundary: Create() (PlatformFactoryDefault.cpp) constructs the build's own implementation
// directly. Selecting by name here names every implementation, so a game -- which only ever asks
// for the default -- must not reach this file, or Headless and Terminal ride along with SDL3.
#include "CNA/Platform/PlatformFactory.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include "Headless/HeadlessPlatform.hpp"

#if defined(CNA_PLATFORM_SDL3)
#  include "Sdl3/Sdl3Platform.hpp"
#endif

// Compiled on every POSIX target regardless of the selection -- see the module's CMakeLists for
// why, and _WIN32 for why not there.
#if !defined(_WIN32)
#  include "Terminal/TerminalPlatform.hpp"
#endif

namespace CNA::Platform {

    std::unique_ptr<IPlatform> PlatformFactory::Create(const std::string& name)
    {
#if defined(CNA_PLATFORM_SDL3)
        if (name == "SDL3")
        {
            return std::make_unique<Sdl3::Sdl3Platform>();
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
        available.emplace_back("Headless");
#if !defined(_WIN32)
        available.emplace_back("Terminal");
#endif
        return available;
    }


} // namespace CNA::Platform
