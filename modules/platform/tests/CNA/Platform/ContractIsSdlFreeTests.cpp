// SPDX-License-Identifier: MS-PL
//
// PLAT-27: Phase 1's exit criterion, enforced by the compiler.
//
// This translation unit includes every CNA/Platform header and then asserts that no SDL header
// came along. If any contract header ever grows a direct or transitive SDL include, THIS FILE
// FAILS TO COMPILE -- which is the point. A runtime assertion would be too late; the coupling
// this plan removes is a compile-time one.
//
// tools/platform/check_contract.py keeps the include list below complete, so a newly added
// contract header cannot quietly escape the check by not being listed.

#include "CNA/Audio/Platform/IAudioDevice.hpp"
#include "CNA/Audio/Platform/IAudioRecordingDevice.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/IPlatformGlContext.hpp"
#include "CNA/Platform/IPlatformSurfacePresenter.hpp"
#include "CNA/Platform/IPlatformSystemServices.hpp"
#include "CNA/Platform/IPlatformVulkanSurface.hpp"
#include "CNA/Platform/IPlatformWindow.hpp"
#include "CNA/Platform/Input/IPlatformGamepad.hpp"
#include "CNA/Platform/Input/IPlatformHaptics.hpp"
#include "CNA/Platform/Input/IPlatformInputDevices.hpp"
#include "CNA/Platform/Input/IPlatformJoystick.hpp"
#include "CNA/Platform/Input/IPlatformKeyboard.hpp"
#include "CNA/Platform/Input/IPlatformMouse.hpp"
#include "CNA/Platform/Input/IPlatformSensors.hpp"
#include "CNA/Platform/Input/KeyCode.hpp"
#include "CNA/Platform/Input/Scancode.hpp"
#include "CNA/Platform/Input/IPlatformTextInput.hpp"
#include "CNA/Platform/NativeWindowHandle.hpp"
#include "CNA/Platform/NativeWindowSystem.hpp"
#include "CNA/Platform/PlatformCapabilities.hpp"
#include "CNA/Platform/PlatformCapability.hpp"
#include "CNA/Platform/PlatformEvent.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "CNA/Platform/WindowDescription.hpp"

// Sentinels defined by SDL's own headers. Any of them being visible here means a contract header
// pulled SDL in.
#if defined(SDL_h_) || defined(SDL_MAJOR_VERSION) || defined(SDL_INIT_VIDEO) || \
    defined(SDL_PLATFORM_ANDROID) || defined(SDL_VERSION) || defined(SDL_FUNCTION)
#  error "A CNA/Platform header transitively included SDL. The platform contract must be SDL-free."
#endif

// Likewise for the graphics APIs the services describe with opaque handles: neither
// IPlatformVulkanSurface nor IPlatformGlContext may drag a real API header into every consumer.
#if defined(VK_VERSION_1_0) || defined(VULKAN_H_)
#  error "A CNA/Platform header included Vulkan. Handles are deliberately opaque."
#endif
#if defined(GL_VERSION_1_1) || defined(__gl_h_) || defined(__glew_h__)
#  error "A CNA/Platform header included OpenGL. GetProcAddress returns an untyped pointer."
#endif

#include <gtest/gtest.h>

namespace {

TEST(ContractIsSdlFreeTests, EveryContractHeaderCompilesWithoutSdl)
{
    // The real assertion is the #error block above, evaluated at compile time. This case exists
    // so the guarantee is visible in the test report rather than only in a green build.
    SUCCEED() << "all CNA/Platform headers compiled with no SDL, Vulkan or GL header reachable";
}

} // namespace
