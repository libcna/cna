#include <cstdio>

#include "System/IServiceProvider.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Net/GameEndedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/GameStartedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionEndedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/WriteLeaderboardsEventArgs.hpp"

// Task 15.7: cna_demo_session_lifecycle_events. Console-only, single process,
// NetworkSessionType::Local (no real networking at all - purely the local event queue).
//
// Demonstrates NetworkSession::StartGame()/EndGame() and the resulting NetworkSessionState
// transitions/GameStarted/GameEnded events, then an honest spotlight on a documented gap: this
// codebase's WriteArbitratedLeaderboard/WriteUnarbitratedLeaderboard/WriteTrueSkill delegates are
// fully wired (subscribing and Raise()-ing them works correctly) but nothing in production code
// ever calls Raise() on them (confirmed by grep - zero call sites outside this demo) - a real,
// intentional CNA/FNA-parity gap (FNA's own arbitration/leaderboard-writing pipeline was never
// implemented either), not something silently broken. This demo proves the delegate wiring itself
// is sound by manually raising all three, while being explicit in its own output that production
// code never does this on its own.
//
// Also corrects an imprecision in this task's own original description: EndGame() transitions
// NetworkSessionState::Playing back to Lobby (not Ended) - confirmed by reading NetworkSession.cpp
// directly. Reaching Ended requires the local gamer to actually leave the session (this demo does
// so explicitly via RemoveGamer(), which is what a real disconnect/session-end path funnels
// through), giving a genuine, complete Lobby -> Playing -> Lobby -> Ended tour.

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Net;
using Microsoft::Xna::Framework::GamerServices::SignedInGamer;

namespace
{
    class NullServiceProvider : public System::IServiceProvider
    {
    public:
        [[nodiscard]] void* GetService(const std::type_info& /*type*/) const override { return nullptr; }
    };

    const char* StateName(NetworkSessionState state)
    {
        switch (state)
        {
            case NetworkSessionState::Lobby: return "Lobby";
            case NetworkSessionState::Playing: return "Playing";
            case NetworkSessionState::Ended: return "Ended";
        }
        return "?";
    }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    NullServiceProvider services;
    Microsoft::Xna::Framework::GamerServices::GamerServicesDispatcher::Initialize(services);
    SignedInGamer* localGamer = (*Microsoft::Xna::Framework::GamerServices::Gamer::getSignedInGamersProperty())[0];

    NetworkSession* session = NetworkSession::Create(NetworkSessionType::Local, 1, 8);
    session->GameStarted += [](System::Object*, const GameStartedEventArgs&) {
        std::printf("[Lifecycle] GameStarted fired.\n");
    };
    session->GameEnded += [](System::Object*, const GameEndedEventArgs&) {
        std::printf("[Lifecycle] GameEnded fired.\n");
    };
    session->SessionEnded += [](System::Object*, const NetworkSessionEndedEventArgs&) {
        std::printf("[Lifecycle] SessionEnded fired.\n");
    };
    int leaderboardFireCount = 0;
    auto leaderboardHandler = [&leaderboardFireCount](const char* name) {
        return [&leaderboardFireCount, name](System::Object*, const WriteLeaderboardsEventArgs& e) {
            ++leaderboardFireCount;
            std::printf("[Lifecycle] %s fired for \"%s\" (isLeaving=%s)\n", name,
                        e.getGamerProperty()->getGamertagProperty().c_str(),
                        e.getIsLeavingProperty() ? "true" : "false");
        };
    };
    session->WriteArbitratedLeaderboard += leaderboardHandler("WriteArbitratedLeaderboard");
    session->WriteUnarbitratedLeaderboard += leaderboardHandler("WriteUnarbitratedLeaderboard");
    session->WriteTrueSkill += leaderboardHandler("WriteTrueSkill");

    LocalNetworkGamer* localNetworkGamer = session->getLocalGamersProperty()[0];

    std::printf("[Lifecycle] Initial state: %s\n", StateName(session->getSessionStateProperty()));

    session->StartGame();
    session->Update();
    std::printf("[Lifecycle] After StartGame()+Update(): %s\n", StateName(session->getSessionStateProperty()));

    session->EndGame();
    session->Update();
    std::printf("[Lifecycle] After EndGame()+Update(): %s (back to Lobby, not Ended - EndGame()'s "
                "own real transition target, confirmed by reading NetworkSession.cpp)\n",
                StateName(session->getSessionStateProperty()));

    std::printf("[Lifecycle] Manually raising all 3 leaderboard delegates (production code never "
                "does this - confirmed zero real call sites by grep):\n");
    session->WriteArbitratedLeaderboard.Raise(session, WriteLeaderboardsEventArgs::CreateInternal(localNetworkGamer, false));
    session->WriteUnarbitratedLeaderboard.Raise(session, WriteLeaderboardsEventArgs::CreateInternal(localNetworkGamer, false));
    session->WriteTrueSkill.Raise(session, WriteLeaderboardsEventArgs::CreateInternal(localNetworkGamer, false));

    session->RemoveGamer(localNetworkGamer, NetworkSessionEndReason::HostEndedSession);
    session->Update();
    std::printf("[Lifecycle] After RemoveGamer(localGamer)+Update(): %s\n",
                StateName(session->getSessionStateProperty()));

    std::printf("[Lifecycle] Summary: leaderboardDelegateFireCount=%d (expected 3 - only from this "
                "demo's own manual Raise() calls, never from production code)\n", leaderboardFireCount);

    session->Dispose();
    return 0;
}
