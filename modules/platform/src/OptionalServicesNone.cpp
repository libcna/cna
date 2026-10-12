// SPDX-License-Identifier: MS-PL
// EnableCameraService() and EnableTrayService() where no platform with either is compiled: the
// Headless and Terminal platforms report neither, so there is nothing to install. The SDL3
// platform's are in Sdl3/Sdl3OptionalServices.cpp.
#include "CNA/Platform/OptionalServices.hpp"

#if !defined(CNA_PLATFORM_SDL3)
namespace CNA::Platform {

    void EnableCameraService() {}

    void EnableTrayService() {}
}
#endif
