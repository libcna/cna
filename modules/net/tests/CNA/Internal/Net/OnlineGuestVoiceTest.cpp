// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Internal/Net/VoiceChat.hpp"
#include "CNA/Internal/GamerServices/VoiceMutes.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkMachine.hpp"
namespace {
using namespace CNA::Internal::Net;
using namespace Microsoft::Xna::Framework::Net;
using Microsoft::Xna::Framework::GamerServices::SignedInGamer;
namespace Policy=CNA::Internal::GamerServices;
struct Capture final:IVoiceCapture {
    std::shared_ptr<bool> open;
    explicit Capture(std::shared_ptr<bool> value):open(std::move(value)){}
    bool present()override{return true;}
    void setOpen(bool value)override{*open=value;}
    void read(std::vector<std::int16_t>&)override{}
};
struct Playback final:IVoicePlayback {
    void play(std::uint8_t,std::span<const std::int16_t>)override{}
    void release(std::uint8_t)override{}
};
class OnlineGuestVoiceTest:public ::testing::Test {
protected:
    void SetUp()override {
        Policy::resetVoiceMutesForTesting();
        setVoiceDevicesForTesting([state=open]{return std::make_unique<Capture>(state);},[]{return std::make_unique<Playback>();});
    }
    void TearDown()override{setVoiceDevicesForTesting({},{});Policy::resetVoiceMutesForTesting();}
    std::shared_ptr<bool> open=std::make_shared<bool>(false);
};
}
TEST_F(OnlineGuestVoiceTest, BackingParentBlockAppliesToGuestAndCaptureAudience) {
    // Oracle: GAP-16 owner policy. A guest cannot become a new unblocked identity.
    auto profile=SignedInGamer::CreateInternal("Alice",true);
    auto local=LocalNetworkGamer::CreateInternal(&profile,nullptr);
    auto parent=NetworkGamer::CreateInternal(nullptr,"Bob");parent.SetId(3);
    auto guest=NetworkGamer::CreateInternal(nullptr,"Bob (1)");guest.SetId(4);guest.SetIsGuest(true);
    auto machine=std::make_shared<NetworkMachine>(NetworkMachine::CreateInternal());parent.SetSharedMachine(machine);guest.SetSharedMachine(machine);
    bool guestMuted=false;
    VoiceChat::Hooks hooks;hooks.gamers=[&]{return std::vector<NetworkGamer*>{&local,&parent,&guest};};
    hooks.send=[](auto*,auto*,const auto&){};
    hooks.apply=[&](NetworkGamer& gamer,bool,bool,bool muted){if(&gamer==&guest)guestMuted=muted;};
    hooks.policyGamer=[&](NetworkGamer* gamer){return gamer==&guest?&parent:gamer;};
    VoiceChat voice(std::move(hooks));const auto time=std::chrono::steady_clock::time_point{};
    Policy::setPlayerBlocked("Alice","Bob",true);voice.update(time);
    EXPECT_TRUE(guestMuted);EXPECT_FALSE(*open);
    Policy::setPlayerBlocked("Alice","Bob",false);voice.update(time+std::chrono::seconds(6));
    EXPECT_FALSE(guestMuted);EXPECT_TRUE(*open);
}
TEST_F(OnlineGuestVoiceTest, MissingAuthenticatedParentFailsClosed) {
    // Oracle: GAP-16 parent authority; there is no independent fallback guest voice policy.
    auto profile=SignedInGamer::CreateInternal("Alice",true);auto local=LocalNetworkGamer::CreateInternal(&profile,nullptr);
    auto guest=NetworkGamer::CreateInternal(nullptr,"Bob (1)");guest.SetIsGuest(true);
    guest.SetSharedMachine(std::make_shared<NetworkMachine>(NetworkMachine::CreateInternal()));
    bool muted=false;VoiceChat::Hooks hooks;
    hooks.gamers=[&]{return std::vector<NetworkGamer*>{&local,&guest};};hooks.send=[](auto*,auto*,const auto&){};
    hooks.apply=[&](auto& g,bool,bool,bool value){if(&g==&guest)muted=value;};
    hooks.policyGamer=[&](NetworkGamer* gamer)->NetworkGamer*{return gamer==&guest?nullptr:gamer;};
    VoiceChat voice(std::move(hooks));voice.update(std::chrono::steady_clock::time_point{});
    EXPECT_TRUE(muted);EXPECT_FALSE(*open);
}
