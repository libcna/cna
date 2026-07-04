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
        std::fprintf(stderr, "Usage: %s --role=host|client [--port=<n>] [--timeout=<seconds>]\n", argv[0]);
        return 64;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "Unhandled exception: %s\n", ex.what());
        return 2;
    } catch (...) {
        std::fprintf(stderr, "Unhandled unknown exception\n");
        return 2;
    }
}
