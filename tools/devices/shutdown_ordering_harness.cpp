// SPDX-License-Identifier: MS-PL
//
// Task SDLCORE-011: a small standalone (non-GTest) executable reproducing the exact ordering
// hazard Microsoft::Devices::VibrateController::getDefaultProperty()'s function-local static
// singleton exposes -- its destructor (via Detail::SdlHapticVibrateBackend) makes real
// SDL_CloseHaptic()/SDL_QuitSubSystem() calls, but function-local statics are destroyed at
// process-exit static teardown, *after* main() returns, which can be after the application's own
// explicit SDL_Quit() call. The shared CnaTests binary cannot exercise this: calling the real
// SDL_Quit() there would tear down SDL process-wide for every other test sharing that binary.
//
// Touches VibrateController::getDefaultProperty()->getIsSupportedProperty(), which (via
// AcquireHapticDeviceForProbe() -> EnsureHapticSubsystemInitialized()) sets
// SdlHapticVibrateBackend's subsystemHeld_ = true -- SDL_InitSubSystem(SDL_INIT_HAPTIC) succeeds
// even with no physical haptic device attached, so this reliably exercises the
// SDL_QuitSubSystem(SDL_INIT_HAPTIC) branch this harness is testing regardless of what hardware
// this container has.
//
// Pass "--skip-shutdown-call" to omit the Detail::DevicesShutdownCoordinator::Shutdown() call
// this harness otherwise makes before SDL_Quit() -- reproduces the original bug, used to confirm
// under ASan (cmake-build-devices-asan) that this actually detects a real heap-use-after-free
// when the fix's guard is bypassed, not just that the fixed path happens not to crash.
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
