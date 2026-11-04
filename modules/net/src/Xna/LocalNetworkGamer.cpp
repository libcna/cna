// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentException.hpp"
#include "System/InvalidOperationException.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "CNA/Internal/Net/NetPacketCodec.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "System/IO/MemoryStream.hpp"
#include <algorithm>

namespace Microsoft::Xna::Framework::Net
{
    GetTypeNameCPP(LocalNetworkGamer, "Microsoft.Xna.Framework.Net.LocalNetworkGamer")

    namespace
    {
        // XNA SendData(PacketWriter, ...) IL_000e..001e sends the writer's first Length bytes
        // (ByteArray, 0, Length) whatever its Position, and only after a successful send calls
        // PacketWriter.Clear (IL_0023/0031), which empties it.
        std::vector<SharpRuntime::bytecs> PacketBytes(PacketWriter& data)
        {
            auto* stream = dynamic_cast<System::IO::MemoryStream*>(data.getBaseStreamProperty());
            return stream != nullptr ? stream->ToArray() : std::vector<SharpRuntime::bytecs>{};
        }

        void ClearSentPacket(PacketWriter& data)
        {
            // XNA PacketWriter.Clear IL_000c..0017: SetLength(0), then Position 0.
            data.getBaseStreamProperty()->SetLength(0);
            data.setPositionProperty(0);
        }

        // XNA LocalNetworkGamer.SendDataWorker IL_0000..0083, in its order: a sender that left is
        // refused (IL_0000..0012), offset must lie inside the array, so an empty array is refused
        // (IL_0021..0035), count must be positive and fit (IL_0036..004c), and a recipient must
        // still be in, and belong to, the sender's session (IL_004d..0083).
        void ValidateSender(const NetworkGamer& sender)
        {
            if (sender.getHasLeftSessionProperty())
                throw System::InvalidOperationException("This NetworkGamer is no longer valid. The gamer may have left the session.");
        }

        void ValidateRecipient(const NetworkGamer& sender, const NetworkGamer* recipient)
        {
            if (recipient == nullptr) return;
            if (recipient->getHasLeftSessionProperty())
                throw System::InvalidOperationException("This NetworkGamer is no longer valid. The gamer may have left the session.");
            if (recipient->getSessionProperty() != sender.getSessionProperty())
                throw System::ArgumentException("The gamer is not in this session.", "recipient");
        }

        void ValidateSend(const NetworkGamer& sender, int length, int offset, int count, const NetworkGamer* recipient)
        {
            ValidateSender(sender);
            if (offset < 0 || offset >= length) throw System::ArgumentOutOfRangeException("offset");
            if (count <= 0 || count > length - offset) throw System::ArgumentOutOfRangeException("count");
            ValidateRecipient(sender, recipient);
        }

        // XNA's recipient overloads refuse a null recipient before SendDataWorker runs, so before
        // every other check: SendData(byte[], SendDataOptions, NetworkGamer) IL_000e..001b, the
        // offset/count form IL_000e..001c and SendData(PacketWriter, SendDataOptions, NetworkGamer)
        // IL_000e..001b. Only the overloads without a recipient send to every gamer.
        void RequireRecipient(const NetworkGamer* recipient)
        {
            if (recipient == nullptr) throw System::ArgumentNullException("recipient");
        }

        // XNA SendData(PacketWriter, ...) hands SendDataWorker (ByteArray, 0, Length), so an empty
        // writer is refused like an empty range, after the sender check and before the recipient
        // checks. ByteArray is MemoryStream.GetBuffer() (PacketWriter.get_ByteArray IL_0000..0010),
        // whose length is the stream's capacity: a writer whose buffer was never allocated
        // (PacketWriter() or PacketWriter(0)) fails the offset check (IL_0021..0035), and one
        // created with a positive capacity, or that has held bytes, fails the count check
        // (IL_0036..004c), because MemoryStream(int) allocates and MemoryStream.SetLength keeps its
        // buffer. CNA's MemoryStream keeps its bytes in a std::vector, whose capacity survives a
        // shrink the same way (PacketWriterStream allocates it so).
        void ValidateWriterSend(const NetworkGamer& sender, PacketWriter& data, const NetworkGamer* recipient)
        {
            ValidateSender(sender);
            if (data.getLengthProperty() <= 0)
            {
                const auto* stream = dynamic_cast<System::IO::MemoryStream*>(data.getBaseStreamProperty());
                const bool allocated = stream != nullptr && stream->GetBuffer().capacity() != 0;
                throw System::ArgumentOutOfRangeException(allocated ? "count" : "offset");
            }
            ValidateRecipient(sender, recipient);
        }

