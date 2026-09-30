// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ArgumentNullException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "System/ArgumentException.hpp"

using namespace Microsoft::Xna::Framework::Net;
using Microsoft::Xna::Framework::GamerServices::SignedInGamer;

namespace {
    SignedInGamer MakeSignedInGamer(const std::string& tag = "tag1") {
        return SignedInGamer::CreateInternal(tag);
    }

    struct LocalGamerFixture {
        SignedInGamer signedIn = MakeSignedInGamer();
        NetworkSession* session = NetworkSession::Create(
            NetworkSessionType::Local, std::vector<SignedInGamer*>{&signedIn}, 8, 0, NetworkSessionProperties{}
        );
        LocalNetworkGamer* gamer = session->getLocalGamersProperty()[0];

        ~LocalGamerFixture() { session->Dispose(); }
    };

    struct SystemLinkGamerFixture {
        SignedInGamer signedIn = MakeSignedInGamer();
        NetworkSession* session = NetworkSession::Create(
            NetworkSessionType::SystemLink, std::vector<SignedInGamer*>{&signedIn}, 8, 0, NetworkSessionProperties{}
        );
        LocalNetworkGamer* gamer = session->getLocalGamersProperty()[0];

        SystemLinkGamerFixture() { session->Update(); }
        ~SystemLinkGamerFixture() { session->Dispose(); }
    };

    std::vector<SharpRuntime::bytecs> ReceivePendingPacket(SystemLinkGamerFixture& fixture) {
        fixture.session->Update();
        EXPECT_TRUE(fixture.gamer->getIsDataAvailableProperty());
        std::vector<SharpRuntime::bytecs> buffer(16);
        NetworkGamer* sender = nullptr;
        const int length = fixture.gamer->ReceiveData(buffer, sender);
        buffer.resize(static_cast<std::size_t>(length));
        EXPECT_EQ(sender, fixture.gamer);
        return buffer;
    }
}

TEST(LocalNetworkGamerTest, IsLocalIsTrue) {
    LocalGamerFixture fixture;
    EXPECT_TRUE(fixture.gamer->getIsLocalProperty());
}

TEST(LocalNetworkGamerTest, SignedInGamerPropertyMatchesConstructorArgument) {
    LocalGamerFixture fixture;
    EXPECT_EQ(fixture.gamer->getSignedInGamerProperty(), &fixture.signedIn);
}

TEST(LocalNetworkGamerTest, NoDataAvailableByDefault) {
    LocalGamerFixture fixture;
    EXPECT_FALSE(fixture.gamer->getIsDataAvailableProperty());
}

TEST(LocalNetworkGamerTest, ReceiveDataReturnsZeroWhenQueueEmpty) {
    LocalGamerFixture fixture;
    std::vector<SharpRuntime::bytecs> buffer(4);
    NetworkGamer* sender = nullptr;
    EXPECT_EQ(fixture.gamer->ReceiveData(buffer, sender), 0);
    EXPECT_EQ(sender, nullptr);
}

// Task 2.8: len was computed as std::min(packet.size(), data.size()) - completely ignoring
// offset, unlike FNA's own Array.Copy(packet.Packet, 0, data, offset, len), which validates
// offset+len against data.Length and throws ArgumentException on overflow. std::copy has no such
// validation, so writing to data.begin() + offset for len elements silently wrote past the end of
// the caller's buffer (undefined behavior) whenever offset + len > data.size().
TEST(LocalNetworkGamerTest, ReceiveDataWithOffsetThrowsInsteadOfWritingPastBufferEnd) {
    LocalGamerFixture fixture;
    NetworkSession::NetworkEvent evt;
    evt.Packet = {1, 2, 3, 4, 5, 6, 7, 8}; // size 8
    fixture.gamer->EnqueuePacket(evt);
    ASSERT_TRUE(fixture.gamer->getIsDataAvailableProperty());

    std::vector<SharpRuntime::bytecs> buffer(10); // size 10; len = min(8,10) = 8; 5+8=13 > 10
    NetworkGamer* sender = nullptr;
    EXPECT_THROW(fixture.gamer->ReceiveData(buffer, 5, sender), System::ArgumentException);
}

TEST(LocalNetworkGamerTest, SendDataThenReceiveDataRoundtrip) {
    LocalGamerFixture fixture;
    std::vector<SharpRuntime::bytecs> payload{1, 2, 3, 4};
    fixture.gamer->SendData(payload, SendDataOptions::Reliable);

    // SendData enqueues a NetworkEvent on the session, not directly on packetQueue_. Update()'s
    // PacketSend handling (Task 5.5) is gated behind ENetBackend::RealNetworkingEnabled(), which
    // is false for this fixture's NetworkSessionType::Local — so it stays a no-op here (matching
    // FNA's own always-empty PacketSend branch) and IsDataAvailable legitimately stays false. See
    // CNA::Internal::Net::ENetBackendTest's AppData relay tests for the real (SystemLink) path.
    fixture.session->Update();
    EXPECT_FALSE(fixture.gamer->getIsDataAvailableProperty());
}

