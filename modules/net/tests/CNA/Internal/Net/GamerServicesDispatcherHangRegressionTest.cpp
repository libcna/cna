// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
//
// Task 12.1 regression test for DEFERRED.md item #19 (../cna-samples), confirmed live while
// porting ClientServerSample: NetworkSession::Create()/Find()/Join() used to spin forever
// (99% CPU) whenever GamerServicesDispatcher::Initialize() had already run — i.e. whenever a
// GamerServicesComponent exists, exactly what every real XNA sample's own constructor does.
//
// Runs the reproduction in tools/net/gamerservices_dispatcher_harness.cpp as a genuinely separate
// OS process (not a TEST() body here) so that GamerServicesDispatcher::Initialize()'s
// process-lifetime static never contaminates this binary's other tests — see that harness file's
// own top-of-file comment, and the same caveat already documented in
// tests/Microsoft/Xna/Framework/GamerServices/GamerServicesServiceTests.cpp. Structurally mirrors
// tests/CNA/Internal/Net/TwoProcessLoopbackTest.cpp's spawn+watchdog helpers (Task 6.1), scaled
// down to one role and no cross-process handshake.
#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>

extern char** environ;

namespace {
    // Local (not SystemLink) means the harness never touches a real socket, so this should
    // complete in well under a second once the fix is in place; generous margin for slow/loaded
    // CI machines. If the bug regresses, the harness spins forever and this watchdog kills it.
    constexpr int kWatchdogSeconds = 10;