        // XNA's SendDataWorker hands any value to its native packet buffer unchecked. CNA's
        // transport carries only the defined flags, so a bit beyond them is refused rather than
        // silently neither honoured nor sent (CHECKLIST.md, accepted deviations).
        void ValidateSendDataOptions(SendDataOptions options)
        {
            if (!CNA::Internal::Net::NetPacketCodec::HasOnlyDefinedSendDataOptions(options))
            {
                throw System::ArgumentOutOfRangeException("options");
            }
        }
    }

    LocalNetworkGamer::LocalNetworkGamer(GamerServices::SignedInGamer* gamer, NetworkSession* session)
        : NetworkGamer(session, gamer != nullptr ? gamer->getGamertagProperty() : std::string{})
        , signedInGamer_(gamer)
    {
        if (gamer != nullptr)
        {
            serviceUserId_ = gamer->serviceUserId_;
            displayName_ = gamer->getDisplayNameProperty();
        }
    }

    LocalNetworkGamer LocalNetworkGamer::CreateInternal(GamerServices::SignedInGamer* gamer, NetworkSession* session)
    {
        return LocalNetworkGamer(gamer, session);
    }

    bool LocalNetworkGamer::getIsDataAvailableProperty() const { return !packetQueue_.empty(); }
    GamerServices::SignedInGamer* LocalNetworkGamer::getSignedInGamerProperty() const { return signedInGamer_; }
    bool LocalNetworkGamer::getIsLocalProperty() const { return true; }

