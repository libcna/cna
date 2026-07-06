// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
//
// Task 6.1: a small standalone (non-GTest) executable that plays either the "host" or "client"
// role in a real two-process ENet loopback test. Every Phase 5 test proved the ENet backend works
// within a single process (one real NetworkSession plus a raw ENetHostHandle/socket standing in
// for "the other machine" — see NEXT.md section 4/5 for why only one real NetworkSession can
// exist per process). This harness is spawned twice, as two genuinely independent OS processes,
// by tests/CNA/Internal/Net/TwoProcessLoopbackTest.cpp, to prove the same real transport works
// across separate address spaces too.
//
// The host prints "PORT=<n>\n" to stdout as soon as it has a real bound ENet port, then waits for
// the client to join and exchange one AppData round trip. The client receives the host's port via
// --port (handed to it out-of-band by the orchestrating test, not through network discovery — see
// the approved plan for why cross-process ENetDiscoveryService port sharing was deliberately not
// used here). Exit codes: 0 success, 1 internal timeout, 2 unexpected exception or protocol
// mismatch, 64 bad usage.
#include "CNA/Internal/Net/ENetBackend.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include <sys/resource.h>

using namespace CNA::Internal::Net;
using namespace Microsoft::Xna::Framework::Net;
using Microsoft::Xna::Framework::GamerServices::SignedInGamer;

namespace {
    constexpr SharpRuntime::bytecs kMagicPayload[] = {0x42, 0x13, 0x37, 0x99};
    constexpr auto kPollInterval = std::chrono::milliseconds(2);
    constexpr auto kSendFlushWindow = std::chrono::milliseconds(500);

    NetworkGamer* FindRemoteGamer(NetworkSession* session) {
        for (NetworkGamer* gamer : session->getAllGamersProperty()) {
            if (!gamer->getIsLocalProperty()) {
                return gamer;
            }
        }
        return nullptr;
    }

    // Polls session->Update() until predicate is true or deadline elapses. Returns false on
    // timeout (caller decides how to fail).
    template <typename Predicate>
    bool PumpUntil(NetworkSession* session, std::chrono::steady_clock::time_point deadline, Predicate predicate) {
        while (!predicate()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            session->Update();
            std::this_thread::sleep_for(kPollInterval);
        }
        return true;
    }

    int RunHost(int timeoutSeconds) {
        auto gamer = SignedInGamer::CreateInternal("HostPlayer");
        NetworkSession* session = NetworkSession::Create(
            NetworkSessionType::SystemLink, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
        );

        uint16_t port = ENetBackend::GetBoundPort(session);
        if (port == 0) {
            std::fprintf(stderr, "host: never bound a real ENet port\n");
            session->Dispose();
            return 2;
        }
        std::printf("PORT=%u\n", static_cast<unsigned>(port));
        std::fflush(stdout);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);

        if (!PumpUntil(session, deadline, [&] { return session->getAllGamersProperty().getCountProperty() >= 2; })) {
            std::fprintf(stderr, "host: timed out waiting for client to join\n");
            session->Dispose();
            return 1;
        }

        NetworkGamer* remote = FindRemoteGamer(session);
        if (remote == nullptr) {
            std::fprintf(stderr, "host: joined roster has no remote gamer\n");
            session->Dispose();
            return 2;
        }

        LocalNetworkGamer* localGamer = session->getLocalGamersProperty()[0];
        std::vector<SharpRuntime::bytecs> received(sizeof(kMagicPayload));
        NetworkGamer* sender = nullptr;
        bool gotPayload = false;

        if (!PumpUntil(session, deadline, [&] {
                if (!localGamer->getIsDataAvailableProperty()) {
                    return false;
                }
                int len = localGamer->ReceiveData(received, sender);
                gotPayload = (len == static_cast<int>(sizeof(kMagicPayload)));
                return true;
            })) {
            std::fprintf(stderr, "host: timed out waiting for client's payload\n");
            session->Dispose();
            return 1;
        }
        if (!gotPayload) {
            std::fprintf(stderr, "host: received payload had unexpected length\n");
            session->Dispose();
            return 2;
        }

        localGamer->SendData(received, SendDataOptions::Reliable, remote);

        // Give ENet a real window to actually flush the echo out before the process exits.
        auto flushDeadline = std::chrono::steady_clock::now() + kSendFlushWindow;
        while (std::chrono::steady_clock::now() < flushDeadline) {
            session->Update();
            std::this_thread::sleep_for(kPollInterval);
        }

