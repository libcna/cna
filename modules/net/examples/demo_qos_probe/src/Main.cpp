#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "common/SignInEXT.hpp"
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
// What it shows:
//   - The joiner's one-shot QualityOfService from NetworkSession::Find(): the discovery round trip
//     and a downstream bandwidth estimate timed from the host's probe train (GSP-L6). Upstream is
//     0: a host answers discovery at frame boundaries and cannot time what arrives.
//   - Every machine's live NetworkGamer::RoundtripTime: the host reads ENet's per-peer round trip,
//     a client its round trip to the host (plus the host's to a gamer on another client). The
//     value includes how often each side pumps ENet: this demo pumps every 200ms.

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
    // A console program cannot show the Guide: it runs as a gamer signed in automatically.
    SignedInGamer* localGamer = CNAExamplesEXT::WaitForSignedInGamerEXT();
    if (localGamer == nullptr)
    {
        std::printf("[QoSProbe] %s\n", CNAExamplesEXT::kNoSignedInGamerEXT);
        return 1;
    }

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
                std::printf("[QoSProbe] live RTT to \"%s\": %.1fms\n",
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
