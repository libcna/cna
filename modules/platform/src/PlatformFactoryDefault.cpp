// SPDX-License-Identifier: MS-PL
// PlatformFactory::Create(): the implementation selected at build time by CNA_PLATFORM
// (cmake/PlatformSelection.cmake), constructed directly.
//
// Link boundary: kept apart from Create(name) (PlatformFactory.cpp), which names every
// implementation; a game links only the platform it was built for.
#include "CNA/Platform/PlatformFactory.hpp"

#if defined(CNA_PLATFORM_HEADLESS)
#  include "Headless/HeadlessPlatform.hpp"
#elif defined(CNA_PLATFORM_TERMINAL)
#  include "Terminal/TerminalPlatform.hpp"
#elif defined(CNA_PLATFORM_SDL3)
#  include "Sdl3/Sdl3Platform.hpp"
#else
#  include "CNA/Platform/PlatformException.hpp"
#endif

namespace CNA::Platform {

    std::unique_ptr<IPlatform> PlatformFactory::Create()
    {
#if defined(CNA_PLATFORM_HEADLESS)
        return std::make_unique<Headless::HeadlessPlatform>();
#elif defined(CNA_PLATFORM_TERMINAL)
        return std::make_unique<Terminal::TerminalPlatform>();
#elif defined(CNA_PLATFORM_SDL3)
        return std::make_unique<Sdl3::Sdl3Platform>();
#else
        throw PlatformException("PlatformFactory::Create()", "no platform implementation was compiled into this binary");
#endif
    }

    const std::string& PlatformFactory::GetDefaultName()
    {
#if defined(CNA_PLATFORM_HEADLESS)
        static const std::string name = "Headless";
#elif defined(CNA_PLATFORM_TERMINAL)
        static const std::string name = "Terminal";
#else
        static const std::string name = "SDL3";
#endif
        return name;
    }

} // namespace CNA::Platform
