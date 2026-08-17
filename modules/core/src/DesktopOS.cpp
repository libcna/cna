//
// Created by robertvokac on 6/1/25.
//

#include "CNA/DesktopOS.hpp"

#include "CNA/CNAException.hpp"
#include "CNA/TargetPlatform.hpp"

namespace CNA
{
    DesktopOS getCurrentDesktopOS()
    {
        if (getCurrentPlatform() != TargetPlatform::Desktop)
        {
            throw CNAException("Current platform is not desktop.");
        }

#if defined(_WIN32)
        return DesktopOS::Windows;
#elif defined(__linux__)
        return DesktopOS::Linux;
#elif defined(__APPLE__)
        return DesktopOS::MacOSX;
#else
        return DesktopOS::Other;
#endif
    }
} // CNA
