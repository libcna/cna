// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <functional>
#include <limits>
#include <memory>

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
        std::unique_ptr<NetworkSession> sessionOwner{session};
        LocalNetworkGamer* gamer = session->getLocalGamersProperty()[0];

        ~LocalGamerFixture() { session->Dispose(); }
    };

    struct SystemLinkGamerFixture {
        SignedInGamer signedIn = MakeSignedInGamer();
        NetworkSession* session = NetworkSession::Create(
            NetworkSessionType::SystemLink, std::vector<SignedInGamer*>{&signedIn}, 8, 0, NetworkSessionProperties{}
        );
        std::unique_ptr<NetworkSession> sessionOwner{session};
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
// with no check that offset+count <= data.size() was undefined behavior. XNA SendDataWorker
// IL_0036..004c refuses it as ArgumentOutOfRangeException("count").
TEST(LocalNetworkGamerTest, SendDataThrowsWhenOffsetPlusCountExceedsBuffer) {
    LocalGamerFixture fixture;
    std::vector<SharpRuntime::bytecs> payload{1, 2, 3, 4, 5}; // size 5
    EXPECT_THROW(fixture.gamer->SendData(payload, 3, 4, SendDataOptions::None), System::ArgumentOutOfRangeException);
}

TEST(LocalNetworkGamerTest, SendDataToRecipientThrowsWhenOffsetPlusCountExceedsBuffer) {
    LocalGamerFixture fixture;
    std::vector<SharpRuntime::bytecs> payload{1, 2, 3, 4, 5}; // size 5
    EXPECT_THROW(
        fixture.gamer->SendData(payload, 3, 4, SendDataOptions::None, fixture.gamer),
        System::ArgumentOutOfRangeException
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

// XNA SendData(PacketWriter, ...) IL_0023/IL_0031 clears the writer after sending it.
TEST(LocalNetworkGamerTest, SendDataFromPacketWriter) {
    LocalGamerFixture fixture;
    PacketWriter writer;
    writer.Write(static_cast<int32_t>(42));
    fixture.gamer->SendData(writer, SendDataOptions::Reliable);
    EXPECT_EQ(writer.getLengthProperty(), 0);
    EXPECT_EQ(writer.getPositionProperty(), 0);
}

TEST(LocalNetworkGamerTest, SendDataFromPacketWriterToRecipient) {
    LocalGamerFixture fixture;
    PacketWriter writer;
    writer.Write(static_cast<int32_t>(42));
    fixture.gamer->SendData(writer, SendDataOptions::Reliable, fixture.gamer);
    EXPECT_EQ(writer.getLengthProperty(), 0);
    EXPECT_EQ(writer.getPositionProperty(), 0);
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

// XNA's SendDataOptions is a flags enumeration: Chat combines with Reliable and/or InOrder, and
// every combination is sent and delivered.
TEST(LocalNetworkGamerTest, SendDataDeliversChatCombinedWithReliableAndInOrder) {
    SystemLinkGamerFixture fixture;
    const SendDataOptions combinations[] = {
        SendDataOptions::Chat,
        SendDataOptions::Chat | SendDataOptions::Reliable,
        SendDataOptions::Chat | SendDataOptions::InOrder,
        SendDataOptions::Chat | SendDataOptions::ReliableInOrder,
    };
    SharpRuntime::bytecs marker = 1;
    for (const SendDataOptions options : combinations) {
        fixture.gamer->SendData(std::vector<SharpRuntime::bytecs>{marker}, options, fixture.gamer);
        EXPECT_EQ(ReceivePendingPacket(fixture), (std::vector<SharpRuntime::bytecs>{marker}))
            << static_cast<int>(options);
        ++marker;
    }

    PacketWriter writer;
    writer.Write(static_cast<SharpRuntime::bytecs>(42));
    fixture.gamer->SendData(writer, SendDataOptions::Chat | SendDataOptions::ReliableInOrder);
    EXPECT_EQ(ReceivePendingPacket(fixture), (std::vector<SharpRuntime::bytecs>{42}));
}

// A bit outside Reliable, InOrder and Chat is refused by every overload before anything is queued,
// and a refused PacketWriter keeps its contents.
TEST(LocalNetworkGamerTest, SendDataRefusesOptionBitsSendDataOptionsDoesNotDefine) {
    SystemLinkGamerFixture fixture;
    const auto undefined = static_cast<SendDataOptions>(8) | SendDataOptions::Reliable;
    const std::vector<SharpRuntime::bytecs> payload{1, 2};
    EXPECT_THROW(fixture.gamer->SendData(payload, undefined), System::ArgumentOutOfRangeException);
    EXPECT_THROW(fixture.gamer->SendData(payload, 0, 1, undefined), System::ArgumentOutOfRangeException);
    EXPECT_THROW(fixture.gamer->SendData(payload, undefined, fixture.gamer), System::ArgumentOutOfRangeException);
    EXPECT_THROW(fixture.gamer->SendData(payload, 0, 1, undefined, fixture.gamer),
                 System::ArgumentOutOfRangeException);

    PacketWriter writer;
    writer.Write(static_cast<int32_t>(7));
    EXPECT_THROW(fixture.gamer->SendData(writer, undefined), System::ArgumentOutOfRangeException);
    EXPECT_THROW(fixture.gamer->SendData(writer, undefined, fixture.gamer), System::ArgumentOutOfRangeException);
    EXPECT_EQ(writer.getPositionProperty(), 4);

    fixture.session->Update();
    EXPECT_FALSE(fixture.gamer->getIsDataAvailableProperty());
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
    std::unique_ptr<NetworkSession> sessionOwner(session);
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

// XNA SendData(byte[], ...) hands SendDataWorker (data, offset, count), and SendDataNow
// IL_00da..00ea copies exactly count bytes from offset: every overload delivers data[offset,
// offset + count), with this gamer as the sender.
TEST(LocalNetworkGamerTest, SendDataOverloadsDeliverExactlyTheSelectedBytes) {
    SystemLinkGamerFixture fixture;
    const std::vector<SharpRuntime::bytecs> payload{10, 11, 12, 13, 14};
    fixture.gamer->SendData(payload, SendDataOptions::Reliable);
    EXPECT_EQ(ReceivePendingPacket(fixture), payload);
    fixture.gamer->SendData(payload, 1, 3, SendDataOptions::Reliable);
    EXPECT_EQ(ReceivePendingPacket(fixture), (std::vector<SharpRuntime::bytecs>{11, 12, 13}));
    fixture.gamer->SendData(payload, SendDataOptions::InOrder, fixture.gamer);
    EXPECT_EQ(ReceivePendingPacket(fixture), payload);
    fixture.gamer->SendData(payload, 4, 1, SendDataOptions::ReliableInOrder, fixture.gamer);
    EXPECT_EQ(ReceivePendingPacket(fixture), (std::vector<SharpRuntime::bytecs>{14}));
    fixture.session->Update();
    EXPECT_FALSE(fixture.gamer->getIsDataAvailableProperty());
}

// XNA SendDataWorker IL_0021..004c: offset must lie inside the array, so an empty array is
// refused ("offset"), and count must be positive and fit after it ("count"), both as
// ArgumentOutOfRangeException; nothing is sent.
TEST(LocalNetworkGamerTest, SendDataRefusesARangeOutsideTheArrayAndSendsNothing) {
    SystemLinkGamerFixture fixture;
    const std::vector<SharpRuntime::bytecs> payload{1, 2, 3, 4, 5};
    struct Case { int offset; int count; const char* parameter; };
    const Case cases[] = {
        {-1, 1, "offset"}, {5, 1, "offset"}, {6, 0, "offset"},
        {0, 0, "count"}, {0, -1, "count"}, {2, 4, "count"}, {4, 2, "count"},
        {1, std::numeric_limits<int>::max(), "count"},
    };
    const auto expectRefused = [](const std::function<void()>& send, const char* parameter) {
        try {
            send();
            ADD_FAILURE() << "the range was accepted";
        } catch (const System::ArgumentOutOfRangeException& e) {
            EXPECT_EQ(e.getParamNameProperty(), parameter);
        }
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(::testing::Message() << "offset=" << c.offset << " count=" << c.count);
        expectRefused([&] { fixture.gamer->SendData(payload, c.offset, c.count, SendDataOptions::Reliable); },
                      c.parameter);
        expectRefused([&] { fixture.gamer->SendData(payload, c.offset, c.count, SendDataOptions::Reliable,
                                                    fixture.gamer); },
                      c.parameter);
    }
    const std::vector<SharpRuntime::bytecs> empty;
    expectRefused([&] { fixture.gamer->SendData(empty, SendDataOptions::Reliable); }, "offset");
    expectRefused([&] { fixture.gamer->SendData(empty, SendDataOptions::Reliable, fixture.gamer); }, "offset");
    fixture.session->Update();
    EXPECT_FALSE(fixture.gamer->getIsDataAvailableProperty());
}

// XNA SendDataWorker IL_0000..0012 refuses a sender that has left the session before looking at
// its arguments, and IL_004d..0083 refuses a recipient that has left (InvalidOperationException)
// or that belongs to another session (ArgumentException "recipient"). A refused PacketWriter is
// not cleared (Clear follows the worker, IL_0031).
TEST(LocalNetworkGamerTest, SendDataRefusesAGamerThatLeftAndARecipientFromElsewhere) {
    SystemLinkGamerFixture fixture;
    const std::vector<SharpRuntime::bytecs> payload{1, 2};
    PacketWriter writer;
    writer.Write(static_cast<int32_t>(7));

    NetworkGamer stranger = NetworkGamer::CreateInternal(nullptr, "Stranger");
    const std::function<void(NetworkGamer*)> recipientSends[] = {
        [&](NetworkGamer* to) { fixture.gamer->SendData(payload, SendDataOptions::Reliable, to); },
        [&](NetworkGamer* to) { fixture.gamer->SendData(payload, 0, 1, SendDataOptions::Reliable, to); },
        [&](NetworkGamer* to) { fixture.gamer->SendData(writer, SendDataOptions::Reliable, to); },
    };
    for (const auto& send : recipientSends) {
        try {
            send(&stranger);
            ADD_FAILURE() << "a recipient from another session was accepted";
        } catch (const System::ArgumentOutOfRangeException&) {
            ADD_FAILURE() << "the recipient was refused as a range";
        } catch (const System::ArgumentException& e) {
            EXPECT_EQ(e.getParamNameProperty(), "recipient");
        }
    }
    NetworkGamer departed = NetworkGamer::CreateInternal(fixture.session, "Departed");
    departed.SetHasLeftSession(true);
    for (const auto& send : recipientSends)
        EXPECT_THROW(send(&departed), System::InvalidOperationException);
    EXPECT_EQ(writer.getLengthProperty(), 4);
    EXPECT_EQ(writer.getPositionProperty(), 4);

    fixture.gamer->SetHasLeftSession(true);
    EXPECT_THROW(fixture.gamer->SendData(payload, SendDataOptions::Reliable), System::InvalidOperationException);
    // The sender check precedes the range check.
    EXPECT_THROW(fixture.gamer->SendData(std::vector<SharpRuntime::bytecs>{}, SendDataOptions::Reliable),
                 System::InvalidOperationException);
    EXPECT_THROW(fixture.gamer->SendData(payload, -1, 0, SendDataOptions::Reliable), System::InvalidOperationException);
    EXPECT_THROW(fixture.gamer->SendData(writer, SendDataOptions::Reliable), System::InvalidOperationException);
    EXPECT_THROW(fixture.gamer->SendData(writer, SendDataOptions::Reliable, fixture.gamer),
                 System::InvalidOperationException);
    EXPECT_EQ(writer.getLengthProperty(), 4);
    fixture.gamer->SetHasLeftSession(false);

    fixture.session->Update();
    EXPECT_FALSE(fixture.gamer->getIsDataAvailableProperty());
}

// XNA SendData(PacketWriter, ...) IL_000e..001e sends (ByteArray, 0, Length) -- the whole
// written contents, whatever Position is -- and then calls PacketWriter.Clear, which sets the
// length to 0 and the position to 0 (IL_000c..0017).
TEST(LocalNetworkGamerTest, SendDataFromAPacketWriterSendsItsLengthAndEmptiesIt) {
    SystemLinkGamerFixture fixture;
    PacketWriter writer;
    writer.Write(static_cast<int32_t>(0x04030201));
    writer.Write(static_cast<int32_t>(0x08070605));
    writer.setPositionProperty(4);
    fixture.gamer->SendData(writer, SendDataOptions::Reliable);
    EXPECT_EQ(writer.getLengthProperty(), 0);
    EXPECT_EQ(writer.getPositionProperty(), 0);
    EXPECT_EQ(ReceivePendingPacket(fixture), (std::vector<SharpRuntime::bytecs>{1, 2, 3, 4, 5, 6, 7, 8}));

    writer.Write(static_cast<SharpRuntime::bytecs>(9));
    writer.Write(static_cast<SharpRuntime::bytecs>(10));
    writer.setPositionProperty(0);
    fixture.gamer->SendData(writer, SendDataOptions::Reliable, fixture.gamer);
    EXPECT_EQ(writer.getLengthProperty(), 0);
    EXPECT_EQ(writer.getPositionProperty(), 0);
    EXPECT_EQ(ReceivePendingPacket(fixture), (std::vector<SharpRuntime::bytecs>{9, 10}));
}

// XNA ReceiveData IL_0098..00dc dequeues in arrival order (Queue`1) and reports the packet's
// recorded Sender -- still that gamer after it has left the session -- and IsDataAvailable is
// whether the queue holds a packet (IL_0011..001d). An empty queue reports no sender.
TEST(LocalNetworkGamerTest, ReceiveDataReturnsPacketsInOrderWithTheirSender) {
    LocalGamerFixture fixture;
    NetworkGamer remote = NetworkGamer::CreateInternal(fixture.session, "Remote");
    fixture.session->AddRemoteGamer(&remote);
    NetworkSession::NetworkEvent first;
    first.Packet = {1};
    first.Gamer = &remote;
    NetworkSession::NetworkEvent second;
    second.Packet = {2, 3};
    second.Gamer = fixture.gamer;
    fixture.gamer->EnqueuePacket(first);
    fixture.gamer->EnqueuePacket(second);
    fixture.session->RemoveGamer(&remote, NetworkSessionEndReason::Disconnected);

    std::vector<SharpRuntime::bytecs> buffer(4, 0);
    NetworkGamer* sender = nullptr;
    ASSERT_TRUE(fixture.gamer->getIsDataAvailableProperty());
    EXPECT_EQ(fixture.gamer->ReceiveData(buffer, sender), 1);
    EXPECT_EQ(sender, &remote);
    EXPECT_EQ(buffer, (std::vector<SharpRuntime::bytecs>{1, 0, 0, 0}));
    ASSERT_TRUE(fixture.gamer->getIsDataAvailableProperty());
    PacketReader reader;
    EXPECT_EQ(fixture.gamer->ReceiveData(reader, sender), 2);
    EXPECT_EQ(sender, fixture.gamer);
    EXPECT_EQ(reader.getPositionProperty(), 0);
    EXPECT_EQ(reader.ReadByte(), 2);
    EXPECT_EQ(reader.ReadByte(), 3);
    EXPECT_FALSE(fixture.gamer->getIsDataAvailableProperty());
    sender = fixture.gamer;
    EXPECT_EQ(fixture.gamer->ReceiveData(buffer, sender), 0);
    EXPECT_EQ(sender, nullptr);
    EXPECT_EQ(buffer, (std::vector<SharpRuntime::bytecs>{1, 0, 0, 0}));
}
