// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-013 -- report "no usable Vulkan device" as SKIPPED, not as a core dump.
//
// Every example in this directory hand-rolls its own `main()` and none of them catch anything: a
// device-creation failure therefore escapes, `std::terminate` runs, and the process aborts with a
// core dump. From CTest's side an abort and an assertion failure look the same, which is exactly
// how an environment problem gets logged as a regression -- the row's own words, and the reason
// this file exists.
//
// The obvious fix is a `try`/`catch` in each of the 131 `main()`s. This is the same fix in one
// place: a `std::terminate` handler, installed by a static initializer, linked into every example
// by `cna_vulkan_test`. Nothing in any example changes, which also means nothing in any example's
// evidence changes.
//
// **The non-goal is enforced by construction, not by care.** The handler recognises exactly the
// two failures the row names -- `PickPhysicalDevice` finding no device with both `VK_KHR_swapchain`
// and a present-capable queue, and the platform refusing to make a Vulkan surface at all -- by
// their message text, and does nothing whatsoever for anything else. An exception from a device
// that WAS created still reaches the previously installed handler, still aborts, and is still
// reported as a failure. Widening the match would turn a real defect into a skip, which is worse
// than the abort this replaces.
//
// The narrow-substring approach is this project's existing precedent, not a new idea:
// `CNA::Examples::RunPixelTest` already does it for LLGL's `VK_ERROR_SURFACE_LOST_KHR`, with the
// same reasoning written beside it.

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

namespace
{
/// The conventional Automake/BSD "test skipped" code, which the root CMakeLists applies as
/// SKIP_RETURN_CODE to every registered test -- see CNA::Examples::kSkipExitCode, duplicated here
/// rather than included because this file must not depend on anything that can itself fail to
/// initialise.
constexpr int kSkipExitCode = 77;

std::terminate_handler gPrevious = nullptr;

/// The two device-creation failures this renderer produces when no usable device exists. Both
/// strings are thrown by VulkanRenderer/CNA::Platform before any VkDevice exists, so neither can
/// be raised by a renderer that got as far as drawing.
bool IsNoUsableDeviceMessage(const std::string& what)
{
    return what.find("Vulkan: no suitable GPU") != std::string::npos ||
           what.find("no Vulkan-capable physical device") != std::string::npos ||
           (what.find("Vulkan") != std::string::npos &&
            what.find("VK_KHR_surface") != std::string::npos) ||
           (what.find("vulkan surface") != std::string::npos &&
            what.find("unsupported") != std::string::npos) ||
           // plan_vulkan.md VULKAN-013, extended after VULKAN-253's own verification run: an X
           // display that has gone away produces `AcquireSubsystem(Video) failed: x11 not
           // available`, and 55 tests aborted with it in one run. That is the same confusion this
           // file exists to stop -- an environment problem indistinguishable from a regression --
           // and it is even further from "a device that WAS created" than the cases above, since
           // no device is ever reached. The match is on the platform's own wording, and the
           // subsystem-acquire failure cannot be raised by a renderer that got as far as drawing.
           (what.find("AcquireSubsystem(Video) failed") != std::string::npos);
}

[[noreturn]] void OnTerminate()
{
    // std::current_exception() is valid inside a terminate handler reached through an uncaught
    // exception; rethrowing is the only way to see its type and message.
    if (auto active = std::current_exception()) {
        try {
            std::rethrow_exception(active);
        } catch (const std::exception& e) {
            const std::string what = e.what();
            if (IsNoUsableDeviceMessage(what)) {
                std::printf("[SKIP] no usable Vulkan device or display in this environment: %s\n",
                            what.c_str());
                std::fflush(stdout);
                // _Exit rather than exit: static destructors would run against a renderer that
                // never finished constructing.
                std::_Exit(kSkipExitCode);
            }
        } catch (...) {
            // Not a std::exception -- nothing this handler claims to recognise.
        }
    }
    if (gPrevious != nullptr) gPrevious();
    std::abort();
}

const bool gInstalled = [] {
    gPrevious = std::set_terminate(&OnTerminate);
    return true;
}();
}  // namespace
