// SPDX-License-Identifier: MS-PL
//
// Standalone regression harness for VibrateController's static-teardown ordering. Probing the
// controller makes its platform adapter acquire the haptic subsystem. The normal path calls
// DevicesShutdownCoordinator::Shutdown(), which destroys that adapter and balances the platform
// reference before the host's SDL_Quit(). The shared CnaTests binary cannot exercise this because
// SDL_Quit() would tear native services down for every other test in that process.
//
// Pass "--skip-shutdown-call" to omit the Detail::DevicesShutdownCoordinator::Shutdown() call
// this harness otherwise makes before SDL_Quit(), preserving the historical unsafe ordering for
// sanitizer comparison.
//
// Exit code is always 0 if the process reaches the end of main() without crashing -- the actual
// signal this harness exists to produce is an ASan report (or lack of one) on stderr, checked by
// whoever invokes it, not the exit code itself (a real ASan heap-use-after-free report, depending
// on ASAN_OPTIONS, may itself abort the process with a non-zero exit code before reaching here).
#include "Microsoft/Devices/Detail/DevicesShutdownCoordinator.hpp"
#include "Microsoft/Devices/VibrateController.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
    const bool skipShutdownCall = (argc > 1 && std::strcmp(argv[1], "--skip-shutdown-call") == 0);

    (void)Microsoft::Devices::VibrateController::getDefaultProperty()->getIsSupportedProperty();

    if (!skipShutdownCall)
    {
        Microsoft::Devices::Detail::DevicesShutdownCoordinator::Shutdown();
    }

    // The real application shutdown call this harness reproduces the ordering hazard around:
    // VibrateController::instance has not been destroyed yet -- that happens only after main()
    // returns, as part of process-exit static teardown, which is after this SDL_Quit() call.
    SDL_Quit();

    std::fprintf(stderr, "SDL_Quit() returned; now returning from main() to trigger static teardown\n");
    return 0;
}