TEST(LocalNetworkGamerTest, SendDataWithOffsetAndCount) {
    LocalGamerFixture fixture;
    std::vector<SharpRuntime::bytecs> payload{1, 2, 3, 4, 5};
    fixture.gamer->SendData(payload, 1, 3, SendDataOptions::None);
}

// Task 2.9: constructing `std::vector<bytecs> mem(data.begin()+offset, data.begin()+offset+count)`
// with no check that offset+count <= data.size() was undefined behavior where FNA's own
// Array.Copy(data, offset, mem, 0, mem.Length) throws for the equivalent misuse.
TEST(LocalNetworkGamerTest, SendDataThrowsWhenOffsetPlusCountExceedsBuffer) {
    LocalGamerFixture fixture;
    std::vector<SharpRuntime::bytecs> payload{1, 2, 3, 4, 5}; // size 5
    EXPECT_THROW(fixture.gamer->SendData(payload, 3, 4, SendDataOptions::None), System::ArgumentException);
}

TEST(LocalNetworkGamerTest, SendDataToRecipientThrowsWhenOffsetPlusCountExceedsBuffer) {
    LocalGamerFixture fixture;
    std::vector<SharpRuntime::bytecs> payload{1, 2, 3, 4, 5}; // size 5
    EXPECT_THROW(
        fixture.gamer->SendData(payload, 3, 4, SendDataOptions::None, fixture.gamer),
        System::ArgumentException
    );
}

TEST(LocalNetworkGamerTest, SendDataToRecipient) {
    LocalGamerFixture fixture;
    std::vector<SharpRuntime::bytecs> payload{9, 9};
    fixture.gamer->SendData(payload, SendDataOptions::InOrder, fixture.gamer);
}

TEST(LocalNetworkGamerTest, SendDataWithOffsetAndCountToRecipient) {
    LocalGamerFixture fixture;
    std::vector<SharpRuntime::bytecs> payload{1, 2, 3, 4};
    fixture.gamer->SendData(payload, 0, 2, SendDataOptions::None, fixture.gamer);
}

TEST(LocalNetworkGamerTest, SendDataFromPacketWriter) {
    LocalGamerFixture fixture;
    PacketWriter writer;
    writer.Write(static_cast<int32_t>(42));
    fixture.gamer->SendData(writer, SendDataOptions::Reliable);
}

TEST(LocalNetworkGamerTest, SendDataFromPacketWriterToRecipient) {
    LocalGamerFixture fixture;
    PacketWriter writer;
    writer.Write(static_cast<int32_t>(42));
    fixture.gamer->SendData(writer, SendDataOptions::Reliable, fixture.gamer);
}

TEST(LocalNetworkGamerTest, ReusedPacketWriterBroadcastDoesNotSendStaleTrailingBytes) {
    SystemLinkGamerFixture fixture;
    PacketWriter writer;
    writer.Write(static_cast<int32_t>(0x04030201));
    fixture.gamer->SendData(writer, SendDataOptions::Reliable);
    EXPECT_EQ(ReceivePendingPacket(fixture),
              (std::vector<SharpRuntime::bytecs>{1, 2, 3, 4}));

    writer.Write(static_cast<SharpRuntime::bytecs>(9));
    fixture.gamer->SendData(writer, SendDataOptions::Reliable);
    EXPECT_EQ(ReceivePendingPacket(fixture),
              (std::vector<SharpRuntime::bytecs>{9}));
}

TEST(LocalNetworkGamerTest, ReusedPacketWriterRecipientSendDoesNotSendStaleTrailingBytes) {
    SystemLinkGamerFixture fixture;
    PacketWriter writer;
    writer.Write(static_cast<int32_t>(0x04030201));
    fixture.gamer->SendData(writer, SendDataOptions::Reliable, fixture.gamer);
    EXPECT_EQ(ReceivePendingPacket(fixture),
              (std::vector<SharpRuntime::bytecs>{1, 2, 3, 4}));

    writer.Write(static_cast<SharpRuntime::bytecs>(9));
    fixture.gamer->SendData(writer, SendDataOptions::Reliable, fixture.gamer);
    EXPECT_EQ(ReceivePendingPacket(fixture),
              (std::vector<SharpRuntime::bytecs>{9}));
}