    void LocalNetworkGamer::EnableSendVoice(NetworkGamer* remoteGamer, bool enable)
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        if (getHasLeftSessionProperty()) throw System::InvalidOperationException("The gamer has left the session.");
        if (remoteGamer == nullptr) throw System::ArgumentNullException("remoteGamer");
        if (remoteGamer->getHasLeftSessionProperty()) throw System::InvalidOperationException("The remote gamer has left the session.");
        if (remoteGamer->getSessionProperty() != getSessionProperty())
            throw System::ArgumentException("The gamer is not in this session.", "remoteGamer");
        getSessionProperty()->EnableSendVoiceInternal(this, remoteGamer, enable);
    }

    void LocalNetworkGamer::SendPartyInvites()
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        // XNA IL: a gamer who left, or who is alone in (or without) a party, is refused.
        if (getHasLeftSessionProperty()) throw System::InvalidOperationException("The gamer has left the session.");
        if (signedInGamer_ == nullptr || signedInGamer_->getPartySizeProperty() < 2)
            throw System::InvalidOperationException("There is nobody else in the party to invite.");
        // The rest of the party, invited to this online session as the Guide's invitations are.
        const auto& user = CNA::Internal::GamerServices::GamerAccess::userId(*signedInGamer_);
        const auto& active = CNA::Internal::GamerServices::activeOnlineSession();
        if (user.empty() || !active || std::find(active->users.begin(), active->users.end(), user) == active->users.end())
            throw System::InvalidOperationException("Party invitations need an online network session.");
        std::vector<std::string> recipients;
        if (const auto party = CNA::Internal::GamerServices::knownParty(user))
            for (const auto& member : party->members)
                if (member.userId != user && std::find(active->users.begin(), active->users.end(), member.userId) == active->users.end())
                    recipients.push_back(member.gamertag);
        if (!recipients.empty())
            CNA::Internal::GamerServices::sendInvitations(user, recipients, [](int) {});
    }

    int LocalNetworkGamer::ReceiveData(std::vector<SharpRuntime::bytecs>& data, NetworkGamer*& sender)
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        return ReceiveData(data, 0, sender);
    }

    int LocalNetworkGamer::ReceiveData(std::vector<SharpRuntime::bytecs>& data, int offset, NetworkGamer*& sender)
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        // Reference ReceiveData(byte[], int, out NetworkGamer): the offset is checked first, and a
        // packet that does not fit is refused without being taken off the queue.
        if (offset < 0 || offset >= static_cast<int>(data.size()))
        {
            throw System::ArgumentOutOfRangeException("offset");
        }
        sender = nullptr;
        if (!getIsDataAvailableProperty())
        {
            return 0;
        }
        const int size = static_cast<int>(packetQueue_.front().Packet.size());
        if (offset + size > static_cast<int>(data.size()))
        {
            throw System::ArgumentException("The array is too small for the packet.", "data");
        }
        NetworkSession::NetworkEvent packet = std::move(packetQueue_.front());
        packetQueue_.pop();
        std::copy(packet.Packet.begin(), packet.Packet.end(), data.begin() + offset);
        sender = SenderOf(packet);
        return size;
    }

    int LocalNetworkGamer::ReceiveData(PacketReader& data, NetworkGamer*& sender)
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        // XNA ReceiveData(PacketReader, out NetworkGamer) resizes the reader to the next packet, or
        // to 0 when none is queued (IL_001f..0047), and then reads through ReceiveData(byte[], 0,
        // out) with the reader's MemoryStream.GetBuffer() (IL_004c..0055), which refuses an offset
        // outside the array before it touches the sender (IL_000e..0020). With an empty queue, a
        // reader whose buffer was never allocated (PacketReader() is MemoryStream(0)) is refused
        // for "offset"; one created with a capacity, or that has held a packet, answers 0.
        if (!getIsDataAvailableProperty())
        {
            data.ResizeInternal(0);
            const auto* stream = dynamic_cast<System::IO::MemoryStream*>(data.getBaseStreamProperty());
            if (stream == nullptr || stream->GetBuffer().capacity() == 0)
                throw System::ArgumentOutOfRangeException("offset");
            sender = nullptr;
            return 0;
        }
        sender = nullptr;
        NetworkSession::NetworkEvent packet = std::move(packetQueue_.front());
        packetQueue_.pop();
        const int size = static_cast<int>(packet.Packet.size());
        data.ResizeInternal(size);
        data.getBaseStreamProperty()->Write(packet.Packet.data(), 0, size);
        data.setPositionProperty(0);
        sender = SenderOf(packet);
        return size;
    }

    NetworkGamer* LocalNetworkGamer::SenderOf(const NetworkSession::NetworkEvent& packet) const
    {
        // Pointer identity against the session's gamers; a sender that has left the session is
        // still the sender (reference incomingPacket.Sender), found among the previous gamers.
        for (NetworkGamer* gamer : getSessionProperty()->getAllGamersProperty())
        {
            if (gamer == packet.Gamer) return gamer;
        }
        for (NetworkGamer* gamer : getSessionProperty()->getPreviousGamersProperty())
        {
            if (gamer == packet.Gamer) return gamer;
        }
        return nullptr;
    }

    void LocalNetworkGamer::SendData(const std::vector<SharpRuntime::bytecs>& data, SendDataOptions options)
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        SendData(data, 0, static_cast<int>(data.size()), options);
    }

    void LocalNetworkGamer::SendData(const std::vector<SharpRuntime::bytecs>& data, int offset, int count, SendDataOptions options)
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        // The range is validated before the copy, so an out-of-bounds iterator range is never built.
        ValidateSend(*this, static_cast<int>(data.size()), offset, count, nullptr);
        ValidateSendDataOptions(options);
        std::vector<SharpRuntime::bytecs> mem(data.begin() + offset, data.begin() + offset + count);
        getSessionProperty()->SendDataInternal(this, std::move(mem), options, nullptr);
    }

    void LocalNetworkGamer::SendData(const std::vector<SharpRuntime::bytecs>& data, SendDataOptions options, NetworkGamer* recipient)
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        SendData(data, 0, static_cast<int>(data.size()), options, recipient);
    }

    void LocalNetworkGamer::SendData(const std::vector<SharpRuntime::bytecs>& data, int offset, int count, SendDataOptions options, NetworkGamer* recipient)
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        RequireRecipient(recipient);
        ValidateSend(*this, static_cast<int>(data.size()), offset, count, recipient);
        ValidateSendDataOptions(options);
        std::vector<SharpRuntime::bytecs> mem(data.begin() + offset, data.begin() + offset + count);
        getSessionProperty()->SendDataInternal(this, std::move(mem), options, recipient);
    }

    void LocalNetworkGamer::SendData(PacketWriter& data, SendDataOptions options)
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        ValidateWriterSend(*this, data, nullptr);
        ValidateSendDataOptions(options);
        getSessionProperty()->SendDataInternal(this, PacketBytes(data), options, nullptr);
        ClearSentPacket(data);
    }

    void LocalNetworkGamer::SendData(PacketWriter& data, SendDataOptions options, NetworkGamer* recipient)
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        RequireRecipient(recipient);
        ValidateWriterSend(*this, data, recipient);
        ValidateSendDataOptions(options);
        getSessionProperty()->SendDataInternal(this, PacketBytes(data), options, recipient);
        ClearSentPacket(data);
    }

    void LocalNetworkGamer::ClearPacketQueue()
    {
        packetQueue_ = {};
    }

    void LocalNetworkGamer::EnqueuePacket(NetworkSession::NetworkEvent evt)
    {
        packetQueue_.push(std::move(evt));
    }
}
