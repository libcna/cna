// SPDX-License-Identifier: MS-PL
//
// plans/plan_native_platform_validation.md NPV-0102: a process that exits while CNA's current
// platform -- and so its window-system connection -- is still open.
//
// That is the ordinary end of every application written against the XNA API: CurrentPlatform
// creates the default platform lazily into a function-local static, nothing destroys it before
// main() returns, and exit() tears it down among the static destructors. NPV-0102 found the
// platform's teardown running after statics it still used had been destroyed -- silent in an
// ordinary build, reported by AddressSanitizer. This process is run through SDL3's dummy driver
// and, where a private X server is available, through its x11 driver; the non-zero exit status a
// teardown defect produces under a sanitizer is what CnaPlatformExitOrder* asserts on.
//
// It cannot be a test inside CnaTests: the defect lives in static destruction order at process
// exit, and a test binary's statics were constructed long before any one test runs.
//
// Exit status: 0 on a clean exit, 77 (ctest's skip code) when there is no video subsystem to reach.

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/PlatformException.hpp"

#include <cstdio>
#include <vector>

int main()
{
    using namespace CNA::Platform;

    IPlatform& platform = GetCurrentPlatform();
    try
    {
        platform.AcquireSubsystem(PlatformSubsystem::Video);
    }
    catch (const PlatformException& error)
    {
        std::fprintf(stderr, "SKIP: no video subsystem: %s\n", error.what());
        return 77;
    }

    WindowDescription description;
    description.title = "CNA exit-order harness";
    description.width = 64;
    description.height = 48;
    auto window = platform.CreateWindow(description);
    window->Show();
    std::vector<PlatformEvent> events;
    platform.PollEvents(events);
    window.reset();

    // Deliberately no ReleaseSubsystem and no teardown of the platform: returning from main()
    // with it alive is the case under test.
    return 0;
}
