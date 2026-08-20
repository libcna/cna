// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
//
// Task 6.1: spawns two real, independent OS processes (tools/net/net_two_process_harness.cpp)
// playing "host" and "client", and verifies they actually join and exchange data over a genuine
// ENet/UDP connection. Every other loopback test in this suite (ENetBackendTests.cpp,
// ENetDiscoveryServiceTests.cpp) proves the transport works within a single process — one real
// NetworkSession plus a raw ENetHostHandle/socket standing in for "the other machine", since only
// one real NetworkSession can exist per process (see NEXT.md section 4/5). This test is the first
// to prove the same transport works across genuinely separate address spaces.
//
// The host's real bound port is handed to the client out-of-band (via a pipe on the host's
// stdout), not through CNA::Internal::Net::ENetDiscoveryService — two independent processes both
// binding that service's shared well-known port is fragile on this platform (see the approved
// Phase 6 plan and ENetDiscoveryService.cpp's own comments on why the loopback-unicast fallback
// exists in the first place). That mechanism stays validated at the single-process
// ENetDiscoveryService::FindSessions() level (Task 5.8).
#include <gtest/gtest.h>

#include <csignal>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <poll.h>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace {
    constexpr int kHarnessInternalTimeoutSeconds = 8;
    constexpr int kOuterWatchdogSeconds = 20;

    struct SpawnedProcess {
        pid_t pid{-1};
        int readFd{-1};
    };

    SpawnedProcess SpawnHarness(const std::vector<std::string>& args) {
        int pipeFds[2];
        if (pipe(pipeFds) != 0) {
            ADD_FAILURE() << "pipe() failed: " << strerror(errno);
            return {};
        }

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, pipeFds[0]);

        std::vector<std::string> argStorage = args;
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(CNA_NET_HARNESS_PATH));
        for (auto& a : argStorage) {
            argv.push_back(a.data());
        }
        argv.push_back(nullptr);

        pid_t pid = -1;
        int rc = posix_spawn(&pid, CNA_NET_HARNESS_PATH, &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        close(pipeFds[1]); // the child has its own dup'd copy; the parent only needs the read end

        if (rc != 0) {
            ADD_FAILURE() << "posix_spawn(" << CNA_NET_HARNESS_PATH << ") failed: " << strerror(rc);
            close(pipeFds[0]);
            return {};
        }
        return SpawnedProcess{pid, pipeFds[0]};
    }

    // Reads from fd, accumulating into *out, until a newline appears or deadline elapses.
    bool ReadLineWithDeadline(int fd, std::chrono::steady_clock::time_point deadline, std::string* out) {
        char buffer[256];
        while (out->find('\n') == std::string::npos) {
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                return false;
            }
            auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();

            struct pollfd pfd{fd, POLLIN, 0};
            int rv = poll(&pfd, 1, static_cast<int>(remainingMs));
            if (rv < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return false;
            }
            if (rv == 0) {
                return false; // timed out
            }
            ssize_t n = read(fd, buffer, sizeof(buffer));
            if (n <= 0) {
                return false; // EOF/error before a full line arrived
            }
            out->append(buffer, static_cast<size_t>(n));
        }
        return true;
    }

    // Only safe to call once the process has already been waited on (so its write end is fully
    // closed and this can't block) — drains any remaining buffered output for diagnostics.
    void DrainRemaining(int fd, std::string* out) {
        char buffer[256];
        for (;;) {
            ssize_t n = read(fd, buffer, sizeof(buffer));
            if (n <= 0) {
                break;
            }
            out->append(buffer, static_cast<size_t>(n));
        }
    }

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

