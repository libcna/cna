// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentException.hpp"
#include "System/InvalidOperationException.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "System/IO/MemoryStream.hpp"
#include <algorithm>

namespace Microsoft::Xna::Framework::Net
{
    GetTypeNameCPP(LocalNetworkGamer, "Microsoft.Xna.Framework.Net.LocalNetworkGamer")

    namespace
    {
        std::vector<SharpRuntime::bytecs> TakePacket(PacketWriter& data)
        {
            auto* stream = dynamic_cast<System::IO::MemoryStream*>(data.getBaseStreamProperty());
            std::vector<SharpRuntime::bytecs> packet =
                stream != nullptr ? stream->ToArray() : std::vector<SharpRuntime::bytecs>{};

            // PacketWriter is deliberately reusable: XNA sends only the bytes before its current
            // Position and then rewinds it. MemoryStream::ToArray() reflects Length, which may
            // still include a previous, longer packet after the writer has been rewound.
            packet.resize(static_cast<std::size_t>(data.getPositionProperty()));
            data.setPositionProperty(0);
            return packet;
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

    void LocalNetworkGamer::EnableSendVoice(NetworkGamer* remoteGamer, bool /*enable*/)
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        // Reference EnableSendVoice checks; CNA carries no voice, so there is nothing to switch.
        if (getHasLeftSessionProperty()) throw System::InvalidOperationException("The gamer has left the session.");
        if (remoteGamer == nullptr) throw System::ArgumentNullException("remoteGamer");
        if (remoteGamer->getHasLeftSessionProperty()) throw System::InvalidOperationException("The remote gamer has left the session.");
        if (remoteGamer->getSessionProperty() != getSessionProperty())
            throw System::ArgumentException("The gamer is not in this session.", "remoteGamer");
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
        // Reference: the reader is resized to the next packet (emptied when there is none) and the
        // packet size is returned. (The reference then reads through the reader's buffer, which
        // throws for a reader that has never held data; CNA's reader has no separate capacity.)
        sender = nullptr;
        if (!getIsDataAvailableProperty())
        {
            data.ResizeInternal(0);
            return 0;
        }
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
        // Task 2.9: FNA's own Array.Copy(data, offset, mem, 0, mem.Length) validates
        // offset/count against data.Length and throws on overflow - preserved here instead of
        // constructing a vector from an out-of-bounds iterator range (undefined behavior).
        if (offset < 0 || count < 0 || offset + count > static_cast<int>(data.size()))
        {
            throw System::ArgumentException("offset");
        }
        std::vector<SharpRuntime::bytecs> mem(data.begin() + offset, data.begin() + offset + count);
        for (NetworkGamer* gamer : getSessionProperty()->getAllGamersProperty())
        {
            NetworkSession::NetworkEvent evt;
            evt.Type = NetworkSession::NetworkEventType::PacketSend;
            evt.Gamer = gamer;
            evt.Sender = this;
            evt.Packet = mem;
            evt.Reliable = options;
            getSessionProperty()->SendNetworkEvent(std::move(evt));
        }
    }

    void LocalNetworkGamer::SendData(const std::vector<SharpRuntime::bytecs>& data, SendDataOptions options, NetworkGamer* recipient)
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        SendData(data, 0, static_cast<int>(data.size()), options, recipient);
    }

    void LocalNetworkGamer::SendData(const std::vector<SharpRuntime::bytecs>& data, int offset, int count, SendDataOptions options, NetworkGamer* recipient)
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        // Task 2.9: see the non-recipient overload above for why this check exists.
        if (offset < 0 || count < 0 || offset + count > static_cast<int>(data.size()))
        {
            throw System::ArgumentException("offset");
        }
        std::vector<SharpRuntime::bytecs> mem(data.begin() + offset, data.begin() + offset + count);
        NetworkSession::NetworkEvent evt;
        evt.Type = NetworkSession::NetworkEventType::PacketSend;
        evt.Gamer = recipient;
        evt.Sender = this;
        evt.Packet = std::move(mem);
        evt.Reliable = options;
        getSessionProperty()->SendNetworkEvent(std::move(evt));
    }

    void LocalNetworkGamer::SendData(PacketWriter& data, SendDataOptions options)
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        std::vector<SharpRuntime::bytecs> packet = TakePacket(data);

        for (NetworkGamer* gamer : getSessionProperty()->getAllGamersProperty())
        {
            NetworkSession::NetworkEvent evt;
            evt.Type = NetworkSession::NetworkEventType::PacketSend;
            evt.Gamer = gamer;
            evt.Sender = this;
            evt.Packet = packet;
            evt.Reliable = options;
            getSessionProperty()->SendNetworkEvent(std::move(evt));
        }
    }

    void LocalNetworkGamer::SendData(PacketWriter& data, SendDataOptions options, NetworkGamer* recipient)
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        std::vector<SharpRuntime::bytecs> packet = TakePacket(data);

        NetworkSession::NetworkEvent evt;
        evt.Type = NetworkSession::NetworkEventType::PacketSend;
        evt.Gamer = recipient;
        evt.Sender = this;
        evt.Packet = std::move(packet);
        evt.Reliable = options;
        getSessionProperty()->SendNetworkEvent(std::move(evt));
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