    // Polls non-blockingly for pid to exit, up to deadline; SIGKILLs and reaps it on timeout.
    bool WaitWithWatchdog(pid_t pid, std::chrono::steady_clock::time_point deadline, int* exitCode) {
        for (;;) {
            int status = 0;
            pid_t rv = waitpid(pid, &status, WNOHANG);
            if (rv == pid) {
                *exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                return true;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
                *exitCode = -1;
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

TEST(GamerServicesDispatcherHangRegressionTest, CreateDoesNotHangWhenGamerServicesIsInitialized) {
    pid_t pid = -1;
    char* argv[] = {const_cast<char*>(CNA_NET_GAMERSERVICES_HANG_HARNESS_PATH), nullptr};
    int rc = posix_spawn(&pid, CNA_NET_GAMERSERVICES_HANG_HARNESS_PATH, nullptr, nullptr, argv, environ);
    ASSERT_EQ(rc, 0) << "posix_spawn(" << CNA_NET_GAMERSERVICES_HANG_HARNESS_PATH << ") failed: " << strerror(rc);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kWatchdogSeconds);
    int exitCode = -1;
    bool finished = WaitWithWatchdog(pid, deadline, &exitCode);

    EXPECT_TRUE(finished) << "harness did not exit before the watchdog deadline and was killed — "
                             "this is exactly the DEFERRED.md item #19 hang regressing";
    EXPECT_EQ(exitCode, 0) << "harness exited with unexpected code " << exitCode;
}

// Task 7.1: SignedInGamer::BeginGetAchievements had the identical "never marks itself completed"
// bug as DEFERRED.md item #19 above, independently discovered - GetAchievements()'s own polling
// loop only ever completes the action via GamerServicesDispatcher::UpdateAsync() returning false,
// which never happens once GamerServicesDispatcher::Initialize() has run (i.e. whenever a real
// GamerServicesComponent exists). Needs the exact same process isolation as the test above, for
// the same reason (see this file's own top comment and the harness's own top comment).
TEST(GamerServicesDispatcherHangRegressionTest, GetAchievementsDoesNotHangWhenGamerServicesIsInitialized) {
    pid_t pid = -1;
    char* argv[] = {
        const_cast<char*>(CNA_NET_GAMERSERVICES_HANG_HARNESS_PATH),
        const_cast<char*>("--mode=get-achievements"),
        nullptr
    };
    int rc = posix_spawn(&pid, CNA_NET_GAMERSERVICES_HANG_HARNESS_PATH, nullptr, nullptr, argv, environ);
    ASSERT_EQ(rc, 0) << "posix_spawn(" << CNA_NET_GAMERSERVICES_HANG_HARNESS_PATH << ") failed: " << strerror(rc);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kWatchdogSeconds);
    int exitCode = -1;
    bool finished = WaitWithWatchdog(pid, deadline, &exitCode);

    EXPECT_TRUE(finished) << "harness did not exit before the watchdog deadline and was killed — "
                             "this is exactly the Task 7.1 GetAchievements() hang regressing";
    EXPECT_EQ(exitCode, 0) << "harness exited with unexpected code " << exitCode;
}

// Task 7.5: not a hang - reuses this same harness/isolation infrastructure for a memory-leak
// regression instead (GamerServicesDispatcher::Initialize()'s process-lifetime isInitialized_
// static needs the same process isolation either way). A second Initialize() call used to leak
// the first call's 4 SignedInGamer objects permanently (GamerCollection<T> holds non-owning raw
// pointers, and Gamer::setSignedInGamersProperty() only deletes the previous
// SignedInGamerCollection wrapper, not its contents). The harness's own
// GamerServicesDispatcher::GetFreedGamerCountForTesting() check does the real verification;
// this test only confirms the harness process actually completes and reports success.
TEST(GamerServicesDispatcherHangRegressionTest, SecondInitializeDoesNotLeakThePreviousFourGamers) {
    pid_t pid = -1;
    char* argv[] = {
        const_cast<char*>(CNA_NET_GAMERSERVICES_HANG_HARNESS_PATH),
        const_cast<char*>("--mode=initialize-leak-check"),
        nullptr
    };
    int rc = posix_spawn(&pid, CNA_NET_GAMERSERVICES_HANG_HARNESS_PATH, nullptr, nullptr, argv, environ);
    ASSERT_EQ(rc, 0) << "posix_spawn(" << CNA_NET_GAMERSERVICES_HANG_HARNESS_PATH << ") failed: " << strerror(rc);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kWatchdogSeconds);
    int exitCode = -1;
    bool finished = WaitWithWatchdog(pid, deadline, &exitCode);

    EXPECT_TRUE(finished) << "harness did not exit before the watchdog deadline and was killed";
    EXPECT_EQ(exitCode, 0) << "harness exited with unexpected code " << exitCode
                            << " — the second Initialize() call likely leaked or freed the wrong count";
}

// Task 9.8: verifies Initialize()'s actual gamer-population behavior end to end - exact stub
// gamertags/PlayerIndex values, Gamer::getSignedInGamersProperty() ending up with exactly 4
// entries, and SignedInGamer::OnSignIn firing exactly once per gamer - then exercises Task 7.1's
// GetAchievements() fix in the same process, once real initialization has actually happened.
// Needs the exact same process isolation as the tests above, for the same reason.
TEST(GamerServicesDispatcherHangRegressionTest, InitializePopulatesFourStubGamersCorrectly) {
    pid_t pid = -1;
    char* argv[] = {
        const_cast<char*>(CNA_NET_GAMERSERVICES_HANG_HARNESS_PATH),
        const_cast<char*>("--mode=initialize-population-check"),
        nullptr
    };
    int rc = posix_spawn(&pid, CNA_NET_GAMERSERVICES_HANG_HARNESS_PATH, nullptr, nullptr, argv, environ);
    ASSERT_EQ(rc, 0) << "posix_spawn(" << CNA_NET_GAMERSERVICES_HANG_HARNESS_PATH << ") failed: " << strerror(rc);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kWatchdogSeconds);
    int exitCode = -1;
    bool finished = WaitWithWatchdog(pid, deadline, &exitCode);

    EXPECT_TRUE(finished) << "harness did not exit before the watchdog deadline and was killed";
    EXPECT_EQ(exitCode, 0) << "harness exited with unexpected code " << exitCode
                            << " — Initialize()'s gamer population, naming, PlayerIndex "
                               "assignment, or SignedIn firing diverged from what's expected";
}
