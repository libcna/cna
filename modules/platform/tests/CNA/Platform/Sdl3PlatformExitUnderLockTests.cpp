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
// Why a re-executed child
// ------------------------
// The subject is a process that ends. A test cannot assert on `exit()` from inside the process it
// is running in, so each child is a death test in gtest's "threadsafe" style: a fresh execution of
// this binary that runs only its own statement. A plain fork() is not enough -- the child would
// inherit a copy of everything the earlier tests left in this process, and `exit()` would run their
// static destructors too. Run once in a single CnaTests process, the avatar loader's worker thread
// (gamer-services) did exactly that: its destructor joined a thread that does not exist in a
// forked child, and both children hung for a reason that had nothing to do with the lock.
//
// The failure mode is a HANG rather than a crash, so the child arms alarm() first: a hang becomes
// SIGALRM, which the death test reports as a verdict, instead of a test that never returns and a
// `ctest` TIMEOUT that kills the whole binary with none.
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
#include <unistd.h>

#include <cstdlib>

namespace {

using CNA::Platform::IPlatform;
using CNA::Platform::PlatformFactory;
using CNA::Platform::Sdl3::SdlGlobalStateLock;

// The two exit tests are death tests, which GoogleTest builds only where it can fork a child --
// not under Emscripten, which also defines __unix__ (CNA plans/plan_apple_m4.md AM4-280). The
// ownership query below needs no child and runs everywhere this file does.
#if GTEST_HAS_DEATH_TEST
constexpr unsigned kChildDeadlineSeconds = 20;

/// The child's whole life: construct a platform, optionally hold the global-state lock, exit().
[[noreturn]] void ExitWithAPlatform(bool holdGlobalStateLock)
{
    ::alarm(kChildDeadlineSeconds);

    // A function-local static, because `exit()` runs static destructors and skips locals -- and
    // the destructor is the entire subject of this test.
    static std::unique_ptr<IPlatform> platform;
    platform = PlatformFactory::Create("SDL3");

    if (holdGlobalStateLock)
    {
        // A PLAIN LOCAL, and `exit()` is called from inside its scope. `exit()` does not unwind,
        // so this guard is never released -- exactly as an error handler reached from inside a
        // platform call never releases the lock that call was holding. The static destructors
        // exit() then runs therefore execute on a thread that still owns the mutex, which is the
        // whole point of this child.
        //
        // A `static` guard here looks equivalent and is NOT: statics are destroyed in reverse
        // order of construction, so it would be released BEFORE `~Sdl3Platform` ran and the test
        // would pass against the very defect it exists to catch. Measured, not reasoned: the
        // first draft did exactly that and survived the mutation.
        SdlGlobalStateLock held;
        (void)held;
        std::exit(0);
    }
    std::exit(0);
}

TEST(Sdl3PlatformExitUnderLockTest, ControlChildWithoutTheLockExitsPromptly)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_EXIT(ExitWithAPlatform(/*holdGlobalStateLock=*/false), ::testing::ExitedWithCode(0), "")
        << "the control child did not exit cleanly (SIGALRM means it hung): the harness itself is "
           "broken, so the subject child's result below would mean nothing";
}

TEST(Sdl3PlatformExitUnderLockTest, DestructorSurvivesExitReachedFromInsideItsOwnLock)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_EXIT(ExitWithAPlatform(/*holdGlobalStateLock=*/true), ::testing::ExitedWithCode(0), "")
        << "the child hung (SIGALRM) or failed: ~Sdl3Platform re-locked SdlGlobalStateMutex() on "
           "the thread that already owned it (finding F-28). The control test above shows the "
           "harness works.";
}

#endif // GTEST_HAS_DEATH_TEST

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
