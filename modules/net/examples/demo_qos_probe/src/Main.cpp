#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "System/IServiceProvider.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSessionCollection.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp"
#include "Microsoft/Xna/Framework/Net/QualityOfService.hpp"

// Task 15.3: cna_demo_qos_probe. Two real processes over real ENet, extending
// net_two_process_harness's host/client split. Prints a live-refreshing line every ~200ms.
//
// This demo honestly reflects the *actual, current* scope of Task 4.1/4.2's real QoS wiring
// rather than pretending both sides are symmetric:
//   - The HOST measures a real, live, continuously-updating NetworkGamer::RoundtripTime for each
//     of its directly-connected remote (client) gamers every ENet pump (Task 4.1) - this demo's
//     host side prints that live value.
//   - The CLIENT has only a one-shot QualityOfService sample from NetworkSession::Find()'s
//     discovery reply (Task 4.2) - a real measured wall-clock query/reply RTT, but not a live,
//     continuously-refreshing value. A client's own live view of the host's RTT is a documented,
//     known gap (see plans/plan_net.md Task 4.1's own scope decision: the star-topology client side has
//     no direct ENetPeer to read a live RTT from without further plumbing) - this demo's client
//     side prints the one-shot discovery QoS once, then honestly labels the live-refresh column
//     as "not tracked from the client side (Task 4.1 documented gap)" rather than showing a
//     fabricated moving number.

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Net;
using Microsoft::Xna::Framework::GamerServices::Gamer;
using Microsoft::Xna::Framework::GamerServices::SignedInGamer;

namespace
{
    // GamerServicesDispatcher::Initialize() never dereferences its serviceProvider argument
    // (mirrors tools/net/gamerservices_dispatcher_harness.cpp's own NullServiceProvider).
    class NullServiceProvider : public System::IServiceProvider
    {
    public:
        [[nodiscard]] void* GetService(const std::type_info& /*type*/) const override { return nullptr; }
    };

    void PrintQos(const char* label, const QualityOfService& qos)
    {
        std::printf("[QoS] %s: IsAvailable=%s AvgRTT=%.1fms MinRTT=%.1fms Up=%dB/s Down=%dB/s\n",
                    label,
                    qos.getIsAvailableProperty() ? "true" : "false",
                    qos.getAverageRoundtripTimeProperty().getTotalMillisecondsProperty(),
                    qos.getMinimumRoundtripTimeProperty().getTotalMillisecondsProperty(),
                    qos.getBytesPerSecondUpstreamProperty(),
                    qos.getBytesPerSecondDownstreamProperty());
    }
}

int main(int argc, char* argv[])
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    bool isHost = true;
    int smokeIterations = 25; // ~5s at the demo's 200ms refresh rate.

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--host") { isHost = true; }
        else if (arg == "--join") { isHost = false; }
        else if (arg == "--iterations" && i + 1 < argc) { smokeIterations = std::atoi(argv[++i]); }
    }

    NullServiceProvider services;
    Microsoft::Xna::Framework::GamerServices::GamerServicesDispatcher::Initialize(services);
    SignedInGamer* localGamer = (*Gamer::getSignedInGamersProperty())[0];

    NetworkSession* session = nullptr;

    if (isHost)
    {
        session = NetworkSession::Create(NetworkSessionType::SystemLink, 1, 8);
        std::printf("[QoSProbe] Hosting as \"%s\", waiting for a client to join...\n",
                    localGamer->getGamertagProperty().c_str());
    }
    else
    {
        std::printf("[QoSProbe] Searching for a session to join...\n");
        AvailableNetworkSessionCollection available =
            NetworkSession::Find(NetworkSessionType::SystemLink, 1, NetworkSessionProperties{});
        for (int attempt = 0; attempt < 100 && available.getCountProperty() == 0; ++attempt)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            available = NetworkSession::Find(NetworkSessionType::SystemLink, 1, NetworkSessionProperties{});
        }
        if (available.getCountProperty() == 0)
        {
            std::printf("[QoSProbe] No session found after searching - is a host running?\n");
            return 1;
        }
        const auto& constAvailable = available;
        PrintQos("one-shot discovery-time sample", constAvailable[0].getQualityOfServiceProperty());
        session = NetworkSession::Join(&constAvailable[0]);
        std::printf("[QoSProbe] Joined the host's session as \"%s\".\n",
                    localGamer->getGamertagProperty().c_str());
    }

    for (int iteration = 0; iteration < smokeIterations; ++iteration)
    {
        session->Update();

        if (isHost)
        {
            const auto& remotes = session->getRemoteGamersProperty();
            if (remotes.getCountProperty() == 0)
            {
                std::printf("[QoSProbe] (waiting for a remote gamer to measure RTT against)\n");
            }
            for (int i = 0; i < remotes.getCountProperty(); ++i)
            {
                NetworkGamer* gamer = remotes[i];
                std::printf("[QoSProbe] live RTT to \"%s\": %.1fms\n",
                            gamer->getGamertagProperty().c_str(),
                            gamer->getRoundtripTimeProperty().getTotalMillisecondsProperty());
            }
        }
        else
        {
            const auto& remotes = session->getRemoteGamersProperty();
            for (int i = 0; i < remotes.getCountProperty(); ++i)
            {
                NetworkGamer* gamer = remotes[i];
                std::printf("[QoSProbe] live RTT to \"%s\": not tracked from the client side "
                            "(Task 4.1 documented gap) - reported value stays %.1fms\n",
                            gamer->getGamertagProperty().c_str(),
                            gamer->getRoundtripTimeProperty().getTotalMillisecondsProperty());
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::printf("[QoSProbe] Probe complete after %d iterations.\n", smokeIterations);
    session->Dispose();
    return 0;
}
