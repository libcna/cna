// SPDX-License-Identifier: MS-PL
//
// plans/plan_vulkan.md VULKAN-159 (finding F-28): `~Sdl3Platform` must survive `exit()` reached
// from inside the SDL global-state critical section on its own thread.
//
// The hazard, measured rather than imagined. `~Sdl3Platform` locks `SdlGlobalStateMutex()`, and
// eleven methods hold that same non-recursive mutex across calls into SDL. `exit()` runs static
// destructors on the calling thread, so if it is reached from inside any of them the destructor
// re-locks a mutex its own thread already owns and never returns. Captured with gdb on 2026-09-05:
//
//     pthread_mutex_lock <- ~Sdl3Platform <- exit <- _XDefaultError <- _XError <- XSync
//                        <- X11_PumpEvents <- SDL_CreateWindow <- Sdl3Platform::CreateWindow
//
// VULKAN-154 removed the one route that was firing (Xlib's default handler). This file is about
// the hazard underneath it, which outlives that route -- any future `exit()` from inside a
// platform call reaches the same shape.
//
// Why a forked child
// ------------------
// The subject is a process that ends. A test cannot assert on `exit()` from inside the process it
// is running in, and the failure mode is a HANG rather than a crash, so the parent waits with a
// deadline and reports a failure itself instead of leaving `ctest`'s TIMEOUT to kill the whole
// binary with no verdict.
//
// Two children, because one proves nothing
// ----------------------------------------
// The control child does everything the subject child does EXCEPT hold the lock. If the platform
// could not be constructed here at all, or if `exit()` were slow for some unrelated reason, both
// children would behave the same way and the subject's success would mean nothing. Holding the
// lock is the only difference between them, so a subject-only hang names the lock exactly.

#include "../../../src/Sdl3/Sdl3Synchronization.hpp"

#include "CNA/Platform/PlatformFactory.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#if defined(__linux__) || defined(__unix__)
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <ctime>

namespace {

using CNA::Platform::IPlatform;
using CNA::Platform::PlatformFactory;
using CNA::Platform::Sdl3::SdlGlobalStateLock;

/// Runs one child to completion, or gives up after @p seconds. Returns true when it exited on its
/// own, and reports the exit status through @p exitCode.
[[nodiscard]] bool RunChild(bool holdGlobalStateLock, int seconds, int& exitCode)
{
    const pid_t pid = ::fork();
    if (pid == -1) return false;

    if (pid == 0)
    {
        // A function-local static, because `exit()` runs static destructors and skips locals --
        // and the destructor is the entire subject of this test.
        static std::unique_ptr<IPlatform> platform;
        platform = PlatformFactory::Create("SDL3");

        if (holdGlobalStateLock)
        {
            // A PLAIN LOCAL, and `exit()` is called from inside its scope. `exit()` does not
            // unwind, so this guard is never released -- exactly as an error handler reached from
            // inside a platform call never releases the lock that call was holding. The static
            // destructors exit() then runs therefore execute on a thread that still owns the mutex,
            // which is the whole point of this child.
            //
            // A `static` guard here looks equivalent and is NOT: statics are destroyed in reverse
            // order of construction, so it would be released BEFORE `~Sdl3Platform` ran and the
            // test would pass against the very defect it exists to catch. Measured, not reasoned:
            // the first draft did exactly that and survived the mutation.
            SdlGlobalStateLock held;
            (void)held;
            std::exit(0);
        }
        std::exit(0);
    }

    // Parent.
    const std::time_t deadline = std::time(nullptr) + seconds;
    for (;;)
    {
        int status = 0;
        const pid_t r = ::waitpid(pid, &status, WNOHANG);
        if (r == pid)
        {
            exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            return true;
        }
        if (r == -1) return false;
        if (std::time(nullptr) >= deadline)
        {
            ::kill(pid, SIGKILL);
            int discard = 0;
            (void)::waitpid(pid, &discard, 0);
            return false;
        }
        ::usleep(20 * 1000);
    }
}

TEST(Sdl3PlatformExitUnderLockTest, ControlChildWithoutTheLockExitsPromptly)
{
    int code = -1;
    ASSERT_TRUE(RunChild(/*holdGlobalStateLock=*/false, /*seconds=*/20, code))
        << "the control child did not finish: the harness itself is broken, so the subject "
           "child's result below would mean nothing";
    EXPECT_EQ(code, 0);
}

TEST(Sdl3PlatformExitUnderLockTest, DestructorSurvivesExitReachedFromInsideItsOwnLock)
{
    int code = -1;
    ASSERT_TRUE(RunChild(/*holdGlobalStateLock=*/true, /*seconds=*/20, code))
        << "the child hung: ~Sdl3Platform re-locked SdlGlobalStateMutex() on the thread that "
           "already owned it (finding F-28). The control test above shows the harness works.";
    EXPECT_EQ(code, 0);
}

TEST(Sdl3PlatformExitUnderLockTest, OwnershipQueryIsFalseWhenNothingIsHeld)
{
    // The destructor's decision rests on this query, so it is asserted directly as well: a query
    // that answered "held" unconditionally would make the fix look correct while disabling the
    // mutex for every ordinary destruction.
    EXPECT_FALSE(CNA::Platform::Sdl3::SdlGlobalStateHeldByThisThread());
    {
        SdlGlobalStateLock lock;
        EXPECT_TRUE(CNA::Platform::Sdl3::SdlGlobalStateHeldByThisThread());
    }
    EXPECT_FALSE(CNA::Platform::Sdl3::SdlGlobalStateHeldByThisThread());
}

} // namespace

#endif // linux || unix