TEST(TwoProcessLoopbackTest, HostAndClientJoinAndExchangeAppDataAcrossRealProcesses) {
    std::string timeoutArg = "--timeout=" + std::to_string(kHarnessInternalTimeoutSeconds);
    auto outerDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(kOuterWatchdogSeconds);

    SpawnedProcess host = SpawnHarness({"--role=host", timeoutArg});
    ASSERT_NE(host.pid, -1);

    std::string hostOutput;
    bool gotPortLine = ReadLineWithDeadline(host.readFd, outerDeadline, &hostOutput);
    ASSERT_TRUE(gotPortLine) << "timed out waiting for the host's PORT= line; output so far: " << hostOutput;

    auto portPos = hostOutput.find("PORT=");
    ASSERT_NE(portPos, std::string::npos) << "host did not print a PORT= line; output was: " << hostOutput;
    int port = std::stoi(hostOutput.substr(portPos + 5));
    ASSERT_GT(port, 0) << "host reported an invalid port";

    SpawnedProcess client = SpawnHarness({"--role=client", "--port=" + std::to_string(port), timeoutArg});
    ASSERT_NE(client.pid, -1);

    int hostExitCode = -1;
    int clientExitCode = -1;
    bool hostFinished = WaitWithWatchdog(host.pid, outerDeadline, &hostExitCode);
    bool clientFinished = WaitWithWatchdog(client.pid, outerDeadline, &clientExitCode);

    DrainRemaining(host.readFd, &hostOutput);
    std::string clientOutput;
    DrainRemaining(client.readFd, &clientOutput);
    close(host.readFd);
    close(client.readFd);

    EXPECT_TRUE(hostFinished) << "host process did not exit before the watchdog deadline and was killed; output: " << hostOutput;
    EXPECT_TRUE(clientFinished) << "client process did not exit before the watchdog deadline and was killed; output: " << clientOutput;
    EXPECT_EQ(hostExitCode, 0) << "host exited with code " << hostExitCode << "; output: " << hostOutput;
    EXPECT_EQ(clientExitCode, 0) << "client exited with code " << clientExitCode << "; output: " << clientOutput;
}

// Task 6.3: ENetBackend::StartHosting used to commit a new session into its Sessions() map
// *before* ENetDiscoveryService::RegisterHost() (which can throw a bind/socket-create failure) -
// leaving a stale, undiscoverable entry with no rollback. This can only be forced deterministically
// in a process where the discovery socket has never yet been bound (see the harness's own comment
// on RunStartHostingPartialFailure for why an isolated process is required, same reasoning as the
// host/client roles above).
TEST(TwoProcessLoopbackTest, StartHostingRollsBackCleanlyOnDiscoveryRegistrationFailure) {
    auto outerDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(kOuterWatchdogSeconds);

    SpawnedProcess proc = SpawnHarness({"--role=start-hosting-partial-failure"});
    ASSERT_NE(proc.pid, -1);

    int exitCode = -1;
    bool finished = WaitWithWatchdog(proc.pid, outerDeadline, &exitCode);

    std::string output;
    DrainRemaining(proc.readFd, &output);
    close(proc.readFd);

    EXPECT_TRUE(finished) << "process did not exit before the watchdog deadline and was killed; output: " << output;
    EXPECT_EQ(exitCode, 0) << "process exited with code " << exitCode << "; output: " << output;
}