// Reference ReceiveData: the offset is checked before anything else, a packet that does not fit is
// refused and stays queued, and both overloads return the packet size (the reader is sized to it).
TEST(LocalNetworkGamerTest, ReceiveDataReturnsThePacketSizeAndRefusesWithoutDequeuing) {
    LocalGamerFixture fixture;
    std::vector<SharpRuntime::bytecs> empty;
    NetworkGamer* sender = nullptr;
    EXPECT_THROW(fixture.gamer->ReceiveData(empty, sender), System::ArgumentOutOfRangeException);
    NetworkSession::NetworkEvent evt;
    evt.Packet = {1, 2, 3, 4, 5, 6};
    fixture.gamer->EnqueuePacket(evt);
    fixture.gamer->EnqueuePacket(evt);
    std::vector<SharpRuntime::bytecs> small(4);
    EXPECT_THROW(fixture.gamer->ReceiveData(small, sender), System::ArgumentException);
    EXPECT_THROW(fixture.gamer->ReceiveData(small, 4, sender), System::ArgumentOutOfRangeException);
    ASSERT_TRUE(fixture.gamer->getIsDataAvailableProperty());
    std::vector<SharpRuntime::bytecs> buffer(8, 0);
    EXPECT_EQ(6, fixture.gamer->ReceiveData(buffer, 2, sender));
    EXPECT_EQ((std::vector<SharpRuntime::bytecs>{0, 0, 1, 2, 3, 4, 5, 6}), buffer);
    PacketReader reader;
    EXPECT_EQ(6, fixture.gamer->ReceiveData(reader, sender));
    EXPECT_EQ(6, reader.getLengthProperty());
    EXPECT_EQ(1, reader.ReadByte());
    EXPECT_EQ(0, fixture.gamer->ReceiveData(reader, sender));
    EXPECT_EQ(0, reader.getLengthProperty());
}

TEST(LocalNetworkGamerTest, ReceiveDataIntoPacketReaderReturnsZero) {
    LocalGamerFixture fixture;
    PacketReader reader;
    NetworkGamer* sender = nullptr;
    // No packet queued, so IsDataAvailable is false and this returns 0 immediately.
    EXPECT_EQ(fixture.gamer->ReceiveData(reader, sender), 0);
}

// Reference EnableSendVoice argument checks, and SendPartyInvites refusing a gamer whose party has
// fewer than two members (a profile without a service party has none).
TEST(LocalNetworkGamerTest, EnableSendVoiceAndSendPartyInvitesFollowTheReferenceChecks) {
    LocalGamerFixture fixture;
    fixture.gamer->EnableSendVoice(fixture.gamer, true);
    EXPECT_THROW(fixture.gamer->EnableSendVoice(nullptr, true), System::ArgumentNullException);
    NetworkGamer stranger = NetworkGamer::CreateInternal(nullptr);
    EXPECT_THROW(fixture.gamer->EnableSendVoice(&stranger, true), System::ArgumentException);
    stranger.SetHasLeftSession(true);
    EXPECT_THROW(fixture.gamer->EnableSendVoice(&stranger, true), System::InvalidOperationException);
    EXPECT_THROW(fixture.gamer->SendPartyInvites(), System::InvalidOperationException);
}

// XNA's NetworkGamer.IsGuest is the gamer's guest state: a local gamer reports its profile's,
// whether it joined with the session or through AddLocalGamer.
TEST(LocalNetworkGamerTest, IsGuestFollowsTheSignedInProfile) {
    auto account = SignedInGamer::CreateInternal("Alice", true, false, Microsoft::Xna::Framework::PlayerIndex::One);
    auto guest = SignedInGamer::CreateInternal("Alice (1)", true, true, Microsoft::Xna::Framework::PlayerIndex::Two);
    auto second = SignedInGamer::CreateInternal("Alice (2)", true, true, Microsoft::Xna::Framework::PlayerIndex::Three);
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&account, &guest}, 8, 0, NetworkSessionProperties{});
    session->AddLocalGamer(&second);
    auto& locals = session->getLocalGamersProperty();
    ASSERT_EQ(locals.getCountProperty(), 3);
    EXPECT_FALSE(locals[0]->getIsGuestProperty());
    EXPECT_TRUE(locals[1]->getIsGuestProperty());
    EXPECT_TRUE(locals[2]->getIsGuestProperty());
    session->Dispose();
}

TEST(LocalNetworkGamerTest, ClearPacketQueueLeavesNoDataAvailable) {
    LocalGamerFixture fixture;
    fixture.gamer->ClearPacketQueue();
    EXPECT_FALSE(fixture.gamer->getIsDataAvailableProperty());
}


TEST(LocalNetworkGamerTest, PreservesSignedInProfileGamertagAndDisplayName) {
    auto profile = SignedInGamer::CreateInternal("RealHost");
    profile.setDisplayNameProperty("Host display name");
    auto gamer = LocalNetworkGamer::CreateInternal(&profile, nullptr);
    EXPECT_EQ(gamer.getSignedInGamerProperty(), &profile);
    EXPECT_EQ(gamer.getGamertagProperty(), "RealHost");
    EXPECT_EQ(gamer.getDisplayNameProperty(), "Host display name");
    EXPECT_EQ(gamer.ToString(), "Host display name");
}
