// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/PlatformFactory.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include "Headless/HeadlessPlatform.hpp"

#if defined(CNA_PLATFORM_SDL3)
#  include "Sdl3/Sdl3Platform.hpp"
#endif

namespace CNA::Platform {

    namespace {

        // The build-time selection made by cmake/PlatformSelection.cmake. Resolved here rather
        // than at each call site so exactly one file knows which implementations exist.
#if defined(CNA_PLATFORM_HEADLESS)
        const std::string kDefaultName = "Headless";
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

        // Always available -- see the module's CMakeLists for why it is not gated on
        // CNA_PLATFORM.
        if (name == "Headless")
        {
            return std::make_unique<Headless::HeadlessPlatform>();
        }

        // Remaining implementations register here as their phases land: TerminalPlatform
        // (PLAT-130). An unknown name refuses rather than
        // returning null or a do-nothing stub, which would let a caller believe it had a
        // working platform.
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
        return available;
    }

    const std::string& PlatformFactory::GetDefaultName()
    {
        return kDefaultName;
    }

} // namespace CNA::Platform
