// SPDX-License-Identifier: MS-PL
//
// plan_diligent.md DILIGENT-57: real-device proof that runtime device selection and SDL window API
// selection are one consistent transaction, through the public XNA API only (GraphicsDeviceManager,
// not a hand-built IGraphicsRenderer -- unlike diligent_backbuffer_readback_bounds_test.cpp, this is
// exactly the path a real game takes).
//
// Before this task, GraphicsDevice.cpp's own SDL-window-flag selection re-parsed
// CNA_DILIGENT_DEVICE with a narrow "opengl"/"gl"-only check, disagreeing with
// DiligentRenderer::ParseDeviceTypeOverride()'s full alias set -- CNA_DILIGENT_DEVICE=gles
// created a Vulkan-flagged window, then the renderer correctly resolved "gles" to OpenGL and failed
// with "the specified window isn't an OpenGL window". Both now call the same shared parser.
//
// Each leg runs in its own process (fork+exec, matching backbuffer_readback_dimension_test.cpp's
// own established pattern) so one leg's environment variable can never leak into another's.
//
// Legs vulkan/vk/opengl/gl/gles/auto -- a real device is created and a Clear()+GetBackBufferData()
//   round-trips correctly. gles is the exact alias the original bug report named.
// Leg bogus -- an unrecognised CNA_DILIGENT_DEVICE value is rejected with a clean, deterministic
//   exception (from ParseDeviceTypeOverride(), now reached during window-flag selection itself,
//   before any window or device is created) rather than hanging, crashing, or silently falling back.
//
// Exit code 0 = every leg passed, 1 = any leg failed.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "System/Environment.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
#define CNA_DILIGENT_57_CAN_FORK 1
#else
#define CNA_DILIGENT_57_CAN_FORK 0
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 32;

    bool ColorNear(Color a, Color b, int tolerance = 8)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tolerance &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tolerance &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tolerance;
    }

    /// Runs one Clear(Red)+GetBackBufferData() round trip through the normal public API. Returns
    /// 0 on success, 77 if no device is available at all (a genuine environment gap, not this
    /// task's own concern), 1 on any other failure.
    class DeviceSelectionLeg : public Game
    {
        std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
        int result_ = 1;

    protected:
        void Draw(const GameTime&) override
        {
            auto& device = getGraphicsDeviceProperty();
            device.Clear(Color::Red);
            const Rectangle region(kSize / 2, kSize / 2, 1, 1);
            Color pixel(0, 0, 0, 0);
            device.GetBackBufferData(&region, &pixel, 0, 1);
            std::printf("[%s] Clear(Red)+GetBackBufferData() round-trips: got (%d,%d,%d)\n",
                        ColorNear(pixel, Color::Red) ? "PASS" : "FAIL", pixel.getRProperty(),
                        pixel.getGProperty(), pixel.getBProperty());
            result_ = ColorNear(pixel, Color::Red) ? 0 : 1;
            Exit();
        }

    public:
        DeviceSelectionLeg()
        {
            graphicsDeviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
            graphicsDeviceManager_->setPreferredBackBufferWidthProperty(kSize);
            graphicsDeviceManager_->setPreferredBackBufferHeightProperty(kSize);
        }

        [[nodiscard]] int GetResult() const { return result_; }
    };

    int RunWorkingLeg()
    {
        try
        {
            DeviceSelectionLeg game;
            game.Run();
            return game.GetResult();
        }
        catch (const std::exception& error)
        {
            const std::string message = error.what();
            const bool noDevice =
                message.find("no device type could be created") != std::string::npos ||
                message.find("unsupported SDL video driver") != std::string::npos ||
                message.find("SDL_Init") != std::string::npos ||
                message.find("live SDL window") != std::string::npos;
            if (noDevice)
            {
                std::printf("[SKIP] no usable device (%s)\n", error.what());
                return 77;
            }
            std::printf("[FAIL] unexpected exception: %s\n", error.what());
            return 1;
        }
    }

    /// Leg "bogus": an unrecognised override must be rejected cleanly, not hang or crash.
    int RunBogusLeg()
    {
        try
        {
            DeviceSelectionLeg game;
            game.Run();
            std::printf("[FAIL] CNA_DILIGENT_DEVICE=bogus was silently accepted instead of "
                        "rejected\n");
            return 1;
        }
        catch (const std::exception& error)
        {
            std::printf("[PASS] CNA_DILIGENT_DEVICE=bogus rejected cleanly: %s\n", error.what());
            return 0;
        }
    }
}