// Task 5.5 (plans/plan_net.md Phase 5): spawns 3 real, independent OS processes - one migration-host and
// two migration-survivor roles - and proves real host migration works across genuinely separate
// address spaces: the host dies mid-session, exactly one survivor gets deterministically promoted
// to a real new host (discoverable via a real ENetDiscoveryService::RegisterHost call, reachable
// from a completely different process), the other survivor really finds it via a real LAN
// rediscovery search (ENetDiscoveryService::FindSessions - the one thing host/client's own tests
// above deliberately avoid relying on cross-process, see this file's own top comment on why
// migration-survivor is the deliberate exception), and both exchange one real AppData round trip
// over the fresh post-migration connection.
//
// SurvivorA is spawned first and its own JOINED line is awaited before spawning SurvivorB - host-
// side wire-ids are assigned in ClientHello arrival order (EnsureLocalWireIds's own doc comment),
// so this ordering deterministically makes SurvivorA (not SurvivorB) the one with the lower
// surviving wire id once HostPlayer (wire id 0) dies, and therefore the one Task 5.1's own "lowest
// remaining wire id" rule promotes - a guaranteed outcome, not a race this test has to tolerate.
TEST(TwoProcessLoopbackTest, HostMigrationPromotesOneSurvivorAndTheOtherReconnectsAcrossRealProcesses) {
    // Confirmed flaky at 10s under heavy concurrent load (observed timing out once inside a full
    // ~4700-test suite run, despite 711ms/run consistently in isolation across 8 separate runs) -
    // this test spawns 3 real processes and waits on a real cross-process ENet handshake plus a
    // real UDP discovery round trip, all real wall-clock work that a loaded CI/dev machine can
    // genuinely slow down well past what isolated timing suggests. 30s absorbs that without
    // masking a real hang (a genuine bug still fails this well before the deadline in practice).
    constexpr int kMigrationHarnessTimeoutSeconds = 30;
    std::string timeoutArg = "--timeout=" + std::to_string(kMigrationHarnessTimeoutSeconds);
    auto outerDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(kOuterWatchdogSeconds + 30);

    SpawnedProcess host = SpawnHarness({"--role=migration-host", timeoutArg});
    ASSERT_NE(host.pid, -1);

    std::string hostOutput;
    bool gotPortLine = ReadLineWithDeadline(host.readFd, outerDeadline, &hostOutput);
    ASSERT_TRUE(gotPortLine) << "timed out waiting for the host's PORT= line; output so far: " << hostOutput;
    auto portPos = hostOutput.find("PORT=");
    ASSERT_NE(portPos, std::string::npos) << "host did not print a PORT= line; output was: " << hostOutput;
    int port = std::stoi(hostOutput.substr(portPos + 5));
    ASSERT_GT(port, 0) << "host reported an invalid port";
    std::string portArg = "--port=" + std::to_string(port);

    SpawnedProcess survivorA = SpawnHarness({"--role=migration-survivor", "--gamertag=SurvivorA", portArg, timeoutArg});
    ASSERT_NE(survivorA.pid, -1);

    std::string survivorAOutput;
    bool survivorAJoined = ReadLineWithDeadline(survivorA.readFd, outerDeadline, &survivorAOutput);
    ASSERT_TRUE(survivorAJoined) << "timed out waiting for SurvivorA's JOINED line; output so far: " << survivorAOutput;
    ASSERT_NE(survivorAOutput.find("JOINED"), std::string::npos)
        << "SurvivorA did not print JOINED first; output was: " << survivorAOutput;

    SpawnedProcess survivorB = SpawnHarness({"--role=migration-survivor", "--gamertag=SurvivorB", portArg, timeoutArg});
    ASSERT_NE(survivorB.pid, -1);

    int hostExitCode = -1, survivorAExitCode = -1, survivorBExitCode = -1;
    bool hostFinished = WaitWithWatchdog(host.pid, outerDeadline, &hostExitCode);
    bool survivorAFinished = WaitWithWatchdog(survivorA.pid, outerDeadline, &survivorAExitCode);
    bool survivorBFinished = WaitWithWatchdog(survivorB.pid, outerDeadline, &survivorBExitCode);

    DrainRemaining(host.readFd, &hostOutput);
    DrainRemaining(survivorA.readFd, &survivorAOutput);
    std::string survivorBOutput;
    DrainRemaining(survivorB.readFd, &survivorBOutput);
    close(host.readFd);
    close(survivorA.readFd);
    close(survivorB.readFd);

    EXPECT_TRUE(hostFinished) << "migration-host did not exit before the watchdog deadline and was killed; output: " << hostOutput;
    EXPECT_TRUE(survivorAFinished) << "SurvivorA did not exit before the watchdog deadline and was killed; output: " << survivorAOutput;
    EXPECT_TRUE(survivorBFinished) << "SurvivorB did not exit before the watchdog deadline and was killed; output: " << survivorBOutput;
    EXPECT_EQ(hostExitCode, 0) << "migration-host exited with code " << hostExitCode << "; output: " << hostOutput;
    EXPECT_EQ(survivorAExitCode, 0) << "SurvivorA exited with code " << survivorAExitCode << "; output: " << survivorAOutput;
    EXPECT_EQ(survivorBExitCode, 0) << "SurvivorB exited with code " << survivorBExitCode << "; output: " << survivorBOutput;

    // The deterministic outcome this whole test exists to prove: SurvivorA (the lower surviving
    // wire id) was promoted, SurvivorB genuinely rediscovered and reconnected to it.
    EXPECT_NE(survivorAOutput.find("PROMOTED"), std::string::npos)
        << "expected SurvivorA to be promoted; output: " << survivorAOutput;
    EXPECT_NE(survivorBOutput.find("RECONNECTED"), std::string::npos)
        << "expected SurvivorB to reconnect to the promoted SurvivorA; output: " << survivorBOutput;
}
