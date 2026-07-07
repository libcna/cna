// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
//
// Task 12.1 regression harness: reproduces DEFERRED.md item #19 from the sibling cna-samples
// repo (../cna-samples/DEFERRED.md and samples/ClientServerSample/missing.md) — calling
// NetworkSession::Create()/Find()/Join() after GamerServicesDispatcher::Initialize() has run
// (i.e. whenever a GamerServicesComponent exists, exactly as every real XNA sample's own
// constructor does) used to spin forever in a tight busy-loop, because
// GamerServicesDispatcher::Update() is a permanently empty no-op (matching FNA's own reference
// source — confirmed to be a real upstream FNA/XNA bug, not just a CNA porting defect) and
// nothing else ever completed the pending NetworkSessionAction once GamerServicesDispatcher's
// UpdateAsync() started unconditionally returning true.
//
// This must run as its own standalone (non-GTest) process rather than a TEST() inside CnaTests:
// GamerServicesDispatcher::Initialize() sets a process-lifetime static (isInitialized_) with no
// way to reset it, which would silently change GamerServicesDispatcher::UpdateAsync()'s behavior
// for every other test sharing the same binary (see the caveat already documented at the top of
// tests/Microsoft/Xna/Framework/GamerServices/GamerServicesServiceTests.cpp). Spawned and
// watchdog-timed by tests/CNA/Internal/Net/GamerServicesDispatcherHangRegressionTest.cpp, mirroring
// the same isolation technique Task 6.1's cna_net_two_process_harness already established for a
// different process-global-state hazard (only one real NetworkSession per process).
//
// Deliberately uses NetworkSessionType::Local (not SystemLink) — ENetBackend::RealNetworkingEnabled
// only returns true for SystemLink, so this harness never touches a real socket; it exercises
// exactly the GamerServicesDispatcher/NetworkSessionAction completion bug and nothing else.
//
// --mode=get-achievements (Task 7.1): the identical "never marks itself completed" bug found
// independently in SignedInGamer::BeginGetAchievements - GetAchievements()'s own polling loop
// only ever completes the action via GamerServicesDispatcher::UpdateAsync() returning false,
// which (same reasoning as above) never happens once GamerServicesDispatcher::Initialize() has
// run. Needs the same process isolation as the default mode, for the same reason.
//
// Exit codes: 0 success, 2 unexpected exception/state, 64 bad usage. (No internal timeout code:
// if a bug regresses, this process hangs and is killed by the outer test's watchdog instead.)
#include "Microsoft/Xna/Framework/GamerServices/AchievementCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "System/IServiceProvider.hpp"

#include <cstdio>
#include <string>
#include <typeinfo>
#include <vector>

using namespace Microsoft::Xna::Framework::Net;
using Microsoft::Xna::Framework::GamerServices::GamerServicesDispatcher;
using Microsoft::Xna::Framework::GamerServices::SignedInGamer;

namespace {
    // GamerServicesDispatcher::Initialize() never dereferences its serviceProvider argument;
    // an empty stub is sufficient (mirrors how GamerServicesComponent::Initialize() forwards
    // Game::getServicesProperty(), but this harness has no live Game to construct one from).
    class NullServiceProvider : public System::IServiceProvider {
    public:
        [[nodiscard]] void* GetService(const std::type_info& /*type*/) const override { return nullptr; }
    };

    int RunNetworkSessionCheck() {
        auto gamer = SignedInGamer::CreateInternal("HarnessPlayer");
        NetworkSession* session = NetworkSession::Create(
            NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
        );

        if (session == nullptr) {
            std::fprintf(stderr, "NetworkSession::Create() returned null\n");
            return 2;
        }
        if (session->getSessionStateProperty() != NetworkSessionState::Lobby) {
            std::fprintf(stderr, "session did not end up in the Lobby state\n");
            session->Dispose();
            return 2;
        }

        session->Dispose();
        return 0;
    }

    int RunGetAchievementsCheck() {
        auto gamer = SignedInGamer::CreateInternal("HarnessPlayer");
        auto achievements = gamer.GetAchievements();
        if (achievements.getCountProperty() != 0) {
            std::fprintf(stderr, "GetAchievements() returned an unexpectedly non-empty collection\n");
            return 2;
        }
        return 0;
    }
}

int main(int argc, char** argv) {
    std::string mode = "network-session";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--mode=", 0) == 0) {
            mode = arg.substr(7);
        }
    }

    try {
        NullServiceProvider services;
        GamerServicesDispatcher::Initialize(services);

        if (mode == "network-session") {
            return RunNetworkSessionCheck();
        }
        if (mode == "get-achievements") {
            return RunGetAchievementsCheck();
        }
        std::fprintf(stderr, "Usage: %s [--mode=network-session|get-achievements]\n", argv[0]);
        return 64;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "Unhandled exception: %s\n", ex.what());
        return 2;
    } catch (...) {
        std::fprintf(stderr, "Unhandled unknown exception\n");
        return 2;
    }
}
