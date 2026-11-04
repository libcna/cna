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

// XNA ReceiveData(PacketReader, out NetworkGamer) sizes the reader to the next packet, or to 0 when
// none is queued (IL_001f..0047), and then reads through ReceiveData(byte[], 0, out) with the
// reader's MemoryStream.GetBuffer() (IL_004c..0055). That overload refuses an offset outside the
// array before anything else (IL_000e..0020). With an empty queue, a reader whose stream never
// allocated -- new PacketReader() is MemoryStream(0) (.ctor IL_0000..0007, IL_0000..000c) -- is
// therefore refused for "offset". A reader created with a capacity, or one that has held a packet
// (Resize's SetLength keeps the buffer, IL_0000..001c), answers 0 with no sender (TQ-043).
TEST(LocalNetworkGamerTest, ReceiveDataIntoANeverAllocatedPacketReaderOnAnEmptyQueueIsRefusedForOffset) {
    LocalGamerFixture fixture;
    NetworkGamer* sender = nullptr;
    PacketReader fresh;
    PacketReader zero(0);
    for (PacketReader* reader : {&fresh, &zero}) {
        try {
            fixture.gamer->ReceiveData(*reader, sender);
            ADD_FAILURE() << "a never-allocated reader answered an empty queue";
        } catch (const System::ArgumentOutOfRangeException& e) {
            EXPECT_EQ(e.getParamNameProperty(), "offset");
        }
        EXPECT_EQ(reader->getLengthProperty(), 0);
        EXPECT_EQ(reader->getPositionProperty(), 0);
    }

    PacketReader sized(16);
    sender = fixture.gamer;
    EXPECT_EQ(fixture.gamer->ReceiveData(sized, sender), 0);
    EXPECT_EQ(sender, nullptr);
    EXPECT_EQ(sized.getLengthProperty(), 0);

    // A packet allocates the fresh reader, which then answers an empty queue with 0.
    NetworkSession::NetworkEvent evt;
    evt.Packet = {7, 8};
    evt.Gamer = fixture.gamer;
    fixture.gamer->EnqueuePacket(evt);
    EXPECT_EQ(fixture.gamer->ReceiveData(fresh, sender), 2);
    EXPECT_EQ(sender, fixture.gamer);
    EXPECT_EQ(fresh.ReadByte(), 7);
    EXPECT_EQ(fresh.ReadByte(), 8);
    EXPECT_EQ(fixture.gamer->ReceiveData(fresh, sender), 0);
    EXPECT_EQ(sender, nullptr);
    EXPECT_EQ(fresh.getLengthProperty(), 0);
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
    session->Update(); // an added gamer joins at the next Update (TQ-039)
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

// XNA's recipient overloads check the recipient for null before SendDataWorker runs
// (SendData(byte[], SendDataOptions, NetworkGamer) IL_000e..001b, the offset/count form
// IL_000e..001c, SendData(PacketWriter, SendDataOptions, NetworkGamer) IL_000e..001b): a null
// recipient is ArgumentNullException("recipient") even with an invalid range, an empty array or
// writer, or a sender that has left. Nothing is sent and the refused writer keeps its bytes; the
// overloads without a recipient are the ones that send to every gamer (TQ-039).
TEST(LocalNetworkGamerTest, SendDataRefusesANullRecipientBeforeAnyOtherCheck) {
    SystemLinkGamerFixture fixture;
    const std::vector<SharpRuntime::bytecs> payload{1, 2, 3};
    PacketWriter writer;
    writer.Write(static_cast<int32_t>(7));
    PacketWriter empty;
    NetworkGamer* none = nullptr;
    const auto expectNullRecipient = [](const std::function<void()>& send) {
        try {
            send();
            ADD_FAILURE() << "a null recipient was accepted";
        } catch (const System::ArgumentNullException& e) {
            EXPECT_EQ(e.getParamNameProperty(), "recipient");
        } catch (const std::exception& e) {
            ADD_FAILURE() << "refused by a later check: " << e.what();
        }
    };
    const std::function<void()> sends[] = {
        [&] { fixture.gamer->SendData(payload, SendDataOptions::Reliable, none); },
        [&] { fixture.gamer->SendData(payload, 1, 2, SendDataOptions::Reliable, none); },
        [&] { fixture.gamer->SendData(writer, SendDataOptions::Reliable, none); },
        [&] { fixture.gamer->SendData(payload, -1, 0, SendDataOptions::Reliable, none); },
        [&] { fixture.gamer->SendData(std::vector<SharpRuntime::bytecs>{}, SendDataOptions::Reliable, none); },
        [&] { fixture.gamer->SendData(empty, SendDataOptions::Reliable, none); },
    };
    for (const auto& send : sends) expectNullRecipient(send);
    fixture.gamer->SetHasLeftSession(true);
    for (const auto& send : sends) expectNullRecipient(send);
    fixture.gamer->SetHasLeftSession(false);
    EXPECT_EQ(writer.getLengthProperty(), 4);
    EXPECT_EQ(writer.getPositionProperty(), 4);

    fixture.session->Update();
    EXPECT_FALSE(fixture.gamer->getIsDataAvailableProperty());
    fixture.gamer->SendData(writer, SendDataOptions::Reliable);
    EXPECT_EQ(ReceivePendingPacket(fixture), (std::vector<SharpRuntime::bytecs>{7, 0, 0, 0}));
}

// XNA SendData(PacketWriter, ...) hands SendDataWorker (ByteArray, 0, Length) (IL_000e..001e), so
// an empty writer is refused as an empty range is: after the sender check (IL_0000..0012), before
// the recipient checks (IL_004d..0083). ByteArray is MemoryStream.GetBuffer()
// (PacketWriter.get_ByteArray IL_0000..0010). A new PacketWriter() wraps MemoryStream(0) (.ctor
// IL_0000..000c), whose empty buffer fails the offset check (IL_0021..0035, "offset"); a writer
// that has held bytes keeps its buffer through SetLength(0) (PacketWriter.Clear IL_000c..0017),
// so the count check refuses it (IL_0036..004c, "count"). Nothing is sent and a refused writer is
// left as it was (TQ-039).
TEST(LocalNetworkGamerTest, SendDataRefusesAnEmptyPacketWriterAndKeepsIt) {
    SystemLinkGamerFixture fixture;
    NetworkGamer stranger = NetworkGamer::CreateInternal(nullptr, "Stranger");
    const auto expectRefused = [](const std::function<void()>& send, const char* parameter) {
        try {
            send();
            ADD_FAILURE() << "an empty writer was sent";
        } catch (const System::ArgumentOutOfRangeException& e) {
            EXPECT_EQ(e.getParamNameProperty(), parameter);
        }
    };

    PacketWriter fresh;
    expectRefused([&] { fixture.gamer->SendData(fresh, SendDataOptions::Reliable); }, "offset");
    expectRefused([&] { fixture.gamer->SendData(fresh, SendDataOptions::Reliable, fixture.gamer); }, "offset");
    expectRefused([&] { fixture.gamer->SendData(fresh, SendDataOptions::Reliable, &stranger); }, "offset");

    PacketWriter used;
    used.Write(static_cast<SharpRuntime::bytecs>(5));
    fixture.gamer->SendData(used, SendDataOptions::Reliable);
    EXPECT_EQ(ReceivePendingPacket(fixture), (std::vector<SharpRuntime::bytecs>{5}));
    ASSERT_EQ(used.getLengthProperty(), 0);
    expectRefused([&] { fixture.gamer->SendData(used, SendDataOptions::Reliable); }, "count");
    expectRefused([&] { fixture.gamer->SendData(used, SendDataOptions::Reliable, fixture.gamer); }, "count");
    expectRefused([&] { fixture.gamer->SendData(used, SendDataOptions::Reliable, &stranger); }, "count");

    PacketWriter truncated;
    truncated.Write(static_cast<int32_t>(9));
    truncated.getBaseStreamProperty()->SetLength(0);
    expectRefused([&] { fixture.gamer->SendData(truncated, SendDataOptions::Reliable); }, "count");

    fixture.gamer->SetHasLeftSession(true);
    EXPECT_THROW(fixture.gamer->SendData(fresh, SendDataOptions::Reliable), System::InvalidOperationException);
    EXPECT_THROW(fixture.gamer->SendData(used, SendDataOptions::Reliable, fixture.gamer),
                 System::InvalidOperationException);
    fixture.gamer->SetHasLeftSession(false);

    for (const PacketWriter* writer : {&fresh, &used, &truncated}) {
        EXPECT_EQ(writer->getLengthProperty(), 0);
        EXPECT_EQ(writer->getPositionProperty(), 0);
    }
    fixture.session->Update();
    EXPECT_FALSE(fixture.gamer->getIsDataAvailableProperty());

    used.Write(static_cast<SharpRuntime::bytecs>(6));
    fixture.gamer->SendData(used, SendDataOptions::Reliable, fixture.gamer);
    EXPECT_EQ(ReceivePendingPacket(fixture), (std::vector<SharpRuntime::bytecs>{6}));
}

// XNA PacketWriter(int capacity) wraps MemoryStream(capacity) (.ctor IL_0000..000c), and
// PacketWriter() passes 0 (IL_0000..0007). MemoryStream(n) allocates n bytes at once, so GetBuffer(),
// the ByteArray SendData(PacketWriter, ...) hands SendDataWorker (IL_000e..001e), is n long while
// Length is 0: a never-written PacketWriter(n > 0) passes the offset check (0 < n, IL_0021..0035)
// and is refused by the count check (count 0, IL_0036..004c), where PacketWriter() and
// PacketWriter(0) are refused for "offset". Nothing is sent; once written, the sized writer sends
// exactly its bytes and is refused for "count" again when empty (TQ-043).
TEST(LocalNetworkGamerTest, ANeverWrittenPacketWriterWithACapacityIsRefusedForCount) {
    SystemLinkGamerFixture fixture;
    const auto expectRefused = [](const std::function<void()>& send, const char* parameter) {
        try {
            send();
            ADD_FAILURE() << "an empty writer was sent";
        } catch (const System::ArgumentOutOfRangeException& e) {
            EXPECT_EQ(e.getParamNameProperty(), parameter);
        }
    };
    PacketWriter unsized;
    PacketWriter zero(0);
    PacketWriter one(1);
    PacketWriter sized(16);
    expectRefused([&] { fixture.gamer->SendData(unsized, SendDataOptions::Reliable); }, "offset");
    expectRefused([&] { fixture.gamer->SendData(zero, SendDataOptions::Reliable, fixture.gamer); }, "offset");
    expectRefused([&] { fixture.gamer->SendData(one, SendDataOptions::Reliable); }, "count");
    expectRefused([&] { fixture.gamer->SendData(sized, SendDataOptions::Reliable); }, "count");
    expectRefused([&] { fixture.gamer->SendData(sized, SendDataOptions::Reliable, fixture.gamer); }, "count");
    for (const PacketWriter* writer : {&unsized, &zero, &one, &sized}) {
        EXPECT_EQ(writer->getLengthProperty(), 0);
        EXPECT_EQ(writer->getPositionProperty(), 0);
    }
    fixture.session->Update();
    EXPECT_FALSE(fixture.gamer->getIsDataAvailableProperty());

    // Int32 0x0A0B0C0D, little-endian.
    sized.Write(static_cast<int32_t>(0x0A0B0C0D));
    EXPECT_EQ(sized.getLengthProperty(), 4);
    fixture.gamer->SendData(sized, SendDataOptions::Reliable, fixture.gamer);
    EXPECT_EQ(ReceivePendingPacket(fixture), (std::vector<SharpRuntime::bytecs>{0x0D, 0x0C, 0x0B, 0x0A}));
    EXPECT_EQ(sized.getLengthProperty(), 0);
    expectRefused([&] { fixture.gamer->SendData(sized, SendDataOptions::Reliable); }, "count");
}