        session->Dispose();
        return 0;
    }

    int RunClient(uint16_t port, int timeoutSeconds) {
        if (port == 0) {
            std::fprintf(stderr, "client: --port is required and must be nonzero\n");
            return 64;
        }

        auto gamer = SignedInGamer::CreateInternal("ClientPlayer");
        NetworkSession* session = NetworkSession::Create(
            NetworkSessionType::SystemLink, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
        );

        ENetBackend::ConnectToHost(session, "127.0.0.1", port);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);

        if (!PumpUntil(session, deadline, [&] { return session->getAllGamersProperty().getCountProperty() >= 2; })) {
            std::fprintf(stderr, "client: timed out waiting to join host\n");
            session->Dispose();
            return 1;
        }

        NetworkGamer* remote = FindRemoteGamer(session);
        if (remote == nullptr) {
            std::fprintf(stderr, "client: joined roster has no remote gamer\n");
            session->Dispose();
            return 2;
        }

        LocalNetworkGamer* localGamer = session->getLocalGamersProperty()[0];
        std::vector<SharpRuntime::bytecs> payload(kMagicPayload, kMagicPayload + sizeof(kMagicPayload));
        localGamer->SendData(payload, SendDataOptions::Reliable, remote);

        std::vector<SharpRuntime::bytecs> received(sizeof(kMagicPayload));
        NetworkGamer* sender = nullptr;
        bool gotReply = false;

        if (!PumpUntil(session, deadline, [&] {
                if (!localGamer->getIsDataAvailableProperty()) {
                    return false;
                }
                int len = localGamer->ReceiveData(received, sender);
                gotReply = (len == static_cast<int>(sizeof(kMagicPayload)));
                return true;
            })) {
            std::fprintf(stderr, "client: timed out waiting for host's echo\n");
            session->Dispose();
            return 1;
        }

        session->Dispose();

        if (!gotReply || received != payload) {
            std::fprintf(stderr, "client: echoed payload did not match what was sent\n");
            return 2;
        }
        return 0;
    }

    // Task 6.3: ENetBackend::StartHosting used to emplace() the new session into its Sessions()
    // map *before* calling ENetDiscoveryService::RegisterHost(), which can throw (EnsureSocket's
    // enet_socket_create()/enet_socket_bind() failure). A throw there used to leave a permanent,
    // leaked Sessions() entry - a real, live, still-bound ENet host that's never discoverable via
    // Find() and never torn down (TeardownSession only ever runs from NetworkSession::Dispose(),
    // and the failed NetworkSession was never fully constructed, so no caller ever holds a
    // pointer to Dispose()).
    //
    // Forcing this deterministically needs an isolated process for two independent reasons: (1)
    // ENetDiscoveryService's own discovery socket is a process-wide singleton with no reset hook
    // (see ENetLibrary's own precedent) - once successfully bound, EnsureSocket() never attempts
    // to bind again, so this must run somewhere that has never touched it before, not inside
    // CnaTests, where countless earlier tests already have; and (2) forcing an actual bind
    // failure by occupying the discovery port with another socket doesn't work at all - this port
    // is deliberately designed so any two REUSEADDR-set UDP sockets can coexist on it (see
    // ENetDiscoveryService.cpp's own EnsureSocket, Task 6.5's note, and NEXT.md), so a
    // port-conflict approach is inherently racy/unreliable (confirmed empirically: it failed for
    // the wrong reason under real CnaTests suite ordering). Instead, this lowers RLIMIT_NOFILE to
    // exactly one more than the number of file descriptors already open (stdin/stdout/stderr, the
    // last two dup2'd onto the orchestrating test's pipe) - a portable, deterministic way to make
    // the *second* socket()/open() call from this point on fail with EMFILE. The "+1" headroom
    // matters: StartHosting's own ENetHostHandle::CreateHost() call (its real game-hosting ENet
    // host) opens one socket *before* ever reaching RegisterHost()'s discovery socket, and must be
    // allowed to succeed normally so the failure actually lands where this test needs it to -
    // inside RegisterHost() itself, exercising the exact reordering this task fixed - rather than
    // failing a step earlier (which the original, buggier limit of "already-open count, no
    // headroom" was confirmed to do: it made CreateHost() itself throw first, meaning neither the
    // buggy nor the fixed emplace()/RegisterHost() ordering was ever actually exercised, and the
    // test passed either way for the wrong reason).
    //
    // Confirms NetworkSession::Create(SystemLink, ...) throws cleanly (not a crash/hang) under
    // that condition, then asserts via ENetBackend::GetSessionCountForTesting() that the failed
    // attempt left *zero* sessions registered - the real, deterministic proof (not dependent on
    // whether a later allocation happens to reuse the failed session's freed address, which a
    // naive "does a retry work" check would be). Restores the original file descriptor limit and
    // retries with the same session type as a secondary sanity check that real hosting still
    // works normally afterward.
    int RunStartHostingPartialFailure() {
        rlimit originalLimit{};
        if (getrlimit(RLIMIT_NOFILE, &originalLimit) != 0) {
            std::fprintf(stderr, "partial-failure: getrlimit(RLIMIT_NOFILE) failed\n");
            return 2;
        }

        // 3 covers stdin/stdout/stderr, the only descriptors this minimal harness has open at
        // this point; +1 is the headroom for ENetHostHandle::CreateHost()'s own socket (see the
        // function's own top comment for why this exact number matters). Lowering rlim_cur below
        // the current open-descriptor count is POSIX-legal and doesn't need closing anything
        // first - it only blocks descriptors opened *after* this point.
        rlimit exhaustedLimit = originalLimit;
        exhaustedLimit.rlim_cur = 4;
        if (setrlimit(RLIMIT_NOFILE, &exhaustedLimit) != 0) {
            std::fprintf(stderr, "partial-failure: setrlimit(RLIMIT_NOFILE) failed\n");
            return 2;
        }

        if (ENetBackend::GetSessionCountForTesting() != 0) {
            std::fprintf(stderr, "partial-failure: expected a freshly-started process with zero registered "
                                  "sessions before this test even runs\n");
            setrlimit(RLIMIT_NOFILE, &originalLimit);
            return 2;
        }

        auto gamer = SignedInGamer::CreateInternal("HostPlayer");

        bool threwAsExpected = false;
        try {
            NetworkSession* neverConstructed = NetworkSession::Create(
                NetworkSessionType::SystemLink, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
            );
            (void) neverConstructed;
        } catch (const std::exception&) {
            threwAsExpected = true;
        }

        // Restore the real limit; the retry below needs to actually open a socket successfully.
        setrlimit(RLIMIT_NOFILE, &originalLimit);

        if (!threwAsExpected) {
            std::fprintf(stderr, "partial-failure: expected NetworkSession::Create to throw while the "
                                  "file descriptor limit was exhausted, but it did not\n");
            return 2;
        }

        // The real proof: StartHosting must be all-or-nothing. A correct fix never commits the
        // failed session into Sessions() at all, regardless of where the retry below happens to
        // land on the heap.
        std::size_t countAfterFailure = ENetBackend::GetSessionCountForTesting();
        if (countAfterFailure != 0) {
            std::fprintf(stderr, "partial-failure: %zu session(s) left registered after the failed "
                                  "StartHosting attempt - expected exactly 0\n", countAfterFailure);
            return 2;
        }

        NetworkSession* retry = NetworkSession::Create(
            NetworkSessionType::SystemLink, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
        );
        uint16_t port = ENetBackend::GetBoundPort(retry);
        if (port == 0) {
            std::fprintf(stderr, "partial-failure: retry never bound a real ENet port\n");
            retry->Dispose();
            return 2;
        }

        retry->Dispose();
        return 0;
    }
}

int main(int argc, char** argv) {
    std::string role;
    uint16_t port = 0;
    int timeoutSeconds = 8;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--role=", 0) == 0) {
            role = arg.substr(7);
        } else if (arg.rfind("--port=", 0) == 0) {
            port = static_cast<uint16_t>(std::stoi(arg.substr(7)));
        } else if (arg.rfind("--timeout=", 0) == 0) {
            timeoutSeconds = std::stoi(arg.substr(10));
        }
    }

    try {
        if (role == "host") {
            return RunHost(timeoutSeconds);
        }
        if (role == "client") {
            return RunClient(port, timeoutSeconds);
        }
        if (role == "start-hosting-partial-failure") {
            return RunStartHostingPartialFailure();
        }
        std::fprintf(stderr,
                      "Usage: %s --role=host|client|start-hosting-partial-failure [--port=<n>] [--timeout=<seconds>]\n",
                      argv[0]);
        return 64;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "Unhandled exception: %s\n", ex.what());
        return 2;
    } catch (...) {
        std::fprintf(stderr, "Unhandled unknown exception\n");
        return 2;
    }
}