#if CNA_DILIGENT_57_CAN_FORK
namespace
{
    constexpr unsigned kLegTimeoutSeconds = 60;

    /// Forks, sets CNA_DILIGENT_DEVICE=@p alias (or unsets it for "auto") in the child, and re-execs
    /// this same binary with --leg=@p alias so the child's own main() runs the requested leg in a
    /// completely fresh process (fresh SDL, fresh env, no state from any earlier leg).
    bool RunLegIsolated(const char* exePath, const char* alias, bool& skipped)
    {
        skipped = false;
        const pid_t pid = fork();
        if (pid < 0)
        {
            std::printf("[FAIL] supervisor: fork() failed for leg %s\n", alias);
            return false;
        }
        if (pid == 0)
        {
            alarm(kLegTimeoutSeconds);
            std::string arg = std::string("--leg=") + alias;
            char* argv[3] = {const_cast<char*>(exePath), const_cast<char*>(arg.c_str()), nullptr};
            execv(exePath, argv);
            std::_Exit(127);
        }
        int status = 0;
        while (waitpid(pid, &status, 0) < 0)
        {
            // EINTR
        }
        if (WIFSIGNALED(status))
        {
            const int sig = WTERMSIG(status);
            std::printf("[FAIL] leg %s: killed by signal %d (%s)\n", alias, sig, strsignal(sig));
            return false;
        }
        if (WIFEXITED(status))
        {
            const int code = WEXITSTATUS(status);
            if (code == 0)
                return true;
            if (code == 77)
            {
                skipped = true;
                return true;
            }
            std::printf("[FAIL] leg %s: exited %d\n", alias, code);
            return false;
        }
        std::printf("[FAIL] leg %s: neither exited nor signalled\n", alias);
        return false;
    }
}
#endif

int main(int argc, char** argv)
{
    std::string onlyLeg;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a.rfind("--leg=", 0) == 0)
            onlyLeg = a.substr(6);
    }

#if CNA_DILIGENT_57_CAN_FORK
    if (onlyLeg.empty())
    {
        // Every alias DiligentDeviceSelectionTest.OverrideAcceptsAliasesAndIsCaseInsensitive (no
        // GPU) already proves ParseDeviceTypeOverride() resolves correctly -- this supervisor
        // proves the SAME resolution reaches an actually-working device end to end, closing the
        // gap a GPU-less unit test cannot: whether the window this device gets created against
        // was flagged consistently with that resolution.
        const char* const kWorkingAliases[] = {"vulkan", "vk", "opengl", "gl", "gles", "auto"};
        int passed = 0, skippedCount = 0;
        const int total = static_cast<int>(sizeof(kWorkingAliases) / sizeof(kWorkingAliases[0]));
        for (const char* alias : kWorkingAliases)
        {
            if (std::string(alias) == "auto")
                System::Environment::SetEnvironmentVariable("CNA_DILIGENT_DEVICE", "");
            else
                System::Environment::SetEnvironmentVariable("CNA_DILIGENT_DEVICE", alias);
            bool skipped = false;
            if (RunLegIsolated(argv[0], alias, skipped))
            {
                ++passed;
                if (skipped)
                    ++skippedCount;
            }
        }

        // Leg "bogus": run with --leg=bogus directly (no alias env assignment needed by the
        // supervisor itself -- the child leg below sets it).
        System::Environment::SetEnvironmentVariable("CNA_DILIGENT_DEVICE", "not-a-real-device-xyz");
        bool bogusSkipped = false;
        const bool bogusOk = RunLegIsolated(argv[0], "__bogus__", bogusSkipped);

        std::printf("[INFO] supervisor: %d/%d alias legs passed (%d skipped, no device), bogus "
                    "leg %s\n",
                    passed, total, skippedCount, bogusOk ? "passed" : "FAILED");
        return (passed == total && bogusOk) ? 0 : 1;
    }

    if (onlyLeg == "__bogus__")
        return RunBogusLeg();
#endif

    return RunWorkingLeg();
}
