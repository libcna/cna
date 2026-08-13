// SPDX-License-Identifier: MS-PL
//
// Task SDLCORE-011: VibrateController::getDefaultProperty()'s function-local static singleton
// destructs at process-exit static teardown, which can run after the application's own SDL_Quit()
// call -- this environment cannot exercise that ordering hazard by calling the real SDL_Quit()
// inside the shared CnaTests process itself (it would tear down SDL for every other test sharing
// this binary). Spawns tools/devices/shutdown_ordering_harness.cpp, mirroring the precedent set by
// tests/CNA/Internal/Audio/AudioMixerTests.cpp for the same "needs a fresh process" problem.
#include <gtest/gtest.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

namespace
{
    constexpr int kWatchdogSeconds = 10;

    struct SpawnedProcess
    {
        pid_t pid{-1};
        int readFd{-1};
    };

    // Spawns the harness with no arguments (the supported, intended usage -- calls
    // Detail::DevicesShutdownCoordinator::Shutdown() before SDL_Quit()), capturing its stderr for
    // diagnostics on failure. Returns {pid, readFd} on success, {-1, -1} on a spawn-side failure
    // (already reported via ADD_FAILURE).
    SpawnedProcess SpawnHarness()
    {
        int pipeFds[2];
        if (pipe(pipeFds) != 0)
        {
            ADD_FAILURE() << "pipe() failed: " << strerror(errno);
            return {};
        }

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, pipeFds[0]);

        char* argv[] = {const_cast<char*>(CNA_DEVICES_SHUTDOWN_ORDERING_HARNESS_PATH), nullptr};

        pid_t pid = -1;
        int rc = posix_spawn(&pid, CNA_DEVICES_SHUTDOWN_ORDERING_HARNESS_PATH, &actions, nullptr, argv, environ);
        posix_spawn_file_actions_destroy(&actions);
        close(pipeFds[1]); // the child has its own dup'd copy; the parent only needs the read end

        if (rc != 0)
        {
            ADD_FAILURE() << "posix_spawn(" << CNA_DEVICES_SHUTDOWN_ORDERING_HARNESS_PATH
                           << ") failed: " << strerror(rc);
            close(pipeFds[0]);
            return {};
        }
        return SpawnedProcess{pid, pipeFds[0]};
    }

    // Polls non-blockingly for pid to exit, up to deadline; SIGKILLs and reaps it on timeout.
    bool WaitWithWatchdog(pid_t pid, std::chrono::steady_clock::time_point deadline, int* exitCode)
    {
        for (;;)
        {
            int status = 0;
            pid_t rv = waitpid(pid, &status, WNOHANG);
            if (rv == pid)
            {
                *exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                return true;
            }
            if (std::chrono::steady_clock::now() >= deadline)
            {
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
                *exitCode = -1;
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    void DrainRemaining(int fd, std::string* out)
    {
        char buffer[256];
        for (;;)
        {
            ssize_t n = read(fd, buffer, sizeof(buffer));
            if (n <= 0)
            {
                break;
            }
            out->append(buffer, static_cast<std::size_t>(n));
        }
    }
} // namespace

// Task SDLCORE-011: proves the harness process -- which touches
// VibrateController::getDefaultProperty(), calls DevicesShutdownCoordinator::Shutdown(), then the
// real SDL_Quit(), then returns from main() (triggering the singleton's static destructor after
// SDL_Quit() already ran) -- exits cleanly (0) with no crash or hang, both in ordinary CI builds
// (a regression guard against a hang/crash regressing unnoticed) and, when this specific build is
// an ASan one, as empirical proof no heap-use-after-free was detected. See
// Detail::DevicesShutdownCoordinator's own doc comment for the honestly-scoped limitation this
// does *not* prove: a real, successfully-opened haptic_ device is never available in this
// container, so the SDL_CloseHaptic() guard specifically remains reasoned-from-source only, not
// reproduced under ASan here.
TEST(DevicesShutdownOrderingTest, HarnessExitsCleanlyAfterShutdownCoordinatorThenRealSdlQuit)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kWatchdogSeconds);

    SpawnedProcess proc = SpawnHarness();
    ASSERT_NE(proc.pid, -1);

    int exitCode = -1;
    bool finished = WaitWithWatchdog(proc.pid, deadline, &exitCode);

    std::string output;
    DrainRemaining(proc.readFd, &output);
    close(proc.readFd);

    ASSERT_TRUE(finished) << "harness process did not exit before the watchdog deadline and was killed; output: "
                           << output;
    EXPECT_EQ(exitCode, 0) << "expected the harness to exit cleanly; got " << exitCode << "; output: " << output;
}
