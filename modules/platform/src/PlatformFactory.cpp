// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/PlatformFactory.hpp"

#include "CNA/Platform/PlatformException.hpp"

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
        // Implementations register here as their phases land: Sdl3Platform (PLAT-28),
        // HeadlessPlatform (PLAT-113), TerminalPlatform (PLAT-130). Until then the factory
        // refuses by name rather than returning a null or a do-nothing stub -- a stub would let
        // a caller believe it had a working platform.
        throw PlatformException(
            "PlatformFactory::Create(" + name + ")",
            "no platform implementation is compiled into this binary yet (plan_platform.md Phase 2)");
    }

    std::vector<std::string> PlatformFactory::GetAvailable()
    {
        return {};
    }

    const std::string& PlatformFactory::GetDefaultName()
    {
        return kDefaultName;
    }

} // namespace CNA::Platform
