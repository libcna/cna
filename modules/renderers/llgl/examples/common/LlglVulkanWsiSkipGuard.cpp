// SPDX-License-Identifier: MS-PL
// LLGL-55: report Vulkan WSI unavailability as an infrastructure skip, for every LLGL test binary,
// not just the ~16 that already route through RunPixelTest<TGame>() (common/PixelTestGame.hpp).
//
// The other ~50 LLGL example tests each hand-roll their own Game-derived class and a bare
// `int main() { Test t; t.Run(); return t.Result(); }` (multi-frame/state-machine shapes that don't
// fit PixelTestGame's single-shot convention -- see that header's own top comment). None of them
// wrap construction/Run() in a try/catch, so on an Xvfb display with no DRI3 support, LLGL's Vulkan
// module throws `std::runtime_error("... VK_ERROR_SURFACE_LOST_KHR")` out of GraphicsDevice
// construction, straight through main(), and `std::terminate()` aborts the process -- a crash ctest
// reports as FAILED ("Subprocess aborted") for a problem that is neither this test's nor this
// renderer's rendering logic, but the test environment's own display capabilities. (Note:
// SKIP_REGULAR_EXPRESSION on the ctest registration was tried first and does NOT help here --
// CTest classifies a signal-terminated process as "Subprocess aborted" before it ever consults that
// property, regardless of what the crashing process printed.)
//
// Rather than retrofit ~50 individual main() functions, this single translation unit is linked into
// every LLGL test executable by cna_llgl_test() (cmake/Tests/LlglTests.cmake) and installs one
// process-wide std::terminate handler at static-init time. It inspects the in-flight exception; if
// its message contains the exact marker LLGL's own VKSwapChain throws, it reports the same
// "[SKIP] ..." + exit(77) contract RunPixelTest already uses (kSkipExitCode -- ctest's bulk
// SKIP_RETURN_CODE 77 registration, see root CMakeLists.txt, then reports it as SKIPPED, not
// FAILED). Any other uncaught exception -- a genuine bug -- falls through to the previous terminate
// handler unchanged, so a real crash still fails loudly.
//
// Deliberately test-only: this lives in examples/common/, linked only into test binaries via
// cna_llgl_test(), never into the LLGL renderer library itself. A real game hitting a genuinely lost
// Vulkan surface should not have that silently swallowed.

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

namespace
{
    constexpr int kLlglVulkanWsiSkipExitCode = 77; // CNA::Examples::kSkipExitCode (common/PixelTestGame.hpp)

    std::terminate_handler g_previousTerminateHandler = nullptr;

    void LlglVulkanWsiSkipTerminateHandler()
    {
        if (const std::exception_ptr eptr = std::current_exception())
        {
            try
            {
                std::rethrow_exception(eptr);
            }
            catch (const std::exception& ex)
            {
                const std::string what = ex.what();
                if (what.find("VK_ERROR_SURFACE_LOST_KHR") != std::string::npos)
                {
                    std::printf("[SKIP] Vulkan WSI unavailable on this display (no DRI3?): %s\n",
                                ex.what());
                    std::fflush(stdout);
                    std::_Exit(kLlglVulkanWsiSkipExitCode);
                }
            }
            catch (...)
            {
                // Not the exception type we're looking for -- fall through to the default handler.
            }
        }
        if (g_previousTerminateHandler != nullptr)
        {
            g_previousTerminateHandler();
        }
        std::abort();
    }

    struct LlglVulkanWsiSkipGuardInstaller
    {
        LlglVulkanWsiSkipGuardInstaller()
        {
            g_previousTerminateHandler = std::set_terminate(LlglVulkanWsiSkipTerminateHandler);
        }
    };

    const LlglVulkanWsiSkipGuardInstaller g_llglVulkanWsiSkipGuardInstaller;
}
